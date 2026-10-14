/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*
 * test_shader_sdl - the POSIX twin of test_ffshadercompile and test_ffvertexcompile: every program
 * the generators write, under the SDL3 GPU target, taken the whole way to what SDL's GPU backends
 * accept (decision 4).
 *
 * For each program in shader_cases.h - the 49 decision 4 was measured on less the three bumped
 * terrain programs that went with the normal maps, and the extras - it:
 *   1. generates the SDL3_GPU text,
 *   2. compiles it to SPIR-V through SDL3_Compile_HLSL_To_SPIRV (the one place the HLSL front end is
 *      named, so this test follows the compiler if decision 4's exit is ever taken),
 *   3. validates the SPIR-V with spirv-val for Vulkan 1.0, when the build found one,
 *   4. translates it to MSL through SDL_shadercross, and checks the slot contract in that MSL: every
 *      [[texture(n)]] has its [[sampler(n)]], and n is below the slot count SDL3_Shader_Slots gives
 *      the device - SDL binds slots 0 to n - 1 as texture-sampler pairs, and a texture outside that
 *      range is one Metal accepts and nothing ever binds,
 *   5. on macOS, has Metal compile it: SDL_CreateGPUShader on a real device, which hands the MSL to
 *      the driver (newLibraryWithSource).  Not the offline `xcrun metal`, which is not installed.
 *
 * A second test, on macOS, links every generated vertex program with every pixel program it meets
 * in the game into a real SDL pipeline: the stages agree by the locations sdl3target.h pins, or Metal
 * says which input is not written.  Which programs meet is the game's pairing, not every pair:
 *   - the engine's Trees vertex program meets ffshader's combiner programs only.  W3DTreeBuffer
 *     binds Trees.vso and no pixel shader (it loads Trees.pso and never binds it), and engineshader
 *     transcribes no Trees pixel program;
 *   - the engine's pixel programs (water, terrain, road, monochrome) meet ffvertex's programs only:
 *     their draws bind a .pso over the fixed-function vertex pipeline.
 * Trees against a water or terrain pixel program does not link - they read coordinate sets 2 and 3
 * and Trees writes two - and the game never draws it.
 *
 * Steps 3 and 5 can be unavailable - no spirv-val on the machine, no GPU in the session - and then
 * they say SKIPPED on a line of their own, with the reason and the count they did not check.  A
 * skipped step is never folded into a pass.
 */
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "sdl3shadercompile.h"
#include "shader_cases.h"
#include "Platform/WaterBedMaterial.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterFftSampling.h"
#include "sdl3target.h"
#include <limits>

namespace {

struct Tally
{
	unsigned programs;
	unsigned generated;
	unsigned compiled;
	unsigned validated;
	unsigned translated;
	unsigned slots_kept;
	unsigned accepted;
};

// The [[texture(n)]] or [[sampler(n)]] indices in an MSL entry point's signature.
std::vector<int> msl_indices(const std::string & msl, const char * kind)
{
	std::vector<int> indices;
	const std::string opening = std::string("[[") + kind + "(";
	for (size_t at = msl.find(opening); at != std::string::npos; at = msl.find(opening, at + 1)) {
		indices.push_back(atoi(msl.c_str() + at + opening.size()));
	}
	return indices;
}

// Every texture has the sampler of its own index, and every index is one SDL binds.
bool slots_kept(const std::string & msl, unsigned samplers, std::string & why)
{
	const std::vector<int> textures = msl_indices(msl, "texture");
	const std::vector<int> sampler_indices = msl_indices(msl, "sampler");
	for (size_t i = 0; i < textures.size(); ++i) {
		if (textures[i] < 0 || (unsigned)textures[i] >= samplers) {
			why = "texture " + std::to_string(textures[i]) + " is outside the " + std::to_string(samplers) + " slots SDL binds";
			return false;
		}
		bool paired = false;
		for (size_t k = 0; k < sampler_indices.size(); ++k) paired = paired || sampler_indices[k] == textures[i];
		if (!paired) {
			why = "texture " + std::to_string(textures[i]) + " has no sampler of its own index";
			return false;
		}
		for (size_t j = i + 1; j < textures.size(); ++j) {
			if (textures[j] == textures[i]) {
				why = "two textures share slot " + std::to_string(textures[i]);
				return false;
			}
		}
	}
	for (size_t k = 0; k < sampler_indices.size(); ++k) {
		if (sampler_indices[k] < 0 || (unsigned)sampler_indices[k] >= samplers) {
			why = "sampler " + std::to_string(sampler_indices[k]) + " is outside the slots SDL binds";
			return false;
		}
	}
	return true;
}

// spirv-val on one module, through a file, because it reads files.
bool validate(const char * spirv_val, const std::vector<unsigned char> & spirv, std::string & output)
{
	const char * temp = getenv("TMPDIR");
	char path[1024];
	snprintf(path, sizeof(path), "%s/test_shader_sdl_%d.spv", (temp != NULL && *temp) ? temp : "/tmp", (int)getpid());
	FILE * file = fopen(path, "wb");
	if (file == NULL) return false;
	fwrite(&spirv[0], 1, spirv.size(), file);
	fclose(file);

	std::string command = std::string("\"") + spirv_val + "\" --target-env vulkan1.0 \"" + path + "\" 2>&1";
	FILE * pipe = popen(command.c_str(), "r");
	output.clear();
	if (pipe == NULL) return false;
	char line[512];
	while (fgets(line, sizeof(line), pipe) != NULL) output += line;
	const int status = pclose(pipe);
	remove(path);
	return status == 0;
}

void print_tally(const char * what, const Tally & t, bool validating, bool metal)
{
	printf("  %-15s %3u programs: generated %u, compiled %u, spirv-val %s, MSL %u (slots kept %u), Metal %s\n", what,
		t.programs, t.generated, t.compiled,
		validating ? (std::to_string(t.validated) + "/" + std::to_string(t.compiled)).c_str() : "SKIPPED",
		t.translated, t.slots_kept,
		metal ? (std::to_string(t.accepted) + "/" + std::to_string(t.translated)).c_str() : "SKIPPED");
}

} // namespace

// "[[vk::location(4)]] float2 TexCoord0 : TEXCOORD0;" lines of a vertex program's Input structure,
// as SDL vertex attributes over one interleaved buffer.  D3DCOLOR is four bytes read as UBYTE4_NORM,
// which is why sdl3target.h swaps it back to RGBA in the program.
bool vertex_layout(const std::string & hlsl, std::vector<SDL_GPUVertexAttribute> & attributes, Uint32 & pitch)
{
	attributes.clear();
	pitch = 0;
	const size_t start = hlsl.find("struct Input\n{\n");
	if (start == std::string::npos) return false;
	const size_t end = hlsl.find("};", start);
	size_t line = hlsl.find('\n', start + 14) + 1;
	while (line < end) {
		const size_t stop = hlsl.find('\n', line);
		const std::string text = hlsl.substr(line, stop - line);
		line = stop + 1;
		const size_t open = text.find("[[vk::location(");
		if (open == std::string::npos) continue;
		SDL_GPUVertexAttribute attribute;
		SDL_zero(attribute);
		attribute.location = (Uint32)atoi(text.c_str() + open + 15);
		attribute.offset = pitch;
		if (text.find(": COLOR") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
			pitch += 4;
		}
		else if (text.find(" float4 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
			pitch += 16;
		}
		else if (text.find(" float3 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
			pitch += 12;
		}
		else if (text.find(" float2 ") != std::string::npos) {
			attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
			pitch += 8;
		}
		else return false;
		attributes.push_back(attribute);
	}
	return !attributes.empty();
}

TEST(runtime_bed_material_is_tileable_deterministic_and_has_distinct_relief)
{
	// Sample every texel: wrapping must be identical without a random generator shared with gameplay.
	// Each channel must carry useful contrast; a flat texture or an accidental repeated channel fails.
	double sum[4] = {}, squares[4] = {};
	unsigned different = 0, raised = 0;
	const int side = 256;
	for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
		const uint32_t value = WaterBedMaterial::Texel(x,y,side);
		CHECK_EQ(value, WaterBedMaterial::Texel(x+side,y-side,side));
		const unsigned relief = value & 255, pebbles = (value>>8)&255, sand = (value>>16)&255;
		different += pebbles != sand;
		raised += relief > 100;
		for (unsigned c = 0; c < 4; ++c) {
			const double b = (value >> (c*8)) & 255;
			sum[c] += b; squares[c] += b*b;
		}
	}
	CHECK(different > unsigned(side*side/2));
	CHECK(raised > unsigned(side*side/8) && raised < unsigned(side*side*3/4));
	for (unsigned c = 0; c < 4; ++c) {
		const double mean = sum[c]/(side*side);
		CHECK(mean > 10.0 && mean < 240.0);
		CHECK(squares[c]/(side*side)-mean*mean > 200.0);
	}
}

TEST(fft_water_vertex_and_pixel_compile_with_separate_texture_bindings)
{
	VertexPipelineDescription description=shader_cases_detail::plain_vertex();
	description.FVF=0x252; // XYZ, normal, diffuse, two texture coordinate sets, the actual water mesh.
	description.LightingEnabled=false;
	description.StageCount=4;
	for(unsigned i=0;i<4;++i) {
		description.Stages[i].TextureCoordinateIndex=i<2?i:0;
		description.Stages[i].TextureTransformFlags=FF_TTFF_DISABLE;
	}
	std::string vertex;
	CHECK(VertexShader_Generate(description,VERTEX_SHADER_TARGET_D3D11,vertex));
	// Hull constants must remain a water-only second slot. The ordinary fixed vertex program
	// keeps its existing single block even when it has the same water-compatible FVF.
	{
		std::string fixed=vertex,log;std::vector<unsigned char> spirv;
		CHECK(SDL3_Shader_Retarget(fixed,true));
		const bool compiled=SDL3_Compile_HLSL_To_SPIRV(fixed,true,spirv,log);CHECK(compiled);
		if(compiled) {
			unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);
			CHECK_EQ(samplers,0u);CHECK_EQ(buffers,1u);
		}
	}
	CHECK(WaterFftSampling::WrapVertex(vertex));
	CHECK(SDL3_Shader_Retarget(vertex,true));
	const size_t swap=vertex.find("input.Diffuse = input.Diffuse.bgra");
	CHECK(swap!=std::string::npos);
	CHECK(vertex.find("input.Diffuse = input.Diffuse.bgra",swap+1)==std::string::npos);
	PixelPipelineDescription pipeline={};
	std::string pixel;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,pixel,COMBINER_SHADER_TARGET_SDL3_GPU));
	for(int stage=0;stage<2;++stage) {
		const bool isVertex=stage==0;
		std::vector<unsigned char> spirv;std::string log,msl,why;
		const bool compiled=SDL3_Compile_HLSL_To_SPIRV(isVertex?vertex:pixel,isVertex,spirv,log);
		if(!compiled) printf("FFT water stage%d: %s\n",stage,log.c_str());
		CHECK(compiled);if(!compiled) continue;
		unsigned samplers=0,buffers=0;
		SDL3_Shader_Slots(spirv,isVertex,samplers,buffers);
		CHECK_EQ(samplers,isVertex?3u:14u);CHECK_EQ(buffers,isVertex?2u:1u);
		const char *validator=getenv("ZH_SPIRV_VAL");
		if(validator && *validator) CHECK(validate(validator,spirv,log));
		CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,isVertex,msl,log));
		CHECK(slots_kept(msl,samplers,why));
	}
}

