// Project-owned global hardware RT gate and actual current-device probe.
#include "SdlHardwareSunShadow.h"
#include "SdlGpuFrame.h"
#include "PosixDevice9.h"
#include "Platform/HardwareRaySun.h"
#include <SDL3/SDL.h>
#include <set>
#include <memory>
#include <cmath>
#include <cstring>
namespace HardwareRayTracing {
namespace { bool enabled = false; uint64_t requestGeneration = 1; }
bool Requested() { return enabled; }
void SetRequested(bool value) { if (enabled != value) { enabled = value; ++requestGeneration; } }
uint64_t RequestGeneration() { return requestGeneration; }
}
#if defined(__linux__)
#include "../../../../Libraries/Source/SDL_gpu_vulkan_rayquery.h"

namespace HardwareRayTracing {
bool Probe(IDirect3DDevice9 *device, std::string &error)
{
    auto *posix=dynamic_cast<PosixDevice9 *>(device);
    SdlGpuFrame *frame=posix ? posix->Get_Gpu() : nullptr;
    if (!frame) { error="no active SDL GPU device"; return false; }
    static std::set<uint64_t> successfulDevices;
    if (successfulDevices.count(frame->Device_Generation())) { error.clear(); return true; }
    char diagnostic[1024] = {};
    if (!ZHR_SDLRayQuerySupported(frame->Device(),diagnostic,sizeof(diagnostic))) {
        error=diagnostic; return false;
    }
    // The left sample is blocked and the right sample is sunny. Both must be
    // produced by the active device; support bits or an untouched clear cannot pass.
    const float positions[]={-.75f,-.5f,.5f, -.25f,-.5f,.5f, -.25f,.5f,.5f, -.75f,.5f,.5f};
    const uint32_t indices[]={0,1,2,0,2,3};
    std::unique_ptr<SdlHardwareSunShadow> scene(SdlHardwareSunShadow::Create(*frame,positions,4,indices,6,error));
    if (!scene) return false;
    SDL_GPUTextureCreateInfo info = {};
    info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER; info.width=2; info.height=1;
    info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *depth=SDL_CreateGPUTexture(frame->Device(),&info);
    if (!depth) { error=SDL_GetError(); return false; }
    SDL_GPUTransferBufferCreateInfo transferInfo = {};
    transferInfo.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD; transferInfo.size=2*sizeof(float);
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(frame->Device(),&transferInfo);
    bool submitted=false;
    if (transfer) {
        float *mapped=static_cast<float *>(SDL_MapGPUTransferBuffer(frame->Device(),transfer,false));
        if (mapped) {
            mapped[0]=mapped[1]=.25f;
            SDL_UnmapGPUTransferBuffer(frame->Device(),transfer);
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
            if (commands) {
                SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
                if (copy) {
                    SDL_GPUTextureTransferInfo source = {}; source.transfer_buffer=transfer;
                    source.pixels_per_row=2; source.rows_per_layer=1;
                    SDL_GPUTextureRegion region = {}; region.texture=depth; region.w=2; region.h=region.d=1;
                    SDL_UploadToGPUTexture(copy,&source,&region,false);
                    SDL_EndGPUCopyPass(copy);
                    submitted=SDL_SubmitGPUCommandBuffer(commands);
                } else SDL_CancelGPUCommandBuffer(commands);
            }
        }
    }
    const float inverse[]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const float sun[]={0,0,1};
    const SdlTarget target={nullptr,nullptr,2,1};
    std::vector<uint8_t> bytes;
    const bool read=submitted && scene->RenderResolved(target,depth,inverse,sun,.001f,10,error) &&
        frame->Read_Back(scene->Mask(),2,1,bytes);
    float samples[2] = {-1,-1};
    if (read && bytes.size()==sizeof(samples)) std::memcpy(samples,bytes.data(),sizeof(samples));
    const bool passed=read && std::isfinite(samples[0]) && std::isfinite(samples[1]) &&
        std::fabs(samples[0])<.00001f && std::fabs(samples[1]-1)<.00001f;
    if (!passed && error.empty()) error=SDL_GetError()[0] ? SDL_GetError() : "ray-query known hit/miss readback failed";
    // Retire probe resources after its own queue work, without touching game attachments.
    scene.reset();
    if (transfer) SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);
    SDL_ReleaseGPUTexture(frame->Device(),depth);
    if (!passed) return false;
    successfulDevices.insert(frame->Device_Generation());
    SDL_Log("Hardware ray tracing probe passed: known hit=%.1f miss=%.1f; %s",samples[0],samples[1],diagnostic);
    error.clear(); return true;
}
bool Available(IDirect3DDevice9 *device) { std::string error; return Probe(device,error); }
}

#else

namespace HardwareRayTracing {
bool Probe(IDirect3DDevice9 *,std::string &error) { error="hardware ray tracing requires the SDL Vulkan device"; return false; }
bool Available(IDirect3DDevice9 *) { return false; }
}

#endif
