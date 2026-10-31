# Moving-camera water reuse and loading preparation

This work starts at `3c1f8285a46d19d1b333a77ea691df3ab0d51ebe` on
`feat/water-wavefront-phase`. It preserves the approved water direction, wave
evaluation, shoreline clipping, geometry budgets, reflection settings and
update cadence.

## Why this work targets geometry

A deterministic SWANGLE1 route exposed a larger CPU cost in rebuilding the
camera-dependent mesh than in rebuilding static foam lookup data. During its
slow pan, the original water mesh preparation averaged 5.145 ms, compared with
0.500 ms while stationary. Terrain rejection took 1.499 ms, waterline clipping
1.359 ms, and compaction 0.214 ms. These are instrumented CPU elapsed times,
not isolated water GPU timings or an uninstrumented frame-rate measurement.

The map analyzer and wave-direction fields already survive camera movement.
The projected lattice and its clipped output depend on the exact camera, so
simply retaining more previous views does not avoid work during a continuous
pan. The selected changes reuse immutable terrain facts and eliminate copies
around the remaining camera-dependent work.

## Changes

- A bounded terrain cache answers the original wet-node rectangle predicate
  from summed-area tables. Its cutoff uses the original multiplication
  comparison, including the displacement epsilon. Triangle and mesh work
  budgets are charged exactly as before, even when the lookup is constant
  time. Unknown, invalid and over-budget queries keep the original fallback.
- Vertex compaction preserves ascending original vertex order in place.
  Waterline clipping stages only new intersections and its replacement
  indices. It returns early on a validated no-op and preserves atomic failure,
  canonical intersection order, all 16 bisection steps and original metadata.
- Prepared mesh results are immutable and pinned by a scoped shared owner
  through shader setup and submission. Cache hits avoid an output copy;
  misses move the output buffers. Clearing or evicting the cache during shader
  setup cannot invalidate the active draw. Capacity accounting uses retained
  buffers, with a compact-copy fallback when spare capacity would exceed the
  existing cache bound.
- Static optical, wave-direction and terrain-proof data are prepared while
  loading. New games warm after terrain, objects and bridges are ready. Saves
  and checkpoints warm after snapshot restoration, before the saved random
  stream is restored. Preparation does not advance water time, install or
  advance shore history, or generate time-dependent foam. Static shoreline
  source and topology preparation is staged separately; activation still uses
  the original first gameplay update's water time.

The terrain cache uses CPU RAM, not additional VRAM. SWANGLE1's instrumented
route retained one 689,724-byte table (about 0.658 MiB), built once. The bound is
eight tables; the largest supported 2048-by-2048 terrain could require about
128 MiB. Equal terrain cutoffs share a table. Loading preparation skips SAT
warming when there are more than eight distinct exact-flat levels, preserving
the existing lazy fallback instead of cycling through evictions. Prepared
mesh cache ownership remains bounded at 32 MiB; an active draw can temporarily
keep an evicted output alive until that draw ends.

Loading preparation moves necessary work behind the loading screen; it does
not make that work disappear. The static cache follows the loaded map and
relevant terrain/style revisions; camera movement alone reuses it. Camera-
dependent meshes and changing foam or ship effects still require runtime work.

## Measurement protocol and evidence provenance

Evidence and private-profile runners were stored under
`build-linux/water-moving-performance/`. Original saves and Options files were
not modified. Frozen executable, source-patch, input, route and camera-state
hashes were recorded for every run. At the user's request, local copies of the
validation artifacts from this pass, including videos, frozen builds and
reference files, were removed on 2026-11-01. The user retained copies
elsewhere; their location is unspecified. Paths and hashes below identify the
artifacts used for validation and do not claim those local copies still exist.
The current build and launcher were preserved.
The matched baseline retains committed water behavior and carries the same
camera/attribution hooks as the candidate; it is not the untouched executable.

The matched route contains 1,200 logic frames: initial hold, slow translation,
fast out-and-back pan, translation return, zoom/rotation and return, then an
ending hold. Its 900 moving frames are also reported separately. The exact
position, effective zoom, view plane, eye and forward vectors are checked;
the final benchmark comparisons require the same saved camera state and
zero unexplained frame omissions. Early scouts with naturally settled camera
poses are excluded from matched comparisons.

Performance runs use 1920-by-1080 Hybrid, High water/reflection detail and Full
refraction, uncapped offscreen Vulkan on the RTX 4060. The command requests
8x MSAA, but this native backend reports single sampling (`multisampling: 0x`);
these are not 8x measurements. Timings are whole-engine intervals, not a
promise of desktop FPS. Builds, tests, captures and encoding are serialized
with timing runs. Instrumented attribution runs are excluded from throughput
comparisons.
Path-reflection throughput is not remeasured in this pass.

