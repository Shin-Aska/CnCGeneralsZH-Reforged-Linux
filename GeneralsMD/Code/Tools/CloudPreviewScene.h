// Standalone cloud preview scene; never linked into the game executable.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

namespace CloudPreviewScene {

// Call once after a real, non-shell map session has finished loading. Requires
// the ordinary logic, terrain, players, pathfinder, particles and tactical view.
// Replaces that preview session's initial objects with harmless visual samples,
// four unobstructed infantry routes, and render-only structures/props. The map's
// inspection pond is ordinary renderer water, without gameplay water damage.
bool initialize();

// Preview-only comparison; also valid before initialize(). Never saves a setting.
enum class Appearance { Original, Volumetric };
void setAppearance(Appearance appearance);
Appearance getAppearance();
const char *appearanceName();

enum class Scenario {
    Comparison, Tractor, AnthraxBomb, AnthraxGamma, ToxinBuilding,
    NuclearMissile, NuclearCannon, NuclearReactor
};
void setScenario(Scenario scenario);
Scenario getScenario();
const char *scenarioName();
const char *scenarioPhase();
void replayScenario();
void setLooping(bool enabled);
bool isLooping();

// Call before the host's normal engine update. Repeated calls on the same logic
// frame are harmless. The engine remains responsible for simulation/rendering.
void update();

// Reports particle presence, intact unit health and (after three seconds) motion.
// Closing an interactive preview before it has had time to move is permitted.
bool validationPassed();

// Call while engine subsystems still exist, before their teardown.
void shutdown();

} // namespace CloudPreviewScene
