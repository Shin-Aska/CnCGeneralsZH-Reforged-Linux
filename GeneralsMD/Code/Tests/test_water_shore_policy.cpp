#include "test_harness.h"

#include "Platform/WaterShoreOverlay.h"
#include "Platform/WaterRenderPacket.h"
#include "Platform/WaterRenderAdapters.h"

#include <initializer_list>
#include <limits>

namespace {

using namespace WaterShoreOverlay;

Polygon polygon(std::initializer_list<Point> points,double level=7)
{
    Polygon result;result.count=static_cast<unsigned>(points.size());result.level=level;
    unsigned i=0;for(const Point &point:points) result.points[i++]=point;return result;
}

Stamp stamp()
{
    Stamp result;result.frame=1;result.generation=2;result.capture=3;result.colour=4;result.depth=5;result.view=6;
    result.width=1280;result.height=720;result.format=28;result.samples=1;
    for(unsigned i=0;i<4;++i) {result.viewState.view[i*5]=1;result.viewState.projection[i*5]=1;}
    result.viewState.viewport={0,0,1280,720,0,1};return result;
}

AcceptedRegion accepted(const Polygon &coverage,const Stamp &view=stamp(),std::uint64_t patch=1)
{
    AcceptedRegion result;result.stamp=view;result.patch=patch;result.coverage=coverage;result.ownership=OwnVessel|OwnShore;return result;
}

}

TEST(native_dispatch_policy_is_atomic_per_patch)
{
    DispatchState state;
    CHECK(ShouldAttempt(state));
    Issued(state);
    CHECK(!PreEmissionRefused(state));
    CHECK(ShouldAttempt(state));
    Issued(state);
    CHECK(ShouldAttempt(state));

    DispatchState firstClassic;
    CHECK(!PreEmissionRefused(firstClassic));
    CHECK(!ShouldAttempt(firstClassic));
    CHECK(!BlockedResult(firstClassic));

    DispatchState lost;
    Issued(lost);DeviceLost(lost);
    CHECK(!ShouldAttempt(lost));
    CHECK(BlockedResult(lost));
}

TEST(water_packet_device_matrix_translation_readback)
{
    float engineView[3][4]={{0,-1,0,-34},{1,0,0,-12},{0,0,1,-56}};
    WaterRenderPacket::Inputs inputs={};
    WaterRenderAdapters::DeviceView(engineView,inputs.scene.view);
    const float engineProjection[4][4]={{2,0,0,0},{0,3,0,0},{0,0,4,5},{0,0,6,0}};
    WaterRenderAdapters::DeviceProjection(engineProjection,inputs.scene.projection);
    inputs.scene.viewport={0,0,1280,720,0,1};
    inputs.surface.waterParameters={0,0,0,0};
    inputs.surface.terrainParameters={0,1,1,0};
    WaterShaderLayout::PixelConstants constants={};
    WaterShaderLayout::HullPacketConstants hulls={};
    WaterRenderPacket::ValidationError error=WaterRenderPacket::ValidationError::None;
    CHECK(WaterRenderPacket::PackConstants(inputs.scene,inputs.surface,constants,hulls,&error));
    CHECK_EQ(inputs.scene.view[1],1.0f);CHECK_EQ(inputs.scene.view[4],-1.0f);
    CHECK_EQ(inputs.scene.projection[2*4+3],6.0f);
    CHECK_EQ(inputs.scene.projection[3*4+2],5.0f);
    CHECK_EQ(constants.Common.CameraPosition.x,12.0f);
    CHECK_EQ(constants.Common.CameraPosition.y,-34.0f);
    CHECK_EQ(constants.Common.CameraPosition.z,56.0f);
}

TEST(water_grid_metadata_preserves_world_height_without_high_slot_transport)
{
    const WaterRenderAdapters::GridMetadata metadata=WaterRenderAdapters::ModernGridMetadata(37.25f);
    CHECK_EQ(metadata.u2,0.0f);CHECK_EQ(metadata.v2,37.25f);
}

