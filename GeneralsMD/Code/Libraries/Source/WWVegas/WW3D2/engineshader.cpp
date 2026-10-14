/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

#include "engineshader.h"
#include "ffvertex.h"
#include "sdl3target.h"
#include "Platform/WaterSurfaceModel.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterHullModel.h"
#include "Platform/WaterFftSampling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
** The terrain and the roads are eight variations on one program: a base colour, and then a chain of
** things multiplied into it.  Written out of the -dx11dump disassembly, one line of ps_1_1 each:
**
**     terrain.pso         lrp r0, v0.w, t1, t0 ; mul r0, r0, v0
**     terrainnoise.pso    the same, then mul r0, r0, t2
**     terrainnoise2.pso   the same, then mul r0, r0, t3
**     fterrain.pso        mul r0, t1, t0 ; mul r0, r0, v0
**     fterrain0.pso       mov r0, t1 ; mul r0, r0, v0
**     fterrainnoise.pso   fterrain, then mul r0, r0, t2
**     fterrainnoise2.pso  the same, then mul r0, r0, t3
**     roadnoise2.pso      mul r0, t0, t1 ; mul r0, r0, t2 ; mul r0, r0, v0
**
** lrp dest, a, b, c is c + a * (b - c), so v0.w weights the second layer over the first: that is
** the terrain blending one ground texture into the next by the alpha in its own vertices.  The road
** one multiplies the vertex colour in last rather than second, which is the same arithmetic until a
** term saturates.
*/
static const char * const TERRAIN_CHAIN[] = { "input.Diffuse", NULL };
static const char * const TERRAIN_NOISE_CHAIN[] = { "input.Diffuse", "texel2", NULL };
static const char * const TERRAIN_NOISE_2_CHAIN[] = { "input.Diffuse", "texel2", "texel3", NULL };
static const char * const ROAD_NOISE_2_CHAIN[] = { "texel2", "input.Diffuse", NULL };

static const char * const TERRAIN_OPENING = "lerp(texel0, texel1, input.Diffuse.a)";
static const char * const FLAT_TERRAIN_OPENING = "texel1 * texel0";
static const char * const FLAT_TERRAIN_BASE_OPENING = "texel1";
static const char * const ROAD_NOISE_2_OPENING = "texel0 * texel1";

// One entry per shipped shader this can write.  The file name is the whole key: the engine loads
// each of them from a fixed path and there is exactly one shader per file.  Opening and Chain are
// set for the multiply-chain programs above and null for the ones written out by hand.
struct EngineShaderEntry
{
	EngineShaderProgram Program;
	const char * FileName;
	const char * Name;
	const char * Opening;
	const char * const * Chain;
};

static const EngineShaderEntry ENGINE_SHADERS[] = {
	{ ENGINE_SHADER_TREES, "trees.vso", "engine:trees", NULL, NULL },
	{ ENGINE_SHADER_WATER_TRAPEZOID, "trapezoid water ps.1.1", "engine:trapezoidwater",
		NULL, NULL },
	{ ENGINE_SHADER_WATER_RIVER, "river water ps.1.1", "engine:riverwater", NULL, NULL },
	{ ENGINE_SHADER_WATER_REFLECTION, "water reflection ps.1.1", "engine:waterreflection",
		NULL, NULL },
	{ ENGINE_SHADER_TERRAIN, "terrain.pso", "engine:terrain",
		TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE, "terrainnoise.pso", "engine:terrainnoise",
		TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_TERRAIN_NOISE_2, "terrainnoise2.pso", "engine:terrainnoise2",
		TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN, "fterrain.pso", "engine:flatterrain",
		FLAT_TERRAIN_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_BASE, "fterrain0.pso", "engine:flatterrainbase",
		FLAT_TERRAIN_BASE_OPENING, TERRAIN_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE, "fterrainnoise.pso", "engine:flatterrainnoise",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_CHAIN },
	{ ENGINE_SHADER_FLAT_TERRAIN_NOISE_2, "fterrainnoise2.pso", "engine:flatterrainnoise2",
		FLAT_TERRAIN_OPENING, TERRAIN_NOISE_2_CHAIN },
	{ ENGINE_SHADER_ROAD_NOISE_2, "roadnoise2.pso", "engine:roadnoise2",
		ROAD_NOISE_2_OPENING, ROAD_NOISE_2_CHAIN },
	{ ENGINE_SHADER_MONOCHROME, "monochrome.pso", "engine:monochrome", NULL, NULL }
};

static const unsigned ENGINE_SHADER_COUNT = sizeof(ENGINE_SHADERS) / sizeof(ENGINE_SHADERS[0]);

static const EngineShaderEntry * entry_for(EngineShaderProgram program)
{
	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		if (ENGINE_SHADERS[entry].Program == program) {
			return &ENGINE_SHADERS[entry];
		}
	}
	return NULL;
}

// The declaration every transcribed vertex program opens with.  The constant bank is the engine's
// own float4 registers, so the shader indexes them by the same number the SetVertexShaderConstantF
// call used and the transcription reads like the assembly it came from.
static void write_preamble(std::string & hlsl)
{
	char line[128];
	snprintf(line, sizeof(line), "cbuffer EngineConstants : register(b0)\n{\n    float4 c[%u];\n};\n\n",
		ENGINE_SHADER_CONSTANTS);
	hlsl += line;
}

// ffvertex's output structure, member for member (append_output_structure).  See the file comment for why
// it cannot differ: it had two coordinate sets where ffvertex writes MAXIMUM_VERTEX_STAGES, which put Fog
// on another location than every pixel program reads it from, and Direct3D 12 refuses such a pair (X1).
static void write_output_structure(std::string & hlsl)
{
	hlsl +=
		"struct Output\n"
		"{\n"
		"    float4 Position : SV_Position;\n"
		"    float4 Diffuse  : COLOR0;\n"
		"    float4 Specular : COLOR1;\n";
	for (unsigned stage = 0; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}
	hlsl +=
		"    float Fog : FOG;\n"
		"};\n"
		"\n";
}

// The coordinate sets a transcription does not write: zero, as ffvertex writes a set it has no source for.
static void write_unused_coordinates(std::string & hlsl, unsigned first)
{
	for (unsigned stage = first; stage < MAXIMUM_VERTEX_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    output.TexCoord%u = float2(0.0, 0.0);\n", stage);
		hlsl += line;
	}
}

