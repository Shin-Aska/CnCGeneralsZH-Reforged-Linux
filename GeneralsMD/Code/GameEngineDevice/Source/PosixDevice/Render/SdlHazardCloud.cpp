// Shared world-space hazard clouds; game and isolated preview use one renderer.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "Platform/SdlHazardCloud.h"
#include "SdlHazardCloudShader.h"
#include "SdlHazardCloudEmbers.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/sdl3shadercompile.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlRenderMatrix.h"
#include "SdlResourceMirror.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace SdlHazardCloud {
namespace {
const float Steps = 96.0f;
const float MaximumDisplacement = 18.0f;
const unsigned MaximumBodies = 32;
const SDL_GPUTextureFormat LowTargetFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
const SDL_GPUTextureUsageFlags LowTargetUsage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
const SDL_GPUTextureFormat DensityTargetFormat = SDL_GPU_TEXTUREFORMAT_R16_FLOAT;
const unsigned CacheSlices = 24;
const unsigned CacheColumns = 6;
const unsigned CacheRows = CacheSlices / CacheColumns;
struct FieldCache {
    SdlTarget density = {};
    SdlTarget lighting = {};
    unsigned side = 0;
};
struct SharedResources {
    SdlGpuFrame *frame = nullptr;
    SDL_GPUDevice *gpu = nullptr;
    unsigned users = 0;
    SDL_GPUShader *vertexShader = nullptr;
    SDL_GPUShader *pixelShader = nullptr;
    SDL_GPUShader *compositePixelShader = nullptr;
    SDL_GPUShader *heatPixelShader = nullptr;
    SDL_GPUShader *densityPixelShader = nullptr;
    SDL_GPUShader *lightingPixelShader = nullptr;
    SDL_GPUGraphicsPipeline *volumePipeline = nullptr;
    SDL_GPUGraphicsPipeline *compositePipeline = nullptr;
    SDL_GPUGraphicsPipeline *heatPipeline = nullptr;
    SDL_GPUGraphicsPipeline *densityPipeline = nullptr;
    SDL_GPUGraphicsPipeline *lightingPipeline = nullptr;
    SDL_GPUSampler *depthSampler = nullptr;
    SDL_GPUSampler *flowSampler = nullptr;
    SDL_GPUSampler *cacheSampler = nullptr;
    SdlTarget lowTarget = {};
    SdlTarget heatBackground = {};
};
SharedResources shared;
constexpr std::array<Palette, 4> approved = {{
    {{0.769999981f, 0.939999998f, 0.550000012f}, {0.125490203f, 0.31764707f, 0.0549019612f}, 0.699999988f, 0.670000017f},
    {{0.749019623f, 0.670588255f, 0.469999999f}, {0.298039228f, 0.184313729f, 0.0705882385f}, 1.29999995f, 0.0f},
    {{0.660000026f, 0.75999999f, 1.0f}, {0.0649999976f, 0.100000001f, 0.209999993f}, 1.52999997f, 0.769999981f},
    {{0.839999974f, 0.629999995f, 1.0f}, {0.192156866f, 0.0470588244f, 0.235294119f}, 1.32000005f, 0.74000001f},
}};

unsigned cacheSide(const FieldState &field)
{
    const float extent = 2.0f * (field.radius + field.fringe + MaximumDisplacement);
    const unsigned samples = static_cast<unsigned>(std::clamp(std::ceil(extent / 2.0f) + 1.0f, 32.0f, 512.0f));
    return (samples + 15u) & ~15u;
}

void releaseShared(bool deviceAlive)
{
    if (deviceAlive && shared.frame && shared.gpu) {
        if (shared.lowTarget.Colour) shared.frame->Release_After_Batch(shared.lowTarget.Colour, nullptr);
        if (shared.heatBackground.Colour) shared.frame->Release_After_Batch(shared.heatBackground.Colour, nullptr);
        // Last layer only: pipelines cannot retire through Release_After_Batch.
        shared.frame->Flush();
        SDL_WaitForGPUIdle(shared.gpu);
        SdlHazardCloudEmbers::shutdown();
        if (shared.vertexShader) SDL_ReleaseGPUShader(shared.gpu, shared.vertexShader);
        if (shared.pixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.pixelShader);
        if (shared.compositePixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.compositePixelShader);
        if (shared.heatPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.heatPixelShader);
        if (shared.densityPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.densityPixelShader);
        if (shared.lightingPixelShader) SDL_ReleaseGPUShader(shared.gpu, shared.lightingPixelShader);
        if (shared.volumePipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.volumePipeline);
        if (shared.compositePipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.compositePipeline);
        if (shared.heatPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.heatPipeline);
        if (shared.densityPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.densityPipeline);
        if (shared.lightingPipeline) SDL_ReleaseGPUGraphicsPipeline(shared.gpu, shared.lightingPipeline);
        if (shared.depthSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.depthSampler);
        if (shared.flowSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.flowSampler);
        if (shared.cacheSampler) SDL_ReleaseGPUSampler(shared.gpu, shared.cacheSampler);
    } else {
        // The device owns native allocations; after its destruction only forget
        // our stale handles, never call through a dangling GPU/frame pointer.
        SdlHazardCloudEmbers::abandon();
    }
    shared = {};
}
} // namespace

const Palette &defaultPalette(unsigned index) { return approved[index < 4 ? index : 0]; }

