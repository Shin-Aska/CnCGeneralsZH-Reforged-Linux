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
#include "Platform/WaterMovementCompatibilityShader.h"
#include "Platform/WaterWavefront.h"
#include "Platform/WaterSharedShader.h"
#include "Platform/WaterCleanShader.h"

namespace WaterFftSampling {
inline void CleanReplace(std::string &s,const std::string &from,const std::string &to) {
    for(size_t at=s.find(from);at!=std::string::npos;at=s.find(from,at+to.size()))s.replace(at,from.size(),to);
}
inline void CleanRemoveRead(std::string &s,const char *token) {
    for(size_t p=s.find(token);p!=std::string::npos;p=s.find(token,p+15)) {
        size_t end=p+std::strlen(token);int nesting=1;
        for(;end<s.size() && nesting;++end) {if(s[end]=='(')++nesting;else if(s[end]==')')--nesting;}
        s.replace(p,end-p,"float4(0,0,0,0)");
    }
}

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
// A/B experiment only: ZH_WATER_FFT_TIMESCALE overrides the FFT tempo (default 0.4).
inline float TimeScaleFromEnvironment() {
    const char *value=std::getenv("ZH_WATER_FFT_TIMESCALE");
    const float scale=value ? float(std::atof(value)) : 0.4f;
    return scale>0.0f && scale<=4.0f ? scale : 0.4f;
}
inline const float TimeScale=TimeScaleFromEnvironment();
// Keep phase continuous when the live speed changes, including while the game is paused.
inline float PhaseTime(float time) {
    if(!WaterTuning::Enabled())return time*TimeScale;
    static double speed=WaterTuning::Current()[WaterTuning::WaveSpeed],offset=0;
    static float previous=time;
    const double next=WaterTuning::Current()[WaterTuning::WaveSpeed];
    if(next!=speed) {offset+=double(previous)*(speed-next);speed=next;}
    previous=time;return float(double(time)*speed+offset);
}
constexpr unsigned SwellComponents = 4;
constexpr double SwellWavelengths[SwellComponents] = {287, 431, 683, 1091};
constexpr double SwellOffsets[SwellComponents] = {0.37, 2.13, 4.71, 1.19};
constexpr double SwellNoiseSize = 400, SwellNoiseAmount = 0.30;
constexpr double SwellGravity = 9.81, SwellHorizontal = -1.2;
constexpr double SwellHashScale = 0.1031, SwellHashOffset = 33.33;
// Both stages consume the same immutable draw snapshot. Slot zero remains the generated
// fixed vertex block; the vessel field is water-only slot one on the vertex stage.
inline std::string UniformSource(const char *name,unsigned slot,bool visual_hull=false,bool live_tuning=true) {
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
    if(live_tuning && WaterTuning::Enabled())source+="    float4 WaterTuningDepth;\n    float4 WaterTuningChop;\n    float4 WaterTuningLook;\n    float4 WaterTuningRms;\n    float4 WaterTuningVariance[9];\n    float4 WaterTuningShore;\n    float4 WaterTuningHeading;\n";
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
// The original headed evaluator remains the sea path. Local water only applies
// a smooth residual envelope and a small, irregular current-advected surface.
// The local phase values never become wave crests; only their direction is used.
inline std::string LocalSource(unsigned resolution = WaterFftSpectrum::Resolution, bool openSea = false) {
    char constants[128];
    std::snprintf(constants, sizeof(constants), "static const float fftLocalTexel=%.9g;\n",
        double(WorldScale *WaterFftSpectrum::Sizes[2] / resolution));
    std::string source = std::string(constants) + WaterWavefront::ShaderSource() + R"HLSL(
struct FftLocalFields {
    FftWavefrontParameters first,second;
    float3 influence,legacyGain; // value and world X/Y derivatives
    float2 shore,shoreX,shoreY; // inward wet normal times sqrt(bank fade), and derivatives
};
// Quintic value interpolation has zero first/second derivatives at cell edges.
// Every shared corner therefore gives C2 gain/influence, including zero support.
float3 fftLocalEnvelope(float4 values,float2 f,float spacing) {
    float2 u=float2(fftWavefrontBasis(f.x).y,fftWavefrontBasis(f.y).y);
    float2 du=float2(fftWavefrontBasisDerivative(f.x).y,fftWavefrontBasisDerivative(f.y).y)/spacing;
    return float3(lerp(lerp(values.x,values.y,u.x),lerp(values.z,values.w,u.x),u.y),
        lerp(values.y-values.x,values.w-values.z,u.y)*du.x,
        lerp(values.z-values.x,values.w-values.y,u.x)*du.y);
}
// Metadata-only version for the analytic side of the FFT mesh seam. CPU
// fallback vertices sample the same quintic residual envelope exactly once.
float3 fftLocalResidualGain(float2 p) {
    if(FftParameters.z<=1.0)return float3(1,0,0);
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    int2 size=int2(width/5,height);if(any(size<2))return float3(1,0,0);
    float scale=(width/5)/max(TerrainParameters.y,1.0),spacing=1.0/scale;
    float2 rawCell=(p+TerrainParameters.xx)*scale,cell=clamp(rawCell,0.0,float2(size-1));
    int2 lo=min(int2(floor(cell)),size-2),hi=lo+1;
    float2 f=cell-float2(lo),texel=1.0/float2(width,height);float4 influence,residual;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=int2(x?hi.x:lo.x,y?hi.y:lo.y);int i=y*2+x;
        float2 centre=(float2(at.x*5,at.y)+0.5)*texel;
        float4 m=FftHeadingTexture.SampleLevel(FftHeadingSampler,centre+float2(4*texel.x,0),0);
        influence[i]=m.b;residual[i]=m.a;
    }
    float3 a=fftLocalEnvelope(influence,f,spacing);
    [branch] if(a.x==0.0)return float3(1,0,0);
    float3 r=fftLocalEnvelope(residual,f,spacing);
    float2 inside=step(0.0,rawCell)*step(rawCell,float2(size-1));
    return float3(1-a.x+a.x*r.x,(a.yz*(r.x-1)+a.x*r.yz)*inside);
}
float3 fftLocalAnalyticWave(float3 wave,float3 gain) {
    [branch] if(gain.x==1.0 && all(gain.yz==0.0))return wave;
    return float3(wave.x*gain.x,wave.yz*gain.x+wave.x*gain.yz);
}
FftWavefrontParameters fftLocalChart(float2 centres[4],float offset,float2 texel,FftWavefrontBasis basis,float spacing) {
    float4 corners[4];float phaseStep=max(FftParameters.z-1.0,0.0);
    [unroll] for(int i=0;i<4;++i) {
        float4 c=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(offset*texel.x,0),0)*255.0);
        corners[i]=float4((c.r*256+c.g)*phaseStep,(c.ba-128.0)*fftGradientRange/127.0,0);
    }
    return fftWavefrontInterpolatePrepared(corners[0],corners[1],corners[2],corners[3],1.0,basis,spacing);
}
FftLocalFields fftLocalFields(float2 p) {
    precise FftLocalFields fields=(FftLocalFields)0;fields.legacyGain=float3(1,0,0);
    if(FftParameters.z<=1.0)return fields;
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    int2 size=int2(width/5,height);if(any(size<2))return fields;
    float scale=(width/5)/max(TerrainParameters.y,1.0),spacing=1.0/scale;
    float2 rawCell=(p+TerrainParameters.xx)*scale,cell=clamp(rawCell,0.0,float2(size-1));
    int2 lo=min(int2(floor(cell)),size-2),hi=lo+1;
    float2 f=cell-float2(lo),texel=1.0/float2(width,height);
    float2 centres[4];float4 weight0,weight1,influence,residual;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=int2(x?hi.x:lo.x,y?hi.y:lo.y);int i=y*2+x;
        centres[i]=(float2(at.x*5,at.y)+0.5)*texel;
        float4 m=FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(4*texel.x,0),0);
        weight0[i]=m.r;weight1[i]=m.g;influence[i]=m.b;residual[i]=m.a;
    }
    float2 inside=step(0.0,rawCell)*step(rawCell,float2(size-1));
    fields.influence=fftLocalEnvelope(influence,f,spacing);fields.influence.yz*=inside;
    // Exact identity: no phase reads, wave evaluation or amplitude arithmetic in sea.
    [branch] if(fields.influence.x==0.0)return fields;
    float3 r=fftLocalEnvelope(residual,f,spacing);r.yz*=inside;
    fields.legacyGain=float3(1-fields.influence.x+fields.influence.x*r.x,
        fields.influence.yz*(r.x-1)+fields.influence.x*r.yz);
    bool active0=any(weight0>0.0),active1=any(weight1>0.0);
    [branch] if(!active0 && !active1)return fields;
    float3 a=fftLocalEnvelope(weight0,f,spacing),b=fftLocalEnvelope(weight1,f,spacing);
    a.yz*=inside;b.yz*=inside;
    float4 shore0,shore1;
    [unroll] for(int i=0;i<4;++i) {
        // G/B of the original metadata word were unused. Sea corners retain
        // their original bytes, so explicitly supply neutral zero there.
        float2 bankBytes=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(texel.x,0),0).gb*255.0);
        float2 bank=influence[i]>0.0?(bankBytes-128.0)/127.0:float2(0,0);
        shore0[i]=bank.x;shore1[i]=bank.y;
    }
    float3 sx=fftLocalEnvelope(shore0,f,spacing),sy=fftLocalEnvelope(shore1,f,spacing);
    fields.shore=float2(sx.x,sy.x);
    fields.shoreX=float2(sx.y,sy.y)*inside.x;fields.shoreY=float2(sx.z,sy.z)*inside.y;
    FftWavefrontBasis basis=fftWavefrontPrepare(f);
    // Nonnegative corner weights make an absent route's value and derivatives
    // exactly zero. It cannot contribute waves, so omit its phase reads/tensor.
    [branch] if(active0)fields.first=fftLocalChart(centres,2.0,texel,basis,spacing);
    [branch] if(active1)fields.second=fftLocalChart(centres,3.0,texel,basis,spacing);
    fields.first.gradient*=inside;fields.second.gradient*=inside;
    fields.first.hessian*=float3(inside.x,inside.x*inside.y,inside.y);
    fields.second.hessian*=float3(inside.x,inside.x*inside.y,inside.y);
    fields.first.amplitude=fields.influence.x*a.x;
    fields.first.amplitudeGradient=fields.influence.yz*a.x+fields.influence.x*a.yz;
    fields.second.amplitude=fields.influence.x*b.x;
    fields.second.amplitudeGradient=fields.influence.yz*b.x+fields.influence.x*b.yz;
    return fields;
}
struct FftLocalWaves {
    float3 displacement,derivativeX,derivativeY;
    float3 shadingDisplacement,shadingDerivativeX,shadingDerivativeY;
    float variance;
};
// C2 world-anchored scalar noise. Return height and its coordinate gradient.
float fftStreamHash(float2 p) {
    precise float3 q=frac(float3(p.x,p.y,p.x)*.1031);
    q+=dot(q,q.yzx+33.33);return frac((q.x+q.y)*q.z);
}
float3 fftStreamNoise(float2 p) {
    precise float2 cell=floor(p),f=frac(p),u=f*f*f*(f*(f*6-15)+10);
    precise float2 du=30*f*f*(f-1)*(f-1);
    precise float a=fftStreamHash(cell),b=fftStreamHash(cell+float2(1,0));
    precise float c=fftStreamHash(cell+float2(0,1)),d=fftStreamHash(cell+1);
    return float3(2*lerp(lerp(a,b,u.x),lerp(c,d,u.x),u.y)-1,
        2*lerp(b-a,d-c,u.y)*du.x,2*lerp(c-a,d-b,u.x)*du.y);
}
float2 fftStreamQuarterTurn(float2 p) {return float2(-p.y,p.x);}
// Two short advection intervals reset only while their weight and its first two
// derivatives are zero. Spatially staggered clocks avoid a whole-map blend pulse.
// Both coordinates collapse to p for zero velocity, preserving a still pool.
float3 fftStreamAdvected(float2 p,float2 velocity,float2 velocityX,float2 velocityY,float time,float3 clock) {
    const float period=12.0;
    [branch] if(all(velocity==0.0) && all(velocityX==0.0) && all(velocityY==0.0))return fftStreamNoise(p);
    precise float phase=time/period+.22*clock.x;
    precise float2 phaseGradient=.22*clock.yz;
    precise float height=0,weight=0;float2 slope=0,weightGradient=0;
    [unroll] for(int layer=0;layer<2;++layer) {
        precise float age=frac(phase+.5*layer),r=age*(1-age);
        precise float w=64*r*r*r,dw=192*r*r*(1-2*age);
        precise float2 wg=dw*phaseGradient;
        precise float elapsed=period*age;float2 elapsedGradient=period*phaseGradient;
        precise float2 offset=layer*7.4*fftStreamQuarterTurn(velocity);
        precise float2 q=p-velocity*elapsed+offset;
        precise float2 qx=float2(1,0)-velocityX*elapsed-velocity*elapsedGradient.x+layer*7.4*fftStreamQuarterTurn(velocityX);
        precise float2 qy=float2(0,1)-velocityY*elapsed-velocity*elapsedGradient.y+layer*7.4*fftStreamQuarterTurn(velocityY);
        precise float3 n=fftStreamNoise(q);
        height+=w*n.x;weight+=w;
        slope+=w*float2(dot(n.yz,qx),dot(n.yz,qy))+wg*n.x;
        weightGradient+=wg;
    }
    return float3(height/weight,(slope-height/weight*weightGradient)/weight);
}
// Differentiate directly in world coordinates. The old A*J*A^-1 velocity
// Jacobian and A^-T clock gradient cancel with the final A^T slope map.
// p/velocity stay in noise coordinates, preserving every hash input and height.
float3 fftStreamAdvectedWorld(float2 p,float2 velocity,float2 velocityX,float2 velocityY,float2 basisX,float2 basisY,float time,float3 clock) {
    const float period=12.0;
    [branch] if(all(velocity==0.0) && all(velocityX==0.0) && all(velocityY==0.0)) {
        float3 n=fftStreamNoise(p);return float3(n.x,dot(n.yz,basisX),dot(n.yz,basisY));
    }
    precise float phase=time/period+.22*clock.x;
    precise float2 phaseGradient=.22*clock.yz;
    precise float height=0,weight=0;float2 slope=0,weightGradient=0;
    [unroll] for(int layer=0;layer<2;++layer) {
        precise float age=frac(phase+.5*layer),r=age*(1-age);
        precise float w=64*r*r*r,dw=192*r*r*(1-2*age);
        precise float2 wg=dw*phaseGradient;
        precise float elapsed=period*age;float2 elapsedGradient=period*phaseGradient;
        precise float2 offset=layer*7.4*fftStreamQuarterTurn(velocity);
        precise float2 q=p-velocity*elapsed+offset;
        precise float2 qx=basisX-velocityX*elapsed-velocity*elapsedGradient.x+layer*7.4*fftStreamQuarterTurn(velocityX);
        precise float2 qy=basisY-velocityY*elapsed-velocity*elapsedGradient.y+layer*7.4*fftStreamQuarterTurn(velocityY);
        precise float3 n=fftStreamNoise(q);
        height+=w*n.x;weight+=w;
        slope+=w*float2(dot(n.yz,qx),dot(n.yz,qy))+wg*n.x;
        weightGradient+=wg;
    }
    return float3(height/weight,(slope-height/weight*weightGradient)/weight);
}
// Smoothly remove only motion into the bank. |shore|^2 is the supplied fade,
// so this never reverses the normal component or increases speed. Differentiate
// both the incoming velocity and the spatially changing shore vector.
void fftStreamProject(inout float2 velocity,inout float2 velocityX,inout float2 velocityY,
    float2 shore,float2 shoreX,float2 shoreY) {
    precise float d=dot(velocity,shore);
    [branch] if(d>=0.0)return;
    precise float dx=dot(velocityX,shore)+dot(velocity,shoreX);
    precise float dy=dot(velocityY,shore)+dot(velocity,shoreY);
    precise float t=saturate(-d/.02),smooth=t*t*(3-2*t);
    precise float remove=d*smooth,removeSlope=smooth-d*6*t*(1-t)/.02;
    velocity-=shore*remove;
    velocityX-=shoreX*remove+shore*(removeSlope*dx);
    velocityY-=shoreY*remove+shore*(removeSlope*dy);
}
void fftLocalArrival(inout FftLocalWaves value,float2 p,FftWavefrontParameters field,float2 shore,float2 shoreX,float2 shoreY,float footprint2,float3 clock,float budget,float2 budgetGradient,float gradientBudget) {
    [branch] if(field.amplitude==0.0 && all(field.amplitudeGradient==0.0))return;
    // Arrival distance is a direction surrogate, never a phase oscillator or a
    // speed estimate. Energy sets motion; the regularizer keeps dry/flat charts smooth.
    precise float inverseLength=rsqrt(dot(field.gradient,field.gradient)+.0016);
    precise float2 direction=field.gradient*inverseLength;
    precise float2 directionX=(field.hessian.xy-direction*dot(direction,field.hessian.xy))*inverseLength;
    precise float2 directionY=(field.hessian.yz-direction*dot(direction,field.hessian.yz))*inverseLength;
    precise float speed=.12+1.1*budget;
    precise float2 velocity=direction*speed;
    precise float2 velocityX=directionX*speed+direction*(1.1*budgetGradient.x);
    precise float2 velocityY=directionY*speed+direction*(1.1*budgetGradient.y);
    fftStreamProject(velocity,velocityX,velocityY,shore,shoreX,shoreY);
    precise float3 sizes=float3(19,10,5.2),heights=float3(.25,.15,.09);
    precise float3 rotations=float3(.37,1.63,2.71);
    [unroll] for(int i=0;i<3;++i) {
        precise float sn,cs;sincos(rotations[i],sn,cs);float2 rotation=float2(cs,sn);
        // Rotate the irregular pattern, not the underlying current directions.
        precise float2 shape=float2(1,1.65);
        precise float2 q=shape*fftRotate(p,rotation)/sizes[i]+float2(13.1*i,-7.7*i);
        precise float2 v=shape*fftRotate(velocity,rotation)/sizes[i];
        precise float2 vx=shape*fftRotate(velocityX,rotation)/sizes[i],vy=shape*fftRotate(velocityY,rotation)/sizes[i];
        precise float2 basisX=shape*float2(cs,sn)/sizes[i],basisY=shape*float2(-sn,cs)/sizes[i];
        precise float3 n=fftStreamAdvectedWorld(q,v,vx,vy,basisX,basisY,WaterParameters.x,clock);
        precise float2 ng=n.yz;
        precise float h=heights[i]*field.amplitude*n.x;
        precise float2 slope=heights[i]*(field.amplitudeGradient*n.x+field.amplitude*ng);
        value.displacement.z+=h;value.derivativeX.z+=slope.x;value.derivativeY.z+=slope.y;
        // LOD affects shading only. Its per-octave factor is spatially constant,
        // so the filtered tangent remains the derivative of filtered height.
        precise float k=6.283185307179586/sizes[i],filter=exp(-k*k*footprint2);
        value.shadingDisplacement.z+=filter*h;
        value.shadingDerivativeX.z+=filter*slope.x;value.shadingDerivativeY.z+=filter*slope.y;
        value.variance+=.4*heights[i]*heights[i]*(1-filter*filter)*
            (field.amplitude*budget*k*k+length(field.amplitudeGradient)*gradientBudget);
    }
}
FftLocalWaves fftLocalWaves(float2 p,FftLocalFields fields,float lod) {
    precise FftLocalWaves value=(FftLocalWaves)0;
    [branch] if(fields.first.amplitude==0.0 && all(fields.first.amplitudeGradient==0.0)
        && fields.second.amplitude==0.0 && all(fields.second.amplitudeGradient==0.0))return value;
    // Both routes share the spatial clock and footprint, not their current or phase.
    precise float footprint2=max(0.0,exp2(2*lod)-1)*fftLocalTexel*fftLocalTexel/24.0;
    precise float3 clock=fftStreamNoise(p/83.0+float2(11.7,-6.3));clock.yz/=83.0;
    float budget=fields.first.amplitude+fields.second.amplitude;
    float2 budgetGradient=fields.first.amplitudeGradient+fields.second.amplitudeGradient;
    float gradientBudget=length(fields.first.amplitudeGradient)+length(fields.second.amplitudeGradient);
    fftLocalArrival(value,p,fields.first,fields.shore,fields.shoreX,fields.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    fftLocalArrival(value,p,fields.second,fields.shore,fields.shoreX,fields.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    return value;
}
)HLSL";
    if (openSea) {
        const std::string setup = "precise FftLocalFields fields=(FftLocalFields)0;fields.legacyGain=float3(1,0,0);";
        source.insert(source.find(setup) + setup.size(), "return fields;");
    }
    return source;
}
// A/B experiment only: snapshot once at shader generation, never per frame.
inline bool SharedEnabled() {
#if defined(__linux__)
    static const bool enabled=[] {const char *v=std::getenv("ZH_WATER_SHARED");return v && std::strcmp(v,"1")==0;}();
    return enabled && !WaterCleanSpectrum::Enabled(); // A/B experiment only: CLEAN wins over SHARED.
#else
    return false;
#endif
}
// A/B experiment only: read once for shader generation, never while rendering a frame.
inline bool WindRequested() {
#if defined(__linux__)
    static const bool enabled=[] { const char *value=std::getenv("ZH_WATER_MEDIUM");
        return value && std::strcmp(value,"wind")==0; }();
    return enabled;
#else
    return false;
#endif
}
inline bool OneMediumWindEnabled() {return WaterCleanSpectrum::Enabled() || SharedEnabled() || WindRequested();}
// A/B experiment only: D's extra atlas word preserves all height/cage channels and bindings.
inline std::string MediumDepthSource(bool cleanMain=false) {
    if(cleanMain) {
        std::string source=R"HLSL(
float4 fftMediumDepthField(float2 p) {

    if(TerrainParameters.w<.5)return float4(1e9,0,0,1);
    uint width,height;FftCleanDepth.GetDimensions(width,height);
    if(width<2 || height<2)return float4(1e9,0,0,1);
    float scale=width/max(TerrainParameters.y,1.0);
    float2 cell=(p+TerrainParameters.xx)*scale;
    if(any(cell<0) || any(cell>float2(width-1,height-1)))return float4(1e9,0,0,1);
    int2 lo=min(int2(floor(cell)),int2(width-2,height-2));float2 f=cell-float2(lo);
    float depth=FftCleanDepth.SampleLevel(FftCleanDepthSampler,(cell+.5)/float2(width,height),0).r;
    float3 coefficients=FftCleanDepth.SampleLevel(FftCleanDepthSampler,(float2(lo)+.5)/float2(width,height),0).gba;
    return float4(depth,(coefficients.xy+coefficients.z*f.yx)*scale,1);
}
float3 fftMediumGain(float4 depth,float reach) {
    float t=saturate(depth.x/reach);
    return float3(t*t*(3-2*t),depth.yz*(6*t*(1-t)/reach));
}
void fftMediumScale(inout float3 d,inout float3 dx,inout float3 dy,float3 gain) {
    dx=dx*gain.x+d*gain.y;dy=dy*gain.x+d*gain.z;d*=gain.x;
}
)HLSL";
        if(WaterCleanProfile::Get().nodepth) {
            const size_t begin=source.find("    if(TerrainParameters.w"),end=source.find("\n}\nfloat3 fftMediumGain",begin);
            source.replace(begin,end-begin,"    return float4(1e9,0,0,1);");
        }
        return source;
    }
    std::string source=R"HLSL(
