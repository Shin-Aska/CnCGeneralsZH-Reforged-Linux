// Optional water-only software path tracing. No scene or shadow setting is changed.
#pragma once
#include "Platform/WaterPathTracingScene.h"
#include "Platform/WaterPathTracingMaterials.h"
#include <string>

struct IDirect3DDevice9;
namespace WaterPathTracing {

// Called only for the selected experimental mode. Geometry is copied/built only
// when its fingerprint changes. An unchanged view accumulates up to 64 samples.
// False means no usable capture: the caller should draw its planar fallback.
// A nonzero revision must change whenever triangle data or the optional material
// packet changes. Zero asks the backend to hash both; cached exporters can avoid that.
bool Prepare(IDirect3DDevice9 *device, const std::vector<Triangle> &triangles, const View &view,
    uint64_t revision = 0,const PrimaryMaterialPacket *materials = nullptr);
void Clear(IDirect3DDevice9 *device);
std::string Report(IDirect3DDevice9 *device);
}
