#include <metal_stdlib>
using namespace metal;

struct WorldVertex {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct ViewTransforms {
    float4x4 worldToViewClip;
    float4x4 worldToSunClip;
};

struct WorldSample {
    float4 position [[position]];
    float4 color;
    float2 uv;
    float4 sunClip;
    float3 world;
    float viewDepth;
};

struct SceneLight {
    float4 controls;     // night vision, shadow strength, inverse depth width, bias
    float4 colors;       // ambient tint strength, sunshine warmth, exposure, fog strength
    float4 cameraAndFog; // local camera position, inverse fog distance
    float4 horizon;      // fog color, current skylight
};
struct WaterOptics {
    float4 eyeAndSurface;
    float4 sunAndDay;
    float4 fogAndDensity;
    float4 effects; // seconds, night vision, open sky, advanced lighting
    float4 terrain; // current skylight, unused, unused, unused
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

// Match the byte layout in renderer.c. Keep sources separate so the sun
// shadow map cannot dim block light or self-lit geometry.
float3 terrainLighting(float encoded, float skyLight) {
    float code = floor(encoded * 255.0 + 0.5);
    if (code > 63.0) {
        return float3(encoded, 0.0, 0.0);
    }
    float grade = floor(code / 16.0);
    float level = (code - 16.0 * grade) / 15.0;
    float openness = grade == 1.0 ? 1.0 : grade == 2.0 ? 0.63 : grade == 3.0 ? 0.36 : 0.0;
    return float3(mix(0.09, 1.0, level), skyLight * openness, grade == 1.0 ? 1.0 : 0.0);
}

float waterFocus(float2 p, float t) {
    float a = dot(p, float2(2.65, 1.37)) - t * 1.77 + 1.46 * sin(p.y * 1.43 + t * 0.79);
    float b = dot(p, float2(-1.68, 3.12)) + t * 1.06 + 1.59 * sin(p.x * 1.38 - t * 1.13);
    float wave = abs(sin(a) + 0.79 * sin(b));
    return 1.0 - smoothstep(0.033, 0.28, wave);
}

fragment float4 underwaterFragment(WorldSample input [[stage_in]],
                                   texture2d<float> atlas [[texture(0)]],
                                   sampler blockFilter [[sampler(0)]],
                                   constant WaterOptics &water [[buffer(0)]]) {
    MaterialAddress at=materialAddress(float2(input.uv.x,fract(input.uv.y)),input.viewDepth,
        atlas.get_width()==2048 && atlas.get_height()==128);
    float4 block=atlas.sample(blockFilter,at.first,level(0.0));
    if (at.weight>0.0) { block=mix(block,atlas.sample(blockFilter,at.second,level(0.0)),at.weight); }
    float3 sight = input.world - water.eyeAndSurface.xyz;
    float medium = length(sight);
    if (input.world.y > water.eyeAndSurface.w && water.effects.z > 0.0) {
        medium *= saturate((water.eyeAndSurface.w - water.eyeAndSurface.y) / max(0.001, sight.y));
    }
    float depth = max(0.0, water.eyeAndSurface.w - input.world.y);
    float daylight = water.sunAndDay.w * water.effects.z;
    float3 lights = terrainLighting(input.color.a, water.terrain.x);
    // The mesh treats water as a sky occluder. Keep the diffuse fill for
    // sea beds while the camera is in water open to the sky.
    float attenuation = exp(-depth * 0.112);
    float skyFill = max(lights.y * (0.23 + 0.50 * attenuation),
                        daylight * (0.57 + 0.31 * attenuation));
    float lit = mix(max(lights.x, skyFill), 1.0, water.effects.y);
    float bright = mix(mix(0.91, 1.17, daylight), 1.22, water.effects.y);
    float3 base = block.rgb * input.color.rgb * lit * bright;
    float3 ray = sight / max(0.001, length(sight));
    float beam = 0.0;
    if (water.effects.w > 0.0 && daylight > 0.03 && medium < 42.0) {
        float2 tilt = water.sunAndDay.xz * 0.83;
        float2 entry = input.world.xz + tilt * depth;
        float fade = smoothstep(0.0, 0.16, depth) * (1.0 - smoothstep(3.0, 16.0, depth));
        float sunlit = max(lights.y, 0.24 + 0.76 * smoothstep(0.34, 0.83, lights.x));
        float focus = waterFocus(entry, water.effects.x) * daylight * fade * sunlit;
        base += float3(0.39, 0.58, 0.48) * focus * (0.27 + 0.73 * block.rgb);
        float3 mid = water.eyeAndSurface.xyz + ray * min(0.46 * medium, 12.0);
        float midDepth = max(0.0, water.eyeAndSurface.w - mid.y);
        float broad = waterFocus(mid.xz * 0.58 + tilt * midDepth * 0.58, water.effects.x * 0.49);
        // One analytic volume sample; no depth texture walk or full-screen distortion.
        float phase = pow(saturate(dot(ray, water.sunAndDay.xyz) * 0.72 + 0.28), 3.0);
        beam = daylight * (0.18 + 0.82 * phase) * (0.21 + 0.79 * broad) * exp(-midDepth * 0.16);
    }
    float3 transmission = exp(-medium * water.fogAndDensity.w * float3(1.90, 0.92, 0.62));
    float3 scatter = water.fogAndDensity.rgb + float3(0.11, 0.24, 0.22) * beam;
    return float4(saturate(mix(scatter, base, transmission)), block.a);
}

float3 ambientTint(float authoredLight, float tint, float warmth) {
    float3 shadowTint = float3(0.78, 0.87, 1.0);
    float3 sunTint = mix(float3(1.0), float3(1.10, 1.0, 0.78), saturate(warmth));
    return mix(float3(1.0), mix(shadowTint, sunTint, smoothstep(0.12, 0.88, authoredLight)), saturate(tint));
}

// First pass: render opaque terrain into the sampled depth attachment.
vertex float4 shadowCaster(WorldVertex input [[stage_in]],
                           constant float4x4 &worldToSun [[buffer(0)]]) {
    return worldToSun * float4(input.position, 1);
}

vertex WorldSample worldVertex(WorldVertex input [[stage_in]],
                               constant ViewTransforms &transforms [[buffer(0)]]) {
    float4 world = float4(input.position, 1);
    float4 clip=transforms.worldToViewClip*world;
    return {clip, input.color, input.uv, transforms.worldToSunClip*world, input.position, clip.w};
}

float4 worldSurface(WorldSample input, float4 block, SceneLight air, float3 lights, float sky) {
    float vision = saturate(air.controls.x);
    float light = mix(max(lights.x, sky), 1.0, vision);
    float3 tint = ambientTint(sky, air.colors.x * (1.0 - vision) * (1.0 - lights.x), air.colors.y);
    float3 visible = input.color.rgb * block.rgb * tint * light * air.colors.z;
    float range = length(input.world - air.cameraAndFog.xyz) * air.cameraAndFog.w;
    float haze = smoothstep(0.38, 1.0, range) * saturate(air.colors.w);
    return float4(saturate(mix(visible, air.horizon.rgb, haze)), block.a);
}

// Second pass: use this frame's depth buffer directly, without a CPU readback.
fragment float4 worldFragment(WorldSample input [[stage_in]],
                              texture2d<float> atlas [[texture(0)]],
                              depth2d<float> shadow [[texture(1)]],
                              sampler blockFilter [[sampler(0)]],
                              sampler compareDepth [[sampler(1)]],
                              constant SceneLight &air [[buffer(0)]]) {
    float4 controls = air.controls;
    MaterialAddress at=materialAddress(input.uv,input.viewDepth,atlas.get_width()==2048 && atlas.get_height()==128);
    float4 block=atlas.sample(blockFilter,at.first,level(0.0));
    if (at.weight>0.0) { block=mix(block,atlas.sample(blockFilter,at.second,level(0.0)),at.weight); }
    float3 lights = terrainLighting(input.color.a, air.horizon.w);
    float sky = lights.y;
    // The map only shadows direct light; a wall near an opening already
    // receives indirect sky light and must not be darkened a second time.
    float shadowStrength = lights.z * saturate(controls.y);
    if (shadowStrength > 0.0 && input.sunClip.w > 0.0) {
        float3 projected = input.sunClip.xyz / input.sunClip.w;
        float2 st = projected.xy * float2(0.5, -0.5) + float2(0.5);
        float border = controls.z * 2.2;
        if (st.x >= border && st.y >= border && st.x <= 1.0 - border &&
            st.y <= 1.0 - border && projected.z > 0.0 && projected.z < 1.0) {
            float lit = 0.0;
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    float2 offset = float2(x, y) * controls.z * 1.65;
                    lit += shadow.sample_compare(compareDepth, st + offset, projected.z - controls.w);
                }
            }
            float edge = min(min(st.x, st.y), min(1.0 - st.x, 1.0 - st.y));
            float coverage = smoothstep(border, 0.10, edge)
                           * smoothstep(0.0, 0.08, min(projected.z, 1.0 - projected.z));
            sky *= 1.0 - shadowStrength * coverage * (1.0 - lit / 9.0);
        }
    }
    return worldSurface(input, block, air, lights, sky);
}

