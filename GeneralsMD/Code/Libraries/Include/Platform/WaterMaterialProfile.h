// Developer-only material cost experiments, never player quality settings.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include <cstring>

namespace WaterMaterialProfile {
enum Mode { Full, ConstantSurface, NoFoamLighting, NoSkyReflection,
    NoTerrainShadow, NoSeabedDetail, NoCaustics, Count };

inline Mode &Applied() { static Mode mode=Full; return mode; }
inline const char *Name(Mode mode)
{
    static const char *names[]={"full","constant-surface","no-foam-lighting",
        "no-sky-reflection","no-terrain-shadow","no-seabed-detail","no-caustics"};
    return unsigned(mode)<Count?names[mode]:"invalid";
}
inline bool Parse(const char *name, Mode &mode)
{
    for(unsigned i=0;i<Count;++i) if(std::strcmp(name,Name(Mode(i)))==0) {
        mode=Mode(i);return true;
    }
    return false;
}
} // namespace WaterMaterialProfile
