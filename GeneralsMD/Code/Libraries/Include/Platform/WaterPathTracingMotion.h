// Capture resolution recovery for the optional water path tracer.
#pragma once
#include <cstdint>

namespace WaterPathTracing {

class MotionRecovery
{
public:
    // Longer than the game's 100 ms scene refresh interval: repeated render
    // frames of one held snapshot cannot establish that moving geometry stopped.
    static constexpr uint64_t QuietMs = 150;
    static constexpr unsigned QuietFrames = 8;

    bool Observe(uint64_t nowMs,bool changed)
    {
        if(changed || (observedClock && nowMs<lastObservedMs)) {
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
