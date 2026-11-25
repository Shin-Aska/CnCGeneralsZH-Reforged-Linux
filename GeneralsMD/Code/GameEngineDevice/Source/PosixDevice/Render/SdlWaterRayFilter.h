#pragma once
#include <cstdint>
class SdlGpuFrame;
struct IDirect3DDevice9;
struct SDL_GPUTexture;
struct SdlTarget;
struct SdlWaterPixelConstants;
// Shader selection must also check same-device native feature support and keep
// its original plain shader fallback. This gate never changes user settings.
struct SDL_GPUDevice;
bool SdlWaterRayFilterSupported(SDL_GPUDevice *device);
// Call before staging/constants for this draw: first use may Flush the frame.
// All refusals write four zero words, preserving the ordinary pure SSR path.
bool SdlPrepareWaterRayFilter(SdlGpuFrame &,IDirect3DDevice9 *,const SdlTarget &,
    SDL_GPUTexture *depth,uint64_t mainViewKey,const SdlWaterPixelConstants &,uint32_t addresses[4]);
// No-flush final check after bindings: zero addresses if snapshot/matrix/frame
// changed since Prepare. Safe after current draw staging.
void SdlValidateWaterRayFilter(SdlGpuFrame &,const SdlTarget &,SDL_GPUTexture *,uint64_t mainViewKey,
    const SdlWaterPixelConstants &,uint32_t addresses[4]);
// Appends an exact uint4 at the end of the actual fragment b0. No vertex prefix,
// hull/tuning layout or additional uniform block is changed.

#include <vector>
bool SdlInstallWaterRayQuery(std::vector<unsigned char> &spirv);
