// Optional geometry path tracing for the water capture. Uses ordinary SDL GPU
// graphics resources; no Vulkan extensions or changes to the raster scene.
#include "SdlGpuFrame.h"
#include "PosixDevice9.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterPathTracingMotion.h"
#include "Platform/WaterPathTracingShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

struct SdlGpuFrame::PathResources {
    WaterPathTracing::Scene Scene;
    uint64_t Fingerprint = 0;
    uint64_t Revision = 0;
    const void *Source = nullptr;
    WaterPathTracing::Uniforms LastView{};
    SDL_GPUTexture *Nodes = nullptr, *Triangles = nullptr, *History[2]{};
    SDL_GPUTransferBuffer *Uploads[2]{};
    unsigned UploadBytes[2]{}, DataHeights[2]{};
    SDL_GPUSampler *Point = nullptr, *Linear = nullptr;
    SDL_GPUShader *Vertex = nullptr, *Pixel = nullptr;
    SDL_GPUGraphicsPipeline *Pipeline = nullptr;
    unsigned Size = 0, Samples = 0, Current = 0, Builds = 0, Refits = 0, SinceBuild = 0;
    WaterPathTracing::MotionRecovery Motion;
    bool Ready = false;
    double BuildMs = 0;
    std::string Failure;
};

void SdlGpuFrame::Clear_Water_Path()
{
    if (!WaterPath) return;
    // Submit recorded readers before dropping their bindings.
    if (!Flush()) {
        // Failed submission leaves queued draws intact. Their textures and sampler
        // must remain alive until a later clear succeeds (or frame destruction).
        WaterPath->Ready = false;
        WaterPath->Samples = 0;
        WaterPath->Source = nullptr;
        WaterPath->Revision = 0;
        return;
    }
    std::fprintf(stderr, "%s\n", Water_Path_Report().c_str());
    for (auto *texture : {WaterPath->Nodes, WaterPath->Triangles, WaterPath->History[0], WaterPath->History[1]})
        if (texture) SDL_ReleaseGPUTexture(GpuDevice, texture);
    for (auto *upload : WaterPath->Uploads)
        if (upload) SDL_ReleaseGPUTransferBuffer(GpuDevice, upload);
    if (WaterPath->Pipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, WaterPath->Pipeline);
    if (WaterPath->Vertex) SDL_ReleaseGPUShader(GpuDevice, WaterPath->Vertex);
    if (WaterPath->Pixel) SDL_ReleaseGPUShader(GpuDevice, WaterPath->Pixel);
    if (WaterPath->Point) SDL_ReleaseGPUSampler(GpuDevice, WaterPath->Point);
    if (WaterPath->Linear) SDL_ReleaseGPUSampler(GpuDevice, WaterPath->Linear);
    delete WaterPath; WaterPath = nullptr;
}