/*
** Trees.vso, transcribed from the vs_1_1 the translator disassembles:
**
**     dcl_position v0, dcl_blendweight v1, dcl_blendindices v2, dcl_texcoord v7
**     mov r2, v1.wwzw       ; a float3 input reads w as one, so this is (1, 1, v1.z, 1)
**     add r2, v0, -r2       ; r2.z is the vertex height above the tree's own base
**     mov a0.x, v1          ; the sway type, rounded
**     mov r0, c8[a0.x]      ; c8 is the no-sway slot, c9 to c18 the ten breezes
**     mad r1, r2.zzzw, r0, v0
**     m4x4 oPos, r1, c4
**     mov r2, v1.yyyw
**     mul oD0, v2, r2       ; the tree's lit shade, with the vertex alpha kept
**     mov oT0, v7
**     add r1, v0, c32
**     mul oT1, r1, c33      ; the shroud coordinate
**
** The declaration beside it (W3DTreeBuffer::initData) puts position in register 0, the normal slot
** in 1 and the D3DCOLOR in 2, which is the byte layout of DX8_FVF_XYZNDUV1.  Nothing in the tree
** vertex is a normal: nx is the sway type, ny the shade the push-aside left, nz the base height.
**
** oFog is never written, so the fog factor is one and the trees are drawn unfogged, which is what
** the Direct3D 9 device does with this shader bound.
*/
static void write_trees(std::string & hlsl)
{
	write_preamble(hlsl);
	hlsl +=
		"struct Input\n"
		"{\n"
		"    float3 Position : POSITION;\n"
		"    float3 Sway : NORMAL;\n"
		"    float4 Diffuse : COLOR0;\n"
		"    float2 TexCoord0 : TEXCOORD0;\n"
		"};\n"
		"\n";
	write_output_structure(hlsl);
	hlsl +=
		"Output main(Input input)\n"
		"{\n"
		"    Output output;\n"
		"    int sway_type = (int)(input.Sway.x + 0.5);\n"
		"    float height = input.Position.z - input.Sway.z;\n"
		"    float4 swayed = float4(height * c[8 + sway_type].xyz + input.Position, 1.0);\n"
		"    output.Position = float4(dot(swayed, c[4]), dot(swayed, c[5]),\n"
		"        dot(swayed, c[6]), dot(swayed, c[7]));\n"
		"    output.Diffuse = input.Diffuse * float4(input.Sway.yyy, 1.0);\n"
		"    output.Specular = float4(0.0, 0.0, 0.0, 0.0);\n"
		"    output.TexCoord0 = input.TexCoord0;\n"
		"    output.TexCoord1 = ((float4(input.Position, 1.0) + c[32]) * c[33]).xy;\n";
	write_unused_coordinates(hlsl, 2);
	hlsl +=
		"    output.Fog = 1.0;\n"
		"    return output;\n"
		"}\n";
}

// ffshader's declarations, member for member and in the same order, so a transcribed pixel program
// is bound with the same constant buffer, the same textures and the same samplers as a generated
// one.  Reading fewer of them than are declared costs nothing.
static void write_pixel_preamble(std::string & hlsl, bool volumetric = false)
{
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"Texture2D Texture%u : register(t%u);\n"
			"SamplerState Sampler%u : register(s%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}

	hlsl +=
		"cbuffer CombinerConstants : register(b0)\n"
		"{\n"
		"    float4 TextureFactor;\n"
		"    float4 FogColour;\n"
		"    float4 AlphaReference;\n";
	{
		// The whole of DX11BackendClass::PixelConstantBlock up to the sun, in its order.  Declared
		// whether this program lights with it or not, because what follows is found by offset.
		char line[256];
		snprintf(line, sizeof(line),
			"    float4 NormalLightDirection[%u];\n"
			"    float4 NormalLightDiffuse[%u];\n"
			"    float4 NormalMapParameters;\n"
			"    float4 TerrainSunDirection;\n",
			NORMAL_MAPPED_LIGHTS, NORMAL_MAPPED_LIGHTS);
		hlsl += line;
	}
	// The shadow fields close the block, in the backend's order, and every transcribed program
	// declares them whether it reads them or not: a field is found by what comes before it.
	hlsl +=
		"    row_major float4x4 ShadowFromClip;\n"
		"    float4 ShadowParameters;\n"
		"    float4 ShadowViewport;\n"
		"    float4 ShadowSoftness;\n"
		"    float4 Sky;\n"
		"    float4 SkyUp;\n";
	// The smoke's field follows them, on the Direct3D 11 text alone (VOLUMETRIC_SAMPLING).
	if (volumetric) {
		hlsl += VOLUMETRIC_CONSTANTS;
	}
	hlsl += "};\n";
	hlsl += SHADOW_SAMPLING;
	if (volumetric) {
		hlsl += VOLUMETRIC_SAMPLING;
	}
	hlsl +=
		"\n"
		"struct Input\n"
		"{\n"
		"    float4 Position  : SV_Position;\n"
		"    float4 Diffuse   : COLOR0;\n"
		"    float4 Specular  : COLOR1;\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[64];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}

	hlsl += "    float Fog        : FOG;\n";
	hlsl +=
		"};\n"
		"\n";
	hlsl +=
		"float4 main(Input input) : SV_Target\n"
		"{\n";

	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[128];
		snprintf(line, sizeof(line),
			"    float4 texel%u = Texture%u.Sample(Sampler%u, input.TexCoord%u);\n",
			stage, stage, stage, stage);
		hlsl += line;
	}
}

/*
** The trapezoid water, the ps.1.1 W3DWater assembles for every flat water body:
**
**     tex t0 ; the water texture
**     tex t1 ; white highlights on black
**     tex t2 ; the same highlights, tiled harder, on the camera space position
**     tex t3 ; the shroud, or white where there is none
**     mul r0, v0, t0
**     mad r0.rgb, t1, t2, r0
**     mul r0.rgb, r0, t3
**
** ps_1_1's registers hold [-1, 1] (PixelShader1xMaxValue 1, the documented minimum for ps 1.0 to 1.3),
** and only a _sat modifier or the final write clamps to [0, 1].  Every value here is a product or sum
** of texels and vertex colours in [0, 1], so none goes below zero, and the saturate on each line is the
** clamp at one: the same result.  Measured against the tests' ps_1_1 interpreter on captured draws (A3e-3).
** The alpha is the vertex alpha times the water texture's and the shroud never touches it: a
** shrouded stretch of water is dark, not transparent.
*/
static void write_trapezoid_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current = saturate(input.Diffuse * texel0);\n"
		"    current.rgb = saturate(texel1.rgb * texel2.rgb + current.rgb);\n"
		"    current.rgb = saturate(current.rgb * texel3.rgb);\n";
}

