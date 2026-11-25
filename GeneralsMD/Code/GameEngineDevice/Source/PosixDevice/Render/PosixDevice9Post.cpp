// Explicit main-scene ownership. Mirrors, sun capture and arbitrary render textures keep their targets.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlHardwareSunShadow.h"
#include "Platform/HardwareRaySun.h"
#if defined(__linux__)
#include "../../../../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#endif
#include "SdlPostProcess.h"
#include "SdlPostAtmosphere.h"
#include "SdlPostDistortion.h"
#include "SdlPostSceneEffects.h"
#include "Platform/RenderParticles.h"
#include "Platform/RenderEffects.h"
#include "Platform/RenderTeamGlow.h"
#include "Platform/RenderWaterFoam.h"
#include "SdlConstants.h"
#include "ffshader.h"
#include "SdlSceneCapture.h"
#include "SdlSceneContext.h"
#include "SdlGpuParticlesContext.h"
#include "SdlGpuParticles.h"
#include "SdlResourceMirror.h"
#include "SdlRenderMatrix.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace {
SdlGpuFrame *provider_frame(IDirect3DDevice9 *device) { return device ? static_cast<PosixDevice9 *>(device)->Get_Gpu() : nullptr; }
bool supports_post(IDirect3DDevice9 *device) { return provider_frame(device) != nullptr; }
bool supports_hdr(IDirect3DDevice9 *device) {
    SdlGpuFrame *frame = provider_frame(device);
    return frame && SDL_GPUTextureSupportsFormat(frame->Device(), SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
bool supports_depth_capture(IDirect3DDevice9 *device) {
    SdlGpuFrame *frame = provider_frame(device);
    return frame && frame->Supports_Scene_Depth() && SDL_GPUTextureSupportsFormat(frame->Device(),
        SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
bool supports_ao(IDirect3DDevice9 *device) {
    SdlGpuFrame *frame = provider_frame(device);
    if (!frame || !frame->Supports_Scene_Depth()) return false;
    for (auto format : {SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT})
        if (!SDL_GPUTextureSupportsFormat(frame->Device(), format, SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) return false;
    return true;
}
const bool ModelsRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::ModelEffects, supports_post);
const bool TeamRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::TeamGlow, supports_hdr);
const bool SoftRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::SoftParticles, supports_depth_capture);
const bool FoamRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::WaterFoam, supports_depth_capture);
const bool FireRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::ProceduralFire, supports_post);
const bool PostRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::PostProcessing, supports_post);
const bool HdrRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::InternalHDR, supports_hdr);
const bool AoRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::AmbientOcclusion, supports_ao);
bool supports_environment(IDirect3DDevice9 *device) {
    return supports_depth_capture(device);
}
const bool AtmosphereRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::AtmosphereHaze, supports_environment);
bool supports_shafts(IDirect3DDevice9 *device) {
#if defined(__linux__)
    return supports_environment(device) && supports_ao(device);
#else
    return false;
#endif
}
const bool ShaftsRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::SunShafts, supports_shafts);
const bool DistortionRegistered = PosixRenderer_Register_Feature_Provider(RendererFeature::HeatBlastDistortion,
    [](IDirect3DDevice9 *device) { return supports_environment(device) && supports_hdr(device); });
}

struct PosixDevice9::PostSession {
    explicit PostSession(SdlGpuFrame &frame) : Captures(frame) {}
    SdlPostProcess Processor;
    SdlPostAtmosphere Atmosphere;
    SdlPostDistortion Distortion;
    SdlPostSceneEffects SceneEffects;
    bool ShaftsDone = false;
    bool HeatPending = false, BlastPending = false;
    bool EnvironmentReady = false;
    SdlSceneCaptureCache Captures;
    SdlPostSettings Settings;
    RenderPost::Environment Environment;
    bool EnvironmentFresh = false, EnvironmentCurrent = false;
    SdlTarget Scene = {}, Presentation = {};
    SdlMainViewSnapshot CurrentView, LastView, ViewStack[8], SoftView;
    bool Classic = false;
    SdlSceneColourCapture ModelColour;
    SdlMainViewSnapshot FoamView;
    uint64_t FoamRefusedKey = 0;
    uint64_t ModelColourRefusedKey = 0;
    unsigned ViewDepth = 0;
    uint64_t Serial = 0;
    bool Routed = false, InView = false, Entered = false, WorldAO = false, RequestedChain = false;
    float Near = 1, Far = 1200, WorldFromClip[16] = {}, ProjectionScale[2] = {};
    IDirect3DBaseTexture9 *StandInTexture = nullptr;
    IDirect3DSurface9 *StandInSurface = nullptr;
    SDL_GPUTexture *StandInCopy = nullptr;
    unsigned CopyWidth = 0, CopyHeight = 0, CopyFormat = 0;

    std::unique_ptr<SdlHardwareSunShadow> HardwareSun;
    SdlMainViewSnapshot HardwareView;
    SdlMainViewSnapshot SunPreflightView, SunPreflightOwner;
    D3DVIEWPORT9 SunPreflightViewport = {};
    uint64_t SunPreflightScene = 0;
    bool SunPreflightValid = false;
    bool MatchesSunPreflight(SdlGpuFrame *frame, uint64_t device, uint64_t scene) const {
        return SunPreflightValid && SunPreflightScene==scene && SunPreflightView.DeviceGeneration==device &&
            SunPreflightView.FrameSerial==frame->Scene_Frame() && SunPreflightView.Capture.Generation==frame->Scene_Generation() &&
            CurrentView.Pass==SdlSceneView::Main && CurrentView.ViewKey==SunPreflightOwner.ViewKey &&
            CurrentView.DeviceGeneration==SunPreflightOwner.DeviceGeneration &&
            CurrentView.FrameSerial==SunPreflightOwner.FrameSerial &&
            CurrentView.Capture.Same_Identity(SunPreflightOwner.Capture) &&
            CurrentView.Target.Colour==SunPreflightOwner.Target.Colour && CurrentView.Target.Depth==SunPreflightOwner.Target.Depth &&
            CurrentView.Target.Width==SunPreflightOwner.Target.Width && CurrentView.Target.Height==SunPreflightOwner.Target.Height &&
            !std::memcmp(CurrentView.ViewProjection,SunPreflightOwner.ViewProjection,sizeof(CurrentView.ViewProjection)) &&
            !std::memcmp(CurrentView.WorldFromClip,SunPreflightOwner.WorldFromClip,sizeof(CurrentView.WorldFromClip)) &&
            !std::memcmp(CurrentView.Viewport,SunPreflightOwner.Viewport,sizeof(CurrentView.Viewport)) &&
            !std::memcmp(CurrentView.Scissor,SunPreflightOwner.Scissor,sizeof(CurrentView.Scissor));
    }
    D3DVIEWPORT9 HardwareViewport = {};
    uint64_t HardwareRevision = 0, HardwareScene = 0, HardwareBuilds = 0;
    float HardwareLight[3] = {}, HardwareStrength = 0;
    bool HardwareOwned = false, HardwareDone = false, HardwareRetryMapped = false;
    bool HardwareDumped = false, HardwareLogged = false;
    std::string HardwareError, HardwareReportedError;

