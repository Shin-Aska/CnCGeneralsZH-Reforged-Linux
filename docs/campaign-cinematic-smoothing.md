# Campaign cinematic smoothing

Camera and skeletal-animation smoothing run automatically during normal single-player play.
Ordinary model interpolation eligibility remains an opt-in experiment. These presentation changes
do not change the mission's requested simulation speed, scripted camera completion counters or save format.
Normal-play captures on a private software display verify removal of the repeated-frame aircraft
bounce described below. Physical-display appearance, scanout and audible results remain unverified.

## Running the game

No flag is needed for the single-player camera and animation fix:

```sh
# Default single-player camera and animation smoothing
./run-game.sh

# Optional live model-smoothing eligibility (B)
ZH_SMOOTH_MOTION_LIVE_RATE=1 ./run-game.sh
```

Use the project's usual game launcher in place of `./run-game.sh` if it has a different name.
The model-smoothing setting is read from the process environment for that run and is not saved to
preferences. Unset it, or set it to any value other than `1`, to leave B disabled. For a useful B
comparison, use the same map, display mode, refresh, render cap and other settings in each run.
Review the takeoff, flyover and bombing pan, plus
hard cuts, pause/resume and ordinary gameplay. Stop the game normally after the opening.

The retired `ZH_CINEMATIC_CAMERA_SMOOTH` variable is ignored, including when set to `0`.
There is no replacement camera toggle. Historical A/B labels below refer to the earlier opt-in builds.

## What the USA Mission 2 investigation established

The Zero Hour USA Mission 2 intro requests 22 retail frames per second, which this engine converts
to 44 logic ticks per second. Its carrier takeoff shot combines a three-second scripted turn and
pitch. Those two camera channels retained their original whole-step updates, about 22 per second,
while waypoint camera translation and skeletal animation can update between those steps.

The default smooth-motion eligibility check compares adapter refresh against the constant 60 Hz.
A 60 Hz adapter therefore does not automatically interpolate ordinary object transforms while
this mission runs at 44 Hz, even when smooth motion is enabled in the user's preferences.

The later close-up aircraft are cinematic skeletal props, not ordinary jet locomotors. Their
world-space roots are stationary; animation moves their aircraft bones. The three Raptor clips'
largest adjacent translation changes are below the raw-animation 20-unit cut threshold, and
their adjacent rotations pass the existing rotation-cut test. The actual carrier and hatch/ramp
assets also pass the translation guard. Another asset, `PSAirCarrierTp`, has fast translation
channels which fail that guard, but it is not bound to these mission objects and does not explain
this scene. Raising or removing the global animation cut threshold is not part of these changes.

The later normal-play comparison exposed a third defect: the renderer held W3D animation time
on repeated simulation frames, while the waypoint camera kept moving. A trace of the close-up
flyover captured unchanged animation and all three aircraft mesh transforms across a repeated
client frame, with 15 ms of camera progress and a 21.8-pixel foreground aircraft displacement.
The earlier, slower GDB traces contained no repeated client frames and therefore missed this path.
This finding does not establish that the experiments address every source of visible jitter.

## Automatic single-player camera and animation presentation

During ordinary single-player wall-clock play, W3D animation time advances on normal
renders which repeat the same client simulation frame. The existing elapsed-time, slowdown and
animation interpolation formulas are unchanged. This keeps skeletal aircraft moving while the
waypoint camera moves between logic ticks; it does not change the waypoint camera clock or path.
Real pause, debug/script freeze and the frozen-time camera loop retain their existing animation
freeze. The change is excluded from network games, replays, headless runs, shell/loading/clearing
states, scripted fast time and command-line turbo. Both the fractional camera display and animation
clock use that normal-play eligibility policy; the mission-speed preference remains supported.
Configured video sessions are excluded throughout pre-roll, recording and post-roll, even after
the recording sink finishes. Configured screenshots, a pending F12 screenshot, shadow-capture
clock mode and multiple pictures per simulation frame also keep the previous duplicate-picture
guard. Additional phase-4 pictures beside a tick retain their existing held W3D clock.

Pause and debug freeze hold the fractional camera pose without changing session eligibility.
A world/script freeze deliberately retains the authored camera movement allowed by the legacy
camera steppers, while the skeletal-animation clock holds. The frozen-camera inner loop also
keeps its legacy path. This distinction avoids hiding authored camera motion during a world freeze.

