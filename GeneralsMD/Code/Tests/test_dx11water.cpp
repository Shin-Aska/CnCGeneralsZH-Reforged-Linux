#include "test_harness.h"

#include "Platform/WaterGpuShaders.h"
#include "engineshader.h"
#include "ffshader.h"
#include "ffvertex.h"

#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

using WaterGpuShaders::DebugMode;
using WaterGpuShaders::Surface;
using WaterGpuShaders::Target;
using WaterGpuShaders::Variant;
using WaterGpuShaders::Vertex;

typedef HRESULT(WINAPI *CompileFunction)(LPCVOID, SIZE_T, LPCSTR,
	const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT,
	ID3DBlob **, ID3DBlob **);
typedef HRESULT(WINAPI *ReflectFunction)(LPCVOID, SIZE_T, REFIID, void **);

struct Compiler
{
	CompileFunction compile = nullptr;
	ReflectFunction reflect = nullptr;
};

unsigned compiled_blobs = 0;

Compiler load_compiler()
{
	HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
	Compiler result;
	if (module != nullptr) {
		result.compile = reinterpret_cast<CompileFunction>(GetProcAddress(module, "D3DCompile"));
		result.reflect = reinterpret_cast<ReflectFunction>(GetProcAddress(module, "D3DReflect"));
	}
	return result;
}

ID3DBlob *compile_shader(const Compiler &compiler, const std::string &source,
	const char *name, const char *profile)
{
	const char *artifact_directory = std::getenv("ZH_TEST_SHADER_ARTIFACT_DIR");
	std::string artifact_stem;
	if (artifact_directory != nullptr) {
		std::string file_name(name);
		for (char &character : file_name) {
			if (character == ':' || character == '/' || character == '\\') character = '_';
		}
		artifact_stem = std::string(artifact_directory) + "\\" + file_name;
		std::ofstream artifact(artifact_stem + ".hlsl",
			std::ios::binary);
		artifact.write(source.data(), static_cast<std::streamsize>(source.size()));
	}
	ID3DBlob *code = nullptr;
	ID3DBlob *errors = nullptr;
	const HRESULT result = compiler.compile(source.data(), source.size(), name, nullptr, nullptr,
		"main", profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION,
		0, &code, &errors);
	if (FAILED(result) && errors != nullptr) {
		std::printf("  %s (%s): %s\n", name, profile,
			static_cast<const char *>(errors->GetBufferPointer()));
	}
	if (errors != nullptr) errors->Release();
	if (SUCCEEDED(result) && code != nullptr) {
		++compiled_blobs;
		if (!artifact_stem.empty()) {
			std::ofstream artifact(artifact_stem + ".cso", std::ios::binary);
			artifact.write(static_cast<const char *>(code->GetBufferPointer()),
				static_cast<std::streamsize>(code->GetBufferSize()));
		}
	}
	return SUCCEEDED(result) ? code : nullptr;
}

ID3D11ShaderReflection *reflect_shader(const Compiler &compiler, ID3DBlob *code)
{
	ID3D11ShaderReflection *reflection = nullptr;
	const HRESULT result = compiler.reflect(code->GetBufferPointer(), code->GetBufferSize(),
		__uuidof(ID3D11ShaderReflection), reinterpret_cast<void **>(&reflection));
	CHECK(SUCCEEDED(result));
	return reflection;
}

bool binding(ID3D11ShaderReflection *reflection, const char *name,
	D3D_SHADER_INPUT_TYPE type, UINT slot)
{
	D3D11_SHADER_INPUT_BIND_DESC description = {};
	return SUCCEEDED(reflection->GetResourceBindingDescByName(name, &description))
		&& description.Type == type && description.BindPoint == slot && description.BindCount == 1;
}

bool texture_array_binding(ID3D11ShaderReflection *reflection, const char *name, UINT slot)
{
	D3D11_SHADER_INPUT_BIND_DESC description = {};
	return SUCCEEDED(reflection->GetResourceBindingDescByName(name, &description))
		&& description.Type == D3D_SIT_TEXTURE
		&& description.Dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY
		&& description.BindPoint == slot && description.BindCount == 1;
}

