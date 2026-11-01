# Water performance recovery

This pass starts from the approved channel appearance in `a094410c`. The map
analyzer already builds and caches its source, shelter and route fields when
the map changes. The older topology, prepared-mesh and terrain-sample caches
are still active. In a settled SWANGLE2 northern-channel capture they report
120 hits / zero misses per batch for topology and prepared meshes, and
1,608,000 hits / zero misses for terrain samples. Rebuilding the analyzer each
frame is not the cause of the reported slowdown.

## Reused work

- The shader reads and reconstructs a route's phase chart only when its corner
  weights contain a contribution. Ordinary single-route water avoids four
  packed texture reads and the unused chart's derivative reconstruction.
  Crossing water continues to evaluate both routes.
- Stream detail differentiates directly in world coordinates. This cancels
  the previous noise-coordinate Jacobian conversions and inverse conversion
  of the resulting slope. Noise positions, hashes, clock phases, amplitude,
  filtering and the approved residual wind/current composition are preserved.
- Path reflection export hashes each shared mesh model once within a refresh.
  Mesh identity, transform, visibility and the actual posed skin vertices
  remain per-instance inputs. The memo is destroyed after every refresh;
  model/material/texture changes are checked again on the next one. Reflection
  resolution, sampling and refresh cadence are unchanged.
- Path geometry-tree rebuilds calculate each triangle's bounds and centroid
  once, then reuse them during recursive partitioning. Partition order, tie
  comparisons and packed GPU output remain identical. Temporary storage costs
  36 bytes per input triangle (4.12 MiB at 120,000 triangles) and expires after
  each build; ordinary refits are unchanged.

The existing shader evaluates a smooth static field at arbitrary pixel
positions, so precomputing its final normalized, bank-projected velocity on a
coarser grid would change the interpolation. This pass retains that field and
removes redundant evaluation rather than introducing a new spatial approximation.

## Correctness evidence

The Release build, shader compilation suite, local GPU checks and full NVIDIA
GPU frame selfcheck pass. A frozen preoptimization local evaluator is compared
against 102 queries, including absent first/second routes, crossings, bank
projection, filtered slopes, spatial clocks and still pools. The maximum
fields/waves difference is `4.55e-8`; displacement-only vertex results are
identical. Existing wind/current composition and direction tests remain active.

One added query exposes `8.71e-6` of vertex-versus-pixel specialization error in
the original evaluator too. Its allowance uses that frozen baseline, with a
separate strict old/new vertex comparison; the sea and full-surface oracle
bounds are unchanged.

The new model-digest tests pass 606 checks. The expanded Path CPU suite passes
749,595 checks, including byte comparisons against the frozen original tree
builder for random, terrain-like, repeated, tied, degenerate and extreme
geometry, with invalid-input rollback. The Path GPU test passes 122,402 checks.
A separate native verification run recomputes all 307,263 actual
model memo hits across 412 refreshes and records zero mismatches. Verification
is disabled in performance runs. It can be repeated with
`ZH_WATER_PATH_VERIFY=1`; `ZH_WATER_PROFILE=1` reports model hit counts, bytes and
terrain/collection/export CPU phases. Those phase reports are process-wide
diagnostics, not isolated water GPU timings.

The build fingerprint manifest now includes the new files and water files
omitted by preceding changes; both manifest/fingerprint checks pass.

An isolated build benchmark uses the actual GCC 16 Release floating-point
flags, rotated execution order and exact output comparisons. At 120,000
triangles over six repetitions, bounds/centroid reuse reduces median rebuild
time by 18–33.5% on random, terrain-like and repeated-mesh data, and by 9–10.8%
on the tied/extreme-coordinate cases. This measures tree construction only;
it is not a whole-frame speedup.

## Measurement protocol

Local evidence is retained under `build-linux/water-performance-recovery/`.
The immutable baseline is the approved `b672a48f…74092` executable. Private
profiles use the same SWANGLE2 save, 1920×1080, High waves/reflection detail,
Full refraction and the saved `MSAA=3` preference. Path and Hybrid are measured separately.
The original runner also passed CLI `-msaa 3`: that argument is a sample count,
so it requested 2×, whereas Options level 3 requests 8×. The native SDL backend
currently accepts only one sample and logs `multisampling: 0x` for both requests.
Thus these comparisons use the same single-sampled backend; they are not 8× MSAA
measurements. The SWANGLE1 follow-up explicitly records requested and effective samples.

Each unpaced native run retains the same 600 consecutive logic frames,
110624–111223, after at least 600 settled warm-up frames. Runs alternate in
ABBA order for Path and normal-zoom Hybrid; close-up Hybrid uses one fresh A/B pair. There
are no concurrent builds, captures or encoding. Frame CSVs,
executable/input hashes, complete commands and NVIDIA telemetry are retained.
These are whole-engine frame intervals, not per-water-pass GPU timestamps.

