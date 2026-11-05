# Volumetric hazard clouds

**Options → Graphics → Cloud style** selects **Classic** or **Volumetric**. Classic, the original
toxin and radiation ground effects, is the default; Volumetric is an opt-in. Switching applies live
and persists as `CloudStyle = 0` or `CloudStyle = 1` in Options.ini. The choice is independent of
the particle and smoke-shadow settings. The Classic graphics setting overrides it: that is the
game's own picture, puddles included. The heat effects option takes the nuclear shimmer off.

## How it is split

- **Neutral core**, built on every platform: `Platform/HazardCloud.h` (the renderer API: field and
  body types, palettes, the camera a cloud is drawn from, the abstract `HazardCloud::Layer`,
  `Supported()`, `CreateLayer()` and `ReleaseDevice()`), `Platform/HazardCloudPolicy.h` (which
  templates are which hazard, build-up, validity), `HazardCloudFlow` (the planar smoke-transport
  simulation), `HazardCloud.cpp` (approved palettes, cache estimate), and the field tracking in
  `W3DParticleSys.cpp`.
- **Device-free half of a layer**, also on every platform: `Platform/HazardCloudCore.h` with
  `HazardCloudCore.cpp` (`HazardCloud::LayerCore`: fields, wakes, palettes, terrain grids, the
  shader constants and when the atlases rebake) and `HazardCloudShader.cpp` (the HLSL text of the
  cloud passes and the nuclear embers). A backend derives from `LayerCore` and supplies the device
  half: `initialize`, `render`, `estimatedCacheBytes` and `shutdown`.
- **Shared SDL volume**, Linux and macOS: `Platform/HazardCloudShared.h`,
  `Platform/HazardCloudSharedModel.h`, `HazardCloudSharedModel.cpp` and
  `PosixDevice/Render/SdlHazardCloudShared*.cpp`. Gameplay and `cloud_stress_preview`
  use the same renderer. Source snapshots and variable-length spatial bins have no eight-field
  admission limit. `Platform/HazardCloudBake.h` bounds injection to eight slices per render.
- **SDL GPU backend**, built where the SDL GPU renderer is (Linux and macOS):
  `PosixDevice/Render/SdlHazardCloud*.cpp`. `Supported()` is true when the device in use is the SDL
  GPU device and it provides a sampleable scene depth plus the R16F/RGBA16F/RGBA32F formats the
  clouds use. Its shaders go through `SDL3_Create_Shader`, so Metal gets them transpiled from SPIR-V.
- **Direct3D 11 backend**, Windows: `W3DDevice/GameClient/DX11HazardCloud.cpp`. `Supported()` is
  true while the Direct3D 11 frame presents (not under `-d3d9` or `-headless`) on a feature level
  11 device that renders and samples R16F and RGBA16F and samples RGBA32F and R32F. The same HLSL
  is compiled at shader model 5 without SDL's register spaces, through
  `DX11BackendClass::Compile_Program`, so the programs land in `Run/dx11shaders.cache` and a second
  run compiles nothing. `test_dx11backend` compiles every pass and fails if FXC refuses one.
- **No backend**: `HazardCloudUnsupported.cpp`, where `Supported()` is false and `CreateLayer()`
  returns null, linked only into the tests that carry no device.

The Direct3D 11 passes run on the context between two of the backend's draws, from
`W3DParticleSystemManager::doParticles`, while the draws go into the scene's own target or into a
screen filter's texture over the scene's depth (black and white, motion blur, cross fade:
`DX11BackendClass::Scene_Target`); a mirror and the sun's map keep Classic. The scene's depth is
read through `DX11DeviceClass::Get_Depth_Texture`, so the composite binds that target with no depth
buffer. Afterwards the scene target, its depth and the viewport are
bound again and `Forget_Bindings` makes the backend's next draw bind everything it needs. The heat
haze bends a `CopyResource` of the scene taken after the smoke and embers. The embers are drawn
with the backend's half-pixel viewport offset, so they sit where Direct3D 9 would put them.

