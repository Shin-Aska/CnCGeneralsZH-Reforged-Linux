// Exact source-row retention for a native water capture packet. Render-only.
#pragma once
#include "Platform/WaterPathTracingMaterials.h"

namespace WaterPathTracing {

static_assert(sizeof(Triangle)==12*sizeof(float),"Packet comparisons require packed triangle fields");

// Revision is a producer-owned, monotonically assigned packet generation, not a
// geometry hash. Reuse requires the same identity, generation, count and offset.
struct PacketSpanStamp {
    const void *Identity=nullptr;
    size_t First=0,Count=0;
    uint64_t Revision=0;
};
struct PacketSpanRows {
    PacketSpanStamp Stamp;
    const Triangle *Triangles=nullptr;
    const PrimaryMaterial *Materials=nullptr;
};

// Current spans must be contiguous, ordered and within the scene capacity.
// Sources must remain alive and separate from the destination until Apply ends.
// Planning never mutates published rows; callers can finish validation first.
class RetainedPacketUpdate {
public:
    struct Patch { PacketSpanRows Rows; bool Triangles=false,Materials=false; };
    std::vector<Patch> Patches;
    std::vector<PacketSpanStamp> Stamps;
    size_t Count=0,TriangleRowsRetained=0,MaterialRowsRetained=0;
    bool TrianglesChanged=false,MaterialsChanged=false;

    RetainedPacketUpdate(const std::vector<PacketSpanStamp> &before,
        const std::vector<PacketSpanRows> &current,const std::vector<Triangle> &triangles,
        const std::vector<PrimaryMaterial> &materials)
    {
        Stamps.reserve(current.size());Patches.reserve(current.size());
        for(size_t i=0;i<current.size();++i) {
            const auto &rows=current[i];const auto &span=rows.Stamp;
            const bool haveTriangles=span.First<=triangles.size() && span.Count<=triangles.size()-span.First;
            const bool haveMaterials=span.First<=materials.size() && span.Count<=materials.size()-span.First;
            const bool same=i<before.size() && span.Revision && before[i].Revision==span.Revision
                && before[i].Identity==span.Identity && before[i].First==span.First && before[i].Count==span.Count;
            const bool keepTriangles=haveTriangles && (same || !span.Count
                || std::memcmp(triangles.data()+span.First,rows.Triangles,span.Count*sizeof(Triangle))==0);
            const bool keepMaterials=haveMaterials && (same || !span.Count
                || std::memcmp(materials.data()+span.First,rows.Materials,span.Count*sizeof(PrimaryMaterial))==0);
            if(keepTriangles)TriangleRowsRetained+=span.Count;
            if(keepMaterials)MaterialRowsRetained+=span.Count;
            if(!keepTriangles || !keepMaterials)Patches.push_back({rows,!keepTriangles,!keepMaterials});
            TrianglesChanged|=!keepTriangles;MaterialsChanged|=!keepMaterials;
            Count+=span.Count;Stamps.push_back(span);
        }
        TrianglesChanged|=Count!=triangles.size();MaterialsChanged|=Count!=materials.size();
    }

    void Apply(std::vector<Triangle> &triangles,std::vector<PrimaryMaterial> &materials) const
    {
        triangles.resize(Count);materials.resize(Count);
        for(const auto &patch:Patches) {
            const auto &rows=patch.Rows;const auto &span=rows.Stamp;
            if(!span.Count)continue;
            if(patch.Triangles)std::memcpy(triangles.data()+span.First,rows.Triangles,span.Count*sizeof(Triangle));
            if(patch.Materials)std::memcpy(materials.data()+span.First,rows.Materials,span.Count*sizeof(PrimaryMaterial));
        }
    }
};
}