bool no_binding(ID3D11ShaderReflection *reflection, UINT slot)
{
	D3D11_SHADER_DESC shader = {};
	if (FAILED(reflection->GetDesc(&shader))) return false;
	for (UINT index = 0; index < shader.BoundResources; ++index) {
		D3D11_SHADER_INPUT_BIND_DESC resource = {};
		if (SUCCEEDED(reflection->GetResourceBindingDesc(index, &resource))
			&& resource.Type != D3D_SIT_SAMPLER && resource.BindPoint == slot) return false;
	}
	return true;
}

bool constant_buffer(ID3D11ShaderReflection *reflection, const char *name,
	UINT slot, UINT bytes)
{
	ID3D11ShaderReflectionConstantBuffer *buffer = reflection->GetConstantBufferByName(name);
	D3D11_SHADER_BUFFER_DESC description = {};
	return buffer != nullptr && SUCCEEDED(buffer->GetDesc(&description))
		&& description.Size == bytes && binding(reflection, name, D3D_SIT_CBUFFER, slot);
}

bool common_surface_contract(ID3D11ShaderReflection *reflection, bool mesh_contact)
{
	return constant_buffer(reflection, "CombinerConstants", 0,
			(mesh_contact ? 202u : 194u) * 16)
		&& (!mesh_contact || (constant_buffer(reflection, "WaterVisualBlock0", 1, 256 * 16)
			&& constant_buffer(reflection, "WaterVisualBlock1", 2, 256 * 16)
			&& constant_buffer(reflection, "WaterVisualBlock2", 3, 192 * 16)));
}

bool surface_resource_contract(ID3D11ShaderReflection *reflection, bool coastal_surf)
{
	return binding(reflection, "Texture0", D3D_SIT_TEXTURE, 0)
		&& binding(reflection, "Texture1", D3D_SIT_TEXTURE, 1)
		&& binding(reflection, "Texture2", D3D_SIT_TEXTURE, 2)
		&& binding(reflection, "Texture3", D3D_SIT_TEXTURE, 3)
		&& binding(reflection, "Texture4", D3D_SIT_TEXTURE, 4)
		&& binding(reflection, "Texture5", D3D_SIT_TEXTURE, 5)
		&& binding(reflection, "Texture6", D3D_SIT_TEXTURE, 6)
		&& binding(reflection, "Texture7", D3D_SIT_TEXTURE, 7)
		&& texture_array_binding(reflection, "OceanMaps", 8)
		&& binding(reflection, "ShadowMap", D3D_SIT_TEXTURE, 9)
		&& binding(reflection, "SmokeMap", D3D_SIT_TEXTURE, 10)
		&& no_binding(reflection, 11) && no_binding(reflection, 12) && no_binding(reflection, 13)
		&& binding(reflection, "Texture14", D3D_SIT_TEXTURE, 14)
		&& (!coastal_surf || binding(reflection, "Texture15", D3D_SIT_TEXTURE, 15));
}

bool diagnostic_contract(ID3D11ShaderReflection *reflection)
{
	ID3D11ShaderReflectionConstantBuffer *common=reflection->GetConstantBufferByName("CombinerConstants");
	D3D11_SHADER_BUFFER_DESC common_description = {};
	if (common==nullptr || FAILED(common->GetDesc(&common_description))
		|| (common_description.Size!=194u*16 && common_description.Size!=202u*16)
		|| !binding(reflection,"CombinerConstants",D3D_SIT_CBUFFER,0)) return false;
	D3D11_SHADER_DESC shader = {};
	if (FAILED(reflection->GetDesc(&shader))) return false;
	for (UINT index=0;index<shader.BoundResources;++index) {
		D3D11_SHADER_INPUT_BIND_DESC resource = {};
		if (FAILED(reflection->GetResourceBindingDesc(index,&resource))) return false;
		if (resource.Type==D3D_SIT_SAMPLER) {
			if (resource.BindPoint>15 || (resource.BindPoint>=11 && resource.BindPoint<=13)) return false;
			continue;
		}
		if (resource.Type==D3D_SIT_CBUFFER) {
			if (resource.BindPoint==0) continue;
			const UINT sizes[]={0,256u*16,256u*16,192u*16};
			if (resource.BindPoint>3) return false;
			ID3D11ShaderReflectionConstantBuffer *buffer=reflection->GetConstantBufferByName(resource.Name);
			D3D11_SHADER_BUFFER_DESC description = {};
			if (buffer==nullptr || FAILED(buffer->GetDesc(&description))
				|| description.Size!=sizes[resource.BindPoint]) return false;
			continue;
		}
		if (resource.Type!=D3D_SIT_TEXTURE || resource.BindPoint>15
			|| (resource.BindPoint>=11 && resource.BindPoint<=13)) return false;
		if (resource.BindPoint==8) {
			if (std::strcmp(resource.Name,"OceanMaps")!=0
				|| resource.Dimension!=D3D_SRV_DIMENSION_TEXTURE2DARRAY) return false;
		} else if (resource.Dimension!=D3D_SRV_DIMENSION_TEXTURE2D) return false;
	}
	return true;
}

