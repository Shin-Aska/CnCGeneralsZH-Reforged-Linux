/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "test_harness.h"
#include "dx11backend.h"
#include "dx11water.h"

#include <d3d11sdklayers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static const unsigned SCENE_WIDTH = 64;
static const unsigned SCENE_HEIGHT = 48;

static HWND make_hidden_window()
{
	static const char * const CLASS_NAME = "CnCGeneralsZHDX11WaterSceneTest";
	WNDCLASSA window_class;
	ZeroMemory(&window_class, sizeof(window_class));
	window_class.lpfnWndProc = DefWindowProcA;
	window_class.hInstance = GetModuleHandleA(NULL);
	window_class.lpszClassName = CLASS_NAME;
	RegisterClassA(&window_class);
	return CreateWindowExA(0, CLASS_NAME, CLASS_NAME, WS_OVERLAPPEDWINDOW, 0, 0,
		SCENE_WIDTH, SCENE_HEIGHT, NULL, NULL, GetModuleHandleA(NULL), NULL);
}

static WaterRenderBridge::ViewState make_view(float marker)
{
	WaterRenderBridge::ViewState state = {};
	state.view[0] = state.view[5] = state.view[10] = state.view[15] = 1.0f;
	state.projection[0] = state.projection[5] = state.projection[10] = 1.0f;
	state.projection[15] = marker;
	state.viewport.x = 3.0f;
	state.viewport.y = 5.0f;
	state.viewport.width = static_cast<float>(SCENE_WIDTH - 7);
	state.viewport.height = static_cast<float>(SCENE_HEIGHT - 9);
	state.viewport.minDepth = 0.2f;
	state.viewport.maxDepth = 0.8f;
	return state;
}

static bool create_colour(ID3D11Device * device, unsigned width, unsigned height, unsigned samples,
	ID3D11Texture2D ** surface, ID3D11RenderTargetView ** view)
{
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	description.SampleDesc.Count = samples;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	return SUCCEEDED(device->CreateTexture2D(&description, NULL, surface))
		&& SUCCEEDED(device->CreateRenderTargetView(*surface, NULL, view));
}

static bool create_depth(ID3D11Device * device, unsigned width, unsigned height, unsigned samples,
	ID3D11Texture2D ** surface, ID3D11DepthStencilView ** view)
{
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = width;
	description.Height = height;
	description.MipLevels = 1;
	description.ArraySize = 1;
	description.Format = DXGI_FORMAT_R24G8_TYPELESS;
	description.SampleDesc.Count = samples;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&description, NULL, surface))) return false;
	D3D11_DEPTH_STENCIL_VIEW_DESC depth = {};
	depth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.ViewDimension = samples == 1 ? D3D11_DSV_DIMENSION_TEXTURE2D
		: D3D11_DSV_DIMENSION_TEXTURE2DMS;
	return SUCCEEDED(device->CreateDepthStencilView(*surface, &depth, view));
}

static bool create_colour_array(ID3D11Device * device, ID3D11Texture2D ** surface,
	ID3D11RenderTargetView * views[2])
{
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = 8;
	description.Height = 8;
	description.MipLevels = 1;
	description.ArraySize = 2;
	description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&description, NULL, surface))) return false;
	for (unsigned slice = 0; slice < 2; ++slice) {
		D3D11_RENDER_TARGET_VIEW_DESC view = {};
		view.Format = description.Format;
		view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
		view.Texture2DArray.ArraySize = 1;
		view.Texture2DArray.FirstArraySlice = slice;
		if (FAILED(device->CreateRenderTargetView(*surface, &view, &views[slice]))) return false;
	}
	return true;
}

static bool create_depth_array(ID3D11Device * device, ID3D11Texture2D ** surface,
	ID3D11DepthStencilView * views[2])
{
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = 8;
	description.Height = 8;
	description.MipLevels = 1;
	description.ArraySize = 2;
	description.Format = DXGI_FORMAT_R24G8_TYPELESS;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_DEFAULT;
	description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&description, NULL, surface))) return false;
	for (unsigned slice = 0; slice < 2; ++slice) {
		D3D11_DEPTH_STENCIL_VIEW_DESC view = {};
		view.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		view.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
		view.Texture2DArray.ArraySize = 1;
		view.Texture2DArray.FirstArraySlice = slice;
		if (FAILED(device->CreateDepthStencilView(*surface, &view, &views[slice]))) return false;
	}
	return true;
}

