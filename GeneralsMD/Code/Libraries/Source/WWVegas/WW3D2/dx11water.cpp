/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "dx11water.h"
#include "dx11waterfft.h"

#include "dx11backend.h"
#include "dx11device.h"
#include "dx11layout.h"
#include "dx11texture.h"
#include "engineshader.h"

#include "Platform/WaterGpuShaders.h"
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterHullVisual.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void water_init_trace(const char * phase, unsigned index)
{
	if (getenv("ZH_WATER_INIT_TRACE") == NULL) return;
	fprintf(stderr, "water-init %llu %s %u\n", static_cast<unsigned long long>(GetTickCount64()),
		phase, index);
	fflush(stderr);
}

template<class T> void release_object(T *& object)
{
	if (object != NULL) object->Release();
	object = NULL;
}

void trace_native_reflection(ID3D11Device * device, ID3D11DeviceContext * context,
	std::uint64_t patch_id, const void * engine_identity, ID3D11ShaderResourceView * view)
{
	if (getenv("ZH_WATER_NATIVE_RESOURCE_TRACE") == NULL || view == NULL) return;
	D3D11_SHADER_RESOURCE_VIEW_DESC view_description = {};
	view->GetDesc(&view_description);
	UINT mip = 0, array_slice = 0;
	if (view_description.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D) {
		mip = view_description.Texture2D.MostDetailedMip;
	} else if (view_description.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY) {
		mip = view_description.Texture2DArray.MostDetailedMip;
		array_slice = view_description.Texture2DArray.FirstArraySlice;
	} else {
		fprintf(stderr, "water-resource reflection patch=%llu engine=%p status=unsupported-view dimension=%u\n",
			static_cast<unsigned long long>(patch_id), engine_identity,
			static_cast<unsigned>(view_description.ViewDimension));
		fflush(stderr);
		return;
	}
	ID3D11Resource * resource = NULL;
	ID3D11Texture2D * texture = NULL;
	view->GetResource(&resource);
	if (resource == NULL || FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D),
			reinterpret_cast<void **>(&texture)))) {
		fprintf(stderr, "water-resource reflection patch=%llu engine=%p native=%p status=not-texture2d\n",
			static_cast<unsigned long long>(patch_id), engine_identity, resource);
		fflush(stderr);
		release_object(resource);
		return;
	}
	D3D11_TEXTURE2D_DESC source = {};
	texture->GetDesc(&source);
	if (source.SampleDesc.Count != 1 || mip >= source.MipLevels || array_slice >= source.ArraySize) {
		fprintf(stderr, "water-resource reflection patch=%llu engine=%p native=%p status=unsupported-subresource"
			" resourceWidth=%u resourceHeight=%u format=%u mip=%u slice=%u samples=%u\n",
			static_cast<unsigned long long>(patch_id), engine_identity, resource, source.Width, source.Height,
			static_cast<unsigned>(view_description.Format), mip, array_slice, source.SampleDesc.Count);
		fflush(stderr);
		release_object(texture); release_object(resource);
		return;
	}
	const UINT mip_width = source.Width >> mip, mip_height = source.Height >> mip;
	const UINT width = mip_width == 0 ? 1u : mip_width;
	const UINT height = mip_height == 0 ? 1u : mip_height;
	D3D11_TEXTURE2D_DESC staging_description = source;
	staging_description.Width = width; staging_description.Height = height;
	staging_description.MipLevels = staging_description.ArraySize = 1;
	staging_description.Usage = D3D11_USAGE_STAGING;
	staging_description.BindFlags = staging_description.MiscFlags = 0;
	staging_description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	ID3D11Texture2D * staging = NULL;
	HRESULT status = device->CreateTexture2D(&staging_description, NULL, &staging);
	if (SUCCEEDED(status)) {
		const UINT source_subresource = D3D11CalcSubresource(mip, array_slice, source.MipLevels);
		context->CopySubresourceRegion(staging, 0, 0, 0, 0, texture, source_subresource, NULL);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		status = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
		if (SUCCEEDED(status)) {
			const DXGI_FORMAT format = view_description.Format;
			const bool rgba = format == DXGI_FORMAT_R8G8B8A8_UNORM
				|| format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
			const bool bgra = format == DXGI_FORMAT_B8G8R8A8_UNORM
				|| format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
			const bool bgrx = format == DXGI_FORMAT_B8G8R8X8_UNORM
				|| format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
			if (rgba || bgra || bgrx) {
				std::uint64_t hash = 1469598103934665603ull;
				for (UINT y = 0; y < height; ++y) {
					const unsigned char * row = static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch;
					for (UINT index = 0; index < width * 4; ++index) { hash ^= row[index]; hash *= 1099511628211ull; }
				}
				auto sample = [mapped, rgba, bgrx](UINT x, UINT y) {
					const unsigned char * pixel = static_cast<const unsigned char *>(mapped.pData) + y * mapped.RowPitch + x * 4;
					const unsigned r = rgba ? pixel[0] : pixel[2];
					const unsigned g = pixel[1];
					const unsigned b = rgba ? pixel[2] : pixel[0];
					const unsigned a = bgrx ? 255u : pixel[3];
					return (r << 24) | (g << 16) | (b << 8) | a;
				};
				fprintf(stderr, "water-resource reflection patch=%llu engine=%p native=%p status=ready"
					" width=%u height=%u format=%u mip=%u slice=%u edge=%08x interior=%08x"
					" hash=fnv1a64:%016llx\n", static_cast<unsigned long long>(patch_id), engine_identity,
					resource, width, height, static_cast<unsigned>(format), mip, array_slice,
					sample(0, height / 2), sample(width / 2, height / 2),
					static_cast<unsigned long long>(hash));
			} else {
				fprintf(stderr, "water-resource reflection patch=%llu engine=%p native=%p status=unsupported-format"
					" width=%u height=%u format=%u mip=%u slice=%u rowPitch=%u\n",
					static_cast<unsigned long long>(patch_id), engine_identity, resource, width, height,
					static_cast<unsigned>(format), mip, array_slice, mapped.RowPitch);
			}
			fflush(stderr);
			context->Unmap(staging, 0);
		}
	}
	if (FAILED(status)) {
		fprintf(stderr, "water-resource reflection patch=%llu engine=%p native=%p status=readback-failed hr=%08x\n",
			static_cast<unsigned long long>(patch_id), engine_identity, resource, static_cast<unsigned>(status));
		fflush(stderr);
	}
	release_object(staging); release_object(texture); release_object(resource);
}

void multiply_matrix(const float * left, const float * right, float * result)
{
	float next[16] = {};
	for (unsigned row = 0; row < 4; ++row) {
		for (unsigned column = 0; column < 4; ++column) {
			for (unsigned index = 0; index < 4; ++index) {
				next[row * 4 + column] += left[row * 4 + index] * right[index * 4 + column];
			}
		}
	}
	std::memcpy(result, next, sizeof(next));
}

bool create_solid_texture(ID3D11Device * device, unsigned colour,
	ID3D11Texture2D *& texture, ID3D11ShaderResourceView *& view, unsigned layers = 1)
{
	D3D11_TEXTURE2D_DESC description = {};
	description.Width = description.Height = description.MipLevels = 1;
	description.ArraySize = layers;
	description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	description.SampleDesc.Count = 1;
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	std::vector<unsigned> pixels(layers, colour);
	std::vector<D3D11_SUBRESOURCE_DATA> data(layers);
	for (unsigned index = 0; index < layers; ++index) {
		data[index].pSysMem = &pixels[index];
		data[index].SysMemPitch = sizeof(unsigned);
	}
	if (FAILED(device->CreateTexture2D(&description, &data[0], &texture))) return false;
	D3D11_SHADER_RESOURCE_VIEW_DESC resource = {};
	resource.Format = description.Format;
	resource.ViewDimension = layers == 1 ? D3D11_SRV_DIMENSION_TEXTURE2D
		: D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	if (layers == 1) resource.Texture2D.MipLevels = 1;
	else {
		resource.Texture2DArray.MipLevels = 1;
		resource.Texture2DArray.ArraySize = layers;
	}
	if (FAILED(device->CreateShaderResourceView(texture, &resource, &view))) {
		release_object(texture);
		return false;
	}
	return true;
}

bool supports(ID3D11Device * device, DXGI_FORMAT format, UINT required)
{
	UINT support = 0;
	return SUCCEEDED(device->CheckFormatSupport(format, &support)) && (support & required) == required;
}

DXGI_FORMAT colour_srv_format(DXGI_FORMAT resource_format, DXGI_FORMAT view_format)
{
	if (view_format != DXGI_FORMAT_UNKNOWN) {
		return view_format;
	}
	switch (resource_format) {
	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
		return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	default:
		return resource_format;
	}
}

DXGI_FORMAT depth_srv_format(DXGI_FORMAT resource_format)
{
	switch (resource_format) {
	case DXGI_FORMAT_R24G8_TYPELESS:
		return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_R32_TYPELESS:
		return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_R16_TYPELESS:
		return DXGI_FORMAT_R16_UNORM;
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

bool render_subresource(const D3D11_RENDER_TARGET_VIEW_DESC & view,
	const D3D11_TEXTURE2D_DESC & texture, UINT & mip, UINT & slice)
{
	switch (view.ViewDimension) {
	case D3D11_RTV_DIMENSION_TEXTURE2D:
		mip = view.Texture2D.MipSlice;
		slice = 0;
		return true;
	case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
		mip = view.Texture2DArray.MipSlice;
		slice = view.Texture2DArray.FirstArraySlice;
		return view.Texture2DArray.ArraySize == 1;
	default:
		return false;
	}
}

bool depth_subresource(const D3D11_DEPTH_STENCIL_VIEW_DESC & view,
	const D3D11_TEXTURE2D_DESC & texture, UINT & mip, UINT & slice)
{
	switch (view.ViewDimension) {
	case D3D11_DSV_DIMENSION_TEXTURE2D:
		mip = view.Texture2D.MipSlice;
		slice = 0;
		return true;
	case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
		mip = view.Texture2DArray.MipSlice;
		slice = view.Texture2DArray.FirstArraySlice;
		return view.Texture2DArray.ArraySize == 1;
	default:
		return false;
	}
}

bool same_key(const DX11WaterClass::SnapshotView & left,
	const DX11WaterClass::SnapshotView & right)
{
	return left.colourIdentity == right.colourIdentity
		&& left.depthIdentity == right.depthIdentity
		&& left.colourSubresource == right.colourSubresource
		&& left.depthSubresource == right.depthSubresource
		&& left.colourFormat == right.colourFormat
		&& left.depthFormat == right.depthFormat
		&& left.width == right.width && left.height == right.height
		&& left.sampleCount == right.sampleCount
		&& left.deviceGeneration == right.deviceGeneration
		&& left.resourceGeneration == right.resourceGeneration
		&& left.frameId == right.frameId && left.captureSerial == right.captureSerial
		&& WaterRenderBridge::SameViewState(left.viewState, right.viewState);
}

std::uintptr_t next_snapshot_view_identity()
{
	static std::uintptr_t next = 0;
	if (++next == 0) ++next;
	return next;
}

struct SavedContext
{
	ID3D11RenderTargetView * targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
	ID3D11DepthStencilView * depth;
	ID3D11ShaderResourceView * vertexResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11ShaderResourceView * hullResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11ShaderResourceView * domainResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11ShaderResourceView * geometryResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11ShaderResourceView * pixelResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	ID3D11ShaderResourceView * computeResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
	D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
	UINT viewportCount;
	UINT scissorCount;

	SavedContext()
		: depth(NULL), viewportCount(D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE),
		  scissorCount(D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
	{
		std::memset(targets, 0, sizeof(targets));
		std::memset(vertexResources, 0, sizeof(vertexResources));
		std::memset(hullResources, 0, sizeof(hullResources));
		std::memset(domainResources, 0, sizeof(domainResources));
		std::memset(geometryResources, 0, sizeof(geometryResources));
		std::memset(pixelResources, 0, sizeof(pixelResources));
		std::memset(computeResources, 0, sizeof(computeResources));
		std::memset(viewports, 0, sizeof(viewports));
		std::memset(scissors, 0, sizeof(scissors));
	}

	void Capture(ID3D11DeviceContext * context)
	{
		context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, &depth);
		context->VSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, vertexResources);
		context->HSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, hullResources);
		context->DSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, domainResources);
		context->GSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, geometryResources);
		context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, pixelResources);
		context->CSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, computeResources);
		context->RSGetViewports(&viewportCount, viewports);
		context->RSGetScissorRects(&scissorCount, scissors);
	}

	void Detach(ID3D11DeviceContext * context)
	{
		ID3D11ShaderResourceView * empty[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
		context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, empty);
		context->OMSetRenderTargets(0, NULL, NULL);
	}

	void Restore(ID3D11DeviceContext * context)
	{
		context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, targets, depth);
		if (viewportCount != 0) {
			context->RSSetViewports(viewportCount, viewports);
		}
		if (scissorCount != 0) {
			context->RSSetScissorRects(scissorCount, scissors);
		}
		context->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, vertexResources);
		context->HSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, hullResources);
		context->DSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, domainResources);
		context->GSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, geometryResources);
		context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, pixelResources);
		context->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, computeResources);
	}

	void Release()
	{
		for (unsigned index = 0; index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++index) {
			if (targets[index] != NULL) targets[index]->Release();
		}
		if (depth != NULL) depth->Release();
		for (unsigned index = 0; index < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++index) {
			if (vertexResources[index] != NULL) vertexResources[index]->Release();
			if (hullResources[index] != NULL) hullResources[index]->Release();
			if (domainResources[index] != NULL) domainResources[index]->Release();
			if (geometryResources[index] != NULL) geometryResources[index]->Release();
			if (pixelResources[index] != NULL) pixelResources[index]->Release();
			if (computeResources[index] != NULL) computeResources[index]->Release();
		}
	}
};