    bool inputs(PosixDevice9 &device, SdlGpuFrame &frame, SdlSceneCapturePhase phase, SdlPostSceneInputs &result) {
        result = {};
        if (LastView.Target.Colour != Scene.Colour || LastView.Target.Depth != Scene.Depth ||
            LastView.Target.Width != Scene.Width || LastView.Target.Height != Scene.Height ||
            LastView.ViewKey != Settings.MainViewKey || LastView.FrameSerial != frame.Scene_Frame() ||
            LastView.Capture.Generation != frame.Scene_Generation()) return false;
        SdlSceneCaptureRequest request;
        request.Source = Scene; request.Frame = frame.Scene_Frame(); request.Generation = frame.Scene_Generation();
        request.ViewKey = Settings.MainViewKey; request.Phase = phase; request.Depth = true;
        SdlSceneDepthCapture capture;
        if (!Captures.Capture(request, capture)) return false;
        result.Capture = capture.Identity; result.SampledDepth = capture.Texture;
        result.DepthWidth = capture.Width; result.DepthHeight = capture.Height; result.DepthFormat = capture.Format;
        result.ActualSamples = 1; result.Near = Near; result.Far = Far;
        std::memcpy(result.WorldFromClip, WorldFromClip, sizeof(WorldFromClip));
        std::memcpy(result.ProjectionScale, ProjectionScale, sizeof(ProjectionScale));
        if (EnvironmentCurrent && Environment.Shroud) {
            std::string error;
            result.Shroud = device.Get_Mirrors()->Texture(Environment.Shroud, error);
            if (!result.Shroud) return false;
            std::memcpy(result.ShroudRect, Environment.ShroudRect, sizeof(result.ShroudRect));
        }
        return true;
    }
};

void PosixDevice9::Set_Post_Environment(const RenderPost::Environment &environment)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    // Invalidate prepared borrowed views before releasing the old packet's COM
    // references. Already queued draws retain their native readers through batch retirement.
    Post->Atmosphere.Abandon(); Post->EnvironmentReady = false;
    IDirect3DBaseTexture9 *sky = Post->Environment.Sky, *shroud = Post->Environment.Shroud;
    Posix_Bind(sky, environment.Sky); Posix_Bind(shroud, environment.Shroud);
    Post->Environment = environment; Post->Environment.Sky = sky; Post->Environment.Shroud = shroud;
    Post->EnvironmentFresh = true;
    Post->EnvironmentCurrent = false;
}

void PosixDevice9::Set_Post_Heat(const float *records, unsigned count)
{
    if (!Post) return;
    PostSession &s = *Post; s.HeatPending = false;
    if (!Post_Active() || !s.RequestedChain || s.Classic || !s.InView || SunActive) return;
    SdlMainViewSnapshot main;
    if (!Read_Main_View(main, false, SdlSceneCapturePhase::OpaqueScene)) return;
    s.HeatPending = s.Distortion.Prepare_Heat(*Gpu, s.Processor, s.Scene, main.Capture, records, count) && count;
}
void PosixDevice9::Set_Post_Warps(const RenderPost::Warp *records, unsigned count)
{
    if (!Post) return;
    PostSession &s = *Post; s.BlastPending = false;
    if (!Post_Active() || !s.RequestedChain || s.Classic || SunActive || !s.Entered || (!records && count)) return;
    DX11PostWarp warps[DX11_POST_WARP_LIMIT] = {};
    const unsigned copied = count < DX11_POST_WARP_LIMIT ? count : DX11_POST_WARP_LIMIT;
    for (unsigned i = 0; i < copied; ++i) {
        const auto &from = records[i]; auto &to = warps[i];
        to.CentreX = from.CentreX; to.CentreY = from.CentreY; to.Radius = from.Radius; to.Pull = from.Pull;
        to.RingRadius = from.RingRadius; to.RingWidth = from.RingWidth; to.RingStrength = from.RingStrength; to.RingSquash = from.RingSquash;
    }
    // ViewScope has ended at the producer's pre-HUD hook. LastView is the explicit
    // completed primary view; the module checks its frame/target/key again.
    s.BlastPending = s.Distortion.Prepare_Blast_Warps(*Gpu, s.Processor, s.Scene, s.Presentation,
        s.LastView.Capture, warps, copied) && copied;
}

void PosixDevice9::Begin_Post(const RenderPost::Settings &settings)
{
    if (!Gpu) return;
    if (!Post) Post = std::make_shared<PostSession>(*Gpu);
    PostSession &s = *Post;
    if (s.Routed) Finish_Post(true);
    s.Atmosphere.Abandon(); s.EnvironmentReady = false;
    s.Distortion.Abandon(); s.HeatPending = s.BlastPending = false;
    s.InView = s.Entered = s.Routed = false;
    s.Classic = settings.Classic; s.SoftView = {};
    s.ShaftsDone = false;
    ParticlesAllowed = ModelEffectsAllowed = !settings.Classic;
    s.ModelColour = {}; s.ModelColourRefusedKey = 0; s.FoamView = {}; s.FoamRefusedKey = 0;
    s.CurrentView = {}; s.LastView = {}; s.ViewDepth = 0;
    s.EnvironmentCurrent = s.EnvironmentFresh && !settings.Classic && s.Environment.MapIdentity && s.Environment.MapGeneration;
    s.EnvironmentFresh = false;
    s.StandInTexture = nullptr; s.StandInSurface = nullptr;
    s.WorldAO = settings.WorldAO && !settings.Classic;
    s.Settings = {};
    s.RequestedChain = !settings.Classic && DX11Post_Parse_Chain(settings.Chain, s.Settings.Chain, s.Settings.Count) && s.Settings.Count;
    // Sun ray queries write light-space depth, and hardware water has its own capture.
    // Neither needs to route the main view through a copy-only post chain.
    if (!s.RequestedChain && !settings.Classic && (s.WorldAO || settings.IndependentScene || (s.EnvironmentCurrent &&
        (s.Environment.Haze.Strength > 0 || s.Environment.TransitionWarmHaze.Strength > 0 || s.Environment.Atmosphere.Strength > 0
            || s.Environment.Shafts.Strength > 0)))) {
        s.Settings.Count = 1; s.Settings.Chain[0] = DX11_POST_COPY;
    }
    if (++s.Serial == 0) ++s.Serial;
    s.Settings.MainViewKey = s.Serial;
    s.Settings.ActualSamples = 1;
    if (s.RequestedChain) {
        s.Settings.BloomThreshold = settings.BloomThreshold; s.Settings.BloomIntensity = settings.BloomIntensity;
        s.Settings.BloomSaturation = settings.BloomSaturation; s.Settings.Clarity = settings.Clarity;
        s.Settings.AdditiveGain = settings.AdditiveGain; s.Settings.BlurPasses = settings.BlurPasses;
        s.Settings.Grade = settings.Grade; s.Settings.InternalHDR = settings.InternalHDR;
    }
    s.Presentation = Gpu->Back_Buffer_Target();
    if (!s.Processor.Prepare(*Gpu, s.Presentation, s.Settings, s.Scene)) return;
    s.Routed = s.Processor.Active();
    if (s.Routed) Gpu->Set_Target(s.Scene);
}

