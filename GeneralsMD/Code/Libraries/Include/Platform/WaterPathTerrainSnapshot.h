// Exact source proof for the optional native water terrain packet. No time/camera shortcut.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace WaterPathTracing {
struct TerrainSnapshotKey {
    const void *Map=nullptr,*Shroud=nullptr;
    int Width=0,Height=0,Border=0,FlipRowBytes=0,CellColumns=0,CellRows=0;
    float CellWidth=0,CellHeight=0,WaterLevel=0,TerrainCellSize=10;
    bool HasView=false,HasShroud=false;
    bool operator==(const TerrainSnapshotKey &other) const {
        return Map==other.Map && Shroud==other.Shroud && Width==other.Width && Height==other.Height
            && Border==other.Border && FlipRowBytes==other.FlipRowBytes && CellColumns==other.CellColumns
            && CellRows==other.CellRows && CellWidth==other.CellWidth && CellHeight==other.CellHeight
            && WaterLevel==other.WaterLevel && TerrainCellSize==other.TerrainCellSize
            && HasView==other.HasView && HasShroud==other.HasShroud;
    }
};
class TerrainSnapshotProof {
public:
    uint64_t Digest=0,Reuses=0,Refreshes=0;
    bool Matches(const TerrainSnapshotKey &key,const unsigned char *heights,size_t heightBytes,
        const unsigned char *flips,size_t flipBytes,const std::vector<unsigned char> &cells) const {
        return Ready && Key==key && same(Heights,heights,heightBytes) && same(Flips,flips,flipBytes) && Cells==cells;
    }
    void Commit(const TerrainSnapshotKey &key,const unsigned char *heights,size_t heightBytes,
        const unsigned char *flips,size_t flipBytes,const std::vector<unsigned char> &cells,uint64_t digest) {
        Key=key;copy(Heights,heights,heightBytes);copy(Flips,flips,flipBytes);Cells=cells;
        Digest=digest;Ready=true;++Refreshes;
    }
private:
    TerrainSnapshotKey Key;
    std::vector<unsigned char> Heights,Flips,Cells;
    bool Ready=false;
    static bool same(const std::vector<unsigned char> &saved,const unsigned char *data,size_t count) {
        return saved.size()==count && (!count || (data && std::memcmp(saved.data(),data,count)==0));
    }
    static void copy(std::vector<unsigned char> &saved,const unsigned char *data,size_t count) {
        if(count)saved.assign(data,data+count);else saved.clear();
    }
};
inline void ExpandTerrainShroud(const TerrainSnapshotKey &key,const std::vector<unsigned char> &cells,
    std::vector<unsigned char> &visibility) {
    visibility.resize(size_t(key.Width)*key.Height);
    if(!key.HasShroud){std::fill(visibility.begin(),visibility.end(),255);return;}
    std::vector<int> columns(key.Width);
    for(int x=0;x<key.Width;++x)columns[x]=int(std::floor((x-key.Border)*key.TerrainCellSize/key.CellWidth));
    for(int y=0;y<key.Height;++y) {
        const int row=int(std::floor((y-key.Border)*key.TerrainCellSize/key.CellHeight));
        for(int x=0;x<key.Width;++x) {
            const int column=columns[x];
            visibility[size_t(y)*key.Width+x]=row>=0 && column>=0 && row<key.CellRows && column<key.CellColumns
                ? cells[size_t(row)*key.CellColumns+column] : 0;
        }
    }
}
}
