/* Exact static field reuse for consecutive water draws. Animated inputs stay outside this cache. */
#pragma once

#include "WaterProjectedMesh.h"
#include "WaterSurfaceModel.h"

namespace WaterStaticSampleCache {
struct Sample {
    WaterSurfaceModel::Sample offshore;
    float exposure;
    float terrainDepth;
    float terrainDamping;
};

// A stable projected mesh visits the same coordinates in the same order. A sequence
// cache avoids a hash lookup per vertex; changed order/coordinates are ordinary misses.
// The owner must Clear when terrain, optical polygons, or field interpretation changes.
class Cache {
    struct Entry { WaterProjectedMesh::WorldKey key; Sample value; };
    std::vector<Entry> entries;
    size_t cursor = 0;
    const size_t limit;
public:
    explicit Cache(size_t maximum = WaterSurfaceModel::VertexBudget) : limit(maximum) {}
    void BeginRender() { cursor = 0; }
    void Clear() { entries.clear(); cursor = 0; }
    size_t Size() const { return entries.size(); }

    template<class Compute>
    Sample Get(float x, float y, float level, const Compute &compute, bool *reused = nullptr) {
        if (reused) *reused = false;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(level) || cursor >= limit)
            return compute();
        const auto key = WaterProjectedMesh::SampleKey(x, y, level);
        const size_t index = cursor++;
        if (index < entries.size() && entries[index].key == key) {
            if (reused) *reused = true;
            return entries[index].value;
        }
        const Sample value = compute();
        if (index < entries.size()) entries[index] = {key, value};
        else entries.push_back({key, value});
        return value;
    }
};
}
