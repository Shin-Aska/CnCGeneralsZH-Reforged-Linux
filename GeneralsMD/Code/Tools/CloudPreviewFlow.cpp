// Compatibility singleton for the isolated cloud preview and its CPU checks.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewFlow.h"

namespace CloudPreviewFlow {
namespace {

SdlHazardCloudFlow::Flow &previewFlow()
{
    static SdlHazardCloudFlow::Flow flow;
    return flow;
}

} // namespace

void setDomain(float minX, float minY, float maxX, float maxY)
{
    previewFlow().setDomain(minX, minY, maxX, maxY);
}
void reset() { previewFlow().reset(); }
void step(float deltaSeconds, const Body *bodies, unsigned count)
{
    previewFlow().step(deltaSeconds, bodies, count);
}

unsigned width() { return previewFlow().width(); }
unsigned height() { return previewFlow().height(); }
float domainMinX() { return previewFlow().domainMinX(); }
float domainMinY() { return previewFlow().domainMinY(); }
float domainMaxX() { return previewFlow().domainMaxX(); }
float domainMaxY() { return previewFlow().domainMaxY(); }
const float *data() { return previewFlow().data(); }
bool valid() { return previewFlow().valid(); }
float maxDisturbance() { return previewFlow().maxDisturbance(); }
unsigned activeBodies() { return previewFlow().activeBodies(); }

} // namespace CloudPreviewFlow
