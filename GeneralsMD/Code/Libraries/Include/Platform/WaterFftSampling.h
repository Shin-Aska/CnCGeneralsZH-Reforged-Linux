// Native sampling of the three-cascade Phillips ocean described by codeagent/webgl-ocean.
// The reference uses Y-up; the game uses Z-up. The field stays entirely on the GPU.
#pragma once
#include <string>
#include <cstdio>
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselWaves.h"

namespace WaterFftSampling {
// The reference's close water-level camera uses metre-sized waves. Generals' RTS camera
// requires a larger render scale; scale wavelength and displacement together, not normals.
constexpr float WorldScale=20.0f;
constexpr float TimeScale=0.4f;
// Both stages consume the same immutable draw snapshot. Slot zero remains the generated
// fixed vertex block; the vessel field is water-only slot one on the vertex stage.
inline std::string UniformSource(const char *name,unsigned slot) {
    return std::string("cbuffer ")+name+" : register(b"+std::to_string(slot)+R"HLSL()
{
    float4 TextureFactor;
    float4 FogColour;
    float4 AlphaReference;
    row_major float4x4 WorldFromClip;
    float4 CameraPosition;
    float4 SunDirection;
    float4 SunColour;
    float4 Viewport;
    float4 ReflectionArea;
    float4 WaterParameters;
    float4 TerrainParameters;
    float4 SceneParameters;
    float4 OpticalParameters;
    float4 HullParameters;
    float4 HullData[80];
    float4 FftParameters;
};
)HLSL";
}
inline std::string CommonSource()
{
    char limits[256];
    std::snprintf(limits,sizeof(limits),
        "static const float fftVerticalBound=%.9g;\nstatic const float fftHorizontalBound=%.9g;\nstatic const float fftWorldScale=%.9g;\n",
        double(WaterFftGeometry::MaxVerticalDisplacement),double(WaterFftGeometry::MaxHorizontalDisplacement)/1.4142135623730951,double(WorldScale));
    return std::string(limits)+R"HLSL(
// Smooth limits make culling/floor bounds independent of the random spectrum's extrema.
// Each XY component is bounded by the radial limit / sqrt(2), preserving the hull-clearance
// proof even for diagonal crests. Keep the derivative in the same model as displacement.
float3 fftLimit(float3 d) {
    float3 bound=float3(fftHorizontalBound,fftHorizontalBound,fftVerticalBound);
    return bound*tanh(d/bound);
}
float3 fftLimitDerivative(float3 d) {
    float3 q=tanh(d/float3(fftHorizontalBound,fftHorizontalBound,fftVerticalBound));
    return 1.0-q*q;
}
)HLSL";
}
inline std::string VertexSource()
{
    return std::string(R"HLSL(
Texture2D FftDisplacement0 : register(t0);
SamplerState FftSampler0 : register(s0);
Texture2D FftDisplacement1 : register(t1);
SamplerState FftSampler1 : register(s1);
Texture2D FftDisplacement2 : register(t2);
SamplerState FftSampler2 : register(s2);
)HLSL")+CommonSource()+WaterVesselWaves::Source()+R"HLSL(
float3 fftRawDisplacement(float2 p) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
}
float3 fftVertexDisplacement(float2 p,float level) {
    float3 displacement=fftRawDisplacement(p);
    if(FftParameters.y>0.5) {
        int count=clamp((int)HullParameters.x,0,8);
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0)
                displacement+=vesselReflectedValue(fftRawDisplacement(reflected.samplePosition),reflected);
            displacement.z+=vesselGeneratedWave(p,level,WaterParameters.x,h).x;
        }
    }
    return fftLimit(displacement);
}
float3 fftVertexDisplacement(float2 p) {return fftVertexDisplacement(p,WaterParameters.w);}
)HLSL";
}
// Add a water-only wrapper to the generated fixed vertex program. It retains the engine's
// original projection, shroud coordinate generation and fog after moving the actual vertex.
inline bool WrapVertex(std::string &hlsl)
{
    const std::string entry="Output main(Input input)";
    const size_t at=hlsl.find(entry);
    if(at==std::string::npos || hlsl.find("float3 Normal")==std::string::npos
        || hlsl.find("float2 TexCoord1")==std::string::npos) return false;
    hlsl.replace(at,entry.size(),"Output waterFixedVertex(Input input)");
    hlsl+=UniformSource("WaterVertexParameters",1);
    hlsl+=VertexSource();
    hlsl+=R"HLSL(
Output main(Input input)
{
    float2 parameter=input.Position.xy;
    float weight=saturate(input.Normal.x), freeDamping=saturate(input.Normal.y);
    float offshore=saturate(input.Normal.z);
    float originalHeight=input.Position.z-input.TexCoord1.y;
    if(weight>0.0) {
        float damping=vesselApplyHullDamping(parameter,input.TexCoord1.y,
            float3(freeDamping,0,0),float3(offshore,0,0)).x;
        float3 displacement=fftVertexDisplacement(parameter,input.TexCoord1.y);
        input.Position.xy+=displacement.xy*(weight*damping);
        input.Position.z=input.TexCoord1.y+lerp(originalHeight,displacement.z*damping,weight);
    }
    input.Normal=float3(0,0,1);
    Output output=waterFixedVertex(input);
    // TC3 is the existing shroud; TC2 and specular are unused by this water material.
    output.TexCoord2=parameter;
    output.Specular=float4(weight,freeDamping,originalHeight,offshore);
    return output;
}
)HLSL";
    return true;
}
inline std::string PixelSource()
{
    return CommonSource()+WaterVesselWaves::Source()+R"HLSL(
struct FftSurface {
    float3 displacement;
    float3 derivativeX;
    float3 derivativeY;
    float compression;
    float incidentCompression;
    float vesselHeight;
    float vesselCrest;
};
FftSurface fftRawSurface(float2 p) {
    float4 a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),0);
    float4 ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),0);
    float4 b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),0);
    float4 bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),0);
    float4 c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),0);
    float4 cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),0);
    float3 d=(a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
    // Scaling wavelength and height together leaves dimensionless derivatives unchanged.
    float3 x=float3(ad.z,a.w,ad.x)*float3(-1.2,-1.2,1)
        +float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1)+float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1);
    float3 y=float3(a.w,ad.w,ad.y)*float3(-1.2,-1.2,1)
        +float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1)+float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    FftSurface value=(FftSurface)0;
    value.displacement=d;
    value.derivativeX=x;
    value.derivativeY=y;
    return value;
}
FftSurface fftSurface(float2 p,float level) {
    FftSurface value=fftRawSurface(p);
    float oldHeight=fftLimit(value.displacement).z;
    // Mirroring a field around a hull changes its parameterization; a compressed mirror
    // Jacobian alone is not a breaking wave. Preserve incident whitecaps and add only the
    // restrained bow/wake crest signal below, while using full derivatives for the normals.
    float3 incidentLimit=fftLimitDerivative(value.displacement);
    float3 incidentX=value.derivativeX*incidentLimit,incidentY=value.derivativeY*incidentLimit;
    value.incidentCompression=(1+incidentX.x)*(1+incidentY.y)-incidentX.y*incidentY.x;
    if(FftParameters.y>0.5) {
        int count=clamp((int)HullParameters.x,0,8);
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0) {
                FftSurface incident=fftRawSurface(reflected.samplePosition);
                value.displacement+=vesselReflectedValue(incident.displacement,reflected);
                value.derivativeX+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(1,0),reflected);
                value.derivativeY+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(0,1),reflected);
            }
            float3 generated=vesselGeneratedWave(p,level,WaterParameters.x,h);
            value.displacement.z+=generated.x;
            value.derivativeX.z+=generated.y;
            value.derivativeY.z+=generated.z;
            value.vesselCrest+=max(generated.x,0.0)*0.32;
        }
    }
    float3 derivative=fftLimitDerivative(value.displacement);
    value.displacement=fftLimit(value.displacement);
    value.derivativeX*=derivative;
    value.derivativeY*=derivative;
    value.vesselHeight=value.displacement.z-oldHeight;
    value.vesselCrest=saturate(value.vesselCrest);
    value.compression=(1+value.derivativeX.x)*(1+value.derivativeY.y)
        -value.derivativeX.y*value.derivativeY.x;
    return value;
}
FftSurface fftSurface(float2 p) {return fftSurface(p,WaterParameters.w);}
float2 fftParameterGradient(float value,float2 px,float2 py) {
    float det=px.x*py.y-px.y*py.x;
    float inverse=abs(det)>0.000001?1.0/det:0.0;
    return float2(ddx(value)*py.y-ddy(value)*px.y,ddy(value)*px.x-ddx(value)*py.x)*inverse;
}
)HLSL";
}
} // namespace WaterFftSampling
