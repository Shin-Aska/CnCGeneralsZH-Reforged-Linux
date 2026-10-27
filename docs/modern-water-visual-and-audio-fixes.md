# Modern Water visual fixes and audio investigation

**Status: water fixes built; targeted NVIDIA and Intel checks pass.** This note
records the screenshot investigation, validation and separate audio report.
The source checkpoint for the investigation is `88945641`. No audio changes
remain in the patch. The current High tier uses 512×512 Path captures;
see the High-detail follow-up below. Earlier timings are labelled by checkpoint.

## Path-traced water edge in the saved view

The user's saved view was captured at 1920×1080 with Path tracing, High 256
waves, Full refraction and MSAA 3, retaining the other Ultra settings. The
diagonal edge in the screenshot reproduced across six initial cases: Path,
Hybrid, sky, cache-elision off, cage off and refraction off. Disabling FFT also
reproduced it. Normals and offshore waves remained continuous, which points away
from FFT or CPU-cache geometry as the source.

Contact-lighting diagnostics showed a green discontinuity at the exact edge,
while reflection-debug output did not show the diagonal. The current evidence
therefore points to direct-light visibility rather than water geometry or
caching. The `shadow-sources` capture confirms the terrain-height shadow as
the source: the affected area is green (terrain visibility zero, mapped
visibility one). The water surface is continuous there. This identifies the
shadow producer, not a false occluder; terrain-shadow behavior is unchanged.

The Path changes address filtering, the capture/material mismatch, and an
Intel traversal defect exposed by the new regression scene:

1. Temporal Path histories now allocate mip levels, generate mipmaps from the
   averaged linear RGBA history and use trilinear material sampling. Temporal
   accumulation continues to read level zero.
2. A primary-ray miss now stores transparent black, allowing the water material
   to evaluate its procedural sky consistently from the actual wave normal,
   including at the viewport edge. A real black geometry hit retains coverage;
   secondary lighting is unchanged.
3. Child descent and deferred-node handling use structured branches instead of
   early loop `continue` statements. The original traversal missed 28 of 32
   stripes on Intel UHD 770 in both tested triangle layouts. Direct tracing and
   an escape-pointer traversal found every stripe; GPU counts and uploaded
   records were correct. The equivalent structured traversal passes on both
   GPUs while preserving visit budgets, leaf order and nearest-first ordering.
   Restoring the old sky-on-miss behavior retained the Intel failure, so the
   missing geometry was not introduced by the new coverage contract.

Those initial three changes did not increase capture resolution or capture cadence.
The subsequent close-up investigation below corrects stationary resolution recovery
and primary materials. Animated scenes still reset lighting history globally; this
is not a claim that all Path-traced water scenes are artifact-free.

## Initial wide-view validation

Release `generals` and the targeted tests build successfully. On RTX 4060
(NVIDIA driver 595.99.02), `test_water_path_gpu`, `test_water_path_tracing` and
`test_shader_sdl` pass. `test_water_path_gpu` also passes on Intel UHD 770.
The GPU test covers unchanged mip-zero detail, filtered linear radiance and
coverage, minification, empty capture edges, real black geometry, and actual
main-water/foam composition code. Both long and compact stripe layouts are
checked against CPU ray results. This is targeted validation, not a full-suite
or Windows validation claim.

Negative controls fail as intended: disabling mip generation and sampling
caused 1,075 checks to fail; restoring captured sky on primary misses caused
4,389 checks to fail. The original Intel traversal failed the expanded fixture
with 3,073 failed checks. Temporary diagnostic modes were removed afterward.

Fixed-frame native captures use private copies of the SW General save and
options, 1920×1080, High waves, Full refraction and the other Ultra settings.
The restored material sky is visibly brighter/more detailed than the old
captured gradient; this is an intentional appearance correction. The supplied
screenshot is more zoomed in than the saved camera, so these captures do not
prove every marked screen-edge symptom resolved. The edge regression test
checks the underlying sky mismatch directly. A Hybrid comparison of 144,950
selected water pixels found 342 differing pixels, with a maximum RGB-channel
difference of 2; unrelated animation/shadow differences remain possible.
The final executable's Path capture at logic frame 106577 completed and was
visually inspected; geometry coverage, shoreline continuity and the restored
sky appearance remained consistent with the earlier candidate capture.

