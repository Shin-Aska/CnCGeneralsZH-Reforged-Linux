// Independent main-scene normalized depth. The frame must outlive this cache.
#pragma once
#include "SdlGpuFrame.h"
#include <string>

struct SdlSceneDepthCapture
{
    SdlSceneCaptureRequest Identity;
    SDL_GPUTexture *Texture = nullptr;
    unsigned Width = 0, Height = 0, Format = 0;
};

struct SdlSceneColourCapture
{
    SdlSceneCaptureRequest Identity;
    SDL_GPUTexture *Texture = nullptr;
    unsigned Width = 0, Height = 0, Format = 0;
};

class SdlSceneCaptureCache
{
public:
    explicit SdlSceneCaptureCache(SdlGpuFrame &frame);
    ~SdlSceneCaptureCache();
    SdlSceneCaptureCache(const SdlSceneCaptureCache &) = delete;
    SdlSceneCaptureCache &operator=(const SdlSceneCaptureCache &) = delete;
    // Requires explicit nonzero main-view ownership and current frame/generation. All phases have
    // distinct storage; captures never borrow water snapshots. An unchanged identity reuses its copy.
    // Replacement submits all prior borrowers before overwriting. False always returns an empty view.
    bool Capture(const SdlSceneCaptureRequest &request, SdlSceneDepthCapture &result);
    // Cloak admission allocates before legacy looks are suppressed. Copying is
    // deferred to the first real cloaked mesh, then cached for this exact owner.
    bool Prepare_Colour(const SdlSceneCaptureRequest &request);
    bool Capture_Colour(const SdlSceneCaptureRequest &request, SdlSceneColourCapture &result);
    void Release(); // retires texture handles through the still-live frame
    const std::string &Error() const { return LastError; }
private:
    struct Entry { SdlSceneDepthCapture View; bool Ready = false; } Entries[4];
    SdlSceneColourCapture Colour;
    bool ColourReady = false;
    SdlGpuFrame &Frame;
    std::string LastError;
    bool Fail(const char *message);
};