struct Layer::Impl {
    FieldState fieldStates[2] = {};
    unsigned fieldCount = 0;
    float sourceTravel[2] = {}, sourceAge[2] = {}, peakDensity[2] = {};
    float simulationTime = 0;
    bool hasSimulationTime = false;
    bool initialized = false;
    bool interactionEnabled = true;
    bool heatEnabled = true;
    bool flowDirty = true;
    bool failed = false;
    bool explicitDomain = false;
    unsigned renderedFrames = 0;
    unsigned flowUploads = 0;
    uint64_t flowUsedBatch = ~uint64_t(0);
    std::vector<Body> bodies;
    SdlHazardCloudFlow::Flow flow;
    std::array<Palette, 4> palettes = approved;
    SdlGpuFrame *frame = nullptr;
    SDL_GPUDevice *gpu = nullptr;
    SDL_GPUTexture *flowTexture = nullptr;
    SDL_GPUTexture *terrainTexture = nullptr;
    std::vector<float> terrainPixels = std::vector<float>(TerrainGridSide * TerrainGridSide * 2, 0.0f);
    bool terrainEnabled[2] = {};
    float terrainMin[2] = {}, terrainMax[2] = {};
    bool terrainDirty = false;
    FieldCache fieldCaches[2];
    SdlHazardCloudUniforms cachedInputs = {};
    bool cachedInputsValid = false;
    unsigned cachedFlowUploads = 0;
    unsigned cacheUpdates = 0;
    unsigned cacheBakes = 0;

void fail(const char *reason)
{
    if (!failed) std::fprintf(stderr, "cloud-preview: volumetric renderer FAIL: %s\n", reason);
    failed = true;
}

bool makeResources()
{
    std::vector<unsigned char> spirv;
    std::string log;
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudVertexShader(), true, spirv, log)
        || !(shared.vertexShader = SDL3_Create_Shader(gpu, spirv, true, log))) {
        fail(("vertex shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudPixelShader(false), false, spirv, log)
        || !(shared.pixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudPixelShader(true), false, spirv, log)
        || !(shared.compositePixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume composite shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudHeatShader(), false, spirv, log)
        || !(shared.heatPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume heat shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudCacheShader(false), false, spirv, log)
        || !(shared.densityPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume density cache shader: " + log).c_str());
        return false;
    }
    if (!SDL3_Compile_HLSL_To_SPIRV(SdlHazardCloudCacheShader(true), false, spirv, log)
        || !(shared.lightingPixelShader = SDL3_Create_Shader(gpu, spirv, false, log))) {
        fail(("volume lighting cache shader: " + log).c_str());
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(gpu, LowTargetFormat, SDL_GPU_TEXTURETYPE_2D, LowTargetUsage)) {
        fail("RGBA16F cloud rendering and texture sampling are unavailable");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(gpu, DensityTargetFormat, SDL_GPU_TEXTURETYPE_2D, LowTargetUsage)) {
        fail("R16F cloud density rendering and texture sampling are unavailable");
        return false;
    }
    SDL_GPUColorTargetDescription target = {};
    target.format = LowTargetFormat;
    SDL_GPUGraphicsPipelineCreateInfo description = {};
    description.vertex_shader = shared.vertexShader;
    description.fragment_shader = shared.pixelShader;
    description.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    description.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    description.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    // The volume clips its ray against the sampled opaque scene depth. Its
    // fullscreen proxy neither tests nor modifies the real depth attachment.
    description.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
    description.depth_stencil_state.enable_depth_test = false;
    description.depth_stencil_state.enable_depth_write = false;
    description.target_info.color_target_descriptions = &target;
    description.target_info.num_color_targets = 1;
    // The low pass owns only a colour target. Its shader reads the captured
    // full-resolution depth and writes premultiplied radiance without blending.
    shared.volumePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.volumePipeline) { fail(SDL_GetError()); return false; }
    target.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    description.fragment_shader = shared.compositePixelShader;
    description.target_info.has_depth_stencil_target = true;
    description.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(frame->Depth_Format());
    shared.compositePipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.compositePipeline) { fail(SDL_GetError()); return false; }
    description.fragment_shader = shared.heatPixelShader;
    shared.heatPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.heatPipeline) { fail(SDL_GetError()); return false; }
    target = {};
    target.format = DensityTargetFormat;
    description.fragment_shader = shared.densityPixelShader;
    description.target_info.has_depth_stencil_target = false;
    description.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_INVALID;
    shared.densityPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.densityPipeline) { fail(SDL_GetError()); return false; }
    target.format = LowTargetFormat;
    description.fragment_shader = shared.lightingPixelShader;
    shared.lightingPipeline = SDL_CreateGPUGraphicsPipeline(gpu, &description);
    if (!shared.lightingPipeline) { fail(SDL_GetError()); return false; }
    SDL_GPUSamplerCreateInfo sampler = {};
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w
        = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    shared.depthSampler = SDL_CreateGPUSampler(gpu, &sampler);
    // Float32 linear filtering is optional on some backends. The volume shader
    // performs bilinear interpolation explicitly using this point sampler.
    shared.flowSampler = SDL_CreateGPUSampler(gpu, &sampler);
    // The atlas shader clamps XY to individual slice texel centres before the
    // bilinear lookup; it interpolates Z explicitly between adjacent slices.
    sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    shared.cacheSampler = SDL_CreateGPUSampler(gpu, &sampler);
    if (!shared.depthSampler || !shared.flowSampler || !shared.cacheSampler) {
        fail(SDL_GetError());
        return false;
    }
    return true;
}

bool makeFlowTexture()
{
    const SDL_GPUTextureFormat flowFormat = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    const SDL_GPUTextureUsageFlags flowUsage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (!SDL_GPUTextureSupportsFormat(gpu, flowFormat, SDL_GPU_TEXTURETYPE_2D, flowUsage)) {
        fail("RGBA32F interaction texture sampling is unavailable");
        return false;
    }
    SDL_GPUTextureCreateInfo flowDescription = {};
    flowDescription.type = SDL_GPU_TEXTURETYPE_2D;
    flowDescription.format = flowFormat;
    flowDescription.usage = flowUsage;
    flowDescription.width = flow.width();
    flowDescription.height = flow.height();
    flowDescription.layer_count_or_depth = flowDescription.num_levels = 1;
    flowTexture = SDL_CreateGPUTexture(gpu, &flowDescription);
    if (!flowTexture) {
        fail(SDL_GetError());
        return false;
    }
    return true;
}

bool resizeLowTarget(unsigned width, unsigned height)
{
    const unsigned lowWidth = width / 2 + width % 2;
    const unsigned lowHeight = height / 2 + height % 2;
    if (shared.lowTarget.Colour && shared.lowTarget.Width == lowWidth && shared.lowTarget.Height == lowHeight) return true;
    SDL_GPUTextureCreateInfo description = {};
    description.type = SDL_GPU_TEXTURETYPE_2D;
    description.format = LowTargetFormat;
    description.usage = LowTargetUsage;
    description.width = lowWidth;
    description.height = lowHeight;
    description.layer_count_or_depth = description.num_levels = 1;
    description.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu, &description);
    if (!texture) { fail(SDL_GetError()); return false; }
    // Recorded passes retain the old texture until this batch is submitted;
    // SDL then keeps it alive until the GPU has finished those users.
    if (shared.lowTarget.Colour) frame->Release_After_Batch(shared.lowTarget.Colour, nullptr);
    shared.lowTarget = { texture, nullptr, lowWidth, lowHeight, 0 };
    return true;
}

