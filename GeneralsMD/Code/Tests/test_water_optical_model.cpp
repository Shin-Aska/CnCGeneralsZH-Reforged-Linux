#include "test_harness.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif
// Exercise the Windows header environment on POSIX compilers too.
#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif
#ifndef near
#define near
#endif

#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterShoreSource.h"
#include "Platform/WaterBedMaterial.h"

TEST(water_optical_model_accepts_windows_min_max_macros)
{
    CHECK_EQ(min(2,3),2);
    CHECK_EQ(max(2,3),3);
    CHECK_EQ(WaterOpticalModel::Unit(-1.0f),0.0f);
    CHECK_EQ(WaterOpticalModel::Unit(0.5f),0.5f);
    CHECK_EQ(WaterOpticalModel::Unit(2.0f),1.0f);
    CHECK_EQ(WaterOpticalModel::Path((std::numeric_limits<float>::infinity)()),
        WaterOpticalModel::MaximumFiniteInput);
    CHECK_EQ(WaterOpticalModel::Unit((std::numeric_limits<float>::quiet_NaN)()),0.0f);
    CHECK_EQ(WaterOpticalModel::ReferenceDepth(-1.0f),
        WaterOpticalModel::MinimumReferenceDepth);
    CHECK_NEAR(WaterOpticalModel::RayShift(5.0f,1.0f,1.0f,10.0f,2.0f,3.0f),
        2.92f,0.00001f);
    CHECK_NEAR(WaterOpticalModel::SourceOffshore(0.75f,1.0f),0.25f,0.00001f);
    const WaterOpticalModel::RGB transmission=
        WaterOpticalModel::Transmission(0.0f,0.0f,0.0f,1.0f,0.0f);
    CHECK_EQ(transmission.r,1.0f);
    CHECK_EQ(transmission.g,1.0f);
    CHECK_EQ(transmission.b,1.0f);
    CHECK(strstr(WaterOpticalModel::GetHlslSource(),"WaterOpticalTransmission") != 0);
}

TEST(water_shore_source_accepts_windows_near_macro)
{
    WaterShoreSource::Batch batch;
    CHECK_EQ(batch.Build(nullptr,0,0,0,10.0,1.0,{},{}),
        WaterShoreHistory::BuildResult::Invalid);
    CHECK(batch.Cells().empty());
    CHECK(WaterShoreSource::Covered({{0.0,2.0}},0.5,1.5));
}

TEST(water_bed_material_accepts_windows_min_max_macros)
{
    CHECK_EQ(WaterBedMaterial::Saturate(-1.0f),0.0f);
    CHECK_EQ(WaterBedMaterial::Saturate(0.5f),0.5f);
    CHECK_EQ(WaterBedMaterial::Saturate(2.0f),1.0f);
    CHECK_EQ(WaterBedMaterial::Texel(0,0,32),WaterBedMaterial::Texel(32,32,32));
}
