/* Optional coastal history presentation; no source births or gameplay state. */
#pragma once
#include "Platform/WaterCoastalSurf.h"
#include <cmath>

namespace WaterCoastalPresentation {
struct Moments {float density=0,age=0;};
inline Moments AtTime(float density,float ageMoment,float elapsed){
    if(!std::isfinite(density)||!std::isfinite(ageMoment)||!std::isfinite(elapsed)
        ||density<=0||density>16||ageMoment<0||ageMoment>1024||elapsed<0||elapsed>=14)return {};
    const float age=ageMoment/density+elapsed;
    if(!std::isfinite(age)||age>=14)return {};
    return {float(density*std::exp(-double(elapsed)/5)),age};
}
inline float ModelGate(float actual,float canonical,float correction,float gx,float gy){
    using WaterCoastalSurf::Smooth;
    if(!std::isfinite(actual)||!std::isfinite(canonical)||!std::isfinite(correction)
        ||!std::isfinite(gx)||!std::isfinite(gy)||actual<0||actual>1||canonical<0||canonical>1)return 0;
    return (1-Smooth(.015f,.06f,std::abs(actual-canonical)))
        *(1-Smooth(.02f,.15f,std::abs(correction)))*(1-Smooth(.002f,.02f,std::hypot(gx,gy)));
}
inline const char *Source(){return R"HLSL(
bool waterCoastalViewValid(float4 area,float4 params,float2 position,float level,float time) {
    if(!all(isfinite(area))||!all(isfinite(params))||!all(isfinite(position))||!isfinite(level)||!isfinite(time)
        ||params.x<.5||any(area.zw<=0)||abs(params.y-level)>.01||abs(params.w-time)>.0001
        ||params.z>params.w||params.z<0)return false;
    float2 uv=(position-area.xy)/area.zw;
    return all(uv>=0)&&all(uv<=1);
}
float2 waterCoastalAtTime(float2 moments,float elapsed) {
    if(!all(isfinite(moments))||!isfinite(elapsed)||moments.x<=0||moments.x>16
        ||moments.y<0||moments.y>1024||elapsed<0||elapsed>=14)return 0;
    float age=moments.y/moments.x+elapsed;
    if(!isfinite(age)||age>=14)return 0;
    return float2(moments.x*exp(-elapsed/5.0),age);
}
float waterCoastalModelGate(float actual,float canonical,float correction,float2 correctionSlope) {
    if(!all(isfinite(float4(actual,canonical,correction,0)))||!all(isfinite(correctionSlope))
        ||actual<0||actual>1||canonical<0||canonical>1)return 0;
    // The canonical field is camera independent; the vertex metadata includes the
    // actual patch/camera FFT fade. Zero-R analytic shore remains a valid endpoint.
    // Corrections are representation deltas, not a new breaking-wave source.
    return (1-smoothstep(.015,.06,abs(actual-canonical)))
        *(1-smoothstep(.02,.15,abs(correction)))*(1-smoothstep(.002,.02,length(correctionSlope)));
}
float waterCoastalWetGate(float rawDepth,float3 hit,float3 world,float3 view,float level,
    float worldTerrain,float parameterTerrain,float hitTerrain,float obstacle,float hull) {
    if(!isfinite(rawDepth)||rawDepth<0||rawDepth>=.999999||!all(isfinite(hit))
        ||!all(isfinite(world))||!all(isfinite(view))||!isfinite(level)
        ||!all(isfinite(float4(worldTerrain,parameterTerrain,hitTerrain,obstacle)))||!isfinite(hull))return 0;
    // Real original raster depth only. Both the current chop location and fixed
    // parameter must remain wet; a background sample or an above-water face cannot
    // borrow terrain metadata as evidence of water. Keep original ordinary rims.
    if(hit.z>min(world.z,level)-.08||dot(hit-world,view)>=-.01
        ||worldTerrain>=min(world.z-.08,level-.15)||parameterTerrain>=level-.15)return 0;
    // Terrain agreement is a bounded heuristic, not an object-ID reconstruction.
    // Known moving hulls and interpolated structure damping independently reject
    // their footprint; thin unregistered objects still require native inspection.
    float terrainMatch=1-smoothstep(.5,1.5,abs(hit.z-hitTerrain));
    float solid=smoothstep(.001,.1,min(saturate(obstacle),saturate(hull)));
    return smoothstep(0,.8,world.z-hit.z)*smoothstep(.15,.8,level-worldTerrain)*terrainMatch*solid;
}
float waterCoastalPorousCoverage(float2 position,float footprint,float2 densityAge) {
    if(densityAge.x<=0)return 0;
    float coverage=waterFoamOpticalCoverage(densityAge.x);
    // Existing static world-anchored pore material: aging changes its area, not its
    // phase. No scrolling texture or independent oscillator impersonates a front.
    float aged=smoothstep(.4,3.0,densityAge.y);
    float porous=waterFoamOldCoverage(position,coverage,footprint,1.0);
    return lerp(coverage,porous,aged)*(1-smoothstep(8,14,densityAge.y));
}
)HLSL";}
} // namespace WaterCoastalPresentation
