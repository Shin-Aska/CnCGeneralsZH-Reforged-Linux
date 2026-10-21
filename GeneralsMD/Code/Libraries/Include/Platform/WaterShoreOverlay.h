#pragma once
// Render-only admission for replacing authored .wak surf. No texture-name inference.
#include "Platform/WaterRenderBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace WaterShoreOverlay {
constexpr unsigned MaxRegions = 128;
constexpr unsigned MaxPolygonPoints = WaterRenderBridge::MaxCoveragePoints;
constexpr unsigned MaxPolicyEdges = 1024;
constexpr unsigned MaxSweepEvents = 16384;
struct Quad { float xy[8] = {}; float level = 0; };
struct Point { double x = 0, y = 0; };
struct Polygon {
    Point points[MaxPolygonPoints] = {};
    unsigned count = 0;
    double level = 0;
};
struct Stamp {
    std::uint64_t frame = 0, generation = 0, capture = 0;
    std::uintptr_t colour = 0, depth = 0, view = 0;
    std::uint32_t width = 0, height = 0, format = 0, samples = 0;
    WaterRenderBridge::ViewState viewState = {};
};
enum Ownership : unsigned { OwnNone=0,OwnVessel=1,OwnShore=2 };
struct AcceptedRegion { Stamp stamp; std::uint64_t patch = 0; Polygon coverage; unsigned ownership = OwnNone; };
struct DispatchState { bool validPair = false, blocked = false; };
inline bool ShouldAttempt(const DispatchState &state) { return !state.blocked; }
inline bool BlockedResult(const DispatchState &state) { return state.validPair; }
inline bool PreEmissionRefused(DispatchState &state) {
    if(!state.validPair)state.blocked=true;
    return false;
}
inline void Issued(DispatchState &state) { state.validPair=true; }
inline void DeviceLost(DispatchState &state) { state.validPair=true;state.blocked=true; }
struct View {
    std::uintptr_t colour = 0, depth = 0;
    std::uint64_t capture = 0;
    unsigned width = 0, height = 0;
    float worldFromClip[16] = {}, viewport[4] = {};
};
inline bool SameView(const View &a, const View &b) {
    for(float f:a.worldFromClip) if(!std::isfinite(f)) return false;
    for(float f:a.viewport) if(!std::isfinite(f)) return false;
    if(a.viewport[2]<=0||a.viewport[3]<=0) return false;
    return a.colour && a.depth && a.capture && a.width && a.height &&
        a.colour==b.colour && a.depth==b.depth && a.capture==b.capture &&
        a.width==b.width && a.height==b.height &&
        std::memcmp(a.worldFromClip,b.worldFromClip,sizeof(a.worldFromClip))==0 &&
        std::memcmp(a.viewport,b.viewport,sizeof(a.viewport))==0;
}
inline double Cross(const Quad &q, unsigned a, unsigned b, double x, double y) {
    return (double(q.xy[b*2])-q.xy[a*2])*(y-q.xy[a*2+1])-
        (double(q.xy[b*2+1])-q.xy[a*2+1])*(x-q.xy[a*2]);
}
inline bool Convex(const Quad &q) {
    if(!std::isfinite(q.level)) return false;
    for(float f:q.xy) if(!std::isfinite(f)) return false;
    double orientation=0;
    for(unsigned i=0;i<4;++i) {
        const double c=Cross(q,i,(i+1)%4,q.xy[((i+2)%4)*2],q.xy[((i+2)%4)*2+1]);
        if(std::abs(c)<1e-8 || (orientation!=0 && c*orientation<=0)) return false;
        orientation=c;
    }
    return true;
}
inline bool Contains(const Quad &patch,const Quad &track) {
    if(!Convex(patch)||!Convex(track)||patch.level!=track.level) return false;
    const double orientation=Cross(patch,0,1,patch.xy[4],patch.xy[5]);
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) {
        const double c=Cross(patch,i,(i+1)%4,track.xy[j*2],track.xy[j*2+1]);
        if((orientation>0 && c<0)||(orientation<0 && c>0)) return false;
    }
    return true;
}

