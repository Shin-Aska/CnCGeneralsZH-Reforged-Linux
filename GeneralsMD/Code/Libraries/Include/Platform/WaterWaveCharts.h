/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// Two fixed characteristic charts, not two sorted nearest arrivals. Each chart
// transports the incident front toward one transverse side. This retains opposite
// island paths without exchanging identities at the lee arrival-order crossing.
namespace WaterWaveCharts {
constexpr unsigned MaxSweepCycles=64;
constexpr double ConvergenceTolerance=1e-7;
struct Edge {
    int ax,ay,bx,by;
    double lo,hi,length,projection,perpendicular;
};
inline std::vector<Edge> Stencil(double wx,double wy,int side) {
    constexpr int directions[8][2]={{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
    std::vector<Edge> edges;
    for(unsigned k=0;k<8;++k) {
        const unsigned j=(k+1)%8;
        const int ax=directions[k][0],ay=directions[k][1],bx=directions[j][0],by=directions[j][1];
        double lo=0,hi=1;
        auto clip=[&](double a,double b) {
            if(std::abs(b-a)<1e-12){if(a<-1e-12)lo=2;return;}
            const double t=-a/(b-a);
            if(b>a)lo=(std::max)(lo,t);else hi=(std::min)(hi,t);
        };
        clip(-wx*ax-wy*ay,-wx*bx-wy*by);
        clip(side*(-wx*ay+wy*ax),side*(-wx*by+wy*bx));
        if(lo>hi+1e-12 || lo>1 || hi<0)continue;
        lo=(std::max)(0.0,lo);hi=(std::min)(1.0,hi);
        const double dx=bx-ax,dy=by-ay,length=std::hypot(dx,dy),projection=(ax*dx+ay*dy)/length;
        edges.push_back({ax,ay,bx,by,lo,hi,length,projection,
            std::sqrt((std::max)(0.0,double(ax*ax+ay*ay)-projection*projection))});
    }
    return edges;
}
inline double EdgeValue(double a,double b,const Edge& e,double slow) {
    const double slope=(b-a)/e.length;
    double t;
    if(slope>=slow)t=e.lo;
    else if(slope<=-slow)t=e.hi;
    else {
        const double along=-slope*e.perpendicular/std::sqrt((slow-slope)*(slow+slope));
        t=(std::max)(e.lo,(std::min)(e.hi,(along-e.projection)/e.length));
    }
    return a+(b-a)*t+slow*std::hypot(e.ax+t*(e.bx-e.ax),e.ay+t*(e.by-e.ay));
}
inline bool Sweep(const uint8_t* wet,int width,int height,const std::vector<double>& slow,
                  const std::vector<uint8_t>& fixed,const std::vector<Edge>& edges,std::vector<double>& u,
                  const uint8_t* links=nullptr) {
    // All eight-neighbour travel segments stay in the original four-connected wet
    // domain. A diagonal requires both orthogonal cells; no thin barrier corner cuts.
    auto reachable=[&](int x,int y,int dx,int dy) {
        const int nx=x+dx,ny=y+dy;
        if(nx<0 || nx>=width || ny<0 || ny>=height || !wet[ny*width+nx])return false;
        if(dx && dy && (!wet[y*width+nx] || !wet[ny*width+x]))return false;
        if(!links)return true;
        auto horizontal=[&](int xx,int yy){return (links[yy*width+(std::min)(xx,xx+dx)]&1)!=0;};
        auto vertical=[&](int xx,int yy){return (links[(std::min)(yy,yy+dy)*width+xx]&2)!=0;};
        if(dx && (!horizontal(x,y) || (dy && !horizontal(x,ny))))return false;
        if(dy && (!vertical(x,y) || (dx && !vertical(nx,y))))return false;
        return true;
    };
    const double infinity=std::numeric_limits<double>::infinity();
    for(unsigned cycle=0;cycle<MaxSweepCycles;++cycle) {
        bool changed=false;
        for(unsigned sweep=0;sweep<4;++sweep)for(int Y=0;Y<height;++Y)for(int X=0;X<width;++X) {
            const int x=(sweep&1)?width-1-X:X,y=(sweep&2)?height-1-Y:Y,i=y*width+x;
            if(!wet[i] || fixed[i])continue;
            double value=u[i];
            for(const Edge& e:edges) {
                const double a=reachable(x,y,e.ax,e.ay)?u[(y+e.ay)*width+x+e.ax]:infinity;
                const double b=reachable(x,y,e.bx,e.by)?u[(y+e.by)*width+x+e.bx]:infinity;
                // Endpoint updates also keep one-cell channels and texture edges connected.
                if(std::isfinite(a) && e.lo<=1e-10)value=(std::min)(value,a+slow[i]*(e.ax && e.ay?1.4142135623730951:1.0));
                if(std::isfinite(b) && e.hi>=1-1e-10)value=(std::min)(value,b+slow[i]*(e.bx && e.by?1.4142135623730951:1.0));
                if(std::isfinite(a) && std::isfinite(b))value=(std::min)(value,EdgeValue(a,b,e,slow[i]));
            }
            if(value<u[i]-ConvergenceTolerance){u[i]=value;changed=true;}
        }
        if(!changed)return true;
    }
    return false; // Never publish a partially converged field.
}
inline bool SolveChart(const uint8_t* wet,int width,int height,const std::vector<double>& slow,
                  const std::vector<double>& seed,double wx,double wy,int side,std::vector<double>& chart,
                  const uint8_t*links=nullptr) {
    const size_t count=size_t(width)*height;
    std::vector<uint8_t> fixed(count);
    for(size_t i=0;i<count;++i)fixed[i]=std::isfinite(seed[i]);
        chart=seed;
        if(!Sweep(wet,width,height,slow,fixed,Stencil(wx,wy,side),chart,links))return false;
        // A bent passage can require a transverse reversal. Extend ONLY unreached
        // cells from this chart's existing front; never introduce new shore sources
        // or let the extension alter a previously reached incident/lee arrival.
        bool needsExtension=false;
        for(size_t i=0;i<count;++i) {
            fixed[i]=std::isfinite(chart[i]);
            needsExtension=needsExtension || (wet[i] && !fixed[i]);
        }
        if(needsExtension && !Sweep(wet,width,height,slow,fixed,Stencil(0,0,0),chart,links))return false;
        for(size_t i=0;i<count;++i)if(wet[i] && !std::isfinite(chart[i]))return false;
    return true;
}
inline bool Solve(const uint8_t* wet,int width,int height,const std::vector<double>& slow,
                  const std::vector<double>& seed,double wx,double wy,std::array<std::vector<double>,2>& charts,
                  const uint8_t*links=nullptr) {
    for(unsigned c=0;c<2;++c)if(!SolveChart(wet,width,height,slow,seed,wx,wy,c?1:-1,charts[c],links))return false;
    return true;
}
}
