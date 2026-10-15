/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Rendering metadata only: real scene depth remains the source of refraction and intersections.
// A broad polygon can contain both an ocean and enclosed pools, so classify its actual wet terrain.
namespace WaterOpticalContext {
constexpr int MaxDimension = 2048;
constexpr size_t MaxCells = 1024u * 1024u;
constexpr size_t MaxPoints = 65536;
constexpr size_t MaxLevels = 8;
// Independent physical terrain-coast distance; never packed into optical RGBA.
constexpr float PhysicalShoreRange = 160.0f;
struct Point { float x, y, z; };
struct Polygon { std::vector<Point> points; };
struct Field {
    std::vector<uint8_t> ocean;
    std::vector<uint8_t> shore;
    std::vector<uint8_t> exposure; // wet region connected to the playable map boundary
    std::vector<uint8_t> physicalShore; // 0..PhysicalShoreRange, terrain only; polygon edges are excluded
    std::vector<uint8_t> coast;    // terrain-coast replacement validity, inset from every nonzero ocean texel
};
inline uint32_t Texel(const Field &field,size_t n) {
    return (uint32_t(field.coast[n])<<24)|(uint32_t(field.ocean[n])<<16)
        |(uint32_t(field.shore[n])<<8)|uint32_t(field.exposure[n]);
}

inline float Offshore(float distance) {
    float t = (std::max)(0.0f, (std::min)(1.0f, (distance - 30.0f) / 70.0f));
    return t*t*(3.0f-2.0f*t);
}

inline bool SamePolygons(const std::vector<Polygon>& a, const std::vector<Polygon>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i=0; i<a.size(); ++i) {
        if (a[i].points.size() != b[i].points.size()) return false;
        for (size_t j=0; j<a[i].points.size(); ++j) {
            const Point &p=a[i].points[j], &q=b[i].points[j];
            if (p.x!=q.x || p.y!=q.y || p.z!=q.z) return false;
        }
    }
    return true;
}

