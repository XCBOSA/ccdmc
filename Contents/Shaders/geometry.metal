#include <metal_stdlib>
using namespace metal;

struct GeometryInput {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

struct GeometryOutput {
    float4 position [[position]];
    float4 color;
};

vertex GeometryOutput xcSDLGeometryVertex(GeometryInput v [[stage_in]],
                                          constant float4x4 &transform [[buffer(0)]]) {
    return {transform * float4(v.position, 1), v.color};
}

fragment float4 xcSDLGeometryFragment(GeometryOutput v [[stage_in]]) {
    return v.color;
}
