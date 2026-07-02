/*
 * Vulkan shader compilation and storage subsystem
 * Adapted from PoseidonGLES32 shaders to Vulkan SPIRV layout
 */

#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace Poseidon
{

// glslang process initialization wrapper
struct GlslangInitializer
{
    GlslangInitializer()
    {
        glslang::InitializeProcess();
    }
    ~GlslangInitializer()
    {
        glslang::FinalizeProcess();
    }
};

static GlslangInitializer g_glslangInit;

// shader sources

// full-screen vertex shader for 2d rendering and post-processing
static const char s_vsScreenGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 0) uniform VSConstants {
    mat4 _pad_proj;
    mat4 _pad_view;
    mat4 _pad_world;
    vec4 _pad_sunDir;
    vec4 _pad_ambient;
    vec4 _pad_diffuse;
    vec4 _pad_emissive;
    vec4 _pad_fog;
    vec4 _pad_camPos;
    vec4 _pad_spec;
    vec4 _pad_specEn;
    vec4 _pad_sunEn;
    vec4 vpScale;
};

layout(location = 0) in vec3 aPos;
layout(location = 1) in float aRhw;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aSpecular;
layout(location = 4) in vec2 aUV0;
layout(location = 5) in vec2 aUV1;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec4 vSpecColor;
layout(location = 2) out vec2 vUV0;
layout(location = 3) out vec2 vUV1;
layout(location = 4) out float vFogTC;
layout(location = 5) out vec3 vWorldRel;

void main() {
    float w = 1.0 / aRhw;
    gl_Position.x = (aPos.x * vpScale.x - 1.0) * w;
    gl_Position.y = (1.0 - aPos.y * vpScale.y) * w;
    gl_Position.z = aPos.z * w;
    gl_Position.w = w;
    vColor = aColor.bgra;
    vSpecColor = aSpecular.bgra;
    vUV0 = aUV0;
    vUV1 = aUV1;
    vFogTC = aSpecular.a;
    vWorldRel = vec3(0.0);
}
)";

// 3d scene vertex shader with lighting calculations and instancing
static const char s_vsTransformGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 0) uniform VSConstants {
    mat4 proj;
    mat4 view;
    mat4 world;
    vec4 sunDir;
    vec4 ambient;
    vec4 diffuse;
    vec4 emissive;
    vec4 fogParam;
    vec4 camPos;
    vec4 specular;
    vec4 specEn;
    vec4 sunEn;
    vec4 vpScale;
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;
    mat4 texMat1;
    vec4 texCtrl;
    vec4 lightCount;
    vec4 lightPos[8];
    vec4 lightDiffuse[8];
    vec4 lightAmbient[8];
    vec4 localLightDir[8];
    mat4 lightVP;
};

layout(std140, set = 0, binding = 1) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec4 vSpecColor;
layout(location = 2) out vec2 vUV0;
layout(location = 3) out vec2 vUV1;
layout(location = 4) out float vFogTC;
layout(location = 5) out vec3 vWorldRel;

