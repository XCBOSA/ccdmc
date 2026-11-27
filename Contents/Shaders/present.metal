#include <metal_stdlib>
using namespace metal;

struct PresentOutput {
    float4 position [[position]];
    float2 uv;
};

// Full-screen triangle. The SDL draw supplies vertex IDs 0, 1 and 2.
vertex PresentOutput xcSDLVertex(uint i [[vertex_id]]) {
    float2 p = i == 1 ? float2(3, -1) : (i == 2 ? float2(-1, 3) : float2(-1, -1));
    float2 uv = i == 1 ? float2(2, 1) : (i == 2 ? float2(0, -1) : float2(0, 1));
    return {float4(p, 0, 1), uv};
}

// Bind a nearest, clamp-to-edge SDL sampler.
fragment float4 xcSDLFragment(PresentOutput v [[stage_in]],
                              texture2d<float> frame [[texture(0)]],
                              sampler nearest [[sampler(0)]]) {
    return float4(frame.sample(nearest, v.uv).rgb, 1);
}
