#pragma once

#include "test_harness.h"
#include "dx11backend.h"
#include "dx11water.h"

#include <d3d11sdklayers.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace WaterDx11SurfaceTest {

template<class T> void Release(T *&object)
{
	if (object != NULL) object->Release();
	object = NULL;
}

struct Pixel { float r, g, b, a; };
struct Vertex { float x, y, z; unsigned colour; float u, v, damping, level; };

struct Texture
{
	ID3D11Texture2D *surface;
	ID3D11ShaderResourceView *view;
	Texture() : surface(NULL), view(NULL) {}
	~Texture() { Release(view); Release(surface); }
	bool Set(ID3D11Device *device, const Pixel &pixel)
	{
		Release(view); Release(surface);
		D3D11_TEXTURE2D_DESC description = {};
		description.Width = description.Height = description.MipLevels = description.ArraySize = 1;
		description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		description.SampleDesc.Count = 1; description.Usage = D3D11_USAGE_IMMUTABLE;
		description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = { &pixel, sizeof(pixel), 0 };
		return SUCCEEDED(device->CreateTexture2D(&description, &data, &surface))
			&& SUCCEEDED(device->CreateShaderResourceView(surface, NULL, &view));
	}
};

class Fixture
{
public:
	static const unsigned Width = 64, Height = 48;
	DX11DeviceClass device;
	DX11BackendClass backend;
	DX11WaterClass *water;
	Texture base, mirror, height, edge, optical, bed;
	ID3D11Texture2D *colourSurface, *depthSurface;
	ID3D11RenderTargetView *colourView;
	ID3D11DepthStencilView *depthView;
	WaterRenderBridge::PrepareRequest request;
	WaterRenderBridge::BatchRequest batches[2];
	Vertex vertices[4];
	unsigned short indices[6];
	WaterRenderBridge::CoveragePoint coverage[4];
	unsigned frame;

