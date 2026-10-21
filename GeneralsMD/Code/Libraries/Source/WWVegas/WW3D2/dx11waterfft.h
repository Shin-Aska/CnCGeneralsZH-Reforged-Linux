#ifndef DX11WATERFFT_H
#define DX11WATERFFT_H
#include "dx11water.h"
#include <functional>
#include <string>

class DX11WaterFft
{
public:
	using Compiler = std::function<bool(const std::string &, const char *, const char *, std::vector<unsigned char> &)>;
	DX11WaterFft();
	~DX11WaterFft();
	bool Prepare(ID3D11Device *device, ID3D11DeviceContext *context,
		const Compiler &compile, float phase_time);
	void Release();
	void Reset();
	void Fail(DX11WaterClass::FftTestFailure stage, unsigned ordinal);
	bool Initial(const float *values, std::size_t count);
	bool Fields(ID3D11ShaderResourceView *a, ID3D11ShaderResourceView *b,
		ID3D11ShaderResourceView *c, ID3D11ShaderResourceView *array);
	bool HasFields() const { return Override[0] != NULL; }
	bool SurfaceFailure() const { return Failure == DX11WaterClass::FftTestFailure::SurfacePipeline; }
	ID3D11ShaderResourceView *Map(unsigned index) const;
	ID3D11ShaderResourceView *Array() const;
	ID3D11SamplerState *Sampler() const;
	DX11WaterClass::FftTestState State;
private:
	struct Resources;
	Resources *Data;
	DX11WaterClass::FftTestFailure Failure;
	unsigned FailureOrdinal;
	std::vector<float> InitialOverride;
	ID3D11ShaderResourceView *Override[4];
	bool Step(DX11WaterClass::FftTestFailure stage);
	bool Create(ID3D11Device *device, const Compiler &compile);
	bool Pass(ID3D11DeviceContext *context, unsigned shader, ID3D11RenderTargetView **targets,
		ID3D11ShaderResourceView **sources, unsigned count, const float *parameters, unsigned side);
	bool Copy(ID3D11DeviceContext *context, ID3D11Texture2D *destination, unsigned subresource,
		ID3D11Texture2D *source, unsigned source_subresource, unsigned side);
};
#endif