static ID3D11Texture2D * staging_copy(ID3D11Device * device, ID3D11DeviceContext * context,
	ID3D11ShaderResourceView * view)
{
	ID3D11Resource * resource = NULL;
	view->GetResource(&resource);
	ID3D11Texture2D * source = NULL;
	if (resource == NULL || FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
			reinterpret_cast<void **>(&source)))) {
		if (resource != NULL) resource->Release();
		return NULL;
	}
	resource->Release();
	D3D11_TEXTURE2D_DESC description;
	source->GetDesc(&description);
	description.Usage = D3D11_USAGE_STAGING;
	description.BindFlags = 0;
	description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	description.MiscFlags = 0;
	ID3D11Texture2D * staging = NULL;
	if (SUCCEEDED(device->CreateTexture2D(&description, NULL, &staging))) {
		context->CopyResource(staging, source);
	}
	source->Release();
	return staging;
}

static unsigned debug_warnings(ID3D11Device * device)
{
	ID3D11InfoQueue * queue = NULL;
	if (FAILED(device->QueryInterface(__uuidof(ID3D11InfoQueue),
			reinterpret_cast<void **>(&queue)))) return ~0u;
	unsigned result = 0;
	const UINT64 count = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
	for (UINT64 index = 0; index < count; ++index) {
		SIZE_T size = 0;
		queue->GetMessage(index, NULL, &size);
		std::vector<unsigned char> storage(size);
		D3D11_MESSAGE * message = reinterpret_cast<D3D11_MESSAGE *>(&storage[0]);
		if (SUCCEEDED(queue->GetMessage(index, message, &size))
				&& message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) ++result;
	}
	queue->Release();
	return result;
}

TEST(dx11water_scene_owner_starts_without_a_device)
{
	DX11WaterClass water;
	CHECK(!water.GetCapability().Available());
	CHECK_EQ(water.Snapshot_Count(), 0u);
}

