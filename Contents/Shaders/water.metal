#include <metal_stdlib>
using namespace metal;

struct WaterVertex {
    float3 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 uv [[attribute(2)]];
};

struct WaterTransforms {
    float4x4 viewClip;
    float4 timeAndWeather; // seconds, rain, waves enabled, current skylight
};

struct WaterSurface {
    float4 position [[position]];
    float3 world;
    float3 tint;
    float2 slope;
    float3 surfaceInfo; // undisplaced height, shore weight, continuous water depth
    float light;
    float face; // constant across each triangle; see waterFace()
};

struct FlatWaterSurface {
    float4 position [[position]];
    float4 color;
    float2 uv;
    // Must match TexturedOutput: basic water uses xcSDLResidentFragment.
    float viewDepth;
};

struct WaterLighting {
    float4 cameraAndTime;
    float4 sunAndWeather;  // unit light direction, rain intensity
    float4 shallowColor;   // RGB and selected reflection plane height
    float4 deepColor;
    float4 horizon;
    float4x4 mirrorClip;
    float4 mirrorTexel;   // inverse probe size, night vision, unused
};
struct WaterOptics {
    float4 eyeAndSurface;
    float4 sunAndDay;
    float4 fogAndDensity;
    float4 effects;
    float4 skyCapture; // overhead projection scale, valid capture, rain, unused
};

// The fractional parts still address the original tile in low graphics.
vertex FlatWaterSurface waterFlatVertex(WaterVertex vertexIn [[stage_in]],
                                       constant float4x4 &viewClip [[buffer(0)]]) {
    float4 clip=viewClip*float4(vertexIn.position,1.0);
    return {clip,vertexIn.color,fract(vertexIn.uv),clip.w};
}

// Gerstner-like orbital displacement plus the derivative of the same height.
// Even the shortest mesh wave spans >4 grid vertices. Small ripples only
// affect the surface normal and cannot alias into steps in the silhouette.
struct Swell {
    float height;
    float2 orbit;
    float2 slope;
};

Swell swell(float2 p, float2 direction, float k, float amplitude, float time, float offset) {
    float phase = dot(p, direction) * k - sqrt(9.81 * k) * time * 0.64 + offset;
    float s = sin(phase);
    float c = cos(phase);
    return {amplitude * s, amplitude * c * direction, k * amplitude * c * direction};
}

vertex WaterSurface waterVertex(WaterVertex vertexIn [[stage_in]],
                                constant WaterTransforms &transform [[buffer(0)]]) {
    float3 base = vertexIn.position;
    float2 p = base.xz;
    float t = transform.timeAndWeather.x;
    float2 packed = floor(vertexIn.uv);
    // Matches WATER_DEPTH_BIN. The low graphics shader still reads fract(uv).
    float depthCode = floor(packed.y / 2048.0);
    packed.y -= depthCode * 2048.0;
    float weight = floor(packed.y / 64.0);
    float shore = weight * 0.25;
    uint face = uint(packed.x) / 64u;
    float2 grade = (packed - 64.0 * float2(float(face), weight) - 24.0) / 24.0;
    float2 groupA = float2(0.046, -0.072);
    float2 groupB = float2(-0.081, -0.031);
    float packetA = dot(p, groupA) + 0.47 * sin(dot(p, groupB)) - t * 0.48;
    float packetB = dot(p, groupB) + t * 0.34;
    float envelopeA = 0.70 + 0.30 * sin(packetA);
    float envelopeB = 0.55 + 0.45 * sin(packetB);
    Swell a = swell(p, float2(0.872, 0.489), 0.32, 0.27, t, 0.9);
    Swell b = swell(p, float2(0.663, 0.749), 0.59, 0.125, t, 2.7);
    Swell c = swell(p, float2(-0.362, 0.932), 0.87, 0.11, t, 5.1);
    Swell d = swell(p, float2(-0.931, -0.365), 1.37, 0.045, t, 3.7);
    float weather = saturate(transform.timeAndWeather.y);
    float gain = shore * mix(1.0, 1.57, weather) * transform.timeAndWeather.z;
    float height = gain * ((a.height + b.height) * envelopeA + (c.height + d.height) * envelopeB);
    float2 orbital = 0.78 * gain * ((a.orbit + b.orbit) * envelopeA + (c.orbit + d.orbit) * envelopeB);
    float2 slope = gain * ((a.slope + b.slope) * envelopeA + (c.slope + d.slope) * envelopeB
        + ((a.height + b.height) * 0.30 * cos(packetA)) * (groupA + 0.47 * cos(dot(p, groupB)) * groupB)
        + ((c.height + d.height) * 0.45 * cos(packetB)) * groupB);
    float4 world = float4(base + float3(orbital.x, height, orbital.y), 1.0);
    float code = floor(vertexIn.color.a * 255.0 + 0.5);
    float light = vertexIn.color.a;
    if (code <= 31.0) {
        float exposed = step(16.0, code);
        float level = (code - 16.0 * exposed) / 15.0;
        light = max(mix(0.09, 1.0, level), transform.timeAndWeather.w * exposed);
    }
    return {transform.viewClip * world, world.xyz, vertexIn.color.rgb, grade + slope,
            float3(base.y, shore, depthCode + 1.0), light, float(face)};
}