TEST(every_generated_program_reaches_sdl3_gpu)
{
	const char * spirv_val = getenv("ZH_SPIRV_VAL");
	const bool validating = spirv_val != NULL && *spirv_val != '\0';
	if (!validating) {
		printf("  SKIPPED spirv-val: ZH_SPIRV_VAL is not set (CMake found no spirv-val; install spirv-tools)\n");
	}

	// Metal only.  On Linux the SPIR-V is the program, and spirv-val is the check there is.
	SDL_GPUDevice * device = NULL;
#if defined(__APPLE__)
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED Metal: no GPU device in this session (%s)\n", SDL_GetError());
	}
#endif
	const bool metal = device != NULL;

	Tally reference, extra;
	memset(&reference, 0, sizeof(reference));
	memset(&extra, 0, sizeof(extra));

	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		const ShaderCase & c = cases[i];
		Tally & t = c.Reference ? reference : extra;
		++t.programs;

		std::string hlsl;
		if (!Shader_Case_Generate(c, SHADER_CASE_SDL3_GPU, hlsl)) {
			printf("  %s: the generator refused the SDL3 target\n", c.Name.c_str());
			CHECK(false);
			continue;
		}
		++t.generated;

		std::vector<unsigned char> spirv;
		std::string log;
		if (!SDL3_Compile_HLSL_To_SPIRV(hlsl, c.VertexStage, spirv, log)) {
			printf("  %s: HLSL -> SPIR-V failed:\n%s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.compiled;

		if (validating) {
			std::string output;
			if (validate(spirv_val, spirv, output)) {
				++t.validated;
			}
			else {
				printf("  %s: spirv-val refused it:\n%s\n", c.Name.c_str(), output.c_str());
				CHECK(false);
			}
		}

		std::string msl;
		if (!SDL3_Translate_SPIRV_To_MSL(spirv, c.VertexStage, msl, log)) {
			printf("  %s: SPIR-V -> MSL failed: %s\n", c.Name.c_str(), log.c_str());
			CHECK(false);
			continue;
		}
		++t.translated;

		unsigned samplers = 0, uniform_buffers = 0;
		SDL3_Shader_Slots(spirv, c.VertexStage, samplers, uniform_buffers);
		std::string why;
		if (slots_kept(msl, samplers, why)) {
			++t.slots_kept;
		}
		else {
			printf("  %s: slot contract broken in its MSL: %s\n", c.Name.c_str(), why.c_str());
			CHECK(false);
		}

		if (metal) {
			SDL_GPUShader * shader = SDL3_Create_Shader(device, spirv, c.VertexStage, log);
			if (shader != NULL) {
				++t.accepted;
				SDL_ReleaseGPUShader(device, shader);
			}
			else {
				printf("  %s: Metal refused it: %s\n", c.Name.c_str(), log.c_str());
				CHECK(false);
			}
		}
	}

	print_tally("reference set", reference, validating, metal);
	print_tally("beyond it", extra, validating, metal);
	CHECK_EQ(reference.programs, 46u);
	CHECK(extra.programs > 0);

	if (device != NULL) {
		SDL_DestroyGPUDevice(device);
	}
	SDL_Quit();
}

