// Opaque, diffuse scene geometry for the optional water path tracer. Render-only.
#pragma once

#include "Platform/WaterPathTracingView.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace WaterPathTracing {

constexpr unsigned DataTextureWidth = 2048;
constexpr unsigned MaximumTriangles = 1u << 20;
constexpr unsigned MaximumSamples = 64;
constexpr unsigned NodeTexels = 2;
constexpr unsigned TriangleTexels = 4;

struct Float4 { float x, y, z, w; };
struct Triangle { Vec3 a, b, c, albedo; }; // Linear reflectance; all faces are two-sided.

// Match the water shader's waterLinear: clamp negative channels, preserve HDR.
inline Vec3 DisplayToLinear(Vec3 rgb)
{
    return {std::pow(std::max(rgb.x,0.0f),2.2f),
        std::pow(std::max(rgb.y,0.0f),2.2f),std::pow(std::max(rgb.z,0.0f),2.2f)};
}

inline Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 Sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 Scale(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline float Component(Vec3 v, unsigned axis) { return axis==0?v.x:axis==1?v.y:v.z; }
inline Vec3 Minimum(Vec3 a, Vec3 b)
{
    return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};
}
inline Vec3 Maximum(Vec3 a, Vec3 b)
{
    return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
}
inline Vec3 Normalized(Vec3 v)
{
    const float length2=Dot(v,v);
    return length2>0&&std::isfinite(length2)?Scale(v,1/std::sqrt(length2)):Vec3{0,0,0};
}

// Hash fields explicitly: no padding, addresses, or compiler-specific struct layout.
// The original order is intentional; exporters keep a stable order between frames.
inline uint64_t Fingerprint(const std::vector<Triangle> &triangles)
{
    uint64_t hash=14695981039346656037ull;
    auto word=[&](uint32_t value) {
        for(unsigned byte=0;byte<4;++byte) { hash^=(value>>(byte*8))&255u;hash*=1099511628211ull; }
    };
    word(static_cast<uint32_t>(triangles.size()));
    for(const Triangle &t:triangles) {
        for(Vec3 v:{t.a,t.b,t.c,t.albedo}) {
            for(float f:{v.x,v.y,v.z}) {
                if(f==0) f=0; // Canonicalise negative zero.
                uint32_t bits;std::memcpy(&bits,&f,sizeof(bits));word(bits);
            }
        }
    }
    return hash;
}

// Shared verbatim with the shader cbuffer; all fields occupy complete 16-byte registers.
// Directions are world-space, z is up. sunDirection points from the hit towards the sun.
struct Uniforms {
    Float4 originWater;  // Reflected camera xyz, water-plane z.
    Float4 rayTopLeft;   // Unnormalised direction through the viewport's top-left corner.
    Float4 rayDx;        // Direction change across the entire viewport width.
    Float4 rayDy;        // Direction change across the entire viewport height.
    Float4 sunDirection;
    Float4 sunRadiance;  // Linear directional irradiance.
    Float4 skyLow;       // Linear sky radiance at the horizon.
    Float4 skyHigh;      // Linear sky radiance towards the zenith.
    Float4 counts;       // Node count, triangle count, prior sample count, max ray distance.
    Float4 output;       // Width, height, ray origin epsilon (world units), reserved.
    Float4 transport;    // Secondary distance, sun distance, secondary nodes, sun nodes.
};
static_assert(sizeof(Uniforms)==176,"Water path tracing uniforms must match the HLSL cbuffer");

struct Hit {
    bool found=false;
    float distance=0;
    unsigned triangle=0;
    Vec3 position={0,0,0};
    Vec3 normal={0,0,0};
    Vec3 albedo={0,0,0};
};

inline unsigned LeafFirst(unsigned tag) { return (tag-1u)/4u; }
inline unsigned LeafCount(unsigned tag) { return (tag-1u)%4u+1u; }

inline bool IntersectTriangle(const Triangle &triangle,Vec3 origin,Vec3 direction,
                              float minimum,float &maximum)
{
    const Vec3 edge1=Sub(triangle.b,triangle.a),edge2=Sub(triangle.c,triangle.a);
    const Vec3 p=Cross(direction,edge2);
    const float determinant=Dot(edge1,p);
    // Relative parallel tolerance preserves small triangles in a large world.
    if(std::abs(determinant)<=1e-8f*std::sqrt(Dot(edge1,edge1)*Dot(edge2,edge2))) return false;
    const float inverse=1/determinant;
    const Vec3 distance=Sub(origin,triangle.a);
    const float u=Dot(distance,p)*inverse;
    if(u<0||u>1) return false;
    const Vec3 q=Cross(distance,edge1);
    const float v=Dot(direction,q)*inverse;
    if(v<0||u+v>1) return false;
    const float t=Dot(edge2,q)*inverse;
    if(!(t>=minimum&&t<maximum)) return false;
    maximum=t;return true;
}

