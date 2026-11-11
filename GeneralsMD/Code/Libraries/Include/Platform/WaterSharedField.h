/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterMediumDepth.h"
#include "Platform/WaterFftSpectrum.h"
#include <array>

// A/B experiment only: static amplitudes; no local phase, rotation, or simulation state.
namespace WaterSharedField {
constexpr int Directions=16, Bands=2, Channels=Directions*Bands, Words=Channels/4;
struct Field {
    int width=0,height=0;
    std::vector<float> amplitude; // Quantized floats, exactly matching RGBA8 transport.
};
inline std::array<float,2> Direction(int i) {
    const double angle=2*WaterFftSpectrum::Pi*(i%Directions)/Directions;
    return {{float(std::cos(angle)),float(std::sin(angle))}};
}
inline float Response(float d,int band) {
    const float reach=band==0?24.0f:12.0f;
    return WaterMediumDepth::Gain(d,reach)*(1+0.12f*WaterMediumDepth::Gain(40-d,24));
}
inline bool Seed(double windX,double windY,const double *powers,std::array<float,Channels> &seed) {
    seed.fill(0);
    if(!powers || !std::isfinite(windX) || !std::isfinite(windY)
        || std::hypot(windX,windY)<1e-8) return false;
    for(int b=0;b<Bands;++b) {
        if(!std::isfinite(powers[b]) || powers[b]<0 || powers[b]>64)return false;
        double energy=0;
        for(int i=0;i<Directions;++i) {
            const auto dir=Direction(i);
            const double alignment=(dir[0]*windX+dir[1]*windY)/std::hypot(windX,windY);
            // Phillips uses cos^p POWER: transported amplitude is its square root.
            const float a=powers[b]==0?1.0f:alignment>1e-7?float(std::pow(alignment,0.5*powers[b])):0;
            seed[b*Directions+i]=a;energy+=a*a;
        }
        if(energy<=0)return false;
        for(int i=0;i<Directions;++i)seed[b*Directions+i]/=float(std::sqrt(energy));
    }
    return true;
}
// fetch: optional e-folding regrowth distances in world units, {long, medium}. Over open water the
// wind rebuilds each band toward its open-sea seed with distance travelled, so sheltered water between
// islands regains medium chop quickly and long swell slowly. Null keeps pure transport.
inline bool Build(const uint8_t *wet,const WaterMediumDepth::Field &depth,float spacing,
    double windX,double windY,const double *powers,Field &result,int border=0,const float *fetch=nullptr) {
    result=Field{};
    const int w=depth.width,h=depth.height;
    std::array<float,Channels> seed;
    if(!wet || w<2 || h<2 || w>WaterMediumDepth::MaxDimension || h>WaterMediumDepth::MaxDimension
        || size_t(w)*h>WaterMediumDepth::MaxCells || depth.depth.size()!=size_t(w)*h
        || !std::isfinite(spacing) || spacing<=0 || border<0 || border>(std::min)(w,h)/2-1
        || !Seed(windX,windY,powers,seed))return false;
    for(float d:depth.depth)if(!std::isfinite(d) || d<0 || d>WaterMediumDepth::Cap)return false;
    if(fetch)for(int b=0;b<Bands;++b)if(!std::isfinite(fetch[b]) || fetch[b]<=0)return false;
    const size_t count=size_t(w)*h;
    std::vector<float> a(count*Channels,0),previous;
    const int x0=border,x1=w-1-border,y0=border,y1=h-1-border;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)if(wet[size_t(y)*w+x])
        std::copy(seed.begin(),seed.end(),a.begin()+(size_t(y)*w+x)*Channels);
    // Two directional transport sweeps. A cross-march diffusion kernel widens each
    // shadow with travel distance; weak neighbour-bin exchange supplies diffraction.
    for(int pass=0;pass<2;++pass) {
        previous=a;
        for(int i=0;i<Directions;++i) {
            const auto dir=Direction(i);const bool majorX=std::abs(dir[0])>=std::abs(dir[1]);
            const float major=majorX?dir[0]:dir[1],minor=majorX?dir[1]:dir[0];
            const int sign=major>=0?1:-1,first=majorX?x0:y0,last=majorX?x1:y1;
            const int lo=majorX?y0:x0,hi=majorX?y1:x1;
            // Distance travelled per grid step along this direction, and each band's regrowth per step.
            float regrow[Bands]={0,0};
            if(fetch)for(int b=0;b<Bands;++b)regrow[b]=1-std::exp(-(spacing/std::abs(major))/fetch[b]);
            for(int step=0;step<=last-first;++step) {
                const int m=sign>0?first+step:last-step;
                for(int n=lo;n<=hi;++n) {
                    const int x=majorX?m:n,y=majorX?n:m;const size_t cell=size_t(y)*w+x;
                    for(int b=0;b<Bands;++b) {
                        const int channel=b*Directions+i;float value=0;
                        if(wet[cell]) {
                            if(step==0)value=seed[channel];
                            else {
                                const float upstream=n-minor/std::abs(major);
                                const int low=int(std::floor(upstream));const float t=upstream-low;
                                for(int tap=-1;tap<=1;++tap)for(int k=0;k<2;++k) {
                                    const int cross=low+tap+k;const float weight=(tap==0?.70f:.15f)*(k?t:1-t);
                                    if(cross<lo || cross>hi) {value+=weight*seed[channel];continue;}
                                    const int xx=majorX?m-sign:cross,yy=majorX?cross:m-sign;
                                    const size_t at=(size_t(yy)*w+xx)*Channels;
                                    float carried=a[at+channel];
                                    if(pass) {
                                        const int left=b*Directions+(i+15)%16,right=b*Directions+(i+1)%16;
                                        const float incoming=seed[left]+seed[right];
                                        if(incoming>1e-8f)carried=.96f*carried+.04f*seed[channel]
                                            *(previous[at+left]+previous[at+right])/incoming;
                                    }
                                    value+=weight*carried;
                                }
                                value+=(seed[channel]-value)*regrow[b];
                            }
                        }
                        a[cell*Channels+channel]=value;
                    }
                }
            }
        }
    }
    previous.assign(a.size(),0);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        const size_t cell=size_t(y)*w+x;if(!wet[cell])continue;
        auto d=[&](int xx,int yy) {return depth.depth[size_t((std::max)(y0,(std::min)(y1,yy)))*w
            +(std::max)(x0,(std::min)(x1,xx))];};
        // D increases offshore. The shoreward depth gradient is -grad(D).
        const float gx=d(x-1,y)-d(x+1,y),gy=d(x,y-1)-d(x,y+1),length=std::hypot(gx,gy);
        const float turn=length>1e-5f?.35f*(1-WaterMediumDepth::Gain(d(x,y),24)):0;
        const float angle=std::atan2(gy,gx);
        for(int b=0;b<Bands;++b)for(int i=0;i<Directions;++i) {
            const float delta=std::remainder(angle-float(i*2*WaterFftSpectrum::Pi/Directions),float(2*WaterFftSpectrum::Pi));
            const int target=(i+(delta>=0?1:15))%16;
            const float fraction=turn*(std::min)(1.0f,std::abs(delta)*float(Directions/(2*WaterFftSpectrum::Pi)));
            const float value=a[cell*Channels+b*Directions+i]*Response(d(x,y),b);
            previous[cell*Channels+b*Directions+i]+=value*(1-fraction);
            previous[cell*Channels+b*Directions+target]+=value*fraction;
        }
    }
    a.swap(previous);
    // A radius-two wet-only Gaussian smooths amplitude, including the depth response.
    constexpr float kernel[5]={1,4,6,4,1};
    for(int axis=0;axis<2;++axis) {
        std::fill(previous.begin(),previous.end(),0);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            const size_t cell=size_t(y)*w+x;if(!wet[cell])continue;
            float total=0;
            for(int k=-2;k<=2;++k) {
                const int xx=x+(axis==0?k:0),yy=y+(axis==1?k:0);
                if(xx<x0 || xx>x1 || yy<y0 || yy>y1 || !wet[size_t(yy)*w+xx])continue;
                const size_t at=(size_t(yy)*w+xx)*Channels;total+=kernel[k+2];
                for(int c=0;c<Channels;++c)previous[cell*Channels+c]+=kernel[k+2]*a[at+c];
            }
            for(int c=0;c<Channels;++c)previous[cell*Channels+c]/=total;
        }
        a.swap(previous);
    }
    for(float &v:a)v=std::round((std::max)(0.0f,(std::min)(1.0f,v))*255)/255;
    result.width=w;result.height=h;result.amplitude.swap(a);return true;
}
// Engine word is A8R8G8B8; HLSL reads RGBA, so pack channels in that order.
inline uint32_t Word(const Field &f,size_t cell,int word) {
    if(word<0 || word>=Words || cell>=size_t(f.width)*f.height
        || f.amplitude.size()!=size_t(f.width)*f.height*Channels)return 0;
    const float *v=&f.amplitude[cell*Channels+word*4];
    return (uint32_t(std::lround(v[0]*255))<<16)|(uint32_t(std::lround(v[1]*255))<<8)
        |uint32_t(std::lround(v[2]*255))|(uint32_t(std::lround(v[3]*255))<<24);
}
}