struct SavedDrawState
{
	ID3D11InputLayout *layout;
	D3D11_PRIMITIVE_TOPOLOGY topology;
	ID3D11Buffer *vertexBuffer;
	UINT vertexStride, vertexOffset;
	ID3D11Buffer *indexBuffer;
	DXGI_FORMAT indexFormat;
	UINT indexOffset;
	ID3D11VertexShader *vertexShader;
	ID3D11HullShader *hullShader;
	ID3D11DomainShader *domainShader;
	ID3D11GeometryShader *geometryShader;
	ID3D11PixelShader *pixelShader;
	ID3D11ClassInstance *vertexClasses[D3D11_SHADER_MAX_INTERFACES];
	ID3D11ClassInstance *hullClasses[D3D11_SHADER_MAX_INTERFACES];
	ID3D11ClassInstance *domainClasses[D3D11_SHADER_MAX_INTERFACES];
	ID3D11ClassInstance *geometryClasses[D3D11_SHADER_MAX_INTERFACES];
	ID3D11ClassInstance *pixelClasses[D3D11_SHADER_MAX_INTERFACES];
	UINT vertexClassCount, hullClassCount, domainClassCount, geometryClassCount, pixelClassCount;
	ID3D11Buffer *vertexConstants[4];
	ID3D11Buffer *pixelConstants[4];
	ID3D11ShaderResourceView *vertexResources[16];
	ID3D11ShaderResourceView *pixelResources[16];
	ID3D11SamplerState *vertexSamplers[16];
	ID3D11SamplerState *pixelSamplers[16];
	ID3D11BlendState *blend;
	FLOAT blendFactor[4];
	UINT sampleMask;
	ID3D11DepthStencilState *depth;
	UINT stencilReference;
	ID3D11RasterizerState *rasterizer;

	SavedDrawState() { std::memset(this, 0, sizeof(*this)); }
	void Capture(ID3D11DeviceContext * context)
	{
		context->IAGetInputLayout(&layout);
		context->IAGetPrimitiveTopology(&topology);
		UINT count = 1;
		context->IAGetVertexBuffers(0, count, &vertexBuffer, &vertexStride, &vertexOffset);
		context->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
		vertexClassCount = hullClassCount = domainClassCount = geometryClassCount = pixelClassCount =
			D3D11_SHADER_MAX_INTERFACES;
		context->VSGetShader(&vertexShader, vertexClasses, &vertexClassCount);
		context->HSGetShader(&hullShader, hullClasses, &hullClassCount);
		context->DSGetShader(&domainShader, domainClasses, &domainClassCount);
		context->GSGetShader(&geometryShader, geometryClasses, &geometryClassCount);
		context->PSGetShader(&pixelShader, pixelClasses, &pixelClassCount);
		context->VSGetConstantBuffers(0, 4, vertexConstants);
		context->PSGetConstantBuffers(0, 4, pixelConstants);
		context->VSGetShaderResources(0, 16, vertexResources);
		context->PSGetShaderResources(0, 16, pixelResources);
		context->VSGetSamplers(0, 16, vertexSamplers);
		context->PSGetSamplers(0, 16, pixelSamplers);
		context->OMGetBlendState(&blend, blendFactor, &sampleMask);
		context->OMGetDepthStencilState(&depth, &stencilReference);
		context->RSGetState(&rasterizer);
	}
	void Restore(ID3D11DeviceContext * context)
	{
		context->IASetInputLayout(layout);
		context->IASetPrimitiveTopology(topology);
		context->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
		context->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
		context->VSSetShader(vertexShader, vertexClasses, vertexClassCount);
		context->HSSetShader(hullShader, hullClasses, hullClassCount);
		context->DSSetShader(domainShader, domainClasses, domainClassCount);
		context->GSSetShader(geometryShader, geometryClasses, geometryClassCount);
		context->PSSetShader(pixelShader, pixelClasses, pixelClassCount);
		context->VSSetConstantBuffers(0, 4, vertexConstants);
		context->PSSetConstantBuffers(0, 4, pixelConstants);
		context->VSSetShaderResources(0, 16, vertexResources);
		context->PSSetShaderResources(0, 16, pixelResources);
		context->VSSetSamplers(0, 16, vertexSamplers);
		context->PSSetSamplers(0, 16, pixelSamplers);
		context->OMSetBlendState(blend, blendFactor, sampleMask);
		context->OMSetDepthStencilState(depth, stencilReference);
		context->RSSetState(rasterizer);
	}
	void Release()
	{
		release_object(layout); release_object(vertexBuffer); release_object(indexBuffer);
		release_object(vertexShader); release_object(hullShader); release_object(domainShader);
		release_object(geometryShader); release_object(pixelShader);
		for (unsigned index = 0; index < vertexClassCount; ++index) release_object(vertexClasses[index]);
		for (unsigned index = 0; index < hullClassCount; ++index) release_object(hullClasses[index]);
		for (unsigned index = 0; index < domainClassCount; ++index) release_object(domainClasses[index]);
		for (unsigned index = 0; index < geometryClassCount; ++index) release_object(geometryClasses[index]);
		for (unsigned index = 0; index < pixelClassCount; ++index) release_object(pixelClasses[index]);
		for (unsigned index = 0; index < 4; ++index) {
			release_object(vertexConstants[index]); release_object(pixelConstants[index]);
		}
		for (unsigned index = 0; index < 16; ++index) {
			release_object(vertexResources[index]); release_object(pixelResources[index]);
			release_object(vertexSamplers[index]); release_object(pixelSamplers[index]);
		}
		release_object(blend); release_object(depth); release_object(rasterizer);
	}
};

}

DX11WaterClass::DX11WaterClass()
	: Device(NULL), Backend(NULL), DeviceGeneration(0), PresentationEpoch(1), UseSerial(0),
	  LastResourceGeneration(0),
	  RendererState(RENDERER_UNINITIALIZED), Failure(TEST_FAILURE_NONE),
	  Fft(new DX11WaterFft), FftEnabled(WaterFftSampling::Enabled()),
	  BlendState(NULL), DepthState(NULL), RasterizerState(NULL),
	  LinearSampler(NULL), PointSampler(NULL), BedSampler(NULL), FallbackWhiteSurface(NULL),
	  FallbackWhite(NULL), FallbackBlackSurface(NULL), FallbackBlack(NULL),
	  FallbackArraySurface(NULL), FallbackArray(NULL), LastTracedReflectionIdentity(NULL),
	  LastTracedReflectionPatch(0), LastTracedReflectionFrame(0),
	  LastTracedReflectionGeneration(0), NextCaptureSerial(0), DeviceLostFrame(0)
{
	std::memset(Snapshots, 0, sizeof(Snapshots));
	std::memset(Pipelines, 0, sizeof(Pipelines));
	Capability.backend = WaterRenderBridge::BackendKind::Unavailable;
	Capability.features = 0;
	Capability.reason = WaterRenderBridge::CapabilityReason::NoDevice;
}

DX11WaterClass::~DX11WaterClass()
{
	Shutdown();
	delete Fft;
}

bool DX11WaterClass::Initialise(DX11DeviceClass * device, DX11BackendClass * backend)
{
	Shutdown();
	Device = device;
	Backend = backend;
	static std::uint64_t next_device_generation = 0;
	DeviceGeneration = ++next_device_generation;
	PresentationEpoch = 1;
	UseSerial = 0;
	LastResourceGeneration = 0;
	RendererState = RENDERER_UNINITIALIZED;
	Failure = TEST_FAILURE_NONE;
	Capability.backend = WaterRenderBridge::BackendKind::NativeD3D11;
	Capability.features = WaterRenderBridge::Feature::CoreRenderer | WaterRenderBridge::Feature::Device;
	Capability.reason = WaterRenderBridge::CapabilityReason::CoreShaderUnavailable;
	water_init_trace("probe-begin", 0);
	if (!Probe_Formats()) return false;
	water_init_trace("probe-end", 0);
	return true;
}

void DX11WaterClass::Shutdown()
{
	Release_Renderer();
	Fft->Reset();
	RendererState = RENDERER_UNINITIALIZED;
	Release_All();
	Device = NULL;
	Backend = NULL;
	Capability.backend = WaterRenderBridge::BackendKind::Unavailable;
	Capability.features = 0;
	Capability.reason = WaterRenderBridge::CapabilityReason::NoDevice;
}