TEST(every_vertex_program_links_with_every_pixel_program_it_can_meet)
{
#if !defined(__APPLE__)
	printf("  SKIPPED pipeline linking: Metal only; on Linux the stages are checked by spirv-val alone\n");
#else
	SDL_GPUDevice * device = NULL;
	if (SDL_Init(SDL_INIT_VIDEO)) {
		device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, true, NULL);
	}
	if (device == NULL) {
		printf("  SKIPPED pipeline linking: no GPU device in this session (%s)\n", SDL_GetError());
		return;
	}

	struct Built
	{
		std::string name;
		std::string hlsl;
		SDL_GPUShader * shader;
		bool engine;
	};
	std::vector<Built> vertex, pixel;
	const std::vector<ShaderCase> cases = Shader_Cases();
	for (size_t i = 0; i < cases.size(); ++i) {
		Built built;
		built.name = cases[i].Name;
		std::vector<unsigned char> spirv;
		std::string log;
		if (!Shader_Case_Generate(cases[i], SHADER_CASE_SDL3_GPU, built.hlsl)
			|| !SDL3_Compile_HLSL_To_SPIRV(built.hlsl, cases[i].VertexStage, spirv, log)) {
			CHECK(false);
			continue;
		}
		built.shader = SDL3_Create_Shader(device, spirv, cases[i].VertexStage, log);
		built.engine = cases[i].Kind == SHADER_CASE_ENGINE;
		if (built.shader == NULL) {
			CHECK(false);
			continue;
		}
		(cases[i].VertexStage ? vertex : pixel).push_back(built);
	}

	unsigned pairs = 0, linked = 0;
	for (size_t v = 0; v < vertex.size(); ++v) {
		std::vector<SDL_GPUVertexAttribute> attributes;
		Uint32 pitch = 0;
		if (!vertex_layout(vertex[v].hlsl, attributes, pitch)) {
			printf("  %s: no vertex layout could be read from its Input structure\n", vertex[v].name.c_str());
			CHECK(false);
			continue;
		}
		SDL_GPUVertexBufferDescription buffer;
		SDL_zero(buffer);
		buffer.pitch = pitch;
		buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
		SDL_GPUColorTargetDescription target;
		SDL_zero(target);
		target.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

		for (size_t p = 0; p < pixel.size(); ++p) {
			if (vertex[v].engine && pixel[p].engine) continue;	// the game never draws that pair
			++pairs;
			SDL_GPUGraphicsPipelineCreateInfo info;
			SDL_zero(info);
			info.vertex_shader = vertex[v].shader;
			info.fragment_shader = pixel[p].shader;
			info.vertex_input_state.vertex_buffer_descriptions = &buffer;
			info.vertex_input_state.num_vertex_buffers = 1;
			info.vertex_input_state.vertex_attributes = &attributes[0];
			info.vertex_input_state.num_vertex_attributes = (Uint32)attributes.size();
			info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			info.target_info.color_target_descriptions = &target;
			info.target_info.num_color_targets = 1;
			SDL_ClearError();
			SDL_GPUGraphicsPipeline * pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
			if (pipeline != NULL) {
				++linked;
				SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
			}
			else {
				printf("  %s + %s: Metal refused the pipeline: %s\n", vertex[v].name.c_str(), pixel[p].name.c_str(), SDL_GetError());
				CHECK(false);
			}
		}
	}
	printf("  %zu vertex x %zu pixel programs: %u pairs the game can form, %u linked on Metal\n", vertex.size(), pixel.size(),
		pairs, linked);
	CHECK(pairs > 0);
	CHECK_EQ(linked, pairs);

	for (size_t i = 0; i < vertex.size(); ++i) SDL_ReleaseGPUShader(device, vertex[i].shader);
	for (size_t i = 0; i < pixel.size(); ++i) SDL_ReleaseGPUShader(device, pixel[i].shader);
	SDL_DestroyGPUDevice(device);
	SDL_Quit();
