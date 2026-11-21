// Exact native sky, ground haze and horizon fade. Caller owns map/weather/view policy and order.
#pragma once
#include "SdlPostProcess.h"
#include <cstdint>
#include <string>

struct SdlPipelineKey;

struct SdlPostBorrowedView
{
    SDL_GPUTexture *Texture = nullptr;
    uint64_t Identity = 0, Generation = 0; // both nonzero for a texture; both zero for an absent view
};
struct SdlPostAtmosphereViews
{
    uint64_t MapIdentity = 0, MapGeneration = 0; // explicit producer epoch; never inferred from pointers
    SdlPostBorrowedView Sky, Shroud;
    float ShroudRect[4] = {0,0,1,1};
};
struct SdlPostGroundHazeSettings
{
    float Colour[3] = {};
    float Top = 0, Thickness = 1, Strength = 0, Reach = 1, WaterHeight = -1.0e6f;
    float TowardSun[3] = {0,0,1};
    float Warmth = 0;
    bool BeforeWater = false;
};
struct SdlPostAtmosphereSettings
{
    float Horizon[3] = {}, Tint[3] = {1,1,1};
    float Start = 0, Scale = 1, Strength = 0, Ground = 0, MapSize[2] = {};
};
struct SdlPostAtmosphereResources
{
    unsigned Textures = 0, Shaders = 0, Pipelines = 0, Samplers = 0;
    unsigned long long Creations = 0;
};

class SdlPostAtmosphere
{
public:
    SdlPostAtmosphere();
    ~SdlPostAtmosphere();
    SdlPostAtmosphere(const SdlPostAtmosphere &) = delete;
    SdlPostAtmosphere &operator=(const SdlPostAtmosphere &) = delete;
    // Prepare every enabled program/pipeline/fallback texture before the first visible operation.
    // Copies settings, views and the row-major inverse camera. Main identity must name the active,
    // unresolved post scene at OpaqueScene. All strengths zero disables/resets without GPU work.
    // Weather haze replaces warm-light haze: the caller selects ONE ground-haze settings packet.
    bool Prepare(SdlGpuFrame &frame, const SdlPostProcess &post, const SdlTarget &scene,
        const SdlSceneCaptureRequest &mainIdentity, const float worldFromClip[16],
        const SdlPostGroundHazeSettings &haze, const SdlPostAtmosphereSettings &atmosphere,
        const SdlPostAtmosphereViews &views, const SdlPostGroundHazeSettings &transitionWarm = SdlPostGroundHazeSettings());
    // Sky after shadow preparation, before world geometry. No depth input or attachment is used.
    bool Draw_Sky(SdlGpuFrame &frame, const SdlTarget &scene);
    // Reuse an accepted water draw's displaced vertices. This optional R32 surface
    // keeps warm haze on visible water, rather than on the submerged scene behind it.
    bool Record_Water_Surface(SdlGpuFrame &frame, const SdlTarget &scene,
        const SdlRecordedDraw &draw, const SdlPipelineKey &key);
    // Weather ground haze at OpaqueScene before water; ordinary haze and horizon fade at BeforeTransparent.
    // Neither method captures depth. Captured matrix/shroud must match the copied scene packet.
    bool Apply_Ground_Haze(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    bool Apply_Transition_Warm_Haze(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    bool Apply_Atmosphere(SdlGpuFrame &frame, const SdlTarget &scene, const SdlPostSceneInputs &inputs);
    enum class TestFailure { None, Program, Pipeline, Target, TargetAfterOne, WaterTarget };
    void Set_Failure_For_Test(TestFailure failure);
    void Abandon();
    bool Release(SdlGpuFrame &frame);
    const std::string &Error() const;
    SdlPostAtmosphereResources Resources() const;
    // The frame AND post owner must outlive this module. Borrowed views/depth stay alive and
    // unchanged through submission; retire replacements via frame.Release_After_Batch.
private:
    struct Impl;
    Impl *State;
};
