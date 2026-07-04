#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>

inline int FracAlpha(float a)
{
    int ia = toInt(a);
    saturate(ia, 0, 255);
    return ia;
}

void EngineVK::DrawDecal(Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color,
                           const MipInfo& mip, int specFlags)
{
    float vx = screen.X();
    float vy = screen.Y();
    float z = screen.Z();

    float oow = rhw;

    // perform simple clipping
    float xBeg = vx - sizeX;
    float xEnd = vx + sizeX;
    float yBeg = vy - sizeY;
    float yEnd = vy + sizeY;
    float uBeg = 0;
    float vBeg = 0;
    float uEnd = 1;
    float vEnd = 1;

    if (xBeg < 0)
    {
        // -xBeg is out, side length is 2*sizeX
        uBeg = -xBeg / (2 * sizeX);
        xBeg = 0;
    }
    if (xEnd > _w)
    {
        // xEnd-_w is out, side length is 2*sizeX
        uEnd = 1 - (xEnd - _w) / (2 * sizeX);
        xEnd = _w;
    }
    if (yBeg < 0)
    {
        // -yBeg is out, side length is 2*sizeY
        vBeg = -yBeg / (2 * sizeY);
        yBeg = 0;
    }
    if (yEnd > _h)
    {
        // yEnd-_h is out, side length is 2*sizeY
        vEnd = 1 - (yEnd - _h) / (2 * sizeY);
        yEnd = _h;
    }

    if (xBeg >= xEnd || yBeg >= yEnd)
    {
        return;
    }

    TLVertex v[4];

    // set vertex 0
    v[0].pos[0] = xBeg;
    v[0].pos[1] = yBeg;
    v[0].pos[2] = z;
    v[0].t0.u = uBeg;
    v[0].t0.v = vBeg;
    // set vertex 1
    v[1].pos[0] = xEnd;
    v[1].pos[1] = yBeg;
    v[1].pos[2] = z;
    v[1].t0.u = uEnd;
    v[1].t0.v = vBeg;
    // set vertex 2
    v[2].pos[0] = xEnd;
    v[2].pos[1] = yEnd;
    v[2].pos[2] = z;
    v[2].t0.u = uEnd;
    v[2].t0.v = vEnd;
    // set vertex 3
    v[3].pos[0] = xBeg;
    v[3].pos[1] = yEnd;
    v[3].pos[2] = z;
    v[3].t0.u = uBeg;
    v[3].t0.v = vEnd;

    // add vertices to vertex buffer

    if (specFlags & IsAlphaFog)
    {
        v[0].color = color;
        v[0].specular = PackedColor(0xff000000);
    }
    else
    {
        // use fog with z
        v[0].specular = PackedColor(0xff000000 - (color & 0xff000000));
        v[0].color = PackedColor(color | 0xff000000);
    }

    int i;
    for (i = 0; i < 4; i++)
    {
        v[i].rhw = oow;
        v[i].pos[2] = z;
        v[i].color = v[0].color;
        v[i].specular = v[0].specular;
    }

    SwitchRenderMode(RMTris);
    BeginScreenPass();

    AddVertices(v, 4);

    QueuePrepareTriangle(mip, specFlags);
    static const VertexIndex indices[4] = {0, 1, 2, 3};
    QueueFan(indices, 4);
}

void EngineVK::DrawPolygon(const VertexIndex* ii, int n)
{
    QueueFan(ii, n);
}

void EngineVK::DrawSection(const FaceArray& face, Offset beg, Offset end)
{
    if (false)
    {
        return;
    }
    for (Offset i = beg; i < end; face.Next(i))
    {
        const Poly& f = face[i];
        QueueFan(f.GetVertexList(), f.N());
    }
}