void main() {
    vec4 worldPos    = worldArr[gl_InstanceIndex] * vec4(pos, 1.0);
    vec3 worldNormal = normalize(mat3(worldArr[gl_InstanceIndex]) * normal);
    vec4 viewPos     = view * worldPos;
    gl_Position      = proj * viewPos;
    vWorldRel        = worldPos.xyz;

    float NdotL = max(0.0, dot(worldNormal, -sunDir.xyz));
    vec4 litColor;
    litColor.rgb = emissive.rgb + ambient.rgb * sunEn.x + diffuse.rgb * NdotL * sunEn.x;
    litColor.a   = emissive.a   + ambient.a   * sunEn.x + diffuse.a   * NdotL * sunEn.x;

    const float MIN_INSIDE2 = 0.95677279;
    const float MAX_INSIDE2 = 0.98063081;
    int nLights = int(lightCount.x);
    for (int i = 0; i < nLights; i++)
    {
        vec3 toLight = lightPos[i].xyz - worldPos.xyz;
        float size2 = dot(toLight, toLight);
        float startAtten2 = lightPos[i].w * lightPos[i].w;
        float endAtten2 = startAtten2 * 100.0;
        if (size2 >= endAtten2)
            continue;

        float cone = 1.0;
        if (localLightDir[i].w > 0.5)
        {
            float inside = -dot(toLight, localLightDir[i].xyz);
            if (inside <= 0.0)
                continue;
            float cos2 = (inside * inside) / size2;
            if (cos2 < MIN_INSIDE2)
                continue;
            cone = clamp((cos2 - MIN_INSIDE2) / (MAX_INSIDE2 - MIN_INSIDE2), 0.0, 1.0);
        }

        float atten = (size2 >= startAtten2) ? (startAtten2 / size2) : 1.0;
        float cosFi = dot(toLight, worldNormal);
        vec3 contrib;
        if (cosFi > 0.0)
        {
            cosFi *= inversesqrt(size2);
            contrib = (lightDiffuse[i].rgb * cosFi + lightAmbient[i].rgb) * (atten * cone);
        }
        else
        {
            contrib = lightAmbient[i].rgb * atten;
        }
        litColor.rgb += contrib;
    }

    vColor = clamp(litColor, 0.0, 1.0);

    vec3 spec = vec3(0.0);
    if (specEn.x > 0.5 && sunEn.x > 0.0) {
        vec3 viewDir = normalize(camPos.xyz - worldPos.xyz);
        vec3 halfVec = normalize(-sunDir.xyz + viewDir);
        float NdotH = max(0.0, dot(worldNormal, halfVec));
        float specPow = max(1.0, specular.w);
        spec = specular.rgb * pow(NdotH, specPow) * sunEn.x;
    }
    vSpecColor = vec4(clamp(spec, 0.0, 1.0), 0.0);

    float dist = length(worldPos.xyz - camPos.xyz);
    float fogFactor = clamp(1.0 - (dist - fogParam.x) * fogParam.y, 0.0, 1.0);
    vFogTC = (fogParam.z > 0.5) ? fogFactor : 1.0;

    vUV0 = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1 = (texCtrl.y > 0.5) ? (texMat1 * vec4(uv, 0, 1)).xy : uv;
}
)";

// shadow map generation vertex shader
static const char s_vsShadowGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 0) uniform VSConstants {
    mat4 proj;
    mat4 view;
    mat4 world;
    vec4 sunDir;
    vec4 ambient;
    vec4 diffuse;
    vec4 emissive;
    vec4 fogParam;
    vec4 camPos;
    vec4 specular;
    vec4 specEn;
    vec4 sunEn;
    vec4 vpScale;
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;
    mat4 texMat1;
    vec4 texCtrl;
};

layout(std140, set = 0, binding = 1) uniform WorldInstances {
    mat4 worldArr[256];
};

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec4 vSpecColor;
layout(location = 2) out vec2 vUV0;
layout(location = 3) out vec2 vUV1;
layout(location = 4) out float vFogTC;
layout(location = 5) out vec3 vWorldRel;

void main() {
    vec4 worldPos = worldArr[gl_InstanceIndex] * vec4(pos, 1.0);
    gl_Position   = proj * view * worldPos;
    vColor        = diffuse;
    vSpecColor    = vec4(0.0);
    vUV0          = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1          = vUV0;
    vFogTC        = 1.0;
    vWorldRel     = vec3(0.0);
}
)";

