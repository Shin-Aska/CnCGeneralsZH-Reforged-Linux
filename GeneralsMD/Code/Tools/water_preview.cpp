// Standalone water-material editor using the production SDL device, FFT, shader and INI rules.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/EngineShaderName.h"
#include "Platform/ModernWaterConfig.h"
#include "Platform/ModernWaterMaterial.h"
#include "Platform/WaterScreenReflection.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterPerformanceSettings.h"
#include "Platform/WaterMaterialProfile.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "WaterPreviewColorPanel.h"
#include "WaterPreviewReflectionBar.h"
#include "WaterPreviewScene.h"
#include "WaterPreviewCamera.h"

namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr float WaterLevel = 40.0f;
constexpr unsigned Grid = 220;
constexpr float Extent = 1100.0f;
constexpr float GridCell = 2*Extent/Grid;
constexpr float BedExtent = 2600.0f; // the fixed bed covers the patch at either pan limit
constexpr RenderUInt32 WaterFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX4;

struct Options {
    unsigned width = 1280, height = 800, frames = 0;
    bool sceneReflections = true, benchmark = false, freezeTime = false;
    WaterMaterialProfile::Mode materialProfile = WaterMaterialProfile::Full;
    int waveDetail = 1, refraction = WaterPerformanceSettings::Full, reflectionDetail = 1;
    unsigned benchmarkWarmup = 120;
    float time = 18.0f, strength = 0.80f, brightness = 0.67f;
    float mapDepth = 2.0f, mapOpacity = 0.8f;
    WaterPreviewCamera::State camera;
    int mode = 2;
    int reflection = WaterPreviewReflection::Hybrid;
    bool offscreen = false, bubbles = true, props = true;
    bool controls = true, strengthSet = false, brightnessSet = false, bubblesSet = false;
    bool loadPreset = false, widthSet = false, heightSet = false;
    std::string screenshot, colour, config, exportConfig, scene, debug, benchmarkCsv;
    std::string mapColour = "324B32", mapTexture = "TSWater03.tga";
    float underwaterClarity = -1.f; // absent: retain resolved or banked material
    float reflectionStrength = -1.f;
};

void help()
{
    std::puts(
        "Water Preview - standalone production-water material editor\n"
        "Usage: water_preview [--offscreen] [--frames N] [--screenshot FILE.png]\n"
        "       [--mode clean|material|split] [--no-bubbles] [--strength 0..1]\n"
        "       [--color RRGGBB] [--brightness 0.1..1] [--hide-controls]\n"
        "       [--config FILE.ini] [--map-color RRGGBB] [--map-texture NAME]\n"
        "       [--map-depth N] [--map-opacity 0..1] [--export-config FILE.ini]\n"
        "       [--underwater-clarity 0..1]\n"
        "       [--reflection-strength 0..4]\n"
        "       [--load-preset] [--width N] [--height N] [--time SECONDS]\n"
        "       [--reflection legacy|ssr|hybrid|path|compare] [--no-props] [--scene NAME|PATH]\n"
        "       [--yaw RADIANS] [--pitch 0.48..1.38] [--distance 180..1050]\n"
        "       [--target X Y] [--debug VIEW]\n"
        "       [--scene-reflections on|off] [--wave-detail low|high]\n"
        "       [--refraction full|simple|off] [--benchmark] [--benchmark-warmup N]\n"
        "       [--benchmark-csv FILE.csv] [--reflection-detail low|high]\n"
        "       [--freeze-time] [--profile-material full|constant-surface|no-foam-lighting|\n"
        "           no-sky-reflection|no-terrain-shadow|no-seabed-detail|no-caustics]\n"
        "\n1 clean baseline | 2 resolved material | 3 split (baseline left / material right)\n"
        "4 legacy | 5 SSR | 6 hybrid (legacy + SSR) | 8 path traced (experimental)\n"
        "7 legacy/SSR/hybrid side by side; path tracing is separate and optional.\n"
        "P synthetic props: bank, towers, bridge, sandstone ruins and striped pole\n"
        "B bubbles | Space pause | [ / ] pollution | S save PNG | E export rule | Escape close\n"
        "Synthetic camera: arrows pan; right drag pans away from its press anchor.\n"
        "Middle drag turns (0.01 rad/pixel); Alt+middle drag tilts (down tips down).\n"
        "Middle click resets heading/pitch/zoom; Home or R also resets the ground target.\n"
        "Wheel / +/- zoom; Ctrl+wheel zooms 5x faster; Alt+wheel tilts (forward flatter).\n"
        "--target X Y starts the ground target at water level (each coordinate -700..700).\n"
        "Resets use the starting --yaw/--pitch/--distance and, for Home/R, --target.\n"
        "H color panel: RGB, brightness, pollution; Save color stores an editor preset.\n"
        "L loads saved editor colors; --load-preset restores them at startup.\n"
        "Synthetic starts resolve the INI rules; scene starts retain the banked material.\n"
        "E writes --export-config or water-preview-rule.ini in the working directory.\n"
        "The texture name is a matching input; authored texture pixels are not loaded.\n"
        "\n--scene: FIXED CAMERA (banked frame); camera input and start options have no effect.\n"
        "Banked mode 4 is CAPTURED PLANAR: a bank cannot regenerate the original legacy scene.\n"
        "Path tracing requires synthetic scene geometry; old scene banks contain only images.\n"
        "--reflection mirror remains an alias for legacy.\n"
        "Scene names resolve beside the executable: water-scenes/NAME.wbank.\n"
        "--width/--height scale and letterbox that view; compare uses three bank-size targets.\n"
        "Synthetic High planar targets: legacy 512x512, hybrid 1024x1024; banks retain captured resolution.\n"
        "Synthetic mode requires no game data. No game/save files are changed.\n"
        "--offscreen defaults to one frame; captures use deterministic 1/60 s steps.\n"
        "--benchmark defaults to 720 bounded frames, excludes the first 120 frames,\n"
        "and reports host draw+Present timings without added GPU synchronization.\n"
        "Use --frames/--benchmark-warmup to override; screenshots are separate runs.\n"
        "Scene reflections Off skips planar/path producers; banks retain injected inputs.\n"
        "Reflection detail Low halves planar sides; Path uses 256 still/128 moving versus High 512 always. SSR is unchanged.\n"
        "Banks retain their captured planar resolution even with Reflection detail Low.");
}

