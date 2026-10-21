#include "dx11waterfft.h"
#include "dx11backend.h"
#include "Platform/WaterFftSpectrum.h"
#include <cmath>
#include <cstring>
#include <memory>

namespace {
template<class T> void drop(T *&value) { if (value) value->Release(); value = NULL; }
std::string native_source(std::string source)
{
	const char *spaces[] = {", space2", ", space3"};
	for (const char *space : spaces) {
		std::size_t at;
		while ((at = source.find(space)) != std::string::npos) source.erase(at, std::strlen(space));
	}
	return source;
}
struct Field
{
	ID3D11Texture2D *texture;
	ID3D11ShaderResourceView *view;
	ID3D11RenderTargetView *target;
	Field() : texture(NULL), view(NULL), target(NULL) {}
	~Field() { drop(target); drop(view); drop(texture); }
};
}

struct DX11WaterFft::Resources
{
	Field initial[3], butterfly, ping[2][6], maps[6], array;
	ID3D11VertexShader *vertex;
	ID3D11PixelShader *pixels[4];
	ID3D11SamplerState *point, *linear;
	ID3D11Buffer *constants;
	ID3D11BlendState *blend;
	ID3D11DepthStencilState *depth;
	ID3D11RasterizerState *rasterizer;
	Resources() : vertex(NULL), point(NULL), linear(NULL), constants(NULL), blend(NULL),
		depth(NULL), rasterizer(NULL) { std::memset(pixels, 0, sizeof(pixels)); }
	~Resources() {
		drop(vertex); for (auto &pixel : pixels) drop(pixel);
		drop(point); drop(linear); drop(constants); drop(blend); drop(depth); drop(rasterizer);
	}
};

DX11WaterFft::DX11WaterFft() : Data(NULL), Failure(DX11WaterClass::FftTestFailure::None), FailureOrdinal(0)
{ std::memset(&State, 0, sizeof(State)); std::memset(Override, 0, sizeof(Override)); }
DX11WaterFft::~DX11WaterFft() { Release(); for (auto &view : Override) drop(view); }
void DX11WaterFft::Release() { delete Data; Data = NULL; State.ready = false; }
void DX11WaterFft::Reset()
{
	Release(); for (auto &view : Override) drop(view); InitialOverride.clear();
	Failure = DX11WaterClass::FftTestFailure::None; FailureOrdinal = 0;
	std::memset(&State, 0, sizeof(State));
}
void DX11WaterFft::Fail(DX11WaterClass::FftTestFailure stage, unsigned ordinal)
{ Failure = stage; FailureOrdinal = ordinal; }
bool DX11WaterFft::Step(DX11WaterClass::FftTestFailure stage)
{
	unsigned *count = stage == DX11WaterClass::FftTestFailure::Allocation ? &State.allocationCount
		: stage == DX11WaterClass::FftTestFailure::Compile ? &State.compileCount
		: stage == DX11WaterClass::FftTestFailure::Draw ? &State.drawCount : &State.copyCount;
	++*count;
	return Failure != stage || *count != FailureOrdinal;
}
bool DX11WaterFft::Initial(const float *values, std::size_t count)
{
	if (values == NULL && count == 0) { InitialOverride.clear(); Release(); return true; }
	if (!values || count != 3u * 256u * 256u * 4u) return false;
	for (std::size_t i = 0; i < count; ++i) if (!std::isfinite(values[i])) return false;
	InitialOverride.assign(values, values + count); Release(); ++State.generation; return true;
}
bool DX11WaterFft::Fields(ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b,
	ID3D11ShaderResourceView *c, ID3D11ShaderResourceView *array)
{
	ID3D11ShaderResourceView *views[4] = {a,b,c,array};
	if (a || b || c || array) for (unsigned i = 0; i < 4; ++i) {
		if (!views[i]) return false;
		D3D11_SHADER_RESOURCE_VIEW_DESC desc = {}; views[i]->GetDesc(&desc);
		if (desc.Format != DXGI_FORMAT_R32G32B32A32_FLOAT
			|| (i < 3 ? desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D
				: desc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2DARRAY
					|| desc.Texture2DArray.ArraySize != 6)) return false;
	}
	for (unsigned i = 0; i < 4; ++i) { if (views[i]) views[i]->AddRef(); drop(Override[i]); Override[i] = views[i]; }
	++State.generation;
	return true;
}
ID3D11ShaderResourceView *DX11WaterFft::Map(unsigned index) const
{ return index < 6 ? (HasFields() && index % 2 == 0 ? Override[index / 2] : State.ready && Data ? Data->maps[index].view : NULL) : NULL; }
ID3D11ShaderResourceView *DX11WaterFft::Array() const
{ return HasFields() ? Override[3] : State.ready && Data ? Data->array.view : NULL; }
ID3D11SamplerState *DX11WaterFft::Sampler() const { return Data ? Data->linear : NULL; }

