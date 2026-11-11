/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanShore.h"
#include "Platform/WaterWaveHeading.h"
#include <limits>

// Immutable map fields. No clocks or GPU objects: tuning changes only invalidate
// the angle lattice; packed source bytes remain the reference for that rebuild.
namespace WaterCleanPrecompute {
struct Pixel { float r,g,b,a; };
static_assert(sizeof(Pixel)==16,"RGBA32F transport");
struct Image {
    int width=0,height=0;
    std::vector<Pixel> pixels;
    uint64_t revision=0;
};
inline uint64_t NextRevision() {static uint64_t revision=0;return ++revision;}
struct Key {
    uint64_t heading=0;
    float tile=0,follow=0,turn=0,reach=0,maxTurn=0;
    double travelAngle=0;
    bool manual=false,force=false,live=false;
    bool operator==(const Key &o) const {
        return heading==o.heading && tile==o.tile && follow==o.follow && turn==o.turn
            && reach==o.reach && maxTurn==o.maxTurn && travelAngle==o.travelAngle
            && manual==o.manual && force==o.force && live==o.live;
    }
    bool operator!=(const Key &o) const {return !(*this==o);}
};
inline Key MakeKey(uint64_t heading,const WaterCleanShore::Settings &s,double angle,bool manual,bool force,bool live=false) {
    return {heading,s.tile,s.follow,s.turn,s.reach,s.maxTurn,angle,manual,force,live};
}
// Pure tuning dependency projection, also used by the cache-trigger tests.
inline Key MakeKey(uint64_t heading,const WaterTuning::Values &values,bool force,bool live) {
    WaterCleanShore::Settings s;
    s.tile=(std::max)(100.f,(std::min)(3000.f,float(values[WaterTuning::ShoreTile])));
    s.turn=(std::max)(0.f,(std::min)(1.f,float(values[WaterTuning::ShoreTurn])));
    s.reach=(std::max)(100.f,(std::min)(3000.f,float(values[WaterTuning::ShoreReach])));
    s.follow=values[WaterTuning::HeadingFollow];s.maxTurn=values[WaterTuning::ShoreMaxTurn];
    return MakeKey(heading,s,WaterTuning::EffectiveTravelAngle(values),values[WaterTuning::TravelMode]!=0,force,live);
}
// Preserve the shader's wind construction, including signed zero at cardinal
// antiparallel headings. Replacing this with cos(travel) can choose the other
// shortest turn for fractional heading following.
inline WaterWaveHeading::Vector BaseDirection(double travel,bool live) {
    const double wind=WaterTuning::NormalizeAngle(travel-180);
    if(live) {
        const double radians=wind*WaterFftSpectrum::Pi/180;
        return {-float(std::cos(radians)),-float(std::sin(radians))};
    }
    const float rotation=float((wind-45)*WaterFftSpectrum::Pi/180);
    const float radians=rotation+0.7853981633974483f;
    return {-std::cos(radians),-std::sin(radians)};
}
inline bool BuildDepth(const WaterMediumDepth::Field &source,int side,Image &out) {
    if(source.width<2 || source.height<2 || side<source.width || side<source.height
        || side>WaterMediumDepth::MaxDimension || source.depth.size()!=size_t(source.width)*source.height)return false;
    Image next;next.width=next.height=side;next.pixels.resize(size_t(side)*side);
    auto value=[&](int x,int y) {
        return WaterMediumDepth::Encode(source.depth[size_t((std::min)(y,source.height-1))*source.width+(std::min)(x,source.width-1)])*(WaterMediumDepth::Cap/255.f);
    };
    for(int y=0;y<side;++y)for(int x=0;x<side;++x) {
        const float a=value(x,y),b=value((std::min)(x+1,side-1),y),c=value(x,(std::min)(y+1,side-1)),d=value((std::min)(x+1,side-1),(std::min)(y+1,side-1));
        next.pixels[size_t(y)*side+x]={a,b-a,c-a,d-b-c+a};
    }
    next.revision=NextRevision();out=std::move(next);return true;
}
// CPU mirror of two hardware reads (filtered R, centre GBA). Gradients are per
// cell; scale and the original padded-domain inside test belong to the caller.
inline Pixel SampleDepth(const Image &image,float gx,float gy) {
    if(image.width<2 || image.height<2 || image.pixels.size()!=size_t(image.width)*image.height)return {};
    gx=(std::max)(0.f,(std::min)(float(image.width-1),gx));gy=(std::max)(0.f,(std::min)(float(image.height-1),gy));
    const int x=(std::min)(int(gx),image.width-2),y=(std::min)(int(gy),image.height-2);const float fx=gx-x,fy=gy-y;
    const auto &a=image.pixels[size_t(y)*image.width+x];
    return {a.r+a.g*fx+a.b*fy+a.a*fx*fy,a.g+a.a*fy,a.b+a.a*fx,1};
}
// Exact packed atlas gradient: word 0 contains BA gradient bytes, word 1 alpha
// gates validity. Dry corners contribute neither vector nor interpolation weight.
inline WaterWaveHeading::Vector SampleHeading(const std::vector<uint32_t> &words,int width,int height,int side,float gx,float gy) {
    if(width<2 || height<2 || side<width || side<height || words.size()!=size_t(width)*height*5)return {0,0};
    gx=(std::max)(0.f,(std::min)(float(side-1),gx));gy=(std::max)(0.f,(std::min)(float(side-1),gy));
    const int x=int(gx),y=int(gy);const float fx=gx-x,fy=gy-y;float sum=0,vx=0,vy=0;
    for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
        const int xx=(std::min)(x+k,width-1),yy=(std::min)(y+j,height-1);
        const size_t n=(size_t(yy)*width+xx)*5;
        if((words[n+1]>>24)<128)continue;
        const float w=(k?fx:1-fx)*(j?fy:1-fy);const auto g=WaterWaveHeading::DecodeGradient(words[n]);
        vx+=g.x*w;vy+=g.y*w;sum+=w;
    }
    return sum>0?WaterWaveHeading::Vector{vx/sum,vy/sum}:WaterWaveHeading::Vector{0,0};
}
struct Rotation {
    Image image;
    int originX=0,originY=0;
    Key key;
    bool keyed=false;
};
inline bool BuildRotation(const WaterMediumDepth::Field &shore,const std::vector<uint32_t> &heading,int side,int border,
    float spacing,const Key &key,Rotation &out) {
    if(side<2 || side>WaterMediumDepth::MaxDimension || border<0 || border>=side || shore.width<2 || shore.height<2
        || shore.width>side || shore.height>side || !std::isfinite(spacing) || spacing<=0
        || !std::isfinite(key.tile) || key.tile<=0 || !std::isfinite(key.travelAngle)
        || !std::isfinite(key.follow) || !std::isfinite(key.turn) || !std::isfinite(key.reach) || key.reach<=0
        || !std::isfinite(key.maxTurn) || heading.size()!=size_t(shore.width)*shore.height*5)return false;
    const float lo=-float(border)*spacing,hi=float(side-1)*spacing-float(border)*spacing;
    float minX=(std::numeric_limits<float>::max)(),minY=minX,maxX=-minX,maxY=-minX;
    for(float y:{lo,hi})for(float x:{lo,hi}) {
        const float u=(x-y*.577350269f)/key.tile,v=y*1.154700538f/key.tile;
        minX=(std::min)(minX,u);minY=(std::min)(minY,v);maxX=(std::max)(maxX,u);maxY=(std::max)(maxY,v);
    }
    if(!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY)
        || minX<-1000000000.f || minY<-1000000000.f || maxX>1000000000.f || maxY>1000000000.f)return false;
    Rotation next;next.originX=int(std::floor(minX))-1;next.originY=int(std::floor(minY))-1;
    next.image.width=int(std::floor(maxX))+3-next.originX;next.image.height=int(std::floor(maxY))+3-next.originY;
    if(next.image.width<2 || next.image.height<2 || next.image.width>2048 || next.image.height>2048
        || size_t(next.image.width)*next.image.height>WaterMediumDepth::MaxCells)return false;
    next.image.pixels.resize(size_t(next.image.width)*next.image.height);
    WaterCleanShore::Settings settings;settings.tile=key.tile;settings.follow=key.follow;settings.turn=key.turn;settings.reach=key.reach;settings.maxTurn=key.maxTurn;
    // fftShoreWind negates the wind direction; WindAngle is travel minus pi.
    const auto base=BaseDirection(key.travelAngle,key.live);
    const float baseX=base.x,baseY=base.y;
    for(int y=0;y<next.image.height;++y)for(int x=0;x<next.image.width;++x) {
        const float i=float(x+next.originX),j=float(y+next.originY);
        const float px=(i+.5f*j)*key.tile,py=.866025404f*j*key.tile;
        const float gx=(px+float(border)*spacing)/spacing,gy=(py+float(border)*spacing)/spacing;
        const auto g=SampleHeading(heading,shore.width,shore.height,side,gx,gy);
        const auto s=WaterCleanShore::SampleField(shore,gx,gy,baseX,baseY);
        const float magnitude=std::hypot(g.x,g.y);
        const float fieldX=magnitude>1e-5f?g.x/magnitude:baseX,fieldY=magnitude>1e-5f?g.y/magnitude:baseY;
        float angle=WaterCleanShore::HeadingAngle(baseX,baseY,fieldX,fieldY,key.follow)+WaterCleanShore::VertexAngle(fieldX,fieldY,s,settings);
        if(key.force && key.turn>0) {
            const float n=std::hypot(g.x,g.y),dx=n>1e-5f?g.x/n:baseX,dy=n>1e-5f?g.y/n:baseY;
            const float cross=dx*s.y-dy*s.x,cosine=dx*s.x+dy*s.y;
            const float delta=cross==0 && cosine<0?3.141592654f:std::atan2(cross,cosine),limit=key.maxTurn*(3.141592654f/180.f);
            angle=WaterCleanShore::HeadingAngle(baseX,baseY,fieldX,fieldY,key.follow)+(std::max)(-limit,(std::min)(limit,delta));
        }
        next.image.pixels[size_t(y)*next.image.width+x]={std::cos(angle),std::sin(angle),angle,1};
    }
    next.key=key;next.keyed=true;next.image.revision=NextRevision();out=std::move(next);return true;
}
}
