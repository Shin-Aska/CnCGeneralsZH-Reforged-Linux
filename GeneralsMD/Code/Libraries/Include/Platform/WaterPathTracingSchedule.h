// Geometry refresh policy for the optional water path tracer.
#pragma once
#include <cstdint>

namespace WaterPathTracing {

// A stationary capture reuses its scene snapshot for up to 100 ms, then refreshes
// on the next rendered frame. Moving objects can therefore appear one snapshot
// late. Camera, crop, water-plane and lighting changes must set viewChanged.
// This limits CPU export work only: the current snapshot may still accumulate
// samples every render frame. Its reported geometry revision must reset history
// whenever a refresh changes the exported triangles.
class RefreshSchedule
{
public:
    static constexpr uint64_t IntervalMs = 100;

    bool ShouldRefresh(uint64_t nowMs,bool viewChanged=false)
    {
        if(viewChanged || (observedClock && nowMs<lastObservedMs)) forced=true;
        lastObservedMs=nowMs;
        observedClock=true;
        return !initialized || forced || nowMs<lastRefreshMs
            || nowMs-lastRefreshMs>=IntervalMs;
    }

    // Commit only after export succeeds. A failed export must not make an old
    // snapshot appear current or suppress the next attempt.
    void MarkRefreshed(uint64_t nowMs)
    {
        lastRefreshMs=nowMs;
        lastObservedMs=nowMs;
        initialized=true;
        observedClock=true;
        forced=false;
    }

    void Reset()
    {
        initialized=false;
        observedClock=forced=false;
        lastRefreshMs=lastObservedMs=0;
    }

private:
    uint64_t lastRefreshMs=0;
    uint64_t lastObservedMs=0;
    bool initialized=false,observedClock=false,forced=false;
};

}
