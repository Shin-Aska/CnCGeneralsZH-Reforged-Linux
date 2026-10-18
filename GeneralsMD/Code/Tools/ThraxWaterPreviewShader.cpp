// Archived initial material experiment; not compiled by water_preview or the game.
// The maintained tool uses Platform/ModernWaterMaterial.h and the production shader.
// Build a private copy of the production generator into the standalone proposal.
// The preview links the device archive with this generator instead of engineshader.
// The game target continues to use the ordinary engineshader archive, unchanged.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#define EngineShader_Pixel_Program EngineShader_Preview_BasePixel
#include "../Libraries/Source/WWVegas/WW3D2/engineshader.cpp"
#undef EngineShader_Pixel_Program
#include "ThraxWaterPollution.h"

bool EngineShader_Pixel_Program(EngineShaderProgram program,
    const PixelPipelineDescription &pipeline,std::string &hlsl,CombinerShaderTarget target) {
    if(!EngineShader_Preview_BasePixel(program,pipeline,hlsl,target)) return false;
    if(target==COMBINER_SHADER_TARGET_SDL3_GPU &&
        (program==ENGINE_SHADER_WATER_TRAPEZOID || program==ENGINE_SHADER_WATER_RIVER))
        return ThraxWaterPollution::Apply(hlsl);
    return true;
}