unsigned PosixDevice9::Begin_Post_View(bool eligible, float nearPlane, float farPlane, bool reflection)
{
    if (!Post || !Gpu) return 0;
    PostSession &s = *Post;
    if (s.ViewDepth == 8) { s.CurrentView = {}; s.InView = false; return 0; }
    s.ViewStack[s.ViewDepth++] = s.CurrentView;
    const unsigned token = s.ViewDepth;
    s.CurrentView = {}; s.CurrentView.Pass = SunActive ? SdlSceneView::Sun :
        reflection ? SdlSceneView::Reflection : SdlSceneView::Other;
    s.InView = false;
    SdlTarget target = {}; std::string error;
    if (SunActive || reflection || !Resolve_Target(target, error) || !target.Colour || !target.Depth) return token;
    s.CurrentView.Target = target;
    if (!eligible || !Viewport.Width || !Viewport.Height || Viewport.X > target.Width || Viewport.Y > target.Height ||
        Viewport.Width > target.Width - Viewport.X || Viewport.Height > target.Height - Viewport.Y ||
        !std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0 || farPlane <= nearPlane) return token;
    SdlMainViewSnapshot view;
    view.Frame = Gpu; view.Target = target; view.Pass = SdlSceneView::Main;
    view.DeviceGeneration = GpuDeviceGeneration; view.FrameSerial = Gpu->Scene_Frame();
    const float viewport[] = {float(Viewport.X), float(Viewport.Y), float(Viewport.Width), float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ};
    std::memcpy(view.Viewport, viewport, sizeof(viewport));
    view.Scissor[0] = Viewport.X; view.Scissor[1] = Viewport.Y;
    view.Scissor[2] = Viewport.Width; view.Scissor[3] = Viewport.Height;
    multiply(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11, view.ViewProjection);
    if (!render_inverse(view.ViewProjection, view.WorldFromClip) ||
        !render_full_texture_to_world(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11,
            target.Width, target.Height, viewport, view.FullTextureWorldFromClip, view.ProjectionScale)) return token;
    if (s.Entered) {
        s.ShaftsDone = false;
        s.ModelColour = {}; s.ModelColourRefusedKey = 0; s.FoamView = {}; s.FoamRefusedKey = 0;
        s.Atmosphere.Abandon(); s.EnvironmentReady = false;
        s.Distortion.Abandon(); s.HeatPending = s.BlastPending = false;
        if (++s.Serial == 0) ++s.Serial;
        s.Settings.MainViewKey = s.Serial;
        if (s.Routed && !s.Processor.Prepare(*Gpu, s.Presentation, s.Settings, s.Scene)) s.Routed = false;
    }
    view.ViewKey = s.Settings.MainViewKey;
    view.Capture.Source = target;
    view.Capture.Source.ColourFormat = Gpu->Target_Colour_Format(target);
    view.Capture.Source.DepthFormat = Gpu->Target_Depth_Format(target);
    view.Capture.Frame = view.FrameSerial; view.Capture.Generation = Gpu->Scene_Generation();
    view.Capture.ViewKey = view.ViewKey;
    view.Near = nearPlane; view.Far = farPlane;
    s.CurrentView = s.LastView = view;
    std::memcpy(s.WorldFromClip, view.FullTextureWorldFromClip, sizeof(s.WorldFromClip));
    std::memcpy(s.ProjectionScale, view.ProjectionScale, sizeof(s.ProjectionScale));
    s.Near = nearPlane; s.Far = farPlane;
    s.InView = s.Entered = true;
    return token;
}

void PosixDevice9::End_Post_View(unsigned token)
{
    if (!Post || !token || token != Post->ViewDepth) return; // reset/teardown invalidates open scopes
    Post->CurrentView = Post->ViewStack[--Post->ViewDepth];
    Post->InView = Post->CurrentView.Pass == SdlSceneView::Main;
}

bool PosixDevice9::Read_Main_View(SdlMainViewSnapshot &snapshot, bool capture, SdlSceneCapturePhase phase)
{
    snapshot = {};
    if (!Gpu || !Post || !Post->InView || SunActive || Post->CurrentView.Pass != SdlSceneView::Main) return false;
    SdlMainViewSnapshot view = Post->CurrentView;
    SdlTarget actual = {}; std::string error;
    if (!Resolve_Target(actual, error)) return false;
    SdlSceneCaptureRequest current = view.Capture;
    current.Source = actual;
    current.Source.ColourFormat = Gpu->Target_Colour_Format(actual);
    current.Source.DepthFormat = Gpu->Target_Depth_Format(actual);
    current.Frame = Gpu->Scene_Frame(); current.Generation = Gpu->Scene_Generation();
    if (!current.Same_Identity(view.Capture) || view.DeviceGeneration != GpuDeviceGeneration) return false;
    view.Capture.Phase = phase; view.Capture.Depth = capture;
    if (capture) {
        SdlSceneDepthCapture depth;
        if (!Post->Captures.Capture(view.Capture, depth)) return false;
        view.Capture = depth.Identity; view.SampledDepth = depth.Texture;
        view.DepthWidth = depth.Width; view.DepthHeight = depth.Height; view.DepthFormat = depth.Format;
    }
    if (capture && phase == SdlSceneCapturePhase::BeforeTransparent) Post->SoftView = view;
    snapshot = view;
    return true;
}