bool DX11WaterClass::Create_Surface_Pipeline(SurfacePipeline & pipeline, bool river, bool fog,
	std::uint32_t layout, std::uint32_t stride, bool fft)
{
	if (Failure == TEST_FAILURE_SHADER || Backend == NULL) return false;
	VertexPipelineDescription vertex_description = {};
	vertex_description.FVF = layout;
	vertex_description.StageCount = 2;
	vertex_description.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU | 0;
	vertex_description.Stages[1].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU | 1;
	vertex_description.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	vertex_description.Stages[1].TextureTransformFlags = D3DTTFF_DISABLE;
	if (!river) {
		vertex_description.StageCount = 4;
		vertex_description.Stages[2] = vertex_description.Stages[1];
		vertex_description.Stages[3].TextureCoordinateIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
		vertex_description.Stages[3].TextureTransformFlags = D3DTTFF_COUNT2;
	}
	vertex_description.FogEnabled = fog;
	vertex_description.FogVertexMode = fog ? D3DFOG_LINEAR : D3DFOG_NONE;
	std::string base_vertex;
	if (!VertexShader_Generate(vertex_description, VERTEX_SHADER_TARGET_D3D11, base_vertex)) return false;

	WaterGpuShaders::Variant variant;
	variant.target = WaterGpuShaders::Target::NativeD3D11;
	variant.surface = river ? WaterGpuShaders::Surface::River : WaterGpuShaders::Surface::Flat;
	variant.vertex = fft ? WaterGpuShaders::Vertex::Fft : WaterGpuShaders::Vertex::Analytic;
	static const bool mesh_contact_enabled = WaterHullVisual::Enabled();
	variant.meshContact = mesh_contact_enabled;
	variant.coastalSurf = false;
	variant.coastalCrest = false;
	static const WaterGpuShaders::EnvironmentSnapshot water_policy =
		WaterGpuShaders::SnapshotEnvironment(WaterGpuShaders::Target::NativeD3D11, layout);
	variant.shoreFoam = water_policy.flat.shoreFoam;
	variant.bowLift = water_policy.flat.bowLift;
	variant.bowShoulder = water_policy.flat.bowShoulder;
	variant.materialFvf = layout;
	PixelPipelineDescription pixel_description = {};
	pixel_description.AlphaFunction = D3DCMP_ALWAYS;
	pixel_description.FogEnabled = fog;
	std::string vertex_hlsl;
	std::string pixel_hlsl;
	if (!WaterGpuShaders::GenerateVertex(variant, base_vertex, vertex_hlsl)
			|| !WaterGpuShaders::GenerateSurfacePixel(variant, pixel_description, pixel_hlsl)) return false;
	std::vector<unsigned char> vertex_code;
	std::vector<unsigned char> pixel_code;
	water_init_trace("compile-vs-begin", layout);
	if (!Backend->Compile_Program(vertex_hlsl, "water-surface", "vs_5_0", vertex_code)) return false;
	water_init_trace("compile-vs-end", layout);
	water_init_trace("compile-ps-begin", layout);
	if (!Backend->Compile_Program(pixel_hlsl, "water-surface", "ps_5_0", pixel_code)) return false;
	water_init_trace("compile-ps-end", layout);
	D3D11_INPUT_ELEMENT_DESC elements[MAXIMUM_LAYOUT_ELEMENTS];
	unsigned element_count = 0;
	unsigned generated_stride = 0;
	if (!DX11Layout_From_FVF(layout, elements, element_count, generated_stride)
			|| generated_stride != stride) return false;
	ID3D11Device * device = Device->Get_Device();
	water_init_trace("create-shaders-begin", layout);
	if (FAILED(device->CreateVertexShader(&vertex_code[0], vertex_code.size(), NULL, &pipeline.vertex))
			|| FAILED(device->CreatePixelShader(&pixel_code[0], pixel_code.size(), NULL, &pipeline.pixel))
			|| FAILED(device->CreateInputLayout(elements, element_count, &vertex_code[0],
				vertex_code.size(), &pipeline.layout))) {
		release_object(pipeline.layout);
		release_object(pipeline.pixel);
		release_object(pipeline.vertex);
		return false;
	}
	water_init_trace("create-shaders-end", layout);
	pipeline.river = river;
	pipeline.fog = fog;
	pipeline.fft = fft;
	VertexBytecode[pipeline.vertex] = vertex_code;
	pipeline.vertexLayout = layout;
	pipeline.vertexStride = stride;
	pipeline.edgeCoordinateIndex = river ? 0u : D3DTSS_TCI_CAMERASPACEPOSITION;
	pipeline.edgeTransformFlags = river ? D3DTTFF_DISABLE : D3DTTFF_COUNT2;
	pipeline.supportedFeatures = 0;
	if (!river) {
		using WaterRenderBridge::Feature;
		pipeline.supportedFeatures = WaterRenderBridge::Mask(Feature::FoamHistory);
		if (variant.meshContact) pipeline.supportedFeatures |= WaterRenderBridge::Mask(Feature::MeshContact);
		if (variant.shoreFoam) pipeline.supportedFeatures |= Feature::ShoreHistory | Feature::ShoreFoam;
		if (fft) {
			pipeline.supportedFeatures |= WaterRenderBridge::Mask(Feature::WaveCages);
			if (variant.bowLift && variant.bowShoulder)
				pipeline.supportedFeatures |= WaterRenderBridge::Mask(Feature::VesselWaves);
		}
	}
	return true;
}

bool DX11WaterClass::Initialise_Renderer()
{
	Release_Renderer();
	if (Device == NULL || Backend == NULL || Device->Get_Device() == NULL
			|| Device->Get_Feature_Level() < D3D_FEATURE_LEVEL_11_0) return false;
	water_init_trace("pipeline-begin", 0);
	if (!Create_Surface_Pipeline(Pipelines[0], false, false, 0x242u, 32u)) {
		Capability.reason = WaterRenderBridge::CapabilityReason::CoreShaderUnavailable;
		Release_Renderer();
		return false;
	}
	water_init_trace("pipeline-end", 0);
	ID3D11Device * device = Device->Get_Device();
	D3D11_BLEND_DESC blend = {};
	blend.RenderTarget[0].BlendEnable = TRUE;
	blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	D3D11_DEPTH_STENCIL_DESC depth = {};
	depth.DepthEnable = TRUE;
	depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	D3D11_RASTERIZER_DESC rasterizer = {};
	rasterizer.FillMode = D3D11_FILL_SOLID;
	rasterizer.CullMode = D3D11_CULL_NONE;
	rasterizer.DepthClipEnable = TRUE;
	D3D11_SAMPLER_DESC sampler = {};
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler.MaxLOD = D3D11_FLOAT32_MAX;
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	const bool ordinary = SUCCEEDED(device->CreateBlendState(&blend, &BlendState))
		&& SUCCEEDED(device->CreateDepthStencilState(&depth, &DepthState))
		&& SUCCEEDED(device->CreateRasterizerState(&rasterizer, &RasterizerState))
		&& SUCCEEDED(device->CreateSamplerState(&sampler, &LinearSampler));
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	const bool point = ordinary && SUCCEEDED(device->CreateSamplerState(&sampler, &PointSampler));
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	const bool bed = point && SUCCEEDED(device->CreateSamplerState(&sampler, &BedSampler));
	if (!bed || !create_solid_texture(device, 0xffffffffu, FallbackWhiteSurface, FallbackWhite)
			|| !create_solid_texture(device, 0x00000000u, FallbackBlackSurface, FallbackBlack)
			|| !create_solid_texture(device, 0x00000000u, FallbackArraySurface, FallbackArray, 6)) {
		Capability.reason = WaterRenderBridge::CapabilityReason::LayoutUnavailable;
		Release_Renderer();
		return false;
	}
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::CoreAnalyticShader);
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::WaterLayout);
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::Optical);
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::Bed);
	Capability.reason = WaterRenderBridge::CapabilityReason::None;
	return true;
}

bool DX11WaterClass::Ensure_Renderer()
{
	if (RendererState == RENDERER_READY) return true;
	if (RendererState == RENDERER_INITIALIZING || RendererState == RENDERER_FAILED) return false;
	RendererState = RENDERER_INITIALIZING;
	const bool ready = Initialise_Renderer();
	RendererState = ready ? RENDERER_READY : RENDERER_FAILED;
	return ready;
}

void DX11WaterClass::Release_Renderer()
{
	Fft->Release();
	Fft->Fields(NULL, NULL, NULL, NULL);
	VertexBytecode.clear();
	Release_Prepared_All();
	Clear_Test_Resources();
	LastTracedReflectionIdentity = NULL;
	LastTracedReflectionPatch = 0;
	LastTracedReflectionFrame = 0;
	LastTracedReflectionGeneration = 0;
	for (unsigned index = 0; index < 16; ++index) {
		Capability.features &= ~Pipelines[index].supportedFeatures;
		Pipelines[index].supportedFeatures = 0;
		release_object(Pipelines[index].layout);
		release_object(Pipelines[index].pixel);
		release_object(Pipelines[index].vertex);
	}
	release_object(BlendState);
	release_object(DepthState);
	release_object(RasterizerState);
	release_object(LinearSampler);
	release_object(PointSampler);
	release_object(BedSampler);
	release_object(FallbackWhite);
	release_object(FallbackWhiteSurface);
	release_object(FallbackBlack);
	release_object(FallbackBlackSurface);
	release_object(FallbackArray);
	release_object(FallbackArraySurface);
}

bool DX11WaterClass::Probe_Formats()
{
	if (Device == NULL || Device->Get_Device() == NULL) {
		Capability.reason = WaterRenderBridge::CapabilityReason::NoDevice;
		return false;
	}
	if (Device->Get_Feature_Level() < D3D_FEATURE_LEVEL_11_0) {
		Capability.reason = WaterRenderBridge::CapabilityReason::FeatureLevelUnsupported;
		return true;
	}
	ID3D11Device * device = Device->Get_Device();
	const UINT colour_required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_RENDER_TARGET
		| D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
	if (!supports(device, DXGI_FORMAT_R16G16B16A16_FLOAT, colour_required)) {
		Capability.reason = WaterRenderBridge::CapabilityReason::ColourFormatUnsupported;
		return true;
	}
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::SceneColourFormat);
	if (!supports(device, DXGI_FORMAT_R24G8_TYPELESS, D3D11_FORMAT_SUPPORT_TEXTURE2D)
			|| !supports(device, DXGI_FORMAT_D24_UNORM_S8_UINT,
				D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
			|| !supports(device, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
				D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
		Capability.reason = WaterRenderBridge::CapabilityReason::DepthFormatUnsupported;
		return true;
	}
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::SceneDepthFormat);
	Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::SceneSnapshots);
	Capability.reason = RendererState == RENDERER_READY ? WaterRenderBridge::CapabilityReason::None
		: WaterRenderBridge::CapabilityReason::CoreShaderUnavailable;
	return true;
}

void DX11WaterClass::Before_Resize()
{
	Release_Prepared_All();
	Release_All();
}

void DX11WaterClass::After_Resize()
{
	Probe_Formats();
}

