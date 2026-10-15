/* Bounded render-only nonlinear coastal crest restoration. Positive temporal mean is intentional
 * in this experimental approximation; it is not a mass-conserving shallow-water solver. */
#pragma once
#include "Platform/WaterShoreFoam.h"
namespace WaterCoastalCrest {
using Field=WaterSurfaceModel::Sample; // value, d/dx, d/dy
inline bool Eligible(bool river,bool context,float offshore) {return !river&&context&&offshore==0;}
inline float EncodeObstacle(float obstacle) {
    return float((WaterSurfaceModel::EncodeObstacleAttenuation(0,obstacle)>>16)&255)/255.0f;
}
inline bool Finite(const Field& f) {return std::isfinite(f.height)&&std::isfinite(f.dx)&&std::isfinite(f.dy);}
inline Field Unit(Field f) {
    if(f.height<=0)return {0,0,0};if(f.height>=1)return {1,0,0};return f;
}
inline Field Product(const Field&a,const Field&b) {
    return {a.height*b.height,a.dx*b.height+a.height*b.dx,a.dy*b.height+a.height*b.dy};
}
inline Field Smooth(float a,float b,Field f) {
    if(f.height<=a)return {0,0,0};if(f.height>=b)return {1,0,0};
    const float t=(f.height-a)/(b-a),derivative=6*t*(1-t)/(b-a);
    return {t*t*(3-2*t),derivative*f.dx,derivative*f.dy};
}
// Caller must preserve river/offshore/missing-context identity. Terrain clearance is evaluated
// at the same point as the original envelope; this addition never lowers that original mesh.
inline Field Correction(Field broad,Field depth,Field support,Field exposure,Field obstacle,Field damping) {
    if(!Finite(broad)||!Finite(depth)||!Finite(support)||!Finite(exposure)||!Finite(obstacle)||!Finite(damping)
        ||broad.height<=0||depth.height<=WaterShoreFoam::MinDepth)return {0,0,0};
    support=Unit(support);exposure=Unit(exposure);obstacle=Unit(obstacle);damping=Unit(damping);
    // Diffuse red is UNORM8 rounded by the existing vertex encoder. Remove half a code
    // so restoration can never exceed the original unquantized obstacle envelope.
    obstacle.height-=0.5f/255.0f;if(obstacle.height<=0)obstacle={0,0,0};
    const Field gain={obstacle.height-damping.height,obstacle.dx-damping.dx,obstacle.dy-damping.dy};
    if(gain.height<=0||support.height<=0||exposure.height<=0)return {0,0,0};
    const float clearance=depth.height-WaterShoreFoam::MinDepth,ratio=broad.height/clearance;
    const float q=std::tanh(ratio),dq=1-q*q,depthDerivative=q-ratio*dq;
    const Field limited={clearance*q,dq*broad.dx+depthDerivative*depth.dx,dq*broad.dy+depthDerivative*depth.dy};
    const Field wet=Smooth(WaterShoreFoam::MinDepth,WaterShoreFoam::WetDepth,depth);
    const Field crest=Smooth(0,WaterShoreFoam::CrestHeight,broad);
    return Product(Product(Product(support,exposure),Product(wet,gain)),Product(limited,crest));
}
inline float Height(float broad,float depth,float support,float exposure,float obstacle,float damping) {
    return Correction({broad,0,0},{depth,0,0},{support,0,0},{exposure,0,0},{obstacle,0,0},{damping,0,0}).height;
}
inline std::string Source() {return R"COAST(
float3 waterCoastUnit(float3 f) {if(f.x<=0)return 0;if(f.x>=1)return float3(1,0,0);return f;}
float3 waterCoastProduct(float3 a,float3 b) {return float3(a.x*b.x,a.yz*b.x+a.x*b.yz);}
float3 waterCoastSmooth(float a,float b,float3 f) {
    if(f.x<=a)return 0;if(f.x>=b)return float3(1,0,0);
    float t=(f.x-a)/(b-a);return float3(t*t*(3-2*t),(6*t*(1-t)/(b-a))*f.yz);
}
float3 waterCoastalCrest(float3 broad,float3 depth,float3 support,float3 exposure,float3 obstacle,float3 damping) {
    if(!all(isfinite(broad))||!all(isfinite(depth))||!all(isfinite(support))||!all(isfinite(exposure))
        ||!all(isfinite(obstacle))||!all(isfinite(damping))||broad.x<=0||depth.x<=ShoreMinDepth)return 0;
    support=waterCoastUnit(support);exposure=waterCoastUnit(exposure);
    obstacle=waterCoastUnit(obstacle);damping=waterCoastUnit(damping);
    obstacle.x-=0.5/255.0;if(obstacle.x<=0)obstacle=0;
    float3 gain=obstacle-damping;if(gain.x<=0||support.x<=0||exposure.x<=0)return 0;
    float clearance=depth.x-ShoreMinDepth,ratio=broad.x/clearance,q=tanh(ratio),dq=1-q*q;
    float3 limited=float3(clearance*q,dq*broad.yz+(q-ratio*dq)*depth.yz);
    float3 wet=waterCoastSmooth(ShoreMinDepth,ShoreWetDepth,depth),crest=waterCoastSmooth(0,ShoreCrestHeight,broad);
    return waterCoastProduct(waterCoastProduct(waterCoastProduct(support,exposure),waterCoastProduct(wet,gain)),
        waterCoastProduct(limited,crest));
}
)COAST";}
}
