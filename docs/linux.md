# Linux hazard clouds

On Linux, **Options → Graphics → Cloud style** selects **Classic** or **Volumetric**. Volumetric is the default; switching applies
live and persists as `CloudStyle = 0` or `CloudStyle = 1` in Options.ini. The choice
is independent of the existing particle and smoke-shadow settings. Other platforms keep their
existing ground fields and hide this selector.

The game shares its volume renderer with the [isolated preview](cloud-preview.md).
Approved green toxin, blue beta, purple gamma and amber nuclear palettes use
transparency defaults of 67%, 77%, 74% and 0%, respectively. The exact linear RGB,
brightness and transparency values are retained in
[preferred.ini](../GeneralsMD/Code/Tools/cloud_preview/presets/preferred.ini) and the
matching compiled defaults. Preview palette files affect only the preview.
Increasing transparency reduces optical thickness and self-shadowing; 100% hides
smoke while nuclear heat and embers remain visible.

Supported effects read their actual emission radius and hazard lifetime. Toxin
billows form outward; nuclear haze fades into its full footprint. Formation is
capped to one quarter of short hazard lifetimes. Visible infantry stir the smoke;
hidden, stealthed or shrouded infantry contribute no new wakes. These effects do
not change damage, weapons, movement, gameplay RNG or simulation state.

The client admits at most eight visible cloud layers within a 192 MiB atlas-cache
budget. Over-budget fields, unknown effects and unavailable GPU resources keep
Classic fallback. Rendering runs after opaque geometry and water in the main
scene; reflection and custom passes leave the main-view cloud caches and wakes
alone. Caches and wakes reset on map or device changes.

The support model follows height-map terrain or an elevated plane. Cliffs and
crossing decks have no dedicated production fixture or automatic geometry
fallback; one height per XY sample cannot represent multiple support surfaces
within a field. The overlap fixture separates its owners: position-based emitter
association cannot distinguish exactly coincident owners of the same palette.

From an already configured Linux Release build with existing dependencies:

```sh
cmake --build build-linux --target generals cloud_preview \
  test_hazard_cloud_policy test_gameengine
ctest --test-dir build-linux --output-on-failure \
  -R '^(test_hazard_cloud_policy|test_gameengine)$'
```

`cloud_preview` is excluded from the default build and is available only when
Python 3 was found at configuration. The game does not depend on its scene
controller, palette editor or generated inspection map. See the
[preview guide](cloud-preview.md) for controls and standalone CPU checks, and the
[production runtime guide](../GeneralsMD/Code/Tools/cloud_preview/runtime-check.md)
for preparing a retail file-link root with `replay-check.sh --keep` and running
real hazard fixtures. The runtime harness records executable/input hashes, logs,
CRCs and captures in each new output directory. Inspect the captures and measure
representative scenes separately: a passing harness does not establish image
quality, crowded-battle performance or Windows/macOS runtime compatibility.
