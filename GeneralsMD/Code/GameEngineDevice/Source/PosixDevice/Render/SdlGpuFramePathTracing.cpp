// Optional geometry path tracing for the water capture. Uses ordinary SDL GPU
// graphics resources. An explicit hardware variant replaces only the primary intersection query.
#include "SdlGpuFrame.h"
#include "PosixDevice9.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterPathTracingMotion.h"
#include "Platform/WaterPathTracingShader.h"
#include "Platform/WaterHardwareReflectionShader.h"
#include "Platform/WaterHardwarePathTracingShader.h"
#include "Platform/HardwareRaySun.h"
#if defined(__linux__)
#include "../../../../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#endif
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <cstdlib>

struct SdlGpuFrame::PathResources {
    WaterPathTracing::Scene Scene;
    uint64_t Fingerprint = 0;
    uint64_t Revision = 0, MaterialFingerprint = 0;
    uint64_t PackedGeometryEpoch = 0, HardwareGeometryEpoch = 0, HardwarePathGeometryEpoch = 0;
    size_t SourceStaticTriangleCount = 0;
    const WaterPathTracing::PrimaryMaterialPacket *MaterialSource = nullptr;
    std::vector<WaterPathTracing::Float4> MaterialPixels;
    bool MaterialPixelsUploaded = false;
    std::vector<WaterPathTracing::Float4> MaterialAttributes, UploadedRows[4];
    WaterPathTracing::PrimaryMaterialValidation MaterialValidation;
    uint64_t StaticNativePackSkips = 0, StaticAttributeSkips = 0;
    bool UploadSnapshotValid[4]{};
    bool UploadsNeedRefresh = false;
    uint64_t TextureUploadRows = 0, TextureUploadSkips = 0, TextureReallocations = 0;
    const void *Source = nullptr;
    WaterPathTracing::Uniforms LastView{};
    SDL_GPUTexture *Nodes = nullptr, *Triangles = nullptr, *History[2]{};
    SDL_GPUTexture *Materials = nullptr, *Texels = nullptr;
    SDL_GPUTransferBuffer *Uploads[4]{};
    unsigned UploadBytes[4]{}, DataHeights[4]{};
    SDL_GPUSampler *Point = nullptr, *Linear = nullptr;
    SDL_GPUShader *Vertex = nullptr, *Pixel = nullptr;
    SDL_GPUGraphicsPipeline *Pipeline = nullptr;
    unsigned Size = 0, Samples = 0, Current = 0, Builds = 0, Refits = 0, SinceBuild = 0, LinearPacks = 0;
    WaterPathTracing::MotionRecovery Motion;
    bool Ready = false, UsingHardware = false, UsingHardwarePath = false;
#if defined(__linux__)
    ZHR_SDLRayScene *HardwareScene = nullptr, *HardwarePathScene = nullptr;
#endif
    SDL_GPUTexture *HardwareHits = nullptr, *HardwareTransport = nullptr;
    SDL_GPUShader *HardwarePixel = nullptr, *HardwarePathPixel = nullptr;
    SDL_GPUGraphicsPipeline *HardwarePipeline = nullptr, *HardwarePathPipeline = nullptr;
    SDL_GPUFence *HardwareFence = nullptr;
    unsigned HardwareSize = 0, HardwareBuilds = 0, HardwarePathBuilds = 0;
    unsigned HardwareCreates = 0, HardwarePathCreates = 0, HardwareUpdates = 0, HardwarePathUpdates = 0;
    unsigned HardwareSkips = 0, HardwarePathSkips = 0;
    std::vector<float> NativePositions;
    std::vector<uint32_t> NativeIndices;
    struct NativeMesh {
        std::vector<float> Positions;
        std::vector<uint32_t> Indices;
        bool Pack(const std::vector<WaterPathTracing::Triangle> &triangles, size_t first, size_t count) {
            bool changed = Positions.size() != count * 9;
            if (!changed) {
                size_t i = 0;
                for (size_t t = first; t < first + count && !changed; ++t)
                    for (WaterPathTracing::Vec3 v : {triangles[t].a, triangles[t].b, triangles[t].c}) {
                        changed |= Positions[i] != v.x || Positions[i+1] != v.y || Positions[i+2] != v.z;
                        i += 3;
                    }
            }
            if (!changed) return false;
            Positions.resize(count * 9);
            size_t i = 0;
            for (size_t t = first; t < first + count; ++t)
                for (WaterPathTracing::Vec3 v : {triangles[t].a, triangles[t].b, triangles[t].c}) {
                    Positions[i++] = v.x; Positions[i++] = v.y; Positions[i++] = v.z;
                }
            if (Indices.size() != count * 3) {
                Indices.resize(count * 3);
                for (size_t j = 0; j < Indices.size(); ++j) Indices[j] = uint32_t(j);
            }
            return true;
        }
    } StaticMesh, DynamicMesh;
    unsigned StaticBuilds = 0, DynamicBuilds = 0, StaticReuses = 0, DynamicReuses = 0;
    struct Performance {
        uint64_t Calls = 0, NoWork = 0, FenceWaits = 0, NativeCreates = 0, NativeUpdates = 0, GeometrySkips = 0;
        double FenceMs = 0, ScenePackMs = 0, NativePackMs = 0, NativeCreateMs = 0, NativeUpdateMs = 0;
        uint64_t RetainedPacks = 0, AlbedoPacks = 0, FullPacks = 0;
        double RetainedPackMs = 0, AlbedoPackMs = 0, FullPackMs = 0;
        void Report(const char *mode) const {
            std::fprintf(stderr, "[water-hardware-profile] mode=%s calls=%llu no_work=%llu fence_waits=%llu fence_wait_cpu_ms=%.4f scene_pack_cpu_ms=%.4f native_pack_cpu_ms=%.4f native_creates=%llu native_create_cpu_ms=%.4f native_updates=%llu native_update_cpu_ms=%.4f unchanged_geometry_skips=%llu\n",
                mode, (unsigned long long)Calls, (unsigned long long)NoWork, (unsigned long long)FenceWaits,
                FenceMs, ScenePackMs, NativePackMs, (unsigned long long)NativeCreates, NativeCreateMs,
                (unsigned long long)NativeUpdates, NativeUpdateMs, (unsigned long long)GeometrySkips);
            std::fprintf(stderr,"[water-hardware-pack-profile] retained=%llu retained_cpu_ms=%.4f albedo=%llu albedo_cpu_ms=%.4f full=%llu full_cpu_ms=%.4f\n",
                (unsigned long long)RetainedPacks,RetainedPackMs,(unsigned long long)AlbedoPacks,AlbedoPackMs,
                (unsigned long long)FullPacks,FullPackMs);
        }
    } Profile;
    std::string HardwareFailure, HardwarePathFailure;
    uint64_t HardwareFailureGeneration = 0, HardwarePathFailureGeneration = 0;
    bool HardwareCapabilityFailure = false, HardwarePathCapabilityFailure = false, HardwareDeviceLost = false;
    std::vector<unsigned char> HardwareSpirv, HardwarePathSpirv;
    bool HardwareLogged = false;
    double BuildMs = 0;
    std::string Failure;
};

