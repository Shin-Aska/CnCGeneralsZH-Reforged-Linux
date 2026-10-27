# Modern Water performance settings: finalized Linux plan

Investigated 2026-10-26 against `317f23ba`. Final implementation scope:
**Linux only, using the SDL GPU/Vulkan renderer.** The original Windows Iris Xe
report motivated the investigation; it is not the implementation or benchmark
target. This document records the original plan and its subsequent priorities.

**Historical follow-up, 2026-10-26:** the initial controls and Reflection detail
were implemented in the working tree. The user's SW General save reproduced a
large Modern/Classic performance gap, including approximately 13 ms of CPU
mesh construction/filling that those controls left unchanged. See the
[saved-game diagnosis](sw-general-water-performance-diagnosis.md) for the
measurements and their limits.

**Current status, 2026-10-27:** the completed bounded goal is the RTX 4060 saved-view
target of approximately 45 FPS at the same 1080p Modern High / other Ultra
settings, with no quality reduction. The goal implementation now includes
neutral distant-obstacle exclusion, skipping replaced analytic GPU waves while
retaining FFT slope work, direct FFT output into the sampled array, and exact
reuse of prepared terrain-culled/waterline-clipped geometry after live bow
refinement. Three comparable runs measured 19.850, 20.025 and 20.234 ms (median
20.025 ms, 49.94 FPS). Automated, saved-view and camera/vessel checks are complete;
the target is accepted for this stationary comparison, not as a minimum frame rate.
See the
[45 FPS goal report](modern-water-45fps-goal.md). The earlier
[CPU cache validation](modern-water-cpu-cache-validation.md) is historical for
the previous cache pass. Further optimization implementation is stopped;
keep quality tiers, reflection cadence, precision and simulation
frequency deferred. Preserve the working tree when choosing a checkout.

## New-session handoff

Start by reading this plan and checking the current source revision and working
tree. Line references describe the investigation revision and may move. Follow
the user's no-install rule and branch naming preference; do not use a `codex/`
prefix. Use the existing configured dependencies and build where suitable.

Implement and expose the new controls on Linux only. Windows/D3D11 feature
work, Windows UI changes and Windows benchmarking are outside this task.
Shared shader/FFT/helpers may be edited where Linux needs them, but retain
existing defaults/interfaces for non-Linux callers and avoid imposing new
Linux-only settings on other platforms. This supersedes the earlier
cross-platform handoff and its Windows validation requirements.

Deliver the work in these stages:

1. **Initial settings:** Scene reflections On/Off, Wave detail Low/High
   (128/256 internally), and Refraction Full/Simple/Off. Include persistence,
   supported-device handling, UI layout, safe runtime application and actual
   renderer work suppression. Add profiling alongside implementation and keep
   current appearance as the default.
2. **Selected follow-ups:** Reflection detail Low/High is implemented. Material
   cost investigation remains useful evidence for a possible Water shading Low
   option after the CPU reuse pass. Keep reflection update-frequency work
   deferred and restricted to a measured stationary-camera investigation.
3. **Appearance-preserving CPU reuse:** the earlier base-lattice and static
   terrain/offshore/exposure pass is historical and documented in the CPU cache
   validation report. The bounded 45 FPS work extends exact reuse through the
   terrain-culled and waterline-clipped prepared mesh, keyed after live bow
   refinement. Its runtime visual and dynamic validation remains in progress;
   see the [45 FPS goal report](modern-water-45fps-goal.md).
4. **Optional quality tiers/backlog:** consider Water surface detail and Water
   shading only after the appearance-preserving CPU reuse pass. Modern foam and
   caustic controls remain later candidates. FFT storage consolidation, visibility
   preparation, reduced precision and reduced simulation frequency keep the
   conditions in the reviewed backlog. They are not prerequisites for stage 1.

Finalize and test the initial UI before adding later tiers. The combined
Performance preset is a later deliverable, once its component settings exist
and have been validated. Do not silently implement deferred experiments.

