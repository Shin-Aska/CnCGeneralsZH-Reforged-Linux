#pragma once

#include <cstdint>
#include <string>

struct PixelPipelineDescription;

namespace WaterGpuShaders {

enum class Target : std::uint8_t { NativeD3D11, Sdl3Gpu };
enum class Surface : std::uint8_t { Flat, River, Shore };
enum class Vertex : std::uint8_t { Analytic, Fft };
enum class DebugMode : std::uint8_t {
    None,
    Depth,
    Bed,
    Reflection,
    Normal,
    Highlight,
    Offshore,
    WaveCage,
    CageWash,
    ShoreHistory,
    ShoreHistoryTerms,
    Shore,
    ShoreRaw,
    ShoreSourceTerms,
    Foam,
    Vessel,
    Pressure,
    ResolvedNormal,
    Roughness,
    PressureRetention,
    Footprint,
    FootprintPath,
    FootprintColour,
    FootprintReasons,
    FootprintOverlay,
    HullDepth,
    HullDepthSuppression,
    HullDepthHeight,
    ContactMesh,
    ContactMeshAlpha,
    ContactMaterial,
    ContactLighting,
    ContactMeshQuery,
    ContactMeshSources,
    BowRetention,
    CoastalSurf,
    CoastalDensity,
    CoastalMaterial
};

struct Variant
{
    Target target = Target::NativeD3D11;
    Surface surface = Surface::Flat;
    Vertex vertex = Vertex::Analytic;
    bool meshContact = false;
    bool coastalSurf = false;
    bool coastalCrest = false;
    bool shoreFoam = true;
    bool bowLift = false;
    bool bowShoulder = true;
    DebugMode debug = DebugMode::None;
    std::uint32_t materialFvf = 0;
};

struct EnvironmentSnapshot
{
    Variant flat;
    Variant river;
};

bool DebugNeedsMeshContact(DebugMode mode);
bool DebugNeedsCoastalSurf(DebugMode mode);
EnvironmentSnapshot SnapshotEnvironment(Target target, std::uint32_t materialFvf);
bool Valid(const Variant &variant);
std::string Key(const Variant &variant);
bool GenerateVertex(const Variant &variant, const std::string &base, std::string &hlsl);
bool GenerateSurfacePixel(const Variant &variant, const PixelPipelineDescription &pipeline,
    std::string &hlsl);
bool GenerateShorePixel(const Variant &variant, const std::string &base, std::string &hlsl);

}
