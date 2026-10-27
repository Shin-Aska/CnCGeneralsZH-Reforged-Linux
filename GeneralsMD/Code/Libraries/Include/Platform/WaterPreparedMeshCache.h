/* Exact reuse of one terrain-culled, rest-waterline-clipped projected water mesh. */
#pragma once
#include "WaterProjectedMesh.h"

namespace WaterPreparedMeshCache {
class Cache {
public:
    explicit Cache(size_t maxBytes=32u*1024u*1024u):maxBytes_(maxBytes) {}

    // The owner clears this cache when terrain, optical support or processing rules change.
    // Dynamic bow refinement is part of the complete input mesh, never an implicit key.
    template<class Compute>
    void Process(WaterProjectedMesh::Mesh &mesh,float level,bool cullTerrain,bool waveCage,
        const Compute &compute,bool *reused=nullptr) {
        if (reused) *reused=false;
        size_t inputBytes=0;
        if (!maxBytes_ || !std::isfinite(level) || !Measure(mesh,maxBytes_,inputBytes)) { compute(mesh);return; }
        if (occupied_ && Same(level,level_) && cullTerrain==cullTerrain_ && waveCage==waveCage_ && Same(mesh,input_)) {
            mesh=output_;
            if (reused) *reused=true;
            return;
        }
        if (!Valid(mesh)) { compute(mesh);return; }
        WaterProjectedMesh::Mesh before=mesh;
        compute(mesh);
        size_t outputBytes=0;
        // Bypasses preserve the previous entry. Neither an oversized output nor invalid
        // callback result can displace a valid earlier input/output pair.
        if (!Measure(mesh,maxBytes_-inputBytes,outputBytes) || !Valid(mesh)) return;
        WaterProjectedMesh::Mesh after=mesh;
        if (!Measure(before,maxBytes_,inputBytes,true) || !Measure(after,maxBytes_-inputBytes,outputBytes,true)) return;
        std::swap(input_,before);std::swap(output_,after);
        level_=level;cullTerrain_=cullTerrain;waveCage_=waveCage;
        cachedBytes_=inputBytes+outputBytes;occupied_=true;
    }
    void Clear() {
        input_=WaterProjectedMesh::Mesh{};output_=WaterProjectedMesh::Mesh{};
        cachedBytes_=0;occupied_=false;
    }
    size_t CachedBytes() const { return cachedBytes_; }

private:
    static bool Same(double a,double b) {
        uint64_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
    }
    static bool Same(float a,float b) {
        uint32_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
    }
    static bool Same(WaterProjectedMesh::Point a,WaterProjectedMesh::Point b) { return Same(a.x,b.x) && Same(a.y,b.y); }
    static bool Same(const WaterProjectedMesh::Mesh &a,const WaterProjectedMesh::Mesh &b) {
        if (a.cells!=b.cells || a.indices!=b.indices || a.vertices.size()!=b.vertices.size() ||
            a.collars.size()!=b.collars.size() || a.transitions.size()!=b.transitions.size()) return false;
        for (size_t i=0;i<a.vertices.size();++i) {
            const auto &x=a.vertices[i],&y=b.vertices[i];
            if (!Same(x.world,y.world) || x.boundary!=y.boundary || !Same(x.edgeParameter,y.edgeParameter) ||
                x.knot!=y.knot || x.transition!=y.transition || x.collar!=y.collar || x.lattice!=y.lattice) return false;
        }
        for (size_t i=0;i<a.transitions.size();++i) {
            const auto &x=a.transitions[i],&y=b.transitions[i];
            if (!Same(x.analytic,y.analytic)) return false;
            for (int j=0;j<3;++j) if (!Same(x.source[j],y.source[j]) || !Same(x.weight[j],y.weight[j])) return false;
        }
        for (size_t i=0;i<a.collars.size();++i) {
            const auto &x=a.collars[i],&y=b.collars[i];
            if (x.count!=y.count || !Same(x.fade,y.fade)) return false;
            // Build leaves unused slots uninitialized. Only active slots have meaning.
            for (int j=0;j<x.count;++j) if (x.edge[j]!=y.edge[j] || !Same(x.parameter[j],y.parameter[j]) || !Same(x.weight[j],y.weight[j])) return false;
        }
        return true;
    }
    static bool Measure(const WaterProjectedMesh::Mesh &mesh,size_t limit,size_t &bytes,bool allocated=false) {
        bytes=0;
        const size_t counts[]={allocated?mesh.vertices.capacity():mesh.vertices.size(),allocated?mesh.indices.capacity():mesh.indices.size(),
            allocated?mesh.transitions.capacity():mesh.transitions.size(),allocated?mesh.collars.capacity():mesh.collars.size()};
        const size_t widths[]={sizeof(WaterProjectedMesh::Vertex),sizeof(uint16_t),sizeof(WaterProjectedMesh::CentreTransition),sizeof(WaterProjectedMesh::BoundaryCollar)};
        for (int i=0;i<4;++i) {
            if (counts[i]>(limit-bytes)/widths[i]) return false;
            bytes+=counts[i]*widths[i];
        }
        return mesh.vertices.size()<=65536 && mesh.indices.size()%3==0;
    }
    static bool Valid(const WaterProjectedMesh::Mesh &mesh) {
        for (const auto &v:mesh.vertices) if (!std::isfinite(v.world.x) || !std::isfinite(v.world.y) || !std::isfinite(v.edgeParameter) ||
            v.boundary < -1 || v.boundary>3 || v.transition < -1 || v.collar < -1 ||
            (v.transition>=0 && size_t(v.transition)>=mesh.transitions.size()) || (v.collar>=0 && size_t(v.collar)>=mesh.collars.size())) return false;
        for (uint16_t i:mesh.indices) if (i>=mesh.vertices.size()) return false;
        for (const auto &t:mesh.transitions) {
            if (!std::isfinite(t.analytic)) return false;
            for (int i=0;i<3;++i) if (!std::isfinite(t.source[i].x) || !std::isfinite(t.source[i].y) || !std::isfinite(t.weight[i])) return false;
        }
        for (const auto &c:mesh.collars) {
            if (c.count<0 || c.count>4 || !std::isfinite(c.fade)) return false;
            for (int i=0;i<c.count;++i) if (!std::isfinite(c.parameter[i]) || !std::isfinite(c.weight[i])) return false;
        }
        return true;
    }
    size_t maxBytes_,cachedBytes_=0;
    WaterProjectedMesh::Mesh input_,output_;
    float level_=0;
    bool occupied_=false,cullTerrain_=false,waveCage_=false;
};
}
