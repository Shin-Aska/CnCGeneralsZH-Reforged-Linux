// SDL shared-volume hazard clouds with bounded incremental snapshot publication.
#pragma once
#include "Platform/HazardCloud.h"
#include <cstdint>
#include <memory>
#include <string>
class SdlGpuFrame;
namespace HazardCloudShared {
class Model;
struct RendererConfig {
    unsigned volumeSide = 256;
    unsigned slices = 24;
    float binCellSize = 128.0f;
    float bakesPerSecond = 20.0f;
    // Bound injection work on every display render, including between logic
    // updates. A complete material snapshot is lit and published atomically.
    unsigned bakeSlicesPerFrame = 8;
    bool incrementalBaking = true;
    bool heat = true;
    bool embers = true;
};
struct RendererStats {
    unsigned sources = 0, activeSources = 0, maximumBinSources = 0;
    unsigned activeBodies = 0, interactionUploads = 0;
    float maxDisturbance = 0;
    unsigned bakes = 0, frames = 0;
    // Active sources in the visible snapshot, unfinished material slices, and
    // the largest injection allowance actually consumed since initialization.
    unsigned bakedSources = 0, pendingSlices = 0, maxSlicesPerFrame = 0;
    // Current simulation time minus the visible snapshot's captured time.
    float publicationAgeSeconds = 0;
    std::uint64_t volumeBytes = 0, metadataBytes = 0;
};
class Renderer {
public:
    Renderer();
    ~Renderer();
    bool initialize(std::string &error);
    bool initialize(SdlGpuFrame *frame, const RendererConfig &config, std::string &error);
    // False guarantees no cloud draw touched scene colour; classic fallback is safe.
    bool render(const HazardCloud::Camera &camera, const Model &model, float seconds, std::string &error);
    // Optional shared terrain grid covering the model's XY bounds. Values are
    // absolute world heights, copied with each snapshot and its XY bounds.
    // Only sources with terrain=true sample this grid. nullptr restores planes.
    void setTerrain(const float *heights, unsigned side);
    void setHeatEnabled(bool enabled);
    void setBodies(const HazardCloud::Body *bodies, unsigned count);
    void reset();
    void shutdown();
    const RendererStats &stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
