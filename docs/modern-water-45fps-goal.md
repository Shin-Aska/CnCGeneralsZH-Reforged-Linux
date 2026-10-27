# Linux Modern Water: 45 FPS goal

Goal completed 2026-10-27. The RTX 4060 target was reached in three consecutive
runs, with the scoped visual, automated and motion checks completed. Further
optimization work stops here; the limitations below remain explicit.

This is the historical 45 FPS result. The later goal and fresh checkpoint
comparison are recorded in the [60 FPS goal report](modern-water-60fps-goal.md).

## Completion target

Reach at least approximately **45 FPS**, or **22.2 ms mean frame time**, on the
RTX 4060 in the user's SW General save at 1920×1080. Keep Modern Water High and
the same other Ultra preferences used by the previous comparisons. Preserve the
current High appearance; changing graphics settings does not satisfy this goal.

Use the existing uncapped/offscreen methodology: 180 measured frame intervals
after 25 seconds from first Present, private save/profile copies, no simultaneous
game/GPU benchmarks. Confirm the result over at least three comparable runs and
report individual means plus their median. Check relevant automated tests, water
images and camera/dynamic cases before accepting a change. Check Intel UHD 770
impact separately; this target does not claim 45 FPS on Intel or every game view.

The starting point is the completed CPU reuse pass: median RTX mean frame time
26.236 ms (38.1 FPS), approximately 11.4 ms CPU mesh/fill, Intel Low approximately
149 ms. Its exact methodology and limits are recorded in
[CPU cache validation](modern-water-cpu-cache-validation.md).

## Bounded work

1. Remove provably neutral distant-obstacle work for coastal CPU samples.
2. Measure and remove redundant GPU analytic-wave calculations where FFT fully
   replaces them, preserving derivative behavior at mixed shoreline pixels.
3. Investigate safe direct FFT output into the sampled array, preserving mip
   generation, resource hazards, vertex/coastal consumers and fallback behavior.
4. Evaluate unnecessary preparation while water is invisible. Current review
   finds that animation/history maintenance must continue, and the remaining
   safe savings do not help this visible-water benchmark; defer invasive changes
   unless the preceding work leaves a relevant measured need.

The first three changes reached a median 42.2 FPS over three runs (23.686,
22.013 and 23.935 ms); an earlier 46.9 FPS run was not accepted on its own.
The remaining profile justified one additional bounded change: exact reuse of
terrain-culled, rest-waterline-clipped geometry after live bow refinement. That
closed the measured gap. No further optimization implementation is planned for
this goal.

Optional lower-quality geometry/shading tiers are separate from meeting the
High target. Reflection update cadence, half precision and reduced simulation
frequency remain deferred. Stop expanding scope once the target is reliably met
and validated. If safe improvements cannot reach it, report the measured blocker
and remaining options instead of silently lowering quality or promising 60 FPS.

## Local artifacts

Ignored artifacts are under `build-linux/water-45fps-validation/`, including the
pre-goal executable and source snapshots, commands, per-run manifests and logs.
No dependencies or game installation updates are authorized by this goal.

## Implemented changes

- **Coastal obstacle filtering:** extend the existing conservative obstacle bins
  and distance rejection to zero-offshore CPU samples. Omitted obstacles have an
  exact neutral attenuation; the original ordered narrow phase remains for
  candidates, and failed/outside queries retain the full list. This brought
  sampled damping from about 3.5 to 0.65 ms in the target view.
- **Analytic wave work:** when the FFT completely replaces analytic slopes
  (weight exactly one, spatial weight derivative exactly zero), bypass the
  replaced 24 legacy waves and 14 non-swell ocean modes. Keep the six swell
  modes and warp phases live to preserve derivative behavior across mixed
  shoreline pixels. Shader inspection confirms the branches bypass the work;
  caustic evaluation is unchanged.
- **FFT output storage:** render directly into the six-layer sampled array.
  Vertex and coastal consumers use the matching layer bindings and shader
  cache variants. Eliminate six duplicate textures and 54 final packing copies
  at 256, or 48 at 128. Retain 48/42 scratch-to-mip copies respectively to avoid
  sampling attached storage. Capability/allocation fallback retains the old
  path. Production profile logs confirm array mode at 256.
- **Prepared geometry reuse:** compare the entire mesh after live bow refinement,
  water level and culling/support gates, then reuse the exact terrain rejection,
  rest-waterline clipping and vertex-compaction result. A single entry bounds
  retained input/output allocation to 32 MiB; the target view uses 2,879,034
  bytes. Terrain/optical, map, polygon, resource and style invalidations clear
  it. Caller copies prevent cache mutation. Animated filling, hulls, obstacles
  and wave time continue to update. Mesh preparation fell from about 3.8 to
  0.55–0.60 ms in the stationary target view.

These changes are enabled by default on Linux. Diagnostic environment controls
retain the old paths for comparisons: `ZH_WATER_SHORE_OBSTACLE_BINS=0`,
`ZH_WATER_ANALYTIC_ELISION=0`, `ZH_WATER_FFT_ARRAY_OUTPUT=0`, and
`ZH_WATER_PREPARED_MESH_CACHE=0`. `ZH_WATER_CPU_CACHE=0` also disables the prepared
cache and the earlier base/static caches. These are developer controls, not new
quality settings. High density, FFT precision/resolution, shading, reflection
refresh rules and the existing user preferences are preserved.

## Measured result