W3D animation completion can trigger model transitions, debris effects and embedded animation
sounds. Those consumers can now observe intermediate normal-play poses between logic ticks;
recorded-picture controls alone cannot validate their normal-play timing. Script timers and
authoritative camera completion are unchanged. Normal-intro and replay checks passed as detailed
below; hardware appearance and audible results still require observation.

Zoom, effect pitch and fixed-target rotation are evaluated from their authored easing curves at
the current whole step plus the fractional remainder of the **existing** camera step clock. The
integer steppers retain ownership of counters, scalar camera state, completion flags, holds,
mission speed and script queries. There is no previous/current camera-pose buffer and no added
whole-step delay. The resulting display scalars feed the camera matrix and its pitch-dependent
clip planes; the camera's scalar getters continue to return the authoritative values.

The audio listener and ambient effects already use the actual camera eye. That API and its formulas
are unchanged; the listener's spatial position and zoom-based attenuation can now vary between
scripted camera steps. Scripted audio cue timing is unchanged. Audible behavior on hardware has
not been verified, and numerical identity of spatial audio is not claimed.

A new action starts exactly at its first pose. If it arrived part way through the shared step
interval, presentation covers the first step over that interval's remaining time, then uses the
ordinary fractional step. One-step actions remain cuts. Pausing or freezing for debugging holds
the last displayed progress. Resume starts at that held pose and uses the shared interval's remaining
time to reach its next boundary; paused wall time cannot jump the display ahead. Replacement actions,
manual camera controls, reset/load cancellation and other camera owners invalidate the previous
presentation state. Rotation keeps the authored
unwrapped sweep, including reverse rotations and multiple full turns. Constraint rebuilding uses an
explicit authoritative camera transform, so display interpolation cannot alter its boundary or look point.

Additional pictures rendered beside a logic tick advance from the last ordinary client pass's cached
phase, wall-clock epoch, rate and eligibility. They stop at the next authoritative camera boundary.
That path changes only presentation state and camera matrix/clip planes: it does not query logic,
rebuild constraints, update the terrain center or step camera shake. Picture-clock and frozen-time
modes do not advance from that cached wall clock.

Recorded pictures retain legacy camera output and the existing picture clock. Recording has its
own engine clock and forced object interpolation; recorded footage alone cannot establish the
behavior of normal wall-clock play.

These cases deliberately retain their existing path:

- Incremental camera rotation which tracks a moving object. Independent zoom/pitch channels can
  still use the experiment alongside that rotation.
- RealZoom mode, whose legacy transform builder also changes field of view and effect pitch.
- The frozen-time inner camera draw loop, drawable camera locks, and free, slaved or diagnostic
  camera ownership.

## B: ordinary model interpolation eligibility

Set `ZH_SMOOTH_MOTION_LIVE_RATE=1` to opt into the POSIX experiment. Windows retains the legacy
eligibility check. Existing forced smoothing, explicit opt-out, headless and recorded-picture
controls retain their precedence.

The experiment compares the scheduled logic rate with a median of successful, visible SDL
swapchain submission intervals. It needs at least eight intervals spanning approximately half a
second. Interpolation turns on above 102% of the scheduled logic rate and turns off at or below
101%. The current engine render cap also bounds the estimate; a cap no higher than the logic rate
immediately disables automatic interpolation. The POSIX backend's default 60 Hz refresh value is
not treated as a display measurement. Pauses, loads, long gaps and rate/cap transitions discard
old timing evidence. A user opt-out or forced-display mode also discards automatic timing evidence;
returning to automatic selection starts a fresh observation window.

The measured cadence is the rate at which visible swapchain submission calls return. It is not a
measurement of GPU completion or physical display scanout. Hidden, minimized, offscreen,
unacquired-image and failed submissions do not provide evidence. This keeps automated offscreen
runs useful as controls, while leaving visible hardware testing necessary.

## Validation and remaining checks

The automatic single-player build is
`d8a5ebf22abf6578d2266af6ed5bceef22a90b75ea610d74f5805eb3f9243628`.
Its Release focused checks pass: fourteen camera/clock tests (41,354 checks), nineteen
model-smoothing tests (475 checks), the mission-speed test (five checks), and manifest verification
(4,739 entries). The new tests use the production session snapshot, cover the retired flag absent
and set to `0`, `1` or `off`, actual game-mode/playback/loading exclusions, cached extra-picture
invalidation, and the distinction between pause/debug hold and authored world-freeze camera motion.
The constructor leaves eligibility inactive until the ordinary client update captures the session.
An isolated mutation which disables that production eligibility capture makes the new default and
retired-flag regression fail twelve of its 29 checks; it does not replace the canonical engine.
The current default-no-flag normal-play and contract results for this promotion are recorded below.
Earlier opt-in executable results are identified separately where they provide historical context.