TEST(water_w3d_main_capture_scope_stamps_every_original_once)
{
    WaterRenderAdapters::CaptureScope scope;
    WaterRenderBridge::PrepareRequest outside={};
    CHECK(!scope.Stamp(outside));CHECK_EQ(outside.captureSerial,0u);

    const std::uint64_t first=scope.Begin();
    CHECK(first!=0);
    WaterRenderBridge::PrepareRequest flatA={},flatB={},flatC={},riverA={},riverB={},grid={};
    WaterRenderBridge::PrepareRequest *originals[]={&flatA,&flatB,&flatC,&riverA,&riverB,&grid};
    for(WaterRenderBridge::PrepareRequest *request:originals) {
        request->frameId=7;
        CHECK(scope.Stamp(*request));CHECK_EQ(request->captureSerial,first);
    }
    scope.End();
    WaterRenderBridge::PrepareRequest ended={};
    CHECK(!scope.Stamp(ended));CHECK_EQ(ended.captureSerial,0u);

    const std::uint64_t second=scope.Begin();
    CHECK(second!=0);CHECK(second!=first);
    WaterRenderBridge::PrepareRequest sameFrame={};sameFrame.frameId=7;
    CHECK(scope.Stamp(sameFrame));CHECK_EQ(sameFrame.captureSerial,second);
    scope.End();

    std::uint64_t wrap=(std::numeric_limits<std::uint64_t>::max)();
    CHECK_EQ(WaterRenderAdapters::NextCaptureSerial(wrap),1u);
    CHECK_EQ(wrap,1u);
}

TEST(shore_policy_accepts_concave_and_adjacent_union_coverage)
{
    const Stamp view=stamp();
    const Polygon concave=polygon({{0,0},{8,0},{8,3},{3,3},{3,8},{0,8}});
    AcceptedRegion regions[]={accepted(concave,view)};
    CHECK(CoveredByUnion(polygon({{1,1},{7,1},{2,2},{2,7},{1,7}}),view,regions,1));
    CHECK(!CoveredByUnion(polygon({{1,1},{7,1},{7,7},{1,7}}),view,regions,1));

    regions[0]=accepted(polygon({{0,0},{5,0},{5,10},{0,10}}),view,10);
    AcceptedRegion adjacent=accepted(polygon({{5,0},{10,0},{10,10},{5,10}}),view,11);
    AcceptedRegion pair[]={regions[0],adjacent};
    const Polygon crossing=polygon({{4,2},{6,2},{6,8},{4,8}});
    CHECK(!CoveredByUnion(crossing,view,pair,1));
    CHECK(CoveredByUnion(crossing,view,pair,2));
}

TEST(shore_policy_rejects_unproven_sub_tolerance_slivers)
{
    const Stamp view=stamp();
    const Polygon thin=polygon({{0,0},{5.0e-10,0},{5.0e-10,100},{0,100}});
    AcceptedRegion disjoint=accepted(polygon({{10,0},{11,0},{11,1},{10,1}}),view);
    CHECK(Valid(thin));
    CHECK(!CoveredByUnion(thin,view,&disjoint,1));
    AcceptedRegion exact=accepted(thin,view);
    CHECK(CoveredByUnion(thin,view,&exact,1));
    AcceptedRegion gap[]={
        accepted(polygon({{0,0},{2,0},{2,1},{0,1}}),view),
        accepted(polygon({{0,1+5.0e-10},{2,1+5.0e-10},{2,2},{0,2}}),view)
    };
    CHECK(!CoveredByUnion(polygon({{.5,.5},{1.5,.5},{1.5,1.5},{.5,1.5}}),view,gap,2));
    const Polygon thinArm=polygon({{0,0},{10001,0},{10001,5.0e-9},{1,5.0e-9},{1,1},{0,1}});
    AcceptedRegion broadOnly=accepted(polygon({{0,0},{1,0},{1,1},{0,1}}),view);
    CHECK(Valid(thinArm));
    CHECK(!CoveredByUnion(thinArm,view,&broadOnly,1));
    CHECK(!Valid(polygon({{0,0},{1.0e200,0},{1.0e200,1.0e200},{0,1.0e200}})));
}

TEST(shore_policy_rejects_disconnected_stale_wrong_level_and_malformed_regions)
{
    const Stamp view=stamp();
    AcceptedRegion regions[]={
        accepted(polygon({{0,0},{4,0},{4,10},{0,10}}),view,1),
        accepted(polygon({{6,0},{10,0},{10,10},{6,10}}),view,2)
    };
    CHECK(!CoveredByUnion(polygon({{3,2},{7,2},{7,8},{3,8}}),view,regions,2));
    Stamp stale=view;stale.capture++;
    regions[1]=accepted(polygon({{4,0},{10,0},{10,10},{4,10}}),stale,2);
    CHECK(!CoveredByUnion(polygon({{3,2},{7,2},{7,8},{3,8}}),view,regions,2));
    regions[1]=accepted(polygon({{4,0},{10,0},{10,10},{4,10}},8),view,2);
    CHECK(!CoveredByUnion(polygon({{3,2},{7,2},{7,8},{3,8}}),view,regions,2));
    regions[1].coverage.level=7;regions[1].coverage.points[2].x=(std::numeric_limits<double>::quiet_NaN)();
    CHECK(!CoveredByUnion(polygon({{3,2},{7,2},{7,8},{3,8}}),view,regions,2));
    const Polygon selfCrossing=polygon({{0,0},{10,10},{0,10},{10,0}});
    CHECK(!Valid(selfCrossing));
    CHECK(!CoveredByUnion(selfCrossing,view,regions,2));
}