bool SdlGpuFrame::Prepare_Water_Path(const std::vector<WaterPathTracing::Triangle> &triangles,
    const WaterPathTracing::View &view, uint64_t revision)
{
    using namespace WaterPathTracing;
    if (!WaterPath) WaterPath = new PathResources;
    PathResources &p = *WaterPath;
    const auto fail = [&](const std::string &why) {
        if (p.Failure != why) std::fprintf(stderr, "Water path tracing unavailable: %s; using Hybrid\n", why.c_str());
        p.Failure = why; p.Ready = false; return false;
    };
    // A failed pipeline is retried after leaving/re-entering the mode, not each frame.
    if (!p.Failure.empty()) return false;
    if (view.size < 32 || view.size > 1024 || !std::isfinite(view.waterLevel)
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
    constants.output = Float4{float(view.size),float(view.size),0.02f,0};
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
    const bool geometryChanged = !p.Nodes || (revision
        ? p.Source != &triangles || p.Revision != revision
        : p.Revision != 0 || fingerprint != p.Fingerprint);
    const bool viewChanged = std::memcmp(&constants, &p.LastView, sizeof(constants)) != 0;
    const bool resolvingMotion = p.Motion.Observe(SDL_GetTicks(),
        p.Ready && (geometryChanged || viewChanged));
    // Motion invalidates the sample history anyway. Trace fewer pixels until the
    // scene settles, then restart accumulation at the requested capture density.
    const unsigned captureSize = resolvingMotion ? std::min(view.size,128u) : view.size;
    if (geometryChanged || viewChanged) p.Samples = 0;
    if (p.Ready && p.Samples >= 64 && p.Size == captureSize && !geometryChanged && !viewChanged) return true;
    // Previous water draws can still be queued against the history we're about to overwrite.
    if (!Flush()) return fail("pending draw submission failed");
    const SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    if (!p.Pipeline) {
        if (!SDL_GPUTextureSupportsFormat(GpuDevice, format, SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER))
            return fail("RGBA16F capture unsupported");
        std::vector<unsigned char> spirv; std::string error;
        if (!SDL3_Compile_HLSL_To_SPIRV(VertexSource(),true,spirv,error)
            || !(p.Vertex = SDL3_Create_Shader(GpuDevice,spirv,true,error))
            || !SDL3_Compile_HLSL_To_SPIRV(FragmentSource(),false,spirv,error)
            || !(p.Pixel = SDL3_Create_Shader(GpuDevice,spirv,false,error)))
            return fail(error);
        SDL_GPUColorTargetDescription target{}; target.format = format;
        SDL_GPUGraphicsPipelineCreateInfo pipeline{};
        pipeline.vertex_shader = p.Vertex; pipeline.fragment_shader = p.Pixel;
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.target_info.color_target_descriptions = &target;
        pipeline.target_info.num_color_targets = 1;
        p.Pipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        p.Point = SDL_CreateGPUSampler(GpuDevice,&sampler);
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
        p.Linear = SDL_CreateGPUSampler(GpuDevice,&sampler);
        if (!p.Pipeline || !p.Point || !p.Linear) return fail(SDL_GetError());
    }
    const auto texture = [&](unsigned w, unsigned h, SDL_GPUTextureFormat f, SDL_GPUTextureUsageFlags usage) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D; info.format = f; info.usage = usage;
        info.width = w; info.height = h; info.layer_count_or_depth = info.num_levels = 1;
        return SDL_CreateGPUTexture(GpuDevice,&info);
    };
    if (p.Size != captureSize) {
        for (auto *&history : p.History) {
            if (history) SDL_ReleaseGPUTexture(GpuDevice,history);
            history = texture(captureSize,captureSize,format,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
        }
        if (!p.History[0] || !p.History[1]) return fail(SDL_GetError());
        p.Size = captureSize; p.Samples = 0; p.Current = 0;
    }
    if (geometryChanged) {
        const Uint64 start = SDL_GetTicksNS();
        // Refit moving geometry without repartitioning the whole map. Periodic
        // rebuilds restore spatial quality after accumulated instance movement.
        if (p.Nodes && p.SinceBuild < 31 && p.Scene.Refit(triangles,!revision)) {
            ++p.Refits; ++p.SinceBuild;
        } else {
            if (!p.Scene.Build(triangles,!revision)) return fail("invalid or oversized triangle scene");
            ++p.Builds; p.SinceBuild = 0;
        }
        p.BuildMs = double(SDL_GetTicksNS()-start)*1e-6;
    }
    SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
    if (!commands) return fail(SDL_GetError());
    const auto abandon = [&](const std::string &why) {
        SDL_CancelGPUCommandBuffer(commands);
        return fail(why);
    };
    if (geometryChanged) {
        const auto upload = [&](unsigned slot, SDL_GPUTexture *&destination, const std::vector<Float4> &data) {
            const unsigned width = DataTextureWidth;
            const unsigned height = std::max(1u,unsigned((data.size()+width-1)/width));
            if (!destination || p.DataHeights[slot] != height) {
                SDL_GPUTexture *replacement = texture(width,height,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,SDL_GPU_TEXTUREUSAGE_SAMPLER);
                if (!replacement) return false;
                if (destination) SDL_ReleaseGPUTexture(GpuDevice,destination);
                destination = replacement; p.DataHeights[slot] = height;
            }
            const unsigned bytes = width*height*sizeof(Float4);
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
            if (!data.empty()) std::memcpy(mapped,data.data(),data.size()*sizeof(Float4));
            std::memset(static_cast<unsigned char *>(mapped)+data.size()*sizeof(Float4),
                0,bytes-data.size()*sizeof(Float4));
            SDL_UnmapGPUTransferBuffer(GpuDevice,transfer);
            SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
            if (!copy) return false;
            SDL_GPUTextureTransferInfo source{}; source.transfer_buffer = transfer;
            source.pixels_per_row = width; source.rows_per_layer = height;
            SDL_GPUTextureRegion region{}; region.texture = destination;
            region.w = width; region.h = height; region.d = 1;
            SDL_UploadToGPUTexture(copy,&source,&region,false);
            SDL_EndGPUCopyPass(copy);
            return true;
        };
        if (!upload(0,p.Nodes,p.Scene.nodeTexels) || !upload(1,p.Triangles,p.Scene.triangleTexels))
            return abandon(SDL_GetError());
    }
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
    SDL_BindGPUGraphicsPipeline(pass,p.Pipeline);
    SDL_GPUTextureSamplerBinding bindings[] = {{p.Nodes,p.Point},{p.Triangles,p.Point},{p.History[p.Current],p.Point}};
    SDL_BindGPUFragmentSamplers(pass,0,bindings,3);
    p.LastView = constants;
    constants.output.x = constants.output.y = float(captureSize);
    constants.counts.x = float(p.Scene.nodeTexels.size()/2);
    constants.counts.y = float(p.Scene.triangleTexels.size()/4);
    constants.counts.z = float(p.Samples);
    SDL_PushGPUFragmentUniformData(commands,0,&constants,sizeof(constants));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    SDL_EndGPURenderPass(pass);
    const bool submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
    if (!submitted) return fail("capture submission failed");
    p.Current = next; ++p.Samples; p.Ready = true; p.Fingerprint = fingerprint;
    p.Revision = revision; p.Source = &triangles;
    return true;
}

SDL_GPUTexture *SdlGpuFrame::Water_Path_Texture() const
{
    return WaterPath && WaterPath->Ready ? WaterPath->History[WaterPath->Current] : nullptr;
}
SDL_GPUSampler *SdlGpuFrame::Water_Path_Sampler() const { return WaterPath ? WaterPath->Linear : nullptr; }
std::string SdlGpuFrame::Water_Path_Report() const
{
    if (!WaterPath) return "water path tracing inactive";
    std::ostringstream report;
    report << "water path tracing: " << WaterPath->Scene.triangleTexels.size()/4 << " triangles, "
        << WaterPath->Size << "x" << WaterPath->Size << ", " << WaterPath->Samples << "/64 samples, "
        << WaterPath->Builds << " BVH builds, " << WaterPath->Refits << " refits, last update " << WaterPath->BuildMs << " ms";
    if (!WaterPath->Failure.empty()) report << "; unavailable: " << WaterPath->Failure;
    return report.str();
}

namespace WaterPathTracing {
bool Prepare(IDirect3DDevice9 *device, const std::vector<Triangle> &triangles, const View &view, uint64_t revision)
{
    auto *posix = dynamic_cast<PosixDevice9 *>(device);
    return posix && posix->Get_Gpu() && posix->Get_Gpu()->Prepare_Water_Path(triangles,view,revision);
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