std::string hexColour(const char *text)
{
    const std::string value=text;
    if (value.size()!=6 || value.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
        throw std::runtime_error("color must be six hexadecimal digits RRGGBB");
    return value;
}

ModernWaterConfig::Color mapColour(const std::string &value)
{
    const unsigned rgb=unsigned(std::strtoul(value.c_str(),nullptr,16));
    return {int((rgb>>16)&255),int((rgb>>8)&255),int(rgb&255)};
}

unsigned integer(const char *text, unsigned low, unsigned high)
{
    char *end = nullptr;
    const unsigned long n = std::strtoul(text, &end, 10);
    if (!*text || *end || n < low || n > high)
        throw std::runtime_error(std::string("invalid integer: ") + text);
    return static_cast<unsigned>(n);
}

float number(const char *text, float low, float high)
{
    char *end = nullptr;
    const float n = std::strtof(text, &end);
    if (!*text || *end || !std::isfinite(n) || n < low || n > high)
        throw std::runtime_error(std::string("invalid number: ") + text);
    return n;
}

Options parse(int argc, char **argv)
{
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() {
            if (i + 1 == argc) throw std::runtime_error("missing value after " + arg);
            return argv[++i];
        };
        if (arg == "--offscreen") o.offscreen = true;
        else if (arg == "--benchmark") o.benchmark = true;
        else if (arg == "--freeze-time") o.freezeTime = true;
        else if (arg == "--profile-material") {
            if (!WaterMaterialProfile::Parse(value(),o.materialProfile))
                throw std::runtime_error("unknown profile-material diagnostic");
        }
        else if (arg == "--benchmark-csv") o.benchmarkCsv = value();
        else if (arg == "--benchmark-warmup") o.benchmarkWarmup = integer(value(), 0, 999999);
        else if (arg == "--scene-reflections") {
            const std::string v = value();
            if (v != "on" && v != "off") throw std::runtime_error("scene-reflections must be on or off");
            o.sceneReflections = v == "on";
        }
        else if (arg == "--wave-detail") {
            const std::string v = value();
            if (v != "low" && v != "high") throw std::runtime_error("wave-detail must be low or high");
            o.waveDetail = v == "high" ? 1 : 0;
        }
        else if (arg == "--reflection-detail") {
            const std::string v = value();
            if (v != "low" && v != "high") throw std::runtime_error("reflection-detail must be low or high");
            o.reflectionDetail = v == "high" ? 1 : 0;
        }
        else if (arg == "--refraction") {
            const std::string v = value();
            if (v != "full" && v != "simple" && v != "off") throw std::runtime_error("refraction must be full, simple or off");
            o.refraction = v == "off" ? WaterPerformanceSettings::Off : v == "simple" ? WaterPerformanceSettings::Simple : WaterPerformanceSettings::Full;
        }
        else if (arg == "--no-bubbles") { o.bubbles = false; o.bubblesSet = true; }
        else if (arg == "--no-props") o.props = false;
        else if (arg == "--yaw") o.camera.yaw = number(value(), -100, 100);
        else if (arg == "--pitch") o.camera.pitch = number(value(), 0.48f, 1.38f);
        else if (arg == "--distance") o.camera.distance = number(value(), 180, 1050);
        else if (arg == "--target") {
            o.camera.x = number(value(), -WaterPreviewCamera::TargetLimit, WaterPreviewCamera::TargetLimit);
            o.camera.y = number(value(), -WaterPreviewCamera::TargetLimit, WaterPreviewCamera::TargetLimit);
        }
        else if (arg == "--reflection") {
            const std::string reflection = value();
            if (reflection == "legacy" || reflection == "mirror") o.reflection = WaterPreviewReflection::Mirror;
            else if (reflection == "ssr") o.reflection = WaterPreviewReflection::ScreenSpace;
            else if (reflection == "hybrid") o.reflection = WaterPreviewReflection::Hybrid;
            else if (reflection == "path" || reflection == "path-traced") o.reflection = WaterPreviewReflection::PathTraced;
            else if (reflection == "compare") o.reflection = WaterPreviewReflection::Compare;
            else throw std::runtime_error("reflection must be legacy, ssr, hybrid, path or compare");
        }
        else if (arg == "--width") { o.width = integer(value(), 320, 8192); o.widthSet = true; }
        else if (arg == "--height") { o.height = integer(value(), 240, 8192); o.heightSet = true; }
        else if (arg == "--frames") o.frames = integer(value(), 1, 1000000);
        else if (arg == "--time") o.time = number(value(), 0, 1000000);
        else if (arg == "--strength") { o.strength = number(value(), 0, 1); o.strengthSet = true; }
        else if (arg == "--brightness") { o.brightness = number(value(), 0.1f, 1); o.brightnessSet = true; }
        else if (arg == "--hide-controls") o.controls = false;
        else if (arg == "--load-preset") o.loadPreset = true;
        else if (arg == "--color") o.colour = hexColour(value());
        else if (arg == "--map-color") o.mapColour = hexColour(value());
        else if (arg == "--map-texture") {
            o.mapTexture=value();
            if (o.mapTexture.empty() || o.mapTexture.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.\\/")!=std::string::npos)
                throw std::runtime_error("map texture must be a filename without spaces or INI delimiters");
        }
        else if (arg == "--map-depth") o.mapDepth = number(value(), 0.001f, 1000000);
        else if (arg == "--map-opacity") o.mapOpacity = number(value(), 0, 1);
        else if (arg == "--underwater-clarity") o.underwaterClarity = number(value(), 0, 1);
        else if (arg == "--reflection-strength") o.reflectionStrength = number(value(), 0, 4);
        else if (arg == "--scene") o.scene = value();
        else if (arg == "--debug") o.debug = value();
        else if (arg == "--config") o.config = value();
        else if (arg == "--export-config") o.exportConfig = value();
        else if (arg == "--screenshot") o.screenshot = value();
        else if (arg == "--mode") {
            const std::string mode = value();
            if (mode == "clean") o.mode = 1;
            else if (mode == "polluted" || mode == "material") o.mode = 2;
            else if (mode == "split") o.mode = 3;
            else throw std::runtime_error("mode must be clean, material (or polluted), or split");
        } else throw std::runtime_error("unknown argument: " + arg);
    }
    if (!o.scene.empty() && o.reflection == WaterPreviewReflection::PathTraced)
        throw std::runtime_error("path tracing is unavailable for scene banks: they contain images, not complete scene geometry; omit --scene to use the synthetic scene");
    if (o.materialProfile != WaterMaterialProfile::Full &&
        (!o.debug.empty() || (SDL_getenv("ZH_WATER_DEBUG") && *SDL_getenv("ZH_WATER_DEBUG"))))
        throw std::runtime_error("profile-material ablations require debug views to be disabled");
    if (o.freezeTime && o.benchmark && o.benchmarkWarmup == 0)
        throw std::runtime_error("frozen benchmark needs at least one warmup frame to initialize FFT");
    if (!o.benchmarkCsv.empty() && !o.benchmark) throw std::runtime_error("benchmark-csv requires --benchmark");
    if (o.benchmark) {
        if (!o.frames) o.frames = 720;
        if (o.benchmarkWarmup >= o.frames) throw std::runtime_error("benchmark warmup must be less than frames");
        if (!o.screenshot.empty()) throw std::runtime_error("benchmark screenshots must be captured in a separate run");
        if (SDL_getenv("ZH_GPU_TIMING_SYNC") && std::strcmp(SDL_getenv("ZH_GPU_TIMING_SYNC"),"0") != 0)
            throw std::runtime_error("benchmark requires ZH_GPU_TIMING_SYNC to be unset or 0");
    }
    if (o.offscreen && !o.frames) o.frames = 1;
    return o;
}

struct Vec3 { float x, y, z; };
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 normalized(Vec3 v) { const float s = 1.0f / std::sqrt(dot(v,v)); return {v.x*s,v.y*s,v.z*s}; }

D3DMATRIX identity()
{
    D3DMATRIX m = {};
    m._11 = m._22 = m._33 = m._44 = 1;
    return m;
}

// Row-vector, right-handed view and zero-to-one depth, as the game's D3D9 device expects.
D3DMATRIX viewMatrix(Vec3 eye, Vec3 target)
{
    const Vec3 z = normalized(subtract(eye,target));
    const Vec3 x = normalized(cross({0,0,1},z));
    const Vec3 y = cross(z,x);
    D3DMATRIX m = identity();
    m._11=x.x; m._12=y.x; m._13=z.x;
    m._21=x.y; m._22=y.y; m._23=z.y;
    m._31=x.z; m._32=y.z; m._33=z.z;
    m._41=-dot(x,eye); m._42=-dot(y,eye); m._43=-dot(z,eye);
    return m;
}

D3DMATRIX multiply(const D3DMATRIX &a, const D3DMATRIX &b)
{
    D3DMATRIX r = {};
    for (unsigned i=0; i<4; ++i) for (unsigned j=0; j<4; ++j)
        for (unsigned k=0; k<4; ++k) r.m[i][j] += a.m[i][k]*b.m[k][j];
    return r;
}

D3DMATRIX projection(float aspect)
{
    const float near = 2.0f, far = 6000.0f, scale = 1.0f / std::tan(48.0f*Pi/360.0f);
    D3DMATRIX m = {};
    m._11=scale/aspect; m._22=scale; m._33=far/(near-far);
    m._34=-1; m._43=near*far/(near-far);
    return m;
}

void require(RenderResult result, const char *operation)
{
    if (result != D3D_OK) throw std::runtime_error(std::string(operation) + " failed (" + std::to_string(result) + ")");
}

struct WaterVertex { float x,y,z,nx,ny,nz; uint32_t colour; float uv[8]; };
struct BedVertex { float x,y,z; uint32_t colour; float u,v; };
struct PropVertex { float x,y,z; uint32_t colour; };

// Flat-shaded scenery in and above the water, lit by the same sun the water shader uses.
class PropBuilder {
public:
    std::vector<PropVertex> vertices;
    // Everything below `floor` is cut away: the mirror pass keeps only what stands above the water.
    explicit PropBuilder(float floor, bool shadeFaces = true) : floor(floor), shadeFaces(shadeFaces) {}
    void box(float x0, float y0, float z0, float x1, float y1, float z1, uint32_t colour)
    {
        z0 = std::max(z0, floor);
        if (z1 <= z0) return;
        quad({x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1},colour);
        quad({x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1},colour);
        quad({x1,y1,z0},{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},colour);
        quad({x0,y1,z0},{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},colour);
        quad({x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1},colour);
    }
    // Tilt about the box's local y axis, then turn it on the ground. Faces go through the
    // same water-plane clip as the upright scenery, including the slab's visible underside.
    void tiltedBox(Vec3 centre, Vec3 size, float yaw, float tilt, uint32_t colour)
    {
        const auto corner=[&](float x, float y, float z) {
            x*=size.x*.5f; y*=size.y*.5f; z*=size.z*.5f;
            const float tx=x*std::cos(tilt)+z*std::sin(tilt),tz=z*std::cos(tilt)-x*std::sin(tilt);
            return Vec3{centre.x+tx*std::cos(yaw)-y*std::sin(yaw),
                centre.y+tx*std::sin(yaw)+y*std::cos(yaw),centre.z+tz};
        };
        const Vec3 a=corner(-1,-1,-1),b=corner(1,-1,-1),c=corner(1,1,-1),d=corner(-1,1,-1);
        const Vec3 e=corner(-1,-1,1),f=corner(1,-1,1),g=corner(1,1,1),h=corner(-1,1,1);
        quad(e,f,g,h,colour); quad(d,c,b,a,colour);
        quad(a,b,f,e,colour); quad(c,d,h,g,colour);
        quad(d,a,e,h,colour); quad(b,c,g,f,colour);
    }
    // A bank rising along +y from (yLow, zLow) to (yHigh, zHigh), then a tiled plateau beyond.
    void bank(float x0, float x1, float yLow, float zLow, float yHigh, float zHigh, float yFar)
    {
        if (zHigh <= floor) return;
        const float yCut = zLow >= floor ? yLow : yLow + (yHigh - yLow) * (floor - zLow) / (zHigh - zLow);
        const float zCut = std::max(zLow, floor);
        quad({x0,yCut,zCut},{x1,yCut,zCut},{x1,yHigh,zHigh},{x0,yHigh,zHigh},0xff9c8a62);
        const float tile = 60;
        for (float y = yHigh; y < yFar; y += tile)
            for (float x = x0; x < x1; x += tile) {
                const bool odd = (int(std::floor((x - x0) / tile)) + int(std::floor((y - yHigh) / tile))) & 1;
                quad({x,y,zHigh},{x+tile,y,zHigh},{x+tile,y+tile,zHigh},{x,y+tile,zHigh},odd ? 0xff5f7a3c : 0xff6f8a46);
            }
    }
private:
    float floor;
    bool shadeFaces;
    static uint32_t shade(uint32_t colour, Vec3 normal)
    {
        const Vec3 sun = normalized({-.30f,-.45f,.84f});
        const float light = .38f + .62f * std::max(0.f, dot(normal, sun));
        const auto channel = [&](unsigned shift) {
            return uint32_t(std::min(255.f, float((colour >> shift) & 255) * light)) << shift; };
        return 0xff000000u | channel(16) | channel(8) | channel(0);
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t colour)
    {
        const uint32_t lit = shadeFaces ? shade(colour, normalized(cross(subtract(b,a),subtract(d,a)))) : colour;
        // A tilted face may straddle the plane: clamping its corners would change its slope.
        // Clip its edges instead, then triangulate the remaining convex polygon in its plane.
        Vec3 clipped[8];
        unsigned count=0;
        Vec3 previous=d;
        for (const Vec3 &p : {a,b,c,d}) {
            if ((previous.z>=floor)!=(p.z>=floor)) {
                const float t=(floor-previous.z)/(p.z-previous.z);
                clipped[count++]={previous.x+(p.x-previous.x)*t,previous.y+(p.y-previous.y)*t,floor};
            }
            if (p.z>=floor) clipped[count++]=p;
            previous=p;
        }
        for (unsigned i=1; i+1<count; ++i)
            for (const Vec3 &p : {clipped[0],clipped[i],clipped[i+1]}) vertices.push_back({p.x,p.y,p.z,lit});
    }
};

// Towers on the far bank (tall enough to leave the top of the screen), a bridge on two piers
// across the middle, sandstone ruins in the shallows, and a striped pole close to the camera.
std::vector<PropVertex> buildProps(float floor, bool shadeFaces = true)
{
    PropBuilder b(floor, shadeFaces);
    b.bank(-900, 900, 130, 10, 260, 95, 960);
    b.box(-330, 300, 95, -270, 360, 290, 0xffc0392b);
    b.box(-120, 320, 95, -70, 370, 210, 0xffe8e4dc);
    b.box(60, 300, 95, 130, 350, 250, 0xffd9a521);
    b.box(250, 330, 95, 300, 380, 180, 0xff2a8c82);
    b.box(-330, 60, 78, 330, 95, 88, 0xffa09a8c);
    b.box(-162, 66, 0, -138, 90, 78, 0xff8c8478);
    b.box(108, 66, 0, 132, 90, 78, 0xff8c8478);
    // A submerged masonry footing makes the ruins' water shallow. The 56-58 unit columns
    // start on it below the surface; refraction can see their bases and the broken column.
    const uint32_t sandstone=0xffc8b184,lintel=0xffd6c397;
    b.box(-120, -90, 0, 110, 22, 22, 0xff9c8a68);
    b.box(-82, -4, 22, -72, 6, 80, sandstone);
    b.box(-26, -4, 22, -16, 6, 80, sandstone);
    b.box(-88, -8, 80, -10, 10, 89, lintel);
    // Three columns support the two arms of an L, with a small overhang at either end.
    b.box(26, -4, 22, 36, 6, 78, sandstone);
    b.box(82, -4, 22, 92, 6, 78, sandstone);
    b.box(82, -60, 22, 92, -50, 78, sandstone);
    b.box(20, -8, 78, 78, 10, 87, lintel);
    b.box(78, -66, 78, 98, 10, 87, lintel);
    b.box(-111, -30, 22, -101, -20, 51, sandstone);
    b.tiltedBox({-60,-56,44},{66,15,10},-.35f,.55f,lintel);
    for (int band = 0; band < 6; ++band)
        b.box(30, -130, band ? 20.f + band * 12.f : 0.f, 42, -118, 32.f + band * 12.f, band & 1 ? 0xffe8e4dc : 0xffc0392b);
    return std::move(b.vertices);
}

class Preview {
public:
    explicit Preview(Options options) : options(options), time(options.time), cameraControl(options.camera) {}
    ~Preview()
    {
        scene.release();
        for (auto *target : {&legacyMirror, &hybridMirror}) {
            if (target->depth) target->depth->Release();
            if (target->surface) target->surface->Release();
            if (target->texture) target->texture->Release();
        }
        if (vertices) vertices->Release();
        if (indices) indices->Release();
        if (shader) shader->Release();
        for (auto *texture : textures) texture->Release();
        if (device && pathActive) WaterPathTracing::Clear(device);
        if (device) device->Release();
        if (adapter) adapter->Release();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }

    int run()
    {
        WaterMaterialProfile::Applied()=options.materialProfile;
        applyPerformanceSettings();
        initialize();
        std::printf("Material cost diagnostic: profile=%s freeze_time=%s; ablation images are not quality equivalents.\n",
            WaterMaterialProfile::Name(options.materialProfile),options.freezeTime?"yes":"no");
        updateTitle();
        std::printf("Controls: left-drag color sliders; H panel; L load saved colors; 1 clean baseline, 2 material, 3 split; 4 legacy (captured planar in banks), 5 SSR, 6 hybrid (legacy + SSR), 7 compare legacy/SSR/hybrid, 8 path traced (synthetic only); B bubbles; [/] pollution; Space pause; S PNG; E export rule; Esc close. %s\n",
            scene.loaded()?"FIXED CAMERA (banked frame).":"P props; arrows/right drag pan; middle drag turns; Alt+middle drag tilts (down tips down); middle click resets heading/pitch/zoom; wheel/+/- zoom; Ctrl+wheel 5x zoom; Alt+wheel tilts (forward flatter); Home/R resets camera and target.");
        uint64_t previous = SDL_GetTicksNS();
        unsigned frame = 0;
        bool done = false;
        std::vector<double> hostFrames;
        WorkCounts measuredStart{};
        if (options.benchmark) hostFrames.reserve(options.frames-options.benchmarkWarmup);
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (!options.benchmark) handle(event, done);
                else if (event.type == SDL_EVENT_QUIT) done = true;
            }
            if (done) break;
            const uint64_t now = SDL_GetTicksNS();
            const float elapsed = options.frames ? 1.0f/60.0f : std::min(float(now-previous)*1.0e-9f,0.05f);
            previous = now;
            if (frame && !paused && !options.freezeTime) time += elapsed;
            if (!options.benchmark) keyboard(elapsed);
            if (options.benchmark && frame == options.benchmarkWarmup) measuredStart = workCounts();
            const uint64_t drawStart = SDL_GetTicksNS();
            draw();
            const bool last = options.frames && frame+1 >= options.frames;
            if (capture || (last && !options.screenshot.empty())) {
                save(options.screenshot.empty() ? "water-preview.png" : options.screenshot);
                capture = false;
            }
            require(device->Present(nullptr,nullptr,nullptr,nullptr),"present");
            if (options.benchmark && frame >= options.benchmarkWarmup)
                hostFrames.push_back(double(SDL_GetTicksNS()-drawStart)*1.0e-6);
            ++frame;
            if (last) done = true;
        }
        if (options.benchmark) reportBenchmark(hostFrames,measuredStart,workCounts());
        printWork("Total work",workCounts());
        std::printf("Wave allocation: requested=%u active=%u\n",WaterPerformanceSettings::Applied().requestedFftResolution,
            WaterPerformanceSettings::Applied().activeFftResolution);
        if (pathActive) std::printf("Water path tracing final: %s\n",WaterPathTracing::Report(device).c_str());
        if (frame) std::printf("Rendered %u frames; %u recorded draws; %llu production FFT updates; no refused draws.\n",
            frame,device->Draws_Recorded(),static_cast<unsigned long long>(device->Get_Gpu()->Ocean_Updates()));
        return 0;
    }