float4 fftMediumDepthField(float2 p) {
    if(TerrainParameters.w<0.5 || FftParameters.z<=0.5)return float4(40,0,0,0);
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    const int stride=6;
    int2 size=int2(width/stride,height);if(any(size<2))return float4(40,0,0,0);
    float scale=(width/stride)/max(TerrainParameters.y,1.0);
    float2 raw=(p+TerrainParameters.xx)*scale,cell=clamp(raw,0.0,float2(size-1));
    int2 lo=min(int2(floor(cell)),size-2);float2 f=cell-float2(lo);
    float4 depths=0;float valid=1;
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=lo+int2(x,y);
        float2 uv=(float2(at.x*stride+stride-1,at.y)+0.5)/float2(width,height);
        float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,uv,0);
        depths[y*2+x]=packed.g*40.0;valid=min(valid,packed.a);
    }
    float2 inside=step(0.0,raw)*step(raw,float2(size-1));
    return float4(lerp(lerp(depths.x,depths.y,f.x),lerp(depths.z,depths.w,f.x),f.y),
        lerp(depths.y-depths.x,depths.w-depths.z,f.y)*scale*inside.x,
        lerp(depths.z-depths.x,depths.w-depths.y,f.x)*scale*inside.y,valid);
}
float3 fftMediumGain(float4 depth,float reach) {
    float t=saturate(depth.x/reach);
    return float3(t*t*(3-2*t),depth.yz*(6*t*(1-t)/reach));
}
float3 fftMediumBand(float4 depth,float reach) {
    if(depth.w<0.5)return float3(1,0,0);
    // The CPU already applies the fine envelope once, including obstacles/hulls.
    // Divide it out here: the final gain is the requested band gain, never gain*fine.
    // Cancel the shared D^2 analytically below D=4, including its finite dry-edge limit.
    // This avoids both a raw-wave discontinuity and a tiny fine^2 denominator.
    if(depth.x<4.0) {
        float c=16.0/(reach*reach),denominator=3.0-0.5*depth.x;
        return float3(c*(3.0-2.0*depth.x/reach)/denominator,
            depth.yz*(c*(1.5-6.0/reach)/(denominator*denominator)));
    }
    return fftMediumGain(depth,reach);
}
void fftMediumScale(inout float3 d,inout float3 dx,inout float3 dy,float3 gain) {
    dx=dx*gain.x+d*gain.y;dy=dy*gain.x+d*gain.z;d*=gain.x;
}
)HLSL";
    if(WaterCleanSpectrum::Enabled()) {
        const size_t field=source.find("float4 fftMediumDepthField"),begin=source.find("    if(TerrainParameters.w",field),end=source.find("\n}\nfloat3 fftMediumGain",begin);
        std::string legacy=source.substr(field,end+3-field);
        CleanReplace(legacy,"fftMediumDepthField(","fftCleanLegacyDepthField(");
        if(WaterCleanShore::Enabled()) {
            CleanReplace(legacy,"const int stride=6;","const int stride=7;");
            CleanReplace(legacy,"at.x*stride+stride-1","at.x*stride+5");
            CleanReplace(legacy,"valid=min(valid,packed.a);","valid=min(valid,step(0.5,packed.a));");
        }
        source.replace(begin,end-begin,R"HLSL(    if(TerrainParameters.w<0.5 || FftParameters.z<=0.5)return float4(40,0,0,0);
    uint width,height;FftCleanDepth.GetDimensions(width,height);
    if(width<2 || height<2)return fftCleanLegacyDepthField(p);
    float scale=width/max(TerrainParameters.y,1.0);
    float2 raw=(p+TerrainParameters.xx)*scale,cell=clamp(raw,0.0,float2(width-1,height-1));
    int2 lo=min(int2(floor(cell)),int2(width-2,height-2));float2 f=cell-float2(lo);
    // R is hardware-linear D; the exact-centre GBA lookup retains the product-rule gradient.
    float depth=FftCleanDepth.SampleLevel(FftCleanDepthSampler,(cell+.5)/float2(width,height),0).r;
    float3 coefficients=FftCleanDepth.SampleLevel(FftCleanDepthSampler,(float2(lo)+.5)/float2(width,height),0).gba;
    float2 inside=step(0.0,raw)*step(raw,float2(width-1,height-1));
    return float4(depth,(coefficients.xy+coefficients.z*f.yx)*scale*inside,1);)HLSL");
        if(!WaterCleanProfile::Get().nodepth)source.insert(field,legacy);
    }
    if(WaterCleanSpectrum::Enabled() && WaterCleanProfile::Get().nodepth) {
        const size_t begin=source.find("    if(TerrainParameters.w"),end=source.find("\n}\nfloat3 fftMediumGain",begin);
        source.replace(begin,end-begin,"    return float4(40,0,0,step(8.0,FftParameters.w)*step(0.5,FftParameters.z)*step(0.5,TerrainParameters.w));");
    }
    if(WaterCleanSpectrum::Enabled() && WaterCleanProfile::Get().nodepth)
        CleanReplace(source,"float3 fftMediumGain(float4 depth,float reach) {","float3 fftMediumGain(float4 depth,float reach) { return float3(1,0,0);");
    return source;
}
// A/B experiment only: reuse the current reconstructed by fftLocalArrival, including bank projection.
inline std::string MediumFlowSource() {
    return R"HLSL(
struct FftMediumFlow {float2 velocity,velocityX,velocityY;};
void fftMediumFlowArrival(inout FftMediumFlow flow,FftWavefrontParameters field,FftLocalFields fields,float budget,float2 budgetGradient) {
    float inverseLength=rsqrt(dot(field.gradient,field.gradient)+0.0016);
    float2 direction=field.gradient*inverseLength;
    float2 directionX=(field.hessian.xy-direction*dot(direction,field.hessian.xy))*inverseLength;
    float2 directionY=(field.hessian.yz-direction*dot(direction,field.hessian.yz))*inverseLength;
    float speed=0.12+1.1*budget;
    float2 v=direction*speed,vx=directionX*speed+direction*(1.1*budgetGradient.x);
    float2 vy=directionY*speed+direction*(1.1*budgetGradient.y);
    fftStreamProject(v,vx,vy,fields.shore,fields.shoreX,fields.shoreY);
    flow.velocity+=field.amplitude*v;
    flow.velocityX+=field.amplitude*vx+field.amplitudeGradient.x*v;
    flow.velocityY+=field.amplitude*vy+field.amplitudeGradient.y*v;
}
FftMediumFlow fftMediumFlow(float2 p) {
    FftMediumFlow flow=(FftMediumFlow)0;
    FftLocalFields fields=fftLocalFields(p);
    if(fields.influence.x==0.0)return flow;
    float budget=fields.first.amplitude+fields.second.amplitude;
    float2 gradient=fields.first.amplitudeGradient+fields.second.amplitudeGradient;
    fftMediumFlowArrival(flow,fields.first,fields,budget,gradient);
    fftMediumFlowArrival(flow,fields.second,fields,budget,gradient);
    return flow;
}
float2 fftMediumFlowAge() {
    return frac(WaterParameters.x/12.0+float2(0,0.5));
}
float2 fftMediumFlowWeight(float2 age) {return 1.0-abs(2.0*age-1.0);}
)HLSL";
}
// Opt-in Linux field; default/shared shader text remains unchanged.
inline std::string HeadingSource(bool vertex, unsigned resolution = WaterFftSpectrum::Resolution,
    bool openSea = false)
{
    char constants[2048];
    const double noiseVariance = (26.0 / 35) * (26.0 / 35) / 12;
    std::snprintf(constants, sizeof(constants),
        "static const float fftGradientRange=%.9g;\nstatic const float fftSwellHeight=%.9g;\n"
        "static const float fftSwellTime=%.9g;\nstatic const float fftSwellTexel=%.9g;\n"
        "static const float4 fftSwellWavelengths=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float4 fftSwellOffsets=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float fftSwellNoiseSize=%.9g,fftSwellNoiseAmount=%.9g,fftSwellNoiseNormalization=%.9g;\n"
        "static const float fftSwellGravityScale=%.9g,fftSwellHorizontal=%.9g;\n"
        "static const float fftSwellHashScale=%.9g,fftSwellHashOffset=%.9g;\n",
        double(WaterWaveHeading::GradientRange), WaterFftSpectrum::HeightRms[0] * std::sqrt(2.0 / SwellComponents),
        double(TimeScale), double(WorldScale *WaterFftSpectrum::Sizes[0] / resolution),
        SwellWavelengths[0], SwellWavelengths[1], SwellWavelengths[2], SwellWavelengths[3],
        SwellOffsets[0], SwellOffsets[1], SwellOffsets[2], SwellOffsets[3],
        SwellNoiseSize, SwellNoiseAmount, std::sqrt(1 + SwellNoiseAmount *SwellNoiseAmount *noiseVariance),
        SwellGravity *WorldScale, SwellHorizontal, SwellHashScale, SwellHashOffset);
    char base[128];
    std::snprintf(base, sizeof(base), "static const float2 fftBaseHeading=float2(%.9g,%.9g);\n",
        double(WaterWaveHeading::BaseHeading.x), double(WaterWaveHeading::BaseHeading.y));
    // A/B experiment only: immutable shader-generation selection, shared with the material.
    const bool medium = OneMediumWindEnabled();
    const std::string headingBase = std::string(base) + (medium
        ? "static const bool fftOneMediumWind=true;\n" : "");
    std::string source = std::string(vertex ? R"HLSL(
Texture2D FftHeadingTexture : register(t4);
SamplerState FftHeadingSampler : register(s4);
)HLSL" : R"HLSL(
Texture2D FftHeadingTexture : register(t11);
SamplerState FftHeadingSampler : register(s11);
)HLSL")
        + (WaterCleanSpectrum::Enabled() && !WaterCleanProfile::Get().nodepth
            ? (vertex ? "Texture2D FftCleanDepth : register(t5);SamplerState FftCleanDepthSampler : register(s5);\n"
                        "Texture2D FftCleanRotation : register(t6);SamplerState FftCleanRotationSampler : register(s6);\n"
                      : "Texture2D FftCleanDepth : register(t12);SamplerState FftCleanDepthSampler : register(s12);\n"
                        "Texture2D FftCleanRotation : register(t13);SamplerState FftCleanRotationSampler : register(s13);\n") : std::string())
        + constants + headingBase + (medium ? MediumDepthSource() : std::string()) + R"HLSL(
float2 fftRotate(float2 p,float2 cs) {return float2(cs.x*p.x-cs.y*p.y,cs.y*p.x+cs.x*p.y);}
float3 fftRotateVector(float3 v,float2 cs) {return float3(fftRotate(v.xy,cs),v.z);}
struct FftSwell {float3 displacement,derivativeX,derivativeY,shadingDisplacement,shadingDerivativeX,shadingDerivativeY;float variance;
    float2 rotation0,rotation1,weights;};
float4 fftSwellField(float2 p) {
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    float2 cell=clamp((p+TerrainParameters.xx)*(width/5)/max(TerrainParameters.y,1.0),0.0,float2(width/5-1,height-1));
    int2 lo=int2(floor(cell)),hi=min(lo+1,int2(width/5-1,height-1));
    float2 f=frac(cell),texel=1.0/float2(width,height);float4 value=0;float weight=0;
    // The sampler is point-filtered and these are texel centres, so each read is one exact packed
    // texel. Sampling, not Load, keeps the image paired with its sampler for ShaderCross.
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=int2(x?hi.x:lo.x,y?hi.y:lo.y);
        float2 centre=(float2(at.x*5,at.y)+0.5)*texel;
        float4 a=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centre,0)*255.0);
        float4 b=FftHeadingTexture.SampleLevel(FftHeadingSampler,centre+float2(texel.x,0),0);
        float w=(x?f.x:1-f.x)*(y?f.y:1-f.y)*step(0.5,b.a);
        value+=w*float4((a.r*256+a.g)*FoamFillLights[3].w,(a.ba-128.0)*fftGradientRange/127.0,b.r);
        weight+=w;
    }
    return weight>0?value/weight:0;
}
// Differentiable value noise along crests. Return value and its world gradient.
float fftSwellHash(float2 p) {
    float3 q=frac(float3(p.x,p.y,p.x)*fftSwellHashScale);
    q+=dot(q,q.yzx+fftSwellHashOffset);return frac((q.x+q.y)*q.z);
}
float3 fftSwellNoise(float2 p) {
    float2 q=p/fftSwellNoiseSize,i=floor(q),f=frac(q),u=f*f*(3-2*f),du=6*f*(1-f)/fftSwellNoiseSize;
    float a=fftSwellHash(i),b=fftSwellHash(i+float2(1,0)),c=fftSwellHash(i+float2(0,1)),d=fftSwellHash(i+1);
    return float3(lerp(lerp(a,b,u.x),lerp(c,d,u.x),u.y),
        lerp(b-a,d-c,u.y)*du.x,lerp(c-a,d-b,u.x)*du.y);
}
FftSwell fftSwell(float2 p,float lod) {
    float4 field=fftSwellField(p);float2 g=field.yz,n=g/max(length(g),0.00001);
    FftSwell value=(FftSwell)0;
    // Medium FFT chop follows the refracted direction in 45-degree sectors. The field turns
    // gradually and never flips across a channel, so the pairwise cross-fade stays spread out.
    // Fine FFT ripples use the map's prevailing rotation independently of this local field.
    if(dot(g,g)<1e-10) {
        sincos(FftParameters.w,value.rotation0.y,value.rotation0.x);
        value.rotation1=value.rotation0;value.weights=float2(1,0);
    } else {
        float phi=atan2(fftBaseHeading.x*n.y-fftBaseHeading.y*n.x,dot(fftBaseHeading,n));
        float sector=phi/0.7853981633974483,k=floor(sector),t=frac(sector),u=saturate((t-0.25)/0.5);
        sincos(k*0.7853981633974483,value.rotation0.y,value.rotation0.x);
        sincos((k+1.0)*0.7853981633974483,value.rotation1.y,value.rotation1.x);
        // Explicit endpoints make the zero-weight branch exact (cos(pi/2) need not be zero).
        value.weights=u<=0.0?float2(1,0):u>=1.0?float2(0,1):float2(cos(u*1.5707963267948966),sin(u*1.5707963267948966));
    }
    // Four non-harmonic wavelengths inside the broad band; equal powers sum to HeightRms[0]^2.
    if(fftOneMediumWind) {
        // A/B experiment only: medium chop follows the wind, no sector rotation or cross-fade.
        sincos(FftParameters.w,value.rotation0.y,value.rotation0.x);
        value.rotation1=value.rotation0;value.weights=float2(1,0);
    }
    float3 noise=fftSwellNoise(p);
    // Mean-square normalization: smooth bilinear value noise has variance (26/35)^2/12.
    float gain=(1+fftSwellNoiseAmount*(noise.x-0.5))/fftSwellNoiseNormalization;
    float2 gainGradient=fftSwellNoiseAmount*noise.yz/fftSwellNoiseNormalization;
    [unroll] for(int i=0;i<4;++i) {
        float k=6.283185307179586/fftSwellWavelengths[i],sn,cs;
        sincos(k*field.x-sqrt(fftSwellGravityScale*k)*WaterParameters.x*fftSwellTime+fftSwellOffsets[i],sn,cs);
        float a=fftSwellHeight*field.w;
        float3 d=a*gain*float3(fftSwellHorizontal*n*sn,cs);
        float3 dx=a*(gain*float3(fftSwellHorizontal*n*cs,-sn)*k*g.x+gainGradient.x*float3(fftSwellHorizontal*n*sn,cs));
        float3 dy=a*(gain*float3(fftSwellHorizontal*n*cs,-sn)*k*g.y+gainGradient.y*float3(fftSwellHorizontal*n*sn,cs));
        // Gaussian with the box mip's second moment; level zero remains exact.
        float filter=exp(-k*k*dot(g,g)*max(0.0,exp2(2*lod)-1)*fftSwellTexel*fftSwellTexel/24.0);
        value.displacement+=d;value.derivativeX+=dx;value.derivativeY+=dy;
        value.shadingDisplacement+=filter*d;value.shadingDerivativeX+=filter*dx;value.shadingDerivativeY+=filter*dy;
        value.variance+=0.5*a*a*gain*gain*k*k*dot(g,g)*(1-filter*filter);
    }
    return value;
}
FftSwell fftSwell(float2 p) {return fftSwell(p,0.0);}
FftSwell fftFilteredSwell(FftSwell value) {
    value.displacement=value.shadingDisplacement;value.derivativeX=value.shadingDerivativeX;value.derivativeY=value.shadingDerivativeY;
    return value;
}
)HLSL" + LocalSource(resolution, openSea && !vertex) + (medium ? MediumFlowSource() : std::string()) + WaterMovementCompatibilityShader::HeadingSource();
    // A/B experiment only: omit the inactive prototype branch, retaining the default source contract.
    if (!medium) {
        const size_t begin=source.find("    if(fftOneMediumWind) {");
        const size_t end=source.find("    float3 noise=fftSwellNoise(p);",begin);
        source.erase(begin,end-begin);
    }
    // A/B experiment only: append depth metadata to the existing heading atlas; default text is untouched.
    if (medium) {
        auto replace = [&](const std::string &from, const std::string &to) {
            for (size_t at=source.find(from); at!=std::string::npos; at=source.find(from,at+to.size()))
                source.replace(at,from.size(),to);
        };
        replace("width/5", "width/6"); replace("at.x*5", "at.x*6");
        const std::string swellFields="float2 rotation0,rotation1,weights;};";
        source.replace(source.find(swellFields),swellFields.size(),"float2 rotation0,rotation1,weights;float4 mediumDepth;float mediumBreakFoam;};");
        const std::string finish = "    return value;\n}\nFftSwell fftSwell(float2 p)";
        replace(finish, R"HLSL(
    // A/B experiment only: incoming swell crests trip the depth limit before the band fade.
    value.mediumDepth=fftMediumDepthField(p);
    float crest=max(0.0,value.displacement.z),D=value.mediumDepth.x;
    value.mediumBreakFoam=smoothstep(-0.15,0.45,crest-0.20*D)*smoothstep(0.25,0.9,crest)
        *smoothstep(0.0,2.0,D)*value.mediumDepth.w;
    float3 band=fftMediumBand(value.mediumDepth,24.0);
    fftMediumScale(value.displacement,value.derivativeX,value.derivativeY,band);
    fftMediumScale(value.shadingDisplacement,value.shadingDerivativeX,value.shadingDerivativeY,band);
    value.variance*=band.x*band.x;
    return value;
}
FftSwell fftSwell(float2 p))HLSL");

    }
    // A/B experiment only: amplitude rows share the original heading binding.
    if (SharedEnabled()) {
        auto replace=[&](const std::string &from,const std::string &to) {
            for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size()))source.replace(at,from.size(),to);
        };
        replace("int2(width/6,height)","int2(width/6,height/3)");
        replace("width/6-1,height-1","width/6-1,height/3-1");
        replace("int2(width/stride,height)","int2(width/stride,height/3)");
        // Inactive elevations retain the previous map-aware swell/sector path.
        if(!WindRequested())replace("if(fftOneMediumWind)","if(fftOneMediumWind && fftMediumDepthField(p).w>0.5)");
        replace("float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,(p+TerrainParameters.xx+5.0)/max(TerrainParameters.y,1.0),0);",
            "uint aw,ah;FftHeadingTexture.GetDimensions(aw,ah);float side=ah/3;"
            "float2 at=clamp(floor((p+TerrainParameters.xx+5.0)*side/max(TerrainParameters.y,1.0)),0.0,side-1);"
            "float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,(float2(at.x*6,at.y)+0.5)/float2(aw,ah),0);");
        source+=WaterSharedShader::Source(TimeScale);
    }
    if(WaterCleanSpectrum::Enabled() && WaterCleanShore::Enabled()) {
        CleanReplace(source,"width/6","width/7");CleanReplace(source,"at.x*6","at.x*7");
    }
    // A/B experiment only: inactive elevations keep the original evaluator unless wind was explicit.
    if(WaterCleanSpectrum::Enabled() && !WindRequested()) {
        const std::string from="if(fftOneMediumWind)",to="if(fftOneMediumWind && fftMediumDepthField(p).w>0.5)";
        for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size()))source.replace(at,from.size(),to);
    }
    return source;
}

