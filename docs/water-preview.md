# Water Preview

`water_preview` is the dedicated native editor for Modern water materials. It shares the
production SDL renderer, GPU FFT, Modern water shader and `ModernWater.ini` parser with
the game. The default synthetic scene requires no game assets, save or running match.
`--scene` also replays a locally banked game view for reflection comparisons.

## Configuration and matching

On an ordinary launch the tool looks beside its executable for
`overlay/Data/INI/ModernWater.ini`, then `Data/INI/ModernWater.ini`. If neither exists it
uses the same built-in rule as the game. `--config FILE.ini` selects another file;
a missing file uses built-in defaults and reports that choice. Invalid configuration
produces a diagnostic and stops the preview so a bad edit cannot be mistaken for a
successful comparison. An empty file intentionally disables all replacement rules.

The initial map inputs are color **#324B32** (RGB 50,75,50), texture name `TSWater03.tga`,
water depth **2.0**, and minimum opacity **0.8**. These are the common authored water
settings in `GC_ChemGeneral` and `MD_USA05`. The texture name is used for rule matching;
the tool does not load the authored texture's pixels. The default matched material uses
**#2D3A0B** (RGB 45,58,11), **67% brightness**, **80% pollution**, and bubbles.

```ini
ModernWaterRule PollutedWater
  MatchColors
    Color = R:50 G:75 B:50
  End
  MatchTextures
    Texture = TSWater03.tga
  End
  ReplacementColor = R:45 G:58 B:11
  Brightness = 0.67
  Pollution = 0.80
  Bubbles = Yes
End
```

Each list accepts additional entries. A rule matches any listed color **and** any listed
texture; the first enabled matching rule wins. Colors match exact RGB bytes. Texture
matching ignores case and directory prefixes. An unmatched color is used by Modern
water without pollution; white preserves its normal blue appearance. Pollution does not
control tint strength. The Modern material's bubbles are procedural surface shading
with popping rings, not a volumetric gas simulation.

## Game integration

The global source file is `GeneralsMD/Code/Data/INI/ModernWater.ini`. A normal native
build stages it as `<build>/overlay/Data/INI/ModernWater.ini` (in this checkout,
`build-linux/overlay/Data/INI/ModernWater.ini`). The game reads `Data/INI/ModernWater.ini`
through its existing filesystem/overlay lookup at startup. There is no in-game settings
control for these rules.

Matching uses the effective map `StandingWaterColor` and `StandingWaterTexture` after
the existing map overrides have loaded, including save restoration. It does not inspect
map names, save names or the chosen general. The material is resolved again when those
effective values change. Classic water retains its authored parameters; Modern rules
replace only the material color and effects, retaining the map's depth and opacity.

If the game cannot read the INI or an edit is invalid, it logs a diagnostic and keeps the
complete built-in default rule set. Parsing is transactional: a valid first rule followed
by an invalid rule cannot install a partial configuration. A valid empty/comment-only
file disables all replacement rules. The preview instead stops on an invalid file so
editing mistakes are visible. Neither the game nor the preview automatically regenerates
a missing INI or rewrites its loaded configuration; preview export writes only its
selected output.

Keep durable project changes in the source INI. Building `water_preview`, building the
`zh_overlay` target, or running a normal full build copies it into the runtime overlay.
Restaging recreates that overlay, so direct edits to the staged copy are replaced. Restart
the game or preview after editing the configuration; neither watches the file for changes.

## Build and run

With an existing configured native build and dependencies:

```sh
cmake --build build-linux --target water_preview -j 4
./build-linux/water_preview
./build-linux/water_preview --help
```

The old `thrax_water_preview` build target remains a compatibility target that builds
`water_preview`; use the new executable for the maintained tool. No packages are
installed, and the preview does not edit the game's configuration or maps automatically.

## Interactive editor

- **1:** clean Modern baseline; **2:** resolved/editable material; **3:** split comparison,
  baseline on the left. Split mode uses two cropped views of the same scene and shader.
