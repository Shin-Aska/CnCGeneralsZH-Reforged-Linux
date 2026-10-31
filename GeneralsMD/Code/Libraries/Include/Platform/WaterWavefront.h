/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <array>
#include <cmath>
#include <string>

// Parameter reconstruction inspired by Jeschke and Wojtan, Water Wave Animation via
// Wavefront Parameter Interpolation (2015). Two fixed phase charts retain opposite-side island arrivals;
// this bounded approximation does not track every branch of their multivalued wavefront mesh.
namespace WaterWavefront {
struct Corner {
    double phase=0,gradientX=0,gradientY=0,amplitude=0;
    bool valid=false;
};
struct Parameters {
    double phase=0,gradientX=0,gradientY=0;
    double hessianXX=0,hessianXY=0,hessianYY=0;
    double amplitude=0,amplitudeX=0,amplitudeY=0;
};
struct Basis {
    std::array<double,2> value,derivative,second,slope,slopeDerivative,slopeSecond;
};
inline Basis HermiteBasis(double t) {
    const double t2=t*t,t3=t2*t,t4=t3*t,t5=t4*t;
    return {{{1-10*t3+15*t4-6*t5,10*t3-15*t4+6*t5}},
            {{-30*t2+60*t3-30*t4,30*t2-60*t3+30*t4}},
            {{-60*t+180*t2-120*t3,60*t-180*t2+120*t3}},
            {{t-6*t3+8*t4-3*t5,-4*t3+7*t4-3*t5}},
            {{1-18*t2+32*t3-15*t4,-12*t2+28*t3-15*t4}},
            {{-36*t+96*t2-60*t3,-24*t+84*t2-60*t3}}};
}
// Corner order is 00,10,01,11. Gradients and spacing use world units. Every
// corner must carry finite phase and gradients, including globally shared dry
// ghost payloads in the wet boundary halo. `valid` means wet, not payload validity.
// All patches touching water use the same tensor quintic Hermite reconstruction.
// Corner first derivatives are prescribed; all second and mixed corner derivatives
// are zero. Phase, gradient and Hessian therefore agree across wet/partial-patch
// edges (C2), preserving continuous direction and footprint-filter derivatives.
// Affine phase is exact; curved fields trade exact quadratic reproduction for C2
// continuity without extra samples. Dry amplitude is zero; interpolation fades to dry corners
// without dividing by a vanishing validity weight. Entirely dry patches return zero.
inline Parameters Interpolate(const std::array<Corner,4>& corners,double fx,double fy,double spacing) {
    Parameters result;
    if(!(spacing>0) || !std::isfinite(spacing))return result;
    bool anyWet=false;
    for(const Corner& c:corners)anyWet=anyWet || c.valid;
    if(!anyWet)return result;
    const double origin=corners[0].phase;
    const std::array<double,2> bx={{1-fx,fx}},by={{1-fy,fy}},db={{-1,1}};
    const Basis hx=HermiteBasis(fx),hy=HermiteBasis(fy);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        const Corner& c=corners[y*2+x];
        const double amplitude=c.valid?c.amplitude:0;
        result.amplitude+=amplitude*bx[x]*by[y];
        result.amplitudeX+=amplitude*db[x]*by[y]/spacing;
        result.amplitudeY+=amplitude*bx[x]*db[y]/spacing;
        const double relative=c.phase-origin,sx=c.gradientX*spacing,sy=c.gradientY*spacing;
        result.phase+=relative*hx.value[x]*hy.value[y]+sx*hx.slope[x]*hy.value[y]+sy*hx.value[x]*hy.slope[y];
        result.gradientX+=relative*hx.derivative[x]*hy.value[y]+sx*hx.slopeDerivative[x]*hy.value[y]+sy*hx.derivative[x]*hy.slope[y];
        result.gradientY+=relative*hx.value[x]*hy.derivative[y]+sx*hx.slope[x]*hy.derivative[y]+sy*hx.value[x]*hy.slopeDerivative[y];
        result.hessianXX+=relative*hx.second[x]*hy.value[y]+sx*hx.slopeSecond[x]*hy.value[y]+sy*hx.second[x]*hy.slope[y];
        result.hessianXY+=relative*hx.derivative[x]*hy.derivative[y]+sx*hx.slopeDerivative[x]*hy.derivative[y]+sy*hx.derivative[x]*hy.slopeDerivative[y];
        result.hessianYY+=relative*hx.value[x]*hy.second[y]+sx*hx.slope[x]*hy.second[y]+sy*hx.value[x]*hy.slopeSecond[y];
    }
    result.phase+=origin;
    result.gradientX/=spacing;result.gradientY/=spacing;
    result.hessianXX/=spacing*spacing;result.hessianXY/=spacing*spacing;result.hessianYY/=spacing*spacing;
    return result;
}

