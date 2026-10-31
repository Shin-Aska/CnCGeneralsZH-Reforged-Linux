// Immutable headed shader reference from d1b6a8b5, before local analyzer integration.
// Test-only: compare exact original sea evaluation, including packed inputs and FFT detail.
#pragma once
namespace WaterHeadingLegacyReference {
// Freeze the values used by the original generator too; importing current
// artistic constants would make a changed reference pass accidentally.
constexpr float WorldScale=20.0f,TimeScale=.4f;
constexpr unsigned SwellComponents=4;
constexpr double SwellWavelengths[SwellComponents]={287,431,683,1091};
constexpr double SwellOffsets[SwellComponents]={.37,2.13,4.71,1.19};
constexpr double SwellNoiseSize=400,SwellNoiseAmount=.30,SwellGravity=9.81,SwellHorizontal=-1.2;
constexpr double SwellHashScale=.1031,SwellHashOffset=33.33;
namespace WaterFftSpectrum {
constexpr unsigned Resolution=256;
constexpr double HeightRms[3]={1.5,.75,.18};
constexpr double Sizes[3]={450,103,13};
}
namespace WaterWaveHeading {
constexpr float GradientRange=8;
struct Vector{float x,y;};
constexpr Vector BaseHeading={-.7071067811865475f,-.7071067811865475f};
}
inline std::string HeadingSource(bool vertex,unsigned resolution=WaterFftSpectrum::Resolution) {
    char constants[2048];
    const double noiseVariance=(26.0/35)*(26.0/35)/12;
    std::snprintf(constants,sizeof(constants),
        "static const float fftGradientRange=%.9g;\nstatic const float fftSwellHeight=%.9g;\n"
        "static const float fftSwellTime=%.9g;\nstatic const float fftSwellTexel=%.9g;\n"
        "static const float4 fftSwellWavelengths=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float4 fftSwellOffsets=float4(%.9g,%.9g,%.9g,%.9g);\n"
        "static const float fftSwellNoiseSize=%.9g,fftSwellNoiseAmount=%.9g,fftSwellNoiseNormalization=%.9g;\n"
        "static const float fftSwellGravityScale=%.9g,fftSwellHorizontal=%.9g;\n"
        "static const float fftSwellHashScale=%.9g,fftSwellHashOffset=%.9g;\n",
        double(WaterWaveHeading::GradientRange),WaterFftSpectrum::HeightRms[0]*std::sqrt(2.0/SwellComponents),
        double(TimeScale),double(WorldScale*WaterFftSpectrum::Sizes[0]/resolution),
        SwellWavelengths[0],SwellWavelengths[1],SwellWavelengths[2],SwellWavelengths[3],
        SwellOffsets[0],SwellOffsets[1],SwellOffsets[2],SwellOffsets[3],
        SwellNoiseSize,SwellNoiseAmount,std::sqrt(1+SwellNoiseAmount*SwellNoiseAmount*noiseVariance),
        SwellGravity*WorldScale,SwellHorizontal,SwellHashScale,SwellHashOffset);
    char base[128];
    std::snprintf(base,sizeof(base),"static const float2 fftBaseHeading=float2(%.9g,%.9g);\n",
        double(WaterWaveHeading::BaseHeading.x),double(WaterWaveHeading::BaseHeading.y));
    return std::string(vertex?R"HLSL(
Texture2D FftHeadingTexture : register(t4);
SamplerState FftHeadingSampler : register(s4);
)HLSL":R"HLSL(
Texture2D FftHeadingTexture : register(t11);
SamplerState FftHeadingSampler : register(s11);
)HLSL")+constants+base+R"HLSL(
float2 fftRotate(float2 p,float2 cs) {return float2(cs.x*p.x-cs.y*p.y,cs.y*p.x+cs.x*p.y);}
float3 fftRotateVector(float3 v,float2 cs) {return float3(fftRotate(v.xy,cs),v.z);}
struct FftSwell {float3 displacement,derivativeX,derivativeY,shadingDisplacement,shadingDerivativeX,shadingDerivativeY;float variance;
    float2 rotation0,rotation1,weights;};
float4 fftSwellField(float2 p) {
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    float2 cell=clamp((p+TerrainParameters.xx)*(width/2)/max(TerrainParameters.y,1.0),0.0,float2(width/2-1,height-1));
    int2 lo=int2(floor(cell)),hi=min(lo+1,int2(width/2-1,height-1));
    float2 f=frac(cell),texel=1.0/float2(width,height);float4 value=0;float weight=0;
    // The sampler is point-filtered and these are texel centres, so each read is one exact packed
    // texel. Sampling, not Load, keeps the image paired with its sampler for ShaderCross.
    [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
        int2 at=int2(x?hi.x:lo.x,y?hi.y:lo.y);
        float2 centre=(float2(at.x*2,at.y)+0.5)*texel;
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
    // The FFT cascades, the waves actually seen moving at game scale, follow the refracted
    // direction in 45-degree sectors. The field turns gradually and never flips across a channel,
    // so the pairwise sector cross-fade stays spread out. Unreached water keeps the prevailing wind.
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
)HLSL";
}
inline std::string HeadingVertexSource() {
    return R"HLSL(
// The texture-array rewrite expects each cascade sampled once, at q.
float3 fftChopDisplacement(float2 p,float2 r) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
    return fftRotateVector((b.xzy+c.xzy)*float3(-1.5,-1.5,1)*fftWorldScale,r);
}
float3 fftRawDisplacement(float2 p) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedDisplacement(p);
    FftSwell swell=fftSwell(p);float3 result=swell.displacement;
    [branch] if(swell.weights.x>0.0) result+=swell.weights.x*fftChopDisplacement(p,swell.rotation0);
    [branch] if(swell.weights.y>0.0) result+=swell.weights.y*fftChopDisplacement(p,swell.rotation1);
    return result;
}
)HLSL";
}
inline std::string HeadingPixelSource() {
    return R"HLSL(
// Both FFT cascades at one sector rotation, accumulated with its cross-fade weight. The medium
// cascade also feeds ambient breaking and contact wash, kept apart in `medium`.
void fftAccumulateChop(inout FftSurface medium,inout FftSurface fine,float2 p,float3 lod,float2 r,float w) {
    float2 q=fftRotate(p,r*float2(1,-1));
    float4 b=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod.y);
    float4 bd=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod.y);
    float4 c=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod.z);
    float4 cd=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod.z);
    float3 x=float3(bd.z,b.w,bd.x)*float3(-1.5,-1.5,1),y=float3(b.w,bd.w,bd.y)*float3(-1.5,-1.5,1);
    medium.displacement+=w*fftRotateVector(b.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    // Chain rule for q=R(-a)p, then R(a) on displacement XY.
    medium.derivativeX+=w*fftRotateVector(r.x*x-r.y*y,r);
    medium.derivativeY+=w*fftRotateVector(r.y*x+r.x*y,r);
    medium.contactIncident.yz+=w*fftRotate(bd.xy,r);
    x=float3(cd.z,c.w,cd.x)*float3(-1.5,-1.5,1);y=float3(c.w,cd.w,cd.y)*float3(-1.5,-1.5,1);
    fine.displacement+=w*fftRotateVector(c.xzy*float3(-1.5,-1.5,1)*fftWorldScale,r);
    fine.derivativeX+=w*fftRotateVector(r.x*x-r.y*y,r);
    fine.derivativeY+=w*fftRotateVector(r.y*x+r.x*y,r);
}
FftSurface fftSwellSurface(float2 p,float3 lod,FftSwell swell) {
    FftSurface medium=(FftSurface)0,fine=(FftSurface)0;
    [branch] if(swell.weights.x>0.0) fftAccumulateChop(medium,fine,p,lod,swell.rotation0,swell.weights.x);
    [branch] if(swell.weights.y>0.0) fftAccumulateChop(medium,fine,p,lod,swell.rotation1,swell.weights.y);
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
FftSurface fftRawSurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z<=0.5) return fftUnrotatedSurface(p,lod);
    return fftSwellSurface(p,lod,fftFilteredSwell(fftSwell(p,lod.x)));
}
FftSurface fftRawSurface(float2 p) {return fftRawSurface(p,float3(0,0,0));}
void fftRawAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z<=0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftSwell swell=fftSwell(p,lod.x);
    physical=fftSwellSurface(p,float3(0,0,0),swell);shading=physical;
    [branch] if(any(lod>0.0)) shading=fftSwellSurface(p,lod,fftFilteredSwell(swell));
}
)HLSL";
}
}