bool PosixDevice9::Post_Active() const { return Post && Post->Routed; }
bool PosixDevice9::Post_Replaces_Bloom() const { return Post_Active() && Post->Processor.Replaces_Legacy_Bloom(); }
void PosixDevice9::Post_Stand_In(IDirect3DBaseTexture9 *texture, IDirect3DSurface9 *surface)
{
    if (Post) {
        // endRenderToTexture still samples the stand-in during its default-filter copy/bloom.
        if (texture) Post->StandInTexture = texture;
        Post->StandInSurface = surface;
    }
}
bool PosixDevice9::Post_Target(IDirect3DSurface9 *surface, SdlTarget &target) const
{
    if (!Post_Active() || (surface != BackBuffer && surface != Post->StandInSurface)) return false;
    target = Post->Scene; return true;
}
bool PosixDevice9::Post_Texture(IDirect3DBaseTexture9 *texture, SDL_GPUTexture *&result)
{
    if (!Post_Active() || !texture || texture != Post->StandInTexture) return false;
    PostSession &s = *Post; result = nullptr;
    const unsigned format = Gpu->Target_Colour_Format(s.Scene);
    if (!s.StandInCopy || s.CopyWidth != s.Scene.Width || s.CopyHeight != s.Scene.Height || s.CopyFormat != format) {
        SDL_GPUTextureCreateInfo info = {}; info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPUTextureFormat(format); info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = s.Scene.Width; info.height = s.Scene.Height; info.layer_count_or_depth = info.num_levels = 1;
        SDL_GPUTexture *replacement = SDL_CreateGPUTexture(Gpu->Device(), &info);
        if (!replacement) return true;
        if (s.StandInCopy) Gpu->Release_After_Batch(s.StandInCopy, nullptr);
        s.StandInCopy = replacement; s.CopyWidth = info.width; s.CopyHeight = info.height; s.CopyFormat = format;
    }
    const int32_t rect[4] = {0, 0, int32_t(s.Scene.Width), int32_t(s.Scene.Height)};
    Gpu->Record_Blit(s.Scene.Colour, rect, s.StandInCopy, rect, false);
    result = s.StandInCopy; return true;
}
float PosixDevice9::Post_Additive_Gain(const SdlTarget &target) const
{
    return !SunActive && Post_Active() && Post->InView && target.Colour == Post->Scene.Colour &&
        target.Depth == Post->Scene.Depth ? Post->Processor.Additive_Gain() : 1.0f;
}
void PosixDevice9::Post_Sky(bool drawSky)
{
    if (!Post_Active() || !Post->InView || !Post->EnvironmentCurrent) return;
    PostSession &s = *Post;
    SdlMainViewSnapshot view;
    if (!Read_Main_View(view, false, SdlSceneCapturePhase::OpaqueScene) ||
        view.Target.Colour != s.Scene.Colour || view.Target.Depth != s.Scene.Depth) return;
    SdlPostGroundHazeSettings haze;
    std::memcpy(haze.Colour, s.Environment.Haze.Colour, sizeof(haze.Colour));
    haze.Top = s.Environment.Haze.Top; haze.Thickness = s.Environment.Haze.Thickness;
    haze.Strength = s.Environment.Haze.Strength; haze.Reach = s.Environment.Haze.Reach;
    haze.WaterHeight = s.Environment.Haze.WaterHeight; haze.Warmth = s.Environment.Haze.Warmth;
    haze.BeforeWater = s.Environment.Haze.BeforeWater;
    std::memcpy(haze.TowardSun, s.Environment.Haze.TowardSun, sizeof(haze.TowardSun));
    SdlPostGroundHazeSettings warm;
    const auto &fromWarm = s.Environment.TransitionWarmHaze;
    std::memcpy(warm.Colour, fromWarm.Colour, sizeof(warm.Colour));
    warm.Top=fromWarm.Top;warm.Thickness=fromWarm.Thickness;warm.Strength=fromWarm.Strength;
    warm.Reach=fromWarm.Reach;warm.WaterHeight=fromWarm.WaterHeight;warm.Warmth=fromWarm.Warmth;
    std::memcpy(warm.TowardSun, fromWarm.TowardSun, sizeof(warm.TowardSun));
    SdlPostAtmosphereSettings atmosphere;
    std::memcpy(atmosphere.Horizon, s.Environment.Atmosphere.Horizon, sizeof(atmosphere.Horizon));
    std::memcpy(atmosphere.Tint, s.Environment.Atmosphere.Tint, sizeof(atmosphere.Tint));
    atmosphere.Start = s.Environment.Atmosphere.Start; atmosphere.Scale = s.Environment.Atmosphere.Scale;
    atmosphere.Strength = s.Environment.Atmosphere.Strength; atmosphere.Ground = s.Environment.Atmosphere.Ground;
    std::memcpy(atmosphere.MapSize, s.Environment.Atmosphere.MapSize, sizeof(atmosphere.MapSize));
    SdlPostAtmosphereViews views;
    views.MapIdentity = s.Environment.MapIdentity; views.MapGeneration = s.Environment.MapGeneration;
    std::memcpy(views.ShroudRect, s.Environment.ShroudRect, sizeof(views.ShroudRect));
    std::string error;
    auto mirror = [&](IDirect3DBaseTexture9 *source, SdlPostBorrowedView &destination) {
        if (!source) return true;
        SdlGpuParticleTextureView native;
        if (!SdlResourceMirrors::Native_Texture_View(source, native, error)) return false;
        destination.Texture = native.Texture; destination.Identity = native.Identity; destination.Generation = native.Generation;
        return true;
    };
    if (!mirror(s.Environment.Sky, views.Sky) || !mirror(s.Environment.Shroud, views.Shroud)) return;
    s.EnvironmentReady = s.Atmosphere.Prepare(*Gpu, s.Processor, s.Scene, view.Capture,
        view.FullTextureWorldFromClip, haze, atmosphere, views, warm);
    if (drawSky && s.EnvironmentReady && atmosphere.Strength > 0) s.Atmosphere.Draw_Sky(*Gpu, s.Scene);
}
void PosixDevice9::Post_Opaque(bool allowWorldAO)
{
    if (!Post_Active() || !Post->InView) return;
    const bool worldAO = allowWorldAO && Post->WorldAO;
    const bool weather = Post->EnvironmentCurrent && Post->Environment.Haze.BeforeWater && Post->Environment.Haze.Strength>0.0f;
    const bool haze = Post->EnvironmentCurrent &&
        (Post->Environment.Haze.Strength>0.0f || Post->Environment.TransitionWarmHaze.Strength>0.0f);
    // Warm haze still composes later, but its surface recorder must be ready
    // before the first water draw, including with sky and AO both disabled.
    if (haze && !Post->EnvironmentReady) Post_Sky(false);
    if (!worldAO && !weather) return;
    SdlPostSceneInputs inputs;
    if (Post->inputs(*this, *Gpu, SdlSceneCapturePhase::OpaqueScene, inputs)) {
        if (worldAO) Post->Processor.Apply_World_AO(*Gpu, Post->Scene, inputs);
        if (weather && Post->EnvironmentReady) Post->Atmosphere.Apply_Ground_Haze(*Gpu, Post->Scene, inputs);
    }
}
void PosixDevice9::Post_Water_Surface(const SdlTarget &target, const SdlRecordedDraw &draw, const SdlPipelineKey &key)
{
    if (!Post_Active() || !Post->InView || SunActive || !Post->EnvironmentReady ||
        Post->CurrentView.Pass != SdlSceneView::Main || target.Colour != Post->Scene.Colour ||
        target.Depth != Post->Scene.Depth) return;
    Post->Atmosphere.Record_Water_Surface(*Gpu, target, draw, key);
}
void PosixDevice9::Post_Before_Transparent()
{
    const auto particles = SdlGpuParticles::Stats();
    const bool particleDepth = particles.Capacity && particles.DeviceGeneration == GpuDeviceGeneration;
    if (Post && !Post->Classic && Post->InView && (Post_Active() || particleDepth)) {
        SdlMainViewSnapshot depth;
        Read_Main_View(depth, true, SdlSceneCapturePhase::BeforeTransparent);
    }
    if (!Post_Active() || !Post->InView) return;
    // A haze-only scene also needs preparation when the Atmosphere option did not
    // request the earlier sky hook. This performs no visible sky draw at strength0.
    if (Post->EnvironmentCurrent && !Post->EnvironmentReady) Post_Sky(false);
    SdlPostSceneInputs inputs;
    if (Post->inputs(*this, *Gpu, SdlSceneCapturePhase::BeforeTransparent, inputs)) {
        Post->Processor.Apply_Chain_AO(*Gpu, Post->Scene, inputs);
        if (Post->EnvironmentReady) {
            if (!Post->Environment.Haze.BeforeWater) Post->Atmosphere.Apply_Ground_Haze(*Gpu, Post->Scene, inputs);
            Post->Atmosphere.Apply_Transition_Warm_Haze(*Gpu, Post->Scene, inputs);
        }
        if (!Post->ShaftsDone && Post->EnvironmentCurrent && Post->Environment.Shafts.Strength>0) {
            SDL_GPUTexture *sunMap=nullptr;float sunFromWorld[16],bias=0,strength=0;
            if (Read_Sun_Post_View(sunMap,sunFromWorld,bias,strength)) {
                std::string error;
                SDL_GPUTexture *heights=TerrainHeights ? Mirrors->Texture(TerrainHeights,error) : nullptr;
                Post->ShaftsDone=Post->SceneEffects.Sun_Shafts(*Gpu,Post->Scene,inputs,Post->Environment.Shafts,
                    sunMap,sunFromWorld,bias,strength,heights,TerrainSun+33);
            }
        }
        if (Post->EnvironmentReady) {
            Post->Atmosphere.Apply_Atmosphere(*Gpu, Post->Scene, inputs);
        }
    }
}
void PosixDevice9::Finish_Post(bool fallback)
{
    if (!Post_Active()) return;
    PostSession &s = *Post;
    SdlPostSceneInputs inputs;
    const SdlPostSceneInputs *depth = !fallback && s.Entered && (s.Processor.Needs_BeforePost_Depth() || s.HeatPending) &&
        s.inputs(*this, *Gpu, SdlSceneCapturePhase::BeforePost, inputs) ? &inputs : nullptr;
    const bool normal = !fallback && s.Entered;
    if (normal && s.HeatPending) {
        // No earlier snapshot stands in for BeforePost. A missing capture refuses
        // this effect and consumes its records, while the normal chain still runs.
        s.Distortion.Apply_Heat(*Gpu, s.Scene, inputs);
        s.HeatPending = false;
    }
    bool finished = !normal ? s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation) :
        s.Processor.Finish(*Gpu, s.Scene, s.Presentation, depth);
    const bool normalFinished = normal && finished;
    if (!finished && normal) finished = s.Processor.Copy_Through(*Gpu, s.Scene, s.Presentation);
    if (!finished)
        std::fprintf(stderr, "PosixDevice9: cannot finish main scene: %s\n", s.Processor.Error().c_str());
    if (normalFinished && s.BlastPending) s.Distortion.Apply_Blast_Warp(*Gpu, s.Scene, s.Presentation);
    s.Routed = s.InView = false;
    s.Atmosphere.Abandon(); s.EnvironmentReady = false;
    s.Distortion.Abandon(); s.HeatPending = s.BlastPending = false;
    Gpu->Set_Target(s.Presentation);
}
void PosixDevice9::Release_Post()
{
    if (!Post || !Gpu) return;
    Clear_Hardware_Sun(true);
    if (!Post->SceneEffects.Release(*Gpu)) {
        Gpu->Discard_Pending_Work_For_Teardown();
        Post->SceneEffects.Release(*Gpu);
    }
    if (!Post->Distortion.Release(*Gpu)) {
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Distortion.Release(*Gpu)) std::fprintf(stderr, "PosixDevice9: distortion teardown: %s\n", Post->Distortion.Error().c_str());
    }
    if (!Post->Atmosphere.Release(*Gpu)) {
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Atmosphere.Release(*Gpu)) std::fprintf(stderr, "PosixDevice9: atmosphere teardown: %s\n", Post->Atmosphere.Error().c_str());
    }
    if (!Post->Processor.Release(*Gpu)) {
        Gpu->Discard_Pending_Work_For_Teardown();
        if (!Post->Processor.Release(*Gpu)) std::fprintf(stderr, "PosixDevice9: post teardown: %s\n", Post->Processor.Error().c_str());
    }
    if (Post->StandInCopy) Gpu->Release_After_Batch(Post->StandInCopy, nullptr);
    Post->StandInCopy = nullptr;
    Posix_Bind(Post->Environment.Sky, static_cast<IDirect3DBaseTexture9 *>(nullptr));
    Posix_Bind(Post->Environment.Shroud, static_cast<IDirect3DBaseTexture9 *>(nullptr));
    Post.reset();
}
void PosixDevice9::Invalidate_Post_Route()
{
    if (!Post) return;
    Clear_Hardware_Sun(true);
    Post->Atmosphere.Abandon(); Post->EnvironmentReady = false;
    Post->Distortion.Abandon(); Post->HeatPending = Post->BlastPending = false;
    Post->ModelColour = {}; Post->ModelColourRefusedKey = 0; Post->FoamView = {}; Post->FoamRefusedKey = 0;
    Post->Routed = Post->InView = Post->Entered = false;
    Post->CurrentView = {}; Post->LastView = {}; Post->ViewDepth = 0;
    Post->StandInSurface = nullptr; Post->StandInTexture = nullptr;
}