The preserved baseline is `build-linux/water-artifact-validation/baseline/generals`.
Screenshot inputs, captures, negative controls and profiling artifacts are
under the ignored `build-linux/water-artifact-validation/` directory.

Two serialized runs per executable measured 180 host frames after 25 seconds,
with Path reflections and the same saved view/settings described above:

| Executable | Mean frame time, ms | FPS derived from mean | Frame-time p95, ms |
| --- | --- | --- | --- |
| Checkpoint, run 1 | 15.770 | 63.41 | 41.609 |
| Checkpoint, run 2 | 15.304 | 65.34 | 40.701 |
| Final, run 1 | 13.678 | 73.11 | 28.596 |
| Final, run 2 | 14.067 | 71.09 | 37.076 |

Mean frame time across the two runs decreased from 15.537 to 13.873 ms
(10.7%). These are live-scene host timings, including normal GPU backpressure,
not per-pass GPU timings or a 60 FPS minimum. Background activity and scene
timing were not fully controlled. This small comparison is a performance
guardrail for this patch, separate from the earlier Hybrid-mode 60 FPS goal.

The initial mip/sky candidate with the old traversal measured 18.105–21.052 ms.
Temporary mip controls and traversal probes investigated that regression;
the final structured traversal passes the guardrail with full mip generation
and the original three material lookups retained. The intermediate experiments
do not establish an isolated sampling-cost estimate for the final shader.

Initial wide-view fix executable SHA-256:
`45a5d323634cddc25d734d9242b432e86d2d2889f773321707f25df30336b239`.
Checkpoint executable SHA-256:
`5004740446f9e1da6a63480d0f7665deeaf76fcfbb5dd400e3fdad57089130a5`.
The raw run manifests and `performance-final.json` retain the commands,
private settings, source-save hash and executable identities. The installed
game and the source user profile/save were not replaced.

## Close-up reflection follow-up

The supplied pillar screenshots were reproduced with a native Cinema camera following
`GreekRuin02` at relative zoom `0.18`, preserving the saved heading and pitch. These
are actual 1920×1080 game renders, not enlarged crops. The private SW General save,
High waves, Full refraction and other Ultra options remain the inputs.

Raw-capture, coverage, distortion-disabled and Hybrid comparisons separated the
source capture from the final water material. Live captures stayed at 128×128 and
only a few accumulated samples because scene animation kept invalidating history.
A verified paused diagnostic reached 256×256 and 64/64 samples; even that converged
image retained flat coloured faces. The exporter reduced each triangle to one
UV-centroid texture colour, so accumulation could not recover the lost texture.
The raw capture attached the pillars to their reflected shapes; the evidence did
not establish an incorrect reflected-camera projection in this view.

The follow-up changes are:

- Capture density now follows camera origin, water plane, crop rays and requested
  size. Animated geometry, material, lighting and transport changes still reset
  lighting history, but do not hold a stationary camera at 128. Camera motion keeps
  the existing reduction, followed by recovery after 150 ms and eight quiet calls.
- Primary rays stay at pixel centres. Secondary light samples remain stochastic.
  This removes repeated silhouette/material-face jitter when accumulation restarts.
  It also replaces the old temporal subpixel coverage sampling; thin silhouettes
  can still alias as the camera moves. A small barycentric edge tolerance avoids
  cracks where neighbouring triangles both rejected exact shared-edge rays.
- Eligible opaque mesh primary hits preserve UVs and base diffuse colour. They
  sample the retained texture at the actual hit, with wrapped trilinear filtering
  selected by an analytic ray/triangle-plane footprint. Linear mip chains include
  odd-sized edge texels. Texture snapshots are at most 64 pixels per side; the
  atlas is capped at 8 MiB of texels, separate from attributes, upload buffers and
  CPU copies. Geometry reorder/refit and dropped degenerate faces preserve source
  material associations. Unavailable/cap-exceeded materials retain the fallback.
- Secondary material approximation, geometric normals, terrain material detail,
  lighting model and exported-geometry exclusions are unchanged. The immutable
  texture atlas is not uploaded again for ordinary animated geometry refits.

