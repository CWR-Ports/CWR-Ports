#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;    // {ref, enabled, 0, 0}.
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}.
    vec4 constColor;  // c3: per-object IsColored tint; white is the identity.
    vec4 _pad4;
    vec4 _pad5;
    vec4 _pad6;
    vec4 rgbEyeCoef;
};

layout(set = 1, binding = 0) uniform sampler2D tex0;

layout(location = 0) in vec4 vColor;
layout(location = 2) in vec2 vUV0;

layout(set = 1, binding = 2) uniform sampler2DArray shadowMap; // unit 2: cascade depth-map array, unused unless shadowCtl.x > 0.5.
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    // force late tests via gl_FragDepth even with the phase 3 replace-0xff
    // stencil path.
    // replace is idempotent across overlapping shadow casters, but not across
    // alpha-cutout discard.
    // if early-z lets the stencil write happen before the fragment discard,
    // foliage gaps would stamp the mask and EndShadowPass would darken them.
    // forcing late tests makes discard suppress the stencil write.
    gl_FragDepth = gl_FragCoord.z;

    float a = vColor.a * texture(tex0, vUV0).a;
    if (a - alphaRef.x * alphaRef.y < 0.0) discard;

    fragColor = vec4(0.0, 0.0, 0.0, a);
}