bool DX11WaterClass::Resolve_Key(std::uint64_t frame_id, std::uint64_t resource_generation,
	std::uint64_t capture_serial, const WaterRenderBridge::ViewState & view_state,
	ID3D11RenderTargetView * & colour, ID3D11DepthStencilView * & depth,
	SnapshotKey & key, ID3D11Texture2D * & colour_surface, ID3D11Texture2D * & depth_surface,
	DXGI_FORMAT & colour_view_format, DXGI_FORMAT & depth_view_format,
	SnapshotStatus & failure) const
{
	colour_surface = NULL;
	depth_surface = NULL;
	if (Device == NULL || Device->Get_Device() == NULL) {
		failure = SNAPSHOT_NO_DEVICE;
		return false;
	}
	if (colour == NULL) colour = Backend != NULL
		? Backend->Current_Render_Target() : Device->Get_Scene_View();
	if (depth == NULL) depth = Backend != NULL
		? Backend->Current_Depth_Target() : Device->Get_Depth_Stencil_View();
	if (colour == NULL || depth == NULL || !frame_id || !resource_generation || !capture_serial
			|| !WaterRenderBridge::ValidViewState(view_state)) {
		failure = SNAPSHOT_MISSING_TARGET;
		return false;
	}

	ID3D11Resource * colour_resource = NULL;
	ID3D11Resource * depth_resource = NULL;
	colour->GetResource(&colour_resource);
	depth->GetResource(&depth_resource);
	if (colour_resource == NULL || depth_resource == NULL
			|| FAILED(colour_resource->QueryInterface(__uuidof(ID3D11Texture2D),
				reinterpret_cast<void **>(&colour_surface)))
			|| FAILED(depth_resource->QueryInterface(__uuidof(ID3D11Texture2D),
				reinterpret_cast<void **>(&depth_surface)))) {
		if (colour_resource != NULL) colour_resource->Release();
		if (depth_resource != NULL) depth_resource->Release();
		if (colour_surface != NULL) colour_surface->Release();
		if (depth_surface != NULL) depth_surface->Release();
		colour_surface = NULL;
		depth_surface = NULL;
		failure = SNAPSHOT_UNSUPPORTED_RESOURCE;
		return false;
	}
	colour_resource->Release();
	depth_resource->Release();

	D3D11_TEXTURE2D_DESC colour_description;
	D3D11_TEXTURE2D_DESC depth_description;
	D3D11_RENDER_TARGET_VIEW_DESC colour_view;
	D3D11_DEPTH_STENCIL_VIEW_DESC depth_view;
	colour_surface->GetDesc(&colour_description);
	depth_surface->GetDesc(&depth_description);
	colour->GetDesc(&colour_view);
	depth->GetDesc(&depth_view);
	if (colour_description.SampleDesc.Count != 1 || depth_description.SampleDesc.Count != 1) {
		colour_surface->Release();
		depth_surface->Release();
		colour_surface = NULL;
		depth_surface = NULL;
		failure = SNAPSHOT_MULTISAMPLED;
		return false;
	}

	UINT colour_mip = 0, colour_slice = 0, depth_mip = 0, depth_slice = 0;
	if (!render_subresource(colour_view, colour_description, colour_mip, colour_slice)
			|| !depth_subresource(depth_view, depth_description, depth_mip, depth_slice)
			|| colour_mip >= colour_description.MipLevels || depth_mip >= depth_description.MipLevels
			|| colour_slice >= colour_description.ArraySize || depth_slice >= depth_description.ArraySize) {
		colour_surface->Release();
		depth_surface->Release();
		colour_surface = NULL;
		depth_surface = NULL;
		failure = SNAPSHOT_UNSUPPORTED_RESOURCE;
		return false;
	}

	const UINT colour_width = (std::max)(1u, colour_description.Width >> colour_mip);
	const UINT colour_height = (std::max)(1u, colour_description.Height >> colour_mip);
	const UINT depth_width = (std::max)(1u, depth_description.Width >> depth_mip);
	const UINT depth_height = (std::max)(1u, depth_description.Height >> depth_mip);
	if (colour_width != depth_width || colour_height != depth_height) {
		colour_surface->Release();
		depth_surface->Release();
		colour_surface = NULL;
		depth_surface = NULL;
		failure = SNAPSHOT_SIZE_MISMATCH;
		return false;
	}
	colour_view_format = colour_srv_format(colour_description.Format, colour_view.Format);
	depth_view_format = depth_srv_format(depth_description.Format);
	if (depth_view_format == DXGI_FORMAT_UNKNOWN
			|| !supports(Device->Get_Device(), colour_view_format,
				D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)
			|| !supports(Device->Get_Device(), depth_view_format,
				D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) {
		colour_surface->Release();
		depth_surface->Release();
		colour_surface = NULL;
		depth_surface = NULL;
		failure = SNAPSHOT_FORMAT_UNSUPPORTED;
		return false;
	}

	std::memset(&key, 0, sizeof(key));
	key.colourIdentity = reinterpret_cast<std::uintptr_t>(colour_surface);
	key.depthIdentity = reinterpret_cast<std::uintptr_t>(depth_surface);
	key.colourSubresource = D3D11CalcSubresource(colour_mip, colour_slice, colour_description.MipLevels);
	key.depthSubresource = D3D11CalcSubresource(depth_mip, depth_slice, depth_description.MipLevels);
	key.colourFormat = static_cast<std::uint32_t>(colour_description.Format);
	key.depthFormat = static_cast<std::uint32_t>(depth_description.Format);
	key.width = colour_width;
	key.height = colour_height;
	key.sampleCount = 1;
	key.deviceGeneration = DeviceGeneration;
	key.resourceGeneration = resource_generation;
	key.frameId = frame_id;
	key.captureSerial = capture_serial;
	key.viewState = view_state;
	failure = SNAPSHOT_READY;
	return true;
}

int DX11WaterClass::Find(const SnapshotKey & key)
{
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) {
		const SnapshotEntry & entry = Snapshots[index];
		if (!entry.valid) continue;
		SnapshotView left = {};
		SnapshotView right = {};
		Publish(entry, left);
		right.colourIdentity = key.colourIdentity;
		right.depthIdentity = key.depthIdentity;
		right.colourSubresource = key.colourSubresource;
		right.depthSubresource = key.depthSubresource;
		right.colourFormat = key.colourFormat;
		right.depthFormat = key.depthFormat;
		right.width = key.width;
		right.height = key.height;
		right.sampleCount = key.sampleCount;
		right.deviceGeneration = key.deviceGeneration;
		right.resourceGeneration = key.resourceGeneration;
		right.frameId = key.frameId;
		right.captureSerial = key.captureSerial;
		right.viewState = key.viewState;
		if (same_key(left, right)) return static_cast<int>(index);
	}
	return -1;
}

void DX11WaterClass::Publish(const SnapshotEntry & entry, SnapshotView & snapshot) const
{
	std::memset(&snapshot, 0, sizeof(snapshot));
	snapshot.colour = entry.colourView;
	snapshot.depth = entry.depthView;
	snapshot.colourIdentity = entry.key.colourIdentity;
	snapshot.depthIdentity = entry.key.depthIdentity;
	snapshot.viewIdentity = entry.viewIdentity;
	snapshot.colourSubresource = entry.key.colourSubresource;
	snapshot.depthSubresource = entry.key.depthSubresource;
	snapshot.colourFormat = entry.key.colourFormat;
	snapshot.depthFormat = entry.key.depthFormat;
	snapshot.width = entry.key.width;
	snapshot.height = entry.key.height;
	snapshot.sampleCount = entry.key.sampleCount;
	snapshot.deviceGeneration = entry.key.deviceGeneration;
	snapshot.resourceGeneration = entry.key.resourceGeneration;
	snapshot.frameId = entry.key.frameId;
	snapshot.captureSerial = entry.key.captureSerial;
	snapshot.viewState = entry.key.viewState;
}

DX11WaterClass::SnapshotStatus DX11WaterClass::Find_Scene(std::uint64_t frame_id,
	std::uint64_t resource_generation, std::uint64_t capture_serial,
	const WaterRenderBridge::ViewState & view_state, ID3D11RenderTargetView * colour,
	ID3D11DepthStencilView * depth, SnapshotView & snapshot)
{
	SnapshotKey key;
	ID3D11Texture2D * colour_surface = NULL;
	ID3D11Texture2D * depth_surface = NULL;
	DXGI_FORMAT colour_format = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
	SnapshotStatus failure;
	if (!Resolve_Key(frame_id, resource_generation, capture_serial, view_state, colour, depth,
			key, colour_surface, depth_surface, colour_format, depth_format, failure)) {
		return failure;
	}
	colour_surface->Release();
	depth_surface->Release();
	const int found = Find(key);
	if (found < 0) return SNAPSHOT_MISSING_TARGET;
	Snapshots[found].used = ++UseSerial;
	Publish(Snapshots[found], snapshot);
	return SNAPSHOT_REUSED;
}

DX11WaterClass::SnapshotStatus DX11WaterClass::Capture_Scene(std::uint64_t frame_id,
	std::uint64_t resource_generation, std::uint64_t capture_serial,
	const WaterRenderBridge::ViewState & view_state, ID3D11RenderTargetView * colour,
	ID3D11DepthStencilView * depth, SnapshotView & snapshot)
{
	SnapshotKey key;
	ID3D11Texture2D * colour_source = NULL;
	ID3D11Texture2D * depth_source = NULL;
	DXGI_FORMAT colour_format = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
	SnapshotStatus failure;
	if (!Resolve_Key(frame_id, resource_generation, capture_serial, view_state, colour, depth,
			key, colour_source, depth_source, colour_format, depth_format, failure)) {
		return failure;
	}

	const int found = Find(key);
	if (found >= 0) {
		colour_source->Release();
		depth_source->Release();
		Snapshots[found].used = ++UseSerial;
		Publish(Snapshots[found], snapshot);
		return SNAPSHOT_REUSED;
	}

	SnapshotEntry replacement = {};
	replacement.key = key;
	D3D11_TEXTURE2D_DESC colour_description;
	colour_source->GetDesc(&colour_description);
	colour_description.Width = key.width;
	colour_description.Height = key.height;
	colour_description.MipLevels = 1;
	colour_description.ArraySize = 1;
	colour_description.SampleDesc.Count = 1;
	colour_description.SampleDesc.Quality = 0;
	colour_description.Usage = D3D11_USAGE_DEFAULT;
	colour_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	colour_description.CPUAccessFlags = 0;
	colour_description.MiscFlags = 0;
	if (Failure == TEST_FAILURE_COLOUR_ALLOCATION
			|| FAILED(Device->Get_Device()->CreateTexture2D(&colour_description, NULL,
				&replacement.colourSurface))) {
		colour_source->Release();
		depth_source->Release();
		return SNAPSHOT_ALLOCATION_FAILED;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC colour_view = {};
	colour_view.Format = colour_format;
	colour_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	colour_view.Texture2D.MipLevels = 1;
	if (FAILED(Device->Get_Device()->CreateShaderResourceView(replacement.colourSurface,
			&colour_view, &replacement.colourView))) {
		colour_source->Release();
		depth_source->Release();
		Release(replacement);
		return SNAPSHOT_ALLOCATION_FAILED;
	}

	D3D11_TEXTURE2D_DESC depth_description;
	depth_source->GetDesc(&depth_description);
	depth_description.Width = key.width;
	depth_description.Height = key.height;
	depth_description.MipLevels = 1;
	depth_description.ArraySize = 1;
	depth_description.SampleDesc.Count = 1;
	depth_description.SampleDesc.Quality = 0;
	depth_description.Usage = D3D11_USAGE_DEFAULT;
	depth_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	depth_description.CPUAccessFlags = 0;
	depth_description.MiscFlags = 0;
	if (Failure == TEST_FAILURE_DEPTH_ALLOCATION
			|| FAILED(Device->Get_Device()->CreateTexture2D(&depth_description, NULL,
				&replacement.depthSurface))) {
		colour_source->Release();
		depth_source->Release();
		Release(replacement);
		return SNAPSHOT_ALLOCATION_FAILED;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC depth_view = {};
	depth_view.Format = depth_format;
	depth_view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	depth_view.Texture2D.MipLevels = 1;
	if (FAILED(Device->Get_Device()->CreateShaderResourceView(replacement.depthSurface,
			&depth_view, &replacement.depthView))) {
		colour_source->Release();
		depth_source->Release();
		Release(replacement);
		return SNAPSHOT_ALLOCATION_FAILED;
	}
	replacement.colourSource = colour_source;
	replacement.depthSource = depth_source;
	colour_source = NULL;
	depth_source = NULL;
	replacement.viewIdentity = next_snapshot_view_identity();
	replacement.presentationEpoch = PresentationEpoch;

	SavedContext saved;
	ID3D11DeviceContext * context = Device->Get_Context();
	saved.Capture(context);
	saved.Detach(context);
	context->CopySubresourceRegion(replacement.colourSurface, 0, 0, 0, 0,
		replacement.colourSource, key.colourSubresource, NULL);
	context->CopySubresourceRegion(replacement.depthSurface, 0, 0, 0, 0,
		replacement.depthSource, key.depthSubresource, NULL);
	saved.Restore(context);
	saved.Release();
	if (Backend != NULL) Backend->Forget_Bindings();
	if (FAILED(Device->Device_Removed_Reason())) {
		Release(replacement);
		return SNAPSHOT_DEVICE_LOST;
	}

	unsigned slot = SNAPSHOT_LIMIT;
	std::uint64_t oldest = ~static_cast<std::uint64_t>(0);
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) {
		if (!Snapshots[index].valid) {
			slot = index;
			break;
		}
		if (Snapshots[index].used < oldest) {
			oldest = Snapshots[index].used;
			slot = index;
		}
	}
	replacement.valid = true;
	replacement.used = ++UseSerial;
	Release(Snapshots[slot]);
	Snapshots[slot] = replacement;
	LastResourceGeneration = resource_generation;
	Publish(Snapshots[slot], snapshot);
	return SNAPSHOT_READY;
}

void DX11WaterClass::Release(SnapshotEntry & entry)
{
	if (entry.colourView != NULL) entry.colourView->Release();
	if (entry.colourSurface != NULL) entry.colourSurface->Release();
	if (entry.colourSource != NULL) entry.colourSource->Release();
	if (entry.depthView != NULL) entry.depthView->Release();
	if (entry.depthSurface != NULL) entry.depthSurface->Release();
	if (entry.depthSource != NULL) entry.depthSource->Release();
	std::memset(&entry, 0, sizeof(entry));
}

void DX11WaterClass::Release_All()
{
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) Release(Snapshots[index]);
}

void DX11WaterClass::Invalidate_Source(ID3D11Resource * resource)
{
	const std::uintptr_t identity = reinterpret_cast<std::uintptr_t>(resource);
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) {
		if (Snapshots[index].valid && (Snapshots[index].key.colourIdentity == identity
				|| Snapshots[index].key.depthIdentity == identity)) {
			Release(Snapshots[index]);
		}
	}
}

unsigned DX11WaterClass::Snapshot_Count() const
{
	unsigned count = 0;
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) {
		if (Snapshots[index].valid) ++count;
	}
	return count;
}

WaterRenderBridge::Capability DX11WaterClass::GetCapability() const
{
	const_cast<DX11WaterClass *>(this)->Ensure_Renderer();
	return Capability;
}

void DX11WaterClass::Advance_Presented_Frame()
{
	if (++PresentationEpoch == 0) ++PresentationEpoch;
}

