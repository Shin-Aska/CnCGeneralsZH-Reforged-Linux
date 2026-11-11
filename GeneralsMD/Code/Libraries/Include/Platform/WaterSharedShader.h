/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterSharedField.h"
#include <string>
#include <cstdio>

// A/B experiment only: no bindings or shader source emitted unless shared mode is selected.
namespace WaterSharedShader {
inline std::string Source(float timeScale) {
    std::string source=R"HLSL(
// A/B experiment only: six heading/depth words above two rows of four amplitude words.
// The atlas is 6*side by 3*side, bounded even for the largest supported terrain.
struct FftSharedWave {
    float height,filteredHeight,variance;
    float2 slope,filteredSlope;
    float longTotal,mediumTotal;float2 mediumDirection;
};
static const float2 fftSharedDirections[16]={
)HLSL";
    char value[256];
    for(int i=0;i<16;++i) {
        auto d=WaterSharedField::Direction(i);
        std::snprintf(value,sizeof(value),"float2(%.9g,%.9g)%s\n",double(d[0]),double(d[1]),i==15?"":",");source+=value;
    }
    source+=R"HLSL(};
float fftSharedFootprint(float3 lod) {
    // Use the finest resolved FFT footprint so medium profiles filter before the
    // long cascade's first mip; the long term still covers a saturated fine mip.
    return sqrt(max(fftSwellTexel*fftSwellTexel*max(0.0,exp2(2*lod.x)-1),
        fftLocalTexel*fftLocalTexel*max(0.0,exp2(2*lod.z)-1)));
}
FftSharedWave fftSharedWave(float2 p,float footprint) {
    FftSharedWave result=(FftSharedWave)0;
    uint width,height;FftHeadingTexture.GetDimensions(width,height);
    int side=int(height/3);float scale=side/max(TerrainParameters.y,1.0);
    float2 raw=(p+TerrainParameters.xx)*scale,cell=clamp(raw,0.0,float2(side-1,side-1));
    int2 lo=min(int2(floor(cell)),int2(side-2,side-2));float2 f=cell-float2(lo);
    float2 inside=step(0.0,raw)*step(raw,float2(side-1,side-1));
    float4 timeSin[2],timeCos[2];
    const float4 harmonics[2]={float4(1,2,4,7),float4(1,3,6,10)};
    const float periods[2]={1056.0,240.0};
)HLSL";
    std::snprintf(value,sizeof(value),"    float sharedTime=WaterParameters.x*%.9g;\n",double(timeScale));source+=value;
    source+=R"HLSL(
    [unroll] for(int band=0;band<2;++band) {
        float4 k=6.283185307179586*harmonics[band]/periods[band];
        sincos(-sqrt(fftSwellGravityScale*k)*sharedTime+float4(0.43,2.19,4.31,5.67),timeSin[band],timeCos[band]);
    }
    [loop] for(int word=0;word<8;++word) {
        float4 corners[4];
        [unroll] for(int y=0;y<2;++y) [unroll] for(int x=0;x<2;++x) {
            int2 at=lo+int2(x,y);
            float2 uv=(float2(at.x*6+word%4,side+2*at.y+word/4)+0.5)/float2(width,height);
            corners[y*2+x]=FftHeadingTexture.SampleLevel(FftHeadingSampler,uv,0);
        }
        float4 amplitude=lerp(lerp(corners[0],corners[1],f.x),lerp(corners[2],corners[3],f.x),f.y);
        float4 ax=lerp(corners[1]-corners[0],corners[3]-corners[2],f.y)*scale*inside.x;
        float4 ay=lerp(corners[2]-corners[0],corners[3]-corners[1],f.x)*scale*inside.y;
        int band=word/4;
        [loop] for(int channel=0;channel<4;++channel) {
            int direction=(word%4)*4+channel;float2 dir=fftSharedDirections[direction];
            float k0=6.283185307179586/periods[band],sn,cs;
            sincos(k0*dot(p,dir),sn,cs);
            // A/B experiment only: six complex products produce all needed integer
            // harmonics; no per-component transcendental or ten-step selection loop.
            float s2=2*sn*cs,c2=cs*cs-sn*sn;
            float s3=s2*cs+c2*sn,c3=c2*cs-s2*sn;
            float s4=2*s2*c2,c4=c2*c2-s2*s2;
            float s6=2*s3*c3,c6=c3*c3-s3*s3;
            float s7=s6*cs+c6*sn,c7=c6*cs-s6*sn;
            float s10=s6*c4+c6*s4,c10=c6*c4-s6*s4;
            float4 hs=band==0?float4(sn,s2,s4,s7):float4(sn,s3,s6,s10);
            float4 hc=band==0?float4(cs,c2,c4,c7):float4(cs,c3,c6,c10);
            float4 k=k0*harmonics[band];
            float4 waves=hc*timeCos[band]-hs*timeSin[band];
            float4 slopes=-k*(hs*timeCos[band]+hc*timeSin[band]);
            // Four equal powers: sum_i A_i^2=1 offshore, giving HeightRms per band.
            float gain=band==0?1.060660172:0.530330086;
            float profile=dot(waves,float4(1,1,1,1))*gain,derivative=dot(slopes,float4(1,1,1,1))*gain;
            float4 filter=exp(-k*k*footprint*footprint/24.0);
            float filtered=dot(waves,filter)*gain,filteredDerivative=dot(slopes,filter)*gain;
            float a=amplitude[channel];float2 gradient=float2(ax[channel],ay[channel]);
            result.height+=a*profile;result.slope+=a*derivative*dir+gradient*profile;
            result.filteredHeight+=a*filtered;result.filteredSlope+=a*filteredDerivative*dir+gradient*filtered;
            result.variance+=0.5*a*a*gain*gain*dot(k*k,1-filter*filter);
            if(band==0)result.longTotal+=a;
            else {result.mediumTotal+=a;result.mediumDirection+=a*dir;}
        }
    }
    return result;
}
)HLSL";
    std::snprintf(value,sizeof(value),"float gain=band==0?%.9g:%.9g;",
        WaterFftSpectrum::HeightRms[0]*std::sqrt(0.5),WaterFftSpectrum::HeightRms[1]*std::sqrt(0.5));
    const std::string budget="float gain=band==0?1.060660172:0.530330086;";
    source.replace(source.find(budget),budget.size(),value);
    return source;
}
}
