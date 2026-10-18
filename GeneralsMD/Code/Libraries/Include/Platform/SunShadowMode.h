// Sun-depth selection. Normal launches use maps; diagnostic overrides retain legacy/capture modes.
// This never selects dynamic lighting.
#pragma once

#include <cstdlib>
#include <cstring>

namespace SunShadow {
enum class Mode { Legacy, CaptureOnly, Mapped };

inline Mode ParseMode(const char *text, Mode defaultMode)
{
    if (!text || !*text) return defaultMode;
    if (std::strcmp(text, "capture-only") == 0) return Mode::CaptureOnly;
    if (std::strcmp(text, "mapped") == 0) return Mode::Mapped;
    // Unknown values retain the conservative legacy fallback.
    return Mode::Legacy;
}

inline Mode RequestedMode()
{
    const Mode defaultMode = Mode::Mapped;
#if defined(__linux__)
    return ParseMode(std::getenv("ZH_SUN_SHADOW_MODE"), defaultMode);
#else
    return defaultMode;
#endif
}

inline const char *ModeName(Mode mode)
{
    switch (mode) {
        case Mode::CaptureOnly: return "capture-only";
        case Mode::Mapped: return "mapped";
        default: return "legacy";
    }
}
}
