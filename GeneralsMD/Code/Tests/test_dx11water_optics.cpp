#include "WaterDx11SurfaceFixture.h"

namespace {

using namespace WaterDx11SurfaceTest;

float Difference(const Pixel &a, const Pixel &b)
{
	return std::fabs(a.r-b.r) + std::fabs(a.g-b.g) + std::fabs(a.b-b.b);
}

bool NewScene(Fixture &fixture, float depth = 0.7f, bool occluder = false)
{
	const bool ready = fixture.Scene({0.32f,0.42f,0.52f,1},depth,occluder);
	CHECK(ready); return ready;
}

void ReflectionAndBed(Fixture &fixture)
{
	if (!NewScene(fixture)) return;
	const Pixel black = fixture.Draw("mirror-black-alpha-zero");
	CHECK(fixture.mirror.Set(fixture.device.Get_Device(),{0,0,0,1}));
	fixture.BindResources();
	const Pixel blackOpaque = fixture.Draw("mirror-black-alpha-one");
	CHECK_NEAR(Difference(black,blackOpaque),0,0.00001f);
	if (!NewScene(fixture)) return;
	CHECK(fixture.mirror.Set(fixture.device.Get_Device(),{1,0.02f,0.02f,0})); fixture.BindResources();
	const Pixel red = fixture.Draw("mirror-red");
	if (!NewScene(fixture)) return;
	CHECK(fixture.mirror.Set(fixture.device.Get_Device(),{0.02f,1,0.02f,0})); fixture.BindResources();
	const Pixel green = fixture.Draw("mirror-green");
	CHECK(red.r > green.r + 0.02f); CHECK(green.g > red.g + 0.02f);
	CHECK_NEAR(red.b,green.b,0.00001f);
	if (!NewScene(fixture)) return;
	CHECK(fixture.bed.Set(fixture.device.Get_Device(),{0.1f,0.1f,0.5f,0.5f})); fixture.BindResources();
	const Pixel darkBed = fixture.Draw("dark-bed");
	if (!NewScene(fixture)) return;
	CHECK(fixture.bed.Set(fixture.device.Get_Device(),{0.9f,0.9f,0.5f,0.5f})); fixture.BindResources();
	const Pixel brightBed = fixture.Draw("bright-bed");
	CHECK(brightBed.r > darkBed.r + 0.05f);
	CHECK(brightBed.g > darkBed.g + 0.05f);
	CHECK(brightBed.b > darkBed.b + 0.05f);
}

void DepthAndOccluder(Fixture &fixture)
{
	if (!NewScene(fixture,0.55f)) return;
	const Pixel shallow = fixture.Draw("depth-shallow");
	if (!NewScene(fixture,0.85f)) return;
	const Pixel deep = fixture.Draw("depth-deep");
	CHECK(Difference(shallow,deep) > 0.01f);
	if (!NewScene(fixture)) return;
	fixture.Draw("depth-step-reference");
	std::vector<Pixel> reference; CHECK(fixture.Read(reference));
	if (!NewScene(fixture,0.7f,true)) return;
	fixture.Draw("depth-step-foreground");
	std::vector<Pixel> occluded; CHECK(fixture.Read(occluded));
	if (reference.empty() || occluded.empty()) return;
	const unsigned row = Fixture::Height/2;
	const Pixel foreground = occluded[row*Fixture::Width + Fixture::Width/2-1];
	CHECK_NEAR(foreground.r,1,0.00001f); CHECK_NEAR(foreground.g,0,0.00001f);
	CHECK_NEAR(foreground.b,1,0.00001f);
	const Pixel adjacent = occluded[row*Fixture::Width + Fixture::Width/2];
	const Pixel originalAdjacent = reference[row*Fixture::Width + Fixture::Width/2];
	const bool greenReady = fixture.Scene({0.32f,0.42f,0.52f,1},0.7f,true,{0,1,0,1});
	CHECK(greenReady); if (!greenReady) return;
	fixture.Draw("depth-step-foreground-green");
	std::vector<Pixel> greenOccluder; CHECK(fixture.Read(greenOccluder));
	if (greenOccluder.empty()) return;
	const Pixel greenAdjacent = greenOccluder[row*Fixture::Width + Fixture::Width/2];
	CHECK_NEAR(adjacent.r,greenAdjacent.r,0.00001f);
	CHECK_NEAR(adjacent.g,greenAdjacent.g,0.00001f);
	CHECK_NEAR(adjacent.b,greenAdjacent.b,0.00001f);
	std::printf("  depth-step foreground=%.6f,%.6f,%.6f adjacent-difference=%.7f\n",
		foreground.r,foreground.g,foreground.b,Difference(adjacent,originalAdjacent));
}

void EdgeShroudAndFog(Fixture &fixture)
{
	if (!NewScene(fixture)) return;
	CHECK(fixture.edge.Set(fixture.device.Get_Device(),{0,0,0,1})); fixture.BindResources();
	const Pixel shrouded = fixture.Draw("flat-black-shroud");
	CHECK_NEAR(shrouded.r,0,0.00001f); CHECK_NEAR(shrouded.g,0,0.00001f);
	CHECK_NEAR(shrouded.b,0,0.00001f);
	if (!NewScene(fixture)) return;
	fixture.request.inputs.surface.river = true; fixture.request.inputs.surface.waterParameters.y = 1;
	CHECK(fixture.edge.Set(fixture.device.Get_Device(),{1,1,1,0})); fixture.BindResources();
	const Pixel transparent = fixture.Draw("river-edge-zero");
	CHECK_NEAR(transparent.r,0.32f,0.00001f); CHECK_NEAR(transparent.g,0.42f,0.00001f);
	CHECK_NEAR(transparent.b,0.52f,0.00001f);
	if (!NewScene(fixture)) return;
	fixture.request.inputs.surface.river = true; fixture.request.inputs.surface.waterParameters.y = 1;
	const Pixel opaque = fixture.Draw("river-edge-one");
	if (!NewScene(fixture)) return;
	fixture.request.inputs.surface.river = true; fixture.request.inputs.surface.waterParameters.y = 1;
	CHECK(fixture.edge.Set(fixture.device.Get_Device(),{1,1,1,0.25f})); fixture.BindResources();
	const Pixel quarter = fixture.Draw("river-edge-quarter");
	CHECK_NEAR(quarter.r,0.25f*opaque.r+0.75f*transparent.r,0.00002f);
	CHECK_NEAR(quarter.g,0.25f*opaque.g+0.75f*transparent.g,0.00002f);
	CHECK_NEAR(quarter.b,0.25f*opaque.b+0.75f*transparent.b,0.00002f);
	CHECK(Difference(quarter,opaque) > 0.01f);
	if (!NewScene(fixture)) return;
	fixture.backend.Set_Render_State(D3DRS_FOGENABLE,TRUE);
	fixture.backend.Set_Render_State(D3DRS_FOGVERTEXMODE,D3DFOG_LINEAR);
	const float start = -4, end = -3; DWORD startBits = 0, endBits = 0;
	std::memcpy(&startBits,&start,sizeof(start)); std::memcpy(&endBits,&end,sizeof(end));
	fixture.backend.Set_Render_State(D3DRS_FOGSTART,startBits);
	fixture.backend.Set_Render_State(D3DRS_FOGEND,endBits);
	fixture.backend.Set_Render_State(D3DRS_FOGCOLOR,0xff336699);
	fixture.request.inputs.scene.combiner[1] = {0,0,0,1};
	const Pixel fog = fixture.Draw("flat-full-fog");
	CHECK_NEAR(fog.r,0.2f,0.00001f); CHECK_NEAR(fog.g,0.4f,0.00001f);
	CHECK_NEAR(fog.b,0.6f,0.00001f);
}

void ExpectPrepareRefusal(Fixture &fixture, const char *label)
{
	std::printf("  optics phase=refusal label=%s frame=%u tick=%llu\n",label,fixture.frame,
		static_cast<unsigned long long>(GetTickCount64()));
	std::fflush(stdout);
	std::vector<Pixel> before, after; CHECK(fixture.Read(before));
	const unsigned captures = fixture.water->Snapshot_Count();
	DrawCounter counter(fixture);
	WaterRenderBridge::PreparedPatch prepared = {};
	const WaterRenderBridge::PrepareStatus status = fixture.water->PreparePatch(fixture.request,prepared);
	CHECK(status != WaterRenderBridge::PrepareStatus::Ready); CHECK(!prepared.complete);
	const WaterRenderBridge::SubmissionReceipt receipt = fixture.water->SubmitPreparedPatch(prepared);
	CHECK(receipt.status == WaterRenderBridge::SubmitStatus::Refused);
	CHECK_EQ(receipt.commandsIssued,0u); CHECK(!receipt.coveragePublished);
	CHECK_EQ(counter.End(),0ull); CHECK_EQ(fixture.water->Snapshot_Count(),captures);
	CHECK(fixture.Read(after)); CHECK_EQ(before.size(),after.size());
	if (!before.empty() && before.size() == after.size()) CHECK_MEM(&before[0],&after[0],before.size()*sizeof(Pixel));
	std::printf("  refusal %s prepare=%u commands=%u captures=%u\n",label,
		unsigned(status),receipt.commandsIssued,fixture.water->Snapshot_Count());
}

void RefusalsAndLifetime(Fixture &fixture)
{
	Texture *required[] = {&fixture.base,&fixture.mirror,&fixture.optical,&fixture.bed};
	const char *labels[] = {"missing-authored","missing-reflection","missing-flagged-optical","missing-flagged-bed"};
	for (unsigned i = 0; i < 4; ++i) {
		if (!NewScene(fixture)) return;
		fixture.water->Set_Test_Resource(required[i],NULL);
		ExpectPrepareRefusal(fixture,labels[i]);
	}
	const DX11WaterClass::TestFailure failures[] = {DX11WaterClass::TEST_FAILURE_SHADER,
		DX11WaterClass::TEST_FAILURE_BUFFER,DX11WaterClass::TEST_FAILURE_LATER_BATCH,
		DX11WaterClass::TEST_FAILURE_COLOUR_ALLOCATION,DX11WaterClass::TEST_FAILURE_DEPTH_ALLOCATION};
	const char *failureLabels[] = {"shader-fault","buffer-fault","later-batch-fault","scene-colour-fault","scene-depth-fault"};
	for (unsigned i = 0; i < 5; ++i) {
		if (!NewScene(fixture)) return;
		fixture.batches[1] = fixture.batches[0]; fixture.batches[1].partId = 2;
		fixture.request.batchCount = fixture.request.batchCapacity = 2;
		fixture.water->Set_Test_Failure(failures[i]); ExpectPrepareRefusal(fixture,failureLabels[i]);
	}
	if (!NewScene(fixture)) return;
	fixture.batches[1] = fixture.batches[0]; fixture.batches[1].partId = 2;
	fixture.batches[1].vertexBytes -= 1;
	fixture.request.batchCount = fixture.request.batchCapacity = 2;
	ExpectPrepareRefusal(fixture,"invalid-second-batch-range");
	if (!NewScene(fixture)) return;
	fixture.request.inputs.surface.opticalAvailable = false; fixture.request.inputs.surface.optical = {};
	fixture.request.inputs.surface.bedAvailable = false; fixture.request.inputs.surface.bed = {};
	fixture.water->Set_Test_Resource(&fixture.optical,NULL); fixture.water->Set_Test_Resource(&fixture.bed,NULL);
	fixture.Draw("optional-optics-explicitly-unavailable");
	if (!NewScene(fixture)) return;
	WaterRenderBridge::PreparedPatch prepared = {};
	CHECK(fixture.water->PreparePatch(fixture.request,prepared) == WaterRenderBridge::PrepareStatus::Ready);
	if (!prepared.complete) return;
	WaterRenderBridge::PreparedPatch mutated = prepared; mutated.packet.constants.Common.WaterParameters.x += 1;
	DrawCounter mutationCounter(fixture);
	const WaterRenderBridge::SubmissionReceipt rejected = fixture.water->SubmitPreparedPatch(mutated);
	CHECK(rejected.status == WaterRenderBridge::SubmitStatus::Refused); CHECK_EQ(rejected.commandsIssued,0u);
	CHECK_EQ(mutationCounter.End(),0ull);
	fixture.water->Advance_Presented_Frame();
	DrawCounter staleCounter(fixture);
	const WaterRenderBridge::SubmissionReceipt stale = fixture.water->SubmitPreparedPatch(prepared);
	CHECK(stale.status == WaterRenderBridge::SubmitStatus::Refused); CHECK_EQ(stale.commandsIssued,0u);
	CHECK_EQ(staleCounter.End(),0ull);
	if (!NewScene(fixture)) return;
	CHECK(fixture.water->PreparePatch(fixture.request,prepared) == WaterRenderBridge::PrepareStatus::Ready);
	if (!prepared.complete) return;
	std::memset(fixture.vertices,0,sizeof(fixture.vertices));
	std::memset(fixture.indices,0,sizeof(fixture.indices));
	fixture.water->Clear_Test_Resources();
	DrawCounter retainedCounter(fixture);
	const WaterRenderBridge::SubmissionReceipt retained = fixture.water->SubmitPreparedPatch(prepared);
	CHECK(WaterRenderBridge::Truthful(retained,prepared)); CHECK_EQ(retainedCounter.End(),2ull);
	std::vector<Pixel> retainedPixels; CHECK(fixture.Read(retainedPixels));
	if (!retainedPixels.empty()) CHECK(Difference(retainedPixels[(Fixture::Height/2)*Fixture::Width
		+Fixture::Width/2],{0.32f,0.42f,0.52f,1}) > 0.01f);
	if (!NewScene(fixture)) return;
	fixture.batches[1] = fixture.batches[0]; fixture.batches[1].partId = 2;
	fixture.request.batchCount = fixture.request.batchCapacity = 2;
	CHECK(fixture.water->PreparePatch(fixture.request,prepared) == WaterRenderBridge::PrepareStatus::Ready);
	if (!prepared.complete) return;
	fixture.water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_DEVICE_LOST_AFTER_FIRST_DRAW);
	DrawCounter lossCounter(fixture);
	const WaterRenderBridge::SubmissionReceipt lost = fixture.water->SubmitPreparedPatch(prepared);
	CHECK(lost.status == WaterRenderBridge::SubmitStatus::DeviceLost);
	CHECK_EQ(lost.commandsIssued,1u); CHECK(!lost.coveragePublished); CHECK_EQ(lossCounter.End(),2ull);
	fixture.water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_NONE);
	++fixture.request.patchId;
	WaterRenderBridge::PreparedPatch later = {};
	CHECK(fixture.water->PreparePatch(fixture.request,later) != WaterRenderBridge::PrepareStatus::Ready);
	if (!NewScene(fixture)) return;
	fixture.Draw("device-loss-next-frame-recovery");
}

