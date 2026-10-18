// Archived initial material experiment; not compiled by water_preview or the game.
// The maintained tool uses Platform/ModernWaterMaterial.h and the production shader.
// Preview-only material experiment. No gameplay state or random generator is used.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include <cstdio>
#include <string>

namespace ThraxWaterPollution {
inline bool InsertBefore(std::string &source, const char *anchor, const char *addition) {
    const auto at=source.find(anchor);
    if(at==std::string::npos || source.find(anchor,at+1)!=std::string::npos) {
        std::fprintf(stderr,"thrax preview: production shader hook is missing or ambiguous: %s\n",anchor);
        return false;
    }
    source.insert(at,addition);
    return true;
}

inline bool Apply(std::string &source) {
    // These hooks apply to generated text owned by the preview executable only.
    // Fail closed if the production generator changes, rather than silently presenting clean water.
    return InsertBefore(source,"float4 main(Input input) : SV_Target",R"HLSL(
struct ThraxBubbles { float2 slope; float cap; float rim; float ripple; };
ThraxBubbles thraxBubbles(float2 p,float time,float footprint) {
    ThraxBubbles result=(ThraxBubbles)0;
    // Each emitter has a different cycle, radius and position. The maximum footprint
    // stays inside the cell even while drifting: there are no cut rings at cell edges.
    const float spacing=18.0;
    float2 cell=floor(p/spacing);
    float seed=oceanHash(cell+float2(13.1,7.7));
    float period=4.8+seed*4.2;
    float clock=time/period+oceanHash(cell+43.7);
    float cycle=floor(clock);
    float age=frac(clock)*period;
    float emit=step(0.28,oceanHash(cell+float2(cycle*7.1,cycle*3.4)));
    float2 centre=(cell+0.28+0.44*float2(oceanHash(cell+1.5),oceanHash(cell+71.3)))*spacing;
    centre+=float2(sin(time*0.34+seed*23.0),cos(time*0.28+seed*15.0))*0.35;
    float2 delta=p-centre;
    float distance=length(delta);
    float2 axis=delta/max(distance,0.001);
    float radius=(0.38+seed*0.68)*smoothstep(0.0,0.9,age);
    float live=smoothstep(0.0,0.24,age)*(1.0-smoothstep(1.24,1.42,age))*emit;
    float aa=max(footprint*0.65,0.08);
    float resolved=1.0-smoothstep(1.0,3.5,footprint);
    float cap=(1.0-smoothstep(max(0.0,radius-aa),radius+aa,distance))*live*resolved;
    // A small curved cap changes the same normal used for the existing sun and sky.
    result.slope=axis*(distance/max(radius,0.08))*cap*0.55;
    result.cap=cap;
    result.rim=exp(-pow((distance-radius)/max(aa,0.10),2.0))*live*resolved;
    float sincePop=age-1.42;
    float ringRadius=radius+max(0.0,sincePop)*2.3;
    float ringLife=smoothstep(0.0,0.12,sincePop)*(1.0-smoothstep(0.35,1.20,sincePop))*emit;
    float width=max(0.19,aa);
    float ring=exp(-pow((distance-ringRadius)/width,2.0))*ringLife*resolved;
    result.slope+=axis*ring*(distance-ringRadius)/width*0.16;
    result.ripple=ring;
    return result;
}
)HLSL") && InsertBefore(source,"    float3 light = normalize(SunDirection.xyz + float3(0, 0, 0.00001));",R"HLSL(
    // TextureFactor is unused by production modern water. The isolated viewer uses
    // RGB for contamination amount, bubbles enabled and clean-left comparison.
    float thraxAmount=saturate(TextureFactor.r);
    if(TextureFactor.b>0.5 && screenUV.x<0.5) thraxAmount=0.0;
    float thraxPatch=oceanNoise(wavePosition*0.009+float2(time*0.008,-time*0.006));
    float thraxDensity=thraxAmount*lerp(0.78,1.0,thraxPatch);
    float thraxBubbleWeight=thraxAmount*saturate(TextureFactor.g)*smoothstep(0.1,1.0,depth);
    ThraxBubbles thraxGas=(ThraxBubbles)0;
    if(thraxBubbleWeight>0.0) {
        thraxGas=thraxBubbles(wavePosition,time,footprint);
        float3 capNormal=float3(-thraxGas.slope*thraxBubbleWeight,0);
        normal=normalize(normal+capNormal);
        resolvedNormal=normalize(resolvedNormal+capNormal);
        waveLightingNormal=normalize(waveLightingNormal+capNormal);
    }
)HLSL") && InsertBefore(source,"    float3 body=WaterOpticalBody(scene,transmission,scatter,caustic,SceneParameters.x);",R"HLSL(
    // Change the suspended material, preserving FFT motion, reflection and lighting.
    // Olive-green scattering and preferential blue absorption suggest contamination
    // without emissive neon or an opaque green surface paint.
    transmission*=exp(-float3(0.012,0.004,0.038)*min(length(bottom-opticalWorld),80.0)*thraxDensity);
    // The viewer carries its display-space RGB in unused fill-light W components;
    // SunColour.w is its brightness control. Production lighting uses RGB/XYZ only.
    float3 thraxColour=pow(saturate(float3(FoamFillLights[1].w,FoamFillLights[2].w,FoamFillLights[3].w)),2.2);
    float3 thraxScatter=thraxColour*clamp(SunColour.w,0.1,1.0)
        *(0.08+0.92*daylight*sunVisible)*(0.55+1.10*swellLight);
    scatter=lerp(scatter,thraxScatter,thraxDensity);
)HLSL") && InsertBefore(source,"    // Foam replaces transmission and the clear-water material locally.",R"HLSL(
    // The tiny cap rim and the short-lived pop ring borrow incident light. They do
    // not glow in shadow. Their footprint filtering removes distant sparkle noise.
    float thraxFilm=saturate(thraxGas.rim*0.16+thraxGas.cap*0.045+thraxGas.ripple*0.065)*thraxBubbleWeight;
    surface=lerp(surface,float3(0.23,0.30,0.10)*(0.12+0.88*daylight*sunVisible),thraxFilm);
)HLSL");
}
}
