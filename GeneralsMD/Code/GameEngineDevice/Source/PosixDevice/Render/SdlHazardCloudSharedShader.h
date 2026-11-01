#pragma once
#include <string>
namespace HazardCloudShared {
enum class ShaderPass { Inject, Light, Volume, Composite, Heat };
std::string sharedPixelShader(ShaderPass pass);
}