void print_reflection(const char *variant,ID3D11ShaderReflection *reflection)
{
	D3D11_SHADER_DESC shader = {};
	if (FAILED(reflection->GetDesc(&shader))) return;
	std::printf("  reflection mismatch %s (%u resources)\n",variant,shader.BoundResources);
	for (UINT index=0;index<shader.BoundResources;++index) {
		D3D11_SHADER_INPUT_BIND_DESC resource = {};
		if (SUCCEEDED(reflection->GetResourceBindingDesc(index,&resource)))
			std::printf("    %s type=%u slot=%u count=%u dimension=%u\n",resource.Name,
				static_cast<unsigned>(resource.Type),resource.BindPoint,resource.BindCount,
				static_cast<unsigned>(resource.Dimension));
	}
	for (UINT index=0;index<shader.ConstantBuffers;++index) {
		ID3D11ShaderReflectionConstantBuffer *buffer=reflection->GetConstantBufferByIndex(index);
		D3D11_SHADER_BUFFER_DESC description = {};
		if (buffer!=nullptr && SUCCEEDED(buffer->GetDesc(&description)))
			std::printf("    cbuffer %s bytes=%u\n",description.Name,description.Size);
	}
}

unsigned reflected_variants = 0;
unsigned reflected_production_variants = 0;
bool reflected_common_contract = true;
bool reflected_resource_contract = true;
bool reflected_diagnostic_contract = true;
bool rejected_missing_common = false;
bool rejected_wrong_ocean = false;

PixelPipelineDescription plain_pixel_pipeline()
{
	PixelPipelineDescription result = {};
	result.AlphaFunction = D3DCMP_ALWAYS;
	return result;
}

CombinerDescription plain_shore_combiner()
{
	CombinerDescription result = {};
	result.StageCount = 2;
	for (unsigned stage = 0; stage < result.StageCount; ++stage) {
		result.Stages[stage].ColourOperation = D3DTOP_SELECTARG1;
		result.Stages[stage].ColourArgument1 = D3DTA_TEXTURE;
		result.Stages[stage].ColourArgument2 = D3DTA_CURRENT;
		result.Stages[stage].AlphaOperation = D3DTOP_SELECTARG1;
		result.Stages[stage].AlphaArgument1 = D3DTA_TEXTURE;
		result.Stages[stage].AlphaArgument2 = D3DTA_CURRENT;
		result.Stages[stage].TextureCoordinateIndex = stage;
		result.Stages[stage].TextureBound = true;
	}
	result.PixelPipeline = plain_pixel_pipeline();
	return result;
}

VertexPipelineDescription water_vertex_description()
{
	VertexPipelineDescription result = {};
	result.FVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
	result.ColourVertexEnabled = true;
	result.DiffuseMaterialSource = D3DMCS_COLOR1;
	result.AmbientMaterialSource = D3DMCS_MATERIAL;
	result.EmissiveMaterialSource = D3DMCS_MATERIAL;
	result.SpecularMaterialSource = D3DMCS_MATERIAL;
	result.StageCount = 2;
	result.Stages[0].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU;
	result.Stages[1].TextureCoordinateIndex = D3DTSS_TCI_PASSTHRU | 1;
	result.Stages[0].TextureTransformFlags = D3DTTFF_DISABLE;
	result.Stages[1].TextureTransformFlags = D3DTTFF_DISABLE;
	result.FogVertexMode = D3DFOG_NONE;
	return result;
}

