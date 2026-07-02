#version 450
precision highp float;
precision highp int;
precision highp sampler2DArray;
layout(std140, set = 0, binding = 2) uniform PSConstants {
    vec4 fogColor;
    vec4 alphaRef;
    vec4 shadowCtl;   // c2: {enable, bias, darkness, texelSize}.
    vec4 constColor;  // c3: per-object IsColored tint; white is the identity.
    vec4 _pad4;
    vec4 grassCoef1;
    vec4 grassCoef2;
    vec4 _pad7;
    mat4 cascadeVP[4]; // c8-c23: per-cascade light view-projection.
    vec4 cascadeSplits;// c24: per-tier select distance (omni: radius; frustum: far eye-depth).
    vec4 cascadeCtl;   // c25: {count, fadeRange, biasBase, omniCount}.
    vec4 camFwd;       // c26: camera forward (eye-depth = dot(vWorldRel, camFwd)).
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
    // no gl_FragDepth; see psnormal.
    vec4 t0 = texture(tex0, vUV0);
    vec4 t1 = texture(tex1, vUV1);

    if (vFogTC < 0.0) discard;

    vec4 r0;
    r0.rgb = vColor.rgb * t0.rgb;
    r0.a = clamp((grassCoef1.a * 2.0 - 1.0) + t1.a, 0.0, 1.0);
    r0.rgb = clamp(r0.rgb * t1.rgb * 2.0, 0.0, 1.0);
    if (shadowCtl.x > 0.5) {
        // tiered shadow maps use camera-centred sphere tiers first and frustum
        // tiers afterward.
        // the sphere tiers are selected by 3d distance so casters behind the
        // camera can still cast into view; frustum tiers are selected by eye-depth.
        // pick the tightest matching tier, then fall through to the next tier if
        // the projection is out of bounds so a too-tight near tier does not drop
        // the shadow.
        // apply 3x3 pcf, cross-fade to the next tier over a band, fade at the far
        // edge, and dim by fog so distant shadows stay soft.
        // cascadeCtl is {count, fadeRange, biasBase, omniCount}; cascadeSplits is
        // the select distance per tier.
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
                // p0 is the primary tier and p1 is the blend partner; if nothing has
                // covered yet, later p values force-sample the next looser tier.
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
                float strength = (1.0 - lit) * fade * clamp(vFogTC, 0.0, 1.0); // dimmer in fog and at distance.
                r0.rgb *= mix(1.0, shadowCtl.z, strength);
            }
        }
    }
    r0.a = clamp(grassCoef2.a * r0.a * 2.0, 0.0, 1.0);

    if (alphaRef.z > 0.5) {
        // alpha-to-coverage sharpens alpha around the cutout threshold so the
        // MSAA resolve grades sub-pixel cutout features (fence wire, foliage)
        // instead of the hard test keeping or killing the whole pixel.
        float cov = clamp((r0.a - alphaRef.x) / max(fwidth(r0.a), 1e-4) + 0.5, 0.0, 1.0);
        if (cov <= 0.0) discard;
        r0.a = cov;
    } else if (r0.a - alphaRef.x * alphaRef.y < 0.0) discard;

    r0.rgb = mix(fogColor.rgb, r0.rgb, vFogTC);
    fragColor = alphaRef.w > 0.5 ? vec4(1.0, 0.0, 0.0, 1.0) : r0;
}