namespace WaterLocalWaveTest {
inline void ReplaceAll(std::string &s,const std::string &from,const std::string &to) {
    for(size_t at=s.find(from);at!=std::string::npos;at=s.find(from,at+to.size()))s.replace(at,from.size(),to);
}
inline std::string ReferenceNames(const std::string &source) {
    std::string result;
    for(size_t at=0;at<source.size();) {
        const unsigned char c=source[at];
        if((c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_') {
            size_t end=at+1;
            while(end<source.size() && ((source[end]>='a' && source[end]<='z') || (source[end]>='A' && source[end]<='Z')
                || (source[end]>='0' && source[end]<='9') || source[end]=='_'))++end;
            const std::string name=source.substr(at,end-at);
            if(name.compare(0,3,"fft")==0 || name=="FftSwell" || name=="FftHeadingTexture" || name=="FftHeadingSampler")result+="reference";
            result+=name;at=end;
        } else result+=source[at++];
    }
    return result;
}
inline std::string PixelSource(unsigned rows,unsigned resolution=256) {
    std::string source="struct Query{float4 samplePoint;};cbuffer QueryBlock:register(b0,space3){Query Queries["+std::to_string(rows)+"];};\n";
    source+=R"HLSL(
static float4 WaterParameters,FftParameters;
static const float4 TerrainParameters=float4(0,80,0,0);
static const float4 FoamFillLights[4]={float4(0,0,0,0),float4(0,0,0,0),float4(0,0,0,0),float4(0,0,0,.125)};
struct FftSurface {
    float3 displacement,derivativeX,derivativeY,contactIncident;
    float incidentCompression,unresolvedSlopeVariance,localInfluence,localVariance;
};
FftSurface fftUnrotatedSurface(float2 p,float3 lod) {return (FftSurface)0;}
void fftUnrotatedAndShadingSurface(float2 p,float3 lod,out FftSurface a,out FftSurface b) {a=(FftSurface)0;b=a;}
float3 fftUnrotatedDisplacement(float2 p) {return 0;}
FftSurface referencefftUnrotatedSurface(float2 p,float3 lod) {return (FftSurface)0;}
void referencefftUnrotatedAndShadingSurface(float2 p,float3 lod,out FftSurface a,out FftSurface b) {a=(FftSurface)0;b=a;}
float3 referencefftUnrotatedDisplacement(float2 p) {return 0;}
// Analytic FFT maps are independent of the supplied derivative channels and
// nonzero in every cascade. The unchanged reference and production paths see
// exactly the same maps, time, filtering, phases, amplitudes and rotations.
float4 fixtureMap(float2 uv,float lod,float period,bool derivative) {
    float2 k=float2(.043,.027);float phase=dot(uv*period*20,k)+WaterParameters.x*.23;
    float sn,cs;sincos(phase,sn,cs);float gain=exp2(-.3*lod);
    float2 q=k*20;
    if(derivative)return gain*float4(-.04*q*sn,.03*q*q*cs);
    return gain*float4(.03*q.x*sn,.04*cs,.03*q.y*sn,.03*q.x*q.y*cs);
}
float4 fixtureMap1(float2 uv,float lod) {return fixtureMap(uv,lod,103,false);}
float4 fixtureMap1D(float2 uv,float lod) {return fixtureMap(uv,lod,103,true);}
float4 fixtureMap2(float2 uv,float lod) {return fixtureMap(uv,lod,13,false);}
float4 fixtureMap2D(float2 uv,float lod) {return fixtureMap(uv,lod,13,true);}
)HLSL";
    source+=WaterFftSampling::CommonSource()+WaterFftFiltering::Source(WaterFftSpectrum::ForResolution(resolution));
    std::string current=WaterFftSampling::HeadingSource(false,resolution)+WaterFftSampling::HeadingPixelSource()+WaterFftSampling::HeadingVertexSource();
    ReplaceAll(current,"register(t11)","register(t0,space2)");ReplaceAll(current,"register(s11)","register(s0,space2)");
    std::string reference=WaterHeadingLegacyReference::HeadingSource(false,resolution)
        +WaterHeadingLegacyReference::HeadingPixelSource()+WaterHeadingLegacyReference::HeadingVertexSource();
    ReplaceAll(reference,"register(t11)","register(t1,space2)");ReplaceAll(reference,"register(s11)","register(s1,space2)");
    source+="static const float referencefftWorldScale=fftWorldScale,referencefftVerticalBound=fftVerticalBound;\n"
        "float3 referencefftLimitDerivative(float3 d){return fftLimitDerivative(d);}\n";
    std::string body=current+ReferenceNames(reference);
    for(const auto &item:{std::pair<const char*,const char*>{"Texture10.SampleLevel(Sampler10,","fixtureMap1("},
        {"Texture11.SampleLevel(Sampler11,","fixtureMap1D("},{"Texture12.SampleLevel(Sampler12,","fixtureMap2("},
        {"Texture13.SampleLevel(Sampler13,","fixtureMap2D("},{"FftDisplacement1.SampleLevel(FftSampler1,","fixtureMap1("},
        {"FftDisplacement2.SampleLevel(FftSampler2,","fixtureMap2("}})ReplaceAll(body,item.first,item.second);
    source+=body;
    source+=R"HLSL(
// Independent closed-form answer for the sinusoidal fixture. Rotate its world
// wavevector instead of sampling/decoding maps or copying the production chain rule.
FftSurface fixtureFineOracle(float2 p,float lod,float2 rotation) {
    float2 k=float2(rotation.x*.043-rotation.y*.027,rotation.y*.043+rotation.x*.027);
    float phase=dot(k,p)+WaterParameters.x*.23,sn,cs;sincos(phase,sn,cs);
    float gain=exp2(-.3*lod);FftSurface value=(FftSurface)0;
    value.displacement=gain*float3(-18*k*sn,.8*cs);
    value.derivativeX=gain*float3(-18*k*k.x*cs,-.8*k.x*sn);
    value.derivativeY=gain*float3(-18*k*k.y*cs,-.8*k.y*sn);
    return value;
}
FftSurface fixtureFineCorrection(float2 p,float lod,referenceFftSwell old) {
    float2 wind;sincos(FftParameters.w,wind.y,wind.x);
    FftSurface expected=fixtureFineOracle(p,lod,wind);
    FftSurface first=fixtureFineOracle(p,lod,old.rotation0),second=fixtureFineOracle(p,lod,old.rotation1);
    expected.displacement-=old.weights.x*first.displacement+old.weights.y*second.displacement;
    expected.derivativeX-=old.weights.x*first.derivativeX+old.weights.y*second.derivativeX;
    expected.derivativeY-=old.weights.x*first.derivativeY+old.weights.y*second.derivativeY;
    return expected;
}
// Remove the immutable reference medium response from a complete production
// surface. Two unrelated coastal sector fields must leave the same fine layer.
FftSurface fixtureIsolatedFine(float2 p,float lod,bool alternate,out FftSurface oldFine) {
    FftSwell coast=(FftSwell)0;
    float angle=alternate?1.4:-1.1;
    sincos(angle,coast.rotation0.y,coast.rotation0.x);
    sincos(angle+.7853981633974483,coast.rotation1.y,coast.rotation1.x);
    coast.weights=alternate?float2(.6,.8):float2(.8,.6);
    FftSurface actual=fftSwellSurface(p,float3(lod,lod,lod),coast),medium=(FftSurface)0;
    oldFine=(FftSurface)0;
    referencefftAccumulateChop(medium,oldFine,p,float3(lod,lod,lod),coast.rotation0,coast.weights.x);
    referencefftAccumulateChop(medium,oldFine,p,float3(lod,lod,lod),coast.rotation1,coast.weights.y);
    actual.displacement-=medium.displacement;
    actual.derivativeX-=medium.derivativeX;actual.derivativeY-=medium.derivativeY;
    return actual;
}
float3 fixtureAnalyticWave(float2 p) {
    float phase=.2*p.x-.3*p.y+WaterParameters.x*.7;float sn,cs;sincos(phase,sn,cs);
    return float3(sn,cs*float2(.2,-.3));
}
float3 fixtureFallbackBlend(float2 p) {
    float3 gain=fftLocalResidualGain(p);
    float3 legacy=fftLocalAnalyticWave(fixtureAnalyticWave(p),gain);
    // The ocean fixture is an affine scalar field, so its supplied slope and
    // independent finite-difference height describe the same surface.
    float3 ocean=fftLocalAnalyticWave(float3(.4+.05*p.x-.03*p.y,.05,-.03),gain);
    float offshore=.3+.004*p.y;
    float3 wave=lerp(legacy,ocean,offshore);
    wave.yz+=(ocean.x-legacy.x)*float2(0,.004);return wave;
}
float2 fixtureProjected(float2 p,out float2 vx,out float2 vy) {
    float2 v=float2(-.02+.0003*p.x,.01-.0002*p.y);
    vx=float2(.0003,0);vy=float2(0,-.0002);
    fftStreamProject(v,vx,vy,float2(.6+.002*p.x,.2+.001*p.y),float2(.002,0),float2(0,.001));
    return v;
}
float4 main(float4 position:SV_Position):SV_Target0 {
    int column=int(position.x),row=int(position.y);float4 query=Queries[row].samplePoint;
    float2 p=query.xy;float lod=query.w;WaterParameters=float4(query.z,0,0,0);FftParameters=float4(1,0,1.125,.3);
    FftSurface a,b,ra,rb;float3 levels=float3(lod,lod,lod);
    fftRawAndShadingSurface(p,levels,a,b);referencefftRawAndShadingSurface(p,levels,ra,rb);
    float3 originalDelta=a.displacement-ra.displacement;
    float originalVertexDelta=length(fftRawDisplacement(p)-referencefftRawDisplacement(p));
    referenceFftSwell originalSwell=referencefftSwell(p,lod);
    FftSurface correction=fixtureFineCorrection(p,0,originalSwell),filteredCorrection=fixtureFineCorrection(p,lod,originalSwell);
    ra.displacement+=correction.displacement;ra.derivativeX+=correction.derivativeX;ra.derivativeY+=correction.derivativeY;
    rb.displacement+=filteredCorrection.displacement;rb.derivativeX+=filteredCorrection.derivativeX;rb.derivativeY+=filteredCorrection.derivativeY;
    FftLocalFields f=fftLocalFields(p);FftLocalWaves waves=fftLocalWaves(p,f,lod);
    float3 expectedVertex=referencefftRawDisplacement(p)+correction.displacement;
    // Explicit product rule retains the existing local residual gain and stream layer.
    if(f.influence.x>0.0) {
        expectedVertex=f.legacyGain.x*expectedVertex+waves.displacement;
        ra.derivativeX=ra.derivativeX*f.legacyGain.x+ra.displacement*f.legacyGain.y+waves.derivativeX;
        ra.derivativeY=ra.derivativeY*f.legacyGain.x+ra.displacement*f.legacyGain.z+waves.derivativeY;
        rb.derivativeX=rb.derivativeX*f.legacyGain.x+rb.displacement*f.legacyGain.y+waves.shadingDerivativeX;
        rb.derivativeY=rb.derivativeY*f.legacyGain.x+rb.displacement*f.legacyGain.z+waves.shadingDerivativeY;
        ra.displacement=ra.displacement*f.legacyGain.x+waves.displacement;
        rb.displacement=rb.displacement*f.legacyGain.x+waves.shadingDisplacement;
        ra.contactIncident=float3(ra.contactIncident.x*f.legacyGain.x,ra.contactIncident.yz*f.legacyGain.x+ra.contactIncident.x*f.legacyGain.yz);
        ra.incidentCompression=1+f.legacyGain.x*(ra.incidentCompression-1);
    }
    if(column==0)return float4(a.displacement-ra.displacement,0);
    if(column==1)return float4(a.derivativeX-ra.derivativeX,0);
    if(column==2)return float4(a.derivativeY-ra.derivativeY,0);
    if(column==3)return float4(b.displacement-rb.displacement,0);
    if(column==4)return float4(b.derivativeX-rb.derivativeX,0);
    if(column==5)return float4(b.derivativeY-rb.derivativeY,0);
    if(column==6)return float4(a.contactIncident-ra.contactIncident,a.incidentCompression-ra.incidentCompression);
    if(column==7)return float4(a.displacement,a.localInfluence);
    const float epsilon=.01;float2 dx=float2(epsilon,0),dy=float2(0,epsilon);
    FftLocalWaves xp=fftLocalWaves(p+dx,fftLocalFields(p+dx),lod),xm=fftLocalWaves(p-dx,fftLocalFields(p-dx),lod);
    FftLocalWaves yp=fftLocalWaves(p+dy,fftLocalFields(p+dy),lod),ym=fftLocalWaves(p-dy,fftLocalFields(p-dy),lod);
    if(column==8)return float4(waves.displacement.z,waves.derivativeX.z,waves.derivativeY.z,waves.variance);
    if(column==9)return float4((xp.displacement.z-xm.displacement.z)/(2*epsilon),(yp.displacement.z-ym.displacement.z)/(2*epsilon),0,0);
    if(column==10)return float4(waves.shadingDisplacement.z,waves.shadingDerivativeX.z,waves.shadingDerivativeY.z,0);
    if(column==11)return float4((xp.shadingDisplacement.z-xm.shadingDisplacement.z)/(2*epsilon),(yp.shadingDisplacement.z-ym.shadingDisplacement.z)/(2*epsilon),0,0);
    if(column==12)return float4(f.legacyGain,f.influence.x);
    if(column==13) {
        FftLocalFields fxp=fftLocalFields(p+dx),fxm=fftLocalFields(p-dx),fyp=fftLocalFields(p+dy),fym=fftLocalFields(p-dy);
        return float4((fxp.legacyGain.x-fxm.legacyGain.x)/(2*epsilon),(fyp.legacyGain.x-fym.legacyGain.x)/(2*epsilon),0,0);
    }
    // Evaluate each independent arrival with the original total energy/motion budget.
    FftLocalWaves w1=(FftLocalWaves)0,w2=(FftLocalWaves)0;
    float budget=f.first.amplitude+f.second.amplitude;
    float2 budgetGradient=f.first.amplitudeGradient+f.second.amplitudeGradient;
    float gradientBudget=length(f.first.amplitudeGradient)+length(f.second.amplitudeGradient);
    float footprint2=max(0.0,exp2(2*lod)-1)*fftLocalTexel*fftLocalTexel/24.0;
    float3 clock=fftStreamNoise(p/83.0+float2(11.7,-6.3));clock.yz/=83.0;
    fftLocalArrival(w1,p,f.first,f.shore,f.shoreX,f.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    fftLocalArrival(w2,p,f.second,f.shore,f.shoreX,f.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    if(column==14)return float4(waves.displacement-w1.displacement-w2.displacement,waves.derivativeX.z-w1.derivativeX.z-w2.derivativeX.z);
    if(column==15)return float4(waves.derivativeY.z-w1.derivativeY.z-w2.derivativeY.z,0,0,0);
    if(column==16) {
        FftLocalFields average=f;float total=f.first.amplitude+f.second.amplitude;
        average.first.phase=(f.first.phase*f.first.amplitude+f.second.phase*f.second.amplitude)/max(total,.00001);
        average.first.gradient=(f.first.gradient*f.first.amplitude+f.second.gradient*f.second.amplitude)/max(total,.00001);
        average.first.amplitude=total;average.second.amplitude=0;average.second.amplitudeGradient=0;
        FftLocalWaves wrong=fftLocalWaves(p,average,lod);return float4(waves.displacement-wrong.displacement,0);
    }
    if(column==17)return float4(fftRawDisplacement(p)-expectedVertex,0);
    if(column==18)return float4(a.displacement-fftRawDisplacement(p),0);
    if(column==19)return float4(b.localVariance,f.first.amplitude,f.second.amplitude,f.influence.x);
    if(column==20)return float4(f.first.phase,f.first.gradient,f.first.amplitude);
    if(column==21)return float4(f.second.phase,f.second.gradient,f.second.amplitude);
    if(column==22) {
        FftSurface original=(FftSurface)0;original.displacement=float3(.7,-.4,1.1);original.contactIncident=float3(.3,.2,-.1);original.incidentCompression=.7;
        FftSurface local=fftLocalSurface(original,f,waves,levels,false);
        return float4(local.derivativeX.z,local.derivativeY.z,local.displacement.z,local.incidentCompression);
    }
    if(column==24)return float4(fftLocalResidualGain(p),0);
    if(column==25)return float4(fftLocalAnalyticWave(fixtureAnalyticWave(p),fftLocalResidualGain(p)),0);
    if(column==26) {
        float3 xp=fftLocalAnalyticWave(fixtureAnalyticWave(p+dx),fftLocalResidualGain(p+dx));
        float3 xm=fftLocalAnalyticWave(fixtureAnalyticWave(p-dx),fftLocalResidualGain(p-dx));
        float3 yp=fftLocalAnalyticWave(fixtureAnalyticWave(p+dy),fftLocalResidualGain(p+dy));
        float3 ym=fftLocalAnalyticWave(fixtureAnalyticWave(p-dy),fftLocalResidualGain(p-dy));
        return float4((xp.x-xm.x)/(2*epsilon),(yp.x-ym.x)/(2*epsilon),0,0);
    }
    if(column==27)return float4(fftLocalAnalyticWave(fixtureAnalyticWave(p),float3(1,0,0))-fixtureAnalyticWave(p),0);
    if(column==28)return float4(fixtureFallbackBlend(p),0);
    if(column==29)return float4((fixtureFallbackBlend(p+dx).x-fixtureFallbackBlend(p-dx).x)/(2*epsilon),
        (fixtureFallbackBlend(p+dy).x-fixtureFallbackBlend(p-dy).x)/(2*epsilon),0,0);
    if(column==30) {
        float2 q=p/9.0,v=float2(.06,-.035);float3 clock=0;
        float3 before=fftStreamAdvected(q,v,0,0,12-.001,clock),after=fftStreamAdvected(q,v,0,0,12+.001,clock);
        float3 beforeHalf=fftStreamAdvected(q,v,0,0,6-.001,clock),afterHalf=fftStreamAdvected(q,v,0,0,6+.001,clock);
        return float4(length(before-after),length(beforeHalf-afterHalf),0,0);
    }
    if(column==31) {
        float3 initial=fftStreamAdvected(p/9.0,0,0,0,0,0);
        return float4(fftStreamAdvected(p/9.0,0,0,0,query.z+12.001,0)-initial,0);
    }
    if(column==32) {
        FftLocalFields shifted=f;shifted.first.phase+=1234;shifted.second.phase-=5678;
        FftLocalWaves same=fftLocalWaves(p,shifted,lod);
        return float4(waves.displacement-same.displacement,waves.derivativeX.z-same.derivativeX.z);
    }
    if(column==33) {
        float2 q=p/9.0,v=float2(.08,.04);float dt=.02;
        float next=fftStreamAdvected(q,v,0,0,6.0+dt,0).x;
        float correct=fftStreamAdvected(q-v*dt,v,0,0,6.0,0).x;
        float against=fftStreamAdvected(q+v*dt,v,0,0,6.0,0).x;
        float across=fftStreamAdvected(q+fftStreamQuarterTurn(v)*dt,v,0,0,6.0,0).x;
        return float4(abs(next-correct),abs(next-against),abs(next-across),abs(next-fftStreamAdvected(q,v,0,0,6.0,0).x));
    }
    if(column==34) {
        float4 errors=0;float2 v=float2(.08,.04);float dt=.02;
        [loop] for(int phase=0;phase<12;++phase) [loop] for(int sample=0;sample<8;++sample) {
            float2 q=p/9.0+float2(sample*2.371,sample*1.879);float time=.25+phase;
            float next=fftStreamAdvected(q,v,0,0,time+dt,0).x;
            float correct=fftStreamAdvected(q-v*dt,v,0,0,time,0).x;
            float against=fftStreamAdvected(q+v*dt,v,0,0,time,0).x;
            float across=fftStreamAdvected(q+fftStreamQuarterTurn(v)*dt,v,0,0,time,0).x;
            errors+=float4(abs(next-correct),abs(next-against),abs(next-across),abs(next-fftStreamAdvected(q,v,0,0,time,0).x));
        }
        return errors/96.0;
    }
    if(column==35)return float4(f.shore,length(f.shore),f.influence.x);
    if(column==36) {
        // The bank changes on a 10-world grid. This still-small stencil keeps
        // its quantized float interpolation above subtraction roundoff.
        const float bankEpsilon=.04;float2 bx=float2(bankEpsilon,0),by=float2(0,bankEpsilon);
        FftLocalFields fxp=fftLocalFields(p+bx),fxm=fftLocalFields(p-bx),fyp=fftLocalFields(p+by),fym=fftLocalFields(p-by);
        return float4(f.shoreX-(fxp.shore-fxm.shore)/(2*bankEpsilon),f.shoreY-(fyp.shore-fym.shore)/(2*bankEpsilon));
    }
    if(column==37) {
        float2 incoming=float2(-.8,.35),outgoing=float2(.8,.35),vx=0,vy=0;
        fftStreamProject(incoming,vx,vy,float2(1,0),0,0);
        fftStreamProject(outgoing,vx,vy,float2(1,0),0,0);
        return float4(incoming,outgoing);
    }
    if(column==38) {
        float2 tangent=float2(0,.5),still=float2(-.6,.3),vx=0,vy=0;
        fftStreamProject(tangent,vx,vy,float2(1,0),0,0);fftStreamProject(still,vx,vy,0,0,0);
        return float4(tangent-float2(0,.5),still-float2(-.6,.3));
    }
    if(column==39) {
        float2 partial=float2(-.8,.35),vx=0,vy=0;
        fftStreamProject(partial,vx,vy,float2(.5,0),0,0);
        return float4(partial-float2(-.6,.35),length(partial),0);
    }
    if(column==40) {
        float2 vx,vy,junkX,junkY;fixtureProjected(p,vx,vy);
        float2 numericalX=(fixtureProjected(p+dx,junkX,junkY)-fixtureProjected(p-dx,junkX,junkY))/(2*epsilon);
        float2 numericalY=(fixtureProjected(p+dy,junkX,junkY)-fixtureProjected(p-dy,junkX,junkY))/(2*epsilon);
        return float4(vx-numericalX,vy-numericalY);
    }
    if(column==41) {
        float2 routeA=float2(-.8,.35),routeB=float2(.8,-.35),vx=0,vy=0;
        fftStreamProject(routeA,vx,vy,float2(1,0),0,0);fftStreamProject(routeB,vx,vy,float2(1,0),0,0);
        return float4(routeA-float2(0,.35),routeB-float2(.8,-.35));
    }
    if(column>=42 && column<=51) {
        FftSurface oldFirst,oldSecond;
        FftSurface first=fixtureIsolatedFine(p,lod,false,oldFirst),second=fixtureIsolatedFine(p,lod,true,oldSecond);
        float2 wind;sincos(FftParameters.w,wind.y,wind.x);
        FftSurface expected=fixtureFineOracle(p,lod,wind);
        if(column==51)return float4(first.displacement-second.displacement,length(oldFirst.displacement-oldSecond.displacement));
        FftSurface actual=column<45?first:second;
        float armed=0;
        if(column>=48) {
            FftParameters.w=1.2;actual=fixtureIsolatedFine(p,lod,true,oldSecond);
            sincos(FftParameters.w,wind.y,wind.x);expected=fixtureFineOracle(p,lod,wind);
            armed=length(actual.displacement-second.displacement);
        }
        int component=(column-42)%3;
        return float4(component==0?actual.displacement-expected.displacement:component==1?actual.derivativeX-expected.derivativeX:actual.derivativeY-expected.derivativeY,armed);
    }
    if(column==52)return float4(originalDelta,originalVertexDelta);
    if(column==53) {FftSurface fine=fftWindSurface(p,lod);return float4(fine.contactIncident,fine.incidentCompression);}
    if(column==54)return float4(fftWindDisplacement(p)-fftWindSurface(p,0).displacement,0);
    if(column>=55 && column<=63) {
        // Compare per-cascade cache reuse against the immutable evaluator,
        // including independently reconstructed wind rotation. Each LOD axis
        // is isolated so an unrelated cascade cannot silently be filtered.
        int group=(column-55)/3,component=(column-55)%3;
        float3 mixed=group==0?float3(0,0,lod):group==1?float3(0,lod,0):float3(lod,0,0);
        FftSurface actualPhysical,actualShading,oldPhysical,oldShading;
        fftLegacyAndShadingSurface(p,mixed,actualPhysical,actualShading);
        referencefftRawAndShadingSurface(p,mixed,oldPhysical,oldShading);
        referenceFftSwell oldSwell=referencefftSwell(p,mixed.x);
        FftSurface dc=fixtureFineCorrection(p,0,oldSwell),sc=fixtureFineCorrection(p,mixed.z,oldSwell);
        oldPhysical.displacement+=dc.displacement;oldPhysical.derivativeX+=dc.derivativeX;oldPhysical.derivativeY+=dc.derivativeY;
        oldShading.displacement+=sc.displacement;oldShading.derivativeX+=sc.derivativeX;oldShading.derivativeY+=sc.derivativeY;
        float physicalError=length(actualPhysical.displacement-oldPhysical.displacement)
            +length(actualPhysical.derivativeX-oldPhysical.derivativeX)+length(actualPhysical.derivativeY-oldPhysical.derivativeY);
        return float4(component==0?actualShading.displacement-oldShading.displacement:
            component==1?actualShading.derivativeX-oldShading.derivativeX:actualShading.derivativeY-oldShading.derivativeY,physicalError);
    }
    if(column>=64 && column<=75) {
        // Full visible-surface temporal regression: shelter keeps the committed
        // .06/.12 residual wind budget even against or across the current.
        // The oracle includes evolving FFT wind AND current, rather than
        // asserting that a composite surface must advect as current-only noise.
        int group=(column-64)/3,component=(column-64)%3;
        float gain=group<2?.06:.12;
        float2 wind;sincos(FftParameters.w,wind.y,wind.x);
        FftLocalFields route=(FftLocalFields)0;
        route.influence=float3(1,0,0);route.legacyGain=float3(gain,0,0);
        float2 fixtureTravel=-normalize(float2(wind.x*.043-wind.y*.027,wind.y*.043+wind.x*.027));
        route.first.gradient=group%2==0?-fixtureTravel:fftStreamQuarterTurn(fixtureTravel);
        route.first.amplitude=.7;
        float3 actual[2],expected[2];float windMotion=0,currentMotion=0;
        float3 previousFine=0,previousCurrent=0;
        [unroll] for(int sample=0;sample<2;++sample) {
            WaterParameters.x=query.z+sample*1.3;
            FftLocalWaves current=fftLocalWaves(p,route,lod);
            FftSurface original=fftLegacySurface(p,levels);
            FftSurface visible=fftLocalSurface(original,route,current,levels,true);
            FftSurface old=referencefftRawSurface(p,levels);
            referenceFftSwell oldSwell=referencefftSwell(p,lod);
            FftSurface windCorrection=fixtureFineCorrection(p,lod,oldSwell);
            float3 oldValue=component==0?old.displacement:component==1?old.derivativeX:old.derivativeY;
            float3 correctedWind=oldValue+(component==0?windCorrection.displacement:component==1?windCorrection.derivativeX:windCorrection.derivativeY);
            float3 currentValue=component==0?current.shadingDisplacement:component==1?current.shadingDerivativeX:current.shadingDerivativeY;
            actual[sample]=component==0?visible.displacement:component==1?visible.derivativeX:visible.derivativeY;
            expected[sample]=gain*correctedWind+currentValue;
            FftSurface fine=fixtureFineOracle(p,lod,wind);
            float3 fineValue=component==0?fine.displacement:component==1?fine.derivativeX:fine.derivativeY;
            if(sample==1) {windMotion=length(gain*(fineValue-previousFine));currentMotion=length(currentValue-previousCurrent);}
            previousFine=fineValue;previousCurrent=currentValue;
        }
        float3 actualMotion=actual[1]-actual[0],expectedMotion=expected[1]-expected[0];
        // Check both full endpoints as well as temporal change. A static
        // extra fine layer would cancel from the latter but still fail here.
        float3 endpointError=max(abs(actual[0]-expected[0]),abs(actual[1]-expected[1]));
        return float4(max(endpointError,abs(actualMotion-expectedMotion)),min(windMotion,currentMotion));
    }
    float variance=b.localInfluence>0.0?b.localVariance:(fftShadingVariance(float3(0,levels.yz))+b.unresolvedSlopeVariance);
    float referenceVariance=fftShadingVariance(float3(0,levels.yz))+rb.unresolvedSlopeVariance;
    return float4(variance-referenceVariance,b.unresolvedSlopeVariance-rb.unresolvedSlopeVariance,0,0);
}
)HLSL";
    return source;
}
}