	Fixture() : water(NULL), colourSurface(NULL), depthSurface(NULL), colourView(NULL),
		depthView(NULL), frame(0) {}
	~Fixture()
	{
		if (water != NULL) water->Clear_Test_Resources();
		if (device.Get_Context() != NULL) device.Get_Context()->ClearState();
		Release(colourView); Release(depthView); Release(colourSurface); Release(depthSurface);
		backend.Shutdown();
	}
	bool Initialise()
	{
		std::printf("  optics phase=device-create tick=%llu\n",static_cast<unsigned long long>(GetTickCount64()));
		std::fflush(stdout);
		device.Request_Debug_Layer();
		if (!device.Create_Offscreen()) return false;
		std::printf("  optics phase=backend-initialise tick=%llu\n",static_cast<unsigned long long>(GetTickCount64()));
		std::fflush(stdout);
		if (!backend.Initialise(&device)) return false;
		water = backend.Water_Owner();
		if (water == NULL) return false;
		CHECK(device.Has_Debug_Layer());
		const char *expected = std::getenv("ZH_TEST_DX11_EXPECT_DRIVER");
		if (expected != NULL && _stricmp(expected, "warp") == 0) CHECK(device.Is_Software());
		if (expected != NULL && _stricmp(expected, "hardware") == 0) CHECK(!device.Is_Software());
		std::printf("  optics adapter=%s core=%u production=%u\n", device.Is_Software() ? "warp" : "hardware",
			water->GetCapability().Available() ? 1u : 0u,
			WaterRenderBridge::HasAll(water->GetCapability().features, WaterRenderBridge::ProductionRequired) ? 1u : 0u);
		CHECK(water->GetCapability().Available());
		WaterRenderBridge::CurrentView current = {};
		CHECK(!water->QueryCurrentView(current));
		return water->GetCapability().Available();
	}
	bool Scene(const Pixel &colour = Pixel{0.32f, 0.42f, 0.52f, 1}, float depth = 0.7f,
		bool occluder = false, const Pixel &occluderColour = Pixel{1,0,1,1})
	{
		water->End(frame); water->Advance_Presented_Frame(); ++frame;
		water->Set_Test_Failure(DX11WaterClass::TEST_FAILURE_NONE);
		device.Get_Context()->ClearState(); backend.Forget_Bindings();
		Release(colourView); Release(depthView); Release(colourSurface); Release(depthSurface);
		std::vector<Pixel> colours(Width * Height, colour);
		std::vector<float> depths(Width * Height, depth);
		if (occluder) for (unsigned y = 0; y < Height; ++y) for (unsigned x = 0; x < Width / 2; ++x) {
			colours[y * Width + x] = occluderColour; depths[y * Width + x] = 0.45f;
		}
		D3D11_TEXTURE2D_DESC description = {};
		description.Width = Width; description.Height = Height;
		description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
		description.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		description.Usage = D3D11_USAGE_DEFAULT;
		description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		D3D11_SUBRESOURCE_DATA data = { &colours[0], Width * sizeof(Pixel), 0 };
		if (FAILED(device.Get_Device()->CreateTexture2D(&description, &data, &colourSurface))
			|| FAILED(device.Get_Device()->CreateRenderTargetView(colourSurface, NULL, &colourView))) return false;
		description.Format = DXGI_FORMAT_R32_TYPELESS;
		description.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		data.pSysMem = &depths[0]; data.SysMemPitch = Width * sizeof(float);
		D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription = {};
		depthDescription.Format = DXGI_FORMAT_D32_FLOAT;
		depthDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device.Get_Device()->CreateTexture2D(&description, &data, &depthSurface))
			|| FAILED(device.Get_Device()->CreateDepthStencilView(depthSurface, &depthDescription, &depthView))) return false;
		device.Get_Context()->OMSetRenderTargets(1, &colourView, depthView);
		D3D11_VIEWPORT viewport = { 0.5f, 0.5f, float(Width), float(Height), 0, 1 };
		device.Get_Context()->RSSetViewports(1, &viewport);
		if (!base.Set(device.Get_Device(), {1, 1, 1, 1})
			|| !mirror.Set(device.Get_Device(), {0, 0, 0, 0})
			|| !height.Set(device.Get_Device(), {-0.8f, 0, 0, 0})
			|| !edge.Set(device.Get_Device(), {1, 1, 1, 1})
			|| !optical.Set(device.Get_Device(), {0, 0, 0, 0})
			|| !bed.Set(device.Get_Device(), {0.55f, 0.45f, 0.5f, 0.5f})) return false;
		request = {}; request.frameId = frame; request.resourceGeneration = 1;
		request.captureSerial = frame; request.patchId = frame;
		request.targetWidth = Width; request.targetHeight = Height;
		request.viewIdentity = 0x12345678;
		request.batches = batches; request.batchCount = request.batchCapacity = 1;
		request.coverage = coverage; request.coverageCount = request.coverageCapacity = 4;
		const Vertex quad[4] = {{-1,-1,0,0xffffffff,0,1,0,0}, {-1,1,0,0xffffffff,0,0,0,0},
			{1,1,0,0xffffffff,1,0,0,0}, {1,-1,0,0xffffffff,1,1,0,0}};
		const unsigned short order[6] = {0,1,2,0,2,3};
		std::memcpy(vertices, quad, sizeof(vertices)); std::memcpy(indices, order, sizeof(indices));
		const WaterRenderBridge::CoveragePoint polygon[4] = {{-1,-1},{1,-1},{1,1},{-1,1}};
		std::memcpy(coverage, polygon, sizeof(coverage));
		batches[0] = {}; batches[0].partId = 1;
		batches[0].topology = WaterRenderBridge::Topology::TriangleList;
		batches[0].indexFormat = WaterRenderBridge::IndexFormat::UInt16;
		batches[0].vertexLayout = 0x242; batches[0].vertexStride = sizeof(Vertex);
		batches[0].vertexCount = 4; batches[0].indexCount = 6; batches[0].primitiveCount = 2;
		batches[0].vertices = vertices; batches[0].indices = indices;
		batches[0].vertexBytes = sizeof(vertices); batches[0].indexBytes = sizeof(indices);
		WaterRenderPacket::SceneInput &scene = request.inputs.scene;
		WaterRenderPacket::Identity(scene.view); WaterRenderPacket::Identity(scene.projection);
		scene.view[14] = -2; scene.projection[10] = -0.25f;
		scene.viewport = {0,0,float(Width),float(Height),0,1};
		scene.combiner[0] = {1,1,1,1}; scene.sunDirection = {0,0,-1,0}; scene.sunColour = {1,1,1,0};
		for (unsigned i = 0; i < 4; ++i) scene.shadowFromClip[i][i] = 1;
		WaterRenderPacket::SurfaceInput &surface = request.inputs.surface;
		surface.reflectionArea = {-1,-1,1,1}; surface.waterParameters = {0,0,1,0};
		surface.terrainParameters = {0,16,1,1}; surface.opticalParameters = {8,0.2f,0,0};
		surface.hullParameters = {0,0.2f,0.2f,0.2f};
		surface.authoredMaterialAvailable = surface.reflectionAvailable = surface.heightWaveCageAvailable = true;
		surface.terrainAvailable = surface.opticalAvailable = surface.bedAvailable = true;
		BindResources();
		backend.Set_Texture_Stage_State(3,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACEPOSITION);
		backend.Set_Texture_Stage_State(3,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2);
		backend.Set_Transform(D3DTS_VIEW, scene.view); backend.Set_Transform(D3DTS_PROJECTION, scene.projection);
		backend.Set_Render_State(D3DRS_FOGENABLE, FALSE);
		return true;
	}
	void BindResources()
	{
		Texture *textures[] = {&base,&mirror,&height,&edge,&optical,&bed};
		WaterRenderPacket::Resource *resources[] = {&request.inputs.surface.authoredMaterial,
			&request.inputs.surface.reflection,&request.inputs.surface.heightWaveCage,
			&request.inputs.surface.terrainHeight,&request.inputs.surface.optical,&request.inputs.surface.bed};
		for (unsigned i = 0; i < 6; ++i) {
			water->Set_Test_Resource(textures[i], textures[i]->view);
			*resources[i] = {textures[i], WaterRenderPacket::ResourceType::Texture2D, true,1,1,1,1};
		}
		backend.Set_Texture(3, edge.view);
	}
	bool Read(std::vector<Pixel> &pixels, ID3D11Texture2D *source = NULL)
	{
		if (source == NULL) source = colourSurface;
		D3D11_TEXTURE2D_DESC description = {}; source->GetDesc(&description);
		description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
		description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
		ID3D11Texture2D *staging = NULL;
		if (FAILED(device.Get_Device()->CreateTexture2D(&description, NULL, &staging))) return false;
		device.Get_Context()->CopyResource(staging, source);
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		const bool ok = SUCCEEDED(device.Get_Context()->Map(staging,0,D3D11_MAP_READ,0,&mapped));
		if (ok) {
			pixels.resize(Width * Height);
			for (unsigned y = 0; y < Height; ++y) std::memcpy(&pixels[y * Width],
				static_cast<unsigned char *>(mapped.pData) + y * mapped.RowPitch, Width * sizeof(Pixel));
			device.Get_Context()->Unmap(staging,0);
		}
		Release(staging); return ok;
	}
	Pixel Draw(const char *label, bool *issued = NULL)
	{
		std::printf("  optics phase=prepare label=%s frame=%u tick=%llu\n",label,frame,
			static_cast<unsigned long long>(GetTickCount64()));
		std::fflush(stdout);
		WaterRenderBridge::PreparedPatch prepared = {};
		const WaterRenderBridge::PrepareStatus status = water->PreparePatch(request, prepared);
		CHECK(status == WaterRenderBridge::PrepareStatus::Ready);
		Pixel result = {};
		if (status != WaterRenderBridge::PrepareStatus::Ready) { if (issued) *issued = false; return result; }
		CHECK_NE(prepared.viewIdentity, request.viewIdentity);
		WaterRenderBridge::CurrentView current = {}; CHECK(water->QueryCurrentView(current));
		CHECK_EQ(current.viewIdentity, prepared.viewIdentity);
		const WaterRenderBridge::SubmissionReceipt receipt = water->SubmitPreparedPatch(prepared);
		CHECK(WaterRenderBridge::Truthful(receipt, prepared));
		CHECK_EQ(receipt.commandsIssued, 1u);
		if (issued) *issued = receipt.status == WaterRenderBridge::SubmitStatus::Issued;
		std::vector<Pixel> pixels; CHECK(Read(pixels));
		if (!pixels.empty()) result = pixels[(Height / 2) * Width + Width / 2];
		for (std::size_t i = 0; i < pixels.size(); ++i) {
			CHECK(std::isfinite(pixels[i].r) && std::isfinite(pixels[i].g) && std::isfinite(pixels[i].b));
			CHECK(pixels[i].r >= -0.0001f && pixels[i].r <= 1.0001f
				&& pixels[i].g >= -0.0001f && pixels[i].g <= 1.0001f
				&& pixels[i].b >= -0.0001f && pixels[i].b <= 1.0001f);
		}
		std::printf("  optics %s frame=%u rgb=%.7f,%.7f,%.7f alpha=%.7f commands=%u\n",
			label,frame,result.r,result.g,result.b,result.a,receipt.commandsIssued);
		std::fflush(stdout);
		return result;
	}
};

