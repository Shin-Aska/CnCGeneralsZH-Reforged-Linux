/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterBodyAnalysis.h"

// Approved artistic transport beneath authored bridges. This never modifies
// wet samples, covered-segment links, terrain, or physical component labels.
// Invoke separately for each water level; a wet mask must describe one level.
namespace WaterBridgeConnections {
struct Point {float x=0,y=0;};
struct Bridge {Point from,to;float width=0;};
constexpr size_t MaxBridges=1024;

inline bool Build(const uint8_t*wet,const uint8_t*exactLinks,int width,int height,int border,
                  float spacing,const std::vector<Bridge>&bridges,
                  std::vector<WaterBodyAnalysis::VisualConnection>&result,
                  const int*physicalComponents=nullptr) {
    result.clear();
    if(!wet || !exactLinks || width<2 || height<2 || width>WaterBodyAnalysis::MaxDimension
        || height>WaterBodyAnalysis::MaxDimension || size_t(width)*height>WaterBodyAnalysis::MaxCells
        || border<0 || border>(width-2)/2 || border>(height-2)/2 || !std::isfinite(spacing)
        || spacing<=0 || spacing>100000 || bridges.size()>MaxBridges)return false;
    if(bridges.empty())return true;
    const int count=width*height;
    auto inside=[&](int x,int y){return x>=border && y>=border && x<width-border && y<height-border;};
    auto active=[&](int x,int y){return inside(x,y) && wet[y*width+x];};
    auto connected=[&](int a,int b){
        if(b==a+1 && a/width==b/width)return bool(exactLinks[a]&WaterBodyAnalysis::PositiveX);
        if(b==a-1 && a/width==b/width)return bool(exactLinks[b]&WaterBodyAnalysis::PositiveX);
        if(b==a+width)return bool(exactLinks[a]&WaterBodyAnalysis::PositiveY);
        if(b==a-width)return bool(exactLinks[b]&WaterBodyAnalysis::PositiveY);
        return false;
    };
    // The optional labels must come from the original exact-link graph (e.g.
    // WaterBodyAnalysis::Field::component), never from visual transport links.
    std::vector<int> labels,queue;
    if(!physicalComponents){
        labels.assign(count,-1);queue.reserve(count);int component=0;
        for(int y=border;y<height-border;++y)for(int x=border;x<width-border;++x){
            const int start=y*width+x;if(!wet[start] || labels[start]>=0)continue;
            queue.clear();queue.push_back(start);labels[start]=component;
            for(size_t k=0;k<queue.size();++k){
                const int i=queue[k],ix=i%width,iy=i/width;
                auto visit=[&](int xx,int yy){if(!active(xx,yy))return;const int j=yy*width+xx;
                    if(labels[j]<0 && connected(i,j)){labels[j]=component;queue.push_back(j);}};
                visit(ix-1,iy);visit(ix+1,iy);visit(ix,iy-1);visit(ix,iy+1);
            }
            ++component;
        }
        physicalComponents=labels.data();
    }
    for(const Bridge&bridge:bridges){
        if(!std::isfinite(bridge.from.x) || !std::isfinite(bridge.from.y)
            || !std::isfinite(bridge.to.x) || !std::isfinite(bridge.to.y)
            || !std::isfinite(bridge.width) || bridge.width<spacing)continue;
        const double dx=double(bridge.to.x)-bridge.from.x,dy=double(bridge.to.y)-bridge.from.y;
        const double length=std::hypot(dx,dy);
        if(length<6*spacing || length>100000)continue;
        const double ux=dx/length,uy=dy/length,nx=-uy,ny=ux;
        const double cx=(double(bridge.from.x)+bridge.to.x)*.5,cy=(double(bridge.from.y)+bridge.to.y)*.5;
        // Bridge mesh widths and terrain-grid quantization need a modest apron.
        // Never search more than 120 world units or 35% of the bridge span on
        // either side, regardless of width. Only the central two grid strips
        // are eligible: an endpoint bank cannot become an unrelated lake port.
        const double radius=(std::min)(120.0,(std::min)(length*.35,
            (std::max)(5.0*spacing,double(bridge.width)*.5+2*spacing)));
        if(radius<2*spacing)continue;
        const double gx=cx/spacing+border,gy=cy/spacing+border;
        if(gx<border || gy<border || gx>=width-border || gy>=height-border)continue;
        const int centerX=int(std::floor(gx+.5)),centerY=int(std::floor(gy+.5));
        // This override is for a terrain-filled crossing. A physically wet
        // midspan is left to the exact graph, including its subcell gaps.
        if(!inside(centerX,centerY) || active(centerX,centerY))continue;
        const double extent=radius+spacing;
        const int x0=int((std::max)(double(border),std::floor((cx-extent)/spacing+border)));
        const int x1=int((std::min)(double(width-border-1),std::ceil((cx+extent)/spacing+border)));
        const int y0=int((std::max)(double(border),std::floor((cy-extent)/spacing+border)));
        const int y1=int((std::min)(double(height-border-1),std::ceil((cy+extent)/spacing+border)));
        int ports[2]={-1,-1};double score[2]={1e100,1e100},along[2]={0,0},normal[2]={0,0};
        const int stepX=std::abs(nx)>std::abs(ny)?(nx>0?1:-1):0;
        const int stepY=stepX?0:(ny>0?1:-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            if(!active(x,y))continue;
            const double px=(x-border)*double(spacing)-cx,py=(y-border)*double(spacing)-cy;
            const double u=px*ux+py*uy,v=px*nx+py*ny;
            if(std::abs(u)>spacing || std::abs(v)>radius || std::abs(v)<spacing)continue;
            const int i=y*width+x;
            // A port sits one physical sample inside its wet side, with wet
            // support in both normal directions. Tiny puddles cannot qualify.
            if(!active(x-stepX,y-stepY) || !active(x+stepX,y+stepY)
                || !connected(i,i-stepX-stepY*width) || !connected(i,i+stepX+stepY*width))continue;
            const int side=v>0?1:0;const double rank=std::abs(v)+2*std::abs(u);
            if(rank<score[side]){score[side]=rank;ports[side]=i;along[side]=u;normal[side]=v;}
        }
        if(ports[0]<0 || ports[1]<0 || physicalComponents[ports[0]]<0 || physicalComponents[ports[1]]<0
            || physicalComponents[ports[0]]==physicalComponents[ports[1]])continue;
        // Nearest supported ports only: do not skip one water body to reach a
        // farther component. Also reject an appreciable along-bridge shortcut.
        if(std::abs(along[1]-along[0])>spacing || normal[1]-normal[0]<2*spacing)continue;
        const int a=(std::min)(ports[0],ports[1]),b=(std::max)(ports[0],ports[1]);
        bool duplicate=false;for(const auto&edge:result)duplicate=duplicate || (edge.a==a && edge.b==b);
        if(!duplicate)result.push_back({a,b,.65f});
    }
    return true;
}
}
