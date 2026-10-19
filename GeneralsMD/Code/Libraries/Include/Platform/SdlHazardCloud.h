// Shared visual-only hazard cloud renderer for the Linux game and cloud preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Lib/BaseType.h"
#include "Platform/SdlHazardCloudFlow.h"
#include <cstdint>

namespace SdlHazardCloud {

constexpr unsigned TerrainGridSide = 129;

struct FieldState {
    Coord3D ground;
    float radius;
    float height;       // Full world-space height above the sampled ground.
    float density;
    float palette;      // 0 toxin, 1 radiation, 2 anthrax blue, 3 gamma purple.
    float formation;
    float release;
    float fringe;
};
using Body = SdlHazardCloudFlow::Body;
struct Color { float r, g, b; };
struct Palette {
    Color main, shadow;
    float brightness;
    // 0 retains full optical density; 1 makes only the smoke invisible.
    // Formation, footprint, wakes, heat and embers remain independent.
    float transparency = 0.0f;
};

// Stable user-approved palette, independent of preview files or game options.
const Palette &defaultPalette(unsigned index);

// Main render thread only. Owns up to two fields with independent density,
// lighting and infantry-wake history. Pipelines and transient scene targets are
// shared by all layers on the same device. No scene object is registered.
// Without a terrain grid, ground support is the field's flat ground/deck plane.
// The support model has one height per XY position. The production adapter uses
// terrain samples or an elevated plane; cliffs and crossing decks have no
// dedicated validation fixture or automatic geometry fallback yet.
class Layer {
public:
    Layer();
    ~Layer();
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    bool initialize();
    void setFields(const FieldState *fields, unsigned count);
    void setBodies(const Body *bodies, unsigned count); // At most 32.
    void setFlowDomain(float minX, float minY, float maxX, float maxY);
    // Copies TerrainGridSide squared world heights, row-major with increasing Y.
    // Grid nodes span ground.xy +/- (radius + fringe + 18 world units), matching
    // this field's cache domain. Refresh after changing that domain or terrain.
    // nullptr disables terrain sampling for this field and restores its plane.
    void setTerrain(unsigned fieldIndex, const float *heights);
    void setPalette(unsigned index, const Palette &palette);
    void setInteractionEnabled(bool enabled);
    bool isInteractionEnabled() const;
    void setHeatEnabled(bool enabled);
    bool isHeatEnabled() const;
    void update(float timeSeconds);
    void reset(); // Clears source/wake history, retaining fields and allocations.

    // Call only in the main scene after opaque geometry/water. False means no
    // cloud scene draw was recorded, so the caller may draw classic effects.
    bool render();
    bool valid() const;
    unsigned activeBodies() const;
    float maxDisturbance() const;
    unsigned interactionUploads() const;
    unsigned renderedFrames() const;

    // Density + lighting atlas allocations and local flow texture. Excludes
    // shared screen scratch targets; estimate before admitting a new layer.
    static std::uint64_t estimateCacheBytes(const FieldState *fields, unsigned count);
    std::uint64_t estimatedCacheBytes() const;

    // Release before the display/device is destroyed. Repeated calls are safe;
    // releasing one layer leaves all other layers' resources and history intact.
    void shutdown();

private:
    struct Impl;
    Impl *impl;
};

} // namespace SdlHazardCloud
