// Render-only breeze timing. No random draws, game state or phase resets.
#pragma once

namespace WeatherBreezeMath {
inline int period(int scriptPeriod, int stormPeriod, float envelope)
{
    if (!(envelope>0.0f)) return scriptPeriod;
    const int wet=scriptPeriod<stormPeriod ? scriptPeriod : stormPeriod;
    if (envelope>=1.0f) return wet;
    return int(scriptPeriod+(wet-scriptPeriod)*envelope+0.5f);
}
inline float step(float cachedStep, int cachedPeriod, int desiredPeriod)
{
    // Preserve legacy/off arithmetic exactly; never repeatedly rescale the cached random step.
    if (cachedPeriod==desiredPeriod) return cachedStep;
    if (cachedPeriod<=0 || desiredPeriod<=0) return 0.0f;
    const float ratio=float(cachedPeriod)/float(desiredPeriod);
    return cachedStep*ratio;
}
}
