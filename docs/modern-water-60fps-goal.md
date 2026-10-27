# Linux Modern Water: 60 FPS goal

Goal completed 2026-10-27, starting at checkpoint `736a8d54` after the completed
[45 FPS goal](modern-water-45fps-goal.md). All three final RTX 4060 runs exceed
60 FPS average at the same quality settings: **87.87, 76.17 and 82.36 FPS**.
The median run mean is **12.142 ms / 82.36 FPS**. The scoped visual, automated,
camera and moving-vessel checks are complete. Optimization scope stops here.

## Target and limits

Reach at least **60 FPS average / 16.67 ms mean frame time** on RTX 4060 in the
same SW General saved-camera comparison: 1920×1080, Modern High, Hybrid scene
reflections, Full refraction, High reflection detail and the same other Ultra
preferences (`MSAA=3`). Preserve appearance, animation and required updates;
lowering quality does not count. Prefer focused, measured optimizations.

Use the established serial, uncapped/offscreen methodology: private copies of
save/preferences, 180 frame intervals after 25 seconds from first Present,
VSync and Dynamic LOD off. Compare at least three final runs and report each
mean, their median, and frame-time tails. This target is a saved-view average,
not a minimum frame rate or a claim of 60 FPS during all camera/gameplay cases.

Check automated tests and image equivalence plus camera and moving-vessel
behavior. Measure UHD 770 separately as a secondary result. Do not extrapolate
from this desktop iGPU to Iris Xe configurations. Install nothing, preserve
user changes, and do not push or replace the installed game. Stop adding
optimization scope when the target is reliably reached and validated.

## Implemented changes

The checkpoint's earlier three-run median was 20.025 ms / 49.94 FPS, with
approximately 5.1 ms in CPU water vertex filling and about 4.8 ms sampled wave
work. These profile components overlap and must not be added together as
independent GPU timings.

The first change is a dedicated scalar river-height evaluator. GCC 16
Release retains slope calculations when callers read `.height` from the full
24-mode evaluator. The new scalar path preserves every height operation and
mode order, following the existing ocean-height specialization. Standalone
measurements show 23–28% less time per river evaluation, with bitwise parity.
The ocean height path also gains a scalar return, retaining identical warp,
mode order and envelope arithmetic while reducing compiler stack/register
spill work. Its focused benchmark improves 7–10%. Together these small
changes did not reliably reach 60 FPS: candidate runs measured 16.673 and
18.970 ms. They were not accepted as success.

The main optimization omits CPU height calculations only for
direct interior vertices with final FFT blend weight exactly one, with no
boundary, collar or transition dependency. Damping, optical fields, hulls and
all other vertex metadata remain current. Omitted height placeholders never
enter the shared XYZ sample cache; analytic edge/transition requests continue
to obtain complete heights.

A Linux backend preparation scope establishes actual FFT resources and a valid
generated vertex shader before CPU filling. Failure keeps the complete CPU
fallback. A successful scope retains the actual shader/field decision through
all index batches and validates resource generation, phase, target and vertex
state. Index, cull, blending and fill-mode changes remain valid. An incompatible
state change is a contract violation and refuses the draw rather than silently
using missing CPU heights. Successful device resets invalidate the scope even
if replacement surfaces reuse old addresses; failed resets preserve it. The
normal synchronous patch scope ends after its draws; subsequent patches and
fallbacks are independent. No scope violations appeared in the live checks.

The diagnostic `ZH_WATER_CPU_FFT_ELISION=0` retains the full CPU path. The feature
is enabled by default on Linux; other platforms retain their existing filling
path. Simulation frequency, FFT resolution/precision, shader material and user
quality settings are unchanged.

In the target view, the final code omits approximately 11,828 CPU heights per
frame. Late profile batches show CPU vertex filling at 1.19–1.23 ms, compared
with approximately 5.0–5.1 ms in the fresh checkpoint runs. Total water render
time is 3.46–4.08 ms versus 7.67–7.87 ms. FFT preparation now occurs before
filling and is explicitly reported as `prepare_ms` (1.59–1.96 ms in those final
batches); its old submission cost was not simply removed from the accounting.
Host timings include GPU backpressure and some counters overlap. They are not
independent GPU measurements to add together.

