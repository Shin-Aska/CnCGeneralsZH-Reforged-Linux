# Isolated toxin and radiation preview

`cloud_preview` is a separate development executable using the current game renderer,
particle definitions, terrain, and animated infantry. It opens a small inspection yard
with the Toxin Tractor's Contaminate release selected by default. Eight scenes cover
cloud comparisons and individual toxin or radiation sources. Four Rangers walk
through the clouds. All releases are visual only: source vehicles, buildings, bombs,
and missiles are render models, without their gameplay objects. The preview does not
fire weapons, activate special powers, execute object-creation lists, or create
gameplay hazards. The infantry remain passive, unable to attack, and indestructible;
their health is checked against its initial value.

The yard includes low structures, crates and barrels inside or along cloud edges,
plus a shallow pond beneath their near edge. This makes it possible to judge
building silhouettes, ground visibility, shoreline coverage and the baseline water surface
through the smoke. Scenery uses render-only models; the four walking lanes remain
clear and dry. The pond uses the unchanged baseline water renderer. The production runtime harness uses a separate flat fixture.

Volumetric smoke is the default in this executable. Each source keeps its full field
radius, centre, and infantry-height bounds throughout the sequence. As the release
begins, a thin veil covers the full footprint. Toxin and anthrax billows build outward
inside it; nuclear smoke gradually fades in across the entire area, without a
spreading front. Nuclear missile fallout takes six seconds to form after impact;
cannon and reactor fallout take three. The source then lets the smoke linger and
fade. Formation changes the density inside the fixed bounds rather than expanding
the radius, moving the centre, or raising the height. The renderer marches the camera
ray through that density,
accumulating light and opacity. It uses the opaque scene's depth to stop at terrain
and infantry, and samples density toward the light to shade the cloud's own interior.
The cloud's top is set to the infantry's measured model height, with authored collision
height as a fallback. Procedural noise uses world-space sizes, so a larger field
contains more billows instead of stretching one shape. Curling motion and rolling
tops animate the density, with outward source transport for toxin/anthrax. Lighter green, blue, purple, and
warm radiation palettes retain their colour in shadow through scattered fill light.
Where fields overlap, their density and light contributions are integrated together
along the view ray. A small persistent
flow field responds to the infantry's actual positions and velocities: movement
stirs the smoke's density into side curls and a trailing flow that gradually settles.
The response is weighted by height. Infantry have no surrounding clear bubble or
dense rim; smoke can remain in front of them and ordinary scene depth handles their
visible surfaces. Standing still injects no disturbance. Wakes move the billows but
leave the thin coverage veil in place, so infantry do not carve apparently safe holes
through the reference hazard footprint.

This is a local interaction model with planar flow and decay, not a full 3D fluid
solver. It affects smoke appearance; it does not push or damage the infantry.

Nuclear volumes mix charcoal-grey billows into a darker amber haze and shed sparse
orange-gold embers. Broad heat ripples rise through the air, bending both the cloud
contours and the scenery and infantry inside them. The distortion builds with the
release and fades with the field. Green toxin, blue anthrax, and purple gamma retain
their existing appearance. Press H to compare nuclear heat on/off, including while
paused, or start with `--no-heat`. H controls only refraction; the charcoal smoke and
embers remain. All three additions apply only to volumetric mode; original game
effects remain the original reference.

The shimmer samples a private copy of scene colour taken after cloud and ember
rendering, so dense smoke cannot hide the distortion. Displacement is capped at
eight pixels. Four-tap depth checks allow an immersed unit's silhouette to bend
with the surrounding haze when both surfaces lie within the same nuclear field.
The warp uses a continuous air interval and broad lateral waves across those
silhouettes, rather than changing at the unit's surface. Foreground objects outside
the field still retain
the depth guard. The hot-air envelope reaches 2.6 times the
cloud height; the smoke itself keeps its infantry-height bounds. No bubble or rim
is added around units. Animation uses simulation time and freezes on pause. The
original explosion and fire particles draw afterward and are not refracted.

