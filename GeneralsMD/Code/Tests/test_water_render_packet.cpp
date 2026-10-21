#include "test_harness.h"

#include "Platform/WaterRenderBridge.h"
#include "Platform/WaterRenderPacket.h"
#include "Platform/WaterShaderLayout.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {

using WaterRenderPacket::Float4Span;
using WaterRenderPacket::Resource;
using WaterRenderPacket::ResourceType;
using WaterShaderLayout::Float4;

Resource texture(ResourceType type=ResourceType::Texture2D)
{
    Resource result={};
    result.identity=reinterpret_cast<const void *>(0x1234);
    result.type=type;
    result.available=true;
    result.width=64;
    result.height=32;
    result.layers=type==ResourceType::Texture2DArray?6u:1u;
    result.capacity=64u*32u*result.layers;
    return result;
}

WaterRenderPacket::Inputs valid_inputs(Float4 *mean_hulls,Float4 *contacts,
    Float4 *metadata,Float4 *packets)
{
    WaterRenderPacket::Inputs in={};
    in.scene.view[0]=2;in.scene.view[5]=3;in.scene.view[10]=4;in.scene.view[15]=1;
    in.scene.view[12]=10;in.scene.view[13]=-6;in.scene.view[14]=8;
    in.scene.projection[0]=5;in.scene.projection[5]=7;in.scene.projection[10]=11;in.scene.projection[15]=1;
    in.scene.viewport.x=17;in.scene.viewport.y=23;in.scene.viewport.width=640;in.scene.viewport.height=360;
    in.scene.viewport.minDepth=.2f;in.scene.viewport.maxDepth=.85f;
    in.scene.snapshotAvailable=true;
    in.scene.sceneColour=texture();in.scene.sceneDepth=texture();
    in.surface.waterParameters={12.5f,0,1,7};
    in.surface.reflectionArea={-4,-3,8,6};
    in.surface.terrainParameters={160,5120,159.375f,1};
    in.surface.opticalParameters={2.7f,.75f,1,1};
    in.surface.hullParameters={2,.1f,.2f,.3f};
    in.surface.meanHulls={mean_hulls,20,20};
    in.surface.contactHulls={contacts,20,20};
    in.surface.visualHullMetadata={metadata,2,2};
    in.surface.visualHullPackets={packets,176,176};
    in.surface.meshContactAvailable=true;
    in.surface.authoredMaterialAvailable=true;in.surface.authoredMaterial=texture();
    in.surface.reflectionAvailable=true;in.surface.reflection=texture();
    in.surface.heightWaveCageAvailable=true;in.surface.heightWaveCage=texture();
    in.surface.terrainAvailable=true;in.surface.terrainHeight=texture();
    in.surface.opticalAvailable=true;in.surface.optical=texture();
    in.surface.bedAvailable=true;in.surface.bed=texture();
    in.surface.fftAvailable=true;
    for(unsigned i=0;i<3;++i)in.surface.fftDisplacement[i]=texture();
    in.surface.fftArray=texture(ResourceType::Texture2DArray);
    in.surface.foamHistoryAvailable=true;in.surface.foamHistory=texture();
    in.surface.foamHistoryArea={-128,64,1.0f/1024,1.0f/512};
    in.surface.foamHistoryParameters={1,7,2,12.25f};
    return in;
}

}

TEST(water_shader_layout_is_exact)
{
    using namespace WaterShaderLayout;
    CHECK_EQ(sizeof(CommonConstants),3104u);
    CHECK_EQ(sizeof(PixelConstants),3232u);
    CHECK_EQ(sizeof(HullPacketBlock0),4096u);
    CHECK_EQ(sizeof(HullPacketBlock1),4096u);
    CHECK_EQ(sizeof(HullPacketBlock2),3072u);
    CHECK_EQ(sizeof(ShoreConstants),176u);
    CHECK_EQ(offsetof(CommonConstants,WorldFromClip),3u*16u);
    CHECK_EQ(offsetof(CommonConstants,CameraPosition),7u*16u);
    CHECK_EQ(offsetof(CommonConstants,HullData),17u*16u);
    CHECK_EQ(offsetof(CommonConstants,FftParameters),97u*16u);
    CHECK_EQ(offsetof(CommonConstants,ContactHullData),100u*16u);
    CHECK_EQ(offsetof(CommonConstants,FoamFillLights),180u*16u);
    CHECK_EQ(offsetof(CommonConstants,CoastalArea),184u*16u);
    CHECK_EQ(offsetof(CommonConstants,ShadowFromClip),186u*16u);
    CHECK_EQ(offsetof(CommonConstants,VolumeParameters),193u*16u);
    CHECK_EQ(offsetof(PixelConstants,VisualHullMeta),194u*16u);
}

