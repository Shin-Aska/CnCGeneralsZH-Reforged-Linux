#pragma once

#include "Platform/WaterShaderLayout.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace WaterRenderPacket {

using WaterShaderLayout::Float4;

enum class ResourceType : std::uint8_t { None,Texture2D,Texture2DArray };

struct Resource
{
    const void *identity;
    ResourceType type;
    bool available;
    std::uint32_t width,height,layers,capacity;
};

struct Float4Span
{
    const Float4 *data;
    std::size_t count,capacity;
};

struct Viewport
{
    float x,y,width,height,minDepth,maxDepth;
};

struct SceneInput
{
    float view[16];
    float projection[16];
    Viewport viewport;
    Float4 combiner[3];
    Float4 sunDirection,sunColour;
    Float4 shadowFromClip[4];
    Float4 shadowParameters,shadowViewport,shadowSoftness,volumeParameters;
    bool snapshotAvailable;
    Resource sceneColour,sceneDepth;
};

struct SurfaceInput
{
    bool river;
    Float4 reflectionArea,waterParameters,terrainParameters,opticalParameters,hullParameters;
    Float4Span meanHulls,contactHulls,visualHullMetadata,visualHullPackets;
    Float4 fftParameters,foamHistoryArea,foamHistoryParameters;
    Float4 foamFillLights[4];
    Float4 coastalArea,coastalParameters;
    bool authoredMaterialAvailable,reflectionAvailable,heightWaveCageAvailable,terrainAvailable;
    bool opticalAvailable,bedAvailable,fftAvailable,foamHistoryAvailable,coastalAvailable,meshContactAvailable;
    Resource authoredMaterial,reflection,heightWaveCage,terrainHeight,optical,bed,foamHistory,coastal;
    Resource fftDisplacement[3],fftArray;
};

struct Inputs { SceneInput scene; SurfaceInput surface; };

struct BoundResources
{
    bool snapshotAvailable,authoredMaterialAvailable,reflectionAvailable,heightWaveCageAvailable;
    bool terrainAvailable,opticalAvailable,bedAvailable,fftAvailable;
    bool foamHistoryAvailable,coastalAvailable,meshContactAvailable;
    Resource sceneColour,sceneDepth,authoredMaterial,reflection,heightWaveCage,terrainHeight,optical,bed,foamHistory,coastal;
    Resource fftDisplacement[3],fftArray;
};

struct DrawPacket
{
    WaterShaderLayout::PixelConstants constants;
    WaterShaderLayout::HullPacketConstants hullPackets;
    BoundResources resources;
};

enum class ValidationError : std::uint8_t
{
    None,NonFinite,InvalidViewport,SingularMatrix,Capacity,Unavailable,ResourceType,ResourceExtent,TargetMismatch,
    InvalidHistory,InconsistentHullPacket
};

inline bool Finite(const Float4 &v)
{
    return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::isfinite(v.w);
}

inline bool Finite(const float *values,std::size_t count)
{
    for(std::size_t i=0;i<count;++i)if(!std::isfinite(values[i]))return false;
    return true;
}

inline void Identity(float *matrix)
{
    std::memset(matrix,0,16*sizeof(float));
    for(unsigned i=0;i<4;++i)matrix[i*5]=1;
}

inline bool Invert(const float *source,float *inverse)
{
    double rows[4][8]={};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)rows[r][c]=source[r*4+c];
    for(unsigned i=0;i<4;++i)rows[i][4+i]=1;
    for(unsigned column=0;column<4;++column) {
        unsigned pivot=column;
        for(unsigned r=column+1;r<4;++r)if(std::fabs(rows[r][column])>std::fabs(rows[pivot][column]))pivot=r;
        if(std::fabs(rows[pivot][column])<=1.0e-12)return false;
        if(pivot!=column)for(unsigned c=0;c<8;++c){const double v=rows[column][c];rows[column][c]=rows[pivot][c];rows[pivot][c]=v;}
        const double divisor=rows[column][column];
        for(unsigned c=0;c<8;++c)rows[column][c]/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=column) {
            const double scale=rows[r][column];
            for(unsigned c=0;c<8;++c)rows[r][c]-=scale*rows[column][c];
        }
    }
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)inverse[r*4+c]=static_cast<float>(rows[r][4+c]);
    return Finite(inverse,16);
}