Charcoal patches are baked into the existing nuclear lighting cache, retaining the
amber coverage veil and the source's formation and dissipation. Each active nuclear
field adds 24–96 GPU-generated ember billboards with small glowing cores, short tails,
upward drift, and gradual fading. Evenly spaced, slightly jittered anchors fill the
whole circular footprint, including its visual fringe, with a gentle fade at the
outermost edge. Counts scale with visible area: 64 in comparison, 96 for the
missile, and 24 for cannon/reactor fallout. Formation controls their opacity without
contracting their radius. They rise above the smoke to at most 2.76 times
its height, respect opaque scene depth, and require no new texture assets or
gameplay particle objects. Cleared fields and original mode draw no embers.

Two appearances are available in every scene:

| Appearance | What it shows |
| --- | --- |
| Original | The current game's field templates and colours for the selected toxin, anthrax, or radiation source |
| Volumetric | World-space density rendered by ray marching, with opaque-depth intersections and internal shadowing |

Keys 1, 2, and C change the cloud appearance within the current scene. They keep
the exact release phase, source model and release particles, infantry positions,
and stored volumetric wake. Selecting another scene keeps the chosen appearance.
Use 1 and 2 for a direct original/volumetric comparison, including while paused.
Key 3 remains an alias for volumetric smoke for compatibility with earlier previews.

The volume renderer, shaders, embers and flow live in the shared
`SdlHazardCloud` implementation. The normal Linux game exposes **Cloud style:
Classic / Volumetric** on the Graphics page. This executable retains its
own harmless scenes, A/B controls and palette editor; its Classic comparison is
independent of the normal game's saved cloud setting. Production fields read actual
gameplay radii and lifetimes and support terrain-following ground heights.

## Live color controls

The color panel opens with the demo. Press **P** to hide or show it. It has independent
palettes for **Toxin**, **Toxin beta** (blue), **Toxin gamma** (purple), and **Nuclear**.
Choose a palette, then drag the RGB sliders for its **Main** or **Shadow** color.
The swatch shows the rounded hex and RGB values; brightness ranges from 0 to 200%.
Each palette also has **Transparency**, from 0 to 100%. Zero uses the original
optical thickness; increasing it makes the cloud easier to see through, and 100%
hides its smoke. This changes optical thickness and self-shadowing without shrinking the
footprint or changing its source timing, formation or infantry wakes. Nuclear
heat and embers remain independent; use H to compare heat distortion separately.
Main controls the lit color, Shadow controls the shade (including nuclear charcoal
billows), and Brightness affects the whole cloud. These are material colors, so
the rendered result also depends on the cloud's lighting and transparency.

The preview and normal Linux game use these approved defaults. The table shows
rounded labels; the exact linear RGB, brightness and transparency floats are in
[preferred.ini](../GeneralsMD/Code/Tools/cloud_preview/presets/preferred.ini) and the
matching compiled palettes. Preserve those floats when editing or exporting; do
not reconstruct them from the hex labels.

| Cloud | Main | Shadow | Brightness | Transparency |
| --- | --- | --- | --- | --- |
| Toxin | `#C4F08C` | `#20510E` | 70% | 67% |
| Toxin beta | `#A8C2FF` | `#111A36` | 153% | 77% |
| Toxin gamma | `#D6A1FF` | `#310C3C` | 132% | 74% |
| Nuclear | `#BFAB78` | `#4C2F12` | 130% | 0% |

A valid existing preview preset loads automatically at startup. If the file is
absent, the preview uses its compiled defaults without creating a preset. A
malformed startup file reports an error and leaves those defaults intact; a failed
manual reload keeps the currently active palettes. The normal game always uses
its approved compiled defaults independently of preview files. **Reset palette**
applies those defaults to the selected palette; **Save all** keeps the result for
later preview runs.

Palette selection alone leaves the scene alone. **Show scene** opens that palette's
source in volumetric mode and resumes playback so the cloud can form. The scene
keys F2–F8 select the corresponding palette when changing scenes. Use **Space** to
freeze a useful frame: colors still update immediately while paused, without
replaying the release or changing infantry positions. Original mode remains the
unchanged game reference; edited colors appear when switching back to volumetric.

- **Reset palette** restores that palette's user-approved colors, brightness and
  transparency from the table above.
- **Save all** writes all four palettes to `cloud-colors.ini` in the preview user
  directory, by default `build-linux/cloud-preview/user/cloud-colors.ini`.
