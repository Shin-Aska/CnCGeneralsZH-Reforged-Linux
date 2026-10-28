# SW General: Modern Water Low follow-up

Investigated on Linux/SDL GPU/Vulkan, 2026-10-26, after the initial water settings
implementation in the working tree on `317f23ba`.

The reported gap was reproducible in the October 26 runs. The settings removed
real work, but the lowest selections retained expensive CPU surface construction
and most of the water material. No ignored-setting or shader-cache correctness
defect was found.
The October 26 investigation changed documentation only; it did not implement a
new tier. Its measurements below describe that revision, before the CPU caches.

**Current status, 2026-10-27:** the RTX 4060 saved-view goal completed at a median
82.36 FPS across three run means (11.381, 13.128 and 12.142 ms), keeping Modern
High, 1080p and the same other Ultra settings. Fresh checkpoint controls measured
17.155, 16.827 and 16.568 ms (59.43 FPS median); the earlier 49.94 FPS result
from the 45 FPS report is historical and is not a direct control. No quality
setting changed. The final work adds scalar CPU height evaluators and skips CPU
analytic height only where the prepared GPU FFT fully replaces it for an interior
vertex. The selected 144,950 water pixels were RGB-identical in both normal and
FFT-disabled comparisons; camera, boat, hull and bow checks pass. The moving-camera
run averages 42.28 FPS, so the stationary result is not a 60 FPS minimum. One Intel
UHD 770 Low pair went from 91.322 to 91.966 ms (10.95 to 10.87 FPS), with no
measurable additional gain. See the [60 FPS goal report](modern-water-60fps-goal.md)
for evidence and limitations; the earlier [45 FPS goal report](modern-water-45fps-goal.md)
and [CPU cache validation](modern-water-cpu-cache-validation.md) remain historical. Optional geometry and
shading tiers, reflection cadence, reduced precision and lower simulation
frequency remain deferred.

## Saved-game comparison

Loaded the user's `00000010.sav` (Superweapon General), retaining its saved camera.
Each process used a private copy of the save and preferences. All comparisons
used 1920×1080 and the same other graphics settings from the user's Ultra profile,
including `MSAA = 3`. VSync and dynamic LOD were disabled for comparison;
rendering was uncapped and offscreen. This is **water Low**, not overall graphics Low.

Modern Low explicitly selected reflections Off, Wave detail Low, refraction Off
and Reflection detail Low. High selected On/High/Full/High, retaining Hybrid.
Classic changed Water style only relative to the High configuration.

One live run per case measured 180 frame intervals starting 25 seconds after the
first Present. FPS below is 1000 / mean frame time, an uncapped diagnostic
throughput, not a promise of windowed gameplay FPS.

| GPU | Water | Mean frame, ms | Median, ms | 95th percentile, ms | Approximate FPS |
| --- | --- | ---: | ---: | ---: | ---: |
| RTX 4060 | Classic | 8.697 | 8.559 | 10.947 | 115 |
| RTX 4060 | Modern Low | 27.716 | 26.984 | 34.956 | 36 |
| RTX 4060 | Modern High | 30.936 | 29.348 | 44.818 | 32 |
| Intel UHD 770 | Classic | 23.416 | 21.828 | 35.278 | 43 |
| Intel UHD 770 | Modern Low | 142.002 | 141.942 | 155.619 | 7 |
| Intel UHD 770 | Modern High | 248.529 | 249.305 | 278.294 | 4 |

Low reduces Intel's mean frame time substantially, but still leaves single-digit
throughput. The RTX result shows a modest Low/High difference and a much larger
Modern/Classic gap. These runs do not establish Iris Xe performance.

The live simulation continues during measurement, so samples contain different
unit/effect states and different lengths of simulated activity. They are not
frame-identical replays or repeated-run confidence estimates. Loading completed
before the measurement window. All six live processes exited successfully.

A separate paused RTX comparison measured mean frame times of 5.271 ms Classic,
20.790 ms Modern Low and 23.360 ms Modern High. This supports a large rendering
floor even with simulation stopped, but paused water stops FFT updates too.
Do not compare those values directly with live results. All three paused timing
windows completed; their processes required forced termination after control
shutdown failed. No user-owned process was terminated.

## October 26 CPU finding: surface detail is unchanged

Both Modern tiers select a 192×128 projected lattice in this view, producing
approximately 21,100 vertices and 39,500 triangles, with about 22,000 evaluated
samples per render. Late RTX Low profile batches report about 4.7–4.8 ms building
the projected mesh and 8.4–8.5 ms filling vertices. High remains similar. This
roughly **13 ms CPU cost** survives every implemented water quality option.