TEST(dx11water_scene_snapshots_are_immutable_paired_bounded_and_restore_state)
{
	HWND window = make_hidden_window();
	CHECK(window != NULL);
	DX11DeviceClass device;
	device.Request_Debug_Layer();
	CHECK(device.Create(window, SCENE_WIDTH, SCENE_HEIGHT));
	CHECK(device.Has_Debug_Layer());
	const char * expected_driver = getenv("ZH_TEST_DX11_EXPECT_DRIVER");
	if (expected_driver != NULL && _stricmp(expected_driver, "warp") == 0) CHECK(device.Is_Software());
	if (expected_driver != NULL && _stricmp(expected_driver, "hardware") == 0) CHECK(!device.Is_Software());
	printf("  adapter=%s debug=%u feature=0x%x sample=1 colour=%u depth=%u\n",
		device.Is_Software() ? "warp" : "hardware", device.Has_Debug_Layer() ? 1u : 0u,
		static_cast<unsigned>(device.Get_Feature_Level()),
		static_cast<unsigned>(DXGI_FORMAT_R16G16B16A16_FLOAT),
		static_cast<unsigned>(DXGI_FORMAT_R24G8_TYPELESS));

	ID3D11InfoQueue * queue = NULL;
	CHECK(SUCCEEDED(device.Get_Device()->QueryInterface(__uuidof(ID3D11InfoQueue),
		reinterpret_cast<void **>(&queue))));
	if (queue != NULL) {
		queue->ClearStoredMessages();
		queue->Release();
	}

	DX11BackendClass backend;
	CHECK(backend.Initialise(&device));
	DX11WaterClass * water = backend.Water_Owner();
	CHECK(water != NULL);
	WaterRenderBridge::Capability capability = water->GetCapability();
	CHECK(capability.backend == WaterRenderBridge::BackendKind::NativeD3D11);
	CHECK(capability.Available());
	CHECK(capability.reason == WaterRenderBridge::CapabilityReason::None);
	CHECK(WaterRenderBridge::HasAll(capability.features,
		WaterRenderBridge::CoreRequired));

	ID3D11Texture2D * scene_colour = NULL;
	ID3D11RenderTargetView * scene_view = NULL;
	CHECK(create_colour(device.Get_Device(), SCENE_WIDTH, SCENE_HEIGHT, 1,
		&scene_colour, &scene_view));
	device.Set_Scene_View(scene_view);
	ID3D11DepthStencilView * scene_depth = device.Get_Depth_Stencil_View();
	ID3D11DeviceContext * context = device.Get_Context();
	const float first_colour[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	context->ClearRenderTargetView(scene_view, first_colour);
	context->ClearDepthStencilView(scene_depth, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.25f, 0x5a);
	context->OMSetRenderTargets(1, &scene_view, scene_depth);
	D3D11_VIEWPORT viewport = { 3.0f, 5.0f, static_cast<float>(SCENE_WIDTH - 7),
		static_cast<float>(SCENE_HEIGHT - 9), 0.2f, 0.8f };
	D3D11_RECT scissor = { 4, 6, static_cast<LONG>(SCENE_WIDTH - 3),
		static_cast<LONG>(SCENE_HEIGHT - 2) };
	context->RSSetViewports(1, &viewport);
	context->RSSetScissorRects(1, &scissor);
	D3D11_TEXTURE2D_DESC sentinel_description = {};
	sentinel_description.Width = 1;
	sentinel_description.Height = 1;
	sentinel_description.MipLevels = 1;
	sentinel_description.ArraySize = 1;
	sentinel_description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sentinel_description.SampleDesc.Count = 1;
	sentinel_description.Usage = D3D11_USAGE_DEFAULT;
	sentinel_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	ID3D11Texture2D * sentinel_surface = NULL;
	ID3D11ShaderResourceView * sentinel = NULL;
	CHECK(SUCCEEDED(device.Get_Device()->CreateTexture2D(&sentinel_description, NULL,
		&sentinel_surface)));
	CHECK(SUCCEEDED(device.Get_Device()->CreateShaderResourceView(sentinel_surface, NULL,
		&sentinel)));
	context->VSSetShaderResources(3, 1, &sentinel);
	context->HSSetShaderResources(4, 1, &sentinel);
	context->DSSetShaderResources(5, 1, &sentinel);
	context->GSSetShaderResources(6, 1, &sentinel);
	context->PSSetShaderResources(9, 1, &sentinel);
	context->CSSetShaderResources(7, 1, &sentinel);
	WaterRenderBridge::CurrentView before_capture = {};
	CHECK(!water->QueryCurrentView(before_capture));

	WaterRenderBridge::ViewState view = make_view(1.0f);
	DX11WaterClass::SnapshotView snapshot = {};
	CHECK(water->Capture_Scene(11, 7, 1, view, NULL, NULL, snapshot)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK_EQ(water->Snapshot_Count(), 1u);
	CHECK_EQ(snapshot.width, SCENE_WIDTH);
	CHECK_EQ(snapshot.height, SCENE_HEIGHT);
	CHECK_EQ(snapshot.sampleCount, 1u);
	WaterRenderBridge::CurrentView current_view = {};
	CHECK(water->QueryCurrentView(current_view));
	CHECK(WaterRenderBridge::ValidCurrentView(current_view));
	CHECK_EQ(current_view.frameId, 11u);
	CHECK_EQ(current_view.resourceGeneration, 7u);
	CHECK_EQ(current_view.captureSerial, 1u);
	CHECK(current_view.colourIdentity == snapshot.colourIdentity);
	CHECK(current_view.depthIdentity == snapshot.depthIdentity);
	CHECK(current_view.viewIdentity == snapshot.viewIdentity);
	CHECK_EQ(current_view.targetWidth, snapshot.width);
	CHECK_EQ(current_view.targetHeight, snapshot.height);
	CHECK_EQ(current_view.targetFormat, snapshot.colourFormat);
	CHECK_EQ(current_view.targetSamples, snapshot.sampleCount);
	CHECK(WaterRenderBridge::SameViewState(current_view.viewState, snapshot.viewState));
	WaterRenderBridge::CurrentView repeated_view = {};
	CHECK(water->QueryCurrentView(repeated_view));
	CHECK(repeated_view.viewIdentity == current_view.viewIdentity);

	D3D11_VIEWPORT changed_viewport = viewport;
	changed_viewport.TopLeftX += 1.0f;
	context->RSSetViewports(1, &changed_viewport);
	WaterRenderBridge::CurrentView changed_current = {};
	CHECK(!water->QueryCurrentView(changed_current));
	context->RSSetViewports(1, &viewport);
	CHECK(water->QueryCurrentView(changed_current));
	float changed_projection[16];
	memcpy(changed_projection, view.projection, sizeof(changed_projection));
	changed_projection[12] = 1.0f;
	backend.Set_Transform(D3DTS_PROJECTION, changed_projection);
	CHECK(!water->QueryCurrentView(changed_current));
	backend.Set_Transform(D3DTS_PROJECTION, view.projection);
	CHECK(water->QueryCurrentView(changed_current));

	ID3D11RenderTargetView * restored_target = NULL;
	ID3D11DepthStencilView * restored_depth = NULL;
	context->OMGetRenderTargets(1, &restored_target, &restored_depth);
	CHECK(restored_target == scene_view);
	CHECK(restored_depth == scene_depth);
	if (restored_target != NULL) restored_target->Release();
	if (restored_depth != NULL) restored_depth->Release();
	UINT viewport_count = 1;
	D3D11_VIEWPORT restored_viewport = {};
	context->RSGetViewports(&viewport_count, &restored_viewport);
	CHECK_EQ(viewport_count, 1u);
	CHECK(memcmp(&viewport, &restored_viewport, sizeof(viewport)) == 0);
	UINT scissor_count = 1;
	D3D11_RECT restored_scissor = {};
	context->RSGetScissorRects(&scissor_count, &restored_scissor);
	CHECK_EQ(scissor_count, 1u);
	CHECK(memcmp(&scissor, &restored_scissor, sizeof(scissor)) == 0);
	ID3D11ShaderResourceView * restored_vertex = NULL;
	ID3D11ShaderResourceView * restored_hull = NULL;
	ID3D11ShaderResourceView * restored_domain = NULL;
	ID3D11ShaderResourceView * restored_geometry = NULL;
	ID3D11ShaderResourceView * restored_pixel = NULL;
	ID3D11ShaderResourceView * restored_compute = NULL;
	context->VSGetShaderResources(3, 1, &restored_vertex);
	context->HSGetShaderResources(4, 1, &restored_hull);
	context->DSGetShaderResources(5, 1, &restored_domain);
	context->GSGetShaderResources(6, 1, &restored_geometry);
	context->PSGetShaderResources(9, 1, &restored_pixel);
	context->CSGetShaderResources(7, 1, &restored_compute);
	CHECK(restored_vertex == sentinel);
	CHECK(restored_hull == sentinel);
	CHECK(restored_domain == sentinel);
	CHECK(restored_geometry == sentinel);
	CHECK(restored_pixel == sentinel);
	CHECK(restored_compute == sentinel);
	if (restored_vertex != NULL) restored_vertex->Release();
	if (restored_hull != NULL) restored_hull->Release();
	if (restored_domain != NULL) restored_domain->Release();
	if (restored_geometry != NULL) restored_geometry->Release();
	if (restored_pixel != NULL) restored_pixel->Release();
	if (restored_compute != NULL) restored_compute->Release();

	const float second_colour[4] = { 1.0f, 0.0f, 0.125f, 0.5f };
	context->ClearRenderTargetView(scene_view, second_colour);
	context->ClearDepthStencilView(scene_depth, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.75f, 0xa5);
	ID3D11Texture2D * colour_readback = staging_copy(device.Get_Device(), context, snapshot.colour);
	ID3D11Texture2D * depth_readback = staging_copy(device.Get_Device(), context, snapshot.depth);
	CHECK(colour_readback != NULL);
	CHECK(depth_readback != NULL);
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	CHECK(SUCCEEDED(context->Map(colour_readback, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned short * half = static_cast<const unsigned short *>(mapped.pData);
	const unsigned short colour_values[4] = { half[0], half[1], half[2], half[3] };
	CHECK_EQ(colour_values[0], static_cast<unsigned short>(0x3400));
	CHECK_EQ(colour_values[1], static_cast<unsigned short>(0x3800));
	CHECK_EQ(colour_values[2], static_cast<unsigned short>(0x3a00));
	CHECK_EQ(colour_values[3], static_cast<unsigned short>(0x3c00));
	context->Unmap(colour_readback, 0);
	CHECK(SUCCEEDED(context->Map(depth_readback, 0, D3D11_MAP_READ, 0, &mapped)));
	const unsigned packed_depth = *static_cast<const unsigned *>(mapped.pData) & 0x00ffffffu;
	CHECK(packed_depth >= 0x003ffffeu && packed_depth <= 0x00400001u);
	context->Unmap(depth_readback, 0);
	printf("  readback colour_half=%04x,%04x,%04x,%04x depth24=%06x\n",
		colour_values[0], colour_values[1], colour_values[2], colour_values[3], packed_depth);
	colour_readback->Release();
	depth_readback->Release();

	DX11WaterClass::SnapshotView reused = {};
	CHECK(water->Capture_Scene(11, 7, 1, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_REUSED);
	CHECK(reused.colour == snapshot.colour);
	CHECK(reused.depth == snapshot.depth);
	CHECK(water->Find_Scene(11, 7, 1, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_REUSED);
	WaterRenderBridge::ViewState second_view = make_view(2.0f);
	CHECK(water->Capture_Scene(11, 7, 2, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK_EQ(water->Snapshot_Count(), 2u);
	CHECK(water->Capture_Scene(11, 8, 3, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK_EQ(water->Snapshot_Count(), 3u);
	water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_COLOUR_ALLOCATION);
	CHECK(water->Capture_Scene(11, 8, 4, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_ALLOCATION_FAILED);
	CHECK_EQ(water->Snapshot_Count(), 3u);
	water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_DEPTH_ALLOCATION);
	CHECK(water->Capture_Scene(11, 8, 5, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_ALLOCATION_FAILED);
	CHECK_EQ(water->Snapshot_Count(), 3u);
	water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_NONE);
	for (unsigned serial = 10; serial < 20; ++serial) {
		CHECK(water->Capture_Scene(11, 8, serial, second_view, NULL, NULL, reused)
			== DX11WaterClass::SNAPSHOT_READY);
	}
	CHECK_EQ(water->Snapshot_Count(), 8u);

	backend.Clear(false, true, first_colour);
	CHECK_EQ(water->Snapshot_Count(), 0u);
	CHECK(water->Find_Scene(11, 8, 19, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_MISSING_TARGET);
	CHECK(water->Capture_Scene(11, 8, 20, second_view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	backend.Clear(true, false, second_colour);
	CHECK_EQ(water->Snapshot_Count(), 0u);

	ID3D11Texture2D * array_colour = NULL;
	ID3D11Texture2D * array_depth = NULL;
	ID3D11RenderTargetView * array_views[2] = {};
	ID3D11DepthStencilView * array_depths[2] = {};
	CHECK(create_colour_array(device.Get_Device(), &array_colour, array_views));
	CHECK(create_depth_array(device.Get_Device(), &array_depth, array_depths));
	DX11WaterClass::SnapshotView slice_a = {};
	DX11WaterClass::SnapshotView slice_b = {};
	DX11WaterClass::SnapshotView slice_a_again = {};
	CHECK(water->Capture_Scene(21, 9, 1, view, array_views[0], array_depths[0], slice_a)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK(water->Capture_Scene(21, 9, 2, view, array_views[1], array_depths[1], slice_b)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK(slice_a.colourIdentity == slice_b.colourIdentity);
	CHECK(slice_a.depthIdentity == slice_b.depthIdentity);
	CHECK(slice_a.viewIdentity != 0);
	CHECK(slice_b.viewIdentity != 0);
	CHECK(slice_a.viewIdentity != slice_b.viewIdentity);
	CHECK(water->Capture_Scene(21, 9, 1, view, array_views[0], array_depths[0], slice_a_again)
		== DX11WaterClass::SNAPSHOT_REUSED);
	CHECK(slice_a_again.viewIdentity == slice_a.viewIdentity);
	WaterRenderBridge::CurrentView slice_current = {};
	context->OMSetRenderTargets(1, &array_views[0], array_depths[0]);
	CHECK(water->QueryCurrentView(slice_current));
	CHECK(slice_current.viewIdentity == slice_a.viewIdentity);
	context->OMSetRenderTargets(1, &array_views[1], array_depths[1]);
	CHECK(water->QueryCurrentView(slice_current));
	CHECK(slice_current.viewIdentity == slice_b.viewIdentity);
	context->OMSetRenderTargets(1, &array_views[0], array_depths[0]);
	CHECK(water->QueryCurrentView(slice_current));
	CHECK(slice_current.viewIdentity == slice_a.viewIdentity);
	context->OMSetRenderTargets(1, &scene_view, scene_depth);
	water->InvalidateMap();
	CHECK_EQ(water->Snapshot_Count(), 0u);
	for (unsigned slice = 0; slice < 2; ++slice) {
		array_views[slice]->Release();
		array_depths[slice]->Release();
	}
	array_colour->Release();
	array_depth->Release();

	UINT quality = 0;
	CHECK(SUCCEEDED(device.Get_Device()->CheckMultisampleQualityLevels(
		DXGI_FORMAT_R16G16B16A16_FLOAT, 2, &quality)));
	CHECK(quality > 0);
	ID3D11Texture2D * ms_colour = NULL;
	ID3D11RenderTargetView * ms_view = NULL;
	ID3D11Texture2D * ms_depth_surface = NULL;
	ID3D11DepthStencilView * ms_depth = NULL;
	CHECK(create_colour(device.Get_Device(), 8, 8, 2, &ms_colour, &ms_view));
	CHECK(create_depth(device.Get_Device(), 8, 8, 2, &ms_depth_surface, &ms_depth));
	CHECK(water->Capture_Scene(12, 9, 1, view, ms_view, ms_depth, reused)
		== DX11WaterClass::SNAPSHOT_MULTISAMPLED);
	ms_depth->Release();
	ms_depth_surface->Release();
	ms_view->Release();
	ms_colour->Release();

	ID3D11Texture2D * secondary_colour = NULL;
	ID3D11RenderTargetView * secondary_view = NULL;
	CHECK(create_colour(device.Get_Device(), 17, 13, 1, &secondary_colour, &secondary_view));
	backend.Set_Render_Target(NULL);
	context->RSSetViewports(1, &viewport);
	CHECK(water->Capture_Scene(13, 10, 1, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	WaterRenderBridge::CurrentView main_target = {};
	CHECK(water->QueryCurrentView(main_target));
	backend.Set_Render_Target(secondary_view);
	WaterRenderBridge::CurrentView other_target = {};
	CHECK(!water->QueryCurrentView(other_target));
	CHECK(backend.Current_Depth_Target() != NULL);
	CHECK(backend.Current_Depth_Target() != device.Get_Depth_Stencil_View());
	CHECK(backend.Current_Depth_Texture() != NULL);
	backend.Set_Render_Target(scene_view);
	CHECK(backend.Current_Depth_Target() == device.Get_Depth_Stencil_View());
	CHECK(backend.Current_Depth_Texture() == device.Get_Depth_Texture());
	backend.Set_Render_Target(NULL);
	context->RSSetViewports(1, &viewport);
	CHECK(water->QueryCurrentView(main_target));

	water->Advance_Presented_Frame();
	WaterRenderBridge::CurrentView expired = {};
	CHECK(!water->QueryCurrentView(expired));
	CHECK(water->Capture_Scene(14, 10, 2, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK(water->QueryCurrentView(expired));
	CHECK_EQ(expired.frameId, 14u);
	CHECK(expired.viewIdentity != main_target.viewIdentity);
	water->InvalidateResources(11);
	CHECK(!water->QueryCurrentView(expired));
	CHECK_EQ(water->Snapshot_Count(), 0u);
	CHECK(water->Capture_Scene(14, 11, 3, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK(water->QueryCurrentView(expired));
	water->InvalidateMap();
	CHECK(!water->QueryCurrentView(expired));
	CHECK(water->Capture_Scene(14, 11, 4, view, NULL, NULL, reused)
		== DX11WaterClass::SNAPSHOT_READY);
	CHECK(water->QueryCurrentView(expired));
	device.Set_Scene_View(NULL);
	backend.Before_Resize();
	CHECK_EQ(water->Snapshot_Count(), 0u);
	CHECK(!water->QueryCurrentView(expired));
	CHECK(device.Resize(80, 50));
	backend.After_Resize();

	context->ClearState();
	context->Flush();
	sentinel->Release();
	sentinel_surface->Release();
	secondary_view->Release();
	secondary_colour->Release();
	scene_view->Release();
	scene_colour->Release();
	CHECK_EQ(debug_warnings(device.Get_Device()), 0u);
	backend.Shutdown();
	device.Release();
	DestroyWindow(window);
}