Release `generals`, `test_water_path_gpu`, `test_water_path_tracing` and
`test_shader_sdl` build. All three targeted tests pass on NVIDIA; the GPU suite
also passes on Intel UHD 770, with 122,285 checks and no failures. New coverage
includes fixed silhouettes across accumulation and scene changes, stationary
full-resolution recovery under animation, UV/material mutations, BVH reorder and
refit, degenerate removal, invalid payloads, wrapped bilinear detail and mip
minification. A repeated high-frequency texture remains at its filtered linear
mean under subtexel UV shifts. Existing sky/coverage, capture mip, indirect-light,
shadow-budget and Intel traversal checks remain enabled.

The production executable was captured again at the matching close-up, including
raw capture output. Paired six-second native sequences hold the camera for one
second, orbit 12 degrees over two seconds, then settle. Each contains 360 actual
1080p frames, encoded at 60 FPS without resizing. Representative stationary,
orbit and settled frames were inspected; the reflections follow the geometry and
recover detail when the camera settles. This is visual and targeted regression
validation, not proof that all moving-light noise is gone.

Artifacts under the ignored `build-linux/water-artifact-validation/` directory:

- `closeup-discovery-1/`: prior build at matching native zoom.
- `closeup-attribution/`: raw/coverage/distortion and Hybrid comparisons.
- `closeup-paused-3/`: verified 256×256, 64-sample diagnostic. The paused process
  required explicit termination after its screenshot; its manifest preserves that
  outcome rather than reporting a clean exit.
- `closeup-mips-final/`: final normal and raw close-ups.
- `closeup-motion-before/` and `closeup-motion-final/`: paired native frames and
  `GreekRuin02-path-106577/water-closeup.mp4`.
- `closeup-mips-nvidia-tests.log` and `closeup-mips-intel-tests.log`: final checks.

The performance check kept the original saved wide camera (not the close-up),
1920×1080 and the same Path/High/Full settings. Four serialized runs used
180 host frames after 25 seconds, in before/after/after/before order:

| Run | Mean frame time, ms | FPS from mean | p95, ms |
| --- | --- | --- | --- |
| closeup-perf-before | 16.851 | 59.34 | 40.761 |
| closeup-perf-after | 16.456 | 60.77 | 43.928 |
| closeup-perf-after-2 | 16.780 | 59.59 | 43.990 |
| closeup-perf-before-2 | 132.677 | 7.54 | 243.013 |

The two candidate runs together correspond to about 60.2 FPS. The second baseline
run is a large unexplained outlier; multiple CPU phase timings rose sharply too,
but external interference was not established. All four manifests/logs are
retained in `performance-closeup-fixes.json` and the named run directories. These
results do not establish a speedup or an FPS floor. The visual fixes restore more
capture work in stationary animated scenes, so their cost can vary by scene/GPU.
This check is separate from the completed Hybrid-mode optimization target.

Close-up material-fix executable SHA-256:
`868bfc2899a22c993f472a10a2f2585066d0cd0c65fb075d75a6432d4c2ec760`.
The comparison executable is the previous visual-fix build, SHA-256
`45a5d323634cddc25d734d9242b432e86d2d2889f773321707f25df30336b239`.
Temporary backend probes were removed. The four developer-only Path attribution
views remain documented in `water-preview.md`. No installed game, source save,
profile or audio implementation was changed.

## High-detail Path capture at 512×512

The existing **Reflection detail** preference now applies to Path mode in the
Linux game and synthetic preview. High uses 512×512 while stationary and moving.
Low retains 256×256 while settled and 128×128 during camera motion, with the same
150 ms/eight-call recovery. SSR resolution is unchanged. Existing Legacy/Hybrid
planar sizes are unchanged. The English tooltip and all five localized entries
now describe this choice; the saved preference format is unchanged.

The capture View carries a separate motion-size request, so High never silently
falls back to 128. Both requested sizes participate in history change detection.
Changing the option through Accept, including while paused, reaches the renderer
at its existing synchronization boundary and resets the capture history. High
traces four times as many pixels as settled Low and sixteen times as many as
moving Low; this is a pixel-count ratio, not a whole-game frame-time estimate.
Its two mipmapped RGBA16F histories use approximately 5.33 MiB instead of 1.33 MiB
at settled Low, separate from geometry/material resources.

