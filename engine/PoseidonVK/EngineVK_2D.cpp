#include <PoseidonVK/EngineVK.hpp>

namespace Poseidon
{

void EngineVK::Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip)
{
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip, int specFlags)
{
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices, const Rect2DPixel& clip, int specFlags)
{
}

void EngineVK::DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1, const Rect2DAbs& clip)
{
}

void EngineVK::DrawLine(int beg, int end)
{
}

} // namespace Poseidon
