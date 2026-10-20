// Scene export for the optional water path tracer. It does not change scene visibility.
#pragma once

#if defined(__linux__)
#include "Platform/WaterPathTracingScene.h"
#include <memory>
#include <vector>

class SceneClass;
class RenderObjClass;
class BaseHeightMapRenderObjClass;
namespace WaterPathTracing { struct View; }

class W3DWaterPathScene
{
public:
    W3DWaterPathScene();
    ~W3DWaterPathScene();
    bool Update(SceneClass *scene, BaseHeightMapRenderObjClass *terrain, RenderObjClass *water,
        const WaterPathTracing::View *view=nullptr);
    const std::vector<WaterPathTracing::Triangle> &Triangles() const;
    uint64_t Revision() const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
#endif