bool DX11WaterFft::Create(ID3D11Device *device, const Compiler &compile)
{
	using namespace WaterFftSpectrum;
	State.allocationCount = State.compileCount = 0;
	if (Failure == DX11WaterClass::FftTestFailure::FormatUnsupported) return false;
	UINT support = 0;
	const UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | D3D11_FORMAT_SUPPORT_RENDER_TARGET;
	if (FAILED(device->CheckFormatSupport(DXGI_FORMAT_R32G32B32A32_FLOAT, &support)) || (support & required) != required) return false;
	std::unique_ptr<Resources> next(new Resources);
	const std::vector<float> initial = InitialOverride.empty() ? InitialSpectrum() : InitialOverride;
	const std::vector<float> butterfly = Butterfly();
	auto field = [&](Field &f, unsigned width, unsigned mips, unsigned layers, bool render, const float *data) {
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width; desc.Height = Resolution; desc.MipLevels = mips; desc.ArraySize = layers;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count = 1;
		desc.Usage = data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (render ? D3D11_BIND_RENDER_TARGET : 0);
		D3D11_SUBRESOURCE_DATA upload = {data, width * 16u, 0};
		if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateTexture2D(&desc, data ? &upload : NULL, &f.texture))) return false;
		if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateShaderResourceView(f.texture, NULL, &f.view))) return false;
		if (render) {
			D3D11_RENDER_TARGET_VIEW_DESC target = {}; target.Format = desc.Format; target.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateRenderTargetView(f.texture, &target, &f.target))) return false;
		}
		return true;
	};
	for (unsigned i = 0; i < 3; ++i) if (!field(next->initial[i], Resolution, 1, 1, false, initial.data() + i * Resolution * Resolution * 4)) return false;
	if (!field(next->butterfly, Phases, 1, 1, false, butterfly.data())) return false;
	for (auto &bank : next->ping) for (auto &f : bank) if (!field(f, Resolution, 1, 1, true, NULL)) return false;
	for (auto &f : next->maps) if (!field(f, Resolution, Phases + 1, 1, true, NULL)) return false;
	if (!field(next->array, Resolution, Phases + 1, Maps, false, NULL)) return false;
	std::vector<unsigned char> code;
	if (!Step(DX11WaterClass::FftTestFailure::Compile) || !compile(VertexHlsl(), "water-fft-vs", "vs_5_0", code)
		|| !Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateVertexShader(code.data(), code.size(), NULL, &next->vertex))) return false;
	const std::string sources[4] = {SpectrumHlsl(), ButterflyHlsl(), PostHlsl(), MipHlsl()};
	const char *names[4] = {"water-fft-spectrum", "water-fft-butterfly", "water-fft-post", "water-fft-mip"};
	for (unsigned i = 0; i < 4; ++i) if (!Step(DX11WaterClass::FftTestFailure::Compile)
		|| !compile(native_source(sources[i]), names[i], "ps_5_0", code)
		|| !Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreatePixelShader(code.data(), code.size(), NULL, &next->pixels[i]))) return false;
	D3D11_SAMPLER_DESC sampler = {}; sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampler.MaxLOD = float(Phases); sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateSamplerState(&sampler, &next->point))) return false;
	sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateSamplerState(&sampler, &next->linear))) return false;
	D3D11_BUFFER_DESC constants = {}; constants.ByteWidth = 32; constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER; constants.Usage = D3D11_USAGE_DEFAULT;
	if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateBuffer(&constants, NULL, &next->constants))) return false;
	D3D11_BLEND_DESC blend = {}; for (auto &target : blend.RenderTarget) target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	D3D11_DEPTH_STENCIL_DESC depth = {}; depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
	D3D11_RASTERIZER_DESC rasterizer = {}; rasterizer.FillMode = D3D11_FILL_SOLID; rasterizer.CullMode = D3D11_CULL_NONE; rasterizer.DepthClipEnable = TRUE;
	if (!Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateBlendState(&blend, &next->blend))
		|| !Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateDepthStencilState(&depth, &next->depth))
		|| !Step(DX11WaterClass::FftTestFailure::Allocation) || FAILED(device->CreateRasterizerState(&rasterizer, &next->rasterizer))) return false;
	Data = next.release(); return true;
}