inline bool Fail(ValidationError value,ValidationError *error)
{
    if(error)*error=value;
    return false;
}

inline bool ValidateSpan(const Float4Span &span,std::size_t maximum,ValidationError *error)
{
    if(span.count>span.capacity||span.count>maximum||(!span.data&&span.count))return Fail(ValidationError::Capacity,error);
    for(std::size_t i=0;i<span.count;++i)if(!Finite(span.data[i]))return Fail(ValidationError::NonFinite,error);
    return true;
}

inline bool ValidateResource(const Resource &resource,ResourceType type,ValidationError *error)
{
    if(!resource.available||!resource.identity)return Fail(ValidationError::Unavailable,error);
    if(resource.type!=type)return Fail(ValidationError::ResourceType,error);
    const std::uint64_t required=std::uint64_t(resource.width)*resource.height*resource.layers;
    if(!resource.width||!resource.height||!resource.capacity||!resource.layers||resource.capacity<required)
        return Fail(ValidationError::ResourceExtent,error);
    if(type==ResourceType::Texture2D&&resource.layers!=1)return Fail(ValidationError::ResourceExtent,error);
    if(type==ResourceType::Texture2DArray&&resource.layers<6)return Fail(ValidationError::ResourceExtent,error);
    return true;
}

inline bool PackConstants(const SceneInput &scene,const SurfaceInput &surface,
    WaterShaderLayout::PixelConstants &constants,WaterShaderLayout::HullPacketConstants &packets,
    ValidationError *error=nullptr)
{
    if(error)*error=ValidationError::None;
    if(!Finite(scene.view,16)||!Finite(scene.projection,16)||!Finite(&scene.viewport.x,6)
        ||!Finite(reinterpret_cast<const float *>(scene.combiner),12)
        ||!Finite(scene.sunDirection)||!Finite(scene.sunColour)
        ||!Finite(reinterpret_cast<const float *>(scene.shadowFromClip),16)
        ||!Finite(scene.shadowParameters)||!Finite(scene.shadowViewport)||!Finite(scene.shadowSoftness)
        ||!Finite(scene.volumeParameters)||!Finite(surface.reflectionArea)||!Finite(surface.waterParameters)
        ||!Finite(surface.terrainParameters)||!Finite(surface.opticalParameters)||!Finite(surface.hullParameters)
        ||!Finite(surface.fftParameters)||!Finite(surface.foamHistoryArea)||!Finite(surface.foamHistoryParameters)
        ||!Finite(reinterpret_cast<const float *>(surface.foamFillLights),16)
        ||!Finite(surface.coastalArea)||!Finite(surface.coastalParameters))return Fail(ValidationError::NonFinite,error);
    if(scene.viewport.width<=0||scene.viewport.height<=0||scene.viewport.maxDepth-scene.viewport.minDepth<=1.0e-8f)
        return Fail(ValidationError::InvalidViewport,error);
    if(!ValidateSpan(surface.meanHulls,80,error)||!ValidateSpan(surface.contactHulls,80,error)
        ||!ValidateSpan(surface.visualHullMetadata,8,error)||!ValidateSpan(surface.visualHullPackets,704,error))return false;
    const float hullCount=surface.hullParameters.x;
    if(hullCount<0||hullCount>8||std::floor(hullCount)!=hullCount)return Fail(ValidationError::Capacity,error);
    const std::size_t contourRegisters=static_cast<std::size_t>(hullCount)*10;
    if(!surface.river&&(surface.meanHulls.count!=contourRegisters
        ||(surface.contactHulls.count&&surface.contactHulls.count!=contourRegisters)))
        return Fail(ValidationError::InconsistentHullPacket,error);
    if(surface.visualHullPackets.count!=surface.visualHullMetadata.count*88)
        return Fail(ValidationError::InconsistentHullPacket,error);
    if(surface.meshContactAvailable&&surface.visualHullMetadata.count!=static_cast<std::size_t>(hullCount))
        return Fail(ValidationError::Unavailable,error);
    std::memset(&constants,0,sizeof(constants));
    std::memset(&packets,0,sizeof(packets));
    std::memcpy(constants.Common.Combiner,scene.combiner,sizeof(constants.Common.Combiner));
    float viewProjection[16]={},worldFromClip[16],inverseView[16];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)
        viewProjection[r*4+c]+=scene.view[r*4+k]*scene.projection[k*4+c];
    if(!Invert(viewProjection,worldFromClip)||!Invert(scene.view,inverseView))return Fail(ValidationError::SingularMatrix,error);
    const float depthRange=scene.viewport.maxDepth-scene.viewport.minDepth;
    for(unsigned column=0;column<4;++column) {
        const float zRow=worldFromClip[8+column]/depthRange;
        worldFromClip[12+column]-=scene.viewport.minDepth*zRow;
        worldFromClip[8+column]=zRow;
    }
    std::memcpy(constants.Common.WorldFromClip,worldFromClip,sizeof(worldFromClip));
    constants.Common.CameraPosition={inverseView[12],inverseView[13],inverseView[14],0};
    constants.Common.SunDirection=scene.sunDirection;constants.Common.SunColour=scene.sunColour;
    constants.Common.Viewport={scene.viewport.x+.5f,scene.viewport.y+.5f,scene.viewport.width,scene.viewport.height};
    constants.Common.ReflectionArea=surface.reflectionArea;constants.Common.WaterParameters=surface.waterParameters;
    constants.Common.TerrainParameters=surface.terrainParameters;constants.Common.OpticalParameters=surface.opticalParameters;
    constants.Common.HullParameters=surface.hullParameters;constants.Common.FftParameters=surface.fftParameters;
    constants.Common.FoamHistoryArea=surface.foamHistoryArea;constants.Common.FoamHistoryParameters=surface.foamHistoryParameters;
    std::memcpy(constants.Common.FoamFillLights,surface.foamFillLights,sizeof(constants.Common.FoamFillLights));
    constants.Common.CoastalArea=surface.coastalArea;constants.Common.CoastalParams=surface.coastalParameters;
    std::memcpy(constants.Common.ShadowFromClip,scene.shadowFromClip,sizeof(constants.Common.ShadowFromClip));
    constants.Common.ShadowParameters=scene.shadowParameters;constants.Common.ShadowViewport=scene.shadowViewport;
    constants.Common.ShadowSoftness=scene.shadowSoftness;constants.Common.VolumeParameters=scene.volumeParameters;
    if(!surface.river) {
        if(surface.meanHulls.count)std::memcpy(constants.Common.HullData,surface.meanHulls.data,surface.meanHulls.count*sizeof(Float4));
        if(surface.contactHulls.count)std::memcpy(constants.Common.ContactHullData,surface.contactHulls.data,surface.contactHulls.count*sizeof(Float4));
        if(surface.meshContactAvailable&&surface.visualHullMetadata.count)
            std::memcpy(constants.VisualHullMeta,surface.visualHullMetadata.data,surface.visualHullMetadata.count*sizeof(Float4));
        if(surface.meshContactAvailable&&surface.visualHullPackets.count)
            std::memcpy(packets.registers,surface.visualHullPackets.data,surface.visualHullPackets.count*sizeof(Float4));
    } else constants.Common.HullParameters.x=0;
    return true;
}