inline std::string CleanHeadingSource(bool vertex) {
    std::string source=vertex
        ? "Texture2D FftHeadingTexture : register(t4);SamplerState FftHeadingSampler : register(s4);\n"
        : "Texture2D FftHeadingTexture : register(t11);SamplerState FftHeadingSampler : register(s11);\n";
    if(!WaterCleanProfile::Get().nodepth)source+=vertex
        ? "Texture2D FftCleanDepth : register(t5);SamplerState FftCleanDepthSampler : register(s5);\nTexture2D FftCleanRotation : register(t6);SamplerState FftCleanRotationSampler : register(s6);\n"
        : "Texture2D FftCleanDepth : register(t12);SamplerState FftCleanDepthSampler : register(s12);\nTexture2D FftCleanRotation : register(t13);SamplerState FftCleanRotationSampler : register(s13);\n";
    // Preserve map-field diagnostics without adding any legacy wave evaluators.
    const std::string heading=HeadingSource(vertex);
    const size_t begin=heading.find("float4 fftSwellField("),end=heading.find("// Differentiable value noise",begin);
    char range[96];std::snprintf(range,sizeof(range),"static const float fftGradientRange=%.9g;\n",double(WaterWaveHeading::GradientRange));
    source+=std::string(range)+heading.substr(begin,end-begin);
    return source+MediumDepthSource(true)+"float2 fftRotate(float2 p,float2 cs) {return float2(cs.x*p.x-cs.y*p.y,cs.y*p.x+cs.x*p.y);}\n";
}

