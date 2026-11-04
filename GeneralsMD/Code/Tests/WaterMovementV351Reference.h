/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Frozen v3.5.1 (875c96a6c1dd968044b2a3bf967b1a567528fb3f) raw FFT decoder and heading wrappers.
// Test-only oracle: artistic constants and all evaluator operations are retained independently.
#pragma once
#include <string>
#include <cstdio>
namespace WaterMovementV351Reference {
inline std::string CommonSource() {
    char limits[256];
    std::snprintf(limits, sizeof(limits),
        "static const float fftVerticalBound=%.9g;\nstatic const float fftHorizontalBound=%.9g;\nstatic const float fftWorldScale=%.9g;\n",
        6.8785200119018555, 8.0 / 1.4142135623730951, 20.0);
    return std::string(limits) + R"HLSL(
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
    std::snprintf(base, sizeof(base), "static const float2 fftBaseHeading=float2(%.9g,%.9g);\n",
        -0.7071067690849304, -0.7071067690849304);
    return std::string(vertex ? R"HLSL(
Texture2D FftHeadingTexture : register(t4);
SamplerState FftHeadingSampler : register(s4);
)HLSL" : R"HLSL(
Texture2D FftHeadingTexture : register(t11);
SamplerState FftHeadingSampler : register(s11);
)HLSL") + base + R"HLSL(
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

inline std::string PixelSource() {
    return CommonSource() + R"V351(
struct FftSurface { float3 displacement;
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
FftSurface fftUnrotatedSurface(float2 p,float3 lod) {return fftDecodeSamples(fftMapSamples(p,lod));}
FftSurface fftUnrotatedSurface(float2 p) {return fftUnrotatedSurface(p,float3(0,0,0));}
void fftUnrotatedAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
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
)V351" + HeadingSource(false) + HeadingPixelSource();
}
inline std::string VertexSource() {
    return CommonSource() + R"V351(
float3 fftUnrotatedDisplacement(float2 p) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-1.2,-1.2,1)+b.xzy*float3(-1.5,-1.5,1)
        +c.xzy*float3(-1.5,-1.5,1))*fftWorldScale;
})V351" + HeadingSource(true) + HeadingVertexSource();
}
}