Path refreshes use a 100 ms wall-time schedule. Equal logic-frame windows do
not imply identical reflection snapshot sequences or equal numbers of scene
updates; faster runs finish the window with fewer wall-time refreshes. Report
the resulting throughput and producer counts together. Historical 3.5.1 FPS
figures from different windows/settings are not a matched comparison.

## Earlier SWANGLE2 native result

On the RTX 4060 / NVIDIA 595.99.02 Vulkan setup, the final Path runs are
18.469 / 18.157 ms for the approved baseline and 16.993 / 16.604 ms for the
optimized executable. Both pairs improve, by 7.99% and 8.56% respectively.

| View / reflection | Baseline mean | Optimized mean | Change |
| --- | ---: | ---: | ---: |
| SWANGLE2 saved camera / Path | 18.313 ms | 16.798 ms | −8.27% |
| SWANGLE2 saved camera / Hybrid | 11.585 ms | 11.524 ms | −0.53%, effectively unchanged |
| SWANGLE2 close south bay / Hybrid | 16.525 ms | 16.503 ms | −0.14%, effectively unchanged |

The Path means correspond to approximately 54.6 → 59.5 frames/second in
this benchmark. Path p95 falls from 59.473 / 58.899 ms to 45.961 / 46.318 ms,
about 22%. Normal-zoom Hybrid averages approximately 86.3 → 86.8 frames/second;
the separate close-up result is approximately 60.6. The historical references
are roughly 90 FPS for Hybrid and up to 60 FPS for Path, not a shared target
for both modes. The older Hybrid report used save 10 (`c6821007…6182`) and a
different wall-timed window; the current save 16 is `39c93d6b…b9c5`.
All ten final windows contain 600 consecutive frames with zero
rejections and clean exits, and no Path fallback was reported. The earlier
series includes an unusually slow 29.6 ms baseline; it is retained separately
and is not pooled into the final result. The Hybrid results do not establish
a native shader speedup or recovery of the historical 90 FPS figure.

Full frame tails, process-level reflection counts, caveats and hashes are in
`build-linux/water-performance-recovery/bvh-timing-results.md` and
`bvh-summary.json`. The full CTest suite was not rerun for this performance
pass; the targeted build, shader, GPU, Path, cache and fingerprint checks above
were run.

That pass updated the local packaged executable. Its approved predecessor
is retained under `build-linux/water-performance-recovery/package/`. Final
raw SHA-256: `6fd531c0076677f4991f57b3c3e78800b6e802a299c7d3d364598cd440ec74f4`;
raw and stripped package share build ID
`5686637b44111785de59dafd31c0c751c655000f`.

## Native appearance comparison

Eight verified clips cover the SWANGLE2 northern entrance and island junction,
plus the carrier map's open sea and inlet. Each source contains 481 consecutive
frames at 60 fps, with zero missed pictures. The two approved SWANGLE2 baseline
clips were reused after checking binary, save, options, camera, frame-range
and media hashes; the other six clips are fresh captures. Original user saves
and preferences remain unchanged.

These normal-zoom captures use the established Hybrid / requested MSAA 0 visual setup,
separate from the performance runs above; both use the native single-sample backend.
The reels show eight seconds per
view at fixed 60 fps playback; they demonstrate appearance, not rendering speed.
Selected water-only regions in native lossless first/last images differ by
mean RGB levels of 0.007–0.127 out of 255. Full-frame and water-region inspection
montages are retained alongside the clips. These still comparisons support
appearance preservation; motion is covered by the regression checks and reels.

## SWANGLE1 Hybrid follow-up

The follow-up reproduces the user's saved SWANGLE1 camera (Save15), rather than
using SWANGLE2 as a proxy. Its baseline is the preceding `6fd531c0…` build above.
The analyzer and existing geometry caches were already hitting; repeated CPU
shoreline and draw preparation remained expensive. Main-thread stack sampling
found shoreline source/history/rasterization, sampler hashing and repeated
dynamic vertex copies. Stack counts are diagnostic frequencies, not GPU timings.

The added reuse is automatic:

- Shoreline raster lookup data is retained for each camera mapping and water
  level. Terrain/support revisions invalidate it. Density, age, phase and the
  existing upload schedule remain live. The world-fixed foam breakup pattern
  is prepared with the mapping instead of re-evaluated on every atlas upload.
- Coastal source evaluation caches terrain-only terms while preserving float
  operation order and the current phase-dependent wave/front calculations.
- Projected surface samples keep their allocated hash nodes across renders.
  An epoch rejects every preceding render's values, so moving hulls, wave time,
  terrain and camera inputs are recalculated. Stale storage is pruned under
  camera movement, and the original per-render admission budget is preserved.
- Consecutive indexed draws reuse an unchanged dynamic vertex range within a
  native batch. Buffer ownership, write version, range, stride and batch must
  match. Open locks and user pointers bypass this shortcut. Reset discards it.