bool prepareHeatBackground(const SdlTarget &target)
{
    if (!shared.heatBackground.Colour || shared.heatBackground.Width != target.Width || shared.heatBackground.Height != target.Height) {
        SDL_GPUTextureCreateInfo description = {};
        description.type = SDL_GPU_TEXTURETYPE_2D;
        description.format = static_cast<SDL_GPUTextureFormat>(SdlGpuFrame::Target_Format());
        description.usage = LowTargetUsage;
        description.width = target.Width;
        description.height = target.Height;
        description.layer_count_or_depth = description.num_levels = 1;
        description.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture *texture = SDL_CreateGPUTexture(gpu, &description);
        if (!texture) { fail(SDL_GetError()); return false; }
        if (shared.heatBackground.Colour) frame->Release_After_Batch(shared.heatBackground.Colour, nullptr);
        shared.heatBackground = { texture, nullptr, target.Width, target.Height, 0 };
    }
    return true;
}

void captureHeatBackground(const SdlTarget &target)
{
    // Capture the actual scene at this draw, not a possibly earlier pre-water
    // snapshot. The queued blit separates attachment writes from shader reads;
    // it requires neither a readback nor an extra GPU submission/wait.
    const int32_t rectangle[4] = { 0, 0, static_cast<int32_t>(target.Width), static_cast<int32_t>(target.Height) };
    frame->Record_Blit(target.Colour, rectangle, shared.heatBackground.Colour, rectangle, false);
}

void retireCache(FieldCache &cache)
{
    if (cache.density.Colour) frame->Release_After_Batch(cache.density.Colour, nullptr);
    if (cache.lighting.Colour) frame->Release_After_Batch(cache.lighting.Colour, nullptr);
    cache = {};
}

bool resizeFieldCache(unsigned index)
{
    const FieldState &field = fieldStates[index];
    const float extent = 2.0f * (field.radius + field.fringe + MaximumDisplacement);
    // Include both domain boundaries, retain at most two world units between
    // grid nodes, and round the dimension up to a small allocation granularity.
    const unsigned side = cacheSide(field);
    FieldCache &cache = fieldCaches[index];
    if (cache.side == side && cache.density.Colour && cache.lighting.Colour) return true;
    SDL_GPUTextureCreateInfo description = {};
    description.type = SDL_GPU_TEXTURETYPE_2D;
    description.format = DensityTargetFormat;
    description.usage = LowTargetUsage;
    description.width = side * CacheColumns;
    description.height = side * CacheRows;
    description.layer_count_or_depth = description.num_levels = 1;
    description.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *density = SDL_CreateGPUTexture(gpu, &description);
    if (!density) { fail(SDL_GetError()); return false; }
    description.format = LowTargetFormat;
    SDL_GPUTexture *lighting = SDL_CreateGPUTexture(gpu, &description);
    if (!lighting) {
        fail(SDL_GetError());
        SDL_ReleaseGPUTexture(gpu, density);
        return false;
    }
    retireCache(cache);
    cache.density = { density, nullptr, description.width, description.height, 0 };
    cache.lighting = { lighting, nullptr, description.width, description.height, 0 };
    cache.side = side;
    cachedInputsValid = false;
    std::fprintf(stderr, "cloud-preview: volume-cache field=%u grid=%ux%ux%u atlas=%ux%u spacing=%.3f\n",
        index, side, side, CacheSlices, description.width, description.height, extent / (side - 1));
    return true;
}

void bindCachedTextures(SdlRecordedDraw &draw, bool density)
{
    for (unsigned i = 0; i < 2; ++i) {
        SDL_GPUTexture *texture = density ? fieldCaches[i].density.Colour : fieldCaches[i].lighting.Colour;
        // An inactive/unallocated field never samples its cache. Still bind a
        // valid 2D texture, with a point sampler for the RGBA32F placeholder.
        draw.Textures[3 + i] = texture ? texture : flowTexture;
        draw.Samplers[3 + i] = texture ? shared.cacheSampler : shared.flowSampler;
    }
}

bool prepareCaches(SdlHazardCloudUniforms &uniforms, SDL_GPUTexture *sceneDepth,
                   uint32_t vertexOffset, const SdlTarget &restoreTarget)
{
    bool active[2] = {};
    float *grids[] = { uniforms.Cache0, uniforms.Cache1 };
    for (unsigned i = 0; i < fieldCount; ++i) {
        const FieldState &field = fieldStates[i];
        active[i] = field.radius > 0.001f && field.height > 0.002f && field.density > 0.00001f;
        if (!active[i]) continue;
        if (!resizeFieldCache(i)) return false;
        grids[i][0] = grids[i][1] = static_cast<float>(fieldCaches[i].side);
        grids[i][2] = static_cast<float>(CacheSlices);
        grids[i][3] = static_cast<float>(CacheColumns);
    }
    // Exclude all view-dependent values from the cache key. Orbiting, zooming,
    // resizing or drawing a paused frame can reuse the same world-space volume.
    SdlHazardCloudUniforms inputs = uniforms;
    std::fill(inputs.WorldFromClip, inputs.WorldFromClip + 16, 0.0f);
    std::fill(inputs.CameraPosition, inputs.CameraPosition + 4, 0.0f);
    std::fill(inputs.Viewport, inputs.Viewport + 4, 0.0f);
    inputs.Scene[0] = inputs.Scene[1] = inputs.Scene[2] = 0;
    inputs.Settings[1] = inputs.Settings[3] = 0;
    std::fill(inputs.CachePass, inputs.CachePass + 4, 0.0f);
    // Preview-only stress measurement: include full cache construction in every
    // rendered frame, even when rendering outruns simulation. This bounds the
    // cost when other effects leave only one render per simulation update.
    static const bool forceRebuild = []() {
        const char *value = std::getenv("ZH_CLOUD_PREVIEW_REBUILD_EVERY_FRAME");
        return value && value[0] == '1';
    }();
    if (!forceRebuild && cachedInputsValid && cachedFlowUploads == flowUploads
        && std::memcmp(&inputs, &cachedInputs, sizeof(inputs)) == 0) return true;

    bool rebuilt = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        if (!active[i]) continue;
        const FieldCache &cache = fieldCaches[i];
        SdlHazardCloudUniforms bake = uniforms;
        bake.CachePass[0] = static_cast<float>(i);
        SdlRecordedDraw draw = {};
        draw.Pipeline = shared.densityPipeline;
        draw.VertexOffset = vertexOffset;
        draw.Count = 3;
        draw.PixelConstants = frame->Constants(1, &bake, sizeof(bake));
        draw.PixelConstantsSize = sizeof(bake);
        // Shader reflection can retain declared atlas bindings even when this
        // variant does not read them. Supply all five slots, with harmless
        // placeholders that never alias the density attachment being written.
        draw.SamplerCount = 5;
        bindTerrain(draw);
        draw.Textures[0] = sceneDepth;
        draw.Samplers[0] = shared.depthSampler;
        draw.Textures[1] = flowTexture;
        draw.Samplers[1] = shared.flowSampler;
        for (unsigned slot = 2; slot < 5; ++slot) {
            draw.Textures[slot] = flowTexture;
            draw.Samplers[slot] = shared.flowSampler;
        }
        draw.Viewport[2] = static_cast<float>(cache.density.Width);
        draw.Viewport[3] = static_cast<float>(cache.density.Height);
        draw.Viewport[5] = 1;
        draw.Scissor[2] = static_cast<int32_t>(cache.density.Width);
        draw.Scissor[3] = static_cast<int32_t>(cache.density.Height);
        frame->Set_Target(cache.density);
        frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
        frame->Record_Draw(draw);

        // Switching targets ends the density-writing pass before the lighting
        // pass reads it. The later camera pass similarly reads completed light.
        draw.Pipeline = shared.lightingPipeline;
        draw.SamplerCount = 5;
        bindTerrain(draw);
        draw.Textures[2] = flowTexture;
        draw.Samplers[2] = shared.flowSampler;
        bindCachedTextures(draw, true);
        frame->Set_Target(cache.lighting);
        frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
        frame->Record_Draw(draw);
        ++cacheBakes;
        rebuilt = true;
    }
    frame->Set_Target(restoreTarget);
    cachedInputs = inputs;
    cachedFlowUploads = flowUploads;
    cachedInputsValid = true;
    if (rebuilt) ++cacheUpdates;
    return true;
}