bool PosixDevice9::Hardware_Sun_Available() const
{
#if defined(__linux__)
    const bool available = (HardwareRaySun::Requested() || (HardwareRayTracing::Requested()
        && HardwareRayTracing::Available(const_cast<PosixDevice9 *>(this)))) && Gpu && Post && !Post->Classic && Post->InView &&
        Post->CurrentView.Pass == SdlSceneView::Main && SunSceneDepth == 1 && !SunSceneExclusions &&
        ZHR_SDLRayQuerySupported(Gpu->Device(), nullptr, 0);
    if (!SunActive && Post) {
        // A failed late preparation must not expose the previous frame's scene.
        Post->SunPreflightValid=false;
        Post->HardwareOwned=Post->HardwareDone=false;
        Post->HardwareView={};
    }
    if (!available) return false;
    if (SunActive) return Post->MatchesSunPreflight(Gpu,GpuDeviceGeneration,SunSceneGeneration);
    SdlMainViewSnapshot view;
    if (!const_cast<PosixDevice9*>(this)->Read_Main_View(view,false,SdlSceneCapturePhase::OpaqueScene)) return false;
    Post->SunPreflightView=view;
    Post->SunPreflightOwner=Post->CurrentView;
    Post->SunPreflightViewport=Viewport;
    Post->SunPreflightScene=SunSceneGeneration;
    Post->SunPreflightValid=true;
    return true;
#else
    return false;
#endif
}

bool PosixDevice9::Prepare_Hardware_Sun(const float *positions, uint32_t vertexCount, uint64_t revision,
    const float towardsSun[3], float strength, const std::vector<uint32_t> &meshTriangleEnds,
    const std::vector<uint32_t> &buildingTriangleEnds)
{
    if (!Hardware_Sun_Available() || !Post || Post->Classic || !Post->InView ||
        SunSceneDepth != 1 || SunSceneExclusions || !positions ||
        !vertexCount || vertexCount % 3 || !revision || !towardsSun || !std::isfinite(strength) || strength <= 0) return false;
    PostSession &s = *Post;
    s.HardwareOwned = s.HardwareDone = false;
    float lengthSquared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(towardsSun[i])) return false;
        lengthSquared += towardsSun[i] * towardsSun[i];
    }
    if (lengthSquared < 1e-8f) return false;
    SdlMainViewSnapshot view;
    auto refused = [&](const std::string &message) {
        if (message != s.HardwareReportedError) std::fprintf(stderr, "HardwareRaySun: preflight refused: %s; mapped fallback\n", message.c_str());
        s.HardwareReportedError = message;
        s.HardwareError = message;
        s.HardwareSun.reset(); s.HardwareRevision = 0;
        return false;
    };
    if (SunActive) {
        if (!s.MatchesSunPreflight(Gpu,GpuDeviceGeneration,SunSceneGeneration))
            return refused("late geometry lost saved main-view provenance");
        view=s.SunPreflightView;
    } else if (!Read_Main_View(view, false, SdlSceneCapturePhase::OpaqueScene))
        return refused("main-view preflight identity/target metadata unavailable");
    if (!s.HardwareSun || s.HardwareRevision != revision) {
        const bool existing = bool(s.HardwareSun);
        if (existing) {
            if (!s.HardwareSun->UpdateGeometry(positions, vertexCount, nullptr, vertexCount,
                    s.HardwareError, meshTriangleEnds, buildingTriangleEnds))
                return refused(s.HardwareError);
        } else {
            s.HardwareSun.reset(SdlHardwareSunShadow::Create(*Gpu, positions, vertexCount, nullptr, vertexCount,
                s.HardwareError, meshTriangleEnds, buildingTriangleEnds));
            if (!s.HardwareSun) return refused(s.HardwareError);
        }
        s.HardwareRevision = revision;
        ++s.HardwareBuilds;
        s.HardwareLogged = s.HardwareDumped = false;
        std::fprintf(stderr, "HardwareRaySun: %s revision=%llu vertices=%u triangles=%u scene-builds=%llu pipeline=%s\n",
            s.HardwareSun->Diagnostics().c_str(), (unsigned long long)revision, vertexCount, vertexCount / 3,
            (unsigned long long)s.HardwareBuilds, existing ? "reused" : "created");
    }
    s.HardwareView = view;
    s.HardwareViewport = SunActive ? s.SunPreflightViewport : Viewport;
    s.HardwareScene = SunSceneGeneration;
    std::memcpy(s.HardwareLight, towardsSun, sizeof(s.HardwareLight));
    s.HardwareStrength = strength < 1 ? strength : 1;
    s.HardwareError.clear();
    return true;
}

