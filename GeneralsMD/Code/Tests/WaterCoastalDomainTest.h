#pragma once
// Physical-domain regressions use armed optical controls and actual map geometry.
// Included after test_harness.h; no custom runner or process exit.
#include "Platform/WaterCoastalDomain.h"
namespace WaterCoastalDomainTest {
using Domain=WaterCoastalDomain::Domain;
using Result=WaterCoastalDomain::Result;
namespace Opt=WaterOpticalContext;
struct Map {
    int width,height,border=0;double spacing=10,scale=1;float level=10;
    std::vector<uint8_t> heights;std::vector<Opt::Polygon> polygons;Opt::Field field;
    Map(int w,int h):width(w),height(h),heights(size_t(w)*h,0){Full();}
    static Opt::Polygon Rect(float x0,float y0,float x1,float y1,float z){return {{{x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z}}};}
    void Full(){polygons={Rect(float(-border*spacing),float(-border*spacing),float((width-border-1)*spacing),float((height-border-1)*spacing),level)};}
    bool Build(){const bool ok=Opt::Build(heights.data(),width,height,border,float(spacing),float(scale),level,polygons,field);CHECK(ok);return ok;}
    Result Install(Domain&d,size_t budget=WaterCoastalDomain::MaxBuildWork)const{return d.Build(heights.data(),width,height,border,spacing,scale,polygons,{{level,&field}},budget);}
};
inline std::vector<double> Signature(const Domain &d){
    std::vector<double> s{d.Grid().originX,d.Grid().originY,d.Grid().spacing,double(d.Support().TileCount()),double(d.Support().VertexCount()),double(d.Support().Work())};
    for(const auto&c:d.Cells())s.insert(s.end(),{double(c.x),double(c.y),c.level,double(c.sourceIndex),double(c.connections)});
    for(const auto&n:d.Nodes()){
        for(float v:n.world)s.push_back(v);
        const auto&c=n.context;s.insert(s.end(),{c.depth,c.distance,c.outwardX,c.outwardY,c.exposure,c.offshore,c.analyticDamping,c.spectralDamping,c.wet,c.admitted});
        for(auto id:n.neighbour)s.push_back(id);
    }
    for(const auto&e:d.Topology().Edges())s.insert(s.end(),{double(e.a),double(e.b)});
    return s;
}
inline void Graph(const Domain &d){
    CHECK(WaterCoastalSurf::ValidGraph(d.Nodes(),d.Grid().spacing));
    CHECK(d.Cells().size()==d.Nodes().size()&&d.Topology().Cells().size()==d.Nodes().size());
    if(d.Cells().size()!=d.Nodes().size()||d.Topology().Cells().size()!=d.Nodes().size())return;
    for(size_t i=0;i<d.Cells().size();++i){
        const auto &c=d.Cells()[i],&sorted=d.Topology().Cells()[i];
        CHECK(c.sourceIndex==i&&sorted.sourceIndex==i&&WaterShoreHistory::SameKey(c,sorted));
        CHECK(i==0||WaterShoreHistory::Less(d.Cells()[i-1],c));
        const auto &n=d.Nodes()[i];CHECK(n.context.exposure==1&&n.context.wet==1&&n.context.admitted==1);
        CHECK(n.world[0]==float(d.Grid().originX+c.x*d.Grid().spacing)&&n.world[1]==float(d.Grid().originY+c.y*d.Grid().spacing)&&n.world[2]==float(c.level));
        CHECK(n.context.analyticDamping==WaterSurfaceModel::TerrainDamping(n.context.depth,n.context.offshore)&&n.context.spectralDamping==WaterFftGeometry::SafeDamping(n.context.analyticDamping,n.context.depth));
    }
}

inline void Check(){
    Map ridge(11,11);
    for(int y=3;y<=7;++y)for(int x=3;x<=7;++x)if(x==3||x==7||y==3||y==7)ridge.heights[size_t(y)*11+x]=12;
    if(!ridge.Build())return;Domain kept;CHECK(ridge.Install(kept)==Result::Ready);Graph(kept);
    const auto leaking=WaterSurfaceModel::SampleOffshore(ridge.field.exposure.data(),11,11,3.5f,5,10);
    CHECK(leaking.height>.49f&&leaking.height<.51f);
    CHECK(kept.Topology().Find(7,10,10)<0);
    CHECK(kept.Topology().Find(4,10,10)>=0);
    for(const auto&c:kept.Cells())CHECK(!(c.x>6&&c.x<14&&c.y>6&&c.y<14));


    Map gap(11,11);gap.heights[5*11+6]=12;
    gap.polygons={Map::Rect(0,0,43,100,10),Map::Rect(44,20,76,80,10)};if(!gap.Build())return;Domain separated;
    CHECK(gap.field.exposure[4*11+5]==255);
    CHECK(gap.Install(separated)==Result::Ready);Graph(separated);
    CHECK(separated.Topology().Find(10,8,10)<0);
    CHECK(separated.Topology().Find(8,8,10)>=0);
    for(const auto&n:separated.Nodes())CHECK(n.world[0]<=43);


    Map island(17,17);island.level=20;island.Full();
    for(int y=7;y<=9;++y)for(int x=7;x<=9;++x)island.heights[size_t(y)*17+x]=24;
    if(!island.Build())return;Domain coast;CHECK(island.Install(coast)==Result::Ready);Graph(coast);
    CHECK(coast.Topology().Find(13,16,20)>=0);
    CHECK(!coast.Cells().empty());
    for(const auto&c:coast.Cells())CHECK(c.x>0&&c.y>0&&c.x<32&&c.y<32);

    auto layeredPolygons=island.polygons;layeredPolygons.push_back(Map::Rect(0,0,160,160,10));Opt::Field lower;
    CHECK(Opt::Build(island.heights.data(),17,17,0,10,1,10,layeredPolygons,lower));
    Domain layered;CHECK(layered.Build(island.heights.data(),17,17,0,10,1,layeredPolygons,{{20,&island.field},{10,&lower}})==Result::Ready);Graph(layered);
    CHECK(std::any_of(layered.Cells().begin(),layered.Cells().end(),[](const auto&c){return c.level==10;})&&std::any_of(layered.Cells().begin(),layered.Cells().end(),[](const auto&c){return c.level==20;}));

    for(double spacing:{32.,64.,128.}){
        Map sea(130,130);sea.level=6;sea.spacing=spacing;sea.Full();if(!sea.Build())return;
        CHECK(std::all_of(sea.field.physicalShore.begin(),sea.field.physicalShore.end(),[](uint8_t v){return v==255;}));
        Domain empty;CHECK(sea.Install(empty)==Result::Ready&&empty.Nodes().empty());Graph(empty);
    }


    // Playable boundary, not the padded terrain extent, is the exposure seed.
    Map boundary(5,5);boundary.border=1;boundary.level=10;boundary.Full();boundary.heights.assign(25,12);
    for(int y=1;y<=3;++y)boundary.heights[size_t(y)*5+2]=0;
    if(!boundary.Build())return;Domain boundaryDomain;CHECK(boundary.Install(boundaryDomain)==Result::Ready);
    CHECK(boundaryDomain.Topology().Find(4,4,10)>=0);Graph(boundaryDomain);
    Map lastRow(5,5);lastRow.heights.assign(25,12);lastRow.heights[4*5+2]=0;if(!lastRow.Build())return;Domain lastDomain;
    CHECK(lastRow.Install(lastDomain)==Result::Ready&&lastDomain.Topology().Find(4,7,10)>=0);Graph(lastDomain);
    Map dryEdge(5,5);dryEdge.heights.assign(25,12);dryEdge.heights[3*5+2]=0;dryEdge.heights[4*5+2]=10;if(!dryEdge.Build())return;Domain dryDomain;
    CHECK(dryEdge.Install(dryDomain)==Result::Ready&&dryDomain.Nodes().empty());


    // Diagonal wet endpoints in one half-cell can still straddle a dry saddle.
    Map saddle(3,3);saddle.level=14;saddle.Full();saddle.heights={13,17,17,17,5,5,17,5,5};if(!saddle.Build())return;Domain saddleDomain;
    CHECK(saddle.Install(saddleDomain)==Result::Ready);
    CHECK(saddleDomain.Topology().Find(0,0,14)>=0&&saddleDomain.Topology().Find(1,1,14)>=0);
    auto stencil=saddleDomain.Support().At(0,0,14,1,1);CHECK(!stencil.Allows(0,0));Graph(saddleDomain);

    const auto before=Signature(kept);
    CHECK(ridge.Install(kept,1)==Result::Invalid&&Signature(kept)==before);
    auto bad=ridge.field;bad.physicalShore.pop_back();
    CHECK(kept.Build(ridge.heights.data(),11,11,0,10,1,ridge.polygons,{{10,&bad}})==Result::Invalid&&Signature(kept)==before);
    std::vector<Opt::Polygon> tooMany(Opt::MaxPoints/3+1);
    CHECK(kept.Build(ridge.heights.data(),11,11,0,10,1,tooMany,{{10,&ridge.field}})==Result::Invalid&&Signature(kept)==before);
    CHECK(kept.Build(ridge.heights.data(),11,11,0,10,1,ridge.polygons,{{10,&ridge.field},{10,&ridge.field}})==Result::Invalid&&Signature(kept)==before);
    Map capped(260,260);capped.level=6;capped.Full();if(!capped.Build())return;capped.field.physicalShore.assign(capped.field.physicalShore.size(),1);
    CHECK(capped.Install(kept)==Result::TooManyCells&&Signature(kept)==before);

}
}