bool SdlGpuFrame::Clear_Water_Path()
{
    if (!WaterPath) return true;
    // Submit recorded readers before dropping their bindings.
    if (Has_Pending_Work() && !Flush()) {
        // Failed submission leaves queued draws intact. Their textures and sampler
        // must remain alive until a later clear succeeds (or frame destruction).
        WaterPath->Ready = false;
        WaterPath->Samples = 0;
        WaterPath->Source = nullptr;
        WaterPath->Revision = 0;
        return false;
    }
#if defined(__linux__)
    if ((WaterPath->HardwareScene || WaterPath->HardwarePathScene || WaterPath->HardwareFence)
        && ZHR_SDLRayWaitForRetirement(GpuDevice) == ZHR_SDLRAY_RETIRE_REFUSED) {
        // No ownership changes on an ordinary idle refusal. Later CPU packets
        // cannot sample a capture whose native state has become uncertain.
        WaterPath->Ready = false;
        return false;
    }
#endif
    std::fprintf(stderr, "%s\n", Water_Path_Report().c_str());
    if (std::getenv("ZH_WATER_PROFILE") || std::getenv("ZH_HARDWARE_WATER_PROFILE"))
        WaterPath->Profile.Report("teardown");
#if defined(__linux__)
    if (WaterPath->HardwareScene) ZHR_SDLRayDestroySceneAfterRetirement(WaterPath->HardwareScene);
    if (WaterPath->HardwarePathScene) ZHR_SDLRayDestroySceneAfterRetirement(WaterPath->HardwarePathScene);
#endif
    if (WaterPath->HardwareFence) SDL_ReleaseGPUFence(GpuDevice, WaterPath->HardwareFence);
    if (WaterPath->HardwareHits) SDL_ReleaseGPUTexture(GpuDevice, WaterPath->HardwareHits);
    if (WaterPath->HardwarePipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, WaterPath->HardwarePipeline);
    if (WaterPath->HardwarePixel) SDL_ReleaseGPUShader(GpuDevice, WaterPath->HardwarePixel);
    if (WaterPath->HardwareTransport) SDL_ReleaseGPUTexture(GpuDevice, WaterPath->HardwareTransport);
    if (WaterPath->HardwarePathPipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, WaterPath->HardwarePathPipeline);
    if (WaterPath->HardwarePathPixel) SDL_ReleaseGPUShader(GpuDevice, WaterPath->HardwarePathPixel);
    for (auto *texture : {WaterPath->Nodes, WaterPath->Triangles, WaterPath->History[0], WaterPath->History[1],
                          WaterPath->Materials, WaterPath->Texels})
        if (texture) SDL_ReleaseGPUTexture(GpuDevice, texture);
    for (auto *upload : WaterPath->Uploads)
        if (upload) SDL_ReleaseGPUTransferBuffer(GpuDevice, upload);
    if (WaterPath->Pipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, WaterPath->Pipeline);
    if (WaterPath->Vertex) SDL_ReleaseGPUShader(GpuDevice, WaterPath->Vertex);
    if (WaterPath->Pixel) SDL_ReleaseGPUShader(GpuDevice, WaterPath->Pixel);
    if (WaterPath->Point) SDL_ReleaseGPUSampler(GpuDevice, WaterPath->Point);
    if (WaterPath->Linear) SDL_ReleaseGPUSampler(GpuDevice, WaterPath->Linear);
    delete WaterPath; WaterPath = nullptr;
    return true;
}

