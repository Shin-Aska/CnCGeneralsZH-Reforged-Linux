/* Conservative, render-only admission for a flat water capture. */
#pragma once

#include "Platform/WaterTerrainCulling.h"

namespace WaterCaptureVisibility
{
struct Bounds { double minX, minY, maxX, maxY; };
struct Point3 { double x, y, z; };
// Inside is a*x + b*y + c*z + d >= 0. The engine's frustum planes use the opposite sign.
struct Plane { double a, b, c, d; };
enum class Result { Offscreen, Dry, PossibleWater, OutsideTerrain, UnknownSurface, CameraUnproven };
inline bool Hidden(Result result) { return result == Result::Offscreen || result == Result::Dry; }
inline const char *Name(Result result)
{
    switch (result) {
    case Result::Offscreen: return "offscreen";
    case Result::Dry: return "terrain-dry";
    case Result::PossibleWater: return "possible-water";
    case Result::OutsideTerrain: return "outside-terrain";
    case Result::CameraUnproven: return "camera-unproven";
    default: return "unknown-surface";
    }
}

// Rasterized rays begin at the near plane, rather than at the eye. Even an eye
// above terrain can have its hiding terrain clipped away. Prove the entire near
// rectangle starts above every live terrain cell corner below it. The rectangle
// and minimum corner Z are conservative for tilted perspective/orthographic planes.
inline bool NearPlaneAboveTerrain(const Point3 (&corners)[4], const uint8_t *heights,
    int width, int height, int border, double spacing, double heightScale)
{
    if (!heights || width < 2 || height < 2 || width > 2048 || height > 2048 ||
        border < 0 || border > (width - 2) / 2 || border > (height - 2) / 2 ||
        !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(heightScale) || heightScale <= 0)
        return false;
    Bounds bounds = {corners[0].x,corners[0].y,corners[0].x,corners[0].y};
    double minimumZ = corners[0].z, magnitude = 1;
    for (const auto &corner : corners) {
        if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z))
            return false;
        bounds.minX = (std::min)(bounds.minX,corner.x); bounds.maxX = (std::max)(bounds.maxX,corner.x);
        bounds.minY = (std::min)(bounds.minY,corner.y); bounds.maxY = (std::max)(bounds.maxY,corner.y);
        minimumZ = (std::min)(minimumZ,corner.z);
        magnitude = (std::max)(magnitude,(std::max)(std::abs(corner.x),
            (std::max)(std::abs(corner.y),std::abs(corner.z))));
    }
    const double epsilon = 16 * std::numeric_limits<float>::epsilon() *
        (std::max)(magnitude,255 * heightScale);
    const double minX = (bounds.minX-epsilon)/spacing+border, maxX = (bounds.maxX+epsilon)/spacing+border;
    const double minY = (bounds.minY-epsilon)/spacing+border, maxY = (bounds.maxY+epsilon)/spacing+border;
    if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY) ||
        minX < 0 || minY < 0 || maxX > width-1 || maxY > height-1)
        return false;
    const int firstX = int(std::floor(minX)), lastX = int(std::ceil(maxX));
    const int firstY = int(std::floor(minY)), lastY = int(std::ceil(maxY));
    // Unusually broad near planes retain captures rather than add unbounded frame work.
    if (size_t(lastX-firstX+1)*size_t(lastY-firstY+1) > 4096)
        return false;
    for (int y = firstY; y <= lastY; ++y)
        for (int x = firstX; x <= lastX; ++x)
            if (minimumZ <= heights[size_t(y)*width+x]*heightScale+epsilon)
                return false;
    return true;
}