Classic, or no backend, tracks the ground fields as retail did. The extended tracking (fields keyed
by height and owner, owner binding for nuclear pulses, the sparse-emitter grace rule, skipping
systems still in their initial delay) runs only with a backend and the Volumetric style. The device
is asked once and the answer cached; the mode is decided at the sync point only, and when it changes
the field tables are cleared before their key ordering changes. The first sync point with Volumetric
on (or a new device) also initializes the shared SDL renderer, or a throwaway layer on Windows
which builds the programs every layer shares: built by the first hazard, a program missing from the shader cache froze that frame for 3.6
to 5.5 s on Direct3D 11.

A field's object fires its weapon on the frame it dies, so one last system turns up with no living
owner. It joins the bound field already standing at the same place and palette, the dead owner's,
and fades with it. Without that it started a new unbound cloud at full strength, about ten seconds
of smoke after the hazard ended (a Toxin Tractor's contamination: owner gone at frame 3850, the
second cloud drawn until 4436).

## What Volumetric draws

Approved green toxin, blue beta, purple gamma and amber nuclear palettes use transparency defaults
of 86%, 86%, 86% and 0%. Effects read their actual emission radius and hazard lifetime. Toxin
billows form outward; nuclear haze fades into its full footprint. Formation is capped to one quarter
of short hazard lifetimes. Toxin Tractor contamination uses volumetric ground clouds while firing,
including Anthrax Beta and Gamma, as well as for the lingering field after firing stops.
Normal toxin impacts also use their green, beta or gamma volume, including the brief gamma splash.
The Tractor's attached continuous puddles and the damaged decorative GLA toxin tank's attached
leak use their actual world-space particle support. The tank leak remains a visual effect; it
gains no damage or gameplay hazard. These effects follow live-particle lifetimes, so stopping or
destroying the emitter does not start a new long-lived cloud.
Coincident detached impacts of the same template share one support, using the latest actual live
particle expiry; repeated shots do not add another full cloud's opacity for every splash. Attached
emitters retain their own stable particle-system identity. Original attached sprites are suppressed
only when that source's replacement is admitted in the main view; Classic and unavailable rendering
resources retain the original drawing path.
Visible infantry stir the smoke; hidden, stealthed or shrouded infantry
leave no wakes. Nothing here changes damage, weapons, movement, gameplay randomness or simulation
state: Classic and Volumetric runs of the same scenario give the same game-state CRC.

Everything the clouds read from the game (field owners and their remaining lifetime, infantry
positions, visibility and shroud) is taken in `updateGroundFields` at the sync point, through
`W3DParticleSystemManager::takeReadsForRender`; the draw reads only those copies, the render
terrain heights and the scene camera, which the caller hands over from `rinfo.Camera` rather than
the device's transforms (those can be the last draw's until the next state flush).

On SDL GPU, all recognized fields in the main view contribute to one shared world-space volume.
Production keeps its existing owner/lifetime, stealth, reveal and removal fades; source history
advances before camera culling. Visible support bounds are quantized to 128 world units, keeping
distant offscreen fields from diluting local voxel detail. One shared terrain grid supports
terrain-following sources; elevated sources retain their own plane.

The volume uses a 256 × 256 × 24 material atlas and two lighting atlases (36 MiB total, independent
of source count). Each display render injects at most eight interleaved slices. Once all slices
are ready, lighting runs and the finished snapshot replaces the front atomically. The front's
sources, terrain, wakes and bounds remain immutable while the next snapshot bakes. New sources,
moving supports, terrain changes and camera-domain changes queue the next snapshot without
restarting partial work. A reset or rewind discards it. With a 60 Hz display this adds about
33 ms of publication latency and sustains 20 completed bakes per second. A single half-resolution
ray march and depth-aware composite draw the shared clouds. Nuclear heat and embers remain
separate effects. Visible moving infantry receive the wake solver's existing 32-body allowance.