/*
** The river water, the same four stages with the sparkles kept apart from the base colour:
**
**     mul r0.rgb, v0, t0    ; the water, tinted by the vertex colour
**     mov r0.a, t0          ; the vertex alpha carries the shroud and must not fade the water
**     mul r1, t1, t2
**     add r1.rgb, r1, t3
**     mul r1.rgb, r1, v0.a  ; the sparkles and the edge glow do get darkened by the shroud
**     +mul r0.a, r0, t3
**     add r0.rgb, r0, r1
**
** The + pairs that alpha instruction with the colour one above it, so it reads r0.a as the previous
** line left it and writes the shroud's alpha into it.
*/
static void write_river_water(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float4 current;\n"
		"    current.rgb = saturate(input.Diffuse.rgb * texel0.rgb);\n"
		"    current.a = saturate(texel0.a * texel3.a);\n"
		"    float3 sparkle = saturate(texel1.rgb * texel2.rgb);\n"
		"    sparkle = saturate(sparkle + texel3.rgb);\n"
		"    sparkle = saturate(sparkle * input.Diffuse.a);\n"
		"    current.rgb = saturate(current.rgb + sparkle);\n";
}

/*
** The reflection W3DWater lays over map water once the water itself is drawn:
**
**     def c1, 0.333333, 0.333333, 0.333333, 0
**     tex t0 ; the mirrored scene, looked up through a projected stage
**     dp3 r1.rgb, t0, c1
**     mov r1.rgb, 1-r1
**     add r1.rgb, r1, r1
**     add r1.rgb, r1, r1
**     mul r0.rgb, r1, c0
**     mov r0.rgb, 1-r0
**     +mov r0.a, c0
**
** The result multiplies the water already drawn, and the mirror was cleared to white, so open sky
** leaves the water its own colour.  Whatever stands in the mirror darkens it by the same amount
** whatever its own colour: a sunlit palm is nearly as bright as the white around it and used to
** vanish from the water the moment the fog lifted off it.  The two doublings clamp at one each, which
** is the saturate.  D3D9 reads the strength out of c0.  A transcribed program shares the combiners'
** constant buffer and not the engine's register bank, so here it is the texture factor's alpha,
** which W3DWater sets to the same value.
*/
static void write_water_reflection(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float coverage = saturate((1.0 - dot(texel0.rgb, float3(0.333333, 0.333333, 0.333333))) * 4.0);\n"
		"    float4 current = float4((1.0 - TextureFactor.a * coverage).xxx, TextureFactor.a);\n";
}

/*
** The grey the campaign briefings and some cutscenes lay over the whole view, ScreenBWFilter's:
**
**     tex t0                ; the frame, rendered into a texture first
**     dp3 r1, t0, c0        ; c0 is (0.3, 0.59, 0.11), the luminance
**     mul r1, r1, c1        ; c1 tints it: white, red or green, alpha one
**     lrp r0, c2, r1, t0    ; c2 is the fade, the same value in all four
**
** D3D9 reads the tint and the fade out of c1 and c2.  Here they ride in the texture factor, tint in
** the colour and fade in the alpha, which ScreenBWFilter sets beside the constants.  c2.w is one
** whatever the fade, so the alpha comes out as the luminance at every step of it.
*/
static void write_monochrome(std::string & hlsl)
{
	write_pixel_preamble(hlsl);
	hlsl +=
		"    float luminance = saturate(dot(texel0.rgb, float3(0.3, 0.59, 0.11)));\n"
		"    float4 tinted = saturate(luminance * float4(TextureFactor.rgb, 1.0));\n"
		"    float4 current = lerp(texel0, tinted, float4(TextureFactor.aaa, 1.0));\n";
}

// ps_1_1's registers hold [-1, 1] and only _sat or the final write clamps to [0, 1] (see the trapezoid
// water above).  A chain of products of values in [0, 1] never goes below zero, so the clamp that
// matters is the one at the top, and it applies at every step, not only the last: a chain that
// overflows in the middle and comes back down is a different colour with the clamps than without them.
static void write_multiply_chain(std::string & hlsl, const EngineShaderEntry & entry,
	bool volumetric)
{
	write_pixel_preamble(hlsl, volumetric);

	hlsl += "    float4 current = saturate(";
	hlsl += entry.Opening;
	hlsl += ");\n";

	for (const char * const * step = entry.Chain; *step != NULL; ++step) {
		hlsl += "    current = saturate(current * ";
		hlsl += *step;
		hlsl += ");\n";
	}

	// The ground is where a shadow is read, so every transcribed program that paints it takes one.
	hlsl += volumetric ? VOLUMETRIC_SHADOW_APPLY : SHADOW_APPLY;
}

bool EngineShader_Paints_Ground(EngineShaderProgram program)
{
	return program == ENGINE_SHADER_TERRAIN || program == ENGINE_SHADER_TERRAIN_NOISE
		|| program == ENGINE_SHADER_TERRAIN_NOISE_2 || program == ENGINE_SHADER_FLAT_TERRAIN
		|| program == ENGINE_SHADER_FLAT_TERRAIN_BASE || program == ENGINE_SHADER_FLAT_TERRAIN_NOISE
		|| program == ENGINE_SHADER_FLAT_TERRAIN_NOISE_2 || program == ENGINE_SHADER_ROAD_NOISE_2;
}

static char lowered(char character)
{
	return (character >= 'A' && character <= 'Z') ? (char)(character - 'A' + 'a') : character;
}

// The last path component, lowered, compared against the table.  The engine writes its paths with
// backslashes and mixed case, and neither is worth carrying into the table.
EngineShaderProgram EngineShader_From_File(const char * file_path)
{
	if (file_path == NULL) {
		return ENGINE_SHADER_NONE;
	}

	const char * name = file_path;
	for (const char * step = file_path; *step != '\0'; ++step) {
		if (*step == '\\' || *step == '/') {
			name = step + 1;
		}
	}

	for (unsigned entry = 0; entry < ENGINE_SHADER_COUNT; ++entry) {
		const char * wanted = ENGINE_SHADERS[entry].FileName;
		const char * found = name;
		while (*wanted != '\0' && lowered(*found) == *wanted) {
			++wanted;
			++found;
		}
		if (*wanted == '\0' && *found == '\0') {
			return ENGINE_SHADERS[entry].Program;
		}
	}
	return ENGINE_SHADER_NONE;
}

bool EngineShader_Vertex_Program(EngineShaderProgram program, std::string & hlsl,
	VertexShaderTarget target)
{
	// Transcribed for the D3D11 profile only: on D3D9 the shipped .vso ran on the device itself.
	// SDL3 GPU is the D3D11 text with its bindings rewritten.
	if (target == VERTEX_SHADER_TARGET_D3D9) {
		hlsl.clear();
		return false;
	}
	if (target == VERTEX_SHADER_TARGET_SDL3_GPU) {
		return EngineShader_Vertex_Program(program, hlsl, VERTEX_SHADER_TARGET_D3D11)
			&& SDL3_Shader_Retarget(hlsl, true);
	}

	hlsl.clear();
	switch (program) {
	case ENGINE_SHADER_TREES:
		write_trees(hlsl);
		return true;
	default:
		return false;
	}
}

static bool write_engine_pixel_program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, bool volumetric);