uint waterFace(float face) {
    // Every vertex of a triangle carries the same face ID. Round away
    // interpolation error without requiring a flat Metal varying.
    return uint(face + 0.5);
}

// Vertical faces use world-space ripples, so the pattern flows downward
// across voxel seams instead of restarting at each face's atlas UV.
float4 waterBoundary(WaterSurface surface, float3 view, float t, float light, float day, float rain) {
    uint face = waterFace(surface.face);
    float3 n = face == 0u ? float3(-1,0,0) : face == 1u ? float3(1,0,0) :
               face == 3u ? float3(0,-1,0) : face == 4u ? float3(0,0,-1) : float3(0,0,1);
    float3 p = surface.world;
    float a = dot(p, float3(3.1, 4.9, 2.7)) + t * 4.3;
    float b = dot(p, float3(-4.6, 6.2, 3.6)) + t * 5.1 + 0.55 * sin(p.x + p.z);
    float3 ripple = float3(cos(a), cos(b), sin(b));
    ripple -= n * dot(n, ripple);
    n = normalize(n + ripple * (0.08 + 0.05 * rain));
    float fresnel = pow(1.0 - saturate(abs(dot(n, view))), 4.0);
    float bands = 0.5 + 0.5 * sin(a + 0.75 * sin(b));
    float3 base = mix(float3(0.05, 0.20, 0.29), float3(0.18, 0.42, 0.48), 0.25 + bands * 0.55);
    base *= surface.tint * light;
    base += float3(0.21, 0.32, 0.37) * day * (0.12 * bands + 0.40 * fresnel);
    return float4(base, mix(0.58, 0.84, fresnel));
}

