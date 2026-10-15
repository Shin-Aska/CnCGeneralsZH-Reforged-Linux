#pragma once
// Existing test_harness.h supplies CHECK. These fixtures use real physical domains,
// with separate expected coordinates/weights and armed rejected contributions.
#include "Platform/WaterCoastalView.h"
#include "WaterCoastalDomainTest.h"
namespace WaterCoastalViewTest {
using WaterCoastalDomainTest::Map;
using WaterCoastalDomainTest::Domain;
using WaterCoastalDomainTest::Result;
using WaterCoastalView::Mapping;
using WaterCoastalView::Lookup;
inline double Weight(const Lookup&v){double sum=0;for(float w:v.weight)sum+=w;return sum;}
inline bool Same(const std::vector<Lookup>&a,const std::vector<Lookup>&b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)for(unsigned c=0;c<4;++c)
        if(a[i].node[c]!=b[i].node[c]||a[i].weight[c]!=b[i].weight[c])return false;
    return true;
}
inline void ConstantAndCoordinates(){
    Map map(11,11);for(int y=0;y<11;++y)map.heights[size_t(y)*11]=12;
    if(!map.Build())return;Domain domain;CHECK(map.Install(domain)==Result::Ready);
    // Non-square pixel/world extents expose a transposed or half-texel-shifted chart.
    const Mapping mapping{20,20,20,30,7,9};std::vector<Lookup> lookup;
    CHECK(WaterCoastalView::Build(domain,mapping,10,lookup));CHECK(lookup.size()==63);
    if(lookup.size()!=63)return;
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<7;++x){
        const double wx=20+(x+.5)*20/7,wy=20+(y+.5)*30/9;
        const double gx=wx/5,gy=wy/5;const int ix=int(std::floor(gx)),iy=int(std::floor(gy));
        const double fx=gx-ix,fy=gy-iy;const auto&v=lookup[size_t(y)*7+x];
        CHECK(std::abs((wx-20)/20-(x+.5)/7)<1e-14);
        CHECK(std::abs((wy-20)/30-(y+.5)/9)<1e-14);
        double density=0,ageMoment=0;
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
            const unsigned c=dy*2+dx;const int id=domain.Topology().Find(ix+int(dx),iy+int(dy),10);
            const double expected=(dx?fx:1-fx)*(dy?fy:1-fy);
            CHECK(id>=0);CHECK(v.node[c]==(expected>0&&id>=0?unsigned(id)+1:0));
            CHECK(std::abs(v.weight[c]-expected)<3e-8);
            density+=v.weight[c]*3.25;ageMoment+=v.weight[c]*7.5;
        }
        CHECK(std::abs(Weight(v)-1)<8e-8);
        CHECK(std::abs(density-3.25)<3e-7&&std::abs(ageMoment-7.5)<6e-7);
    }
}
inline void GapAndSaddle(){
    Map map(11,11);map.heights[5*11+6]=12;
    map.polygons={Map::Rect(0,0,43,100,10),Map::Rect(44,20,76,80,10)};
    if(!map.Build())return;Domain domain;CHECK(map.Install(domain)==Result::Ready);
    CHECK(map.field.exposure[4*11+5]==0); // optical connectivity also respects the authored gap
    CHECK(domain.Topology().Find(8,8,10)>=0&&domain.Topology().Find(9,8,10)<0);
    std::vector<Lookup> lookup;
    // In connected polygon: retain its original half-weight, never normalize to one.
    CHECK(WaterCoastalView::Build(domain,{42,39.5,1,1,1,1},10,lookup));
    CHECK(lookup.size()==1);if(lookup.size()!=1)return;
    CHECK(lookup[0].node[0]==unsigned(domain.Topology().Find(8,8,10))+1);
    CHECK(lookup[0].weight[0]==.5f&&Weight(lookup[0])==.5);
    for(double ox:{43.,44.}){ // target in polygon gap, then inside disconnected pool
        CHECK(WaterCoastalView::Build(domain,{ox,39.5,1,1,1,1},10,lookup));
        CHECK(lookup.size()==1&&Weight(lookup[0])==0);
    }
    Map saddle(3,3);saddle.level=14;saddle.Full();saddle.heights={13,17,17,17,5,5,17,5,5};
    if(!saddle.Build())return;Domain sd;CHECK(saddle.Install(sd)==Result::Ready);
    CHECK(sd.Topology().Find(0,0,14)>=0&&sd.Topology().Find(1,1,14)>=0);
    CHECK(!sd.Support().At(0,0,14,.9,.9).Allows(0,0)); // wet endpoints, dry interior
    CHECK(WaterCoastalView::Build(sd,{4,4,1,1,1,1},14,lookup));
    CHECK(lookup.size()==1);if(lookup.size()!=1)return;
    CHECK(lookup[0].node[0]==0&&lookup[0].weight[0]==0);
    CHECK(lookup[0].node[3]==unsigned(sd.Topology().Find(1,1,14))+1);
    CHECK(std::abs(lookup[0].weight[3]-.81)<3e-8&&std::abs(Weight(lookup[0])-.81)<3e-8);
}
inline void BordersAndLevels(){
    Map map(11,11);map.border=2;map.Full();map.heights[5*11+2]=12;
    if(!map.Build())return;Domain domain;CHECK(map.Install(domain)==Result::Ready);
    CHECK(domain.Grid().originX==-20&&domain.Grid().originY==-20);
    std::vector<Lookup> lookup;
    CHECK(WaterCoastalView::Build(domain,{-20,0,10,10,4,4},10,lookup));CHECK(lookup.size()==16);
    if(lookup.size()!=16)return;
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){
        const double gx=(x+.5)*.5,gy=4+(y+.5)*.5;
        const int ix=int(std::floor(gx)),iy=int(std::floor(gy));const auto&v=lookup[size_t(y)*4+x];
        CHECK(std::abs(Weight(v)-1)<8e-8);
        CHECK(v.node[0]==unsigned(domain.Topology().Find(ix,iy,10))+1);
    }
    CHECK(WaterCoastalView::Build(domain,{-30,0,10,10,2,2},10,lookup));
    CHECK(lookup.size()==4);for(const auto&v:lookup)CHECK(Weight(v)==0); // no edge clamp
    CHECK(WaterCoastalView::Build(domain,{1099511627776.,0,64,64,2,2},10,lookup));
    for(const auto&v:lookup)CHECK(Weight(v)==0); // valid float chart, out of int32 lattice

    Map levels(11,11);for(int y=0;y<11;++y)levels.heights[size_t(y)*11+3]=12;
    levels.heights[5*11+8]=20;levels.polygons.push_back(Map::Rect(0,0,100,100,14));
    if(!levels.Build())return;WaterOpticalContext::Field upper;
    CHECK(WaterOpticalContext::Build(levels.heights.data(),11,11,0,10,1,14,levels.polygons,upper));
    Domain layered;CHECK(layered.Build(levels.heights.data(),11,11,0,10,1,levels.polygons,{{14,&upper},{10,&levels.field}})==Result::Ready);
    const int lowerId=layered.Topology().Find(5,8,10),upperId=layered.Topology().Find(5,8,14);
    CHECK(lowerId>=0&&upperId>=0&&lowerId!=upperId);
    const Mapping centred{24.5,39.5,1,1,1,1};
    for(double level:{10.,14.}){
        CHECK(WaterCoastalView::Build(layered,centred,level,lookup));CHECK(lookup.size()==1);
        if(lookup.size()!=1)return;
        CHECK(lookup[0].node[0]==unsigned(level==10?lowerId:upperId)+1&&lookup[0].weight[0]==1);
        CHECK(Weight(lookup[0])==1);
    }
    CHECK(WaterCoastalView::Build(layered,centred,11,lookup));CHECK(lookup.size()==1&&Weight(lookup[0])==0);
}
inline void BoundsAndAtomicity(){
    Domain empty;const Mapping good{0,0,64,64,1,1};
    std::vector<Lookup> output(2);output[0].node[0]=17;output[0].weight[0]=.75f;output[1].node[3]=29;output[1].weight[3]=.25f;
    const auto original=output;
    CHECK(WaterCoastalView::SameMapping(good,good));
    for(unsigned field=0;field<6;++field){Mapping changed=good;
        if(field==0)changed.originX=1;if(field==1)changed.originY=1;if(field==2)changed.widthWorld=32;
        if(field==3)changed.heightWorld=32;if(field==4)changed.widthPixels=2;if(field==5)changed.heightPixels=2;
        CHECK(!WaterCoastalView::SameMapping(good,changed));
    }
    for(unsigned kind=0;kind<11;++kind){Mapping bad=good;
        if(kind==0)bad.widthPixels=0;if(kind==1)bad.heightPixels=0;
        if(kind==2)bad.widthPixels=513;if(kind==3)bad.heightPixels=513;
        if(kind==4)bad.widthWorld=0;if(kind==5)bad.heightWorld=-1;
        if(kind==6)bad.originX=std::numeric_limits<double>::quiet_NaN();
        if(kind==7)bad.originY=.1; // cannot represent exact shader metadata
        if(kind==8)bad.widthWorld=double((std::numeric_limits<float>::denorm_min)());
        if(kind==9){bad.originX=double((std::numeric_limits<float>::max)());bad.widthWorld=bad.originX;}
        if(kind==10)bad.heightWorld=std::numeric_limits<double>::infinity();
        CHECK(!WaterCoastalView::ValidMapping(bad));
        CHECK(!WaterCoastalView::Build(empty,bad,10,output)&&Same(output,original));
    }
    for(double badLevel:{.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        CHECK(!WaterCoastalView::Build(empty,good,badLevel,output)&&Same(output,original));
    CHECK(!WaterCoastalView::Build(empty,good,10,output,WaterCoastalView::WorkPerPixel-1)&&Same(output,original));
    CHECK(WaterCoastalView::Build(empty,good,10,output,WaterCoastalView::WorkPerPixel));
    CHECK(output.size()==1&&Weight(output[0])==0);
    const Mapping largest{-256,-256,512,512,512,512};
    const auto one=output;
    CHECK(!WaterCoastalView::Build(empty,largest,10,output,WaterCoastalView::MaxBuildWork-1)&&Same(output,one));
    CHECK(WaterCoastalView::Build(empty,largest,10,output));CHECK(output.size()==512u*512u);
    bool allZero=true;for(const auto&v:output)for(unsigned c=0;c<4;++c)allZero=allZero&&v.node[c]==0&&v.weight[c]==0;
    CHECK(allZero);
}
inline void Check(){ConstantAndCoordinates();GapAndSaddle();BordersAndLevels();BoundsAndAtomicity();}
}