#endif
}

namespace {
float opticalChannel(const WaterOpticalModel::RGB &rgb,int channel)
{
	return channel==0?rgb.r:(channel==1?rgb.g:rgb.b);
}
float displayedBed(float bed,float scatter,float transmission)
{
	return std::pow(std::pow(bed,2.2f)*transmission+scatter*(1.0f-transmission),1.0f/2.2f);
}
float displayedBedContrast(float transmission,float scatter)
{
	return (displayedBed(0.65f,scatter,transmission)-displayedBed(0.25f,scatter,transmission))/0.4f;
}
}

TEST(water_optics_shores_and_unclassified_pools_keep_existing_shallow_transmission)
{
	using namespace WaterOpticalModel;
	for (float river:{0.0f,0.25f,0.5f,1.0f}) for (float distance:{0.0f,0.25f,2.0f,5.0f,10.0f,20.0f,80.0f}) {
		const float materialPath=distance*(2.4f+(1.0f-2.4f)*river);
		const RGB original={std::exp(-materialPath*0.065f),std::exp(-materialPath*0.026f),std::exp(-materialPath*0.019f)};
		for (float opacity:{0.0f,0.5f,0.75f,1.0f}) for (float depth:{0.25f,2.7f,3.0f,10.0f}) {
			const RGB shore=Transmission(distance,river,0.0f,depth,opacity);
			CHECK(memcmp(&shore,&original,sizeof(RGB))==0);
			const RGB unauthored=Transmission(distance,river,1.0f,depth,0.0f);
			CHECK(memcmp(&unauthored,&original,sizeof(RGB))==0);
		}
	}
	CHECK(std::abs(OffshoreExtinction(2.7f,0.75f)-(-std::log(0.25f)/2.7f))<0.000001f);
	CHECK(std::abs(OffshoreExtinction(3.0f,1.0f)-(-std::log(0.01f)/3.0f))<0.000001f);
	const RGB zero=Transmission(0,0,1,0,1);
	CHECK(zero.r==1 && zero.g==1 && zero.b==1);
	CHECK(std::abs(displayedBed(0.43f,0.028f,zero.g)-0.43f)<0.000001f);
}