inline unsigned DebugWarnings(ID3D11Device *device)
{
	ID3D11InfoQueue *queue = NULL;
	if (FAILED(device->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void **>(&queue)))) return ~0u;
	unsigned result = 0;
	for (UINT64 i = 0; i < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
		SIZE_T size = 0; queue->GetMessage(i,NULL,&size); std::vector<unsigned char> bytes(size);
		D3D11_MESSAGE *message = reinterpret_cast<D3D11_MESSAGE *>(&bytes[0]);
		if (SUCCEEDED(queue->GetMessage(i,message,&size)) && message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
			++result; std::printf("  D3D11 warning: %s\n", message->pDescription);
		}
	}
	Release(queue); return result;
}

class DrawCounter
{
	ID3D11DeviceContext *context;
	ID3D11Query *query;
public:
	DrawCounter(Fixture &fixture) : context(fixture.device.Get_Context()), query(NULL)
	{
		D3D11_QUERY_DESC description = {D3D11_QUERY_PIPELINE_STATISTICS,0};
		CHECK(SUCCEEDED(fixture.device.Get_Device()->CreateQuery(&description,&query)));
		if (query != NULL) context->Begin(query);
	}
	~DrawCounter() { Release(query); }
	unsigned long long End()
	{
		if (query == NULL) return ~0ull;
		context->End(query); context->Flush();
		D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics = {};
		const ULONGLONG deadline = GetTickCount64() + 5000;
		HRESULT status = S_FALSE;
		while ((status = context->GetData(query,&statistics,sizeof(statistics),0)) == S_FALSE
			&& GetTickCount64() < deadline) Sleep(1);
		CHECK_EQ(status,S_OK);
		return status == S_OK ? statistics.IAPrimitives : ~0ull;
	}
};

