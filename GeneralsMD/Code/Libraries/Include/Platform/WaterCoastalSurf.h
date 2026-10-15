/* Bounded render-only coastal source/history core. GPU transport is a separate owner. */
#pragma once
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterShoreHistory.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace WaterCoastalSurf {
constexpr unsigned AtlasSide=256,MaxNodes=AtlasSide*AtlasSide,MaxLevels=8,MaxViewSide=512;
constexpr double SourceTick=.1,MaximumSourceInterval=.25,Lifetime=5,Diffusivity=2;
constexpr float MaximumRate=1.2f,PhysicalCoastFull=96,PhysicalCoastEnd=128;
// Coordinates/heights are game world units. dt is elapsed modern-water seconds, not
// FFT time (the native maps already contain the WaterFftSampling::TimeScale phase).
// The field excludes fine ripples and generated vessel waves. It is a component
// of the existing geometry, not an additional independent surf oscillator.
struct Field {float height=0,dx=0,dy=0,compression=1;};
struct Context {
    float depth=0,distance=0,outwardX=0,outwardY=0;
    float exposure=0,offshore=0,analyticDamping=0,spectralDamping=0;
    float wet=0,admitted=0;
};
// Four float channels on the GPU. rate=-1 means no valid previous source sample.
// History itself stays in the fixed mean-water parameter/world chart. Sampling
// it at the corresponding parameter makes it follow the actual horizontal chop.
struct State {float density=0,ageMoment=0,height=0,rate=-1;};
inline float Clamp(float x,float a,float b){return (std::max)(a,(std::min)(b,x));}
inline float Smooth(float a,float b,float x){const float t=Clamp((x-a)/(b-a),0,1);return t*t*(3-2*t);}
inline bool Finite(const Field &f){return std::isfinite(f.height)&&std::isfinite(f.dx)&&std::isfinite(f.dy)&&std::isfinite(f.compression)
    &&std::abs(f.height)<=64&&std::abs(f.dx)<=1024&&std::abs(f.dy)<=1024&&std::abs(f.compression)<=1048576;}
inline bool Finite(const Context &c){return std::isfinite(c.depth)&&std::isfinite(c.distance)&&std::isfinite(c.outwardX)&&std::isfinite(c.outwardY)
    &&std::isfinite(c.exposure)&&std::isfinite(c.offshore)&&std::isfinite(c.analyticDamping)&&std::isfinite(c.spectralDamping)
    &&std::isfinite(c.wet)&&std::isfinite(c.admitted)&&std::abs(c.depth)<=1048576&&std::abs(c.distance)<=1048576
    &&std::abs(c.outwardX)<=1024&&std::abs(c.outwardY)<=1024&&c.exposure>=0&&c.exposure<=1
    &&c.offshore>=0&&c.offshore<=1&&c.analyticDamping>=0&&c.analyticDamping<=1
    &&c.spectralDamping>=0&&c.spectralDamping<=1&&c.wet>=0&&c.wet<=1&&c.admitted>=0&&c.admitted<=1;}
inline bool Finite(const State &s){return std::isfinite(s.density)&&std::isfinite(s.ageMoment)&&std::isfinite(s.height)&&std::isfinite(s.rate)
    &&s.density>=0&&s.density<=16&&s.ageMoment>=0&&s.ageMoment<=1024&&std::abs(s.height)<=64&&s.rate>=-1&&s.rate<=MaximumRate;}
// Incident-component indicators at fixed blend weight, deliberately excluding grad(R).
// This is not the final mixed geometric normal/Jacobian: a static coastal transition
// must not create a breaker from two different but spatially constant input heights.
inline Field Blend(const Field &analytic,const Field &spectral,float weight){
    if(!Finite(analytic)||!Finite(spectral)||!std::isfinite(weight))return {};
    const float w=Clamp(weight,0,1);
    return {analytic.height+(spectral.height-analytic.height)*w,
        analytic.dx+(spectral.dx-analytic.dx)*w,analytic.dy+(spectral.dy-analytic.dy)*w,
        1+(spectral.compression-1)*w};
}
inline float Domain(const Context &c){
    if(!Finite(c)||c.wet<.5f||c.admitted<.5f||c.exposure<=0||c.depth<=.15f||c.distance<0)return 0;
    // This physical source domain is independent of optical A replacement validity.
    // The depth bound makes the 128-unit distance limit a work bound, not a uniform ribbon.
    return Smooth(.15f,.8f,c.depth)*(1-Smooth(8,12,c.depth))
        *(1-Smooth(PhysicalCoastFull,PhysicalCoastEnd,c.distance))*Clamp(c.exposure,0,1);
}
inline float Rate(const Context &c,const Field &f,const State &previous,double elapsed){
    const float domain=Domain(c);
    if(domain<=0||!Finite(f)||!Finite(previous)||previous.rate<0||!std::isfinite(elapsed)
        ||elapsed<=1e-6||elapsed>MaximumSourceInterval||f.height<=0)return 0;
    const float normal=std::hypot(c.outwardX,c.outwardY),slope=std::hypot(f.dx,f.dy);
    if(normal<=1e-8f||slope<=1e-8f)return 0;
    const float ht=float((double(f.height)-previous.height)/elapsed);
    // Backward temporal difference of the same broad field, on the fixed parameter
    // chart. This is a bounded phase-direction approximation, not Eulerian fluid velocity.
    // Positive h_t selects the growing face; the outward gradient then identifies arrival.
    if(!(ht>0))return 0;
    if(!std::isfinite(ht))return 0;
    const float incoming=Smooth(0,.25f,(f.dx/slope)*(c.outwardX/normal)+(f.dy/slope)*(c.outwardY/normal));
    const float heightBreak=Smooth(.4f,.85f,2*f.height/(std::max)(c.depth+.5f,1.0f));
    const float compressionBreak=Smooth(.04f,.30f,1-f.compression);
    const float breaking=(std::max)(heightBreak,compressionBreak);
    const float damping=Clamp(c.analyticDamping+(c.spectralDamping-c.analyticDamping)*Clamp(c.offshore,0,1),0,1);
    // Dissipation is already implied by the actual shallow-water damping. No constant
    // source floor, camera derivative, artificial crest, or fine-ripple trigger is added.
    const float lostEnergy=1-damping*damping;
    return MaximumRate*domain*incoming*breaking*lostEnergy*Smooth(0,.5f,f.height)*Smooth(0,.08f,ht);
}
// Factored integrals S_n(x)=integral_0^1 u^n exp(-x*u) du, x<=.05;
// the series avoids cancellation and division by tiny x in the linear-rate update.
inline double IntegralSeries(unsigned n,double x){
    double sum=0,term=1;
    for(unsigned k=0;k<10;++k){sum+=term/double(n+k+1);term*=-x/double(k+1);}
    return sum;
}
inline State Integrate(const State &previous,float height,float rate,double elapsed,bool source){
    if(!Finite(previous)||!std::isfinite(height)||!std::isfinite(rate)||!std::isfinite(elapsed)||elapsed<0)return {};
    if(elapsed<=1e-6)return previous;
    const double x=elapsed/Lifetime,q=std::exp(-x);
    double density=q*previous.density,age=q*(previous.ageMoment+elapsed*previous.density);
    if(source&&elapsed<=MaximumSourceInterval){
        const double a=Clamp(previous.rate,0,MaximumRate),b=Clamp(rate,0,MaximumRate);
        const double j0=IntegralSeries(0,x),j1=IntegralSeries(1,x),j2=IntegralSeries(2,x);
        const double w0=elapsed*j1,w1=elapsed*(j0-j1);
        const double m0=elapsed*elapsed*j2,m1=elapsed*elapsed*(j1-j2);
        density+=a*w0+b*w1;age+=a*m0+b*m1;
    }
    // Underflow explicitly produces zero; never evaluate inf*0 after an enormous gap.
    if(q==0){density=0;age=0;}
    return {float(density),float(age),height,source?Clamp(rate,0,MaximumRate):0};
}
inline State Advance(const Context &c,const Field &field,const State &previous,double elapsed,bool allowSource=true){
    if(elapsed>=0&&elapsed<=1e-6&&Finite(previous))return previous;
    if(!Finite(c)||!Finite(field)||c.wet<.5f||c.admitted<.5f||c.exposure<=0||c.depth<=.15f)return {};
    const float rate=allowSource?Rate(c,field,previous,elapsed):0;
    return Integrate(previous,field.height,rate,elapsed,allowSource&&elapsed<=MaximumSourceInterval);
}
inline float Coverage(const State &s,float porous){
    if(!Finite(s)||!std::isfinite(porous)||s.density<=0)return 0;
    const float age=s.ageMoment/s.density;
    const float old=Smooth(.4f,3,age),terminal=1-Smooth(8,14,age);
    return float(-std::expm1(-double(s.density)))*(1-old+old*Clamp(porous,0,1))*terminal;
}
// Immutable graph payload. Node identity is its exact level and fixed world-grid key.
// Zero neighbour IDs mean no connection; IDs are otherwise index+1 (exact in float32).
struct Node {
    float world[4]={}; // mean-water x,y,exact level,unused
    Context context;
    uint32_t neighbour[4]={}; // -X,+X,-Y,+Y, reciprocal same-level wet segments only
};
inline bool ValidGraph(const std::vector<Node> &nodes,double spacing){
    if(nodes.size()>MaxNodes||!std::isfinite(spacing)||spacing<4||spacing>64)return false;
    std::vector<float> levels;std::vector<std::array<float,3>> keys;keys.reserve(nodes.size());
    const unsigned opposite[4]={1,0,3,2};
    for(unsigned i=0;i<nodes.size();++i){const Node &n=nodes[i];
        for(float v:n.world)if(!std::isfinite(v))return false;
        // Every installed slot is valid wet history support, including the no-birth
        // spread margin. Otherwise diffusion could refill a slot Advance just cleared.
        if(!Finite(n.context)||n.context.wet<.5f||n.context.admitted<.5f
            ||n.context.exposure<=0||n.context.depth<=.15f)return false;
        keys.push_back({n.world[2],n.world[1],n.world[0]});
        if(std::find(levels.begin(),levels.end(),n.world[2])==levels.end())levels.push_back(n.world[2]);
        if(levels.size()>MaxLevels)return false;
        for(unsigned side=0;side<4;++side){const uint32_t id=n.neighbour[side];if(!id)continue;
            if(id>nodes.size()||id==i+1)return false;
            const Node &other=nodes[id-1];
            if(other.world[2]!=n.world[2]||other.neighbour[opposite[side]]!=i+1)return false;
            const double dx=other.world[0]-n.world[0],dy=other.world[1]-n.world[1];
            const double expectedX=side==0?-spacing:side==1?spacing:0;
            const double expectedY=side==2?-spacing:side==3?spacing:0;
            if(std::abs(dx-expectedX)>1e-5||std::abs(dy-expectedY)>1e-5)return false;
        }
    }
    std::sort(keys.begin(),keys.end());
    if(std::adjacent_find(keys.begin(),keys.end())!=keys.end())return false;
    return true;
}
inline State Diffuse(const State &centre,const std::array<State,4> &neighbours,unsigned mask,double spacing,double elapsed){
    if(!Finite(centre)||!std::isfinite(spacing)||spacing<4||!std::isfinite(elapsed)||elapsed<0||elapsed>MaximumSourceInterval)return {};
    const double a=Diffusivity*elapsed/(spacing*spacing);State out=centre;
    double density=centre.density,age=centre.ageMoment;
    for(unsigned i=0;i<4;++i)if(mask&(1u<<i)){
        if(!Finite(neighbours[i]))return {};
        density+=a*(neighbours[i].density-centre.density);age+=a*(neighbours[i].ageMoment-centre.ageMoment);
    }
    out.density=float(density);out.ageMoment=float(age);return out;
}
// Camera-view composition stores source indices/weights, not copied history. Rejected
// contributions have zero weight; weights are never renormalized across a dry gap.
struct Lookup {uint32_t node[4]={};float weight[4]={};};
inline bool ValidLookup(const Lookup &lookup,unsigned count){
    if(count>MaxNodes)return false;double total=0;
    for(unsigned i=0;i<4;++i){if(lookup.node[i]>count||!std::isfinite(lookup.weight[i])||lookup.weight[i]<0
        ||lookup.weight[i]>1||(!lookup.node[i]&&lookup.weight[i]!=0))return false;total+=lookup.weight[i];}
    return total<=1.000001;
}
enum class Action {None,Reset,Sample,GapDecay,Invalid};
struct Plan {Action action=Action::None;double time=0,elapsed=0;uint64_t tick=0;};
class Clock {
    bool ready=false;uint64_t revision=0,completed=0;double epoch=0,sampled=0,observed=0;
public:
    // Planning never advances state: commit only after all GPU writes were submitted.
    Plan Inspect(uint64_t rev,double time)const{
        if(!rev||!WaterShoreHistory::ValidTime(time))return {Action::Invalid};
        if(!ready||rev!=revision||time<observed)return {Action::Reset,time,0,0};
        const uint64_t tick=uint64_t((std::max)(0.0,std::floor((time-epoch)/SourceTick+1e-9)));
        if(tick<=completed)return {Action::None,time,0,tick};
        const double elapsed=time-sampled;
        return {elapsed>MaximumSourceInterval?Action::GapDecay:Action::Sample,time,elapsed,tick};
    }
    bool Commit(uint64_t rev,const Plan &plan){
        if(plan.action==Action::Invalid||!rev||!WaterShoreHistory::ValidTime(plan.time))return false;
        const Plan expected=Inspect(rev,plan.time);
        if(expected.action!=plan.action||expected.elapsed!=plan.elapsed||expected.tick!=plan.tick)return false;
        observed=plan.time;
        if(plan.action==Action::None)return true;
        if(plan.action==Action::Reset){ready=true;revision=rev;epoch=sampled=plan.time;completed=0;}
        else {sampled=plan.time;completed=plan.tick;}
        return true;
    }
    double SampledTime()const{return sampled;}
    void Clear(){*this=Clock();}
};

// Called after the real fftDecodeSamples/fftLimit helpers. Zeroing the fine sample
// excludes its phase/derivatives while retaining the production broad/medium decode
// and limiter; no duplicate Phillips equations or differently seeded spectrum exist.
inline const char *IncidentSource(){return R"HLSL(
float4 waterSurfIncident(FftMapSamples samples) {
    samples.c=0;samples.cd=0;
    FftSurface field=fftDecodeSamples(samples);
    float3 derivative=fftLimitDerivative(field.displacement);
    return float4(fftLimit(field.displacement).z,
        field.derivativeX.z*derivative.z,field.derivativeY.z*derivative.z,field.incidentCompression);
}
)HLSL";}
inline const char *Source(){return R"HLSL(
struct WaterSurfContext {float4 coast;float4 surface;float4 admission;};
// coast=(depth,physicalDistance,outwardX,outwardY)
// surface=(exposure,offshore,analyticDamping,spectralDamping)
// admission=(wet,admitted,0,0); optical A is deliberately absent.
bool waterSurfContextFinite(WaterSurfContext c){return all(isfinite(c.coast))&&all(isfinite(c.surface))&&all(isfinite(c.admission))
    &&all(abs(c.coast.xy)<=1048576)&&all(abs(c.coast.zw)<=1024)&&all(c.surface>=0)&&all(c.surface<=1)
    &&all(c.admission.xy>=0)&&all(c.admission.xy<=1);}
bool waterSurfFieldFinite(float4 f){return all(isfinite(f))&&abs(f.x)<=64&&all(abs(f.yz)<=1024)&&abs(f.w)<=1048576;}
float waterSurfDomain(WaterSurfContext c){
    if(!waterSurfContextFinite(c)||c.admission.x<.5||c.admission.y<.5||c.surface.x<=0||c.coast.x<=.15||c.coast.y<0)return 0;
    return smoothstep(.15,.8,c.coast.x)*(1-smoothstep(8,12,c.coast.x))
        *(1-smoothstep(96,128,c.coast.y))*saturate(c.surface.x);
}
bool waterSurfStateFinite(float4 s){return all(isfinite(s))&&s.x>=0&&s.x<=16&&s.y>=0&&s.y<=1024&&abs(s.z)<=64&&s.w>=-1&&s.w<=1.2;}
float4 waterSurfBlend(float4 analytic,float4 spectral,float weight){
    if(!waterSurfFieldFinite(analytic)||!waterSurfFieldFinite(spectral)||!isfinite(weight))return float4(0,0,0,1);
    float w=saturate(weight);return float4(analytic.xyz+(spectral.xyz-analytic.xyz)*w,1+(spectral.w-1)*w);
}
float waterSurfRate(WaterSurfContext c,float4 field,float4 previous,float dt){
    float domain=waterSurfDomain(c);
    if(domain<=0||!waterSurfFieldFinite(field)||!waterSurfStateFinite(previous)||previous.w<0||!isfinite(dt)||dt<=.000001||dt>.25||field.x<=0)return 0;
    float n=length(c.coast.zw),slope=length(field.yz);if(n<=.00000001||slope<=.00000001)return 0;
    float ht=(field.x-previous.z)/dt;if(!isfinite(ht)||!(ht>0))return 0;
    float incoming=smoothstep(0,.25,dot(field.yz/slope,c.coast.zw/n));
    float breaking=max(smoothstep(.4,.85,2*field.x/max(c.coast.x+.5,1)),smoothstep(.04,.30,1-field.w));
    float damping=saturate(lerp(c.surface.z,c.surface.w,saturate(c.surface.y)));
    return 1.2*domain*incoming*breaking*(1-damping*damping)*smoothstep(0,.5,field.x)*smoothstep(0,.08,ht);
}
float3 waterSurfIntegrals(float x){
    float3 sum=0;float term=1;
    [unroll]for(int k=0;k<10;++k){sum+=term/float3(k+1,k+2,k+3);term*=-x/(k+1);}
    return sum;
}
float4 waterSurfIntegrate(float4 previous,float height,float rate,float dt,bool source){
    if(!waterSurfStateFinite(previous)||!isfinite(height)||!isfinite(rate)||!isfinite(dt)||dt<0)return float4(0,0,0,-1);
    if(dt<=.000001)return previous;
    float x=dt/5,q=exp(-x);float density=q*previous.x,age=q*(previous.y+dt*previous.x);
    if(source&&dt<=.25){
        float a=clamp(previous.w,0,1.2),b=clamp(rate,0,1.2);float3 j=waterSurfIntegrals(x);
        float w0=dt*j.y,w1=dt*(j.x-j.y),m0=dt*dt*j.z,m1=dt*dt*(j.y-j.z);
        density+=a*w0+b*w1;age+=a*m0+b*m1;
    }
    if(q==0){density=0;age=0;}
    return float4(density,age,height,source?clamp(rate,0,1.2):0);
}
float4 waterSurfAdvance(WaterSurfContext c,float4 field,float4 previous,float dt,bool allowSource){
    if(dt>=0&&dt<=.000001&&waterSurfStateFinite(previous))return previous;
    if(!waterSurfContextFinite(c)||!waterSurfFieldFinite(field)||c.admission.x<.5||c.admission.y<.5||c.surface.x<=0||c.coast.x<=.15)return float4(0,0,0,-1);
    float rate=allowSource?waterSurfRate(c,field,previous,dt):0;
    return waterSurfIntegrate(previous,field.x,rate,dt,allowSource&&dt<=.25);
}
float4 waterSurfDiffuse(float4 centre,float4 a,float4 b,float4 c,float4 d,uint mask,float spacing,float dt){
    if(!waterSurfStateFinite(centre)||!isfinite(spacing)||spacing<4||!isfinite(dt)||dt<0||dt>.25)return float4(0,0,0,-1);
    float amount=2*dt/(spacing*spacing);float2 value=centre.xy;
    if(mask&1){if(!waterSurfStateFinite(a))return float4(0,0,0,-1);value+=amount*(a.xy-centre.xy);}
    if(mask&2){if(!waterSurfStateFinite(b))return float4(0,0,0,-1);value+=amount*(b.xy-centre.xy);}
    if(mask&4){if(!waterSurfStateFinite(c))return float4(0,0,0,-1);value+=amount*(c.xy-centre.xy);}
    if(mask&8){if(!waterSurfStateFinite(d))return float4(0,0,0,-1);value+=amount*(d.xy-centre.xy);}
    return float4(value,centre.zw);
}
float waterSurfCoverage(float4 state,float porous){
    if(!waterSurfStateFinite(state)||!isfinite(porous)||state.x<=0)return 0;
    float age=state.y/state.x,old=smoothstep(.4,3,age);
    return (1-exp(-state.x))*lerp(1,saturate(porous),old)*(1-smoothstep(8,14,age));
}
)HLSL";}
} // namespace WaterCoastalSurf