inline std::string ShaderSource() {
    return R"HLSL(
struct FftWavefrontParameters {
    float phase;
    float2 gradient;
    float3 hessian; // xx,xy,yy in world units
    float amplitude;
    float2 amplitudeGradient;
};
// Quintic endpoint values and slopes, with endpoint second derivatives zero.
// Components are endpoint values (00,01) followed by endpoint slopes (10,11).
float4 fftWavefrontBasis(float t) {
    float t2=t*t,t3=t2*t,t4=t3*t,t5=t4*t;
    return float4(1-10*t3+15*t4-6*t5,10*t3-15*t4+6*t5,
        t-6*t3+8*t4-3*t5,-4*t3+7*t4-3*t5);
}
float4 fftWavefrontBasisDerivative(float t) {
    float t2=t*t,t3=t2*t,t4=t3*t;
    return float4(-30*t2+60*t3-30*t4,30*t2-60*t3+30*t4,
        1-18*t2+32*t3-15*t4,-12*t2+28*t3-15*t4);
}
float4 fftWavefrontBasisSecond(float t) {
    float t2=t*t,t3=t2*t;
    return float4(-60*t+180*t2-120*t3,60*t-180*t2+120*t3,
        -36*t+96*t2-60*t3,-24*t+84*t2-60*t3);
}
// Basis and cell coordinates are shared by both fixed arrival charts.
struct FftWavefrontBasis {float4 hx,hy,dx,dy,dxx,dyy;float2 f;};
FftWavefrontBasis fftWavefrontPrepare(float2 f) {
    FftWavefrontBasis b;b.f=f;
    b.hx=fftWavefrontBasis(f.x);b.hy=fftWavefrontBasis(f.y);
    b.dx=fftWavefrontBasisDerivative(f.x);b.dy=fftWavefrontBasisDerivative(f.y);
    b.dxx=fftWavefrontBasisSecond(f.x);b.dyy=fftWavefrontBasisSecond(f.y);
    return b;
}
// `valid` is the wet mask. Dry halo corners retain finite shared phase/partials.
// Separable tensor evaluation is algebraically identical to the CPU corner sum.
FftWavefrontParameters fftWavefrontInterpolatePrepared(float4 a0,float4 a1,float4 a2,float4 a3,float4 valid,FftWavefrontBasis b,float spacing) {
    FftWavefrontParameters result=(FftWavefrontParameters)0;
    if(spacing<=0.0 || !any(valid>=0.5))return result;
    float origin=a0.x;
    float2 u0=float2(a0.x-origin,a2.x-origin),u1=float2(a1.x-origin,a3.x-origin);
    float2 sx0=float2(a0.y,a2.y)*spacing,sx1=float2(a1.y,a3.y)*spacing;
    float2 sy0=float2(a0.z,a2.z)*spacing,sy1=float2(a1.z,a3.z)*spacing;
    float2 p=u0*b.hx.x+u1*b.hx.y+sx0*b.hx.z+sx1*b.hx.w;
    float2 q=sy0*b.hx.x+sy1*b.hx.y;
    float2 px=u0*b.dx.x+u1*b.dx.y+sx0*b.dx.z+sx1*b.dx.w;
    float2 qx=sy0*b.dx.x+sy1*b.dx.y;
    float2 pxx=u0*b.dxx.x+u1*b.dxx.y+sx0*b.dxx.z+sx1*b.dxx.w;
    float2 qxx=sy0*b.dxx.x+sy1*b.dxx.y;
    result.phase=origin+dot(p,b.hy.xy)+dot(q,b.hy.zw);
    result.gradient=float2(dot(px,b.hy.xy)+dot(qx,b.hy.zw),dot(p,b.dy.xy)+dot(q,b.dy.zw))/spacing;
    result.hessian=float3(dot(pxx,b.hy.xy)+dot(qxx,b.hy.zw),dot(px,b.dy.xy)+dot(qx,b.dy.zw),dot(p,b.dyy.xy)+dot(q,b.dyy.zw))/(spacing*spacing);
    float4 amp=float4(a0.w,a1.w,a2.w,a3.w)*step(0.5,valid);
    float2 left=float2(1-b.f.x,b.f.x),up=float2(1-b.f.y,b.f.y);
    result.amplitude=dot(amp,float4(left*up.x,left*up.y));
    result.amplitudeGradient=float2(dot(amp,float4(-up.x,up.x,-up.y,up.y)),dot(amp,float4(-left,left)))/spacing;
    return result;
}
FftWavefrontParameters fftWavefrontInterpolate(float4 a0,float4 a1,float4 a2,float4 a3,float4 valid,float2 f,float spacing) {
    return fftWavefrontInterpolatePrepared(a0,a1,a2,a3,valid,fftWavefrontPrepare(f),spacing);
}

)HLSL";
}
} // namespace WaterWavefront