void ConfigureOrdinary(Fixture &fixture)
{
	DX11BackendClass &backend = fixture.backend;
	float identity[16]; WaterRenderPacket::Identity(identity);
	backend.Set_Transform(D3DTS_WORLD,identity);
	backend.Set_Render_State(D3DRS_LIGHTING,FALSE); backend.Set_Render_State(D3DRS_COLORVERTEX,TRUE);
	backend.Set_Render_State(D3DRS_FOGENABLE,FALSE); backend.Set_Render_State(D3DRS_ZENABLE,D3DZB_FALSE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE,FALSE); backend.Set_Render_State(D3DRS_CULLMODE,D3DCULL_NONE);
	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE,FALSE);
	backend.Set_Texture_Stage_State(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
	backend.Set_Texture_Stage_State(1,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
	backend.Set_Vertex_Format(0x242);
}

void OrdinaryStateAndImmutableCapture(Fixture &fixture)
{
	if (!NewScene(fixture)) return;
	ConfigureOrdinary(fixture);
	Vertex ordinary[4] = {fixture.vertices[0],fixture.vertices[1],fixture.vertices[3],fixture.vertices[2]};
	for (unsigned i = 0; i < 4; ++i) ordinary[i].colour = 0xff4080c0;
	CHECK(fixture.backend.Draw_User_Strip(ordinary,2,sizeof(Vertex)));
	std::vector<Pixel> reference; CHECK(fixture.Read(reference));
	fixture.device.Get_Context()->VSSetShaderResources(15,1,&fixture.bed.view);
	fixture.device.Get_Context()->PSSetShaderResources(15,1,&fixture.bed.view);
	const std::vector<std::uintptr_t> before = ContextState(fixture.device.Get_Context());
	DrawCounter counter(fixture);
	fixture.Draw("ordinary-water-ordinary");
	CHECK_EQ(counter.End(),2ull);
	CHECK(before == ContextState(fixture.device.Get_Context()));
	CHECK(fixture.backend.Draw_User_Strip(ordinary,2,sizeof(Vertex)));
	std::vector<Pixel> actual; CHECK(fixture.Read(actual));
	CHECK_EQ(actual.size(),reference.size());
	if (!reference.empty() && actual.size() == reference.size()) CHECK_MEM(&reference[0],&actual[0],actual.size()*sizeof(Pixel));
	if (!NewScene(fixture)) return;
	WaterRenderBridge::PreparedPatch first = {}, second = {};
	CHECK(fixture.water->PreparePatch(fixture.request,first) == WaterRenderBridge::PrepareStatus::Ready);
	if (!first.complete) return;
	const void *originalColour = first.packet.resources.sceneColour.identity;
	const void *originalDepth = first.packet.resources.sceneDepth.identity;
	std::vector<Pixel> snapshotBefore, snapshotAfter;
	ID3D11ShaderResourceView *snapshotView = static_cast<ID3D11ShaderResourceView *>(const_cast<void *>(originalColour));
	ID3D11Resource *resource = NULL; snapshotView->GetResource(&resource);
	ID3D11Texture2D *snapshotSurface = NULL;
	CHECK(SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void **>(&snapshotSurface))));
	Release(resource);
	if (snapshotSurface == NULL) return;
	CHECK(fixture.Read(snapshotBefore,snapshotSurface));
	const float changed[4] = {1,0,0,1};
	fixture.device.Get_Context()->ClearRenderTargetView(fixture.colourView,changed);
	fixture.device.Get_Context()->ClearDepthStencilView(fixture.depthView,D3D11_CLEAR_DEPTH,0.95f,0);
	++fixture.request.patchId;
	CHECK(fixture.water->PreparePatch(fixture.request,second) == WaterRenderBridge::PrepareStatus::Ready);
	CHECK_EQ(second.packet.resources.sceneColour.identity,originalColour);
	CHECK_EQ(second.packet.resources.sceneDepth.identity,originalDepth);
	CHECK_EQ(second.viewIdentity,first.viewIdentity);
	CHECK(fixture.Read(snapshotAfter,snapshotSurface)); Release(snapshotSurface);
	CHECK_EQ(snapshotBefore.size(),snapshotAfter.size());
	if (!snapshotBefore.empty() && snapshotBefore.size() == snapshotAfter.size())
		CHECK_MEM(&snapshotBefore[0],&snapshotAfter[0],snapshotBefore.size()*sizeof(Pixel));
}

}

TEST(dx11water_optics_real_surface_readbacks_state_and_atomic_refusal)
{
	Fixture fixture;
	const bool initialised = fixture.Initialise(); CHECK(initialised);
	if (!initialised) return;
	ReflectionAndBed(fixture);
	DepthAndOccluder(fixture);
	EdgeShroudAndFog(fixture);
	RefusalsAndLifetime(fixture);
	OrdinaryStateAndImmutableCapture(fixture);
	CHECK_EQ(DebugWarnings(fixture.device.Get_Device()),0u);
}
