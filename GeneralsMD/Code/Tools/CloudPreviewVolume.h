// Preview scene adapter for the shared production hazard-cloud renderer.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "CloudPreviewFlow.h"
#include "Platform/SdlHazardCloud.h"
#include "Lib/BaseType.h"

namespace CloudPreviewVolume {

using FieldState = SdlHazardCloud::FieldState;

// Positions are ground-level centres. Requires a loaded W3D scene and SDL GPU.
bool initialize(const Coord3D &toxin, const Coord3D &radiation);
void setEnabled(bool enabled);
// Copies up to two live fields without reallocating/resetting the flow domain.
// Zero radius/height/density and an empty list represent an inactive source.
void setFields(const FieldState *fields, unsigned count);
// Restart wakes and source animation when replaying/reconfiguring a scenario.
void resetFlow();
// World-space full height, normally measured from the preview infantry model.
void setInfantryHeight(float height);
// At most four preview infantry; values are copied until the next update.
void setBodies(const CloudPreviewFlow::Body *bodies, unsigned count);
void setInteractionEnabled(bool enabled);
bool isInteractionEnabled();
// Heat distortion applies only to nuclear fields in the volumetric preview.
void setHeatEnabled(bool enabled);
bool isHeatEnabled();
void update(float timeSeconds);
unsigned activeBodies();
float maxDisturbance();
unsigned interactionUploads();
bool validationPassed();
// Release before the game client, scene and graphics device are destroyed.
void shutdown();

}