- **Load saved** reloads that file. Saved palettes also load automatically at startup.
- **Copy all** places the complete palette settings on the clipboard. Paste them
  into a text file to share colors and transparency; the text is a complete preset.

Edits and resets are live but are not saved automatically. Saving explicitly replaces
the previous preset atomically; invalid files are rejected without partially applying
their contents. The file preserves exact floating-point colors, while its hex comments
are rounded for readability. Different `--user-dir` profiles keep separate presets.
`--no-color-panel` starts with the panel hidden; P can still open it.
New exports use version 2 and include `transparency = 0..1` in every palette.
Existing version 1 presets load with zero transparency, preserving their appearance.

Versioned presets are kept in
`GeneralsMD/Code/Tools/cloud_preview/presets/preferred.ini` and
`GeneralsMD/Code/Tools/cloud_preview/presets/pre-tuning.ini`. The first contains
the approved defaults above; the second preserves the colors from before tuning.
To compare the earlier colors, copy `pre-tuning.ini` to a separate preview profile
as `cloud-colors.ini`, then launch with `--user-dir` pointing to that profile.
This leaves the preferred preset in the normal preview profile intact.

The editor uses the existing in-game UI renderer and adds no dependency. Palette
changes refresh cached lighting, including paused views. Procedural cloud shapes,
hazard coverage, formation, infantry interaction, heat eligibility, and ember behavior
stay unchanged.

The palette persistence checks can be run without opening the game:

```sh
mkdir -p build-linux/cloud-preview
g++ -std=c++17 -O2 -Wall -Wextra -Werror -IGeneralsMD/Code/Tools \
  GeneralsMD/Code/Tools/cloud_preview/test_colors.cpp \
  GeneralsMD/Code/Tools/CloudPreviewColors.cpp \
  -o build-linux/cloud-preview/test_colors
./build-linux/cloud-preview/test_colors
```

These cover exact default colors and save/load roundtrips, startup loading, invalid
edits and preset files, and failed saves preserving the previous preset. Tests use
temporary directories and do not change the preview's saved colors.

For a live UI check, pause each palette's scene and compare transparency at 0%,
50% and 100%. Confirm that Reset restores the approved values, Original effects
stay unchanged, and nuclear heat/embers remain independent of smoke transparency.
Exercise sliders and buttons at 640 × 480 and 800 × 600, including
hiding the panel during a drag. Save, copy, reload and restart with an isolated
`--user-dir`; compare the complete exported floats after each step. Retain captures
and GPU validation logs for the executable being reviewed.

## Rendering cost

The volume is integrated into a half-width, half-height RGBA16F target, so the main
ray pass evaluates one quarter as many pixels. A full-resolution composite blends
premultiplied light and opacity, checking the opaque depth of each sample. Local
surface slopes keep ordinary terrain from being mistaken for a silhouette. Samples
across an object edge are rejected; if none match, that pixel is ray marched at full
resolution to retain thin infantry silhouettes. Odd target dimensions retain their
last row and column. The scratch target is reused and replaced safely on size changes.

Density and internal lighting are evaluated into a world-space volume cache
before the camera ray pass. Each field uses 24 height slices, stored as a 2D atlas,
with horizontal sample spacing of at most two world units for the current scenes.
One pass builds density; another evaluates the same three sunlight samples and
stores scattered light and extinction. Camera rays interpolate these cached values
instead of repeating procedural noise and sunlight calculations at every step.
The full hazard footprint, source formation, palettes, height, and infantry flow
remain in use. Grid interpolation adds slight smoothing to the smallest details.
Density evaluation also skips billow cells beyond their support and calculations
whose masks contribute exactly zero.

The cache refreshes when simulation time, source state, field geometry, palette,
or infantry flow changes. Paused views and additional rendered frames reuse it;
camera motion still gets a fresh depth-clipped ray pass. Atlases are reused until
their size changes, with old textures retired after their recorded GPU users.
Atlas storage depends on the field dimensions. Production admission allows eight
visible layers within a 192 MiB atlas-cache budget, with Classic fallback for
fields that cannot be admitted. Shared screen scratch targets are outside that
budget. The preview owns at most two fields in a layer; the game assigns layers
to independently recognized hazard owners.