The repeated-frame animation-clock candidate is
`64ebff3abdd113b7da2e91b3128caa8417dae1f4a92518924dc8485a43db707b`.
Its Release focused checks pass: eleven camera/clock tests (41,262 checks), nineteen model-smoothing
tests (475 checks), the mission-speed test (five checks), and manifest verification (4,739 entries).
Restoring the unconditional repeated-picture freeze in the actual production display guard makes
the two new mechanism tests fail (4,033 failures across 7,883 checks). Their rigid-point projection
is synthetic; it verifies the clock mismatch mechanism, rather than the complete rendered mission.
The shader-test executable is byte-identical to the prior candidate; reuse and execution of the
shader checks are accounted for separately in the suite results below.

The matching normal-clock A+B comparison uses the prior `601ca810…` executable and the new
`64ebff3a…` executable on the same private 640×360 software display, captured externally at
60 fps and played at the original speed. The close-up flyover's repeated-frame pulse is visibly
removed in that capture. For the foreground aircraft's measured 60-frame window:

| Measure | Prior A+B | Updated A+B |
| --- | ---: | ---: |
| Vertical high-frequency centroid residual, RMS | 5.936 px | 1.002 px |
| Largest consecutive-frame displacement | 17.126 px | 4.608 px |
| Repeated HUD-frame pairs with an upward residual over 5 px | 15 / 15 | 0 / 16 |
| Mean vertical residual on repeated HUD frames | −9.660 px | +0.064 px |

These are image measurements for this crop and capture, rather than a general smoothness score.
The comparison covers the reported 0:09–0:12 flyover; its centroid measurements isolate a shorter
60-frame window. The video and measurements are in
`build-linux/campaign-jitter-evidence/residual-flyover/new-fix/` as
`residual-fix-comparison.mp4`, `jet-summary.json` and `repeat-upshift-summary.json`.

The current default-no-flag executable sampled 181 visible normal-play targets at 59.919 Hz.
Eligibility was active for all 682 client updates; 414 camera poses were valid, camera pitch moved
between every one of 101 repeated authoritative pitch pairs, and 179 intermediate zoom values were
observed. All 48 repeated-client pairs advanced W3D animation, skeletal animation and camera path
time. B's separate live-rate eligibility remained inactive. See
`build-linux/campaign-jitter-evidence/default-single-player/clock-summary.json` and the adjacent
`clock-trace/` logs. This confirms use of the default production render path above the mission's
44 Hz simulation rate; it does not measure physical scanout.

The current default-no-flag USA02 opening matched all 355 ordered script actions, including
arguments, script names and authoritative frame numbers, against the immutable A-enabled reference
`64ebff3a…`; both handed input back at frame 2577. See
`build-linux/campaign-jitter-evidence/default-single-player/normal-opening-comparison.json`. This
validates the opening script and handoff contract, not every intermediate effect or audio event.
The final default-no-flag USA02 and China01 controlled goldens also matched baseline exactly:
390 USA02 contract events with handoff at frame 2577, and 282 China01 events with handoff at frame
3547. The 12,000-frame default replay and armed-wrapper check completed with normal exit. These
contracts and replay results are recorded under
`build-linux/campaign-jitter-evidence/default-single-player/`.

The final ledger covers 164 tests: 44 freshly executed and 120 unchanged test-case reuses, including
all 41 shader cases. It records 141 passes, 23 skips, zero failures and no closure gaps. Two
sandbox-socket cases passed their required outside-sandbox reruns. The bounded original-USA1 startup diagnostic also passed
with normal exit: automatic eligibility was active, with four successful present returns, five
script passes, one prewarm and no headless, video-end-frame or screenshot mode. See
`build-linux/campaign-jitter-evidence/default-single-player/original-smoke/results.json`. Its
initial diagnostic issue was a harness lookup of the wrong debugger symbol; the corrected passive
read confirmed behavior without engine or source changes.