private:
    struct WorkCounts {
        uint64_t draws=0, fftUpdates=0, fftPasses=0, fftCopies=0, colourCaptures=0, depthCaptures=0;
        uint64_t planarRenders=0, pathPrepares=0, bankInjections=0;
    };
    uint64_t planarRenders=0, pathPrepares=0, bankInjections=0;
    WorkCounts workCounts() const
    {
        const auto *gpu=device->Get_Gpu();
        return {device->Draws_Recorded(),gpu->Ocean_Updates(),gpu->Ocean_Passes(),gpu->Ocean_Copies(),
            gpu->Water_Colour_Captures(),gpu->Water_Depth_Captures(),planarRenders,pathPrepares,bankInjections};
    }
    static void printWork(const char *label, const WorkCounts &c)
    {
        std::printf("%s: draws=%llu fft_updates=%llu fft_passes=%llu fft_copies=%llu colour_captures=%llu depth_captures=%llu planar_renders=%llu path_prepares=%llu bank_injections=%llu\n",
            label,(unsigned long long)c.draws,(unsigned long long)c.fftUpdates,(unsigned long long)c.fftPasses,
            (unsigned long long)c.fftCopies,(unsigned long long)c.colourCaptures,(unsigned long long)c.depthCaptures,
            (unsigned long long)c.planarRenders,(unsigned long long)c.pathPrepares,(unsigned long long)c.bankInjections);
    }
    void reportBenchmark(std::vector<double> &samples, const WorkCounts &start, const WorkCounts &end)
    {
        if (samples.empty()) throw std::runtime_error("benchmark ended before measured frames");
        if (options.freezeTime && (end.fftUpdates != start.fftUpdates
            || end.fftPasses != start.fftPasses || end.fftCopies != start.fftCopies))
            throw std::runtime_error("frozen material benchmark unexpectedly updated FFT after warmup");
        if (!options.benchmarkCsv.empty()) {
            std::ofstream output(options.benchmarkCsv);
            output.precision(9);
            output << "frame,host_draw_present_ms\n";
            for (size_t i=0;i<samples.size();++i) output << options.benchmarkWarmup+i+1 << ',' << samples[i] << '\n';
            if (!output) throw std::runtime_error("write benchmark CSV: "+options.benchmarkCsv);
        }
        std::sort(samples.begin(),samples.end());
        const size_t n=samples.size();
        const double median=n%2?samples[n/2]:(samples[n/2-1]+samples[n/2])*.5;
        const double p95=samples[size_t(std::ceil(.95*n))-1];
        const char *layers=SDL_getenv("VK_INSTANCE_LAYERS");
        const bool mesa=layers && std::strstr(layers,"VK_LAYER_MESA_overlay");
        std::printf("Benchmark host draw+Present (%s; not GPU time): warmup=%u samples=%zu median_ms=%.6f p95_ms=%.6f\n",
            mesa?"Mesa overlay instrumented; external query waits":"unsynchronized; no added GPU wait",
            options.benchmarkWarmup,n,median,p95);
        printWork("Measured work",{end.draws-start.draws,end.fftUpdates-start.fftUpdates,end.fftPasses-start.fftPasses,
            end.fftCopies-start.fftCopies,end.colourCaptures-start.colourCaptures,end.depthCaptures-start.depthCaptures,
            end.planarRenders-start.planarRenders,end.pathPrepares-start.pathPrepares,end.bankInjections-start.bankInjections});
    }
    void applyPerformanceSettings()
    {
        WaterPerformanceSettings::Apply(options.sceneReflections,options.waveDetail,options.refraction,options.reflectionDetail);
        WaterPerformanceSettings::Applied().sceneColourRequired = WaterPerformanceSettings::SceneColourRequired(
            WaterPerformanceSettings::Refraction(options.refraction),
            options.sceneReflections && (options.reflection == WaterPreviewReflection::ScreenSpace
                || options.reflection == WaterPreviewReflection::Hybrid || comparing()
                || (options.reflection == WaterPreviewReflection::PathTraced && !pathReady)));
    }
    Options options;
    WaterPreviewScene::Scene scene;
    SDL_Window *window = nullptr;
    IDirect3D9 *adapter = nullptr;
    PosixDevice9 *device = nullptr;
    IDirect3DPixelShader9 *shader = nullptr;
    IDirect3DVertexBuffer9 *vertices = nullptr;
    IDirect3DIndexBuffer9 *indices = nullptr;
    std::vector<IDirect3DTexture9 *> textures;
    IDirect3DTexture9 *bed = nullptr, *height = nullptr, *optical = nullptr, *material = nullptr;
    struct MirrorTarget {
        unsigned size;
        IDirect3DTexture9 *texture = nullptr;
        IDirect3DSurface9 *surface = nullptr, *depth = nullptr;
    };
    MirrorTarget legacyMirror{WaterScreenReflection::LegacyMirrorSize},
        hybridMirror{WaterScreenReflection::HybridMirrorSize};
    std::vector<PropVertex> props, mirrorProps;
    std::vector<WaterPathTracing::Triangle> pathTriangles;
    bool pathActive = false, pathReady = false, pathHasProps = false;
    std::vector<WaterVertex> waterGrid;
    D3DMATRIX waterWorld = identity();
    WaterPreviewReflection::Bar reflectionBar;
    float time;
    WaterPreviewCamera::Camera cameraControl;
    bool paused = false, capture = false;
    WaterPreviewColor::Panel colourPanel;
    std::string colourSettingsPath;
    ModernWaterConfig::Material resolvedMaterial;
    std::string loadedConfigPath;

    void loadMaterial()
    {
        auto rules=ModernWaterConfig::Defaults();
        std::string path=options.config;
        if (path.empty()) {
            const char *base=SDL_GetBasePath();
            if (base) {
                for (const char *suffix : {"overlay/Data/INI/ModernWater.ini","Data/INI/ModernWater.ini"}) {
                    const std::string candidate=std::string(base)+suffix;
                    if (std::filesystem::exists(candidate)) { path=candidate; break; }
                }
            }
        }
        if (!path.empty()) {
            std::ifstream input(path,std::ios::binary);
            if (input) {
                const std::string text((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
                std::string error;
                if (!ModernWaterConfig::Parse(text,rules,error))
                    throw std::runtime_error("invalid ModernWater INI "+path+": "+error);
                loadedConfigPath = path;
                std::printf("Modern water configuration: %s\n",path.c_str());
            } else if (std::filesystem::exists(path)) {
                throw std::runtime_error("cannot read ModernWater INI: "+path);
            } else std::printf("Modern water configuration absent (%s); using built-in defaults.\n",path.c_str());
        } else std::puts("Modern water configuration absent; using built-in defaults.");
        resolvedMaterial=ModernWaterConfig::Resolve(rules,mapColour(options.mapColour),options.mapTexture);
        if (options.underwaterClarity>=0) resolvedMaterial.underwaterClarity=options.underwaterClarity;
        if (options.reflectionStrength>=0) resolvedMaterial.reflectionStrength=options.reflectionStrength;
        auto &v=colourPanel.values;
        v.red=resolvedMaterial.color.r/255.f;
        v.green=resolvedMaterial.color.g/255.f;
        v.blue=resolvedMaterial.color.b/255.f;
        v.brightness=resolvedMaterial.brightness;
        v.strength=resolvedMaterial.pollution;
        colourPanel.defaults=v;
        if (!options.bubblesSet) options.bubbles=resolvedMaterial.bubbles;
        colourPanel.status=resolvedMaterial.ruleName.empty()?"MAP COLOR - NO RULE MATCH":"INI MATERIAL LOADED";
        std::printf("Map input: #%s, %s; material: %s; tint #%02X%02X%02X, brightness %.2f, pollution %.2f, bubbles %s.\n",
            options.mapColour.c_str(),options.mapTexture.c_str(),
            resolvedMaterial.ruleName.empty()?"map color fallback":resolvedMaterial.ruleName.c_str(),
            resolvedMaterial.color.r,resolvedMaterial.color.g,resolvedMaterial.color.b,
            v.brightness,v.strength,options.bubbles?"yes":"no");
    }

    void loadColour()
    {
        char *folder=SDL_GetPrefPath("CnCGeneralsZH-Reforged","WaterPreview");
        if (folder) { colourSettingsPath=std::string(folder)+"colors.ini"; SDL_free(folder); }
        // The INI is authoritative unless loading a local editor preset is requested explicitly.
        if (options.loadPreset && !loadSavedColour())
            throw std::runtime_error("no valid saved preview preset at "+colourSettingsPath);
        auto &v=colourPanel.values;
        if (options.strengthSet) v.strength=options.strength;
        if (options.brightnessSet) v.brightness=options.brightness;
        if (!options.colour.empty()) {
            const unsigned rgb=unsigned(std::strtoul(options.colour.c_str(),nullptr,16));
            v.red=float((rgb>>16)&255)/255; v.green=float((rgb>>8)&255)/255; v.blue=float(rgb&255)/255;
        }
        if (options.strengthSet || options.brightnessSet || !options.colour.empty())
            { colourPanel.status="UNSAVED CHANGES"; colourPanel.edited=true; }
        colourPanel.visible=options.controls;
    }

    bool loadSavedColour()
    {
        if (colourSettingsPath.empty()) { colourPanel.status="LOAD UNAVAILABLE"; return false; }
        std::ifstream input(colourSettingsPath);
        if (!input) { colourPanel.status="NO SAVED COLOR FOUND"; return false; }
        WaterPreviewColor::Values saved;
        if (!WaterPreviewColor::readPreset(input, saved)) {
            colourPanel.status="SAVED COLOR INVALID";
            return false;
        }
        colourPanel.values=saved;
        colourPanel.edited=true;
        colourPanel.status="SAVED COLOR LOADED";
        std::printf("Loaded preview color: %s\n",colourSettingsPath.c_str());
        return true;
    }

    void saveColour()
    {
        colourPanel.saveRequested=false;
        if (colourSettingsPath.empty()) { colourPanel.status="SAVE UNAVAILABLE"; return; }
        const auto &v=colourPanel.values;
        const std::string temporary=colourSettingsPath+".tmp";
        std::ofstream output(temporary,std::ios::trunc);
        output.imbue(std::locale::classic());
        output.precision(9);
        output<<"WATER_PREVIEW_COLOR_V1\n"<<v.red<<' '<<v.green<<' '<<v.blue<<' '
            <<v.brightness<<' '<<v.strength<<'\n';
        output.close();
        if (!output || std::rename(temporary.c_str(),colourSettingsPath.c_str())!=0) {
            std::remove(temporary.c_str());
            colourPanel.status="SAVE FAILED";
            return;
        }
        colourPanel.status="COLOR SAVED";
        std::printf("Saved preview color: %s\n",colourSettingsPath.c_str());
    }

    void exportRule()
    {
        const std::string path=options.exportConfig.empty()?"water-preview-rule.ini":options.exportConfig;
        if (options.exportConfig.empty()
            && WaterPreviewColor::defaultExportOverwritesInput(loadedConfigPath, path))
            throw std::runtime_error("default export would overwrite the loaded configuration; "
                "choose --export-config explicitly");
        const std::string temporary=path+".tmp";
        const auto input=mapColour(options.mapColour);
        const auto &v=colourPanel.values;
        const auto channel=[](float value) { return int(std::lround(std::clamp(value,0.f,1.f)*255)); };
        std::ostringstream contents;
        contents.imbue(std::locale::classic());
        contents.precision(9);
        contents<<"; Exported by Water Preview. Add entries to either matching list as needed.\n"
            <<"ModernWaterRule PreviewMaterial\n"
            <<"  MatchColors\n    Color = R:"<<input.r<<" G:"<<input.g<<" B:"<<input.b<<"\n  End\n"
            <<"  MatchTextures\n    Texture = "<<options.mapTexture<<"\n  End\n"
            <<"  ReplacementColor = R:"<<channel(v.red)<<" G:"<<channel(v.green)<<" B:"<<channel(v.blue)<<"\n"
            <<"  Brightness = "<<v.brightness<<"\n  Pollution = "<<v.strength
            <<"\n  Bubbles = "<<(options.bubbles?"Yes":"No")
            <<"\n  UnderwaterClarity = "<<resolvedMaterial.underwaterClarity
            <<"\n  ReflectionStrength = "<<resolvedMaterial.reflectionStrength<<"\nEnd\n";
        ModernWaterConfig::Rules parsed;
        std::string error;
        if (!ModernWaterConfig::Parse(contents.str(),parsed,error))
            throw std::runtime_error("cannot export material rule: "+error);
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<contents.str();
        output.close();
        if (!output || std::rename(temporary.c_str(),path.c_str())!=0) {
            std::remove(temporary.c_str());
            throw std::runtime_error("cannot write material rule: "+path);
        }
        colourPanel.status="INI RULE EXPORTED";
        std::printf("Exported ModernWater rule: %s\n",path.c_str());
    }

    IDirect3DTexture9 *texture(unsigned side, const std::vector<uint32_t> &pixels)
    {
        IDirect3DTexture9 *result = nullptr;
        require(device->CreateTexture(side,side,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&result,nullptr),"create texture");
        textures.push_back(result);
        D3DLOCKED_RECT locked = {};
        require(result->LockRect(0,&locked,nullptr,0),"lock texture");
        for (unsigned y=0; y<side; ++y)
            std::memcpy(static_cast<uint8_t *>(locked.pBits)+y*locked.Pitch,pixels.data()+y*side,side*4);
        require(result->UnlockRect(0),"unlock texture");
        return result;
    }

    void initialize()
    {
        legacyMirror.size=WaterScreenReflection::MirrorSize(WaterScreenReflection::Mirror,options.reflectionDetail);
        hybridMirror.size=WaterScreenReflection::MirrorSize(WaterScreenReflection::Hybrid,options.reflectionDetail);
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(std::string("SDL video: ")+SDL_GetError());
        if (!options.scene.empty()) {
            scene.load(options.scene);
            if (!options.widthSet) options.width=scene.bank.width;
            if (!options.heightSet) options.height=scene.bank.height;
            time=0;
            const auto &c=scene.bank.draws.front().pixelConstants;
            resolvedMaterial.underwaterClarity=c.size()>=(ModernWaterMaterial::RegisterOptics+1)*4
                ? c[ModernWaterMaterial::RegisterOptics*4]:0.f;
            if(options.underwaterClarity>=0) resolvedMaterial.underwaterClarity=options.underwaterClarity;
            resolvedMaterial.reflectionStrength=c.size()>=(ModernWaterMaterial::RegisterOptics+1)*4
                ? 1.f+c[ModernWaterMaterial::RegisterOptics*4+1]:1.f;
            if(options.reflectionStrength>=0) resolvedMaterial.reflectionStrength=options.reflectionStrength;
            auto &v=colourPanel.values;
            v.red=c[94*4]; v.green=c[94*4+1]; v.blue=c[94*4+2]; v.brightness=c[94*4+3]; v.strength=c[95*4+1];
            colourPanel.defaults=v;
            if (!options.bubblesSet) options.bubbles=c[95*4+2]>.5f;
            colourPanel.status="BANKED MATERIAL - FIXED CAMERA";
        } else loadMaterial();
        loadColour();
        if (scene.loaded() && options.bubblesSet) colourPanel.edited=true;
        if (!options.exportConfig.empty()) exportRule();
        // The preview requires the game's native FFT path even if a user's game launch disables it.
        SDL_setenv_unsafe("ZH_WATER_FFT","1",1);
        // An inherited game diagnostic would replace the picture; only --debug asks for one.
        if (options.debug.empty()) SDL_unsetenv_unsafe("ZH_WATER_DEBUG");
        else SDL_setenv_unsafe("ZH_WATER_DEBUG",options.debug.c_str(),1);
        if (!options.offscreen) {
            window = SDL_CreateWindow("Water Preview",options.width,options.height,0);
            if (!window) throw std::runtime_error(std::string("create window: ")+SDL_GetError());
        } else SDL_SetHint("ZH_OFFSCREEN_FRAMES","1");
        adapter = Direct3DCreate9(D3D_SDK_VERSION);
        if (!adapter) throw std::runtime_error("create D3D9-shaped SDL adapter");
        D3DPRESENT_PARAMETERS parameters = {};
        parameters.BackBufferWidth=options.width; parameters.BackBufferHeight=options.height;
        parameters.BackBufferFormat=D3DFMT_X8R8G8B8;
        parameters.hDeviceWindow=reinterpret_cast<RenderWindow>(window);
        parameters.Windowed=1; parameters.EnableAutoDepthStencil=1; parameters.AutoDepthStencilFormat=D3DFMT_D24S8;
        IDirect3DDevice9 *created = nullptr;
        require(adapter->CreateDevice(0,D3DDEVTYPE_HAL,parameters.hDeviceWindow,0,&parameters,&created),"create GPU device");
        device = static_cast<PosixDevice9 *>(created);
        if (!device->Get_Gpu()) throw std::runtime_error("no SDL GPU frame available");
        const SDL_PropertiesID gpuProperties=SDL_GetGPUDeviceProperties(device->Get_Gpu()->Device());
        std::printf("GPU: %s; driver: %s; driver_info: %s; ICD: %s\n",
            SDL_GetStringProperty(gpuProperties,SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"),
            SDL_GetStringProperty(gpuProperties,SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,"unknown"),
            SDL_GetStringProperty(gpuProperties,SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING,
                SDL_GetStringProperty(gpuProperties,SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,"unknown")),
            SDL_getenv("VK_ICD_FILENAMES")?SDL_getenv("VK_ICD_FILENAMES"):"loader default");
        std::printf("Water settings: scene_reflections=%s wave_detail=%s refraction=%s reflection_detail=%s; start_time=%.6f frames=%u scene=%s\n",
            options.sceneReflections?"on":"off",options.waveDetail?"high":"low",
            options.refraction==0?"full":options.refraction==1?"simple":"off",options.reflectionDetail?"high":"low",options.time,options.frames,
            options.scene.empty()?"synthetic":options.scene.c_str());
        std::printf("Renderer: %s; %ux%u; standalone production-water preview\n",SDL_GetGPUDeviceDriver(device->Get_Gpu()->Device()),options.width,options.height);
        const RenderUInt32 tokens[] = {0xffff0101u,0x0000ffffu};
        require(device->CreatePixelShader(tokens,&shader),"create water shader");
        PosixDevice_Name_Shader(shader,"trapezoid water ps.1.1");
        if (scene.loaded()) {
            scene.initialize(device);
            if (!options.reflectionDetail)
                std::printf("Banked planar detail retains captured resolution; Reflection detail Low cannot regenerate or resize this bank.\n");
        }
        else {
            createMesh();
            createTextures();
            props=buildProps(-1.0e9f);
            if (options.sceneReflections) mirrorProps=buildProps(WaterLevel);
        }
        reflectionBar.choice=options.reflection;
        reflectionBar.pathAvailable=!scene.loaded();
        device->SetRenderState(D3DRS_LIGHTING,0);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        device->SetRenderState(D3DRS_FOGENABLE,0);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,0);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        device->SetVertexShader(nullptr);
        for (unsigned stage=0; stage<8; ++stage) {
            device->SetSamplerState(stage,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
            device->SetSamplerState(stage,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            device->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            device->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,stage);
            device->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        }
    }

    void createMesh()
    {
        waterGrid.reserve((Grid+1)*(Grid+1));
        for (unsigned y=0; y<=Grid; ++y) for (unsigned x=0; x<=Grid; ++x) {
            const float px=float(x)*GridCell-Extent, py=float(y)*GridCell-Extent;
            // Exact production FFT metadata: weight, free damping, offshore exposure;
            // UV0 geometric correction, UV1 attenuation and original water elevation.
            waterGrid.push_back({px,py,WaterLevel,1.0f,0.72f,0.26f,0xffffffffu,{0,0,0.72f,WaterLevel,0,0,0,0}});
        }
        std::vector<uint16_t> faces;
        faces.reserve(Grid*Grid*6);
        for (unsigned y=0; y<Grid; ++y) for (unsigned x=0; x<Grid; ++x) {
            const uint16_t a=uint16_t(y*(Grid+1)+x), b=uint16_t(a+1), c=uint16_t(a+Grid+1), d=uint16_t(c+1);
            faces.insert(faces.end(),{a,b,c,b,d,c});
        }
        require(device->CreateVertexBuffer(unsigned(waterGrid.size()*sizeof(WaterVertex)),D3DUSAGE_WRITEONLY,WaterFVF,D3DPOOL_MANAGED,&vertices,nullptr),"create water vertices");
        void *memory = nullptr;
        require(vertices->Lock(0,0,&memory,0),"lock water vertices");
        std::memcpy(memory,waterGrid.data(),waterGrid.size()*sizeof(WaterVertex));
        require(vertices->Unlock(),"unlock water vertices");
        require(device->CreateIndexBuffer(unsigned(faces.size()*sizeof(uint16_t)),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices,nullptr),"create water indices");
        require(indices->Lock(0,0,&memory,0),"lock water indices");
        std::memcpy(memory,faces.data(),faces.size()*sizeof(uint16_t));
        require(indices->Unlock(),"unlock water indices");
    }

    void moveWaterGrid(const D3DMATRIX &world)
    {
        if (world._41==waterWorld._41 && world._42==waterWorld._42) return;
        // The production vertex FFT takes input.Position.xy before the fixed transform,
        // whereas the pixel shader reconstructs world.xy. Bake this world translation into
        // the upload and draw with identity, so both stages sample the same fixed positions.
        // Integer cells also keep shared vertices and their triangles identical after a step.
        for (unsigned y=0; y<=Grid; ++y) for (unsigned x=0; x<=Grid; ++x) {
            auto &v=waterGrid[y*(Grid+1)+x];
            v.x=float(x)*GridCell-Extent+world._41;
            v.y=float(y)*GridCell-Extent+world._42;
        }
        void *memory=nullptr;
        require(vertices->Lock(0,0,&memory,0),"lock moving water vertices");
        std::memcpy(memory,waterGrid.data(),waterGrid.size()*sizeof(WaterVertex));
        require(vertices->Unlock(),"unlock moving water vertices");
        waterWorld=world;
    }

    void createTextures()
    {
        std::vector<uint32_t> colour(256*256), detail(256*256);
        for (unsigned y=0; y<256; ++y) for (unsigned x=0; x<256; ++x) {
            uint32_t hash=(x*1973u+y*9277u+89173u); hash=(hash^(hash>>13))*1274126177u;
            const float grain=float(hash&255)/255.0f;
            const float variation=std::sin(float(x)*0.068f+std::sin(float(y)*0.037f)*2)*0.04f;
            const auto channel=[](float c) { return uint32_t(std::clamp(c,0.0f,1.0f)*255.0f); };
            colour[y*256+x]=0xff000000u|(channel(.39f+grain*.11f+variation)<<16)
                |(channel(.36f+grain*.09f+variation)<<8)|channel(.27f+grain*.07f+variation);
            detail[y*256+x]=0xff000000u|(channel(.49f+grain*.12f)<<16)|(channel(.40f+grain*.10f)<<8)|channel(.45f+grain*.10f);
        }
        bed=texture(256,colour);
        material=texture(256,detail);
        height=texture(1,{0x00000000u}); // zero-height bed, alpha zero disables wave cages
        optical=texture(1,{0xff430000u}); // enclosed-water exposure: 0.263, no coastline
    }

    // Allocate only modes that are actually shown. Compare keeps separate targets so its
    // Legacy column remains the original 512-square baseline while Hybrid gets more detail.
    void createMirror(MirrorTarget &target, const char *name)
    {
        if (target.texture) return;
        require(device->CreateTexture(target.size,target.size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,&target.texture,nullptr),"create mirror target");
        require(target.texture->GetSurfaceLevel(0,&target.surface),"mirror target surface");
        require(device->CreateDepthStencilSurface(target.size,target.size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,1,
            &target.depth,nullptr),"create mirror depth");
        std::printf("Synthetic planar target: %s %ux%u.\n",name,target.size,target.size);
    }

    bool comparing() const { return options.reflection==WaterPreviewReflection::Compare; }
    // Compare draws the same camera three times, each in a third of the window.
    float viewAspect() const
    {
        return float(comparing()?options.width/3:options.width)/options.height;
    }
    unsigned columnLeft(unsigned i) const { return options.width/3*i; }
    unsigned columnWidth(unsigned i) const { return i==2?options.width-2*(options.width/3):options.width/3; }

    bool showsMirror() const
    {
        return options.sceneReflections && options.props && (options.reflection==WaterPreviewReflection::Mirror
            || options.reflection==WaterPreviewReflection::Hybrid || comparing()
            || (options.reflection==WaterPreviewReflection::PathTraced && !pathReady));
    }

    void drawProps(const std::vector<PropVertex> &list)
    {
        if (list.empty()) return;
        device->SetPixelShader(nullptr);
        for (unsigned stage=0; stage<16; ++stage) device->SetTexture(stage,nullptr);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
        require(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,unsigned(list.size()/3),list.data(),sizeof(PropVertex)),"draw props");
    }

    // The scene seen from the camera reflected in the water plane, as WaterRenderObjClass::renderMirror
    // draws it: a point on the plane lands where the main camera sees it, so the shader can look the
    // mirror up at its own screen position.
    void drawMirror(MirrorTarget &target, const char *name, unsigned viewWidth)
    {
        createMirror(target,name);
        ++planarRenders;
        IDirect3DSurface9 *colour=nullptr,*depth=nullptr;
        require(device->GetRenderTarget(0,&colour),"get render target");
        require(device->GetDepthStencilSurface(&depth),"get depth target");
        D3DMATRIX view,project;
        device->GetTransform(D3DTS_VIEW,&view);
        device->GetTransform(D3DTS_PROJECTION,&project);
        const D3DMATRIX reflectedProject=projection(float(viewWidth)/options.height);
        D3DMATRIX reflect=identity();
        reflect._33=-1; reflect._43=2*WaterLevel;
        const D3DMATRIX mirrored=multiply(reflect,view);
        require(device->SetRenderTarget(0,target.surface),"set mirror target");
        require(device->SetDepthStencilSurface(target.depth),"set mirror depth");
        const D3DVIEWPORT9 viewport={0,0,target.size,target.size,0,1};
        require(device->SetViewport(&viewport),"set mirror viewport");
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0x00000000u,1,0),"clear mirror");
        device->SetTransform(D3DTS_VIEW,&mirrored);
        device->SetTransform(D3DTS_PROJECTION,&reflectedProject);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE,1);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
        drawProps(mirrorProps);
        device->SetTransform(D3DTS_VIEW,&view);
        device->SetTransform(D3DTS_PROJECTION,&project);
        require(device->SetRenderTarget(0,colour),"restore render target");
        require(device->SetDepthStencilSurface(depth),"restore depth target");
        colour->Release();
        if (depth) depth->Release();
        const D3DVIEWPORT9 full={0,0,options.width,options.height,0,1};
        require(device->SetViewport(&full),"restore viewport");
    }

    void camera()
    {
        cameraControl.clamp();
        auto &c=cameraControl.state;
        const Vec3 offset={std::sin(c.yaw)*std::cos(c.pitch),-std::cos(c.yaw)*std::cos(c.pitch),std::sin(c.pitch)};
        const Vec3 right=normalized(cross({0,0,1},offset)),up=cross(offset,right);
        const float tangent=std::tan(48.0f*Pi/360.0f),aspect=viewAspect();
        float farthest=0;
        for (int x : {-1,1}) for (int y : {-1,1}) {
            const Vec3 ray={-offset.x+tangent*(up.x*y+right.x*x*aspect),
                -offset.y+tangent*(up.y*y+right.y*x*aspect),-offset.z+tangent*(up.z*y+right.z*x*aspect)};
            const float travel=-offset.z/ray.z;
            farthest=std::max(farthest,std::max(std::abs(offset.x+ray.x*travel),std::abs(offset.y+ray.y*travel)));
        }
        // Keep the finite patch outside every camera ray, including wide windows and
        // low viewpoints. Measure from the target, with room for half a snapped grid cell
        // and the actual wave displacement near the boundary.
        c.distance=std::min(c.distance,(Extent*.90f-GridCell*.5f)/std::max(farthest,.01f));
        const Vec3 eye={c.x+offset.x*c.distance,c.y+offset.y*c.distance,WaterLevel+offset.z*c.distance};
        const D3DMATRIX world=identity(),view=viewMatrix(eye,{c.x,c.y,WaterLevel}),project=projection(viewAspect());
        device->SetTransform(D3DTS_WORLD,&world);
        device->SetTransform(D3DTS_VIEW,&view);
        device->SetTransform(D3DTS_PROJECTION,&project);
    }

    void preparePathTracing()
    {
        if (!options.sceneReflections || options.reflection!=WaterPreviewReflection::PathTraced) {
            if (pathActive) WaterPathTracing::Clear(device);
            pathActive=false;
            pathReady=false;
            return;
        }
        // Build only on first use or a scenery toggle. Camera movement changes the rays,
        // not the world-space geometry. Colours are authored albedo, before raster lighting.
        if (pathTriangles.empty() || pathHasProps!=options.props) {
            pathTriangles.clear();
            pathHasProps=options.props;
            const auto linear=[](uint32_t color) {
                return WaterPathTracing::DisplayToLinear({float((color>>16)&255)/255.f,
                    float((color>>8)&255)/255.f,float(color&255)/255.f});
            };
            if (options.props) {
                const auto unlit=buildProps(-1.0e9f,false);
                pathTriangles.reserve(unlit.size()/3+2);
                for (size_t i=0;i+2<unlit.size();i+=3) {
                    const auto &a=unlit[i],&b=unlit[i+1],&c=unlit[i+2];
                    pathTriangles.push_back({{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},linear(a.colour)});
                }
            }
            // The raster bed has a subtle procedural grain; use its average albedo here.
            const auto bedAlbedo=linear(0xff71674eu);
            pathTriangles.push_back({{-BedExtent,-BedExtent,0},{BedExtent,-BedExtent,0},
                {-BedExtent,BedExtent,0},bedAlbedo});
            pathTriangles.push_back({{BedExtent,-BedExtent,0},{BedExtent,BedExtent,0},
                {-BedExtent,BedExtent,0},bedAlbedo});
        }
        const auto &c=cameraControl.state;
        const Vec3 offset={std::sin(c.yaw)*std::cos(c.pitch),-std::cos(c.yaw)*std::cos(c.pitch),std::sin(c.pitch)};
        const Vec3 right=normalized(cross({0,0,1},offset)),up=cross(offset,right);
        const float tangent=std::tan(48.0f*Pi/360.0f),aspect=viewAspect();
        const auto reflected=[](Vec3 v) { return WaterPathTracing::Vec3{v.x,v.y,-v.z}; };
        WaterPathTracing::View view;
        WaterPathTracing::ApplyReflectionDetail(view,WaterPerformanceSettings::Applied().reflectionDetail);
        view.origin={c.x+offset.x*c.distance,c.y+offset.y*c.distance,WaterLevel-offset.z*c.distance};
        view.rayTopLeft=reflected({-offset.x+tangent*(up.x-right.x*aspect),
            -offset.y+tangent*(up.y-right.y*aspect),-offset.z+tangent*(up.z-right.z*aspect)});
        view.rayDx=reflected({2*tangent*aspect*right.x,2*tangent*aspect*right.y,2*tangent*aspect*right.z});
        view.rayDy=reflected({-2*tangent*up.x,-2*tangent*up.y,-2*tangent*up.z});
        const Vec3 sun=normalized({-.30f,-.45f,.84f});
        view.sunDirection={sun.x,sun.y,sun.z};
        view.sunRadiance=WaterPathTracing::DisplayDiffuseToIrradiance({.88f,.81f,.66f});
        view.skyLow=WaterPathTracing::DisplayToLinear({.49f,.62f,.68f});
        view.skyHigh=WaterPathTracing::DisplayToLinear({.23f,.38f,.48f});
        view.waterLevel=WaterLevel;
        const bool wasReady=pathReady;
        ++pathPrepares;
        pathReady=WaterPathTracing::Prepare(device,pathTriangles,view);
        if (!pathActive || pathReady!=wasReady) {
            std::printf("Water path tracing: %s%s\n",WaterPathTracing::Report(device).c_str(),
                pathReady?"":"; using Hybrid fallback");
        }
        pathActive=true;
    }

    void draw()
    {
        applyPerformanceSettings();
        if (scene.loaded()) {
            require(device->BeginScene(),"begin bank scene");
            bankInjections += comparing()?3:1;
            scene.draw(options.width,options.height,options.reflection,time,colourPanel.edited,colourPanel.values,options.bubbles,options.mode,options.underwaterClarity);
            colourPanel.draw(device,options.width,options.height);
            if (options.controls) reflectionBar.draw(device,options.width,options.height,comparing()?3:1,true);
            require(device->EndScene(),"end bank scene");
            checkDraws();
            return;
        }
        camera();
        preparePathTracing();
        applyPerformanceSettings(); // path setup may have selected the Hybrid fallback
        require(device->BeginScene(),"begin scene");
        if (showsMirror()) {
            if (comparing() || options.reflection==WaterPreviewReflection::Mirror)
                drawMirror(legacyMirror,"legacy",comparing()?columnWidth(0):options.width);
            if (comparing() || options.reflection==WaterPreviewReflection::Hybrid
                || (options.reflection==WaterPreviewReflection::PathTraced && !pathReady))
                drawMirror(hybridMirror,"hybrid",comparing()?columnWidth(2):options.width);
        }
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff1b2830u,1,0),"clear frame");
        // Every view's opaque scene precedes any water, so the device's one scene snapshot of
        // the frame holds all of it for the refraction and the trace.
        if (comparing()) {
            for (unsigned i=0; i<3; ++i) {
                const D3DVIEWPORT9 viewport={columnLeft(i),0,columnWidth(i),options.height,0,1};
                require(device->SetViewport(&viewport),"set column viewport");
                const D3DMATRIX project=projection(float(columnWidth(i))/options.height);
                require(device->SetTransform(D3DTS_PROJECTION,&project),"set column projection");
                drawScene();
            }
        } else drawScene();
        // The first Legacy comparison column may need only depth, but later SSR/Hybrid
        // columns still need the common pre-water colour from all opaque views.
        if (comparing()) device->Get_Gpu()->Prepare_Water_Snapshot(device->Get_Gpu()->Back_Buffer_Target(),
            WaterPerformanceSettings::Applied().sceneColourRequired,true);
        drawAllWater();
        colourPanel.draw(device,options.width,options.height);
        if (options.controls)
            reflectionBar.draw(device,options.width,options.height,comparing()?3:1);
        require(device->EndScene(),"end scene");
        checkDraws();
        if (!device->Get_Gpu()->Ocean_Updates()) throw std::runtime_error("production FFT did not initialize");
    }

    void checkDraws()
    {
        if (!device->Draw_Refusals().empty()) {
            std::string why="GPU refused a draw:";
            for (const auto &entry:device->Draw_Refusals()) why+=" "+entry.first+";";
            throw std::runtime_error(why);
        }
    }

    void drawScene()
    {
        const D3DMATRIX world=identity();
        device->SetTransform(D3DTS_WORLD,&world);
        device->SetPixelShader(nullptr);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);
        device->SetRenderState(D3DRS_ZWRITEENABLE,1);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
        device->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffffu);
        for (unsigned stage=0; stage<16; ++stage) device->SetTexture(stage,nullptr);
        device->SetTexture(0,bed);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1);
        const float edge=BedExtent,uv=edge/80;
        const BedVertex floor[]={{-edge,-edge,0,0xffffffffu,-uv,-uv},{edge,-edge,0,0xffffffffu,uv,-uv},{-edge,edge,0,0xffffffffu,-uv,uv},
            {edge,-edge,0,0xffffffffu,uv,-uv},{edge,edge,0,0xffffffffu,uv,uv},{-edge,edge,0,0xffffffffu,-uv,uv}};
        require(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,floor,sizeof(BedVertex)),"draw submerged bed");
        // Opaque scenery precedes the water, so the device's scene snapshot holds it for the trace.
        if (options.props) {
            drawProps(props);
            device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
            device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
            device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
            device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
            device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        }
    }

    void drawAllWater()
    {
        // Only the water mesh follows the target. Whole-cell steps keep every vertex on
        // the same world lattice; moveWaterGrid applies that transform before the FFT.
        const auto &c=cameraControl.state;
        D3DMATRIX world=identity();
        world._41=std::round(c.x/GridCell)*GridCell;
        world._42=std::round(c.y/GridCell)*GridCell;
        moveWaterGrid(world);
        const D3DMATRIX fixed=identity();
        device->SetTransform(D3DTS_WORLD,&fixed);
        device->SetTexture(0,nullptr);
        device->SetTexture(2,height);
        device->SetTexture(4,optical);
        device->SetTexture(7,material);
        device->SetPixelShader(shader);
        // Height and optical context describe the fixed world bed, not the moving patch.
        // Their one-texel fields are uniform; the full bed mapping still covers every offset.
        const float constants[7][4]={{time,0,0,WaterLevel},{-Extent,-Extent,Extent,Extent},
            {-.30f,-.45f,.84f,0},{.88f,.81f,.66f,1},{BedExtent,BedExtent*2,100,1},
            {options.mapDepth,options.mapOpacity,1,1},{0,.26f,.28f,.30f}};
        device->SetPixelShaderConstantF(1,&constants[0][0],7);
        const float fill[4][4]={{0,0,0,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}};
        device->SetPixelShaderConstantF(90,&fill[0][0],4);
        device->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffffu);
        device->SetRenderState(D3DRS_ZWRITEENABLE,0);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,1);
        device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        device->SetFVF(WaterFVF);
        device->SetStreamSource(0,vertices,0,sizeof(WaterVertex));
        device->SetIndices(indices);
        const int reflection=options.reflection;
        if (comparing()) {
            // Legacy (512) | screen space | hybrid (1024): the same view three times over.
            for (unsigned i=0; i<3; ++i)
                drawWater(options.mode!=1,columnLeft(i),columnWidth(i),int(i),true);
            const D3DVIEWPORT9 viewport={0,0,options.width,options.height,0,1};
            require(device->SetViewport(&viewport),"restore viewport");
        } else if (options.mode==3) {
            // Crop the same projection into two viewports. The device's viewport scissor
            // supplies the comparison boundary; production shading needs no split hook.
            drawWater(false,0,options.width/2,reflection,false);
            drawWater(true,options.width/2,options.width-options.width/2,reflection,false);
            const D3DVIEWPORT9 viewport={0,0,options.width,options.height,0,1};
            require(device->SetViewport(&viewport),"restore viewport");
            const D3DMATRIX project=projection(float(options.width)/options.height);
            require(device->SetTransform(D3DTS_PROJECTION,&project),"restore projection");
        } else drawWater(options.mode!=1,0,options.width,reflection,false);
    }

    // `own` draws a complete view in the viewport; otherwise the viewport crops the window's view.
    void drawWater(bool enabled,unsigned left,unsigned width,int reflection,bool own)
    {
        // Each mode is a per-draw choice in the production shader: the mirror (c1.z, stage 1,
        // the viewport's slice of it in c2) and the trace (c95.w), as the game's water sets them.
        const bool path=options.sceneReflections && reflection==WaterPreviewReflection::PathTraced && pathReady;
        if (reflection==WaterPreviewReflection::PathTraced && !path) reflection=WaterPreviewReflection::Hybrid;
        const bool mirror=showsMirror() && (reflection==WaterPreviewReflection::Mirror || reflection==WaterPreviewReflection::Hybrid);
        const bool trace=options.sceneReflections && (reflection==WaterPreviewReflection::ScreenSpace || reflection==WaterPreviewReflection::Hybrid);
        const float water[4]={time,0,(mirror||path)?1.f:0.f,WaterLevel};
        const float areaLeft=own?-1.f:-2.f*float(left)/float(width)-1.f;
        const float area[4]={areaLeft,-1,own?1.f:areaLeft+2.f*float(options.width)/float(width),1};
        require(device->SetPixelShaderConstantF(1,water,1),"set water parameters");
        require(device->SetPixelShaderConstantF(2,area,1),"set reflection area");
        device->SetTexture(1,mirror
            ? (reflection==WaterPreviewReflection::Hybrid?hybridMirror.texture:legacyMirror.texture)
            : nullptr);
        // Synthetic Legacy has always used linear filtering. Set both modes explicitly so
        // a preceding diagnostic/UI pass cannot leave Hybrid sampling whole mirror texels.
        device->SetSamplerState(1,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(1,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        const D3DVIEWPORT9 viewport={left,0,width,options.height,0,1};
        require(device->SetViewport(&viewport),"set water viewport");
        D3DMATRIX project=projection(float(own?width:options.width)/options.height);
        if (!own) {
            project._11*=float(options.width)/width;
            const float offset=(float(options.width)-2.f*left-width)/width;
            project._31+=offset*project._34;
        }
        require(device->SetTransform(D3DTS_PROJECTION,&project),"set water projection");
        const auto &v=colourPanel.values;
        const float colour[4]={v.red,v.green,v.blue,v.brightness};
        // Editing a neutral fallback creates an explicit tint without changing pollution.
        const float tint=resolvedMaterial.tintAmount>0 || v.red<1 || v.green<1 || v.blue<1 || v.brightness<1 ? 1.f : 0.f;
        const float parameters[4]={enabled?tint:0.f,enabled?v.strength:0.f,enabled&&options.bubbles?1.f:0.f,path?2.f:trace?1.f:0.f};
        require(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterColor,colour,1),"set water material color");
        require(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterParameters,parameters,1),"set water material parameters");
        const float optics[4]={enabled?resolvedMaterial.underwaterClarity:0.f,
            enabled?resolvedMaterial.reflectionStrength-1.f:0.f,0,0};
        require(device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterOptics,optics,1),"set water material optics");
        require(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,(Grid+1)*(Grid+1),0,Grid*Grid*2),"draw modern water");
    }

    void save(const std::string &path)
    {
        std::vector<uint8_t> pixels;
        if (!device->Get_Gpu()->Read_Back(device->Get_Gpu()->Back_Buffer(),options.width,options.height,pixels))
            throw std::runtime_error("read screenshot from GPU");
        // The window ignores destination alpha, which a banked game frame carries for shore
        // blending; a picture with it left in shows transparent holes the window never had.
        for (size_t i=3; i<pixels.size(); i+=4) pixels[i]=255;
        SDL_Surface *surface=SDL_CreateSurfaceFrom(options.width,options.height,SDL_PIXELFORMAT_BGRA32,pixels.data(),options.width*4);
        if (!surface) throw std::runtime_error(std::string("create screenshot surface: ")+SDL_GetError());
        const bool bmp=path.size()>=4 && path.substr(path.size()-4)==".bmp";
        const bool saved=bmp?SDL_SaveBMP(surface,path.c_str()):SDL_SavePNG(surface,path.c_str());
        SDL_DestroySurface(surface);
        if (!saved) throw std::runtime_error(std::string("save screenshot: ")+SDL_GetError());
        std::printf("Screenshot: %s\n",path.c_str());
    }

    void updateTitle()
    {
        if (!window) return;
        char title[320];
        const char *mode=options.mode==1?"Clean baseline":options.mode==2?"Resolved material":"Baseline LEFT | Material RIGHT";
        const char *reflection=options.reflection==WaterPreviewReflection::Mirror?(scene.loaded()?"captured planar":"legacy")
            :options.reflection==WaterPreviewReflection::ScreenSpace?"SSR"
            :options.reflection==WaterPreviewReflection::Hybrid?"hybrid (legacy + SSR)"
            :options.reflection==WaterPreviewReflection::PathTraced?"path traced (experimental)"
            :scene.loaded()?"captured planar | SSR | hybrid":"legacy | SSR | hybrid";
        std::snprintf(title,sizeof(title),"Water Preview: %s | reflection %s | brightness %.0f%% | pollution %.0f%% | bubbles %s | H panel, E export%s",
            mode,reflection,colourPanel.values.brightness*100,colourPanel.values.strength*100,options.bubbles?"ON":"OFF",paused?" | PAUSED":"");
        if (scene.loaded()) {
            const std::string bankTitle=std::string(title)+" | "+std::filesystem::path(scene.path).filename().string()+" | FIXED CAMERA (banked frame)";
            SDL_SetWindowTitle(window,bankTitle.c_str());
        } else SDL_SetWindowTitle(window,title);
    }

    void handle(const SDL_Event &event, bool &done)
    {
        if (event.type==SDL_EVENT_WINDOW_FOCUS_LOST) cameraControl.cancelDrag();
        if (!scene.loaded() && cameraControl.captures(event)) {
            cameraControl.handle(event);
            return;
        }
        if (colourPanel.handle(event)) {
            if (colourPanel.saveRequested) saveColour();
            updateTitle();
            return;
        }
        if (options.controls && reflectionBar.handle(event,options.width)) {
            if (reflectionBar.changed) { options.reflection=reflectionBar.choice; reflectionBar.changed=false; }
            updateTitle();
            return;
        }
        if (event.type==SDL_EVENT_QUIT) done=true;
        else if (!scene.loaded() && cameraControl.handle(event)) return;
        else if (event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            switch (event.key.key) {
                case SDLK_ESCAPE: done=true; break;
                case SDLK_1: options.mode=1; break;
                case SDLK_2: options.mode=2; break;
                case SDLK_3: options.mode=3; break;
                case SDLK_B: options.bubbles=!options.bubbles; colourPanel.edited=true; colourPanel.status="UNSAVED CHANGES"; break;
                case SDLK_P: options.props=!options.props; break;
                case SDLK_4: case SDLK_5: case SDLK_6:
                    options.reflection=reflectionBar.choice=int(event.key.key-SDLK_4); break;
                case SDLK_7: options.reflection=reflectionBar.choice=WaterPreviewReflection::Compare; break;
                case SDLK_8:
                    if (!scene.loaded()) options.reflection=reflectionBar.choice=WaterPreviewReflection::PathTraced;
                    break;
                case SDLK_SPACE: paused=!paused; break;
                case SDLK_S: capture=true; break;
                case SDLK_L: colourPanel.visible=true; loadSavedColour(); break;
                case SDLK_E:
                    try { exportRule(); }
                    catch (const std::exception &error) { colourPanel.status="EXPORT FAILED"; std::fprintf(stderr,"%s\n",error.what()); }
                    break;
                case SDLK_LEFTBRACKET: colourPanel.values.strength=std::max(0.0f,colourPanel.values.strength-.05f); colourPanel.edited=true; colourPanel.status="UNSAVED CHANGES"; break;
                case SDLK_RIGHTBRACKET: colourPanel.values.strength=std::min(1.0f,colourPanel.values.strength+.05f); colourPanel.edited=true; colourPanel.status="UNSAVED CHANGES"; break;
                default: break;
            }
            updateTitle();
        }
    }

    void keyboard(float dt)
    {
        if (scene.loaded()) return;
        cameraControl.update(dt,SDL_GetKeyboardState(nullptr));
    }
};
} // namespace

int main(int argc, char **argv)
{
    for (int i=1;i<argc;++i) if (std::strcmp(argv[i],"--help")==0 || std::strcmp(argv[i],"-h")==0) {help();return 0;}
    try { Preview preview(parse(argc,argv)); return preview.run(); }
    catch (const std::exception &error) { std::fprintf(stderr,"water_preview: %s\n",error.what()); return 1; }
}
