// Standalone CPU checks for the isolated cloud preview's infantry wakes.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "CloudPreviewFlow.h"

// These executable checks must also run in release builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace F = CloudPreviewFlow;

namespace {

unsigned valueCount()
{
    return F::width() * F::height() * 4;
}

std::vector<float> snapshot()
{
    return {F::data(), F::data() + valueCount()};
}

void checkBounds()
{
    assert(F::valid());
    for (unsigned i = 0; i < valueCount(); i += 4) {
        const float *pixel = F::data() + i;
        for (unsigned channel = 0; channel < 4; ++channel)
            assert(std::isfinite(pixel[channel]));
        assert(std::hypot(pixel[0], pixel[1]) <= 18.001f);
        // Flow must only transport smoke, never clear a body-shaped hole or
        // add a compressed ring. The reserved texture channels stay exact zero.
        assert(pixel[2] == 0 && pixel[3] == 0);
    }
}

void initialize()
{
    F::setDomain(300, 500, 1000, 780);
}

std::vector<float> runPath(float hz, float speed = 35.0f)
{
    initialize();
    for (unsigned i = 0; i < 3 * hz; ++i) {
        float time = (i + 1) / hz;
        F::Body body = {450 + time * speed, 640, 0, speed, 0, 6, 22};
        F::step(1.0f / hz, &body, 1);
    }
    checkBounds();
    return snapshot();
}

} // namespace

int main()
{
    auto start = std::chrono::steady_clock::now();
    initialize();
    for (unsigned i = 0; i < 60; ++i)
        F::step(1.0f / 60, nullptr, 0);
    assert(F::maxDisturbance() == 0);
    assert(std::all_of(F::data(), F::data() + valueCount(), [](float value) { return value == 0; }));
    std::puts("PASS empty field stays exactly zero");

    F::Body resting = {500, 640, 0, 0, 0, 6, 22};
    for (unsigned i = 0; i < 60; ++i)
        F::step(1.0f / 60, &resting, 1);
    assert(std::all_of(F::data(), F::data() + valueCount(), [](float value) { return value == 0; }));
    assert(F::maxDisturbance() == 0);
    std::puts("PASS resting body leaves every field channel exactly zero");

    auto at30 = runPath(30);
    float movingPeak = F::maxDisturbance();
    assert(movingPeak * 18 > 4 && movingPeak * 18 < 6);
    auto beforePause = snapshot();
    F::step(0, nullptr, 0);
    assert(beforePause == snapshot());
    std::puts("PASS moving infantry displace smoke; zero-delta freezes an active wake exactly");
    for (unsigned i = 0; i < 3 * 60; ++i)
        F::step(1.0f / 60, nullptr, 0);
    float after3 = F::maxDisturbance();
    for (unsigned i = 0; i < 3 * 60; ++i)
        F::step(1.0f / 60, nullptr, 0);
    float after6 = F::maxDisturbance();
    // Momentum continues transporting smoke briefly after the actor passes;
    // most displacement should settle in three seconds, then nearly vanish.
    assert(after3 < movingPeak * 0.25f);
    assert(after6 < movingPeak * 0.015f);
    std::printf("PASS wake displacement heals: initial %.5f, +3s %.5f, +6s %.5f world units\n",
                movingPeak * 18, after3 * 18, after6 * 18);

    auto at60 = runPath(60);
    auto at120 = runPath(120);
    double norm = 0, error30 = 0, error120 = 0;
    for (unsigned i = 0; i < valueCount(); ++i) {
        norm += std::abs(at60[i]);
        error30 += std::abs(at30[i] - at60[i]);
        error120 += std::abs(at120[i] - at60[i]);
    }
    assert(norm > 0);
    std::printf("Frame-rate path-relative L1 difference: 30 vs 60 %.3f%%, 120 vs 60 %.3f%%\n",
                100 * error30 / norm, 100 * error120 / norm);
    assert(error30 / norm < 0.12);
    assert(error120 / norm < 0.001);
    std::puts("PASS equivalent path across frame rates");

    runPath(60, 20);
    float infantryDisplacement = F::maxDisturbance() * 18;
    std::printf("Typical infantry speed: 20 world units/s produces %.5f units of displacement\n",
                infantryDisplacement);
    assert(infantryDisplacement > 2 && infantryDisplacement < 4);

    initialize();
    F::Body teleported = {750, 640, 0, 50000, 0, 6, 22};
    F::step(1.0f / 30, &teleported, 1);
    for (unsigned i = 0; i < valueCount(); i += 4)
        assert(F::data()[i] == 0 && F::data()[i + 1] == 0);
    checkBounds();
    F::step(100, nullptr, 0);
    checkBounds();
    F::Body invalid = resting;
    invalid.x = std::numeric_limits<float>::quiet_NaN();
    F::step(1.0f / 30, &invalid, 1);
    assert(!F::valid());
    F::reset();
    assert(F::valid() && F::maxDisturbance() == 0 && F::activeBodies() == 0);
    std::puts("PASS teleport rejection, bounded stalled frame, invalid-input reporting and reset");

    // Four opposing paths exercise overlapping injections and solver bounds.
    initialize();
    for (unsigned i = 0; i < 600; ++i) {
        float time = i / 60.0f;
        F::Body bodies[4] = {
            {450 + 35 * time, 624, 0, 35, 0, 6, 22},
            {850 - 35 * time, 630, 0, -35, 0, 6, 22},
            {450 + 35 * time, 650, 0, 35, 0, 6, 22},
            {850 - 35 * time, 656, 0, -35, 0, 6, 22}
        };
        F::step(1.0f / 60, bodies, 4);
        if (i % 60 == 0) checkBounds();
    }
    assert(F::activeBodies() == 4);
    checkBounds();
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("PASS four crossing infantry remain finite and bounded; full test %.3fs\n", elapsed);
}
