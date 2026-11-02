# Water at the playable map boundary

The analyzer previously required a large patch of water at least 450 world units
from shore. Pooling fragmented coastal patches fixed Flash Effect, but Tournament A
still failed: its entire playable perimeter is water, yet only 48,500 square world
units met that clearance test, below the 180,000 requirement. Its water therefore
used the quieter channel spectrum everywhere.

## Boundary evidence and local movement

The load-time analyzer now separates three things:

- `exterior` records evidence of sea beyond the playable crop. A continuous,
  polygon-covered straight boundary frontage of at least 900 world units is
  sufficient, independently of visible area or distance from shore. The existing
  exposed offshore-core test remains another source of evidence. Length counts
  covered links, not sample nodes; short sides cannot accumulate across a corner
  or an uncovered polygon gap.
- `sourceCells` supplies arrival distance, phase and transported movement. An
  established broad sea retains its existing source footprint. A boundary-only
  sea selects the first qualifying opening clockwise from the upper left. Each
  independent transport network elects one such region: its largest qualifying
  broad sea, or its first long coastline when no broad sea qualifies. Other
  openings in that network remain passive. A short perimeter mouth remains a
  single global fallback; it cannot activate every disconnected river. Across
  authored elevations, the original primary candidate still controls selection;
  frontage, then core area, breaks ties deterministically.
- `sea` is the blend into the original FFT spectrum near the selected source.
  Boundary-only sources can fill adjoining broad water within 450 world units
  of covered wet-path distance, followed by the existing 220-unit transition.
  This bound prevents a long broad corridor from restoring full sea waves at its
  downstream end. Appearance does not add phase anchors or reset movement energy.

This is an explicit interpretation of unknown exterior geography, not a fluid
simulation. A long edge-hugging water strip and a cropped sea can have identical
visible geometry; the chosen default is sea continuation. The 900/450/220-unit
scales are visual policy, not physical constants. Enclosed lakes, narrow river
mouths and basins behind narrow passages retain the existing shelter behavior.
Visual bridge connections still affect transport only. They join source elections
into one transport network so linked bodies share one incoming phase. They never
join physical components, establish exterior evidence or expand the sea blend.
Independent sources own their chart heading, phase origin and source-kind energy
policy, preserving the primary ocean when another disconnected coast qualifies.

The new work happens during the existing cached map analysis. No shader, FFT
resolution, GPU texture layout or per-frame classification pass was added.

## Validation

Release build and `ctest --test-dir build-linux -R '^test_water_grid$'
passed: 225 tests, 12,153,401 checks. New cases cover thin cropped coastlines,
closed lakes, narrow mouths, exact frontage thresholds at two grid spacings,
corner wrapping, uncovered links, a later qualifying source, passive openings
in both narrow and broad connections, blind inland corridors, disabled sources,
source ranking, packed spectrum metadata and transported phase.

Two temporary negative controls were also exercised: disabling frontage evidence
fails the new sea cases; removing the coastal reach bound fails the shared-region,
blind-corridor and passive-spectrum checks. The working tree was not changed for
these controls.

Native map fields were compared against the working build before this change,
which already included the earlier Flash Effect grouping fix. Percentages below
count playable wet samples whose original-spectrum blend is fully enabled:

| Map | Before | After | Preservation check |
| --- | ---: | ---: | --- |
| Tournament A | 0% | 69.73% | Exact energy, direction and both packed phase charts |
| Flash Effect | 66.31% | 66.31% | Complete field byte-identical |
| Superweapon General | 0% | 14.67% | Exact energy, direction and both packed phase charts |
| Aircraft carrier, sea level | 74.43% | 74.43% | Complete field byte-identical, including bridge transport |
| Aircraft carrier, raised water | 0% | 0% | Complete field byte-identical; still calm |

Tournament A was also rebuilt in fresh native launches with seeds 0 and 17;
the complete fields matched. These checks establish deterministic classification
and transport preservation. They do not imply pixel-identical rendered scenes
or a measured FPS improvement.

The comparison video covers Tournament A's coast, Flash Effect's coast and inlet,
Superweapon General's entrance and island channels, and the carrier's sea and
bridge inlet. It uses private revealed snapshots for saved missions; original
saves are untouched. Capture clocks require both the diagnostic environment and
their eligibility flags: a valid control port for the water picture clock, and
a scheduled screenshot for the scenery logic clock. Camera, phase and capture
counts must be verified before presenting a paired video.

The final reel is `water-boundary-before-after.mp4`: 36 seconds, 2,160 decoded
frames, 2560 by 720 at 60 fps. All six pairs have 360 matching camera and effective
water-time samples and no missed capture pictures. A pure-water Flash Effect
control region is pixel-identical throughout all 360 frames. Carrier controls
retain their appearance, with small startup/reflection or UI differences; no
whole-image identity or frame-rate improvement is claimed. The first and last
decoded frames of every scene were visually checked.

Capture commands, build fingerprints, camera anchors and the final video belong
to the task's devlog directory, outside the source/build tree:
`/mnt/sandisk-2tb/devlog/sessions/2026-11-01-water-boundary/`.

## North America: independent oceans

The native North America crop contains twelve physical wet components. Its
western ocean had a qualifying 421,300-square-unit offshore core; the eastern
and northeastern oceans independently had long coastlines, but the previous
single winner suppressed their forcing. Exterior geometry was present while
arrival distance, transported phase and the full FFT blend were absent.

The November 2 fix elects a source in each independent qualifying ocean. On the
exact native 515-by-515 grid (70-sample border, 10-unit spacing, water level 25):

| Physical body | Source cells before → after | Full FFT samples before → after |
| --- | ---: | ---: |
| Western ocean (0) | 16,782 → 16,782 | 12,000 → 12,000 |
| Eastern/southeastern ocean (2) | 0 → 191 | 0 → 3,059 (22.30%) |
| Northern/northeastern ocean (8) | 0 → 140 | 0 → 894 (4.91%) |

The western source order, heading, sea blend, energy, direction and downstream
analysis are preserved. Its original FFT words, encoded chart gradients and
blend/amplitude metadata are exact. Adding the independent charts increases
the shared packed phase step from 0.0327421 to 0.0481964 world units; the maximum
western decoded-phase difference is 0.0403642, below the combined half-step
quantization bound of 0.0405693. The other nine physical components retain their
calm energy and all packed words exactly. This comparison uses the native field
as input and recompiles the same production analysis headers; it does not claim
a rendered before/after capture.

The combined release build of `generals` and `test_water_grid` passed. Focused
`ctest --test-dir build-linux --output-on-failure -R '^test_water_grid$'` passed:
225 tests, 12,814,407 checks, zero failures.

Regression fixtures cover one broad sea alongside two independent boundary
seas, two independent broad seas, connected passive coastlines, bridge-unioned
source elections, globally passive short mouths and lakes, disabled forcing,
and exact primary analysis with decoded phase preservation. Diagnostic inputs,
candidate fields and comparison summaries are retained outside the repository:
`/mnt/sandisk-2tb/devlog/sessions/2026-11-02-north-america-water/`.

Independent phases that touch across a subcell bank can share an interpolation
patch in the existing atlas. Its packing-confidence guard remains conservative:
an unresolved cross-bank phase jump suppresses local amplitude in the immediate
bank stencil. A regression verifies unchanged primary analysis and decoded phase,
zero unsafe bank weights, and unchanged weights two samples farther inland. This
layout limitation is absent from the separated North America ocean comparison;
ignoring neighbouring chart payloads would weaken interpolation safety.
