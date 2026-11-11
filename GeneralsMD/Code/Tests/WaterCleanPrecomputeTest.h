#pragma once
#include "Platform/WaterCleanPrecompute.h"
// Independent scalar references for immutable GPU transports and cache dependencies.
namespace WaterCleanPrecomputeTest {
inline void Check() {
    using namespace WaterCleanPrecompute;
    uint32_t state=0x619731bdu;
    auto random=[&](){state=1664525u*state+1013904223u;return float(state>>8)/16777216.f;};
    WaterMediumDepth::Field field;field.width=13;field.height=9;
    field.depth.resize(117);field.shoreX.resize(117);field.shoreY.resize(117);
    field.shoreConfidence.resize(117);field.shoreDistance.resize(117);
    for(size_t i=0;i<117;++i) {
        field.depth[i]=random()*40;field.shoreX[i]=uint8_t(random()*255);field.shoreY[i]=uint8_t(random()*255);
        field.shoreConfidence[i]=uint8_t(random()*127);field.shoreDistance[i]=random()*3000;
    }
    WaterCleanPrecompute::Image depth;CHECK(BuildDepth(field,16,depth));CHECK(depth.width==16 && depth.pixels.size()==256);
    auto at=[&](int x,int y){return WaterMediumDepth::Encode(field.depth[size_t((std::min)(y,8))*13+(std::min)(x,12)])*(40.f/255.f);};
    for(int n=0;n<4000;++n) {
        const float x=n<16?float(n):random()*15,y=n<16?15.f:random()*15;
        const int ix=(std::min)(int(x),14),iy=(std::min)(int(y),14);const float u=x-ix,v=y-iy;
        const float a=at(ix,iy),b=at(ix+1,iy),c=at(ix,iy+1),d=at(ix+1,iy+1);
        const auto sample=SampleDepth(depth,x,y);
        CHECK_NEAR(sample.r,(a*(1-u)+b*u)*(1-v)+(c*(1-u)+d*u)*v,2e-5);
        CHECK_NEAR(sample.g,(b-a)*(1-v)+(d-c)*v,1e-5);
        CHECK_NEAR(sample.b,(c-a)*(1-u)+(d-b)*u,1e-5);
    }
    // Exact upper endpoints retain the original inclusive inside derivative.
    WaterMediumDepth::Field ramp;ramp.width=ramp.height=2;ramp.depth={0,4,8,12};WaterCleanPrecompute::Image rampDepth;
    CHECK(BuildDepth(ramp,2,rampDepth));const auto edge=SampleDepth(rampDepth,1,1);
    CHECK_NEAR(edge.g,WaterMediumDepth::Encode(4)*(40.f/255.f),1e-6);
    CHECK_NEAR(edge.b,WaterMediumDepth::Encode(8)*(40.f/255.f),1e-6);
    CHECK_NEAR(SampleDepth(depth,-100,100).r,at(0,15),1e-6);
    std::vector<uint32_t> words(117*5);
    for(size_t i=0;i<117;++i) {
        words[i*5]=WaterWaveHeading::EncodeGradient(random()*16-8)|(WaterWaveHeading::EncodeGradient(random()*16-8)<<24);
        words[i*5+1]=(i%3?0xff000000u:0u);
    }
    for(int n=0;n<1000;++n) {
        const float x=random()*20-2,y=random()*20-2;
        const float cx=(std::max)(0.f,(std::min)(15.f,x)),cy=(std::max)(0.f,(std::min)(15.f,y));
        const int ix=int(cx),iy=int(cy);const float u=cx-ix,v=cy-iy;float gx=0,gy=0,total=0;
        for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
            const size_t p=(size_t((std::min)(iy+j,8))*13+(std::min)(ix+k,12))*5;
            const float w=(k?u:1-u)*(j?v:1-v);if((words[p+1]>>24)<128)continue;
            gx+=(float(words[p]&255)-128)*(8.f/127.f)*w;gy+=(float(words[p]>>24)-128)*(8.f/127.f)*w;total+=w;
        }
        const auto sample=SampleHeading(words,13,9,16,x,y);
        CHECK_NEAR(sample.x,total?gx/total:0,1e-6);CHECK_NEAR(sample.y,total?gy/total:0,1e-6);
    }
    WaterCleanShore::Settings settings;settings.tile=100;settings.follow=.5f;
    Key key=MakeKey(3,settings,0,false,false);
    const Key original=key;settings.sharp+=1;settings.bands+=1;
    CHECK(MakeKey(3,settings,0,false,false)==original);
    auto changed=original;changed.heading++;CHECK(changed!=original);
    changed=original;changed.tile++;CHECK(changed!=original);
    changed=original;changed.follow+=.1f;CHECK(changed!=original);
    changed=original;changed.turn+=.1f;CHECK(changed!=original);
    changed=original;changed.reach++;CHECK(changed!=original);
    changed=original;changed.maxTurn++;CHECK(changed!=original);
    changed=original;changed.travelAngle++;CHECK(changed!=original);
    changed=original;changed.manual=true;CHECK(changed!=original);
    changed=original;changed.force=true;CHECK(changed!=original);
    WaterTuning::Values values{};for(int i=0;i<WaterTuning::Count;++i)values.value[i]=WaterTuning::Keys[i].initial;
    values.value[WaterTuning::TravelMode]=1;
    const auto tuningKey=MakeKey(7,values,false,true);
    for(int i=0;i<WaterTuning::Count;++i) {
        auto edited=values;
        edited.value[i]=values.value[i]==WaterTuning::Keys[i].high?WaterTuning::Keys[i].low:WaterTuning::Keys[i].high;
        const bool relevant=i==WaterTuning::ShoreTile || i==WaterTuning::HeadingFollow || i==WaterTuning::ShoreTurn
            || i==WaterTuning::ShoreReach || i==WaterTuning::ShoreMaxTurn || i==WaterTuning::TravelMode || i==WaterTuning::TravelAngle;
        CHECK((MakeKey(7,edited,false,true)!=tuningKey)==relevant);
    }
    for(int n=0;n<50;++n) {
        key=original;key.tile=100+random()*900;key.travelAngle=n<2?n*180.:random()*360;
        key.follow=random();key.turn=random();key.force=n%2;key.live=n%3;
        Rotation rotation;CHECK(BuildRotation(field,words,16,2,10,key,rotation));
        CHECK(rotation.keyed && rotation.key==key && rotation.image.revision);
        const float low=-20,high=15*10.f-20;
        for(float yy:{low,high})for(float xx:{low,high}) {
            const float u=(xx-yy*.577350269f)/key.tile,v=yy*1.154700538f/key.tile;
            const int ix=int(std::floor(u)),iy=int(std::floor(v));
            CHECK(ix>=rotation.originX+1 && iy>=rotation.originY+1);
            CHECK(ix+1<rotation.originX+rotation.image.width && iy+1<rotation.originY+rotation.image.height);
        }
        // Mirror the shader's operation order, including normalized heading before atan2.
        const double wind=WaterTuning::NormalizeAngle(key.travelAngle-180);
        const double liveRadians=wind*WaterFftSpectrum::Pi/180;
        const float bakedRadians=float((wind-45)*WaterFftSpectrum::Pi/180)+0.7853981633974483f;
        const float bx=key.live?-float(std::cos(liveRadians)):-std::cos(bakedRadians),by=key.live?-float(std::sin(liveRadians)):-std::sin(bakedRadians);
        for(int y=0;y<rotation.image.height;++y)for(int x=0;x<rotation.image.width;++x) {
            const float px=(float(x+rotation.originX)+.5f*float(y+rotation.originY))*key.tile;
            const float py=.866025404f*float(y+rotation.originY)*key.tile;
            auto g=SampleHeading(words,13,9,16,(px+20)/10,(py+20)/10);const float len=std::hypot(g.x,g.y);
            const float dx=len>1e-5?g.x/len:bx,dy=len>1e-5?g.y/len:by;
            auto turn=[](float ax,float ay,float cx,float cy) {const float cross=ax*cy-ay*cx,cosine=ax*cx+ay*cy;return cross==0 && cosine<0?3.141592654f:std::atan2(cross,cosine);};
            const auto shore=WaterCleanShore::SampleField(field,(px+20)/10,(py+20)/10,bx,by);
            const float reach=(std::max)(0.f,(std::min)(1.f,shore.distance/key.reach));
            const float c=(std::max)(0.f,(std::min)(1.f,(shore.confidence-.02f)/.13f));
            float angle=turn(dx,dy,shore.x,shore.y)*key.turn*c*c*(3-2*c)*(1-reach*reach*(3-2*reach));
            if(key.force && key.turn>0)angle=turn(dx,dy,shore.x,shore.y);
            const float limit=key.maxTurn*(3.141592654f/180.f);
            angle=turn(bx,by,dx,dy)*key.follow+(std::max)(-limit,(std::min)(limit,angle));
            const auto &actual=rotation.image.pixels[size_t(y)*rotation.image.width+x];
            CHECK_NEAR(actual.b,angle,2e-6);CHECK_NEAR(actual.r,std::cos(angle),2e-6);CHECK_NEAR(actual.g,std::sin(angle),2e-6);CHECK(actual.a==1);
        }
    }
    // Exact antiparallel + fractional follow: a signed-zero mistake reverses
    // the chosen half turn. Both live constants and baked FftParameters agree.
    for(bool live:{false,true})for(bool force:{false,true}) {
        auto cardinal=original;cardinal.travelAngle=-180;cardinal.live=live;cardinal.force=force;cardinal.follow=.5f;cardinal.turn=0;
        std::vector<uint32_t> east(117*5,0);
        for(size_t i=0;i<117;++i){east[5*i]=WaterWaveHeading::EncodeGradient(1)|(WaterWaveHeading::EncodeGradient(0)<<24);east[5*i+1]=0xff000000u;}
        Rotation rotation;CHECK(BuildRotation(field,east,16,2,10,cardinal,rotation));
        for(const auto &p:rotation.image.pixels) {CHECK_NEAR(p.b,3.141592654f*.5f,1e-6);CHECK_NEAR(p.g,1,1e-6);}
    }
    CHECK(!BuildDepth(field,8,depth));
    Rotation invalid;key.tile=0;CHECK(!BuildRotation(field,words,16,2,10,key,invalid));
}
}