bool PosixDevice9::Hardware_Sun_Owns_Target(const SdlTarget &target) const
{
    if (!Gpu || !Post || !Post->HardwareOwned || !Post->HardwareSun || !Post->InView ||
        Post->CurrentView.Pass != SdlSceneView::Main || SunActive || SunSceneDepth != 1 || SunSceneExclusions) return false;
    const PostSession &s = *Post;
    const SdlMainViewSnapshot &view = s.HardwareView;
    return s.HardwareScene == SunSceneGeneration && view.DeviceGeneration == GpuDeviceGeneration &&
        view.FrameSerial == Gpu->Scene_Frame() && view.Capture.Generation == Gpu->Scene_Generation() &&
        view.ViewKey == s.CurrentView.ViewKey && target.Colour == view.Target.Colour &&
        target.Depth == view.Target.Depth && target.Width == view.Target.Width && target.Height == view.Target.Height &&
        std::memcmp(&Viewport, &s.HardwareViewport, sizeof(Viewport)) == 0;
}

bool PosixDevice9::Hardware_Sun_Owns()
{
    if (!Gpu || !Post || !Post->HardwareOwned) return false;
    SdlTarget target = {}; std::string error;
    return Resolve_Target(target, error) && Hardware_Sun_Owns_Target(target);
}

void PosixDevice9::Clear_Hardware_Sun(bool release)
{
    if (!Post) return;
    if (release && SunSceneExclusions) return; // Nested-view invalidation cannot retire the main scene.
    Post->HardwareOwned = Post->HardwareDone = false;
    Post->HardwareView = {};
    Post->SunPreflightValid = false;
    if (release) {
        Post->HardwareSun.reset(); Post->HardwareRevision = 0;
        Post->HardwareRetryMapped = false;
        Post->HardwareDumped = Post->HardwareLogged = false;
    }
}

bool PosixDevice9::Merge_Hardware_Sun_Depth(const SdlTarget &target,const float *viewProjection, bool submit)
{
    if (!Gpu || !Post || !Post->HardwareSun || Post->HardwareDone ||
        Post->Classic || Post->HardwareScene!=SunSceneGeneration || SunSceneExclusions || !viewProjection
        || !Post->InView || Post->CurrentView.Pass != SdlSceneView::Main
        || Post->HardwareView.DeviceGeneration != GpuDeviceGeneration
        || Post->HardwareView.FrameSerial != Gpu->Scene_Frame()
        || Post->HardwareView.Capture.Generation != Gpu->Scene_Generation()
        || Post->HardwareView.ViewKey != Post->CurrentView.ViewKey
        || (!HardwareRaySun::Requested() && !HardwareRayTracing::Requested())) return false;
    auto &s=*Post;s.HardwareDone=true;
    float inverse[16];
    bool merged=render_inverse(viewProjection,inverse)
        && s.HardwareSun->RenderSunDepth(target,inverse,s.HardwareError)
        && s.HardwareSun->MergeSunDepth(target,s.HardwareError,submit);
    s.HardwareOwned=merged;
    if (!merged) {
        if (s.HardwareError!=s.HardwareReportedError) {
            std::fprintf(stderr,"HardwareRaySun: sun-depth merge refused: %s; mapped coverage retained\n",s.HardwareError.c_str());
            s.HardwareReportedError=s.HardwareError;
        }
        return false;
    }
    if (!s.HardwareLogged) {
        std::fprintf(stderr,"HardwareRaySun: %s closest static depth revision=%llu scene=%llu size=%ux%u\n",
            submit ? "submitted" : "queued",
            (unsigned long long)s.HardwareRevision,(unsigned long long)s.HardwareScene,target.Width,target.Height);
        s.HardwareLogged=true;
    }
    return true;
}

namespace HardwareRaySun {
bool Requested()
{
#if defined(__linux__)
    const char *value = std::getenv("ZH_HARDWARE_SUN_SHADOW");
    return value && std::strcmp(value, "1") == 0;
#else
    return false;
#endif
}
bool Available(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Hardware_Sun_Available(); }
bool PrepareScene(IDirect3DDevice9 *device, const float *positions, uint32_t vertexCount,
    uint64_t revision, const float towardsSun[3], float strength,
    const std::vector<uint32_t> &meshTriangleEnds, const std::vector<uint32_t> &buildingTriangleEnds)
{
    return device && static_cast<PosixDevice9 *>(device)->Prepare_Hardware_Sun(positions, vertexCount,
        revision, towardsSun, strength, meshTriangleEnds, buildingTriangleEnds);
}
bool CommitMappedDepth(IDirect3DDevice9 *device)
{
    return device && static_cast<PosixDevice9 *>(device)->Commit_Hardware_Sun_Depth();
}
void Invalidate(IDirect3DDevice9 *device) { if (device) static_cast<PosixDevice9 *>(device)->Clear_Hardware_Sun(true); }
bool OwnsCurrentScene(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Hardware_Sun_Owns(); }
}

