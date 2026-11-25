// Native sun-shaft transport and visual-only shroud filtering, using the DX11 programs.
#pragma once
#include "SdlPostProcess.h"
#include "Platform/SunShaftSettings.h"
#include <string>

class SdlPostSceneEffects {
public:
    SdlPostSceneEffects();
    ~SdlPostSceneEffects();
    SdlPostSceneEffects(const SdlPostSceneEffects &) = delete;
    SdlPostSceneEffects &operator=(const SdlPostSceneEffects &) = delete;
    bool Soften_Shroud(SdlGpuFrame &, SDL_GPUTexture *cells, unsigned width, unsigned height,
        uint64_t identity, float seconds, bool written, float fogLight, bool fogged);
    SDL_GPUTexture *Soft_Shroud() const;
    bool Fogged() const;
    bool Memory(SdlGpuFrame &, const SdlTarget &, const SdlPostSceneInputs &);
    bool Sun_Shafts(SdlGpuFrame &, const SdlTarget &, const SdlPostSceneInputs &,
        const RenderPost::SunShaftSettings &, SDL_GPUTexture *sunMap, const float sunFromWorld[16],
        float bias, float shadowStrength, SDL_GPUTexture *heights, const float heightsMapping[3]);
    bool Release(SdlGpuFrame &);
    const std::string &Error() const;
private:
    struct Impl;
    Impl *State;
};
