#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;    // c1: shared slot; water reads only .w (flatDebug).
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}.
    vec4 constColor;  // c3: per-object IsColored tint; water does not use it.
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

layout(set = 1, binding = 2) uniform sampler2DArray shadowMap; // unit 2: cascade depth-map array, unused unless shadowCtl.x > 0.5.
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