The scope is focused: scalar CPU height evaluators, the guarded Linux draw
preparation/height omission, profiling and regression tests. No new quality
tier, material simplification, simulation cadence change or shader edit was
needed for this goal.

## Measured result

All cases use the method and settings above. FPS is `1000 / mean frame time`;
the median below is the median of three run means, not the per-frame p50.

| Final run | Mean frame | FPS from mean | p50 | p95 | p99 | Worst |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `nv-high-final-1` | 11.381 ms | 87.87 | 10.716 ms | 18.298 ms | 21.875 ms | 21.983 ms |
| `nv-high-final-2` | 13.128 ms | 76.17 | 12.601 ms | 21.396 ms | 24.353 ms | 28.104 ms |
| `nv-high-final-3` | 12.142 ms | 82.36 | 11.382 ms | 18.819 ms | 21.835 ms | 22.354 ms |

Fresh checkpoint controls are essential: the same preserved executable that
previously measured 49.94 FPS ran faster in this session, before the new changes.

| Checkpoint control | Mean frame | FPS from mean | p95 |
| --- | ---: | ---: | ---: |
| `nv-high-baseline-1` | 17.155 ms | 58.29 | 24.762 ms |
| `nv-high-baseline-2` | 16.827 ms | 59.43 | 24.705 ms |
| `nv-high-baseline-3` | 16.568 ms | 60.36 | 24.221 ms |

Comparing median run means, **16.827 → 12.142 ms** is approximately **27.8% less
frame time / 38.6% more FPS**. Do not attribute the entire historical 50 → 82 FPS
difference to this patch: the baseline session variation is unresolved. Live
simulation also means the timing windows contain differing unit/effect states,
not identical replay frames. No simultaneous builds or GPU tests ran during
the benchmarks.

An additional control, `nv-high-elision-off`, used the candidate with the new
CPU omission disabled but retained the scalar evaluators. It measured
15.972 ms / 62.61 FPS, versus 11.265–11.726 ms with omission enabled in the
three initial candidate runs. Those four runs used the same intermediate
binary, before the final reset-invalidation safeguard; they support the source
of the gain but are not substituted for the three final runs above.

The goal is an average in the established stationary view. Final p95 values
remain above 16.67 ms, and the camera-pan probe measured **23.653 ms / 42.28 FPS**
(p95 31.635 ms). Moving views still rebuild geometry and can contain more scene
work. This is not a 60 FPS minimum, a windowed frame-rate guarantee, or a claim
that every map/view now reaches 60.

### Intel secondary comparison

One same-method UHD 770 pair uses Modern Low (reflections Off, Wave detail Low,
refraction Off, Reflection detail Low), with other graphics still Ultra.

| Case | Mean frame | FPS from mean | p95 |
| --- | ---: | ---: | ---: |
| `intel-low-baseline` | 91.322 ms | 10.95 | 106.613 ms |
| `intel-low-final` | 91.966 ms | 10.87 | 109.609 ms |

This pass provides **no demonstrated additional Intel FPS benefit**. The small
difference in a single pair is not a repeatable regression estimate. Host
profiles still show roughly 50 ms of GPU flush/backpressure; moving that wait
from submission into preparation does not remove it. The earlier 45 FPS pass's
Intel improvement remains historical evidence, not a result of this patch.
The integrated-GPU bottleneck is unresolved; no Iris Xe result is inferred.

## Verification

- Release build succeeds. `test_water_grid`: **192 tests, 10,346,229 checks,
  zero failures**. Scalar heights are compared bitwise with full evaluators and
  the previous ocean template across coordinates, negative/positive times,
  blend endpoints, clamping and nonzero offshore derivatives.
- `test_shader_sdl`: **46 tests, 692,362 checks, zero failures**. GPU shader
  sources did not change in this goal.
- Final `posix_gpu_draw_selfcheck` passes on both NVIDIA and Intel. Actual
  depth readback distinguishes FFT height replacement from complete analytic
  fallback. Tests exercise allocation failure, optical/height gates, multiple
  batches, allowed fill/blend changes, nested/stale tokens, phase/resource
  changes, failed-reset preservation and successful double-reset invalidation.
