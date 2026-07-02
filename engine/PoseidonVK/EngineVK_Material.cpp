#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <cstring>

namespace Poseidon
{

static uint64_t LightsSignature(const LightList& lights)
{
    uint64_t sig = static_cast<uint64_t>(lights.Size());
    for (int i = 0; i < lights.Size(); i++)
        sig = sig * 1099511628211ull ^ reinterpret_cast<uintptr_t>(static_cast<const Light*>(lights[i]));
    return sig;
}

void EngineVK::SetMaterial(const TLMaterial& mat, const LightList& lights, const render::LegacySpec& spec)
{
    // update vsconstants
    LightSun* sun = GScene->MainLight();
    
    // sun light direction
    Vector3P sunDir = sun->Direction();
    _vsConstants.sunDir[0] = sunDir.X();
    _vsConstants.sunDir[1] = sunDir.Y();
    _vsConstants.sunDir[2] = sunDir.Z();
    _vsConstants.sunDir[3] = 0.0f;
    
    bool disableSun = static_cast<std::uint32_t>(spec.material & render::Material::DisableSun) != 0;
    float night = disableSun ? 1.0f : sun->NightEffect();
    float sunEn = (!disableSun) ? 1.0f : 0.0f;
    
    _vsConstants.sunEn[0] = sunEn;
    _vsConstants.sunEn[1] = 0.0f;
    _vsConstants.sunEn[2] = 0.0f;
    _vsConstants.sunEn[3] = 0.0f;
    
    Color diffuse = (mat.specFlags & 0x1) ? mat.forcedDiffuse : mat.diffuse; // fallback flag for diffuse
    
    _vsConstants.diffuse[0] = diffuse.R();
    _vsConstants.diffuse[1] = diffuse.G();
    _vsConstants.diffuse[2] = diffuse.B();
    _vsConstants.diffuse[3] = diffuse.A();
    
    _vsConstants.ambient[0] = mat.ambient.R();
    _vsConstants.ambient[1] = mat.ambient.G();
    _vsConstants.ambient[2] = mat.ambient.B();
    _vsConstants.ambient[3] = mat.ambient.A();
    
    _vsConstants.emissive[0] = mat.emmisive.R();
    _vsConstants.emissive[1] = mat.emmisive.G();
    _vsConstants.emissive[2] = mat.emmisive.B();
    _vsConstants.emissive[3] = mat.emmisive.A();
    
    _vsConstants.specular[0] = mat.specular.R();
    _vsConstants.specular[1] = mat.specular.G();
    _vsConstants.specular[2] = mat.specular.B();
    _vsConstants.specular[3] = static_cast<float>(mat.specularPower);
    
    _vsConstants.specEn[0] = (mat.specularPower > 0) ? 1.0f : 0.0f;
    
    // process local lights
    int nLights = std::min(lights.Size(), MaxLocalLights);
    _vsConstants.lightCount[0] = static_cast<float>(nLights);
    
    for (int i = 0; i < nLights; i++)
    {
        const Light* l = lights[i];
        LightDescription desc;
        l->GetDescription(desc);
        
        if (desc.type == LTPoint)
        {
            _vsConstants.lightPos[i][0] = desc.pos.X();
            _vsConstants.lightPos[i][1] = desc.pos.Y();
            _vsConstants.lightPos[i][2] = desc.pos.Z();
            _vsConstants.lightPos[i][3] = desc.startAtten;
            
            _vsConstants.lightDiffuse[i][0] = desc.diffuse.R() * night;
            _vsConstants.lightDiffuse[i][1] = desc.diffuse.G() * night;
            _vsConstants.lightDiffuse[i][2] = desc.diffuse.B() * night;
            _vsConstants.lightDiffuse[i][3] = desc.diffuse.A() * night;
            
            _vsConstants.lightAmbient[i][0] = desc.ambient.R() * night;
            _vsConstants.lightAmbient[i][1] = desc.ambient.G() * night;
            _vsConstants.lightAmbient[i][2] = desc.ambient.B() * night;
            _vsConstants.lightAmbient[i][3] = desc.ambient.A() * night;
            
            _vsConstants.localLightDir[i][3] = 0.0f; // not a spotlight
        }
        else if (desc.type == LTSpotLight)
        {
            _vsConstants.lightPos[i][0] = desc.pos.X();
            _vsConstants.lightPos[i][1] = desc.pos.Y();
            _vsConstants.lightPos[i][2] = desc.pos.Z();
            _vsConstants.lightPos[i][3] = desc.startAtten;
            
            _vsConstants.lightDiffuse[i][0] = desc.diffuse.R() * night;
            _vsConstants.lightDiffuse[i][1] = desc.diffuse.G() * night;
            _vsConstants.lightDiffuse[i][2] = desc.diffuse.B() * night;
            _vsConstants.lightDiffuse[i][3] = desc.diffuse.A() * night;
            
            _vsConstants.lightAmbient[i][0] = desc.ambient.R() * night;
            _vsConstants.lightAmbient[i][1] = desc.ambient.G() * night;
            _vsConstants.lightAmbient[i][2] = desc.ambient.B() * night;
            _vsConstants.lightAmbient[i][3] = desc.ambient.A() * night;
            
            _vsConstants.localLightDir[i][0] = desc.dir.X();
            _vsConstants.localLightDir[i][1] = desc.dir.Y();
            _vsConstants.localLightDir[i][2] = desc.dir.Z();
            _vsConstants.localLightDir[i][3] = 1.0f; // is a spotlight
        }
    }
}

void EngineVK::EnableSunLight(bool enable)
{
    // no-op for vulkan phase 3 right now
}

} // namespace poseidon