The Direct3D 11 backend still uses independent field atlases, conservative per-layer screen
bounds, an eight-layer limit and a 192 MiB cache budget. Those Windows limits can produce Classic
overflow; they are not used by the SDL shared path. The old SDL Layer backend remains available
for the two-field reference preview. Unknown effect templates and unavailable GPU resources
retain Classic coverage. Fully faded recognized fields stay invisible. Clouds draw after opaque
geometry and water in the main scene; reflection and custom passes leave the main-view state
alone. Map resets clear snapshots and wakes while keeping SDL programs and allocations warm.
Device teardown, resolution changes and replacement release the shared renderer.

The support model follows height-map terrain or an elevated plane. Cliffs and crossing decks have no
dedicated fixture or automatic geometry fallback: one height per XY sample cannot represent
several support surfaces within a field. Exactly coincident owners of the same palette cannot be
told apart by position.

`HAZARDCLOUD:` in the Windows log says when a layer first draws on Direct3D 11, and why the clouds
stopped if a program or a texture was refused.

Set `ZH_CLOUD_REPORT=1` to report cloud draws and Classic fallbacks to stderr. SDL also emits
`hazard-cloud-shared` counts, published snapshot age, pending slices, maximum slices per render
and shared atlas bytes. On the independent-layer path, a fallback with
`reason=budget limiter=layers` reached the layer limit; `reason=budget limiter=cache` could not fit
its atlases into the cache budget. Other fallback reasons use `limiter=none`.

## Tests

```sh
cmake --build build-linux --target generals test_hazard_cloud_policy test_cloud_stress_model test_cloud_stress_bake test_gameengine sdl_gpu_frame_selfcheck
ctest --test-dir build-linux --output-on-failure -R '^(test_hazard_cloud_policy|test_cloud_stress_model|test_cloud_stress_bake|test_gameengine|sdl_gpu_frame_selfcheck)$'
```

On Windows `build.bat Release test` runs `test_hazard_cloud_policy` and `test_dx11backend`.
`Scenarios/toxinslope.txt` puts a Scud Storm field on the mound at 1075, 3775 on Twilight Flame;
with `ClassicGraphics = no` and `CloudStyle = 1` in the run's Options.ini, `-camera 1075 3775 -turbo -screenshot 2200` shows
it.

`Tools/hazard-cloud-check.py` also exercises `budget` (nine medium toxin fields), `gamma-budget`
(nine large gamma fields) and `cache-budget` (seven large radiation fields). On SDL all of these
fields must stay volumetric. It also covers 128 mixed, nuclear and gamma owners, with distinct
and densely overlapping centres, plus late source/domain growth, terrain, water, formation/fade,
live Classic switching, save/load and identical gameplay CRC between styles.
The `tractor-green`, `tractor-beta` and `tractor-gamma` cases issue the actual Contaminate command
and check for volumetric clouds both during casting and after stopping; `tractor-green-move`
cancels casting by moving away. Gamma is researched through the Palace; `--crc-pair` also checks
the selected tractor cases against Classic.
The `tractor-attack-green` and `rebel-attack-beta/gamma` cases exercise real primary attack impacts;
`gamma-splash-expiry` isolates the retail ten-frame gamma pulse. `toxin-tank-attached` damages and
then destroys a real `GLAToxinTank` away from the map origin. Attached-source checks distinguish
the Tractor's continuous particles from the larger detached contamination pulses. The move-away
case cancels casting and checks the remaining particles rather than assuming the Tractor can move
while casting. Attachment transform composition was checked by source review, and the actual prop
fixture verifies world placement away from the origin. No requested real actor continuously emits
these puddles while moving, so that runtime route remains unexercised. The Direct3D 11 integration
was reviewed in source; these production fixtures run on Linux/SDL.
`gamma-splash-rebirth` checks that a second same-position pulse after expiry starts its own formation.
With toxin cases selected, `--live` additionally switches an active Tractor and damaged prop between
styles, destroys the prop during Classic, and switches back while its residual particles remain.
It also emits a brief gamma splash in each mode. `--timing` records 120 active whole-engine frames
for primary toxin attack cases; reports retain grouped member counts and shared-source peaks.

