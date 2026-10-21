/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#ifndef DX11WATER_H
#define DX11WATER_H

#include "Platform/WaterRenderBridge.h"

#include <d3d11.h>
#include <map>
#include <vector>

class DX11BackendClass;
class DX11DeviceClass;
class DX11WaterFft;

class DX11WaterClass : public WaterRenderBridge::Bridge
{
public:
	enum class FftTestFailure { None, Allocation, Compile, Draw, Copy, FormatUnsupported, DeviceLostDuringFft, SurfacePipeline };
	struct FftTestState {
		bool ready;
		bool deviceLost;
		float time; // Committed phase time: raw water time * WaterFftSampling::TimeScale.
		std::uint64_t draws, copies, updates, generation;
		unsigned allocationCount, compileCount, drawCount, copyCount;
	};
	FftTestState Get_Test_Fft_State() const;
	ID3D11ShaderResourceView *Get_Test_Fft_Map(unsigned index) const;
	ID3D11ShaderResourceView *Get_Test_Fft_Array() const;
	void Set_Test_Fft_Failure(FftTestFailure stage, unsigned ordinal);
	bool Set_Test_Fft_Initial_Spectra(const float *values, std::size_t count);
	bool Prepare_Test_Fft(float raw_time);
	bool Set_Test_Fft_Fields(ID3D11ShaderResourceView *map0, ID3D11ShaderResourceView *map2,
		ID3D11ShaderResourceView *map4, ID3D11ShaderResourceView *array);
	bool Get_Test_Prepared_Vertex_Bytecode(std::uint64_t patch_id,
		std::vector<unsigned char> &bytecode) const;
	ID3D11Buffer *Get_Test_Prepared_Vertex_Constant_Buffer(std::uint64_t patch_id) const;
	ID3D11ShaderResourceView *Get_Test_Prepared_Vertex_Resource(std::uint64_t patch_id, unsigned slot) const;
	enum SnapshotStatus
	{
		SNAPSHOT_READY,
		SNAPSHOT_REUSED,
		SNAPSHOT_NO_DEVICE,
		SNAPSHOT_MISSING_TARGET,
		SNAPSHOT_UNSUPPORTED_RESOURCE,
		SNAPSHOT_FORMAT_UNSUPPORTED,
		SNAPSHOT_SIZE_MISMATCH,
		SNAPSHOT_MULTISAMPLED,
		SNAPSHOT_ALLOCATION_FAILED,
		SNAPSHOT_DEVICE_LOST
	};

	enum TestFailure
	{
		TEST_FAILURE_NONE,
		TEST_FAILURE_COLOUR_ALLOCATION,
		TEST_FAILURE_DEPTH_ALLOCATION,
		TEST_FAILURE_SHADER,
		TEST_FAILURE_BUFFER,
		TEST_FAILURE_LATER_BATCH,
		TEST_FAILURE_DEVICE_LOST_AFTER_FIRST_DRAW
	};

	struct SnapshotView
	{
		ID3D11ShaderResourceView * colour;
		ID3D11ShaderResourceView * depth;
		std::uintptr_t colourIdentity;
		std::uintptr_t depthIdentity;
		std::uintptr_t viewIdentity;
		std::uint32_t colourSubresource;
		std::uint32_t depthSubresource;
		std::uint32_t colourFormat;
		std::uint32_t depthFormat;
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t sampleCount;
		std::uint64_t deviceGeneration;
		std::uint64_t resourceGeneration;
		std::uint64_t frameId;
		std::uint64_t captureSerial;
		WaterRenderBridge::ViewState viewState;
	};

	DX11WaterClass();
	~DX11WaterClass();

	bool Initialise(DX11DeviceClass * device, DX11BackendClass * backend);
	void Shutdown();
	void Before_Resize();
	void After_Resize();