fragment float4 waterBelow(WaterSurface surface [[stage_in]],
                           texture2d<float> aboveWater [[texture(0)]],
                           sampler sceneSampler [[sampler(0)]],
                           constant WaterOptics &water [[buffer(0)]]) {
    float3 sight = surface.world - water.eyeAndSurface.xyz;
    float range = max(0.001, length(sight));
    float3 ray = sight / range;
    if (waterFace(surface.face) != 2u) {
        float4 side = waterBoundary(surface, -ray, water.effects.x,
            mix(surface.light, 1.0, water.effects.y), water.sunAndDay.w * water.effects.z, water.skyCapture.z);
        side.rgb = mix(water.fogAndDensity.rgb, side.rgb, exp(-range * water.fogAndDensity.w));
        return side;
    }
    float2 p = surface.world.xz;
    float t = water.effects.x;
    float a = dot(p, float2(3.74, 1.87)) - t * 2.31 + sin(p.y * 0.6 + t * 0.36);
    float b = dot(p, float2(-2.57, 3.66)) - t * 3.17 + sin(p.x * 0.4 - t * 0.27);
    float rippleScale = (1.0 - smoothstep(8.0, 38.0, range)) * water.effects.w;
    float2 detail = (float2(0.065, 0.034) * cos(a) + float2(-0.032, 0.057) * cos(b)) * rippleScale;
    float2 slope = surface.slope + detail;
    float3 normal = normalize(float3(-slope.x, 1.0, -slope.y));
    // The same moving surface normal determines the critical angle and
    // bends the transmitted view. Outside this cone there is no sky ray.
    float facing = saturate(dot(ray, normal));
    float cot2 = 1.0 - 1.777 * (1.0 - facing * facing);
    float outCos = sqrt(max(0.0001, cot2));
    float3 bent = normalize(1.333 * ray + (outCos - 1.333 * facing) * normal);
    float window = smoothstep(-0.025, 0.06, cot2);
    float day = water.sunAndDay.w * water.effects.z;
    float sunFacing = saturate(dot(bent, water.sunAndDay.xyz));
    float3 fallback = mix(float3(0.037, 0.103, 0.17), float3(0.42, 0.68, 0.81), day);
    fallback += float3(0.49, 0.36, 0.19) * pow(sunFacing, 28.0) * day;
    float3 sky = fallback;
    if (water.skyCapture.y > 0.5 && window > 0.01 && bent.y > 0.05) {
        float2 plane = bent.xz * (water.skyCapture.x / max(0.05, bent.y));
        float edge = max(abs(plane.x), abs(plane.y));
        if (edge < 1.0) {
            float2 uv = plane * 0.5 + float2(0.5);
            float3 scene = aboveWater.sample(sceneSampler, uv, level(0.0)).rgb;
            sky = mix(fallback, scene, 1.0 - smoothstep(0.82, 1.0, edge));
        }
    }
    float ripple = pow(saturate(0.40 + 0.36 * sin(a) + 0.29 * sin(b)), 3.0) * rippleScale;
    float rim = (1.0 - smoothstep(0.0, 0.36, abs(cot2))) * (0.05 + ripple * 0.52);
    float3 fog = water.fogAndDensity.rgb;
    float3 inside = fog * float3(0.57, 0.85, 0.95) + float3(0.15, 0.29, 0.31) * ripple * day;
    float visibility = window * (0.70 + 0.28 * outCos);
    float3 path = exp(-range * water.fogAndDensity.w * float3(1.75, 0.79, 0.53));
    float3 color = mix(fog, mix(inside, sky, visibility) + float3(0.30, 0.54, 0.62) * rim * day, path);
    return float4(saturate(color), mix(0.59, 0.96, visibility));
}

float2 mirrorUV(float4 point) {
    return point.xy / max(0.001, point.w) * float2(0.5, -0.5) + float2(0.5);
}

