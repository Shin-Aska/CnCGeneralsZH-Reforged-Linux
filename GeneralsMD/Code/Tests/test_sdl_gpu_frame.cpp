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

// A3a's frame (PosixDevice/Render/SdlGpuFrame), on this machine's GPU, offscreen, read back:
//   - a colour clear lands as D3D9's ARGB in the back buffer's B8G8R8A8 bytes, every pixel;
//   - a depth-only clear leaves the colour as it was (the colour is loaded, not cleared);
//   - the gamma pass maps each channel through its own ramp, on a picture where every pixel differs, so
//     a flipped, shifted or channel-swapped pass cannot match; the identity ramp's blit gives the picture
//     back unchanged;
//   - an armed control: the gamma output compared against the wrong ramp must not match.
//
// WHAT THIS DOES NOT PROVE:
//   - Presenting to a window: the swap chain needs a display, and ctest has none.  The window's path is
//     the same pass into the swap chain's texture instead of this test's.
//   - Any GPU but this one.  Exits 77, and ctest reports it skipped, where SDL has no GPU device.

#include "SdlGpuFrame.h"
#include "SdlConstants.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3target.h"
#include "../Libraries/Source/WWVegas/WW3D2/engineshader.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterFftSampling.h"
#include <complex>

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stddef.h>

static int failures = 0;

#define CHECK(condition) \
	do { if (!(condition)) { ++failures; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static const unsigned int WIDTH = 64;
static const unsigned int HEIGHT = 48;

// Rasterize a submerged object independently of the water capture: a triangle writes its own
// depth, so a terrain-only estimate or a copy of the depth clear cannot satisfy the assertions.
class DepthObject
{
public:
	SDL_GPUShader *Vertex = NULL, *Pixel = NULL;
	SDL_GPUGraphicsPipeline *Pipeline = NULL;
	SdlGpuFrame *Frame;
	explicit DepthObject(SdlGpuFrame *frame) : Frame(frame)
	{
		const char *vertex =
			"cbuffer Object : register(b0, space1) { float4 Depth; };\n"
			"float4 main(uint id : SV_VertexID) : SV_Position {\n"
			"float2 p = id == 0 ? float2(-0.75,-0.75) : (id == 1 ? float2(0.75,-0.75) : float2(0,0.75));\n"
			"return float4(p,Depth.x,1); }\n";
		const char *pixel = "float4 main() : SV_Target0 { return float4(0.8,0.2,0.1,1); }\n";
		std::vector<unsigned char> spirv;
		std::string error;
		CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error));
		Vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
		CHECK(Vertex != NULL);
		CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error));
		Pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
		CHECK(Pixel != NULL);
		SDL_GPUColorTargetDescription colour = {};
		colour.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = Vertex;
		info.fragment_shader = Pixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.depth_stencil_state.enable_depth_test = true;
		info.depth_stencil_state.enable_depth_write = true;
		info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
		info.target_info.color_target_descriptions = &colour;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = true;
		info.target_info.depth_stencil_format = (SDL_GPUTextureFormat)frame->Depth_Format();
		Pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &info);
		CHECK(Pipeline != NULL);
	}
	~DepthObject()
	{
		Frame->Flush();
		if (Pipeline) SDL_ReleaseGPUGraphicsPipeline(Frame->Device(), Pipeline);
		if (Vertex) SDL_ReleaseGPUShader(Frame->Device(), Vertex);
		if (Pixel) SDL_ReleaseGPUShader(Frame->Device(), Pixel);
	}
	void Draw(const SdlTarget &target, float depth)
	{
		if (!Pipeline) return;
		Frame->Set_Target(target);
		SdlRecordedDraw draw = {};
		draw.Pipeline = Pipeline;
		draw.Count = 3;
		memset(Frame->Stage(16, draw.VertexOffset), 0, 16);
		const float values[4] = { depth, 0, 0, 0 };
		draw.VertexConstants = Frame->Constants(0, values, sizeof(values));
		draw.VertexConstantsSize = sizeof(values);
		draw.Viewport[2] = (float)target.Width;
		draw.Viewport[3] = (float)target.Height;
		draw.Viewport[5] = 1;
		draw.Scissor[2] = target.Width;
		draw.Scissor[3] = target.Height;
		Frame->Record_Draw(draw);
	}
};

static float depth_at(const std::vector<uint8_t> &bytes, unsigned int width, unsigned int x, unsigned int y)
{
	float value = -1;
	const size_t offset = ((size_t)y * width + x) * sizeof(float);
	if (offset + sizeof(float) <= bytes.size()) memcpy(&value, &bytes[offset], sizeof(value));
	return value;
}

static void check_scene_depth(SdlGpuFrame *frame)
{
	if (!(frame->Depth_Usage() & SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
		printf("sdl_gpu_frame_selfcheck: scene depth unavailable on this device (fallback colour still tested)\n");
		CHECK(frame->Water_Depth(frame->Back_Buffer_Target()) == NULL);
		return;
	}
	DepthObject object(frame);
	const SdlTarget scene = frame->Back_Buffer_Target();
	frame->Set_Target(scene);
	frame->Clear_Back_Buffer(true, true, true, 0xFF183048, 0.9f, 0);
	frame->Clear_Rect(0, 0, 16, 12, false, true, false, 0, 0.65f, 0);
	object.Draw(scene, 0.3f);
	SDL_GPUTexture *depth = frame->Water_Depth(scene);
	CHECK(depth != NULL && depth != scene.Depth && depth != scene.Colour);
	std::vector<uint8_t> sampled;
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, sampled));
	CHECK(fabs(depth_at(sampled, scene.Width, 32, 24) - 0.3f) < 0.00001f);
	CHECK(fabs(depth_at(sampled, scene.Width, 2, 2) - 0.65f) < 0.00001f);
	CHECK(fabs(depth_at(sampled, scene.Width, 62, 46) - 0.9f) < 0.00001f);
	std::vector<uint8_t> colour;
	CHECK(frame->Read_Back(frame->Water_Background(scene), scene.Width, scene.Height, colour));
	CHECK(colour[(24 * scene.Width + 32) * 4 + 2] == 204); // the same object's colour

	// A later draw must not contaminate the pre-water depth, even over multiple flushes.
	object.Draw(scene, 0.1f);
	CHECK(frame->Water_Depth(scene) == depth);
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, colour));
	CHECK(colour == sampled);
	// No clear is needed to refresh the next frame's depth.
	CHECK(frame->Present(NULL));
	CHECK(frame->Water_Depth(scene) == depth);
	CHECK(frame->Read_Back(depth, scene.Width, scene.Height, sampled));
	CHECK(fabs(depth_at(sampled, scene.Width, 32, 24) - 0.1f) < 0.00001f);

	SDL_GPUTextureCreateInfo info = {};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)frame->Depth_Format();
	info.usage = frame->Depth_Usage();
	info.width = 23; info.height = 17; info.layer_count_or_depth = 1; info.num_levels = 1;
	SDL_GPUTexture *other_depth = SDL_CreateGPUTexture(frame->Device(), &info);
	info.format = (SDL_GPUTextureFormat)frame->Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	SDL_GPUTexture *other_colour = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(other_depth && other_colour);
	const SdlTarget intermediate = { other_colour, other_depth, 23, 17 };
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(true, true, true, 0xFFABCDEF, 0.8f, 0);
	object.Draw(intermediate, 0.45f);
	SDL_GPUTexture *intermediate_depth = frame->Water_Depth(intermediate);
	CHECK(intermediate_depth && intermediate_depth != depth);
	CHECK(frame->Read_Back(intermediate_depth, 23, 17, colour));
	CHECK(fabs(depth_at(colour, 23, 11, 8) - 0.45f) < 0.00001f);
	CHECK(fabs(depth_at(colour, 23, 0, 0) - 0.8f) < 0.00001f);
	// Returning to a source preserves its own snapshot instead of copying previously rendered water.
	object.Draw(scene, 0.05f);
	CHECK(frame->Read_Back(frame->Water_Depth(scene), scene.Width, scene.Height, colour));
	CHECK(colour == sampled);
	// Depth-only clear invalidates the pair, then release source handles with a capture still queued.
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(false, true, false, 0, 0.7f, 0);
	CHECK(frame->Read_Back(frame->Water_Depth(intermediate), 23, 17, colour));
	CHECK(fabs(depth_at(colour, 23, 11, 8) - 0.7f) < 0.00001f);
	frame->Clear_Back_Buffer(true, true, true, 0xFF102030, 0.6f, 0);
	CHECK(frame->Water_Depth(intermediate) != NULL);
	frame->Release_After_Batch(other_colour, NULL);
	frame->Release_After_Batch(other_depth, NULL);
	CHECK(frame->Flush());
	frame->Set_Target(scene);
	CHECK(frame->Present(NULL));
}

static void check_gpu_wave_model(SdlGpuFrame *frame)
{
	// Execute the exact shared HLSL model on the GPU. Columns are height, dH/dx and dH/dy; rows use
	// different positions/times, including the free-camera view and large/negative map coordinates.
	const float fixtures[][4] = {
		{0, 0, 0, 0}, {0, 0, 1.7f, 0}, {12, -21, 3.2f, 0}, {-80, 43, 19.4f, 0},
		{1938, 736, 0, 0}, {1938, 736, 4.2f, 0}, {1938, 736, 67.1f, 0},
		{4100, 3700, 13.8f, 0}, {-1900, 720, 3.6f, 0}, {736, 1938, 431.2f, 0},
		{17, 89, 864.3f, 0}, {4096, 4096, 3600, 0} };
	const unsigned int count = sizeof(fixtures) / sizeof(fixtures[0]);
	const char *vertex_source =
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source = "cbuffer Samples : register(b0, space3) { float4 Fixtures[12]; };\n";
	pixel_source += WaterSurfaceModel::ShaderSource();
	pixel_source += "float main(float4 position : SV_Position) : SV_Target0 {\n"
		"float4 f=Fixtures[(uint)position.y]; return waterWave(f.xy,f.z)[(uint)position.x]; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description = {};
	description.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.vertex_shader = vertex;
	pipeline_info.fragment_shader = pixel;
	pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions = &description;
	pipeline_info.target_info.num_color_targets = 1;
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &pipeline_info);
	SDL_GPUTextureCreateInfo texture_info = {};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width = 3; texture_info.height = count;
	texture_info.layer_count_or_depth = 1; texture_info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment = {};
	attachment.texture = target;
	attachment.load_op = SDL_GPU_LOADOP_DONT_CARE;
	attachment.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &attachment, 1, NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_PushGPUFragmentUniformData(commands, 0, fixtures, sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target, 3, count, bytes));
	float max_model_error = 0, max_difference_error = 0;
	for (unsigned int i = 0; i < count; ++i) {
		const float x = fixtures[i][0], y = fixtures[i][1], seconds = fixtures[i][2];
		const WaterSurfaceModel::Sample cpu = WaterSurfaceModel::Evaluate(x, y, seconds);
		const float values[3] = { cpu.height, cpu.dx, cpu.dy };
		for (unsigned int c = 0; c < 3; ++c) {
			const float difference = fabsf(depth_at(bytes, 3, c, i) - values[c]);
			if (difference > max_model_error) max_model_error = difference;
			CHECK(difference < 0.0025f); // float phase precision at large map coordinates/times
		}
		const float epsilon = 0.03125f;
		const float dx = (WaterSurfaceModel::Evaluate(x + epsilon, y, seconds).height
			- WaterSurfaceModel::Evaluate(x - epsilon, y, seconds).height) / (2 * epsilon);
		const float dy = (WaterSurfaceModel::Evaluate(x, y + epsilon, seconds).height
			- WaterSurfaceModel::Evaluate(x, y - epsilon, seconds).height) / (2 * epsilon);
		const float error_x = fabsf(depth_at(bytes, 3, 1, i) - dx), error_y = fabsf(depth_at(bytes, 3, 2, i) - dy);
		if (error_x > max_difference_error) max_difference_error = error_x;
		if (error_y > max_difference_error) max_difference_error = error_y;
		CHECK(error_x < 0.01f && error_y < 0.01f);
	}
	printf("sdl_gpu_frame_selfcheck: GPU wave height/normal consistency: max CPU model error %.7f, finite-difference slope error %.7f\n",
		max_model_error, max_difference_error);
	SDL_ReleaseGPUTexture(frame->Device(), target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(), pipeline);
	SDL_ReleaseGPUShader(frame->Device(), vertex);
	SDL_ReleaseGPUShader(frame->Device(), pixel);
}

