// Isolated stress-scene host. Never linked into the normal match executable.
#pragma once
#include "CloudStressModel.h"
namespace CloudStressScene {
struct Settings {
    CloudStress::Config sources;
    bool reference = false, incrementalBaking = true;
    unsigned bakeSlicesPerFrame = 8;
    float freezeTime = -1; // Validation only: fixed cloud time, no wake inputs.
    unsigned replayFrame = 0, paletteFrame = 0;
    int nextPalette = -1;
    unsigned volumeSide = 256, slices = 24;
    float zoom = 0; // Zero chooses the scene framing.
};
bool initialize(const Settings &settings);
void update();
void setPalette(int palette);
void replay();
bool healthy();
bool validationPassed();
const char *paletteName();
void shutdown();
}