TEST(water_packet_packs_scene_and_immutable_blocks)
{
    Float4 mean[20],contact[20],metadata[2],packets[176];
    for(unsigned i=0;i<20;++i){mean[i]={float(i),1,2,3};contact[i]={-float(i),4,5,6};}
    for(unsigned i=0;i<2;++i)metadata[i]={float(i+1),2,3,1};
    for(unsigned i=0;i<176;++i)packets[i]={float(i)+.25f,7,8,9};
    const WaterRenderPacket::Inputs in=valid_inputs(mean,contact,metadata,packets);
    const WaterRenderPacket::Inputs before=in;
    WaterRenderPacket::DrawPacket out={};
    WaterRenderPacket::ValidationError error=WaterRenderPacket::ValidationError::None;
    CHECK(WaterRenderPacket::Pack(in,out,&error));
    CHECK_EQ(error,WaterRenderPacket::ValidationError::None);
    CHECK_MEM(&in,&before,sizeof(in));
    CHECK_MEM(out.constants.Common.HullData,mean,sizeof(mean));
    CHECK_MEM(out.constants.Common.ContactHullData,contact,sizeof(contact));
    CHECK_MEM(out.constants.VisualHullMeta,metadata,sizeof(metadata));
    CHECK_MEM(out.hullPackets.registers,packets,sizeof(packets));
    CHECK_NEAR(out.constants.Common.Viewport[0],17.5f,0.00001f);
    CHECK_NEAR(out.constants.Common.Viewport[1],23.5f,0.00001f);
    CHECK_NEAR(out.constants.Common.Viewport[2],640,0.00001f);
    CHECK_NEAR(out.constants.Common.Viewport[3],360,0.00001f);
    CHECK_NEAR(out.constants.Common.CameraPosition[0],-5,0.00001f);
    CHECK_NEAR(out.constants.Common.CameraPosition[1],2,0.00001f);
    CHECK_NEAR(out.constants.Common.CameraPosition[2],-2,0.00001f);
    CHECK_NEAR(out.constants.Common.WorldFromClip[0][0],.1f,0.00001f);
    CHECK_NEAR(out.constants.Common.WorldFromClip[1][1],1.0f/21.0f,0.00001f);
    CHECK_NEAR(out.constants.Common.WorldFromClip[2][2],1.0f/(44.0f*.65f),0.00001f);
    CHECK(out.resources.fftAvailable && out.resources.snapshotAvailable && out.resources.foamHistoryAvailable);
}

TEST(water_packet_rejects_invalid_inputs_and_gates_history)
{
    Float4 mean[80]={},contact[80]={},metadata[8]={},packets[704]={};
    WaterRenderPacket::Inputs in=valid_inputs(mean,contact,metadata,packets);
    WaterRenderPacket::DrawPacket out={};
    WaterRenderPacket::DrawPacket unchanged;
    memset(&unchanged,0x6c,sizeof(unchanged));out=unchanged;
    WaterRenderPacket::ValidationError error=WaterRenderPacket::ValidationError::None;
    in.surface.meanHulls={mean,81,80};
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::Capacity);
    CHECK_MEM(&out,&unchanged,sizeof(out));
    in=valid_inputs(mean,contact,metadata,packets);in.surface.waterParameters.x=(std::numeric_limits<float>::infinity)();
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::NonFinite);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.fftArray.type=ResourceType::Texture2D;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::ResourceType);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.terrainHeight.available=false;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::Unavailable);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.bed.type=ResourceType::Texture2DArray;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::ResourceType);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.heightWaveCage.available=false;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::Unavailable);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.bed.capacity=1;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::ResourceExtent);
    in=valid_inputs(mean,contact,metadata,packets);in.scene.sceneDepth.width=32;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::TargetMismatch);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.foamHistoryParameters.x=4;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::InvalidHistory);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.visualHullPackets.capacity=175;
    CHECK(!WaterRenderPacket::Pack(in,out,&error));CHECK_EQ(error,WaterRenderPacket::ValidationError::Capacity);
    in=valid_inputs(mean,contact,metadata,packets);in.surface.foamHistoryAvailable=false;
    CHECK(WaterRenderPacket::Pack(in,out,&error));
    CHECK(!out.resources.foamHistoryAvailable);
    const Float4 zero={};CHECK_MEM(&out.constants.Common.FoamHistoryArea,&zero,sizeof(zero));
    CHECK_MEM(&out.constants.Common.FoamHistoryParameters,&zero,sizeof(zero));
    in=valid_inputs(mean,contact,metadata,packets);in.surface.meshContactAvailable=false;
    CHECK(WaterRenderPacket::Pack(in,out,&error));
    WaterShaderLayout::Float4 zeroMetadata[8]={},zeroPackets[704]={};
    CHECK_MEM(out.constants.VisualHullMeta,zeroMetadata,sizeof(zeroMetadata));
    CHECK_MEM(out.hullPackets.registers,zeroPackets,sizeof(zeroPackets));
}

