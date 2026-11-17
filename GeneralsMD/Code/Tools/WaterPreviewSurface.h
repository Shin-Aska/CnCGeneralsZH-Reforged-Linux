// Synthetic map inputs for the same main FFT water path used by the game.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include "Platform/WaterCleanPrecompute.h"
#include "Platform/WaterLocalWaveField.h"
#include "Platform/WaterOpticalContext.h"

namespace WaterPreviewSurface {
struct Field {
    static constexpr int Side=128, Border=32, Words=7;
    float spacing=0;
    WaterOpticalContext::Field optical;
    WaterMediumDepth::Field medium;
    WaterLocalWaveField::Field heading;
    WaterCleanPrecompute::Image depth;
    WaterCleanPrecompute::Rotation rotation;
    std::vector<uint32_t> packedHeading;

    bool build(float halfExtent,float level,const WaterTuning::Values &values,bool live=false,bool force=false)
    {
        if(!std::isfinite(halfExtent) || halfExtent<=0 || !std::isfinite(level) || level<=0)return false;
        spacing=halfExtent/Border;
        const float high=(Side-1-Border)*spacing;
        const WaterOpticalContext::Polygon polygon={{{-halfExtent,-halfExtent,level},
            {high,-halfExtent,level},{high,high,level},{-halfExtent,high,level}}};
        const std::vector<uint8_t> heights(Side*Side,0);
        if(!WaterOpticalContext::Build(heights.data(),Side,Side,Border,spacing,1,level,{polygon},optical)
            || !WaterMediumDepth::Build(heights.data(),optical.wet.data(),optical.shore.data(),
                Side,Side,spacing,1,level,medium)
            || !WaterCleanShore::Build(medium,optical.wet.data(),spacing))return false;
        const double radians=WaterTuning::EffectiveTravelAngle(values)*WaterFftSpectrum::Pi/180;
        const WaterWaveHeading::Vector travel={float(std::cos(radians)),float(std::sin(radians))};
        if(!WaterLocalWaveField::Build(optical.wet.data(),optical.exposure.data(),optical.links.data(),
                Side,Side,Border,spacing,heading,{},true,nullptr,values[WaterTuning::TravelMode]?&travel:nullptr)
            || !WaterCleanPrecompute::BuildDepth(medium,Side,depth))return false;
        const auto key=WaterCleanPrecompute::MakeKey(WaterCleanPrecompute::NextRevision(),values,force,live);
        if(!WaterCleanPrecompute::BuildRotation(medium,heading.texels,Side,Border,spacing,key,rotation))return false;
        packedHeading.resize(Side*Side*Words);
        for(size_t cell=0;cell<Side*Side;++cell) {
            for(unsigned word=0;word<WaterLocalWaveField::TexelsPerCell;++word)
                packedHeading[cell*Words+word]=heading.texels[cell*WaterLocalWaveField::TexelsPerCell+word];
            packedHeading[cell*Words+5]=WaterCleanShore::Word(medium,cell);
            packedHeading[cell*Words+6]=WaterCleanShore::DistanceWord(medium,cell);
        }
        return true;
    }
};
}