- A four-entry sampler shortcut handles alternating texture settings; its
  fallback hashes state words while still comparing all state bits exactly.
- Headed FFT water omits the unused broad-cascade generation/copies. Full fields
  are completed for unheaded or coastal consumers, including late heading
  resource failure. This alone was near-neutral in native throughput; the final
  comparison measures the combined CPU and producer changes.

The final Release build and seven serial checks pass: water grid, native GPU
frame/draw/pipeline/reset, and both fingerprint checks. New tests compare frozen
source/raster results, exact atlas pixels and cache hit/admission behavior; they
cover mapping/revision changes, live values, budget pressure and epoch wrap.
Native pixel tests exercise buffer writes, open locks, range changes, failed
staging, batch submission and reset. No wave settings, reflection quality,
motion directions or update frequencies were reduced. The full CTest suite was
not rerun; the targeted results are retained in `reuse-tests.log`.

Caches trade bounded memory for work: source preparation adds at most 10 MiB
at the 65,536-cell domain limit. A 512-square per-level raster lookup plus
breakup data is bounded to roughly 4.3 MiB, with at most eight levels.
Projected sample storage retains at most the existing vertex budget and prunes
stale camera coordinates. One vertex buffer is retained by staging reuse.

Evidence is under `build-linux/swangle1-hybrid-performance/`. Final timings use
fresh ABBA processes, the same 600 frames 109420–110019, at least 600 settled
warmup frames, Hybrid/High/Full at 1920×1080, and requested 8× / effective one
sample. Benchmark processes run alone, without profiling, builds or video
encoding. Private saves/options remain unchanged. These are uncapped offscreen
whole-engine intervals, not a guarantee of desktop FPS or isolated GPU cost.

The final four runs are 14.829 / 13.431 / 13.268 / 14.749 ms (A/B/B/A).
Baseline mean is **14.789 ms**, final mean **13.349 ms**, a **9.73% reduction**.
FPS derived from the mean rises **67.62 → 74.91**. Both adjacent pairs improve
(9.43% and 10.04%), with much less within-binary drift than the earlier trials.
Mean per-run p95 falls **23.137 → 18.171 ms (21.46%)**; p99 falls
**25.627 → 20.777 ms (18.93%)**. These are averages of per-process quantiles.
Draw count changes by only −0.112%, and all runs retain four flushes per frame.
Each accepts exactly 600 frames with zero rejections after 1006 warmup frames.
Older exploratory and intermediate measurements are excluded from these means.

This is a measurable improvement; it does **not** recover the historical
90 FPS Hybrid figure in this scene. No comparable final Path FPS result is
claimed by this SWANGLE1 Hybrid measurement. Full provenance, tails, settings
and caveats are in `final-findings.md` and `final-summary.json`.

Final raw executable SHA-256:
`6610876e171a553b8701833a6d12c863fe053f587682bd2b651e3c436374e7ea`.
Build ID: `17fa8b6d7421052176201b55bdadb29dde29f3a0`.
The water suite reports 210 tests / 11,248,410 checks with zero failures.

The initial SWANGLE1 visual captures exposed a separate validation-clock issue:
`ZH_WATER_CAPTURE_CLOCK=picture` fixes modern water's phase, but authored `.wak`
beach waves still use wall time. Bank-region mean RGB differences were 4.97/7.84
out of 255 at the first/last frames; repeating the **same baseline binary** gave
4.37/7.15 in the unchanged region. These unaligned captures are retained as
diagnostic evidence and do not establish a shoreline appearance change.
Visual recapture uses the existing `ZH_SUN_SHADOW_CAPTURE_CLOCK=logic` with
`-turbo` and a positive screenshot frame beyond the run's end, identically for
both frozen binaries. This aligns authored surf and W3D animation clocks without
taking a screenshot or changing production code. It also changes diagnostic
presentation scheduling; final performance runs above retain their original
configuration and are separate from these appearance captures.

With those clocks aligned, the unchanged SWANGLE1 bank ROI is pixel-identical
at both captured endpoints (mean and maximum RGB error zero). This resolves
the bank discrepancy without a shader, geometry or appearance change.
The carrier open-water control is also pixel-identical at both endpoints;
SWANGLE1 interior-water mean RGB error is 0.048/0.070 out of 255. The four
aligned source clips each contain 481 consecutive pictures with no misses.
The side-by-side reel presents eight seconds of SWANGLE1 followed by eight
seconds of carrier open sea, at fixed 60 fps. It demonstrates appearance,
not rendering speed. Aligned images/metrics and the unaligned/A-A controls
remain alongside the clips for inspection.

The local package at `build-linux/ZeroHourReforged/bin/generals` now contains
the stripped final executable with the same build ID as the benchmark binary.
Package SHA-256:
`66ab2ba3f1c80148b998c1bb1168641e42c3d9a48e49b131bb79fcbe34f6b519`.
The preceding package is retained in the evidence directory's `package/`
folder. Original saves/preferences are unchanged.
