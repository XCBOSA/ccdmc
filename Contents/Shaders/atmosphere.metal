#include <metal_stdlib>
using namespace metal;

struct SkySample {
    float4 position [[position]];
    float2 screen;
};

struct Atmosphere {
    float4 cameraAndTime; // position in blocks, time in seconds
    float4 viewForward;   // world direction and approximate star pixel footprint
    float4 viewRight;     // scaled by horizontal tan(fov / 2)
    float4 viewUp;        // scaled by vertical tan(fov / 2)
    float4 sunAndWeather; // unit sun direction, rain strength
    float4 cloudLayer;    // lower height, upper height, opacity, distance
    float4 skyAndPhase;   // background for the fast path, lunar phase 0...7
};

float hashDensity(float3 p) {
    p = fract(p * float3(0.1031, 0.11369, 0.13787));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float densityNoise(float3 p) {
    float3 i = floor(p);
    float3 u = fract(p);
    u *= u * (3.0 - 2.0 * u);
    float a = mix(hashDensity(i), hashDensity(i + float3(1, 0, 0)), u.x);
    float b = mix(hashDensity(i + float3(0, 1, 0)), hashDensity(i + float3(1, 1, 0)), u.x);
    float c = mix(hashDensity(i + float3(0, 0, 1)), hashDensity(i + float3(1, 0, 1)), u.x);
    float d = mix(hashDensity(i + float3(0, 1, 1)), hashDensity(i + float3(1, 1, 1)), u.x);
    return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}

float cloudDensity(float3 world, float height, float weather, float seconds) {
    float3 wind = float3(seconds * 1.6, 0, seconds * 0.5);
    float3 p = (world + wind) * float3(0.012, 0.023, 0.012);
    float mass = densityNoise(p);
    float edge = mix(0.51, 0.37, weather);
    float profile = smoothstep(0.0, 0.24, height) * (1.0 - smoothstep(0.63, 1.0, height));
    if (mass < edge - 0.08 || profile < 0.001) {
        return 0.0;
    }
    float details = densityNoise(p * 2.6 + float3(3, 8, 1));
    return saturate((mass * 0.82 + details * 0.18 - edge) * 4.3) * profile;
}

vertex SkySample skyVertex(uint index [[vertex_id]]) {
    float2 p = index == 1 ? float2(3, -1) : (index == 2 ? float2(-1, 3) : float2(-1, -1));
    // CCD's depth state is LESS. Stay just in front of the cleared depth,
    // but behind the last drawable chunks, so the sky only shades empty pixels.
    return {float4(p, 0.99999976, 1), p};
}

float3 skyRay(float2 screen, Atmosphere sky) {
    return normalize(sky.viewForward.xyz + sky.viewRight.xyz * screen.x + sky.viewUp.xyz * screen.y);
}

float3 clearSky(float3 ray, Atmosphere sky) {
    float3 sun = sky.sunAndWeather.xyz;
    float weather = saturate(sky.sunAndWeather.w);
    float day = smoothstep(-0.16, 0.25, sun.y);
    float sunFacing = saturate(dot(ray, sun));
    float zenith = smoothstep(0.0, 0.65, ray.y);
    float sunset = (1.0 - smoothstep(0.08, 0.39, abs(sun.y))) * day;
    float3 horizon = mix(float3(0.035, 0.058, 0.15), float3(0.66, 0.81, 0.94), day);
    float3 overhead = mix(float3(0.008, 0.015, 0.05), float3(0.12, 0.38, 0.73), day);
    horizon = mix(horizon, float3(1.0, 0.40, 0.15), sunset * (0.18 + 0.82 * pow(sunFacing, 4.0)));
    float3 result = mix(horizon, overhead, zenith);
    float glow = pow(sunFacing, 52.0) * 0.23 + smoothstep(0.9991, 0.9998, sunFacing) * 0.85;
    result += float3(1.0, 0.78, 0.51) * glow * day * (1.0 - weather);
    return mix(result, float3(0.28, 0.33, 0.40) * (0.25 + day * 0.75), weather * 0.66);
}

// A world-locked angular field, not random screen pixels. Stagger sources
// within cells, give them rounded cores, and fade fine stars near the horizon.
float3 nightStars(float3 ray, Atmosphere sky) {
    float night = (1.0 - smoothstep(-0.22, -0.04, sky.sunAndWeather.y))
                * smoothstep(0.02, 0.31, ray.y) * (1.0 - saturate(sky.sunAndWeather.w));
    if (night < 0.004) {
        return float3(0.0);
    }
    float chartScale = 152.0 / (1.0 + ray.y);
    float2 grid = ray.xz * chartScale;
    float2 cell = floor(grid);
    float kind = hashDensity(float3(cell, 33.0));
    float belt = 1.0 - smoothstep(0.04, 0.30, abs(dot(ray, float3(0.71, -0.34, 0.616))));
    if (kind < mix(0.965, 0.900, belt)) {
        return float3(0.0);
    }
    float2 center = 0.28 + 0.44 * float2(hashDensity(float3(cell, 49.0)), hashDensity(float3(cell, 91.0)));
    float amplitude = hashDensity(float3(cell, 27.0));
    float warm = hashDensity(float3(cell, 62.0));
    float projectedPixel = sky.viewForward.w * chartScale / (1.0 + ray.y)
                           * max(0.60, dot(ray, sky.viewForward.xyz));
    float aa = clamp(projectedPixel * 0.48, 0.012, 0.083);
    float radius = min(0.18, max(projectedPixel * 0.60, mix(0.044, 0.135, amplitude * amplitude)));
    float d = length(fract(grid) - center);
    float core = 1.0 - smoothstep(max(0.0, radius - aa), radius + aa, d);
    float peak = mix(0.39, 1.12, amplitude * amplitude);
    float3 color = mix(float3(0.74, 0.86, 1.0), float3(1.0, 0.92, 0.78), warm);
    return color * (core * peak * night);
}

float4 moonDisc(float3 ray, Atmosphere sky) {
    float phase = sky.skyAndPhase.w;
    float3 moon = -sky.sunAndWeather.xyz;
    float facing = dot(ray, moon);
    if (phase == 4.0 || facing < 0.9989 || moon.y <= 0.0 || ray.y <= 0.0) {
        return float4(0.0);
    }
    float3 east = normalize(float3(moon.z, 0.0, -moon.x));
    float3 north = cross(moon, east);
    float2 disc = float2(dot(ray, east), dot(ray, north)) * 35.0;
    float2 pixel = floor(disc * 10.0 + 0.5) * 0.10;
    float squared = dot(pixel, pixel);
    if (squared > 1.0) {
        return float4(0.0);
    }
    float angle = phase * 0.7853981634;
    float surface = sqrt(max(0.0, 1.0 - squared));
    float lit = -sin(angle) * pixel.x + cos(angle) * surface;
    float bright = smoothstep(-0.07, 0.08, lit);
    float terminator = mix(0.05, 1.0, bright);
    float mareA = 1.0 - smoothstep(0.06, 0.13, dot(pixel + float2(0.33, -0.26), pixel + float2(0.33, -0.26)));
    float mareB = 1.0 - smoothstep(0.05, 0.18, dot(pixel - float2(0.38, -0.11), pixel - float2(0.38, -0.11)));
    float rock = 0.88 + 0.12 * hashDensity(float3(pixel * 10.0, 41.0)) - 0.26 * mareA - 0.18 * mareB;
    float fade = smoothstep(0.0, 0.16, moon.y) * (1.0 - saturate(sky.sunAndWeather.w) * 0.88);
    return float4(float3(0.92, 0.95, 1.0) * (rock * terminator), fade);
}

float3 celestial(float3 background, float3 ray, Atmosphere sky, float visibility) {
    float4 moon = moonDisc(ray, sky);
    return mix(background + nightStars(ray, sky) * visibility, moon.rgb, moon.a * visibility);
}

// Integrate only clouds at reduced resolution. The alpha channel carries
// cloud transmittance so sharp stars behind the clouds can be resolved later.
fragment float4 skyFragment(SkySample sample [[stage_in]],
                            constant Atmosphere &sky [[buffer(0)]]) {
    float3 ray = skyRay(sample.screen, sky);
    float3 sun = sky.sunAndWeather.xyz;
    float weather = saturate(sky.sunAndWeather.w);
    float day = smoothstep(-0.16, 0.25, sun.y);
    float sunFacing = saturate(dot(ray, sun));
    float sunset = (1.0 - smoothstep(0.08, 0.39, abs(sun.y))) * day;
    float3 result = clearSky(ray, sky);
    float skyVisibility = 1.0;

    float altitude = sky.cameraAndTime.y;
    float low = sky.cloudLayer.x;
    float high = max(low + 1.0, sky.cloudLayer.y);
    if (abs(ray.y) > 0.035 && sky.cloudLayer.z > 0.0) {
        float t0 = (low - altitude) / ray.y;
        float t1 = (high - altitude) / ray.y;
        float start = max(1.0, min(t0, t1));
        float end = min(sky.cloudLayer.w, max(t0, t1));
        if (end > start) {
            float stride = (end - start) / 10.0;
            float3 accum = float3(0.0);
            float transmittance = 1.0;
            float3 bottomLight = mix(float3(0.09, 0.11, 0.19), float3(0.44, 0.52, 0.63), day);
            float3 topLight = mix(float3(0.20, 0.23, 0.36), float3(1.0, 0.98, 0.93), day);
            topLight = mix(topLight, float3(1.0, 0.66, 0.38), sunset * sunFacing * 0.58);
            for (int rayStep = 0; rayStep < 10; ++rayStep) {
                if (transmittance > 0.025) {
                    float3 world = sky.cameraAndTime.xyz + ray * (start + (float(rayStep) + 0.5) * stride);
                    float height = saturate((world.y - low) / (high - low));
                    float density = cloudDensity(world, height, weather, sky.cameraAndTime.w);
                    float absorb = (1.0 - exp(-density * stride * 0.032)) * saturate(sky.cloudLayer.z);
                    float3 scattering = mix(bottomLight, topLight, height) *
                        (0.88 + 0.12 * pow(sunFacing, 8.0)) * (1.0 - weather * 0.35);
                    accum += scattering * (transmittance * absorb);
                    transmittance *= 1.0 - absorb;
                }
            }
            float fog = 1.0 - smoothstep(sky.cloudLayer.w * 0.58, sky.cloudLayer.w, start);
            result = mix(result, accum + result * transmittance, fog);
            skyVisibility = mix(1.0, transmittance, fog);
        }
    }
    return float4(saturate(result), skyVisibility);
}

fragment float4 skyComposite(SkySample sample [[stage_in]],
                             texture2d<float> cloudColor [[texture(0)]],
                             sampler filtering [[sampler(0)]],
                             constant Atmosphere &sky [[buffer(0)]]) {
    float2 uv = sample.screen * float2(0.5, -0.5) + float2(0.5);
    float4 clouds = cloudColor.sample(filtering, uv);
    float3 ray = skyRay(sample.screen, sky);
    return float4(saturate(celestial(clouds.rgb, ray, sky, clouds.a)), 1.0);
}

fragment float4 skyReflection(SkySample sample [[stage_in]],
                              constant Atmosphere &sky [[buffer(0)]]) {
    float3 ray = skyRay(sample.screen, sky);
    float visibility = 1.0;
    float3 background = clearSky(ray, sky);
    // One sample of the same moving cloud field, not a second ray march.
    float mid = mix(sky.cloudLayer.x, sky.cloudLayer.y, 0.48);
    float cloudRange = (mid - sky.cameraAndTime.y) / max(0.012, ray.y);
    if (ray.y > 0.012 && cloudRange > 0.0 && cloudRange < sky.cloudLayer.w) {
        float weather = saturate(sky.sunAndWeather.w);
        float density = cloudDensity(sky.cameraAndTime.xyz + ray * cloudRange, 0.48, weather, sky.cameraAndTime.w);
        float opacity = density * (1.0 - smoothstep(sky.cloudLayer.w * 0.58, sky.cloudLayer.w, cloudRange)) * 0.66;
        float day = smoothstep(-0.16, 0.25, sky.sunAndWeather.y);
        float3 cloud = mix(float3(0.13, 0.15, 0.24), float3(0.82, 0.86, 0.93), day) * (1.0 - weather * 0.27);
        background = mix(background, cloud, opacity);
        visibility -= opacity;
    }
    return float4(saturate(celestial(background, ray, sky, visibility)), 1.0);
}

// Lunar gameplay isn't tied to enabling the optional cloud and shadow passes.
fragment float4 basicMoon(SkySample sample [[stage_in]],
                          constant Atmosphere &sky [[buffer(0)]]) {
    float4 moon = moonDisc(skyRay(sample.screen, sky), sky);
    return float4(mix(sky.skyAndPhase.rgb, moon.rgb, moon.a), 1.0);
}