TEST(water_optics_authored_extinction_uses_unscaled_physical_ray_length)
{
	using namespace WaterOpticalModel;
	for (float distance:{0.25f,1.0f,3.2f,10.0f,40.0f}) for (float river:{0.0f,0.5f,1.0f}) {
		const RGB base=Transmission(distance,river,0,2.7f,0.75f);
		const RGB offshore=Transmission(distance,river,1,2.7f,0.75f);
		const RGB halfway=Transmission(distance,river,0.5f,2.7f,0.75f);
		const float authored=std::exp(-distance*OffshoreExtinction(2.7f,0.75f));
		for (int channel=0;channel<3;++channel) {
			const float a=opticalChannel(base,channel),b=opticalChannel(offshore,channel),m=opticalChannel(halfway,channel);
			CHECK(std::abs(b-a*authored)<0.000001f);
			CHECK(m>=b && m<=a);
			CHECK(std::abs(m*m-a*b)<0.000001f); // Optical thickness interpolates, not display colour.
		}
	}
}

TEST(water_optics_transmission_is_finite_bounded_and_monotone)
{
	using namespace WaterOpticalModel;
	const float infinity=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
	const float inputs[]={-infinity,nan,-3,0,0.1f,0.5f,1,3,100,infinity};
	for (float distance:inputs) for (float river:inputs) for (float offshore:inputs)
		for (float depth:inputs) for (float opacity:inputs) {
		const RGB t=Transmission(distance,river,offshore,depth,opacity);
		CHECK(std::isfinite(t.r) && std::isfinite(t.g) && std::isfinite(t.b));
		CHECK(t.r>=0 && t.r<=1 && t.g>=0 && t.g<=1 && t.b>=0 && t.b<=1);
	}
	for (float river:{0.0f,1.0f}) for (float offshore:{0.0f,0.5f,1.0f}) {
		RGB previous=Transmission(0,river,offshore,2.7f,0.75f);
		for (int i=1;i<=1000;++i) {
			const RGB current=Transmission(i*0.5f,river,offshore,2.7f,0.75f);
			CHECK(current.r<=previous.r && current.g<=previous.g && current.b<=previous.b);
			previous=current;
		}
	}
}

TEST(water_optics_deep_water_converges_without_a_180_unit_visibility_plateau)
{
	using namespace WaterOpticalModel;
	for (float river:{0.0f,1.0f}) {
		const RGB at180=Transmission(180,river,0,3,0),at360=Transmission(360,river,0,3,0),deep=Transmission(100000,river,0,3,0);
		CHECK(at360.r<at180.r && at360.g<at180.g && at360.b<at180.b);
		CHECK(deep.r<1e-30f && deep.g<1e-30f && deep.b<1e-30f);
		CHECK(std::abs(displayedBed(0.65f,0.041f,deep.b)-std::pow(0.041f,1.0f/2.2f))<0.000001f);
	}
	for (float distance:{0.0f,1.0f,10.0f,100.0f}) {
		const RGB t=Transmission(distance,0,1,3,1);
		// A bed already equal to the scattering radiance must not brighten/darken with distance.
		const float scatter=0.028f,displayScatter=std::pow(scatter,1.0f/2.2f);
		CHECK(std::abs(displayedBed(displayScatter,scatter,t.g)-displayScatter)<0.000001f);
	}
}

TEST(water_optics_authored_offshore_examples_hide_bed_in_display_space)
{
	using namespace WaterOpticalModel;
	const float scatter[3]={0.002f,0.028f,0.041f};
	// Optical interpretations of the maps' existing artistic settings: mission water at Z=7
	// has a ~10-unit ray, opacity .75 at reference depth 2.7; shell water at Z=21 has ~2.25
	// units of vertical depth and a ~3.2-unit ray, opacity 1 at reference depth 3.
	const float scenarios[][3]={{10.0f,0.75f,2.7f},{3.2f,1.0f,3.0f}};
	for (const auto &scenario:scenarios) {
		const RGB shore=Transmission(scenario[0],0,0,scenario[2],scenario[1]);
		const RGB offshore=Transmission(scenario[0],0,1,scenario[2],scenario[1]);
		for (int channel=0;channel<3;++channel) {
			CHECK(displayedBedContrast(opticalChannel(offshore,channel),scatter[channel])<0.05f);
			CHECK(displayedBedContrast(opticalChannel(offshore,channel),scatter[channel])<
				displayedBedContrast(opticalChannel(shore,channel),scatter[channel]));
		}
	}
}

