// Entry points for builds without a hazard cloud renderer backend.
// Replace this implementation when adding a Direct3D 11 backend.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "Platform/HazardCloud.h"

namespace HazardCloud {

bool Supported() { return false; }
std::unique_ptr<Layer> CreateLayer() { return nullptr; }

} // namespace HazardCloud