bool SdlGpuFrame::Prepare_Water_Path(const std::vector<WaterPathTracing::Triangle> &triangles,
    const WaterPathTracing::View &view, uint64_t revision, const WaterPathTracing::PrimaryMaterialPacket *materials, bool hardware, bool hardwarePath,
    size_t staticTriangleCount)
{
    using namespace WaterPathTracing;
    hardware = hardware || hardwarePath;
    // Scene export can legitimately be empty while a map is loading. Do not turn
    // that transient absence into a sticky device/shader failure or change mode.
    if (hardware && triangles.empty()) return false;
    if (!WaterPath) WaterPath = new PathResources;
    PathResources &p = *WaterPath;
    const bool profile = std::getenv("ZH_WATER_PROFILE") || std::getenv("ZH_HARDWARE_WATER_PROFILE");
    struct ProfileWindow {
        PathResources::Performance &Stats;
        bool Enabled;
        const char *Mode;
        ~ProfileWindow() { if (Enabled && Stats.Calls % 64 == 0) Stats.Report(Mode); }
    } profileWindow{p.Profile, profile, hardwarePath ? "hardware-path" : hardware ? "hardware-one-bounce" : "software"};
    if (profile) ++p.Profile.Calls;
    std::string &failure = hardwarePath ? p.HardwarePathFailure : hardware ? p.HardwareFailure : p.Failure;
    uint64_t &failureGeneration = hardwarePath ? p.HardwarePathFailureGeneration : p.HardwareFailureGeneration;
    bool &capabilityFailure = hardwarePath ? p.HardwarePathCapabilityFailure : p.HardwareCapabilityFailure;
    const uint64_t requestGeneration = HardwareRayTracing::RequestGeneration();
    bool uploadStateChanged = false;
    const auto fail = [&](const std::string &why, bool permanent = false) {
        if (failure != why) std::fprintf(stderr, "Water %s unavailable: %s; retaining selected reflection fallback\n",
            hardwarePath ? "hardware path tracing" : hardware ? "hardware reflection" : "path tracing", why.c_str());
        failure = why;
        // Recording/upload helpers may already have replaced a texture or
        // advanced CPU row snapshots before a later command is cancelled. No
        // failed capture can certify those rows as submitted GPU contents.
        if (uploadStateChanged) {
            std::fill_n(p.UploadSnapshotValid, 4, false);
            p.MaterialPixelsUploaded = false;
            p.UploadsNeedRefresh = true;
        }
        if (hardware) { failureGeneration = requestGeneration; capabilityFailure |= permanent; }
        if (!hardware || (p.UsingHardware && p.UsingHardwarePath == hardwarePath)) p.Ready = false;
        return false;
    };
    // Mode switches clear PathResources. Global gate toggles retain the capture,
    // so explicitly allow one retry without turning a refusal into a frame loop.
    if (hardware && p.HardwareDeviceLost) return false;
    if (!failure.empty()) {
        if (!hardware || capabilityFailure || failureGeneration == requestGeneration) return false;
        failureGeneration = requestGeneration; // consume before any flush or idle refusal
        if (Has_Pending_Work() && !Flush()) return false;
#if defined(__linux__)
        ZHR_SDLRayScene *&retired = hardwarePath ? p.HardwarePathScene : p.HardwareScene;
        if (retired || p.HardwareFence) {
            const auto retirement = ZHR_SDLRayWaitForRetirement(GpuDevice);
            if (retirement == ZHR_SDLRAY_RETIRE_REFUSED) return false;
            if (p.HardwareFence) { SDL_ReleaseGPUFence(GpuDevice, p.HardwareFence); p.HardwareFence = nullptr; }
            ZHR_SDLRayDestroySceneAfterRetirement(retired); retired = nullptr;
            (hardwarePath ? p.HardwarePathGeometryEpoch : p.HardwareGeometryEpoch) = 0;
            if (retirement == ZHR_SDLRAY_RETIRE_DEVICE_LOST) {
                p.HardwareDeviceLost = true;
                return false;
            }
        }
#endif
        failure.clear();
    }
    if (hardwarePath && staticTriangleCount > triangles.size()) return fail("invalid static triangle partition");
#if defined(__linux__)
    if (hardware) {
        char diagnostic[1024] = {};
        if (!ZHR_SDLRayQuerySupported(GpuDevice, diagnostic, sizeof(diagnostic))) return fail(diagnostic, true);
    }
#else
    if (hardware) return fail("hardware water reflection requires Linux Vulkan", true);
#endif
    // A sticky hardware refusal must not disturb an already accumulating software capture.
    if (p.UsingHardware != hardware || p.UsingHardwarePath != hardwarePath) { p.Ready = false; p.Samples = 0; }
    p.UsingHardware = hardware; p.UsingHardwarePath = hardwarePath;
    if (view.size < 32 || view.size > 1024 || view.motionSize < 32 || view.motionSize > 1024
        || !std::isfinite(view.waterLevel)
        || !std::isfinite(view.maxDistance) || view.maxDistance <= 0)
        return fail("invalid capture view");
    const auto four = [](Vec3 v, float w = 0) { return Float4{v.x,v.y,v.z,w}; };
    Uniforms constants{};
    constants.originWater = four(view.origin, view.waterLevel);
    constants.rayTopLeft = four(view.rayTopLeft);
    constants.rayDx = four(view.rayDx); constants.rayDy = four(view.rayDy);
    constants.sunDirection = four(view.sunDirection);
    constants.sunRadiance = four(view.sunRadiance);
    constants.skyLow = four(view.skyLow); constants.skyHigh = four(view.skyHigh);
    constants.counts.w = view.maxDistance;
    constants.output = Float4{float(view.size),float(view.size),0.02f,float(view.motionSize)};
    constants.transport = Float4{view.secondaryDistance,view.shadowDistance,
        float(view.secondaryNodeBudget),float(view.shadowNodeBudget)};
    if (!(view.secondaryDistance>0) || !(view.shadowDistance>0)
        || view.secondaryNodeBudget<1 || view.shadowNodeBudget<1
        || view.secondaryNodeBudget>MaximumTriangles || view.shadowNodeBudget>MaximumTriangles)
        return fail("invalid transport limits");
    const float *values = reinterpret_cast<const float *>(&constants);
    for (unsigned i = 0; i < sizeof(constants)/sizeof(float); ++i)
        if (!std::isfinite(values[i])) return fail("non-finite capture view");
    const uint64_t fingerprint = revision ? 0 : Fingerprint(triangles);
    // Hardware packets have source-order IDs and no node texture. A backend transition
    // must repack/reupload even if the producer's pointer and revision are unchanged.
    const bool geometryChanged = p.UploadsNeedRefresh || !p.Triangles || p.Scene.IsLinearPacked()!=hardware || (!hardware && !p.Nodes)
        || (hardwarePath && p.SourceStaticTriangleCount != staticTriangleCount) || (revision
        ? p.Source != &triangles || p.Revision != revision
        : p.Revision != 0 || fingerprint != p.Fingerprint);
    const uint64_t materialFingerprint = materials && !revision ? FingerprintPrimaryMaterials(*materials) : 0;
    const bool materialsChanged = geometryChanged || !p.MaterialPixelsUploaded || p.MaterialSource != materials
        || materialFingerprint != p.MaterialFingerprint;
    const std::vector<Float4> emptyAtlas;
    const auto &pixels = materials ? materials->texels : emptyAtlas;
    const bool sameAtlas = materialsChanged && p.MaterialPixelsUploaded && p.Texels
        && pixels.size() == p.MaterialPixels.size()
        && (pixels.empty() || std::memcmp(pixels.data(),p.MaterialPixels.data(),pixels.size()*sizeof(Float4)) == 0);
    const bool uploadAtlas = materialsChanged && !sameAtlas;
    if (materialsChanged && materials) {
        uploadStateChanged = true; // validation also advances retained static host snapshots
        const bool valid = hardwarePath ? p.MaterialValidation.Validate(*materials,triangles.size(),staticTriangleCount)
            : ValidPrimaryMaterials(*materials,triangles.size(),hardware && sameAtlas);
        if (!valid) return fail("invalid primary materials");
    }
    const bool viewChanged = std::memcmp(&constants, &p.LastView, sizeof(constants)) != 0;
    const bool resolvingMotion = p.Motion.Observe(SDL_GetTicks(),
        p.Ready && !SameCaptureProjection(constants,p.LastView));
    // Low detail reduces density during camera motion; High retains its requested
    // density. Scene animation resets lighting without reducing a still capture.
    const unsigned captureSize = resolvingMotion ? std::min(view.size,view.motionSize) : view.size;
    if (geometryChanged || materialsChanged || viewChanged) p.Samples = 0;
    if (p.Ready && p.Samples >= (hardware && !hardwarePath ? 1u : 64u) && p.Size == captureSize && !geometryChanged && !materialsChanged && !viewChanged) {
        if (profile) ++p.Profile.NoWork;
        return true;
    }
    // A converged capture needs no native wait. Every working call, including a software
    // mode transition, waits before mutating native buffers or any shared GPU resource.
    if (p.HardwareFence) {
        const Uint64 start = profile ? SDL_GetTicksNS() : 0;
        if (!SDL_WaitForGPUFences(GpuDevice, true, &p.HardwareFence, 1)) return fail(SDL_GetError());
        if (profile) { ++p.Profile.FenceWaits; p.Profile.FenceMs += double(SDL_GetTicksNS() - start) * 1e-6; }
        SDL_ReleaseGPUFence(GpuDevice, p.HardwareFence); p.HardwareFence = nullptr;
    }
    // Previous water draws can still be queued against the history we're about to overwrite.
    if (!Flush()) return fail("pending draw submission failed");
    const SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if (hardwarePath ? !p.HardwarePathPipeline : hardware ? !p.HardwarePipeline : !p.Pipeline) {
        if (!SDL_GPUTextureSupportsFormat(GpuDevice, format, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
            return fail("RGBA16F capture unsupported", true);
        std::vector<unsigned char> spirv; std::string error;
        if (!p.Vertex && (!SDL3_Compile_HLSL_To_SPIRV(VertexSource(),true,spirv,error)
            || !(p.Vertex = SDL3_Create_Shader(GpuDevice,spirv,true,error)))) return fail(error);
        SDL_GPUShader *&pixel = hardwarePath ? p.HardwarePathPixel : hardware ? p.HardwarePixel : p.Pixel;
        SDL_GPUGraphicsPipeline *&selectedPipeline = hardwarePath ? p.HardwarePathPipeline : hardware ? p.HardwarePipeline : p.Pipeline;
        if (!pixel && (!SDL3_Compile_HLSL_To_SPIRV(hardwarePath ? HardwarePathFragmentSource() : hardware ? HardwareFragmentSource() : std::string(FragmentSource()),false,spirv,error)
            || !(pixel = SDL3_Create_Shader(GpuDevice,spirv,false,error)))) return fail(error);
        SDL_GPUColorTargetDescription target{}; target.format = format;
        SDL_GPUGraphicsPipelineCreateInfo pipeline{};
        pipeline.vertex_shader = p.Vertex; pipeline.fragment_shader = pixel;
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.target_info.color_target_descriptions = &target;
        pipeline.target_info.num_color_targets = 1;
        selectedPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);
        if (!selectedPipeline) return fail(SDL_GetError());
    }
    // Pipeline creation can succeed before one sampler allocation refuses.
    // Retain that pipeline and independently complete either missing sampler.
    if (!p.Point || !p.Linear) {
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        if (!p.Point) p.Point = SDL_CreateGPUSampler(GpuDevice,&sampler);
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        // The wave-distorted lookup can minify this capture even while its average
        // screen footprint is magnified. Keep level zero sharp, filtering only the
        // radiance footprint requested by the water shader's lookup derivatives.
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler.max_lod = 10.0f; // The largest supported capture is 1024 square.
        if (!p.Linear) p.Linear = SDL_CreateGPUSampler(GpuDevice,&sampler);
        if (!p.Point || !p.Linear) return fail(SDL_GetError());
    }
    const auto texture = [&](unsigned w, unsigned h, SDL_GPUTextureFormat f, SDL_GPUTextureUsageFlags usage,
                             unsigned levels=1) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = f; info.usage = usage;
        info.width = w; info.height = h; info.layer_count_or_depth = 1; info.num_levels = levels;
        return SDL_CreateGPUTexture(GpuDevice,&info);
    };
    if (p.Size != captureSize) {
        unsigned levels=1;
        for(unsigned side=captureSize;side>1;side/=2) ++levels;
        for (auto *&history : p.History) {
            if (history) SDL_ReleaseGPUTexture(GpuDevice,history);
            history = texture(captureSize,captureSize,format,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,levels);
        }
        if (!p.History[0] || !p.History[1]) return fail(SDL_GetError());
        p.Size = captureSize; p.Samples = 0; p.Current = 0;
    }
    if (geometryChanged) {
        uploadStateChanged = true;
        const Uint64 start = SDL_GetTicksNS();
        if (hardware) {
            if (!p.Scene.PackLinear(triangles,!revision,hardwarePath ? unsigned(staticTriangleCount) : 0))
                return fail("invalid or oversized triangle scene");
            ++p.LinearPacks;
        } else {
            // Software traversal retains its BVH/refit policy unchanged.
            if (p.Nodes && p.SinceBuild < 31 && p.Scene.Refit(triangles,!revision)) {
                ++p.Refits; ++p.SinceBuild;
            } else {
                if (!p.Scene.Build(triangles,!revision)) return fail("invalid or oversized triangle scene");
                ++p.Builds; p.SinceBuild = 0;
            }
        }
        p.BuildMs = double(SDL_GetTicksNS()-start)*1e-6;
        if (hardwarePath) {
            if (p.Scene.LinearStaticPrefixReused()) { ++p.Profile.RetainedPacks; p.Profile.RetainedPackMs+=p.BuildMs; }
            else if (p.Scene.LinearStaticGeometryReused()) { ++p.Profile.AlbedoPacks; p.Profile.AlbedoPackMs+=p.BuildMs; }
            else { ++p.Profile.FullPacks; p.Profile.FullPackMs+=p.BuildMs; }
        }
        if (profile) p.Profile.ScenePackMs += p.BuildMs;
        if (++p.PackedGeometryEpoch == 0) ++p.PackedGeometryEpoch;
    }
#if defined(__linux__)
    ZHR_SDLRayScene *&nativeScene = hardwarePath ? p.HardwarePathScene : p.HardwareScene;
    uint64_t &nativeEpoch = hardwarePath ? p.HardwarePathGeometryEpoch : p.HardwareGeometryEpoch;
    std::vector<unsigned char> &nativeSpirv = hardwarePath ? p.HardwarePathSpirv : p.HardwareSpirv;
    unsigned &nativeBuilds = hardwarePath ? p.HardwarePathBuilds : p.HardwareBuilds;
    unsigned &nativeCreates = hardwarePath ? p.HardwarePathCreates : p.HardwareCreates;
    unsigned &nativeUpdates = hardwarePath ? p.HardwarePathUpdates : p.HardwareUpdates;
    unsigned &nativeSkips = hardwarePath ? p.HardwarePathSkips : p.HardwareSkips;
    if (hardware && (!nativeScene || nativeEpoch != p.PackedGeometryEpoch)) {
        if (p.Scene.triangles.empty()) {
            // No capture is published for degenerate-only geometry. Retain the native
            // pipeline/AS allocation for a later valid packet without a device-wide idle.
            nativeEpoch = 0;
            p.Ready = false; p.Samples = 0;
            return false;
        }
        const Uint64 packingStart = profile ? SDL_GetTicksNS() : 0;
        auto &positions = p.NativePositions;
        auto &indices = p.NativeIndices;
        bool staticChanged = false, dynamicChanged = false;
        ZHR_SDLRayMeshView staticMesh{}, dynamicMesh{};
        if (hardwarePath) {
            // PackLinear drops degenerates while preserving source order. The TLAS
            // dynamic base is this packed count, never the original source count.
            const size_t staticCount = p.Scene.LinearStaticTriangleCount();
            if (nativeScene && nativeEpoch && p.Scene.LinearStaticGeometryReused()
                && p.StaticMesh.Indices.size()==staticCount*3) ++p.StaticNativePackSkips;
            else staticChanged = p.StaticMesh.Pack(p.Scene.triangles, 0, staticCount);
            dynamicChanged = p.DynamicMesh.Pack(p.Scene.triangles, staticCount, p.Scene.triangles.size() - staticCount);
            const auto meshView = [](const PathResources::NativeMesh &mesh) {
                return ZHR_SDLRayMeshView{mesh.Positions.data(), Uint32(mesh.Indices.size()),
                    mesh.Indices.data(), Uint32(mesh.Indices.size())};
            };
            staticMesh = meshView(p.StaticMesh); dynamicMesh = meshView(p.DynamicMesh);
        } else {
            positions.clear(); positions.reserve(p.Scene.triangles.size() * 9);
            const size_t indexCount = p.Scene.triangles.size() * 3;
            if (indices.size() != indexCount) {
                indices.resize(indexCount);
                for (size_t i = 0; i < indexCount; ++i) indices[i] = uint32_t(i);
            }
            for (const Triangle &triangle : p.Scene.triangles) for (Vec3 vertex : {triangle.a, triangle.b, triangle.c})
                positions.insert(positions.end(), {vertex.x, vertex.y, vertex.z});
        }
        if (profile) p.Profile.NativePackMs += double(SDL_GetTicksNS() - packingStart) * 1e-6;
        std::string error;
        bool changed = true;
        if (!nativeScene) {
            if (nativeSpirv.empty() &&
                !SDL3_Compile_Ray_Query_GLSL(hardwarePath ? HardwarePathQuerySource() : HardwareQuerySource(), nativeSpirv, error)) return fail(error);
            const Uint64 start = profile ? SDL_GetTicksNS() : 0;
            nativeScene = hardwarePath
                ? ZHR_SDLRayCreateWaterPathSplitScene(GpuDevice, &staticMesh, &dynamicMesh,
                    nativeSpirv.data(), nativeSpirv.size())
                : ZHR_SDLRayCreateWaterScene(GpuDevice, positions.data(), uint32_t(indices.size()),
                    indices.data(), uint32_t(indices.size()), nativeSpirv.data(), nativeSpirv.size());
            if (profile) { ++p.Profile.NativeCreates; p.Profile.NativeCreateMs += double(SDL_GetTicksNS() - start) * 1e-6; }
            if (!nativeScene) return fail(SDL_GetError());
            ++nativeCreates;
            staticChanged = !p.StaticMesh.Indices.empty();
            dynamicChanged = !p.DynamicMesh.Indices.empty();
        } else {
            const Uint64 start = profile ? SDL_GetTicksNS() : 0;
            const bool updated = hardwarePath
                ? ZHR_SDLRayUpdateWaterPathSplitScene(nativeScene,
                    staticChanged ? &staticMesh : nullptr, dynamicChanged ? &dynamicMesh : nullptr,
                    &staticChanged, &dynamicChanged)
                : ZHR_SDLRayUpdateScene(nativeScene, positions.data(), uint32_t(indices.size()),
                    indices.data(), uint32_t(indices.size()), &changed);
            if (profile) { ++p.Profile.NativeUpdates; p.Profile.NativeUpdateMs += double(SDL_GetTicksNS() - start) * 1e-6; }
            if (!updated) return fail(SDL_GetError()); // Native scene is retired only by safe teardown.
            ++nativeUpdates;
            if (hardwarePath) changed = staticChanged || dynamicChanged;
            if (!changed) { ++nativeSkips; if (profile) ++p.Profile.GeometrySkips; }
        }
        if (hardwarePath) {
            if (staticChanged && !p.StaticMesh.Indices.empty()) ++p.StaticBuilds;
            else if (!staticChanged && !p.StaticMesh.Indices.empty()) ++p.StaticReuses;
            if (dynamicChanged && !p.DynamicMesh.Indices.empty()) ++p.DynamicBuilds;
            else if (!dynamicChanged && !p.DynamicMesh.Indices.empty()) ++p.DynamicReuses;
        }
        nativeEpoch = p.PackedGeometryEpoch;
        if (changed) ++nativeBuilds;
        p.HardwareLogged = false;
        std::fprintf(stderr, "%s: revision=%llu packed-triangles=%zu AS-builds=%u native-pipeline-creates=%u updates=%u unchanged-skips=%u %s\n",
            hardwarePath ? "HardwareWaterPathTracing" : "HardwareWaterReflections",
            (unsigned long long)revision, p.Scene.triangles.size(), nativeBuilds, nativeCreates, nativeUpdates, nativeSkips,
            hardwarePath ? "primary+sun+continuation queries" : "one-bounce, no shadow rays");
    }
    if (hardware && (!p.HardwareHits || p.HardwareSize != captureSize)) {
        SDL_GPUTexture *hits = texture(captureSize, captureSize, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (!hits) return fail(SDL_GetError());
        if (p.HardwareHits) SDL_ReleaseGPUTexture(GpuDevice, p.HardwareHits);
        p.HardwareHits = hits; p.HardwareSize = captureSize;
        if (p.HardwareTransport) SDL_ReleaseGPUTexture(GpuDevice, p.HardwareTransport);
        p.HardwareTransport = nullptr;
    }
    if (hardwarePath && !p.HardwareTransport) {
        p.HardwareTransport = texture(captureSize, captureSize, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        if (!p.HardwareTransport) return fail(SDL_GetError());
    }
#endif
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    if (!commands) return fail(SDL_GetError());
    const auto abandon = [&](const std::string &why) {
        SDL_CancelGPUCommandBuffer(commands);
        return fail(why);
    };
    if (geometryChanged || materialsChanged) {
        const auto upload = [&](unsigned slot, SDL_GPUTexture *&destination, const std::vector<Float4> &data) {
            uploadStateChanged = true;
            const unsigned width = DataTextureWidth;
            const unsigned requiredHeight = std::max(1u,unsigned((data.size()+width-1)/width));
            const bool keepOneBounceRows = hardware && !hardwarePath && (slot == 1 || slot == 2);
            const bool keepCapacity = hardwarePath || keepOneBounceRows;
            const bool replace = !destination || (keepCapacity
                ? p.DataHeights[slot] < requiredHeight : p.DataHeights[slot] != requiredHeight);
            unsigned height = p.DataHeights[slot];
            if (replace) {
                height = requiredHeight;
                if (hardwarePath) {
                    height = std::max(1u,p.DataHeights[slot]);
                    while (height < requiredHeight) height *= 2;
                }
                SDL_GPUTexture *replacement = texture(width,height,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,SDL_GPU_TEXTUREUSAGE_SAMPLER);
                if (!replacement) return false;
                if (destination) SDL_ReleaseGPUTexture(GpuDevice,destination);
                destination = replacement; p.DataHeights[slot] = height;
                ++p.TextureReallocations;
            }
            auto &previous = p.UploadedRows[slot];
            // One-bounce shader loads use active triangle IDs. Only upload the used rows.
            unsigned firstRow = 0, endRow = keepOneBounceRows ? requiredHeight : height;
            if (hardwarePath && !replace && p.UploadSnapshotValid[slot]) {
                endRow = unsigned((std::max(data.size(),previous.size())+width-1)/width);
                const auto sameRow = [&](unsigned row) {
                    const size_t first = size_t(row)*width;
                    const size_t nextCount = first<data.size() ? std::min(size_t(width),data.size()-first) : 0;
                    const size_t oldCount = first<previous.size() ? std::min(size_t(width),previous.size()-first) : 0;
                    return nextCount==oldCount && (!nextCount
                        || std::memcmp(data.data()+first,previous.data()+first,nextCount*sizeof(Float4))==0);
                };
                while (firstRow<endRow && sameRow(firstRow)) ++firstRow;
                while (endRow>firstRow && sameRow(endRow-1)) --endRow;
                if (firstRow==endRow) { ++p.TextureUploadSkips; return true; }
            }
            const unsigned rows = endRow-firstRow;
            const unsigned bytes = width*rows*sizeof(Float4);
            if (!p.Uploads[slot] || p.UploadBytes[slot] < bytes) {
                SDL_GPUTransferBufferCreateInfo ti{};
                ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; ti.size = bytes;
                auto *replacement = SDL_CreateGPUTransferBuffer(GpuDevice,&ti);
                if (!replacement) return false;
                if (p.Uploads[slot]) SDL_ReleaseGPUTransferBuffer(GpuDevice,p.Uploads[slot]);
                p.Uploads[slot] = replacement; p.UploadBytes[slot] = bytes;
            }
            SDL_GPUTransferBuffer *transfer = p.Uploads[slot];
            void *mapped = SDL_MapGPUTransferBuffer(GpuDevice,transfer,true);
            if (!mapped) return false;
            const size_t first = size_t(firstRow)*width;
            const size_t count = first<data.size() ? std::min(size_t(rows)*width,data.size()-first) : 0;
            if (count) std::memcpy(mapped,data.data()+first,count*sizeof(Float4));
            std::memset(static_cast<unsigned char *>(mapped)+count*sizeof(Float4),0,bytes-count*sizeof(Float4));
            SDL_UnmapGPUTransferBuffer(GpuDevice,transfer);
            SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
            if (!copy) return false;
            SDL_GPUTextureTransferInfo source{}; source.transfer_buffer = transfer;
            source.pixels_per_row = width; source.rows_per_layer = rows;
            SDL_GPUTextureRegion region{}; region.texture = destination;
            region.y = firstRow; region.w = width; region.h = rows; region.d = 1;
            // A partial upload must retain the untouched static rows in this texture.
            SDL_UploadToGPUTexture(copy,&source,&region,false);
            SDL_EndGPUCopyPass(copy);
            p.TextureUploadRows += rows;
            if (hardwarePath) {
                previous.resize(data.size());
                if (count) std::memcpy(previous.data()+first,data.data()+first,count*sizeof(Float4));
                p.UploadSnapshotValid[slot] = true;
            } else {
                p.UploadSnapshotValid[slot] = false;
            }
            if (WaterPathUploadRefusalForTest == int(slot)) {
                WaterPathUploadRefusalForTest = -1;
                return SDL_SetError("injected water path upload refusal");
            }
            return true;
        };
        if (geometryChanged) {
            if (!hardware && !upload(0,p.Nodes,p.Scene.nodeTexels)) return abandon(SDL_GetError());
            if (!upload(1,p.Triangles,p.Scene.triangleTexels)) return abandon(SDL_GetError());
        }
        if (materialsChanged) {
            // Both packing layouts can omit degenerates; software BVHs can also reorder.
            // Attributes always follow the active source-slot map.
            std::vector<Float4> legacyAttributes;
            auto &attributes = hardwarePath ? p.MaterialAttributes : legacyAttributes;
            attributes.resize(p.Scene.triangles.size()*3);
            const PrimaryMaterial noMaterial{};
            const unsigned first = !p.UploadsNeedRefresh && hardwarePath && materials && p.MaterialValidation.StaticReused
                && p.Scene.LinearStaticGeometryReused() ? p.Scene.LinearStaticTriangleCount() : 0;
            p.StaticAttributeSkips += first;
            if (materials || hardwarePath) for (unsigned i=first;i<p.Scene.triangles.size();++i) {
                const PrimaryMaterial &m=materials ? materials->triangles[p.Scene.SourceTriangleIndex(i)] : noMaterial;
                if (!hardwarePath || std::memcmp(attributes.data()+i*3,&m,sizeof(m))!=0)
                    std::memcpy(attributes.data()+i*3,&m,sizeof(m));
            }
            if (!upload(2,p.Materials,attributes)) return abandon(SDL_GetError());
            if (uploadAtlas) {
                // A cancelled upload leaves the atlas unpublished. A later capture
                // must rebuild it before the snapshot can validate another packet.
                if (!p.MaterialPixelsUploaded) p.UploadSnapshotValid[3] = false;
                p.MaterialPixelsUploaded = false;
                if (!upload(3,p.Texels,pixels)) return abandon(SDL_GetError());
            }
        }
    }
#if defined(__linux__)
    if (hardware) {
        if (hardwarePath) {
            ZHR_SDLRayWaterPathParams params = {};
            static_assert(sizeof(params) == 112, "water path-query push constant ABI");
            std::memcpy(params.OriginWater, &constants.originWater, 4 * sizeof(float));
            std::memcpy(params.RayTopLeft, &constants.rayTopLeft, 4 * sizeof(float));
            std::memcpy(params.RayDx, &constants.rayDx, 4 * sizeof(float));
            std::memcpy(params.RayDy, &constants.rayDy, 4 * sizeof(float));
            std::memcpy(params.SunDirection, &constants.sunDirection, 4 * sizeof(float));
            params.SunDirection[3] = view.sunRadiance.x > 0 || view.sunRadiance.y > 0 || view.sunRadiance.z > 0;
            params.maxDistance = view.maxDistance; params.shadowDistance = view.shadowDistance;
            params.secondaryDistance = view.secondaryDistance; params.epsilon = constants.output.z;
            params.sampleIndex = p.Samples;
            if (!ZHR_SDLRayRecordWaterPath(p.HardwarePathScene, commands, p.Triangles,
                p.HardwareHits, p.HardwareTransport, captureSize, captureSize, &params)) return abandon(SDL_GetError());
        } else {
            ZHR_SDLRayWaterParams params = {};
            static_assert(sizeof(params) == 80, "water ray-query push constant ABI");
            std::memcpy(params.OriginWater, &constants.originWater, 4 * sizeof(float));
            std::memcpy(params.RayTopLeft, &constants.rayTopLeft, 4 * sizeof(float));
            std::memcpy(params.RayDx, &constants.rayDx, 4 * sizeof(float));
            std::memcpy(params.RayDy, &constants.rayDy, 4 * sizeof(float));
            params.maxDistance = view.maxDistance;
            if (!ZHR_SDLRayRecordWater(p.HardwareScene, commands, p.HardwareHits, captureSize, captureSize, &params))
                return abandon(SDL_GetError());
        }
        // Every successful native build/query record must be submitted. Keep the shading in a
        // separate SDL command buffer, ordered on the same queue, so later pass failures cannot
        // cancel a recorded first build and leave the native scene in a falsely built state.
        p.HardwareFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        if (!p.HardwareFence) return fail("hardware query submission failed; native scene cannot be reused");
        commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
        if (!commands) return fail(SDL_GetError());
    }
#endif
    if (!p.Samples) {
        // Even an unused previous sample has a defined value on the first dispatch.
        SDL_GPUColorTargetInfo clear{}; clear.texture = p.History[p.Current];
        clear.load_op = SDL_GPU_LOADOP_CLEAR; clear.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands,&clear,1,nullptr);
        if (!pass) return abandon(SDL_GetError());
        SDL_EndGPURenderPass(pass);
    }
    const unsigned next = 1-p.Current;
    SDL_GPUColorTargetInfo target{}; target.texture = p.History[next];
    target.load_op = SDL_GPU_LOADOP_DONT_CARE; target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands,&target,1,nullptr);
    if (!pass) return abandon(SDL_GetError());
    SDL_BindGPUGraphicsPipeline(pass,hardwarePath ? p.HardwarePathPipeline : hardware ? p.HardwarePipeline : p.Pipeline);
    SDL_GPUTextureSamplerBinding bindings[] = {{hardware ? p.Triangles : p.Nodes,p.Point},{p.Triangles,p.Point},{p.History[p.Current],p.Point},
        {p.Materials,p.Point},{p.Texels,p.Point},{p.HardwareHits,p.Point},{p.HardwareTransport,p.Point}};
    SDL_BindGPUFragmentSamplers(pass,0,bindings,hardwarePath ? 7 : hardware ? 6 : 5);
    p.LastView = constants;
    constants.output.x = constants.output.y = float(captureSize);
    constants.counts.x = float(p.Scene.nodeTexels.size()/2);
    constants.counts.y = float(p.Scene.triangleTexels.size()/4);
    constants.counts.z = float(p.Samples);
    SDL_PushGPUFragmentUniformData(commands,0,&constants,sizeof(constants));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    SDL_EndGPURenderPass(pass);
    // Build from the newly averaged linear RGBA history, including geometry coverage.
    // Temporal accumulation still reads only level zero via PathPrevious.Load.
    SDL_GenerateMipmapsForGPUTexture(commands,p.History[next]);
    const bool submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
    if (!submitted) return fail("capture submission failed");
    if (uploadAtlas) {
        p.MaterialPixels = pixels;
        p.MaterialPixelsUploaded = true;
    }
    p.Current = next; p.Samples = hardware && !hardwarePath ? 1 : p.Samples + 1; p.Ready = true;
    if (hardware && !p.HardwareLogged) {
        std::fprintf(stderr, "%s: traced+shaded %ux%u capture, material revision=%llu, mipmaps ready\n",
            hardwarePath ? "HardwareWaterPathTracing" : "HardwareWaterReflections",
            captureSize, captureSize, (unsigned long long)revision);
        p.HardwareLogged = true;
    } p.Fingerprint = fingerprint;
    p.Revision = revision; p.Source = &triangles;
    p.SourceStaticTriangleCount = hardwarePath ? staticTriangleCount : 0;
    p.MaterialSource = materials; p.MaterialFingerprint = materialFingerprint;
    p.UploadsNeedRefresh = false;
    return true;
}