inline double Cross(const Point &a,const Point &b,const Point &c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
inline bool OnSegment(const Point &a,const Point &b,const Point &p) {
    constexpr double epsilon=1.0e-8;
    return std::abs(Cross(a,b,p))<=epsilon && p.x>=(std::min)(a.x,b.x)-epsilon
        &&p.x<=(std::max)(a.x,b.x)+epsilon&&p.y>=(std::min)(a.y,b.y)-epsilon
        &&p.y<=(std::max)(a.y,b.y)+epsilon;
}
inline bool SegmentsIntersect(const Point &a,const Point &b,const Point &c,const Point &d) {
    constexpr double epsilon=1.0e-8;
    const double abC=Cross(a,b,c),abD=Cross(a,b,d),cdA=Cross(c,d,a),cdB=Cross(c,d,b);
    if(((abC>epsilon&&abD<-epsilon)||(abC<-epsilon&&abD>epsilon))
        &&((cdA>epsilon&&cdB<-epsilon)||(cdA<-epsilon&&cdB>epsilon))) return true;
    return (std::abs(abC)<=epsilon&&OnSegment(a,b,c))||(std::abs(abD)<=epsilon&&OnSegment(a,b,d))
        ||(std::abs(cdA)<=epsilon&&OnSegment(c,d,a))||(std::abs(cdB)<=epsilon&&OnSegment(c,d,b));
}
inline bool Valid(const Polygon &polygon) {
    if(polygon.count<3||polygon.count>MaxPolygonPoints||!std::isfinite(polygon.level)) return false;
    double area=0;
    for(unsigned i=0;i<polygon.count;++i) {
        const Point &a=polygon.points[i],&b=polygon.points[(i+1)%polygon.count];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||std::abs(a.x)>1.0e12||std::abs(a.y)>1.0e12
            ||(a.x==b.x&&a.y==b.y)) return false;
        area+=a.x*b.y-b.x*a.y;
        if(!std::isfinite(area)) return false;
    }
    if(std::abs(area)<=1.0e-8) return false;
    for(unsigned i=0;i<polygon.count;++i) for(unsigned j=i+1;j<polygon.count;++j) {
        if(j==i+1||(i==0&&j+1==polygon.count)) continue;
        if(SegmentsIntersect(polygon.points[i],polygon.points[(i+1)%polygon.count],
            polygon.points[j],polygon.points[(j+1)%polygon.count])) return false;
    }
    return true;
}
inline Stamp StampOf(const WaterRenderBridge::SubmissionReceipt &receipt) {
    Stamp result;
    result.frame=receipt.frameId;result.generation=receipt.resourceGeneration;result.capture=receipt.captureSerial;
    result.colour=receipt.colourIdentity;result.depth=receipt.depthIdentity;result.view=receipt.viewIdentity;
    result.width=receipt.targetWidth;result.height=receipt.targetHeight;result.format=receipt.targetFormat;
    result.samples=receipt.targetSamples;result.viewState=receipt.viewState;return result;
}
inline Stamp StampOf(const WaterRenderBridge::CurrentView &current) {
    Stamp result;
    result.frame=current.frameId;result.generation=current.resourceGeneration;result.capture=current.captureSerial;
    result.colour=current.colourIdentity;result.depth=current.depthIdentity;result.view=current.viewIdentity;
    result.width=current.targetWidth;result.height=current.targetHeight;result.format=current.targetFormat;
    result.samples=current.targetSamples;result.viewState=current.viewState;return result;
}
inline bool SameStamp(const Stamp &a,const Stamp &b) {
    return a.frame&&a.frame==b.frame&&a.generation==b.generation&&a.capture==b.capture
        &&a.colour==b.colour&&a.depth==b.depth&&a.view==b.view&&a.width==b.width&&a.height==b.height
        &&a.format==b.format&&a.samples==b.samples&&WaterRenderBridge::SameViewState(a.viewState,b.viewState);
}
inline bool Accept(const WaterRenderBridge::SubmissionReceipt &receipt,
    const WaterRenderBridge::PreparedPatch &prepared,unsigned ownership,AcceptedRegion &region) {
    if(!ownership) return false;
    if(!WaterRenderBridge::Truthful(receipt,prepared)) return false;
    Polygon polygon;polygon.count=static_cast<unsigned>(prepared.coverageCount);polygon.level=prepared.waterLevel;
    for(unsigned i=0;i<polygon.count;++i) polygon.points[i]={prepared.coverage[i].x,prepared.coverage[i].y};
    if(!Valid(polygon)) return false;
    region.stamp=StampOf(receipt);region.patch=receipt.patchId;region.coverage=polygon;region.ownership=ownership;return true;
}
inline unsigned ReplacementOwnership(const WaterRenderBridge::PreparedPatch &prepared) {
    const auto &constants=prepared.packet.constants.Common;
    const auto &resources=prepared.packet.resources;
    if(constants.WaterParameters.y>.5f||!resources.snapshotAvailable||!resources.fftAvailable
        ||!resources.heightWaveCageAvailable||!resources.foamHistoryAvailable
        ||!resources.terrainAvailable||!resources.opticalAvailable||!resources.bedAvailable)return OwnNone;
    const float channelValue=constants.FoamHistoryParameters.x;
    if(channelValue<0||channelValue>7||std::floor(channelValue)!=channelValue)return OwnNone;
    const unsigned channels=static_cast<unsigned>(channelValue);
    unsigned result=(channels&1)?OwnVessel:OwnNone;
    if(channels&2)result|=OwnShore;
    if(constants.HullParameters.x>0&&!resources.meshContactAvailable)result&=~OwnVessel;
    return result;
}
struct Interval { double first, last; };
inline void IntervalsAt(const Polygon &polygon,double x,std::vector<Interval> &intervals) {
    std::vector<double> crossings;crossings.reserve(polygon.count);
    for(unsigned i=0;i<polygon.count;++i) {
        const Point &a=polygon.points[i],&b=polygon.points[(i+1)%polygon.count];
        if(a.x==b.x) continue;
        const double minimum=(std::min)(a.x,b.x),maximum=(std::max)(a.x,b.x);
        if(x<minimum||x>=maximum) continue;
        const double t=(x-a.x)/(b.x-a.x);crossings.push_back(a.y+(b.y-a.y)*t);
    }
    std::sort(crossings.begin(),crossings.end());
    for(std::size_t i=0;i+1<crossings.size();i+=2)
        if(crossings[i+1]>crossings[i]) intervals.push_back({crossings[i],crossings[i+1]});
}
inline bool AddIntersectionEvent(const Point &a,const Point &b,const Point &c,const Point &d,
    std::vector<double> &events) {
    const double abx=b.x-a.x,aby=b.y-a.y,cdx=d.x-c.x,cdy=d.y-c.y;
    const double denominator=abx*cdy-aby*cdx;
    if(std::abs(denominator)<=1.0e-12) return true;
    const double acx=c.x-a.x,acy=c.y-a.y;
    const double t=(acx*cdy-acy*cdx)/denominator,u=(acx*aby-acy*abx)/denominator;
    if(t>=0&&t<=1&&u>=0&&u<=1) {
        if(events.size()>=MaxSweepEvents) return false;
        events.push_back(a.x+t*abx);
    }
    return true;
}
inline bool CoveredByUnion(const Polygon &footprint,const Stamp &stamp,
    const AcceptedRegion *regions,std::size_t count,unsigned requiredOwnership=OwnNone) {
    if(!Valid(footprint)||!regions||count>MaxRegions) return false;
    std::vector<const Polygon *> covers;covers.reserve(count);
    unsigned edges=footprint.count;
    for(std::size_t i=0;i<count;++i) if((!requiredOwnership||(regions[i].ownership&requiredOwnership)==requiredOwnership)
        &&SameStamp(stamp,regions[i].stamp)
        &&regions[i].coverage.level==footprint.level&&Valid(regions[i].coverage)) {
        if(edges>MaxPolicyEdges-regions[i].coverage.count) return false;
        edges+=regions[i].coverage.count;covers.push_back(&regions[i].coverage);
    }
    if(covers.empty()) return false;
    std::vector<const Polygon *> polygons;polygons.reserve(covers.size()+1);polygons.push_back(&footprint);
    polygons.insert(polygons.end(),covers.begin(),covers.end());
    std::vector<double> events;events.reserve(edges*2);
    for(const Polygon *polygon:polygons) for(unsigned i=0;i<polygon->count;++i) events.push_back(polygon->points[i].x);
    for(std::size_t p=0;p<polygons.size();++p) for(unsigned i=0;i<polygons[p]->count;++i)
        for(std::size_t q=p;q<polygons.size();++q) for(unsigned j=0;j<polygons[q]->count;++j) {
            if(p==q&&(i==j||(i+1)%polygons[p]->count==j||(j+1)%polygons[p]->count==i)) continue;
            if(!AddIntersectionEvent(polygons[p]->points[i],polygons[p]->points[(i+1)%polygons[p]->count],
                polygons[q]->points[j],polygons[q]->points[(j+1)%polygons[q]->count],events)) return false;
        }
    std::sort(events.begin(),events.end());
    events.erase(std::unique(events.begin(),events.end()),events.end());
    bool tested=false;
    for(std::size_t event=0;event+1<events.size();++event) {
        if(events[event+1]<=events[event]) continue;
        const double x=(events[event]+events[event+1])*.5;
        std::vector<Interval> needed,available;IntervalsAt(footprint,x,needed);
        if(needed.empty()) continue;
        tested=true;
        for(const Polygon *cover:covers) IntervalsAt(*cover,x,available);
        std::sort(available.begin(),available.end(),[](const Interval &a,const Interval &b){return a.first<b.first;});
        std::vector<Interval> merged;
        for(const Interval &value:available) {
            if(merged.empty()||value.first>merged.back().last) merged.push_back(value);
            else merged.back().last=(std::max)(merged.back().last,value.last);
        }
        for(const Interval &value:needed) {
            bool found=false;
            for(const Interval &cover:merged) if(cover.first<=value.first&&cover.last>=value.last) {found=true;break;}
            if(!found) return false;
        }
    }
    return tested;
}
inline float Replacement(float depthAvailable,float rawDepth,float offshore,float support,float terrainDifference) {
    if(!std::isfinite(depthAvailable)||depthAvailable<=.5f||!std::isfinite(rawDepth)||
        rawDepth<0||rawDepth>=.999999f||!std::isfinite(offshore)||
        !std::isfinite(support)||!std::isfinite(terrainDifference)) return 0;
    float x=(std::abs(terrainDifference)-.5f);x=x<0?0:x>1?1:x;
    return 1-x*x*(3-2*x);
}
// Append to the ordinary fixed-function D3D11 combiner, then retarget exactly once.
// The original pixel function runs unchanged (including its fog, shroud and alpha test).
inline bool WrapPixel(std::string &source) {
    const std::string fields="    float4 AlphaReference;";
    const std::string entry="float4 main(Input input) : SV_Target";
    const std::size_t at=source.find(fields),main=source.find(entry);
    if(at==std::string::npos||main==std::string::npos) return false;
    source.replace(main,entry.size(),"float4 shoreOriginalPixel(Input input) : SV_Target");
    source.insert(at+fields.size(),R"HLSL(
    row_major float4x4 ShoreWorldFromClip;
    float4 ShoreViewport;
    float4 ShoreTerrain;
    float4 ShoreScene;
    float4 ShoreSurface;
)HLSL");
    source+=R"HLSL(
Texture2D Texture4 : register(t4);
SamplerState Sampler4 : register(s4);
Texture2D Texture6 : register(t6);
SamplerState Sampler6 : register(s6);
bool shoreUnproject(float2 pixel,float d,out float3 world) {
    float2 uv=(pixel-ShoreViewport.xy)/max(ShoreViewport.zw,1.0.xx);
    float4 p=mul(float4(uv*float2(2,-2)+float2(-1,1),d,1),ShoreWorldFromClip);
    world=0;
    if(!all(isfinite(p))||abs(p.w)<1e-8) return false;
    world=p.xyz/p.w;
    return all(isfinite(world));
}
float shoreReplacement(float2 originalPixel) {
    if(!isfinite(ShoreScene.w)||ShoreScene.w<=.5||ShoreTerrain.w<.5||ShoreSurface.z<.5) return 0;
    float2 first=max(.5.xx,ShoreViewport.xy);
    float2 last=min(1.0/ShoreScene.yz-.5,ShoreViewport.xy+ShoreViewport.zw-1);
    float2 pixel=clamp(floor(originalPixel)+.5,first,last);
    float d=Texture6.SampleLevel(Sampler6,pixel*ShoreScene.yz,0).r;
    if(!isfinite(d)||d<0||d>=.999999) return 0;
    float3 scene,a,b;
    if(!shoreUnproject(pixel,d,scene)||!shoreUnproject(pixel,0,a)||!shoreUnproject(pixel,.5,b)) return 0;
    float dz=b.z-a.z;
    if(abs(dz)<1e-8) return 0;
    // Reconstruct the actual elevated sprite plane. SV_Position.z includes legacy Z bias.
    float2 plane=a.xy+(b.xy-a.xy)*((ShoreSurface.y-a.z)/dz);
    if(!all(isfinite(plane))) return 0;
    float2 uv=(plane+ShoreTerrain.xx+5)/max(ShoreTerrain.y,1);
    if(any(uv<0)||any(uv>1)) return 0;
    float4 coast=Texture4.SampleLevel(Sampler4,uv,0);
    if(!all(isfinite(coast))) return 0;
    float2 groundUV=(scene.xy+ShoreTerrain.xx+5)/max(ShoreTerrain.y,1);
    if(any(groundUV<0)||any(groundUV>1)) return 0;
    float ground=Texture2.SampleLevel(Sampler2,groundUV,0).r*ShoreTerrain.z;
    if(!isfinite(ground)) return 0;
    // The accepted modern patch owns this authored surf quad. R only blends wave
    // models, and A only locates fresh breaking; neither limits that ownership.
    // Keeping either here leaves old travelling sprites outside the source band.
    // Actual terrain/depth checks still preserve objects and unavailable resources.
    return 1-smoothstep(.5,1.5,abs(scene.z-ground));
}
float4 main(Input input) : SV_Target {
    float4 result=shoreOriginalPixel(input);
    result.a*=1-shoreReplacement(input.Position.xy);
    return result;
}
)HLSL";
    return true;
}
} // namespace WaterShoreOverlay

#ifndef _WIN32
class IDirect3DDevice9;
// Scope is one actual WaterRender call. Null tags clear pending metadata immediately.
void PosixWaterShoreBegin(IDirect3DDevice9 *,bool enabled);
void PosixWaterShoreEnd(IDirect3DDevice9 *);
void PosixWaterShorePatch(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *);
void PosixWaterShoreTrack(IDirect3DDevice9 *,const WaterShoreOverlay::Quad *);
namespace WaterShoreOverlay {
struct Scope {
    IDirect3DDevice9 *device;
    Scope(IDirect3DDevice9 *d,bool enabled):device(d){PosixWaterShoreBegin(d,enabled);}
    ~Scope(){PosixWaterShoreEnd(device);}
    Scope(const Scope &)=delete;
    Scope &operator=(const Scope &)=delete;
};
}
#endif
