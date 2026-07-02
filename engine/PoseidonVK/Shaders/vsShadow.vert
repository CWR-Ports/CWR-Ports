#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(std140, set = 0, binding = 0) uniform VSConstants {
    mat4 proj;          // c0-c3.
    mat4 view;          // c4-c7.
    mat4 world;         // c8-c11.
    vec4 sunDir;        // c12.
    vec4 ambient;       // c13.
    vec4 diffuse;       // c14.
    vec4 emissive;      // c15.
    vec4 fogParam;      // c16.
    vec4 camPos;        // c17.
    vec4 specular;      // c18.
    vec4 specEn;        // c19.
    vec4 sunEn;         // c20.
    vec4 vpScale;       // c21 - VSScreen only, declared for layout parity.
    vec4 _pad22;
    vec4 _pad23;
    mat4 texMat0;       // c24-c27
    mat4 texMat1;       // c28-c31
    vec4 texCtrl;       // c32
};

// per-instance world matrices (perf effort 08).
// plain glDrawElements has gl_InstanceIndex == 0, so slot 0 carries the classic
// single world matrix and non-instanced draws are unchanged.
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
    vColor        = diffuse;        // unlit: direct from material.diffuse.
    vSpecColor    = vec4(0.0);
    vUV0          = (texCtrl.x > 0.5) ? (texMat0 * vec4(uv, 0, 1)).xy : uv;
    vUV1          = vUV0;
    vFogTC        = 1.0;            // shadows ignore fog (DX8 D3DRS_FOGENABLE=FALSE).
    vWorldRel     = vec3(0.0);      // shadow casters are not shadow-mapped receivers.
}