	SnapshotStatus Capture_Scene(std::uint64_t frame_id, std::uint64_t resource_generation,
		std::uint64_t capture_serial, const WaterRenderBridge::ViewState & view_state,
		ID3D11RenderTargetView * colour, ID3D11DepthStencilView * depth, SnapshotView & snapshot);
	SnapshotStatus Find_Scene(std::uint64_t frame_id, std::uint64_t resource_generation,
		std::uint64_t capture_serial, const WaterRenderBridge::ViewState & view_state,
		ID3D11RenderTargetView * colour, ID3D11DepthStencilView * depth, SnapshotView & snapshot);
	void Invalidate_Source(ID3D11Resource * resource);
	unsigned Snapshot_Count() const;
	void Set_Test_Failure(TestFailure failure) { Failure = failure; }
	void Set_Test_Resource(const void * identity, ID3D11ShaderResourceView * view);
	ID3D11Buffer *Get_Test_Prepared_Pixel_Constant_Buffer(std::uint64_t patch_id) const;
	void Clear_Test_Resources();
	void Advance_Presented_Frame();

	WaterRenderBridge::Capability GetCapability() const;
	bool QueryCurrentView(WaterRenderBridge::CurrentView & current) const;
	WaterRenderBridge::PrepareStatus PreparePatch(const WaterRenderBridge::PrepareRequest & request,
		WaterRenderBridge::PreparedPatch & prepared);
	WaterRenderBridge::SubmissionReceipt SubmitPreparedPatch(
		const WaterRenderBridge::PreparedPatch & prepared);
	void End(std::uint64_t frame_id);
	void InvalidateResources(std::uint64_t resource_generation);
	void InvalidateMap();

private:
	DX11WaterClass(const DX11WaterClass &);
	DX11WaterClass & operator=(const DX11WaterClass &);

	struct SnapshotKey
	{
		std::uintptr_t colourIdentity;
		std::uintptr_t depthIdentity;
		std::uint32_t colourSubresource;
		std::uint32_t depthSubresource;
		std::uint32_t colourFormat;
		std::uint32_t depthFormat;
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t sampleCount;
		std::uint64_t deviceGeneration;
		std::uint64_t resourceGeneration;
		std::uint64_t frameId;
		std::uint64_t captureSerial;
		WaterRenderBridge::ViewState viewState;
	};

	struct SnapshotEntry
	{
		bool valid;
		SnapshotKey key;
		ID3D11Texture2D * colourSource;
		ID3D11Texture2D * colourSurface;
		ID3D11ShaderResourceView * colourView;
		ID3D11Texture2D * depthSource;
		ID3D11Texture2D * depthSurface;
		ID3D11ShaderResourceView * depthView;
		std::uintptr_t viewIdentity;
		std::uint64_t presentationEpoch;
		std::uint64_t used;
	};

	static const unsigned SNAPSHOT_LIMIT = 8;
	bool Resolve_Key(std::uint64_t frame_id, std::uint64_t resource_generation,
		std::uint64_t capture_serial, const WaterRenderBridge::ViewState & view_state,
		ID3D11RenderTargetView * & colour, ID3D11DepthStencilView * & depth,
		SnapshotKey & key, ID3D11Texture2D * & colour_surface, ID3D11Texture2D * & depth_surface,
		DXGI_FORMAT & colour_view_format, DXGI_FORMAT & depth_view_format,
		SnapshotStatus & failure) const;
	int Find(const SnapshotKey & key);
	void Publish(const SnapshotEntry & entry, SnapshotView & snapshot) const;
	void Release(SnapshotEntry & entry);
	void Release_All();
	bool Probe_Formats();
	bool Ensure_Renderer();
	bool Initialise_Renderer();
	void Release_Renderer();
	enum RendererBootstrapState
	{
		RENDERER_UNINITIALIZED,
		RENDERER_INITIALIZING,
		RENDERER_READY,
		RENDERER_FAILED
	};

	struct SurfacePipeline
	{
		ID3D11VertexShader *vertex;
		ID3D11PixelShader *pixel;
		ID3D11InputLayout *layout;
		bool river;
		bool fog;
		bool fft;
		WaterRenderBridge::FeatureMask supportedFeatures;
		std::uint32_t vertexLayout;
		std::uint32_t vertexStride;
		std::uint32_t edgeCoordinateIndex;
		std::uint32_t edgeTransformFlags;
	};

