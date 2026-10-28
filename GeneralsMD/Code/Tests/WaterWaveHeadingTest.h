#pragma once
#include "Platform/WaterWaveHeading.h"
#include <limits>

namespace WaterWaveHeadingTest {
using namespace WaterWaveHeading;
inline Vector Decode(uint32_t texel) {
    return {float((texel>>16)&255)/127.5f-1,float((texel>>8)&255)/127.5f-1};
}
inline float Dot(Vector a,Vector b){return a.x*b.x+a.y*b.y;}
inline void Check() {
    const int w=141,h=101;const size_t count=size_t(w)*h;
    std::vector<uint8_t> wet(count,255),exposure(count,255);Field field,again;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    CHECK(field.prevailing.x==BaseHeading.x && field.prevailing.y==BaseHeading.y);
    // An entirely wet grid has no synthetic land on any of its four edges.
    for(uint32_t texel:field.texels) {
        CHECK((texel>>24)==255 && (texel&255)==0);
        CHECK(Dot(Decode(texel),BaseHeading)>.99f);
    }
    // Straight north/south beach: sea is east of land, so travel is west/onshore.
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)wet[size_t(y)*w+x]=x>10?255:0;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    CHECK(field.prevailing.x<-.999f && std::abs(field.prevailing.y)<.001f);
    CHECK(Decode(field.texels[50*w+12]).x<-.99f);
    CHECK(Dot(Decode(field.texels[50*w+130]),field.prevailing)>.99f);
    CHECK((field.texels[50*w+12]&255)==255 && (field.texels[50*w+130]&255)==0);
    CHECK((field.texels[50*w+5]>>24)==0);
    // Symmetric parallel banks cancel in the prevailing estimate and at mid-channel.
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)wet[size_t(y)*w+x]=x>40 && x<100?255:0;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    CHECK(field.prevailing.x==BaseHeading.x && field.prevailing.y==BaseHeading.y);
    CHECK(Decode(field.texels[50*w+42]).x<-.85f);
    CHECK(Decode(field.texels[50*w+98]).x>.85f);
    CHECK(Dot(Decode(field.texels[50*w+70]),BaseHeading)>.99f);
    // Only exposure weights P: admitting the left bank selects west as the prevailing wind.
    std::fill(exposure.begin(),exposure.end(),0);
    for(int y=0;y<h;++y) for(int x=41;x<47;++x)exposure[size_t(y)*w+x]=255;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,again));
    CHECK(again.prevailing.x<-.999f && std::abs(again.prevailing.y)<.001f);
    // An enclosed lake has no exposed coast and retains the base prevailing heading,
    // while refraction still points toward each nearby shore.
    std::fill(exposure.begin(),exposure.end(),0);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x)wet[size_t(y)*w+x]=x>10 && x<130 && y>10 && y<90?255:0;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    CHECK(field.prevailing.x==BaseHeading.x && field.prevailing.y==BaseHeading.y);
    CHECK(Decode(field.texels[50*w+12]).x<-.85f);
    CHECK(Decode(field.texels[50*w+128]).x>.85f);
    CHECK(Build(wet.data(),exposure.data(),w,h,10,again));
    CHECK(field.texels==again.texels && field.prevailing.x==again.prevailing.x && field.prevailing.y==again.prevailing.y);
    for(size_t i=0;i<count;++i) {
        const Vector v=Decode(field.texels[i]);
        CHECK(std::isfinite(v.x) && std::isfinite(v.y));
        CHECK(std::abs(std::hypot(v.x,v.y)-1)<.006f);
        CHECK((field.texels[i]>>24)==(wet[i]?255u:0u));
    }
    // No shores in every direction: a symmetric exposed island's normals also cancel.
    std::fill(wet.begin(),wet.end(),255);std::fill(exposure.begin(),exposure.end(),255);
    for(int y=40;y<=60;++y)for(int x=60;x<=80;++x)wet[size_t(y)*w+x]=0;
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    CHECK(field.prevailing.x==BaseHeading.x && field.prevailing.y==BaseHeading.y);
    // Empty water is all invalid; even these RG values encode the fallback heading.
    std::fill(wet.begin(),wet.end(),0);
    CHECK(Build(wet.data(),exposure.data(),w,h,10,field));
    for(uint32_t texel:field.texels)CHECK((texel>>24)==0 && Dot(Decode(texel),BaseHeading)>.99f);
    CHECK(!Build(nullptr,exposure.data(),w,h,10,field) && field.texels.empty());
    CHECK(!Build(wet.data(),nullptr,w,h,10,field));
    CHECK(!Build(wet.data(),exposure.data(),1,h,10,field));
    CHECK(!Build(wet.data(),exposure.data(),MaxDimension+1,h,10,field));
    CHECK(!Build(wet.data(),exposure.data(),w,h,0,field));
    CHECK(!Build(wet.data(),exposure.data(),w,h,-1,field));
    CHECK(!Build(wet.data(),exposure.data(),w,h,std::numeric_limits<float>::infinity(),field));
    CHECK(!Build(wet.data(),exposure.data(),w,h,std::numeric_limits<float>::quiet_NaN(),field));
    const uint8_t smallWet[4]={0,255,255,255},smallExposure[4]={0,255,255,255};
    CHECK(Build(smallWet,smallExposure,2,2,std::numeric_limits<float>::min(),field));
    CHECK(Build(smallWet,smallExposure,2,2,std::numeric_limits<float>::max(),field));
}
}