// SDL map water has a dedicated, explicitly packed uniform block (SdlWaterPixelConstants).
// Runtime-generated normal detail, terrain depth, refraction and the coloured planar reflection
// are composed before the existing shore/shroud alpha blend. No additional assets are required.
static void write_modern_water(std::string &hlsl, bool river)
{
	hlsl.clear();
	for (unsigned stage = 0; stage < 14; ++stage) {
		char line[128];
		snprintf(line, sizeof(line), "Texture2D Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
		hlsl += line;
	}
	hlsl += WaterFftSampling::UniformSource("CombinerConstants",0);
	hlsl += R"HLSL(struct Input
{
    float4 Position : SV_Position;
    float4 Diffuse : COLOR0;
    float4 Specular : COLOR1;
)HLSL";
	for (unsigned stage = 0; stage < MAXIMUM_COMBINER_STAGES; ++stage) {
		char line[80];
		snprintf(line, sizeof(line), "    float2 TexCoord%u : TEXCOORD%u;\n", stage, stage);
		hlsl += line;
	}
	hlsl += R"HLSL(    float Fog : FOG;
};
// The legacy scene targets use UNORM rather than sRGB. Decode their colours for light transport,
// then encode the composed water once before the legacy fog and shoreline blend.
float3 waterLinear(float3 rgb) { return pow(max(rgb, float3(0, 0, 0)), 2.2); }
float3 waterDisplay(float3 rgb) { return pow(saturate(rgb), 1.0 / 2.2); }
float oceanHash(float2 p)
{
    float3 q = frac(float3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}
float oceanNoise(float2 p)
{
    float2 cell = floor(p), f = frac(p);
    f = f*f*(3.0-2.0*f);
    return lerp(lerp(oceanHash(cell), oceanHash(cell+float2(1,0)), f.x),
        lerp(oceanHash(cell+float2(0,1)), oceanHash(cell+float2(1,1)), f.x), f.y);
}
// Broad radiance belongs to the reflected sky direction. It therefore follows the evolving
// surface normal instead of looking like a second texture sliding across the water plane.
float3 oceanSky(float3 ray, float3 light, float daylight)
{
    float horizon = pow(1.0-saturate(ray.z), 4.0);
    float2 sunAxis = normalize(light.xy + float2(0.00001,0));
    float2 skyPlane = float2(dot(ray.xy,sunAxis),dot(ray.xy,float2(-sunAxis.y,sunAxis.x)))
        / (0.45+abs(ray.z));
    float cloud = oceanNoise(skyPlane*2.7+float2(7.8,2.3))*0.7
        + oceanNoise(skyPlane*6.1+float2(0.4,13.7))*0.3;
    // A single broad cloud bank lights only the facets reflecting that portion of the sky.
    // Its direction follows the sky's sun-relative frame, independent of the camera and waves.
    float3 cloudDirection=normalize(float3(-sunAxis.y,sunAxis.x,0.85));
    float cloudBank=pow(saturate(dot(ray,cloudDirection)),48.0);
    cloud = smoothstep(0.46,0.73,cloud) * (0.04+5.5*cloudBank);
    // A finite bright part of the sky produces broken silver facets. An azimuth-uniform bright
    // horizon instead illuminates every crest equally, making the waves look like pale stripes.
    float3 radiance = lerp(float3(0.016,0.055,0.110),float3(0.023,0.070,0.130),horizon);
    radiance += cloud*float3(1.85,1.96,2.04);
    radiance += pow(saturate(dot(ray,light)),12.0)*float3(0.24,0.18,0.10);
    // Downward reflected rays see adjacent water. Keep the horizon transition continuous, but
    // do not extend a bright sky over the whole lower hemisphere.
    radiance = lerp(float3(0.001,0.005,0.010),radiance,smoothstep(-0.12,0.18,ray.z));
    return radiance * (0.06+0.94*daylight);
}
float3 oceanEnvironment(float3 ray,float3 light,float daylight,float roughness)
{
    // The unresolved ripple distribution broadens every reflected light source, including the
    // sky. Sampling its sharp horizon/cloud field only once makes small normal changes flicker.
    float3 tangent=normalize(cross(ray,float3(0,0,1))+float3(0.00001,0,0));
    float3 bitangent=cross(ray,tangent);
    float cone=max(roughness,0.22)*0.85;
    float3 value=oceanSky(ray,light,daylight)*0.4;
    value+=oceanSky(normalize(ray+tangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray-tangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray+bitangent*cone),light,daylight)*0.15;
    value+=oceanSky(normalize(ray-bitangent*cone),light,daylight)*0.15;
    return value;
}
float terrainHeight(float2 p)
{
    float2 uv = (p + TerrainParameters.xx + 5.0) / max(TerrainParameters.y, 1.0);
    return Texture2.SampleLevel(Sampler2, uv, 0.0).r * TerrainParameters.z;
}
float terrainSun(float3 world, float3 light)
{
    float visible = 1.0;
    if (TerrainParameters.w > 0.5) {
        float distance = 12.0;
        for (int i = 0; i < 6; ++i) {
            float3 ray = world + light * distance;
            visible = min(visible, smoothstep(-0.6, 1.2, ray.z - terrainHeight(ray.xy)));
            distance *= 2.0;
        }
    }
    return visible;
}
float2 pixelOffset(float2 offset, float2 dx, float2 dy)
{
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float inverse = abs(determinant) > 0.00001 ? 1.0 / determinant : 0.0;
    return float2(offset.x * dy.y - offset.y * dy.x, dx.x * offset.y - dx.y * offset.x) * inverse;
}
float3 scenePosition(float2 pixel, float deviceDepth)
{
    float2 uv = (pixel - Viewport.xy) / max(Viewport.zw, float2(1, 1));
    float4 p = mul(float4(uv * float2(2, -2) + float2(-1, 1), deviceDepth, 1), WorldFromClip);
    return p.xyz / p.w;
}
// Depth is a point-sampled, independent R32 snapshot of the scene's depth attachment. It includes
// submerged objects. Terrain height is only a fallback and a material classification aid.
float3 bottomPosition(float2 pixel, float3 surface, float baseLevel)
{
    float h = TerrainParameters.w > 0.5 ? terrainHeight(surface.xy) : baseLevel - 20.0;
    float3 fallback = float3(surface.xy, min(h, baseLevel));
    if (SceneParameters.w < 0.5) return fallback;
    // A point depth belongs to the sampled texel's centre, even when the colour refraction is
    // fractional. Unproject that same ray and stay inside the source camera's active viewport.
    float2 firstPixel = max(float2(0.5,0.5), Viewport.xy);
    float2 lastPixel = min(1.0 / SceneParameters.yz - 0.5, Viewport.xy + Viewport.zw - 1.0);
    pixel = clamp(floor(pixel) + 0.5, firstPixel, lastPixel);
    float d = Texture6.SampleLevel(Sampler6, pixel * SceneParameters.yz, 0).r;
    if (d >= 0.999999) return fallback;
    return scenePosition(pixel, d);
}
)HLSL";
	hlsl += WaterSurfaceModel::ShaderSource();
	hlsl += WaterSurfaceModel::OceanShaderSource();
	hlsl += WaterOpticalModel::GetHlslSource();
	hlsl += WaterHullModel::GetHlslSource();
	hlsl += WaterFftSampling::PixelSource();
	char oceanBound[96];
	snprintf(oceanBound, sizeof(oceanBound), "static const float oceanDisplacementBound = %.9g;\n",
		(double)WaterSurfaceModel::OceanMaxDisplacement);
	hlsl += oceanBound;
	hlsl += R"HLSL(
float terrainWaveAttenuation(float depth, float offshore)
{
    float shore = saturate(depth/9.0);
    shore = shore*shore*(3.0-2.0*shore);
    return lerp(shore, saturate((depth-0.15)/oceanDisplacementBound), offshore);
}
)HLSL";
	hlsl += WaterHullModel::GetEffectsHlslSource();
	hlsl += R"HLSL(