Variant flat_variant()
{
	Variant result = {};
	result.target = Target::NativeD3D11;
	result.surface = Surface::Flat;
	result.vertex = Vertex::Fft;
	result.shoreFoam = true;
	result.bowShoulder = true;
	result.materialFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX2;
	return result;
}

const DebugMode DEBUG_VARIANTS[] = {
	DebugMode::None, DebugMode::Depth, DebugMode::Bed, DebugMode::Reflection,
	DebugMode::Normal, DebugMode::Highlight, DebugMode::Offshore, DebugMode::WaveCage,
	DebugMode::CageWash, DebugMode::ShoreHistory, DebugMode::ShoreHistoryTerms,
	DebugMode::Shore, DebugMode::ShoreRaw, DebugMode::ShoreSourceTerms,
	DebugMode::Foam, DebugMode::Vessel, DebugMode::Pressure, DebugMode::ResolvedNormal,
	DebugMode::Roughness, DebugMode::PressureRetention, DebugMode::Footprint,
	DebugMode::FootprintPath, DebugMode::FootprintColour, DebugMode::FootprintReasons,
	DebugMode::FootprintOverlay, DebugMode::HullDepth, DebugMode::HullDepthSuppression,
	DebugMode::HullDepthHeight, DebugMode::ContactMesh, DebugMode::ContactMeshAlpha,
	DebugMode::ContactMaterial, DebugMode::ContactLighting, DebugMode::ContactMeshQuery,
	DebugMode::ContactMeshSources, DebugMode::BowRetention, DebugMode::CoastalSurf,
	DebugMode::CoastalDensity, DebugMode::CoastalMaterial
};

}

TEST(dx11water_variant_key_covers_every_shader_control_and_rejects_invalid_combinations)
{
	Variant base = flat_variant();
	CHECK(WaterGpuShaders::Valid(base));
	const std::string key = WaterGpuShaders::Key(base);

	Variant changed = base;
	changed.meshContact = true;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.coastalSurf = true;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.coastalCrest = true;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.shoreFoam = false;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.bowLift = true;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.bowShoulder = false;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.debug = DebugMode::Depth;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; ++changed.materialFvf;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.target = Target::Sdl3Gpu;
	CHECK_NE(key, WaterGpuShaders::Key(changed));
	changed = base; changed.coastalCrest = true;
	CHECK(!WaterGpuShaders::Valid(changed));
	changed = base; changed.surface = Surface::River;
	CHECK(!WaterGpuShaders::Valid(changed));

	std::string source = "unchanged";
	CHECK(!WaterGpuShaders::GenerateSurfacePixel(changed, plain_pixel_pipeline(), source));
	CHECK(source.empty());
}

TEST(dx11water_environment_snapshot_is_immutable_after_capture)
{
	const char *surf_value=std::getenv("ZH_WATER_SURF_GPU");
	const char *debug_value=std::getenv("ZH_WATER_DEBUG");
	const bool had_surf=surf_value!=nullptr,had_debug=debug_value!=nullptr;
	const std::string old_surf=surf_value?surf_value:"",old_debug=debug_value?debug_value:"";
	CHECK_EQ(_putenv_s("ZH_WATER_SURF_GPU","0"),0);
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG","depth"),0);
	const WaterGpuShaders::EnvironmentSnapshot snapshot = WaterGpuShaders::SnapshotEnvironment(
		Target::NativeD3D11,flat_variant().materialFvf);
	const std::string key = WaterGpuShaders::Key(snapshot.flat);
	const std::string river_key = WaterGpuShaders::Key(snapshot.river);
	std::string before;
	CHECK(WaterGpuShaders::GenerateSurfacePixel(snapshot.flat, plain_pixel_pipeline(), before));
	CHECK_EQ(_putenv_s("ZH_WATER_SURF_GPU","1"),0);
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG","normal"),0);
	std::string after;
	CHECK(WaterGpuShaders::GenerateSurfacePixel(snapshot.flat, plain_pixel_pipeline(), after));
	CHECK_EQ(key, WaterGpuShaders::Key(snapshot.flat));
	CHECK_EQ(river_key, WaterGpuShaders::Key(snapshot.river));
	CHECK_EQ(before, after);
	CHECK_EQ(_putenv_s("ZH_WATER_SURF_GPU",had_surf?old_surf.c_str():""),0);
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG",had_debug?old_debug.c_str():""),0);
}

