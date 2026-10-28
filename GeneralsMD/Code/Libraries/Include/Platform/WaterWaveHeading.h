/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace WaterWaveHeading {
constexpr int MaxDimension=2048;
constexpr size_t MaxCells=1024u*1024u;
constexpr float DistanceCap=480.0f, PrevailingShoreRange=60.0f;
constexpr float RefractionStart=40.0f, RefractionEnd=360.0f, BlurRadius=80.0f;
constexpr float PrevailingThreshold=0.15f, NormalizeThreshold=0.05f;
constexpr int BlurPasses=3;
// Linux-only emulated constant register: after material optics c190, before hull packets c192.
constexpr unsigned PrevailingRegister=191;
struct Vector { float x,y; };
// SpectrumHlsl uses h0*exp(+i*omega*t), ButterflyHlsl exp(+i*k.x).
// Constant phase k.x+omega*t therefore travels along -k: wind (1,1) means (-1,-1).
constexpr Vector BaseHeading={-0.7071067811865475f,-0.7071067811865475f};
inline bool Enabled() {
    const char *value=std::getenv("ZH_WATER_HEADING");
    return !value || std::strcmp(value,"0")!=0;
}
struct Field {
    Vector prevailing=BaseHeading;
    std::vector<uint32_t> texels; // A8R8G8B8; invalid dry texels also retain prevailing RG
};
inline float Rotation(Vector heading) {
    return std::atan2(BaseHeading.x*heading.y-BaseHeading.y*heading.x,
                     BaseHeading.x*heading.x+BaseHeading.y*heading.y);
}
inline bool Build(const uint8_t *wet,const uint8_t *exposure,int width,int height,float spacing,Field &result) {
    result=Field{};
    if(!wet || !exposure || width<2 || height<2 || width>MaxDimension || height>MaxDimension
        || size_t(width)*height>MaxCells || !std::isfinite(spacing) || spacing<=0) return false;
    const size_t count=size_t(width)*height;
    // Clamp before arithmetic/conversion, including finite but extreme spacings.
    const float step=(std::min)(spacing,DistanceCap), diagonal=(std::min)(step*1.41421356237f,DistanceCap);
    std::vector<float> distance(count,DistanceCap),strength(count,0);
    for(size_t i=0;i<count;++i) if(!wet[i]) distance[i]=0;
    auto relax=[&](int i,int j,float cost){distance[i]=(std::min)(distance[i],distance[j]+cost);};
    // Borders are absent neighbours, never synthetic land.
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int i=y*width+x;
        if(x>0)relax(i,i-1,step);
        if(y>0){relax(i,i-width,step);if(x>0)relax(i,i-width-1,diagonal);if(x+1<width)relax(i,i-width+1,diagonal);}
    }
    for(int y=height-1;y>=0;--y) for(int x=width-1;x>=0;--x) {
        const int i=y*width+x;
        if(x+1<width)relax(i,i+1,step);
        if(y+1<height){relax(i,i+width,step);if(x>0)relax(i,i+width-1,diagonal);if(x+1<width)relax(i,i+width+1,diagonal);}
    }
    std::vector<Vector> normal(count,{0,0}),values(count,{0,0}),scratch(count,{0,0});
    double meanX=0,meanY=0,weight=0;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const int i=y*width+x;if(!wet[i])continue;
        const int left=(std::max)(0,x-1),right=(std::min)(width-1,x+1);
        const int bottom=(std::max)(0,y-1),top=(std::min)(height-1,y+1);
        // One-sided differences at the texture edge, central differences elsewhere.
        const float gx=(distance[y*width+right]-distance[y*width+left])/float(right-left);
        const float gy=(distance[top*width+x]-distance[bottom*width+x])/float(top-bottom);
        const float length=std::hypot(gx,gy);
        if(length>0.00001f) {
            normal[i]={-gx/length,-gy/length};
            if(distance[i]<PrevailingShoreRange) {
                const double w=double(exposure[i])/255.0;
                meanX+=normal[i].x*w;meanY+=normal[i].y*w;weight+=w;
            }
        }
        const float t=(std::max)(0.0f,(std::min)(1.0f,(distance[i]-RefractionStart)/(RefractionEnd-RefractionStart)));
        strength[i]=1-t*t*(3-2*t);
    }
    const double meanLength=std::hypot(meanX,meanY);
    if(weight>0 && meanLength>=PrevailingThreshold*weight)
        result.prevailing={float(meanX/meanLength),float(meanY/meanLength)};
    for(size_t i=0;i<count;++i) if(wet[i]) {
        // A distance ridge has no unique onshore normal. Leave its local value at P.
        const float s=normal[i].x==0 && normal[i].y==0?0:strength[i];
        values[i]={result.prevailing.x*(1-s)+normal[i].x*s,result.prevailing.y*(1-s)+normal[i].y*s};
    }
    const int radius=int((std::min)(double(MaxDimension),std::ceil(double(BlurRadius)/spacing)));
    // Sliding windows keep all three wet-only separable passes O(cells), even at tiny spacing.
    for(int pass=0;pass<BlurPasses;++pass) for(int axis=0;axis<2;++axis) {
        const int lines=axis?width:height,length=axis?height:width,stride=axis?width:1;
        for(int line=0;line<lines;++line) {
            const int base=axis?line:line*width;
            double sx=0,sy=0;int weights=0;
            auto add=[&](int j,int sign){const int i=base+j*stride;if(wet[i]){sx+=sign*values[i].x;sy+=sign*values[i].y;weights+=sign;}};
            for(int j=0;j<(std::min)(length,radius+1);++j)add(j,1);
            for(int j=0;j<length;++j) {
                const int i=base+j*stride;
                scratch[i]=wet[i] && weights?Vector{float(sx/weights),float(sy/weights)}:Vector{0,0};
                if(j-radius>=0)add(j-radius,-1);
                if(j+radius+1<length)add(j+radius+1,1);
            }
        }
        values.swap(scratch);
    }
    auto encode=[](float value){return uint32_t((std::max)(0.0f,(std::min)(255.0f,(0.5f+0.5f*value)*255.0f+0.5f)));};
    result.texels.resize(count);
    for(size_t i=0;i<count;++i) {
        const float length=std::hypot(values[i].x,values[i].y);
        const Vector heading=wet[i] && length>=NormalizeThreshold?Vector{values[i].x/length,values[i].y/length}:result.prevailing;
        result.texels[i]=(wet[i]?0xff000000u:0u)|(encode(heading.x)<<16)|(encode(heading.y)<<8)
            |uint32_t(strength[i]*255.0f+0.5f);
    }
    return true;
}
}
