// Sparse nuclear embers. No assets, gameplay particles or damage are involved.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "SdlHazardCloudEmbers.h"
#include "Platform/HazardCloud.h"
#include "WW3D2/sdl3shadercompile.h"
#include "SdlGpuFrame.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace SdlHazardCloudEmbers {
namespace {

constexpr unsigned MinimumEmbers = 24;
constexpr unsigned MaximumEmbers = 96;
constexpr float ReferenceEmberRadius = 250.0f;
SDL_GPUDevice *gpu = nullptr;
SDL_GPUShader *vertexShader = nullptr;
SDL_GPUShader *pixelShader = nullptr;
SDL_GPUGraphicsPipeline *pipeline = nullptr;
SDL_GPUSampler *depthSampler = nullptr;
bool failed = false;
unsigned renderedFrames = 0;

struct VertexConstants {
    float viewProjection[16];
    float viewport[4];
    float field[4];
    float shape[4];
    float scene[4];
    float terrain[4]; // enabled, field index, sampled domain radius, reserved
};
static_assert(sizeof(VertexConstants) == 144, "ember shader constant layout");

bool fail(const char *reason)
{
    if (!failed) std::fprintf(stderr, "hazard-cloud: nuclear embers FAIL: %s\n", reason);
    failed = true;
    return false;
}

const char *vertexSource = R"HLSL(
Texture2D<float> TerrainHeight : register(t0, space0);
SamplerState TerrainSampler : register(s0, space0);
cbuffer EmberConstants : register(b0, space1)
{
    row_major float4x4 ViewProjection;
    float4 Viewport;
    float4 Field;
    float4 Shape;
    float4 Scene;
    float4 Terrain;
};
struct EmberVertex {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float2 glow : TEXCOORD1;
};
float emberHash(float seed)
{
    float3 p = frac(float3(seed, seed + 1.0, seed + 2.0) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float emberGround(float2 world)
{
    if (Terrain.x < 0.5) return Field.z;
    uint width, height;
    TerrainHeight.GetDimensions(width, height);
    float side = float(height);
    float2 uv = saturate((world - Field.xy) / max(Terrain.z * 2.0, 0.001) + 0.5);
    float2 texel = uv * (side - 1.0);
    float2 lower = floor(texel), upper = min(lower + 1.0, side - 1.0);
    float2 weight = frac(texel);
    float2 a = (lower + float2(Terrain.y * side, 0) + 0.5) / float2(width, height);
    float2 b = (upper + float2(Terrain.y * side, 0) + 0.5) / float2(width, height);
    float low = lerp(TerrainHeight.SampleLevel(TerrainSampler, a, 0),
        TerrainHeight.SampleLevel(TerrainSampler, float2(b.x, a.y), 0), weight.x);
    float high = lerp(TerrainHeight.SampleLevel(TerrainSampler, float2(a.x, b.y), 0),
        TerrainHeight.SampleLevel(TerrainSampler, b, 0), weight.x);
    return lerp(low, high, weight.y);
}
EmberVertex main(uint vertex : SV_VertexID)
{
    uint particle = vertex / 6;
    uint corner = vertex % 6;
    float2 local;
    if (corner == 0 || corner == 3) local = float2(-1, -1);
    else if (corner == 1) local = float2(1, -1);
    else if (corner == 2 || corner == 4) local = float2(1, 1);
    else local = float2(-1, 1);

    float seed = float(particle) * 13.61 + Field.x * 0.21 + Field.y * 0.37;
    float random = emberHash(seed + 7.0);
    float lifetime = 3.4 + emberHash(seed + 2.0) * 2.8;
    float age = frac(Scene.x / lifetime + emberHash(seed + 19.0));
    // One anchor per equal-area radial stratum, separated by the golden angle.
    // Slight jitter breaks the spiral's regularity without random clustering.
    // Field.w includes the cloud's visual fringe. Its whole footprint ignites
    // together; formation controls opacity rather than moving sparks outward.
    float rotation = emberHash(Field.x * 0.21 + Field.y * 0.37 + 3.0) * 6.2831853;
    float angle = float(particle) * 2.39996323 + rotation
        + (emberHash(seed + 11.0) - 0.5) * 0.12;
    float stratum = (float(particle) + 0.35 + emberHash(seed + 1.0) * 0.30) / max(Scene.y, 1.0);
    float radius = sqrt(stratum) * Field.w;
    float2 origin = Field.xy + float2(cos(angle), sin(angle)) * radius;
    float2 drift = float2(sin(age * 5.0 + seed), cos(age * 4.0 + seed * 0.7));
    drift *= (2.0 + random * 5.0) * age;
    drift += float2(3.0, -1.0) * age * age;
    float height = Shape.x * (0.36 + age * (1.8 + random * 0.6));
    float3 world = float3(origin + drift, emberGround(origin + drift) + height);
    float4 clip = mul(float4(world, 1), ViewProjection);
    float4 upClip = mul(float4(world + float3(0, 0, 1), 1), ViewProjection);
    float2 up = (upClip.xy / max(upClip.w, 0.0001) - clip.xy / max(clip.w, 0.0001))
        * Viewport.zw * float2(0.5, -0.5);
    up = dot(up, up) > 0.00001 ? normalize(up) : float2(0, -1);
    float2 across = float2(-up.y, up.x);
    // The bright core is roughly 1-2 pixels across at the default view, with
    // a softer orange halo and a short tail pointing down its rising path.
    float zoom = clamp(sqrt(max(Viewport.z * Viewport.w, 1.0) / 1024000.0), 0.8, 1.7);
    float2 extent = float2(2.3 + random * 1.1, 3.0 + random * 2.5) * zoom;
    float2 pixelOffset = across * local.x * extent.x + up * local.y * extent.y;
    clip.xy += pixelOffset * float2(2, -2) / max(Viewport.zw, float2(1, 1)) * clip.w;
    float fade = smoothstep(0.0, 0.14, age) * (1.0 - smoothstep(0.58, 1.0, age));
    fade *= saturate(Shape.y * 1.6) * smoothstep(0.02, 0.28, Shape.z);
    fade *= lerp(0.58, 1.0, random) * lerp(0.55, 1.0, smoothstep(0.5, 1.1, height / Shape.x));
    // Keep the outer annulus legible. Only the last few percent of outward
    // drift die away, so the soft cloud fringe does not become a hard spark ring.
    float edgeRadius = length(world.xy - Field.xy) / max(Field.w, 0.001);
    fade *= (1.0 - 0.40 * smoothstep(0.88, 1.0, edgeRadius))
        * (1.0 - smoothstep(1.0, 1.055, edgeRadius));
    EmberVertex output;
    output.position = clip;
    output.local = local;
    output.glow = float2(fade, random);
    return output;
}
)HLSL";

const char *pixelSource = R"HLSL(
Texture2D<float> SceneDepth : register(t0, space2);
SamplerState DepthSampler : register(s0, space2);
cbuffer EmberPixelConstants : register(b0, space3) { float4 Scene; };
struct EmberVertex {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float2 glow : TEXCOORD1;
};
float4 main(EmberVertex input) : SV_Target
{
    float depth = SceneDepth.SampleLevel(DepthSampler, input.position.xy * Scene.xy, 0);
    // Use the same captured opaque depth as the volume: terrain and infantry
    // hide sparks behind them, while this pass never writes the scene depth.
    clip(depth - input.position.z + 0.0000005);
    float2 corePoint = (input.local - float2(0, 0.30)) * float2(1, 1.6);
    float squared = dot(corePoint, corePoint);
    float core = exp2(-squared * 12.0);
    float halo = exp2(-squared * 3.0);
    float tail = (1.0 - smoothstep(0.10, 0.34, abs(input.local.x)))
        * smoothstep(-1.0, -0.30, input.local.y)
        * (1.0 - smoothstep(-0.18, 0.30, input.local.y));
    float edge = (1.0 - smoothstep(0.80, 1.0, abs(input.local.x)))
        * (1.0 - smoothstep(0.85, 1.0, abs(input.local.y)));
    float3 warm = lerp(float3(1.0, 0.21, 0.025), float3(1.0, 0.43, 0.08), input.glow.y);
    float3 light = float3(1.0, 0.80, 0.43) * core * 0.95 + warm * (halo * 0.30 + tail * 0.17);
    return float4(light * input.glow.x * edge, 0);
}
)HLSL";

bool makeResources(SdlGpuFrame *frame, const SdlTarget &target)
{
    gpu = frame->Device();
    std::vector<unsigned char> spirv;
    std::string log;
    if (!SDL3_Compile_HLSL_To_SPIRV(vertexSource, true, spirv, log)
        || !(vertexShader = SDL3_Create_Shader(gpu, spirv, true, log)))
        return fail(("vertex shader: " + log).c_str());
    if (!SDL3_Compile_HLSL_To_SPIRV(pixelSource, false, spirv, log)
        || !(pixelShader = SDL3_Create_Shader(gpu, spirv, false, log)))
        return fail(("pixel shader: " + log).c_str());
    SDL_GPUColorTargetDescription colour = {};
    colour.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
    colour.blend_state.enable_blend = true;
    colour.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo description = {};
    description.vertex_shader = vertexShader;
    description.fragment_shader = pixelShader;
    description.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    description.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    description.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    description.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    description.depth_stencil_state.enable_depth_test = false;
    description.depth_stencil_state.enable_depth_write = false;
    description.target_info.color_target_descriptions = &colour;
    description.target_info.num_color_targets = 1;
    description.target_info.has_depth_stencil_target = true;
    // Clouds render only into the main scene. Its implicit and mirrored depth
    // surfaces use the frame's scene format, independently of shadow targets.
    description.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(frame->Depth_Format());
    pipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!pipeline) return fail(SDL_GetError());
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w
        = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    depthSampler = SDL_CreateGPUSampler(gpu, &sampler);
    if (!depthSampler) return fail(SDL_GetError());
    return true;
}

} // namespace

