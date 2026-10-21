#pragma once

#include "Platform/WaterRenderPacket.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>

namespace WaterRenderBridge {

enum class Topology : std::uint8_t { TriangleList,TriangleStrip };
enum class IndexFormat : std::uint8_t { UInt16,UInt32 };
enum class PrepareStatus : std::uint8_t { Ready,Unavailable,InvalidPacket,InvalidGeometry,Capacity };
enum class SubmitStatus : std::uint8_t { Refused,Issued,DeviceLost };
enum class BackendKind : std::uint8_t { Unavailable,D3D9,NativeD3D11,SdlGpu };
enum class Feature : std::uint64_t
{
    CoreRenderer=1ull<<0,Device=1ull<<1,SceneColourFormat=1ull<<2,SceneDepthFormat=1ull<<3,
    CoreAnalyticShader=1ull<<4,WaterLayout=1ull<<5,SceneSnapshots=1ull<<6,Fft=1ull<<7,
    VesselWaves=1ull<<8,WaveCages=1ull<<9,FoamHistory=1ull<<10,ShoreHistory=1ull<<11,
    ShoreFoam=1ull<<12,MeshContact=1ull<<13,Optical=1ull<<14,Bed=1ull<<15,
    CoastalSurf=1ull<<16,CoastalCrest=1ull<<17
};
using FeatureMask=std::uint64_t;
enum class CapabilityReason : std::uint8_t
{
    None,NoDevice,UnsupportedBackend,FeatureLevelUnsupported,CoreShaderUnavailable,LayoutUnavailable,
    ColourFormatUnsupported,DepthFormatUnsupported
};

constexpr FeatureMask Mask(Feature feature) { return static_cast<FeatureMask>(feature); }
constexpr FeatureMask operator|(Feature left,Feature right) { return Mask(left)|Mask(right); }
constexpr FeatureMask operator|(FeatureMask left,Feature right) { return left|Mask(right); }
constexpr bool HasAll(FeatureMask available,FeatureMask required) { return (available&required)==required; }

constexpr FeatureMask CoreRequired=Feature::CoreRenderer|Feature::Device|Feature::SceneColourFormat
    |Feature::SceneDepthFormat|Feature::CoreAnalyticShader|Feature::WaterLayout|Feature::SceneSnapshots;
constexpr FeatureMask ProductionRequired=CoreRequired|Feature::Fft|Feature::VesselWaves|Feature::WaveCages
    |Feature::FoamHistory|Feature::ShoreHistory|Feature::ShoreFoam|Feature::MeshContact|Feature::Optical|Feature::Bed;

struct Capability
{
    BackendKind backend;
    FeatureMask features;
    CapabilityReason reason;
    bool Available() const { return reason==CapabilityReason::None&&HasAll(features,CoreRequired); }
};

constexpr std::size_t MaxPreparedBatches=64;
constexpr std::size_t MaxCoveragePoints=64;

struct CoveragePoint { double x,y; };

struct RetainedAllocation
{
    // offset is the allocation's absolute byte offset in its retained backend buffer; batch offsets are relative to it.
    std::uint64_t identity,generation,offset,size;
};

struct ViewState
{
    float view[16];
    float projection[16];
    WaterRenderPacket::Viewport viewport;
};

struct PreparedBatch
{
    std::uint64_t partId;
    Topology topology;
    IndexFormat indexFormat;
    std::uint32_t vertexLayout,vertexStride,vertexOffset,baseVertex,indexOffset;
    std::uint32_t vertexCount,indexCount,primitiveCount;
    RetainedAllocation vertices,indices;
};

struct PreparedPatch
{
    std::uint64_t frameId,resourceGeneration,captureSerial,patchId;
    PreparedBatch batches[MaxPreparedBatches];
    std::size_t batchCount;
    CoveragePoint coverage[MaxCoveragePoints];
    std::size_t coverageCount;
    double waterLevel;
    std::uintptr_t colourIdentity,depthIdentity,viewIdentity;
    std::uint32_t targetWidth,targetHeight,targetFormat,targetSamples;
    ViewState viewState;
    WaterRenderPacket::DrawPacket packet;
    bool complete;
};

struct BatchRequest
{
    std::uint64_t partId;
    Topology topology;
    IndexFormat indexFormat;
    std::uint32_t vertexLayout,vertexStride,vertexOffset,baseVertex,indexOffset;
    std::uint32_t vertexCount,indexCount,primitiveCount;
    const void *vertices,*indices;
    std::size_t vertexBytes,indexBytes;
};

enum class FftEligibility : std::uint8_t { Ineligible,ProjectedFlat };

struct PrepareRequest
{
    std::uint64_t frameId,resourceGeneration,captureSerial,patchId;
    const BatchRequest *batches;
    std::size_t batchCount,batchCapacity;
    const CoveragePoint *coverage;
    std::size_t coverageCount,coverageCapacity;
    double waterLevel;
    std::uintptr_t colourIdentity,depthIdentity,viewIdentity;
    std::uint32_t targetWidth,targetHeight,targetFormat,targetSamples;
    FftEligibility fftEligibility;
    WaterRenderPacket::Inputs inputs;
};

struct SubmissionReceipt
{
    SubmitStatus status;
    std::uint64_t frameId,resourceGeneration,captureSerial,patchId;
    std::uintptr_t colourIdentity,depthIdentity,viewIdentity;
    std::uint32_t targetWidth,targetHeight,targetFormat,targetSamples;
    ViewState viewState;
    std::uint32_t commandsIssued,batchesPrepared,batchesIssued;
    bool coveragePublished;
};

// Independently queried live state used to prove a retained receipt still belongs to the
// active target and view. Backends must source this from their current state, not a receipt.
struct CurrentView
{
    std::uint64_t frameId,resourceGeneration,captureSerial;
    std::uintptr_t colourIdentity,depthIdentity,viewIdentity;
    std::uint32_t targetWidth,targetHeight,targetFormat,targetSamples;
    ViewState viewState;
};

inline bool ValidViewState(const ViewState &state)
{
    return WaterRenderPacket::Finite(state.view,16)&&WaterRenderPacket::Finite(state.projection,16)
        &&WaterRenderPacket::Finite(&state.viewport.x,6)&&state.viewport.width>0&&state.viewport.height>0
        &&state.viewport.maxDepth-state.viewport.minDepth>1.0e-8f;
}

inline bool ValidRetainedRange(const RetainedAllocation &allocation,std::uint64_t generation,
    std::uint64_t relativeOffset,std::uint64_t count,std::uint64_t stride)
{
    if(!allocation.identity||allocation.generation!=generation||!allocation.size||!count||!stride)return false;
    if(count>(std::numeric_limits<std::uint64_t>::max)()/stride)return false;
    const std::uint64_t bytes=count*stride;
    if(relativeOffset>allocation.size||bytes>allocation.size-relativeOffset)return false;
    if(allocation.offset>(std::numeric_limits<std::uint64_t>::max)()-relativeOffset)return false;
    const std::uint64_t absoluteOffset=allocation.offset+relativeOffset;
    return absoluteOffset<=(std::numeric_limits<std::uint64_t>::max)()-bytes;
}

inline PrepareStatus ValidatePreparedPatch(const PreparedPatch &patch)
{
    if(!patch.complete||!patch.frameId||!patch.resourceGeneration||!patch.captureSerial||!patch.patchId
        ||!patch.colourIdentity||!patch.depthIdentity||!patch.viewIdentity||!patch.targetWidth||!patch.targetHeight
        ||!patch.targetFormat||!patch.targetSamples)return PrepareStatus::Unavailable;
    if(!ValidViewState(patch.viewState))return PrepareStatus::InvalidGeometry;
    if(!patch.batchCount||patch.batchCount>MaxPreparedBatches)return PrepareStatus::Capacity;
    if(patch.coverageCount<3||patch.coverageCount>MaxCoveragePoints)return PrepareStatus::Capacity;
    if(!std::isfinite(patch.waterLevel))return PrepareStatus::InvalidGeometry;
    for(std::size_t i=0;i<patch.coverageCount;++i)
        if(!std::isfinite(patch.coverage[i].x)||!std::isfinite(patch.coverage[i].y))return PrepareStatus::InvalidGeometry;
    for(std::size_t i=0;i<patch.batchCount;++i) {
        const PreparedBatch &batch=patch.batches[i];
        if(!batch.partId||!batch.vertexStride||!batch.vertexCount||!batch.primitiveCount||!batch.vertices.identity)
            return PrepareStatus::InvalidGeometry;
        if(!batch.indexCount||!batch.indices.identity)return PrepareStatus::InvalidGeometry;
        if(!ValidRetainedRange(batch.vertices,patch.resourceGeneration,batch.vertexOffset,batch.vertexCount,batch.vertexStride)
            ||!ValidRetainedRange(batch.indices,patch.resourceGeneration,batch.indexOffset,batch.indexCount,
                batch.indexFormat==IndexFormat::UInt16?2u:4u))return PrepareStatus::InvalidGeometry;
        if(batch.topology==Topology::TriangleList&&batch.indexCount!=batch.primitiveCount*3)return PrepareStatus::InvalidGeometry;
        if(batch.topology==Topology::TriangleStrip&&batch.indexCount!=batch.primitiveCount+2)return PrepareStatus::InvalidGeometry;
    }
    return PrepareStatus::Ready;
}

inline ViewState CaptureViewState(const PrepareRequest &request)
{
    ViewState result={};
    std::memcpy(result.view,request.inputs.scene.view,sizeof(result.view));
    std::memcpy(result.projection,request.inputs.scene.projection,sizeof(result.projection));
    result.viewport=request.inputs.scene.viewport;
    return result;
}

inline bool SameViewState(const ViewState &left,const ViewState &right)
{
    return std::memcmp(&left,&right,sizeof(ViewState))==0;
}

inline bool RetainCoverage(const PrepareRequest &request,PreparedPatch &prepared)
{
    if(request.coverageCount<3||request.coverageCount>request.coverageCapacity
        ||request.coverageCount>MaxCoveragePoints||!request.coverage||!std::isfinite(request.waterLevel))return false;
    for(std::size_t i=0;i<request.coverageCount;++i)
        if(!std::isfinite(request.coverage[i].x)||!std::isfinite(request.coverage[i].y))return false;
    for(std::size_t i=0;i<request.coverageCount;++i)
        prepared.coverage[i]=request.coverage[i];
    prepared.coverageCount=request.coverageCount;
    prepared.waterLevel=request.waterLevel;
    return true;
}

inline bool Truthful(const SubmissionReceipt &receipt,const PreparedPatch &prepared)
{
    return ValidatePreparedPatch(prepared)==PrepareStatus::Ready&&receipt.status==SubmitStatus::Issued
        &&receipt.commandsIssued>0&&receipt.frameId==prepared.frameId
        &&receipt.resourceGeneration==prepared.resourceGeneration&&receipt.captureSerial==prepared.captureSerial
        &&receipt.patchId==prepared.patchId&&receipt.colourIdentity==prepared.colourIdentity
        &&receipt.depthIdentity==prepared.depthIdentity&&receipt.viewIdentity==prepared.viewIdentity
        &&receipt.targetWidth==prepared.targetWidth&&receipt.targetHeight==prepared.targetHeight
        &&receipt.targetFormat==prepared.targetFormat&&receipt.targetSamples==prepared.targetSamples
        &&SameViewState(receipt.viewState,prepared.viewState)
        &&receipt.batchesPrepared==prepared.batchCount&&receipt.batchesIssued==receipt.batchesPrepared
        &&receipt.coveragePublished;
}

inline bool ValidCurrentView(const CurrentView &current)
{
    return current.frameId&&current.resourceGeneration&&current.captureSerial
        &&current.colourIdentity&&current.depthIdentity&&current.viewIdentity
        &&current.targetWidth&&current.targetHeight&&current.targetFormat&&current.targetSamples
        &&ValidViewState(current.viewState);
}

inline bool MatchesCurrentView(const SubmissionReceipt &receipt,const CurrentView &current)
{
    return ValidCurrentView(current)&&receipt.frameId==current.frameId
        &&receipt.resourceGeneration==current.resourceGeneration&&receipt.captureSerial==current.captureSerial
        &&receipt.colourIdentity==current.colourIdentity&&receipt.depthIdentity==current.depthIdentity
        &&receipt.viewIdentity==current.viewIdentity&&receipt.targetWidth==current.targetWidth
        &&receipt.targetHeight==current.targetHeight&&receipt.targetFormat==current.targetFormat
        &&receipt.targetSamples==current.targetSamples&&SameViewState(receipt.viewState,current.viewState);
}

class Bridge
{
public:
    virtual ~Bridge() {}
    virtual Capability GetCapability() const=0;
    virtual bool QueryCurrentView(CurrentView &) const { return false; }
    virtual PrepareStatus PreparePatch(const PrepareRequest &request,PreparedPatch &prepared)=0;
    virtual SubmissionReceipt SubmitPreparedPatch(const PreparedPatch &prepared)=0;
    virtual void End(std::uint64_t frameId)=0;
    virtual void InvalidateResources(std::uint64_t resourceGeneration)=0;
    virtual void InvalidateMap()=0;
};

Bridge *GetActiveBridge();

}