Heat adds one GPU colour copy and a small pass restricted to the projected hot-air
envelope. Keeping it separate lets the cloud ray/composite shaders retain their lower
cost. It does not add ray-march steps or rebuild the volume cache. The colour image
is reused (about 3.9 MiB at 1280 × 800); toxin-only scenes, cleared fields, offscreen
nuclear fields, and heat-off comparisons skip the copy. No CPU readback or extra
GPU wait is introduced.

Nuclear charcoal colouring adds two noise samples per lighting-cache voxel, with
no additional camera-ray samples. Embers add one small draw per active nuclear
field and no particle simulation or texture allocation.

The preview's two comparison fields share a 48–96 sample ray budget based on the
length of their combined interval. Coincident intervals use 48 samples instead of
doubling the work; intervals spanning twice the distance use 96. Both fields'
extinction and light are still accumulated together at each sample.

Measure the current build with the engine's existing timer from the repository
root. Use a new profile and stop other GPU workloads before each run:

```sh
mkdir -p build-linux/cloud-preview
ZH_UNATTENDED=1 ZH_OFFSCREEN_HZ=0 ZH_GPU_TIMING=12,1200 \
  ZH_CLOUD_PREVIEW_REBUILD_EVERY_FRAME=1 \
  ./build-linux/cloud_preview --scenario comparison --offscreen --frames 2400 \
  --no-color-panel \
  --user-dir "$PWD/build-linux/cloud-preview/perf-check" \
  > build-linux/cloud-preview/perf-check.log 2>&1
```

This excludes display pacing and begins timing twelve seconds after the first
Present. Keep shader compilation, captures and other setup outside the measured
window. Omit `ZH_CLOUD_PREVIEW_REBUILD_EVERY_FRAME` for ordinary cache reuse; with
it set, every rendered frame rebuilds the volume cache, including frames without
a new simulation step. The switch affects the shared renderer in both executables
and should be unset for ordinary play. Shutdown statistics report cache rebuilds,
field bakes and rendered frames.

For comparisons, build each revision in its own checkout/build directory and use
separate preview profiles with identical palettes. Record revision and executable
hash, compiler/build settings, GPU/driver, resolution, scenario, camera, profile
contents, heat and interaction settings, timing window and frame-time distribution.
Run revisions sequentially under comparable conditions. Preview timings include the
whole isolated scene; they do not predict a crowded battle's frame rate. Measure
representative gameplay separately with the same map/save, camera and settings.

## Field footprint

The field radius is the retail hazard radius listed below, or 115 units in the
comparison scene. It remains fixed during formation, lingering, and dissipation.
The original ground renderer scatters textured puddles within that radius; each
puddle can visibly extend beyond its centre. The volumetric renderer therefore uses
a separate soft fringe to cover that visible reach without changing the reference
hazard radius.

The fringe comes from the loaded particle template, including INI overlays and
frame-rate conversion. It follows the original ground renderer's half-life puddle
size and adds `sqrt(2) × 1.04 × puddle half-size` beyond the field radius. This accounts
for the rotated square's diagonal and its maximum four-percent breathing expansion.
It is a conservative visual extent, not an increase in gameplay coverage. The veil
fades through that fringe, and billows soften toward it. The cloud's maximum height
stays tied to the infantry model independently of the horizontal footprint.

## Source scenes

| Key | `--scenario` | Source and field |
| --- | --- | --- |
| F1 | `comparison` | Steady toxin on the left and radiation on the right, each with a 115-unit preview radius |
| F2 | `tractor` | Toxin Tractor / Contaminate: green toxin, 80-unit radius; the default scene |
| F3 | `anthrax` | Anthrax Bomb: blue anthrax, 300-unit radius |
| F4 | `gamma` | Dr. Thrax's Gamma Bomb: purple anthrax, 300-unit radius |
| F5 | `toxin-building` | Dr. Thrax's SCUD Storm destruction: green toxin, 80-unit radius |
| F6 | `nuke` | Nuclear missile impact: radiation, 200-unit radius |
| F7 | `nuke-cannon` | Nuclear cannon shell impact: radiation, 50-unit radius |
| F8 | `reactor` | Nuclear reactor destruction: radiation, 50-unit radius |

