// Render-only tree wind attenuation. No simulation state or random draws.
#pragma once

#include <cmath>

namespace TreeWaterSway {
inline float weight(bool hasWater, float vertexZ, float waterZ)
{
    if (!hasWater || !std::isfinite(vertexZ) || !std::isfinite(waterZ)) return 1.0f;
    // Still at/below the surface, easing into ordinary wind over three world units.
    const float t = (vertexZ-waterZ)/3.0f;
    if (t<=0.0f) return 0.0f;
    if (t>=1.0f) return 1.0f;
    return t*t*(3.0f-2.0f*t);
}

inline float pack(int swayType, bool hasWater, float vertexZ, float waterZ)
{
    // NORMAL.x already carries the integer constant index. Its low fraction holds suppression;
    // a quarter leaves round-to-nearest indexing intact and dry vertices bit-for-bit unchanged.
    return float(swayType) + 0.25f*(1.0f-weight(hasWater,vertexZ,waterZ));
}

struct Offset { float x,y,z; };
inline Offset silhouette(float stretchX, float stretchY, float windX, float windY, float windZ)
{
    // Project the already deformed branch onto its base plane along the sun direction.
    return {stretchX+windX+stretchX*windZ, stretchY+windY+stretchY*windZ, -1.0f};
}
}
