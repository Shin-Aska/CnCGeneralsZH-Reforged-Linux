// Disposable native water-capture fixture. No map, simulation transform or locomotor edits.
#pragma once
#include <cstring>
#include <cmath>
#include <cstdlib>
namespace WaterCaptureFixture {
static const char *const Template = "WaterCaptureCarrier";
inline bool Enabled(const char *mode, int controlPort, bool liveGame, bool multiplayer, bool replay)
{
    return mode && std::strcmp(mode, "carrier") == 0 && controlPort > 0 && controlPort <= 65535
        && liveGame && !multiplayer && !replay;
}
inline bool ValidPlayerName(const char *name)
{
    if (!name || !*name) return false;
    unsigned length = 0;
    for (; *name; ++name, ++length) {
        const unsigned char c = static_cast<unsigned char>(*name);
        if (length >= 63 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}
inline bool ValidCoordinate(const char *word)
{
    if (!word || !*word) return false;
    char *end = 0;
    const double value = std::strtod(word, &end);
    return end != word && *end == 0 && std::isfinite(value) && std::fabs(value) <= 1000000.0;
}
// PSAirCarrier's authored bow is -X. Postmultiply yaw180 without trigonometric error.
// Native PT origin is water+4; observed USA02 carrier origin is water-6.375.
// World-Z translation seats the visible carrier while leaving the physical PT untouched.
template<class Matrix> inline void ApplyRenderTransform(Matrix& matrix)
{
    for (unsigned row=0; row<3; ++row) {
        matrix[row][0] = -matrix[row][0];
        matrix[row][1] = -matrix[row][1];
    }
    matrix[2][3] -= 10.375f;
}
}
