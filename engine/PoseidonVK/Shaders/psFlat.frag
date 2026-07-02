#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(location = 0) in vec4 vColor;
layout(set = 1, binding = 2) uniform sampler2DArray shadowMap; // unit 2: cascade depth-map array, unused unless shadowCtl.x > 0.5.
layout(location = 5) in vec3 vWorldRel;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vColor;
}