	struct PreparedResources
	{
		WaterRenderBridge::PreparedPatch exact;
		SurfacePipeline *pipeline;
		ID3D11Buffer *vertices[WaterRenderBridge::MaxPreparedBatches];
		ID3D11Buffer *indices[WaterRenderBridge::MaxPreparedBatches];
		ID3D11Buffer *vertexConstants;
		ID3D11Buffer *pixelConstants;
		ID3D11Buffer *hullConstants[3];
		ID3D11ShaderResourceView *vertexResources[4];
		ID3D11SamplerState *vertexSamplers[4];
		ID3D11ShaderResourceView *pixelResources[16];
		ID3D11SamplerState *pixelSamplers[16];
		bool degraded;
		std::uint64_t fftGeneration;
		bool usesFft;
		bool fftEligible;
	};

	SurfacePipeline *Select_Pipeline(bool river, bool fog, std::uint32_t layout,
		std::uint32_t stride, bool fft = false);
	bool Create_Surface_Pipeline(SurfacePipeline & pipeline, bool river, bool fog,
		std::uint32_t layout, std::uint32_t stride, bool fft = false);
	bool Prepare_Fft(float raw_time);
	bool Create_Constant_Buffer(const void * data, std::size_t size, ID3D11Buffer * & buffer);
	bool Create_Geometry_Buffer(const void * data, std::size_t size, UINT bind,
		ID3D11Buffer * & buffer);
	ID3D11ShaderResourceView * Resolve_Resource(const WaterRenderPacket::Resource & resource);
	PreparedResources * Find_Prepared(const WaterRenderBridge::PreparedPatch & prepared);
	void Release_Prepared(PreparedResources * prepared);
	void Release_Prepared_All();
	bool Capture_Raw_View(WaterRenderBridge::ViewState & state, ID3D11RenderTargetView * & colour,
		ID3D11DepthStencilView * & depth, std::uint32_t & width, std::uint32_t & height,
		std::uint32_t & format, std::uint32_t & samples) const;

	DX11DeviceClass * Device;
	DX11BackendClass * Backend;
	SnapshotEntry Snapshots[SNAPSHOT_LIMIT];
	std::uint64_t DeviceGeneration;
	std::uint64_t PresentationEpoch;
	std::uint64_t UseSerial;
	std::uint64_t LastResourceGeneration;
	WaterRenderBridge::Capability Capability;
	RendererBootstrapState RendererState;
	TestFailure Failure;
	SurfacePipeline Pipelines[16];
	DX11WaterFft *Fft;
	bool FftEnabled;
	std::map<ID3D11VertexShader *, std::vector<unsigned char> > VertexBytecode;
	ID3D11BlendState *BlendState;
	ID3D11DepthStencilState *DepthState;
	ID3D11RasterizerState *RasterizerState;
	ID3D11SamplerState *LinearSampler;
	ID3D11SamplerState *PointSampler;
	ID3D11SamplerState *BedSampler;
	ID3D11Texture2D *FallbackWhiteSurface;
	ID3D11ShaderResourceView *FallbackWhite;
	ID3D11Texture2D *FallbackBlackSurface;
	ID3D11ShaderResourceView *FallbackBlack;
	ID3D11Texture2D *FallbackArraySurface;
	ID3D11ShaderResourceView *FallbackArray;
	std::vector<PreparedResources *> Prepared;
	std::map<const void *, ID3D11ShaderResourceView *> TestResources;
	const void *LastTracedReflectionIdentity;
	std::uint64_t LastTracedReflectionPatch;
	std::uint64_t LastTracedReflectionFrame;
	std::uint64_t LastTracedReflectionGeneration;
	std::uint64_t NextCaptureSerial;
	std::uint64_t DeviceLostFrame;
};

#endif