bool DX11WaterClass::QueryCurrentView(WaterRenderBridge::CurrentView & current) const
{
	std::memset(&current, 0, sizeof(current));
	if (Device == NULL || Backend == NULL || FAILED(Device->Device_Removed_Reason())) return false;

	WaterRenderBridge::ViewState view_state = {};
	Backend->Current_View_Projection(view_state.view, view_state.projection);
	D3D11_VIEWPORT viewport = {};
	UINT viewport_count = 1;
	Device->Get_Context()->RSGetViewports(&viewport_count, &viewport);
	if (viewport_count != 1) return false;
	view_state.viewport.x = viewport.TopLeftX;
	view_state.viewport.y = viewport.TopLeftY;
	view_state.viewport.width = viewport.Width;
	view_state.viewport.height = viewport.Height;
	view_state.viewport.minDepth = viewport.MinDepth;
	view_state.viewport.maxDepth = viewport.MaxDepth;
	if (!WaterRenderBridge::ValidViewState(view_state)) return false;

	ID3D11RenderTargetView * colour = NULL;
	ID3D11DepthStencilView * depth = NULL;
	Device->Get_Context()->OMGetRenderTargets(1, &colour, &depth);
	if (colour == NULL || depth == NULL) {
		if (colour != NULL) colour->Release();
		if (depth != NULL) depth->Release();
		return false;
	}
	SnapshotKey live;
	ID3D11Texture2D * colour_surface = NULL;
	ID3D11Texture2D * depth_surface = NULL;
	DXGI_FORMAT colour_format = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT depth_format = DXGI_FORMAT_UNKNOWN;
	SnapshotStatus failure = SNAPSHOT_MISSING_TARGET;
	const bool resolved = Resolve_Key(1, 1, 1, view_state, colour, depth, live,
		colour_surface, depth_surface, colour_format, depth_format, failure);
	colour->Release();
	depth->Release();
	if (!resolved) return false;
	colour_surface->Release();
	depth_surface->Release();

	const SnapshotEntry * found = NULL;
	for (unsigned index = 0; index < SNAPSHOT_LIMIT; ++index) {
		const SnapshotEntry & entry = Snapshots[index];
		if (!entry.valid || entry.presentationEpoch != PresentationEpoch
				|| entry.key.colourIdentity != live.colourIdentity
				|| entry.key.depthIdentity != live.depthIdentity
				|| entry.key.colourSubresource != live.colourSubresource
				|| entry.key.depthSubresource != live.depthSubresource
				|| entry.key.colourFormat != live.colourFormat
				|| entry.key.depthFormat != live.depthFormat
				|| entry.key.width != live.width || entry.key.height != live.height
				|| entry.key.sampleCount != live.sampleCount
				|| entry.key.deviceGeneration != live.deviceGeneration
				|| !WaterRenderBridge::SameViewState(entry.key.viewState, live.viewState)) continue;
		if (found == NULL || found->used < entry.used) found = &entry;
	}
	if (found == NULL) return false;

	current.frameId = found->key.frameId;
	current.resourceGeneration = found->key.resourceGeneration;
	current.captureSerial = found->key.captureSerial;
	current.colourIdentity = found->key.colourIdentity;
	current.depthIdentity = found->key.depthIdentity;
	current.viewIdentity = found->viewIdentity;
	current.targetWidth = found->key.width;
	current.targetHeight = found->key.height;
	current.targetFormat = found->key.colourFormat;
	current.targetSamples = found->key.sampleCount;
	current.viewState = live.viewState;
	return WaterRenderBridge::ValidCurrentView(current);
}

DX11WaterClass::SurfacePipeline *DX11WaterClass::Select_Pipeline(bool river, bool fog,
	std::uint32_t layout, std::uint32_t stride, bool fft)
{
	const DWORD edge_coordinates = river ? 0u : Backend->StageStates[3][D3DTSS_TEXCOORDINDEX];
	const DWORD edge_transform = river ? D3DTTFF_DISABLE : Backend->StageStates[3][D3DTSS_TEXTURETRANSFORMFLAGS];
	if (!river && (edge_coordinates != D3DTSS_TCI_CAMERASPACEPOSITION
			|| edge_transform != D3DTTFF_COUNT2)) return NULL;
	if (fft && (river || layout != 0x252u || stride != 44u)) return NULL;
	if (fft && Fft->SurfaceFailure()) return NULL;
	for (unsigned index = 0; index < 16; ++index) {
		SurfacePipeline & pipeline = Pipelines[index];
		if (pipeline.vertex != NULL && pipeline.river == river && pipeline.fog == fog && pipeline.fft == fft
				&& pipeline.vertexLayout == layout && pipeline.vertexStride == stride
				&& pipeline.edgeCoordinateIndex == edge_coordinates
				&& pipeline.edgeTransformFlags == edge_transform) return &pipeline;
	}
	unsigned vertex = layout == 0x242u && stride == 32u ? 0u
		: layout == 0x252u && stride == 44u ? 1u : 2u;
	if (vertex > 1) return NULL;
	const unsigned slot = (fft ? 8u : 0u) + vertex * 4 + (river ? 2u : 0u) + (fog ? 1u : 0u);
	water_init_trace("pipeline-begin", slot);
	if (!Create_Surface_Pipeline(Pipelines[slot], river, fog, layout, stride, fft)) return NULL;
	water_init_trace("pipeline-end", slot);
	return &Pipelines[slot];
}

bool DX11WaterClass::Create_Constant_Buffer(const void * data, std::size_t size,
	ID3D11Buffer *& buffer)
{
	if (Failure == TEST_FAILURE_BUFFER || data == NULL || size == 0 || (size & 15) != 0) return false;
	D3D11_BUFFER_DESC description = {};
	description.ByteWidth = static_cast<UINT>(size);
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA initial = {};
	initial.pSysMem = data;
	return SUCCEEDED(Device->Get_Device()->CreateBuffer(&description, &initial, &buffer));
}

bool DX11WaterClass::Create_Geometry_Buffer(const void * data, std::size_t size, UINT bind,
	ID3D11Buffer *& buffer)
{
	if (Failure == TEST_FAILURE_BUFFER || data == NULL || size == 0 || size > UINT_MAX) return false;
	D3D11_BUFFER_DESC description = {};
	description.ByteWidth = static_cast<UINT>(size);
	description.Usage = D3D11_USAGE_IMMUTABLE;
	description.BindFlags = bind;
	D3D11_SUBRESOURCE_DATA initial = {};
	initial.pSysMem = data;
	return SUCCEEDED(Device->Get_Device()->CreateBuffer(&description, &initial, &buffer));
}

void DX11WaterClass::Set_Test_Resource(const void * identity, ID3D11ShaderResourceView * view)
{
	if (identity == NULL) return;
	std::map<const void *, ID3D11ShaderResourceView *>::iterator found = TestResources.find(identity);
	if (found != TestResources.end()) release_object(found->second);
	if (view != NULL) view->AddRef();
	TestResources[identity] = view;
}

ID3D11Buffer *DX11WaterClass::Get_Test_Prepared_Pixel_Constant_Buffer(
	std::uint64_t patch_id) const
{
	for (std::vector<PreparedResources *>::const_iterator it = Prepared.begin(); it != Prepared.end(); ++it) {
		if ((*it)->exact.patchId == patch_id && (*it)->pixelConstants != NULL) {
			(*it)->pixelConstants->AddRef();
			return (*it)->pixelConstants;
		}
	}
	return NULL;
}

void DX11WaterClass::Clear_Test_Resources()
{
	for (std::map<const void *, ID3D11ShaderResourceView *>::iterator it = TestResources.begin();
			it != TestResources.end(); ++it) release_object(it->second);
	TestResources.clear();
}

DX11WaterClass::FftTestState DX11WaterClass::Get_Test_Fft_State() const { return Fft->State; }
ID3D11ShaderResourceView *DX11WaterClass::Get_Test_Fft_Map(unsigned index) const { return Fft->Map(index); }
ID3D11ShaderResourceView *DX11WaterClass::Get_Test_Fft_Array() const { return Fft->Array(); }
void DX11WaterClass::Set_Test_Fft_Failure(FftTestFailure stage, unsigned ordinal) { Fft->Fail(stage, ordinal); }
bool DX11WaterClass::Set_Test_Fft_Initial_Spectra(const float *values, std::size_t count) { return Fft->Initial(values, count); }
bool DX11WaterClass::Set_Test_Fft_Fields(ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b,
	ID3D11ShaderResourceView *c, ID3D11ShaderResourceView *array)
{
	ID3D11ShaderResourceView *views[4] = {a,b,c,array};
	for (auto *view : views) if (view) {
		ID3D11Device *device = NULL; view->GetDevice(&device);
		const bool same = Device && device == Device->Get_Device(); release_object(device);
		if (!same) return false;
	}
	return Fft->Fields(a,b,c,array);
}
bool DX11WaterClass::Get_Test_Prepared_Vertex_Bytecode(std::uint64_t patch_id,
	std::vector<unsigned char> &bytecode) const
{
	for (auto *prepared : Prepared) if (prepared->exact.patchId == patch_id) {
		auto found = VertexBytecode.find(prepared->pipeline->vertex);
		if (found == VertexBytecode.end()) return false;
		bytecode = found->second; return true;
	}
	return false;
}
ID3D11Buffer *DX11WaterClass::Get_Test_Prepared_Vertex_Constant_Buffer(std::uint64_t patch_id) const
{
	for (auto *prepared : Prepared) if (prepared->exact.patchId == patch_id) return prepared->vertexConstants;
	return NULL;
}
ID3D11ShaderResourceView *DX11WaterClass::Get_Test_Prepared_Vertex_Resource(std::uint64_t patch_id, unsigned slot) const
{
	if (slot >= 4) return NULL;
	for (auto *prepared : Prepared) if (prepared->exact.patchId == patch_id) return prepared->vertexResources[slot];
	return NULL;
}
bool DX11WaterClass::Prepare_Test_Fft(float raw_time) { return Ensure_Renderer() && Prepare_Fft(raw_time); }
bool DX11WaterClass::Prepare_Fft(float raw_time)
{
	if (!FftEnabled || !Device || !Backend) return false;
	WaterRenderBridge::CurrentView current_before = {}, current_after = {};
	const bool had_current = QueryCurrentView(current_before);
	ID3D11DeviceContext *context = Device->Get_Context();
	SavedContext targets; SavedDrawState draw;
	targets.Capture(context); draw.Capture(context);
	ID3D11UnorderedAccessView *uavs[8] = {}, *empty_uavs[8] = {};
	context->OMGetRenderTargetsAndUnorderedAccessViews(0, NULL, NULL, 0, 8, uavs);
	context->OMSetRenderTargetsAndUnorderedAccessViews(0, NULL, NULL, 0, 8, empty_uavs, NULL);
	targets.Detach(context);
	const DX11WaterFft::Compiler compile = [this](const std::string &source, const char *name,
		const char *profile, std::vector<unsigned char> &code) { return Backend->Compile_Program(source, name, profile, code); };
	bool ready = Fft->Prepare(Device->Get_Device(), context, compile,
		raw_time * WaterFftSampling::TimeScale);
	targets.Detach(context); draw.Restore(context); targets.Restore(context);
	unsigned target_count = 0, first_uav = 8;
	for (unsigned i = 0; i < 8; ++i) { if (targets.targets[i]) target_count = i + 1; if (uavs[i] && first_uav == 8) first_uav = i; }
	if (first_uav < 8) {
		UINT counts[8]; for (auto &count : counts) count = UINT(-1);
		context->OMSetRenderTargetsAndUnorderedAccessViews(target_count, targets.targets, targets.depth,
			first_uav, 8 - first_uav, uavs + first_uav, counts + first_uav);
	}
	for (auto &uav : uavs) release_object(uav);
	draw.Release(); targets.Release(); Backend->Forget_Bindings();
	if (had_current && (!QueryCurrentView(current_after)
		|| std::memcmp(&current_before, &current_after, sizeof(current_before)) != 0)) {
		Fft->Release(); ready = false;
	}
	if (ready) Capability.features |= WaterRenderBridge::Mask(WaterRenderBridge::Feature::Fft);
	else Capability.features &= ~WaterRenderBridge::Mask(WaterRenderBridge::Feature::Fft);
	return ready;
}