SDL_GPUTexture *SdlGpuFrame::Water_Path_Texture() const
{
    return WaterPath && WaterPath->Ready ? WaterPath->History[WaterPath->Current] : nullptr;
}
bool SdlGpuFrame::Water_Hardware_Available(bool pathTracing) const
{
#if defined(__linux__)
    if (WaterPath) {
        if (WaterPath->HardwareDeviceLost) return false;
        const bool permanent = pathTracing ? WaterPath->HardwarePathCapabilityFailure : WaterPath->HardwareCapabilityFailure;
        const uint64_t failedGeneration = pathTracing ? WaterPath->HardwarePathFailureGeneration : WaterPath->HardwareFailureGeneration;
        if (!(pathTracing ? WaterPath->HardwarePathFailure : WaterPath->HardwareFailure).empty()
            && (permanent || failedGeneration == HardwareRayTracing::RequestGeneration())) return false;
    }
    char diagnostic[1024] = {};
    return ZHR_SDLRayQuerySupported(GpuDevice, diagnostic, sizeof(diagnostic));
#else
    return false;
#endif
}
SDL_GPUTexture *SdlGpuFrame::Water_Hardware_Hits() const
{
    return WaterPath && WaterPath->Ready && WaterPath->UsingHardware ? WaterPath->HardwareHits : nullptr;
}
SDL_GPUTexture *SdlGpuFrame::Water_Hardware_Transport() const
{
    return WaterPath && WaterPath->Ready && WaterPath->UsingHardwarePath ? WaterPath->HardwareTransport : nullptr;
}
SDL_GPUSampler *SdlGpuFrame::Water_Path_Sampler() const { return WaterPath ? WaterPath->Linear : nullptr; }
std::string SdlGpuFrame::Water_Path_Report() const
{
    if (!WaterPath) return "water path tracing inactive";
    std::ostringstream report;
    report << (WaterPath->UsingHardwarePath ? "hardware water path tracing: " : WaterPath->UsingHardware ? "hardware one-bounce water reflection: " : "water path tracing: ") << WaterPath->Scene.triangleTexels.size()/4 << " triangles, "
        << WaterPath->Size << "x" << WaterPath->Size << ", " << WaterPath->Samples << (WaterPath->UsingHardware && !WaterPath->UsingHardwarePath ? "/1 samples, " : "/64 samples, ")
        << WaterPath->Builds << " BVH builds, " << WaterPath->Refits << " refits, " << WaterPath->LinearPacks
        << " linear packs, last CPU pack " << WaterPath->BuildMs << " ms";
    report << "; native pipeline creates=" << WaterPath->HardwareCreates + WaterPath->HardwarePathCreates
        << ", AS builds=" << WaterPath->HardwareBuilds + WaterPath->HardwarePathBuilds
        << ", native updates=" << WaterPath->HardwareUpdates + WaterPath->HardwarePathUpdates
        << ", unchanged geometry skips=" << WaterPath->HardwareSkips + WaterPath->HardwarePathSkips
        << ", profiled fence waits=" << WaterPath->Profile.FenceWaits
        << ", profiled no-work returns=" << WaterPath->Profile.NoWork;
    report << ", static AS builds=" << WaterPath->StaticBuilds << ", static AS reuses=" << WaterPath->StaticReuses
        << ", dynamic geometry changes=" << WaterPath->DynamicBuilds << ", dynamic AS reuses=" << WaterPath->DynamicReuses;
    report << ", texture upload rows=" << WaterPath->TextureUploadRows
        << ", unchanged texture skips=" << WaterPath->TextureUploadSkips
        << ", data texture allocations=" << WaterPath->TextureReallocations
        << ", triangle texture rows=" << WaterPath->DataHeights[1]
        << ", material texture rows=" << WaterPath->DataHeights[2];
    report << ", static native pack skips=" << WaterPath->StaticNativePackSkips
        << ", static attribute records retained=" << WaterPath->StaticAttributeSkips
        << ", material records checked=" << WaterPath->MaterialValidation.CheckedMaterials
        << ", material records reused=" << WaterPath->MaterialValidation.ReusedMaterials
        << ", atlas texels checked=" << WaterPath->MaterialValidation.CheckedTexels
        << ", atlas texels reused=" << WaterPath->MaterialValidation.ReusedTexels;
#if defined(__linux__)
    ZHR_SDLRayScene *nativeScene = WaterPath->UsingHardwarePath ? WaterPath->HardwarePathScene : WaterPath->HardwareScene;
    if (WaterPath->UsingHardware && nativeScene) {
        ZHR_SDLRayWaterStorageStats storage{};
        if (ZHR_SDLRayGetWaterStorageStats(nativeScene, &storage)) {
            report << ", geometry allocations=" << storage.dynamicAllocations
                << ", geometry count reuses=" << storage.dynamicCountReuses
                << ", geometry reserve fallbacks=" << storage.reserveFallbacks
                << ", geometry active triangles=" << storage.activePrimitives
                << ", geometry triangle capacity=" << storage.primitiveCapacity
                << ", geometry allocation bytes=" << storage.allocationBytes;
            if (WaterPath->UsingHardwarePath)
                report << ", dynamic AS builds=" << storage.dynamicFullBuilds
                    << ", dynamic AS refits=" << storage.dynamicRefits
                    << ", dynamic periodic builds=" << storage.dynamicPeriodicBuilds
                    << ", dynamic pressure builds=" << storage.dynamicPressureBuilds
                    << ", dynamic refit ineligible=" << storage.refitIneligibleFallbacks
                    << ", dynamic refit allocation fallbacks=" << storage.refitAllocationFallbacks
                    << ", dynamic refit enabled=" << storage.dynamicRefitEnabled
                    << ", dynamic refit age=" << storage.dynamicRefitAge;
        }
    }
#endif
    if (!WaterPath->Failure.empty()) report << "; unavailable: " << WaterPath->Failure;
    if (!WaterPath->HardwareFailure.empty()) report << "; hardware unavailable: " << WaterPath->HardwareFailure;
    if (!WaterPath->HardwarePathFailure.empty()) report << "; hardware path unavailable: " << WaterPath->HardwarePathFailure;
    return report.str();
}