// standard diffuse fragment shader with cascaded shadow map sampling
static const char s_psNormalGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 2) uniform sampler2DArray shadowMap;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vSpecColor;
layout(location = 2) in vec2 vUV0;
layout(location = 3) in vec2 vUV1;
layout(location = 4) in float vFogTC;
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 r0 = vColor * texture(tex0, vUV0);
    r0 *= constColor;
    r0.rgb += vSpecColor.rgb;

    if (shadowCtl.x > 0.5) {
        int nC = int(cascadeCtl.x);
        int omniN = int(cascadeCtl.w);
        float eyeDepth = dot(vWorldRel, camFwd.xyz);
        float dist3D = length(vWorldRel);
        int ci = nC;
        for (int i = 0; i < 4; ++i) {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC) {
            float ts = shadowCtl.w;
            float prevEdge = (ci > 0) ? cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p) {
                int c = ci + p;
                if (c >= nC) break;
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = cascadeVP[c] * vec4(vWorldRel, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = sc.xy * 0.5 + 0.5;
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
                    float bias = cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0) {
                float lit = litSum / wSum;
                float lastSplit = cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0);
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }

    if (alphaRef.z > 0.5) {
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    float luminance= clamp(dot(r0.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    r0.rgb = mix(vec3(luminance), r0.rgb, nightBlend);

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// detailed multii texture fragment shader (base texture combined with detail texture)
static const char s_psDetailGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 1) uniform sampler2D tex1;
layout(set = 1, binding = 2) uniform sampler2DArray shadowMap;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vSpecColor;
layout(location = 2) in vec2 vUV0;
layout(location = 3) in vec2 vUV1;
layout(location = 4) in float vFogTC;
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec4 r0 = vColor * t0;
    r0 *= constColor;
    r0.rgb *= t1.a * 2.0;
    r0 += vSpecColor;

    if (shadowCtl.x > 0.5) {
        int nC = int(cascadeCtl.x);
        int omniN = int(cascadeCtl.w);
        float eyeDepth = dot(vWorldRel, camFwd.xyz);
        float dist3D = length(vWorldRel);
        int ci = nC;
        for (int i = 0; i < 4; ++i) {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC) {
            float ts = shadowCtl.w;
            float prevEdge = (ci > 0) ? cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p) {
                int c = ci + p;
                if (c >= nC) break;
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = cascadeVP[c] * vec4(vWorldRel, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = sc.xy * 0.5 + 0.5;
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
                    float bias = cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0) {
                float lit = litSum / wSum;
                float lastSplit = cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0);
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }

    if (alphaRef.z > 0.5) {
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    float luminance = clamp(dot(r0.rgb, rgbEyeCoef.rgb), 0.0, 1.0);
    float nightBlend = clamp(luminance + rgbEyeCoef.a, 0.0, 1.0);
    r0.rgb = mix(vec3(luminance), r0.rgb, nightBlend);

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// customized grass fragment shader with alpha testing and wind blend
static const char s_psGrassGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 _pad4;
    vec4 grassCoef1;
    vec4 grassCoef2;
    vec4 _pad7;
    mat4 cascadeVP[4];
    vec4 cascadeSplits;
    vec4 cascadeCtl;
    vec4 camFwd;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 1) uniform sampler2D tex1;
layout(set = 1, binding = 2) uniform sampler2DArray shadowMap;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vSpecColor;
layout(location = 2) in vec2 vUV0;
layout(location = 3) in vec2 vUV1;
layout(location = 4) in float vFogTC;
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);

    if (vFogTC < 0.0) discard;

    vec4 r0;
    r0.rgb = vColor.rgb * t0.rgb;
    r0.a = clamp((grassCoef1.a * 2.0 - 1.0) + t1.a, 0.0, 1.0);
    r0.rgb = clamp(r0.rgb * t1.rgb * 2.0, 0.0, 1.0);
    if (shadowCtl.x > 0.5) {
        int nC = int(cascadeCtl.x);
        int omniN = int(cascadeCtl.w);
        float eyeDepth = dot(vWorldRel, camFwd.xyz);
        float dist3D = length(vWorldRel);
        int ci = nC;
        for (int i = 0; i < 4; ++i) {
            if (i >= nC) break;
            float metric = (i < omniN) ? dist3D : eyeDepth;
            if (metric <= cascadeSplits[i]) { ci = i; break; }
        }
        if (ci < nC) {
            float ts = shadowCtl.w;
            float prevEdge = (ci > 0) ? cascadeSplits[ci - 1] : 0.0;
            float ciMetric = (ci < omniN) ? dist3D : eyeDepth;
            float band = (cascadeSplits[ci] - prevEdge) * 0.15;
            float bw = (ci + 1 < nC) ? clamp((ciMetric - (cascadeSplits[ci] - band)) / max(band, 0.001), 0.0, 1.0) : 0.0;
            float litSum = 0.0;
            float wSum = 0.0;
            for (int p = 0; p < 4; ++p) {
                int c = ci + p;
                if (c >= nC) break;
                float w = (p == 0) ? (1.0 - bw) : ((wSum <= 0.0) ? 1.0 : ((p == 1) ? bw : 0.0));
                if (w <= 0.0) continue;
                vec4 cp = cascadeVP[c] * vec4(vWorldRel, 1.0);
                vec3 sc = cp.xyz / cp.w;
                vec2 suv = sc.xy * 0.5 + 0.5;
                if (suv.x > 0.0 && suv.x < 1.0 && suv.y > 0.0 && suv.y < 1.0 && sc.z > 0.0 && sc.z < 1.0) {
                    float bias = cascadeCtl.z * float(c + 1) * float(c + 1);
                    float lit = 0.0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx)
                            lit += (sc.z - bias > texture(shadowMap, vec3(suv + vec2(float(dx), float(dy)) * ts, float(c))).r) ? 0.0 : 1.0;
                    litSum += w * (lit / 9.0);
                    wSum += w;
                }
            }
            if (wSum > 0.0) {
                float lit = litSum / wSum;
                float lastSplit = cascadeSplits[nC - 1];
                float fade = clamp((lastSplit - eyeDepth) / max(cascadeCtl.y, 0.001), 0.0, 1.0);
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0);
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }
    r0.a = clamp(grassCoef2.a * r0.a * 2.0, 0.0, 1.0);

    if (alphaRef.z > 0.5) {
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// water fragment shader with specularity calculations
static const char s_psWaterGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 lightDir;
    vec4 grassCoef1;
    vec4 grassCoef2;
    vec4 rgbEyeCoef;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;
layout(set = 1, binding = 1) uniform sampler2D tex1;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec4 vSpecColor;
layout(location = 2) in vec2 vUV0;
layout(location = 3) in vec2 vUV1;
layout(location = 4) in float vFogTC;
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);
    vec3 bumpNormal = -(t1.xyz * 2.0 - 1.0);
    float spec = clamp(dot(lightDir.xyz, bumpNormal), 0.0, 1.0);
    vec4 r0 = vColor * t0;
    r0.rgb += spec;
    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
)";

// fragment shader used for shadow depth map rendering with alpha test
static const char s_psShadowGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;
    vec4 constColor;
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 vColor;
layout(location = 2) in vec2 vUV0;

layout(location = 0) out vec4 fragColor;

void main() {
    float a = vColor.a * texture(tex0, vUV0).a;
    if (a - alphaRef.x * alphaRef.y < 0.0) discard;

    fragColor = vec4(0.0, 0.0, 0.0, a);
}
)";

// simple fragment shader to output vertex colors directly
static const char s_psFlatGLSL[] = R"(#version 450
precision highp float;
precision highp int;

layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

// compilation helper using glslang to compile glsl source code to spirv and create a VkShaderModule
static VkShaderModule CompileShaderModule(VkDevice device, EShLanguage stage, const char* source, const char* name)
{
    glslang::TShader shader(stage);
    const char* strings[1] = { source };
    shader.setStrings(strings, 1);
    
    // specify target environment as vulkan 1.0 and target spirv 1.0
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    
    const TBuiltInResource* resources = GetDefaultResources();
    const EShMessages rules = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    
    if (!shader.parse(resources, 450, false, rules))
    {
        LOG_ERROR(Graphics, "Vulkan: Shader compile error [{}]: {}", name, shader.getInfoLog());
        return VK_NULL_HANDLE;
    }
    
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(rules))
    {
        LOG_ERROR(Graphics, "Vulkan: Shader link error [{}]: {}", name, program.getInfoLog());
        return VK_NULL_HANDLE;
    }
    
    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    
    // if device is null we skip creating the vulkan shader module but keep the verification log
    if (device == VK_NULL_HANDLE)
    {
        LOG_INFO(Graphics, "Vulkan: Compiled SPIR-V for {} (device is NULL, skipping VkShaderModule creation) [{} bytes]", name, spirv.size() * sizeof(unsigned int));
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(unsigned int);
    createInfo.pCode = spirv.data();
    
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create VkShaderModule for {}", name);
        return VK_NULL_HANDLE;
    }
    
    LOG_INFO(Graphics, "Vulkan: Compiled and created VkShaderModule for {} ({} bytes SPIR-V)", name, createInfo.codeSize);
    return shaderModule;
}

