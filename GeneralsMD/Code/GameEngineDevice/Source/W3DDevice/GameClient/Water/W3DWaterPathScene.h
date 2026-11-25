// Scene export for the optional water path tracer. It does not change scene visibility.
#pragma once

#if defined(__linux__)
#include "Platform/WaterPathTracingScene.h"
#include "Platform/WaterPathTracingMaterials.h"
#include <memory>
#include <vector>

class SceneClass;
class RenderObjClass;
class BaseHeightMapRenderObjClass;
namespace WaterPathTracing { struct View; }

class W3DWaterPathScene
{
public:
    enum class Capture { SoftwarePath, HardwarePath, ScopedHardwarePath, HardwareReflection };

    W3DWaterPathScene();
    ~W3DWaterPathScene();
    void takeReadsForRender(BaseHeightMapRenderObjClass *terrain);
    bool Update(SceneClass *scene, BaseHeightMapRenderObjClass *terrain, RenderObjClass *water,
        const WaterPathTracing::View *view=nullptr, Capture capture=Capture::SoftwarePath);
    const std::vector<WaterPathTracing::Triangle> &Triangles() const;
    const WaterPathTracing::PrimaryMaterialPacket &Materials() const;
    uint64_t Revision() const;
    // Source packet prefix, before the renderer filters degenerate triangles.
    size_t StaticTriangleCount() const;
    // The hardware path can retain the caller's request while using a scoped packet.
    Capture RequestedCapture() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
#endif