ID3D11ShaderResourceView *DX11WaterClass::Resolve_Resource(
	const WaterRenderPacket::Resource & resource)
{
	if (!resource.available || resource.identity == NULL) return NULL;
	std::map<const void *, ID3D11ShaderResourceView *>::const_iterator found =
		TestResources.find(resource.identity);
	ID3D11ShaderResourceView * view = NULL;
	if (found != TestResources.end()) view = found->second;
	else {
		view = DX11Texture_Mirror(Device->Get_Device(), Device->Get_Context(),
			reinterpret_cast<IDirect3DBaseTexture9 *>(const_cast<void *>(resource.identity)));
	}
	if (view != NULL) view->AddRef();
	return view;
}

bool DX11WaterClass::Capture_Raw_View(WaterRenderBridge::ViewState & state,
	ID3D11RenderTargetView *& colour, ID3D11DepthStencilView *& depth, std::uint32_t & width,
	std::uint32_t & height, std::uint32_t & format, std::uint32_t & samples) const
{
	std::memset(&state, 0, sizeof(state));
	colour = NULL; depth = NULL;
	if (Device == NULL || Backend == NULL || FAILED(Device->Device_Removed_Reason())) return false;
	Backend->Current_View_Projection(state.view, state.projection);
	D3D11_VIEWPORT viewport = {};
	UINT count = 1;
	Device->Get_Context()->RSGetViewports(&count, &viewport);
	if (count != 1) return false;
	state.viewport.x = viewport.TopLeftX; state.viewport.y = viewport.TopLeftY;
	state.viewport.width = viewport.Width; state.viewport.height = viewport.Height;
	state.viewport.minDepth = viewport.MinDepth; state.viewport.maxDepth = viewport.MaxDepth;
	if (!WaterRenderBridge::ValidViewState(state)) return false;
	Device->Get_Context()->OMGetRenderTargets(1, &colour, &depth);
	if (colour == NULL || depth == NULL) {
		release_object(colour); release_object(depth); return false;
	}
	SnapshotKey key = {};
	ID3D11Texture2D * colour_surface = NULL;
	ID3D11Texture2D * depth_surface = NULL;
	DXGI_FORMAT colour_view = DXGI_FORMAT_UNKNOWN, depth_view = DXGI_FORMAT_UNKNOWN;
	SnapshotStatus failure = SNAPSHOT_MISSING_TARGET;
	ID3D11RenderTargetView * colour_argument = colour;
	ID3D11DepthStencilView * depth_argument = depth;
	if (!Resolve_Key(1, 1, 1, state, colour_argument, depth_argument, key, colour_surface,
			depth_surface, colour_view, depth_view, failure)) {
		release_object(colour); release_object(depth); return false;
	}
	release_object(colour_surface); release_object(depth_surface);
	width = key.width; height = key.height; format = key.colourFormat; samples = key.sampleCount;
	return true;
}

DX11WaterClass::PreparedResources *DX11WaterClass::Find_Prepared(
	const WaterRenderBridge::PreparedPatch & prepared)
{
	for (std::vector<PreparedResources *>::iterator it = Prepared.begin(); it != Prepared.end(); ++it) {
		if (std::memcmp(&(*it)->exact, &prepared, sizeof(prepared)) == 0) return *it;
	}
	return NULL;
}

void DX11WaterClass::Release_Prepared(PreparedResources * prepared)
{
	if (prepared == NULL) return;
	for (unsigned index = 0; index < WaterRenderBridge::MaxPreparedBatches; ++index) {
		release_object(prepared->vertices[index]); release_object(prepared->indices[index]);
	}
	release_object(prepared->vertexConstants); release_object(prepared->pixelConstants);
	for (unsigned index = 0; index < 3; ++index) release_object(prepared->hullConstants[index]);
	for (unsigned index = 0; index < 4; ++index) {
		release_object(prepared->vertexResources[index]); release_object(prepared->vertexSamplers[index]);
	}
	for (unsigned index = 0; index < 16; ++index) {
		release_object(prepared->pixelResources[index]); release_object(prepared->pixelSamplers[index]);
	}
	delete prepared;
}

void DX11WaterClass::Release_Prepared_All()
{
	for (std::vector<PreparedResources *>::iterator it = Prepared.begin(); it != Prepared.end(); ++it)
		Release_Prepared(*it);
	Prepared.clear();
}