bool uploadTerrain()
{
    if (!terrainEnabled[0] && !terrainEnabled[1]) return true;
    if (!terrainTexture) {
        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = TerrainGridSide * 2;
        info.height = TerrainGridSide;
        info.layer_count_or_depth = info.num_levels = 1;
        if (!SDL_GPUTextureSupportsFormat(gpu, info.format, info.type, info.usage)
            || !(terrainTexture = SDL_CreateGPUTexture(gpu, &info))) {
            fail("terrain height texture is unavailable");
            return false;
        }
        terrainDirty = true;
    }
    if (!terrainDirty) return true;
    // Uploads precede every recorded draw in a batch; flush this layer's old
    // readers before replacing heights, just as for its independent flow map.
    if (flowUsedBatch == frame->Batch() && !frame->Flush()) {
        fail("could not submit preceding terrain texture users");
        return false;
    }
    const uint32_t bytes = static_cast<uint32_t>(terrainPixels.size() * sizeof(float));
    uint32_t offset = 0;
    std::memcpy(frame->Upload_Space(bytes, offset), terrainPixels.data(), bytes);
    frame->Queue_Texture_Upload(terrainTexture, 0, TerrainGridSide * 2, TerrainGridSide, offset);
    terrainDirty = false;
    cachedInputsValid = false;
    return true;
}

void bindTerrain(SdlRecordedDraw &draw)
{
    draw.SamplerCount = 7;
    draw.Textures[5] = flowTexture;
    draw.Samplers[5] = shared.flowSampler;
    draw.Textures[6] = terrainTexture ? terrainTexture : flowTexture;
    draw.Samplers[6] = shared.flowSampler;
}

bool uploadFlow()
{
    if (!flowDirty) return true;
    if (!flow.valid()) {
        fail("interaction simulation produced invalid state");
        return false;
    }
    // Batch uploads run before all its draws. Submit any already-recorded user
    // before replacing the texture, otherwise that draw would read a later step.
    // Level-zero upload cycling then protects users in earlier submitted batches.
    if (flowUsedBatch == frame->Batch() && !frame->Flush()) {
        fail("could not submit preceding interaction texture users");
        return false;
    }
    const uint32_t bytes = flow.width() * flow.height() * 4 * sizeof(float);
    uint32_t offset = 0;
    std::memcpy(frame->Upload_Space(bytes, offset), flow.data(), bytes);
    frame->Queue_Texture_Upload(flowTexture, 0, flow.width(), flow.height(), offset);
    flowDirty = false;
    ++flowUploads;
    return true;
}

bool resolveSceneTarget(PosixDevice9 *device, SdlTarget &target)
{
    IDirect3DSurface9 *current = nullptr, *back = nullptr, *depth = nullptr;
    device->GetRenderTarget(0, &current);
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    device->GetDepthStencilSurface(&depth);
    SdlResourceMirrors *mirrors = device->Get_Mirrors();
    std::string reason;
    target = {};
    if (current && current == back) target = frame->Back_Buffer_Target();
    else if (current && mirrors) {
        D3DSURFACE_DESC description;
        if (current->GetDesc(&description) == D3D_OK) {
            target.Width = description.Width;
            target.Height = description.Height;
            // The main scene can be an engine-owned postprocessing texture.
            // Its surface and texture share one GPU allocation, so resolve the
            // texture's existing mirror rather than creating a separate surface.
            IDirect3DTexture9 *texture = nullptr;
            if (current->GetContainer(IID_IDirect3DTexture9, reinterpret_cast<void **>(&texture)) == D3D_OK
                && texture) {
                IDirect3DSurface9 *level = nullptr;
                texture->GetSurfaceLevel(0, &level);
                if (level == current) target.Colour = mirrors->Texture(texture, reason);
                if (level) level->Release();
                texture->Release();
            } else {
                target.Colour = mirrors->Surface(current, false, reason);
            }
        }
    }
    if (target.Colour && mirrors) {
        D3DSURFACE_DESC description;
        // Explicit depth surfaces already used by opaque scene draws have a
        // mirror. The implicit depth surface is owned by SdlGpuFrame instead;
        // the device also borrows its size-matched depth for intermediate targets.
        // Read only a known explicit mirror, never allocate an empty depth here.
        if (depth && mirrors->Has_Copy(depth) && depth->GetDesc(&description) == D3D_OK
            && description.Width == target.Width && description.Height == target.Height)
            target.Depth = mirrors->Surface(depth, true, reason);
        else
            target.Depth = frame->Depth_For(target.Width, target.Height);
    }
    if (current) current->Release();
    if (back) back->Release();
    if (depth) depth->Release();
    if (!target.Colour || !target.Depth || !target.Width || !target.Height) {
        fail(reason.empty() ? "could not resolve the current scene colour/depth target" : reason.c_str());
        return false;
    }
    return true;
}