inline bool IntersectBounds(Vec3 low,Vec3 high,Vec3 origin,Vec3 direction,float maximum)
{
    float minimum=0;
    for(unsigned axis=0;axis<3;++axis) {
        const float o=Component(origin,axis),d=Component(direction,axis);
        const float lo=Component(low,axis),hi=Component(high,axis);
        if(std::abs(d)<1e-20f) { if(o<lo||o>hi)return false;continue; }
        float a=(lo-o)/d,b=(hi-o)/d;
        if(a>b)std::swap(a,b);
        minimum=std::max(minimum,a);maximum=std::min(maximum,b);
        if(maximum<minimum)return false;
    }
    return true;
}

class Scene {
public:
    std::vector<Triangle> triangles;
    std::vector<Float4> nodeTexels;
    std::vector<Float4> triangleTexels;
    uint64_t fingerprint=0;

    unsigned NodeCount() const { return static_cast<unsigned>(nodeTexels.size()/NodeTexels); }
    bool Empty() const { return triangles.empty(); }

    // Invalid non-finite inputs and excess capacity fail without replacing the old scene.
    // Zero-area triangles are harmless and dropped; a valid empty scene is permitted.
    bool Build(const std::vector<Triangle> &input,bool trackFingerprint=true)
    {
        if(input.size()>MaximumTriangles)return false;
        Scene next;
        next.fingerprint=trackFingerprint ? Fingerprint(input) : 0;
        next.inputIncluded.resize(input.size(),0);
        next.inputIndices.reserve(input.size());
        for(unsigned i=0;i<input.size();++i) {
            const int kind=TriangleKind(input[i]);
            if(kind<0)return false;
            if(kind==0)continue;
            next.inputIncluded[i]=1;
            next.inputIndices.push_back(i);
        }
        if(!next.inputIndices.empty()) {
            next.nodeTexels.reserve(next.inputIndices.size()*NodeTexels);
            next.BuildNode(0,static_cast<unsigned>(next.inputIndices.size()),input);
            next.triangles.resize(next.inputIndices.size());
            next.triangleTexels.resize(next.inputIndices.size()*TriangleTexels);
            for(unsigned i=0;i<next.inputIndices.size();++i)
                next.SetTriangle(i,input[next.inputIndices[i]]);
        }
        *this=std::move(next);
        return true;
    }

    // Keep leaf membership and GPU indices, updating only geometry/materials and bounds.
    // Mapping is by original input slot, so reordering same-sized input is still correct
    // (but can worsen traversal quality). The caller periodically rebuilds for quality.
    // Validate the whole input before modifying anything; failure preserves this scene.
    bool Refit(const std::vector<Triangle> &input,bool trackFingerprint=true)
    {
        if(input.size()!=inputIncluded.size())return false;
        for(unsigned i=0;i<input.size();++i) {
            const int kind=TriangleKind(input[i]);
            if(kind<0||static_cast<unsigned>(kind)!=inputIncluded[i])return false;
        }
        const uint64_t nextFingerprint=trackFingerprint ? Fingerprint(input) : 0;
        for(unsigned i=0;i<inputIndices.size();++i)SetTriangle(i,input[inputIndices[i]]);
        // Children always follow their parent in preorder; reverse order reduces both
        // children before the parent. No sorting or allocations occur during a refit.
        for(unsigned end=NodeCount();end>0;--end) {
            const unsigned node=end-1;
            Float4 &lo=nodeTexels[node*NodeTexels],&hi=nodeTexels[node*NodeTexels+1];
            Vec3 low,high;
            const unsigned tag=static_cast<unsigned>(hi.w);
            if(tag) {
                const unsigned first=LeafFirst(tag),count=LeafCount(tag);
                const Triangle &firstTriangle=triangles[first];
                low=Minimum(firstTriangle.a,Minimum(firstTriangle.b,firstTriangle.c));
                high=Maximum(firstTriangle.a,Maximum(firstTriangle.b,firstTriangle.c));
                for(unsigned i=first+1;i<first+count;++i) {
                    const Triangle &t=triangles[i];
                    low=Minimum(low,Minimum(t.a,Minimum(t.b,t.c)));
                    high=Maximum(high,Maximum(t.a,Maximum(t.b,t.c)));
                }
            } else {
                const unsigned left=node+1,right=static_cast<unsigned>(nodeTexels[left*NodeTexels].w);
                const Float4 a=nodeTexels[left*NodeTexels],b=nodeTexels[right*NodeTexels];
                const Float4 c=nodeTexels[left*NodeTexels+1],d=nodeTexels[right*NodeTexels+1];
                low=Minimum({a.x,a.y,a.z},{b.x,b.y,b.z});
                high=Maximum({c.x,c.y,c.z},{d.x,d.y,d.z});
            }
            lo.x=low.x;lo.y=low.y;lo.z=low.z;
            hi.x=high.x;hi.y=high.y;hi.z=high.z;
        }
        fingerprint=nextFingerprint;
        return true;
    }