inline bool Pack(const Inputs &inputs,DrawPacket &packet,ValidationError *error=nullptr)
{
    DrawPacket next={};
    if(!PackConstants(inputs.scene,inputs.surface,next.constants,next.hullPackets,error))return false;
    const SurfaceInput &surface=inputs.surface;
    const SceneInput &scene=inputs.scene;
    if(scene.snapshotAvailable&&(!ValidateResource(scene.sceneColour,ResourceType::Texture2D,error)
        ||!ValidateResource(scene.sceneDepth,ResourceType::Texture2D,error)))return false;
    if(scene.snapshotAvailable&&(scene.sceneColour.width!=scene.sceneDepth.width
        ||scene.sceneColour.height!=scene.sceneDepth.height))return Fail(ValidationError::TargetMismatch,error);
    if(surface.authoredMaterialAvailable&&!ValidateResource(surface.authoredMaterial,ResourceType::Texture2D,error))return false;
    if(surface.reflectionAvailable&&!ValidateResource(surface.reflection,ResourceType::Texture2D,error))return false;
    if(surface.heightWaveCageAvailable&&!ValidateResource(surface.heightWaveCage,ResourceType::Texture2D,error))return false;
    if(surface.terrainAvailable&&!ValidateResource(surface.terrainHeight,ResourceType::Texture2D,error))return false;
    if(surface.opticalAvailable&&!ValidateResource(surface.optical,ResourceType::Texture2D,error))return false;
    if(surface.bedAvailable&&!ValidateResource(surface.bed,ResourceType::Texture2D,error))return false;
    if(surface.fftAvailable) {
        for(unsigned i=0;i<3;++i)if(!ValidateResource(surface.fftDisplacement[i],ResourceType::Texture2D,error))return false;
        if(!ValidateResource(surface.fftArray,ResourceType::Texture2DArray,error))return false;
    }
    if(surface.foamHistoryAvailable&&!ValidateResource(surface.foamHistory,ResourceType::Texture2D,error))return false;
    if(surface.foamHistoryAvailable&&(surface.foamHistoryArea.z<=0||surface.foamHistoryArea.w<=0
        ||surface.foamHistoryParameters.x<=0||surface.foamHistoryParameters.x>7
        ||std::floor(surface.foamHistoryParameters.x)!=surface.foamHistoryParameters.x
        ||(static_cast<unsigned>(surface.foamHistoryParameters.x)&3u)==0u
        ||surface.foamHistoryParameters.z<=0))return Fail(ValidationError::InvalidHistory,error);
    if(surface.coastalAvailable&&!ValidateResource(surface.coastal,ResourceType::Texture2D,error))return false;
    next.resources.snapshotAvailable=scene.snapshotAvailable;next.resources.sceneColour=scene.sceneColour;next.resources.sceneDepth=scene.sceneDepth;
    next.resources.authoredMaterialAvailable=surface.authoredMaterialAvailable;next.resources.authoredMaterial=surface.authoredMaterial;
    next.resources.reflectionAvailable=surface.reflectionAvailable;next.resources.reflection=surface.reflection;
    next.resources.heightWaveCageAvailable=surface.heightWaveCageAvailable;next.resources.heightWaveCage=surface.heightWaveCage;
    next.resources.terrainAvailable=surface.terrainAvailable;next.resources.terrainHeight=surface.terrainHeight;
    next.resources.opticalAvailable=surface.opticalAvailable;next.resources.optical=surface.optical;
    next.resources.bedAvailable=surface.bedAvailable;next.resources.bed=surface.bed;
    next.resources.fftAvailable=surface.fftAvailable;next.resources.fftArray=surface.fftArray;
    std::memcpy(next.resources.fftDisplacement,surface.fftDisplacement,sizeof(next.resources.fftDisplacement));
    next.resources.foamHistoryAvailable=surface.foamHistoryAvailable;next.resources.foamHistory=surface.foamHistory;
    next.resources.coastalAvailable=surface.coastalAvailable;next.resources.coastal=surface.coastal;
    next.resources.meshContactAvailable=surface.meshContactAvailable;next.resources.bed=surface.bed;
    next.constants.Common.SceneParameters={scene.snapshotAvailable?1.0f:0.0f,
        scene.snapshotAvailable?1.0f/static_cast<float>(scene.sceneColour.width):0.0f,
        scene.snapshotAvailable?1.0f/static_cast<float>(scene.sceneColour.height):0.0f,
        scene.snapshotAvailable?1.0f:0.0f};
    next.constants.Common.FftParameters.x=surface.fftAvailable?1.0f:0.0f;
    if(!surface.foamHistoryAvailable) {
        next.constants.Common.FoamHistoryArea={};
        next.constants.Common.FoamHistoryParameters={};
    }
    if(!surface.coastalAvailable) {
        next.constants.Common.CoastalArea={};
        next.constants.Common.CoastalParams={};
    }
    packet=next;
    if(error)*error=ValidationError::None;
    return true;
}

}
