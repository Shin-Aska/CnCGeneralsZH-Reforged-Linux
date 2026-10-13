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
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include "Platform/WaterSurfaceModel.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

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
	check_gpu_wave_model(frame);

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