void heatScissor(const float viewProjection[16], const D3DVIEWPORT9 &viewport,
                 const SdlTarget &target, int32_t rectangle[4])
{
    const float left = static_cast<float>(std::min(viewport.X, target.Width));
    const float top = static_cast<float>(std::min(viewport.Y, target.Height));
    const float right = left + std::min(viewport.Width, target.Width - static_cast<unsigned>(left));
    const float bottom = top + std::min(viewport.Height, target.Height - static_cast<unsigned>(top));
    rectangle[0] = static_cast<int32_t>(left);
    rectangle[1] = static_cast<int32_t>(top);
    rectangle[2] = static_cast<int32_t>(right - left);
    rectangle[3] = static_cast<int32_t>(bottom - top);
    float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
    bool found = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const FieldState &field = fieldStates[i];
        if (field.palette < 0.5f || field.palette >= 1.5f || field.radius <= 0.001f
            || field.height <= 0.002f || field.density <= 0.00001f) continue;
        found = true;
        const float radius = field.radius + field.fringe + MaximumDisplacement;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const float world[4] = {
                field.ground.x + (corner & 1 ? radius : -radius),
                field.ground.y + (corner & 2 ? radius : -radius),
                (terrainEnabled[i] ? (corner & 4 ? terrainMax[i] : terrainMin[i]) : field.ground.z)
                    + (corner & 4 ? field.height * 2.6f : 0.0f), 1.0f
            };
            float clip[4] = {};
            for (unsigned column = 0; column < 4; ++column)
                for (unsigned row = 0; row < 4; ++row)
                    clip[column] += world[row] * viewProjection[row * 4 + column];
            // A box crossing the near plane cannot be bounded by projecting
            // only its corners. Keep the full viewport for that unusual view.
            if (!std::isfinite(clip[0]) || !std::isfinite(clip[1])
                || !std::isfinite(clip[2]) || !std::isfinite(clip[3])
                || clip[3] <= 0.00001f || clip[2] <= 0.00001f) return;
            const float x = viewport.X + 0.5f + (clip[0] / clip[3] + 1.0f) * viewport.Width * 0.5f;
            const float y = viewport.Y + 0.5f + (1.0f - clip[1] / clip[3]) * viewport.Height * 0.5f;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    if (!found) { rectangle[2] = rectangle[3] = 0; return; }
    // Two pixels of conservative rasterization slack keep the projected
    // envelope intact at viewport edges and odd framebuffer dimensions.
    const float boundedLeft = std::clamp(std::floor(minX) - 2.0f, left, right);
    const float boundedTop = std::clamp(std::floor(minY) - 2.0f, top, bottom);
    const float boundedRight = std::clamp(std::ceil(maxX) + 2.0f, left, right);
    const float boundedBottom = std::clamp(std::ceil(maxY) + 2.0f, top, bottom);
    rectangle[0] = static_cast<int32_t>(boundedLeft);
    rectangle[1] = static_cast<int32_t>(boundedTop);
    rectangle[2] = static_cast<int32_t>(std::max(0.0f, boundedRight - boundedLeft));
    rectangle[3] = static_cast<int32_t>(std::max(0.0f, boundedBottom - boundedTop));
}