- **4:** Legacy planar reflection; **5:** SSR; **6:** Hybrid, the game's default;
  **7:** those three side by side, each column the same view; **8:** Path traced
  (experimental, synthetic scene only). The bar at the top of the
  window selects the same choices. See [Reflections](#reflections).
- **P:** show or hide the synthetic props: towers on the far bank, a bridge on two
  piers, sandstone ruins near the centre and a separate striped pole near the camera.
- **H:** show or hide the RGB, brightness and pollution panel.
- **L:** load the saved editor colors. This opens the panel and reports success,
  a missing preset, or an invalid preset without closing the viewer.
- **B:** toggle bubbles; the title reports their state. **[ / ]:** adjust pollution.
- **Space:** pause animation.
- **S:** save `water-preview.png` in the working directory, or the `--screenshot` path.
- **E:** export the current material as `water-preview-rule.ini` in the working
  directory, or the `--export-config` path. Export replaces that destination with a
  single rule matching the selected map color and texture; it does not merge a rule set.
- **Escape:** close the viewer.

The synthetic camera looks at a ground target at water level, with the game's controls:

- **Arrows:** pan over the ground in the current heading's screen directions.
- **Right drag:** pan away from the point where the button went down. The press only
  arms it; movement starts beyond a four-pixel tolerance. Holding the cursor farther from
  that anchor pans faster, and panning continues while the cursor is still. Both arrow
  and mouse pan speeds scale with camera distance.
- **Middle drag:** turn the heading, at **0.01 radians per horizontal pixel**.
  **Alt + middle drag:** tilt instead; dragging down tips the camera down.
- **Middle click** without a drag: reset heading, pitch and zoom to their startup values,
  leaving the current ground target in place.
- **Wheel** or **+ / -:** zoom; **Ctrl + wheel:** zoom five times faster.
  **Alt + wheel:** tilt by two degrees per notch; forward lays the camera flatter.
- **Home** or **R:** reset the entire camera, including its ground target, to the starting
  `--yaw`, `--pitch`, `--distance` and `--target X Y` (default `0 0`).

The left button operates the colour panel and reflection bar. Camera drags start outside
those controls and keep working if they cross them; losing window focus cancels a drag.
The default start is target `0 0`, yaw `0.10`, pitch `0.82` and distance `600`, keeping the
far-bank tower bases visible above the bridge while the ruins sit near the centre.
Pitch stays within **0.48–1.38 radians**, target coordinates within **±700**, and zoom
accepts **180–1050 units**, with the existing additional limit that keeps the finite water
patch's edge out of view at low pitches or wide aspect ratios. These camera controls and
start options apply only to synthetic mode; banked frames have a fixed camera.

The ruins stand on a submerged masonry footing in front of the bridge: a gate with two
square columns and an overhanging lintel, three columns under an L-shaped lintel, a lone
half-height broken column, and a fallen slab tilted through the surface. The columns are
10 units wide and 56–58 tall, with their bases below the water. They use the same flat sun
shading as the other props; the mirror clips every face at the water plane.

**Reset color** restores the material resolved at startup. **Save color** stores the five
panel values in SDL's per-user `CnCGeneralsZH-Reforged/WaterPreview/colors.ini` preference
file. Press **L** to restore those colors in the current session, or use **`--load-preset`**
to restore them at startup. Ordinary starts always resolve the INI, and old Dr Thrax
preview preferences cannot override it silently.
The bubble toggle is a material option exported to INI, not part of the five-value editor
preset. Unsaved slider changes affect only the current session.

## Reflections

Modern water can reflect the scene four ways, and the preview draws each with the
production shader exactly as the game sets it up:

- **Legacy** restores the Modern water reflection from before the reflection feature
  commit: a 512-pixel planar target containing the original selectively reflected objects,
  without the expanded terrain capture. This is the unchanged visual baseline; it is
  distinct from the game's Classic water style. A planar pass can show surfaces hidden
  from the main camera, costs a second scene render when updated, and follows one water level.
- **SSR** traces the reflected ray through the scene the frame has already drawn.
  It costs no extra scene render and works at any water level, but it can only reflect what
  is on screen and not hidden behind something nearer the camera.
- **Hybrid (Legacy + SSR)** combines the planar technique with SSR. Only this mode expands
  the game's planar capture to ordinary scene objects and terrain, at 1024×1024 instead
  of Legacy's 512×512. It filters reflected colour and object coverage together,
  so the transparent-black background fades into sky without a hard dark fringe.
  Where that capture
  supports the water plane and lookup footprint, an SSR hit contributes at most 35%:
  a hit on a main-camera face cannot erase an underside visible only in the planar view.
  Outside that support SSR uses its full confidence. This conservative blend is not
  geometric correspondence or a way to recover hidden surfaces in SSR-only mode.
  Hybrid is the game's default.
- **Path traced (experimental)** is a separate, optional mode. A CPU-built BVH supplies
  scene triangles to a GPU shader. Each sample uses at most three rays: the reflected
  view ray, a sun shadow ray at its hit, and one diffuse continuation for indirect
  colour. Lighting at the second surface uses unshadowed sun and approximate sky,
  which can make enclosed surfaces too bright. This affects the water reflection only.
  The main scene and its shadows
  keep their existing rendering.

The path tracer uses a 256×256 planar capture and two RGBA16F history textures
(1 MiB together). It traces one sample per pixel per update, averages in linear
light, and stops at 64 samples. Camera, geometry, lighting or capture changes reset
the history; there are no motion vectors or reprojection. After a camera or scene
change it traces at 128×128, then restores 256×256 after at least 150 ms and eight
unchanged updates. The quiet interval prevents resolution changes between moving
scene snapshots at high frame rates.
A moving view can remain noisy and soft. The water shader distorts the capture with
the existing wave lookup; it does not trace individual FFT facets or water refraction.

Game geometry includes eligible opaque scene meshes, props and known terrain outside the
main camera view. Collection is limited to the reflected view and nearby geometry
that can affect its indirect light or sun shadows. Fully submerged geometry is omitted.
Terrain uses two-cell patches where shorelines, shroud and shared edges permit;
this can lose small terrain features. Material colour is approximated per triangle. Cutout/blended
meshes, particles, procedural vertex effects and separately batched trees
and bridge decks are not represented. Failed capture setup falls back to Hybrid
and reports the reason. This is a limited experiment, not a replacement for Hybrid.
Its CPU scene collection/BVH work and GPU traversal add cost only while selected.
With a stationary view, scene collection runs at most ten times per second; camera,
capture and lighting changes refresh it immediately. Reflected moving objects can
lag by 100 ms plus the time to the next rendered frame. Samples continue accumulating
between scene refreshes. Unchanged objects reuse cached geometry. Moving objects refit the BVH when topology
allows, with a full rebuild every 32 updates or when needed. Geometry changes still
reset accumulation and upload the complete scene, so animated scenes remain expensive
and can prevent convergence. Offscreen independently animated parts may retain their
last evaluated pose, and cached material samples do not follow in-place texture edits.

Primary rays reach 1,000 world units from the water; indirect and sun shadow rays
reach 160 and 600 units respectively. Distant geometry can therefore disappear from
reflections or stop casting reflected shadows. Indirect and shadow traversal also
have work limits; exhausting them darkens that lighting contribution rather than
assuming a clear path. Primary traversal has no such work limit.

In the same busy saved game and camera view on an RTX 4060 at 1280×720, the original
Path traced implementation averaged 9.3 FPS and this version averaged 38.0 FPS
over logic frames 106000–107000. Both ran offscreen with a 60 Hz cap. These are
whole-game timings during a live battle, not isolated GPU timings or a guarantee
for other maps. This remains an experimental, lower-detail reflection mode.

Run the synthetic test with:

```sh
./build-linux/water_preview --reflection path
```

Existing `.wbank` files contain images and water draws, not complete scene geometry.
They cannot run this mode: `--scene sw-general --reflection path` reports an error
and the Path button is disabled during bank replay. For a converged deterministic
capture, use `--offscreen --frames 64 --reflection path --screenshot path.png`.

Hybrid's filter uses twelve point texture reads across the existing three roughness
offsets, resolving coverage before interpolation. It keeps the original display-space
colour filtering, removes coverage before one gamma decode, then mixes with sky in
linear light. It needs no history or
extra scene pass. Doubling the target width and height adds 3 MiB of colour storage
and increases planar raster work by up to four times; draw submission count and refresh
cadence stay unchanged. Depth backing also grows, and the backend can retain scratch
depth targets for both sizes after switching modes. Immediate target-creation failure
retains the previous target or tries 512×512 on initial allocation.
The synthetic comparison renders separate 512 and 1024 targets. Existing scene banks
keep their captured resolution and demonstrate the filtering change only.

In the Linux game, **Options → Effects → Reflections → Water reflection** selects
Legacy, SSR, Hybrid or Path traced. **Accept** applies the change live and saves `WaterReflection`
to Options.ini (`0`, `1`, `2` or `3`, respectively). Hybrid remains the default when the
preference is absent. These modes apply to **Modern** water, selected under Graphics;
Classic water keeps its original rendering.

**Ctrl+Shift+W** cycles Legacy → SSR → Hybrid → Path traced → Legacy live as a session-only
override. With Classic water selected, the key still cycles the setting for the next
switch to Modern water.
Bind `CYCLE_WATER_REFLECTION` in CommandMap.ini to choose another key; a command bound
to Ctrl+Shift+W takes precedence over this default.

`ZH_WATER_SSR` overrides the setting and hotkey when it names `legacy`, `ssr`, `hybrid` or `path`;
`mirror` remains an alias for `legacy`, including in the preview's `--reflection` option.
`nomirror` draws sky only, for timing what the mirror costs, and is never part of the
hotkey cycle. Unset or unrecognised values leave the setting in control. The environment
is read once per run. `ZH_WATER_DEBUG=ssr`
shows the traced colour over dark-blue misses, and `ZH_WATER_DEBUG=ssr-hit` shows
confidence in red, world distance travelled divided by 700 in green and skipped traces in blue.
`ZH_WATER_DEBUG=ssr-mix` shows the actual SSR blend weight in red, planar trust in green,
and the trace confidence in blue. The trace keeps a fixed spatial sampling phase, bounded
refinement of real crossings and a CPU-uploaded projection matrix. It uses no temporal
history; sparse hits can still produce speckled edges in SSR-only mode.
The preview clears an inherited `ZH_WATER_DEBUG`; pass `--debug ssr` or `--debug ssr-hit`
(or any other water debug view) to show one there.

Tilt the camera down (Alt + middle drag downward) until the towers leave the top of the window to see the
screen-space trace lose them while the mirror keeps them. The default green material
and the steep starting camera make all reflections faint; `--mode clean --pitch 0.6`
shows them more clearly.

## Banked scenes

Bank the saved view from the Linux game, then replay it without running a match:

```sh
mkdir -p build-linux/water-scenes
ZH_WATER_BANK="$PWD/build-linux/water-scenes/ruins.wbank" ZH_WATER_BANK_FRAMES=300 \
  ./build-linux/generals -loadsave "ReferenceSave"
./build-linux/water_preview --scene ruins
./build-linux/water_preview --scene ruins --reflection compare --width 1800 --height 800
./build-linux/water_preview --scene build-linux/water-scenes/ruins.wbank \
  --offscreen --hide-controls --screenshot build-linux/water-scenes/ruins.png
```

Use the game's existing `-loadsave` save name/path convention. Keep the desired view still
until the capture line appears. The counter advances only for presented frames with an
accepted Modern water draw; the default is **300** such frames. The selected frame banks
all its accepted Modern water draws, with the exact pre-water colour and sampled device
depth, mirror, geometry, transforms, constant registers, states and bound 2D textures.
GPU-owned images are copied at draw time and downloaded after submission. A failed
readback or a frame containing different water snapshots reports an error and writes no
bank. Capture runs once per process; leaving `ZH_WATER_BANK` unset uses only a cached check.

Banks live in `<executable directory>/water-scenes/`, normally `build-linux/water-scenes/`.
A name with no slash/backslash and no `.wbank` suffix resolves there; other values are
literal paths. **Banks contain proprietary game imagery and must stay untracked.** The
build directory and all `.wbank` files are ignored; do not force-add them or commit captures.

The bank supplies the window size unless `--width` or `--height` overrides that dimension.
Resized views fit with letterboxing. Reflection comparison renders three independent
bank-size targets before fitting them into the window's thirds; their camera and input
snapshots stay identical. The title and startup controls say **FIXED CAMERA (banked frame)**:
camera drags, middle click, arrows, wheel, **+ / -**, **Home** and **R** have no effect,
and `--yaw`, `--pitch`, `--distance` and `--target` do not move the banked view.
Synthetic props, bed and mirror rendering are skipped. The captured mirror is
used only when present. Mode 4 and the first comparison column are labelled **CAPTURED PLANAR**:
version 1 banks contain one baked mirror and cannot regenerate the original selective
Legacy pass. To inspect that original pass, capture the game with `ZH_WATER_SSR=legacy`;
the synthetic props are shared test geometry, not an archived copy of the original game scene.
**Space** pauses animation; wave time starts at each draw's banked
`c1.x` and advances by the preview's elapsed time (`--time` applies to synthetic mode).
The banked `c94`/`c95.xyz` material remains in use until sliders, material keys, reset,
explicit preset loading or command-line material overrides edit it. Reflection selection
always overrides only `c1.z` and `c95.w`; **1/2/3** retain clean/material/split comparisons.

Occlusion is exact at every pixel: the banked depth is written back into the depth buffer
through a grid with one pretransformed vertex on each pixel centre. The visible opaque colour is
the original bank image, and the water shader samples the **exact banked colour and R32_FLOAT
depth**.
The opaque picture, mirror, foam history and other captured textures are frozen; only
procedural water/FFT time advances. This replays a frame, not the underlying map or units.

`Platform/WaterSceneBank.h` defines version 1: `ZHWBANK1`, little-endian uint32 version,
target dimensions, colour image, float32 depth array, optional mirror image, camera float3,
then water draws. Each draw stores program/FVF/stride, vertex bytes, uint32 triangle-list
indices, world/view/projection, viewport/depth range, pixel/vertex float4 registers, all
256 render states, 16 stage/sampler state arrays and texture transforms, 16 level-zero
BGRA8 images, and the mirror stage (1). Array lengths are uint32 element counts; an image
is width/height/byte count followed by top-first tightly packed pixels. Stage 8 FFT maps
are absent and regenerated from `c1.x` by the production device. The D3D-shaped device has
texture-stage states for stages 0–7; unused entries 8–15 are zero. Readers reject unsupported
versions, truncation, invalid sizes/indices and trailing data before replacing their output.

## Repeatable comparisons and export

```sh
./build-linux/water_preview --offscreen --time 12 --mode split \
  --screenshot /tmp/water-split.png

./build-linux/water_preview --map-color C03020 --map-texture OtherWater.tga \
  --offscreen --screenshot /tmp/water-red-map.png

./build-linux/water_preview --offscreen --mode clean --pitch 0.6 --distance 600 \
  --reflection compare --width 1800 --height 800 --screenshot /tmp/water-reflections.png

./build-linux/water_preview --target -60 -20 --yaw 0.2 --pitch 0.6 --distance 370

./build-linux/water_preview --color 2D3A0B --brightness 0.67 --strength 0.8 \
  --export-config /tmp/proposed-modern-water.ini
```

`--export-config` writes the startup material immediately and makes **E** update that
same file after edits. The exported rule is parsed by the shared parser before writing.
Explicit `--color`, `--brightness` and `--strength` override the resolved material and any
explicitly loaded editor preset. `--no-bubbles` overrides the rule's bubble switch.
`--map-color` and `--map-texture` change matching inputs; `--map-depth` and `--map-opacity`
change the independent authored depth/opacity parameters. `--strength 0` removes
pollution while retaining the selected color; **1** or `--mode clean` shows the untouched
Modern baseline. `--hide-controls` excludes the editor panel from captures.

Offscreen mode defaults to one frame. `--frames 180` runs three seconds with deterministic
1/60-second steps. `--time` sets the initial wave time; `--width` and `--height` set output
size. Startup never loads a saved preset without `--load-preset`, including offscreen
captures; **L** loads it explicitly during an interactive session.

## Implementation

The tool compiles `Tools/water_preview.cpp` with its `Tools/WaterPreview*.h` helpers and links
the ordinary production shader library. The synthetic water patch remains a 220-by-220
grid spanning ±1100 units. Its world translation follows the target, snapped to whole
10-unit cells. The production vertex FFT samples positions before the device world
transform, so the preview bakes this translation into the vertex upload and draws with
an identity device world matrix. Vertices remain on the same world lattice and both
shader stages sample matching world positions; moving the patch does not move the waves.
The distance safeguard measures
view rays from the target and leaves room for the snapping offset and wave displacement.
The bed spans ±2600 and stays fixed with the props. Height and optical contexts retain a
fixed world mapping over that bed; their uniform one-texel fields describe the flat bed
and enclosed-water exposure, while the opaque scene depth supplies submerged prop surfaces.
Material inputs use the dedicated constants in
`Platform/ModernWaterMaterial.h`; light colors, texture factors and vertex metadata retain
their production meanings. The two initial experimental shader files in `Tools/` are
preserved as unbuilt historical references. The original investigation and capture notes
remain in [thrax-water-preview.md](thrax-water-preview.md).

## Local validation (2026-10-18)

The maintained target built with the existing native dependencies. Short deterministic
captures covered the default material, pollution disabled, unmatched red/blue/white,
clean baseline, split view, missing/empty/invalid INIs, and rule export/reimport. Missing
configuration produced a pixel-identical default capture. An exported edited material
reimported pixel-identically; unmatched white matched the clean baseline exactly. Split
halves differed from their full-width reference captures by at most one channel byte.
An isolated saved editor preset was ignored on an ordinary start and applied only with
`--load-preset`.

A 120-frame windowed run displayed the panel and recorded 120 FFT updates and 120 visible
presents, with no refused draws. The default, red, split and panel captures were inspected.
Artifacts are in `build-linux/water-preview-validation/`. These checks validate the tool;
actual map/save integration has separate engine validation.

The rebuilt game also loaded an isolated copy of the existing Dr Thrax save in both
Modern and Classic modes. Modern logged RGB 50,75,50 plus `TSWater03.tga` resolving to
`PollutedWater`; Classic used its original water and reflection shaders. Both completed
normally, and the captures `game-modern-save.png` and `game-classic-save.png` were inspected.
Both runs reported the same single render-target feedback refusal; this comparison does
not establish that the wider renderer is free of existing draw refusals.

The shared parser tests (753 checks), map-override integration tests (126 checks), shader
compilation, GPU material invariants and constant-layout tests passed. The complete engine
suite ran 593 tests with one failure in the existing
`minimap_classic_looks_on_the_press_in_both_mouse_setups` test; the same failure is recorded
in the October 16 logs, before these water changes.

Camera/ruins validation on 2026-10-20 rebuilt `water_preview` without warnings and passed
all six tests selected by `ctest --test-dir build-linux -R "water|test_shader_sdl"`.
A temporary harness under `build-linux/camera-ruins-validation/` passed camera-event and
water-plane clipping checks. Software Vulkan captures covered the default, clean, panned
and pan-limit views. With camera and time fixed, grid centres at 0, +20 and -20 rendered
pixel-identically; a banked frame also stayed pixel-identical with camera start overrides
and injected middle-drag/wheel input. Live windowed mouse feel still needs a human check.