void EngineVK::InitShaders()
{
    LOG_INFO(Graphics, "Vulkan: Initializing and compiling all shader modules...");
    
    _vsModules[VSScreen] = CompileShaderModule(_device, EShLangVertex, s_vsScreenGLSL, "vsScreen");
    _vsModules[VSTransform] = CompileShaderModule(_device, EShLangVertex, s_vsTransformGLSL, "vsTransform");
    _vsModules[VSShadow] = CompileShaderModule(_device, EShLangVertex, s_vsShadowGLSL, "vsShadow");
    
    _fsModules[PSNormal] = CompileShaderModule(_device, EShLangFragment, s_psNormalGLSL, "psNormal");
    _fsModules[PSDetail] = CompileShaderModule(_device, EShLangFragment, s_psDetailGLSL, "psDetail");
    _fsModules[PSGrass] = CompileShaderModule(_device, EShLangFragment, s_psGrassGLSL, "psGrass");
    _fsModules[PSWater] = CompileShaderModule(_device, EShLangFragment, s_psWaterGLSL, "psWater");
    _fsModules[PSFlat] = CompileShaderModule(_device, EShLangFragment, s_psFlatGLSL, "psFlat");
    _fsModules[PSShadow] = CompileShaderModule(_device, EShLangFragment, s_psShadowGLSL, "psShadow");
}