bool renderVolume()
{
    if (!initialized || failed || !fieldCount) return false;
    bool active = false;
    for (unsigned i = 0; i < fieldCount; ++i)
        active = active || (fieldStates[i].radius > 0.001f && fieldStates[i].height > 0.002f
            && fieldStates[i].density > 0.00001f);
    if (!active) return false;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || device->Get_Gpu() != frame) {
        fail("the preview graphics device was replaced");
        return false;
    }
    // The scene's normal pass can target an intermediate texture. Resolve that
    // actual colour/depth pair, rather than assuming it is the final back buffer.
    // Reflection and custom passes are excluded by the Render callback below.
    SdlTarget target;
    if (!resolveSceneTarget(device, target)) return false;
    if (!resizeLowTarget(target.Width, target.Height)) return false;
    if (frame->Batch_Is_Full() && !frame->Flush()) {
        fail("could not submit preceding scene draws");
        return false;
    }
    if (!uploadFlow() || !uploadTerrain()) return false;
    SDL_GPUTexture *sceneDepth = frame->Water_Depth(target);
    if (!sceneDepth) {
        fail("sampleable scene depth is unavailable; volume clipping cannot run");
        return false;
    }
    D3DMATRIX view, projection;
    D3DVIEWPORT9 viewport;
    if (device->GetTransform(D3DTS_VIEW, &view) != D3D_OK
        || device->GetTransform(D3DTS_PROJECTION, &projection) != D3D_OK
        || device->GetViewport(&viewport) != D3D_OK || !viewport.Width || !viewport.Height) {
        fail("could not read the scene camera");
        return false;
    }
    SdlHazardCloudUniforms uniforms = {};
    float viewProjection[16], inverseView[16];
    multiply(&view._11, &projection._11, viewProjection);
    if (!render_inverse(viewProjection, uniforms.WorldFromClip)
        || !render_inverse(&view._11, inverseView)) {
        fail("scene camera has a singular transform");
        return false;
    }
    const float depthRange = viewport.MaxZ - viewport.MinZ;
    if (depthRange <= 1.0e-8f) {
        fail("scene camera has an empty depth range");
        return false;
    }
    for (unsigned column = 0; column < 4; ++column) {
        const float row = uniforms.WorldFromClip[8 + column] / depthRange;
        uniforms.WorldFromClip[12 + column] -= viewport.MinZ * row;
        uniforms.WorldFromClip[8 + column] = row;
    }
    for (unsigned i = 0; i < 3; ++i) uniforms.CameraPosition[i] = inverseView[12 + i];
    uniforms.CameraPosition[3] = 1;
    uniforms.Viewport[0] = static_cast<float>(viewport.X) + 0.5f;
    uniforms.Viewport[1] = static_cast<float>(viewport.Y) + 0.5f;
    uniforms.Viewport[2] = static_cast<float>(viewport.Width);
    uniforms.Viewport[3] = static_cast<float>(viewport.Height);
    uniforms.Scene[0] = 1.0f / target.Width;
    uniforms.Scene[1] = 1.0f / target.Height;
    uniforms.Scene[2] = 1;
    uniforms.Scene[3] = simulationTime;
    float *fields[] = { uniforms.Field0, uniforms.Field1 };
    float *shapes[] = { uniforms.Shape0, uniforms.Shape1 };
    float *sources[] = { uniforms.Source0, uniforms.Source1 };
    bool hotField = false;
    for (unsigned i = 0; i < fieldCount; ++i) {
        const FieldState &field = fieldStates[i];
        hotField = hotField || (field.palette >= 0.5f && field.palette < 1.5f
            && field.radius > 0.001f && field.height > 0.002f && field.density > 0.00001f);
        fields[i][0] = field.ground.x;
        fields[i][1] = field.ground.y;
        fields[i][2] = field.ground.z + field.height * 0.5f;
        fields[i][3] = field.radius;
        shapes[i][0] = field.height * 0.5f;
        shapes[i][1] = field.density;
        shapes[i][2] = field.palette;
        shapes[i][3] = field.formation;
        uniforms.Footprints[i] = field.fringe;
        sources[i][0] = field.release;
        sources[i][1] = sourceTravel[i];
        sources[i][2] = peakDensity[i] > 0.0001f
            ? std::max(0.0f, 1.0f - field.density / peakDensity[i]) : 0.0f;
        sources[i][3] = sourceAge[i];
    }
    float *terrain[] = { uniforms.Terrain0, uniforms.Terrain1 };
    for (unsigned i = 0; i < 2; ++i) {
        terrain[i][0] = terrainMin[i];
        terrain[i][1] = terrainMax[i];
        terrain[i][2] = terrainEnabled[i] ? 1.0f : 0.0f;
    }
    uniforms.Settings[0] = static_cast<float>(fieldCount);
    // Keep palette values in the cache key: live edits must refresh lighting
    // even while the source, infantry and simulation clock are paused.
    for (unsigned i = 0; i < 4; ++i) {
        const auto &palette = palettes[i];
        uniforms.PaletteMain[i][0] = palette.main.r;
        uniforms.PaletteMain[i][1] = palette.main.g;
        uniforms.PaletteMain[i][2] = palette.main.b;
        uniforms.PaletteMain[i][3] = palette.brightness;
        uniforms.PaletteShadow[i][0] = palette.shadow.r;
        uniforms.PaletteShadow[i][1] = palette.shadow.g;
        uniforms.PaletteShadow[i][2] = palette.shadow.b;
        uniforms.PaletteShadow[i][3] = 1.0f - palette.transparency;
    }
    // Visible hot-air refraction only for the warm nuclear palette.
    // This is a preview material control; it never changes a gameplay hazard.
    uniforms.Settings[1] = heatEnabled && hotField ? 10.0f : 0.0f;
    uniforms.Settings[2] = 1;
    uniforms.Settings[3] = Steps;
    uniforms.Interaction[0] = flow.domainMinX();
    uniforms.Interaction[1] = flow.domainMinY();
    uniforms.Interaction[2] = 1.0f / (flow.domainMaxX() - flow.domainMinX());
    uniforms.Interaction[3] = 1.0f / (flow.domainMaxY() - flow.domainMinY());
    uniforms.FlowSettings[0] = interactionEnabled ? 1.0f : 0.0f;
    uniforms.FlowSettings[1] = MaximumDisplacement;

    // Complete all potentially failing material allocations before recording
    // a visible scene draw. The adapter can safely fall back to classic clouds.
    if (uniforms.Settings[1] > 0 && !prepareHeatBackground(target)) return false;
    if (!SdlHazardCloudEmbers::prepare(frame, target, fieldStates, fieldCount)) {
        fail("could not prepare nuclear embers");
        return false;
    }
    uint32_t vertexOffset = 0;
    std::memset(frame->Stage(16, vertexOffset), 0, 16);
    if (!prepareCaches(uniforms, sceneDepth, vertexOffset, target)) return false;
    SdlRecordedDraw draw = {};
    draw.Pipeline = shared.volumePipeline;
    // The common batch binds a vertex buffer even for SV_VertexID shaders.
    // Supply a valid unused allocation; the fullscreen triangle needs no mesh.
    draw.VertexOffset = vertexOffset;
    draw.Count = 3;
    draw.PixelConstants = frame->Constants(1, &uniforms, sizeof(uniforms));
    draw.PixelConstantsSize = sizeof(uniforms);
    draw.SamplerCount = 5;
    bindTerrain(draw);
    draw.Textures[0] = sceneDepth;
    draw.Samplers[0] = shared.depthSampler;
    draw.Textures[1] = flowTexture;
    draw.Samplers[1] = shared.flowSampler;
    // Slot 2 is only sampled by the composite. It must not alias the low target
    // while this pass writes that target, even though the low shader ignores it.
    draw.Textures[2] = flowTexture;
    draw.Samplers[2] = shared.flowSampler;
    bindCachedTextures(draw, false);
    draw.Viewport[2] = static_cast<float>(shared.lowTarget.Width);
    draw.Viewport[3] = static_cast<float>(shared.lowTarget.Height);
    draw.Viewport[5] = 1;
    draw.Scissor[2] = static_cast<int32_t>(shared.lowTarget.Width);
    draw.Scissor[3] = static_cast<int32_t>(shared.lowTarget.Height);
    frame->Set_Target(shared.lowTarget);
    frame->Clear_Back_Buffer(true, false, false, 0, 1.0f, 0);
    frame->Record_Draw(draw);

    // Target changes close the low-resolution pass before the composite samples
    // it. Both draws keep the original camera/depth uniforms; only the low pass's
    // raster viewport changes. No extra submission or GPU wait is necessary.
    draw.Pipeline = shared.compositePipeline;
    draw.SamplerCount = 5;
    bindTerrain(draw);
    draw.Textures[2] = shared.lowTarget.Colour;
    draw.Samplers[2] = shared.depthSampler;
    std::copy(uniforms.Viewport, uniforms.Viewport + 4, draw.Viewport);
    draw.Viewport[4] = viewport.MinZ;
    draw.Viewport[5] = viewport.MaxZ;
    draw.Scissor[0] = static_cast<int32_t>(viewport.X);
    draw.Scissor[1] = static_cast<int32_t>(viewport.Y);
    draw.Scissor[2] = std::min(viewport.Width, target.Width - std::min(viewport.X, target.Width));
    draw.Scissor[3] = std::min(viewport.Height, target.Height - std::min(viewport.Y, target.Height));
    frame->Set_Target(target);
    frame->Record_Draw(draw);
    SdlHazardCloudEmbers::render(frame, target, sceneDepth, viewProjection, draw.Viewport,
        fieldStates, fieldCount, simulationTime, terrainTexture, terrainEnabled);
    if (uniforms.Settings[1] > 0) {
        SdlRecordedDraw heatDraw = draw;
        heatScissor(viewProjection, viewport, target, heatDraw.Scissor);
        if (heatDraw.Scissor[2] > 0 && heatDraw.Scissor[3] > 0) {
            // Include the smoke and embers in the refraction. Warping only the
            // background makes heat disappear under a dense cloud's opacity.
            captureHeatBackground(target);
            heatDraw.Pipeline = shared.heatPipeline;
            heatDraw.SamplerCount = 7;
            heatDraw.Textures[5] = shared.heatBackground.Colour;
            heatDraw.Samplers[5] = shared.cacheSampler;
            frame->Record_Draw(heatDraw);
        }
    }
    flowUsedBatch = frame->Batch();
    // Recorded state is private to this draw. The next engine draw rebinds its
    // own pipeline, samplers and uniforms without any D3D9 state restoration.
    if (++renderedFrames == 1)
        std::fprintf(stderr, "cloud-preview: volumetric draw active; world-space density, "
            "scene-depth clipping, half-resolution %ux%u with depth-aware composite, %.0f ray steps\n",
            shared.lowTarget.Width, shared.lowTarget.Height, Steps);
    return true;
}
void setFields(const FieldState *values, unsigned count)
{
    if (count > 2 || (count && !values)) {
        fail("invalid cloud lifecycle field list");
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        const FieldState &field = values[i];
        if (!std::isfinite(field.ground.x) || !std::isfinite(field.ground.y)
            || !std::isfinite(field.ground.z) || !std::isfinite(field.radius)
            || !std::isfinite(field.height) || !std::isfinite(field.density)
            || !std::isfinite(field.palette) || !std::isfinite(field.formation)
            || !std::isfinite(field.release) || !std::isfinite(field.fringe)
            || field.fringe < 0 || field.radius < 0 || field.height < 0
            || field.density < 0 || field.palette < 0 || field.palette > 3
            || field.formation < 0 || field.formation > 1 || field.release < 0 || field.release > 1) {
            fail("invalid cloud lifecycle field state");
            return;
        }
    }
    if (!explicitDomain && count) {
        float minX = values[0].ground.x, maxX = minX;
        float minY = values[0].ground.y, maxY = minY;
        for (unsigned i = 0; i < count; ++i) {
            const float radius = std::max(1.0f, values[i].radius + values[i].fringe + MaximumDisplacement);
            minX = std::min(minX, values[i].ground.x - radius);
            maxX = std::max(maxX, values[i].ground.x + radius);
            minY = std::min(minY, values[i].ground.y - radius);
            maxY = std::max(maxY, values[i].ground.y + radius);
        }
        if (flow.domainMinX() != minX || flow.domainMaxX() != maxX
            || flow.domainMinY() != minY || flow.domainMaxY() != maxY) {
            flow.setDomain(minX, minY, maxX, maxY);
            flowDirty = true;
        }
    }
    fieldCount = count;
    for (unsigned i = 0; i < count; ++i) {
        fieldStates[i] = values[i];
        peakDensity[i] = std::max(peakDensity[i], values[i].density);
    }
}

