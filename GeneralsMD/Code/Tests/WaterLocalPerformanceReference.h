// Frozen local evaluator from a094410c, before performance recovery.
// Keep original reconstruction and noise-coordinate Jacobians as the GPU oracle.
#pragma once
namespace WaterLocalPerformanceReference {
inline std::string Source() {return R"HLSL(
FftLocalFields fixtureOriginalLocalFields(float2 p) {
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
    [branch] if(!any(weight0>0.0) && !any(weight1>0.0))return fields;
    float3 a=fftLocalEnvelope(weight0,f,spacing),b=fftLocalEnvelope(weight1,f,spacing);
    a.yz*=inside;b.yz*=inside;
    float4 corners0[4],corners1[4];float4 shore0,shore1;
    [unroll] for(int i=0;i<4;++i) {
        float4 c=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(2*texel.x,0),0)*255.0);
        float4 d=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(3*texel.x,0),0)*255.0);
        // G/B of the original metadata word were unused. Sea corners retain
        // their original bytes, so explicitly supply neutral zero there.
        float2 bankBytes=round(FftHeadingTexture.SampleLevel(FftHeadingSampler,centres[i]+float2(texel.x,0),0).gb*255.0);
        float2 bank=influence[i]>0.0?(bankBytes-128.0)/127.0:float2(0,0);
        shore0[i]=bank.x;shore1[i]=bank.y;
        // z retains its original enabled test (>0.5); z-1 carries local phase units.
        float phaseStep=max(FftParameters.z-1.0,0.0);
        corners0[i]=float4((c.r*256+c.g)*phaseStep,(c.ba-128.0)*fftGradientRange/127.0,0);
        corners1[i]=float4((d.r*256+d.g)*phaseStep,(d.ba-128.0)*fftGradientRange/127.0,0);
    }
    float3 sx=fftLocalEnvelope(shore0,f,spacing),sy=fftLocalEnvelope(shore1,f,spacing);
    fields.shore=float2(sx.x,sy.x);
    fields.shoreX=float2(sx.y,sy.y)*inside.x;fields.shoreY=float2(sx.z,sy.z)*inside.y;
    FftWavefrontBasis basis=fftWavefrontPrepare(f);
    fields.first=fftWavefrontInterpolatePrepared(corners0[0],corners0[1],corners0[2],corners0[3],1.0,basis,spacing);
    fields.second=fftWavefrontInterpolatePrepared(corners1[0],corners1[1],corners1[2],corners1[3],1.0,basis,spacing);
    fields.first.gradient*=inside;fields.second.gradient*=inside;
    fields.first.hessian*=float3(inside.x,inside.x*inside.y,inside.y);
    fields.second.hessian*=float3(inside.x,inside.x*inside.y,inside.y);
    fields.first.amplitude=fields.influence.x*a.x;
    fields.first.amplitudeGradient=fields.influence.yz*a.x+fields.influence.x*a.yz;
    fields.second.amplitude=fields.influence.x*b.x;
    fields.second.amplitudeGradient=fields.influence.yz*b.x+fields.influence.x*b.yz;
    return fields;
}
void fixtureOriginalLocalArrival(inout FftLocalWaves value,float2 p,FftWavefrontParameters field,float2 shore,float2 shoreX,float2 shoreY,float footprint2,float3 clock,float budget,float2 budgetGradient,float gradientBudget) {
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
        // fftStreamAdvected differentiates in its noise coordinates. Supply the
        // transformed velocity Jacobian there, then map its scalar slope back.
        precise float2 vqx=sizes[i]*(cs*vx-sn*vy),vqy=sizes[i]*(sn*vx+cs*vy)/shape.y;
        precise float3 localClock=float3(clock.x,sizes[i]*float2(cs*clock.y-sn*clock.z,(sn*clock.y+cs*clock.z)/shape.y));
        precise float3 n=fftStreamAdvected(q,v,vqx,vqy,WaterParameters.x,localClock);
        n.yz*=shape;
        precise float2 ng=float2(cs*n.y+sn*n.z,-sn*n.y+cs*n.z)/sizes[i];
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
FftLocalWaves fixtureOriginalLocalWaves(float2 p,FftLocalFields fields,float lod) {
    precise FftLocalWaves value=(FftLocalWaves)0;
    [branch] if(fields.first.amplitude==0.0 && all(fields.first.amplitudeGradient==0.0)
        && fields.second.amplitude==0.0 && all(fields.second.amplitudeGradient==0.0))return value;
    // Both routes share the spatial clock and footprint, not their current or phase.
    precise float footprint2=max(0.0,exp2(2*lod)-1)*fftLocalTexel*fftLocalTexel/24.0;
    precise float3 clock=fftStreamNoise(p/83.0+float2(11.7,-6.3));clock.yz/=83.0;
    float budget=fields.first.amplitude+fields.second.amplitude;
    float2 budgetGradient=fields.first.amplitudeGradient+fields.second.amplitudeGradient;
    float gradientBudget=length(fields.first.amplitudeGradient)+length(fields.second.amplitudeGradient);
    fixtureOriginalLocalArrival(value,p,fields.first,fields.shore,fields.shoreX,fields.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    fixtureOriginalLocalArrival(value,p,fields.second,fields.shore,fields.shoreX,fields.shoreY,footprint2,clock,budget,budgetGradient,gradientBudget);
    return value;
}
float3 fixtureOriginalRawDisplacement(float2 p) {
    FftLocalFields fields=fixtureOriginalLocalFields(p);
    [branch] if(fields.influence.x==0.0)return fftLegacyDisplacement(p);
    FftLocalWaves waves=fixtureOriginalLocalWaves(p,fields,0.0);
    return fields.legacyGain.x*fftLegacyDisplacement(p)+waves.displacement;
}
)HLSL";}
}