The source scenes use retail field radii, source models, and individual release
particle templates. For example, the tractor uses `UVToxinTrk` and `ToxinSpray`;
the bombs use `AVBomber_B` with the `AnthraxBomb*` or `AnthraxGammaBomb*` particles;
and the nuclear scenes reuse the `Nuke*` or `NukeCannon*` explosion particles.
The lingering fields use the selected appearance with green toxin, blue anthrax,
purple gamma, and radiation palettes. Original mode selects the retail field template
for that source; volumetric mode uses the preview's density renderer. The visible
source release and its timing are shared across both appearances.

The building example is specifically `Chem_GLAScudStorm`, whose retail death
behaviours create `OCL_PoisonFieldMedium` and play
`WeaponFX_BombTruckHighExplosiveBioBombDetonation`. The preview uses its `UBScudStrm`
and `UBScudStrm_E` models and the individual `BombTruckHEBio*` release particles;
it never executes that OCL or the building's death logic. Its green pool matches this
source; this is not a claim that every Dr. Thrax building emits toxin when destroyed.

The sequence is isolated choreography for inspection, with compressed timings of
roughly half a minute per loop (about 25–35 seconds, with the cannon shorter).
It is not a simulation of retail weapon timing or damage lifetime. In the game,
Contaminate's lingering field is created after spraying stops and its lifetime
depends on spray duration; the bomb fields last 60 seconds, while the selected
building and radiation pools last 30 seconds. Here every source visibly builds up,
lingers, and dissipates, then repeats unless looping is disabled. Comparison mode
keeps both fields formed for appearance and infantry-motion inspection.

## Build and launch

Run these commands from the repository root, using an already configured Linux
Release build with its existing dependencies:

```sh
cmake --build build-linux --target cloud_preview
./build-linux/cloud_preview
```

To select a source and watch one release through dissipation:

```sh
./build-linux/cloud_preview --scenario anthrax --no-loop
```

To select an appearance at startup:

```sh
./build-linux/cloud_preview --classic
./build-linux/cloud_preview --volumetric
./build-linux/cloud_preview --volumetric --no-interaction
./build-linux/cloud_preview --scenario comparison --no-heat
./build-linux/cloud_preview --scenario gamma --classic --no-loop
```

`--classic` and `--volumetric` select the appearance of the chosen
`--scenario`, or the tractor when no source is specified. Use `--scenario comparison`
for the steady two-field view. `--no-loop` leaves the scene clear after its release;
press B to replay it or L to enable looping.

The target is excluded from the default build. Python 3, used only to generate the
inspection map, must already be available when CMake configures the target. If the
target is absent, inspect the existing CMake configuration and dependencies before
reconfiguring; a normal game build does not build or verify the preview. Shared
`SdlHazardCloud` implementation units belong to `w3ddevice`; CMake excludes them
from the low-level `posixd3d9` library. Preview controller and editor units are linked
only into `cloud_preview`.

The executable reads the same original game assets as the game;
it finds the saved installation or searches the existing known/Steam locations.
If discovery fails, specify the Zero Hour folder containing `INIZH.big`:

```sh
./build-linux/cloud_preview --data "/path/to/Command & Conquer Generals - Zero Hour"
```

Keep the original Generals data adjacent or in `ZH_Generals`, as with the normal game.
A separate Generals location saved in the player's registry is carried into a new
preview profile when needed.

## Controls

| Input | Action |
| --- | --- |
| F1–F8 | Select the scene listed above |
| [ / ] | Previous / next scene |
| B | Replay the current release |
| L | Toggle automatic looping |
| 1 | Original game effects in the current scene |
| 2 | Volumetric smoke in the current scene |
| 3 | Alias for 2, retained for compatibility |
| C | Toggle Original ↔ Volumetric without restarting the release |
| I | Toggle infantry wakes in volumetric smoke |
| H | Toggle nuclear heat shimmer in volumetric smoke |
| P | Show/hide the live cloud color panel |
| Arrow keys or screen edges | Pan |
| Mouse wheel | Zoom |
| Q / E | Orbit by −45° / +45°, including while paused |
| R | Reset orientation and zoom to the initial height, including while paused |
| Middle mouse drag | Rotate and tilt |
| Middle mouse click | Reset orientation |
| Space | Pause/resume |
| F12 | Save a BMP screenshot, including while paused |
| Escape or window close | Exit |

