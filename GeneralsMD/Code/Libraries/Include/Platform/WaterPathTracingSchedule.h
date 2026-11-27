// Geometry refresh policy for the optional water path tracer.
#pragma once
#include <cstdint>
#include <optional>

namespace WaterPathTracing {

// Pace the whole experimental capture (scene export, upload and tracing) by the
// rate at which the main view actually renders, including frames reusing water.
class CaptureCadence
{
public:
    // A held capture belongs to one scene and map. Observe before testing the
    // cooldown, since scene export has not had a chance to notice an owner swap.
    bool ObserveContext(const void *scene, const void *map)
    {
        if (scene == ownerScene && map == ownerMap) return false;
        ownerScene = scene;
        ownerMap = map;
        InvalidateCapture();
        return true;
    }

    bool BeginFrame(uint64_t nowMs)
    {
        if (!lastFrame || nowMs < *lastFrame || nowMs - *lastFrame > 5000) {
            *this = {};
            windowStart = nowMs;
            lastFrame = nowMs;
            return false;
        }
        // Don't let a 150 ms capture pay for 150 ms of cooldown. Its cost is
        // visible at the next frame boundary, including deferred GPU waits.
        if (capturedThisFrame && captureBudgetStart && nowMs - *captureBudgetStart > BaseIntervalMs)
            captureBudgetStart = nowMs - BaseIntervalMs;
        capturedThisFrame = false;
        lastFrame = nowMs;
        ++frames;
        const uint64_t elapsed = nowMs - windowStart;
        if (elapsed < 500) return false;

        measuredFps = 1000.0f * frames / elapsed;
        if (measuredFps <= 20.0f) {
            if (step < 6) ++step;
            healthyMs = 0;
        } else if (step > 1 && measuredFps >= 27.0f) {
            healthyMs += elapsed;
            if (healthyMs >= 2000) {
                --step;
                healthyMs = 0;
            }
        } else {
            healthyMs = 0;
        }
        windowStart = nowMs;
        frames = 0;
        lastWindowCaptures = captures;
        captures = 0;
        return true;
    }

    bool ShouldCapture() const
    {
        return !captureBudgetStart || *lastFrame - *captureBudgetStart >= IntervalMs();
    }

    void MarkCaptured()
    {
        captureBudgetStart = lastFrame;
        capturedThisFrame = true;
        ++captures;
    }

    void InvalidateCapture() { captureBudgetStart.reset(); }
    unsigned IntervalMs() const { return step * 100 / 3; } // 30 Hz down to 5 Hz.
    float MeasuredFps() const { return measuredFps; }
    unsigned LastWindowCaptures() const { return lastWindowCaptures; }

private:
    static constexpr unsigned BaseIntervalMs = 100 / 3;
    const void *ownerScene = nullptr, *ownerMap = nullptr;
    std::optional<uint64_t> lastFrame, captureBudgetStart;
    uint64_t windowStart = 0, healthyMs = 0;
    unsigned step = 1, frames = 0, captures = 0, lastWindowCaptures = 0;
    float measuredFps = 0;
    bool capturedThisFrame = false;
};

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