TEST(water_optical_ray_correction_preserves_camera_rays_and_zero_offshore)
{
	using namespace WaterOpticalModel;
	for (float vz:{.25f,.5f,.8f,1.0f}) for (float delta:{-3.0f,-.7f,0.0f,.4f,2.0f})
		for (float offshore:{0.0f,.25f,.5f,1.0f}) {
			const float vx=std::sqrt(1-vz*vz),shift=RayShift(delta,vz,offshore,7,100,100);
			const float x=5+vx*shift,z=12+vz*shift;
			// Independent intersection with a horizontal analytic surface: sufficient room to both
			// the camera and the scene means the new point must land exactly at its blended height.
			CHECK(std::abs(z-(12+delta*offshore))<0.000003f);
			CHECK(std::abs((x-5)*vz-(z-12)*vx)<0.000003f); // parallel to the same camera ray
			const float sceneX=5-vx*100,sceneZ=12-vz*100;
			const float distance=std::sqrt((x-sceneX)*(x-sceneX)+(z-sceneZ)*(z-sceneZ));
			CHECK(std::abs(distance-(100+shift))<0.00003f);
			if (offshore==0) {
				CHECK(shift==0 && x==5 && z==12);
				const RGB before=Transmission(100,0,0,3,.75f),after=Transmission(100+shift,0,0,3,.75f);
				CHECK(before.r==after.r && before.g==after.g && before.b==after.b);
			}
		}
}

TEST(water_optical_ray_correction_cannot_cross_scene_hits_or_the_camera)
{
	using namespace WaterOpticalModel;
	for (float delta:{-1000000.0f,-100.0f,-1.0f,0.0f,1.0f,100.0f,1000000.0f})
		for (float vz:{-.8f,0.0f,.001f,.05f,.2f,.8f,1.0f})
			for (float offshore:{0.0f,.25f,.5f,1.0f})
				for (float hit:{0.0f,.01f,.08f,.2f,2.0f,50.0f})
					for (float camera:{0.0f,.04f,.08f,.3f,12.0f,100.0f}) {
						const float shift=RayShift(delta,vz,offshore,7,hit,camera);
						CHECK(std::isfinite(shift));
						CHECK(std::abs(shift)<=70.00001f); // 2*7 height / .2 grazing denominator
						CHECK(hit+shift>=std::min(hit,RaySurfaceMargin)-0.00001f);
						CHECK(camera-shift>=std::min(camera,RaySurfaceMargin)-0.00001f);
						if (vz<=0 || offshore==0) CHECK(shift==0);
					}
	const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
	for (float invalid:{nan,inf,-inf}) {
		CHECK(RayShift(invalid,.8f,1,7,10,100)==0);
		CHECK(RayShift(1,invalid,1,7,10,100)==0);
		CHECK(std::isfinite(RayShift(-1,.8f,1,7,invalid,100)));
		CHECK(std::isfinite(RayShift(1,.8f,1,7,10,invalid)));
		CHECK(std::isfinite(RayShift(1,.8f,invalid,7,10,100)));
		CHECK(std::isfinite(RayShift(1,.8f,1,invalid,10,100)));
	}
}

TEST(water_optical_ray_correction_keeps_submerged_objects_clearer_than_the_bed)
{
	using namespace WaterOpticalModel;
	// An object lies .25 along the rendered camera ray and the terrain lies 4 units away. The
	// analytic trough would move the surface 2.5 units along that ray, behind the close object.
	const float objectShift=RayShift(-2,.8f,1,7,.25f,100),bedShift=RayShift(-2,.8f,1,7,4,100);
	const float objectGap=.25f+objectShift,bedGap=4+bedShift;
	CHECK(std::abs(objectGap-.08f)<0.000001f && std::abs(bedGap-1.5f)<0.000001f);
	const RGB object=Transmission(objectGap,0,1,3,.75f),bed=Transmission(bedGap,0,1,3,.75f);
	CHECK(object.r>bed.r && object.g>bed.g && object.b>bed.b);
	// Armed control reproduces the old bug: a length() taken after passing the hit becomes
	// positive again and makes the near object *darker* than the farther bed.
	const RGB crossed=Transmission(std::abs(.25f-2.5f),0,1,3,.75f);
	CHECK(crossed.r<bed.r && crossed.g<bed.g && crossed.b<bed.b);
	CHECK(RayShift(-2,.8f,1,7,.03f,100)==0); // preserve a hit closer than the safety margin
	CHECK(std::abs(RayShift(2,.8f,1,7,10,.3f)-.22f)<0.000001f);
}

