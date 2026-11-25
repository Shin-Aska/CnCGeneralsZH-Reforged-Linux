// Tile bounds conservatively enclose the eligible samples from the current water depth snapshot.
#pragma once

inline constexpr const char *WaterRayTileShader = R"GLSL(#version 460
#define SSR_USE_HEIGHT_FLOOR 1
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require

layout(local_size_x=16,local_size_y=16) in;
layout(set=0,binding=0) uniform sampler2D sceneDepth;
layout(set=0,binding=1,std430) writeonly buffer Slabs { float values[]; } slabs;
layout(push_constant) uniform Parameters {
    mat4 worldFromClip;
    vec4 clipZ, clipW;
    vec4 viewport, camera;
} p;

const int Tile = 32;
const uint HaloExtent = 34u;
shared vec3 subgroupBounds[256];
shared vec4 subgroupBox[256];
const float Huge = 3.402823e38;
const float DepthScale = 1024.0;
const float DepthPad = 0.000004;
const vec3 Empty = vec3(Huge, -Huge, -Huge);

vec2 screenNdc(vec2 pixel)
{
    return ((pixel + 0.5 - p.viewport.xy) / p.viewport.zw) * vec2(2, -2) + vec2(-1, 1);
}

vec3 combine(vec3 a, vec3 b)
{
    return vec3(min(a.x, b.x), max(a.yz, b.yz));
}

vec3 sampleDepth(ivec2 pixel, ivec2 extent, vec4 invZ, vec4 invW)
{
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, extent)))
        return Empty;
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    if (!(depth < 0.999999))
        return Empty;

    vec4 ndc = vec4(screenNdc(vec2(pixel)), depth, 1);
    float w = dot(invW, ndc);
    float z = dot(invZ, ndc) / w;
    float top = (w > 1e-12 && !isnan(z) && !isinf(z)) ?
        z + 0.01 + abs(z)*0.00002 : Huge;
#ifdef SSR_USE_HEIGHT_FLOOR
    // Valid consumers prove that even their lowest accepted hit reaches this floor.
    // Reject submerged contributors before they can widen a partly visible tile's bounds.
    if (top < p.camera.w) return Empty;
#endif
    return vec3(depth, depth, top);
}

float expandedDepth(float depth, vec2 ndc0, vec2 ndc1, vec4 invW)
{
    // With x/y fixed, inverse homogeneous w is affine in device depth. A ray
    // through the perspective centre can lengthen by at most the SSR thickness.
    // Allow twice the centre's distance from CameraPosition for inverse roundoff
    // and for a camera displaced from the actual projective centre.
    if (!(invW.z < -1e-8))
        return 1.0;
    vec3 centre = p.worldFromClip[2].xyz / invW.z;
    float maxWGrowth = length(p.clipW.xyz) * (6.015 + 2.0 * length(centre - p.camera.xyz));
    vec2 byPosition = max(ndc0 * invW.xy, ndc1 * invW.xy);
    float invDepthW = byPosition.x + byPosition.y + invW.z * depth + invW.w;
    float widenedW = -invW.z * (1.0 + invDepthW * maxWGrowth);
    float expansion = maxWGrowth * invDepthW * invDepthW / widenedW;
    if (!(invDepthW > 1e-12 && expansion >= 0.0))
        return 1.0;
    return min(depth + expansion + DepthPad, 1.0);
}

void main()
{
    ivec2 extent = textureSize(sceneDepth, 0);
    ivec2 start = ivec2(gl_WorkGroupID.xy) * Tile;
    vec4 invW = vec4(p.worldFromClip[0].w, p.worldFromClip[1].w,
        p.worldFromClip[2].w, p.worldFromClip[3].w);
    vec4 invZ = vec4(p.worldFromClip[0].z, p.worldFromClip[1].z,
        p.worldFromClip[2].z, p.worldFromClip[3].z);
    // A one-pixel halo covers nearest sampling and bracket refinement at tile edges.
    vec3 bounds = Empty;
    vec2 lo = vec2(Huge), hi = vec2(-Huge);
    for (uint i = gl_LocalInvocationIndex; i < HaloExtent * HaloExtent; i += 256u) {
        ivec2 pixel = start + ivec2(i % HaloExtent, i / HaloExtent) - 1;
        vec3 sampleBounds = sampleDepth(pixel, extent, invZ, invW);
        bounds = combine(bounds, sampleBounds);
        if (sampleBounds.x <= sampleBounds.y) {
            lo = min(lo, vec2(pixel));
            hi = max(hi, vec2(pixel));
        }
    }
    vec3 aggregate = vec3(subgroupMin(bounds.x), subgroupMax(bounds.y), subgroupMax(bounds.z));
    vec4 box = vec4(subgroupMin(lo), subgroupMax(hi));
    if (subgroupElect()) {
        subgroupBounds[gl_SubgroupID] = aggregate;
        subgroupBox[gl_SubgroupID] = box;
    }
    barrier();
    if (gl_LocalInvocationIndex != 0)
        return;

    bounds = Empty;
    lo = vec2(Huge);
    hi = vec2(-Huge);
    for (uint i = 0; i < gl_NumSubgroups; ++i) {
        bounds = combine(bounds, subgroupBounds[i]);
        lo = min(lo, subgroupBox[i].xy);
        hi = max(hi, subgroupBox[i].zw);
    }
    // Retain the same nearest-sample halo as the full-tile bounds.
    const vec2 tileLo = vec2(start) - 0.001;
    const vec2 tileHi = vec2(min(start + Tile, extent)) + 0.001;
    lo = max(tileLo, lo - 1.001);
    hi = min(tileHi, hi + 2.001);
    const uint first = (gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x) * 8u;
#ifdef SSR_USE_HEIGHT_FLOOR
    const bool empty = bounds.x > bounds.y || bounds.z < p.camera.w;
#else
    const bool empty = bounds.x > bounds.y;
#endif
    // Keep each tile active across refits. An empty tile's placeholder sits
    // beyond the valid screen-space reflection depth range.
    if (empty) {
        const vec2 centre = vec2(start) + vec2(float(Tile) * 0.5);
        slabs.values[first + 0] = centre.x - 0.03125;
        slabs.values[first + 1] = centre.y - 0.03125;
        slabs.values[first + 2] = DepthScale + 0.5;
        slabs.values[first + 3] = centre.x + 0.03125;
        slabs.values[first + 4] = centre.y + 0.03125;
        slabs.values[first + 5] = DepthScale + 1.0;
        slabs.values[first + 6] = -Huge;
        slabs.values[first + 7] = 0.0;
        return;
    }
    const vec2 ndcNear = screenNdc(vec2(max(start - 1, ivec2(0))));
    const vec2 ndcFar = screenNdc(vec2(min(start + Tile, extent - 1)));
    const float farDepth = expandedDepth(bounds.y, ndcNear, ndcFar, invW);
    slabs.values[first + 0] = lo.x;
    slabs.values[first + 1] = lo.y;
    slabs.values[first + 2] = max(bounds.x - DepthPad, 0) * DepthScale;
    slabs.values[first + 3] = hi.x;
    slabs.values[first + 4] = hi.y;
    slabs.values[first + 5] = farDepth * DepthScale;
    slabs.values[first + 6] = bounds.z;
    slabs.values[first + 7] = 0.0;
}
)GLSL";