Run-level means, medians, p95 and p99 were reported, with A/B differences paired
by run number. Quantiles are computed inside each run; frames from several
runs are not treated as independent replications. Counterbalanced ordering
and stationary/other-scene checks are used to expose variability.

Cold-start checks compare the same final executable with loading preparation
enabled against `ZH_WATER_PREWARM=0`. They retain the first eligible world Present
after the selected save load, plus 119 subsequent intervals. Native load,
preparation and Present markers share the SDL timestamp epoch. Reports
distinguish the full loading-to-world transition from load-ready-to-first-
Present response and from the subsequent frame intervals. No warmup window
is discarded in this comparison. The camera hash in marker metadata is an
input attribution; actual camera traces and the effective state roundtrip
provide the independent pose checks.

## Intermediate results and selection

The first candidate, terrain proofs plus compaction alone, did not pass the
whole-frame check. Four matched pairs averaged 16.161 ms for baseline and
16.476 ms for the candidate; moving-only averages were 17.711 and 17.867 ms.
Those runs were recorded in `swangle1-abba-baab20-23-run-statistics.json`. A local CPU
stage saving was insufficient evidence of a throughput improvement.

The subsequent copy-removal candidate improved both matched SWANGLE1 pairs.
The two baseline route means were 17.008 and 17.232 ms; candidate means were
16.496 and 15.700 ms. Moving-only means averaged 18.485 versus 17.390 ms.
These intermediate results were recorded in `swangle1-v2-statistics.json`; final loading
preparation and other-scene validation are reported separately below.

Its instrumented slow-pan mesh preparation fell from 5.145 to 4.284 ms:
terrain rejection 1.499 to 1.034 ms, waterline clipping 1.359 to 1.102 ms,
compaction 0.214 to 0.151 ms, and prepared-result store copies 0.282 to
0.178 ms. Prepared output hit copies were eliminated. Base lattice creation
and vertex fill remained runtime costs.

An initial loading-preparation candidate reduced mean load-ready-to-first-world-
Present response from 402.86 to 210.51 ms across two off/on pairs of the same
executable. Preparation took 202–207 ms behind the loading screen. The full
previous-Present transition averaged 473.96 versus 480.84 ms, so that result
does not imply that startup work disappeared. The separate diagnostic exposed
another 37.51 ms of static shore construction after readiness; actual history
advancement took only 0.000223 ms. This prompted staging shoreline source and
topology during loading too, while retaining the original live activation time.
The earlier cold results were recorded in
`cold-prewarm-off-on-v3-summary.json` and `cold-on13-attribution.json`, and are
not pooled with the final candidate.

## Final validation

The final candidate used for validation was `candidate-v4/generals`, SHA-256
`cdccc6e8c7b74b63e568a23679330678cedc81ec089df57ccc118e09e10be215`.

Two final cold off/on pairs retained all 120 initial samples with exact
cross-run camera/eye/forward equality. Mean load-ready-to-first-world-Present
response was **397.34 ms with preparation disabled, 155.77 ms enabled**.
Loading took about 208 ms longer on average; the complete preceding-Present
transition remained about the same (461.02 versus 456.89 ms). Preparation is
enabled by default. This does not eliminate unrelated engine/driver startup
costs or guarantee that every initial frame is already at steady-state speed.

A separate diagnostic verified 25,276 shoreline cells, 44,159 edges and
31,658 raster tiles prepared before readiness, without changing water time.
The first live shore activation took 0.186 ms instead of the earlier 37.512 ms
construction, at the same water time of 1806.68335 seconds. The terrain proof
table was also already built. The validation report was
`final-v4-cold-validation.json`, with cold run manifests; diagnostic timings
are not pooled with throughput.

Final SWANGLE1 ABBA measurements average **17.337 to 16.259 ms while moving
(-6.22%)**, with both paired moving runs improving (-6.82% and -5.62%). Whole-
route means are 15.895 to 15.002 ms (-5.62%). Mean run p95/p99 moving times
fall from 23.635/26.491 to 22.661/25.939 ms. The stationary guard changes +0.48%
and the ending hold -5.85%. All camera and input checks pass. This is a modest
throughput gain, not evidence of restoring the historical 90 FPS in all views.

| Scene | Baseline moving mean | Candidate moving mean | Interpretation |
| --- | ---: | ---: | --- |
| SWANGLE1 | 17.337 ms | 16.259 ms | -6.22%; two matched pairs |
| SWANGLE2 channels | 16.840 ms | 16.040 ms | -4.75%; one matched pair, stationary guards stable |
| Carrier open sea | 13.235 ms | 13.216 ms | -0.14%; essentially unchanged control |
| Carrier inlet | 11.236 ms | 10.903 ms | -2.96%; two matched pairs |