### Toxin source validation, 2026-11-05

Linux/SDL Vulkan production validation passed 25 runs and 403 checks with GPU validation enabled.
All 11 Classic/Volumetric pairs retained identical gameplay CRCs. Coverage includes actual primary
attacks, all three contamination attachments, move-away cancellation, short gamma expiry and
rebirth, decorative prop damage/destruction, live style switches and the existing save/reload check.
The isolated gamma pulse reached a published shared snapshot during its short life and visibly
faded before expiry. Repeated green/beta impacts had up to 45 live members while retaining one
shared source, avoiding multiplied opacity. Actual sprite decisions confirmed Classic restoration
and suppression after volumetric admission, including the destroyed prop's residual particles.

Manifests, logs, captures, source counts and active-attack frame timings are consolidated in
`build-linux/toxin-expansion-validation.json`. These are private ignored build artifacts; no
installed game files are changed. Direct3D 11 was source-reviewed, not run on this Linux host.

### Shared SDL integration validation, 2026-11-01

The production `generals` executable passed all five checks above. Real game fixtures passed
all four palettes, nuclear initial/lingering owner identity, terrain and water, expiry, the three
former budget-overflow cases, live Classic switching, save/load and identical gameplay CRC.
GPU validation reported no errors. Reviewed captures include terrain-following nuclear embers.
All six simultaneous 128-owner fixtures published all 128 sources without Classic fallback.
The successive-growth fixture published 29, 59, 90 and 119 sources while new fields continued
arriving, then published all 128. Pending work never exceeded the eight-slice render allowance.

The fixture uses ordinary medium hazard objects. Simultaneous stress births retain all owners;
successive births need 56-unit centre spacing because the game's startup cleanup weapon removes
older overlapping hazards. Scenario frames are authored at 30 Hz and scaled to the runtime's
60 Hz logic clock. These are gameplay/fixture rules, not renderer admission limits.

RTX 4060, Vulkan, 1280 × 800, uncapped offscreen: each case measured 120 steady whole-engine
Present intervals at logic frames 240–359, including the airborne visibility scouts. These are
production integration checks, not directly comparable to the earlier isolated large-field
prototype benchmark. No GPU fences were inserted into the timing runs.

| 128 medium fields | Mean | p95 | Maximum |
| --- | ---: | ---: | ---: |
| Mixed, distinct centres | 6.46 ms | 8.38 ms | 9.32 ms |
| Mixed, dense overlap | 10.86 ms | 13.08 ms | 18.64 ms |
| Nuclear, distinct centres | 6.71 ms | 8.32 ms | 11.56 ms |
| Nuclear, dense overlap | 12.17 ms | 15.13 ms | 19.77 ms |
| Gamma, distinct centres | 6.90 ms | 8.61 ms | 11.77 ms |
| Gamma, dense overlap | 11.69 ms | 14.05 ms | 15.14 ms |

None of these 720 samples exceeded 33.33 ms. This does not promise a locked frame rate for every
battle: source overlap still increases injection work, and fixed-grid detail decreases over a
larger visible domain. Local manifests, logs, captures and timings are consolidated in
`build-linux/cloud-integration-check/results.json`; `128-fields.png` shows the six load captures.
The normal Volumetric setting now uses this renderer on SDL. Windows retains its independent
layer backend; macOS shares the SDL code but was not run on this Linux host.

### Screen-bounds validation, 2026-11-01

The Linux Release build passed `test_hazard_cloud_policy`, `test_gameengine` and
`sdl_gpu_frame_selfcheck`. Production fixtures covered palettes, overlap, hillside terrain,
nuclear heat/embers and layer overflow; Classic and Volumetric retained the same gameplay CRC.
Controlled gamma and nuclear screenshots were pixel-identical before and after the bounds
change. Medium-field and hillside comparisons differed only in animated command UI regions.
Direct3D 11 received the same bounds integration but was not run on this Linux host.