float4 main(Input input) : SV_Target
{
    float2 screenUV = (input.Position.xy - Viewport.xy) / max(Viewport.zw, float2(1.0, 1.0));
    float4 worldH = mul(float4(screenUV * float2(2.0, -2.0) + float2(-1.0, 1.0), input.Position.z, 1.0), WorldFromClip);
    float3 world = worldH.xyz / worldH.w;
    float2 dx = ddx(world.xy), dy = ddy(world.xy);
    float time = WaterParameters.x;
    float fftWeight=FftParameters.x*saturate(input.Specular.x);
    // Horizontal chop changes world.xy. Spectral samples must remain on the original surface
    // parameters, as the vertex stage used them, rather than swimming along the displaced mesh.
    float2 wavePosition=FftParameters.x>0.5?input.TexCoord2:world.xy;
    float2 parameterDx=ddx(wavePosition),parameterDy=ddy(wavePosition);
    // Projected flat patches reuse UV0 for a render-only boundary/LOD height correction.
    // Their original material mapping is world.xy/150. Legacy grids and rivers keep their UVs.
    float meshCorrection=OpticalParameters.w>0.5?input.TexCoord0.x:0.0;
    float2 materialUV=OpticalParameters.w>0.5?world.xy/150.0:input.TexCoord0;
    float4 base = Texture0.Sample(Sampler0, materialUV);
    float baseLevel = input.TexCoord1.y;
    float3 bottom = bottomPosition(input.Position.xy, world, baseLevel);
    float depth = max(world.z - bottom.z, 0.0);
    float3 view = normalize(CameraPosition.xyz - world + float3(0, 0, 0.00001));
    float2 contextUV = (world.xy + TerrainParameters.xx + 5.0) / max(TerrainParameters.y, 1.0);
    float offshore = Texture4.SampleLevel(Sampler4, contextUV, 0).r
        * OpticalParameters.z * (1.0 - WaterParameters.y);
    // The mesh and this derivative share every mode, phase, dispersion and crest harmonic.
    // Interpolated attenuation includes shores/structures, and its product derivative is retained.
    float3 legacyWave = waterWave(wavePosition, time);
    float4 oceanSwell;
    float4 oceanSurface = waterOceanSurface(wavePosition, time, oceanSwell);
    float3 oceanWave = oceanSurface.xyz;
    float3 wave = lerp(legacyWave, oceanWave, offshore);
    FftSurface spectral=(FftSurface)0;
    if(FftParameters.x>0.5) spectral=fftSurface(wavePosition,baseLevel);
    float footprint=max(length(dx),length(dy));
    float hullWave=lerp(wave.x,spectral.displacement.z,fftWeight);
    float3 hullEffects=oceanHullEffects(world.xy,baseLevel,time,offshore,hullWave,footprint);
    float attenuation = saturate(input.TexCoord1.x);
    // Flat water carries obstacle-only attenuation in its otherwise unused diffuse red channel.
    // Reconstruct the terrain profile at fragment density, avoiding large triangular shading
    // facets from differentiating the coarse mesh's interpolated shore attenuation.
    float localTerrain = TerrainParameters.w > 0.5
        ? terrainWaveAttenuation(baseLevel-terrainHeight(world.xy), offshore) : 1.0;
    attenuation = lerp(attenuation, min(localTerrain, input.Diffuse.r), offshore);
    attenuation = min(attenuation,lerp(1.0,hullEffects.z,offshore));
    float determinant = parameterDx.x * parameterDy.y - parameterDx.y * parameterDy.x;
    float invDet = abs(determinant) > 0.000001 ? 1.0 / determinant : 0.0;
    float ou = ddx(offshore), ov = ddy(offshore);
    float2 offshoreSlope = float2(ou*parameterDy.y-ov*parameterDx.y, ov*parameterDx.x-ou*parameterDy.x)*invDet;
    wave.yz += (oceanWave.x-legacyWave.x)*offshoreSlope;
    float du = ddx(attenuation), dv = ddy(attenuation);
    float2 attenuationSlope = float2(du*parameterDy.y - dv*parameterDx.y, dv*parameterDx.x - du*parameterDy.x) * invDet;
    float zu = ddx(baseLevel), zv = ddy(baseLevel);
    float2 baseSlope = float2(zu*parameterDy.y - zv*parameterDx.y, zv*parameterDx.x - zu*parameterDy.x) * invDet;
    float cu=ddx(meshCorrection),cv=ddy(meshCorrection);
    float2 correctionSlope=float2(cu*parameterDy.y-cv*parameterDx.y,cv*parameterDx.x-cu*parameterDy.x)*invDet;
    float2 slope = baseSlope + wave.yz * attenuation + wave.x * attenuationSlope + correctionSlope;
    float3 normal = normalize(float3(-slope, 1.0));
    float continuousHeight=baseLevel+wave.x*attenuation+meshCorrection;
    float fftCompression=1.0;
    float fftDamping=0.0;
    if(FftParameters.x>0.5) {
        float freeDamping=saturate(input.Specular.y),fftOffshore=saturate(input.Specular.w);
        float3 dampingField=vesselApplyHullDamping(wavePosition,baseLevel,
            float3(freeDamping,fftParameterGradient(freeDamping,parameterDx,parameterDy)),
            float3(fftOffshore,fftParameterGradient(fftOffshore,parameterDx,parameterDy)));
        fftDamping=dampingField.x;
        float2 weightSlope=fftParameterGradient(fftWeight,parameterDx,parameterDy);
        float2 dampingSlope=dampingField.yz;
        float horizontalWeight=fftWeight*fftDamping;
        float2 horizontalSlope=weightSlope*fftDamping+dampingSlope*fftWeight;
        float oldHeight=continuousHeight-baseLevel;
        float newHeight=spectral.displacement.z*fftDamping;
        float2 newSlope=float2(spectral.derivativeX.z,spectral.derivativeY.z)*fftDamping
            +spectral.displacement.z*dampingSlope;
        float2 heightSlope=baseSlope+lerp(slope-baseSlope,newSlope,fftWeight)
            +(newHeight-oldHeight)*weightSlope;
        float3 tangentX=float3(1.0+spectral.derivativeX.x*horizontalWeight+spectral.displacement.x*horizontalSlope.x,
            spectral.derivativeX.y*horizontalWeight+spectral.displacement.y*horizontalSlope.x,heightSlope.x);
        float3 tangentY=float3(spectral.derivativeY.x*horizontalWeight+spectral.displacement.x*horizontalSlope.y,
            1.0+spectral.derivativeY.y*horizontalWeight+spectral.displacement.y*horizontalSlope.y,heightSlope.y);
        normal=normalize(cross(tangentX,tangentY));
        continuousHeight=baseLevel+lerp(oldHeight,newHeight,fftWeight);
        fftCompression=spectral.incidentCompression;
    }
    // Reconstruct the same continuous wave between mesh vertices for optical integration. The
    // rendered scene bottom stays exact; using coarse triangle heights here exposes their edges
    // through absorption on the campaign shell's deliberately shallow offshore terrain. Move on
    // the camera ray, with a bounded correction at grazing angles, retaining raster occlusion.
    float opticalShift = WaterOpticalRayShift(continuousHeight-world.z,
        view.z, offshore, oceanDisplacementBound, dot(world-bottom,view), length(CameraPosition.xyz-world));
    float3 opticalWorld = world + view*opticalShift;
    float opticalDepth = max(opticalWorld.z-bottom.z,0.0);
    float3 light = normalize(SunDirection.xyz + float3(0, 0, 0.00001));
    float nDotV = max(dot(normal, view), 0.001);
    float nDotL = saturate(dot(normal, light));
    float3 halfway = normalize(view + light + float3(0, 0, 0.00001));
    float nDotH = saturate(dot(normal, halfway));
    float vDotH = saturate(dot(view, halfway));
    float fresnel = 0.025 + 0.975 * pow(1.0 - saturate(nDotV), 5.0);
    // Keep the bed readable at the shallow bank instead of covering it with bright sky bands.
    fresnel = 0.025 + (fresnel - 0.025) * lerp(lerp(0.35, 1.0, smoothstep(3.0, 18.0, depth)), 1.0, offshore);
    float variance = dot(ddx(normal), ddx(normal)) + dot(ddy(normal), ddy(normal));
    float roughness = clamp(sqrt(lerp(0.016, 0.036, offshore) + variance * 0.24), 0.126, 0.34);
    roughness=lerp(roughness,max(roughness,0.27),hullEffects.y*0.5);
    float a2 = pow(roughness, 4.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(3.14159265 * denominator * denominator, 0.0000001);
    float k = roughness * roughness * 0.5;
    float masking = nDotV / (nDotV * (1.0 - k) + k);
    masking *= nDotL / max(nDotL * (1.0 - k) + k, 0.0001);
    float sunFresnel = 0.02 + 0.98 * pow(1.0 - vDotH, 5.0);
    float sunVisible = terrainSun(world, light);
    float3 highlight = waterLinear(SunColour.rgb) * 2.0 * (distribution * masking * sunFresnel * sunVisible / max(4.0 * nDotV, 0.001));
    highlight = highlight / (1.0 + highlight * 0.6);
    float daylight = saturate(max(max(SunColour.r, SunColour.g), SunColour.b));
    // Integrate the short crossing ripples into the environment lobe. Their shared spectrum
    // still supplies physical height, refraction normals and directional sun glints, while the
    // broad sky follows the resolved swell surface instead of tracing every fine ripple edge.
    float rippleResolution=0.88/(1.0+footprint*0.18);
    float3 environmentWave=lerp(oceanSwell.xyz,oceanWave,rippleResolution);
    float2 swellSlope=baseSlope+environmentWave.yz*attenuation+environmentWave.x*attenuationSlope+correctionSlope;
    float3 environmentNormal=normalize(lerp(normal,normalize(float3(-swellSlope,1)),offshore));
    environmentNormal=normalize(lerp(environmentNormal,normal,fftWeight));
    // The remaining fine-slope energy is ~0.045 for this spectrum (including its packet
    // envelope); retaining a fraction of its amplitude leaves (1-fraction^2) as roughness.
    float environmentRoughness=sqrt(roughness*roughness+0.045*(1.0-rippleResolution*rippleResolution)*offshore);
    float3 reflectedRay = reflect(-view, environmentNormal);
    float3 sky = lerp(float3(0.49, 0.62, 0.68), float3(0.23, 0.38, 0.48), saturate(reflectedRay.z)) * (0.06 + daylight * 0.94);
    float2 reflectionOffset = float2((ReflectionArea.x + 1.0) * 0.5, (1.0 - ReflectionArea.w) * 0.5);
    float2 reflectionScale = max((ReflectionArea.zw - ReflectionArea.xy) * 0.5, float2(0.001, 0.001));
    float2 reflectionUV = (screenUV - reflectionOffset) / reflectionScale;
    float2 distortion = pixelOffset(normal.xy * 8.0, dx, dy) / max(Viewport.zw, float2(1, 1)) / reflectionScale;
    float2 blur = roughness * 0.008 / reflectionScale;
    float4 mirror = Texture1.Sample(Sampler1, saturate(reflectionUV + distortion)) * 0.5;
    mirror += Texture1.Sample(Sampler1, saturate(reflectionUV + distortion + blur)) * 0.25;
    mirror += Texture1.Sample(Sampler1, saturate(reflectionUV + distortion - blur)) * 0.25;
    float2 border = min(reflectionUV, 1.0 - reflectionUV);
    // Legacy terrain passes preserve destination alpha for shore blending, including in the
    // mirror. Its transparent-black clear gives RGB coverage even when geometry cannot write A.
    float mirrorCoverage = saturate(max(max(mirror.r, mirror.g), mirror.b) * 64.0);
    float coverage = saturate(min(border.x, border.y) * 30.0) * mirrorCoverage * WaterParameters.z;
    coverage *= 1.0 - smoothstep(0.5, 2.0, abs(baseLevel - WaterParameters.w));
    float3 skyRadiance=waterLinear(sky);
    if(offshore>0.0) skyRadiance=lerp(skyRadiance,oceanEnvironment(reflectedRay,light,daylight,environmentRoughness),offshore);
    float3 reflection = lerp(skyRadiance, waterLinear(mirror.rgb), coverage);
    // Refract the view ray through the interface and map its bed displacement into screen pixels.
    float3 incident = -view;
    float3 underwater = refract(incident, normal, 0.75);
    float2 displacement = opticalDepth * (underwater.xy / min(underwater.z, -0.1) - incident.xy / min(incident.z, -0.1));
    float2 refractionPixels = clamp(pixelOffset(displacement, dx, dy), float2(-8, -8), float2(8, 8));
    refractionPixels *= smoothstep(0.15, 2.5, opticalDepth);
    float2 refractedPixel = input.Position.xy + refractionPixels;
    float3 refractedBottom = bottomPosition(refractedPixel, world, baseLevel);
    // Reject foreground cliffs, bridge surfaces and opposite banks at the displaced coordinate.
    // In particular, don't pull their above-water colour across the water's contact edge.
    float refractedValid = step(refractedBottom.z, min(world.z,opticalWorld.z) - 0.08)
        * step(0.0, min(dot(refractedBottom-world, -view), dot(refractedBottom-opticalWorld, -view)));
    refractedPixel = lerp(input.Position.xy, refractedPixel, refractedValid);
    bottom = lerp(bottom, refractedBottom, refractedValid);
    float bedDepth = max(opticalWorld.z-bottom.z, 0.0);
    float2 sceneUV = refractedPixel * SceneParameters.yz;
    float3 scene = waterLinear(Texture5.Sample(Sampler5, saturate(sceneUV)).rgb);
    // Material detail is fixed to the *actual* submerged scene position. Keep object materials,
    // steep cliff faces, map tint, and visibility; only exposed bed terrain receives this detail.
    float bedHeight = terrainHeight(bottom.xy);
    float2 bedSlope = float2(terrainHeight(bottom.xy+float2(5,0))-terrainHeight(bottom.xy-float2(5,0)),
        terrainHeight(bottom.xy+float2(0,5))-terrainHeight(bottom.xy-float2(0,5))) * 0.1;
    float terrainMask = (1.0-smoothstep(0.6, 2.6, abs(bottom.z-bedHeight)))
        * (1.0-smoothstep(0.5, 1.6, length(bedSlope))) * TerrainParameters.w * SceneParameters.w;
    float2 bedUV = bottom.xy / 48.0;
    float4 material = Texture7.Sample(Sampler7, bedUV);
    float4 broadMaterial = Texture7.Sample(Sampler7, bedUV * 0.173 + float2(0.31,0.67));
    float gravel = saturate(WaterParameters.y * (0.25 + 0.8*broadMaterial.a) + length(bedSlope)*0.2);
    float detail = lerp(material.r / 0.55, material.g / 0.45, gravel);
    float2 bedRelief = float2(Texture7.Sample(Sampler7, bedUV+float2(1.0/256.0,0)).b - Texture7.Sample(Sampler7, bedUV-float2(1.0/256.0,0)).b,
        Texture7.Sample(Sampler7, bedUV+float2(0,1.0/256.0)).b - Texture7.Sample(Sampler7, bedUV-float2(0,1.0/256.0)).b);
    float3 bedNormal = normalize(float3(-bedSlope - bedRelief*gravel*1.8, 1.0));
    float bedLight = 0.7 + 0.45*saturate(dot(bedNormal, light));
    float rockVariation = lerp(0.82, 1.12, broadMaterial.a);
    float bedLuminance = dot(scene, float3(0.2126,0.7152,0.0722));
    scene = lerp(scene, bedLuminance*float3(1.10,1.0,0.80), terrainMask*gravel*0.38);
    scene *= lerp(1.0, clamp(detail*bedLight*rockVariation, 0.38, 1.65), terrainMask * 0.85);
    // Caustics use the evolving surface curvature at the light's entry point. Keep the modulation
    // restrained and attenuate it by depth/footprint to avoid a painted luminous network at RTS zoom.
    float3 causticWave = waterWave(bottom.xy-light.xy/max(light.z,0.2)*bedDepth, time);
    float causticFootprint = max(length(ddx(bottom.xy)), length(ddy(bottom.xy)));
    float3 causticNormal = normalize(float3(-causticWave.yz*attenuation, 1));
    float3 sunRay = refract(-light, causticNormal, 0.75);
    float2 bend = sunRay.xy / min(sunRay.z, -0.1) * bedDepth;
    float2 bx = ddx(bottom.xy), by = ddy(bottom.xy);
    float2 cx = bx+ddx(bend), cy = by+ddy(bend);
    float focused = abs(bx.x*by.y-bx.y*by.x) / max(abs(cx.x*cy.y-cx.y*cy.x),0.0001);
    float caustic = lerp(1.0, clamp(focused, 0.82, 1.55), exp(-bedDepth*0.055)
        * sunVisible * daylight / (1.0 + causticFootprint*0.45));
    // Absorption follows the underwater view-ray distance, including submerged object geometry.
    // Campaign seas can have deliberately flat, shallow terrain beneath full-size ships. Honour
    // their authored opacity offshore, while preserving the clear optical profile at coasts and
    // in enclosed pools. Actual scene ray length still governs submerged objects and refraction.
    float3 transmission = WaterOpticalTransmission(length(bottom-opticalWorld), WaterParameters.y,
        offshore, OpticalParameters.x, OpticalParameters.y);
    float3 scatter = lerp(float3(0.002, 0.028, 0.041), float3(0.0012, 0.015, 0.029), offshore)
        * (0.08 + daylight * 0.92);
    scatter=lerp(scatter,float3(0.002,0.035,0.075)*(0.08+daylight*0.92),fftWeight);
    float3 body = lerp(scatter, scene * transmission * caustic + scatter * (1.0 - transmission), SceneParameters.x);
    // The broad wave faces carry soft directional scattering; small specular ripples should not
    // be the only cue to their volume. Keep this offshore so clear river/pool materials are intact.
    float3 swellNormal=normalize(float3(-oceanSwell.yz*attenuation-correctionSlope,1));
    swellNormal=normalize(lerp(swellNormal,normal,fftWeight));
    float swellLight=saturate(dot(swellNormal,light));
    body *= lerp(1.0,0.40+1.80*swellLight,offshore);
    float foamNoise = smoothstep(0.1, 0.8, wave.x*0.5+0.5);
    float edgeDistance = depth / max(length(float2(ddx(depth), ddy(depth))), 0.01);
    float contact = (1.0 - smoothstep(0.2, 1.35, edgeDistance)) * saturate(depth * 4.0)
        * max(TerrainParameters.w, SceneParameters.w);
    float foam = contact * (0.25 + 0.75 * foamNoise) * (0.2 + 0.8 * daylight);
    // At a known hull, use its irregular wave wash instead of also drawing the generic scene
    // intersection rim. Otherwise that continuous depth edge fills every intended foam gap.
    foam *= lerp(1.0,hullEffects.z,offshore);
    // Curvature of the undamped ocean model selects breaking crests. Including attenuation in
    // this derivative would invent bright rings around a ship's damping footprint.
    float curvature = -oceanSwell.w;
    float crest = smoothstep(0.045, 0.11, curvature) * smoothstep(1.0, 2.4, oceanSwell.x);
    // A narrow band at the swell's turning slope forms a crest line, rather than filling the
    // whole high-curvature cap with a disconnected white patch. Pixel derivatives soften its
    // edges as this world-space band becomes smaller than a pixel at RTS distances.
    float crestSlope=dot(oceanSwell.yz,float2(0.9304,0.3665));
    float crestBand=1.0-smoothstep(0.025,0.085+fwidth(crestSlope)*0.65,abs(crestSlope));
    float foamPatch = oceanNoise(world.xy*0.065 + float2(time*0.06,-time*0.025));
    float foamBreakup = smoothstep(0.35, 0.7, oceanNoise(world.xy*0.38+float2(-time*0.13,time*0.08)));
    float crestFoam = crest * crestBand * smoothstep(0.53, 0.76, foamPatch) * offshore
        * foamBreakup * smoothstep(0.06, 0.25, attenuation) * (0.2+0.8*daylight);
    // Reference foam follows horizontal wave compression, with the same field as geometry.
    float fftFoam=saturate(pow(max(0.0,1.2-fftCompression),7.2))
        *smoothstep(0.15,0.65,fftDamping)*(0.2+0.8*daylight);
    // Generated bow and divergent stern crests use the same height field as the displaced mesh.
    // Stationary vessels contribute reflected incident waves, never a synthetic powered wake.
    fftFoam=max(fftFoam,spectral.vesselCrest*0.2*smoothstep(0.15,0.65,fftDamping)*(0.2+0.8*daylight));
    crestFoam=lerp(crestFoam,fftFoam,fftWeight);
    foam = max(foam, crestFoam*0.48);
    foam = max(foam,hullEffects.x*(0.2+0.8*daylight));
    // Analytic fine normals can face away from the eye between geometric vertices. Fresnel alone
    // approaches one there and paints a bright band on a hidden wave face. Smith visibility
    // smoothly removes that reflected energy while retaining the underlying water body.
    float oceanViewCosine = saturate(dot(environmentNormal,view));
    float environmentK=environmentRoughness*environmentRoughness*0.5;
    float oceanViewVisibility = oceanViewCosine / max(oceanViewCosine*(1.0-environmentK)+environmentK,0.0001);
    float environmentFresnel=0.025+0.975*pow(1.0-oceanViewCosine,5.0);
    float visibleFresnel = lerp(fresnel,environmentFresnel*oceanViewVisibility,offshore);
    float3 surface = lerp(body, reflection, visibleFresnel) + highlight;
    float3 foamColour=lerp(float3(0.68,0.76,0.74),float3(0.84,0.89,0.89),hullEffects.x);
    surface = waterDisplay(lerp(surface, waterLinear(foamColour) * (0.1 + daylight * 0.9), foam * (0.68+0.22*hullEffects.x)));
    float4 current;
)HLSL";
	if (river) {
		hlsl += "    float4 edge = Texture3.Sample(Sampler3, float2(2.0*input.TexCoord0.x, input.TexCoord0.y));\n";
		hlsl += "    current = float4(saturate(surface) * input.Diffuse.a, edge.a * saturate(depth * 2.0) * lerp(base.a, step(0.001, base.a), SceneParameters.x));\n";
	} else {
		hlsl += "    float4 edge = Texture3.Sample(Sampler3, input.TexCoord3);\n";
		hlsl += "    current = float4(saturate(surface) * edge.rgb, saturate(depth * 2.0) * lerp(input.Diffuse.a * base.a, step(0.001, input.Diffuse.a * base.a), SceneParameters.x));\n";
	}
	// Developer captures isolate the actual water inputs; normal game output is unaffected.
	const char *debug = getenv("ZH_WATER_DEBUG");
	if (debug && strcmp(debug, "depth") == 0) hlsl += "current.rgb = (1.0-exp(-depth*0.04)).xxx;\n";
	if (debug && strcmp(debug, "bed") == 0) hlsl += "current.rgb = waterDisplay(scene);\n";
	if (debug && strcmp(debug, "reflection") == 0) hlsl += "current.rgb = waterDisplay(reflection);\n";
	if (debug && strcmp(debug, "normal") == 0) hlsl += "current.rgb = normal*0.5+0.5;\n";
	if (debug && strcmp(debug, "highlight") == 0) hlsl += "current.rgb = waterDisplay(highlight);\n";
	if (debug && strcmp(debug, "offshore") == 0) hlsl += "current.rgb = offshore.xxx;\n";
	if (debug && strcmp(debug, "vessel") == 0) hlsl += "current.rgb = float3(max(0.0,-spectral.vesselHeight),max(0.0,spectral.vesselHeight),spectral.vesselCrest)*fftWeight;\n";
}