void EngineVK::DrawPoints(const TLVertex* vs, int nVertex)
{
    if (false)
    {
        return;
    }
    // now we are drawing quads directly
    for (int i = 0; i < nVertex; i++)
    {
        const TLVertex& v = vs[i];
        PackedColor color = v.color;
        if (color.A8() < 8)
        {
            continue; // do not draw stars that are not visible
        }

        // calculate alpha in TL, TR, BL, BR corners
        // v is used as TL corner
        // we have to simulate TR, BL, BR corners
        int xI = toIntFloor(v.pos[0]);
        int yI = toIntFloor(v.pos[1]);
        float xFrac = v.pos[0] - xI;
        float yFrac = v.pos[1] - yI;
        float ixFrac = 1 - xFrac;
        float iyFrac = 1 - yFrac;

        // quad clipping may be required
        if (xI < 0 || xI + 2 > _w || yI < 0 || yI + 2 > _h)
        {
            // we need all four corners to be on screen
            // otherwise we need to apply clipping
            // we discard whole point instead
            continue;
        }

        float a = color.A8();

        float aTL = FracAlpha(ixFrac * iyFrac * a);
        float aTR = FracAlpha(xFrac * iyFrac * a);
        float aBR = FracAlpha(xFrac * yFrac * a);
        float aBL = FracAlpha(ixFrac * yFrac * a);
        TLVertex vs[4];
        vs[0] = v; // TL
        vs[0].pos[0] = xI + 0.5f;
        vs[0].pos[1] = yI + 0.5f;
        vs[0].color = PackedColorRGB(color, aTL);
        // Screen-space points (stars / laser dots) must NOT be fogged: the
        // screen vertex shader reads vFogTC from specular.a, so alpha 0 mixes
        // the point fully to the (night-dark) fog colour and hides it.
        // 0xff000000 = full vFogTC (no fog) + zero specular tint, matching the
        // 2D/HUD path (EngineVK_2D Draw2D).
        vs[0].specular = PackedColor(0xff000000);

        vs[1] = vs[0]; // TR
        vs[1].pos[0] = xI + 2.5f;
        vs[1].color = PackedColorRGB(color, aTR);
        //
        vs[2] = vs[1]; // BR
        vs[2].pos[1] = yI + 2.5f;
        vs[2].color = PackedColorRGB(color, aBR);

        vs[3] = vs[2]; // BL
        vs[3].pos[0] = vs[0].pos[0];
        vs[3].color = PackedColorRGB(color, aBL);

        static const VertexIndex indices[6] = {0, 1, 2, 3};

        AddVertices(vs, 4);
        QueueFan(indices, 4);
    }
}

void EngineVK::DrawPoints(int beg, int end)
{
    // now we are drawing quads directly

    if (false)
    {
        return;
    }

    for (int i = beg; i < end; i++)
    {
        if (_mesh->Clip(i) & ClipAll)
        {
            continue;
        }

        const TLVertex& v = _mesh->GetVertex(i);
        PackedColor color = v.color;
        if (color.A8() < 8)
        {
            continue; // do not draw stars that are not visible
        }

        // calculate alpha in TL, TR, BL, BR corners
        // v is used as TL corner
        // we have to simulate TR, BL, BR corners
        int xI = toIntFloor(v.pos[0]);
        int yI = toIntFloor(v.pos[1]);
        float xFrac = v.pos[0] - xI;
        float yFrac = v.pos[1] - yI;
        float ixFrac = 1 - xFrac;
        float iyFrac = 1 - yFrac;

        float a = color.A8();

        TLVertex vs[4];
        vs[0] = v; // TL
        vs[0].pos[0] = xI + 0.5f;
        vs[0].pos[1] = yI + 0.5f;
        vs[0].color = PackedColorRGB(color, FracAlpha(ixFrac * iyFrac * a));
        vs[0].specular = PackedColor(0xff000000); // no fog (vFogTC = specular.a); see DrawPoints overload above

        vs[1] = vs[0]; // TR
        vs[1].pos[0] = xI + 2.5f;
        vs[1].color = PackedColorRGB(color, FracAlpha(xFrac * iyFrac * a));
        //
        vs[2] = vs[1]; // BR
        vs[2].pos[1] = yI + 2.5f;
        vs[2].color = PackedColorRGB(color, FracAlpha(xFrac * yFrac * a));

        vs[3] = vs[2]; // BL
        vs[3].pos[0] = vs[0].pos[0];
        vs[3].color = PackedColorRGB(color, FracAlpha(ixFrac * yFrac * a));

        static const VertexIndex indices[6] = {0, 1, 2, 3};

        AddVertices(vs, 4);
        QueueFan(indices, 4);
    }
}

