// Compatibility interface for the isolated preview's shared SDL smoke flow.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "Platform/SdlHazardCloudFlow.h"

namespace CloudPreviewFlow {

// World coordinates and world units per second. The caller supplies the current
// velocity, not a displacement since the last rendered frame. Body order need
// not be stable. Z and height retain the scene's body geometry; flow is planar.
using Body = SdlHazardCloudFlow::Body;

// Covers the texture edges, with cells sampled at their centres. Changing the
// domain resets all flow. reset() preserves the domain and clears its history.
void setDomain(float minX, float minY, float maxX, float maxY);
void reset();

// Uses fixed 1/60-second steps and a capped catch-up interval. Empty input heals
// the existing wake. A zero delta freezes it. Invalid bodies are ignored and
// reported by valid(); velocities too large for infantry are treated as warps.
void step(float deltaSeconds, const Body *bodies, unsigned count);

unsigned width();
unsigned height();
float domainMinX();
float domainMinY();
float domainMaxX();
float domainMaxY();

// Row-major RGBA32F, X varies fastest. R/G are accumulated world displacement;
// sample the smoke noise at world.xy - RG. B/A are reserved and always zero:
// this field moves smoke without carving body-shaped holes or density rings.
// Shader applies the height falloff. Data stays valid until the next
// step/reset/setDomain. Displacement magnitude is bounded to 18 units.
const float *data();

// Cheap diagnostics for the preview's automated checks; not gameplay state.
bool valid();
float maxDisturbance(); // Maximum displacement magnitude divided by 18 units.
unsigned activeBodies();

} // namespace CloudPreviewFlow