// The sampled upper hemisphere is an actual mirrored camera render.
fragment float4 waterFragment(WaterSurface surface [[stage_in]],
                              texture2d<float> reflectedScene [[texture(0)]],
                              sampler reflectionSampler [[sampler(0)]],
                              constant WaterLighting &water [[buffer(0)]]) {
    float2 p = surface.world.xz;
    float time = water.cameraAndTime.w;
    float3 towardEye = water.cameraAndTime.xyz - surface.world;
    float viewRange = length(towardEye);
    float3 view = towardEye / max(0.001, viewRange);
    if (waterFace(surface.face) != 2u) {
        float light = mix(surface.light, 1.0, water.mirrorTexel.z);
        float day = saturate(water.sunAndWeather.y * 3.0) * light;
        return waterBoundary(surface, view, time, light, day, water.sunAndWeather.w);
    }
    float drift = sin(dot(p, float2(0.32, -0.19)) + time * 0.39);
    float rippleA = dot(p, float2(5.74, 2.81)) - time * 4.31 + drift * 1.70;
    float rippleB = dot(p, float2(-3.19, 6.83)) - time * 5.29 - drift * 1.44;
    float fine = (1.0 - smoothstep(8.0, 35.0, viewRange)) * mix(0.20, 1.0, surface.surfaceInfo.y);
    float2 detail = (float2(0.032, 0.014) * cos(rippleA) + float2(-0.011, 0.024) * cos(rippleB)) * fine;
    float2 slope = surface.slope + detail;
    float3 normal = normalize(float3(-slope.x, 1.0, -slope.y));
    float facing = max(0.0, dot(normal, view));
    float fresnel = mix(0.025, 0.97, pow(1.0 - facing, 4.4));
    float3 waterColor = mix(water.shallowColor.rgb, water.deepColor.rgb, (1.0 - facing) * 0.64);
    waterColor *= surface.tint * mix(surface.light, 1.0, water.mirrorTexel.z);
    // The planar probe cannot follow every sharp crease. Keep its reflection
    // lobe wider than the direct specular highlight and fade below the horizon.
    float3 probeNormal = normalize(float3(-slope.x * 0.54, 1.0, -slope.y * 0.54));
    float3 ray = reflect(-view, probeNormal);
    float3 reflected = water.horizon.rgb * mix(0.76, 1.07, saturate(ray.y));
    float planeWeight = 1.0 - smoothstep(0.08, 0.43, abs(surface.surfaceInfo.x - water.shallowColor.w));
    float4 flatClip = water.mirrorClip * float4(surface.world.x, water.shallowColor.w, surface.world.z, 1.0);
    if (water.horizon.w > 0.0 && planeWeight > 0.0 && flatClip.w > 0.0) {
        float2 flatUV = mirrorUV(flatClip);
        float3 probeRay = normalize(float3(ray.x, max(ray.y, 0.008), ray.z));
        float4 reflectedDirection = water.mirrorClip * float4(probeRay, 0.0);
        // Alpha is forward distance, or far for sky. This keeps a nearby
        // shore from moving as far as a mountain when a crest tips the ray.
        float firstDistance = reflectedScene.sample(reflectionSampler, saturate(flatUV)).a * 144.0;
        float4 crest = water.mirrorClip * float4(surface.world, 1.0);
        float stride = max(0.75, firstDistance - crest.w) / max(0.30, reflectedDirection.w);
        float4 bent = crest + reflectedDirection * stride;
        float2 rawOffset = mirrorUV(bent) - flatUV;
        float2 offset = rawOffset / (1.0 + 9.0 * length(rawOffset));
        float2 uv = flatUV + offset;
        float2 border = min(uv, float2(1.0) - uv) - water.mirrorTexel.xy;
        float weight = planeWeight * smoothstep(0.0, 0.037, min(border.x, border.y)) * water.horizon.w;
        weight *= smoothstep(-0.19, -0.02, ray.y);
        reflected = mix(reflected, reflectedScene.sample(reflectionSampler, saturate(uv)).rgb, weight);
    }
    waterColor = mix(waterColor, reflected, 0.055 + fresnel * 0.91);
    float3 sun = water.sunAndWeather.xyz;
    float3 halfVector = sun + view;
    float shine = saturate(dot(halfVector, normal) / max(0.001, length(halfVector)));
    float gloss = (pow(shine, 124.0) * 0.72 + pow(shine, 24.0) * 0.10) * saturate(sun.y * 6.0);
    waterColor += float3(1.0, 0.89, 0.73) * gloss * (1.0 - water.sunAndWeather.w * 0.78);
    // A one-cell shoal keeps the original transparency. Longer paths lose
    // background detail gradually, so the distant sea floor's draw limit
    // cannot project a hard rectangular edge through an otherwise open sea.
    float path = max(0.0, surface.surfaceInfo.z - 1.0) / max(0.28, facing);
    float transmission = (1.0 - mix(0.59, 0.985, fresnel)) * exp(-0.38 * path);
    return float4(saturate(waterColor), 1.0 - transmission);
}
