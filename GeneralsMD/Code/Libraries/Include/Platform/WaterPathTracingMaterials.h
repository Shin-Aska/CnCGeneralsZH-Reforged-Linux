// Optional primary-hit appearance. Secondary transport retains Triangle::albedo.
#pragma once
#include "Platform/WaterPathTracingScene.h"
#include <cstddef>

namespace WaterPathTracing {

constexpr size_t MaximumPrimaryTexels=512u*1024u;
constexpr unsigned MaximumPrimaryTextureSide=64;

// Base level followed by floor-halved levels, with each dimension at least one.
inline size_t PrimaryTextureTexelCount(unsigned width,unsigned height)
{
    if(!width||!height)return 0;
    size_t count=0;
    while(true) {
        count+=size_t(width)*height;
        if(width==1&&height==1)return count;
        width=width>1?width/2:1;height=height>1?height/2:1;
    }
}

struct PrimaryMaterial {
    Float4 uvAB{};           // ua, va, ub, vb
    Float4 uvCSize{};        // uc, vc, width, height
    Float4 diffuseOffset{};  // linear base diffuse RGB, atlas offset + 1; zero disables
};
static_assert(sizeof(PrimaryMaterial)==12*sizeof(float),"Primary appearance must match three GPU texels");

struct PrimaryMaterialPacket {
    // Input triangle order, including degenerate slots; Scene supplies the GPU reorder map.
    std::vector<PrimaryMaterial> triangles;
    // Append-only per scene. Each texture is a contiguous row-major linear RGB
    // mip chain, base first, with PrimaryTextureTexelCount texels in total.
    std::vector<Float4> texels;
};

inline bool ValidPrimaryMaterials(const PrimaryMaterialPacket &packet,size_t triangleCount)
{
    if(packet.triangles.size()!=triangleCount || packet.texels.size()>MaximumPrimaryTexels)return false;
    const auto finite=[](Float4 v) {
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);
    };
    const auto rgb=[](Float4 v) {
        return v.x>=0&&v.x<=1&&v.y>=0&&v.y<=1&&v.z>=0&&v.z<=1;
    };
    for(const Float4 texel:packet.texels)if(!finite(texel)||!rgb(texel))return false;
    for(const PrimaryMaterial &material:packet.triangles) {
        if(!finite(material.uvAB)||!finite(material.uvCSize)||!finite(material.diffuseOffset)
            ||!rgb(material.diffuseOffset)||material.diffuseOffset.w<0)return false;
        if(material.diffuseOffset.w==0)continue;
        const float width=material.uvCSize.z,height=material.uvCSize.w;
        if(width<1||width>MaximumPrimaryTextureSide||height<1||height>MaximumPrimaryTextureSide
            ||std::floor(width)!=width||std::floor(height)!=height
            ||std::floor(material.diffuseOffset.w)!=material.diffuseOffset.w
            ||material.diffuseOffset.w>float(MaximumPrimaryTexels))return false;
        const size_t offset=size_t(material.diffuseOffset.w)-1;
        if(offset>packet.texels.size()
            ||PrimaryTextureTexelCount(unsigned(width),unsigned(height))>packet.texels.size()-offset)return false;
    }
    return true;
}

inline uint64_t FingerprintPrimaryMaterials(const PrimaryMaterialPacket &packet)
{
    uint64_t hash=14695981039346656037ull;
    const auto bytes=[&](const void *data,size_t count) {
        const auto *p=static_cast<const uint8_t*>(data);
        for(size_t i=0;i<count;++i){hash^=p[i];hash*=1099511628211ull;}
    };
    const uint64_t sizes[]={uint64_t(packet.triangles.size()),uint64_t(packet.texels.size())};
    bytes(sizes,sizeof(sizes));
    bytes(packet.triangles.data(),packet.triangles.size()*sizeof(PrimaryMaterial));
    bytes(packet.texels.data(),packet.texels.size()*sizeof(Float4));
    return hash;
}

} // namespace WaterPathTracing