namespace RenderPost {
bool SunShaftsAvailable(IDirect3DDevice9 *d) { return supports_shafts(d); }
bool DistortionAvailable(IDirect3DDevice9 *d) { return supports_environment(d) && supports_hdr(d); }
bool EnvironmentAvailable(IDirect3DDevice9 *d) {
    return supports_depth_capture(d);
}
void BeginMain(IDirect3DDevice9 *d, const Settings &s) { if (d) static_cast<PosixDevice9 *>(d)->Begin_Post(s); }
void SetEnvironment(IDirect3DDevice9 *d, const Environment &e) { if (d) static_cast<PosixDevice9 *>(d)->Set_Post_Environment(e); }
void SetHeatSplats(IDirect3DDevice9 *d, const float *records, unsigned count) { if (d) static_cast<PosixDevice9 *>(d)->Set_Post_Heat(records,count); }
void SetWarps(IDirect3DDevice9 *d, const Warp *records, unsigned count) { if (d) static_cast<PosixDevice9 *>(d)->Set_Post_Warps(records,count); }
unsigned BeginView(IDirect3DDevice9 *d, bool e, float n, float f, bool r) { return d ? static_cast<PosixDevice9 *>(d)->Begin_Post_View(e,n,f,r) : 0; }
void EndView(IDirect3DDevice9 *d, unsigned p) { if (d) static_cast<PosixDevice9 *>(d)->End_Post_View(p); }
void Sky(IDirect3DDevice9 *d) { if (d) static_cast<PosixDevice9 *>(d)->Post_Sky(); }
void OpaqueScene(IDirect3DDevice9 *d, bool allowWorldAO) { if (d) static_cast<PosixDevice9 *>(d)->Post_Opaque(allowWorldAO); }
void BeforeTransparent(IDirect3DDevice9 *d) { if (d) static_cast<PosixDevice9 *>(d)->Post_Before_Transparent(); }
void FinishScene(IDirect3DDevice9 *d, bool f) { if (d) static_cast<PosixDevice9 *>(d)->Finish_Post(f); }
bool ReplacesLegacyBloom(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Replaces_Bloom(); }
bool SceneActive(IDirect3DDevice9 *d) { return d && static_cast<PosixDevice9 *>(d)->Post_Active(); }
void SetSceneStandIn(IDirect3DDevice9 *d, IDirect3DBaseTexture9 *t, IDirect3DSurface9 *s) { if (d) static_cast<PosixDevice9 *>(d)->Post_Stand_In(t,s); }
void SetGlowDraw(IDirect3DDevice9 *d, bool e) { if (d) static_cast<PosixDevice9 *>(d)->Set_Glow_Draw(e); }
}

bool PosixRenderer_Main_View(IDirect3DDevice9 *device, SdlMainViewSnapshot &snapshot)
{
    snapshot = {};
    return device && static_cast<PosixDevice9 *>(device)->Read_Main_View(snapshot, false, SdlSceneCapturePhase::OpaqueScene);
}
bool PosixRenderer_Capture_Main_View(IDirect3DDevice9 *device, SdlSceneCapturePhase phase, SdlMainViewSnapshot &snapshot)
{
    snapshot = {};
    return device && static_cast<PosixDevice9 *>(device)->Read_Main_View(snapshot, true, phase);
}

bool PosixDevice9::Read_Particle_Context(SdlGpuParticlesContext &context)
{
    context = {};
    if (!Gpu || !Mirrors) return false;
    context.Frame = Gpu; context.DeviceGeneration = GpuDeviceGeneration;
    context.FrameSerial = Gpu->Scene_Frame();
    context.MirrorTexture = SdlResourceMirrors::Native_Texture_View;
    std::string error;
    if (!Resolve_Target(context.Target, error)) return true; // admission remains valid outside a draw
    const float viewport[] = {float(Viewport.X), float(Viewport.Y), float(Viewport.Width), float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ};
    std::memcpy(context.Viewport, viewport, sizeof(viewport));
    context.Scissor[0] = Viewport.X; context.Scissor[1] = Viewport.Y;
    context.Scissor[2] = Viewport.Width; context.Scissor[3] = Viewport.Height;
    if (SunActive) { context.Pass = SdlGpuParticlesPass::Sun; return true; }
    if (Post && Post->CurrentView.Pass == SdlSceneView::Reflection) {
        context.Pass = SdlGpuParticlesPass::Reflection; return true;
    }
    SdlMainViewSnapshot view;
    if (!Read_Main_View(view, false, SdlSceneCapturePhase::BeforeTransparent)) return true;
    context.Pass = SdlGpuParticlesPass::MainScene; context.MainViewKey = view.ViewKey;
    context.AdditiveGain = Post_Additive_Gain(context.Target);
    if (ParticlesAllowed && !Post->Classic && Post->SoftView.SampledDepth &&
        Post->SoftView.Capture.Same_Identity(view.Capture)) {
        context.SoftAllowed = true; context.SoftDepth = Post->SoftView.SampledDepth;
        context.SoftCapture = Post->SoftView.Capture;
    }
    return true;
}
bool PosixGpuParticles_Context(IDirect3DDevice9 *device, SdlGpuParticlesContext &context)
{
    context = {};
    return device && static_cast<PosixDevice9 *>(device)->Read_Particle_Context(context);
}

void PosixDevice9::Take_Particle_Depth()
{
    if (!ParticlesAllowed) return;
    SdlMainViewSnapshot depth;
    Read_Main_View(depth, true, SdlSceneCapturePhase::BeforeTransparent);
}
namespace RenderParticles {
void AllowEnhancements(IDirect3DDevice9 *device, bool allowed) { if(device) static_cast<PosixDevice9 *>(device)->Allow_Particle_Enhancements(allowed); }
void SetSoftParticles(IDirect3DDevice9 *device, bool enabled) { if(device) static_cast<PosixDevice9 *>(device)->Set_Soft_Particles(enabled); }
void SetFireLook(IDirect3DDevice9 *device, bool enabled) { if(device) static_cast<PosixDevice9 *>(device)->Set_Fire_Look(enabled); }
void TakeSceneDepth(IDirect3DDevice9 *device) { if(device) static_cast<PosixDevice9 *>(device)->Take_Particle_Depth(); }
}