void EngineVK::DeinitShaders()
{
    LOG_INFO(Graphics, "Vulkan: Destroying all shader modules...");
    
    for (int i = 0; i < NVertexShaders; i++)
    {
        if (_vsModules[i] != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(_device, _vsModules[i], nullptr);
            _vsModules[i] = VK_NULL_HANDLE;
        }
    }
    
    for (int i = 0; i < NPixelShaders; i++)
    {
        if (_fsModules[i] != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(_device, _fsModules[i], nullptr);
            _fsModules[i] = VK_NULL_HANDLE;
        }
    }
}

void EngineVK::InitPipelineLayouts()
{
    LOG_INFO(Graphics, "Vulkan: Initializing Descriptor Set Layouts and Pipeline Layouts...");
    
    // Globals
    VkDescriptorSetLayoutBinding globalBindings[3] = {};
    
    globalBindings[0].binding = 0;
    globalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalBindings[0].descriptorCount = 1;
    globalBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    globalBindings[1].binding = 1;
    globalBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalBindings[1].descriptorCount = 1;
    globalBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    
    globalBindings[2].binding = 2;
    globalBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    globalBindings[2].descriptorCount = 1;
    globalBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo globalLayoutInfo{};
    globalLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    globalLayoutInfo.bindingCount = 3;
    globalLayoutInfo.pBindings = globalBindings;
    
    if (vkCreateDescriptorSetLayout(_device, &globalLayoutInfo, nullptr, &_descriptorSetLayoutGlobals) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create global descriptor set layout!");
    }
    
    // Material
    VkDescriptorSetLayoutBinding materialBindings[3] = {};
    
    materialBindings[0].binding = 0;
    materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBindings[0].descriptorCount = 1;
    materialBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    materialBindings[1].binding = 1;
    materialBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBindings[1].descriptorCount = 1;
    materialBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    materialBindings[2].binding = 2;
    materialBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    materialBindings[2].descriptorCount = 1;
    materialBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
    materialLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    materialLayoutInfo.bindingCount = 3;
    materialLayoutInfo.pBindings = materialBindings;
    
    if (vkCreateDescriptorSetLayout(_device, &materialLayoutInfo, nullptr, &_descriptorSetLayoutMaterial) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create material descriptor set layout!");
    }
    
    // Pipeline Layout
    VkDescriptorSetLayout setLayouts[] = { _descriptorSetLayoutGlobals, _descriptorSetLayoutMaterial };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 2;
    pipelineLayoutInfo.pSetLayouts = setLayouts;
    
    if (vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create pipeline layout!");
    }
}

void EngineVK::DeinitPipelineLayouts()
{
    LOG_INFO(Graphics, "Vulkan: Destroying Pipeline Layouts and Descriptor Set Layouts...");
    
    if (_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
        _pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (_descriptorSetLayoutGlobals != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(_device, _descriptorSetLayoutGlobals, nullptr);
        _descriptorSetLayoutGlobals = VK_NULL_HANDLE;
    }
    
    if (_descriptorSetLayoutMaterial != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(_device, _descriptorSetLayoutMaterial, nullptr);
        _descriptorSetLayoutMaterial = VK_NULL_HANDLE;
    }
}

} // namespace Poseidon
