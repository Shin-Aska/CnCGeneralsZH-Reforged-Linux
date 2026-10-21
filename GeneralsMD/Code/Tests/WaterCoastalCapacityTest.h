#pragma once
#include "WaterCoastalDomainTest.h"
#include "Platform/WaterCoastalView.h"
namespace WaterCoastalCapacityTest {
using WaterCoastalDomainTest::Map;
using WaterCoastalDomainTest::Domain;
using WaterCoastalDomainTest::Result;
inline void Ridges(Map &m){
    m.level=4;m.Full();for(int y=0;y<m.height;++y)for(int x=4;x<m.width;x+=8)m.heights[size_t(y)*m.width+x]=12;
}
inline void Check(){
    // Actual physical coast throughout the map, not a narrowed collar or a forced
    // optical exposure bit. Every channel reaches a playable boundary in the fine flood.
    Map large(260,260);Ridges(large);
    large.heights[40*260+40]=large.heights[41*260+41]=1;
    large.heights[40*260+41]=large.heights[41*260+40]=9;
    // A one-unit polygon gap lies in the SECOND fine link of coarse 640->650.
    large.polygons={Map::Rect(0,0,647,2590,4),Map::Rect(648,0,2590,2590,4)};
    if(!large.Build())return;Domain coast;CHECK(large.Install(coast)==Result::Ready);
    CHECK(coast.Grid().spacing==10&&coast.Nodes().size()<=65536&&coast.Nodes().size()>50000);
    CHECK(WaterCoastalSurf::ValidGraph(coast.Nodes(),10));
    CHECK(coast.Cells().size()==coast.Nodes().size());if(coast.Cells().size()!=coast.Nodes().size())return;
    for(size_t i=0;i<coast.Cells().size();++i){
        const auto&c=coast.Cells()[i];const auto&n=coast.Nodes()[i];
        CHECK(c.x>=0&&c.y>=0&&c.x<259&&c.y<259);if(c.x<0||c.y<0||c.x>=259||c.y>=259)return;
        CHECK(n.world[0]==c.x*10&&n.world[1]==c.y*10&&n.world[2]==4);
        CHECK(n.context.depth==4-large.heights[size_t(c.y)*260+c.x]);
        CHECK(n.context.depth>.15&&n.context.depth<12&&n.context.distance<139);
    }
    const int a=coast.Topology().Find(64,80,4),b=coast.Topology().Find(65,80,4);
    CHECK(a>=0&&b>=0);if(a<0||b<0)return;
    CHECK((coast.Cells()[size_t(a)].connections&1)==0);
    CHECK(coast.Nodes()[size_t(a)].neighbour[1]==0&&coast.Nodes()[size_t(b)].neighbour[0]==0);
    CHECK(!coast.Support().At(64,80,4,1,0).Allows(0,0));
    const auto intervals=WaterShoreSource::Intervals(large.polygons,4,800,true);
    CHECK(WaterShoreSource::Covered(intervals,640,645));
    CHECK(!WaterShoreSource::Covered(intervals,645,650)); // armed second-link control
    const auto saddle=coast.Support().At(40,40,4,1,1);
    CHECK(saddle.tile);if(!saddle.tile)return;
    const float actual[4]={1,9,9,1};for(unsigned i=0;i<4;++i)CHECK(saddle.tile->heights[i]==actual[i]);
    CHECK(coast.Topology().Find(40,40,4)>=0&&coast.Topology().Find(41,41,4)>=0);
    CHECK(!saddle.Allows(0,0)); // exact terrain-cell saddle; endpoint-only coarse check fails
    size_t work=10000;CHECK(saddle.CoversRectangle(.9,.9,1,1,work)==WaterShoreSupport::AreaResult::Covered);
    work=10000;CHECK(saddle.CoversRectangle(0,0,1,1,work)==WaterShoreSupport::AreaResult::Unsupported);
    for(const auto&e:coast.Topology().Edges()){
        const auto&ca=coast.Cells()[e.a],&cb=coast.Cells()[e.b];
        const double ha=large.heights[size_t(ca.y)*260+ca.x],hb=large.heights[size_t(cb.y)*260+cb.x];
        CHECK(ha<4&&hb<4&&(ha+hb)*.5<4);
        CHECK(!(ca.x==64&&cb.x==65&&ca.y==cb.y));
    }
    const auto signature=WaterCoastalDomainTest::Signature(coast);Domain repeat;
    CHECK(large.Install(repeat)==Result::Ready&&WaterCoastalDomainTest::Signature(repeat)==signature);
    CHECK(large.Install(coast,1)==Result::Invalid&&WaterCoastalDomainTest::Signature(coast)==signature);

    Map widest(130,130);widest.spacing=128;widest.level=6;widest.Full();if(!widest.Build())return;
    widest.field.physicalShore.assign(widest.field.physicalShore.size(),1); // capacity-only armed metadata
    CHECK(widest.Install(coast)==Result::TooManyCells&&WaterCoastalDomainTest::Signature(coast)==signature);

    // Count all levels together and use one deterministic stride, preserving exact
    // level keys rather than consuming the capacity with whichever level arrived first.
    Map multi(130,130);Ridges(multi);multi.polygons.push_back(Map::Rect(0,0,1290,1290,5));
    if(!multi.Build())return;WaterOpticalContext::Field upper;
    CHECK(WaterOpticalContext::Build(multi.heights.data(),130,130,0,10,1,5,multi.polygons,upper));
    Domain levels;CHECK(levels.Build(multi.heights.data(),130,130,0,10,1,multi.polygons,{{5,&upper},{4,&multi.field}})==Result::Ready);
    CHECK(levels.Grid().spacing==10&&levels.Nodes().size()<65536&&WaterCoastalSurf::ValidGraph(levels.Nodes(),10));
    const int low=levels.Topology().Find(1,1,4),high=levels.Topology().Find(1,1,5);
    CHECK(low>=0&&high>=0&&low!=high);
    if(low>=0&&high>=0){CHECK(levels.Nodes()[size_t(low)].context.depth==4&&levels.Nodes()[size_t(high)].context.depth==5);}
    Domain sorted;CHECK(sorted.Build(multi.heights.data(),130,130,0,10,1,multi.polygons,{{4,&multi.field},{5,&upper}})==Result::Ready);
    CHECK(WaterCoastalDomainTest::Signature(sorted)==WaterCoastalDomainTest::Signature(levels));

    // Fine maps keep their original lattice; an entirely wet sea still has no
    // physical coast and must not gain source nodes merely to fill the atlas.
    Map smallMap(11,11);Ridges(smallMap);if(!smallMap.Build())return;Domain fine;
    CHECK(smallMap.Install(fine)==Result::Ready&&fine.Grid().spacing==5);
    Map sea(260,260);sea.level=4;sea.Full();if(!sea.Build())return;Domain empty;
    CHECK(sea.Install(empty)==Result::Ready&&empty.Nodes().empty()&&empty.Grid().spacing==5);
}
}