bool PosixDevice9::Effect_Main_View(SdlMainViewSnapshot &view)
{
    if (!Read_Main_View(view, false, SdlSceneCapturePhase::OpaqueScene)) return false;
    // Cosmetic meshes must match their live camera, while independent scene consumers
    // retain the captured main camera through temporary camera-space draw setters.
    const float viewport[] = {float(Viewport.X), float(Viewport.Y), float(Viewport.Width), float(Viewport.Height), Viewport.MinZ, Viewport.MaxZ};
    float viewProjection[16];
    multiply(&Transforms[D3DTS_VIEW]._11, &Transforms[D3DTS_PROJECTION]._11, viewProjection);
    if (std::memcmp(view.Viewport, viewport, sizeof(viewport)) ||
        std::memcmp(view.ViewProjection, viewProjection, sizeof(viewProjection))) return false;
    const auto direct = Gpu->Back_Buffer_Target();
    const bool main = (view.Target.Colour == direct.Colour && view.Target.Depth == direct.Depth) ||
        (Post_Active() && view.Target.Colour == Post->Scene.Colour && view.Target.Depth == Post->Scene.Depth) ||
        (Post && Post->StandInSurface && RenderTargets[0] == Post->StandInSurface);
    // Only the default scene filter explicitly registers this stand-in. Other
    // filter textures and arbitrary RTTs retain their legacy material treatment.
    if (!main) view = {};
    return main;
}
bool PosixDevice9::Takes_Model_Effect(unsigned effect)
{
    SdlMainViewSnapshot main;
    if (!Model_Effects_Available() || effect == DRAW_EFFECT_NONE || effect >= DRAW_EFFECT_CLOAK_SHADOW ||
        !Effect_Main_View(main)) return false;
    if (effect != DRAW_EFFECT_CLOAK) return true;
    if (Post->ModelColourRefusedKey == main.ViewKey) return false;
    if (ModelRefusalForTest & 1) { ModelRefusalForTest &= ~1u; Post->ModelColourRefusedKey = main.ViewKey; return false; }
    main.Capture.Phase = SdlSceneCapturePhase::CloakScene; main.Capture.Colour = true;
    if (!Post->Captures.Prepare_Colour(main.Capture)) { Post->ModelColourRefusedKey = main.ViewKey; return false; }
    return true;
}
void PosixDevice9::Set_Model_Effect(unsigned effect, const float parameters[4])
{
    if (SunActive) return; // caster coverage has its separate, already validated metadata
    if (effect >= DRAW_EFFECT_CLOAK_SHADOW || (effect && !parameters)) effect = DRAW_EFFECT_NONE;
    for (unsigned i = 0; effect && i < 4; ++i) if (!std::isfinite(parameters[i])) effect = DRAW_EFFECT_NONE;
    ModelEffect = effect;
    if (effect) std::memcpy(ModelEffectParameters, parameters, sizeof(ModelEffectParameters));
    else std::memset(ModelEffectParameters, 0, sizeof(ModelEffectParameters));
    if (effect != DRAW_EFFECT_CLOAK || !Takes_Model_Effect(effect)) return;
    SdlMainViewSnapshot main;
    if (!Effect_Main_View(main)) return;
    main.Capture.Phase = SdlSceneCapturePhase::CloakScene; main.Capture.Colour = true;
    const bool injected = (ModelRefusalForTest & 2) != 0; ModelRefusalForTest &= ~2u;
    if (injected || !Post->Captures.Capture_Colour(main.Capture, Post->ModelColour)) {
        Post->ModelColour = {}; Post->ModelColourRefusedKey = main.ViewKey;
    }
}
unsigned PosixDevice9::Requested_Model_Effect(float parameters[4]) const
{
    const unsigned effect = ModelEffectsAllowed && !SunActive ? ModelEffect : unsigned(DRAW_EFFECT_NONE);
    if (effect) std::memcpy(parameters, ModelEffectParameters, sizeof(ModelEffectParameters));
    else std::memset(parameters, 0, sizeof(ModelEffectParameters));
    return effect;
}
SDL_GPUTexture *PosixDevice9::Model_Colour()
{
    SdlMainViewSnapshot main;
    if (!Post || !Post->ModelColour.Texture || !Effect_Main_View(main)) return nullptr;
    main.Capture.Phase = SdlSceneCapturePhase::CloakScene; main.Capture.Colour = true;
    return main.Capture.Same_Identity(Post->ModelColour.Identity) ? Post->ModelColour.Texture : nullptr;
}
unsigned PosixDevice9::Draw_Model_Effect()
{
    SdlMainViewSnapshot main;
    if (!ModelEffectsAllowed || !ModelEffect || SunActive || (FVF & D3DFVF_XYZRHW) || !Effect_Main_View(main)) return DRAW_EFFECT_NONE;
    if (RenderStates[D3DRS_ALPHABLENDENABLE]) {
        const auto source = RenderStates[D3DRS_SRCBLEND], destination = RenderStates[D3DRS_DESTBLEND];
        if (source == D3DBLEND_ZERO || source == D3DBLEND_DESTCOLOR || destination == D3DBLEND_SRCCOLOR) return DRAW_EFFECT_NONE;
        if (source == D3DBLEND_ONE || destination == D3DBLEND_ONE)
            return ModelEffect == DRAW_EFFECT_OUTAGE ? DRAW_EFFECT_OUTAGE : DRAW_EFFECT_NONE;
    }
    return ModelEffect == DRAW_EFFECT_CLOAK && !Model_Colour() ? DRAW_EFFECT_NONE : ModelEffect;
}
bool PosixDevice9::Build_Main_View_Constants(SdlEnginePixelConstants &constants, bool receiving)
{
    SdlMainViewSnapshot main;
    if (!Read_Main_View(main, false, SdlSceneCapturePhase::OpaqueScene)) return false;
    std::memcpy(constants.WorldFromClip, main.FullTextureWorldFromClip, sizeof(constants.WorldFromClip));
    if (receiving) {
        // Preserve the existing shadow's bounded-viewport/half-pixel convention
        // while cosmetic reconstruction uses full-texture pixel coordinates.
        float toShadowClip[16] = {}; toShadowClip[0] = float(main.Target.Width) / main.Viewport[2];
        toShadowClip[5] = float(main.Target.Height) / main.Viewport[3]; toShadowClip[10] = toShadowClip[15] = 1;
        toShadowClip[12] = (main.Target.Width - 2 * main.Viewport[0] - main.Viewport[2] - 1) / main.Viewport[2];
        toShadowClip[13] = (2 * main.Viewport[1] + main.Viewport[3] - main.Target.Height + 1) / main.Viewport[3];
        float folded[16]; multiply(toShadowClip, constants.ShadowFromClip, folded);
        std::memcpy(constants.ShadowFromClip, folded, sizeof(folded));
    }
    constants.ShadowViewport[0] = 1.0f/main.Target.Width; constants.ShadowViewport[1] = 1.0f/main.Target.Height;
    constants.ShadowViewport[2] = constants.ShadowViewport[3] = 0;
    return true;
}
namespace RenderEffects {
bool WorldEffectsAvailable(IDirect3DDevice9 *device) { return provider_frame(device) != nullptr; }
void AllowDrawEffects(IDirect3DDevice9 *device, bool allowed) { if(device) static_cast<PosixDevice9 *>(device)->Allow_Model_Effects(allowed); }
bool DrawEffectsAvailable(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Model_Effects_Available(); }
bool TakesDrawEffect(IDirect3DDevice9 *device, unsigned effect) { return device && static_cast<PosixDevice9 *>(device)->Takes_Model_Effect(effect); }
void SetDrawEffect(IDirect3DDevice9 *device, unsigned effect, const float parameters[4]) { if(device) static_cast<PosixDevice9 *>(device)->Set_Model_Effect(effect, parameters); }
unsigned RequestedDrawEffect(IDirect3DDevice9 *device, float parameters[4]) {
    if(device) return static_cast<PosixDevice9 *>(device)->Requested_Model_Effect(parameters);
    std::memset(parameters, 0, sizeof(float)*4); return DRAW_EFFECT_NONE;
}
}

bool PosixDevice9::Prepare_Classic_Water_Foam()
{
    SdlMainViewSnapshot main;
    if (!Post || Post->Classic || !Effect_Main_View(main)) return false;
    if (Post->FoamRefusedKey == main.ViewKey) return false;
    if (FoamRefusalForTest & 1) { FoamRefusalForTest &= ~1u; Post->FoamRefusedKey = main.ViewKey; return false; }
    if (!Read_Main_View(Post->FoamView, true, SdlSceneCapturePhase::OpaqueScene)) {
        Post->FoamRefusedKey = main.ViewKey; return false;
    }
    return true;
}
SDL_GPUTexture *PosixDevice9::Classic_Water_Depth()
{
    SdlMainViewSnapshot main;
    if (!Post || Post->Classic || !Post->FoamView.SampledDepth ||
        !Effect_Main_View(main)) return nullptr;
    return main.Capture.Same_Identity(Post->FoamView.Capture) ? Post->FoamView.SampledDepth : nullptr;
}
namespace RenderTeamGlow {
void SetDraw(IDirect3DDevice9 *device, bool marked) { if (device) static_cast<PosixDevice9 *>(device)->Set_Team_Glow(marked); }
bool Available(IDirect3DDevice9 *device) { return supports_hdr(device); }
}
namespace RenderWaterFoam {
bool Available(IDirect3DDevice9 *device) { return supports_depth_capture(device); }
bool Ready(IDirect3DDevice9 *device) { return device && static_cast<PosixDevice9 *>(device)->Prepare_Classic_Water_Foam(); }
}
