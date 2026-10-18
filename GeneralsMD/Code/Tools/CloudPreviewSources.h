// Visual source events for the isolated preview; no weapons or gameplay objects.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include "CloudPreviewScene.h"
#include "CloudPreviewVolume.h"
struct Coord3D;
namespace CloudPreviewSources {
struct Field {
    CloudPreviewVolume::FieldState volume;
    const char *particleTemplate;
    float originalRadius;
    float secondsLeft;
};

// Copies the current source fields, returning the number written (at most two).
// Comparison fields keep 60 seconds available; source fields count down to clear.
unsigned getFields(Field *out, unsigned capacity);
bool initialize(const Coord3D &centre, float infantryHeight);
void shutdown();
void update(float deltaSeconds);
void select(CloudPreviewScene::Scenario scenario);
CloudPreviewScene::Scenario selected();
const char *name();
const char *phase();
void replay();
void setLooping(bool enabled);
bool isLooping();
bool valid();
}