inline std::string HeadingVertexSource() {
    std::string source = WaterMovementCompatibilityShader::VertexSource() + R"HLSL(
// The texture-array rewrite expects each cascade sampled once, at q.
float3 fftChopDisplacement(float2 p,float2 r) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
    return fftRotateVector(b.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
}
float3 fftWindDisplacement(float2 p) {
    float2 r;sincos(FftParameters.w,r.y,r.x);
    float2 q=fftRotate(p,r*float2(1,-1));
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
    return fftRotateVector(c.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
}
float3 fftLegacyDisplacement(float2 p) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedDisplacement(p);
    FftSwell swell=fftSwell(p);float3 result=swell.displacement;
    [branch] if(swell.weights.x>0.0) result+=swell.weights.x*fftChopDisplacement(p,swell.rotation0);
    [branch] if(swell.weights.y>0.0) result+=swell.weights.y*fftChopDisplacement(p,swell.rotation1);
    result+=fftWindDisplacement(p);
    return result;
}

float3 fftRawDisplacement(float2 p) {
    [branch] if(FftParameters.z< -0.5)return fftCompatibilityDisplacement(p);
    [branch] if(FftParameters.z<=0.5)return fftUnrotatedDisplacement(p);
    FftLocalFields fields=fftLocalFields(p);
    [branch] if(fields.influence.x==0.0)return fftLegacyDisplacement(p);
    FftLocalWaves waves=fftLocalWaves(p,fields,0.0);
    return fields.legacyGain.x*fftLegacyDisplacement(p)+waves.displacement;
}
)HLSL";
    // A/B experiment only: two-phase flow samples reset at zero triangle weight.
    if (OneMediumWindEnabled()) {
        auto replace=[&](const std::string &from,const std::string &to) {
            for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size()))
                source.replace(at,from.size(),to);
        };
        replace("float3 fftChopDisplacement(float2 p,float2 r)","float3 fftUnadvectedChopDisplacement(float2 p,float2 r)");
        replace("float3 fftWindDisplacement(float2 p)","float3 fftUnadvectedWindDisplacement(float2 p)");
        const size_t before=source.find("float3 fftLegacyDisplacement(float2 p)");
        source.insert(before,R"HLSL(
float3 fftMediumFlowDisplacement(float2 p,float2 r,bool fine) {
    FftMediumFlow flow=fftMediumFlow(p);float3 value=0;
    if(all(flow.velocity==0.0)) {
        value=fine?fftUnadvectedWindDisplacement(p):fftUnadvectedChopDisplacement(p,r);
    } else {
        float2 age=fftMediumFlowAge(),weights=fftMediumFlowWeight(age);
        [unroll] for(int i=0;i<2;++i) {
            float2 q=p-flow.velocity*(12.0*age[i]);
            value+=weights[i]*(fine?fftUnadvectedWindDisplacement(q):fftUnadvectedChopDisplacement(q,r));
        }
    }
    return value*fftMediumBand(fftMediumDepthField(p),fine?4.0:12.0).x;
}
float3 fftChopDisplacement(float2 p,float2 r) {return fftMediumFlowDisplacement(p,r,false);}
float3 fftWindDisplacement(float2 p) {return fftMediumFlowDisplacement(p,float2(1,0),true);}
)HLSL");
    }
    // A/B experiment only: shared phase replaces every incident long/medium/local layer.
    if(SharedEnabled()) {
        if(!WindRequested()) {
            const std::string flow="    FftMediumFlow flow=fftMediumFlow(p);float3 value=0;";
            source.insert(source.find(flow),"    if(fftMediumDepthField(p).w<0.5)return fine?fftUnadvectedWindDisplacement(p):fftUnadvectedChopDisplacement(p,r);\n");
        }
        const std::string entry="float3 fftRawDisplacement(float2 p) {";
        source.insert(source.find(entry)+entry.size(),R"HLSL(
    float4 sharedDepth=fftMediumDepthField(p);
    if(sharedDepth.w>0.5) {
        FftSharedWave sw=fftSharedWave(p,0);
        float2 q=p;
        float3 fine=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xzy
            *float3(-1.5,-1.5,1)*fftWorldScale;
        return float3(0,0,sw.height)+fine*fftMediumGain(sharedDepth,4).x;
    }
)HLSL");
    }
    // A/B experiment only: clean FFT bypasses every local/sector/shared incident layer.
    if(WaterCleanSpectrum::Enabled()) {
        const std::string entry="float3 fftRawDisplacement(float2 p) {";
        source.insert(source.find(entry),WaterCleanShader::VertexSource());
        source.insert(source.find(entry)+entry.size(),
            "\n    float4 cleanDepth=fftMediumDepthField(p);\n    if(cleanDepth.w>0.5)return fftCleanDisplacement(p,cleanDepth);\n");
        if(!WindRequested()) {
            const std::string flow="    FftMediumFlow flow=fftMediumFlow(p);float3 value=0;";
            source.insert(source.find(flow),"    if(fftMediumDepthField(p).w<0.5)return fine?fftUnadvectedWindDisplacement(p):fftUnadvectedChopDisplacement(p,r);\n");
        }
    }
    return source;
}


