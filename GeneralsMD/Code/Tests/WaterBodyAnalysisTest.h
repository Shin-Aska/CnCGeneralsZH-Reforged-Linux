#pragma once
#include "Platform/WaterBodyAnalysis.h"

namespace WaterBodyAnalysisTest {
using namespace WaterBodyAnalysis;
struct Grid {
    int w,h;std::vector<uint8_t> wet,links;
    Grid(int width,int height):w(width),h(height),wet(size_t(w)*h,0),links(size_t(w)*h,0){}
    void Rect(int x0,int y0,int x1,int y1){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)wet[y*w+x]=255;}
    void Link(){for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int i=y*w+x;links[i]=0;if(!wet[i])continue;
        if(x+1<w && wet[i+1])links[i]|=PositiveX;
        if(y+1<h && wet[i+w])links[i]|=PositiveY;
    }}
    bool Build(Field&f,int border=0,const std::vector<VisualConnection>&c={}){
        return WaterBodyAnalysis::Build(wet.data(),links.data(),w,h,border,10,f,c);
    }
};
inline void Check(){
    Field f,again;
    // An unknown crop edge does not form a synthetic bank or erase open sea.
    Grid sea(81,81);sea.Rect(0,0,80,80);sea.Link();CHECK(sea.Build(f));
    CHECK(f.sourceKind==SourceKind::BroadSea && f.sourceCells.size()==sea.wet.size());
    for(size_t i=0;i<sea.wet.size();++i)CHECK(f.sea[i]==1 && f.energy[i]==1 && f.clearance[i]==10000);
    CHECK(sea.Build(again));CHECK(f.sea==again.sea && f.distance==again.distance && f.energy==again.energy);
    CHECK(f.broadCoreArea>=CoreArea && f.selectedCoreFirstCell>=0 && f.firstOpeningPosition==0);
    CHECK(WaterBodyAnalysis::Build(sea.wet.data(),sea.links.data(),sea.w,sea.h,0,10,again,{},false));
    CHECK(again.sourceKind==SourceKind::None && again.sourceCells.empty() && again.broadCoreArea==f.broadCoreArea);
    for(size_t i=0;i<sea.wet.size();++i)CHECK(again.sea[i]==0 && again.energy[i]==CalmEnergy);
    // Subcell authored gaps can leave every sampled node wet. Each 100-world
    // strip still has real banks, and may never acquire a broad-sea core.
    Grid strips(201,201);strips.Rect(0,0,200,200);strips.Link();
    for(int y=0;y<strips.h;++y)for(int x=9;x+1<strips.w;x+=10)strips.links[y*strips.w+x]&=~PositiveX;
    CHECK(strips.Build(again));CHECK(again.sourceKind==SourceKind::ClockwisePerimeter && again.qualifyingCores==0);
    for(float clearance:again.clearance)CHECK(clearance<=95);
    // A large closed lake has a core, but no exposed broad region. It is calm.
    Grid lake(181,181);lake.Rect(10,10,170,170);lake.Link();CHECK(lake.Build(f));
    CHECK(f.sourceKind==SourceKind::None && f.sourceCells.empty());
    for(size_t i=0;i<lake.wet.size();++i){CHECK(f.sea[i]==0);if(lake.wet[i])CHECK(f.energy[i]==CalmEnergy);}
    // A large lake connected to the edge through a thin canal cannot claim sea.
    lake.Rect(0,85,10,96);lake.Link();CHECK(lake.Build(f));
    CHECK(f.sourceKind==SourceKind::ClockwisePerimeter && f.qualifyingCores==0);
    for(float value:f.sea)CHECK(value==0);
    // Clockwise perimeter selection emits from one opening only. The other
    // opening remains passive even where it touches the stored map boundary.
    Grid forks(61,61);forks.Rect(0,40,60,52);forks.Rect(26,0,38,52);forks.Link();CHECK(forks.Build(f));
    CHECK(f.sourceKind==SourceKind::ClockwisePerimeter && f.validOpenings==3);
    CHECK(f.sourceHeading.x<0 && std::abs(std::hypot(f.sourceHeading.x,f.sourceHeading.y)-1)<1e-6f);
    for(int i:f.sourceCells)CHECK(i%forks.w==forks.w-1); // Right opening precedes bottom and left.
    CHECK(f.distance[6*forks.w+32]>0 && f.distance[46*forks.w]>0);
    // A corner-spanning opening is one source; both sides survive scan wrapping.
    Grid corner(31,31);corner.Rect(0,24,6,30);corner.Link();CHECK(corner.Build(f));
    CHECK(f.sourceKind==SourceKind::ClockwisePerimeter && f.validOpenings==1);
    bool north=false,west=false;for(int i:f.sourceCells){north=north||i/corner.w==30;west=west||i%corner.w==0;}CHECK(north && west);
    // Adjacent wet nodes with an uncovered subcell edge are separate bodies.
    // Source transport cannot tunnel through the visually tiny authored gap.
    Grid gap(61,31);gap.Rect(0,8,60,22);gap.Link();for(int y=8;y<=22;++y)gap.links[y*gap.w+29]&=~PositiveX;
    CHECK(gap.Build(f));CHECK(f.component[15*gap.w+20]!=f.component[15*gap.w+40]);
    const int first=f.sourceCells.front();const int other=first%gap.w==60?15*gap.w+10:15*gap.w+50;
    CHECK(!std::isfinite(f.distance[other]) && f.energy[other]==CalmEnergy && f.direction[other].x==0 && f.direction[other].y==0);
    // The explicit visual connection transmits into a separate wet body while
    // leaving its physical identity and the intervening dry cells intact.
    Grid bridge(61,41);bridge.Rect(0,8,20,22);bridge.Rect(26,8,50,22);bridge.Link();CHECK(bridge.Build(f));
    const int a=15*bridge.w+20,b=15*bridge.w+26;const float physicalSea=f.sea[b];
    CHECK(bridge.Build(f,0,{{a,b,.65f}}));CHECK(f.component[a]!=f.component[b] && std::isfinite(f.distance[b]));
    CHECK(f.sea[b]==physicalSea && f.energy[b]>CalmEnergy && f.energy[b]<f.energy[a]);
    for(int x=21;x<=25;++x)CHECK(f.energy[15*bridge.w+x]==0 && f.component[15*bridge.w+x]==-1);
    CHECK(!bridge.Build(f,0,{{a,b,0}}));CHECK(f.energy.empty());
    // Equal bank geometry and fully supported continuation cancel the local
    // gain/shelter terms: the visual span retains its exact transmission budget.
    Grid budget(281,31);budget.Rect(0,8,160,22);budget.Rect(166,8,270,22);budget.Link();
    const int pa=15*budget.w+160,pb=15*budget.w+166;
    CHECK(budget.Build(f,0,{{pa,pb,.65f}}));
    CHECK(f.energy[pa]>CalmEnergy && f.energy[pb]>CalmEnergy);
    CHECK(std::abs(f.energy[pb]/f.energy[pa]-.65f*std::exp(-60.0f/AttenuationLength))<1e-5f);
    // Equal-width, equal-distance necks: a long continuing route carries more
    // downstream support than a short blind bay. No boundary bonus is involved.
    Grid routes(121,121);routes.Rect(0,70,30,90);routes.Rect(25,60,35,100);
    routes.Rect(30,90,110,100);routes.Rect(30,60,55,70);routes.Link();CHECK(routes.Build(f));
    const int through=95*routes.w+48,bay=65*routes.w+48;
    CHECK(f.downstreamArea[through]>f.downstreamArea[bay]);CHECK(f.energy[through]>f.energy[bay]);
    auto reviewedEnergy=[&](int i){return std::exp(-f.distance[i]/AttenuationLength)*(.35f+.65f*Clamp01(f.clearance[i]/ShelterClearance));};
    CHECK(f.energy[through]>reviewedEnergy(through));CHECK(f.energy[bay]<reviewedEnergy(bay));
    // Padding neither supplies a source nor changes the playable crop's banks.
    Grid apron(61,61);apron.Rect(0,0,60,60);apron.Link();CHECK(apron.Build(f,8));
    CHECK(f.sourceKind==SourceKind::BroadSea);CHECK(f.energy[0]==0 && f.clearance[0]==0);
    CHECK(f.clearance[30*apron.w+8]==10000);
    CHECK(!WaterBodyAnalysis::Build(nullptr,apron.links.data(),61,61,0,10,f));
    CHECK(!WaterBodyAnalysis::Build(apron.wet.data(),nullptr,61,61,0,10,f));
    CHECK(!WaterBodyAnalysis::Build(apron.wet.data(),apron.links.data(),61,61,0,0,f));
}
}
