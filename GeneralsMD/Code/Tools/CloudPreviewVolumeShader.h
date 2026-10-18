// Compatibility names for the preview; shader implementation is shared.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include "../GameEngineDevice/Source/PosixDevice/Render/SdlHazardCloudShader.h"
using CloudPreviewVolumeUniforms = SdlHazardCloudUniforms;
std::string CloudPreviewVolumeVertexShader();
std::string CloudPreviewVolumePixelShader(bool composite = false);
std::string CloudPreviewVolumeCacheShader(bool lighting);
std::string CloudPreviewVolumeHeatShader();
