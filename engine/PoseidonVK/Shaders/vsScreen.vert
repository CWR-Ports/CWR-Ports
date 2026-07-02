#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
// shared vs ubo; vsScreen reads vpScale at slot 21 (offset 336 bytes).
// the 21-slot prefix keeps the byte offsets aligned with what vsTransform
// reads. vsScreen ignores those fields, but std140 requires the shared binding
// layout to match the full contents.
layout(std140, set = 0, binding = 0) uniform VSConstants {
    mat4 _pad_proj;     // slots 0..3 - VSTransform projection.
    mat4 _pad_view;     // slots 4..7.
    mat4 _pad_world;    // slots 8..11.
    vec4 _pad_sunDir;   // slot 12.
    vec4 _pad_ambient;  // slot 13.
    vec4 _pad_diffuse;  // slot 14.
    vec4 _pad_emissive; // slot 15.
    vec4 _pad_fog;      // slot 16.
    vec4 _pad_camPos;   // slot 17.
    vec4 _pad_spec;     // slot 18.
    vec4 _pad_specEn;   // slot 19.
    vec4 _pad_sunEn;    // slot 20.
    vec4 vpScale;       // slot 21 - {2/width, 2/height, 0, 0}.
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
    vWorldRel = vec3(0.0); // screen draws are never shadow-mapped.
}
