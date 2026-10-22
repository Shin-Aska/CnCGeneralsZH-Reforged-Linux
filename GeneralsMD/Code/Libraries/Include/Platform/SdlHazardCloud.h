// SDL GPU implementation of the backend-neutral hazard cloud renderer.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Platform/HazardCloud.h"

namespace SdlHazardCloud {

using HazardCloud::FieldState;
using HazardCloud::Body;
using HazardCloud::Color;
using HazardCloud::Palette;
using HazardCloud::TerrainGridSide;
using HazardCloud::defaultPalette;

class Layer final : public HazardCloud::Layer {
public:
    Layer();
    ~Layer() override;
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

    bool initialize() override;
    void setFields(const FieldState *fields, unsigned count) override;
    void setBodies(const Body *bodies, unsigned count) override;
    void setFlowDomain(float minX, float minY, float maxX, float maxY) override;
    void setTerrain(unsigned fieldIndex, const float *heights) override;
    void setPalette(unsigned index, const Palette &palette) override;
    void setInteractionEnabled(bool enabled) override;
    bool isInteractionEnabled() const override;
    void setHeatEnabled(bool enabled) override;
    bool isHeatEnabled() const override;
    void update(float timeSeconds) override;
    void reset() override;
    bool render() override;
    bool valid() const override;
    unsigned activeBodies() const override;
    float maxDisturbance() const override;
    unsigned interactionUploads() const override;
    unsigned renderedFrames() const override;
    std::uint64_t estimatedCacheBytes() const override;
    void shutdown() override;

private:
    struct Impl;
    Impl *impl;
};

} // namespace SdlHazardCloud