The inlet's first stationary guard was +6.51% slower; its counterbalanced
repeat was +0.63%. Both were recorded. Their combined stationary mean is
8.522 to 8.824 ms (+3.55%), with mean run p95 +5.81%. The larger slowdown did
not repeat, but the residual uncertainty is reported rather than declaring
every scene and phase faster. No additional runs were selected to improve
that result. Other stationary guards showed small changes or improvements.
The reports are `final-v4-swangle1-abba50-51.json`,
`final-v4-swangle2-channels-ab50.json`, `final-v4-carrier-sea-ab50.json` and
`final-v4-carrier-inlet-abba50-51.json`.

A private-profile fresh Superweapon General mission also exercised the
non-save loading hook. Optical, terrain-proof and shoreline preparation
completed on the correct map. Its ordinary water clock stayed 0.1 seconds
through preparation and advanced normally to 0.2 at the first live update.
The smoke runner initially asserted a picture-clock value despite using the
ordinary clock; the original failed assertion was preserved beside an offline
corrected validation in `fresh-newgame-v4-smoke/validation-corrected.json`.
The native process itself completed successfully; no code fix or rerun was
used to disguise that harness mistake.

Each of the four capture pairs retained all 1,201 native frames with zero
capture misses and exact camera/eye/forward agreement. All 4,804 frame pairs
were inspected numerically. Over the initial 120-frame stationary segment,
selected pure-water regions had mean RGB8 absolute errors of 0.06263/255 for
SWANGLE1, zero for SWANGLE2 and carrier open sea, and 0.00009645/255 for the
carrier inlet. Maximum channel errors were 9, 0, 0 and 1 respectively. The
SWANGLE1 island/shore region was pixel-identical across those 120 frames.
These selected regions do not establish whole-water pixel identity. Mixed
scene metrics also include animated trees, units and UI. Paired native stills
at the highest mixed-scene errors and during panning/zooming showed consistent
water patterns, direction and shoreline contours, without a visible water
regression. Lossless evidence was recorded in `final-v4-visual-*/inspection.json`,
`frame-parity.csv`, and the native still/capture folders.

The Release build and nine targeted CTest checks passed: water
geometry, diagnostic camera, frame timing, fingerprint and manifest checks,
and four native rendering/reset checks. The water suite passes 225 tests and
11,897,233 checks; camera replay passes 4 tests/104 checks and frame timing
passes 8 tests/707 checks. This is not a claim that the entire
repository test suite ran. Frozen-reference comparisons exercise generated
geometry, active metadata, callback order, budgets, invalid inputs, rollback,
cache clearing/eviction and the lifetime of an active draw. Independent source
reviews found no actionable issue in the geometry changes or load lifecycle.

The comparison reel used for validation was `build-linux/water-moving-performance/final-v4-comparison.mp4`:
44 seconds, 2,640 decoded frames, 3840-by-1080 at 60 fps. It uses separately
captured native frames with synchronized water and scene clocks, covering
SWANGLE1 (20 seconds), SWANGLE2 channels (8 seconds), carrier open sea
(8 seconds) and carrier inlet (8 seconds). Playback is an appearance
comparison, not a performance meter. An opaque header covers native FPS and
wall-clock counters. The first/last frames and both sides of each scene
boundary were decoded and visually checked. Native camera traces, endpoint
frames and lossless frame/ROI comparisons were stored alongside the reel.

The local launcher executable at `build-linux/ZeroHourReforged/bin/generals`
has been updated from the tested frozen candidate. Its stripped package
SHA-256 is `e8922d147f562c8e41d9f5f6be874e9322a1b957b2c239ff00313c5f0270176a`.
All 33 allocated ELF sections match the frozen executable; both share build
ID `7f9adf3717b2227740ef0b613d026edf50690c48`. The preceding packaged binary
was identified as
`build-linux/water-moving-performance/package/generals-before-moving-v4`, with
its provenance recorded in that directory's `manifest.json`.

## Diagnostics

Normal play needs no optimization flag. Diagnostic switches are opt-in:

- `ZH_WATER_MOTION_PROFILE=1` and `ZH_WATER_MOTION_PROFILE_LOGIC=first,last`
  enable CPU phase attribution, cache counts and foam mapping evidence.
- `ZH_CAMERA_ROUTE`, `ZH_CAMERA_ROUTE_STATE`, `ZH_CAMERA_ROUTE_STATE_OUT`
  and `ZH_CAMERA_ROUTE_CSV` replay and verify a fixed single-player camera.
- `ZH_WATER_PRELOAD_TRACE=1` records static-preparation phases and readiness.
- `ZH_WATER_PREWARM=0` bypasses loading preparation for the cold-start control.
- `ZH_GPU_TIMING_COLD=120`, its saved-frame selector and CSV/marker paths
  retain a cold sequence separately from the existing warm logic-window mode.

None of these diagnostics changes ordinary wave quality or enables a reduced
reflection mode.