TEST(water_packet_clears_river_hulls)
{
    Float4 mean[20],contact[20],metadata[2],packets[176];
    memset(mean,0x31,sizeof(mean));memset(contact,0x32,sizeof(contact));
    memset(metadata,0x33,sizeof(metadata));memset(packets,0x34,sizeof(packets));
    WaterRenderPacket::Inputs in=valid_inputs(mean,contact,metadata,packets);
    in.surface.river=true;
    WaterRenderPacket::DrawPacket out={};
    CHECK(WaterRenderPacket::Pack(in,out,nullptr));
    CHECK_EQ(out.constants.Common.HullParameters[0],0.0f);
    WaterShaderLayout::Float4 zeroMean[80]={},zeroContact[80]={},zeroMetadata[8]={},zeroPackets[704]={};
    CHECK_MEM(out.constants.Common.HullData,zeroMean,sizeof(zeroMean));
    CHECK_MEM(out.constants.Common.ContactHullData,zeroContact,sizeof(zeroContact));
    CHECK_MEM(out.constants.VisualHullMeta,zeroMetadata,sizeof(zeroMetadata));
    CHECK_MEM(out.hullPackets.registers,zeroPackets,sizeof(zeroPackets));
}

TEST(water_bridge_contract_retains_complete_geometry)
{
    using namespace WaterRenderBridge;
    CHECK_EQ(static_cast<unsigned>(Topology::TriangleList),0u);
    CHECK_EQ(static_cast<unsigned>(IndexFormat::UInt16),0u);
    PreparedBatch batch={};
    batch.partId=3;batch.topology=Topology::TriangleList;
    batch.indexFormat=IndexFormat::UInt16;batch.vertexStride=36;
    batch.vertexOffset=48;batch.baseVertex=2;batch.indexOffset=12;
    batch.vertexCount=64;batch.indexCount=96;batch.primitiveCount=32;
    batch.vertices={11,9,128,4096};batch.indices={12,9,8192,512};
    PreparedPatch patch={};
    CoveragePoint coverage[4]={{0,0},{1,0},{1,1},{0,1}};
    patch.frameId=7;patch.resourceGeneration=9;patch.captureSerial=5;patch.patchId=41;
    patch.colourIdentity=21;patch.depthIdentity=22;patch.viewIdentity=23;
    patch.targetWidth=640;patch.targetHeight=360;patch.targetFormat=28;patch.targetSamples=1;
    patch.batches[0]=batch;patch.batchCount=1;patch.complete=true;
    PrepareRequest request={};request.coverage=coverage;request.coverageCount=4;request.coverageCapacity=4;request.waterLevel=7;
    for(unsigned i=0;i<4;++i) {
        request.inputs.scene.view[i*5]=1;
        request.inputs.scene.projection[i*5]=1;
    }
    request.inputs.scene.view[12]=10;
    request.inputs.scene.viewport={3,5,640,360,.2f,.85f};
    patch.viewState=CaptureViewState(request);
    CHECK(RetainCoverage(request,patch));
    coverage[0].x=99;batch.indexCount=3;
    CHECK_EQ(patch.coverage[0].x,0.0);
    CHECK_EQ(patch.batches[0].indexCount,96u);
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::Ready);
    const ViewState validView=patch.viewState;
    patch.viewState.view[3]=(std::numeric_limits<float>::quiet_NaN)();
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.viewState=validView;patch.viewState.viewport.width=0;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.viewState=validView;patch.viewState.viewport.maxDepth=patch.viewState.viewport.minDepth;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.viewState=validView;
    patch.batches[0].vertices.generation=8;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.batches[0].vertices.generation=9;patch.batches[0].vertices.size=0;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.batches[0].vertices.size=2000;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.batches[0].vertices.size=4096;
    patch.batches[0].vertices.offset=(std::numeric_limits<std::uint64_t>::max)()-16;
    CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.batches[0].vertices.offset=128;
    const PreparedPatch retained=patch;
    coverage[3].x=(std::numeric_limits<double>::quiet_NaN)();
    CHECK(!RetainCoverage(request,patch));
    CHECK_MEM(patch.coverage,retained.coverage,sizeof(patch.coverage));
    CHECK_EQ(patch.coverageCount,retained.coverageCount);
    CHECK_EQ(patch.waterLevel,retained.waterLevel);
    coverage[3].x=0;
    request.coverageCount=MaxCoveragePoints+1;request.coverageCapacity=request.coverageCount;
    CHECK(!RetainCoverage(request,patch));
    patch.batches[0].indexCount=95;CHECK(ValidatePreparedPatch(patch)==PrepareStatus::InvalidGeometry);
    patch.batches[0].indexCount=96;
    SubmissionReceipt receipt={};receipt.status=SubmitStatus::Issued;
    receipt.frameId=7;receipt.resourceGeneration=9;receipt.captureSerial=5;receipt.patchId=41;
    receipt.colourIdentity=21;receipt.depthIdentity=22;receipt.viewIdentity=23;
    receipt.targetWidth=640;receipt.targetHeight=360;receipt.targetFormat=28;receipt.targetSamples=1;
    receipt.viewState=patch.viewState;
    receipt.commandsIssued=2;receipt.batchesPrepared=1;receipt.batchesIssued=1;receipt.coveragePublished=true;
    CHECK(Truthful(receipt,patch));
    receipt.viewState.view[12]=11;CHECK(!Truthful(receipt,patch));
    receipt.viewState=patch.viewState;receipt.viewState.viewport.minDepth=.25f;CHECK(!Truthful(receipt,patch));
    receipt.viewState=patch.viewState;receipt.commandsIssued=0;CHECK(!Truthful(receipt,patch));
}