- Native fixed-frame SW General captures compare checkpoint, candidate with
  CPU caches off, and default candidate. All **144,950 previously selected
  water pixels are RGB identical** across every pair, both with normal FFT and
  with `ZH_WATER_FFT=0`. This checks fallback appearance as well as the normal
  path. Whole frames contain unrelated animated object/shadow/UI differences;
  this is a partial water comparison, not whole-surface pixel parity. These
  images precede only the reset-invalidation guard, which does not change
  normal rendering. Saved-view inspection found no water regression.
- Twenty pan/return cycles complete with exit code zero. Prepared mesh counters
  reach 120 misses/120 frames during motion, then 120 hits/120 after settling;
  FFT preparation remains active and no scope violation appears. The final
  post-pan water image was inspected. Equal-duration opposite inputs do not
  assert an exact camera-coordinate return.
- The final PT-boat fixture records **408 valid hull observations** across
  distinct logic frames and over **484 world units of actual travel**. A GPU
  bank records a submitted 12-vertex hull with nonzero velocity. Bow refinement
  reaches 697 added vertices / 1,394 triangles; the prepared mesh is rebuilt
  during motion. Six before/during/stop/pan captures complete with exit code
  zero; moving and return images were inspected. This is a functional
  1280×720 High/MSAA-off check, not an FPS run or bitwise animation comparison.
  The stop request is recorded; zero post-stop velocity is not asserted.
- No Vulkan validation errors or prepared-FFT scope refusals appeared in the
  final live runs. The broader Intel GPU-frame suite was not rerun in this
  CPU-focused pass: its 489 previously baselined numerical failures remain
  documented in the [45 FPS report](modern-water-45fps-goal.md). Passing the new
  device-contract checks does not imply that entire suite is clean.
- `git diff --check` and the build-fingerprint manifest check pass. No
  dependencies were installed, and original save/preferences were preserved.

## Reproducibility

Ignored local artifacts: `build-linux/water-60fps-validation/`. The preserved
checkpoint executable is `baseline/generals`, SHA-256
`1cd00ad47d09f788192b78dccfe76cb096f3df8c393698143980550444646d0f`.
The runner scripts preserve the established assets, private-profile settings
and successful offshore vessel route. Use the bundled Python runtime for
the visual matrix's existing NumPy dependency; no installation is needed.

Hardware/software: Intel Core i7-12700, NVIDIA GeForce RTX 4060, NVIDIA driver
595.99.02, Intel UHD 770, Ubuntu 26.04.1 LTS, GCC 16 Release (`-O3 -DNDEBUG`).

Final executable: `build-linux/generals`, SHA-256
`5004740446f9e1da6a63480d0f7665deeaf76fcfbb5dd400e3fdad57089130a5`.
The installed game was not replaced. Changes are local and uncommitted; nothing
was pushed.

The local artifact directory contains `summary.json`, `candidate-build.json`,
per-run manifests/logs/screenshots, `parity-elision-native/`,
`parity-fft-disabled/`, `moving-boat-final/`, CPU compiler/microbenchmark evidence
and build/test logs. Camera artifacts are in the adjacent
`build-linux/water-cpu-cache-validation/60fps-final-camera/` directory. Manifests
record executable/save hashes, private settings, environment and commands.

Reproduction examples from the repository root, using fresh output names:

```sh
python3 build-linux/water-60fps-validation/run_scene.py --cache on --gpu nvidia --water high --name fresh-high
python3 build-linux/water-60fps-validation/run_scene.py --cache on --gpu nvidia --water high --name fresh-no-elision --env ZH_WATER_CPU_FFT_ELISION=0
python3 build-linux/water-60fps-validation/capture_parity.py --name fresh-parity --timeout 90
python3 build-linux/water-60fps-validation/capture_parity.py --name fresh-fallback --timeout 90 --env ZH_WATER_FFT=0
python3 build-linux/water-60fps-validation/run_moving_boat.py --name fresh-boat
```

Run one GPU process at a time. These harnesses and preserved binary are ignored
local artifacts; they are not distributed game files. Reflection cadence,
precision, simulation frequency and optional lower-quality tiers remain
deferred. No further optimization is required to close this bounded goal.