static void check_gpu_ocean_wave_model(SdlGpuFrame *frame)
{
	// A spatially varying offshore weight exercises the derivative of the blend, not just the
	// interpolation of two normals. Every fixture is {x,y,time,weight, dWeight/dx,dWeight/dy,0,0}.
	const float fixtures[][8] = {
		{0,0,0,0, 0,0,0,0}, {12,-21,3.2f,0, 0,0,0,0}, {1938,736,67.1f,0, 0,0,0,0},
		{0,0,0,1, 0,0,0,0}, {0,0,1.7f,1, 0,0,0,0}, {3028,588,12,1, 0,0,0,0},
		{4096,4096,3600,1, 0,0,0,0}, {-1900,720,3.6f,1, 0,0,0,0}, {17,89,864.3f,1, 0,0,0,0},
		{0,0,0,.2f, .014f,-.011f,0,0}, {12,-21,3.2f,.5f, -.006f,.018f,0,0},
		{-80,43,19.4f,.8f, .011f,.007f,0,0}, {1938,736,4.2f,.5f, .008f,-.012f,0,0},
		{4100,3700,13.8f,.5f, -.009f,.004f,0,0}, {736,1938,431.2f,.9f, .002f,-.003f,0,0}
	};
	const unsigned int count=sizeof(fixtures)/sizeof(fixtures[0]), columns=30;
	const float epsilon=.03125f;
	const char *vertex_source=
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source="cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source+=std::to_string(count*2)+"]; };\n";
	WaterSurfaceModel::AppendHLSL(pixel_source);
	pixel_source+=
		"float3 blendAt(float2 offset,float4 f,float2 gradient) {\n"
		"float3 legacy=waterWave(f.xy+offset,f.z), ocean=waterOceanWave(f.xy+offset,f.z);\n"
		"float weight=saturate(f.w+dot(offset,gradient));\n"
		"float3 result=legacy+(ocean-legacy)*weight;\n"
		"result.yz+=(ocean.x-legacy.x)*gradient; return result; }\n"
		"float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y,column=(uint)position.x;\n"
		"float4 f=Fixtures[row*2]; float2 gradient=Fixtures[row*2+1].xy;\n"
		"if (column<3) return blendAt(float2(0,0),f,gradient)[column];\n"
		"if (column<7) { float2 offset=column==3?float2(0.03125,0):\n"
		"column==4?float2(-0.03125,0):column==5?float2(0,0.03125):float2(0,-0.03125);\n"
		"return blendAt(offset,f,gradient).x; }\n"
		"if (column<10) return waterOceanWave(f.xy,f.z)[column-7];\n"
		"if (column<13) return waterWave(f.xy,f.z)[column-10];\n"
		"if (column==13) return waterOceanSurface(f.xy,f.z).w;\n"
		"if (column>=26) { float2 offset=column==26?float2(0.25,0):column==27?float2(-0.25,0):\n"
		"column==28?float2(0,0.25):float2(0,-0.25); return waterOceanWave(f.xy+offset,f.z).x; }\n"
		"if (column>=18) { float4 swell; waterOceanSurface(f.xy,f.z,swell);\n"
		"if (column<22) return swell[column-18];\n"
		"float2 step=column==22?float2(0.125,0):column==23?float2(-0.125,0):column==24?float2(0,0.125):float2(0,-0.125);\n"
		"waterOceanSurface(f.xy+step,f.z,swell); return swell.x; }\n"
		"float2 offset=column==14?float2(0.125,0):column==15?float2(-0.125,0):\n"
		"column==16?float2(0,0.125):float2(0,-0.125);\n"
		"return waterOceanWave(f.xy+offset,f.z).x; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};
	description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info={};
	pipeline_info.vertex_shader=vertex; pipeline_info.fragment_shader=pixel;
	pipeline_info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions=&description;
	pipeline_info.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipeline_info);
	SDL_GPUTextureCreateInfo texture_info={};
	texture_info.type=SDL_GPU_TEXTURETYPE_2D; texture_info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width=columns; texture_info.height=count;
	texture_info.layer_count_or_depth=1; texture_info.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment={};
	attachment.texture=target; attachment.load_op=SDL_GPU_LOADOP_DONT_CARE; attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);
	SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target,columns,count,bytes));
	float max_model_error=0,max_gpu_derivative_error=0,max_cpu_derivative_error=0;
	float max_curvature_model_error=0,max_cpu_curvature_error=0,max_gpu_curvature_error=0;
	float max_cpu_coarse_curvature_error=0,max_gpu_coarse_curvature_error=0;
	float max_swell_model_error=0,max_swell_gradient_error=0,max_swell_laplacian_error=0;
	unsigned int gradient_controls=0,curvature_checks=0,curvature_controls=0,curvature_refinements=0;
	for (unsigned int i=0;i<count;++i) {
		using namespace WaterSurfaceModel;
		const float *f=fixtures[i];
		const Sample legacy=Evaluate(f[0],f[1],f[2]),ocean=EvaluateOcean(f[0],f[1],f[2]);
		const Sample offshore={f[3],f[4],f[5]};
		const Sample blended=BlendSamples(legacy,ocean,offshore);
		const float expected[3]={blended.height,blended.dx,blended.dy};
		const float expected_ocean[3]={ocean.height,ocean.dx,ocean.dy};
		for (unsigned int c=0;c<3;++c) {
			const float a=fabsf(depth_at(bytes,columns,c,i)-expected[c]);
			const float b=fabsf(depth_at(bytes,columns,c+7,i)-expected_ocean[c]);
			max_model_error=fmaxf(max_model_error,fmaxf(a,b));
			CHECK(a<.0025f && b<.0025f); // float phase precision at long elapsed times/large map positions
			if (f[3]==0) CHECK(depth_at(bytes,columns,c,i)==depth_at(bytes,columns,c+10,i));
			if (f[3]==1) CHECK(fabsf(depth_at(bytes,columns,c,i)-depth_at(bytes,columns,c+7,i))<.000001f);
		}
		auto height_at=[&](float ox,float oy) {
			// Independent scalar interpolation deliberately does not use the analytic BlendSamples
			// derivatives. Its central difference must recover both domain warp and blend product terms.
			const float a=Evaluate(f[0]+ox,f[1]+oy,f[2]).height;
			const float b=EvaluateOcean(f[0]+ox,f[1]+oy,f[2]).height;
			const float weight=f[3]+ox*f[4]+oy*f[5];
			return a+(b-a)*weight;
		};
		const float numeric_cpu[2]={(height_at(epsilon,0)-height_at(-epsilon,0))/(2*epsilon),
			(height_at(0,epsilon)-height_at(0,-epsilon))/(2*epsilon)};
		const float numeric_gpu[2]={(depth_at(bytes,columns,3,i)-depth_at(bytes,columns,4,i))/(2*epsilon),
			(depth_at(bytes,columns,5,i)-depth_at(bytes,columns,6,i))/(2*epsilon)};
		for (unsigned int c=0;c<2;++c) {
			const float cpu_error=fabsf(numeric_cpu[c]-expected[c+1]);
			const float gpu_error=fabsf(numeric_gpu[c]-depth_at(bytes,columns,c+1,i));
			max_cpu_derivative_error=fmaxf(max_cpu_derivative_error,cpu_error);
			max_gpu_derivative_error=fmaxf(max_gpu_derivative_error,gpu_error);
			CHECK(cpu_error<.01f && gpu_error<.0125f);
			// Armed control: omitting dWeight/dx,y must measurably break several fixtures.
			const float wrong=(c==0?legacy.dx:legacy.dy)*(1-f[3])+(c==0?ocean.dx:ocean.dy)*f[3];
			if (fabsf(expected[c+1]-wrong)>.008f) ++gradient_controls;
		}
		if (f[3]==0) {
			const Sample endpoint=EvaluateBlended(f[0],f[1],f[2],offshore);
			CHECK(memcmp(&legacy,&endpoint,sizeof(Sample))==0);
		}
		const OceanSurface surface=EvaluateOceanSurface(f[0],f[1],f[2]);
		const float gpu_curvature=depth_at(bytes,columns,13,i);
		const float model_error=fabsf(gpu_curvature-surface.laplacian);
		max_curvature_model_error=fmaxf(max_curvature_model_error,model_error);
		CHECK(model_error<.0025f);
		const OceanSurface swell=EvaluateOceanSwell(f[0],f[1],f[2]);
		const float expected_swell[4]={swell.height,swell.dx,swell.dy,swell.laplacian};
		for(unsigned c=0;c<4;++c) {
			const float difference=fabsf(depth_at(bytes,columns,18+c,i)-expected_swell[c]);
			max_swell_model_error=fmaxf(max_swell_model_error,difference);CHECK(difference<.0025f);
		}
		// Height-only second differences independently exercise the warp Hessian and all packet
		// product terms. Near-origin fixtures avoid subtractive float cancellation from long phases;
		// large-coordinate/elapsed-time fixtures still check CPU/GPU analytic agreement above.
		if (fabsf(f[0])<128 && fabsf(f[1])<128 && f[2]<100) {
			const float swell_x=(depth_at(bytes,columns,22,i)-depth_at(bytes,columns,23,i))/.25f;
			const float swell_y=(depth_at(bytes,columns,24,i)-depth_at(bytes,columns,25,i))/.25f;
			const float swell_laplacian=(depth_at(bytes,columns,22,i)+depth_at(bytes,columns,23,i)
				+depth_at(bytes,columns,24,i)+depth_at(bytes,columns,25,i)-4*depth_at(bytes,columns,18,i))/.015625f;
			const float slope_error=fmaxf(fabsf(swell_x-depth_at(bytes,columns,19,i)),fabsf(swell_y-depth_at(bytes,columns,20,i)));
			const float curvature_error=fabsf(swell_laplacian-depth_at(bytes,columns,21,i));
			max_swell_gradient_error=fmaxf(max_swell_gradient_error,slope_error);
			max_swell_laplacian_error=fmaxf(max_swell_laplacian_error,curvature_error);
			CHECK(slope_error<.0015f && curvature_error<.0015f);
			const float step=.125f;
			// The shortest ripples make the three-point h=.125 stencil's O(h^2) truncation
			// exceed .005 at (0,0,1.7). Use the five-point O(h^4) stencil at the same spacing,
			// rather than weakening the bound or halving h and amplifying float cancellation.
			// Accumulate downloaded/CPU float heights in double; both evaluators stay production float.
			auto cpu_height_sum=[&](float h) {
				return double(EvaluateOcean(f[0]+h,f[1],f[2]).height)
					+EvaluateOcean(f[0]-h,f[1],f[2]).height
					+EvaluateOcean(f[0],f[1]+h,f[2]).height
					+EvaluateOcean(f[0],f[1]-h,f[2]).height;
			};
			const double cpu_near=cpu_height_sum(step),cpu_far=cpu_height_sum(2*step);
			const double gpu_near=double(depth_at(bytes,columns,14,i))+depth_at(bytes,columns,15,i)
				+depth_at(bytes,columns,16,i)+depth_at(bytes,columns,17,i);
			const double gpu_far=double(depth_at(bytes,columns,26,i))+depth_at(bytes,columns,27,i)
				+depth_at(bytes,columns,28,i)+depth_at(bytes,columns,29,i);
			const double gpu_center=depth_at(bytes,columns,7,i);
			const float coarse_cpu=float((cpu_near-4.0*ocean.height)/(step*step));
			const float coarse_gpu=float((gpu_near-4.0*gpu_center)/(step*step));
			const float numeric_cpu=float((16.0*cpu_near-cpu_far-60.0*ocean.height)/(12.0*step*step));
			const float numeric_gpu=float((16.0*gpu_near-gpu_far-60.0*gpu_center)/(12.0*step*step));
			const float cpu_error=fabsf(numeric_cpu-surface.laplacian);
			const float gpu_error=fabsf(numeric_gpu-gpu_curvature);
			const float cpu_coarse_error=fabsf(coarse_cpu-surface.laplacian);
			const float gpu_coarse_error=fabsf(coarse_gpu-gpu_curvature);
			max_cpu_curvature_error=fmaxf(max_cpu_curvature_error,cpu_error);
			max_gpu_curvature_error=fmaxf(max_gpu_curvature_error,gpu_error);
			max_cpu_coarse_curvature_error=fmaxf(max_cpu_coarse_curvature_error,cpu_coarse_error);
			max_gpu_coarse_curvature_error=fmaxf(max_gpu_coarse_curvature_error,gpu_coarse_error);
			CHECK(cpu_error<.005f && gpu_error<.005f);
			if (cpu_coarse_error>.002f && gpu_coarse_error>.002f
				&& cpu_error<cpu_coarse_error*.25f && gpu_error<gpu_coarse_error*.25f) ++curvature_refinements;
			++curvature_checks;
			// Armed control keeps the same warped phases and envelope, but deliberately omits their
			// spatial derivatives. Merely adding k-squared per-mode curvature must fail this fixture.
			const float q0=f[0]*.0241f+f[1]*.0157f-f[2]*.23f+.4f;
			const float q1=f[0]*-.0173f+f[1]*.0299f-f[2]*.19f+2.1f;
			const float s0=sinf(q0),s1=sinf(q1),px=f[0]+5*s0,py=f[1]+4*s1;
			float incomplete_laplacian=0;
#define TEST_OCEAN_UNWARPED_LAPLACIAN(kx,ky,amplitude,omega,phase) { \
			const float p=px*(kx)+py*(ky)-f[2]*(omega)+(phase); \
			const float s=sinf(p),c=cosf(p); \
			incomplete_laplacian+=(amplitude)*(-s-.64f*s*c)*((kx)*(kx)+(ky)*(ky)); }
			WATER_OCEAN_MODES(TEST_OCEAN_UNWARPED_LAPLACIAN)
#undef TEST_OCEAN_UNWARPED_LAPLACIAN
			incomplete_laplacian*=.86f+.14f*s0*s1;
			if (fabsf(numeric_cpu-incomplete_laplacian)>.008f) ++curvature_controls;
		}
	}
	CHECK(gradient_controls>=4);
	CHECK(curvature_checks>=4 && curvature_controls>=4);
	CHECK(curvature_refinements>=1); // a truncation-dominated case must improve on both CPU and GPU
	printf("sdl_gpu_frame_selfcheck: GPU ocean/blended waves: %u fixtures, CPU model error %.7f, numerical slopes CPU %.7f GPU %.7f, gradient controls %u\n",
		count,max_model_error,max_cpu_derivative_error,max_gpu_derivative_error,gradient_controls);
	printf("sdl_gpu_frame_selfcheck: GPU ocean curvature: model error %.7f, fourth-order height Laplacian CPU %.7f GPU %.7f (second-order %.7f/%.7f), %u checks, %u armed controls, %u stencil improvements\n",
		max_curvature_model_error,max_cpu_curvature_error,max_gpu_curvature_error,
		max_cpu_coarse_curvature_error,max_gpu_coarse_curvature_error,curvature_checks,curvature_controls,curvature_refinements);
	printf("sdl_gpu_frame_selfcheck: GPU macro swell: model error %.7f, numerical gradient %.7f Laplacian %.7f\n",
		max_swell_model_error,max_swell_gradient_error,max_swell_laplacian_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_mesh_correction(SdlGpuFrame *frame)
{
	// Exercise the production water normal, including its UV0 metadata gate and fixed-function
	// vertex transport. A zero-damped wave leaves an independently known linear correction plane.
	// The wrapper only supplies vertices; both shader bodies below are the actual generators.
	const unsigned width=32,height=32;
	struct Resources {
		SDL_GPUDevice *device;
		SDL_GPUShader *vertices[2]={},*pixel=NULL;
		SDL_GPUGraphicsPipeline *pipelines[2]={};
		SDL_GPUTexture *target=NULL,*white=NULL;
		SDL_GPUSampler *sampler=NULL;
		~Resources() {
			for(unsigned i=0;i<2;++i) {
				if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vertices[i]) SDL_ReleaseGPUShader(device,vertices[i]);
			}
			if(pixel) SDL_ReleaseGPUShader(device,pixel);
			if(target) SDL_ReleaseGPUTexture(device,target);
			if(white) SDL_ReleaseGPUTexture(device,white);
			if(sampler) SDL_ReleaseGPUSampler(device,sampler);
		}
	} resources;
	resources.device=frame->Device();
	const char *previous=getenv("ZH_WATER_DEBUG");
	const bool hadDebug=previous!=NULL;
	const std::string previousDebug=previous?previous:"";
	CHECK(setenv("ZH_WATER_DEBUG","normal",1)==0);
	PixelPipelineDescription pixelDescription={};std::string pixelSource;
	const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pixelDescription,
		pixelSource,COMBINER_SHADER_TARGET_SDL3_GPU);
	if(hadDebug) CHECK(setenv("ZH_WATER_DEBUG",previousDebug.c_str(),1)==0);
	else CHECK(unsetenv("ZH_WATER_DEBUG")==0);
	CHECK(generated);if(!generated) return;
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);CHECK(compiled);
	if(!compiled) {printf("water correction pixel compile: %s\n",error.c_str());return;}
	unsigned samplers=0,pixelUniforms=0;SDL3_Shader_Slots(spirv,false,samplers,pixelUniforms);
	CHECK(samplers<=16 && pixelUniforms==1);if(samplers>16 || pixelUniforms!=1) return;
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	if(!resources.pixel) return;
	for(unsigned transformed=0;transformed<2;++transformed) {
		VertexPipelineDescription vertexDescription={};
		vertexDescription.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;
		vertexDescription.StageCount=4;
		vertexDescription.Stages[0].TextureCoordinateIndex=0;
		vertexDescription.Stages[0].TextureTransformFlags=transformed?FF_TTFF_COUNT2:FF_TTFF_DISABLE;
		vertexDescription.Stages[1].TextureCoordinateIndex=1;
		std::string vertexSource;
		CHECK(VertexShader_Generate(vertexDescription,VERTEX_SHADER_TARGET_SDL3_GPU,vertexSource));
		const std::string entry="Output main(Input input)";
		const size_t entryAt=vertexSource.find(entry);CHECK(entryAt!=std::string::npos);
		if(entryAt==std::string::npos) return;
		vertexSource.replace(entryAt,entry.size(),"Output waterFixtureVertex(Input input)");
		vertexSource+="\ncbuffer Fixture : register(b1, space1) { float4 Correction; };\n"
			"Output main(uint id : SV_VertexID) { Input input;\n"
			"input.Position=float3(id==1?3.0:-1.0,id==2?3.0:-1.0,0.5);\n"
			"input.Normal=float3(0,0,1);input.Diffuse=float4(1,1,1,1);\n"
			"input.TexCoord0=float2(dot(input.Position.xy,Correction.xy)+Correction.z,0);\n"
			"input.TexCoord1=float2(0,0.5);return waterFixtureVertex(input); }\n";
		compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water correction vertex compile: %s\n",error.c_str());return;}
		resources.vertices[transformed]=SDL3_Create_Shader(resources.device,spirv,true,error);
		CHECK(resources.vertices[transformed]);if(!resources.vertices[transformed]) return;
		SDL_GPUColorTargetDescription colour={};colour.format=(SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
		SDL_GPUGraphicsPipelineCreateInfo info={};
		info.vertex_shader=resources.vertices[transformed];info.fragment_shader=resources.pixel;
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
		info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		resources.pipelines[transformed]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);
		CHECK(resources.pipelines[transformed]);if(!resources.pipelines[transformed]) return;
	}
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;
	textureInfo.format=(SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	textureInfo.width=width;textureInfo.height=height;textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&textureInfo);
	textureInfo.width=textureInfo.height=1;textureInfo.usage|=SDL_GPU_TEXTUREUSAGE_SAMPLER;
	resources.white=SDL_CreateGPUTexture(resources.device,&textureInfo);
	SDL_GPUSamplerCreateInfo samplerInfo={};samplerInfo.min_filter=samplerInfo.mag_filter=SDL_GPU_FILTER_NEAREST;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&samplerInfo);
	CHECK(resources.target && resources.white && resources.sampler);
	if(!resources.target || !resources.white || !resources.sampler) return;
	SdlVertexConstants vertexConstants={};SdlWaterPixelConstants water={};
	for(unsigned i=0;i<4;++i) {
		vertexConstants.WorldViewProjection[i*5]=vertexConstants.WorldView[i*5]=vertexConstants.NormalTransform[i*5]=1;
		water.WorldFromClip[i*5]=1;
		for(unsigned stage=0;stage<MAXIMUM_VERTEX_STAGES;++stage) vertexConstants.TextureMatrix[stage][i*5]=1;
	}
	// A stale matrix must be harmless with metadata passthrough; the armed alternate pipeline
	// enables it and must visibly double the correction gradient.
	vertexConstants.TextureMatrix[0][0]=2;vertexConstants.TextureMatrix[0][8]=13;
	water.Viewport[2]=width;water.Viewport[3]=height;water.CameraPosition[2]=10;water.SunDirection[2]=1;
	water.SunColour[0]=water.SunColour[1]=water.SunColour[2]=1;
	water.TerrainParameters[1]=100;water.SceneParameters[1]=water.SceneParameters[2]=1;
	water.OpticalParameters[0]=3;water.OpticalParameters[1]=1;
	float correction[4]={.35f,-.20f,1.25f,0};
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&](bool enabled,unsigned transformed) {
		water.OpticalParameters[3]=enabled?1.0f:0.0f;
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.white;
		attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
		attachment.clear_color.r=attachment.clear_color.g=attachment.clear_color.b=attachment.clear_color.a=1;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_EndGPURenderPass(pass);
		attachment.texture=resources.target;attachment.clear_color.r=attachment.clear_color.g=attachment.clear_color.b=0;
		pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
		SDL_BindGPUGraphicsPipeline(pass,resources.pipelines[transformed]);
		SDL_GPUTextureSamplerBinding bindings[16]={};
		for(unsigned i=0;i<samplers;++i) {bindings[i].texture=resources.white;bindings[i].sampler=resources.sampler;}
		if(samplers) SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
		SDL_PushGPUVertexUniformData(commands,0,&vertexConstants,sizeof(vertexConstants));
		SDL_PushGPUVertexUniformData(commands,1,correction,sizeof(correction));
		SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
		std::vector<uint8_t> bytes;CHECK(frame->Read_Back(resources.target,width,height,bytes));++renders;return bytes;
	};
	auto checkNormal=[&](const std::vector<uint8_t>& bytes,float gx,float gy) {
		const float inverse=1.0f/sqrtf(1+gx*gx+gy*gy);
		const float expected[3]={.5f+.5f*inverse,.5f-.5f*gy*inverse,.5f-.5f*gx*inverse}; // BGR
		bool correct=bytes.size()==width*height*4;
		if(correct) for(unsigned y=4;y<height-4;++y) for(unsigned x=4;x<width-4;++x) for(unsigned c=0;c<3;++c)
			correct &= fabsf(bytes[(y*width+x)*4+c]/255.0f-expected[c])<2.0f/255.0f;
		CHECK(correct);
	};
	const auto legacy=render(false,0);checkNormal(legacy,0,0);
	const auto corrected=render(true,0);checkNormal(corrected,correction[0],correction[1]);CHECK(corrected!=legacy);
	correction[2]+=17;const auto offset=render(true,0);checkNormal(offset,correction[0],correction[1]);CHECK(offset==corrected);
	const auto transformed=render(true,1);checkNormal(transformed,2*correction[0],2*correction[1]);CHECK(transformed!=corrected);
	// Rotate projection by 90 degrees and independently invert it. A screen-space gradient used
	// directly as a world slope now has the wrong signs/axes even though the height field is linear.
	vertexConstants.WorldViewProjection[0]=vertexConstants.WorldViewProjection[5]=0;
	vertexConstants.WorldViewProjection[1]=1;vertexConstants.WorldViewProjection[4]=-1;
	water.WorldFromClip[0]=water.WorldFromClip[5]=0;water.WorldFromClip[1]=-1;water.WorldFromClip[4]=1;
	const auto rotated=render(true,0);checkNormal(rotated,correction[0],correction[1]);
	correction[0]=-.42f;correction[1]=.28f;
	checkNormal(render(true,0),correction[0],correction[1]);
	checkNormal(render(false,0),0,0);
	printf("sdl_gpu_frame_selfcheck: GPU production water correction: %u renders; known normals, legacy gate, constant offsets, projection rotation and armed texture-transform control passed\n",renders);
}

