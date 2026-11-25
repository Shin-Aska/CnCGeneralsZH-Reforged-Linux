#pragma once
// Experimental static-building sun shadows. CPU geometry is copied during PrepareScene.
#include <cstdint>
#include <string>
#include <vector>
struct IDirect3DDevice9;
namespace HardwareRaySun {
bool Requested();
bool Available(IDirect3DDevice9 *device);
// Main world view only, before opaque rendering. False retains mapped/legacy ownership.
// Positions are consecutive opaque two-sided world triangles. The exclusive mesh
// ends describe coverage bounds, and building ends describe reusable ray geometry.
bool PrepareScene(IDirect3DDevice9 *device, const float *positions, uint32_t vertexCount,
    uint64_t revision, const float towardsSun[3], float strength,
    const std::vector<uint32_t> &meshTriangleEnds, const std::vector<uint32_t> &buildingTriangleEnds);
// Atomically queue prepared depth into the active freshly cleared mapped sun
// attachment using its exact current transforms. True permits snapshot-qualified
// omissions only in this capture; End must verify submission before receivers.
// Refusal keeps every mapped caster. Standalone merges retain synchronous submission.
bool CommitMappedDepth(IDirect3DDevice9 *device);
// Map unload, device reset, or a collector that no longer has a valid static scene.
void Invalidate(IDirect3DDevice9 *device);
// Exact scene ownership remains latched through a late apply failure, avoiding duplicate shadows.
bool OwnsCurrentScene(IDirect3DDevice9 *device);
}

// Shared global opt-in. Probe tests the current SDL Vulkan device with a known hit and miss.
namespace HardwareRayTracing {
bool Requested();
void SetRequested(bool enabled);
// Changes only on explicit gate transitions, so resource refusals can retry once.
uint64_t RequestGeneration();
bool Probe(IDirect3DDevice9 *device, std::string &error);
bool Available(IDirect3DDevice9 *device);
// Game-facing overload resolves the active renderer; no adapter-name inference.
bool Probe(std::string &error);
void QueueUnavailableNotice();
bool TakeUnavailableNotice();
}