bool EngineShader_Pixel_Program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, CombinerShaderTarget target)
{
	// As the vertex half: D3D11 only, and SDL3 GPU as the D3D11 text rebound, without the smoke the
	// SDL3 backend has no map for.
	if (target == COMBINER_SHADER_TARGET_D3D9) {
		hlsl.clear();
		return false;
	}
	if (target == COMBINER_SHADER_TARGET_SDL3_GPU) {
		if (program == ENGINE_SHADER_WATER_TRAPEZOID || program == ENGINE_SHADER_WATER_RIVER) {
			write_modern_water(hlsl, program == ENGINE_SHADER_WATER_RIVER);
			if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) return false;
			hlsl += "    return current;\n}\n";
			return SDL3_Shader_Retarget(hlsl, false);
		}
		return write_engine_pixel_program(program, pipeline, hlsl, false)
			&& SDL3_Shader_Retarget(hlsl, false);
	}
	return write_engine_pixel_program(program, pipeline, hlsl, true);
}

static bool write_engine_pixel_program(EngineShaderProgram program,
	const PixelPipelineDescription & pipeline, std::string & hlsl, bool volumetric)
{
	hlsl.clear();

	const EngineShaderEntry * entry = entry_for(program);
	if (entry != NULL && entry->Opening != NULL) {
		write_multiply_chain(hlsl, *entry, volumetric);
	}
	else if (program == ENGINE_SHADER_WATER_TRAPEZOID) {
		write_trapezoid_water(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_RIVER) {
		write_river_water(hlsl);
	}
	else if (program == ENGINE_SHADER_WATER_REFLECTION) {
		write_water_reflection(hlsl);
	}
	else if (program == ENGINE_SHADER_MONOCHROME) {
		write_monochrome(hlsl);
	}
	else {
		return false;
	}

	if (!CombinerShader_Append_Pixel_Pipeline(pipeline, hlsl)) {
		return false;
	}

	hlsl +=
		"    return current;\n"
		"}\n";
	return true;
}

const char * EngineShader_Name(EngineShaderProgram program)
{
	const EngineShaderEntry * entry = entry_for(program);
	return (entry != NULL) ? entry->Name : "engine:none";
}