inline std::string HeadingPixelSource(WaterFftSpectrum::Configuration configuration=WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution)) {
    std::string source = WaterMovementCompatibilityShader::PixelSource() + R"HLSL(
// Medium chop follows each refracted sector and feeds ambient breaking and contact wash.
// Fine wind ripples are sampled once below, outside the sector cross-fade.
void fftAccumulateChop(inout FftSurface medium,float2 p,float3 lod,float2 r,float w) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float4 b=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod.y);
    float4 bd=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod.y);
    float3 x=float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1),y=float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1);
    medium.displacement+=w*fftRotateVector(b.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    // Chain rule for q=R(-a)p, then R(a) on displacement XY.
    medium.derivativeX+=w*fftRotateVector(r.x*x-r.y*y,r);
    medium.derivativeY+=w*fftRotateVector(r.y*x+r.x*y,r);
    medium.contactIncident.yz+=w*fftRotate(bd.xy,r);
}
FftSurface fftWindSurface(float2 p,float lod) {
    float2 r;sincos(FftParameters.w,r.y,r.x);
    float2 q=fftRotate(p,r*float2(1,-1));
    float4 c=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);
    float4 cd=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);
    float3 x=float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1),y=float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    FftSurface fine=(FftSurface)0;
    fine.displacement=fftRotateVector(c.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    fine.derivativeX=fftRotateVector(r.x*x-r.y*y,r);
    fine.derivativeY=fftRotateVector(r.y*x+r.x*y,r);
    return fine;
}
FftSurface fftMediumSurface(float2 p,float3 lod,FftSwell swell) {
    FftSurface medium=(FftSurface)0;
    [branch] if(swell.weights.x>0.0) fftAccumulateChop(medium,p,lod,swell.rotation0,swell.weights.x);
    [branch] if(swell.weights.y>0.0) fftAccumulateChop(medium,p,lod,swell.rotation1,swell.weights.y);
    return medium;
}
FftSurface fftComposeSwellSurface(FftSurface medium,FftSurface fine,FftSwell swell) {
    // Broad swell and medium chop carry ambient breaking and contact wash, as cascades 0 and 1 did.
    float2 breaking=medium.displacement.xy+swell.displacement.xy;
    float2 breakingX=medium.derivativeX.xy+swell.derivativeX.xy,breakingY=medium.derivativeY.xy+swell.derivativeY.xy;
    float2 limit=fftLimitDerivative(float3(breaking,0)).xy;
    breakingX*=limit;breakingY*=limit;
    FftSurface value=(FftSurface)0;
    value.incidentCompression=(1+breakingX.x)*(1+breakingY.y)-breakingX.y*breakingY.x;
    float height=fftVerticalBound*tanh((medium.displacement.z+swell.displacement.z)/fftVerticalBound);
    float2 slope=medium.contactIncident.yz+float2(swell.derivativeX.z,swell.derivativeY.z);
    value.contactIncident=float3(height,slope*(1-(height/fftVerticalBound)*(height/fftVerticalBound)));
    value.displacement=medium.displacement+fine.displacement+swell.displacement;
    value.derivativeX=medium.derivativeX+fine.derivativeX+swell.derivativeX;
    value.derivativeY=medium.derivativeY+fine.derivativeY+swell.derivativeY;
    value.unresolvedSlopeVariance=swell.variance;
    return value;
}
FftSurface fftSwellSurface(float2 p,float3 lod,FftSwell swell) {
    return fftComposeSwellSurface(fftMediumSurface(p,lod,swell),fftWindSurface(p,lod.z),swell);
}
FftSurface fftLegacySurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedSurface(p,lod);
    return fftSwellSurface(p,lod,fftFilteredSwell(fftSwell(p,lod.x)));
}
FftSurface fftLegacySurface(float2 p) {return fftLegacySurface(p,float3(0,0,0));}
void fftLegacyAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z<=0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftSwell swell=fftSwell(p,lod.x);
    FftSurface medium=fftMediumSurface(p,float3(0,0,0),swell),fine=fftWindSurface(p,0);
    physical=fftComposeSwellSurface(medium,fine,swell);shading=physical;
    [branch] if(any(lod>0.0)) {
        // Reuse each resolved cascade's mip-zero value exactly. Fine-only
        // filtering must not fetch either medium sector a second time.
        [branch] if(lod.y>0.0)medium=fftMediumSurface(p,lod,swell);
        [branch] if(lod.z>0.0)fine=fftWindSurface(p,lod.z);
        shading=fftComposeSwellSurface(medium,fine,fftFilteredSwell(swell));
    }
}