void reset()
{
    flow.reset();
    flowDirty = true;
    for (unsigned i = 0; i < 2; ++i) {
        sourceTravel[i] = sourceAge[i] = peakDensity[i] = 0;
    }
}

void setBodies(const Body *values, unsigned count)
{
    if (count > MaximumBodies || (count && !values)) {
        fail("invalid infantry interaction body list");
        return;
    }
    for (unsigned i = 0; i < count; ++i) {
        const Body &body = values[i];
        if (!std::isfinite(body.x) || !std::isfinite(body.y) || !std::isfinite(body.z)
            || !std::isfinite(body.vx) || !std::isfinite(body.vy)
            || !std::isfinite(body.radius) || !std::isfinite(body.height)
            || body.radius <= 0 || body.height <= 0) {
            fail("invalid infantry interaction body");
            return;
        }
    }
    if (count) bodies.assign(values, values + count);
    else bodies.clear();
}

void setInteractionEnabled(bool value)
{
    if (interactionEnabled == value) return;
    interactionEnabled = value;
    flow.reset();
    flowDirty = true;
}


void setHeatEnabled(bool value) { heatEnabled = value; }

void update(float timeSeconds)
{
    if (!std::isfinite(timeSeconds) || timeSeconds < 0) {
        fail("invalid simulation time");
        return;
    }
    const float elapsed = hasSimulationTime ? timeSeconds - simulationTime : 0.0f;
    simulationTime = timeSeconds;
    hasSimulationTime = true;
    if (elapsed < 0) {
        reset();
    } else if (elapsed > 0 && !failed) {
        for (unsigned i = 0; i < fieldCount; ++i) {
            sourceAge[i] += elapsed;
            sourceTravel[i] += elapsed * fieldStates[i].release * 22.0f;
        }
        // Appearance only controls drawing. Keep the same wake history while
        // the original effects are shown, so toggling back is an in-place A/B.
        if (interactionEnabled) {
            flow.step(elapsed, bodies.empty() ? nullptr : bodies.data(),
                static_cast<unsigned>(bodies.size()));
            if (!flow.valid()) fail("interaction simulation produced invalid state");
            flowDirty = true;
        }
    }
}



bool initialize()
{
    if (initialized) return !failed;
    failed = false;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || !(frame = device->Get_Gpu()) || !(gpu = frame->Device())) {
        fail("an SDL graphics device is required");
        return false;
    }
    if (shared.users && (shared.frame != frame || shared.gpu != gpu)) {
        fail("existing cloud layers belong to a different graphics device");
        frame = nullptr;
        gpu = nullptr;
        return false;
    }
    if (!shared.users) {
        shared.frame = frame;
        shared.gpu = gpu;
        if (!makeResources()) {
            releaseShared(true);
            frame = nullptr;
            gpu = nullptr;
            return false;
        }
    }
    ++shared.users;
    initialized = true;
    if (!makeFlowTexture()) { shutdown(); return false; }
    if (!explicitDomain && !fieldCount) flow.setDomain(-400, -220, 400, 220);
    flowDirty = true;
    return true;
}