TEST(water_bridge_current_view_requires_exact_independent_state)
{
    using namespace WaterRenderBridge;
    SubmissionReceipt receipt={};
    receipt.status=SubmitStatus::Issued;
    receipt.frameId=17;receipt.resourceGeneration=19;receipt.captureSerial=23;receipt.patchId=29;
    receipt.colourIdentity=31;receipt.depthIdentity=37;receipt.viewIdentity=41;
    receipt.targetWidth=1280;receipt.targetHeight=720;receipt.targetFormat=28;receipt.targetSamples=1;
    for(unsigned i=0;i<4;++i) {
        receipt.viewState.view[i*5]=1;
        receipt.viewState.projection[i*5]=1;
    }
    receipt.viewState.view[12]=12;receipt.viewState.view[13]=-34;receipt.viewState.view[14]=56;
    receipt.viewState.viewport={7,11,1280,720,.125f,.875f};

    CurrentView current={};
    current.frameId=17;current.resourceGeneration=19;current.captureSerial=23;
    current.colourIdentity=31;current.depthIdentity=37;current.viewIdentity=41;
    current.targetWidth=1280;current.targetHeight=720;current.targetFormat=28;current.targetSamples=1;
    for(unsigned i=0;i<4;++i) {
        current.viewState.view[i*5]=1;
        current.viewState.projection[i*5]=1;
    }
    current.viewState.view[12]=12;current.viewState.view[13]=-34;current.viewState.view[14]=56;
    current.viewState.viewport={7,11,1280,720,.125f,.875f};
    CHECK(ValidCurrentView(current));
    CHECK(MatchesCurrentView(receipt,current));
    const CurrentView exact=current;

#define CHECK_CURRENT_MUTATION(statement) do { current=exact;statement;CHECK(!MatchesCurrentView(receipt,current)); } while(0)
    CHECK_CURRENT_MUTATION(++current.frameId);
    CHECK_CURRENT_MUTATION(++current.resourceGeneration);
    CHECK_CURRENT_MUTATION(++current.captureSerial);
    CHECK_CURRENT_MUTATION(++current.colourIdentity);
    CHECK_CURRENT_MUTATION(++current.depthIdentity);
    CHECK_CURRENT_MUTATION(++current.viewIdentity);
    CHECK_CURRENT_MUTATION(++current.targetWidth);
    CHECK_CURRENT_MUTATION(++current.targetHeight);
    CHECK_CURRENT_MUTATION(++current.targetFormat);
    CHECK_CURRENT_MUTATION(++current.targetSamples);
    CHECK_CURRENT_MUTATION(current.viewState.view[12]+=1);
    CHECK_CURRENT_MUTATION(current.viewState.projection[5]+=1);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.x+=1);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.y+=1);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.width-=1);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.height-=1);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.minDepth+=.01f);
    CHECK_CURRENT_MUTATION(current.viewState.viewport.maxDepth-=.01f);
