// Isolated preview scene adapter; material and simulation code are shared.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "CloudPreviewVolume.h"
#include "CloudPreviewColors.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/shader.h"
#include "WW3D2/ww3d.h"
#include "WWMath/aabox.h"
#include "WWMath/sphere.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CloudPreviewVolume {
namespace {
constexpr float MaximumDisplacement = 18.0f;
SdlHazardCloud::Layer layer;
FieldState fieldStates[2] = {};
unsigned fieldCount = 0;
bool enabled = false, everEnabled = false, failed = false;

void renderVolume()
{
    for (unsigned i = 0; i < 4; ++i) {
        const auto &p = CloudPreviewColors::get(i);
        layer.setPalette(i, {{p.main.r, p.main.g, p.main.b},
            {p.shadow.r, p.shadow.g, p.shadow.b}, p.brightness, p.transparency});
    }
    layer.render();
}

class VolumeRenderObject : public RenderObjClass
{
public:
    VolumeRenderObject() { Set_Force_Visible(true); }
    RenderObjClass *Clone() const override { return new VolumeRenderObject; }
    int Class_ID() const override { return CLASSID_LAST + 47; }
    const char *Get_Name() const override { return "CloudPreviewVolume"; }
    void fieldsChanged() { Invalidate_Cached_Bounding_Volumes(); }
    void Get_Obj_Space_Bounding_Box(AABoxClass &box) const override
    {
        if (!fieldCount) {
            box.Center.Set(0, 0, 0);
            box.Extent.Set(1, 1, 1);
            return;
        }
        Vector3 low, high;
        for (unsigned i = 0; i < fieldCount; ++i) {
            const FieldState &field = fieldStates[i];
            const float radius = field.radius + field.fringe + MaximumDisplacement;
            const Vector3 fieldLow(field.ground.x - radius, field.ground.y - radius, field.ground.z);
            const float visibleHeight = field.height
                * (field.palette >= 0.5f && field.palette < 1.5f ? 2.9f : 1.0f);
            const Vector3 fieldHigh(field.ground.x + radius, field.ground.y + radius,
                field.ground.z + visibleHeight);
            if (!i) { low = fieldLow; high = fieldHigh; }
            else for (unsigned axis = 0; axis < 3; ++axis) {
                low[axis] = std::min(low[axis], fieldLow[axis]);
                high[axis] = std::max(high[axis], fieldHigh[axis]);
            }
        }
        box.Center = (low + high) * 0.5f;
        box.Extent = (high - low) * 0.5f;
    }
    void Get_Obj_Space_Bounding_Sphere(SphereClass &sphere) const override
    {
        AABoxClass box;
        Get_Obj_Space_Bounding_Box(box);
        sphere.Center = box.Center;
        sphere.Radius = box.Extent.Length();
    }
    void Render(RenderInfoClass &) override
    {
        if (!enabled || failed || !W3DDisplay::m_3DScene) return;
        if (ShaderClass::Is_Backface_Culling_Inverted()
            || W3DDisplay::m_3DScene->getCustomPassMode() != SCENE_PASS_DEFAULT) return;
        if (WW3D::Are_Static_Sort_Lists_Enabled()) {
            // The scene flushes opaque terrain/NPC geometry before static lists.
            // Level 1 is their last bin, keeping the capture after other statics.
            WW3D::Add_To_Static_Sort_List(this, 1);
            return;
        }
        renderVolume();
    }
};
VolumeRenderObject *renderObject = nullptr;

} // namespace

bool initialize(const Coord3D &toxin, const Coord3D &radiation)
{
    if (renderObject) return validationPassed();
    failed = false;
    enabled = everEnabled = false;
    if (!W3DDisplay::m_3DScene || !layer.initialize()) return false;
    layer.setInteractionEnabled(true);
    layer.setHeatEnabled(true);
    const float centreX = (toxin.x + radiation.x) * 0.5f;
    const float centreY = (toxin.y + radiation.y) * 0.5f;
    layer.setFlowDomain(centreX - 400.0f, centreY - 220.0f, centreX + 400.0f, centreY + 220.0f);
    const FieldState initial[] = {
        {toxin, 115.0f, 13.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f},
        {radiation, 115.0f, 13.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f}
    };
    setFields(initial, 2);
    layer.reset();
    if (!layer.valid()) { layer.shutdown(); return false; }
    renderObject = new VolumeRenderObject;
    W3DDisplay::m_3DScene->Add_Render_Object(renderObject);
    std::fprintf(stderr, "cloud-preview: shared volumetric renderer initialized (no particle sprites)\n");
    return true;
}
void setEnabled(bool value) { enabled = value; everEnabled = everEnabled || value; }
void setFields(const FieldState *fields, unsigned count)
{
    layer.setFields(fields, count);
    if (count > 2 || (count && !fields)) return;
    fieldCount = count;
    for (unsigned i = 0; i < count; ++i) fieldStates[i] = fields[i];
    if (renderObject) renderObject->fieldsChanged();
}
void setInfantryHeight(float height)
{
    if (!std::isfinite(height) || height <= 0) { failed = true; return; }
    for (unsigned i = 0; i < fieldCount; ++i) fieldStates[i].height = height;
    layer.setFields(fieldStates, fieldCount);
    if (renderObject) renderObject->fieldsChanged();
}
void resetFlow() { layer.reset(); }
void setBodies(const CloudPreviewFlow::Body *bodies, unsigned count) { layer.setBodies(bodies, count); }
void setInteractionEnabled(bool value) { layer.setInteractionEnabled(value); }
bool isInteractionEnabled() { return layer.isInteractionEnabled(); }
void setHeatEnabled(bool value) { layer.setHeatEnabled(value); }
bool isHeatEnabled() { return layer.isHeatEnabled(); }
void update(float timeSeconds) { layer.update(timeSeconds); }
unsigned activeBodies() { return layer.activeBodies(); }
float maxDisturbance() { return layer.maxDisturbance(); }
unsigned interactionUploads() { return layer.interactionUploads(); }
bool validationPassed()
{
    return renderObject && !failed && layer.valid() && (!everEnabled || layer.renderedFrames() > 0);
}
void shutdown()
{
    enabled = false;
    if (renderObject) {
        renderObject->Remove();
        renderObject->Release_Ref();
        renderObject = nullptr;
    }
    layer.shutdown();
}
} // namespace CloudPreviewVolume