static void check_gpu_water_optical_model(SdlGpuFrame *frame)
{
	// Execute the same light-transport function used by the generated water shader. Each row has
	// path, river blend, offshore blend, optical reference depth, authored opacity and padding.
	// Columns capture all three channels independently in an R32 target without display conversion.
	const float fixtures[][8] = {
		{0, 0, 1, 2.7f, .75f, 0, 0, 0},
		{.25f, 0, 1, 3, 1, 0, 0, 0}, // submerged object's short ray stays much clearer than the bed
		{10, 0, 0, 2.7f, .75f, 0, 0, 0},
		{10, 0, .5f, 2.7f, .75f, 0, 0, 0},
		{10, 0, 1, 2.7f, .75f, 0, 0, 0}, // mission sea optical interpretation
		{3.2f, 0, 0, 3, 1, 0, 0, 0},
		{3.2f, 0, .5f, 3, 1, 0, 0, 0},
		{3.2f, 0, 1, 3, 1, 0, 0, 0}, // shell sea optical interpretation
		{2, 1, 0, 2.7f, .75f, 0, 0, 0},
		{2, 1, .5f, 2.7f, .75f, 0, 0, 0},
		{2, 1, 1, 2.7f, .75f, 0, 0, 0},
		{180, 1, 0, 3, 0, 0, 0, 0},
		{360, 1, 0, 3, 0, 0, 0, 0}, // no old 180-world-unit plateau
		{100000, 1, 1, 3, 1, 0, 0, 0},
		{-4, -1, -1, -1, -1, 0, 0, 0},
		{.5f, 2, 2, 0, 2, 0, 0, 0},
		{10, .5f, .5f, 2.7f, .75f, 0, 0, 0},
		{1.0e20f, 0, 0, 1.0e20f, 0, 0, 0, 0}
	};
	const unsigned int count = sizeof(fixtures) / sizeof(fixtures[0]);
	const char *vertex_source =
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source = "cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source += std::to_string(count * 2) + "]; };\n";
	pixel_source += WaterOpticalModel::GetHlslSource();
	pixel_source += "float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y; float4 f=Fixtures[row*2];\n"
		"return WaterOpticalTransmission(f.x,f.y,f.z,f.w,Fixtures[row*2+1].x)[(uint)position.x]; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source, true, spirv, error));
	SDL_GPUShader *vertex = SDL3_Create_Shader(frame->Device(), spirv, true, error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source, false, spirv, error));
	SDL_GPUShader *pixel = SDL3_Create_Shader(frame->Device(), spirv, false, error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description = {};
	description.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.vertex_shader = vertex;
	pipeline_info.fragment_shader = pixel;
	pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions = &description;
	pipeline_info.target_info.num_color_targets = 1;
	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(frame->Device(), &pipeline_info);
	SDL_GPUTextureCreateInfo texture_info = {};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width = 3; texture_info.height = count;
	texture_info.layer_count_or_depth = 1; texture_info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands != NULL);
	SDL_GPUColorTargetInfo attachment = {};
	attachment.texture = target;
	attachment.load_op = SDL_GPU_LOADOP_DONT_CARE;
	attachment.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &attachment, 1, NULL);
	CHECK(pass != NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_PushGPUFragmentUniformData(commands, 0, fixtures, sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target, 3, count, bytes));
	float max_model_error = 0;
	for (unsigned int i=0; i<count; ++i) {
		const WaterOpticalModel::RGB cpu=WaterOpticalModel::Transmission(
			fixtures[i][0],fixtures[i][1],fixtures[i][2],fixtures[i][3],fixtures[i][4]);
		const float expected[3]={cpu.r,cpu.g,cpu.b};
		for (unsigned int c=0; c<3; ++c) {
			const float gpu=depth_at(bytes,3,c,i),difference=fabsf(gpu-expected[c]);
			if (difference>max_model_error) max_model_error=difference;
			CHECK(gpu>=0 && gpu<=1 && difference<0.00001f);
			CHECK(difference<=expected[c]*0.00003f+1e-36f);
		}
	}
	for (unsigned int c=0;c<3;++c) {
		CHECK(depth_at(bytes,3,c,0)==1.0f);
		CHECK(depth_at(bytes,3,c,1)>depth_at(bytes,3,c,7));
		CHECK(depth_at(bytes,3,c,2)>depth_at(bytes,3,c,3) && depth_at(bytes,3,c,3)>depth_at(bytes,3,c,4));
		CHECK(depth_at(bytes,3,c,5)>depth_at(bytes,3,c,6) && depth_at(bytes,3,c,6)>depth_at(bytes,3,c,7));
		CHECK(depth_at(bytes,3,c,12)<depth_at(bytes,3,c,11));
	}
	printf("sdl_gpu_frame_selfcheck: GPU water optical transmission: %u fixtures, max CPU model error %.9f\n",count,max_model_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_optical_ray_shift(SdlGpuFrame *frame)
{
	// delta-height, view.z, offshore, displacement bound, actual scene ray distance, camera distance.
	const float fixtures[][8]={
		{2,.8f,0,7,20,100,0,0}, {2,.8f,1,7,20,100,0,0}, {-2,.8f,1,7,20,100,0,0},
		{2,.8f,.5f,7,20,100,0,0}, {1000,.8f,1,7,20,100,0,0}, {-1000,.8f,1,7,20,100,0,0},
		{1000,.001f,1,7,100,100,0,0}, {-1000,.001f,1,7,100,100,0,0},
		{1,0,1,7,20,100,0,0}, {1,-.8f,1,7,20,100,0,0},
		{-2,.8f,1,7,.25f,100,0,0}, {-2,.8f,1,7,4,100,0,0}, // near object and farther bed
		{2,.8f,1,7,20,.3f,0,0}, {-2,.8f,1,7,.03f,100,0,0}, {2,.8f,1,7,20,.04f,0,0},
		{2,.8f,-1,7,20,100,0,0}, {2,.8f,2,7,20,100,0,0}, {-1,.2f,.5f,7,2.25f,100,0,0},
		{-1000,.8f,1,0,20,100,0,0}
	};
	const unsigned int count=sizeof(fixtures)/sizeof(fixtures[0]),columns=4;
	const char *vertex_source=
		"float4 main(uint id : SV_VertexID) : SV_Position {\n"
		"return float4(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0, 0, 1); }\n";
	std::string pixel_source="cbuffer Samples : register(b0, space3) { float4 Fixtures[";
	pixel_source+=std::to_string(count*2)+"]; };\n";
	pixel_source+=WaterOpticalModel::GetHlslSource();
	pixel_source+="float main(float4 position : SV_Position) : SV_Target0 {\n"
		"uint row=(uint)position.y,column=(uint)position.x; float4 f=Fixtures[row*2],g=Fixtures[row*2+1];\n"
		"float shift=WaterOpticalRayShift(f.x,f.y,f.z,f.w,g.x,g.y);\n"
		"if (column==0) return shift;\n"
		"if (column==1) return 5+sqrt(max(1-f.y*f.y,0))*shift;\n"
		"if (column==2) return 12+f.y*shift;\n"
		"return WaterOpticalTransmission(abs(g.x+shift),0,1,3,.75).r; }\n";
	std::vector<unsigned char> spirv;
	std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertex_source,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixel_source,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);
	CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};
	description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipeline_info={};
	pipeline_info.vertex_shader=vertex;pipeline_info.fragment_shader=pixel;
	pipeline_info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline_info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
	pipeline_info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipeline_info.target_info.color_target_descriptions=&description;
	pipeline_info.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipeline_info);
	SDL_GPUTextureCreateInfo texture_info={};
	texture_info.type=SDL_GPU_TEXTURETYPE_2D;texture_info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	texture_info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width=columns;texture_info.height=count;
	texture_info.layer_count_or_depth=1;texture_info.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture_info);
	CHECK(pipeline && target);
	CHECK(frame->Flush());
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
	CHECK(commands!=NULL);
	SDL_GPUColorTargetInfo attachment={};
	attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);
	CHECK(pass!=NULL);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);
	SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);
	SDL_EndGPURenderPass(pass);
	CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;
	CHECK(frame->Read_Back(target,columns,count,bytes));
	float max_error=0;
	for (unsigned int i=0;i<count;++i) {
		using namespace WaterOpticalModel;
		const float *f=fixtures[i],cpu=RayShift(f[0],f[1],f[2],f[3],f[4],f[5]);
		const float vx=sqrtf(fmaxf(1-f[1]*f[1],0));
		const float expected[4]={cpu,5+vx*cpu,12+f[1]*cpu,Transmission(fabsf(f[4]+cpu),0,1,3,.75f).r};
		for (unsigned int c=0;c<columns;++c) {
			const float gpu=depth_at(bytes,columns,c,i),difference=fabsf(gpu-expected[c]);
			max_error=fmaxf(max_error,difference);
			CHECK(std::isfinite(gpu) && difference<.00003f);
		}
		const float shift=depth_at(bytes,columns,0,i),x=depth_at(bytes,columns,1,i),z=depth_at(bytes,columns,2,i);
		CHECK(fabsf(shift)<=2*f[3]/.2f+.00001f);
		CHECK(f[4]+shift>=fminf(f[4],RaySurfaceMargin)-.00001f);
		CHECK(f[5]-shift>=fminf(f[5],RaySurfaceMargin)-.00001f);
		CHECK(fabsf((x-5)*f[1]-(z-12)*vx)<.00001f);
		if (f[2]<=0 || f[1]<=0) CHECK(shift==0 && x==5 && z==12);
	}
	CHECK(depth_at(bytes,columns,0,1)==2.5f && depth_at(bytes,columns,0,2)==-2.5f);
	CHECK(depth_at(bytes,columns,0,3)==1.25f);
	CHECK(depth_at(bytes,columns,3,10)>depth_at(bytes,columns,3,11)); // object stays clearer than the bed
	const float wrong_object=WaterOpticalModel::Transmission(fabsf(.25f-2.5f),0,1,3,.75f).r;
	CHECK(wrong_object<depth_at(bytes,columns,3,11)); // control catches a shift that crossed the object
	CHECK(fabsf(depth_at(bytes,columns,0,12)-.22f)<.000001f);
	printf("sdl_gpu_frame_selfcheck: GPU optical ray correction: %u fixtures, max CPU/coordinate error %.9f; ray, bounds, near-object control passed\n",count,max_error);
	SDL_ReleaseGPUTexture(frame->Device(),target);
	SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);
	SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_water_hull_distance(SdlGpuFrame *frame)
{
	using namespace WaterHullModel;
	const unsigned count=16,columns=4;
	struct Uniforms { Hull hulls[MaxHulls]; float fixtures[count][4]; } uniforms={};
	const Point rectangle[]={{-100,-20},{100,-20},{100,20},{-100,20}};
	for(unsigned vessel=0;vessel<MaxHulls;++vessel) {
		Hull &h=uniforms.hulls[vessel];
		h.data[0][0]=3028;h.data[0][1]=588;h.data[0][2]=7;h.data[0][3]=vessel==7?12.0f:4.0f;
		h.data[1][0]=vessel==0?1.0f:0.0f;h.data[1][1]=vessel==0?0.0f:-1.0f;
		h.data[1][2]=100;h.data[1][3]=vessel==7?100.0f:20.0f;
		for(unsigned i=0;i<MaxVertices;++i) {
			const float angle=float(i)*6.28318530718f/MaxVertices;
			const Point p=vessel==7?Point{100*cosf(angle),100*sinf(angle)}:rectangle[i%4];
			h.data[4+i/2][(i%2)*2]=p.x;h.data[4+i/2][(i%2)*2+1]=p.y;
		}
	}
	const Point local[]={{0,0},{0,20},{0,24.5f},{0,29},{100,0},{105,25},{-101,-21},{0,-100},
		{0,0},{0,24.5f},{105,25},{-101,-21},{0,0},{0,110},{98,-26},{86,-50}};
	for(unsigned i=0;i<count;++i) {
		const unsigned index=i<8?0:i<12?3:7;
		const Hull &h=uniforms.hulls[index];const Point p=local[i];
		uniforms.fixtures[i][0]=h.data[0][0]+p.x*h.data[1][0]-p.y*h.data[1][1];
		uniforms.fixtures[i][1]=h.data[0][1]+p.x*h.data[1][1]+p.y*h.data[1][0];
		uniforms.fixtures[i][2]=float(index);
	}
	const char *vertexSource="float4 main(uint id : SV_VertexID) : SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string pixelSource="cbuffer Samples : register(b0, space3) { float4 HullData[80]; float4 Fixtures[16]; };\n";
	pixelSource+=GetHlslSource();
	pixelSource+="float main(float4 position : SV_Position) : SV_Target { uint row=(uint)position.y,column=(uint)position.x; "
		"float4 f=Fixtures[row]; int baseIndex=(int)f.z*10; float2 delta=f.xy-HullData[baseIndex].xy,axis=HullData[baseIndex+1].xy; "
		"float2 local=float2(dot(delta,axis),dot(delta,float2(-axis.y,axis.x))); "
		"float distance=WaterHullSignedDistance(local,baseIndex,(int)HullData[baseIndex].w); "
		"if(column==0) return distance; if(column==1) return smoothstep(0,9,distance); return local[column-2]; }";
	std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error));
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error));
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(vertex && pixel);
	SDL_GPUColorTargetDescription description={};description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};
	pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pipelineInfo.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipelineInfo.target_info.color_target_descriptions=&description;pipelineInfo.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;textureInfo.width=columns;textureInfo.height=count;
	textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&textureInfo);CHECK(pipeline && target);
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
	SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
	SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUFragmentUniformData(commands,0,&uniforms,sizeof(uniforms));
	SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	std::vector<uint8_t> bytes;CHECK(frame->Read_Back(target,columns,count,bytes));float maxError=0;
	for(unsigned i=0;i<count;++i) {
		const Hull &h=uniforms.hulls[(unsigned)uniforms.fixtures[i][2]];
		const Point p=LocalPoint(h,uniforms.fixtures[i][0],uniforms.fixtures[i][1]);
		const float cpu=SignedDistance(p,Unpack(h)),damping=Damping(h,uniforms.fixtures[i][0],uniforms.fixtures[i][1],7);
		const float expected[]={cpu,damping,p.x,p.y};
		for(unsigned c=0;c<4;++c) {const float e=fabsf(depth_at(bytes,columns,c,i)-expected[c]);maxError=fmaxf(maxError,e);CHECK(e<.0003f);}
		if(i<12) { // Independent exact rectangle SDF, including outside corners and inside sign.
			const float qx=fabsf(local[i].x)-100,qy=fabsf(local[i].y)-20;
			const float exact=sqrtf(fmaxf(qx,0)*fmaxf(qx,0)+fmaxf(qy,0)*fmaxf(qy,0))+fminf(fmaxf(qx,qy),0);
			CHECK(fabsf(depth_at(bytes,columns,0,i)-exact)<.0003f);
		}
	}
	CHECK(depth_at(bytes,columns,0,0)<0 && depth_at(bytes,columns,0,2)>0);
	CHECK(fabsf(depth_at(bytes,columns,1,2)-.5f)<.00001f);
	// Last vertex matters: collapsing it measurably moves the lower-right edge of the last hull.
	Polygon missing=Unpack(uniforms.hulls[7]);--missing.count;
	CHECK(fabsf(SignedDistance(local[14],missing)-depth_at(bytes,columns,0,14))>2);
	printf("sdl_gpu_frame_selfcheck: GPU hull contours: %u fixtures, max CPU/rotation error %.7f; exact rectangle, final vertex, sign and damping controls passed\n",count,maxError);
	SDL_ReleaseGPUTexture(frame->Device(),target);SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);SDL_ReleaseGPUShader(frame->Device(),pixel);
}