bool EngineVK::CanGrass() const
{
    return false;
}
#include <PoseidonVK/VertexBufferVK.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

namespace Poseidon
{

extern int g_emitDrawCalls;

void EngineVK::EmitDraw(const render::frame::Draw& d)
{
    g_emitDrawCalls++;
    if (!_vkReady || !_frameOpen) return;

    PipelineKey key;
    key.desc = d.descriptor;
    key.vertexFormat = (d.descriptor.pass == render::PassKind::ScreenSpace3D) ? 0 : 1;
    ApplyDescriptorPSState(d.descriptor,
                           key.vertexFormat == 1 ? PipelineVertexInput::Mesh : PipelineVertexInput::Screen);

    VkCommandBuffer cb = _commandBuffers[_currentFrame];

    // Resolve textures from registry
    TextureVK* tex0 = nullptr;
    TextureVK* tex1 = nullptr;
    if (d.textures[0].id != 0)
    {
        auto it = _textureRegistry.find(d.textures[0].id);
        if (it != _textureRegistry.end()) tex0 = it->second;
    }
    if (d.textures[1].id != 0)
    {
        auto it = _textureRegistry.find(d.textures[1].id);
        if (it != _textureRegistry.end()) tex1 = it->second;
    }

    // Allocate dynamic uniform offsets
    uint32_t offsetVS = 0;
    uint32_t offsetWorld = 0;
    uint32_t offsetPS = 0;
    AllocateUniformSpace(offsetVS, offsetWorld, offsetPS);

    _uniformOffsetVS = offsetVS;
    _uniformOffsetWorld = offsetWorld;
    _uniformOffsetPS = offsetPS;

    // Upload current constant state to UBO
    std::memcpy(static_cast<char*>(_uniformMapped[_currentFrame]) + offsetVS, &_vsConstants, sizeof(VSConstants));
    std::memcpy(static_cast<char*>(_uniformMapped[_currentFrame]) + offsetPS, &_psConstants, sizeof(PSConstants));

    // Upload world matrix
    WorldInstances worldInst = {};
    std::memcpy(&worldInst.worldArr[0], &d.world, sizeof(float) * 16);
    std::memcpy(static_cast<char*>(_uniformMapped[_currentFrame]) + offsetWorld, &worldInst, sizeof(WorldInstances));

    const uint32_t alignment = 256;
    uint32_t sizeVS = (sizeof(VSConstants) + alignment - 1) & ~(alignment - 1);
    uint32_t sizeWorld = (sizeof(WorldInstances) + alignment - 1) & ~(alignment - 1);
    uint32_t sizePS = (sizeof(PSConstants) + alignment - 1) & ~(alignment - 1);
    vmaFlushAllocation(_vmaAllocator, _uniformAllocation[_currentFrame], offsetVS, sizeVS + sizeWorld + sizePS);

    // Bind state, dynamic viewport/scissor, and descriptor sets (globals + material textures)
    BindPipelineStateAndDescriptors(cb, key, tex0, tex1);

    // Resolve and bind mesh buffers
    VertexBufferVK* vbuf = GetVertexBuffer(d.mesh.vao);
    if (vbuf)
    {
        VkBuffer vertexBuffers[] = {vbuf->_vbo};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
        
        if (vbuf->_ibo)
        {
            vkCmdBindIndexBuffer(cb, vbuf->_ibo, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, d.indexCount, 1, d.indexBegin, 0, 0);
        }
        else
        {
            vkCmdDraw(cb, vbuf->_vertexCount, 1, 0, 0);
        }
    }

    ++Poseidon::gPerfDrawCalls;
}

} // namespace Poseidon
