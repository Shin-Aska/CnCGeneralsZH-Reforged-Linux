// Scene post dispatch. This interface carries policy without changing backend identity.
#pragma once
#include <cstdint>
#if !defined(_WIN32)
#include "Platform/SunShaftSettings.h"
#endif
struct IDirect3DBaseTexture9;
struct IDirect3DSurface9;
namespace RenderPost {
struct Settings {
    const char *Chain = "off"; // consumed/copied synchronously
    bool Classic = false, WorldAO = false, IndependentScene = false, InternalHDR = false;
    float BloomThreshold = 1, BloomIntensity = 0.75f, AdditiveGain = 1, Grade = 0;
    float BloomSaturation = 1, Clarity = 0;
    float ClearAlpha = 1; // main-scene clear alpha
    unsigned BlurPasses = 2;
};
struct GroundHazeSettings {
    float Colour[3] = {}, Top = 0, Thickness = 1, Strength = 0, Reach = 1, WaterHeight = -1.0e6f;
    float TowardSun[3] = {0,0,1}, Warmth = 0;
    bool BeforeWater = false; // weather over opaque ground; actual water composites over it
};
struct AtmosphereSettings {
    float Horizon[3] = {}, Tint[3] = {1,1,1}, Start = 0, Scale = 1, Strength = 0, Ground = 0, MapSize[4] = {}; // playable XY, heightmap border, terrain skirt reach (zero keeps legacy closure)
};
struct Warp {
    float CentreX = 0, CentreY = 0, Radius = 0, Pull = 0;
    float RingRadius = 0, RingWidth = 0, RingStrength = 0, RingSquash = 0;
};
// Copied synchronously for the current explicit main owner. These do not enable
// a scene when the user chain is off, including an independent consumer's COPY.
void SetWarps(const Warp *warps, unsigned count);
void SetHeatSplats(const float *records, unsigned count); // existing four-float4 layout
struct Environment {
    std::uint64_t MapIdentity = 0, MapGeneration = 0;
    GroundHazeSettings Haze;
    GroundHazeSettings TransitionWarmHaze; // only the complementary warm-light contribution during rain
    AtmosphereSettings Atmosphere;
#if !defined(_WIN32)
    SunShaftSettings Shafts = {};
#endif
    IDirect3DBaseTexture9 *Sky = nullptr, *Shroud = nullptr;
    float ShroudRect[4] = {0,0,1,1};
};
// Copies every value and retains texture references synchronously. Empty packets invalidate a map.
// Submit once at pushProtoHaze before BeginMain; receivers consume freshness for that display pass.
void SetEnvironment(const Environment &environment);
// Stable producer admission, including map-load/weather update before a scene. Independent of user
// options and current routing; user-visible feature capabilities are advertised after integration.
bool EnvironmentAvailable();
bool DistortionAvailable();
#if defined(__linux__)
bool SunShaftsAvailable();
bool SoftShroudAvailable();
void SetShroud(IDirect3DBaseTexture9 *, float seconds, bool written, float fogLight, bool fogged);
#endif
void BeginMain(const Settings &settings);
bool SetChain(const char *chain);
unsigned BeginView(bool eligible, float nearPlane, float farPlane, bool reflection = false);
void EndView(unsigned previous);
void Sky(); // after sun preparation, before main world geometry
void OpaqueScene(bool allowWorldAO); // World AO follows the caller's option; pre-water weather haze runs independently.
void BeforeTransparent();
void FinishScene();
void FinishFrame(); // no-view fallback, before HUD
bool ReplacesLegacyBloom();
bool SceneActive();
void SetSceneStandIn(IDirect3DBaseTexture9 *texture, IDirect3DSurface9 *surface);
void SetGlowDraw(bool enabled);
class ViewScope {
    unsigned Previous;
public:
    ViewScope(bool eligible, float nearPlane, float farPlane, bool reflection = false) : Previous(BeginView(eligible, nearPlane, farPlane, reflection)) {}
    ~ViewScope() { EndView(Previous); }
    ViewScope(const ViewScope &) = delete;
    ViewScope &operator=(const ViewScope &) = delete;
};
}
#if !defined(_WIN32)
struct IDirect3DDevice9;
namespace RenderPost {
void BeginMain(IDirect3DDevice9 *, const Settings &);
void SetEnvironment(IDirect3DDevice9 *, const Environment &);
void SetWarps(IDirect3DDevice9 *, const Warp *, unsigned);
void SetHeatSplats(IDirect3DDevice9 *, const float *, unsigned);
bool EnvironmentAvailable(IDirect3DDevice9 *);
bool DistortionAvailable(IDirect3DDevice9 *);
bool SunShaftsAvailable(IDirect3DDevice9 *);
bool SoftShroudAvailable(IDirect3DDevice9 *);
void SetShroud(IDirect3DDevice9 *, IDirect3DBaseTexture9 *, float, bool, float, bool);
unsigned BeginView(IDirect3DDevice9 *, bool, float, float, bool);
void EndView(IDirect3DDevice9 *, unsigned);
void Sky(IDirect3DDevice9 *);
void OpaqueScene(IDirect3DDevice9 *, bool allowWorldAO);
void BeforeTransparent(IDirect3DDevice9 *);
void FinishScene(IDirect3DDevice9 *, bool fallback);
bool ReplacesLegacyBloom(IDirect3DDevice9 *);
bool SceneActive(IDirect3DDevice9 *);
void SetSceneStandIn(IDirect3DDevice9 *, IDirect3DBaseTexture9 *, IDirect3DSurface9 *);
void SetGlowDraw(IDirect3DDevice9 *, bool);
}
#endif