void shutdown()
{
    if (!initialized) return;
    PosixDevice9 *device = static_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    const bool alive = device && device->Get_Gpu() == frame;
    if (alive) {
        for (FieldCache &cache : fieldCaches) retireCache(cache);
        if (flowTexture) frame->Release_After_Batch(flowTexture, nullptr);
        if (terrainTexture) frame->Release_After_Batch(terrainTexture, nullptr);
    }
    for (FieldCache &cache : fieldCaches) cache = {};
    flowTexture = terrainTexture = nullptr;
    terrainDirty = terrainEnabled[0] || terrainEnabled[1];
    initialized = false;
    if (shared.users) --shared.users;
    if (!shared.users) releaseShared(alive);
    std::fprintf(stderr, "cloud-preview: volume-cache rebuilds=%u field-bakes=%u rendered-frames=%u\n",
        cacheUpdates, cacheBakes, renderedFrames);
    frame = nullptr;
    gpu = nullptr;
    cachedInputsValid = false;
    cachedInputs = {};
    cachedFlowUploads = cacheUpdates = cacheBakes = 0;
    renderedFrames = flowUploads = 0;
    flowUsedBatch = ~uint64_t(0);
    flowDirty = true;
    hasSimulationTime = false;
    bodies.clear();
    reset();
}
};

Layer::Layer() : impl(new Impl) {}
Layer::~Layer() { shutdown(); delete impl; }
bool Layer::initialize() { return impl->initialize(); }
void Layer::setFields(const FieldState *fields, unsigned count) { impl->setFields(fields, count); }
void Layer::setBodies(const Body *bodies, unsigned count) { impl->setBodies(bodies, count); }
void Layer::setFlowDomain(float minX, float minY, float maxX, float maxY)
{
    impl->flow.setDomain(minX, minY, maxX, maxY);
    impl->explicitDomain = true;
    impl->flowDirty = true;
    if (!impl->flow.valid()) impl->fail("invalid interaction simulation domain");
}
void Layer::setTerrain(unsigned index, const float *heights)
{
    if (index >= 2) { impl->fail("invalid terrain field index"); return; }
    if (!heights) {
        impl->terrainEnabled[index] = false;
        impl->terrainMin[index] = impl->terrainMax[index] = 0;
        return;
    }
    float minimum = heights[0], maximum = heights[0];
    for (unsigned i = 0; i < TerrainGridSide * TerrainGridSide; ++i) {
        if (!std::isfinite(heights[i]) || std::abs(heights[i]) > 100000.0f) {
            impl->fail("invalid terrain height");
            return;
        }
        minimum = std::min(minimum, heights[i]);
        maximum = std::max(maximum, heights[i]);
    }
    bool changed = !impl->terrainEnabled[index];
    for (unsigned y = 0; y < TerrainGridSide; ++y) {
        float *destination = impl->terrainPixels.data() + y * TerrainGridSide * 2 + index * TerrainGridSide;
        const float *source = heights + y * TerrainGridSide;
        changed = changed || std::memcmp(destination, source, TerrainGridSide * sizeof(float)) != 0;
        std::copy(source, source + TerrainGridSide, destination);
    }
    impl->terrainEnabled[index] = true;
    impl->terrainMin[index] = minimum;
    impl->terrainMax[index] = maximum;
    impl->terrainDirty = impl->terrainDirty || changed;
}
void Layer::setPalette(unsigned index, const Palette &palette)
{
    const float values[] = { palette.main.r, palette.main.g, palette.main.b,
        palette.shadow.r, palette.shadow.g, palette.shadow.b, palette.brightness, palette.transparency };
    if (index >= 4) { impl->fail("invalid palette index"); return; }
    for (unsigned i = 0; i < 8; ++i)
        if (!std::isfinite(values[i]) || values[i] < 0 || values[i] > (i == 6 ? 2 : 1)) {
            impl->fail("invalid palette value");
            return;
        }
    impl->palettes[index] = palette;
}
void Layer::setInteractionEnabled(bool value) { impl->setInteractionEnabled(value); }
bool Layer::isInteractionEnabled() const { return impl->interactionEnabled; }
void Layer::setHeatEnabled(bool value) { impl->setHeatEnabled(value); }
bool Layer::isHeatEnabled() const { return impl->heatEnabled; }
void Layer::update(float seconds) { impl->update(seconds); }
void Layer::reset() { impl->reset(); }
bool Layer::render() { return impl->renderVolume(); }
bool Layer::valid() const { return impl->initialized && !impl->failed && impl->flow.valid(); }
unsigned Layer::activeBodies() const { return impl->flow.activeBodies(); }
float Layer::maxDisturbance() const { return impl->flow.maxDisturbance(); }
unsigned Layer::interactionUploads() const { return impl->flowUploads; }
unsigned Layer::renderedFrames() const { return impl->renderedFrames; }
std::uint64_t Layer::estimateCacheBytes(const FieldState *fields, unsigned count)
{
    if (count > 2 || (count && !fields)) return std::numeric_limits<std::uint64_t>::max();
    std::uint64_t result = (192u * 96u * 4u + TerrainGridSide * TerrainGridSide * 2u) * sizeof(float);
    for (unsigned i = 0; i < count; ++i) {
        if (!std::isfinite(fields[i].radius) || !std::isfinite(fields[i].fringe)
            || fields[i].radius < 0 || fields[i].fringe < 0)
            return std::numeric_limits<std::uint64_t>::max();
        if (fields[i].radius <= 0.001f || fields[i].height <= 0.002f || fields[i].density <= 0.00001f) continue;
        const std::uint64_t side = cacheSide(fields[i]);
        result += side * side * CacheSlices * 10u; // R16F density + RGBA16F light.
    }
    return result;
}
std::uint64_t Layer::estimatedCacheBytes() const
{
    std::uint64_t result = impl->flowTexture ? 192u * 96u * 4u * sizeof(float) : 0;
    if (impl->terrainTexture) result += TerrainGridSide * TerrainGridSide * 2u * sizeof(float);
    for (const FieldCache &cache : impl->fieldCaches)
        result += std::uint64_t(cache.side) * cache.side * CacheSlices * 10u;
    return result;
}
void Layer::shutdown() { impl->shutdown(); }

} // namespace SdlHazardCloud
