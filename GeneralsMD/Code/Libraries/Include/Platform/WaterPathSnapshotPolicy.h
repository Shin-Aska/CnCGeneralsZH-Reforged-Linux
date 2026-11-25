// Per-map native water packet selection. Plane changes still use exact current terrain geometry.
#pragma once
#include <algorithm>
#include <cstddef>
namespace WaterPathTracing {
struct NativeSnapshotKey {
    const void *Map=nullptr,*Data=nullptr,*Owner=nullptr;
    int Width=0,Height=0,Border=0;
    bool operator==(const NativeSnapshotKey &other) const {
        return Map==other.Map && Data==other.Data && Owner==other.Owner && Width==other.Width
            && Height==other.Height && Border==other.Border;
    }
};
class NativeSnapshotPolicy {
public:
    // 96Ki source terrain rows cost 9MiB for triangle+material records alone;
    // packed shading, validation snapshots and native/AS buffers multiply that footprint.
    static constexpr size_t TerrainRowBudget=96u*1024u;
    bool Changed=false;
    bool RetainFull(const NativeSnapshotKey &key,size_t rows) {
        Changed=!Initialized || !(Key==key);
        if(Changed){Key=key;Initialized=true;Scoped=false;PeakRows=0;}
        const bool wasScoped=Scoped;
        PeakRows=std::max(PeakRows,rows);
        Scoped=Scoped || rows>TerrainRowBudget;
        Changed=Changed || wasScoped!=Scoped;
        return !Scoped;
    }
    void Reset(){Initialized=false;Scoped=false;PeakRows=0;Changed=false;}
    size_t Peak() const {return PeakRows;}
private:
    NativeSnapshotKey Key;
    size_t PeakRows=0;
    bool Initialized=false,Scoped=false;
};
}
