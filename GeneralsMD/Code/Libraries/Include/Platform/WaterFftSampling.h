// Native sampling of the three-cascade Phillips ocean described by codeagent/webgl-ocean.
// The reference uses Y-up; the game uses Z-up. The field stays entirely on the GPU.
#pragma once
#include <cstdint>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Platform/WaterFftGeometry.h"
#include "Platform/WaterVesselWaves.h"
#include "Platform/WaterFftFiltering.h"
#include "Platform/WaterBowLift.h"
#include "Platform/WaterWaveCageShader.h"
#include "Platform/ModernWaterMaterial.h"
#include "Platform/WaterWaveHeading.h"

namespace WaterFftSampling {
inline bool Enabled() {
    const char *value=std::getenv("ZH_WATER_FFT");
    return !value || std::strcmp(value,"0")!=0;
}
// Linux direct output uses one array for all six mipmapped fields; 0 selects the legacy A/B path.
// Shared shader callers retain their independent-map defaults.
inline bool ArrayOutputEnabled() {
#if defined(__linux__)
    const char *value=std::getenv("ZH_WATER_FFT_ARRAY_OUTPUT");
    return !value || std::strcmp(value,"0")!=0;
#else
    return false;
#endif
}
// The reference's close water-level camera uses metre-sized waves. Generals' RTS camera
// requires a larger render scale; scale wavelength and displacement together, not normals.
constexpr float WorldScale=WaterFftSpectrum::WorldScale;
constexpr float TimeScale=0.4f;
// Both stages consume the same immutable draw snapshot. Slot zero remains the generated
// fixed vertex block; the vessel field is water-only slot one on the vertex stage.
inline std::string UniformSource(const char *name,unsigned slot,bool visual_hull=false) {
    std::string source=std::string("cbuffer ")+name+" : register(b"+std::to_string(slot)+R"HLSL()
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
    float4 FoamHistoryArea;
    float4 FoamHistoryParameters;
    float4 ContactHullData[80];
    float4 FoamFillLights[4];
    float4 CoastalArea;
    float4 CoastalParams;
    row_major float4x4 ShadowFromClip;
    float4 ShadowParameters;
    float4 ShadowViewport;
    float4 ShadowSoftness;
    float4 VolumeParameters;
)HLSL";
#define MODERN_WATER_HLSL_UNIFORM(name) "    float4 " #name ";\n"
    source+=MODERN_WATER_MATERIAL_UNIFORMS(MODERN_WATER_HLSL_UNIFORM);
#undef MODERN_WATER_HLSL_UNIFORM
    source+="    row_major float4x4 ClipFromWorld;\n";
    if(visual_hull) source+="    float4 VisualHullMeta[8];\n";
    source+="};\n";
    if(visual_hull) source+=R"HLSL(
cbuffer WaterVisualBlock0 : register(b1) { float4 VisualHullBlock0[256]; };
cbuffer WaterVisualBlock1 : register(b2) { float4 VisualHullBlock1[256]; };
cbuffer WaterVisualBlock2 : register(b3) { float4 VisualHullBlock2[192]; };
)HLSL";
    return source;
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
// Shared physical coordinate map: generated bow pressure changes only Z, so all
// incident/reflected XY can be resolved first without an iterative height solve.
float2 fftHorizontalPosition(float2 p,float3 raw,float scale) {
    return p+fftLimit(raw).xy*scale;
}
struct FftHorizontalRegistration { float2 position,derivativeX,derivativeY; };
FftHorizontalRegistration fftHorizontalRegistration(float2 p,float3 raw,float3 rawX,float3 rawY,float3 scale) {
    FftHorizontalRegistration map;
    map.position=fftHorizontalPosition(p,raw,scale.x);
    float2 limited=fftLimit(raw).xy,derivative=fftLimitDerivative(raw).xy;
    map.derivativeX=float2(1,0)+rawX.xy*derivative*scale.x+limited*scale.y;
    map.derivativeY=float2(0,1)+rawY.xy*derivative*scale.x+limited*scale.z;
    return map;
}
)HLSL";
}
// Opt-in only: the shared/default generators, including the coastal text decoder, retain
// byte-identical unrotated source. SDL supplies linear/clamp samplers in both stages.
inline std::string HeadingSource(bool vertex) {
    char base[128];
    std::snprintf(base,sizeof(base),"static const float2 fftBaseHeading=float2(%.9g,%.9g);\n",
        double(WaterWaveHeading::BaseHeading.x),double(WaterWaveHeading::BaseHeading.y));
    return std::string(vertex?R"HLSL(
Texture2D FftHeadingTexture : register(t4);
SamplerState FftHeadingSampler : register(s4);
)HLSL":R"HLSL(
Texture2D FftHeadingTexture : register(t11);
SamplerState FftHeadingSampler : register(s11);
)HLSL")+base+R"HLSL(
float2 fftRotate(float2 p,float2 cs) {return float2(cs.x*p.x-cs.y*p.y,cs.y*p.x+cs.x*p.y);}
float3 fftRotateVector(float3 v,float2 cs) {return float3(fftRotate(v.xy,cs),v.z);}
struct FftHeading {float2 rotation0,rotation1,weights;};
FftHeading fftHeading(float2 p) {
    float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,(p+TerrainParameters.xx+5.0)/max(TerrainParameters.y,1.0),0);
    float2 heading=packed.rg*2.0-1.0;
    FftHeading value;
    // Invalid/degenerate texels use the per-level prevailing rotation, without sector blending.
    if(packed.a<=0.0 || dot(heading,heading)<0.0025) {
        sincos(FftParameters.w,value.rotation0.y,value.rotation0.x);
        value.rotation1=value.rotation0;value.weights=float2(1,0);return value;
    }
    float phi=atan2(fftBaseHeading.x*heading.y-fftBaseHeading.y*heading.x,dot(fftBaseHeading,heading));
    float sector=phi/0.7853981633974483,k=floor(sector),t=frac(sector);
    float u=saturate((t-0.25)/0.5);
    sincos(k*0.7853981633974483,value.rotation0.y,value.rotation0.x);
    sincos((k+1.0)*0.7853981633974483,value.rotation1.y,value.rotation1.x);
    // Explicit endpoints make the zero-weight branch exact (cos(pi/2) need not be zero).
    value.weights=u<=0.0?float2(1,0):u>=1.0?float2(0,1):float2(cos(u*1.5707963267948966),sin(u*1.5707963267948966));
    return value;
}
)HLSL";
}
inline std::string HeadingVertexSource() {
    return R"HLSL(
float3 fftRawDisplacement(float2 p) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedDisplacement(p);
    FftHeading heading=fftHeading(p);float3 result=0;
    [branch] if(heading.weights.x>0.0)
        result+=heading.weights.x*fftRotateVector(fftUnrotatedDisplacement(fftRotate(p,heading.rotation0*float2(1,-1))),heading.rotation0);
    [branch] if(heading.weights.y>0.0)
        result+=heading.weights.y*fftRotateVector(fftUnrotatedDisplacement(fftRotate(p,heading.rotation1*float2(1,-1))),heading.rotation1);
    return result;
}
)HLSL";
}
inline std::string HeadingPixelSource() {
    return R"HLSL(
void fftAccumulateHeading(inout FftSurface result,FftSurface value,float2 cs,float weight) {
    result.displacement+=weight*fftRotateVector(value.displacement,cs);
    // Chain rule for q=R(-a)p, then R(a) on displacement XY. The smooth CPU field
    // permits ignoring spatial derivatives of the sector weights and rotations.
    result.derivativeX+=weight*fftRotateVector(cs.x*value.derivativeX-cs.y*value.derivativeY,cs);
    result.derivativeY+=weight*fftRotateVector(cs.y*value.derivativeX+cs.x*value.derivativeY,cs);
    result.contactIncident+=weight*float3(value.contactIncident.x,fftRotate(value.contactIncident.yz,cs));
    result.incidentCompression+=weight*(value.incidentCompression-1.0);
}
FftSurface fftRawSurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedSurface(p,lod);
    FftHeading heading=fftHeading(p);FftSurface result=(FftSurface)0;result.incidentCompression=1;
    [branch] if(heading.weights.x>0.0)
        fftAccumulateHeading(result,fftUnrotatedSurface(fftRotate(p,heading.rotation0*float2(1,-1)),lod),heading.rotation0,heading.weights.x);
    [branch] if(heading.weights.y>0.0)
        fftAccumulateHeading(result,fftUnrotatedSurface(fftRotate(p,heading.rotation1*float2(1,-1)),lod),heading.rotation1,heading.weights.y);
    return result;
}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z<=0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftHeading heading=fftHeading(p);
    physical=(FftSurface)0;physical.incidentCompression=1;shading=physical;
    FftSurface a,b;
    [branch] if(heading.weights.x>0.0) {
        fftUnrotatedAndShadingSurface(fftRotate(p,heading.rotation0*float2(1,-1)),lod,a,b);
        fftAccumulateHeading(physical,a,heading.rotation0,heading.weights.x);
        fftAccumulateHeading(shading,b,heading.rotation0,heading.weights.x);
    }
    [branch] if(heading.weights.y>0.0) {
        fftUnrotatedAndShadingSurface(fftRotate(p,heading.rotation1*float2(1,-1)),lod,a,b);
        fftAccumulateHeading(physical,a,heading.rotation1,heading.weights.y);
        fftAccumulateHeading(shading,b,heading.rotation1,heading.weights.y);
    }
}
)HLSL";
}
inline std::string VertexSource(bool bowLift=false,bool bowShoulder=WaterBowShoulder::Enabled(),bool textureArray=false,bool waveHeading=false)
{
    std::string source=std::string(R"HLSL(
Texture2D FftDisplacement0 : register(t0);
SamplerState FftSampler0 : register(s0);
Texture2D FftDisplacement1 : register(t1);
SamplerState FftSampler1 : register(s1);
Texture2D FftDisplacement2 : register(t2);
SamplerState FftSampler2 : register(s2);
)HLSL")+CommonSource()+(bowLift?WaterBowLift::Source():std::string())+WaterVesselWaves::Source(true,bowShoulder)+R"HLSL(
float3 fftRawDisplacement(float2 p) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
}
)HLSL"+(bowLift?R"HLSL(float3 fftVertexBowDisplacement(float2 p,float level,float horizontalScale,out float bowHeight) {
    bowHeight=0;)HLSL":R"HLSL(float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {)HLSL")+R"HLSL(
    float3 displacement=fftRawDisplacement(p);
    if(FftParameters.y>0.5) {
        int count=clamp((int)HullParameters.x,0,8);
        float3 oldBow[8];
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0)
                displacement+=vesselReflectedValue(fftRawDisplacement(reflected.samplePosition),reflected);
            displacement.z+=vesselGeneratedWave(p,level,WaterParameters.x,h,oldBow[h]).x;
        }
        if()HLSL"+(bowShoulder?std::string("true"):std::string("horizontalScale!=0.0"))+R"HLSL() {
            float2 position=fftHorizontalPosition(p,displacement,horizontalScale);
            for(int h=0;h<count;++h)
)HLSL"+(bowLift?R"HLSL(                {float currentBow=vesselGeneratedBow(position,level,h).x;
                 displacement.z+=currentBow-oldBow[h].x;bowHeight+=currentBow;})HLSL":R"HLSL(                displacement.z+=vesselGeneratedBow(position,level,h).x-oldBow[h].x;)HLSL")+R"HLSL(
)HLSL"+(bowLift?R"HLSL(        } else {for(int h=0;h<count;++h)bowHeight+=oldBow[h].x;}
    }
    return fftLimit(displacement);
}
float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {
    float ignored;return fftVertexBowDisplacement(p,level,horizontalScale,ignored);
}
// Compatibility callers without mesh metadata retain their unregistered model.)HLSL":R"HLSL(        }
    }
    return fftLimit(displacement);
}
// Compatibility callers without mesh metadata retain their unregistered model.)HLSL")+R"HLSL(
float3 fftVertexDisplacement(float2 p,float level) {return fftVertexDisplacement(p,level,0.0);}
float3 fftVertexDisplacement(float2 p) {return fftVertexDisplacement(p,WaterParameters.w);}
)HLSL";
    if(waveHeading) {
        const std::string entry="float3 fftRawDisplacement(float2 p)";
        source.replace(source.find(entry),entry.size(),"float3 fftUnrotatedDisplacement(float2 p)");
        const size_t end=source.find("\n}",source.find("float3 fftUnrotatedDisplacement"))+3;
        source.insert(end,HeadingVertexSource());
        source=HeadingSource(true)+source;
    }
    if(textureArray) for(unsigned cascade=0;cascade<3;++cascade) {
        const std::string i=std::to_string(cascade);
        const std::string declaration="Texture2D FftDisplacement"+i;
        source.replace(source.find(declaration),declaration.size(),"Texture2DArray FftDisplacement"+i);
        const std::string coordinate="p/("+std::string(cascade==0?"450.0":cascade==1?"103.0":"13.0")+"*fftWorldScale)";
        const std::string sample="FftDisplacement"+i+".SampleLevel(FftSampler"+i+","+coordinate+",0)";
        source.replace(source.find(sample),sample.size(),"FftDisplacement"+i+".SampleLevel(FftSampler"+i+",float3("+coordinate+","+std::to_string(cascade*2)+"),0)");
    }
    return source;
}
// Add a water-only wrapper to the generated fixed vertex program. It retains the engine's
// original projection, shroud coordinate generation and fog after moving the actual vertex.
inline bool WrapVertex(std::string &hlsl,bool bowLift=WaterBowLift::Enabled(),bool bowShoulder=WaterBowShoulder::Enabled(),bool textureArray=false,bool waveHeading=false)
{
    const std::string entry="Output main(Input input)";
    const size_t at=hlsl.find(entry);
    if(at==std::string::npos || hlsl.find("float3 Normal")==std::string::npos
        || hlsl.find("float2 TexCoord1")==std::string::npos) return false;
    hlsl.replace(at,entry.size(),"Output waterFixedVertex(Input input)");
    hlsl+=UniformSource("WaterVertexParameters",1);
    hlsl+=VertexSource(bowLift,bowShoulder,textureArray,waveHeading);
    hlsl+=WaterWaveCageShader::Source(true);
    hlsl+=std::string(R"HLSL(
Output main(Input input)
{
    float2 parameter=input.Position.xy;
    float weight=saturate(input.Normal.x), freeDamping=saturate(input.Normal.y);
    float offshore=saturate(input.Normal.z);
    // Carry the same vertical phases through the shallows, while retaining the
    // coastal restriction on horizontal chop and its terrain/obstacle clearance.
    float horizontalWeight=weight*offshore;
    float originalHeight=input.Position.z-input.TexCoord1.y;
    if(weight>0.0) {
        float damping=vesselApplyHullDamping(parameter,input.TexCoord1.y,
            float3(freeDamping,0,0),float3(offshore,0,0)).x;
)HLSL")+(bowLift?R"HLSL(        float bowHeight;
        float3 displacement=fftVertexBowDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping,bowHeight);
        float lifted=fftBowLift(float3(bowHeight,0,0),float3(freeDamping,0,0),float3(damping,0,0)).x;)HLSL":R"HLSL(        float3 displacement=fftVertexDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping);)HLSL")+R"HLSL(
        WaveCage cage=waveCage(parameter,input.TexCoord1.y);
        float cageAmount=cage.support;
        float3 cageDisplacement=0;
        if(cageAmount>0.0) {
            float cageObstacle=vesselApplyHullDamping(parameter,input.TexCoord1.y,
                float3(saturate(input.Diffuse.r),0,0),float3(1,0,0)).x;
            if(cageObstacle>0.0)
                cageDisplacement=waveCageFloor(parameter,input.TexCoord1.y,waveCageMap(cage,displacement)*cageObstacle);
        }
        input.Position.xy+=lerp(displacement.xy*(horizontalWeight*damping),cageDisplacement.xy*weight,cageAmount);
        input.Position.z=input.TexCoord1.y+)HLSL"+(bowLift?R"HLSL(lerp(originalHeight,displacement.z*damping+lifted,weight))HLSL":R"HLSL(lerp(originalHeight,displacement.z*damping,weight))HLSL")+R"HLSL(;
        input.Position.z=lerp(input.Position.z,input.TexCoord1.y+lerp(originalHeight,cageDisplacement.z,weight),cageAmount);
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
inline std::string PixelSource(bool bowLift=false,bool bowShoulder=WaterBowShoulder::Enabled(),bool textureArray=false,WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution),bool waveHeading=false)
{
    std::string source=CommonSource()+(bowLift?WaterBowLift::Source():std::string())+WaterVesselWaves::Source(true,bowShoulder)+WaterFftFiltering::Source(configuration)+R"HLSL(
)HLSL"+(bowLift?R"HLSL(struct FftSurface {
    float3 bow;
    float bowLimitedDelta;
    float3 displacement;)HLSL":R"HLSL(struct FftSurface {
    float3 displacement;)HLSL")+R"HLSL(
    float3 derivativeX;
    float3 derivativeY;
    float compression;
    float incidentCompression;
    float3 contactIncident;
    float vesselHeight;
    float vesselCrest;
    // Diagnostic-only outputs. Unused normal rendering removes this accumulator and the
    // second limiter through dead-code elimination; no displacement/normal inputs change.
    float generatedRaw;
    float generatedHeight;
    float3 shadingDisplacement;
    float3 shadingDerivativeX;
    float3 shadingDerivativeY;
    float unresolvedSlopeVariance;
};
struct FftMapSamples { float4 a,ad,b,bd,c,cd; };
FftMapSamples fftMapSamples(float2 p,float3 lod) {
    FftMapSamples samples;
    samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod[0]);
    samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod[0]);
    samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod[1]);
    samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod[1]);
    samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod[2]);
    samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod[2]);
    return samples;
}
FftSurface fftDecodeSamples(FftMapSamples samples) {
    float4 a=samples.a,ad=samples.ad,b=samples.b,bd=samples.bd,c=samples.c,cd=samples.cd;
    // Keep the same decode operations across paired and independent shader contexts.
    // Reassociation/FMA contraction must not make physical geometry depend on footprint.
    precise float3 d=(a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
    // Scaling wavelength and height together leaves dimensionless derivatives unchanged.
    precise float3 x=float3(ad.z,a.w,ad.x)*float3(-1.2,-1.2,1)
        +float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1)+float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1);
    precise float3 y=float3(a.w,ad.w,ad.y)*float3(-1.2,-1.2,1)
        +float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1)+float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    FftSurface value=(FftSurface)0;
    value.displacement=d;
    value.derivativeX=x;
    value.derivativeY=y;
    // Ambient breaking follows the broad and medium incident surface. Fine ripples
    // still displace geometry and contribute their full normals, but their short-scale
    // compression must not turn the entire surface into whitecaps. Limit this selected
    // surface independently: a limiter evaluated on all three cascades would let the
    // fine displacement modulate the breaking signal even with its derivatives removed.
    precise float2 breakingDisplacement=(a.xz*-1.2+b.xz*-1.5)*fftWorldScale;
    precise float2 breakingX=float2(ad.z,a.w)*-1.2+float2(bd.z,b.w)*-1.5;
    precise float2 breakingY=float2(a.w,ad.w)*-1.2+float2(b.w,bd.w)*-1.5;
    float2 breakingLimit=fftLimitDerivative(float3(breakingDisplacement,0)).xy;
    breakingX*=breakingLimit;
    breakingY*=breakingLimit;
    value.incidentCompression=(1+breakingX.x)*(1+breakingY.y)-breakingX.y*breakingY.x;
    // Contact wash responds to the incident swell/wind waves. Reflected or generated
    // vessel fields and fine ripples must not regenerate stationary propulsion foam.
    // Reuse these physical samples; no extra texture fetch or vertex displacement.
    float contactHeight=fftVerticalBound*tanh((a.y+b.y)*fftWorldScale/fftVerticalBound);
    float contactDerivative=1.0-(contactHeight/fftVerticalBound)*(contactHeight/fftVerticalBound);
    value.contactIncident=float3(contactHeight,(ad.xy+bd.xy)*contactDerivative);
    return value;
}
FftSurface fftRawSurface(float2 p,float3 lod) {return fftDecodeSamples(fftMapSamples(p,lod));}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    FftMapSamples samples=fftMapSamples(p,float3(0,0,0));
    physical=fftDecodeSamples(samples);
    shading=physical;
    if(any(lod>0.0)) {
        // Preserve each already resolved cascade's exact mip-zero samples. Only the
        // unresolved pairs need another lookup; decoding and accumulation order stay identical.
        [branch] if(lod.x>0.0) {
            samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);
            samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);
        }
        [branch] if(lod.y>0.0) {
            samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod.y);
            samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod.y);
        }
        [branch] if(lod.z>0.0) {
            samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);
            samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);
        }
        shading=fftDecodeSamples(samples);
    }
}
FftSurface fftSurface(float2 p,float level,float footprint,float3 horizontalScale) {
    float3 shadingLod=fftShadingLod(footprint);
    bool filtered=any(shadingLod>0.0);
    FftSurface value,shading;
    fftRawAndShadingSurface(p,shadingLod,value,shading);
    float reflectionVariance=1.0;
    float oldHeight=fftLimit(value.displacement).z;
    // Mirroring a field around a hull changes its parameterization; a compressed mirror
    // Jacobian alone is not a breaking wave. Preserve incident whitecaps and add only the
    // restrained bow/wake crest signal below, while using full derivatives for the normals.
    // fftDecodeSamples already retained the broad/medium incident compression before
    // any hull reflection or generated vessel packet changes this full surface.
    if(FftParameters.y>0.5) {
        int count=clamp((int)HullParameters.x,0,8);
        float3 oldBow[8];float oldGenerated[8];
        for(int h=0;h<count;++h) {
            VesselReflection reflected=vesselReflection(p,level,h);
            if(reflected.weight>0.0) {
                FftSurface incident,shadingIncident;
                fftRawAndShadingSurface(reflected.samplePosition,shadingLod,incident,shadingIncident);
                if(filtered) {
                    shading.displacement+=vesselReflectedValue(shadingIncident.displacement,reflected);
                    shading.derivativeX+=vesselReflectedDerivative(shadingIncident.displacement,shadingIncident.derivativeX,
                        shadingIncident.derivativeY,float2(1,0),reflected);
                    shading.derivativeY+=vesselReflectedDerivative(shadingIncident.displacement,shadingIncident.derivativeX,
                        shadingIncident.derivativeY,float2(0,1),reflected);
                    reflectionVariance+=reflected.weight*reflected.weight;
                }
                value.displacement+=vesselReflectedValue(incident.displacement,reflected);
                value.derivativeX+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(1,0),reflected);
                value.derivativeY+=vesselReflectedDerivative(incident.displacement,incident.derivativeX,
                    incident.derivativeY,float2(0,1),reflected);
            }
            float3 generated=vesselGeneratedWave(p,level,WaterParameters.x,h,oldBow[h]);
)HLSL"+(bowLift?R"HLSL(            oldGenerated[h]=generated.x;
            if()HLSL"+(bowShoulder?std::string("false"):std::string("!any(horizontalScale!=0.0)"))+R"HLSL()value.bow+=oldBow[h];)HLSL":R"HLSL(            oldGenerated[h]=generated.x;)HLSL")+R"HLSL(
            if(filtered) {
                shading.displacement.z+=generated.x;
                shading.derivativeX.z+=generated.y;
                shading.derivativeY.z+=generated.z;
            }
            value.displacement.z+=generated.x;
            value.derivativeX.z+=generated.y;
            value.derivativeY.z+=generated.z;
            value.vesselCrest+=max(generated.x,0.0)*0.32;
            value.generatedRaw+=generated.x;
        }
        if()HLSL"+(bowShoulder?std::string("true"):std::string("any(horizontalScale!=0.0)"))+R"HLSL() {
            // The physical mip-zero map is shared even when reflected lighting is filtered.
            // Retaining the original loop above preserves stern/history accumulation and
            // permits an exact zero-chop difference, without another FFT texture sample.
            FftHorizontalRegistration map=fftHorizontalRegistration(p,value.displacement,
                value.derivativeX,value.derivativeY,horizontalScale);
            for(int h=0;h<count;++h) {
                )HLSL"+(bowLift?R"HLSL(float3 bow=vesselGeneratedBow(map.position,level,h);
                bow.yz=float2(dot(bow.yz,map.derivativeX),dot(bow.yz,map.derivativeY));
                value.bow+=bow;
                float3 correction=bow-oldBow[h];)HLSL":R"HLSL(float3 correction=vesselRegisteredBowDifference(map.position,map.derivativeX,map.derivativeY,level,h,oldBow[h]);)HLSL")+R"HLSL(
                if(filtered) {
                    shading.displacement.z+=correction.x;
                    shading.derivativeX.z+=correction.y;
                    shading.derivativeY.z+=correction.z;
                }
                value.displacement.z+=correction.x;
                value.derivativeX.z+=correction.y;
                value.derivativeY.z+=correction.z;
                value.vesselCrest+=(max(oldGenerated[h]+correction.x,0.0)-max(oldGenerated[h],0.0))*0.32;
                value.generatedRaw+=correction.x;
            }
        }
    }
    float3 derivative=fftLimitDerivative(value.displacement);
    float withoutGenerated=fftVerticalBound*tanh((value.displacement.z-value.generatedRaw)/fftVerticalBound);
)HLSL"+(bowLift?R"HLSL(    // Same physical sample and phase, removing only the registered bow contribution.
    // This diagnostic excludes the extra floor lift, which is applied with mesh damping below.
    float withoutBow=fftVerticalBound*tanh((value.displacement.z-value.bow.x)/fftVerticalBound);
)HLSL":std::string())+R"HLSL(
    value.displacement=fftLimit(value.displacement);
)HLSL"+(bowLift?R"HLSL(    value.bowLimitedDelta=value.displacement.z-withoutBow;
)HLSL":std::string())+R"HLSL(
    value.generatedHeight=value.displacement.z-withoutGenerated;
    value.derivativeX*=derivative;
    value.derivativeY*=derivative;
    if(filtered) {
        float3 shadingLimit=fftLimitDerivative(shading.displacement);
        value.shadingDisplacement=fftLimit(shading.displacement);
        value.shadingDerivativeX=shading.derivativeX*shadingLimit;
        value.shadingDerivativeY=shading.derivativeY*shadingLimit;
        // Expected missing slope power, with local limiter attenuation. Reflected copies use
        // an independent-energy approximation; this does not claim a full covariance solver.
        value.unresolvedSlopeVariance=fftShadingVariance(shadingLod)*reflectionVariance*shadingLimit.z*shadingLimit.z;
    } else {
        value.shadingDisplacement=value.displacement;
        value.shadingDerivativeX=value.derivativeX;
        value.shadingDerivativeY=value.derivativeY;
    }
    value.vesselHeight=value.displacement.z-oldHeight;
    value.vesselCrest=saturate(value.vesselCrest);
    value.compression=(1+value.derivativeX.x)*(1+value.derivativeY.y)
        -value.derivativeX.y*value.derivativeY.x;
    return value;
}
// Compatibility callers without mesh metadata retain their unregistered model.
FftSurface fftSurface(float2 p,float level,float footprint) {return fftSurface(p,level,footprint,float3(0,0,0));}
FftSurface fftSurface(float2 p,float level) {return fftSurface(p,level,0.0);}
FftSurface fftSurface(float2 p) {return fftSurface(p,WaterParameters.w);}
float3 fftPressureDebug(FftSurface surface,float weight,float damping,float generatedFoam) {
    return float3(max(surface.generatedRaw,0.0)/8.0,
        max(surface.generatedHeight,0.0)*weight*damping/3.85,generatedFoam);
}
float3 fftPressureRetentionDebug(FftSurface surface,float weight,float damping,float generatedFoam) {
    // Gaussian tails smaller than this raw height are below meaningful float subtraction
    // resolution at the limiter. Do not mislabel their cancellation as a large red footprint.
    if(surface.generatedRaw<=0.0001) return 0;
    float isolated=fftVerticalBound*tanh(surface.generatedRaw/fftVerticalBound);
    return float3(1.0-saturate(surface.generatedHeight/max(isolated,0.000001)),
        weight*damping,generatedFoam);
}
)HLSL"+(bowLift?R"HLSL(float3 fftBowRetentionDebug(FftSurface surface,float weight,float freeDamping,float damping) {
    // R: registered bow / 8 world units. G: its actual marginal final rise / V.
    // B: available mesh envelope. Keep stern/reflected/incident height in the same limiter;
    // an isolated tanh(bow/V) cannot measure the loss at the current FFT phase.
    float lifted=fftBowLift(surface.bow,float3(freeDamping,0,0),float3(damping,0,0)).x;
    return float3(max(surface.bow.x,0.0)/8.0,
        max(damping*surface.bowLimitedDelta+lifted,0.0)*weight/fftVerticalBound,
        weight*freeDamping);
}
)HLSL":std::string())+R"HLSL(
float2 fftParameterGradient(float value,float2 px,float2 py) {
    float det=px.x*py.y-px.y*py.x;
    float inverse=abs(det)>0.000001?1.0/det:0.0;
    return float2(ddx(value)*py.y-ddy(value)*px.y,ddy(value)*px.x-ddx(value)*py.x)*inverse;
}
)HLSL";
    if(waveHeading) {
        // Only rename the position entry points, never fftMapSamples / fftDecodeSamples.
        const size_t begin=source.find("FftSurface fftRawSurface(float2 p,float3 lod)");
        const size_t end=source.find("FftSurface fftSurface(float2 p,float level,float footprint",begin);
        std::string raw=source.substr(begin,end-begin);
        for(const auto &names:{std::pair<const char*,const char*>{"fftRawSurface","fftUnrotatedSurface"},
            {"fftRawAndShadingSurface","fftUnrotatedAndShadingSurface"}})
            for(size_t at=raw.find(names.first);at!=std::string::npos;at=raw.find(names.first,at+std::strlen(names.second)))
                raw.replace(at,std::strlen(names.first),names.second);
        source.replace(begin,end-begin,raw+HeadingPixelSource());
        source=HeadingSource(false)+source;
    }
    if(textureArray) {
        // Preserve the independent-map source contract used by the coastal decoder and
        // numerical fixtures; only the material's native binding changes.
        std::string sampling;
        for(unsigned layer=0;layer<6;++layer) {
            const std::string i=std::to_string(layer),stage=std::to_string(layer+8);
            const std::string from="Texture"+stage+".SampleLevel(Sampler"+stage+",",to="fftMap"+i+"(";
            for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size()))
                source.replace(at,from.size(),to);
            sampling+="float4 fftMap"+i+"(float2 uv,float lod) { return OceanMaps.SampleLevel(OceanSampler,float3(uv,"+i+"),lod); }\n";
        }
        source=sampling+source;
    }
    return source;
}
} // namespace WaterFftSampling

#if defined(__linux__)
class IDirect3DDevice9;
// The W3D water renderer reaches the SDL device through these, beside PosixWaterShore*.
bool PosixWaterGpuAvailable(IDirect3DDevice9 *);
// Nonzero when the patch may omit analytic heights the FFT replaces; see Begin_Prepared_Water_Fft.
uint64_t PosixWaterFftBegin(IDirect3DDevice9 *);
void PosixWaterFftEnd(IDirect3DDevice9 *,uint64_t token);
#endif