Local measurements used an RTX 4060 (driver 595.99.02), Vulkan, 1280 × 800, uncapped offscreen
rendering and identical private profiles. Each executable ran twice, in ABBA order for medium
fields and BAAB for gamma. Every run collected exactly logic frames 240–839, with zero rejected
samples; screenshots and builds ran separately. These are whole-engine Present intervals,
not isolated cloud GPU timings or a prediction for a particular saved battle.

| Nine overlapping fields | Baseline mean | Bounds mean | Frame-time reduction |
| --- | ---: | ---: | ---: |
| Medium toxin | 2.898 ms | 2.598 ms | 10.4% |
| Large gamma | 4.327 ms | 4.260 ms | 1.6% |

At this historical checkpoint the eight-layer/192 MiB policy remained. Avoiding Classic overflow needed a separately
bounded volumetric representation for excess fields; raising the layer limit alone would not
address cache pressure or the cost of overlapping clouds. Local commands, logs, hashes, captures
and timing samples for this check are under `build-linux/cloud-load-check/`, summarized by
`results.json` there; they are ignored build artifacts, not repository fixtures.

## Shared-volume development fixture

`cloud_stress_preview` is an opt-in Linux/SDL GPU executable, excluded from the default build.
It boots the engine with real game assets, a separate flat inspection map and a private user
profile. The fixture itself does not register sources with the production particle manager or change its
Classic/Volumetric selection. The test scene creates visual source records, passive Rangers
and render-only props; it creates no damaging hazard objects.

The prototype stores all sources in one world-space volume. Variable-length spatial-bin lists
select sources for each voxel; they have no fixed per-bin overflow limit. Density and light
use three fixed-size RGBA16F atlases: material scratch and two lighting snapshots. A completed
snapshot is integrated and composited once per view. The 256-side, 24-slice configuration uses
36 MiB for those atlases; 512 uses 144 MiB. This
excludes the screen targets, flow, terrain and source/bin metadata. Source processing and
metadata still grow with field count and overlap: this is bounded volume memory, not unlimited
work. The CLI allows 1–4096 sources, subject to the inspection map's bounds.

It reuses the production procedural density and palette values. Green toxin, blue Anthrax,
purple gamma and nuclear sources retain their formation, release and end-of-life fades.
Toxin expands outward; nuclear haze fades across its full footprint and includes embers and
heat distortion. Shared lighting is experimental and can change the appearance of overlapping
clouds. One shared terrain height per XY position does not resolve stacked support surfaces.
The shared renderer targets SDL GPU; Direct3D 11 retains the independent-layer renderer.

Build and launch with an existing game-data directory (nothing is downloaded or installed):

```sh
cmake --build build-linux --target cloud_stress_preview test_cloud_stress_model test_cloud_stress_bake -j4
ctest --test-dir build-linux --output-on-failure -R '^test_cloud_stress_(model|bake)$'
build-linux/cloud_stress_preview --game-data "/path/to/Zero Hour" --fields 128 --palette mixed --volume-side 512
```

The default private profile is `build-linux/cloud-stress-preview/user`; `--user-dir` selects
another private location. The preview uses its own flat map overlay. Keys **1–4** select green,
blue, purple and nuclear, **5** selects mixed, **R** replays the formation, **Space** pauses,
**F12** captures a screenshot and **Esc** exits. The default mixed scene contains 32 of each
palette. `--layout overlap` places every source at the same location for a worst-case bin.
`--stagger`, `--lifetime`, `--radius`, `--fringe`, `--spacing`, `--columns` and `--zoom` control
fixtures. `--reference --fields 1` runs the existing renderer for a visual comparison.

Automated runs retain commands, executable hashes, logs, screenshots and optional whole-engine
frame timings in a new output directory:

```sh
python3 GeneralsMD/Code/Tools/cloud_preview/stress-check.py \
  --preview build-linux/cloud_stress_preview --root "/path/to/Zero Hour" \
  --out build-linux/cloud-stress-preview/check --fields 128
```