The default window is 1280 × 800; `--width` and `--height` select another size.
The ordinary command bar is hidden and the walking units cannot be selected.
The window title shows the scene, release phase, appearance, looping, and volumetric wakes.
Switching between original and volumetric smoke is immediate, including while paused.
For a fixed-time comparison, pause with Space, select an appearance with 1/2 or C,
orbit with Q/E, and capture each view with F12. Changing the appearance keeps the
scene paused at the same release phase. R restores the initial orientation and zoom.

## Isolation and verification

The executable keeps its generated map, preferences, logs, and screenshots beneath
`build-linux/cloud-preview/user`. `--user-dir` selects another preview directory.
The installed archives remain read-only. The normal game shares the volume renderer
and shader, but does not link the preview controller, source choreography, entry point
or color editor, and does not depend on the preview's generated map.

For a bounded rendered comparison check without opening a visible window:

```sh
mkdir -p build-linux/cloud-preview
./build-linux/cloud_preview --scenario comparison --offscreen \
  --frames 4800 --screenshot-frame 4200 \
  --user-dir "$PWD/build-linux/cloud-preview/check" \
  > build-linux/cloud-preview/check.log 2>&1
```

This runs for 4800 engine logic frames; wall time depends on rendering. `--hidden` can be used
instead of `--offscreen` to exercise a hidden window. Captures are `sshotNNN.bmp`
in the chosen preview directory. The renderer remains active in both modes.

The controller reports particle counts, unit health, distance travelled, visits to
both fields, turnarounds, cloud height, and interaction strength every five seconds.
Volumetric density, GPU embers, and source-scene ground footprints are rendered
separately and are not represented by the particle count. The controller checks that
no gameplay hazard exists and health stays at its initial value. A failed
check or failed startup returns a nonzero exit status. In comparison mode, a complete
crossing is eight field visits (two per infantry); after a full lap the turnaround
count is at least four. Select a different `--scenario` to inspect that source's
release and lifecycle.

The flow model also has standalone CPU checks for wake decay, pause, frame-rate
consistency, teleports, invalid inputs, and overlapping infantry. Using the existing
C++ compiler from the repository root:

```sh
mkdir -p build-linux/cloud-preview
g++ -std=c++17 -O2 -Wall -Wextra -Werror -IGeneralsMD/Code/Tools \
  -IGeneralsMD/Code/Libraries/Include \
  GeneralsMD/Code/Tools/cloud_preview/test_flow.cpp \
  GeneralsMD/Code/Tools/CloudPreviewFlow.cpp \
  GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/SdlHazardCloudFlow.cpp \
  -o build-linux/cloud-preview/test-flow
./build-linux/cloud-preview/test-flow
```

In comparison mode, the original appearance uses `PoisonFieldLarge` and
`RadiationFieldLarge` through the production ground-field rendering path. Those
preview instances have a 115-unit emission radius and an extended lifetime so they
stay available for inspection. Each source scene selects its corresponding retail
field template and follows the preview's formation, lingering, and dissipation timing.
Volumetric mode uses its own world-space
ray-march pass rather than smoke billboards. Switching appearances preserves the
source sequence and does not add gameplay hazards or change infantry damage protection.

Source-scene ground descriptors retain the authored puddle size, texture, colour,
scatter, and bubbling while the preview timeline controls their fade. An explicit
W3D ground-cache refresh lets the preview replace these footprints while paused
without resetting release particles. Classic retains the authored ground effects;
the preview additionally refreshes their footprints for paused A/B changes.

For production integration checks, follow the
[hazard runtime guide](../GeneralsMD/Code/Tools/cloud_preview/runtime-check.md).
It prepares private fixtures for the actual game executable and deliberately uses
real damaging hazards. Preview health protection and compressed choreography do
not enter those tests or normal gameplay. The guide covers the four palettes,
owner continuity, overlap, terrain, water, budget fallback, lifecycle, live mode
switching, persistence and save restoration. Review generated logs and captures
for the exact build being proposed; these fixtures do not score visual quality
or establish full-battle performance.