TEST(water_optics_shared_hlsl_compiles_and_translates_without_gpu_execution)
{
	std::string hlsl=WaterOpticalModel::GetHlslSource();
	hlsl+="float4 main(float4 position : SV_Position) : SV_Target { float shift=WaterOpticalRayShift(position.x,.8,position.z,7,10,100); return float4(WaterOpticalTransmission(abs(position.x), position.y, position.z, 2.7, 0.75), shift); }\n";
	std::vector<unsigned char> spirv;
	std::string log,msl;
	const bool compiled=SDL3_Compile_HLSL_To_SPIRV(hlsl,false,spirv,log);
	if (!compiled) printf("  water optics HLSL compilation failed: %s\n",log.c_str());
	CHECK(compiled);
	if (!compiled) return;
	const char *spirvVal=getenv("ZH_SPIRV_VAL");
	if (spirvVal && *spirvVal) {
		std::string output;
		const bool valid=validate(spirvVal,spirv,output);
		if (!valid) printf("  water optics SPIR-V validation failed: %s\n",output.c_str());
		CHECK(valid);
	}
	const bool translated=SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log);
	if (!translated) printf("  water optics MSL translation failed: %s\n",log.c_str());
	CHECK(translated);
}

TEST(water_hull_slices_actual_triangles_at_water_not_the_deck_or_collision_box)
{
	using namespace WaterHullModel;
	// An asymmetric, tapered hull widens from 10 below water to 30 at its elevated deck.
	// At z=0 its waterline must be exactly half way: width20, not the deck/collision width30.
	const Point outline[]={{-40,-10},{20,-10},{40,0},{20,10},{-40,10}};
	std::vector<Point> intersections;
	for(unsigned i=0;i<5;++i) {
		const Point a=outline[i],b=outline[(i+1)%5];
		const Point3 lowA={a.x,a.y,-5},lowB={b.x,b.y,-5};
		const Point3 highA={a.x,a.y*3,5},highB={b.x,b.y*3,5};
		CHECK(SliceTriangle(lowA,lowB,highB,intersections));
		CHECK(SliceTriangle(lowA,highB,highA,intersections));
	}
	const Polygon hull=BuildPolygon(intersections);
	// The slanted bow quad is not planar: its authored triangle diagonal intersects at
	// (30,15), outside the straight line joining (40,0) and (20,20). Keep that real mesh kink.
	CHECK(hull.count==6 && hull.error<0.0001f);
	CHECK(std::abs(SignedDistance({30,15},hull))<0.0001f);
	CHECK(std::abs(SignedDistance({0,20},hull))<0.0001f);
	CHECK(std::abs(SignedDistance({0,25},hull)-5)<0.0001f);
	CHECK(std::abs(SignedDistance({0,0},hull)+20)<0.0001f);
	CHECK(SignedDistance({38,10},hull)>0); // pointed bow, not a rectangular collision box
	CHECK(SignedDistance({-38,10},hull)<0); // broad stern retained
	const Polygon wrongDeck=BuildPolygon({{-40,-30},{20,-30},{40,0},{20,30},{-40,30}});
	CHECK(SignedDistance({0,25},wrongDeck)<0); // armed deck-footprint control
	std::vector<Point> dry;
	CHECK(SliceTriangle({0,0,1},{1,0,2},{0,1,3},dry));CHECK(dry.empty());
	CHECK(SliceTriangle({0,0,0},{10,0,0},{0,10,0},dry));CHECK(BuildPolygon(dry).count==3);
}

TEST(water_hull_contours_have_bounded_cost_and_report_simplification_error)
{
	using namespace WaterHullModel;
	std::vector<Point> ellipse;
	for(unsigned i=0;i<256;++i) {
		const float angle=float(i)*6.28318530718f/256;
		ellipse.push_back({283.0f*std::cos(angle),36.0f*std::sin(angle)});
	}
	const Polygon reduced=BuildPolygon(ellipse);
	CHECK(reduced.count==MaxVertices && reduced.error>0 && reduced.error<6);
	float independentlyMeasured=0;
	for(Point p:ellipse) independentlyMeasured=std::max(independentlyMeasured,SignedDistance(p,reduced));
	CHECK(std::abs(independentlyMeasured-reduced.error)<0.0001f);
	for(unsigned i=0;i<reduced.count;++i) {
		const Point p=reduced.points[i];
		CHECK(p.x*p.x/(283*283)+p.y*p.y/(36*36)<1.00001f);
	}
	CHECK(BuildPolygon({{0,0},{1,1},{2,2}}).count==0);
	CHECK(BuildPolygon({{0,0},{0,0},{0,0}}).count==0);
	ellipse.resize(MaxSlicePoints+1);CHECK(BuildPolygon(ellipse).count==0);
	std::vector<Point> full(MaxSlicePoints);
	CHECK(!SliceTriangle({0,0,-1},{1,0,1},{0,1,1},full));CHECK(full.size()==MaxSlicePoints);
	std::vector<Point> invalid;
	CHECK(!SliceTriangle({0,0,0},{1,std::numeric_limits<float>::quiet_NaN(),0},{0,1,0},invalid));
}

