#pragma once
#include "Platform/WaterBridgeConnections.h"

namespace WaterBridgeConnectionsTest {
using namespace WaterBridgeConnections;
struct Grid {
    int w=61,h=61;std::vector<uint8_t>wet,links;
    Grid():wet(w*h,0),links(w*h,0){}
    void Rect(int x0,int y0,int x1,int y1){for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)wet[y*w+x]=1;}
    void Link(){for(int y=0;y<h;++y)for(int x=0;x<w;++x){const int i=y*w+x;links[i]=0;
        if(wet[i] && x+1<w && wet[i+1])links[i]|=WaterBodyAnalysis::PositiveX;
        if(wet[i] && y+1<h && wet[i+w])links[i]|=WaterBodyAnalysis::PositiveY;
    }}
    bool Build(const std::vector<Bridge>&bridges,std::vector<WaterBodyAnalysis::VisualConnection>&out,
               int border=0,const int*components=nullptr){
        return WaterBridgeConnections::Build(wet.data(),links.data(),w,h,border,10,bridges,out,components);
    }
};
inline void Check(){
    std::vector<WaterBodyAnalysis::VisualConnection>out;
    // Horizontal bridge over a 50-world-unit dry ridge. Ports lie a full sample
    // inside the nearest wet banks, never on the bridge endpoint banks.
    const Bridge bridge={{180,300},{420,300},70};
    Grid split;split.Rect(0,0,60,27);split.Rect(0,33,60,60);split.Link();
    const auto wet=split.wet,links=split.links;
    CHECK(split.Build({bridge},out));CHECK(out.size()==1);
    CHECK(out[0].a==26*split.w+30 && out[0].b==34*split.w+30 && out[0].transmission==.65f);
    CHECK(split.wet==wet && split.links==links);
    WaterBodyAnalysis::Field physical;
    CHECK(WaterBodyAnalysis::Build(split.wet.data(),split.links.data(),split.w,split.h,0,10,physical));
    const auto original=out;
    CHECK(split.Build({bridge},out,0,physical.component.data()));CHECK(out.size()==1);
    CHECK(out[0].a==original[0].a && out[0].b==original[0].b);
    // Reversing authored endpoints and duplicate bridge objects yield one edge.
    CHECK(split.Build({bridge,{bridge.to,bridge.from,70},bridge},out));CHECK(out.size()==1);
    CHECK(out[0].a==original[0].a && out[0].b==original[0].b);
    // Crop border is a coordinate offset, not a shift to cell centers.
    const Bridge padded={{130,250},{370,250},70};
    CHECK(split.Build({padded},out,5));CHECK(out.size()==1);
    CHECK(out[0].a==original[0].a && out[0].b==original[0].b);
    // A wet route around the bridge already joins these banks; do not insert a
    // shortcut, even though the local center is dry.
    split.Rect(0,27,2,33);split.Link();CHECK(split.Build({bridge},out));CHECK(out.empty());
    // An open-water deck has no artistic terrain underpass to add.
    split.Rect(0,0,60,60);split.Link();CHECK(split.Build({bridge},out));CHECK(out.empty());
    // Opposed water far beyond a bridge's width cannot be searched into a link.
    Grid distant;distant.Rect(0,0,60,14);distant.Rect(0,46,60,60);distant.Link();
    CHECK(distant.Build({{{180,300},{420,300},100000}},out));CHECK(out.empty());
    // Wet water near the bridge endpoints is not an opposite midspan port.
    Grid bank;bank.Rect(0,0,15,60);bank.Rect(45,0,60,60);bank.Link();
    CHECK(bank.Build({bridge},out));CHECK(out.empty());
    // Missing exact support cannot be repaired by an adjacent wet sample.
    Grid narrow;narrow.Rect(0,27,60,27);narrow.Rect(0,33,60,33);narrow.Link();
    CHECK(narrow.Build({bridge},out));CHECK(out.empty());
    Grid uncovered;uncovered.Rect(0,0,60,27);uncovered.Rect(0,33,60,60);uncovered.Link();
    for(int x=0;x<61;++x)uncovered.links[26*61+x]&=~WaterBodyAnalysis::PositiveY;
    CHECK(uncovered.Build({bridge},out));CHECK(out.size()==1);
    CHECK(out[0].a/61==25); // Skips an unsupported shoreline port, stays in same near bank.
    // Rotated crossings use bridge-local normal coordinates, rather than an
    // axis-aligned bounding box that could connect water along an abutment.
    Grid rotated;const float inv=1/std::sqrt(5.0f);
    for(int y=0;y<61;++y)for(int x=0;x<61;++x){
        const float normal=(-(x*10-300)+2*(y*10-300))*inv;
        if(std::abs(normal)>=30)rotated.wet[y*61+x]=1;
    }
    rotated.Link();CHECK(rotated.Build({{{180,240},{420,360},70}},out));CHECK(out.size()==1);
    const float ddx=float(out[0].b%61-out[0].a%61),ddy=float(out[0].b/61-out[0].a/61);
    CHECK(std::abs((-ddx+2*ddy)*inv/std::hypot(ddx,ddy))>.98f);
    // Invalid grid input clears a previous result, unsupported bridges are skipped.
    CHECK(!WaterBridgeConnections::Build(nullptr,links.data(),61,61,0,10,{bridge},out));CHECK(out.empty());
    CHECK(narrow.Build({{{0,0},{0,0},70},{{0,0},{600,600},0}},out));CHECK(out.empty());
    CHECK(!WaterBridgeConnections::Build(wet.data(),links.data(),61,61,0,0,{bridge},out));
}
}