Same SW General save/camera, 1920×1080, Modern High, Hybrid reflections,
Full refraction, High reflection detail, other Ultra preferences including
`MSAA=3`. Each run uses private save/preferences, VSync and Dynamic LOD off,
uncapped offscreen rendering, 180 frame intervals after 25 seconds from first
Present. Runs are serial, with no concurrent builds or GPU tests.

| Final run | Mean frame time | FPS from mean | p50 | p95 |
| --- | ---: | ---: | ---: | ---: |
| `prepared-nv-high-1` | 20.025 ms | 49.94 | 19.131 ms | 28.134 ms |
| `prepared-nv-high-2` | 19.850 ms | 50.38 | 18.500 ms | 28.247 ms |
| `prepared-nv-high-3` | 20.234 ms | 49.42 | 19.050 ms | 28.150 ms |

Median run mean: **20.025 ms / 49.94 FPS**, compared with the previous CPU-cache
pass's 26.236 ms / 38.1 FPS. This is approximately 24% less frame time and 31%
more FPS. All three final means clear 22.2 ms. These are whole-frame host
intervals, not isolated GPU time. Live simulation introduces run variation.
The result is an average for this view, **not a 45 FPS minimum**, a 60 FPS claim,
or a guarantee for every map/camera. The final runs still have p95 around 28 ms
and occasional longer frames.

One same-method Intel UHD 770 Modern Low pair, with other settings still Ultra,
changed from **158.334 to 104.322 ms** (6.32 to 9.59 FPS). That is a substantial
reduction but remains slow. This is a single comparison and does not establish
Iris Xe performance. The RTX target does not resolve all integrated-GPU costs.

## Verification

- Release build succeeds. Final water suite: **191 tests, 10,286,229 checks,
  zero failures**, including exact cache parity through real culling, clipping
  and compaction, key changes, invalidation, caller isolation and memory bounds.
- NVIDIA shader, program-cache and GPU-frame suites pass. The shader suite has
  46 tests and 692,362 checks. FFT tests cover layers/mips, wrapped fractional
  samples, queued readers, coastal/vertex consumers and allocation fallback;
  both array and original output paths pass. Swell/derivative comparisons add
  120 checks; SPIR-V validation passes.
- Intel passes the new array-output and swell parity checks. Its complete GPU
  suite still reports 489 failures: 480 strict paired-FFT zero checks, eight
  sky convergence checks and one cosine-lighting tolerance check. The same
  binary with both new GPU paths disabled produces the identical failure
  sequence and numeric diagnostics. No tolerances were weakened; this is not
  reported as a clean Intel suite.
- Twelve controlled GPU image pairs (24 captures: High/Low, several wave times,
  clean scene and recorded boat scene) are RGB identical between old and new
  GPU paths. These are controlled material/FFT checks, not native CPU-mesh
  coverage.
- Native SW General captures at logic frame 106000 compare the pre-goal binary,
  final cache-disabled path and final default path. All 144,950 pixels in six
  previously selected water regions are RGB identical across all three pairs.
  Whole frames differ in animated objects, shadows and UI; this is a partial
  water comparison, not whole-surface pixel parity. Visual inspection found no
  water regression in the saved view.
- Twenty camera pan/return cycles complete with a clean exit. Prepared-cache
  counters show 120 misses out of 120 frames while moving, then 120 hits out of
  120 after settling. No Vulkan errors were logged. The moving-camera probe
  averages 30.482 ms (32.8 FPS); the 45 FPS target applies to the established
  stationary comparison, not camera motion. Moving views still pay rebuilding
  costs and can contain more scene geometry.
- Vessel check: the first fixture crossed onto the shoreline and failed GPU
  hull admission; it was not accepted as a pass. A corrected open-water
  PT-boat route completes with 309 valid hull observations, over 483 world
  units of actual travel, a captured moving hull packet and active bow
  refinement (up to 815 added vertices / 1,630 triangles / one bow region).
  Prepared-cache counters show 120 misses out of 120 frames while that refined
  geometry moves. Six before/during/stop/pan captures, clean exit and no logged
  Vulkan errors complete the functional check. This is not a bitwise animated
  image comparison against the baseline. The stop request and pan inputs are
  recorded; zero post-stop velocity and exact camera coordinates were not
  independently asserted by this fixture.
- `git diff --check` and the build-fingerprint manifest check pass. Existing
  uncommitted settings work is preserved; no dependencies were installed.

Final executable: `build-linux/generals`, SHA-256
`1cd00ad47d09f788192b78dccfe76cb096f3df8c393698143980550444646d0f`.
Pre-goal baseline SHA-256:
`47488b4ad8aa98cd8deed69cd75bfd9a9aa32e3f7bfefbcd3a9cb2f854d57ca3`.
The installed game and original save/preferences were not replaced.

Reproduction examples from the repository root:

```sh
python3 build-linux/water-45fps-validation/run_scene.py --cache on --gpu nvidia --water high --name fresh-comparison
python3 build-linux/water-45fps-validation/capture_parity.py --name fresh-parity --timeout 90
python3 build-linux/water-45fps-validation/run_moving_boat.py --name fresh-boat-check
```

The artifact directory also contains per-run manifests/logs/screenshots,
`target-performance-summary.json`, `final-visuals/summary.json`, build/test logs,
shader compiler evidence, Intel failure triage and the preserved baseline binary.
Native comparisons are in `parity-final-native-parity/`; the successful boat
check is in `moving-boat-offshore/`. Camera logs are in the adjacent
`build-linux/water-cpu-cache-validation/prepared-cache-camera/` directory.