// These are renderer fixtures, not injected gameplay velocities. The stable USA02 save has
// stationary ships and no PT-boat patrol script, so it cannot validate moving-vessel integration.
// Render the production wake function over an independently specified rectangular waterline.
static void check_gpu_water_hull_effects(SdlGpuFrame *frame)
{
	using namespace WaterHullModel;
	const unsigned samplesX=301,samplesY=81,channels=3,width=samplesX*channels;
	struct Uniforms {
		float hullParameters[4]; Hull hulls[MaxHulls];
		float query[4],testBasis[4]; // level,time,offshore,footprint; origin XY,forward XY
	} uniforms={};
	uniforms.hullParameters[0]=1;
	uniforms.query[0]=7;uniforms.query[1]=1.25f;uniforms.query[2]=1;uniforms.query[3]=.5f;
	uniforms.testBasis[2]=1;
	Hull &h=uniforms.hulls[0];
	h.data[0][2]=7;h.data[0][3]=4;
	h.data[1][0]=1;h.data[1][2]=20;h.data[1][3]=4;
	h.data[2][2]=.37f;h.data[2][3]=sqrtf(20*20+4*4);
	h.data[3][0]=-20;h.data[3][2]=20;
	const Point rectangle[]={{-20,-4},{20,-4},{20,4},{-20,4}};
	for(unsigned i=0;i<MaxVertices;++i) {
		h.data[4+i/2][(i%2)*2]=rectangle[i%4].x;
		h.data[4+i/2][(i%2)*2+1]=rectangle[i%4].y;
	}
	const Uniforms stationaryUniforms=uniforms;
	PixelPipelineDescription waterPipeline={};
	std::string waterSource;
	CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,waterPipeline,waterSource,COMBINER_SHADER_TARGET_SDL3_GPU));
	// Import the exact production noise dependency, not a test-only constant or copy that could
	// hide broken animated wash. The tested effects body is also the production header emission.
	const size_t noiseStart=waterSource.find("float oceanHash("),noiseEnd=waterSource.find("float3 oceanSky(");
	CHECK(noiseStart!=std::string::npos && noiseEnd!=std::string::npos && noiseEnd>noiseStart);
	if(noiseStart==std::string::npos || noiseEnd==std::string::npos || noiseEnd<=noiseStart) return;
	const char *vertexSource="float4 main(uint id : SV_VertexID) : SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string pixelSource="cbuffer Samples : register(b0, space3) { float4 HullParameters; float4 HullData[80]; float4 Query; float4 TestBasis; };\n";
	pixelSource+=waterSource.substr(noiseStart,noiseEnd-noiseStart);
	pixelSource+=GetHlslSource();pixelSource+=GetEffectsHlslSource();
	pixelSource+="float main(float4 pixel : SV_Position) : SV_Target { "
		"uint column=(uint)pixel.x, channel=column%3; float2 local=float2((int)(column/3)-150,(int)pixel.y-40); "
		"float2 world=TestBasis.xy+local.x*TestBasis.zw+local.y*float2(-TestBasis.w,TestBasis.z); "
		"return oceanHullEffects(world,Query.x,Query.y,Query.z,0.75,Query.w)[channel]; }";
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);CHECK(compiled);
	if(!compiled) {printf("wake vertex compile: %s\n",error.c_str());return;}
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);
	compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);CHECK(compiled);
	if(!compiled) {printf("wake pixel compile: %s\n",error.c_str());SDL_ReleaseGPUShader(frame->Device(),vertex);return;}
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(vertex && pixel);
	if(!vertex || !pixel) return;
	SDL_GPUColorTargetDescription description={};description.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pipelineInfo.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
	pipelineInfo.target_info.color_target_descriptions=&description;pipelineInfo.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;textureInfo.width=width;textureInfo.height=samplesY;
	textureInfo.layer_count_or_depth=1;textureInfo.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&textureInfo);CHECK(pipeline && target);
	if(!pipeline || !target) return;
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&]() {
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
		SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUFragmentUniformData(commands,0,&uniforms,sizeof(uniforms));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
		std::vector<uint8_t> bytes;CHECK(frame->Read_Back(target,width,samplesY,bytes));++renders;
		bool bounded=true;
		for(unsigned y=0;y<samplesY;++y) for(unsigned x=0;x<width;++x) {
			const float v=depth_at(bytes,width,x,y);bounded &= std::isfinite(v) && v>=0 && v<=1;
		}
		CHECK(bounded);return bytes;
	};
	auto value=[&](const std::vector<uint8_t>& bytes,int x,int y,unsigned channel) {
		return depth_at(bytes,width,unsigned(x+150)*channels+channel,unsigned(y+40));
	};
	auto empty=[&](const std::vector<uint8_t>& bytes) {
		bool ok=true;
		for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
			ok &= value(bytes,x,y,0)==0 && value(bytes,x,y,1)==0 && value(bytes,x,y,2)==1;
		CHECK(ok);
	};
	const auto stationary=render();
	// A stationary vessel has a broken web outside its thin contact rim, not a filled white
	// halo or a speed-independent wake. Measure the actual production shader over the independent
	// rectangle SDF, excluding its interior and the 0--3 unit contact zone from the lace count.
	unsigned outerSamples=0,outerLace=0,outerGaps=0;bool washBounded=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) {
		const float dx=fmaxf(fabsf(float(x))-20,0),dy=fmaxf(fabsf(float(y))-4,0);
		const float distance=sqrtf(dx*dx+dy*dy),foam=value(stationary,x,y,0);
		if(distance>=3 && distance<=8) {
			++outerSamples;if(foam>.06f) ++outerLace;if(foam<.02f) ++outerGaps;
		}
		if(distance>=12 || (abs(x)<20 && abs(y)<4))
			washBounded &= foam==0 && value(stationary,x,y,1)==0;
	}
	CHECK(washBounded && outerLace>outerSamples/10 && outerGaps>outerSamples/4);
	uniforms.hulls[0].data[2][0]=.3f;const auto threshold=render();CHECK(threshold==stationary);
	uniforms.hulls[0].data[2][0]=6.3f;const auto forward=render();
	// Wake must begin at the stern and have no temporal gap. The two V ribbons have a nonzero
	// floor even if animated foam breaks the centreline; their one-cell steps form an 8-connected
	// path from the hull. Turbulence provides an independently connected centre through the wash.
	bool connected=true;
	for(int behind=1;behind<=10;++behind) {
		const int y=int(floorf(1.4f+.25f*behind+.5f));
		connected &= value(forward,-20-behind,0,1)>.05f;
		connected &= value(forward,-20-behind,y,0)>.04f && value(forward,-20-behind,-y,0)>.04f;
	}
	CHECK(connected);
	CHECK(value(forward,-35,0,1)>.1f && value(stationary,-35,0,1)==0);
	bool forwardBounds=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
		if(x<=-44 || x>=33) forwardBounds &= value(forward,x,y,0)==0 && value(forward,x,y,1)==0;
	CHECK(forwardBounds);
	// Heading and world translation must not move the wake relative to its actual hull. A 90deg
	// rotation is exact in floating point, so this is sensitive to world/local velocity mistakes.
	uniforms.hulls[0].data[0][0]=3028;uniforms.hulls[0].data[0][1]=588;
	uniforms.hulls[0].data[1][0]=0;uniforms.hulls[0].data[1][1]=1;
	uniforms.hulls[0].data[2][0]=0;uniforms.hulls[0].data[2][1]=6.3f;
	uniforms.testBasis[0]=3028;uniforms.testBasis[1]=588;uniforms.testBasis[2]=0;uniforms.testBasis[3]=1;
	const auto rotated=render();CHECK(rotated==forward);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=-6.3f;
	const auto reverse=render();
	bool reversed=true;
	for(int ahead=1;ahead<=10;++ahead) reversed &= value(reverse,20+ahead,0,1)>.05f;
	CHECK(reversed && value(reverse,35,0,1)>.1f && value(reverse,-35,0,1)==0);
	CHECK(value(forward,35,0,1)==0); // Armed wrong-end control: forward's bow must not emit a trail.
	// Near sideways travel the physical wave origin blends across the hull. Foam must use the
	// same continuous endpoint selection, instead of jumping forty units on a velocity sign flip.
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=.00001f;uniforms.hulls[0].data[2][1]=6.3f;
	const auto broadsidePositive=render();uniforms.hulls[0].data[2][0]=-.00001f;
	const auto broadsideNegative=render();float broadsideChange=0;
	CHECK(value(broadsidePositive,0,-15,1)>.1f && value(broadsideNegative,0,-15,1)>.1f);
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) for(unsigned c=0;c<3;++c)
		broadsideChange=fmaxf(broadsideChange,fabsf(value(broadsidePositive,x,y,c)-value(broadsideNegative,x,y,c)));
	CHECK(broadsideChange<.002f);
	// Force both endpoint choices to each old hard-selected end. This armed control must expose
	// a large jump in exactly the atlas and travel direction used by the continuity assertion.
	uniforms.hulls[0].data[3][0]=uniforms.hulls[0].data[3][2]=-20;const auto forcedLeft=render();
	uniforms.hulls[0].data[3][0]=uniforms.hulls[0].data[3][2]=20;const auto forcedRight=render();
	CHECK(value(forcedLeft,-20,-15,1)-value(forcedRight,-20,-15,1)>.1f);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;
	uniforms.query[2]=0;empty(render());
	uniforms.query[2]=1;uniforms.query[0]=7.11f;empty(render());
	uniforms.query[0]=7;uniforms.hullParameters[0]=0;empty(render());
	uniforms.hullParameters[0]=1;uniforms.query[2]=.25f;const auto transition=render();
	bool blended=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x) for(unsigned c=0;c<3;++c)
		blended &= fabsf(value(transition,x,y,c)-value(forward,x,y,c)*(c==2?1.0f:.25f))<.000001f;
	CHECK(blended);
	uniforms.query[2]=1;const auto paused=render();CHECK(paused==forward);
	uniforms.query[1]+=1.0f;const auto animated=render();unsigned changed=0;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=150;++x)
		if(fabsf(value(animated,x,y,0)-value(forward,x,y,0))>.005f) ++changed;
	CHECK(changed>50);
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=60;
	const auto capped=render();bool lengthBounded=true;
	for(int y=-40;y<=40;++y) for(int x=-150;x<=-130;++x)
		lengthBounded &= value(capped,x,y,0)==0 && value(capped,x,y,1)==0;
	CHECK(lengthBounded && value(capped,-100,0,1)>.1f);
	// All eight array entries are legal. Only the final hull matches this level: a truncated count
	// or bad ten-register stride must lose the entire result, rather than accidentally pass.
	uniforms=stationaryUniforms;uniforms.hulls[0].data[2][0]=6.3f;
	uniforms.hulls[7]=uniforms.hulls[0];uniforms.hulls[0]=Hull();uniforms.hullParameters[0]=8;
	const auto lastHull=render();CHECK(lastHull==forward);
	printf("sdl_gpu_frame_selfcheck: GPU production hull wash/wake: %u spatial renders, %u pixels each; local lace %u/%u with %u gaps, speed gate, stern attachment, reverse, broadside continuity %.8f with armed snap control, rigid transform, level/offshore, pause, animation (%u foam pixels), 110-unit bound and final hull passed\n",renders,samplesX*samplesY,outerLace,outerSamples,outerGaps,broadsideChange,changed);
	SDL_ReleaseGPUTexture(frame->Device(),target);SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	SDL_ReleaseGPUShader(frame->Device(),vertex);SDL_ReleaseGPUShader(frame->Device(),pixel);
}

// A float readback used only by verification. Runtime FFT never reads a result back to the CPU.
static std::vector<float> read_float_targets(SdlGpuFrame *frame,SDL_GPUTexture *const *textures,
	unsigned count,unsigned width,unsigned height)
{
	const unsigned bytes=width*height*4*sizeof(float),total=count*bytes;
	std::vector<float> result(total/sizeof(float));
	CHECK(frame->Flush());
	SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=total;
	SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(frame->Device(),&info);CHECK(transfer);
	if(!transfer) return result;
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
	if(!commands) {SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);return result;}
	SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
	for(unsigned i=0;i<count;++i) {
		SDL_GPUTextureRegion region={};region.texture=textures[i];region.w=width;region.h=height;region.d=1;
		SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;destination.offset=i*bytes;
		destination.pixels_per_row=width;destination.rows_per_layer=height;
		SDL_DownloadFromGPUTexture(copy,&region,&destination);
	}
	SDL_EndGPUCopyPass(copy);SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
	if(fence) {
		CHECK(SDL_WaitForGPUFences(frame->Device(),true,&fence,1));SDL_ReleaseGPUFence(frame->Device(),fence);
		const void *mapped=SDL_MapGPUTransferBuffer(frame->Device(),transfer,false);CHECK(mapped);
		if(mapped) {memcpy(result.data(),mapped,total);SDL_UnmapGPUTransferBuffer(frame->Device(),transfer);}
	}
	SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);return result;
}

static std::vector<float> read_ocean(SdlGpuFrame *frame)
{
	SDL_GPUTexture *textures[6];for(unsigned i=0;i<6;++i) textures[i]=frame->Ocean_Map(i);
	return read_float_targets(frame,textures,6,WaterFftSpectrum::Resolution,WaterFftSpectrum::Resolution);
}

static void check_gpu_water_vertex_uniform_order(SdlGpuFrame *frame)
{
	// Use the real recorded draw path, with the complete water block shared between VS b1 and
	// PS b0. Reading its last hull and FFT tail catches a truncated copy that a count-only test
	// misses. All draws remain queued while the source variables are changed again.
	const unsigned width=4,rows=6;
	const unsigned timeRegister=offsetof(SdlWaterPixelConstants,WaterParameters)/16;
	const unsigned lastHullRegister=offsetof(SdlWaterPixelConstants,HullData)/16+79;
	const unsigned fftRegister=offsetof(SdlWaterPixelConstants,FftParameters)/16;
	std::string block="cbuffer WaterBlock : register(b1,space1) { float4 Water[";
	block+=std::to_string(sizeof(SdlWaterPixelConstants)/16)+"]; };\n";
	const std::string output="struct Output { float4 Position:SV_Position; [[vk::location(0)]] float4 Value:TEXCOORD0; };\n";
	const std::string position="Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);";
	std::string waterVertex="cbuffer FixedBlock : register(b0,space1) { float4 Fixed; };\n"+block+output;
	waterVertex+="Output main(uint id:SV_VertexID) {"+position+
		"o.Value=float4(Fixed.x,Water["+std::to_string(timeRegister)+"].x,Water["+
		std::to_string(lastHullRegister)+"].w,Water["+std::to_string(fftRegister)+"].y);return o;}\n";
	std::string waterPixel="cbuffer WaterBlock : register(b0,space3) { float4 Water["+
		std::to_string(sizeof(SdlWaterPixelConstants)/16)+"]; };\n";
	waterPixel+="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {\n"
		"value.w+=abs(value.y-Water["+std::to_string(timeRegister)+"].x)*100.0;return value;}\n";
	const std::string plainVertex="cbuffer FixedBlock : register(b0,space1) { float4 Fixed; };\n"+output+
		"Output main(uint id:SV_VertexID) {"+position+"o.Value=float4(9,8,7,6)*Fixed.x;return o;}\n";
	const char *plainPixel="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {return value;}\n";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vs[2]={},*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};SDL_GPUTexture *target=NULL;
		~Resources() {
			if(target) SDL_ReleaseGPUTexture(device,target);
			for(unsigned i=0;i<2;++i) {
				if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vs[i]) SDL_ReleaseGPUShader(device,vs[i]);if(ps[i]) SDL_ReleaseGPUShader(device,ps[i]);
			}
		}
	} resources;
	resources.device=frame->Device();
	for(unsigned water=0;water<2;++water) {
		std::vector<unsigned char> spirv;std::string error;
		bool compiled=SDL3_Compile_HLSL_To_SPIRV(water?waterVertex:plainVertex,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water uniform VS: %s\n",error.c_str());return;}
		unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,true,samplers,buffers);
		CHECK(samplers==0 && buffers==(water?2u:1u));
		resources.vs[water]=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs[water]);
		compiled=SDL3_Compile_HLSL_To_SPIRV(water?waterPixel:std::string(plainPixel),false,spirv,error);CHECK(compiled);
		if(!compiled) {printf("water uniform PS: %s\n",error.c_str());return;}
		resources.ps[water]=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps[water]);
		if(!resources.vs[water] || !resources.ps[water]) return;
		SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
		SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs[water];info.fragment_shader=resources.ps[water];
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		info.target_info.has_depth_stencil_target=true;info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
		resources.pipelines[water]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipelines[water]);
		if(!resources.pipelines[water]) return;
	}
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=width;texture.height=rows;texture.layer_count_or_depth=texture.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(resources.target);if(!resources.target) return;
	CHECK(frame->Flush());frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
	const bool waterRows[rows]={true,false,true,true,false,true};
	const unsigned state[rows]={0,0,1,0,0,1};
	const float expectedWater[2][4]={{.25f,2.5f,79.25f,1.0f},{.75f,9.125f,-3.75f,0.0f}};
	for(unsigned row=0;row<rows;++row) {
		if(row==3) {
			// Engine rectangle-clear pipelines target its native UNORM scene format.
			frame->Set_Target(frame->Back_Buffer_Target());
			frame->Clear_Rect(0,0,1,1,true,false,false,0xFF123456u,1,0);
			frame->Set_Target({resources.target,frame->Depth_For(width,rows),width,rows});
		}
		SdlRecordedDraw draw={};draw.Pipeline=resources.pipelines[waterRows[row]?1:0];draw.Count=3;
		memset(frame->Stage(16,draw.VertexOffset),0,16);
		float fixed[4]={waterRows[row]?expectedWater[state[row]][0]:2.0f,0,0,0};
		draw.VertexConstants=frame->Constants(0,fixed,sizeof(fixed));draw.VertexConstantsSize=sizeof(fixed);
		if(waterRows[row]) {
			SdlWaterPixelConstants water={};water.WaterParameters[0]=expectedWater[state[row]][1];
			water.HullData[79][3]=expectedWater[state[row]][2];water.FftParameters[0]=1;water.FftParameters[1]=expectedWater[state[row]][3];
			draw.PixelConstants=frame->Constants(1,&water,sizeof(water));draw.PixelConstantsSize=sizeof(water);
			draw.VertexWaterConstants=draw.PixelConstants;draw.VertexWaterConstantsSize=draw.PixelConstantsSize;
			// Poison the source after recording: deferred execution must read its copied snapshot.
			memset(&water,0xDA,sizeof(water));
		} else CHECK(draw.VertexWaterConstantsSize==0 && draw.PixelConstantsSize==0);
		draw.Viewport[1]=float(row);draw.Viewport[2]=width;draw.Viewport[3]=1;draw.Viewport[5]=1;
		draw.Scissor[1]=row;draw.Scissor[2]=width;draw.Scissor[3]=1;frame->Record_Draw(draw);
	}
	const auto values=read_float_targets(frame,&resources.target,1,width,rows);
	frame->Set_Target(frame->Back_Buffer_Target());
	bool matched=values.size()==width*rows*4;
	if(matched) for(unsigned row=0;row<rows;++row) for(unsigned x=0;x<width;++x) for(unsigned c=0;c<4;++c) {
		const float expected=waterRows[row]?expectedWater[state[row]][c]:float(9-c)*2.0f;
		matched &= std::isfinite(values[(row*width+x)*4+c]) && std::abs(values[(row*width+x)*4+c]-expected)<0.00001f;
	}
	CHECK(matched);
	printf("sdl_gpu_frame_selfcheck: GPU water VS slot1: %u queued draws, shared pixel snapshot, final hull/tail, water changes, intervening non-water and rectangle clear checked\n",rows);
}