FftSurface fftLocalSurface(FftSurface original,FftLocalFields fields,FftLocalWaves waves,float3 lod,bool filtered) {
    FftSurface value=original;float3 gain=fields.legacyGain;
    value.displacement=original.displacement*gain.x+(filtered?waves.shadingDisplacement:waves.displacement);
    value.derivativeX=original.derivativeX*gain.x+original.displacement*gain.y+(filtered?waves.shadingDerivativeX:waves.derivativeX);
    value.derivativeY=original.derivativeY*gain.x+original.displacement*gain.z+(filtered?waves.shadingDerivativeY:waves.derivativeY);
    value.contactIncident=float3(original.contactIncident.x*gain.x,
        original.contactIncident.yz*gain.x+original.contactIncident.x*gain.yz);
    value.incidentCompression=1+gain.x*(original.incidentCompression-1);
    value.localInfluence=1;
    value.localVariance=gain.x*gain.x*(fftShadingVariance(float3(0,lod.yz))+original.unresolvedSlopeVariance)+waves.variance;
    // Envelope roughness uses the original total height-variance budget as a
    // conservative local estimate, not an exact curved-footprint covariance solve.
    // No correction is needed at resolved LOD0.
    if(any(lod>0.0))value.localVariance+=dot(gain.yz,gain.yz)*3.0;
    return value;
}
FftSurface fftRawSurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z< -0.5)return fftCompatibilitySurface(p,lod);
    [branch] if(FftParameters.z<=0.5)return fftUnrotatedSurface(p,lod);
    FftLocalFields fields=fftLocalFields(p);
    [branch] if(fields.influence.x==0.0)return fftLegacySurface(p,lod);
    // Evaluate the scalar stream layer before retaining the paired FFT fields.
    // Only its Z/slope outputs need to stay live across texture sampling.
    FftLocalWaves waves=fftLocalWaves(p,fields,lod.z);
    FftSurface original=fftLegacySurface(p,lod);
    return fftLocalSurface(original,fields,waves,lod,true);
}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z< -0.5) {fftCompatibilityAndShadingSurface(p,lod,physical,shading);return;}
    [branch] if(FftParameters.z<=0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftLocalFields fields=fftLocalFields(p);
    [branch] if(fields.influence.x==0.0) {fftLegacyAndShadingSurface(p,lod,physical,shading);return;}
    // Keep the expensive stream evaluation ahead of the paired FFT outputs,
    // reducing their live range without changing samples or arithmetic.
    FftLocalWaves waves=fftLocalWaves(p,fields,lod.z);
    fftLegacyAndShadingSurface(p,lod,physical,shading);
    physical=fftLocalSurface(physical,fields,waves,float3(0,0,0),false);
    [branch] if(any(lod>0.0))shading=fftLocalSurface(shading,fields,waves,lod,true);
    else shading=physical;
}
)HLSL";
    // A/B experiment only: advection and depth products keep complete displacement derivatives.
    if (OneMediumWindEnabled()) {
        auto replace=[&](const std::string &from,const std::string &to) {
            for(size_t at=source.find(from);at!=std::string::npos;at=source.find(from,at+to.size()))
                source.replace(at,from.size(),to);
        };
        replace("FftSurface fftWindSurface(float2 p,float lod)","FftSurface fftUnadvectedWindSurface(float2 p,float lod)");
        replace("FftSurface fftMediumSurface(float2 p,float3 lod,FftSwell swell)","FftSurface fftUnadvectedMediumSurface(float2 p,float3 lod,FftSwell swell)");
        source.insert(source.find("FftSurface fftComposeSwellSurface("),R"HLSL(
FftSurface fftMediumFlowSurface(float2 p,float3 lod,FftSwell swell,bool fine) {
    FftMediumFlow flow=fftMediumFlow(p);FftSurface value=(FftSurface)0;
    // Exact still-water identity avoids two identical FFT reads in the open sea.
    if(all(flow.velocity==0.0) && all(flow.velocityX==0.0) && all(flow.velocityY==0.0)) {
        if(fine)value=fftUnadvectedWindSurface(p,lod.z);
        else value=fftUnadvectedMediumSurface(p,lod,swell);
    } else {
        float2 age=fftMediumFlowAge(),weights=fftMediumFlowWeight(age);
        [unroll] for(int i=0;i<2;++i) {
            float elapsed=12.0*age[i];float2 q=p-flow.velocity*elapsed;
            FftSurface bandSample;
            if(fine)bandSample=fftUnadvectedWindSurface(q,lod.z);
            else bandSample=fftUnadvectedMediumSurface(q,lod,swell);
            float2 qx=float2(1,0)-flow.velocityX*elapsed,qy=float2(0,1)-flow.velocityY*elapsed;
            value.displacement+=weights[i]*bandSample.displacement;
            value.derivativeX+=weights[i]*(bandSample.derivativeX*qx.x+bandSample.derivativeY*qx.y);
            value.derivativeY+=weights[i]*(bandSample.derivativeX*qy.x+bandSample.derivativeY*qy.y);
        }
    }
    float3 band=fftMediumBand(swell.mediumDepth,fine?4.0:12.0);
    fftMediumScale(value.displacement,value.derivativeX,value.derivativeY,band);
    value.contactIncident.yz=float2(value.derivativeX.z,value.derivativeY.z);
    return value;
}
FftSurface fftMediumSurface(float2 p,float3 lod,FftSwell swell) {return fftMediumFlowSurface(p,lod,swell,false);}
FftSurface fftWindSurface(float2 p,float lod) {
    FftSwell metadata=(FftSwell)0;metadata.mediumDepth=fftMediumDepthField(p);
    return fftMediumFlowSurface(p,float3(0,0,lod),metadata,true);
}
float fftMediumVariance(float2 p,float3 lod) {
    float4 depth=fftMediumDepthField(p);
    float medium=fftMediumBand(depth,12.0).x,fine=fftMediumBand(depth,4.0).x;
    return fftShadingVariance(float3(0,lod.y,0))*medium*medium+fftShadingVariance(float3(0,0,lod.z))*fine*fine;
}
)HLSL");
        replace("    value.unresolvedSlopeVariance=swell.variance;", "    value.mediumBreakFoam=swell.mediumBreakFoam;\n    value.unresolvedSlopeVariance=swell.variance;");
        replace("fftShadingVariance(float3(0,lod.yz))", "fftMediumVariance(p,lod)");
        // fftLocalSurface already has a sampled field and no position argument; supply p for variance.
        replace("FftSurface fftLocalSurface(FftSurface original", "FftSurface fftLocalSurface(float2 p,FftSurface original");
        replace("fftLocalSurface(original,", "fftLocalSurface(p,original,");
        replace("fftLocalSurface(physical,", "fftLocalSurface(p,physical,");
        replace("fftLocalSurface(shading,", "fftLocalSurface(p,shading,");
    }
    // A/B experiment only: fine stays on the global unrotated FFT; no current advection.
    if(SharedEnabled()) {
        const std::string helper=R"HLSL(
FftSurface fftSharedSurface(FftSharedWave sw,float2 p,float4 depth,float lod,bool filtered) {
    float2 q=p;
    float4 c=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);
    float4 cd=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);
    float3 fine=c.xzy*float3(-1.5,-1.5,1)*fftWorldScale;
    float3 fx=float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1),fy=float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    float3 gain=fftMediumGain(depth,4);
    fftMediumScale(fine,fx,fy,gain);
    FftSurface value=(FftSurface)0;
    float h=filtered?sw.filteredHeight:sw.height;float2 slope=filtered?sw.filteredSlope:sw.slope;
    value.displacement=float3(0,0,h)+fine;
    value.derivativeX=float3(0,0,slope.x)+fx;value.derivativeY=float3(0,0,slope.y)+fy;
    value.contactIncident=float3(h,slope);
    // Existing crest foam consumes this steepness response; painted breaking stays zero.
    value.incidentCompression=1-0.65*smoothstep(0.06,0.24,length(sw.slope));
    value.localInfluence=1;
    value.localVariance=sw.variance+fftShadingVariance(float3(0,0,lod))*gain.x*gain.x;
    return value;
}
)HLSL";
        const std::string raw="FftSurface fftRawSurface(float2 p,float3 lod) {";
        source.insert(source.find(raw),helper);
        source.insert(source.find(raw)+raw.size(),R"HLSL(
    float4 sharedDepth=fftMediumDepthField(p);
    if(sharedDepth.w>0.5) {
        float footprint=fftSharedFootprint(lod);
        return fftSharedSurface(fftSharedWave(p,footprint),p,sharedDepth,lod.z,true);
    }
)HLSL");
        const std::string pair="void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {";
        source.insert(source.find(pair)+pair.size(),R"HLSL(
    float4 sharedDepth=fftMediumDepthField(p);
    if(sharedDepth.w>0.5) {
        float footprint=fftSharedFootprint(lod);
        FftSharedWave sw=fftSharedWave(p,footprint);
        physical=fftSharedSurface(sw,p,sharedDepth,0,false);shading=physical;
        if(any(lod>0))shading=fftSharedSurface(sw,p,sharedDepth,lod.z,true);
        return;
    }
)HLSL");
        // Flow and the one-medium wind override are inactive at other elevations.
        const std::string flow="    FftMediumFlow flow=fftMediumFlow(p);FftSurface value=(FftSurface)0;";
        if(!WindRequested())source.insert(source.find(flow),"    if(fftMediumDepthField(p).w<0.5) {if(fine)return fftUnadvectedWindSurface(p,lod.z);return fftUnadvectedMediumSurface(p,lod,swell); }\n");
    }
    // A/B experiment only: mip-zero geometry and filtered shading share the identical composition.
    if(WaterCleanSpectrum::Enabled()) {
        const std::string raw="FftSurface fftRawSurface(float2 p,float3 lod) {";
        source.insert(source.find(raw),WaterCleanShader::PixelSource(configuration));
        source.insert(source.find(raw)+raw.size(),
            "\n    float4 cleanDepth=fftMediumDepthField(p);\n    if(cleanDepth.w>0.5)return fftCleanSurface(p,cleanDepth,lod);\n");
        const std::string pair="void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {";
        source.insert(source.find(pair)+pair.size(),
            "\n    float4 cleanDepth=fftMediumDepthField(p);\n    if(cleanDepth.w>0.5) {fftCleanAndShadingSurface(p,cleanDepth,lod,physical,shading);return;}\n");
        if(!WindRequested()) {
            const std::string flow="    FftMediumFlow flow=fftMediumFlow(p);FftSurface value=(FftSurface)0;";
            source.insert(source.find(flow),"    if(fftMediumDepthField(p).w<0.5) {if(fine)return fftUnadvectedWindSurface(p,lod.z);return fftUnadvectedMediumSurface(p,lod,swell); }\n");
        }
    }
    return source;
}