    Hit Trace(Vec3 origin,Vec3 direction,float maximum=1e6f,float minimum=1e-4f) const
    {
        Hit hit;
        if(!Finite(origin)||!Finite(direction)||!std::isfinite(maximum)||maximum<=minimum)return hit;
        direction=Normalized(direction);
        if(Dot(direction,direction)==0)return hit;
        unsigned node=0;
        while(node<NodeCount()) {
            const Float4 lo=nodeTexels[node*NodeTexels],hi=nodeTexels[node*NodeTexels+1];
            if(!IntersectBounds({lo.x,lo.y,lo.z},{hi.x,hi.y,hi.z},origin,direction,maximum)) {
                node=static_cast<unsigned>(lo.w);continue;
            }
            const unsigned tag=static_cast<unsigned>(hi.w);
            if(tag) {
                const unsigned first=LeafFirst(tag),count=LeafCount(tag);
                for(unsigned i=first;i<first+count;++i) {
                    if(IntersectTriangle(triangles[i],origin,direction,minimum,maximum)) {
                        hit.found=true;hit.distance=maximum;hit.triangle=i;
                    }
                }
                node=static_cast<unsigned>(lo.w);
            } else ++node;
        }
        if(hit.found) {
            const Triangle &t=triangles[hit.triangle];
            hit.position=Add(origin,Scale(direction,hit.distance));
            hit.normal=Normalized(Cross(Sub(t.b,t.a),Sub(t.c,t.a)));
            if(Dot(hit.normal,direction)>0)hit.normal=Scale(hit.normal,-1);
            hit.albedo=t.albedo;
        }
        return hit;
    }

private:
    std::vector<unsigned> inputIndices;
    std::vector<uint8_t> inputIncluded;

    // -1: invalid, 0: degenerate, 1: included in the acceleration structure.
    static int TriangleKind(const Triangle &t)
    {
        if(!Finite(t.a)||!Finite(t.b)||!Finite(t.c)||!Finite(t.albedo))return -1;
        const Vec3 e1=Sub(t.b,t.a),e2=Sub(t.c,t.a),normal=Cross(e1,e2);
        const float area2=Dot(normal,normal);
        if(!Finite(e1)||!Finite(e2)||!Finite(normal)||!std::isfinite(area2))return -1;
        return area2>1e-16f?1:0;
    }

    void SetTriangle(unsigned index,Triangle t)
    {
        t.albedo=Minimum(Maximum(t.albedo,{0,0,0}),{1,1,1});
        triangles[index]=t;
        const Vec3 e1=Sub(t.b,t.a),e2=Sub(t.c,t.a);
        triangleTexels[index*TriangleTexels]={t.a.x,t.a.y,t.a.z,0};
        triangleTexels[index*TriangleTexels+1]={e1.x,e1.y,e1.z,0};
        triangleTexels[index*TriangleTexels+2]={e2.x,e2.y,e2.z,0};
        triangleTexels[index*TriangleTexels+3]={t.albedo.x,t.albedo.y,t.albedo.z,0};
    }

    void BuildNode(unsigned first,unsigned count,const std::vector<Triangle> &input)
    {
        Vec3 low={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
        Vec3 high=Scale(low,-1),centroidLow=low,centroidHigh=high;
        auto centre=[](const Triangle &t) { return Add(Add(Scale(t.a,1.f/3),Scale(t.b,1.f/3)),Scale(t.c,1.f/3)); };
        for(unsigned i=first;i<first+count;++i) {
            const Triangle &t=input[inputIndices[i]];
            low=Minimum(low,Minimum(t.a,Minimum(t.b,t.c)));
            high=Maximum(high,Maximum(t.a,Maximum(t.b,t.c)));
            const Vec3 centroid=centre(t);
            centroidLow=Minimum(centroidLow,centroid);centroidHigh=Maximum(centroidHigh,centroid);
        }
        const unsigned node=NodeCount();
        nodeTexels.push_back({low.x,low.y,low.z,0});
        nodeTexels.push_back({high.x,high.y,high.z,0});
        if(count<=4)nodeTexels[node*NodeTexels+1].w=static_cast<float>(first*4u+count);
        else {
            const Vec3 span=Sub(centroidHigh,centroidLow);
            unsigned axis=span.y>span.x?1:0;if(span.z>Component(span,axis))axis=2;
            const unsigned half=count/2;
            std::nth_element(inputIndices.begin()+first,inputIndices.begin()+first+half,inputIndices.begin()+first+count,
                [&](unsigned ai,unsigned bi) {
                    const Triangle &a=input[ai],&b=input[bi];
                    const float ca=Component(centre(a),axis),cb=Component(centre(b),axis);
                    if(ca!=cb)return ca<cb;
                    // A total ordering makes repeated builds reproducible for equal centroids.
                    const float aa[]={a.a.x,a.a.y,a.a.z,a.b.x,a.b.y,a.b.z,a.c.x,a.c.y,a.c.z,
                                      a.albedo.x,a.albedo.y,a.albedo.z};
                    const float bb[]={b.a.x,b.a.y,b.a.z,b.b.x,b.b.y,b.b.z,b.c.x,b.c.y,b.c.z,
                                      b.albedo.x,b.albedo.y,b.albedo.z};
                    return std::lexicographical_compare(aa,aa+12,bb,bb+12);
                });
            BuildNode(first,half,input);BuildNode(first+half,count-half,input);
        }
        nodeTexels[node*NodeTexels].w=static_cast<float>(NodeCount());
    }
};

} // namespace WaterPathTracing
