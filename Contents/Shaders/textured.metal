#include <metal_stdlib>
using namespace metal;

struct TexturedInput {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct TexturedOutput {
    float4 position [[position]];
    float4 color;
    float2 uv;
    float viewDepth;
};

// Material atlas ABI: virtual 124 x 32 lanes; physical 2048 x 128.
// The lower-left bank holds eight native 64-pixel materials. All other
// banks hold 32-pixel or smaller tiles with no hardware mip levels.
float2 materialUV(float tile,float2 local,float level) {
    float edge=32.0*exp2(-level);
    float row=0.0;
    float2 start=float2(512.0,level==2.0 ? 96.0 : level==3.0 ? 104.0 : 108.0);
    if (level==0.0) {
        row=floor(tile/64.0);
        start=float2(0.0,row*32.0);
        tile-=row*64.0;
    } else if (level==1.0) {
        row=floor(tile/96.0);
        start.y=64.0+row*16.0;
        tile-=row*96.0;
    }
    return (start+float2(tile*edge,0.0)+0.5+local*(edge-1.0))/float2(2048.0,128.0);
}
float materialDetailSlot(float tile) {
    if (tile==3.0) { return 0.0; }
    if (tile==15.0) { return 1.0; }
    if (tile==26.0) { return 2.0; }
    if (tile==42.0) { return 3.0; }
    if (tile==51.0) { return 4.0; }
    if (tile==52.0) { return 5.0; }
    if (tile==110.0) { return 6.0; }
    if (tile==111.0) { return 7.0; }
    return -1.0;
}
struct MaterialAddress { float2 first; float2 second; float weight; };
MaterialAddress materialAddress(float2 uv,float depth,bool packed) {
    if (!packed) { return {uv,uv,0.0}; }
    float tile=clamp(floor(uv.x*128.0),0.0,127.0);
    float2 local=saturate((float2(uv.x*4096.0-tile*32.0,uv.y*32.0)-0.5)/31.0);
    if (depth<=24.0) {
        float2 base=materialUV(tile,local,0.0);
        if (depth<13.0) {
            float slot=materialDetailSlot(tile);
            if (slot>=0.0) {
                float2 detailed=(float2(slot*64.0,64.0)+0.5+local*63.0)/float2(2048.0,128.0);
                return {detailed,base,smoothstep(7.0,13.0,depth)};
            }
        }
        return {base,base,0.0};
    }
    float lod=clamp(log2(max(1.0,depth/24.0)),0.0,4.0);
    float level=floor(lod);
    float blend=saturate((lod-level-0.66)*2.9411764);
    float2 first=materialUV(tile,local,level);
    if (level>=4.0 || blend<=0.0) { return {first,first,0.0}; }
    return {first,materialUV(tile,local,level+1.0),blend};
}
// End material atlas ABI.

vertex TexturedOutput xcSDLTexturedVertex(TexturedInput v [[stage_in]],
                                          constant float4x4 &transform [[buffer(0)]]) {
    float4 clip=transform*float4(v.position,1);
    return {clip,v.color,v.uv,clip.w};
}

// Select nearest/linear filtering through the ordinary SDL sampler binding.
fragment float4 xcSDLTexturedFragment(TexturedOutput v [[stage_in]],
                                      texture2d<float> tex [[texture(0)]],
                                      sampler filtering [[sampler(0)]]) {
    MaterialAddress at=materialAddress(v.uv,v.viewDepth,tex.get_width()==2048 && tex.get_height()==128);
    // The pyramid is in the image, not in Metal mip levels. Keep the
    // conditional second fetch independent of implicit screen derivatives.
    float4 sampleColor=tex.sample(filtering,at.first,level(0.0));
    if (at.weight>0.0) { sampleColor=mix(sampleColor,tex.sample(filtering,at.second,level(0.0)),at.weight); }
    return v.color*sampleColor;
}

// The low graphics path uses the same light code as world_lighting.metal.
// parameters: current skylight, night vision, reserved, reserved.
fragment float4 xcSDLResidentFragment(TexturedOutput v [[stage_in]],
                                      texture2d<float> tex [[texture(0)]],
                                      sampler filtering [[sampler(0)]],
                                      constant float4 &parameters [[buffer(0)]]) {
    float code = floor(v.color.a * 255.0 + 0.5);
    float light = v.color.a;
    if (code <= 63.0) {
        float grade = floor(code / 16.0);
        float openness = grade == 1.0 ? 1.0 : grade == 2.0 ? 0.63 : grade == 3.0 ? 0.36 : 0.0;
        float block = (code - 16.0 * grade) / 15.0;
        light = max(mix(0.09, 1.0, block), parameters.x * openness);
    }
    MaterialAddress at=materialAddress(v.uv,v.viewDepth,tex.get_width()==2048 && tex.get_height()==128);
    float4 sampled=tex.sample(filtering,at.first,level(0.0));
    if (at.weight>0.0) { sampled=mix(sampled,tex.sample(filtering,at.second,level(0.0)),at.weight); }
    return float4(v.color.rgb * sampled.rgb * mix(light, 1.0, parameters.y), sampled.a);
}