inline std::string VertexSource(bool bowLift = false, bool bowShoulder = WaterBowShoulder::Enabled(),
    bool textureArray = false, bool waveHeading = false, bool cleanMain = false)
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
    if(cleanMain) {
        const size_t begin=source.find("float3 fftRawDisplacement(float2 p)"),end=source.find("\n}",begin)+3;
        source.replace(begin,end-begin,CleanHeadingSource(true)+WaterCleanShader::VertexSource(true)
            +"float3 fftRawDisplacement(float2 p) {FftCleanContext context=fftCleanContext(p);return "
            +(WaterCleanShore::Enabled()?"fftCleanPreparedDisplacement(p,context.depth,context.tiles)":"fftCleanDisplacement(p,context.depth)")+";}\n");
    }
    if (waveHeading && !cleanMain) {
        const std::string entry = "float3 fftRawDisplacement(float2 p)";
        source.replace(source.find(entry), entry.size(), "float3 fftUnrotatedDisplacement(float2 p)");
        const size_t end = source.find("\n}",
            source.find("float3 fftUnrotatedDisplacement")) + 3;
        source.insert(end, HeadingSource(true) + HeadingVertexSource());
    }
    if(waveHeading && WaterCleanSpectrum::Enabled()) {
        const std::string rawEntry=bowLift?"float3 fftVertexBowDisplacement(float2 p,float level,float horizontalScale,out float bowHeight) {":"float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {";
        const std::string preparedRaw=std::string("float3 fftRawDisplacement(float2 p,FftCleanContext context) {")+(cleanMain?"return ":"if(context.depth.w>.5)return ")
            +std::string(WaterCleanShore::Enabled()?"fftCleanPreparedDisplacement(p,context.depth,context.tiles)":"fftCleanDisplacement(p,context.depth)")
            +(cleanMain?"; }\n":";return fftRawDisplacement(p); }\n");
        source.insert(source.find(rawEntry),preparedRaw);
        const std::string preparedEntry=bowLift?"float3 fftVertexBowDisplacement(float2 p,float level,float horizontalScale,out float bowHeight,FftCleanContext context) {":"float3 fftVertexDisplacement(float2 p,float level,float horizontalScale,FftCleanContext context) {";
        CleanReplace(source,rawEntry,preparedEntry);
        CleanReplace(source,"float3 displacement=fftRawDisplacement(p);","float3 displacement=fftRawDisplacement(p,context);");
        const std::string wrapper=bowLift
            ? "float3 fftVertexBowDisplacement(float2 p,float level,float horizontalScale,out float bowHeight) {return fftVertexBowDisplacement(p,level,horizontalScale,bowHeight,fftCleanContext(p));}\n"
            : "float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {return fftVertexDisplacement(p,level,horizontalScale,fftCleanContext(p));}\n";
        const std::string before=bowLift?"float3 fftVertexDisplacement(float2 p,float level,float horizontalScale) {":"// Compatibility callers without mesh metadata retain their unregistered model.";
        source.insert(source.find(before),wrapper);
    }
    if(textureArray) for(unsigned cascade=0;cascade<3;++cascade) {
        const std::string i=std::to_string(cascade);
        const std::string declaration="Texture2D FftDisplacement"+i;
        source.replace(source.find(declaration),declaration.size(),"Texture2DArray FftDisplacement"+i);
        const std::string coordinate="p/("+std::string(cascade==0?"450.0":cascade==1?"103.0":"13.0")+"*fftWorldScale)";
        const std::string sample="FftDisplacement"+i+".SampleLevel(FftSampler"+i+","+coordinate+",0)";
            for (size_t at = source.find(sample); at != std::string::npos; at = source.find(sample, at + 1))
                source.replace(at, sample.size(),
                    "FftDisplacement" + i + ".SampleLevel(FftSampler" + i
                    + ",float3(" + coordinate + ","
                    + std::to_string(cascade * 2) + "),0)");
            if (waveHeading) {
                const std::string qCoordinate = "q" + coordinate.substr(1);
                const std::string qSample = "FftDisplacement" + i + ".SampleLevel(FftSampler"
                    + i + "," + qCoordinate + ",0)";
                const std::string replacement="FftDisplacement" + i + ".SampleLevel(FftSampler" + i
                    + ",float3(" + qCoordinate + "," + std::to_string(cascade * 2) + "),0)";
                for(size_t at=source.find(qSample);at!=std::string::npos;at=source.find(qSample,at+replacement.size()))
                    source.replace(at,qSample.size(),replacement);
            }
        }
    return source;
}
// Add a water-only wrapper to the generated fixed vertex program. It retains the engine's
// original projection, shroud coordinate generation and fog after moving the actual vertex.
inline bool WrapVertex(std::string & hlsl, bool bowLift = WaterBowLift::Enabled(),
    bool bowShoulder = WaterBowShoulder::Enabled(), bool textureArray = false, bool waveHeading = false, bool cleanMain = false)
{
    const std::string entry="Output main(Input input)";
    const size_t at=hlsl.find(entry);
    if(at==std::string::npos || hlsl.find("float3 Normal")==std::string::npos
        || hlsl.find("float2 TexCoord1")==std::string::npos) return false;
    hlsl.replace(at,entry.size(),"Output waterFixedVertex(Input input)");
    hlsl+=UniformSource("WaterVertexParameters",1);
    hlsl += VertexSource(bowLift, bowShoulder, textureArray, waveHeading, cleanMain);
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
    // A/B experiment only: main-level shared waves replace the analytic seam remainder too.
    if(waveHeading && (SharedEnabled() || WaterCleanSpectrum::Enabled())) {
        const std::string old="float originalHeight=input.Position.z-input.TexCoord1.y;";
        const std::string replacement=old+"\n    if(fftMediumDepthField(parameter).w>0.5)originalHeight=0;";
        hlsl.replace(hlsl.find(old),old.size(),replacement);
    }
    if(waveHeading && WaterCleanSpectrum::Enabled()) {
        CleanReplace(hlsl,"float2 parameter=input.Position.xy;","float2 parameter=input.Position.xy;\n    FftCleanContext cleanContext=fftCleanContext(parameter);");
        CleanReplace(hlsl,"if(fftMediumDepthField(parameter).w>0.5)originalHeight=0;","if(cleanContext.depth.w>0.5)originalHeight=0;");
        CleanReplace(hlsl,"fftVertexBowDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping,bowHeight)","fftVertexBowDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping,bowHeight,cleanContext)");
        CleanReplace(hlsl,"fftVertexDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping)","fftVertexDisplacement(parameter,input.TexCoord1.y,horizontalWeight*damping,cleanContext)");
    }
    if(cleanMain)CleanReplace(hlsl,"if(cleanContext.depth.w>0.5)originalHeight=0;","originalHeight=0;");
    if(waveHeading && WaterCleanSpectrum::Enabled() && WaterCleanProfile::Get().nodepth)
        CleanReplace(hlsl,"sincos(FftParameters.w,","sincos((FftParameters.w-16*step(8,FftParameters.w)),");
    if(waveHeading && WaterCleanSpectrum::Enabled() && WaterCleanProfile::Get().nolong)
        CleanRemoveRead(hlsl,"FftDisplacement0.SampleLevel(");
    return true;
}

