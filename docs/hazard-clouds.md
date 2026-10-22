# Volumetric hazard clouds

**Options → Graphics → Cloud style** selects **Classic** or **Volumetric**. Classic, the original
toxin and radiation ground effects, is the default; Volumetric is an opt-in. Switching applies live
and persists as `CloudStyle = 0` or `CloudStyle = 1` in Options.ini. The choice is independent of
the particle and smoke-shadow settings.

## How it is split

- **Neutral core**, built on every platform: `Platform/HazardCloud.h` (the renderer API: field and
  body types, palettes, the abstract `HazardCloud::Layer`, `HazardCloud::Supported()` and
  `HazardCloud::CreateLayer()`), `Platform/HazardCloudPolicy.h` (which templates are which hazard,
  build-up, validity), `HazardCloudFlow` (the planar smoke-transport simulation; no graphics device),
  `HazardCloud.cpp` (approved palettes, cache estimate), and the field tracking in
  `W3DParticleSys.cpp`.
- **SDL GPU backend**, built where the SDL GPU renderer is (Linux and macOS):
  `PosixDevice/Render/SdlHazardCloud*.cpp`. `Supported()` is true when the device in use is the SDL
  GPU device and it provides a sampleable scene depth plus the R16F/RGBA16F/RGBA32F formats the
  clouds use. Its shaders go through `SDL3_Create_Shader`, so Metal gets them transpiled from SPIR-V.
- **No backend**, everywhere else (Windows today): `HazardCloudUnsupported.cpp`, where `Supported()`
  is false and `CreateLayer()` returns null. A Direct3D 11 backend replaces this file and implements
  `HazardCloud::Layer`.

Without a supported backend the selector is hidden and the ground fields behave exactly as before
this change: the extended field tracking (fields keyed by height and owner, owner binding for
nuclear pulses, the sparse-emitter grace rule, skipping systems still in their initial delay) runs
only when the backend supports clouds. The answer is taken once per device and cached; when it
changes, the field tables are cleared before their key ordering changes.

## What Volumetric draws

Approved green toxin, blue beta, purple gamma and amber nuclear palettes use transparency defaults
of 67%, 77%, 74% and 0%. Effects read their actual emission radius and hazard lifetime. Toxin
billows form outward; nuclear haze fades into its full footprint. Formation is capped to one quarter
of short hazard lifetimes. Visible infantry stir the smoke; hidden, stealthed or shrouded infantry
leave no wakes. Nothing here changes damage, weapons, movement, gameplay randomness or simulation
state: Classic and Volumetric runs of the same scenario give the same game-state CRC.

Everything the clouds read from the game (field owners and their remaining lifetime, infantry
positions, visibility and shroud) is taken in `updateGroundFields` at the sync point, through
`W3DParticleSystemManager::takeReadsForRender`; the draw reads only those copies and the render
terrain heights.

The client admits at most eight visible cloud layers within a 192 MiB atlas-cache budget.
Over-budget fields, unknown effects and missing GPU resources keep the Classic effect. Clouds draw
after opaque geometry and water in the main scene; reflection and custom passes leave the
main-view caches and wakes alone. Caches and wakes reset on map or device changes.

The support model follows height-map terrain or an elevated plane. Cliffs and crossing decks have no
dedicated fixture or automatic geometry fallback: one height per XY sample cannot represent
several support surfaces within a field. Exactly coincident owners of the same palette cannot be
told apart by position.

## Tests

```sh
cmake --build build-linux --target generals test_hazard_cloud_policy test_gameengine sdl_gpu_frame_selfcheck
ctest --test-dir build-linux --output-on-failure -R '^(test_hazard_cloud_policy|test_gameengine|sdl_gpu_frame_selfcheck)$'
```