inline bool Build(const uint8_t* heights, int width, int height, int border,
                  float spacing, float heightScale, float level,
                  const std::vector<Polygon>& polygons, Field& result) {
    result.ocean.clear(); result.shore.clear(); result.exposure.clear(); result.coast.clear(); result.physicalShore.clear();
    if (!heights || width<2 || height<2 || width>MaxDimension || height>MaxDimension ||
        size_t(width)*height>MaxCells || border<0 || border>(width-2)/2 || border>(height-2)/2 ||
        !std::isfinite(spacing) || spacing<=0 || !std::isfinite(heightScale) || heightScale<=0 ||
        !std::isfinite(level) || polygons.size()>MaxPoints/3) return false;
    size_t points=0;
    for (const Polygon& polygon : polygons) {
        if (polygon.points.size()>MaxPoints-points) return false;
        points+=polygon.points.size();
        for (const Point& p : polygon.points)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    }
    const size_t count=size_t(width)*height;
    std::vector<uint8_t> wet(count,0);
    std::vector<double> intersections;
    auto span = [&](int y,double from,double to) {
        const int left=int((std::max)(0.0,(std::min)(double(width),std::ceil(from/spacing+border))));
        const int right=int((std::max)(-1.0,(std::min)(double(width-1),std::floor(to/spacing+border))));
        for (int x=left; x<=right; ++x) {
            const size_t n=size_t(y)*width+x;
            if (heights[n]*heightScale<level) wet[n]=1;
        }
    };
    // Scanline intervals form the union, including concave polygons and overlaps. Only flat,
    // non-river polygons at this level enter the caller's list; different levels never connect.
    for (const Polygon& polygon : polygons) {
        if (polygon.points.size()<3) continue;
        bool sameLevel=true;
        float ymin=polygon.points[0].y, ymax=ymin;
        for (const Point& p : polygon.points) {
            sameLevel &= std::abs(p.z-level)<0.01f;
            ymin=(std::min)(ymin,p.y); ymax=(std::max)(ymax,p.y);
        }
        if (!sameLevel) continue;
        // Clamp before conversion, so even an off-map authored polygon cannot overflow an int.
        const int first=int((std::max)(0.0f,(std::min)(float(height),std::ceil(ymin/spacing+border))));
        const int last=int((std::max)(-1.0f,(std::min)(float(height-1),std::floor(ymax/spacing+border))));
        intersections.reserve(polygon.points.size());
        for (int y=first; y<=last; ++y) {
            const float worldY=(y-border)*spacing;
            intersections.clear();
            for (size_t i=0,j=polygon.points.size()-1; i<polygon.points.size(); j=i++) {
                const Point &a=polygon.points[i], &b=polygon.points[j];
                if ((a.y>worldY)!=(b.y>worldY))
                    intersections.push_back(double(a.x)+(double(worldY)-a.y)*(double(b.x)-a.x)/(double(b.y)-a.y));
                // Include authored edges as well as interiors. Otherwise a polygon ending exactly
                // at the top playable row would acquire a false dry shore from the half-open scan.
                if (a.y==worldY) {
                    span(y,a.x,a.x);
                    if (b.y==worldY) span(y,(std::min)(a.x,b.x),(std::max)(a.x,b.x));
                }
            }
            std::sort(intersections.begin(),intersections.end());
            for (size_t i=0; i+1<intersections.size(); i+=2) {
                span(y,intersections[i],intersections[i+1]);
            }
        }
    }
    result.ocean.assign(count,0);
    result.shore.assign(count,0);
    std::vector<int> queue; queue.reserve(count);
    auto seed = [&](int x,int y) {
        const int n=y*width+x;
        if (wet[n] && !result.ocean[n]) { result.ocean[n]=1; queue.push_back(n); }
    };
    // Playable boundary, not texture padding or polygon bounds, defines connection to open water.
    for (int x=border; x<width-border; ++x) { seed(x,border); seed(x,height-border-1); }
    for (int y=border; y<height-border; ++y) { seed(border,y); seed(width-border-1,y); }
    for (size_t i=0; i<queue.size(); ++i) {
        const int n=queue[i], x=n%width,y=n/width;
        if (x>0) seed(x-1,y);
        if (x+1<width) seed(x+1,y);
        if (y>0) seed(x,y-1);
        if (y+1<height) seed(x,y+1);
    }
    result.exposure=result.ocean;
    for(uint8_t &value:result.exposure)value=value?255:0;
    result.coast.assign(count,0);
    result.physicalShore.assign(count,255);
    // Two-pass chamfer distance is bounded O(cells), with diagonal distances avoiding a square
    // shoreline fade. Grid borders are not synthetic shores; an entirely wet sea has full extent.
    const float limit=100.0f, diagonal=spacing*1.41421356237f;
    std::vector<float> distance(count,limit),terrainDistance(count,PhysicalShoreRange);
    for (size_t n=0; n<count; ++n) {
        if (!wet[n]) distance[n]=0;
        // Authored polygon boundaries over submerged terrain are not physical coasts.
        if (heights[n]*heightScale>=level) terrainDistance[n]=0;
    }
    auto relax = [&](int n,int neighbour,float step) {
        distance[n]=(std::min)(distance[n],distance[neighbour]+step);
        terrainDistance[n]=(std::min)(terrainDistance[n],terrainDistance[neighbour]+step);
    };
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        const int n=y*width+x;
        if (x>0) relax(n,n-1,spacing);
        if (y>0) {
            relax(n,n-width,spacing);
            if (x>0) relax(n,n-width-1,diagonal);
            if (x+1<width) relax(n,n-width+1,diagonal);
        }
    }
    for (int y=height-1; y>=0; --y) for (int x=width-1; x>=0; --x) {
        const int n=y*width+x;
        if (x+1<width) relax(n,n+1,spacing);
        if (y+1<height) {
            relax(n,n+width,spacing);
            if (x>0) relax(n,n+width-1,diagonal);
            if (x+1<width) relax(n,n+width+1,diagonal);
        }
        result.physicalShore[n]=uint8_t(terrainDistance[n]/PhysicalShoreRange*255.0f+0.5f);
        result.shore[n]=uint8_t(distance[n]/limit*255.0f+0.5f);
        result.ocean[n]=result.ocean[n] ? uint8_t(Offshore(distance[n])*255.0f+0.5f) : 0;
    }
    // A contributing bilinear texel must have only zero-R neighbours. Consequently any
    // nonzero interpolated support uses the unchanged analytic nearshore wave model. The
    // shader repeats the zero-R guard against stale/malformed fallback resources.
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const size_t n=size_t(y)*width+x;
        if(terrainDistance[n]>=20.0f)continue;
        bool adjacentWet=wet[n]!=0,exposed=result.exposure[n]!=0;
        for(int yy=(std::max)(0,y-1);yy<=(std::min)(height-1,y+1);++yy)
            for(int xx=(std::max)(0,x-1);xx<=(std::min)(width-1,x+1);++xx) {
                const size_t neighbour=size_t(yy)*width+xx;
                adjacentWet=adjacentWet || wet[neighbour]!=0;
                exposed=exposed || (wet[neighbour] && result.exposure[neighbour]!=0);
            }
        if(!adjacentWet)continue;
        bool analytic=true;
        for(int yy=(std::max)(0,y-1);yy<=(std::min)(height-1,y+1);++yy)
            for(int xx=(std::max)(0,x-1);xx<=(std::min)(width-1,x+1);++xx)
                analytic=analytic && result.ocean[size_t(yy)*width+xx]==0;
        if(!analytic)continue;
        // Replacement validity must include the actual crossing between a dry terrain node
        // and the first wet one. Wet-only A would retain most of the permanent rim there.
        // Emission is independently gated by positive terrain depth and actual scene wetness.
        if(!wet[n])result.exposure[n]=exposed?255:0;
        const float t=(std::max)(0.0f,(std::min)(1.0f,(terrainDistance[n]-10.0f)/10.0f));
        result.coast[n]=uint8_t((1.0f-t*t*(3.0f-2.0f*t))*255.0f+0.5f);
    }
    return true;
}
}