Add `--layout overlap` for coincident sources, or `--lifetime 8 --frames 600` to exercise expiry
at the preview's 60 Hz logic rate. Timing runs use `--timing --frames 900`; they require a
complete 600-frame measurement window and run separately from captures. Logged source counts
prove retention/submission, not visible pixels; captured frames must also be inspected.

### Initial synchronous prototype validation, 2026-11-01

The Linux build and `test_cloud_stress_model` pass. The CPU tests cover mixed palettes,
formation/release/fade, short lifetimes, deterministic replay, staggered births and expiry,
invalid bounds, and variable-length bins with more than 128 sources.

Engine captures verify 128 visible fields in the mixed scene and each individual palette.
All five cases also pass with all 128 sources in the same spatial bin. Additional GPU runs
retain 1, 128 and 256 sources with the same 24 MiB atlas allocation. Eight-second lifecycle
runs retain all records and finish with zero active/submitted fields, without Classic
substitution or damaging hazard objects. Runs with GPU validation enabled reported no
validation errors. A separate 15-second recording shows the four effects forming, lingering
and disappearing; its first second includes the engine's startup fade. A single gamma field
was visually compared with the existing renderer. Shared lighting and analytically integrated
source motion are not a promise of pixel-identical output.

Whole-engine measurements on an RTX 4060, Vulkan, 1280 × 800 used two sequential runs per
case, each with exactly 600 accepted samples at logic frames 240–839. Captures, video encoding
and builds ran separately. The table averages the two run means and reports the larger p95.
These synthetic fixtures are not a saved-battle FPS prediction. Grid fields use radius 30,
fringe 14 and spacing 64; coincident fields use radius 100 and fringe 64. The different volume
resolutions are explicit, so these rows are not a quality-matched comparison.

| 128 fields | Volume side | Atlas memory | Mean engine frame | Larger run p95 |
| --- | ---: | ---: | ---: | ---: |
| Mixed, grid | 512 | 96 MiB | 3.54 ms | 6.72 ms |
| Nuclear, grid | 512 | 96 MiB | 4.92 ms | 8.86 ms |
| Mixed, all coincident | 256 | 24 MiB | 16.14 ms | 43.48 ms |
| Nuclear, all coincident | 256 | 24 MiB | 18.45 ms | 48.89 ms |

The initial prototype removed the small per-field atlas/admission limit, but dense injection
caused large bake-frame spikes (addressed by the incremental work below). Nuclear heat also still scans the source list, and embers
remain separate draws. Those were optimization targets before production integration. The
fixed grid loses spatial detail as its world footprint grows; a production design needs an
explicit spatial-resolution policy. Extreme coincident nuclear sources also make the shared
heat distortion conspicuous. The executable remains an isolated fixture for evaluating those tradeoffs.

Local evidence is under `build-linux/cloud-stress-preview/`: `palettes-128`, `overlap-128`,
`timing-grid`, `timing-overlap`, `source-counts`, `lifecycle.mp4` and `lifecycle-frames.png`.
Each automated run retains its command, executable hash, validation checks and captures or
raw timing CSV. These are ignored build artifacts.


### Incremental baking

The prototype now injects at most eight of its 24 slices per display render, interleaved across
height to distribute work. It lights and publishes only after every material slice is complete.
The visible lighting atlas is never modified piecemeal: source records, bins, flow and source
ages belong to the same immutable snapshot. Camera transforms remain live, as does ember phase.
The low-resolution image is still cleared in full; partial material writes preserve earlier
slices. A reused metadata slot is submitted before new uploads can overwrite its old users.

At 60 display frames per second this retains 20 complete bakes per second. A snapshot takes
three display frames to build; it adds two frames of publication latency relative to a full
bake. Slower display rendering naturally reduces publication rate. Ordinary staggered births
and expiry queue the latest state without repeatedly restarting pending work. Replay, palette,
terrain and incompatible support changes invalidate old work; startup and reset wait for a
complete snapshot. All-expired sources hide immediately. Production promotion subsequently changed support/domain/terrain edits to coalesce into the next
immutable snapshot, preventing repeated cancellation during motion. Terrain is double buffered
with the source metadata. Reset and rewind still discard obsolete snapshots.

