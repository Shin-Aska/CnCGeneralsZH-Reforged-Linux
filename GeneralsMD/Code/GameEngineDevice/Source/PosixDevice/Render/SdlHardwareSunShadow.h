// Vulkan static-building ray scene. Supports shared sun-depth and the diagnostic visibility mask.
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include "SdlSunDepthTiles.h"
#include <string>
struct SDL_GPUTexture;
struct SDL_GPUFence;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct ZHR_SDLRayScene;
class SdlGpuFrame;
struct SdlTarget;
class SdlHardwareSunShadow
{
public:
    static SdlHardwareSunShadow *Create(SdlGpuFrame &frame, const float *positions,
        uint32_t vertexCount, const uint32_t *indices, uint32_t indexCount, std::string &error,
        const std::vector<uint32_t> &meshTriangleEnds = {}, const std::vector<uint32_t> &buildingTriangleEnds = {});
    ~SdlHardwareSunShadow();
    bool UpdateGeometry(const float *positions, uint32_t vertexCount, const uint32_t *indices,
        uint32_t indexCount, std::string &error, const std::vector<uint32_t> &meshTriangleEnds = {},
        const std::vector<uint32_t> &buildingTriangleEnds = {});
    // Matrix is column-major inverse D3D view-projection; light points toward the sun.
    bool Render(const SdlTarget &source, const float *inverseViewProjection, const float *lightDirection,
        float originBias, float maxDistance, std::string &error);
    // Prepare all composite resources before the caller commits exclusive scene ownership.
    bool PrepareComposite(const SdlTarget &target, std::string &error);
    bool RenderResolved(const SdlTarget &source, SDL_GPUTexture *resolvedDepth,
        const float *inverseViewProjection, const float *lightDirection,
        float originBias, float maxDistance, std::string &error);
    bool Composite(const SdlTarget &target, float strength, std::string &error, const int32_t *scissor = nullptr);
    // Orthographic D3D sun clip reconstruction. Produces nearest depth (1 on miss).
    bool RenderSunDepth(const SdlTarget &target, const float *inverseSunViewProjection, std::string &error);
    // Min-merges query depth into the existing mapped attachment, preserving all other casters.
    // Default success includes submission. submit=false atomically queues the merge;
    // its owner must verify the later capture submission before exposing any receivers.
    bool MergeSunDepth(const SdlTarget &target, std::string &error, bool submit = true);
    // True once the AS/query submission and output allocations exist for this size.
    bool ReadyFor(const SdlTarget &target) const;
    // Legacy Render/RenderResolved visibility; sun mode retains an internal guarded cache.
    SDL_GPUTexture *Mask() const { return Visibility; }
    const std::string &Diagnostics() const { return DeviceDiagnostics; }
private:
    explicit SdlHardwareSunShadow(SdlGpuFrame &frame) : Frame(frame) {}
    SdlGpuFrame &Frame;
    ZHR_SDLRayScene *Scene = nullptr;
    bool BuildingScene = false;
    SDL_GPUFence *InFlight = nullptr;
    SDL_GPUTexture *Depth = nullptr, *Visibility = nullptr;
    unsigned Width = 0, Height = 0;
    bool SubmissionFailed = false, SceneSubmitted = false;
    std::string DeviceDiagnostics;
    SDL_GPUShader *CompositeVertex = nullptr, *CompositePixel = nullptr;
    SDL_GPUGraphicsPipeline *CompositePipeline = nullptr;
    SDL_GPUSampler *CompositeSampler = nullptr;
    unsigned CompositeFormat = 0;
    SDL_GPUShader *DepthMergePixel = nullptr;
    SDL_GPUGraphicsPipeline *DepthMergePipeline = nullptr;
    unsigned DepthMergeFormat = 0;
    bool DepthOutput = false, DepthIsSunDummy = false;
    bool SunMergeQueued = false;
    unsigned SunMergeQueuedFlush = 0;
    bool SubmitQueuedSunMerge(std::string &error);
    bool WaitForQuery(std::string &error);
    void BuildGeometryBounds(const float *positions, const uint32_t *indices, uint32_t indexCount,
        const std::vector<uint32_t> &meshTriangleEnds);
    using Rectangle = std::array<uint32_t,4>;
    static_assert(sizeof(Rectangle)==4*sizeof(uint32_t),"sun rectangle dispatch ABI");
    using Bounds = std::array<float,6>;
    Bounds SceneBounds = {};
    std::vector<Bounds> GeometryBounds;
    std::vector<Rectangle> SunCacheRectangles, SunMergeRectangles;
    SdlSunDepthTiles SunTiles;
    float SunDepthInverse[16] = {}, SunSourceInverse[16] = {};
    unsigned SunTargetWidth = 0, SunTargetHeight = 0, SunSourceWidth = 0, SunSourceHeight = 0;
    double SunSampleOffset[2] = {}, SunSampleStride[2] = {1,1};
    float SunDepthScale = 1, SunDepthBias = 0;
    bool SunCacheValid = false, SunCacheZContained = false;
    uint64_t SunTraceTexels = 0, SunMergeTexels = 0, SunTraces = 0, SunReplays = 0;
    uint64_t SunFills = 0, SunFillTexels = 0;
    uint64_t SunRejects[6] = {}, SunGuardLimited = 0, SunLastTraceTexels = 0, SunLastTargetTexels = 0;
    unsigned SunLastReject = ~0u;
    std::vector<std::array<float,9>> GeometryTriangles;
    void ReportSunCache() const;
    bool RenderInternal(const SdlTarget &source, SDL_GPUTexture *resolvedDepth,
        const float *inverseViewProjection, const float *lightDirection,
        float originBias, float maxDistance, std::string &error, bool sunDepth = false,
        const std::vector<Rectangle> *rectangles = nullptr);
    SdlHardwareSunShadow(const SdlHardwareSunShadow &) = delete;
    SdlHardwareSunShadow &operator=(const SdlHardwareSunShadow &) = delete;
};
