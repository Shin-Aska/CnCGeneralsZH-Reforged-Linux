// Frozen Scene::Build evaluator from a094410c, before primitive precomputation.
// Preserve the original recursive bounds walk, centroid arithmetic and sorting.
#pragma once
#include "Platform/WaterPathTracingScene.h"
namespace WaterPathBuildReference {
using namespace WaterPathTracing;
class Scene {
public:
    std::vector<Triangle> triangles;
    std::vector<Float4> nodeTexels;
    std::vector<Float4> triangleTexels;
    uint64_t fingerprint=0;

    unsigned NodeCount() const { return static_cast<unsigned>(nodeTexels.size()/NodeTexels); }
    bool Empty() const { return triangles.empty(); }
    unsigned SourceTriangleIndex(unsigned gpuIndex) const { return inputIndices[gpuIndex]; }

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

} // namespace WaterPathBuildReference
