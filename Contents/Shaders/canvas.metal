#include <metal_stdlib>
using namespace metal;

struct CanvasInput {
    float2 xy [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct CanvasOutput {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex CanvasOutput xcSDLCanvasVertex(CanvasInput v [[stage_in]],
                                      constant float2 &size [[buffer(0)]]) {
    float2 clip = v.xy / size * float2(2, -2) + float2(-1, 1);
    return {float4(clip, 0, 1), v.uv, v.color};
}

fragment float4 xcSDLCanvasFragment(CanvasOutput v [[stage_in]],
                                    texture2d<float> tex [[texture(0)]],
                                    sampler filtering [[sampler(0)]]) {
    return v.color * tex.sample(filtering, v.uv);
}