// No shadow resource is declared in this pipeline. The CCD host validates
// bindings per frame, even when a runtime branch would avoid sampling them.
fragment float4 worldUnshadowedFragment(WorldSample input [[stage_in]],
                                        texture2d<float> atlas [[texture(0)]],
                                        sampler blockFilter [[sampler(0)]],
                                        constant SceneLight &air [[buffer(0)]]) {
    // Keep resource bindings in the entry point for the shader sandbox.
    MaterialAddress at=materialAddress(input.uv,input.viewDepth,atlas.get_width()==2048 && atlas.get_height()==128);
    float4 block=atlas.sample(blockFilter,at.first,level(0.0));
    if (at.weight>0.0) { block=mix(block,atlas.sample(blockFilter,at.second,level(0.0)),at.weight); }
    float3 lights = terrainLighting(input.color.a, air.horizon.w);
    return worldSurface(input, block, air, lights, lights.y);
}

// Reflected terrain is sampled later by water.metal. Clip behind the water
// plane, so submerged cliffs cannot appear above the mirrored horizon.
struct MirrorLight {
    float4 planeAndLight;
    float4 eyeAndScale;
    float4 forward;
    float4 horizon;
};

fragment float4 reflectionFragment(WorldSample input [[stage_in]],
                                   texture2d<float> atlas [[texture(0)]],
                                   sampler blockFilter [[sampler(0)]],
                                   constant MirrorLight &mirror [[buffer(0)]]) {
    if (input.world.y < mirror.planeAndLight.x) {
        discard_fragment();
    }
    MaterialAddress at=materialAddress(input.uv,input.viewDepth,atlas.get_width()==2048 && atlas.get_height()==128);
    // Preserve explicit Metal LOD for the reflection's discarded fragments.
    float4 block=atlas.sample(blockFilter,at.first,level(0.0));
    if (at.weight>0.0) { block=mix(block,atlas.sample(blockFilter,at.second,level(0.0)),at.weight); }
    if (block.a < 0.5) {
        discard_fragment();
    }
    float3 lights = terrainLighting(input.color.a, mirror.horizon.w);
    float3 tint = ambientTint(lights.y, mirror.planeAndLight.y * (1.0 - lights.x), mirror.planeAndLight.z);
    float light = mix(max(lights.x, lights.y), 1.0, mirror.forward.w);
    float3 relative = input.world - mirror.eyeAndScale.xyz;
    float haze = smoothstep(0.40, 1.0, length(relative) * mirror.eyeAndScale.w) * 0.64;
    float3 visible = block.rgb * input.color.rgb * tint * light * mirror.planeAndLight.w;
    float depth = saturate(dot(relative, mirror.forward.xyz) * mirror.eyeAndScale.w);
    return float4(saturate(mix(visible, mirror.horizon.rgb, haze)), depth);
}
