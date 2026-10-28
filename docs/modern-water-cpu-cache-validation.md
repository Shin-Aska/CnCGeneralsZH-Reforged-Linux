# Linux Modern Water CPU reuse validation

2026-10-27. Follow-up to the [SW General diagnosis](sw-general-water-performance-diagnosis.md).
This pass preserves the current surface density and material. It adds no player
setting and does not implement a cheaper shading tier.

## Implementation

- Cache the exact projected **base** lattice, keyed by authored corners, complete
  projection, clipping distances, resolution, vertex budget and ordered boundary
  knots. Return a copy before vessel refinement, terrain culling, waterline
  clipping and compaction. Those later operations still run with current inputs.
- Reuse static offshore samples, exposure, terrain depth and terrain damping at
  the same world coordinates and water level. A bounded sequence cache avoids
  another hash lookup for every vertex; changed order or coordinates cause misses.
- Reuse existing optical invalidation for changed terrain bytes, map identity or
  dimensions, water polygons/levels and resource resets. Water-style changes also
  clear both caches. Animated waves, coastal crests, obstacles and hulls remain live.
- Limit stored base vertices to 262,144 across at most 32 entries, and static
  samples to the existing surface vertex budget. Linux alone enables the caches.
  Shared callers on other platforms retain the original calculation path.

The caches are always on for Linux Modern water; there is no runtime switch. With
`ZH_WATER_PROFILE=1`, `[water-cache]` reports reuse/miss counts and stored sizes.

## Correctness checks

The Release game and `test_water_grid` built successfully. All **187 tests and
10,282,843 checks passed**, including seven new cache tests and the existing GPU
water-style/reflection lifecycle tests. New tests cover every topology key field,
original mesh metadata, caller mutation isolation, failed/empty builds, bounded
storage/eviction, static-field reference calculations, order/level changes,
invalidation and nonfinite inputs. Independent source review found no cache-key,
invalidation or platform-guard issue. The fingerprint manifest and diff checks pass.

The saved camera was captured at logic frame 106000, 1920×1080, Modern High, using
the pre-change executable and the new executable with caching off/on. All three
captures exited cleanly. Six selected open-water/shoreline rectangles, covering
144,950 unique pixels, matched byte for byte across all three images. Whole frames
were **not** identical: client-randomized foliage, smoke, object effects and the
runtime HUD vary between processes. This is partial image parity, not a claim of
identical images for every dynamic effect or camera position.

The screenshot harness uses `-turbo -autoskirmish 2 -loadsave 00000010.sav
-screenshot 106000 -maxframes 106060` and `ZH_SUN_SHADOW_CAPTURE_CLOCK=logic`.
The save takes startup precedence; `-autoskirmish` enables unattended screenshot
and termination handling. An initial diagnostic omitted that flag and timed out;
it produced no usable parity capture and is excluded from results.

## Saved-view performance

Use the same save, saved camera, private profile and Ultra settings as the earlier
diagnosis. Rendering is uncapped/offscreen, VSync and dynamic LOD are off, and the
native timer measures 180 frame intervals starting 25 seconds after first Present.
Do not interpret these host measurements as isolated GPU times or windowed FPS.

Three interleaved RTX 4060 Modern High comparisons:

| Run | Cache off, mean frame ms | Cache on, mean frame ms |
| --- | ---: | ---: |
| 1 | 28.875 | 27.658 |
| 2 | 28.985 | 26.236 |
| 3 | 30.956 | 25.957 |

The median of those run means is **28.985 → 26.236 ms** (about 34.5 → 38.1 FPS).
Late water-profile batches give a more direct CPU result: projected mesh plus
vertex fill falls from approximately **13.0 to 11.4 ms**, a saving of about
**1.6 ms**. Both caches reach complete reuse in settled batches of this saved view.
The same 192×128 lattice and roughly 21,100 vertices remain.

These are short live runs, not a deterministic simulation benchmark. Loading,
clock speeds and unit/effect states differ; the first cache-on run loaded more
slowly than later runs. The frame-time difference includes that variability and
must not all be attributed to caching. No Iris Xe result is established here.

A single Intel UHD 770 Modern Low pair used reflections Off, Wave detail Low,
refraction Off and Reflection detail Low, with other graphics still Ultra:

| Cache | Mean frame, ms | Median, ms | 95th percentile, ms | Approximate FPS |
| --- | ---: | ---: | ---: | ---: |
| Off | 150.952 | 150.586 | 168.869 | 6.62 |
| On | 149.162 | 149.649 | 165.891 | 6.70 |

CPU mesh/fill averages across the available profile batches fell from about
14.2 to 12.9 ms. Both runs exited cleanly. The 1.2% difference in total frame
time is too small to establish a dependable gameplay improvement from this
single pair: expensive device/GPU waits still dominate. These values are a
fresh same-build comparison, not a direct before/after against the earlier
142 ms diagnostic. This cache does **not** resolve the integrated-GPU problem.

## Camera movement

An exploratory native-control sweep alternated right/left camera holds. Both
cache-enabled runs and the disabled run completed cleanly. During continuous
movement, profile batches recorded 0 topology hits / 120 misses; after releasing
input, the settled run returned to 120 hits / 0 misses and no static-sample misses.
Stored base vertices stayed below the 262,144 limit. The post-pan image showed
continuous water and shorelines without an obvious stale-mesh artifact.

Whole-frame means were 36.668/36.714 ms enabled and 38.474 ms disabled. This is
not an equal-camera replay, so it does not establish a movement speedup. Moving
mesh/fill batches were slightly more expensive with caching (roughly a few tenths
of a millisecond), consistent with cache-miss checking/storage overhead. Stable
views recover the measured savings. A moving-vessel scene and live terrain edits
were not separately driven in this native test; unit tests and source review
cover their cache contracts, not an exhaustive visual regression suite.

## Artifacts and remaining limits

Ignored local artifacts are in `build-linux/water-cpu-cache-validation/`: baseline
executable/source copies, build/test logs, runners, manifests, profile logs, capture
comparisons and selected-region analysis. Manifests record executable/save hashes,
commands and private settings. Do not commit proprietary saves or game captures.

Validated executable SHA-256:
`47488b4ad8aa98cd8deed69cd75bfd9a9aa32e3f7bfefbcd3a9cb2f854d57ca3`.
The retained pre-cache executable is
`d5359de932366449c7db22579cf40d1ea76e5f23dfcdc4fa404b1f0132a9c36d`;
the original save remains
`c6821007373bd29900db4fefaf16d1de95d20af217ceb6e270431e1a71ae6182`.

This is a modest CPU optimization. It leaves terrain culling/clipping, animated
CPU samples, mesh upload and the GPU water material intact. It cannot by itself
close the Modern/Classic gap on integrated graphics. Camera motion must rebuild
base lattices when the projection changes; cache benefits are greatest when inputs
remain stable. Further material work and optional surface-detail tiers remain
separate follow-ups, with their own measurement and visual validation.