The shader also samples shared terrain and clamped wake displacement once per voxel, and
skips noise/billow calculations only where their contribution is zero. It retains every source,
the procedural pattern, all palettes and the configured volume resolution. Compared with the
original shader, five dense fixed-frame captures differed by at most one 8-bit channel step
in 24–78 pixels out of 1,024,000; completed incremental and synchronous snapshots were exactly
pixel-identical for all five palette modes.

`--sync-bake` keeps full-at-once baking for validation. `--bake-slices N` changes the injection
work bound, without changing source counts or spatial resolution. Validation controls
`--freeze-time SECONDS` hold cloud time and disable wake inputs while engine frames continue;
`--replay-frame N` and `--palette-at FRAME PALETTE` trigger scene-relative logic-frame actions.
Do not combine frozen time with replay when testing formation: replay then holds source age
at zero. The harness records these settings, completed-source counts, pending slices, maximum
slices per render and publication age. Timing adds p99, maximum and counts above 16.67/33.33 ms;
`--timing-start N` selects a 600-frame window (the default remains 240–839).

### Incremental validation, 2026-11-01

The build, source-model tests and bounded-slice tests pass. GPU validation runs cover all five
palette modes, frozen formation at 0.3/3 seconds, partial fade at 38.5 seconds and fully expired
sources at 43 seconds. Each completed incremental image is pixel-identical to `--sync-bake`
at the same frozen state. Scripted replay and palette changes cancelled jobs with 23 slices
remaining; another replay replaced an already-published 128-source volume. Captures show only
the newly selected palette, and both live reset runs finish with active/submitted/baked counts
of zero. No GPU validation errors or Classic substitutions were reported.

The same RTX 4060/Vulkan/1280 × 800 setup measured two runs per case, with 600 valid frame
samples per run. Dense distinct-position sources use a 16-column grid, spacing 8, radius 140,
fringe 14, stagger 0.02 seconds and lifetime 40; their window is frames 600–1199, after all 128
sources have formed. A frozen original executable ran once before and once after the optimized
measurements. Coincident/grid baselines are the initial measurements above, with matching
fixtures and their 240–839 window. Build, capture and encoding work ran separately from timing.
The table reports the larger p95 of each pair; these remain whole-engine frame times.

| 128 fields | Volume side | Original p95 | Incremental p95 | Incremental mean |
| --- | ---: | ---: | ---: | ---: |
| Mixed, distinct dense | 256 | 34.64 ms | 12.52 ms | 9.65 ms |
| Nuclear, distinct dense | 256 | 39.16 ms | 15.62 ms | 12.17 ms |
| Mixed, all coincident | 256 | 43.48 ms | 15.13 ms | 12.55 ms |
| Nuclear, all coincident | 256 | 48.89 ms | 19.17 ms | 15.86 ms |
| Mixed, spaced grid | 512 | 6.72 ms | 3.87 ms | 3.05 ms |
| Nuclear, spaced grid | 512 | 8.86 ms | 6.09 ms | 4.75 ms |

Across 7,200 optimized measured frames none exceeded 33.33 ms; the largest was 28.33 ms.
This removes the large recurring full-bake spikes in these fixtures, not all possible stalls
or a guarantee of locked 60 FPS: coincident nuclear sources still exceed 16.67 ms in some
frames. The tradeoff is one extra shared lighting atlas (+12 MiB at 256, +48 MiB at 512) and
two display frames of publication latency at the default budget. No source, density or spatial
resolution limit was reduced. These measurements predate promotion to the production SDL path.

Commands, executable hashes, validation logs, captures, frame CSVs, image comparisons,
`results.json` and the before/after `frame-times.png` chart are retained under
`build-linux/cloud-bake-check/`. The original and shader-only intermediate executables are
frozen there as `cloud_stress_baseline` and `cloud_stress_hoist`.