TEST(water_hull_packing_rotation_damping_and_distinct_water_levels)
{
	using namespace WaterHullModel;
	Hull hull;
	hull.data[0][0]=3028;hull.data[0][1]=588;hull.data[0][2]=7;hull.data[0][3]=4;
	hull.data[1][0]=0;hull.data[1][1]=-1;hull.data[1][2]=100;hull.data[1][3]=20;
	const Point p[]={{-100,-20},{100,-20},{100,20},{-100,20}};
	for(unsigned i=0;i<4;++i) {hull.data[4+i/2][(i%2)*2]=p[i].x;hull.data[4+i/2][(i%2)*2+1]=p[i].y;}
	CHECK(Damping(hull,3028,588,7)==0);
	CHECK(Damping(hull,3028,588,9)==1); // another water plane cannot flatten this one
	CHECK(Damping(hull,3052.5f,588,7)==0.5f);
	CHECK(Damping(hull,3057,588,7)==1);
	CHECK(Damping(hull,10000,10000,7)==1);
	for(float blend:{0.0f,.25f,.5f,1.0f}) {
		const float atten=1+(Damping(hull,3052.5f,588,7)-1)*blend;
		CHECK(std::abs(atten-(1-.5f*blend))<0.000001f);
	}
	const Point bowWorld={3028,488};
	const Point local=LocalPoint(hull,bowWorld.x,bowWorld.y);
	CHECK(local.x==100 && local.y==0); // authored negative-X bow can become positive local bow
	CHECK(sizeof(Hull)==RegistersPerHull*16);
}

TEST(water_hull_shared_distance_hlsl_compiles_and_translates)
{
	std::string hlsl="cbuffer Hulls : register(b0, space3) { float4 HullData[80]; };\n";
	hlsl+=WaterHullModel::GetHlslSource();
	hlsl+="float4 main(float4 position : SV_Position) : SV_Target { return WaterHullSignedDistance(position.xy,70,12); }\n";
	std::vector<unsigned char> spirv;std::string log,msl;
	const bool compiled=SDL3_Compile_HLSL_To_SPIRV(hlsl,false,spirv,log);
	if(!compiled) printf("  hull HLSL compilation failed: %s\n",log.c_str());
	CHECK(compiled);if(!compiled) return;
	const bool translated=SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,log);
	if(!translated) printf("  hull HLSL MSL translation failed: %s\n",log.c_str());
	CHECK(translated);
}

TEST(water_macro_swell_gradients_and_curvature_include_warp_and_packet_derivatives)
{
	using namespace WaterSurfaceModel;
	unsigned gradientControls=0,curvatureControls=0;
	for(float time:{0.0f,1.7f,12.0f,38.0f}) for(float x:{-80.0f,0.0f,17.0f}) for(float y:{-21.0f,0.0f,43.0f}) {
		const OceanSurface s=EvaluateOceanSwell(x,y,time);
		const float step=.125f;
		const float xp=EvaluateOceanSwell(x+step,y,time).height,xm=EvaluateOceanSwell(x-step,y,time).height;
		const float yp=EvaluateOceanSwell(x,y+step,time).height,ym=EvaluateOceanSwell(x,y-step,time).height;
		CHECK(std::abs((xp-xm)/(2*step)-s.dx)<.0015f);
		CHECK(std::abs((yp-ym)/(2*step)-s.dy)<.0015f);
		CHECK(std::abs((xp+xm+yp+ym-4*s.height)/(step*step)-s.laplacian)<.0015f);
		const float q0=x*.0241f+y*.0157f-time*.23f+.4f,q1=x*-.0173f+y*.0299f-time*.19f+2.1f;
		const float s0=std::sin(q0),s1=std::sin(q1),px=x+5*s0,py=y+4*s1,envelope=.86f+.14f*s0*s1;
		float wrongX=0,wrongY=0,wrongLaplacian=0,height=0;
#define TEST_MACRO_SWELL(kx,ky,amplitude,omega,phase) if((kx)*(kx)+(ky)*(ky)<.18f) { \
		const float p=px*(kx)+py*(ky)-time*(omega)+(phase),sn=std::sin(p),cs=std::cos(p); \
		height+=(amplitude)*(sn+.16f*sn*cs); \
		const float derivative=(amplitude)*(cs+.16f*(cs*cs-sn*sn)); \
		wrongX+=derivative*(kx);wrongY+=derivative*(ky); \
		wrongLaplacian+=(amplitude)*(-sn-.64f*sn*cs)*((kx)*(kx)+(ky)*(ky)); }
		WATER_OCEAN_MODES(TEST_MACRO_SWELL)
#undef TEST_MACRO_SWELL
		CHECK(std::abs(height*envelope-s.height)<.000001f);
		if(std::max(std::abs(wrongX*envelope-s.dx),std::abs(wrongY*envelope-s.dy))>.002f) ++gradientControls;
		if(std::abs(wrongLaplacian*envelope-s.laplacian)>.002f) ++curvatureControls;
	}
	CHECK(gradientControls>=8 && curvatureControls>=8);
}
