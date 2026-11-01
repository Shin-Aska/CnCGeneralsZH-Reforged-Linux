/* Exact, bounded reuse of the projected base lattice before dynamic water edits. */
#pragma once
#include "WaterProjectedMesh.h"
#include <deque>

namespace WaterProjectedMeshCache {
// Optional coarse CPU attribution. The caller supplies its existing clock; null
// Profile arguments perform no clock reads and preserve normal cache behavior.
struct Profile {
    double (*clock)() = nullptr;
    double stamp = 0, lookupMs = 0, hitCopyMs = 0, buildMs = 0, storeMs = 0;
    size_t probes = 0, hits = 0, misses = 0, hitCopyBytes = 0, storeCopyBytes = 0;
    void Begin() {
        stamp = clock();
    }
    void Mark(double &phase) {
        const double now = clock();
        phase += now - stamp;
        stamp = now;
    }
};

inline size_t ProfileMeshBytes(const WaterProjectedMesh::Mesh &m) {
    return m.vertices.size() * sizeof(WaterProjectedMesh::Vertex) + m.indices.size() * sizeof(uint16_t)
        + m.transitions.size() * sizeof(WaterProjectedMesh::CentreTransition)
        + m.collars.size() * sizeof(WaterProjectedMesh::BoundaryCollar);
}
class Cache {
public:
    explicit Cache(size_t maxVertices=262144,size_t maxEntries=32)
        : maxVertices_(maxVertices),maxEntries_(maxEntries) {}

    bool Build(const WaterProjectedMesh::Point corners[4], const WaterProjectedMesh::Projection &projection,
        WaterProjectedMesh::Resolution resolution, WaterProjectedMesh::Mesh &out, size_t vertexBudget = 60000,
        const std::vector<WaterProjectedMesh::BoundaryKnot> &knots = {}, bool *reused = nullptr,
        Profile *profile = nullptr) {
        if (profile)
            profile->Begin();
        if (reused) *reused=false;
        for (const Entry &entry : entries_) {
            if (profile)
                ++profile->probes;
            if (!entry.Matches(corners, projection, resolution, vertexBudget, knots))
                continue;
            if (profile) {
                profile->Mark(profile->lookupMs);
                ++profile->hits;
            }
            // Callers refine, cull and clip their copy. None of those edits can enter the cache.
            out=entry.mesh;
            if (profile) {
                profile->Mark(profile->hitCopyMs);
                profile->hitCopyBytes += ProfileMeshBytes(entry.mesh);
            }
            if (reused) *reused=true;
            return true;
        }
        if (profile) {
            profile->Mark(profile->lookupMs);
            ++profile->misses;
        }
        // Preserve Build's result, including a partially built mesh on failure. A failed or
        // oversized request must not evict useful entries or masquerade as an earlier success.
        const bool built = WaterProjectedMesh::Build(corners, projection, resolution, out, vertexBudget, knots);
        if (profile)
            profile->Mark(profile->buildMs);
        if (!built)
            return false;
        if (!maxEntries_ || out.vertices.size()>maxVertices_) return true;
        while (!entries_.empty() && (entries_.size()>=maxEntries_ ||
            cachedVertices_>maxVertices_-out.vertices.size())) {
            cachedVertices_-=entries_.front().mesh.vertices.size();entries_.pop_front();
        }
        entries_.emplace_back(corners,projection,resolution,vertexBudget,knots,out);
        cachedVertices_+=out.vertices.size();
        if (profile) {
            profile->Mark(profile->storeMs);
            profile->storeCopyBytes += ProfileMeshBytes(out) + knots.size() * sizeof(WaterProjectedMesh::BoundaryKnot);
        }
        return true;
    }
    void Clear() { entries_.clear();cachedVertices_=0; }
    size_t EntryCount() const { return entries_.size(); }
    size_t CachedVertices() const { return cachedVertices_; }

private:
    static bool Same(double a,double b) {
        uint64_t aa,bb;std::memcpy(&aa,&a,sizeof(aa));std::memcpy(&bb,&b,sizeof(bb));return aa==bb;
    }
    static bool Same(WaterProjectedMesh::Point a,WaterProjectedMesh::Point b) {
        return Same(a.x,b.x) && Same(a.y,b.y);
    }
    struct Entry {
        WaterProjectedMesh::Point corners[4];
        WaterProjectedMesh::Projection projection;
        WaterProjectedMesh::Resolution resolution;
        size_t vertexBudget;
        std::vector<WaterProjectedMesh::BoundaryKnot> knots;
        WaterProjectedMesh::Mesh mesh;
        Entry(const WaterProjectedMesh::Point c[4],const WaterProjectedMesh::Projection &p,
            WaterProjectedMesh::Resolution r,size_t budget,const std::vector<WaterProjectedMesh::BoundaryKnot> &k,
            const WaterProjectedMesh::Mesh &m):projection(p),resolution(r),vertexBudget(budget),knots(k),mesh(m) {
            std::copy(c,c+4,corners);
        }
        bool Matches(const WaterProjectedMesh::Point c[4],const WaterProjectedMesh::Projection &p,
            WaterProjectedMesh::Resolution r,size_t budget,const std::vector<WaterProjectedMesh::BoundaryKnot> &k) const {
            if (r.columns!=resolution.columns || r.rows!=resolution.rows || budget!=vertexBudget || k.size()!=knots.size()) return false;
            for (int i=0;i<4;++i) if (!Same(c[i],corners[i])) return false;
            for (int i=0;i<9;++i) if (!Same(p.matrix[i],projection.matrix[i]) || !Same(p.inverse[i],projection.inverse[i])) return false;
            if (!Same(p.depth.a,projection.depth.a) || !Same(p.depth.b,projection.depth.b) || !Same(p.depth.c,projection.depth.c) ||
                !Same(p.nearDistance,projection.nearDistance) || !Same(p.farDistance,projection.farDistance) || !Same(p.margin,projection.margin)) return false;
            for (size_t i=0;i<k.size();++i) if (!Same(k[i].world,knots[i].world) || k[i].boundary!=knots[i].boundary ||
                k[i].index!=knots[i].index || !Same(k[i].edgeParameter,knots[i].edgeParameter)) return false;
            return true;
        }
    };
    size_t maxVertices_,maxEntries_,cachedVertices_=0;
    std::deque<Entry> entries_; // FIFO keeps capacity and eviction independent of frame timing.
};
}