inline std::string PixelSource(bool bowLift = false, bool bowShoulder = WaterBowShoulder::Enabled(),
    bool textureArray = false,
    WaterFftSpectrum::Configuration configuration = WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution),
    bool waveHeading = false, bool openSea = false, bool cleanMain = false)
{
    std::string source=CommonSource()+(bowLift?WaterBowLift::Source():std::string())+WaterVesselWaves::Source(true,bowShoulder)+WaterFftFiltering::Source(configuration,waveHeading && WaterCleanSpectrum::Enabled())+R"HLSL(
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
    if(cleanMain) {
        const size_t begin=source.find("FftSurface fftDecodeSamples("),end=source.find("FftSurface fftSurface(float2 p,float level,float footprint",begin);
        source.replace(begin,end-begin,CleanHeadingSource(false)+WaterCleanShader::PixelSource(configuration,true)+R"HLSL(
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    FftCleanContext context=fftCleanContext(p);
)HLSL"+(WaterCleanShore::Enabled()
            ?"fftCleanPreparedAndShadingSurface(p,context.depth,lod,context.tiles,physical,shading);"
            :"fftCleanAndShadingSurface(p,context.depth,lod,physical,shading);")+"\n}\n");
        const std::string surface="struct FftSurface {";
        source.insert(source.find(surface)+surface.size(),"\n    float localInfluence,localVariance,mediumBreakFoam;");
        CleanReplace(source,"fftShadingVariance(shadingLod)*","shading.localVariance*");
        char threshold[160];std::snprintf(threshold,sizeof(threshold),"float3 shadingLod=fftShadingLod(footprint);shadingLod*=step(%.9g,shadingLod);",double(WaterCleanCollapse::ShadingLod));
        CleanReplace(source,"float3 shadingLod=fftShadingLod(footprint);",threshold);
    }
    if (waveHeading && !cleanMain) {
        // Only rename the position entry points, never fftMapSamples / fftDecodeSamples.
        const size_t begin = source.find("FftSurface fftRawSurface(float2 p,float3 lod)");
        const size_t end = source.find("FftSurface fftSurface(float2 p,float level,float footprint", begin);
        std::string raw = source.substr(begin, end - begin);
        for (const auto &names :{std::pair<const char *, const char *>{"fftRawSurface",
            "fftUnrotatedSurface"},
            {"fftRawAndShadingSurface", "fftUnrotatedAndShadingSurface"}})
            for (size_t at = raw.find(names.first); at != std::string::npos; at = raw.find(names.first,
                at + std::strlen(names.second)))
                raw.replace(at, std::strlen(names.first), names.second);
        source.replace(begin, end - begin, raw + HeadingSource(false,
            configuration.resolution, openSea) + HeadingPixelSource(configuration));
        const std::string surface = "struct FftSurface {";
        source.insert(source.find(surface) + surface.size(), "\n    float localInfluence,localVariance;");
        // A/B experiment only: incoming phase-locked swell breaking follows the same evaluator.
        if (OneMediumWindEnabled())
            source.insert(source.find(surface)+surface.size(), "\n    float mediumBreakFoam;");
        const std::string variance = "fftShadingVariance(shadingLod)*";
        source.replace(source.find(variance), variance.size(),
            "(FftParameters.z< -0.5?fftShadingVariance(shadingLod):(shading.localInfluence>0.0?shading.localVariance:(fftShadingVariance(float3(0,shadingLod.yz))+shading.unresolvedSlopeVariance)))*");
        // A/B experiment only: unresolved medium/fine power has the same depth gains.
        if (OneMediumWindEnabled()) {
            const std::string old="fftShadingVariance(float3(0,shadingLod.yz))";
            source.replace(source.find(old),old.size(),"fftMediumVariance(p,shadingLod)");
        }
    }
    if(waveHeading && WaterCleanSpectrum::Enabled()) {
        CleanReplace(source,"FftSurface fftSurface(float2 p,float level,float footprint,float3 horizontalScale) {",
            "FftSurface fftSurface(float2 p,float level,float footprint,float3 horizontalScale,FftCleanContext context) {");
        const std::string preparedCall=WaterCleanShore::Enabled()
            ? "fftCleanPreparedAndShadingSurface(p,context.depth,shadingLod,context.tiles,value,shading)"
            : "fftCleanAndShadingSurface(p,context.depth,shadingLod,value,shading)";
        CleanReplace(source,"    fftRawAndShadingSurface(p,shadingLod,value,shading);",
            (cleanMain?"    "+preparedCall+";":"    if(context.depth.w>.5)"+preparedCall+";else fftRawAndShadingSurface(p,shadingLod,value,shading);"));
        const std::string before="// Compatibility callers without mesh metadata retain their unregistered model.";
        source.insert(source.find(before),"FftSurface fftSurface(float2 p,float level,float footprint,float3 horizontalScale) {return fftSurface(p,level,footprint,horizontalScale,fftCleanContext(p));}\n");
        const auto &profile=WaterCleanProfile::Get();
        if(profile.nodepth)CleanReplace(source,"sincos(FftParameters.w,","sincos((FftParameters.w-16*step(8,FftParameters.w)),");
        if(profile.nolong) {CleanRemoveRead(source,"Texture8.SampleLevel(");CleanRemoveRead(source,"Texture9.SampleLevel(");}
        if(profile.nomips) {
            CleanReplace(source,"float3 shadingLod=fftShadingLod(footprint);","float3 shadingLod=0;");
            for(const char *name:{"value.localVariance=","value.unresolvedSlopeVariance="})
                for(size_t p=source.find(name);p!=std::string::npos;p=source.find(name,p+std::strlen(name)+2)) {size_t e=source.find(';',p);source.replace(p,e-p,std::string(name)+"0");}
        }
    }
    if(waveHeading && WaterCleanSpectrum::Enabled() && !textureArray) {
        CleanReplace(source,"FftHeadingTexture : register(t11)","FftHeadingTexture : register(t14)");
        CleanReplace(source,"FftHeadingSampler : register(s11)","FftHeadingSampler : register(s14)");
        CleanReplace(source,"FftCleanDepth : register(t12)","FftCleanDepth : register(t5)");
        CleanReplace(source,"FftCleanDepthSampler : register(s12)","FftCleanDepthSampler : register(s5)");
        CleanReplace(source,"FftCleanRotation : register(t13)","FftCleanRotation : register(t6)");
        CleanReplace(source,"FftCleanRotationSampler : register(s13)","FftCleanRotationSampler : register(s6)");
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