namespace WaterPathTracing {
bool Prepare(IDirect3DDevice9 *device, const std::vector<Triangle> &triangles, const View &view, uint64_t revision, const PrimaryMaterialPacket *materials)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Prepare_Water_Path(triangles,view,revision,materials);
}
bool HardwareRequested()
{
#if defined(__linux__)
    const char *value = std::getenv("ZH_HARDWARE_WATER_REFLECTIONS");
    return (value && std::strcmp(value, "1") == 0) || HardwarePathRequested();
#else
    return false;
#endif
}
bool HardwarePathRequested()
{
#if defined(__linux__)
    const char *value = std::getenv("ZH_HARDWARE_WATER_PATH_TRACING");
    return value && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}
bool HardwareAvailable(IDirect3DDevice9 *device, bool pathTracing)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Water_Hardware_Available(pathTracing);
}
bool HasCapture(IDirect3DDevice9 *device)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Water_Path_Texture();
}
bool PrepareHardware(IDirect3DDevice9 *device, const std::vector<Triangle> &triangles, const View &view,
    uint64_t revision, const PrimaryMaterialPacket *materials)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Prepare_Water_Path(triangles,view,revision,materials,true);
}
bool PrepareHardwarePath(IDirect3DDevice9 *device, const std::vector<Triangle> &triangles, const View &view,
    uint64_t revision, const PrimaryMaterialPacket *materials, size_t staticTriangleCount)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Prepare_Water_Path(triangles,view,revision,materials,true,true,staticTriangleCount);
}
void Clear(IDirect3DDevice9 *device)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    if (posix && posix->Get_Gpu()) posix->Get_Gpu()->Clear_Water_Path();
}
std::string Report(IDirect3DDevice9 *device)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() ? posix->Get_Gpu()->Water_Path_Report() : "water path tracing has no GPU device";
}
}
