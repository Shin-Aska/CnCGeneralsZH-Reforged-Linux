#include "WaterDx11SurfaceFixture.h"

namespace {

using namespace WaterDx11SurfaceTest;

const unsigned ViewportX = 7;
const unsigned ViewportY = 5;
const unsigned ViewportWidth = 50;
const unsigned ViewportHeight = 34;

float Difference(const Pixel &left, const Pixel &right)
{
	return std::fabs(left.r - right.r) + std::fabs(left.g - right.g)
		+ std::fabs(left.b - right.b);
}

DWORD FloatBits(float value)
{
	DWORD bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

void Multiply(const float left[16], const float right[16], float result[16])
{
	std::memset(result, 0, 16 * sizeof(float));
	for (unsigned row = 0; row < 4; ++row)
		for (unsigned column = 0; column < 4; ++column)
			for (unsigned inner = 0; inner < 4; ++inner)
				result[row * 4 + column] += left[row * 4 + inner] * right[inner * 4 + column];
}

void ConfigureOrdinary(DX11BackendClass &backend)
{
	float identity[16];
	WaterRenderPacket::Identity(identity);
	backend.Set_Transform(D3DTS_WORLD, identity);
	backend.Set_Render_State(D3DRS_LIGHTING, FALSE);
	backend.Set_Render_State(D3DRS_COLORVERTEX, TRUE);
	backend.Set_Render_State(D3DRS_FOGENABLE, FALSE);
	backend.Set_Render_State(D3DRS_ZENABLE, D3DZB_FALSE);
	backend.Set_Render_State(D3DRS_ZWRITEENABLE, FALSE);
	backend.Set_Render_State(D3DRS_CULLMODE, D3DCULL_NONE);
	backend.Set_Render_State(D3DRS_ALPHABLENDENABLE, FALSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	backend.Set_Texture_Stage_State(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
	backend.Set_Texture_Stage_State(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	backend.Set_Texture_Stage_State(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	backend.Set_Vertex_Format(0x242);
}

void PrimeStaleOrdinaryState(Fixture &fixture)
{
	ConfigureOrdinary(fixture.backend);
	fixture.backend.Set_Render_State(D3DRS_TEXTUREFACTOR, 0xffc02010);
	fixture.backend.Set_Render_State(D3DRS_FOGCOLOR, 0xffff2008);
	fixture.backend.Set_Render_State(D3DRS_ALPHAREF, 0x11);
	float mirror_view[16];
	WaterRenderPacket::Identity(mirror_view);
	mirror_view[0] = -1.0f;
	mirror_view[12] = 3.0f;
	float stale_projection[16];
	WaterRenderPacket::Identity(stale_projection);
	stale_projection[5] = 0.5f;
	fixture.backend.Set_Transform(D3DTS_VIEW, mirror_view);
	fixture.backend.Set_Transform(D3DTS_PROJECTION, stale_projection);
	Vertex ordinary[4] = {fixture.vertices[0], fixture.vertices[1], fixture.vertices[3], fixture.vertices[2]};
	for (unsigned index = 0; index < 4; ++index) ordinary[index].colour = 0xff4080c0;
	CHECK(fixture.backend.Draw_User_Strip(ordinary, 2, sizeof(Vertex)));
}

void ConfigureCallerScene(Fixture &fixture, bool fog)
{
	WaterRenderPacket::SceneInput &scene = fixture.request.inputs.scene;
	std::memset(scene.combiner, 0, sizeof(scene.combiner));
	std::memset(scene.shadowFromClip, 0, sizeof(scene.shadowFromClip));
	for (unsigned index = 0; index < 4; ++index) scene.shadowFromClip[index][index] = 1.0f;
	scene.shadowParameters = {};
	scene.shadowViewport = {};
	scene.shadowSoftness = {};
	scene.volumeParameters = {};
	scene.viewport = {static_cast<float>(ViewportX), static_cast<float>(ViewportY),
		static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight), 0.0f, 1.0f};
	scene.sunDirection = {0.0f, 0.28f, 0.96f, 0.0f};
	scene.sunColour = {0.7f, 0.6f, 0.5f, 0.0f};

	fixture.backend.Set_Transform(D3DTS_VIEW, scene.view);
	fixture.backend.Set_Transform(D3DTS_PROJECTION, scene.projection);
	fixture.backend.Set_Render_State(D3DRS_TEXTUREFACTOR, 0xcc336699);
	fixture.backend.Set_Render_State(D3DRS_FOGCOLOR, 0xff336699);
	fixture.backend.Set_Render_State(D3DRS_ALPHAREF, 0xa5);
	fixture.backend.Set_Render_State(D3DRS_FOGENABLE, fog ? TRUE : FALSE);
	fixture.backend.Set_Render_State(D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR);
	fixture.backend.Set_Render_State(D3DRS_FOGSTART, FloatBits(-4.0f));
	fixture.backend.Set_Render_State(D3DRS_FOGEND, FloatBits(-3.0f));

	ID3D11DeviceContext *context = fixture.device.Get_Context();
	context->OMSetRenderTargets(1, &fixture.colourView, fixture.depthView);
	D3D11_VIEWPORT viewport = {static_cast<float>(ViewportX) + 0.5f, static_cast<float>(ViewportY) + 0.5f,
		static_cast<float>(ViewportWidth), static_cast<float>(ViewportHeight), 0.0f, 1.0f};
	context->RSSetViewports(1, &viewport);
}

void ConfigurePhysicalSun(Fixture &fixture, float strength)
{
	float sun_view[16];
	WaterRenderPacket::Identity(sun_view);
	sun_view[12] = 0.125f;
	sun_view[13] = -0.25f;
	float sun_projection[16];
	WaterRenderPacket::Identity(sun_projection);
	sun_projection[0] = 0.01f;
	sun_projection[5] = 0.0125f;
	sun_projection[10] = 0.001f;
	sun_projection[14] = 0.5f;
	fixture.backend.Set_Transform(D3DTS_VIEW, sun_view);
	fixture.backend.Set_Transform(D3DTS_PROJECTION, sun_projection);
	CHECK(fixture.backend.Begin_Shadow_Map(64));
	ID3D11RenderTargetView *unused_target = NULL;
	ID3D11DepthStencilView *shadow_depth = NULL;
	fixture.device.Get_Context()->OMGetRenderTargets(1, &unused_target, &shadow_depth);
	CHECK(unused_target == NULL);
	CHECK(shadow_depth != NULL);
	if (shadow_depth != NULL)
		fixture.device.Get_Context()->ClearDepthStencilView(shadow_depth, D3D11_CLEAR_DEPTH, 0.0f, 0);
	Release(unused_target);
	Release(shadow_depth);
	fixture.backend.End_Shadow_Map();
	fixture.backend.Set_Shadow_Parameters(0.003f, strength, 5.5f, 1.25f, 0.75f, 1000.0f, 0.2f);
	const float smoke_caster[5] = {0.0f, 0.0f, 100.0f, 50.0f, 2.0f};
	CHECK(fixture.backend.Fill_Smoke_Map(smoke_caster, 1, 0.6f));
}

bool ReadPreparedConstants(Fixture &fixture, std::uint64_t patch_id,
	WaterShaderLayout::PixelConstants &constants)
{
	ID3D11Buffer *source = fixture.water->Get_Test_Prepared_Pixel_Constant_Buffer(patch_id);
	CHECK(source != NULL);
	if (source == NULL) return false;
	D3D11_BUFFER_DESC description = {};
	source->GetDesc(&description);
	CHECK_EQ(description.ByteWidth, static_cast<UINT>(sizeof(constants)));
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	description.StructureByteStride = 0;
	ID3D11Buffer *staging = NULL;
	const HRESULT created = fixture.device.Get_Device()->CreateBuffer(&description, NULL, &staging);
	CHECK(SUCCEEDED(created));
	if (FAILED(created)) {
		Release(source);
		return false;
	}
	fixture.device.Get_Context()->CopyResource(staging, source);
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	const HRESULT read = fixture.device.Get_Context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
	CHECK(SUCCEEDED(read));
	if (SUCCEEDED(read)) {
		std::memcpy(&constants, mapped.pData, sizeof(constants));
		fixture.device.Get_Context()->Unmap(staging, 0);
	}
	Release(staging);
	Release(source);
	return SUCCEEDED(read);
}

bool Prepare(Fixture &fixture, WaterRenderBridge::PreparedPatch &prepared,
	WaterShaderLayout::PixelConstants *constants = NULL)
{
	const WaterRenderBridge::PrepareStatus status = fixture.water->PreparePatch(fixture.request, prepared);
	CHECK(status == WaterRenderBridge::PrepareStatus::Ready);
	if (status != WaterRenderBridge::PrepareStatus::Ready) return false;
	CHECK(prepared.complete);
	CHECK_NE(prepared.viewIdentity, fixture.request.viewIdentity);
	WaterRenderBridge::CurrentView live = {};
	CHECK(fixture.water->QueryCurrentView(live));
	CHECK_EQ(live.viewIdentity, prepared.viewIdentity);
	CHECK_NEAR(live.viewState.viewport.x, static_cast<float>(ViewportX) + 0.5f, 0.00001f);
	CHECK_NEAR(live.viewState.viewport.y, static_cast<float>(ViewportY) + 0.5f, 0.00001f);
	if (constants != NULL && !ReadPreparedConstants(fixture, fixture.request.patchId, *constants)) return false;
	return true;
}

Pixel SubmitAndRead(Fixture &fixture, const WaterRenderBridge::PreparedPatch &prepared)
{
	const WaterRenderBridge::SubmissionReceipt receipt = fixture.water->SubmitPreparedPatch(prepared);
	CHECK(WaterRenderBridge::Truthful(receipt, prepared));
	CHECK_EQ(receipt.commandsIssued, 1u);
	std::vector<Pixel> pixels;
	CHECK(fixture.Read(pixels));
	if (pixels.empty()) return {};
	return pixels[(ViewportY + ViewportHeight / 2) * Fixture::Width
		+ ViewportX + ViewportWidth / 2];
}

void CheckActiveConstants(const Fixture &fixture, const WaterShaderLayout::PixelConstants &constants)
{
	const WaterShaderLayout::CommonConstants &common = constants.Common;
	const float expected_combiner[3][4] = {
		{0.2f, 0.4f, 0.6f, 0.8f},
		{0.2f, 0.4f, 0.6f, 1.0f},
		{165.0f, 0.0f, 0.0f, 0.0f}
	};
	for (unsigned row = 0; row < 3; ++row)
		for (unsigned column = 0; column < 4; ++column)
			CHECK_NEAR(common.Combiner[row][column], expected_combiner[row][column], 0.00001f);
	CHECK_NEAR(common.CameraPosition.x, 0.0f, 0.00001f);
	CHECK_NEAR(common.CameraPosition.y, 0.0f, 0.00001f);
	CHECK_NEAR(common.CameraPosition.z, 2.0f, 0.00001f);
	CHECK_NEAR(common.SunDirection.x, 0.0f, 0.00001f);
	CHECK_NEAR(common.SunDirection.y, 0.28f, 0.00001f);
	CHECK_NEAR(common.SunDirection.z, 0.96f, 0.00001f);
	CHECK_NEAR(common.SunColour.x, 0.7f, 0.00001f);
	CHECK_NEAR(common.SunColour.y, 0.6f, 0.00001f);
	CHECK_NEAR(common.SunColour.z, 0.5f, 0.00001f);
	CHECK_NEAR(common.Viewport.x, ViewportX + 0.5f, 0.00001f);
	CHECK_NEAR(common.Viewport.y, ViewportY + 0.5f, 0.00001f);
	CHECK_NEAR(common.Viewport.z, static_cast<float>(ViewportWidth), 0.00001f);
	CHECK_NEAR(common.Viewport.w, static_cast<float>(ViewportHeight), 0.00001f);

	float sun_view[16];
	WaterRenderPacket::Identity(sun_view);
	sun_view[12] = 0.125f;
	sun_view[13] = -0.25f;
	float sun_projection[16];
	WaterRenderPacket::Identity(sun_projection);
	sun_projection[0] = 0.01f;
	sun_projection[5] = 0.0125f;
	sun_projection[10] = 0.001f;
	sun_projection[14] = 0.5f;
	float sun_view_projection[16], scene_view_projection[16], clip_to_world[16], expected_shadow[16];
	Multiply(sun_view, sun_projection, sun_view_projection);
	Multiply(fixture.request.inputs.scene.view, fixture.request.inputs.scene.projection, scene_view_projection);
	CHECK(WaterRenderPacket::Invert(scene_view_projection, clip_to_world));
	Multiply(clip_to_world, sun_view_projection, expected_shadow);
	const float *actual_shadow = reinterpret_cast<const float *>(common.ShadowFromClip);
	for (unsigned index = 0; index < 16; ++index)
		CHECK_NEAR(actual_shadow[index], expected_shadow[index], 0.00001f);

	CHECK_NEAR(common.ShadowParameters.x, 1.0f / 64.0f, 0.00001f);
	CHECK_NEAR(common.ShadowParameters.y, 0.003f, 0.00001f);
	CHECK_NEAR(common.ShadowParameters.z, 0.85f, 0.00001f);
	CHECK_NEAR(common.ShadowParameters.w, 5.5f, 0.00001f);
	CHECK_NEAR(common.ShadowViewport.x, 1.0f / ViewportWidth, 0.00001f);
	CHECK_NEAR(common.ShadowViewport.y, 1.0f / ViewportHeight, 0.00001f);
	CHECK_NEAR(common.ShadowViewport.z, static_cast<float>(ViewportX), 0.00001f);
	CHECK_NEAR(common.ShadowViewport.w, static_cast<float>(ViewportY), 0.00001f);
	CHECK_NEAR(common.ShadowSoftness.x, 1.25f, 0.00001f);
	CHECK_NEAR(common.ShadowSoftness.y, 0.75f, 0.00001f);
	CHECK_NEAR(common.ShadowSoftness.z, 1000.0f, 0.00001f);
	CHECK_NEAR(common.ShadowSoftness.w, 0.2f, 0.00001f);
	CHECK_NEAR(common.VolumeParameters.x, 0.6f, 0.00001f);
	CHECK(common.VolumeParameters.y > 0.0f);
	CHECK_NEAR(common.VolumeParameters.z, 0.0f, 0.00001f);
	CHECK(common.VolumeParameters.w > 0.0f);
}

}

TEST(dx11water_scene_inputs_follow_the_live_backend_view_and_optional_maps)
{
	Fixture fixture;
	const bool initialised = fixture.Initialise();
	CHECK(initialised);
	if (!initialised) return;
	CHECK_EQ(fixture.device.Get_Feature_Level(), D3D_FEATURE_LEVEL_11_0);
	CHECK(fixture.device.Has_Debug_Layer());

	const bool first_scene = fixture.Scene();
	CHECK(first_scene);
	if (!first_scene) return;
	PrimeStaleOrdinaryState(fixture);
	ConfigurePhysicalSun(fixture, 0.85f);
	ConfigureCallerScene(fixture, true);
	WaterRenderBridge::PreparedPatch fog_prepared = {};
	WaterShaderLayout::PixelConstants active_constants = {};
	if (!Prepare(fixture, fog_prepared, &active_constants)) return;
	const Pixel fog = SubmitAndRead(fixture, fog_prepared);
	const Pixel live_fog = {0.2f, 0.4f, 0.6f, 1.0f};
	std::printf("  scene-input full-fog rgb=%.7f,%.7f,%.7f expected=%.7f,%.7f,%.7f\n",
		fog.r, fog.g, fog.b, live_fog.r, live_fog.g, live_fog.b);
	CHECK_NEAR(fog.r, live_fog.r, 0.00001f);
	CHECK_NEAR(fog.g, live_fog.g, 0.00001f);
	CHECK_NEAR(fog.b, live_fog.b, 0.00001f);
	CheckActiveConstants(fixture, active_constants);

	const bool shadow_scene = fixture.Scene();
	CHECK(shadow_scene);
	if (!shadow_scene) return;
	ConfigureCallerScene(fixture, false);
	WaterRenderBridge::PreparedPatch shadow_prepared = {};
	if (!Prepare(fixture, shadow_prepared)) return;
	const Pixel shadowed = SubmitAndRead(fixture, shadow_prepared);

	const bool lit_scene = fixture.Scene();
	CHECK(lit_scene);
	if (!lit_scene) return;
	fixture.backend.Clear_Shadow_Parameters();
	CHECK(fixture.backend.Fill_Smoke_Map(NULL, 0, 0.0f));
	ConfigureCallerScene(fixture, false);
	WaterRenderBridge::PreparedPatch lit_prepared = {};
	WaterShaderLayout::PixelConstants disabled_constants = {};
	if (!Prepare(fixture, lit_prepared, &disabled_constants)) return;
	const Pixel lit = SubmitAndRead(fixture, lit_prepared);
	std::printf("  scene-input shadow-smoke rgb=%.7f,%.7f,%.7f lit=%.7f,%.7f,%.7f difference=%.7f\n",
		shadowed.r, shadowed.g, shadowed.b, lit.r, lit.g, lit.b, Difference(shadowed, lit));
	CHECK(Difference(shadowed, lit) > 0.002f);
	CHECK(lit.r + lit.g + lit.b > shadowed.r + shadowed.g + shadowed.b + 0.002f);
	CHECK_NEAR(disabled_constants.Common.ShadowParameters.z, 0.0f, 0.00001f);
	CHECK_NEAR(disabled_constants.Common.VolumeParameters.x, 0.0f, 0.00001f);
	CHECK_EQ(DebugWarnings(fixture.device.Get_Device()), 0u);
}