bool DX11WaterFft::Pass(ID3D11DeviceContext *context, unsigned shader, ID3D11RenderTargetView **targets,
	ID3D11ShaderResourceView **sources, unsigned count, const float *parameters, unsigned side)
{
	if (!Step(DX11WaterClass::FftTestFailure::Draw)) return false;
	if (Failure == DX11WaterClass::FftTestFailure::DeviceLostDuringFft && State.drawCount == FailureOrdinal) {
		State.deviceLost = true; return false;
	}
	context->UpdateSubresource(Data->constants, 0, NULL, parameters, 0, 0);
	context->OMSetRenderTargets(6, targets, NULL);
	context->IASetInputLayout(NULL); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context->VSSetShader(Data->vertex, NULL, 0); context->HSSetShader(NULL, NULL, 0);
	context->DSSetShader(NULL, NULL, 0); context->GSSetShader(NULL, NULL, 0); context->PSSetShader(Data->pixels[shader], NULL, 0);
	context->PSSetConstantBuffers(0, 1, &Data->constants); context->PSSetShaderResources(0, count, sources);
	ID3D11SamplerState *samplers[7]; for (auto &sampler : samplers) sampler = Data->point;
	context->PSSetSamplers(0, count, samplers);
	context->OMSetBlendState(Data->blend, NULL, 0xffffffffu); context->OMSetDepthStencilState(Data->depth, 0); context->RSSetState(Data->rasterizer);
	D3D11_VIEWPORT viewport = {0,0,float(side),float(side),0,1}; context->RSSetViewports(1, &viewport);
	context->Draw(3, 0); ++State.draws;
	ID3D11ShaderResourceView *empty[7] = {}; context->PSSetShaderResources(0, 7, empty); context->OMSetRenderTargets(0, NULL, NULL);
	return true;
}
bool DX11WaterFft::Copy(ID3D11DeviceContext *context, ID3D11Texture2D *destination, unsigned subresource,
	ID3D11Texture2D *source, unsigned source_subresource, unsigned side)
{
	if (!Step(DX11WaterClass::FftTestFailure::Copy)) return false;
	D3D11_BOX box = {0,0,0,side,side,1};
	context->CopySubresourceRegion(destination, subresource, 0,0,0, source, source_subresource, &box); ++State.copies; return true;
}
bool DX11WaterFft::Prepare(ID3D11Device *device, ID3D11DeviceContext *context, const Compiler &compile, float time)
{
	using namespace WaterFftSpectrum;
	State.drawCount = State.copyCount = 0;
	State.deviceLost = false;
	if (!std::isfinite(time)) { Release(); return false; }
	if (State.ready && State.time == time) return true;
	++State.generation;
	if (!Data && !Create(device, compile)) {
		State.deviceLost = FAILED(device->GetDeviceRemovedReason()); return false;
	}
	State.ready = false;
	float parameters[8] = {float(Resolution),time,0,0,Sizes[0],Sizes[1],Sizes[2],0};
	ID3D11RenderTargetView *targets[6]; ID3D11ShaderResourceView *sources[7];
	for (unsigned i = 0; i < 6; ++i) targets[i] = Data->ping[0][i].target;
	for (unsigned i = 0; i < 3; ++i) sources[i] = Data->initial[i].view;
	bool ok = Pass(context, 0, targets, sources, 3, parameters, Resolution); unsigned ping = 0;
	for (unsigned axis = 0; ok && axis < 2; ++axis) for (unsigned phase = 0; ok && phase < Phases; ++phase) {
		for (unsigned i = 0; i < 6; ++i) { targets[i] = Data->ping[1-ping][i].target; sources[i] = Data->ping[ping][i].view; }
		sources[6] = Data->butterfly.view; parameters[2] = float(phase); parameters[3] = float(axis);
		ok = Pass(context, 1, targets, sources, 7, parameters, Resolution); ping = 1-ping;
	}
	for (unsigned i = 0; i < 6; ++i) { targets[i] = Data->maps[i].target; sources[i] = Data->ping[ping][i].view; }
	if (ok) ok = Pass(context, 2, targets, sources, 6, parameters, Resolution);
	for (unsigned level = 1; ok && level <= Phases; ++level) {
		parameters[2] = float(level);
		for (unsigned i = 0; i < 6; ++i) { targets[i] = Data->ping[0][i].target; sources[i] = Data->maps[i].view; }
		ok = Pass(context, 3, targets, sources, 6, parameters, Resolution >> level);
		for (unsigned i = 0; ok && i < 6; ++i) ok = Copy(context, Data->maps[i].texture, level, Data->ping[0][i].texture, 0, Resolution >> level);
	}
	for (unsigned layer = 0; ok && layer < 6; ++layer) for (unsigned level = 0; ok && level <= Phases; ++level)
		ok = Copy(context, Data->array.texture, D3D11CalcSubresource(level, layer, Phases + 1), Data->maps[layer].texture, level, Resolution >> level);
	if (FAILED(device->GetDeviceRemovedReason())) State.deviceLost = true;
	if (!ok || State.deviceLost) { Release(); return false; }
	State.time = time; State.ready = true; ++State.updates; return true;
}
