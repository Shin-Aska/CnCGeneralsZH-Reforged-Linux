// Geometry refresh policy for the optional water path tracer.
#pragma once
#include <cstdint>
#include <optional>

namespace WaterPathTracing {

// Software refreshes its scene at most every 100 ms with a stationary view.
// Hardware follows each new logic frame read by the renderer. View or backend
// changes refresh immediately. The GPU can accumulate samples between exports.
class RefreshSchedule
{
public:
    static constexpr uint64_t IntervalMs = 100;

    bool ShouldRefresh(uint64_t nowMs, bool viewChanged = false,
        std::optional<uint32_t> hardwareLogicFrame = std::nullopt)
    {
        if(viewChanged || (observedClock && nowMs<lastObservedMs)) forced=true;
        lastObservedMs=nowMs;
        observedClock=true;
        if (!initialized || forced || nowMs < lastRefreshMs)
            return true;
        return hardwareLogicFrame ? hardwareLogicFrame != lastHardwareLogicFrame
                                  : nowMs - lastRefreshMs >= IntervalMs;
    }

    // Commit only after export succeeds. A failed export must not make an old
    // snapshot appear current or suppress the next attempt.
    void MarkRefreshed(uint64_t nowMs, std::optional<uint32_t> hardwareLogicFrame = std::nullopt)
    {
        lastRefreshMs=nowMs;
        lastObservedMs=nowMs;
        lastHardwareLogicFrame=hardwareLogicFrame;
        initialized=true;
        observedClock=true;
        forced=false;
    }

    void Reset()
    {
        initialized=false;
        observedClock=forced=false;
        lastRefreshMs=lastObservedMs=0;
        lastHardwareLogicFrame.reset();
    }

private:
    uint64_t lastRefreshMs=0;
    uint64_t lastObservedMs=0;
    std::optional<uint32_t> lastHardwareLogicFrame;
    bool initialized=false,observedClock=false,forced=false;
};

}
