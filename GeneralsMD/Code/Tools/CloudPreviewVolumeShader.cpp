// Shared shader compatibility entry points for the isolated preview.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewVolumeShader.h"
std::string CloudPreviewVolumeVertexShader() { return SdlHazardCloudVertexShader(); }
std::string CloudPreviewVolumePixelShader(bool composite) { return SdlHazardCloudPixelShader(composite); }
std::string CloudPreviewVolumeCacheShader(bool lighting) { return SdlHazardCloudCacheShader(lighting); }
std::string CloudPreviewVolumeHeatShader() { return SdlHazardCloudHeatShader(); }