TEST(shore_policy_requires_truthful_complete_receipt_before_registration)
{
    using namespace WaterRenderBridge;
    PreparedPatch prepared={};prepared.frameId=1;prepared.resourceGeneration=2;prepared.captureSerial=3;prepared.patchId=4;
    prepared.colourIdentity=5;prepared.depthIdentity=6;prepared.viewIdentity=7;
    prepared.targetWidth=1280;prepared.targetHeight=720;prepared.targetFormat=28;prepared.targetSamples=1;
    for(unsigned i=0;i<4;++i) {prepared.viewState.view[i*5]=1;prepared.viewState.projection[i*5]=1;}
    prepared.viewState.viewport={0,0,1280,720,0,1};prepared.complete=true;prepared.waterLevel=7;
    prepared.coverageCount=4;prepared.coverage[0]={0,0};prepared.coverage[1]={10,0};
    prepared.coverage[2]={10,10};prepared.coverage[3]={0,10};
    PreparedBatch &batch=prepared.batches[0];prepared.batchCount=1;batch.partId=1;batch.topology=Topology::TriangleList;
    batch.indexFormat=IndexFormat::UInt16;batch.vertexStride=36;batch.vertexCount=4;batch.indexCount=6;batch.primitiveCount=2;
    batch.vertices={8,2,0,144};batch.indices={9,2,0,12};
    SubmissionReceipt receipt={};receipt.status=SubmitStatus::Issued;receipt.frameId=1;receipt.resourceGeneration=2;
    receipt.captureSerial=3;receipt.patchId=4;receipt.colourIdentity=5;receipt.depthIdentity=6;receipt.viewIdentity=7;
    receipt.targetWidth=1280;receipt.targetHeight=720;receipt.targetFormat=28;receipt.targetSamples=1;
    receipt.viewState=prepared.viewState;receipt.commandsIssued=1;receipt.batchesPrepared=1;receipt.batchesIssued=1;
    receipt.coveragePublished=true;
    AcceptedRegion region;
    CHECK(Accept(receipt,prepared,OwnVessel|OwnShore,region));
    CHECK_EQ(region.patch,4u);
    receipt.batchesIssued=0;CHECK(!Accept(receipt,prepared,OwnVessel,region));
    receipt.batchesIssued=1;receipt.viewState.viewport.width=640;CHECK(!Accept(receipt,prepared,OwnVessel,region));
    receipt.viewState=prepared.viewState;receipt.status=SubmitStatus::DeviceLost;CHECK(!Accept(receipt,prepared,OwnVessel,region));
}

TEST(replacement_ownership_rejects_river_and_local_degradation)
{
    using namespace WaterRenderBridge;
    PreparedPatch prepared={};auto &resources=prepared.packet.resources;auto &common=prepared.packet.constants.Common;
    resources.snapshotAvailable=resources.fftAvailable=resources.heightWaveCageAvailable=true;
    resources.foamHistoryAvailable=resources.terrainAvailable=resources.opticalAvailable=resources.bedAvailable=true;
    resources.meshContactAvailable=true;common.FoamHistoryParameters.x=3;common.HullParameters.x=1;
    CHECK_EQ(ReplacementOwnership(prepared),unsigned(OwnVessel|OwnShore));
    resources.opticalAvailable=false;CHECK_EQ(ReplacementOwnership(prepared),unsigned(OwnNone));
    resources.opticalAvailable=true;resources.foamHistoryAvailable=false;CHECK_EQ(ReplacementOwnership(prepared),unsigned(OwnNone));
    resources.foamHistoryAvailable=true;common.WaterParameters.y=1;CHECK_EQ(ReplacementOwnership(prepared),unsigned(OwnNone));
    common.WaterParameters.y=0;resources.meshContactAvailable=false;
    CHECK_EQ(ReplacementOwnership(prepared),unsigned(OwnShore));
}

TEST(shore_policy_preserves_existing_posix_quad_and_depth_rules)
{
    Quad patch={{0,0,100,0,100,100,0,100},7},track={{10,20,60,20,60,70,10,70},7};
    CHECK(Contains(patch,track));
    Quad crossing=track;crossing.xy[0]=-1;CHECK(!Contains(patch,crossing));
    Quad other=track;other.level=8;CHECK(!Contains(patch,other));
    CHECK(Replacement(1,.25f,0,1,0)==1);
    CHECK(Replacement(0,.25f,0,1,0)==0);
    CHECK(Replacement(1,.999999f,0,1,0)==0);
}