TEST(dx11water_legacy_sdl_entry_keeps_its_launch_snapshot_while_explicit_variant_changes)
{
	const char *debug_value=std::getenv("ZH_WATER_DEBUG");
	const bool had_debug=debug_value!=nullptr;
	const std::string old_debug=debug_value?debug_value:"";
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG","depth"),0);
	const WaterGpuShaders::EnvironmentSnapshot launch=WaterGpuShaders::SnapshotEnvironment(
		Target::Sdl3Gpu,flat_variant().materialFvf);
	std::string launch_source;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,plain_pixel_pipeline(),
		launch_source,COMBINER_SHADER_TARGET_SDL3_GPU));
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG","normal"),0);
	std::string stable_source;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,plain_pixel_pipeline(),
		stable_source,COMBINER_SHADER_TARGET_SDL3_GPU));
	CHECK_EQ(launch_source,stable_source);
	WaterGpuShaders::EnvironmentSnapshot changed=launch;
	changed.flat.debug=launch.flat.debug==DebugMode::Normal?DebugMode::Depth:DebugMode::Normal;
	std::string changed_source;
	CHECK(WaterGpuShaders::GenerateSurfacePixel(changed.flat,plain_pixel_pipeline(),changed_source));
	CHECK_NE(WaterGpuShaders::Key(launch.flat),WaterGpuShaders::Key(changed.flat));
	CHECK_NE(launch_source,changed_source);
	CHECK_EQ(_putenv_s("ZH_WATER_DEBUG",had_debug?old_debug.c_str():""),0);
}