inline std::vector<std::uintptr_t> ContextState(ID3D11DeviceContext *context)
{
	std::vector<std::uintptr_t> result;
	auto pointer = [&result](IUnknown *object) {
		result.push_back(reinterpret_cast<std::uintptr_t>(object));
		if (object != NULL) object->Release();
	};
	auto bytes = [&result](const void *source, std::size_t size) {
		const unsigned char *data = static_cast<const unsigned char *>(source);
		for (std::size_t i = 0; i < size; ++i) result.push_back(data[i]);
	};
	ID3D11VertexShader *vs = NULL; ID3D11HullShader *hs = NULL; ID3D11DomainShader *ds = NULL;
	ID3D11GeometryShader *gs = NULL; ID3D11PixelShader *ps = NULL; ID3D11ComputeShader *cs = NULL;
	context->VSGetShader(&vs,NULL,NULL); context->HSGetShader(&hs,NULL,NULL);
	context->DSGetShader(&ds,NULL,NULL); context->GSGetShader(&gs,NULL,NULL);
	context->PSGetShader(&ps,NULL,NULL); context->CSGetShader(&cs,NULL,NULL);
	pointer(vs); pointer(hs); pointer(ds); pointer(gs); pointer(ps); pointer(cs);
	typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*ResourceGetter)(UINT,UINT,ID3D11ShaderResourceView **);
	typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*SamplerGetter)(UINT,UINT,ID3D11SamplerState **);
	typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*BufferGetter)(UINT,UINT,ID3D11Buffer **);
	const ResourceGetter resourceGetters[] = {&ID3D11DeviceContext::VSGetShaderResources,
		&ID3D11DeviceContext::HSGetShaderResources,&ID3D11DeviceContext::DSGetShaderResources,
		&ID3D11DeviceContext::GSGetShaderResources,&ID3D11DeviceContext::PSGetShaderResources,
		&ID3D11DeviceContext::CSGetShaderResources};
	const SamplerGetter samplerGetters[] = {&ID3D11DeviceContext::VSGetSamplers,
		&ID3D11DeviceContext::HSGetSamplers,&ID3D11DeviceContext::DSGetSamplers,
		&ID3D11DeviceContext::GSGetSamplers,&ID3D11DeviceContext::PSGetSamplers,
		&ID3D11DeviceContext::CSGetSamplers};
	const BufferGetter bufferGetters[] = {&ID3D11DeviceContext::VSGetConstantBuffers,
		&ID3D11DeviceContext::HSGetConstantBuffers,&ID3D11DeviceContext::DSGetConstantBuffers,
		&ID3D11DeviceContext::GSGetConstantBuffers,&ID3D11DeviceContext::PSGetConstantBuffers,
		&ID3D11DeviceContext::CSGetConstantBuffers};
	for (unsigned stage = 0; stage < 6; ++stage) {
		ID3D11ShaderResourceView *resources[16] = {}; ID3D11SamplerState *samplers[16] = {};
		ID3D11Buffer *buffers[4] = {};
		(context->*resourceGetters[stage])(0,16,resources);
		(context->*samplerGetters[stage])(0,16,samplers);
		(context->*bufferGetters[stage])(0,4,buffers);
		for (unsigned i = 0; i < 16; ++i) { pointer(resources[i]); pointer(samplers[i]); }
		for (unsigned i = 0; i < 4; ++i) pointer(buffers[i]);
	}
	ID3D11InputLayout *layout = NULL; ID3D11Buffer *vertex = NULL, *index = NULL;
	UINT stride = 0, offset = 0, indexOffset = 0; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
	context->IAGetInputLayout(&layout); context->IAGetVertexBuffers(0,1,&vertex,&stride,&offset);
	context->IAGetIndexBuffer(&index,&format,&indexOffset); context->IAGetPrimitiveTopology(&topology);
	pointer(layout); pointer(vertex); pointer(index); result.push_back(stride); result.push_back(offset);
	result.push_back(indexOffset); result.push_back(format); result.push_back(topology);
	ID3D11RenderTargetView *targets[8] = {}; ID3D11DepthStencilView *depthTarget = NULL;
	context->OMGetRenderTargets(8,targets,&depthTarget);
	for (unsigned i = 0; i < 8; ++i) pointer(targets[i]); pointer(depthTarget);
	ID3D11BlendState *blend = NULL; ID3D11DepthStencilState *depth = NULL; ID3D11RasterizerState *raster = NULL;
	float factors[4] = {}; UINT mask = 0, stencil = 0;
	context->OMGetBlendState(&blend,factors,&mask); context->OMGetDepthStencilState(&depth,&stencil);
	context->RSGetState(&raster); pointer(blend); pointer(depth); pointer(raster);
	bytes(factors,sizeof(factors)); result.push_back(mask); result.push_back(stencil);
	D3D11_VIEWPORT viewports[16] = {}; D3D11_RECT scissors[16] = {};
	UINT viewportCount = 16, scissorCount = 16;
	context->RSGetViewports(&viewportCount,viewports); context->RSGetScissorRects(&scissorCount,scissors);
	result.push_back(viewportCount); bytes(viewports,viewportCount * sizeof(viewports[0]));
	result.push_back(scissorCount); bytes(scissors,scissorCount * sizeof(scissors[0]));
	return result;
}

}