WaterRenderBridge::PrepareStatus DX11WaterClass::PreparePatch(
	const WaterRenderBridge::PrepareRequest & request, WaterRenderBridge::PreparedPatch & prepared)
{
	std::memset(&prepared, 0, sizeof(prepared));
	if (!Ensure_Renderer()) return WaterRenderBridge::PrepareStatus::Unavailable;
	if (!Capability.Available() || DeviceLostFrame == request.frameId
			|| Failure == TEST_FAILURE_SHADER) {
		return WaterRenderBridge::PrepareStatus::Unavailable;
	}
	if (!request.frameId || !request.resourceGeneration || !request.captureSerial || !request.patchId
			|| request.batchCount == 0 || request.batchCount > request.batchCapacity
			|| request.batchCount > WaterRenderBridge::MaxPreparedBatches || request.batches == NULL) {
		return WaterRenderBridge::PrepareStatus::Capacity;
	}
	if (request.fftEligibility != WaterRenderBridge::FftEligibility::Ineligible
		&& request.fftEligibility != WaterRenderBridge::FftEligibility::ProjectedFlat)
		return WaterRenderBridge::PrepareStatus::InvalidGeometry;
	for (std::size_t batch = 0; batch < request.batchCount; ++batch) {
		const auto &source = request.batches[batch];
		const bool u16 = source.indexFormat == WaterRenderBridge::IndexFormat::UInt16;
		if ((!u16 && source.indexFormat != WaterRenderBridge::IndexFormat::UInt32)
			|| (source.topology != WaterRenderBridge::Topology::TriangleList && source.topology != WaterRenderBridge::Topology::TriangleStrip)
			|| !source.vertices || !source.indices || !source.vertexCount || !source.vertexStride || source.baseVertex
			|| (u16 && source.vertexCount > 65535u)) return WaterRenderBridge::PrepareStatus::InvalidGeometry;
		const std::uint64_t vb = std::uint64_t(source.vertexOffset) * source.vertexStride;
		const std::uint64_t vs = std::uint64_t(source.vertexCount) * source.vertexStride;
		const std::uint64_t ib = std::uint64_t(source.indexOffset) * (u16 ? 2u : 4u);
		const std::uint64_t is = std::uint64_t(source.indexCount) * (u16 ? 2u : 4u);
		const std::uint64_t expected = source.topology == WaterRenderBridge::Topology::TriangleList
			? std::uint64_t(source.primitiveCount) * 3 : std::uint64_t(source.primitiveCount) + 2;
		if (!source.indexCount || source.indexCount != expected || vb > source.vertexBytes || vs > source.vertexBytes-vb
			|| ib > source.indexBytes || is > source.indexBytes-ib) return WaterRenderBridge::PrepareStatus::InvalidGeometry;
		if ((source.vertexLayout == 0x252u && source.vertexStride == 44u)
			|| (source.vertexLayout == 0x242u && source.vertexStride == 32u)) {
			const unsigned position_count = source.vertexLayout == 0x252u ? 6u : 3u;
			const unsigned uv_offset = source.vertexLayout == 0x252u ? 28u : 16u;
			const auto *vertices = static_cast<const unsigned char *>(source.vertices) + vb;
			for (unsigned i = 0; i < source.vertexCount; ++i) {
				float values[10]; const auto *vertex = vertices + std::size_t(i)*source.vertexStride;
				std::memcpy(values, vertex, position_count*sizeof(float));
				std::memcpy(values+position_count, vertex+uv_offset, 4*sizeof(float));
				if (!WaterRenderPacket::Finite(values, position_count+4)) return WaterRenderBridge::PrepareStatus::InvalidGeometry;
			}
		}
		const auto *indices = static_cast<const unsigned char *>(source.indices) + ib;
		for (unsigned i = 0; i < source.indexCount; ++i) {
			std::uint32_t value = 0;
			if (u16) { std::uint16_t v; std::memcpy(&v, indices + i*2u, 2); value = v; }
			else std::memcpy(&value, indices + std::size_t(i)*4u, 4);
			if (value >= source.vertexCount) return WaterRenderBridge::PrepareStatus::InvalidGeometry;
		}
	}
	WaterRenderPacket::Inputs inputs = request.inputs;
	inputs.surface.fftAvailable = false;
	if (!inputs.surface.river && !WaterRenderPacket::Finite(Backend->TextureTransforms[3], 16))
		return WaterRenderBridge::PrepareStatus::InvalidPacket;
	inputs.scene.snapshotAvailable = false;
	WaterRenderBridge::ViewState raw_state = {};
	ID3D11RenderTargetView * raw_colour = NULL;
	ID3D11DepthStencilView * raw_depth = NULL;
	std::uint32_t raw_width = 0, raw_height = 0, raw_format = 0, raw_samples = 0;
	if (!Capture_Raw_View(raw_state, raw_colour, raw_depth, raw_width, raw_height, raw_format,
			raw_samples)) return WaterRenderBridge::PrepareStatus::Unavailable;
	WaterRenderBridge::ViewState requested_state = WaterRenderBridge::CaptureViewState(request);
	requested_state.viewport.x += 0.5f;
	requested_state.viewport.y += 0.5f;
	if (!WaterRenderBridge::SameViewState(raw_state, requested_state)
			|| (request.targetWidth && request.targetWidth != raw_width)
			|| (request.targetHeight && request.targetHeight != raw_height)
			|| (request.targetFormat && request.targetFormat != raw_format)
			|| (request.targetSamples && request.targetSamples != raw_samples)) {
		release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidGeometry;
	}
	const bool receives_shadow = Backend->Water_Shadow_Receiving();
	DX11BackendClass::PixelConstantBlock physical_scene = {};
	Backend->Build_Water_Physical_Scene_Constants(physical_scene, inputs.scene.view,
		inputs.scene.projection, inputs.scene.viewport.x, inputs.scene.viewport.y,
		inputs.scene.viewport.width, inputs.scene.viewport.height, receives_shadow);
	std::memcpy(inputs.scene.combiner, physical_scene.TextureFactor, sizeof(inputs.scene.combiner));
	std::memcpy(inputs.scene.shadowFromClip, physical_scene.ShadowFromClip,
		sizeof(inputs.scene.shadowFromClip));
	std::memcpy(&inputs.scene.shadowParameters, physical_scene.ShadowParameters,
		sizeof(inputs.scene.shadowParameters));
	std::memcpy(&inputs.scene.shadowViewport, physical_scene.ShadowViewport,
		sizeof(inputs.scene.shadowViewport));
	std::memcpy(&inputs.scene.shadowSoftness, physical_scene.ShadowSoftness,
		sizeof(inputs.scene.shadowSoftness));
	std::memcpy(&inputs.scene.volumeParameters, physical_scene.VolumeParameters,
		sizeof(inputs.scene.volumeParameters));
	WaterRenderPacket::DrawPacket initial_packet = {};
	WaterRenderPacket::ValidationError packet_error = WaterRenderPacket::ValidationError::None;
	if (!WaterRenderPacket::Pack(inputs, initial_packet, &packet_error)) {
		water_init_trace("initial-packet-error", static_cast<unsigned>(packet_error));
		release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidPacket;
	}
	const bool fog = Backend->RenderStates.Get_Render_State(D3DRS_FOGENABLE) != 0;
	SurfacePipeline * pipeline = Select_Pipeline(inputs.surface.river, fog,
		request.batches[0].vertexLayout, request.batches[0].vertexStride);
	if (pipeline == NULL) {
		release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidGeometry;
	}
	PreparedResources * resources = new PreparedResources;
	std::memset(resources, 0, sizeof(*resources));
	resources->pipeline = pipeline;
	WaterRenderBridge::PreparedPatch & next = resources->exact;
	next.frameId = request.frameId; next.resourceGeneration = request.resourceGeneration;
	next.captureSerial = request.captureSerial; next.patchId = request.patchId;
	for (std::size_t index = 0; index < request.batchCount; ++index) {
		const WaterRenderBridge::BatchRequest & source = request.batches[index];
		if (source.vertexLayout != pipeline->vertexLayout || source.vertexStride != pipeline->vertexStride
				|| source.vertices == NULL || source.indices == NULL || !source.partId
				|| !source.vertexCount || !source.indexCount || !source.primitiveCount) {
			Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
			return WaterRenderBridge::PrepareStatus::InvalidGeometry;
		}
		const std::size_t index_stride = source.indexFormat == WaterRenderBridge::IndexFormat::UInt16 ? 2u : 4u;
		const std::uint64_t vertex_begin = std::uint64_t(source.vertexOffset) * source.vertexStride;
		const std::uint64_t vertex_size = std::uint64_t(source.vertexCount) * source.vertexStride;
		const std::uint64_t index_begin = std::uint64_t(source.indexOffset) * index_stride;
		const std::uint64_t index_size = std::uint64_t(source.indexCount) * index_stride;
		const bool topology_valid = source.topology == WaterRenderBridge::Topology::TriangleList
			? source.indexCount == source.primitiveCount * 3
			: source.indexCount == source.primitiveCount + 2;
		if (!topology_valid || vertex_begin > source.vertexBytes || vertex_size > source.vertexBytes - vertex_begin
				|| index_begin > source.indexBytes || index_size > source.indexBytes - index_begin
				|| !Create_Geometry_Buffer(static_cast<const unsigned char *>(source.vertices) + vertex_begin,
					vertex_size, D3D11_BIND_VERTEX_BUFFER, resources->vertices[index])
				|| !Create_Geometry_Buffer(static_cast<const unsigned char *>(source.indices) + index_begin,
					index_size, D3D11_BIND_INDEX_BUFFER, resources->indices[index])) {
			Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
			return WaterRenderBridge::PrepareStatus::InvalidGeometry;
		}
		WaterRenderBridge::PreparedBatch & destination = next.batches[index];
		destination.partId = source.partId; destination.topology = source.topology;
		destination.indexFormat = source.indexFormat; destination.vertexLayout = source.vertexLayout;
		destination.vertexStride = source.vertexStride; destination.vertexOffset = 0;
		destination.baseVertex = 0; destination.indexOffset = 0;
		destination.vertexCount = source.vertexCount; destination.indexCount = source.indexCount;
		destination.primitiveCount = source.primitiveCount;
		destination.vertices.identity = reinterpret_cast<std::uint64_t>(resources->vertices[index]);
		destination.vertices.generation = request.resourceGeneration; destination.vertices.size = vertex_size;
		destination.indices.identity = reinterpret_cast<std::uint64_t>(resources->indices[index]);
		destination.indices.generation = request.resourceGeneration; destination.indices.size = index_size;
	}
	next.batchCount = request.batchCount;
	if (Failure == TEST_FAILURE_LATER_BATCH
			|| !WaterRenderBridge::RetainCoverage(request, next)) {
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidGeometry;
	}
	const bool eligible = request.fftEligibility == WaterRenderBridge::FftEligibility::ProjectedFlat
		&& !inputs.surface.river && pipeline->vertexLayout == 0x252u && pipeline->vertexStride == 44u;
	resources->fftEligible = eligible;
	bool fft_ready = eligible && FftEnabled && (Fft->HasFields() || Prepare_Fft(inputs.surface.waterParameters.x));
	if (eligible && FftEnabled && !Fft->HasFields() && Fft->State.deviceLost) {
		DeviceLostFrame = request.frameId; Release_Prepared_All(); Release_All();
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::Unavailable;
	}
	if (fft_ready) {
		SurfacePipeline *displaced = Select_Pipeline(false, fog, pipeline->vertexLayout, pipeline->vertexStride, true);
		if (displaced) resources->pipeline = displaced;
		else { fft_ready = false; Capability.features &= ~WaterRenderBridge::Mask(WaterRenderBridge::Feature::Fft); }
	}
	resources->degraded = eligible && !fft_ready;
	resources->usesFft = fft_ready;
	resources->fftGeneration = Fft->State.generation;
	if (fft_ready) {
		inputs.surface.fftAvailable = true;
		auto descriptor = [](ID3D11ShaderResourceView *view, WaterRenderPacket::ResourceType type) {
			WaterRenderPacket::Resource result = {}; ID3D11Resource *resource = NULL; ID3D11Texture2D *texture = NULL;
			view->GetResource(&resource);
			if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&texture)))) {
				D3D11_TEXTURE2D_DESC desc = {}; texture->GetDesc(&desc);
				result = {view,type,true,desc.Width,desc.Height,desc.ArraySize,desc.Width*desc.Height*desc.ArraySize};
			}
			release_object(texture); release_object(resource); return result;
		};
		for (unsigned i = 0; i < 3; ++i) inputs.surface.fftDisplacement[i] = descriptor(Fft->Map(i*2), WaterRenderPacket::ResourceType::Texture2D);
		inputs.surface.fftArray = descriptor(Fft->Array(), WaterRenderPacket::ResourceType::Texture2DArray);
	}
	if (eligible) {
		WaterRenderBridge::ViewState restored = {}; ID3D11RenderTargetView *colour = NULL; ID3D11DepthStencilView *depth = NULL;
		std::uint32_t w=0,h=0,f=0,s=0;
		const bool same = Capture_Raw_View(restored, colour, depth, w,h,f,s)
			&& colour == raw_colour && depth == raw_depth && w == raw_width && h == raw_height
			&& f == raw_format && s == raw_samples && WaterRenderBridge::SameViewState(restored, raw_state);
		release_object(colour); release_object(depth);
		if (!same) { Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth); return WaterRenderBridge::PrepareStatus::Unavailable; }
	}

	auto retain_resource = [this](const WaterRenderPacket::Resource & resource,
		ID3D11ShaderResourceView * fallback) -> ID3D11ShaderResourceView * {
		ID3D11ShaderResourceView * view = Resolve_Resource(resource);
		if (!resource.available && fallback != NULL) { fallback->AddRef(); view = fallback; }
		return view;
	};
	resources->pixelResources[0] = retain_resource(inputs.surface.authoredMaterial, FallbackWhite);
	resources->pixelResources[1] = retain_resource(inputs.surface.reflection, FallbackBlack);
	if (getenv("ZH_WATER_NATIVE_RESOURCE_TRACE") != NULL && resources->pixelResources[1] != NULL) {
		if (LastTracedReflectionIdentity != inputs.surface.reflection.identity
				|| LastTracedReflectionPatch != request.patchId
				|| LastTracedReflectionFrame != request.frameId
				|| LastTracedReflectionGeneration != request.resourceGeneration) {
			trace_native_reflection(Device->Get_Device(), Device->Get_Context(), request.patchId,
				inputs.surface.reflection.identity, resources->pixelResources[1]);
			LastTracedReflectionIdentity = inputs.surface.reflection.identity;
			LastTracedReflectionPatch = request.patchId;
			LastTracedReflectionFrame = request.frameId;
			LastTracedReflectionGeneration = request.resourceGeneration;
		}
	}
	resources->pixelResources[2] = retain_resource(inputs.surface.heightWaveCage, FallbackWhite);
	resources->pixelResources[3] = Backend->Textures[3];
	if (resources->pixelResources[3] != NULL) resources->pixelResources[3]->AddRef();
	resources->pixelResources[4] = retain_resource(inputs.surface.optical, FallbackWhite);
	resources->pixelResources[7] = retain_resource(inputs.surface.bed, FallbackWhite);
	resources->pixelResources[8] = fft_ready ? Fft->Array() : FallbackArray;
	resources->pixelResources[8]->AddRef();
	resources->pixelResources[9] = receives_shadow ? Backend->Shadow_Map() : NULL;
	resources->pixelResources[10] = receives_shadow ? Backend->Smoke_Map() : NULL;
	if (resources->pixelResources[9] == NULL) resources->pixelResources[9] = FallbackWhite;
	if (resources->pixelResources[10] == NULL) resources->pixelResources[10] = FallbackBlack;
	resources->pixelResources[9]->AddRef(); resources->pixelResources[10]->AddRef();
	resources->pixelResources[14] = retain_resource(inputs.surface.foamHistory, FallbackBlack);
	resources->pixelResources[15] = retain_resource(inputs.surface.coastal, FallbackBlack);
	for (unsigned index = 0; index < 3; ++index) {
		resources->vertexResources[index] = fft_ready ? Fft->Map(index*2) : FallbackWhite;
		resources->vertexResources[index]->AddRef();
	}
	resources->vertexResources[3] = retain_resource(inputs.surface.heightWaveCage, FallbackWhite);
	for (unsigned index = 0; index < 4; ++index) {
		resources->vertexSamplers[index] = fft_ready && index < 3 ? BedSampler : LinearSampler;
		resources->vertexSamplers[index]->AddRef();
	}
	for (unsigned index = 0; index < 16; ++index) {
		ID3D11SamplerState *sampler = LinearSampler;
		if (index == 0 || index == 3) sampler = Backend->Sampler_State(index);
		else if (index == 6 || index == 9) sampler = PointSampler;
		else if (index == 7 || (index == 8 && fft_ready)) sampler = BedSampler;
		if (sampler == NULL) {
			Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
			return WaterRenderBridge::PrepareStatus::Unavailable;
		}
		resources->pixelSamplers[index] = sampler;
		sampler->AddRef();
	}
	const bool resources_valid = resources->pixelResources[0] != NULL
		&& resources->pixelResources[1] != NULL && resources->pixelResources[2] != NULL
		&& resources->pixelResources[3] != NULL && resources->pixelResources[4] != NULL
		&& resources->pixelResources[7] != NULL && resources->pixelResources[8] != NULL
		&& resources->pixelResources[14] != NULL && resources->pixelResources[15] != NULL
		&& resources->vertexResources[0] != NULL && resources->vertexResources[1] != NULL
		&& resources->vertexResources[2] != NULL && resources->vertexResources[3] != NULL;
	if (!resources_valid) {
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::Unavailable;
	}

	DX11BackendClass::VertexConstantBlock vertex_constants = {};
	std::memcpy(vertex_constants.WorldView, inputs.scene.view, sizeof(vertex_constants.WorldView));
	multiply_matrix(inputs.scene.view, inputs.scene.projection, vertex_constants.WorldViewProjection);
	float inverse_view[16] = {};
	if (!WaterRenderPacket::Invert(inputs.scene.view, inverse_view)) {
		water_init_trace("inverse-view-error", 0);
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidPacket;
	}
	for (unsigned row = 0; row < 4; ++row) for (unsigned column = 0; column < 4; ++column)
		vertex_constants.NormalTransform[row * 4 + column] = inverse_view[column * 4 + row];
	for (unsigned index = 0; index < 4; ++index) WaterRenderPacket::Identity(vertex_constants.TextureMatrix[index]);
	if (!inputs.surface.river) std::memcpy(vertex_constants.TextureMatrix[3],
		Backend->TextureTransforms[3], sizeof(vertex_constants.TextureMatrix[3]));
	vertex_constants.MaterialAmbient[0] = vertex_constants.MaterialAmbient[1] =
		vertex_constants.MaterialAmbient[2] = vertex_constants.MaterialAmbient[3] = 1.0f;
	std::memcpy(vertex_constants.MaterialDiffuse, vertex_constants.MaterialAmbient, 16);
	std::memcpy(vertex_constants.GlobalAmbient, vertex_constants.MaterialAmbient, 16);
	const DWORD fog_start = Backend->RenderStates.Get_Render_State(D3DRS_FOGSTART);
	const DWORD fog_end = Backend->RenderStates.Get_Render_State(D3DRS_FOGEND);
	const DWORD fog_density = Backend->RenderStates.Get_Render_State(D3DRS_FOGDENSITY);
	std::memcpy(&vertex_constants.FogParameters[0], &fog_start, sizeof(float));
	std::memcpy(&vertex_constants.FogParameters[1], &fog_end, sizeof(float));
	std::memcpy(&vertex_constants.FogParameters[2], &fog_density, sizeof(float));
	vertex_constants.ViewportInverse[0] = 1.0f / raw_state.viewport.width;
	vertex_constants.ViewportInverse[1] = 1.0f / raw_state.viewport.height;
	WaterRenderPacket::Inputs staged_inputs = inputs;
	staged_inputs.scene.snapshotAvailable = true;
	staged_inputs.scene.sceneColour = { raw_colour, WaterRenderPacket::ResourceType::Texture2D, true,
		raw_width, raw_height, 1, raw_width * raw_height };
	staged_inputs.scene.sceneDepth = { raw_depth, WaterRenderPacket::ResourceType::Texture2D, true,
		raw_width, raw_height, 1, raw_width * raw_height };
	WaterRenderPacket::DrawPacket staged_packet = {};
	packet_error = WaterRenderPacket::ValidationError::None;
	if (!WaterRenderPacket::Pack(staged_inputs, staged_packet, &packet_error)) {
		water_init_trace("staged-packet-error", static_cast<unsigned>(packet_error));
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::InvalidPacket;
	}
	WaterShaderLayout::PixelConstants pixel_constants = staged_packet.constants;
	WaterShaderLayout::HullPacketBlock0 hull0 = {};
	WaterShaderLayout::HullPacketBlock1 hull1 = {};
	WaterShaderLayout::HullPacketBlock2 hull2 = {};
	std::memcpy(hull0.registers, staged_packet.hullPackets.registers, sizeof(hull0));
	std::memcpy(hull1.registers, staged_packet.hullPackets.registers + 256, sizeof(hull1));
	std::memcpy(hull2.registers, staged_packet.hullPackets.registers + 512, sizeof(hull2));
	if (!Create_Constant_Buffer(&vertex_constants, sizeof(vertex_constants), resources->vertexConstants)
			|| !Create_Constant_Buffer(&pixel_constants, sizeof(pixel_constants), resources->pixelConstants)
			|| !Create_Constant_Buffer(&hull0, sizeof(hull0), resources->hullConstants[0])
			|| !Create_Constant_Buffer(&hull1, sizeof(hull1), resources->hullConstants[1])
			|| !Create_Constant_Buffer(&hull2, sizeof(hull2), resources->hullConstants[2])) {
		Release_Prepared(resources); release_object(raw_colour); release_object(raw_depth);
		return WaterRenderBridge::PrepareStatus::Unavailable;
	}

	SnapshotView snapshot = {};
	const SnapshotStatus captured = Capture_Scene(request.frameId, request.resourceGeneration,
		request.captureSerial, raw_state, raw_colour, raw_depth, snapshot);
	release_object(raw_colour); release_object(raw_depth);
	if (captured != SNAPSHOT_READY && captured != SNAPSHOT_REUSED) {
		Release_Prepared(resources);
		if (captured == SNAPSHOT_DEVICE_LOST) DeviceLostFrame = request.frameId;
		return WaterRenderBridge::PrepareStatus::Unavailable;
	}
	WaterRenderBridge::CurrentView current = {};
	if (!QueryCurrentView(current) || current.frameId != request.frameId
			|| current.resourceGeneration != request.resourceGeneration
			|| current.captureSerial != request.captureSerial) {
		Release_Prepared(resources); return WaterRenderBridge::PrepareStatus::Unavailable;
	}
	inputs.scene.snapshotAvailable = true;
	inputs.scene.sceneColour = { snapshot.colour, WaterRenderPacket::ResourceType::Texture2D, true,
		snapshot.width, snapshot.height, 1, snapshot.width * snapshot.height };
	inputs.scene.sceneDepth = { snapshot.depth, WaterRenderPacket::ResourceType::Texture2D, true,
		snapshot.width, snapshot.height, 1, snapshot.width * snapshot.height };
	packet_error = WaterRenderPacket::ValidationError::None;
	if (!WaterRenderPacket::Pack(inputs, next.packet, &packet_error)) {
		water_init_trace("final-packet-error", static_cast<unsigned>(packet_error));
		Release_Prepared(resources); return WaterRenderBridge::PrepareStatus::InvalidPacket;
	}
	resources->pixelResources[5] = snapshot.colour; resources->pixelResources[5]->AddRef();
	resources->pixelResources[6] = snapshot.depth; resources->pixelResources[6]->AddRef();
	next.colourIdentity = current.colourIdentity; next.depthIdentity = current.depthIdentity;
	next.viewIdentity = current.viewIdentity; next.targetWidth = current.targetWidth;
	next.targetHeight = current.targetHeight; next.targetFormat = current.targetFormat;
	next.targetSamples = current.targetSamples; next.viewState = current.viewState; next.complete = true;
	if (WaterRenderBridge::ValidatePreparedPatch(next) != WaterRenderBridge::PrepareStatus::Ready) {
		Release_Prepared(resources); return WaterRenderBridge::PrepareStatus::InvalidGeometry;
	}
	prepared = next;
	Prepared.push_back(resources);
	Capability.features |= resources->pipeline->supportedFeatures;
	return WaterRenderBridge::PrepareStatus::Ready;
}