#undef CHECK_CURRENT_MUTATION

    current=exact;current.viewState.view[0]=(std::numeric_limits<float>::quiet_NaN)();
    CHECK(!ValidCurrentView(current));
    current=exact;current.viewState.viewport.width=0;CHECK(!ValidCurrentView(current));
    current=exact;current.viewState.viewport.maxDepth=current.viewState.viewport.minDepth;
    CHECK(!ValidCurrentView(current));
    current=exact;current.colourIdentity=0;CHECK(!ValidCurrentView(current));
    current=exact;current.depthIdentity=0;CHECK(!ValidCurrentView(current));
    current=exact;current.viewIdentity=0;CHECK(!ValidCurrentView(current));
    current=exact;current.frameId=0;CHECK(!ValidCurrentView(current));
    current=exact;current.resourceGeneration=0;CHECK(!ValidCurrentView(current));
    current=exact;current.captureSerial=0;CHECK(!ValidCurrentView(current));
    current=exact;current.targetFormat=0;CHECK(!ValidCurrentView(current));
    current=exact;current.targetSamples=0;CHECK(!ValidCurrentView(current));
}

TEST(water_bridge_capability_is_backend_neutral)
{
    using namespace WaterRenderBridge;
    Capability capability={BackendKind::NativeD3D11,CoreRequired,CapabilityReason::None};
    CHECK(capability.Available());
    CHECK(!HasAll(capability.features,ProductionRequired));
    capability.features=ProductionRequired;
    CHECK(capability.Available());
    CHECK(HasAll(capability.features,ProductionRequired));
    capability.features&=~Mask(Feature::WaterLayout);
    CHECK(!capability.Available());
    capability.features=ProductionRequired;
    capability.reason=CapabilityReason::DepthFormatUnsupported;
    CHECK(!capability.Available());
    capability.backend=BackendKind::D3D9;
    capability.reason=CapabilityReason::UnsupportedBackend;
    CHECK(!capability.Available());

    class TestBridge : public Bridge
    {
    public:
        Capability value;
        Capability GetCapability() const override { return value; }
        PrepareStatus PreparePatch(const PrepareRequest &,PreparedPatch &) override { return PrepareStatus::Unavailable; }
        SubmissionReceipt SubmitPreparedPatch(const PreparedPatch &) override { return {}; }
        void End(std::uint64_t) override {}
        void InvalidateResources(std::uint64_t) override {}
        void InvalidateMap() override {}
    } bridge;
    bridge.value={BackendKind::SdlGpu,ProductionRequired|Feature::CoastalSurf,CapabilityReason::None};
    const Capability queried=bridge.GetCapability();
    CHECK(queried.Available());
    CHECK_EQ(queried.backend,BackendKind::SdlGpu);
    CHECK(HasAll(queried.features,ProductionRequired));
    CHECK(HasAll(queried.features,Mask(Feature::CoastalSurf)));
    CHECK(!HasAll(queried.features,Mask(Feature::CoastalCrest)));
    CurrentView unavailable={};unavailable.frameId=99;
    CHECK(!bridge.QueryCurrentView(unavailable));
    CHECK_EQ(unavailable.frameId,99u);
}