TEST(dx11water_native_vertex_preserves_water_metadata_and_reflects_the_fft_contract)
{
	Compiler compiler = load_compiler();
	if (compiler.compile == nullptr || compiler.reflect == nullptr) {
		std::printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	std::string base;
	CHECK(VertexShader_Generate(water_vertex_description(), VERTEX_SHADER_TARGET_D3D11, base));
	Variant variant = flat_variant();
	variant.bowLift = true;
	std::string source;
	CHECK(WaterGpuShaders::GenerateVertex(variant, base, source));
	CHECK(source.find("Output waterFixedVertex(Input input)") != std::string::npos);
	CHECK(source.find("output.TexCoord2=parameter;") != std::string::npos);
	CHECK(source.find("output.Specular=float4(weight,freeDamping,originalHeight,offshore);") != std::string::npos);
	CHECK(source.find("space0") == std::string::npos);

	ID3DBlob *code = compile_shader(compiler, source, "native-water-fft", "vs_5_0");
	CHECK(code != nullptr);
	if (code == nullptr) return;
	ID3D11ShaderReflection *reflection = reflect_shader(compiler, code);
	CHECK(reflection != nullptr);
	if (reflection != nullptr) {
		CHECK(constant_buffer(reflection, "WaterVertexParameters", 1, 194 * 16));
		CHECK(binding(reflection, "FftDisplacement0", D3D_SIT_TEXTURE, 0));
		CHECK(binding(reflection, "FftDisplacement1", D3D_SIT_TEXTURE, 1));
		CHECK(binding(reflection, "FftDisplacement2", D3D_SIT_TEXTURE, 2));
		CHECK(binding(reflection, "WaveCageTerrain", D3D_SIT_TEXTURE, 3));
		reflection->Release();
	}
	code->Release();
}

TEST(dx11water_native_pixel_compiles_and_reflects_every_feature_and_diagnostic_variant)
{
	Compiler compiler = load_compiler();
	if (compiler.compile == nullptr || compiler.reflect == nullptr) {
		std::printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	std::vector<Variant> variants;
	for (DebugMode debug : DEBUG_VARIANTS) {
		Variant variant = flat_variant();
		variant.debug = debug;
		variant.meshContact = WaterGpuShaders::DebugNeedsMeshContact(debug);
		variant.coastalSurf = WaterGpuShaders::DebugNeedsCoastalSurf(debug);
		variant.coastalCrest = debug == DebugMode::CoastalMaterial;
		variant.bowLift = debug == DebugMode::BowRetention;
		CHECK(WaterGpuShaders::Valid(variant));
		variants.push_back(variant);
	}
	Variant feature=flat_variant();feature.meshContact=true;variants.push_back(feature);
	feature=flat_variant();feature.coastalSurf=true;variants.push_back(feature);
	feature.coastalCrest=true;variants.push_back(feature);
	feature=flat_variant();feature.bowLift=true;variants.push_back(feature);

	for (const Variant &variant : variants) {

		std::string source;
		CHECK(WaterGpuShaders::GenerateSurfacePixel(variant, plain_pixel_pipeline(), source));
		CHECK(source.find("space0") == std::string::npos);
		CHECK(source.find("space1") == std::string::npos);
		CHECK(source.find("space2") == std::string::npos);
		CHECK(source.find("space3") == std::string::npos);
		ID3DBlob *code = compile_shader(compiler, source, WaterGpuShaders::Key(variant).c_str(), "ps_5_0");
		CHECK(code != nullptr);
		if (code == nullptr) continue;
		ID3D11ShaderReflection *reflection = reflect_shader(compiler, code);
		CHECK(reflection != nullptr);
		if (reflection != nullptr) {
			const bool production=variant.debug==DebugMode::None;
			const bool common = production ? common_surface_contract(reflection, variant.meshContact)
				: diagnostic_contract(reflection);
			const bool resources = production ? surface_resource_contract(reflection, variant.coastalSurf)
				: diagnostic_contract(reflection);
			if (!common || !resources) print_reflection(WaterGpuShaders::Key(variant).c_str(),reflection);
			CHECK(common);
			CHECK(resources);
			reflected_common_contract = reflected_common_contract && common;
			reflected_resource_contract = reflected_resource_contract && resources;
			reflected_diagnostic_contract = reflected_diagnostic_contract && diagnostic_contract(reflection);
			if(production) ++reflected_production_variants;
			++reflected_variants;
			reflection->Release();
		}
		code->Release();
	}
}

TEST(dx11water_reflection_contract_rejects_missing_common_buffer_and_wrong_ocean_type)
{
	Compiler compiler = load_compiler();
	if (compiler.compile == nullptr || compiler.reflect == nullptr) return;
	const std::string missing_common =
		"float4 main(float4 position:SV_Position):SV_Target{return position;}";
	ID3DBlob *missing_code = compile_shader(compiler, missing_common, "negative-missing-common", "ps_5_0");
	CHECK(missing_code != nullptr);
	if (missing_code != nullptr) {
		ID3D11ShaderReflection *reflection = reflect_shader(compiler, missing_code);
		CHECK(reflection != nullptr);
		if (reflection != nullptr) {
			rejected_missing_common = !common_surface_contract(reflection, false);
			CHECK(rejected_missing_common);
			reflection->Release();
		}
		missing_code->Release();
	}

	const std::string wrong_ocean =
		"Texture2D OceanMaps:register(t8);SamplerState OceanSampler:register(s8);"
		"float4 main(float4 p:SV_Position):SV_Target{return OceanMaps.SampleLevel(OceanSampler,p.xy,0);}";
	ID3DBlob *wrong_code = compile_shader(compiler, wrong_ocean, "negative-wrong-ocean", "ps_5_0");
	CHECK(wrong_code != nullptr);
	if (wrong_code != nullptr) {
		ID3D11ShaderReflection *reflection = reflect_shader(compiler, wrong_code);
		CHECK(reflection != nullptr);
		if (reflection != nullptr) {
			rejected_wrong_ocean = !texture_array_binding(reflection, "OceanMaps", 8);
			CHECK(rejected_wrong_ocean);
			reflection->Release();
		}
		wrong_code->Release();
	}
}

TEST(dx11water_native_river_and_shore_use_the_canonical_native_declarations)
{
	Compiler compiler = load_compiler();
	if (compiler.compile == nullptr || compiler.reflect == nullptr) {
		std::printf("  d3dcompiler_47.dll not present, skipping\n");
		return;
	}

	Variant river = flat_variant();
	river.surface = Surface::River;
	river.vertex = Vertex::Analytic;
	std::string river_source;
	CHECK(WaterGpuShaders::GenerateSurfacePixel(river, plain_pixel_pipeline(), river_source));
	ID3DBlob *river_code = compile_shader(compiler, river_source, "native-water-river", "ps_5_0");
	CHECK(river_code != nullptr);
	if (river_code != nullptr) river_code->Release();

	std::string shore_base;
	CHECK(CombinerShader_Generate(plain_shore_combiner(), COMBINER_SHADER_TARGET_D3D11, shore_base));
	Variant shore = flat_variant();
	shore.surface = Surface::Shore;
	shore.vertex = Vertex::Analytic;
	std::string shore_source;
	CHECK(WaterGpuShaders::GenerateShorePixel(shore, shore_base, shore_source));
	CHECK(shore_source.find("space0") == std::string::npos);
	CHECK(shore_source.find("space1") == std::string::npos);
	ID3DBlob *shore_code = compile_shader(compiler, shore_source, "native-water-shore", "ps_5_0");
	CHECK(shore_code != nullptr);
	if (shore_code == nullptr) return;
	ID3D11ShaderReflection *reflection = reflect_shader(compiler, shore_code);
	CHECK(reflection != nullptr);
	if (reflection != nullptr) {
		CHECK(constant_buffer(reflection, "CombinerConstants", 0, 11 * 16));
		reflection->Release();
	}
	shore_code->Release();
}

TEST(dx11water_writes_a_machine_readable_reflection_verdict)
{
	const char *artifact_directory = std::getenv("ZH_TEST_SHADER_ARTIFACT_DIR");
	if (artifact_directory == nullptr) return;
	std::ofstream artifact(std::string(artifact_directory) + "\\reflection.json", std::ios::binary);
	artifact << "{\n  \"nativePixelVariants\": " << reflected_variants
		<< ",\n  \"productionPixelVariants\": " << reflected_production_variants
		<< ",\n  \"compiledBlobs\": " << compiled_blobs
		<< ",\n  \"profiles\": {\"vertex\": \"vs_5_0\", \"pixel\": \"ps_5_0\"}"
		<< ",\n  \"commonBufferContract\": " << (reflected_common_contract ? "true" : "false")
		<< ",\n  \"resourceContract\": " << (reflected_resource_contract ? "true" : "false")
		<< ",\n  \"diagnosticReflectedTypeContract\": " << (reflected_diagnostic_contract ? "true" : "false")
		<< ",\n  \"constantBuffers\": {\"VS\": {\"b1\": 3104}, \"PS\": {\"b0\": [3104, 3232], \"b1\": 4096, \"b2\": 4096, \"b3\": 3072}, \"shore\": {\"b0\": 176}}"
		<< ",\n  \"vertexResources\": {\"t0\": \"Texture2D\", \"t1\": \"Texture2D\", \"t2\": \"Texture2D\", \"t3\": \"Texture2D\"}"
		<< ",\n  \"pixelResources\": {\"t0-t7\": \"Texture2D\", \"t8\": \"Texture2DArray\", \"t9-t10\": \"Texture2D\", \"t11-t13\": \"unbound\", \"t14-t15\": \"Texture2D\"}"
		<< ",\n  \"negativeMissingCommonRejected\": " << (rejected_missing_common ? "true" : "false")
		<< ",\n  \"negativeWrongOceanTypeRejected\": " << (rejected_wrong_ocean ? "true" : "false")
		<< "\n}\n";
	CHECK(artifact.good());
}