`WaterProjectedMesh::ChooseResolution` derives its density from screen size and
budget, without a quality input. At the measured revision, `W3DWater.cpp` rebuilt
and filled that mesh each render, sampling terrain, damping, exposure, analytic
waves and coastal crests.
FFT Low changes the simulation texture; it does not reduce this lattice.

The measured `submit_ms` is CPU time inside submission and can include GPU
backpressure. Intel shows long mid-frame flush waits. Do not label these values
as isolated GPU shader timings, add overlapping device timing buckets, or infer
that the entire reported water-render duration is CPU geometry work.

Relevant source: `WaterProjectedMesh.h:77`, `W3DWater.cpp:3527`,
`W3DWater.cpp:4984`, `W3DWater.cpp:3750`.

## GPU finding: the retained material still matters

Source review confirms that reflections Off suppresses map-water mirrors/SSR;
refraction Off removes transmission/seabed/caustics and, together with reflection
Off, scene-colour capture. FFT Low reaches the 128 allocation. Depth remains
required. The shader cache includes the settings and active FFT resolution.

Low retains surface reconstruction, procedural sky, terrain visibility, foam
lighting and hull/coastal effects. The earlier measured material investigation
and its limits are in [the implementation validation report](modern-water-performance-validation.md).

A separate current-preview replay used the existing October 20 SW General bank
at its captured 1920×1080 size, all water settings Low, frozen time, 30 warm-up
frames and 60 measured frames. Intel host draw+Present medians were:

| Diagnostic material | Median, ms |
| --- | ---: |
| Full remaining Low material | 211.916 |
| Remove sky reflection | 223.918 |
| Remove foam lighting | 229.193 |
| Constant surface colour | 15.855 |

These are short, single-run host measurements including bank restoration and
GPU waits, not isolated GPU timings. They replay an older captured scene, not
the new live run. All measured cases had zero FFT updates and identical work
counts (300 draws and 60 bank injections). Removing sky or foam alone did not
demonstrate a benefit here. Broad material simplification did, but constant colour
is not an acceptable Low implementation. Do not infer additive savings or adopt
those diagnostic ablations as player settings.

## Current implementation priorities, 2026-10-27

1. **Appearance-preserving CPU reuse: implemented and runtime validated.**
   In addition to the earlier base-lattice and static terrain/offshore/exposure
   reuse, the prepared mesh cache can now reuse the exact terrain-culled and
   waterline-clipped result, keyed after current bow refinement. Moving
   obstacles/hulls, wave heights and coastal crests continue to use current
   inputs. The previous [CPU cache validation](modern-water-cpu-cache-validation.md)
   is historical and does not validate this extension. Current goal measurements
   and completed visual/dynamic checks are recorded in the [60 FPS goal report](modern-water-60fps-goal.md).
2. **Optional Water surface detail: Low / High.** After the reuse pass, consider
   a reduced projected interior lattice for Low if further geometry savings are
   needed. Preserve authored edge knots, shared seams, river/fallback geometry,
   terrain culling, shoreline clipping and vessel refinement. Measure CPU time
   and inspect coastlines, camera motion and hulls before accepting it.
3. **Optional Water shading: Low / High.** The tier remains unimplemented.
   Locate the remaining expensive surface calculations with controlled
   diagnostics, then validate a coherent cheaper material. This save does not
   support choosing sky or foam removal alone as the solution. Measure on Intel
   and retain High. Benchmark combined tiers only once their individual
   behaviour and visual tradeoffs are established.

The latest bounded work also skips CPU analytic height evaluation only when the
prepared GPU FFT fully replaces height for an interior vertex. Scalar CPU height
evaluators reduce the selected shoreline/interior evaluation overhead. The earlier
GPU work skips replaced analytic-wave output only where FFT weight is one and
spatial slope is zero, while retaining swell derivative work; it writes FFT
results directly into the sampled array and removes 54 packing copies, preserving
scratch mip copies and fallback. Scoped runtime visual/dynamic validation is
complete. Reflection cadence remains deferred.

## Reproduction artifacts

The October diagnosis artifacts are under `build-linux/sw-water-diagnosis/`:
the saved-game runner, individual manifests/logs/screenshots, `frame-summary.json`,
and `shader-replay/` commands/CSV observations. The completed RTX goal artifacts
are under `build-linux/water-60fps-validation/`. No dependencies were installed,
and the user's source saves and Options.ini were not modified.

Game executable SHA-256:
`d5359de932366449c7db22579cf40d1ea76e5f23dfcdc4fa404b1f0132a9c36d`.
Save SHA-256:
`c6821007373bd29900db4fefaf16d1de95d20af217ceb6e270431e1a71ae6182`.
Do not commit save files, banks or proprietary game captures.