// Clip each water area's support, rather than a map-wide/capture-sized ray footprint.
// An area's XY bounding rectangle is a superset even for concave authored polygons.
// Expand every frustum half-space for displacement before clipping the rest surface;
// expanding its surviving XY bounds then includes every displaced visible point.
inline Result Classify(Bounds bounds, double level, const Plane (&planes)[6],
    const uint8_t *heights, int width, int height, int border, double spacing, double heightScale,
    double vertical, double horizontal, WaterTerrainCulling::WetNodeCache *cache = nullptr,
    bool terrainOcclusionProven = false)
{
    if (!std::isfinite(bounds.minX) || !std::isfinite(bounds.minY) ||
        !std::isfinite(bounds.maxX) || !std::isfinite(bounds.maxY) ||
        bounds.minX > bounds.maxX || bounds.minY > bounds.maxY ||
        !heights || width < 2 || height < 2 || width > 2048 || height > 2048 ||
        border < 0 || border > (width - 2) / 2 || border > (height - 2) / 2 ||
        !std::isfinite(spacing) || spacing <= 0 || !std::isfinite(horizontal) || horizontal < 0)
        return Result::UnknownSurface;
    double ceiling;
    if (!WaterTerrainCulling::ProofCeiling(heightScale, level, vertical, ceiling))
        return Result::UnknownSurface;
    const double magnitude = (std::max)(1.0, (std::max)(std::abs(level), (std::max)(
        (std::max)(std::abs(bounds.minX), std::abs(bounds.maxX)),
        (std::max)(std::abs(bounds.minY), std::abs(bounds.maxY)))));
    const double epsilon = 16 * std::numeric_limits<float>::epsilon() * (magnitude + horizontal + vertical);
    WaterProjectedMesh::Polygon support;
    support.count = 4;
    support.points[0] = {bounds.minX, bounds.minY};
    support.points[1] = {bounds.maxX, bounds.minY};
    support.points[2] = {bounds.maxX, bounds.maxY};
    support.points[3] = {bounds.minX, bounds.maxY};
    for (const auto &plane : planes) {
        if (!std::isfinite(plane.a) || !std::isfinite(plane.b) ||
            !std::isfinite(plane.c) || !std::isfinite(plane.d))
            return Result::UnknownSurface;
        const double margin = std::abs(plane.a) * horizontal + std::abs(plane.b) * horizontal
            + std::abs(plane.c) * vertical
            + epsilon * (std::abs(plane.a) + std::abs(plane.b) + std::abs(plane.c));
        if (!WaterProjectedMesh::Clip(support, {plane.a, plane.b, plane.c * level + plane.d + margin}))
            return Result::UnknownSurface;
    }
    if (!support.count) return Result::Offscreen;
    // Empty frustum support is independent of terrain occlusion. Only a nonempty
    // area needs above-terrain eye AND near-plane origins before using a dry proof.
    if (!terrainOcclusionProven) return Result::CameraUnproven;
    bounds = {support.points[0].x, support.points[0].y, support.points[0].x, support.points[0].y};
    for (int i = 1; i < support.count; ++i) {
        bounds.minX = (std::min)(bounds.minX, support.points[i].x);
        bounds.minY = (std::min)(bounds.minY, support.points[i].y);
        bounds.maxX = (std::max)(bounds.maxX, support.points[i].x);
        bounds.maxY = (std::max)(bounds.maxY, support.points[i].y);
    }
    const double minX = (bounds.minX - horizontal - epsilon) / spacing + border;
    const double minY = (bounds.minY - horizontal - epsilon) / spacing + border;
    const double maxX = (bounds.maxX + horizontal + epsilon) / spacing + border;
    const double maxY = (bounds.maxY + horizontal + epsilon) / spacing + border;
    // Never infer dry terrain over skirt seas or any unsampled part of an authored area.
    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY))
        return Result::UnknownSurface;
    if (minX < 0 || minY < 0 || maxX > width - 1 || maxY > height - 1)
        return Result::OutsideTerrain;
    const int firstX = int(std::floor(minX)), firstY = int(std::floor(minY));
    const int lastX = int(std::ceil(maxX)), lastY = int(std::ceil(maxY));
    bool wet = false;
    if (cache) {
        auto query = cache->Prepare(heights, width, height, heightScale, ceiling);
        if (query.HasWet(firstX, lastX, firstY, lastY, wet))
            return wet ? Result::PossibleWater : Result::Dry;
    }
    // Live deformation uses the current bytes rather than an invalid immutable cache.
    for (int y = firstY; y <= lastY; ++y)
        for (int x = firstX; x <= lastX; ++x)
            if (heights[size_t(y) * width + x] * heightScale <= ceiling)
                return Result::PossibleWater;
    return Result::Dry;
}
}
