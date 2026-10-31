/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

// Geometry-only, load-time classification and transport. This is a visual
// shelter model, not a current or pressure solver. All distances are world units.
// Row +Y is world +Y. Link bits describe complete polygon-covered wet segments;
// merely adjacent wet samples do not create transport across an authored gap.
namespace WaterBodyAnalysis {
constexpr int MaxDimension=2048;
constexpr size_t MaxCells=1024u*1024u,MaxVisualConnections=1024;
constexpr float CoreClearance=450,CoreArea=180000,BroadClearance=200;
constexpr float SeaExpansion=220,OpeningWidth=120,AttenuationLength=2400;
constexpr float ShelterClearance=150,CalmEnergy=.055f;
enum : uint8_t { PositiveX=1,PositiveY=2 };
enum class SourceKind { None,BroadSea,ClockwisePerimeter };
struct VisualConnection { int a,b; float transmission=.65f; };
struct Direction {float x=0,y=0;};
struct Field {
    std::vector<float> sea,energy,clearance,distance,downstreamArea;
    std::vector<Direction> direction;
    std::vector<int> component,sourceCells;
    SourceKind sourceKind=SourceKind::None;
    Direction sourceHeading={0,-1}; // Persistent chart axis, from the selected physical source.
    int sourceComponent=-1;
    double broadCoreArea=0;
    int selectedCoreFirstCell=-1;
    float firstOpeningPosition=std::numeric_limits<float>::infinity();
    unsigned qualifyingCores=0,validOpenings=0;
};
inline float Clamp01(float a){return (std::max)(0.0f,(std::min)(1.0f,a));}
inline bool Build(const uint8_t*wet,const uint8_t*links,int width,int height,int border,
                  float spacing,Field&result,const std::vector<VisualConnection>&connections={},bool allowSources=true) {
    result=Field{};
    if(!wet || !links || width<2 || height<2 || width>MaxDimension || height>MaxDimension
        || size_t(width)*height>MaxCells || border<0 || border>(width-2)/2 || border>(height-2)/2
        || !std::isfinite(spacing) || spacing<=0 || spacing>100000
        || connections.size()>MaxVisualConnections)return false;
    const int count=width*height;const float infinity=std::numeric_limits<float>::infinity();
    auto inside=[&](int x,int y){return x>=border && y>=border && x<width-border && y<height-border;};
    auto active=[&](int i){return i>=0 && i<count && inside(i%width,i/width) && wet[i];};
    auto neighbours=[&](int i,auto visit){
        const int x=i%width,y=i/width;
        if(x>border && (links[i-1]&PositiveX) && wet[i-1])visit(i-1);
        if(x+1<width-border && (links[i]&PositiveX) && wet[i+1])visit(i+1);
        if(y>border && (links[i-width]&PositiveY) && wet[i-width])visit(i-width);
        if(y+1<height-border && (links[i]&PositiveY) && wet[i+width])visit(i+width);
    };
    result.component.assign(count,-1);result.clearance.assign(count,0);
    result.sea.assign(count,0);result.energy.assign(count,0);result.distance.assign(count,infinity);
    result.direction.resize(count);result.downstreamArea.assign(count,0);
    std::vector<int> queue;queue.reserve(count);int componentCount=0;
    for(int i=0;i<count;++i)if(active(i) && result.component[i]<0){
        queue.clear();queue.push_back(i);result.component[i]=componentCount;
        for(size_t k=0;k<queue.size();++k)neighbours(queue[k],[&](int j){
            if(result.component[j]<0){result.component[j]=componentCount;queue.push_back(j);}
        });
        ++componentCount;
    }
    // Crop edges are unknown, never banks. Wet nodes adjacent to real dry nodes
    // begin half a sample from shore, matching the reviewed offline classifier.
    constexpr float ClearanceCap=10000;
    for(int y=border;y<height-border;++y)for(int x=border;x<width-border;++x){
        const int i=y*width+x;if(!wet[i])continue;
        result.clearance[i]=ClearanceCap;
        if((x>border && (!wet[i-1] || !(links[i-1]&PositiveX)))
            || (x+1<width-border && (!wet[i+1] || !(links[i]&PositiveX)))
            || (y>border && (!wet[i-width] || !(links[i-width]&PositiveY)))
            || (y+1<height-border && (!wet[i+width] || !(links[i]&PositiveY))))
            result.clearance[i]=(std::min)(ClearanceCap,spacing*.5f);
    }
    auto coveredEdge=[&](int i,int j){
        if(!active(i) || !active(j))return false;
        if(j==i-1)return (links[j]&PositiveX)!=0;
        if(j==i+1)return (links[i]&PositiveX)!=0;
        if(j==i-width)return (links[j]&PositiveY)!=0;
        if(j==i+width)return (links[i]&PositiveY)!=0;
        return false;
    };
    auto relax=[&](int i,int j,float cost){
        if(!active(j))return;
        const int dx=j%width-i%width,dy=j/width-i/width;
        if(dx && dy){
            const int horizontal=i+dx,vertical=i+dy*width;
            if(!coveredEdge(i,horizontal) || !coveredEdge(i,vertical)
                || !coveredEdge(horizontal,j) || !coveredEdge(vertical,j))return;
        }else if(!coveredEdge(i,j))return;
        result.clearance[i]=(std::min)(result.clearance[i],result.clearance[j]+cost);
    };
    const float diagonal=spacing*1.4142135623730951f;
    for(unsigned pass=0;pass<2;++pass)for(int Y=border;Y<height-border;++Y)for(int X=border;X<width-border;++X){
        const int x=pass?width-1-X:X,y=pass?height-1-Y:Y,i=y*width+x;if(!wet[i])continue;
        const int dx=pass?1:-1,dy=pass?1:-1;
        if(inside(x+dx,y))relax(i,i+dx,spacing);
        if(inside(x,y+dy)){
            relax(i,i+dy*width,spacing);
            if(inside(x-1,y+dy))relax(i,i+dy*width-1,diagonal);
            if(inside(x+1,y+dy))relax(i,i+dy*width+1,diagonal);
        }
    }
    std::vector<int> perimeter;
    for(int x=border;x<width-border;++x)perimeter.push_back((height-border-1)*width+x);
    for(int y=height-border-2;y>=border;--y)perimeter.push_back(y*width+width-border-1);
    for(int x=width-border-2;x>=border;--x)perimeter.push_back(border*width+x);
    for(int y=border+1;y<height-border-1;++y)perimeter.push_back(y*width+border);
    auto connected=[&](int a,int b){bool found=false;neighbours(a,[&](int n){found=found||n==b;});return found;};
    std::vector<std::vector<int>> openings;std::vector<int> run;
    for(int i:perimeter){
        if(!active(i)){if(!run.empty()){openings.push_back(std::move(run));run.clear();}}
        else if(!run.empty() && !connected(run.back(),i)){openings.push_back(std::move(run));run={i};}
        else run.push_back(i);
    }
    if(!run.empty())openings.push_back(std::move(run));
    if(openings.size()>1 && connected(openings.back().back(),openings.front().front())){
        openings.front().insert(openings.front().end(),openings.back().begin(),openings.back().end());openings.pop_back();
    }
    openings.erase(std::remove_if(openings.begin(),openings.end(),[&](const std::vector<int>&v){return double(v.size())*spacing<OpeningWidth;}),openings.end());
    result.validOpenings=unsigned(openings.size());
    if(!openings.empty())result.firstOpeningPosition=float(std::find(perimeter.begin(),perimeter.end(),openings.front().front())-perimeter.begin())/float(perimeter.size());
    std::vector<uint8_t> exposed(count,0);for(const auto&o:openings)for(int i:o)exposed[i]=1;
    // Label broad regions once, so maps with many little cores remain O(cells).
    std::vector<int> broadId(count,-1);std::vector<uint8_t> broadExposed;
    for(int i=0;i<count;++i)if(active(i) && result.clearance[i]>=BroadClearance && broadId[i]<0){
        const int id=int(broadExposed.size());broadExposed.push_back(0);queue.clear();queue.push_back(i);broadId[i]=id;
        for(size_t k=0;k<queue.size();++k){const int at=queue[k];broadExposed[id]|=exposed[at];neighbours(at,[&](int j){
            if(broadId[j]<0 && result.clearance[j]>=BroadClearance){broadId[j]=id;queue.push_back(j);}
        });}
    }
    std::vector<uint8_t> seen(count,0);size_t bestSize=0;int chosenBroad=-1;
    for(int i=0;i<count;++i)if(active(i) && result.clearance[i]>=CoreClearance && !seen[i]){
        queue.clear();queue.push_back(i);seen[i]=1;
        for(size_t k=0;k<queue.size();++k)neighbours(queue[k],[&](int j){
            if(!seen[j] && result.clearance[j]>=CoreClearance){seen[j]=1;queue.push_back(j);}
        });
        if(double(queue.size())*spacing*spacing>=CoreArea && broadExposed[broadId[i]]){
            ++result.qualifyingCores;
            if(queue.size()>bestSize){bestSize=queue.size();chosenBroad=broadId[i];result.sourceComponent=result.component[i];result.selectedCoreFirstCell=i;result.broadCoreArea=double(bestSize)*spacing*spacing;}
        }
    }
    if(chosenBroad>=0){
        result.sourceKind=SourceKind::BroadSea;queue.clear();
        for(int i=0;i<count;++i)if(broadId[i]==chosenBroad){result.distance[i]=0;queue.push_back(i);}
        // Unweighted graph distance softens only the selected physical sea's edge.
        for(size_t k=0;k<queue.size();++k){const int i=queue[k];neighbours(i,[&](int j){
            if(!std::isfinite(result.distance[j])){result.distance[j]=result.distance[i]+spacing;queue.push_back(j);}
        });}
        for(int i:queue){result.sea[i]=Clamp01(1-result.distance[i]/SeaExpansion);if(result.sea[i]>=.5f)result.sourceCells.push_back(i);}
    }else if(!openings.empty()){
        result.sourceKind=SourceKind::ClockwisePerimeter;result.sourceCells=openings.front();result.sourceComponent=result.component[result.sourceCells.front()];
    }
    // The caller selects exactly one source across all authored water levels.
    // Disabled levels retain their candidate diagnostics for review, but cannot
    // emit fallback waves merely because another height touches the map edge.
    if(!allowSources){result.sourceKind=SourceKind::None;result.sourceComponent=-1;result.sourceCells.clear();std::fill(result.sea.begin(),result.sea.end(),0);}
    // Explicit, authored visual underpasses alter transport only. They cannot
    // create sources, change wetness, join the classifier, or emit a dry-span arrow.
    std::vector<int> visualHead(count,-1);
    struct VisualEdge{int cell,next;float cost;};std::vector<VisualEdge> visualEdges;
    for(const auto&c:connections){
        if(!active(c.a) || !active(c.b) || c.a==c.b || result.component[c.a]==result.component[c.b]
            || !std::isfinite(c.transmission) || c.transmission<=0 || c.transmission>1){result=Field{};return false;}
        const float span=spacing*std::hypot(float(c.a%width-c.b%width),float(c.a/width-c.b/width));
        const float cost=span-AttenuationLength*std::log(c.transmission);
        visualEdges.push_back({c.b,visualHead[c.a],cost});visualHead[c.a]=int(visualEdges.size())-1;
        visualEdges.push_back({c.a,visualHead[c.b],cost});visualHead[c.b]=int(visualEdges.size())-1;
    }
    using Entry=std::pair<float,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> heap;
    std::fill(result.distance.begin(),result.distance.end(),infinity);
    for(int i:result.sourceCells){result.distance[i]=0;heap.push({0,i});}
    std::vector<int> order;order.reserve(count);
    auto travelCost=[&](int i,int j){return spacing*(1+.55f*(std::max)(0.0f,1-(result.clearance[i]+result.clearance[j])*.5f/ShelterClearance));};
    while(!heap.empty()){
        const Entry e=heap.top();heap.pop();const int i=e.second;if(e.first!=result.distance[i])continue;order.push_back(i);
        auto update=[&](int j,float cost){const float d=e.first+cost;if(d<result.distance[j]){result.distance[j]=d;heap.push({d,j});}};
        neighbours(i,[&](int j){update(j,travelCost(i,j));});
        for(int k=visualHead[i];k>=0;k=visualEdges[k].next)update(visualEdges[k].cell,visualEdges[k].cost);
    }
    // Support flowing back along the arrival DAG distinguishes a transit neck
    // serving downstream water from a blind spur. No passive map edge adds it.
    const float cellArea=spacing*spacing;
    std::vector<float> downstreamReach(count,0);
    for(int i:order)result.downstreamArea[i]=cellArea;
    for(auto it=order.rbegin();it!=order.rend();++it){
        const int i=*it;double total=0;
        neighbours(i,[&](int j){
            if(result.distance[j]<result.distance[i])total+=result.distance[i]-result.distance[j];
            else if(result.distance[j]>result.distance[i])downstreamReach[i]=(std::max)(downstreamReach[i],downstreamReach[j]+spacing);
        });
        for(int k=visualHead[i];k>=0;k=visualEdges[k].next){const int j=visualEdges[k].cell;
            if(result.distance[j]<result.distance[i])total+=result.distance[i]-result.distance[j];
            // The dry underpass's attenuation cost is not downstream wet extent.
            else if(result.distance[j]>result.distance[i])downstreamReach[i]=(std::max)(downstreamReach[i],downstreamReach[j]);
        }
        if(total>0)neighbours(i,[&](int j){if(result.distance[j]<result.distance[i])
            result.downstreamArea[j]+=float(result.downstreamArea[i]*(result.distance[i]-result.distance[j])/total);
        });
        if(total>0)for(int k=visualHead[i];k>=0;k=visualEdges[k].next){const int j=visualEdges[k].cell;
            if(result.distance[j]<result.distance[i])result.downstreamArea[j]+=float(result.downstreamArea[i]*(result.distance[i]-result.distance[j])/total);
        }
    }
    // Frontier normals point from the one source into transported water. Weight
    // by downstream support so a long inlet outweighs a shallow beach fringe.
    // The centroid fallback handles symmetric openings and all-source crops.
    std::vector<uint8_t> source(count,0);for(int i:result.sourceCells)source[i]=1;
    double hx=0,hy=0,totalWeight=0,sx=0,sy=0,rx=0,ry=0;size_t reachable=0;
    for(int i:result.sourceCells){
        sx+=i%width;sy+=i/width;
        neighbours(i,[&](int j){if(!source[j] && std::isfinite(result.distance[j])){
            const double weight=(std::max)(double(cellArea),double(result.downstreamArea[j]));
            hx+=(j%width-i%width)*weight;hy+=(j/width-i/width)*weight;totalWeight+=weight;
        }});
    }
    for(int i:order)if(!source[i]){rx+=i%width;ry+=i/width;++reachable;}
    if(std::hypot(hx,hy)<totalWeight*.05 && reachable && !result.sourceCells.empty()){
        hx=rx/reachable-sx/result.sourceCells.size();hy=ry/reachable-sy/result.sourceCells.size();
    }
    const double headingLength=std::hypot(hx,hy);
    if(headingLength>1e-8)result.sourceHeading={float(hx/headingLength),float(hy/headingLength)};
    // A short, exact grid-edge walk rejects tangents that immediately point
    // across a bank. This uses the original covered-segment graph, including
    // subcell polygon gaps; virtual links are deliberately absent here.
    auto safeDirection=[&](int i,float dx,float dy){
        int x=i%width,y=i/width;const float reach=(std::min)(40.0f,4*spacing)/spacing;
        const int sx=dx>0?1:-1,sy=dy>0?1:-1;
        float tx=dx?(.5f/std::abs(dx)):infinity,ty=dy?(.5f/std::abs(dy)):infinity;
        const float stepx=dx?1/std::abs(dx):infinity,stepy=dy?1/std::abs(dy):infinity;
        while((std::min)(tx,ty)<=reach){
            const int at=y*width+x;
            // At exact grid corners both possible edge orderings must be wet.
            // Otherwise a diagonal could jump a dry corner or uncovered segment.
            if(std::abs(tx-ty)<1e-5f){
                if(!inside(x+sx,y+sy))return true;
                if(!connected(at,at+sx) || !connected(at,at+sy*width)
                    || !connected(at+sx,at+sx+sy*width) || !connected(at+sy*width,at+sx+sy*width))return false;
                x+=sx;y+=sy;tx+=stepx;ty+=stepy;
            }else if(tx<ty){
                if(!inside(x+sx,y))return true;
                if(!connected(at,at+sx))return false;
                x+=sx;tx+=stepx;
            }else{
                if(!inside(x,y+sy))return true;
                if(!connected(at,at+sy*width))return false;
                y+=sy;ty+=stepy;
            }
        }
        return true;
    };
    for(int i=0;i<count;++i)if(active(i)){
        if(!std::isfinite(result.distance[i])){result.energy[i]=CalmEnergy;continue;}
        const float shelter=.35f+.65f*Clamp01(result.clearance[i]/ShelterClearance);
        // Each grid node owns one sample-wide strip of downstream support.
        // Dividing again by the whole passage width would incorrectly weaken
        // wider through routes simply because their flow spans more nodes.
        const float supportedLength=result.downstreamArea[i]/spacing;
        // Arrival-DAG area can concentrate on one bank of a bend. Its longest
        // remaining physical route provides a stable cross-section complement,
        // without needing a skeleton, map-edge bonus, or per-map bay masks.
        const float continuation=Clamp01((std::max)(supportedLength,downstreamReach[i])/600);
        const float narrow=1-Clamp01(result.clearance[i]/ShelterClearance);
        const float transit=1-narrow*.65f*(1-continuation);
        // A continuing narrow passage loses energy more slowly, with its gain
        // bounded at 35% of the reviewed distance/shelter result. Blind spurs
        // receive no gain. The graph distance and visual-link budget stay fixed.
        const float baseDecay=result.distance[i]/AttenuationLength;
        const float throughGain=std::exp((std::min)(std::log(1.35f),baseDecay*.5f)*narrow*continuation);
        result.energy[i]=(std::max)(CalmEnergy,(std::min)(1.0f,std::exp(-baseDecay)*shelter*transit*throughGain));
        if(result.sea[i]>=.5f)result.energy[i]=1;
        float gx=0,gy=0;
        neighbours(i,[&](int j){if(std::isfinite(result.distance[j])){const float diff=result.distance[j]-result.distance[i];
            gx+=(j%width-i%width)*diff;gy+=(j/width-i/width)*diff;
        }});
        const float length=std::hypot(gx,gy);
        if(length>1e-5f && result.distance[i]>0){
            const Direction d={gx/length,gy/length};
            if(safeDirection(i,d.x,d.y))result.direction[i]=d;
            else{
                float best=0;
                neighbours(i,[&](int j){const float difference=result.distance[j]-result.distance[i];
                    const Direction step={float(j%width-i%width),float(j/width-i/width)};
                    if(difference>best && safeDirection(i,step.x,step.y)){best=difference;result.direction[i]=step;}
                });
            }
        }
    }
    return true;
}
}