static void check_gpu_vessel_wave_model(SdlGpuFrame *frame)
{
	using WaterHullModel::Hull;
	Hull hulls[8];
	auto rectangle=[](Hull &h,float velocity) {
		h.data[0][2]=7;h.data[0][3]=4;h.data[1][0]=1;h.data[1][2]=20;h.data[1][3]=6;
		h.data[2][0]=velocity;h.data[2][2]=.23f;h.data[2][3]=sqrtf(436.0f);
		h.data[3][0]=-20;h.data[3][2]=20;
		const float points[8]={-20,-6,20,-6,20,6,-20,6};memcpy(h.data[4],points,sizeof(points));
	};
	rectangle(hulls[0],0);rectangle(hulls[1],12);rectangle(hulls[2],-12);rectangle(hulls[4],.3f);
	hulls[3]=hulls[1];const float cosine=cosf(.63f),sine=sinf(.63f);
	hulls[3].data[0][0]=137;hulls[3].data[0][1]=-83;hulls[3].data[1][0]=cosine;hulls[3].data[1][1]=sine;
	hulls[3].data[2][0]=12*cosine;hulls[3].data[2][1]=12*sine;
	hulls[5]=hulls[1];hulls[5].data[0][3]=2;hulls[7]=hulls[1];
	rectangle(hulls[6],.00001f);hulls[6].data[2][1]=12;
	struct Fixture { float point[4],other[4]; };
	std::vector<Fixture> fixtures;
	auto add=[&](float x,float y,unsigned h,float time=4,float level=7) {
		Fixture f={{x,y,level,time},{float(h),0,0,0}};fixtures.push_back(f);return unsigned(fixtures.size()-1);
	};
	const unsigned edge=add(0,8,0),corner=add(23,10,0),inside=add(0,0,0),far=add(90,70,0);
	const unsigned wrong=add(0,8,0,4,8),fade=add(0,34,0),leftCorner=add(19.999f,10,0),rightCorner=add(20.001f,10,0);
	const unsigned wake=add(-40,9,1),bow=add(28,0,1),stationary=add(-40,9,0),reverse=add(40,-9,2);
	const unsigned rotated=add(137-40*cosine-9*sine,-83-40*sine+9*cosine,3);
	const unsigned slow=add(-40,9,4),bad=add(-40,9,5),last=add(-40,9,7);
	const unsigned repeat=add(-40,9,1),churn=add(-40,0,1),later=add(-40,0,1,5),wakeEnd=add(-140,0,1),bowFar=add(100,0,1);
	// Keep the broadside endpoint control inside the corner region: x==20 is
	// an edge/corner derivative crease, so a central difference is not valid there.
	const unsigned sideBow=add(0,8,6),sideSwitch=add(20.1f,8,6),sideStern=add(0,-20,6),sideWing=add(9,-20,6);
	add(-24,-10,0);add(-15,-7,0);add(8,19,0);add(137+23*cosine-10*sine,-83+23*sine+10*cosine,3);
	const unsigned columns=10,count=unsigned(fixtures.size());
	std::string ps="cbuffer FixturesBlock : register(b0,space3) { float4 HullData[80]; float4 Fixtures[";
	ps+=std::to_string(count*2)+"]; };\nstatic const float4 HullParameters=float4(0,0,0,0);\n";
	ps+=WaterVesselWaves::Source();
	ps+=R"HLSL(
float3 incident(float2 p) {return float3(2.0*sin(.07*p.x+.025*p.y),1.5*cos(.035*p.x-.045*p.y),3.0*sin(.055*p.x-.08*p.y));}
float3 incidentX(float2 p) {return float3(.14*cos(.07*p.x+.025*p.y),-.0525*sin(.035*p.x-.045*p.y),.165*cos(.055*p.x-.08*p.y));}
float3 incidentY(float2 p) {return float3(.05*cos(.07*p.x+.025*p.y),.0675*sin(.035*p.x-.045*p.y),-.24*cos(.055*p.x-.08*p.y));}
float4 main(float4 pixel:SV_Position):SV_Target0 {
    uint row=(uint)pixel.y,col=(uint)pixel.x;float4 f=Fixtures[row*2];int h=(int)Fixtures[row*2+1].x;
    float2 p=f.xy;
    if(col>=6) p+=col==6?float2(.03125,0):col==7?float2(-.03125,0):col==8?float2(0,.03125):float2(0,-.03125);
    VesselReflection v=vesselReflection(p,f.z,h);float3 d=incident(v.samplePosition);
    float3 value=vesselReflectedValue(d,v),generated=vesselGeneratedWave(p,f.z,f.w,h);
    if(col==0 || col>=6) return float4(value,generated.x);
    if(col==1) return float4(vesselReflectedDerivative(d,incidentX(v.samplePosition),incidentY(v.samplePosition),float2(1,0),v),0);
    if(col==2) return float4(vesselReflectedDerivative(d,incidentX(v.samplePosition),incidentY(v.samplePosition),float2(0,1),v),0);
    if(col==3) return float4(v.samplePosition,v.weight,v.corner);
    if(col==4) return float4(v.weightGradient,v.normal);
    return float4(generated,0);
}

)HLSL";
	const char *vs="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::vector<unsigned char> spirv;std::string error;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);if(!compiled) return;
	SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);CHECK(vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled) {printf("vessel wave model: %s\n",error.c_str());if(vertex) SDL_ReleaseGPUShader(frame->Device(),vertex);return;}
	SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=vertex;info.fragment_shader=pixel;
	info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
	SDL_GPUGraphicsPipeline *pipeline=vertex&&pixel?SDL_CreateGPUGraphicsPipeline(frame->Device(),&info):NULL;CHECK(pipeline);
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=colour.format;
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=columns;texture.height=count;texture.layer_count_or_depth=texture.num_levels=1;
	SDL_GPUTexture *target=SDL_CreateGPUTexture(frame->Device(),&texture);CHECK(target);
	if(pipeline && target) {
		std::vector<float> uniforms(80*4+count*8);memcpy(uniforms.data(),hulls,sizeof(hulls));
		memcpy(uniforms.data()+80*4,fixtures.data(),fixtures.size()*sizeof(Fixture));
		auto render=[&]() {
			CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(commands);
			SDL_GPUColorTargetInfo attachment={};attachment.texture=target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
			SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);
			SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_PushGPUFragmentUniformData(commands,0,uniforms.data(),uniforms.size()*sizeof(float));
			SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
			return read_float_targets(frame,&target,1,columns,count);
		};
		const auto values=render();
		auto value=[&](unsigned row,unsigned col,unsigned component){return double(values[(row*columns+col)*4+component]);};
		// Independent CPU geometry: clamp to a rectangle, then mirror about that closest point.
		// This does not call the production closest-edge, mirror-Jacobian, or derivative helpers.
		auto reflected=[&](unsigned row,double x,double y,double result[3]) {
			const Fixture &f=fixtures[row];const Hull &h=hulls[unsigned(f.other[0])];
			result[0]=result[1]=result[2]=0;
			if(h.data[0][3]<3 || std::abs(f.point[2]-h.data[0][2])>.1) return;
			const double ax=h.data[1][0],ay=h.data[1][1],dx=x-h.data[0][0],dy=y-h.data[0][1];
			const double lx=dx*ax+dy*ay,ly=-dx*ay+dy*ax;
			if(std::abs(lx)<=20 && std::abs(ly)<=6) return;
			const double qx=(std::max)(-20.0,(std::min)(20.0,lx)),qy=(std::max)(-6.0,(std::min)(6.0,ly));
			const double distance=std::hypot(lx-qx,ly-qy);if(distance>=28 || distance<.0001) return;
			const double nx=(ax*(lx-qx)-ay*(ly-qy))/distance,ny=(ay*(lx-qx)+ax*(ly-qy))/distance;
			const double mx=x-2*distance*nx,my=y-2*distance*ny,t=distance/28,fade=1-t*t*(3-2*t),weight=.68*fade*fade;
			const double d[3]={2*std::sin(.07*mx+.025*my),1.5*std::cos(.035*mx-.045*my),3*std::sin(.055*mx-.08*my)};
			const double normal=nx*d[0]+ny*d[1];result[0]=(d[0]-2*nx*normal)*weight;result[1]=(d[1]-2*ny*normal)*weight;result[2]=d[2]*weight;
		};
		double maxValue=0,maxDerivative=0,maxPacketDerivative=0;
		for(unsigned row=0;row<count;++row) {
			const double x=fixtures[row].point[0],y=fixtures[row].point[1];double expected[3];reflected(row,x,y,expected);
			for(unsigned c=0;c<3;++c) {maxValue=(std::max)(maxValue,std::abs(value(row,0,c)-expected[c]));CHECK(std::abs(value(row,0,c)-expected[c])<.00002);}
			for(unsigned axis=0;axis<2;++axis) {
				double plus[3],minus[3];const double step=.0001;
				reflected(row,x+(axis==0?step:0),y+(axis==1?step:0),plus);
				reflected(row,x-(axis==0?step:0),y-(axis==1?step:0),minus);
				for(unsigned c=0;c<3;++c) {
					const double difference=std::abs(value(row,1+axis,c)-(plus[c]-minus[c])/(2*step));
					maxDerivative=(std::max)(maxDerivative,difference);CHECK(difference<.0002);
				}
				const double numeric=(value(row,6+axis*2,3)-value(row,7+axis*2,3))/.0625;
				const double difference=std::abs(numeric-value(row,5,axis+1));
				maxPacketDerivative=(std::max)(maxPacketDerivative,difference);CHECK(difference<.0003);
			}
			for(unsigned col=0;col<columns;++col) for(unsigned c=0;c<4;++c) CHECK(std::isfinite(value(row,col,c)));
		}
		CHECK(std::abs(value(edge,3,0))<.00001 && std::abs(value(edge,3,1)-4)<.00001 && value(edge,3,3)==0);
		CHECK(std::abs(value(corner,3,0)-17)<.00001 && std::abs(value(corner,3,1)-2)<.00001 && value(corner,3,3)==1);
		for(unsigned row:{inside,far,wrong,fade}) CHECK(value(row,3,2)==0);
		for(unsigned c=0;c<3;++c) CHECK(std::abs(value(leftCorner,0,c)-value(rightCorner,0,c))<.001);
		CHECK(value(wake,5,0)>.05 && value(bow,5,0)>.5);
		for(unsigned row:{stationary,slow,bad,wakeEnd,bowFar}) CHECK(std::abs(value(row,5,0))<.00001);
		CHECK(std::abs(value(reverse,5,0)-value(wake,5,0))<.00002);
		CHECK(std::abs(value(reverse,5,1)+value(wake,5,1))<.00002 && std::abs(value(reverse,5,2)+value(wake,5,2))<.00002);
		CHECK(std::abs(value(rotated,5,0)-value(wake,5,0))<.00002);
		CHECK(std::abs(value(rotated,5,1)-(value(wake,5,1)*cosine-value(wake,5,2)*sine))<.00002);
		CHECK(std::abs(value(rotated,5,2)-(value(wake,5,1)*sine+value(wake,5,2)*cosine))<.00002);
		for(unsigned c=0;c<3;++c) CHECK(value(repeat,5,c)==value(wake,5,c) && value(last,5,c)==value(wake,5,c));
		// The divergent crests are steady in the boat frame. Animate the centre churn instead:
		// at along=20/across=0 both spatial envelopes are one, so its expected temporal delta
		// is independently just the scalar sinusoid times the measured-speed gate.
		const double speedT=(12.0-.3)/(14.0-.3),speedGate=speedT*speedT*(3-2*speedT);
		const double phase=6.0+double(hulls[1].data[2][2])*6.2831853;
		const double expectedDelta=.24*speedGate*(std::sin(phase-5*1.15)-std::sin(phase-4*1.15));
		CHECK(expectedDelta>.15 && std::abs((value(later,5,0)-value(churn,5,0))-expectedDelta)<.00002);
		// The travel direction crosses sideways by only 0.00000167 radians. Heights and slopes
		// must not jump from one end of a forty-unit hull to the other. Check active bow/churn and
		// both old endpoint regions; the armed endpoint-forcing control proves this catches a snap.
		hulls[6].data[2][0]=-.00001f;memcpy(uniforms.data()+6*40,&hulls[6],sizeof(Hull));
		const auto oppositeSide=render();double maxSideChange=0;
		CHECK(value(sideBow,5,0)>.5);
		for(unsigned row:{sideBow,sideSwitch,sideStern,sideWing}) for(unsigned c=0;c<3;++c) {
			const double change=std::abs(value(row,5,c)-oppositeSide[(row*columns+5)*4+c]);
			maxSideChange=(std::max)(maxSideChange,change);CHECK(change<.0003);
		}
		hulls[6].data[2][0]=.00001f;hulls[6].data[3][0]=hulls[6].data[3][2]=20;
		memcpy(uniforms.data()+6*40,&hulls[6],sizeof(Hull));const auto forcedRight=render();
		hulls[6].data[2][0]=-.00001f;hulls[6].data[3][0]=hulls[6].data[3][2]=-20;
		memcpy(uniforms.data()+6*40,&hulls[6],sizeof(Hull));const auto forcedLeft=render();
		CHECK(std::abs(forcedRight[(sideSwitch*columns+5)*4]-forcedLeft[(sideSwitch*columns+5)*4])>1.5);
		printf("sdl_gpu_frame_selfcheck: GPU physical vessel waves: %u fixtures, independent reflection value %.8f/gradient %.8f, packet finite-difference %.8f, sideways height/slope change %.8f; stationary/corners/reverse/rotation/level/speed/pause/support/final hull and armed endpoint snap checked\n",count,maxValue,maxDerivative,maxPacketDerivative,maxSideChange);
	}
	if(target) SDL_ReleaseGPUTexture(frame->Device(),target);if(pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	if(vertex) SDL_ReleaseGPUShader(frame->Device(),vertex);if(pixel) SDL_ReleaseGPUShader(frame->Device(),pixel);
}

static void check_gpu_vessel_hull_damping(SdlGpuFrame *frame)
{
	// Independent rectangle distances and finite differences check the shared VS/PS helper.
	// The reference never calls the production closest-edge or damping-gradient expressions.
	SdlWaterPixelConstants water={};water.HullParameters[0]=8;
	struct Rectangle {double x,y,angle,level;unsigned slot;};
	const Rectangle rectangles[]={{0,0,0,7,0},{80,23,.63,7,4},{91,27,.63,7,5},{-70,-40,-.41,7,7}};
	for(const auto &r:rectangles) {
		WaterHullModel::Hull hull;hull.data[0][0]=float(r.x);hull.data[0][1]=float(r.y);hull.data[0][2]=float(r.level);hull.data[0][3]=4;
		hull.data[1][0]=float(cos(r.angle));hull.data[1][1]=float(sin(r.angle));hull.data[1][2]=20;hull.data[1][3]=6;
		const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(hull.data[4],outline,sizeof(outline));
		memcpy(water.HullData+r.slot*10,&hull,sizeof(hull));
	}
	memcpy(water.HullData+10,water.HullData,sizeof(WaterHullModel::Hull));water.HullData[10][3]=2; // invalid contour
	memcpy(water.HullData+20,water.HullData,sizeof(WaterHullModel::Hull));water.HullData[20][2]=11; // another level
	struct Fixture {float point[4],freeDamping[4],offshore[4];};
	std::vector<Fixture> fixtures;
	auto add=[&](float x,float y,float level,float freeValue,float offshoreValue,float fx=0,float fy=0,float ox=0,float oy=0) {
		Fixture f={{x,y,level,0},{freeValue,fx,fy,0},{offshoreValue,ox,oy,0}};fixtures.push_back(f);
	};
	for(float y:{0.0f,5.0f,6.0f,6.01f,8.0f,10.5f,14.99f,15.0f,15.01f,40.0f}) add(0,y,7,1,1);
	for(float x:{19.99f,20.0f,20.01f,22.0f,25.0f}) add(x,8,7,1,1);
	const double localPoints[][2]={{0,8},{23,10},{-23,-10}};
	for(const auto &r:rectangles) for(const auto &p:localPoints)
		add(float(r.x+cos(r.angle)*p[0]-sin(r.angle)*p[1]),float(r.y+sin(r.angle)*p[0]+cos(r.angle)*p[1]),7,1,1);
	add(0,8,8,1,1);add(0,8,7,1,0);add(0,8,7,0,1);add(0,8,7,.9f,.6f,.003f,-.002f,.004f,.006f);
	add(0,12,7,.1f,.6f,.003f,-.002f,.004f,.006f); // floor/terrain wins the minimum
	add(0,8,7,WaterFftGeometry::SafeDamping(1,2.25f),1);
	add(0,14,7,WaterFftGeometry::SafeDamping(1,2.25f),1);
	add(23,10,7,.95f,.8f,-.004f,.001f,-.003f,.004f);
	add(80,23,7,.7f,.4f,.003f,-.002f,.004f,.006f); // inside the rotated hull
	const unsigned rows=unsigned(fixtures.size()),columns=3;
	std::string common=WaterFftSampling::UniformSource("WaterBlock",0)+WaterVesselWaves::Source()+
		"cbuffer FixtureBlock:register(b1) {float4 Point;float4 FreeDamping;float4 Offshore;};\n";
	std::string vs=common+R"HLSL(
struct Output {float4 Position:SV_Position;[[vk::location(0)]] float3 Value:TEXCOORD0;};
Output main(uint id:SV_VertexID) {Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
o.Value=vesselApplyHullDamping(Point.xy,Point.z,FreeDamping.xyz,Offshore.xyz);return o;}
)HLSL";
	std::string ps=common+R"HLSL(
float4 main(float4 pixel:SV_Position,[[vk::location(0)]] float3 value:TEXCOORD0):SV_Target0 {
uint col=(uint)pixel.x;
if(col==0) return float4(vesselHullDamping(Point.xy,Point.z),1);
if(col==1) return float4(vesselApplyHullDamping(Point.xy,Point.z,FreeDamping.xyz,Offshore.xyz),1);
return float4(value,1);}
)HLSL";
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUTexture *target=NULL;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		~Resources(){if(target)SDL_ReleaseGPUTexture(device,target);if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
			if(vertex)SDL_ReleaseGPUShader(device,vertex);if(pixel)SDL_ReleaseGPUShader(device,pixel);}
	} resources;
	resources.device=frame->Device();std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Shader_Retarget(vs,true));CHECK(SDL3_Shader_Retarget(ps,false));
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull damping VS: %s\n",error.c_str());return;}
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull damping PS: %s\n",error.c_str());return;}
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=columns;ti.height=rows;ti.layer_count_or_depth=ti.num_levels=1;
	resources.target=SDL_CreateGPUTexture(resources.device,&ti);CHECK(resources.target);if(!resources.target)return;
	CHECK(frame->Flush());SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
	SDL_GPUColorTargetInfo target={};target.texture=resources.target;target.load_op=SDL_GPU_LOADOP_DONT_CARE;target.store_op=SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
	SDL_PushGPUVertexUniformData(commands,0,&water,sizeof(water));SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
	for(unsigned row=0;row<rows;++row) {
		SDL_GPUViewport viewport={0,float(row),float(columns),1,0,1};SDL_SetGPUViewport(pass,&viewport);
		SDL_PushGPUVertexUniformData(commands,1,&fixtures[row],sizeof(Fixture));SDL_PushGPUFragmentUniformData(commands,1,&fixtures[row],sizeof(Fixture));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);
	}
	SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
	const auto values=read_float_targets(frame,&resources.target,1,columns,rows);CHECK(values.size()==rows*columns*4);if(values.size()!=rows*columns*4)return;
	auto raw=[&](double x,double y,double level) {
		double result=1;
		for(const auto &r:rectangles) {
			if(std::abs(level-r.level)>.1)continue;
			const double ax=water.HullData[r.slot*10+1][0],ay=water.HullData[r.slot*10+1][1];
			const double localX=(x-r.x)*ax+(y-r.y)*ay,localY=-(x-r.x)*ay+(y-r.y)*ax;
			const double distance=std::hypot((std::max)(0.0,std::abs(localX)-20),(std::max)(0.0,std::abs(localY)-6));
			const double t=(std::min)(1.0,distance/9);result=(std::min)(result,t*t*(3-2*t));
		}
		return result;
	};
	double maxValue=0,maxGradient=0,maxStage=0;unsigned hullWins=0,freeWins=0,gradientControls=0;
	for(unsigned row=0;row<rows;++row) {
		const Fixture &f=fixtures[row];const double x=f.point[0],y=f.point[1],level=f.point[2];
		auto composed=[&](double px,double py) {
			const double free=f.freeDamping[0]+f.freeDamping[1]*(px-x)+f.freeDamping[2]*(py-y);
			const double offshore=f.offshore[0]+f.offshore[1]*(px-x)+f.offshore[2]*(py-y);
			if(free<=0 || offshore<=0)return free;
			return (std::min)(free,1+(raw(px,py,level)-1)*offshore);
		};
		for(unsigned col=0;col<2;++col) {
			const double expected=col?composed(x,y):raw(x,y,level);
			const double difference=std::abs(values[(row*columns+col)*4]-expected);
			maxValue=(std::max)(maxValue,difference);CHECK(difference<.000004);
			for(unsigned axis=0;axis<2;++axis) {
				const double step=.0001,px=x+(axis==0?step:0),py=y+(axis==1?step:0),mx=x-(axis==0?step:0),my=y-(axis==1?step:0);
				const double numeric=(col?composed(px,py)-composed(mx,my):raw(px,py,level)-raw(mx,my,level))/(2*step);
				const double error=std::abs(values[(row*columns+col)*4+1+axis]-numeric);
				maxGradient=(std::max)(maxGradient,error);CHECK(error<.00004);
			}
		}
		for(unsigned c=0;c<3;++c) {
			const double error=std::abs(values[(row*columns+1)*4+c]-values[(row*columns+2)*4+c]);maxStage=(std::max)(maxStage,error);CHECK(error<.000001);
		}
		CHECK(values[(row*columns+1)*4]>=0 && values[(row*columns+1)*4]<=f.freeDamping[0]+.000001f);
		if(f.offshore[0]==0) for(unsigned c=0;c<3;++c) CHECK(values[(row*columns+1)*4+c]==f.freeDamping[c]);
		if(composed(x,y)<f.freeDamping[0]-.01)++hullWins;else if(f.freeDamping[0]>0 && f.freeDamping[0]<1)++freeWins;
		const double missingOffshoreX=values[row*columns*4+1]*f.offshore[0];
		if(std::abs(values[(row*columns+1)*4+1]-missingOffshoreX)>.001)++gradientControls;
		const double shallow=(std::min)(double(WaterFftGeometry::SafeDamping(1,2.25f)),composed(x,y));
		CHECK(-WaterFftGeometry::MaxVerticalDisplacement*shallow>=-2.100001);
	}
	for(float value:values)CHECK(std::isfinite(value));CHECK(hullWins>8 && freeWins>=2 && gradientControls>=2);
	printf("sdl_gpu_frame_selfcheck: GPU analytic hull damping: %u fixtures, independent value %.8f/gradient %.8f/VS-PS %.8f; rotated/corner/final hull, level, min winner, offshore product, zero/floor bounds checked\n",rows,maxValue,maxGradient,maxStage);
}