bool prepare(SdlGpuFrame *frame, const SdlTarget &target,
             const HazardCloud::FieldState *fields, unsigned fieldCount)
{
    if (failed || !frame || !frame->Device() || !target.Colour || !target.Depth
        || fieldCount > 2 || (fieldCount && !fields)) return false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const auto &field = fields[i];
        if (field.palette < 0.5f || field.palette >= 1.5f || field.density <= 0.00001f
            || field.radius <= 0.001f || field.height <= 0.002f || field.formation <= 0.02f) continue;
        if (!pipeline && !makeResources(frame, target)) return false;
        if (gpu != frame->Device()) return fail("the graphics device was replaced");
    }
    return true;
}

bool render(SdlGpuFrame *frame, const SdlTarget &target, SDL_GPUTexture *sceneDepth,
            const float viewProjection[16], const float viewport[6],
            const HazardCloud::FieldState *fields, unsigned fieldCount,
            float timeSeconds, SDL_GPUTexture *terrainTexture, const bool *terrainEnabled)
{
    if (failed) return false;
    if (!frame || !frame->Device() || !target.Colour || !target.Depth || !sceneDepth
        || !target.Width || !target.Height || !viewProjection || !viewport
        || fieldCount > 2 || (fieldCount && !fields) || !std::isfinite(timeSeconds))
        return fail("invalid rendering inputs");
    bool active = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const auto &field = fields[i];
        if (field.palette < 0.5f || field.palette >= 1.5f || field.density <= 0.00001f
            || field.radius <= 0.001f || field.height <= 0.002f || field.formation <= 0.02f) continue;
        if (!pipeline && !makeResources(frame, target)) return false;
        if (gpu != frame->Device()) return fail("the preview graphics device was replaced");
        const float footprintRadius = field.radius + field.fringe;
        const float areaRatio = footprintRadius * footprintRadius
            / (ReferenceEmberRadius * ReferenceEmberRadius);
        // Match particle density to visible area: 64 at the comparison size,
        // fewer for cannon residue and at most 96 for the large missile field.
        const unsigned emberCount = static_cast<unsigned>(std::clamp(
            std::round(64.0f * areaRatio / 8.0f) * 8.0f,
            static_cast<float>(MinimumEmbers), static_cast<float>(MaximumEmbers)));
        VertexConstants constants = {};
        std::copy(viewProjection, viewProjection + 16, constants.viewProjection);
        std::copy(viewport, viewport + 4, constants.viewport);
        constants.field[0] = field.ground.x;
        constants.field[1] = field.ground.y;
        constants.field[2] = field.ground.z;
        constants.field[3] = footprintRadius;
        constants.shape[0] = field.height;
        constants.shape[1] = field.density;
        constants.shape[2] = field.formation;
        constants.scene[0] = timeSeconds;
        constants.scene[1] = static_cast<float>(emberCount);
        constants.terrain[0] = terrainTexture && terrainEnabled && terrainEnabled[i] ? 1.0f : 0.0f;
        constants.terrain[1] = static_cast<float>(i);
        constants.terrain[2] = footprintRadius + 18.0f;
        const float pixelConstants[4] = { 1.0f / target.Width, 1.0f / target.Height, 0, 0 };
        SdlRecordedDraw draw = {};
        draw.Pipeline = pipeline;
        // SV_VertexID creates each billboard; the common batch still binds a
        // valid staging buffer even though this pipeline has no vertex inputs.
        std::memset(frame->Stage(16, draw.VertexOffset), 0, 16);
        draw.Count = emberCount * 6;
        draw.VertexConstants = frame->Constants(0, &constants, sizeof(constants));
        draw.VertexConstantsSize = sizeof(constants);
        draw.PixelConstants = frame->Constants(1, pixelConstants, sizeof(pixelConstants));
        draw.PixelConstantsSize = sizeof(pixelConstants);
        draw.VertexSamplerCount = 1;
        draw.VertexTextures[0] = terrainTexture ? terrainTexture : sceneDepth;
        draw.VertexSamplers[0] = depthSampler;
        draw.SamplerCount = 1;
        draw.Textures[0] = sceneDepth;
        draw.Samplers[0] = depthSampler;
        std::copy(viewport, viewport + 6, draw.Viewport);
        draw.Scissor[0] = static_cast<int32_t>(std::clamp(viewport[0], 0.0f, static_cast<float>(target.Width)));
        draw.Scissor[1] = static_cast<int32_t>(std::clamp(viewport[1], 0.0f, static_cast<float>(target.Height)));
        draw.Scissor[2] = static_cast<int32_t>(std::min(viewport[2], static_cast<float>(target.Width - draw.Scissor[0])));
        draw.Scissor[3] = static_cast<int32_t>(std::min(viewport[3], static_cast<float>(target.Height - draw.Scissor[1])));
        frame->Set_Target(target);
        frame->Record_Draw(draw);
        active = true;
    }
    if (active) ++renderedFrames;
    return true;
}

bool validationPassed() { return !failed; }

void shutdown()
{
    if (gpu) {
        if (pipeline) SDL_ReleaseGPUGraphicsPipeline(gpu, pipeline);
        if (depthSampler) SDL_ReleaseGPUSampler(gpu, depthSampler);
        if (vertexShader) SDL_ReleaseGPUShader(gpu, vertexShader);
        if (pixelShader) SDL_ReleaseGPUShader(gpu, pixelShader);
    }
    abandon();
}

void abandon()
{
    gpu = nullptr;
    vertexShader = pixelShader = nullptr;
    pipeline = nullptr;
    depthSampler = nullptr;
    renderedFrames = 0;
    failed = false;
}

} // namespace SdlHazardCloudEmbers