For the earlier `64ebff3a…` A-enabled executable, all four controlled OFF/A goldens matched the
baseline exactly: 390 contract events for each USA02 run and 282 for each China01 run, with input handoffs at frames 2577 and
3547 respectively. Recording/capture modes deliberately retain the prior animation-clock guard,
so these are camera/script controls rather than evidence for the normal-play clock change.

A separate normal-clock USA02 comparison of old `601ca810…` A and new `64ebff3a…` A matched all
355 ordered script actions, including arguments, script names and actual authoritative frame
numbers. Both handed input back at frame 2577 and exited cleanly. It used an offscreen 320×240
observer diagnostic without video or `-recordfps`; it checks opening script/handoff contracts,
rather than physical pacing or equality of every intermediate effect or audio event.

That candidate's active 12,000-frame A+B live/replay/repeat check matched CRC `6C6CF1BA`
with 22 structures built. An intentionally edited seed diverged to `E18825AF`, and the negative
control was detected. Both installed-asset guards confirmed all 370 entries retained their sizes,
timestamps and contents.

That earlier candidate's suite accounted for 164 unique tests: 137 newly executed and 27 explicitly
reused shader results. The reused checks are IDs 99–125; their executable SHA remains
`a740f85d69e7925c76822eb162ddbc49bd85c4d2fc4322a9f549a4377a990b1d`, with unchanged shader inputs
and system dependencies. The remaining fourteen shader checks, IDs 126–139, were executed again.
The aggregate is 141 passes, 23 skips and no unresolved failures after the two sandbox-socket
failures passed their required reruns outside the sandbox. Skips include asset-backed audio,
video, terrain and archive tests, optional game/tool audits, and external architecture/D3DX
oracle checks; individual names and log provenance are recorded in `verification-summary.json`.
That file, `normal-opening-comparison.json`, `goldens/*-comparison.json`, `shader-reuse.json` and
`replay-active.log` are in the same `new-fix/` evidence directory.

The earlier `d04d5697…` mechanism traces belong to that prior executable and are not the final
artifact's validation. They showed A adding displayed intermediate poses in the ordinary USA02
takeoff (113/114 displayed states/renders versus 90 authoritative states), while preserving the
90 authoritative states; the scalar-camera sampler was inactive in shots 06a and 08a. These results do not establish a
perceptual improvement.

The prior candidate was `601ca810d0439a02971dc59ff3bf3d95eebb2d3ef3a380b6a45e7f6f6229c644`.
Its earlier private visible-Xvfb B integration run exercised 3,298 presentations, observed
zero hidden or no-drawable presents, measured a 62.60 Hz median submission cadence against a
44 Hz logic rate and 60 Hz cap, and exited normally at frame 2375. This confirms virtual-display
submission integration only. The accompanying passive timing summary (2,917 frames, 16.704 ms
mean, 16.635 ms p50) does not measure physical scanout or perception.

The camera tests drive actual W3D camera movers and matrix construction at 60, 120 and 144 Hz
presentation with 22 and 30 Hz authoritative camera steps. They compare authoritative scalar
getters, counters and completion flags against a control view, check that the rendered matrix
changes between steps, and cover easing, endpoints, angle wrapping, multi-turn arcs, new-action
phase, repeated pause/resume, cuts, cancellation, canonical constraint boundaries, extra-picture
clock advancement, excluded modes and recorded-picture phase.

The camera fixture runs without GPU assets; it also exercises the actual extra-picture camera
publication entry point.

Three earlier isolated camera mutations demonstrate that the integration checks detect regressions:

- Removing displayed zoom/yaw/pitch consumption from the matrix builder fails all three independent
  channel checks (three failures in sixteen checks).
- Rebuilding constraints from the displayed pose fails the boundary and canonical-position comparison
  (six failures in ten checks).
- Removing extra-picture camera publication fails the published-matrix comparison (one failure in
  eleven checks).

The pure sampler's separate isolated mutation restores whole-step output: 984 of 1,296 cadence
checks fail, while fractional evaluation passes all 1,296. Mutants are built and linked separately;
the canonical engine and source are not replaced. Earlier asset guards also passed twice, confirming 370
replayed assets retained their sizes, timestamps and contents.

Camera and animation smoothing are now automatic in normal single-player play; B remains default-off.
The historical private software-display capture establishes a visible fix for the measured
repeated-frame flyover pulse. Physical-display comparisons at 60 Hz and an available high-refresh
mode, with per-shot review, remain unperformed. Broader hardware and
perceptual conclusions, GPU scanout and audible spatialization remain unverified.