Validate on available Linux/Vulkan hardware. If a Linux Intel integrated-GPU
machine is unavailable, proceed with implementation and available builds/tests,
prepare reproducible Linux hardware checks, and report Intel performance
validation as pending. Do not infer Linux Iris Xe gains from the original
Windows report, another GPU or resource arithmetic. Measurement-dependent
shading work must retain its validation gate.

### Recommended agents

Use **GPT-6.1 Sol with High reasoning** for the new session's lead. This is a
task-specific recommendation, consistent with the user's model preferences.

| Role | Model / reasoning | Work |
| --- | --- | --- |
| Lead/integrator | `gpt-6.1-sol`, high | Settings contract, shared shaders/FFT, frame-boundary application, integration and completion |
| Renderer worker | `gpt-6.1-sol`, high | Linux SDL/Vulkan backend changes and relevant regression tests |
| Supporting worker | `gpt-6-luna`, high | Targeted source inventory, agreed labels/tooltips, documentation and test-log summaries |
| Independent reviewer | `gpt-6.1-sol`, high | Resource lifetime, disabled-work behaviour, persistence and Linux regression review; preserve shared interfaces |
| Escalation only | `gpt-6-astra`, high | Unresolved FFT numerical issues, ambiguous rendering contracts or difficult resource/history regressions |

Agree on setting values and shared constants before parallel implementation.
Give each agent explicit file ownership; only one agent edits shared shader,
FFT or settings-contract files at a time. Reuse worker slots for review rather
than launching all roles simultaneously. Luna should not own numerical FFT,
GPU lifetime or temporal-history changes. Escalate a concrete difficult problem
to Astra if Sol's analysis/tests leave it unresolved; Astra is not required at
startup or for every review. These assignments are recommendations, not changes
to the user's global model configuration.

