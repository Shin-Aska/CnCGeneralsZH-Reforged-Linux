#pragma once
#include "Platform/WaterSharedField.h"
#include <limits>

// A/B experiment only: transport outcomes, not render-device-dependent fixtures.
namespace WaterSharedFieldTest {
inline float Total(const WaterSharedField::Field &f,int x,int y,int band) {
    float sum=0;for(int i=0;i<16;++i)sum+=f.amplitude[(size_t(y)*f.width+x)*32+band*16+i];return sum;
}
inline float MeanY(const WaterSharedField::Field &f,int x,int y) {
    float sum=0;for(int i=0;i<16;++i)sum+=WaterSharedField::Direction(i)[1]*f.amplitude[(size_t(y)*f.width+x)*32+16+i];
    return sum/(std::max)(1e-8f,Total(f,x,y,1));
}
inline void Check() {
    using namespace WaterSharedField;
    const int w=129,h=65;const double powers[2]={2,6},focused[2]={32,32};
    WaterMediumDepth::Field d;d.width=w;d.height=h;d.depth.assign(w*h,40);
    std::vector<uint8_t> wet(w*h,255);Field open,island,shallow,turned,again;
    std::array<float,Channels> seed;
    CHECK(Seed(1,0,powers,seed));
    CHECK(Build(wet.data(),d,10,1,0,powers,open));
    for(int band=0;band<2;++band) {
        float energy=0;
        for(int i=0;i<16;++i) {
            const float a=open.amplitude[(size_t(30)*w+64)*Channels+band*16+i];energy+=a*a;
            CHECK_NEAR(a,seed[band*16+i],1.0/255);
            CHECK_NEAR(a,open.amplitude[(size_t(10)*w+8)*Channels+band*16+i],1e-7);
            CHECK_NEAR(a,open.amplitude[(size_t(55)*w+120)*Channels+band*16+i],1e-7);
        }
        CHECK_NEAR(energy,1,.015);
    }
    CHECK(seed[16]>seed[0]); // Narrower medium power, with the same total RMS.
    CHECK_NEAR(seed[8],0,1e-8);
    const double isotropic[2]={0,0};std::array<float,Channels> uniform;
    CHECK(Seed(1,0,isotropic,uniform));for(float a:uniform)CHECK_NEAR(a,.25f,1e-7);
    CHECK(Build(wet.data(),d,10,1,0,focused,open));
    for(int y=29;y<=35;++y)for(int x=24;x<=28;++x)wet[y*w+x]=0;
    CHECK(Build(wet.data(),d,10,1,0,focused,island));
    const auto at=[&](const Field &f,int x,int y){return f.amplitude[(size_t(y)*w+x)*Channels+16];};
    CHECK(at(island,32,32)<at(open,32,32)*.8f);
    CHECK(at(island,110,32)>at(island,32,32)+.1f); // Shadow refills with travel distance.
    CHECK(at(island,36,36)>at(island,36,32));
    CHECK(at(island,36,36)<at(open,36,36)); // A soft, partially sheltered edge.
    for(int c=0;c<Channels;++c)CHECK_NEAR(island.amplitude[(size_t(32)*w+26)*Channels+c],0,1e-8);
    // A/B experiment only: wind regrowth refills the lee faster, medium (band 1) faster than long (band 0),
    // leaves open water at its seed, and still keeps land at zero.
    const float fetch[Bands]={800,200};
    Field regrown;
    CHECK(Build(wet.data(),d,10,1,0,focused,regrown,0,fetch));
    CHECK(at(regrown,40,32)>at(island,40,32)+.05f);
    CHECK(Total(regrown,40,32,1)/Total(open,40,32,1)>Total(regrown,40,32,0)/Total(open,40,32,0));
    CHECK_NEAR(Total(regrown,120,10,1),Total(open,120,10,1),2.0/255);
    for(int c=0;c<Channels;++c)CHECK_NEAR(regrown.amplitude[(size_t(32)*w+26)*Channels+c],0,1e-8);
    const float badFetch[Bands]={0,200};
    CHECK(!Build(wet.data(),d,10,1,0,focused,regrown,0,badFetch));
    std::fill(wet.begin(),wet.end(),255);
    std::fill(d.depth.begin(),d.depth.end(),2);
    CHECK(Build(wet.data(),d,10,1,0,powers,shallow));
    CHECK(Total(shallow,64,32,0)<Total(shallow,64,32,1));
    CHECK(Total(shallow,64,32,1)<Total(open,64,32,1)*.3f);
    // Beach to the north: shoreward -grad(D) is +Y, perpendicular to the beach.
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)d.depth[y*w+x]=float(8.0-0.08*y);
    CHECK(Build(wet.data(),d,10,1,0,powers,turned));
    CHECK(MeanY(turned,64,32)>MeanY(open,64,32)+.05f);
    CHECK(Build(wet.data(),d,10,1,0,powers,again));
    CHECK(again.amplitude==turned.amplitude);
    for(float a:turned.amplitude)CHECK(a>=0 && a<=1 && std::abs(a*255-std::round(a*255))<2e-5);
    for(int word=0;word<8;++word) {
        const uint32_t packed=Word(turned,32*w+64,word);
        const int shift[4]={16,8,0,24};
        for(int c=0;c<4;++c)CHECK_NEAR(float((packed>>shift[c])&255)/255,turned.amplitude[(32*w+64)*32+word*4+c],1e-8);
    }
    CHECK(!Build(nullptr,d,10,1,0,powers,again));CHECK(again.amplitude.empty());
    CHECK(!Build(wet.data(),d,0,1,0,powers,again));
    CHECK(!Build(wet.data(),d,10,0,0,powers,again));
    CHECK(!Build(wet.data(),d,10,std::numeric_limits<double>::infinity(),0,powers,again));
    CHECK(!Build(wet.data(),d,10,1,0,powers,again,33));
    const double invalid[2]={2,std::numeric_limits<double>::quiet_NaN()};
    CHECK(!Build(wet.data(),d,10,1,0,invalid,again));
    d.depth[5]=-1;CHECK(!Build(wet.data(),d,10,1,0,powers,again));
    d.depth[5]=std::numeric_limits<float>::quiet_NaN();CHECK(!Build(wet.data(),d,10,1,0,powers,again));
    d.depth.clear();CHECK(!Build(wet.data(),d,10,1,0,powers,again));
    CHECK_EQ(Word(again,0,0),0u);
}
}