static void check_gpu_water_hull_normal(SdlGpuFrame *frame)
{
	// A constant spectral height has zero spectral slope. Its production normal is therefore
	// independently the derivative of the analytic hull envelope. Changing a coarse quad's
	// diagonal must not change that normal; the old interpolated-envelope control must change it.
	const unsigned width=96,height=32;
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUShader *vertex=NULL,*pixel=NULL;SDL_GPUGraphicsPipeline *pipeline=NULL;
		SDL_GPUTexture *target=NULL,*white=NULL,*zero=NULL,*displacement=NULL;SDL_GPUSampler *sampler=NULL;
		~Resources(){if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);if(vertex)SDL_ReleaseGPUShader(device,vertex);
			if(pixel)SDL_ReleaseGPUShader(device,pixel);for(auto *t:{target,white,zero,displacement})if(t)SDL_ReleaseGPUTexture(device,t);
			if(sampler)SDL_ReleaseGPUSampler(device,sampler);}
	} resources;
	resources.device=frame->Device();
	const char *previous=getenv("ZH_WATER_DEBUG");const bool hadDebug=previous!=NULL;const std::string oldDebug=previous?previous:"";
	CHECK(setenv("ZH_WATER_DEBUG","normal",1)==0);
	PixelPipelineDescription pd={};std::string ps;const bool generated=EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pd,ps,COMBINER_SHADER_TARGET_SDL3_GPU);
	if(hadDebug)CHECK(setenv("ZH_WATER_DEBUG",oldDebug.c_str(),1)==0);else CHECK(unsetenv("ZH_WATER_DEBUG")==0);
	CHECK(generated);if(!generated)return;
	VertexPipelineDescription vd={};vd.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_DIFFUSE|FF_FVF_TEX2;vd.StageCount=4;
	vd.Stages[0].TextureCoordinateIndex=0;vd.Stages[1].TextureCoordinateIndex=1;
	std::string vs;CHECK(VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,vs));CHECK(WaterFftSampling::WrapVertex(vs));
	CHECK(SDL3_Shader_Retarget(vs,true));
	const std::string entry="Output main(Input input)";const size_t at=vs.find(entry);CHECK(at!=std::string::npos);if(at==std::string::npos)return;
	vs.replace(at,entry.size(),"Output hullFixtureWaterVertex(Input input)");
	vs+=R"HLSL(
cbuffer Fixture:register(b2,space1) {float4 FixtureControls;};
Output main(uint id:SV_VertexID) {
    // Two diagonals through the same asymmetric quad. A corner and its nearby straight hull
    // edge both cross the domain, so interpolating the old envelope is measurably non-planar.
    uint c;
    if(FixtureControls.x<0.5) c=id==0?0:id==1?1:id==2?2:id==3?0:id==4?2:3;
    else c=id==0?0:id==1?1:id==2?3:id==3?1:id==4?2:3;
    float2 p=float2(c==0||c==3?-24.0:28.0,c<2?4.0:17.0);
    Input input=(Input)0;input.Position=float3(p,0.5);input.Diffuse=float4(0,0,0,1);
    input.Normal=float3(FixtureControls.z,FixtureControls.w,1);
    if(FixtureControls.y>0.5) {
        input.Normal.y=min(input.Normal.y,vesselHullDamping(p,0.5).x);
        input.Normal.z=0; // armed pre-fix path: transport the hull-combined vertex damping
    }
    input.TexCoord1=float2(0,0.5);
    Output o=hullFixtureWaterVertex(input);
    // Keep an exact parameter-space atlas after executing the production vertex wrapper.
    // Its displacement was checked separately; its actual Specular/TC2 transport stays intact.
    o.Position=float4((p.x-2.0)/26.0,(p.y-10.5)/6.5,0.5,1);
    return o;
}
)HLSL";
	std::string error;std::vector<unsigned char> spirv;
	bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull normal VS: %s\n",error.c_str());return;}
	resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);
	compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
	if(!compiled){printf("hull normal PS: %s\n",error.c_str());return;}
	unsigned samplers=0,uniforms=0;SDL3_Shader_Slots(spirv,false,samplers,uniforms);CHECK(samplers<=16 && uniforms==1);
	resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=resources.vertex;pi.fragment_shader=resources.pixel;
	pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pi.target_info.color_target_descriptions=&colour;pi.target_info.num_color_targets=1;
	resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pi);CHECK(resources.pipeline);if(!resources.pipeline)return;
	SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=colour.format;ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	ti.width=width;ti.height=height;ti.layer_count_or_depth=ti.num_levels=1;resources.target=SDL_CreateGPUTexture(resources.device,&ti);
	ti.width=ti.height=1;ti.usage|=SDL_GPU_TEXTUREUSAGE_SAMPLER;
	resources.white=SDL_CreateGPUTexture(resources.device,&ti);resources.zero=SDL_CreateGPUTexture(resources.device,&ti);resources.displacement=SDL_CreateGPUTexture(resources.device,&ti);
	SDL_GPUSamplerCreateInfo si={};si.min_filter=si.mag_filter=SDL_GPU_FILTER_NEAREST;resources.sampler=SDL_CreateGPUSampler(resources.device,&si);
	CHECK(resources.target && resources.white && resources.zero && resources.displacement && resources.sampler);
	if(!resources.target || !resources.white || !resources.zero || !resources.displacement || !resources.sampler)return;
	SdlVertexConstants vertex={};SdlWaterPixelConstants water={};
	for(unsigned i=0;i<4;++i) {
		vertex.WorldViewProjection[i*5]=vertex.WorldView[i*5]=vertex.NormalTransform[i*5]=1;water.WorldFromClip[i*5]=1;
		for(unsigned stage=0;stage<MAXIMUM_VERTEX_STAGES;++stage)vertex.TextureMatrix[stage][i*5]=1;
	}
	water.WorldFromClip[0]=26;water.WorldFromClip[5]=6.5f;water.WorldFromClip[12]=2;water.WorldFromClip[13]=10.5f;
	water.Viewport[2]=width;water.Viewport[3]=height;water.CameraPosition[2]=100;
	water.SunDirection[2]=1;water.SunColour[0]=water.SunColour[1]=water.SunColour[2]=1;
	water.TerrainParameters[1]=100;water.SceneParameters[1]=water.SceneParameters[2]=1;
	water.OpticalParameters[0]=3;water.OpticalParameters[1]=water.OpticalParameters[2]=1;
	water.FftParameters[0]=1;water.FftParameters[1]=0; // isolate clearance from optional reflected/generated waves
	water.HullParameters[0]=1;water.HullData[0][2]=.5f;water.HullData[0][3]=4;
	water.HullData[1][0]=1;water.HullData[1][2]=20;water.HullData[1][3]=6;
	const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(water.HullData[4],outline,sizeof(outline));
	CHECK(frame->Flush());unsigned renders=0;
	auto render=[&](unsigned diagonal,bool coarse,float weight,float freeDamping) {
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		SDL_GPUColorTargetInfo attachment={};attachment.load_op=SDL_GPU_LOADOP_CLEAR;attachment.store_op=SDL_GPU_STOREOP_STORE;
		for(unsigned i=0;i<3;++i) {
			attachment.texture=i==0?resources.white:i==1?resources.zero:resources.displacement;
			attachment.clear_color={i==0?1.0f:0.0f,i==0?1.0f:i==2?.05f:0.0f,i==0?1.0f:0.0f,i==0?1.0f:0.0f};
			SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_EndGPURenderPass(pass);
		}
		attachment.texture=resources.target;attachment.clear_color={0,0,0,0};
		SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
		SDL_GPUTextureSamplerBinding bindings[16]={};for(unsigned i=0;i<samplers;++i) {
			bindings[i].texture=i<8?resources.white:i==8?resources.displacement:resources.zero;bindings[i].sampler=resources.sampler;
		}
		SDL_BindGPUFragmentSamplers(pass,0,bindings,samplers);
		SDL_GPUTextureSamplerBinding vertexBindings[3]={{resources.displacement,resources.sampler},{resources.zero,resources.sampler},{resources.zero,resources.sampler}};
		SDL_BindGPUVertexSamplers(pass,0,vertexBindings,3);
		const float controls[4]={float(diagonal),coarse?1.0f:0.0f,weight,freeDamping};
		SDL_PushGPUVertexUniformData(commands,0,&vertex,sizeof(vertex));SDL_PushGPUVertexUniformData(commands,1,&water,sizeof(water));
		SDL_PushGPUVertexUniformData(commands,2,controls,sizeof(controls));SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
		SDL_DrawGPUPrimitives(pass,6,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));++renders;
		return read_float_targets(frame,&resources.target,1,width,height);
	};
	const auto first=render(0,false,1,1),second=render(1,false,1,1);
	const auto coarseFirst=render(0,true,1,1),coarseSecond=render(1,true,1,1);
	const auto seam=render(0,false,0,1),floor=render(1,false,1,WaterFftGeometry::SafeDamping(1,2.25f));
	CHECK(first.size()==width*height*4 && second.size()==first.size() && coarseFirst.size()==first.size() && coarseSecond.size()==first.size() && seam.size()==first.size() && floor.size()==first.size());
	if(first.size()!=width*height*4 || second.size()!=first.size() || coarseFirst.size()!=first.size() || coarseSecond.size()!=first.size() || seam.size()!=first.size() || floor.size()!=first.size())return;
	const double waveHeight=WaterFftGeometry::MaxVerticalDisplacement*std::tanh(1.0/WaterFftGeometry::MaxVerticalDisplacement);
	double maximumError=0,maximumDiagonal=0,maximumControl=0,maximumSeam=0,maximumFloor=0;unsigned controls=0,active=0;
	for(unsigned y=1;y<height-1;++y)for(unsigned x=1;x<width-1;++x) {
		const double px=-24+52*(x+.5)/width,py=17-13*(y+.5)/height;
		const double sx=px<-20?px+20:px>20?px-20:0,sy=py>6?py-6:py<-6?py+6:0,distance=std::hypot(sx,sy);
		const double t=(std::min)(1.0,distance/9),damping=t*t*(3-2*t),slope=6*t*(1-t)/9;
		const double gx=distance>0?slope*sx/distance:0,gy=distance>0?slope*sy/distance:0;
		const double normalLength=std::sqrt(1+waveHeight*waveHeight*(gx*gx+gy*gy));
		const double normal[3]={-waveHeight*gx/normalLength,-waveHeight*gy/normalLength,1/normalLength};
		const bool floorWins=WaterFftGeometry::SafeDamping(1,2.25f)<=damping;
		for(unsigned c=0;c<3;++c) {
			const unsigned offset=(y*width+x)*4+c;
			const double error=std::abs(first[offset]-(.5+.5*normal[c]));maximumError=(std::max)(maximumError,error);
			maximumDiagonal=(std::max)(maximumDiagonal,double(std::abs(first[offset]-second[offset])));
			const double control=std::abs(coarseFirst[offset]-coarseSecond[offset]);maximumControl=(std::max)(maximumControl,control);controls+=control>.005;
			maximumSeam=(std::max)(maximumSeam,std::abs(seam[offset]-(c==2?1.0:.5)));
			const double expected=floorWins?(c==2?1.0:.5):(.5+.5*normal[c]);maximumFloor=(std::max)(maximumFloor,std::abs(floor[offset]-expected));
		}
		active+=std::abs(normal[0])+std::abs(normal[1])>.08;
	}
	for(const auto *image:{&first,&second,&coarseFirst,&coarseSecond,&seam,&floor})for(float v:*image)CHECK(std::isfinite(v));
	CHECK(maximumError<.0001 && maximumDiagonal<.0001 && maximumFloor<.0001 && maximumSeam<.0001);
	CHECK(active>200 && maximumControl>.01 && controls>100);
	printf("sdl_gpu_frame_selfcheck: GPU production hull normal: %u renders, independent normal %.8f/diagonal %.8f/floor %.8f/zero-weight %.8f; armed interpolated damping diagonal error %.8f (%u components), disabled vessel-wave gate checked\n",renders,maximumError,maximumDiagonal,maximumFloor,maximumSeam,maximumControl,controls);
}

static void check_gpu_fft_vessel_composition(SdlGpuFrame *frame)
{
	// Known Fourier modes, independently packed into the six real field maps, make a numerical
	// derivative meaningful: the random full-band spectrum is too close to texture Nyquist for a
	// small finite difference of bilinear samples to approximate its stored spectral derivative.
	const unsigned side=256,rows=10,columns=4,plane=side*side*4;
	const double amplitude[3]={.13,.17,.32},phase[3]={.31,-.42,.71},chop[3]={-1.2,-1.5,-1.5};
	const int modes[3][2]={{1,1},{1,-1},{1,0}};
	const float points[rows][4]={{0,8,7,0},{23,10,7,0},{-24,-10,7,0},{12,18,7,0},{28,0,7,0},
		{-40,9,7,0},{-60,-15,7,0},{80,65,7,0},{0,0,7,0},{8,-17,7,0}};
	std::vector<float> fields(6*plane);
	for(unsigned c=0;c<3;++c) for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
		const double kx=2*WaterFftSpectrum::Pi*modes[c][0]/WaterFftSpectrum::Sizes[c];
		const double ky=2*WaterFftSpectrum::Pi*modes[c][1]/WaterFftSpectrum::Sizes[c],k=std::hypot(kx,ky);
		const double angle=2*WaterFftSpectrum::Pi*(modes[c][0]*(x+.5)+modes[c][1]*(y+.5))/side+phase[c];
		const double a=amplitude[c]*std::sin(angle),b=amplitude[c]*std::cos(angle);
		float *d=&fields[c*2*plane+(y*side+x)*4],*s=d+plane;
		d[0]=float(a*kx/k);d[1]=float(b);d[2]=float(a*ky/k);d[3]=float(b*kx*ky/k);
		s[0]=float(-a*kx);s[1]=float(-a*ky);s[2]=float(b*kx*kx/k);s[3]=float(b*ky*ky/k);
	}
	struct Resources {
		SDL_GPUDevice *device;SDL_GPUTexture *maps[6]={},*targets[2]={};SDL_GPUSampler *sampler=NULL;
		SDL_GPUShader *vs[2]={},*ps[2]={};SDL_GPUGraphicsPipeline *pipelines[2]={};
		~Resources() {
			for(auto *p:maps) if(p) SDL_ReleaseGPUTexture(device,p);
			for(auto *p:targets) if(p) SDL_ReleaseGPUTexture(device,p);
			if(sampler) SDL_ReleaseGPUSampler(device,sampler);
			for(unsigned i=0;i<2;++i) {if(pipelines[i]) SDL_ReleaseGPUGraphicsPipeline(device,pipelines[i]);
				if(vs[i]) SDL_ReleaseGPUShader(device,vs[i]);if(ps[i]) SDL_ReleaseGPUShader(device,ps[i]);}
		}
	} resources;
	resources.device=frame->Device();
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;texture.width=texture.height=side;texture.layer_count_or_depth=texture.num_levels=1;
	for(auto &map:resources.maps) {map=SDL_CreateGPUTexture(resources.device,&texture);CHECK(map);if(!map) return;}
	texture.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=columns;texture.height=rows;
	for(auto &target:resources.targets) {target=SDL_CreateGPUTexture(resources.device,&texture);CHECK(target);if(!target) return;}
	SDL_GPUSamplerCreateInfo sampler={};sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
	sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	resources.sampler=SDL_CreateGPUSampler(resources.device,&sampler);CHECK(resources.sampler);if(!resources.sampler) return;
	auto upload=[&]() {
		SDL_GPUTransferBufferCreateInfo ti={};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=fields.size()*sizeof(float);
		SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(resources.device,&ti);CHECK(transfer);if(!transfer) return false;
		void *mapped=SDL_MapGPUTransferBuffer(resources.device,transfer,false);CHECK(mapped);
		if(!mapped) {SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
		memcpy(mapped,fields.data(),ti.size);SDL_UnmapGPUTransferBuffer(resources.device,transfer);
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		if(!commands) {SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return false;}
		SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);CHECK(copy);
		for(unsigned i=0;i<6;++i) {
			SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;source.offset=i*plane*sizeof(float);source.pixels_per_row=source.rows_per_layer=side;
			SDL_GPUTextureRegion dest={};dest.texture=resources.maps[i];dest.w=dest.h=side;dest.d=1;
			SDL_UploadToGPUTexture(copy,&source,&dest,false);
		}
		SDL_EndGPUCopyPass(copy);const bool submitted=SDL_SubmitGPUCommandBuffer(commands);CHECK(submitted);
		SDL_ReleaseGPUTransferBuffer(resources.device,transfer);return submitted;
	};
	CHECK(frame->Flush());if(!upload()) return;
	const std::string basicVS="float4 main(uint id:SV_VertexID):SV_Position {return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
	std::string sampleVS=WaterFftSampling::UniformSource("WaterBlock",0)+WaterFftSampling::VertexSource()+R"HLSL(
