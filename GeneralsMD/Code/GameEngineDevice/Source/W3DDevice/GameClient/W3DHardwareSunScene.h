// Bounded static-building snapshot for the native SDL Vulkan shared sun-depth pass.
#ifndef W3D_HARDWARE_SUN_SCENE_H
#define W3D_HARDWARE_SUN_SCENE_H
#include <cstdint>
#include <cstddef>
class SceneClass;
class RenderObjClass;
#if defined(__linux__)
// roots are the current enabled, non-invisible mapped caster list, before camera culling.
// The caller holds the live-model lock from gathering this list through caster selection.
void W3DHardwareSunPrepare(SceneClass *scene, RenderObjClass *const *roots, std::size_t count);
// Render-thread snapshot membership, not live-root revalidation. Raster ownership also
// requires a backend depth commit into the current sun attachment and a live-model lock
// spanning preparation through caster selection (or exact live revalidation by the caller).
// A failed/unavailable preparation publishes no proof.
uint64_t W3DHardwareSunSnapshotId();
std::size_t W3DHardwareSunCompleteCasterCount(uint64_t snapshotId);
bool W3DHardwareSunHasCompleteCaster(const RenderObjClass *root, uint64_t snapshotId);
#endif
#endif
