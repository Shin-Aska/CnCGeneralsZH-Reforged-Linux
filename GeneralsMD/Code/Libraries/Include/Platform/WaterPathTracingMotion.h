// Capture resolution recovery for the optional water path tracer.
#pragma once
#include "Platform/WaterPathTracingScene.h"
#include <cstdint>

namespace WaterPathTracing {

// Resolution follows the capture camera, independently of history invalidation.
// Geometry, lighting and transport changes still invalidate accumulated samples,
// but do not make a stationary camera less capable of resolving reflected detail.
inline bool SameCaptureProjection(const Uniforms &a,const Uniforms &b)
{
    return std::memcmp(&a.originWater,&b.originWater,4*sizeof(Float4))==0
        && std::memcmp(&a.output,&b.output,2*sizeof(float))==0
        && a.output.w==b.output.w;
}

class MotionRecovery
{
public:
    // Camera updates may arrive in held snapshots. Require both time and render
    // observations before restoring the requested capture density.
    static constexpr uint64_t QuietMs = 150;
    static constexpr unsigned QuietFrames = 8;

    bool Observe(uint64_t nowMs,bool cameraChanged)
    {
        if(cameraChanged || (observedClock && nowMs<lastObservedMs)) {
            reduced=true;
            unchangedFrames=0;
            lastChangeMs=nowMs;
        } else if(reduced) {
            if(unchangedFrames<QuietFrames) ++unchangedFrames;
            if(unchangedFrames>=QuietFrames && nowMs-lastChangeMs>=QuietMs)
                reduced=false;
        }
        lastObservedMs=nowMs;
        observedClock=true;
        return reduced;
    }

private:
    uint64_t lastChangeMs=0,lastObservedMs=0;
    unsigned unchangedFrames=0;
    bool reduced=false,observedClock=false;
};

}
