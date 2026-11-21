#include "test_harness.h"
#include "ffvertex.h"
#include "engineshader.h"
#include "sdl3target.h"
#include "sdl3shadercompile.h"
#include "Platform/WaterFftSampling.h"
#include "Platform/WaterHullVisual.h"
#include <cstdio>
TEST(heading_vertex_admission_and_production_pixel_stages_compile) {
    for(bool heading:{false,true}) {
        VertexPipelineDescription description={};description.FVF=0x252;description.StageCount=4;
        for(unsigned i=0;i<4;++i)description.Stages[i].TextureCoordinateIndex=i<2?i:0;
        std::string vertex,pixel;PixelPipelineDescription pipeline={};
        CHECK(VertexShader_Generate(description,VERTEX_SHADER_TARGET_D3D11,vertex));
        CHECK(WaterFftSampling::WrapVertex(vertex,false,false,false,heading));CHECK(SDL3_Shader_Retarget(vertex,true));
        CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,pipeline,pixel,COMBINER_SHADER_TARGET_SDL3_GPU));
        CHECK((vertex.find("FftHeadingTexture")!=std::string::npos)==heading);
        // The fork production pixel always carries the draw-selected map-aware path.
        CHECK((pixel.find("FftHeadingTexture")!=std::string::npos)==WaterFftSampling::Enabled());
        for(bool isVertex:{true,false}) {
            std::vector<unsigned char> spirv;std::string log,msl;
            const bool compiled=SDL3_Compile_HLSL_To_SPIRV(isVertex?vertex:pixel,isVertex,spirv,log);
            if(!compiled)std::printf("heading=%d vertex=%d: %s\n",heading,isVertex,log.c_str());
            CHECK(compiled);if(!compiled)continue;
            unsigned samplers=0,buffers=0;SDL3_Shader_Slots(spirv,isVertex,samplers,buffers);
            // SDL needs the highest occupied binding plus one, including holes.
            // Unheaded vertices end at the cage's t3; headed Clean vertices also
            // consume depth t5 and rotation t6 (ordinary headed FFT ends at t4).
            const bool cleanDepth=WaterCleanSpectrum::Enabled() && !WaterCleanProfile::Get().nodepth;
            const unsigned vertexSamplers=heading?(cleanDepth?7u:5u):4u;
            // The production pixel packs the six FFT maps into t8 and reserves
            // through t14. Only the optional coastal-surf history adds t15.
            const char *surf=std::getenv("ZH_WATER_SURF_GPU");
            const unsigned pixelSamplers=surf && std::strcmp(surf,"1")==0?16u:15u;
            const unsigned expectedSamplers=isVertex?vertexSamplers:pixelSamplers;
            if(samplers!=expectedSamplers)
                std::printf("heading=%d vertex=%d: sampler span %u, expected %u\n",heading,isVertex,samplers,expectedSamplers);
            CHECK_EQ(samplers,expectedSamplers);
            CHECK_EQ(buffers,isVertex?2u:(WaterHullVisual::Enabled()?4u:1u));
            CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,isVertex,msl,log));
        }
    }
}