WaterRenderBridge::SubmissionReceipt DX11WaterClass::SubmitPreparedPatch(
	const WaterRenderBridge::PreparedPatch & prepared)
{
	WaterRenderBridge::SubmissionReceipt receipt = {};
	receipt.status = WaterRenderBridge::SubmitStatus::Refused;
	receipt.frameId = prepared.frameId;
	receipt.resourceGeneration = prepared.resourceGeneration;
	receipt.captureSerial = prepared.captureSerial;
	receipt.patchId = prepared.patchId;
	if (WaterRenderBridge::ValidatePreparedPatch(prepared) != WaterRenderBridge::PrepareStatus::Ready
			|| DeviceLostFrame == prepared.frameId) return receipt;
	PreparedResources * resources = Find_Prepared(prepared);
	WaterRenderBridge::CurrentView current = {};
	if (resources == NULL || (resources->usesFft && (resources->fftGeneration != Fft->State.generation
		|| (!Fft->HasFields() && !Fft->State.ready))) || !QueryCurrentView(current)
			|| current.frameId != prepared.frameId
			|| current.resourceGeneration != prepared.resourceGeneration
			|| current.captureSerial != prepared.captureSerial
			|| current.colourIdentity != prepared.colourIdentity
			|| current.depthIdentity != prepared.depthIdentity
			|| current.viewIdentity != prepared.viewIdentity
			|| current.targetWidth != prepared.targetWidth
			|| current.targetHeight != prepared.targetHeight
			|| current.targetFormat != prepared.targetFormat
			|| current.targetSamples != prepared.targetSamples
			|| !WaterRenderBridge::SameViewState(current.viewState, prepared.viewState)) return receipt;

	ID3D11DeviceContext * context = Device->Get_Context();
	SavedDrawState saved;
	saved.Capture(context);
	context->IASetInputLayout(resources->pipeline->layout);
	context->VSSetShader(resources->pipeline->vertex, NULL, 0);
	context->HSSetShader(NULL, NULL, 0);
	context->DSSetShader(NULL, NULL, 0);
	context->GSSetShader(NULL, NULL, 0);
	context->PSSetShader(resources->pipeline->pixel, NULL, 0);
	ID3D11Buffer * vertex_constants[2] = { resources->vertexConstants, resources->pixelConstants };
	context->VSSetConstantBuffers(0, 2, vertex_constants);
	ID3D11Buffer * pixel_constants[4] = { resources->pixelConstants, resources->hullConstants[0],
		resources->hullConstants[1], resources->hullConstants[2] };
	context->PSSetConstantBuffers(0, 4, pixel_constants);
	context->VSSetShaderResources(0, 4, resources->vertexResources);
	context->VSSetSamplers(0, 4, resources->vertexSamplers);
	context->PSSetShaderResources(0, 16, resources->pixelResources);
	context->PSSetSamplers(0, 16, resources->pixelSamplers);
	const float blend_factor[4] = { 1, 1, 1, 1 };
	context->OMSetBlendState(BlendState, blend_factor, 0xffffffffu);
	context->OMSetDepthStencilState(DepthState, 0);
	context->RSSetState(RasterizerState);
	unsigned issued = 0;
	for (std::size_t index = 0; index < prepared.batchCount; ++index) {
		const WaterRenderBridge::PreparedBatch & batch = prepared.batches[index];
		UINT stride = batch.vertexStride, offset = 0;
		context->IASetVertexBuffers(0, 1, &resources->vertices[index], &stride, &offset);
		context->IASetIndexBuffer(resources->indices[index],
			batch.indexFormat == WaterRenderBridge::IndexFormat::UInt16
				? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
		context->IASetPrimitiveTopology(batch.topology == WaterRenderBridge::Topology::TriangleList
			? D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		context->DrawIndexed(batch.indexCount, 0, 0);
		++issued;
		if ((Failure == TEST_FAILURE_DEVICE_LOST_AFTER_FIRST_DRAW && issued == 1)
				|| FAILED(Device->Device_Removed_Reason())) {
			DeviceLostFrame = prepared.frameId;
			receipt.status = WaterRenderBridge::SubmitStatus::DeviceLost;
			break;
		}
	}
	ID3D11ShaderResourceView * empty[16] = {};
	context->VSSetShaderResources(0, 16, empty);
	context->PSSetShaderResources(0, 16, empty);
	saved.Restore(context);
	saved.Release();
	Backend->Forget_Bindings();
	receipt.commandsIssued = issued;
	receipt.batchesPrepared = static_cast<std::uint32_t>(prepared.batchCount);
	receipt.batchesIssued = issued;
	if (receipt.status != WaterRenderBridge::SubmitStatus::DeviceLost && issued == prepared.batchCount) {
		receipt.status = WaterRenderBridge::SubmitStatus::Issued;
		receipt.colourIdentity = prepared.colourIdentity; receipt.depthIdentity = prepared.depthIdentity;
		receipt.viewIdentity = prepared.viewIdentity; receipt.targetWidth = prepared.targetWidth;
		receipt.targetHeight = prepared.targetHeight; receipt.targetFormat = prepared.targetFormat;
		receipt.targetSamples = prepared.targetSamples; receipt.viewState = prepared.viewState;
		receipt.coveragePublished = !resources->degraded;
		if (getenv("ZH_WATER_NATIVE_RESOURCE_TRACE") != NULL) {
			fprintf(stderr, "water-device: frame=%llu patch=%llu software=%u swapchain=%u\n",
				static_cast<unsigned long long>(prepared.frameId), static_cast<unsigned long long>(prepared.patchId),
				unsigned(Device->Is_Software()), unsigned(Device->Has_Swap_Chain()));
			const FftTestState fft = Fft->State;
			fprintf(stderr, "water-resource fft patch=%llu issued=%u vertex=%s river=%u layout=%u eligible=%u enabled=%u degraded=%u override=%u ready=%u generation=%llu phase=%.9g draws=%llu copies=%llu updates=%llu lastDraws=%u lastCopies=%u vs0=%p vs1=%p vs2=%p ps8=%p coverage=%u\n",
				static_cast<unsigned long long>(prepared.patchId), issued, resources->pipeline->fft ? "fft" : "analytic",
				resources->pipeline->river, resources->pipeline->vertexLayout, resources->fftEligible, FftEnabled, resources->degraded, Fft->HasFields(), fft.ready,
				static_cast<unsigned long long>(fft.generation), fft.time, static_cast<unsigned long long>(fft.draws),
				static_cast<unsigned long long>(fft.copies), static_cast<unsigned long long>(fft.updates), fft.drawCount, fft.copyCount,
				resources->vertexResources[0], resources->vertexResources[1], resources->vertexResources[2], resources->pixelResources[8], receipt.coveragePublished);
			fflush(stderr);
		}
	}
	for (std::vector<PreparedResources *>::iterator it = Prepared.begin(); it != Prepared.end(); ++it) {
		if (*it == resources) { Prepared.erase(it); break; }
	}
	Release_Prepared(resources);
	return receipt;
}

void DX11WaterClass::End(std::uint64_t)
{
	Release_Prepared_All();
}

void DX11WaterClass::InvalidateResources(std::uint64_t resource_generation)
{
	if (resource_generation != LastResourceGeneration) {
		Release_Prepared_All();
		Release_All();
		LastResourceGeneration = resource_generation;
	}
}

void DX11WaterClass::InvalidateMap()
{
	Release_Prepared_All();
	Release_All();
	LastResourceGeneration = 0;
}
