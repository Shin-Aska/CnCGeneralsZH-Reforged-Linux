#pragma once
#include <cstdlib>
#include <cstring>

// Fault validation only, at resource/program/pipeline creation, never the cached
// draw path. The harness clears inherited requests unless --fault is explicit.
inline bool Sdl_Sun_Shadow_Fault(const char *point)
{
    const char *requested = std::getenv("ZH_TEST_SUN_SHADOW_FAILURE");
    return requested && std::strcmp(requested, point) == 0;
}