Official guidance checked on 2026-10-26 recommends Sol for demanding multi-step
agents and Luna for narrow repeatable work; the reasoning levels and task split
above are selected for this renderer change. See
[OpenAI subagent guidance](https://learn.chatgpt.com/docs/agent-configuration/subagents)
and [model guidance](https://learn.chatgpt.com/docs/models).

## Investigation summary

The original Windows Iris Xe slowdown prompted this investigation. The Linux
renderer shares substantial water shading and FFT work, but its performance must
be measured separately. Source inspection identifies several costs that remain
at low general graphics settings; no Linux Iris Xe timings or FPS gains were
established in this pass.

## Findings

### Low general settings do not provide a low-cost Modern Water path

- Linux's persistent `WaterReflection` selector has Legacy, SSR, Hybrid and Path
  traced values, with no user-facing Off choice. Keep those existing numeric
  values intact when adding a separate enable switch.
- `WaterFoam` explicitly requires `m_waterStyle == 0`; it only changes Classic
  water. Modern foam has independent rendering/history code. Turning off the
  current foam setting does not disable Modern foam.
- Wave simulation resolution and water mesh density are not quality settings.
- The Linux water backend takes scene colour and available depth snapshots for
  Modern water. The material uses them for transmission, refraction,
  intersection handling and screen-space reflections.

Evidence: `OptionsCatalog.cpp:471`, `W3DWater.cpp:251`, `W3DWater.cpp:1446`,
`W3DWater.cpp:3073`, and `PosixDevice9Draw.cpp:1110` (paths listed below).

### The 256 x 256 grid is the wave simulation, not the visible mesh

`WaterFftSpectrum.h:12` fixes the FFT at 256 x 256, eight butterfly stages per
axis, three wave cascades and six output maps. Eligible flat ocean patches use
this GPU simulation; rivers and unsupported patches can use the analytic path.

The SDL update performs one spectrum pass, sixteen butterfly passes and one
post pass, each writing six RGBA32F targets. Eight additional render passes
generate the mip chains through scratch targets, with 48 scratch-to-mip copies.
It then packs all nine levels of each field into a texture array: 54 further
copies per wave update. Equal wave time is already reused.

Derived resource/work estimates, not GPU timings:

| Quantity | Current 256 | Proposed 128 |
| --- | ---: | ---: |
| FFT stages per axis | 8 | 7 |
| Main FFT passes | 18 | 16 |
| Additional mip-generation passes | 8 | 7 |
| Scratch-to-mip copies | 48 | 42 |
| Final array-packing copies | 54 | 48 |
| Base render-target output per update | 108 MiB | 24 MiB |
| Ocean texture allocation, approximately | 31.03 MiB | 7.76 MiB |

Output estimates count six RGBA32F targets per pass; they exclude texture reads,
mip generation, copies, cache effects and all other water work. Butterfly work
scales to about 22% and field storage to about 25% at 128. These figures are not
an estimate of total FPS improvement. The final water material still performs
largely the same number of per-pixel FFT texture fetches.

Changing one constant is unsafe. Butterfly lookup currently divides by `8.0`;
the filtering shader embeds a nine-entry variance table, `256.0`, and mip/index
limits of eight/seven. Spectrum generation, normalization, filtering tables,
shader identity, mip counts and SDL resources must agree on the active size.
Retain the current defaults for shared-helper callers outside Linux.

Visible geometry is separate: the analytic mesh already caps at 128 cells,
while projected ocean geometry caps at 192 x 128 with approximately one cell
per eight screen pixels, subject to budgets. A separate Low mesh setting should
target CPU mesh building, vertex filling and GPU vertex work.

Evidence: `WaterFftSpectrum.h:12`, `WaterFftSpectrum.h:156`,
`WaterFftFiltering.h:39`, `WaterFftSampling.h:277`, `WaterSurfaceModel.h:238`,
`WaterProjectedMesh.h:78`, `W3DWater.cpp:3478`, and `SdlGpuFrame.cpp:376-504`.

### Reflection Off must prevent the extra scene render

Linux planar reflections are cropped to visible water. With a stationary camera
they already reuse the previous reflection on alternate frames; camera motion
causes updates. Legacy uses a 512 x 512 target, while Hybrid uses 1024 x 1024.
SSR and Path traced modes have different rendering/scheduling requirements.

A new switch must skip reflection rendering and unnecessary allocation, disable
scene-reflection sampling in the material, and invalidate stale content when
re-enabled. Merely setting a reflection strength to zero would leave the extra
scene render in place. The current Legacy shader also samples the mirror three
times when coverage is zero, and the foam material performs additional mirror
samples; gate those consumers explicitly. Preserve sky colour and sun highlights to
keep the surface readable; label this control **Scene reflections** so its
meaning is clear. Also suppress SSR/path tracing when it is off,
while remembering the user's preferred reflection mode.

Evidence: `W3DWater.cpp:1413`, `W3DWater.cpp:1450`, `W3DWater.cpp:3099`,
`W3DWater.cpp:3116`, `engineshader.cpp:1333`, and `WaterFoamShading.h:40`.

### Refraction has two distinct savings levels

The current material computes a displaced lookup and validates neighbouring
colour taps against scene depth. Disabling distortion can remove this work
while keeping clear shallow water. It still needs the scene colour snapshot
for transmission and scene depth for underwater objects/intersections.

An opaque, tinted fallback can omit transmission, seabed detail and caustics,
and avoid the colour snapshot when no other active effect consumes it. Depth
must be retained until its other consumers are also disabled or have a verified
terrain/hull fallback. Linux SSR remains a scene-colour/depth consumer when
scene reflections are enabled. Never remove captures based only on a generic
refraction checkbox.

Evidence: `SdlGpuFrame.cpp:1075`, `PosixDevice9Draw.cpp:1110`, `engineshader.cpp:786`,
`engineshader.cpp:830`, `engineshader.cpp:1393`, and `engineshader.cpp:1485`.

## Proposed user settings

Place the controls together under Modern Water in the Linux UI. Save them in `Options.ini`;
`ModernWater.ini` remains the map material rule file. Preserve the existing
appearance for missing preference keys.

| Control | Choices | Default | Scope |
| --- | --- | --- | --- |
| Scene reflections | On / Off | On | First implementation |
| Wave detail | Low / High | High | First implementation |
| Refraction | Full / Simple / Off | Full | First implementation; Simple=undistorted transmission; Off=opaque tinted water |
| Water surface detail | Low / High | High | Optional after appearance-preserving CPU reuse validation |
| Modern foam | Off / Low / High | High | Later measured improvement; separate from the Classic foam option |
| Caustics | On / Off | On | Optional after measuring material cost; implicitly off with opaque refraction |
| Reflection detail | Low / High | High | Selected follow-up; High preserves current mode-dependent resolution |
| Water shading | Low / High | High | Optional after CPU reuse; measure and validate Low before exposing it |

Candidate preference names are `WaterReflectionEnabled`, `WaterFftResolution`
(enum), `WaterRefractionQuality`, `WaterMeshQuality`, `ModernWaterFoamQuality`,
`WaterCaustics`, `WaterReflectionDetail` and `WaterShadingQuality`. The last
two cover the selected follow-ups. Do not name the FFT option only "grid": it would obscure
the independent geometry cost.

The player-facing **Wave detail** choices are **Low** and **High**. Low uses a
128 x 128 FFT; High uses 256 x 256. Keep these dimensions in implementation
notes, not the menu labels. Suggested tooltip: "Controls fine detail in water
waves. Low improves performance; High shows more detail." Both choices retain
animated Modern water, and High preserves the current default.

A later candidate Modern Water Performance preset is reflections Off, Wave detail Low,
refraction Simple, mesh Low, foam Low and caustics Off. Offer refraction Off for
further savings. These are proposed starting points for Linux integrated-GPU testing, not
validated hardware defaults. Keep the independent controls available and use
Custom when a user changes them. Add this combined preset only after all its
component tiers are implemented and validated; it is not part of the initial
three-control deliverable. Avoid changing the global quality slider's
behaviour until the water tiers have been measured.

## Implementation sequence

Steps 1–5 retain the original settings implementation sequence for reference;
those controls are implemented. The current work is step 6. Step 7 describes
subsequent optional tiers.

1. **Establish a repeatable Linux baseline.** Use a fixed map/save, camera,
   resolution and unit count. Record GPU, Vulkan driver and renderer/backend,
   power mode and whether the laptop is plugged in. Separate shader warm-up
   stalls from sustained performance. Measure stationary and panning views.
   Existing `ZH_WATER_PROFILE` logs CPU phases every 120 frames. Linux's
   `ZH_GPU_TIMING` reports host-side timings; `ZH_GPU_TIMING_SYNC` serializes
   submissions and provides coarse fence-wait measurements that change frame
   timing. Neither establishes per-water-pass GPU time. Establish a supported
   Linux GPU profiling path for reflection, FFT, captures and final-surface work
   using available tooling or validated SDL/Vulkan instrumentation. Do not add
   synchronous GPU waits to production or label CPU submission time as GPU time.
2. **Wire preferences and reflection Off.** Add defaults/accessors/catalog
   rows, supported-device handling, translated labels and persistence checks.
   Resolve an effective water configuration at the render boundary, including
   while the options menu pauses gameplay. Gate reflection rendering, resource
   creation and shader work on the Linux SDL renderer. Restore a fresh reflection on
   re-enable. Use an explicit renderer feature state rather than relying on
   environment variables or material reflection strength.
3. **Parameterize the FFT at 128 and 256.** Use one validated resolution
   descriptor for dimensions, log2 stages, mip levels and spectrum/filtering
   generation. Include quality in affected shader/cache identities, or supply
   matching runtime constants/tables. Replace the single static spectrum variance
   table with resolution-specific tables. SDL's byte-keyed program-cache fast
   lookups must also include quality or be invalidated; changing the generated
   shader text alone is insufficient. Rebuild only the affected GPU resources
   safely between frames; retain the current valid state on allocation failure.
   Keep animation time and gameplay state intact. The current sequential random
   seed maps differently at each resolution, so keeping time does not preserve
   overlapping wave phases. Accept/document a visual transition, or seed by
   stable frequency coordinates and validate the changed spectrum. Verify seeded energy,
   real-valued fields, mip filtering and SDL bindings at each size.
4. **Implement refraction tiers.** Simple bypasses the distortion and tap
   validation while retaining original scene colour and depth. Off selects an
   intentional opaque fallback and skips bed detail/caustics. Determine colour
   and depth capture requirements independently from all active consumers.
   SDL's `Water_Snapshot` currently creates colour and available depth together;
   both `Water_Background` and `Water_Depth` call it. Separate capture demand
   and track readiness/invalidation correctly before attempting depth-only
   operation. Preserve source/extent matching, queued capture ordering, and
   requirements of SSR and other active consumers. Never let a snapshot include
   earlier water draws or reuse stale depth after a target write.
   Compile or specialize disabled material paths so work is removed before
   expensive calculations, rather than multiplied by zero afterward.
5. **Finish the initial UI.** The current Effects page is nearly full
   (roughly 304 of 320 pixels); adding these rows requires a layout change or
   a dedicated Water page. Edit the layout generator and regenerate the WND,
   rather than appending clipped controls. Apply after Accept and preserve
   choices across Modern/Classic switches and unsupported-device runs. Verify
   the first three controls before expanding the UI to follow-up settings.
6. **Appearance-preserving CPU reuse (completed, 2026-10-27).** The
   caches retain the exact base projected lattice before dynamic edits and static
   terrain/offshore/exposure samples. Unit tests and source review cover cache
   keys, invalidation and mutation isolation; native saved-view and camera-motion
   checks are complete. Native moving-vessel and terrain-edit visual checks remain
   outside this pass. Measurements and limits are in
   [CPU cache validation](modern-water-cpu-cache-validation.md).
7. **Optional follow-up: measure and add geometry/material tiers where justified.** Start Low mesh
   at one cell per sixteen pixels and a 64-cell analytic cap. Preserve common
   edges, boundary knots, morphing and budgets. Modern foam quality must gate
   CPU history/atlas producers as well as shader consumers, while retaining
   hull geometry/damping needed for displacement. Caustics and seabed shading
   can have separate gates if they materially affect the measured GPU time.
   This is later work after the CPU reuse pass; Reflection detail is already
   implemented, and Water shading remains subject to measurement. Add the
   combined preset only after its component settings pass validation.

Settings integration: `GlobalData.h:328`, `GlobalData.cpp:975`,
`OptionsCatalog.cpp:122`, `OptionsCatalog.cpp:471`,
`OptionsMenu.cpp:1124`, `OptionsMenu.cpp:1216`,
`Tools/optionsmenu_layout.py:169`, `Tools/optionsmenu_layout.py:393`,
and `Data/Patch.str`.

## Reviewed optimization backlog

Reviewed again on 2026-10-26 against source, tests and Git history. Several
current costs are deliberate correctness or compatibility tradeoffs. The tasks
below preserve those requirements; they are not instructions to remove code
because it looks expensive. The initial controls and CPU reuse pass are now
implemented and measured. Integrated-GPU performance still needs further work.
Measure follow-up gains at both Wave detail levels:
the benefit of eliminating duplicate storage is smaller after selecting Low.

The user selected these three follow-ups on 2026-10-26:

| Selected work | Status | Required behaviour |
| --- | --- | --- |
| Lower reflection resolution | Plan **Reflection detail: Low / High** | High retains current resolution; preserve filtering, allocation fallback and refresh safeguards |
| Lower reflection update frequency | Retain as a **deferred investigation** | Explore stationary-camera reuse only after measurement; keep immediate refresh for movement and invalidations |
| Simplify pixel shading | **Measure first**, then validate **Water shading: Low / High** | Keep current High lighting; add Low only with measured benefit and acceptable visual error |

These selections add to the initial Modern Water controls. A reflection update
rate control is not yet specified; the deferred investigation must establish
safe behaviour before a user-facing setting is designed.

### 1. Measure material costs before choosing further simplifications

Selected follow-up: measure first; proposed menu label **Water shading**.

- [ ] Add isolated GPU measurements for the final water material and controlled
  comparisons for foam lighting, sky/reflection shading, terrain shadow taps,
  seabed detail and caustics. Include open water, shorelines and boat-heavy views.
- [ ] If a branch is a material bottleneck, propose an explicit Low-quality
  approximation and validate its visual error. Preserve the existing High path.

**Why the current work exists:** `WaterFoamEnvironment.h` builds a cached table
from 65,536 sky samples at shader-source preparation time, then evaluates 48
precomputed directional terms at eligible foam pixels. These are not 48 texture
reads or a fresh sky integration at every pixel. The approximation preserves
nonnegative lighting and avoids adding extra energy or applying the sun twice.
The production call sites are conditional on vessel/coastal foam and offshore
weight; it is not a universal cost at every water pixel. Tests compare it with
independent sky integrals, including rotated and inverted normals, HDR colour,
zero-foam endpoints and shadow increments.

**Acceptance:** a measured GPU improvement plus an explicit image/error budget;
retain positivity, colour behaviour, shadow increments and exact disabled
endpoints. Do not simply lower the bin count or replace the material with a
constant ambient colour as a supposedly equivalent optimization.

Evidence: `WaterFoamEnvironment.h:90`, `WaterFoamEnvironment.h:127`,
`engineshader.cpp:1643`, `Tests/test_shader_sdl.cpp:1393`,
`Tests/test_sdl_gpu_frame.cpp:4389`; introduced with `b0751114f`.

### 2. Consolidate FFT output storage

- [x] Direct FFT output into the sampled six-layer array on Linux SDL/Vulkan,
  removing the 54 final packing copies. Existing scratch mip-generation copies,
  resource fallback and consumers are retained. Runtime visual/dynamic
  validation and limitations are recorded in the [45 FPS goal report](modern-water-45fps-goal.md).
- [ ] Verify SDL array-layer render attachments, matching vertex/coastal sampler
  declarations, resource ordering and the portable mip-generation algorithm.
  Keep this conditional architectural work outside the initial settings scope.

**Why the duplicate exists:** commit `a4984563` packed the six FFT maps into one
fragment binding so water could also bind sun, smoke, foam and coastal inputs
within SDL's sixteen texture slots. Vertex and coastal code retained individual
maps. This purpose is documented in
`docs/sdl-sun-shadows.md:803`; line 976 already leaves duplicate storage as
separate architectural work. The copying serves a binding contract today.

**Room for improvement:** investigate consolidating the underlying storage
while preserving the single fragment binding. Validate the vendored SDL GPU
API's array-layer attachment and sampling contracts before choosing a layout.
Keep format capability checks, matching vertex/coastal code and safe mip
generation. Feasibility on another graphics API does not establish SDL/Vulkan
correctness or performance.

At 256 this could eliminate about 8 MiB of duplicate storage and 54 final
packing copies containing about 8 MiB of payload per update. At 128 the duplicate
storage/payload is about 2 MiB and the final packing count is 48. Payload is not
total bus traffic or an FPS prediction. SDL additionally performs 48 scratch-to-
mip copies at 256; these are distinct from the 54 packing copies. Its scratch
passes intentionally avoid reading an attached texture, even another mip, and
avoid relying on Vulkan float linear-blit capabilities. Preserve that safe mip
path until an independently validated replacement exists.

**Acceptance:** compare every layer/mip and fractional wrapped sample against
the current numerical reference; verify vertex displacement, coastal inputs,
pause, resize/reset, allocation fallback and queued readers. Check read/write
aliases across all sampling stages. Shader compilation alone is insufficient.

Evidence: `PosixDevice9Draw.cpp:1122`, `SdlGpuFrame.cpp:339`,
`SdlGpuFrame.cpp:473`, `SdlGpuFrame.cpp:568`,
and `Tests/test_sdl_gpu_frame.cpp:2146`.

### 3. Skip only proven unnecessary render preparation

- [ ] Measure recurring preparation separately from cached texture creation.
  Separate persistent history updates from render-only mesh/packing/upload work,
  then bypass measured render-only work when conservative bounds prove no
  Modern water can draw.

**Why a blanket skip is wrong:** client updates explicitly advance every
fixed-world shoreline level while the camera is away. Missing more than eight
shore-history ticks causes decay without new foam births/diffusion; resuming at
the right wave time does not reconstruct the skipped history. Offscreen boats
also influence visible water, and vessel motion observations preserve travelled
paths. The foam history treats observation gaps over 0.5 seconds specially.
Skipping all hull preparation can lose those observations. Bed/optical texture
work is already substantially cached, so its call count does not establish a
large recurring cost.

**Required scope:** retain clock, shoreline history, vessel observations and
map invalidation. Include horizontal/vertical displacement bounds, rivers,
all elevations, nonplanar patches and analytic fallback in visibility tests.
Do not reuse `visibleFlatPatches`: it controls projected sample caching and
excludes valid water paths. Preserve map-wide mesh counts and shared-edge
budgets so crossing a viewport boundary cannot change seams/topology.

**Acceptance:** compare pan-away/pan-back and offscreen-boat cases with an
always-updated reference, including long absences, pauses, teleports, level/map
changes and near-water cameras. A CPU improvement must be measured. General
history suspension/catch-up remains a separate research problem.

Evidence: `W3DWater.cpp:691`, `W3DWater.cpp:880`, `W3DWater.cpp:2896`,
`W3DWater.cpp:3440`, `W3DWater.cpp:3474`, `W3DWater.cpp:4608`,
`WaterShoreHistory.h:217`, `WaterFoamHistory.h:12`,
`Tests/test_water_grid.cpp:1620`, `Tests/test_water_grid.cpp:4030`.

### 4. Benchmark an optional lower reflection resolution

Selected follow-up: **Reflection detail: Low / High**.

- [ ] Evaluate a user-facing **Reflection detail: Low / High** choice while
  retaining current mode-dependent target sizes at High. Keep this separate
  from Scene reflections On/Off and the Linux reflection-mode selector.

**Why current sizes exist:** Linux Legacy planar water uses 512; Hybrid uses
1024. Commit `d5c284a7` deliberately increased Hybrid resolution and
added coverage-aware filtering. A smaller target trades visual detail for
less pixel/depth work; scene traversal and draw submission can remain similar.

**Acceptance:** measure GPU/CPU benefit and check thin objects, shoreline edges,
moving boats and camera pans. Preserve coverage filtering, the old usable
target on allocation failure, queued-reader ownership and immediate refresh
after target replacement. Keep current defaults.

Evidence: `W3DWater.cpp:1413`, `W3DWater.cpp:1422`,
`WaterScreenReflection.h:26`, `Tests/test_water_grid.cpp:5800`.

### 5. Defer broader reflection throttling

Selected follow-up: retain deferred status and the stationary-camera restriction.

- [ ] Only if reflection remains expensive, evaluate a bounded age limit for
  stationary-camera planar captures. Preserve refresh during camera movement
  and force refresh on projection/viewport, visibility, selected water plane,
  mode/style, map, target and device changes. Keep path-tracing scheduling separate.

**Why not unconditional every-N-frame updates:** commit `12e61fce9` already
introduced cropped captures and alternate-frame stationary reuse. An unchanged
camera can reuse aligned projection; a moving camera cannot. Longer reuse can
show swimming reflections, uncovered crop edges and delayed boats. Keeping old
captures during motion would need a separate reprojection/disocclusion design.
The existing single-reflection-plane limitation must not be worsened by stale
plane selection. Do not add a broad update-frequency slider in the first pass.

Evidence: `W3DWater.cpp:1405`, `W3DWater.cpp:3054`,
`W3DWater.cpp:3109`, `W3DWater.cpp:3121`.

### 6. Keep reduced precision and simulation frequency as research

- [ ] Revisit half-precision storage only after profiling the 128 tier and
  storage-consolidation prototype. Establish numerical error and range bounds
  for intermediate FFT fields, final slopes/displacement and foam signals.
- [ ] Revisit reduced simulation frequency only with a coherent interpolation
  design covering geometry, shading, shoreline/boat interaction and pause/time
  changes. Benchmark its added storage/sampling cost as well as saved updates.

The current full-precision path and per-time update are reference behaviour;
the review found no evidence establishing that reduced precision is visually
safe or that skipped updates would be smooth. These are conditional experiments,
not committed default changes or user settings for the first implementation.

## Validation and completion criteria

- Preference round-trip, invalid-value handling, missing-key defaults, Accept
  versus Cancel, restart persistence, and supported/unsupported-device UI.
- Reflection Off produces no reflection render or trace work; re-enabling
  refreshes correctly even with a stationary camera.
- Both FFT sizes pass spectrum/energy/filtering tests and CPU-reference GPU
  comparisons. High retains existing visuals; Low remains stable at shorelines,
  river junctions, boat hulls and multiple water heights.
- Refraction tier tests cover shoreline edges, submerged objects, unavailable
  snapshots and Linux reflection dependencies. Capture counters verify which
  copies actually disappeared.
- Repeated live changes, resize, device reset, paused menus and fallback paths
  show no stale textures, resource leaks, invalid bindings or black water.
- Compile and run the Linux SDL shader/GPU checks and validate gameplay on
  Linux/Vulkan. Keep existing shared-helper defaults/interfaces intact for
  non-Linux callers; Windows implementation and runtime testing are outside
  this task. Results from another Linux GPU do not establish Intel performance.
- Compare each setting individually and then the combined preset, using the
  same open-water, shoreline/structures, boat-heavy and water-offscreen views.
  Report median and 95th-percentile frame times, GPU stage times and CPU water
  timings after warm-up. State measured visual tradeoffs and frame-time gains;
  do not infer them from resource arithmetic.

Existing test homes: `Tests/test_gameengine.cpp` for preferences,
`Tests/test_shader_sdl.cpp` for spectrum/filtering and shader compilation,
`Tests/test_sdl_gpu_frame.cpp` for GPU field checks, `Tests/test_water_grid.cpp`
for geometry. Use the existing Linux configured build and game/preview tools.
The options layout selfcheck passed during this investigation (57 catalog rows).
No game build, runtime test or performance benchmark was run during planning.

## Source path reference

- `GlobalData.h`: `GeneralsMD/Code/GameEngine/Include/Common/GlobalData.h`
- `GlobalData.cpp`, `OptionsCatalog.cpp`:
  `GeneralsMD/Code/GameEngine/Source/Common/`
- `OptionsMenu.cpp`:
  `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp`
- `W3DWater.cpp`:
  `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`
- `WaterFftSpectrum.h`, `WaterFftFiltering.h`, `WaterFftSampling.h`,
  `WaterSurfaceModel.h`, `WaterProjectedMesh.h`, `WaterScreenReflection.h`,
  `WaterFoamShading.h`, `WaterFoamEnvironment.h`, `WaterShoreHistory.h`,
  `WaterFoamHistory.h`:
  `GeneralsMD/Code/Libraries/Include/Platform/`
- `engineshader.cpp`:
  `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/`
- SDL backend:
  `GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp`
  and `PosixDevice9Draw.cpp`, `PosixDevice9Timing.cpp`, `SdlGpuFrameBatch.cpp`
  in the same directory.