cbuffer PointBlock:register(b1) {float4 PointData;};
struct Output {float4 Position:SV_Position;[[vk::location(0)]] float4 Value:TEXCOORD0;};
Output main(uint id:SV_VertexID) {Output o;o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
o.Value=float4(fftVertexDisplacement(PointData.xy,PointData.z),1);return o;}
)HLSL";
	std::string samplePS="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {return value;}";
	std::string surfacePS=WaterFftSampling::UniformSource("WaterBlock",0);
	for(unsigned i=0;i<6;++i) surfacePS+="Texture2D Texture"+std::to_string(8+i)+":register(t"+std::to_string(i)+");SamplerState Sampler"+std::to_string(8+i)+":register(s"+std::to_string(i)+");\n";
	surfacePS+="cbuffer PointBlock:register(b1) {float4 Points["+std::to_string(rows)+"];};\n";
	surfacePS+=WaterFftSampling::PixelSource();
	surfacePS+=R"HLSL(
float4 main(float4 pixel:SV_Position):SV_Target0 {uint col=(uint)pixel.x;float4 p=Points[(uint)pixel.y];
FftSurface s=fftSurface(p.xy,p.z);
if(col==0) return float4(s.displacement,s.compression);
if(col==1) return float4(s.derivativeX,s.vesselHeight);
if(col==2) return float4(s.derivativeY,s.vesselCrest);
return float4(normalize(cross(float3(1,0,0)+s.derivativeX,float3(0,1,0)+s.derivativeY)),s.incidentCompression);}
)HLSL";
	for(unsigned stage=0;stage<2;++stage) {
		std::string vs=stage?sampleVS:basicVS,ps=stage?samplePS:surfacePS,error;std::vector<unsigned char> spirv;
		CHECK(SDL3_Shader_Retarget(vs,true));CHECK(SDL3_Shader_Retarget(ps,false));
		bool compiled=SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error);CHECK(compiled);
		if(!compiled) {printf("FFT vessel VS: %s\n",error.c_str());return;}
		resources.vs[stage]=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vs[stage]);
		compiled=SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error);CHECK(compiled);
		if(!compiled) {printf("FFT vessel PS: %s\n",error.c_str());return;}
		resources.ps[stage]=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.ps[stage]);
		SDL_GPUColorTargetDescription colour={};colour.format=texture.format;
		SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=resources.vs[stage];info.fragment_shader=resources.ps[stage];
		info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.target_info.color_target_descriptions=&colour;info.target_info.num_color_targets=1;
		resources.pipelines[stage]=SDL_CreateGPUGraphicsPipeline(resources.device,&info);CHECK(resources.pipelines[stage]);if(!resources.pipelines[stage]) return;
	}
	SdlWaterPixelConstants water={};water.FftParameters[0]=water.FftParameters[1]=1;water.WaterParameters[0]=4;
	WaterHullModel::Hull hull;hull.data[0][2]=7;hull.data[0][3]=4;hull.data[1][0]=1;hull.data[1][2]=20;hull.data[1][3]=6;
	hull.data[2][2]=.23f;hull.data[2][3]=sqrtf(436.0f);hull.data[3][0]=-20;hull.data[3][2]=20;
	const float outline[8]={-20,-6,20,-6,20,6,-20,6};memcpy(hull.data[4],outline,sizeof(outline));
	// Scalar reference only: normals come from independent double-precision finite differences,
	// not from copying the production derivative/Jacobian expressions.
	auto smooth=[](double a,double b,double x) {const double t=(std::max)(0.0,(std::min)(1.0,(x-a)/(b-a)));return t*t*(3-2*t);};
	auto packet=[&](double x,double y) {
		const double along=-x-20,across=y,width=2.1+along*.353553,strip=3.08,length=50.4;
		auto gaussian=[](double distance,double w) {return std::exp(-distance*distance/(w*w));};
		const double arms=gaussian(across-width,strip)*std::cos((across-width)*.40)+gaussian(across+width,strip)*std::cos((across+width)*.40);
		const double churn=gaussian(across,3.3)*std::sin(along*.30-4*1.15+.23*6.2831853)*.24;
		const double trail=smooth(0,10,along)*(1-smooth(length*.5,length,along));
		const double bow=gaussian(x-28,8)*gaussian(y,5.1)*1.9;
		return (trail*(arms*1.35+churn)+bow)*smooth(.3,14,12)*(1-smooth(std::sqrt(436.0)+80,std::sqrt(436.0)+110,std::hypot(x,y)));
	};
	double maxValue=0,maxDerivative=0,maxNormal=0,maxVertex=0,maxIncident=0;
	unsigned changedStationary=0,changedMoving=0,changedGradient=0,changedCompression=0;
	std::vector<float> baseline,generatedBaseline;
	for(unsigned scenario=0;scenario<7;++scenario) {
		const bool enabled=scenario!=3,hasHull=scenario!=0,moving=scenario==2 || scenario==3 || scenario==5 || scenario==6;
		const bool applicable=hasHull && enabled && scenario!=4,flat=scenario==6;
		if(flat) {std::fill(fields.begin(),fields.end(),0);if(!upload()) return;}
		water.FftParameters[1]=enabled?1:0;water.HullParameters[0]=hasHull?(scenario==5?8:1):0;
		memset(water.HullData,0,sizeof(water.HullData));hull.data[2][0]=moving?12:0;hull.data[0][2]=scenario==4?8:7;
		memcpy(water.HullData+(scenario==5?70:0),&hull,sizeof(hull));
		SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);
		for(unsigned stage=0;stage<2;++stage) {
			SDL_GPUColorTargetInfo attachment={};attachment.texture=resources.targets[stage];attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
			SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,NULL);CHECK(pass);SDL_BindGPUGraphicsPipeline(pass,resources.pipelines[stage]);
			SDL_GPUTextureSamplerBinding bindings[6]={};
			for(unsigned i=0;i<(stage?3u:6u);++i) {bindings[i].texture=resources.maps[stage?i*2:i];bindings[i].sampler=resources.sampler;}
			if(stage) {
				SDL_BindGPUVertexSamplers(pass,0,bindings,3);SDL_PushGPUVertexUniformData(commands,0,&water,sizeof(water));
				for(unsigned row=0;row<rows;++row) {
					SDL_GPUViewport viewport={0,float(row),float(columns),1,0,1};SDL_SetGPUViewport(pass,&viewport);
					SDL_PushGPUVertexUniformData(commands,1,points[row],sizeof(points[row]));SDL_DrawGPUPrimitives(pass,3,1,0,0);
				}
			} else {
				SDL_BindGPUFragmentSamplers(pass,0,bindings,6);SDL_PushGPUFragmentUniformData(commands,0,&water,sizeof(water));
				SDL_PushGPUFragmentUniformData(commands,1,points,sizeof(points));SDL_DrawGPUPrimitives(pass,3,1,0,0);
			}
			SDL_EndGPURenderPass(pass);
		}
		CHECK(SDL_SubmitGPUCommandBuffer(commands));const auto values=read_float_targets(frame,resources.targets,2,columns,rows);
		auto gpu=[&](unsigned row,unsigned col,unsigned c){return double(values[(row*columns+col)*4+c]);};
		auto raw=[&](double x,double y,double out[3]) {
			out[0]=out[1]=out[2]=0;if(flat) return;
			for(unsigned c=0;c<3;++c) {
				const double length=std::hypot(modes[c][0],modes[c][1]);
				const double a=2*WaterFftSpectrum::Pi*(modes[c][0]*x+modes[c][1]*y)/(WaterFftSpectrum::Sizes[c]*20)+phase[c];
				out[0]+=20*chop[c]*amplitude[c]*modes[c][0]/length*std::sin(a);
				out[1]+=20*chop[c]*amplitude[c]*modes[c][1]/length*std::sin(a);out[2]+=20*amplitude[c]*std::cos(a);
			}
		};
		auto surface=[&](double x,double y,double out[3]) {
			raw(x,y,out);
			if(applicable) {
				const double qx=(std::max)(-20.0,(std::min)(20.0,x)),qy=(std::max)(-6.0,(std::min)(6.0,y));
				const double distance=std::hypot(x-qx,y-qy);
				if(distance>.0001 && distance<28) {
					const double nx=(x-qx)/distance,ny=(y-qy)/distance,t=distance/28,fade=1-t*t*(3-2*t),weight=.68*fade*fade;
					double d[3];raw(2*qx-x,2*qy-y,d);const double dot=d[0]*nx+d[1]*ny;
					out[0]+=(d[0]-2*nx*dot)*weight;out[1]+=(d[1]-2*ny*dot)*weight;out[2]+=d[2]*weight;
				}
				if(moving) out[2]+=packet(x,y);
			}
			const double bounds[3]={8/std::sqrt(2.0),8/std::sqrt(2.0),WaterFftGeometry::MaxVerticalDisplacement};
			for(unsigned c=0;c<3;++c) out[c]=bounds[c]*std::tanh(out[c]/bounds[c]);
		};
		for(unsigned row=0;row<rows;++row) {
			const double x=points[row][0],y=points[row][1];double expected[3],gradient[2][3];surface(x,y,expected);
			double incidentGradient[2][2];
			for(unsigned axis=0;axis<2;++axis) {
				double plus[3],minus[3];const double step=.0001;
				surface(x+(axis==0?step:0),y+(axis==1?step:0),plus);surface(x-(axis==0?step:0),y-(axis==1?step:0),minus);
				for(unsigned c=0;c<3;++c) gradient[axis][c]=(plus[c]-minus[c])/(2*step);
				raw(x+(axis==0?step:0),y+(axis==1?step:0),plus);raw(x-(axis==0?step:0),y-(axis==1?step:0),minus);
				const double bound=8/std::sqrt(2.0);
				for(unsigned c=0;c<2;++c) incidentGradient[axis][c]=bound*(std::tanh(plus[c]/bound)-std::tanh(minus[c]/bound))/(2*step);
			}
			const double incident=(1+incidentGradient[0][0])*(1+incidentGradient[1][1])-incidentGradient[0][1]*incidentGradient[1][0];
			maxIncident=(std::max)(maxIncident,std::abs(gpu(row,3,3)-incident));CHECK(std::abs(gpu(row,3,3)-incident)<.0003);
			if(scenario>0 && !flat) CHECK(gpu(row,3,3)==baseline[(row*columns+3)*4+3]);
			if(flat) CHECK(gpu(row,3,3)==1);
			for(unsigned c=0;c<3;++c) {
				maxValue=(std::max)(maxValue,std::abs(gpu(row,0,c)-expected[c]));CHECK(std::abs(gpu(row,0,c)-expected[c])<.004);
				for(unsigned axis=0;axis<2;++axis) {
					const double difference=std::abs(gpu(row,axis+1,c)-gradient[axis][c]);maxDerivative=(std::max)(maxDerivative,difference);CHECK(difference<.003);
				}
				const double vertex=values[rows*columns*4+row*columns*4+c],difference=std::abs(vertex-gpu(row,0,c));
				maxVertex=(std::max)(maxVertex,difference);CHECK(difference<.00001);
			}
			gradient[0][0]+=1;gradient[1][1]+=1;double normal[3];
			for(unsigned c=0;c<3;++c) normal[c]=gradient[0][(c+1)%3]*gradient[1][(c+2)%3]-gradient[0][(c+2)%3]*gradient[1][(c+1)%3];
			const double norm=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
			for(unsigned c=0;c<3;++c) {const double difference=std::abs(gpu(row,3,c)-normal[c]/norm);maxNormal=(std::max)(maxNormal,difference);CHECK(difference<.003);}
			CHECK(std::hypot(gpu(row,0,0),gpu(row,0,1))<=8.00001 && std::abs(gpu(row,0,2))<=WaterFftGeometry::MaxVerticalDisplacement+.00001);
			const float shallow=WaterFftGeometry::SafeDamping(1,2.25f);CHECK(gpu(row,0,2)*shallow>=-2.10-.00001);
			if(scenario==1) {
				changedStationary+=std::abs(gpu(row,0,2)-baseline[row*columns*4+2])>.001;
				double difference=0;
				for(unsigned col=1;col<=2;++col) for(unsigned c=0;c<3;++c)
					difference=(std::max)(difference,std::abs(gpu(row,col,c)-baseline[(row*columns+col)*4+c]));
				changedGradient+=difference>.001;
				changedCompression+=std::abs(gpu(row,0,3)-gpu(row,3,3))>.001;
			}
			if(flat) changedMoving+=std::abs(gpu(row,0,2))>.01;
		}
		for(float v:values) CHECK(std::isfinite(v));
		if(scenario==0) baseline=values;
		if(scenario==2) generatedBaseline=values;
		if(scenario==3 || scenario==4) CHECK(values==baseline);
		if(scenario==5) CHECK(values==generatedBaseline);
	}
	CHECK(changedStationary>=3 && changedGradient>=3 && changedCompression>=2 && changedMoving>=2);
	printf("sdl_gpu_frame_selfcheck: GPU FFT-vessel composition: 7 known-mode atlases, value %.8f/gradient %.8f/normal %.8f/VS-PS %.8f/incident compression %.8f; unchanged incident compression with changed physical gradients/Jacobian, exact disabled/level/final-hull gates, stationary reflection, moving displacement on flat water and bounds checked\n",maxValue,maxDerivative,maxNormal,maxVertex,maxIncident);
}

// Direct O(N^2) Fourier sum at a few points: deliberately independent of GPU butterfly ordering,
// bit reversal, .5-per-phase normalization and checkerboard postprocessing. Complex-pair packing
// also verifies all four channels, not just a visually plausible height map.
static void direct_ocean(const std::vector<float>& initial,unsigned cascade,double px,double py,double time,double out[8])
{
	using C=std::complex<double>;const unsigned n=WaterFftSpectrum::Resolution;
	C sums[4]={},unpacked[8]={};const C imaginary(0,1);
	for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
		const double kx=2*WaterFftSpectrum::Pi*(int(x)-int(n/2))/WaterFftSpectrum::Sizes[cascade];
		const double ky=2*WaterFftSpectrum::Pi*(int(y)-int(n/2))/WaterFftSpectrum::Sizes[cascade],k=std::hypot(kx,ky);
		if(k==0) continue;
		const float *h=&initial[((cascade*n+y)*n+x)*4];
		const C e=std::polar(1.0,std::sqrt(9.81*k)*time),height=C(h[0],h[1])*e+C(h[2],h[3])*std::conj(e);
		const C dx=x?height*C(0,-kx/k):C(0),dz=y?height*C(0,-ky/k):C(0);
		const C sx=x?height*C(0,kx):C(0),sz=y?height*C(0,ky):C(0);
		const C dxx=x?height*(kx*kx/k):C(0),dzz=y?height*(ky*ky/k):C(0),dxz=x&&y?height*(kx*ky/k):C(0);
		const C phase=std::polar(1.0,2*WaterFftSpectrum::Pi*((int(x)-int(n/2))*px+(int(y)-int(n/2))*py)/n);
		sums[0]+=(dx+imaginary*height)*phase;sums[1]+=(dz+imaginary*dxz)*phase;
		sums[2]+=(sx+imaginary*sz)*phase;sums[3]+=(dxx+imaginary*dzz)*phase;
		const C physical[8]={dx,height,dz,dxz,sx,sz,dxx,dzz};
		for(unsigned i=0;i<8;++i) unpacked[i]+=physical[i]*phase;
	}
	for(unsigned i=0;i<4;++i) {out[2*i]=sums[i].real();out[2*i+1]=sums[i].imag();}
	// The packed DFT alone can agree with a broken implementation: an imaginary height component
	// would silently appear in its neighbour's real channel. Independently invert all eight physical
	// spectra and require a real result, before comparing that result against the packed transform.
	for(unsigned i=0;i<8;++i) {
		CHECK(std::abs(unpacked[i].imag())<0.00000001);
		CHECK(std::abs(unpacked[i].real()-out[i])<0.00000001);
	}
}

static void check_fft_spectral_reality(const std::vector<float>& initial)
{
	using namespace WaterFftSpectrum;using C=std::complex<double>;
	auto height=[&](const std::vector<float>& data,unsigned cascade,unsigned x,unsigned y,double time) {
		const double kx=2*Pi*(int(x)-int(Resolution/2))/Sizes[cascade],ky=2*Pi*(int(y)-int(Resolution/2))/Sizes[cascade];
		const float *h=&data[((cascade*Resolution+y)*Resolution+x)*4];
		const C e=std::polar(1.0,std::sqrt(9.81*std::hypot(kx,ky))*time);
		return C(h[0],h[1])*e+C(h[2],h[3])*std::conj(e);
	};
	double maximum=0;bool nyquistZero=true;
	for(unsigned cascade=0;cascade<3;++cascade) for(unsigned y=0;y<Resolution;++y) for(unsigned x=0;x<Resolution;++x) {
		if(x==0 || y==0) for(unsigned c=0;c<4;++c) nyquistZero &= initial[((cascade*Resolution+y)*Resolution+x)*4+c]==0;
		for(double time:{0.0,1.375,3.125}) {
			const C a=height(initial,cascade,x,y,time),b=height(initial,cascade,(Resolution-x)%Resolution,(Resolution-y)%Resolution,time);
			maximum=(std::max)(maximum,std::abs(a-std::conj(b)));
		}
	}
	CHECK(nyquistZero && maximum<0.000000000001);
	// An unpaired self-conjugate Nyquist coefficient is the precise old failure. This control must
	// violate the reality assertion even though a packed CPU/GPU FFT comparison would still pass.
	auto broken=initial;const unsigned at=(Resolution/2)*Resolution*4;
	broken[at]=1;
	const C bad=height(broken,0,0,Resolution/2,1.375);
	CHECK(std::abs(bad-std::conj(bad))>0.01);
}