Release `generals`, `water_preview`, `test_water_path_gpu` and
`test_water_path_tracing` build. CPU policy tests and the GPU suite pass on NVIDIA;
the GPU suite also passes on Intel UHD 770 (122,402 checks, no failures). Tests
exercise High 512 during repeated camera/material changes, High/Low switches with
fresh history and Low 128-to-256 recovery. Two-frame standalone smoke runs report
High 512 and Low 256. Native matching close-ups at logic frame 106577 report the
same 512/256 targets and were visually inspected. Fine pillar reflection edges
are more resolved at 512; the existing stochastic lighting/material limits remain.

Native screenshots are under `path-512-capture-high/` and `path-512-capture-low/`,
inside the ignored `build-linux/water-artifact-validation/` directory. Their
manifests retain private profiles and unchanged source-save hashes. The source
profile/save and installed game were not replaced.

The RTX 4060 timing comparison keeps the original saved wide camera at 1920×1080,
all other water settings High/Full, and a live battle. It measures 180 host frames
25 seconds after first Present, in Low/High/High/Low order:

| Reflection detail / run | Mean frame time, ms | FPS from mean | p95, ms |
| --- | --- | --- | --- |
| path-512-perf-low-1 | 22.580 | 44.29 | 54.904 |
| path-512-perf-high-1 | 19.108 | 52.33 | 46.739 |
| path-512-perf-high-2 | 25.644 | 39.00 | 55.280 |
| path-512-perf-low-2 | 24.820 | 40.29 | 56.407 |

The runs vary too much to isolate a trustworthy FPS penalty: High ranges 39–52 FPS
and Low 40–44 FPS. The apparent average advantage for High is not evidence of a
speedup. All runs are retained in `performance-path-512.json` and their named
folders. The earlier 60 FPS observation belongs to the 256 capture checkpoint;
it is not a guarantee for High 512, other scenes, or camera movement.

Current executable SHA-256:
`20e983626c0c2d9145e96279fc98e60b2314efb07c742d690a123c681536b6c3`.
Validation logs: `build-path-512.log`, `path-512-nvidia-tests.log`,
`path-512-intel-tests.log`, and `path-512-preview-{low,high}.log`.

## Unit-ready voice and pause-menu echo report

The staged `INIZH.big` archive defines `LaserCrusaderVoiceCreate` in
`Data\INI\Voice.ini` as `Type = world global player`, without the `voice` flag.
`Data\INI\Object\LaserGeneral.ini` assigns it to
`Lazr_AmericaTankCrusader` as `UnitSpecificSounds VoiceCreate`. Production adds
the new object's ID to this event, making it positional because it is a `world`
event with an object owner. The 3D dry-state setter tests only the `voice` flag,
so this ready announcement receives the world-room send. The event and routing
classification are longstanding; the object-attached `VoiceCreate` route and
positional classification date to 2025.

The chronology does not establish a preload regression. Audio preload entered
the branch in commit `3778135e` on October 26 at 10:17. The upstream merge
`26ae3689` followed at 10:33 and brought in map reverb (`c6b1b0fc`) and dry
`ST_VOICE` replies (`73139143`), both authored October 24. Thus the room-effects
feature arrived on this branch shortly after preload, rather than predating all
preload use.

The code audit found that preload caches encoded file bytes without playing
events (`GameAudio.cpp:386-445`; event filename enumeration is
`AudioEventRTS.cpp:861-894`). Playback opens an independent `RAMFile` view of
those bytes (`GameAudio.cpp:447-458`, `RAMFile.cpp:254-270`) and follows the
existing WAV inspection and sample-loading path (`MilesAudioManager.cpp:4497-4514`).
Renderer pipeline warmup is separate and does not play audio
(`PosixDevice9Warmup.cpp:83-204`). No concrete preload or decoder defect was
identified. The existing test
`preloaded_audio_preserves_bytes_random_streams_and_independent_file_lifetimes`
checks byte equality, random-state preservation and independent readers
(`test_milesaudiomanager.cpp:623-680`), but it was not rerun for this report.

Pause-menu UI events lack the `world` flag and route as 2D. A lingering tail
from a preceding wet world sound could overlap a menu sound, but that remains a
hypothesis. No audio sample was supplied, so the reported artifact has not been
heard or reproduced. A useful comparison is to reload the same save once with
preload enabled and once with `ZH_AUDIO_PRELOAD=0`, keeping audio options fixed;
changing the variable mid-match does not clear the already loaded cache. At the
user's request, the proposed ready-announcement audio change and its test were
reverted. No speaker/profile settings were changed, and no audio source changes
remain in this patch.
