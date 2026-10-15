#pragma once
#include "Platform/WaterCoastalView.h"
#include "WaterCoastalDomainTest.h"
namespace WaterCoastalFootprintTest {
using WaterCoastalDomainTest::Map;
using WaterCoastalDomainTest::Domain;
using WaterCoastalDomainTest::Result;
using WaterCoastalView::Lookup;
using Area=WaterShoreSupport::AreaResult;
inline double Weight(const Lookup &v){double r=0;for(float w:v.weight)r+=w;return r;}
inline bool PointIn(const WaterOpticalContext::Polygon&p,double x,double y){
    bool in=false;for(size_t i=0,j=p.points.size()-1;i<p.points.size();j=i++){
        const auto&a=p.points[j],&b=p.points[i];if((a.y>y)!=(b.y>y))in=in^(x<a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y));
    }return in;
}
inline void Check(){
    Map gap(11,11);gap.heights[5*11+6]=12;
    gap.polygons={Map::Rect(0,0,43,100,10),Map::Rect(44,20,76,80,10)};
    if(!gap.Build())return;Domain domain;CHECK(gap.Install(domain)==Result::Ready);
    const auto point=domain.Support().At(8,8,10,.6,.2);
    CHECK(point.Allows(0,0)&&point.Allows(0,1));
    CHECK(domain.Topology().Find(8,8,10)>=0&&domain.Topology().Find(9,8,10)<0);
    // The centre-only lookup at (43,41) keeps original weight .4. Linear sampling
    // at (44.25,41) then borrows .375*.4=.15 across the gap into the rejected pool.
    const double oldBorrow=((1-.6)*(1-.2)+(1-.6)*.2)*.375;
    CHECK(std::abs(oldBorrow-.15)<1e-14&&gap.field.exposure[4*11+5]==0);
    std::vector<Lookup> output;
    CHECK(WaterCoastalView::Build(domain,{20,30,48,20,24,10},10,output));CHECK(output.size()==240);
    if(output.size()!=240)return;
    const size_t row=5*24;
    CHECK(std::abs(Weight(output[row+9])-1)<1e-7); // (39,41): complete wet footprint
    CHECK(Weight(output[row+10])==0&&Weight(output[row+11])==0&&Weight(output[row+12])==0);
    CHECK(.375*Weight(output[row+11])+.625*Weight(output[row+12])==0);
    unsigned retained=0;for(const auto&v:output)retained+=Weight(v)>0;CHECK(retained>20);
    const auto &retainedLookup=output[row+9];const double fx=.8,fy=.2;
    for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx)
        CHECK(std::abs(retainedLookup.weight[dy*2+dx]-(dx?fx:1-fx)*(dy?fy:1-fy))<3e-8);

    // A thin notch has no polygon vertex inside the queried rectangle; its corners
    // AND centre are inside the polygon. Full segment/rectangle intersection is essential.
    WaterOpticalContext::Polygon notch{{{0,0,10},{10,0,10},{10,10,10},{2.25f,10,10},
        {2.25f,1,10},{2.125f,1,10},{2.125f,10,10},{0,10,10}}};
    for(double x:{2.,3.})for(double y:{2.,4.})CHECK(PointIn(notch,x,y));CHECK(PointIn(notch,2.5,3));
    for(const auto&p:notch.points)CHECK(!(p.x>2&&p.x<3&&p.y>2&&p.y<4));
    WaterShoreSupport::Domain support;
    const WaterShoreHistory::Grid grid{0,0,5};
    const std::vector<WaterShoreHistory::Cell> cells{{0,0,10,0,0}};
    CHECK(support.Build(grid,cells,2,2,{notch},[](double,double){return 0.f;}));
    size_t work=10000;CHECK(support.At(0,0,10,0,0).CoversRectangle(.4,.4,.6,.8,work)==Area::Unsupported);
    work=10000;CHECK(support.At(0,0,10,0,0).CoversRectangle(.6,.4,.8,.8,work)==Area::Covered);
    work=10000;CHECK(support.At(0,0,11,0,0).CoversRectangle(.6,.4,.8,.8,work)==Area::Unsupported);
    // Union-only seams are explicitly conservative: no single polygon covers this
    // rectangle even though the union does. No accidental acceptance through parity.
    CHECK(support.Build(grid,cells,2,2,{Map::Rect(0,0,2.5,10,10),Map::Rect(2.5,0,10,10,10)},[](double,double){return 0.f;}));
    work=10000;CHECK(support.At(0,0,10,0,0).CoversRectangle(.4,.4,.6,.8,work)==Area::Unsupported);

    Map ridge(11,11);for(int y=0;y<11;++y)ridge.heights[size_t(y)*11+5]=12;
    if(!ridge.Build())return;Domain rd;CHECK(ridge.Install(rd)==Result::Ready);
    CHECK(rd.Topology().Find(9,8,10)>=0&&rd.Topology().Find(11,8,10)>=0); // both centres wet
    CHECK(WaterCoastalView::Build(rd,{40,35,20,10,2,1},10,output));CHECK(output.size()==2);
    if(output.size()!=2)return;CHECK(Weight(output[0])==0&&Weight(output[1])==0); // dry ridge between them

    Map full(11,11);for(int y=0;y<11;++y)full.heights[size_t(y)*11]=12;
    if(!full.Build())return;Domain fd;CHECK(full.Install(fd)==Result::Ready);
    output.assign(1,Lookup{});output[0].node[0]=17;output[0].weight[0]=.75f;
    auto preserved=[&](){return output.size()==1&&output[0].node[0]==17&&output[0].weight[0]==.75f;};
    CHECK(!WaterCoastalView::Build(fd,{20,20,20,20,8,8},10,output,WaterCoastalView::MaxBuildWork,0)&&preserved());
    CHECK(!WaterCoastalView::Build(fd,{-32,-32,128,128,1,1},10,output)&&preserved()); // per-footprint tile cap
    CHECK(WaterCoastalView::Build(fd,{20,20,20,20,8,8},10,output));CHECK(output.size()==64);
    for(const auto&v:output)CHECK(std::abs(Weight(v)-1)<1e-7);
}
}