static void check_gpu_fft_ocean(SdlGpuFrame *frame)
{
	using namespace WaterFftSpectrum;
	CHECK(frame->Ocean_Map(0)==NULL && frame->Ocean_Sampler()==NULL);
	CHECK(!frame->Prepare_Ocean(NAN));
	CHECK(frame->Prepare_Ocean(1.375f));if(!frame->Ocean_Map(0)) return;
	CHECK(frame->Ocean_Map(6)==NULL && frame->Ocean_Sampler()!=NULL && frame->Ocean_Updates()==1);
	std::vector<float> field=read_ocean(frame);const std::vector<float> initial=InitialSpectrum();
	check_fft_spectral_reality(initial);
	const unsigned plane=Resolution*Resolution*4;
	auto value=[&](const std::vector<float>& data,unsigned cascade,unsigned channel,unsigned x,unsigned y) -> double {
		return data[(cascade*2+channel/4)*plane+(y*Resolution+x)*4+channel%4];
	};
	bool finite=true,nonempty=true;double largest=0,means[24]={},energies[24]={};
	for(unsigned m=0;m<6;++m) for(unsigned p=0;p<Resolution*Resolution;++p) for(unsigned c=0;c<4;++c) {
		const double v=field[m*plane+p*4+c];finite &= std::isfinite(v);means[m*4+c]+=v;energies[m*4+c]+=v*v;
	}
	for(unsigned c=0;c<24;++c) {nonempty &= energies[c]>0.00001;CHECK(std::abs(means[c]/(Resolution*Resolution))<0.00001);}
	CHECK(finite && nonempty);
	const unsigned coordinates[][2]={{0,0},{17,53},{128,128},{255,7}};
	for(unsigned cascade=0;cascade<3;++cascade) for(auto &point:coordinates) {
		double expected[8];direct_ocean(initial,cascade,point[0],point[1],1.375,expected);
		for(unsigned c=0;c<8;++c) {const double error=std::abs(value(field,cascade,c,point[0],point[1])-expected[c]);
			largest=(std::max)(largest,error);CHECK(error<0.001+std::abs(expected[c])*0.0001);}
	}
	// Project several real GPU maps onto Fourier modes independently. A wrong UV orientation, sign,
	// cascade size or derivative channel cannot satisfy dh/dx=i*kx*h and dh/dz=i*kz*h.
	const int modes[3][2]={{83,47},{19,11},{3,2}};
	double derivativeError=0,finiteDifferenceError=0;
	for(unsigned cascade=0;cascade<3;++cascade) {
		using C=std::complex<double>;C coefficients[3]={};
		const int mx=modes[cascade][0],my=modes[cascade][1];
		std::vector<C> ex(Resolution),ey(Resolution);
		for(unsigned i=0;i<Resolution;++i) {ex[i]=std::polar(1.0,-2*Pi*mx*i/Resolution);ey[i]=std::polar(1.0,-2*Pi*my*i/Resolution);}
		for(unsigned y=0;y<Resolution;++y) for(unsigned x=0;x<Resolution;++x) {
			const C phase=ex[x]*ey[y]/double(Resolution*Resolution);
			coefficients[0]+=value(field,cascade,1,x,y)*phase;
			coefficients[1]+=value(field,cascade,4,x,y)*phase;coefficients[2]+=value(field,cascade,5,x,y)*phase;
		}
		CHECK(std::abs(coefficients[0])>0.000001);
		for(unsigned axis=0;axis<2;++axis) {
			const double k=2*Pi*(axis?my:mx)/Sizes[cascade];
			const double error=std::abs(coefficients[axis+1]-C(0,k)*coefficients[0]);
			derivativeError=(std::max)(derivativeError,error);CHECK(error<0.00001);
			if(cascade==2) {
				const double step=Sizes[cascade]/Resolution,at=0.173;
				auto height=[&](double p){return (coefficients[0]*std::polar(1.0,k*p)).real();};
				const double numeric=(height(at-2*step)-8*height(at-step)+8*height(at+step)-height(at+2*step))/(12*step);
				const double stored=(coefficients[axis+1]*std::polar(1.0,k*at)).real();
				finiteDifferenceError=(std::max)(finiteDifferenceError,std::abs(numeric-stored));CHECK(std::abs(numeric-stored)<0.000001);
			}
		}
	}
	const uint64_t beforePause=frame->Ocean_Updates();CHECK(frame->Prepare_Ocean(1.375f));
	CHECK(frame->Ocean_Updates()==beforePause && read_ocean(frame)==field);
	CHECK(!frame->Prepare_Ocean(INFINITY) && frame->Ocean_Updates()==beforePause);
	SDL_GPUTexture *original=frame->Ocean_Map(0);
	CHECK(frame->Resize(83,57));CHECK(frame->Prepare_Ocean(1.375f));
	CHECK(frame->Ocean_Map(0)==original && frame->Ocean_Updates()==beforePause && read_ocean(frame)==field);
	CHECK(frame->Resize(WIDTH,HEIGHT));

	// Exercise the actual recorded vertex-sampler path, including updates while an earlier field
	// reader is still unsubmitted. Three vertex reads arrive as an unclamped float colour.
	const char *vs=R"HLSL(
Texture2D<float4> A:register(t0,space0);SamplerState SA:register(s0,space0);
Texture2D<float4> B:register(t1,space0);SamplerState SB:register(s1,space0);
Texture2D<float4> C:register(t2,space0);SamplerState SC:register(s2,space0);
struct Output { float4 Position:SV_Position; [[vk::location(0)]] float4 Value:TEXCOORD0; };
Output main(uint id:SV_VertexID) { Output output;output.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);
float2 uv=float2(17.5,53.5)/256.0;output.Value=float4(A.SampleLevel(SA,uv,0).y,B.SampleLevel(SB,uv,0).y,C.SampleLevel(SC,uv,0).y,1);return output; }
)HLSL";
	const char *ps="float4 main([[vk::location(0)]] float4 value:TEXCOORD0):SV_Target0 {return value;}";
	std::vector<unsigned char> spirv;std::string error;
	CHECK(SDL3_Compile_HLSL_To_SPIRV(vs,true,spirv,error));SDL_GPUShader *vertex=SDL3_Create_Shader(frame->Device(),spirv,true,error);CHECK(vertex);
	CHECK(SDL3_Compile_HLSL_To_SPIRV(ps,false,spirv,error));SDL_GPUShader *pixel=SDL3_Create_Shader(frame->Device(),spirv,false,error);CHECK(pixel);
	SDL_GPUColorTargetDescription colour={};colour.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo={};pipelineInfo.vertex_shader=vertex;pipelineInfo.fragment_shader=pixel;
	pipelineInfo.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipelineInfo.target_info.color_target_descriptions=&colour;pipelineInfo.target_info.num_color_targets=1;
	pipelineInfo.target_info.has_depth_stencil_target=true;pipelineInfo.target_info.depth_stencil_format=(SDL_GPUTextureFormat)frame->Depth_Format();
	SDL_GPUGraphicsPipeline *pipeline=SDL_CreateGPUGraphicsPipeline(frame->Device(),&pipelineInfo);CHECK(pipeline);
	SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=colour.format;
	textureInfo.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;textureInfo.width=textureInfo.height=4;
	textureInfo.layer_count_or_depth=textureInfo.num_levels=1;
	SDL_GPUTexture *targets[2]={SDL_CreateGPUTexture(frame->Device(),&textureInfo),SDL_CreateGPUTexture(frame->Device(),&textureInfo)};
	CHECK(targets[0] && targets[1]);
	auto draw=[&](unsigned target) {
		frame->Set_Target({targets[target],frame->Depth_For(4,4),4,4});SdlRecordedDraw record={};record.Pipeline=pipeline;record.Count=3;
		memset(frame->Stage(16,record.VertexOffset),0,16);record.VertexSamplerCount=3;
		for(unsigned i=0;i<3;++i) {record.VertexTextures[i]=frame->Ocean_Map(2*i);record.VertexSamplers[i]=frame->Ocean_Sampler();}
		record.Viewport[2]=record.Viewport[3]=4;record.Viewport[5]=1;record.Scissor[2]=record.Scissor[3]=4;frame->Record_Draw(record);
	};
	if(pipeline && targets[0] && targets[1]) {
		draw(0);CHECK(frame->Prepare_Ocean(3.125f));CHECK(frame->Ocean_Updates()==beforePause+1);draw(1);
		const auto sampled=read_float_targets(frame,targets,2,4,4),advanced=read_ocean(frame);
		for(unsigned c=0;c<3;++c) {
			CHECK(std::abs(sampled[c]-value(field,c,1,17,53))<0.00001);
			CHECK(std::abs(sampled[64+c]-value(advanced,c,1,17,53))<0.00001);
			double expected[8];direct_ocean(initial,c,17,53,3.125,expected);
			for(unsigned component=0;component<8;++component) CHECK(std::abs(value(advanced,c,component,17,53)-expected[component])<0.001+std::abs(expected[component])*0.0001);
		}
		CHECK(advanced!=field);
	}
	CHECK(frame->Flush());frame->Set_Target(frame->Back_Buffer_Target());
	for(auto *target:targets) if(target) SDL_ReleaseGPUTexture(frame->Device(),target);
	if(pipeline) SDL_ReleaseGPUGraphicsPipeline(frame->Device(),pipeline);
	if(vertex) SDL_ReleaseGPUShader(frame->Device(),vertex);if(pixel) SDL_ReleaseGPUShader(frame->Device(),pixel);
	printf("sdl_gpu_frame_selfcheck: GPU FFT ocean: all24 finite channels, direct Fourier error %.8f, spectral derivative error %.9f, finite-difference mode error %.9f; pause/resize reuse and queued vertex-sampled field ordering checked\n",largest,derivativeError,finiteDifferenceError);
}

// B, G, R and A of pixel (x, y) in the gamma test's picture: no two pixels alike, no channel alike.
static void pattern(unsigned int x, unsigned int y, uint8_t out[4])
{
	out[0] = (uint8_t)(x * 4);
	out[1] = (uint8_t)(y * 5);
	out[2] = (uint8_t)((x * 3 + y * 7) & 0xFF);
	out[3] = 0xFF;
}

// How many pixels are more than one level from expected in any channel.
static unsigned int mismatches(const std::vector<uint8_t> &got, const uint16_t (*ramp)[256])
{
	unsigned int bad = 0;
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			uint8_t in[4];
			pattern(x, y, in);
			const uint8_t *px = &got[(y * WIDTH + x) * 4];
			// The ramp is indexed R, G, B; the bytes are B, G, R.
			const int expect[4] = {
				(int)((ramp[2][in[0]] * 255u + 32767u) / 65535u),
				(int)((ramp[1][in[1]] * 255u + 32767u) / 65535u),
				(int)((ramp[0][in[2]] * 255u + 32767u) / 65535u),
				in[3] };
			for (int c = 0; c < 4; ++c) {
				if (abs((int)px[c] - expect[c]) > 1) {
					++bad;
					break;
				}
			}
		}
	}
	return bad;
}

int main()
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		printf("sdl_gpu_frame_selfcheck: SKIP - SDL_Init: %s\n", SDL_GetError());
		return 77;
	}
	std::string error;
	SdlGpuFrame *frame = SdlGpuFrame::Create(NULL, WIDTH, HEIGHT, error);
	if (frame == NULL) {
		printf("sdl_gpu_frame_selfcheck: SKIP - no GPU device here: %s\n", error.c_str());
		SDL_Quit();
		return 77;
	}
	printf("sdl_gpu_frame_selfcheck: %s\n", SDL_GetGPUDeviceDriver(frame->Device()));
	check_gpu_fft_ocean(frame);
	check_gpu_water_vertex_uniform_order(frame);
	check_gpu_vessel_wave_model(frame);
	check_gpu_vessel_hull_damping(frame);
	check_gpu_water_hull_normal(frame);
	check_gpu_fft_vessel_composition(frame);
	check_gpu_wave_model(frame);
	check_gpu_ocean_wave_model(frame);
	check_gpu_water_mesh_correction(frame);
	check_gpu_water_optical_model(frame);
	check_gpu_water_optical_ray_shift(frame);
	check_gpu_water_hull_distance(frame);
	check_gpu_water_hull_effects(frame);

	// A colour clear: D3DCOLOR 0x80402010 is A 0x80, R 0x40, G 0x20, B 0x10.
	frame->Clear_Back_Buffer(true, true, true, 0x80402010u, 1.0f, 0);
	std::vector<uint8_t> pixels;
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	unsigned int wrong = 0;
	for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
		if (pixels[i] != 0x10 || pixels[i + 1] != 0x20 || pixels[i + 2] != 0x40 || pixels[i + 3] != 0x80) {
			++wrong;
		}
	}
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && wrong == 0);

	// Depth and stencil only: the colour is loaded, so it stays.
	frame->Clear_Back_Buffer(false, true, true, 0xFFFFFFFFu, 0.5f, 7);
	CHECK(frame->Read_Back(frame->Back_Buffer(), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() >= 4 && pixels[0] == 0x10 && pixels[2] == 0x40);
	check_scene_depth(frame);

	// The gamma pass, into a target of the test's.
	std::vector<uint8_t> picture(WIDTH * HEIGHT * 4);
	for (unsigned int y = 0; y < HEIGHT; ++y) {
		for (unsigned int x = 0; x < WIDTH; ++x) {
			pattern(x, y, &picture[(y * WIDTH + x) * 4]);
		}
	}
	CHECK(frame->Upload_Back_Buffer(picture));

	// Refraction gets a stable pre-water snapshot, including ordered pending clears/blits.
	SDL_GPUTexture *background = frame->Water_Background(frame->Back_Buffer_Target());
	CHECK(background != NULL && background != frame->Back_Buffer());
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);
	std::vector<uint8_t> replacement(WIDTH * HEIGHT * 4, 0xFF);
	CHECK(frame->Upload_Back_Buffer(replacement));
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture); // Later water draws must not refract previously drawn water.
	frame->Clear_Back_Buffer(true, false, false, 0xFF123456u, 1.0f, 0);
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x56 && pixels[1] == 0x34
		&& pixels[2] == 0x12 && pixels[3] == 0xFF);
	CHECK(frame->Upload_Back_Buffer(picture));

	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)SdlGpuFrame::Target_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = WIDTH;
	info.height = HEIGHT;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	SDL_GPUTexture *target = SDL_CreateGPUTexture(frame->Device(), &info);
	CHECK(target != NULL);

	uint16_t identity[3][256];
	uint16_t ramp[3][256];
	for (int i = 0; i < 256; ++i) {
		identity[0][i] = identity[1][i] = identity[2][i] = (uint16_t)(i * 257);
		ramp[0][i] = (uint16_t)((255 - i) * 257);	// red inverted
		ramp[1][i] = (uint16_t)(i * 257);			// green as it is
		ramp[2][i] = (uint16_t)((i / 2) * 257);		// blue halved
	}
	CHECK(Sdl_Gamma_Is_Identity(identity) && !Sdl_Gamma_Is_Identity(ramp));

	CHECK(frame->Present_To(target, WIDTH, HEIGHT, ramp));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	const unsigned int gamma_wrong = mismatches(pixels, ramp);
	CHECK(gamma_wrong == 0);
	// The armed control: the same output against the identity must differ nearly everywhere.
	const unsigned int control = mismatches(pixels, identity);
	CHECK(control > WIDTH * HEIGHT / 2);

	// The identity ramp is a blit: the picture back as it went in.
	CHECK(frame->Present_To(target, WIDTH, HEIGHT, identity));
	CHECK(frame->Read_Back(target, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// Present begins a new frame even when it did not clear the colour buffer.
	CHECK(frame->Water_Background(frame->Back_Buffer_Target()) == background);
	CHECK(frame->Read_Back(background, WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// The game also draws water into an intermediate colour target, rather than the back buffer.
	SdlTarget intermediate = { target, frame->Depth_For(WIDTH, HEIGHT), WIDTH, HEIGHT };
	frame->Set_Target(intermediate);
	frame->Clear_Back_Buffer(true, false, false, 0xFF102030u, 1.0f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(intermediate), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x30 && pixels[1] == 0x20
		&& pixels[2] == 0x10);
	frame->Clear_Back_Buffer(true, false, false, 0xFF445566u, 1.0f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(intermediate), WIDTH, HEIGHT, pixels));
	CHECK(pixels.size() == WIDTH * HEIGHT * 4 && pixels[0] == 0x66 && pixels[1] == 0x55
		&& pixels[2] == 0x44);
	frame->Set_Target(frame->Back_Buffer_Target());
	CHECK(frame->Read_Back(frame->Water_Background(frame->Back_Buffer_Target()), WIDTH, HEIGHT, pixels));
	CHECK(pixels == picture);

	// A resize must replace the snapshot and match the new back-buffer dimensions.
	CHECK(frame->Resize(16, 8));
	frame->Clear_Back_Buffer(true, true, true, 0xFFAABBCCu, 0.42f, 0);
	CHECK(frame->Read_Back(frame->Water_Background(frame->Back_Buffer_Target()), 16, 8, pixels));
	CHECK(pixels.size() == 16 * 8 * 4 && pixels[0] == 0xCC && pixels[1] == 0xBB
		&& pixels[2] == 0xAA && pixels[3] == 0xFF);
	if (frame->Depth_Usage() & SDL_GPU_TEXTUREUSAGE_SAMPLER) {
		CHECK(frame->Read_Back(frame->Water_Depth(frame->Back_Buffer_Target()), 16, 8, pixels));
		CHECK(pixels.size() == 16 * 8 * 4 && fabs(depth_at(pixels, 16, 15, 7) - 0.42f) < 0.00001f);
		DepthObject resized_object(frame);
		resized_object.Draw(frame->Back_Buffer_Target(), 0.21f);
		CHECK(frame->Present(NULL));
		CHECK(frame->Read_Back(frame->Water_Depth(frame->Back_Buffer_Target()), 16, 8, pixels));
		CHECK(fabs(depth_at(pixels, 16, 8, 4) - 0.21f) < 0.00001f);
	}

	SDL_ReleaseGPUTexture(frame->Device(), target);
	delete frame;
	SDL_Quit();
	if (failures != 0) {
		printf("sdl_gpu_frame_selfcheck: %d FAILED (gamma: %u pixels wrong; control differed in %u)\n",
			failures, gamma_wrong, control);
		return 1;
	}
	printf("sdl_gpu_frame_selfcheck: clear, depth-only clear, gamma (control differs in %u of %u pixels) "
		"scene colour/depth snapshots, submerged object depth, per-target refresh, resource lifetime, resize and identity blit all hold\n", control, WIDTH * HEIGHT);
	return 0;
}
