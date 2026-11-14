// Presentation-only eligibility for the opt-in live-rate root blend. No simulation clock/state.
#pragma once

#include <algorithm>
#include <cmath>

class SmoothMotionLiveRate
{
public:
	SmoothMotionLiveRate() : logic(0), cap(0), refresh(0), trusted(false), vsync(false), collecting(false)
	{
		reset();
	}
	void reset()
	{
		last = elapsed = measured = effective = 0.0;
		count = next = 0;
		active = false;
	}
	// A rate/cap transition starts fresh evidence, but never touches model history or blend alpha.
	void configure(int logicHz, int capHz, int refreshHz, bool trustedRefresh, bool synchronized,
		bool mayCollect, double now)
	{
		if (logic != logicHz || cap != capHz || refresh != refreshHz || trusted != trustedRefresh
			|| vsync != synchronized || collecting != mayCollect)
			reset();
		logic = logicHz;
		cap = capHz;
		refresh = refreshHz;
		trusted = trustedRefresh;
		vsync = synchronized;
		collecting = mayCollect;
		if (!collecting || logic <= 0 || (last > 0.0 && now - last > MaximumGap))
			reset();
		if (cap > 0 && cap <= logic)
			active = false;
	}
	// Call only after a successfully submitted, visible swapchain picture, never merely End_Render.
	bool submittedPresent(double now)
	{
		if (!collecting || logic <= 0)
			return false;
		if (last <= 0.0 || now <= last || now - last > MaximumGap)
		{
			reset();
			last = now;
			return false;
		}
		const double interval = now - last;
		last = now;
		intervals[next] = interval;
		next = (next + 1) % Capacity;
		if (count < Capacity) ++count;
		elapsed += interval;
		if (elapsed + 1.0e-9 < WindowSeconds || count < MinimumSamples)
			return false;
		double ordered[Capacity];
		std::copy(intervals, intervals + count, ordered);
		std::sort(ordered, ordered + count);
		const double median = count % 2 ? ordered[count / 2]
			: (ordered[count / 2 - 1] + ordered[count / 2]) * 0.5;
		measured = effective = 1.0 / median;
		// Metadata is a hint. Defaults (notably POSIX's 60) and contradicted modes cannot overrule
		// visible submission cadence. A matching Windows mode bounds a synchronized swapchain.
		if (trusted && vsync && refresh > 0 && std::fabs(measured - refresh) <= refresh * 0.10)
			effective = (std::min)(effective, double(refresh));
		if (cap > 0) effective = (std::min)(effective, double(cap));
		if (cap > 0 && cap <= logic) active = false;
		else if (effective > logic * 1.02) active = true;
		else if (effective <= logic * 1.01) active = false;
		count = next = 0;
		elapsed = 0.0;
		return true;
	}
	bool enabled() const { return active; }
	double measuredHz() const { return measured; }
	double effectiveHz() const { return effective; }
	static bool mayMeasure(bool userEnabled, bool forceAnyDisplay, bool sceneEligible)
	{
		return userEnabled && !forceAnyDisplay && sceneEligible;
	}
	static bool select(bool headless, bool userEnabled, bool forceAnyDisplay, bool video, bool eligible)
	{
		return !headless && ((userEnabled && (forceAnyDisplay || eligible)) || video);
	}

private:
	enum { Capacity = 512, MinimumSamples = 8 };
	static constexpr double WindowSeconds = 0.5;
	static constexpr double MaximumGap = 0.25;
	int logic, cap, refresh;
	bool trusted, vsync, collecting, active;
	double last, elapsed, measured, effective, intervals[Capacity];
	unsigned count, next;
};
