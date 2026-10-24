# SDL sun shadow maps

Normal Linux launches now use mapped sun shadows and modern water without rendering opt-ins.
The [default rendering rollout](#default-rendering-rollout-linux-2026-10-16) records tree/palm,
water receiving, state/resource handling, packaging and Linux validation. Explicit legacy and
Classic-water settings remain available.

The checkpoint sections below retain the investigation and intermediate results. Their opt-in
status and exclusions are historical; the default rollout supersedes them. Earlier evidence is
in `build-linux/shadow-phase1/` and `build-linux/shadow-phase2/`; current evidence is in
`build-linux/render-final/`. The work starts from `3ca78ee6` on `codex/sdl-sun-shadows`.

## Phase-one scope and gates

Reuse the Direct3D 11 sun camera, caster selection, animation handling, blocker search,
height-dependent filter, and 1024/2048/4096 quality levels. Support registered unit and
building casters and solid receivers, including terrain, roads and bridges. Keep dynamic
lighting independent of shadow capability. Trees, water receiving, smoke maps, cascades,
and new filtering algorithms are outside this phase.

| Checkpoint | Current evidence / remaining gate |
| --- | --- |
| 1. Repository and baselines | Clean starting tree, preserved executable, real-scene captures, repeatability investigation, isolated performance runs. See below. |
| 2. Development modes | Mode parser, defaults, changes and engine gates implemented; Release build and checks pass. All three live selections save frame 800, reach frame 860 and agree with the untouched baseline CRC. Linux defaults to legacy. |
| 3. Backend interface and sampleable depth-only target | Shadow-only dispatch, sampleable D32 target, optional color attachment and target-specific pipeline format are connected. GPU target tests pass. |
| 4. Caster pass / capture-only | Connected. Native tests cover alpha/additive rules, scene color/depth/state and failure. Real solid/helicopter captures have depth occupancy and baseline CRCs. The settled helicopter frame is pixel-identical to legacy, with matched CRC. The settled capture-only image is preserved after scene ownership changes, and baseline replay playback matches in all modes. A depth-map crop directly shows the rotor blades. See compatibility and final validation. |
| 5. Receivers | Shared-filter receiving is connected. Native GPU tests cover all eight terrain/road programs and fixed solids, offset/depth-range reconstruction, filter controls and exclusions. Active solid/helicopter captures and all three sizes are observed. Object-tree/sorting exclusions are built. The bridge cutout material now receives, with deck-crossing image evidence. Compatibility results are below. |
| 6. Edge cases | Target-level empty clears, size changes, retirement, invalid sizes and scene resize pass GPU checks. Failed captures, live mode changes, queued receiver map replacement and camera/viewport checks pass. Nested scene/target ownership, no-GPU refusal and device reset now pass native GPU checks. A real classic-water reflection is preserved. Optional shader/pipeline and allocation failures now have tested fallback; see the failure increment and its late-failure limitation. |
| 7. Final validation | Final checks pass 11/12, with the unchanged baseline reference mismatch. Controlled bridge/water/forest/rotor and replay evidence is recorded below. All 18 matched performance runs retain their baseline CRCs; unsupported coverage and hardware limits remain explicit. |

## Development modes

`ZH_SUN_SHADOW_MODE=legacy|capture-only|mapped` selects the development path. Unset is
legacy on SDL, preserving the shipped Linux behavior. Windows retains its mapped default.
Unrecognized values select legacy. This is a process setting, not a saved gameplay option.

- **legacy** skips sun capture, disables stale receiving parameters and retains stencils.
- **capture-only** requests the sun pass, leaves receiving disabled and retains stencils.
- **mapped** requests capture and receiving; stencils may stand down only after success.

The SDL backend captures casters in capture-only and mapped selections. Mapped now enables
supported receivers after successful capture and coordinate setup. Stencils stand down only
for the captured scene view/target. A requested mode alone is not evidence of active receiving;
the optional native report includes accepted receiver draw counts.
`Direct3D11_Lights_Per_Pixel` is unchanged.

The earlier pre-caster runs in
`build-linux/shadow-phase1/modes/{legacy,capture-only,mapped}-solid` record the
requested mode in stderr, a saved frame-800 image, successful exit, and identical
frame-860 CRC `0x5331E5B7`. This validates selection and unavailable-backend fallback;
it does not validate an active capture or mapped shadow.
Against this build's legacy image, capture-only differs by more than 8 channel levels
on 9,016 pixels (mean max-channel difference 0.874/255); mapped on 6,628 pixels
(0.427/255). Both sit within the untouched repeat's measured noise, not pixel identity.

## Depth-target increment

`SdlGpuFrame::Sun_Shadow_Target` requires D32 depth with both attachment and sampler
usage. Unsupported usage, invalid size or allocation failure returns no usable target.
Replacements are allocated before retiring the old map through the existing batch
retirement mechanism. Window resize leaves this independent allocation intact.
Targets carry their own depth format; color attachments and color pipeline outputs
can be absent. Sun captures use whole-target depth clears.

The new `sdl_gpu_frame_selfcheck` case runs actual rasterization and sampling through
the production no-color pipeline cache. It checks untouched scene color, triangle
depth and clear depth, queued sampling of an allocation replaced before submission,
map reuse across window resize, invalid sizes, and removal of stale depth in an empty
capture. The checked values are independent expected depths (0.25, 0.6 and 1.0).

The rebuilt GPU test, pipeline-state test and draw-resolution test pass **3/3** with
`ZH_GPU_DEBUG=1` on NVIDIA Vulkan, with no validation error reported. Restoring the old
mandatory-color pipeline assumption makes the GPU test abort on SDL's invalid-format
assertion; restoring the implementation returns to **3/3** passes. Logs are
`depth-target-control.log` and `depth-target-tests-restored.log` under the validation root.
`depth-target-tests.log` was accidentally started before the new binary finished linking
and is explicitly superseded by the rebuilt/restored logs, which contain the new test's
`sun depth target` evidence line.

These tests prove the target mechanism, not caster correctness, receiving, sun matrices,
real-scene capture preservation or performance of mapped shadows. Those gates remain open.

The final checkpoint Release build passes (`checkpoint-final-build.log`). The nine-test
selection repeats **8/9** (`checkpoint-final-tests.log`): the only failure is identical to
baseline, two of 1,675 written pixels in `draw_00001.cap`, worst `(33,20)`, GPU
`(2,50,65,255)` against reference `(3,53,65,255)`. Depth-target checkpoint executable SHA-256:
`590257474e88a6c9c7a8840e7109a10a77cbe62f5400c097062142546c92fe5f`.

The two review increments are saved locally as `01-baselines-and-modes.patch` and
`02-depth-target.patch` under `build-linux/shadow-phase1/`, and remain staged/unstaged
respectively. A configured GPG signing key was locked; no unsigned commit was substituted.

## Caster integration

`Platform/SunShadow.h` separates the sun pass from Direct3D identity and lighting capability.
Windows dispatches to its existing implementation. SDL records into the native depth-only
map, captures all mutable D3D draw state and holds binding references across queue flushes,
then restores the scene. Engine cache invalidation runs before device restoration because
invalidation itself unbinds textures. Capture submission failure or a refused draw prevents
map acceptance. Empty captures clear old depth; absent registered units no longer excludes
bridge-only scenes. Terrain-less scenes cannot begin a capture.

The SDL caster variant calls the shared pixel program for alpha/discard behavior but has
no color output. Blended geometry writes depth at the existing 8/255 alpha threshold;
additive glows discard. Light meshes, sort-list draining, posed geometry, bridge bias and
sun camera/culling remain the existing algorithm. Smoke capture stays on the Windows
backend. Linux dynamic lighting capability is unchanged.

`caster-gpu-tests-final.log` records 4/4 GPU/device tests passing with `ZH_GPU_DEBUG=1`.
The device case verifies fractional-alpha depth versus additive rejection, unchanged scene
color and scene depth, restored offset viewport and render states, empty maps, resizing,
re-entry and failed-draw rejection. The rebuilt device case also passes explicit matrix restoration and retained texture-reference
checks (`caster-cadence-tests.log`).
The target test's former unused color-output warning was removed; the retained final test log
contains no Vulkan validation warning/error.

Retained real runs under `build-linux/shadow-phase1/caster/`:

- `capture-solid-2`: 2048² map; late reports have 39 casters, 456 draws and approximately
  121,000 occupied texels, depth 0.293989..0.635340. Frame-860 CRC `0x5331E5B7`, matching legacy.
- `capture-helicopter`: 34 casters, 466 draws and approximately 71,400 occupied texels,
  depth 0.258674..0.669349. Rotor and legacy rotor shadow remain visible. Frame-360 CRC
  `0x67482401`, matching the baseline fixture. This does not yet isolate the rotor in map readback.
- Both rebuilt shader runs report no Vulkan validation warnings/errors. The first
  `capture-solid` used a fragment color output with no attachment and produced an unused-output
  warning; it is superseded by `capture-solid-2`.

The initial image comparison is **not a preservation pass**: ordinary capture-only versus
legacy differs by more than 8 channel levels on 305,352 pixels (mean maximum-channel error
8.246/255), mostly clouds and animation. Fixing W3D time alone (`clock-legacy-solid` versus
`clock-capture-solid`) reduced that to 57,080 pixels and mean 2.583/255, but still left tree,
particle and animated-unit differences. These runs rendered 926 versus 772 presents: the
five-second screenshot settling interval switches out of turbo and catches up logic without
client updates when drawing is slower. Tree sway and particle updates depend on that cadence.

`--logic-clock` is an explicit screenshot diagnostic under development. It requests
`ZH_SUN_SHADOW_CAPTURE_CLOCK=logic`, fixes W3D scenery time, camera easing and presentation fraction, and
uses the existing one-picture-per-logic-frame turbo path through the settling interval.
It is excluded from performance measurements, multiplayer and replay. Ordinary rendering
is unchanged. Fixing cadence alone gave 1,449 pixels above 8 levels in the solid comparison, versus 47 in
a legacy repeat, and still left 244,712 such pixels in the helicopter scene. Camera easing
was still on wall time. The diagnostic now also selects the existing video camera clock.

`camera-comparison.json` retains the resulting comparisons. Solid: 425 changed pixels of
921,600, 351 above 8 levels, mean maximum-channel error 0.0342/255. The amplified difference
is confined to wall-clock timer text and a small animated effect around the power plant;
the terrain, buildings, dozer, trees, shadows and fixed HUD are otherwise identical.
Helicopter: the 205,000-pixel region containing the aircraft and rotor shadow is pixel-identical.
The top 460 rows differ on 83/588,800 pixels. The whole image still differs substantially
because the HUD is mid-slide in the faster legacy run and settled in capture-only; this is
visible in the retained images and matches the wall-clock transition source. At frame 600, `camera-legacy-helicopter-600` and `camera-capture-helicopter-600` are
**pixel-identical over all 921,600 pixels**, including aircraft, rotor/stencil shadow, ground
and settled HUD (`camera-helicopter-600-comparison.json`). Both end at frame 660 with CRC
`0x646DC72B`; capture reports 466 draws and about 71,400 occupied texels. The image was visually
inspected to confirm the scene is populated. This proves full-frame preservation for that
fixture, not universal scene/replay coverage. Both earlier scenes retain their baseline CRCs.

`--report` enables native depth occupancy readback and cannot be combined with `--measure`.
The older runner's `ZH_WATER_CAPTURE_CLOCK=picture` setting is inactive without that separate
clock's required control/video range; it must not be described as deterministic water here.

The expanded test selection passes 5/6 (`caster-cadence-tests.log`). Its one failure is
`test_gameengine`'s `minimap_classic_looks_on_the_press_in_both_mouse_setups`, assertion at
`test_minimap_input.inc:415`. The same isolated assertion fails after restoring `GameEngine.cpp`
to HEAD and supplying only an always-false stub for the new display symbol
(`caster-minimap-control-3.log`). Current source was restored and rebuilt successfully
(`caster-control-3-restored-build.log`). Two earlier control attempts are invalid evidence:
the first lacked the new display symbol; the second stopped at the user-data guard. The
minimap test is not reported as passing or fixed by this work.

`caster-camera-build.log` confirms the current full Release build. The focused GPU/device
and render-hook selection passes 5/5 (`caster-camera-tests.log`), including the additional
null-input failure path, with no Vulkan validation warning/error. Real `camera-capture-*`
runs likewise report none. Broader scene/secondary-view coverage, receiver integration,
replays and final matched performance remain pending.

The caster-checkpoint executable SHA-256 is
`090b0c1dcc3fe89b3b66755dcd04991b8979700cae8b1bf6ed650704e68ba627`.
`03-caster-capture.patch` is saved against increments 1 and 2, using a temporary index;
the real staged/unstaged boundary remains unchanged. No new commit was made.

## Receiver increment

SDL uses the existing `SHADOW_SAMPLING` and `SHADOW_APPLY`, including the 25-tap blocker search,
height-dependent radius, rotated filter and sky fill. Fixed-function solids use the shared
22-float4 constant layout; all eight transcribed terrain/road programs already declare that
layout. Native D32 depth is bound through logical t5/s5 with the existing point/clamp policy.
Disabled ground receivers bind white and leave strength zero, without disturbing water's t5.

Receiver setup caches the restored scene view, projection, viewport and color/depth target.
A different view/target is ineligible. Viewport origins and its depth range are undone before
reprojection. The capture's half-pixel viewport displacement is composed into the sun matrix.
The shared shader now uses `ShadowViewport.zw` for the receiving origin; D3D11 leaves these
zero, preserving its existing expression. No filter tuning or lighting capability changed.

Batched trees, water/shoreline and existing shadow passes have explicit receiver-exclusion
scopes. Object shrubbery classification follows `RenderInfoClass` into the deferred mesh
renderer, and sorting nodes retain exclusions and cannot combine draws with different
eligibility. Screen-space UI, custom non-ground programs, additive/multiplicative passes and
transparent fixed-function overlays remain ineligible. Windows exclusion dispatch is a no-op.

The first GPU run passed the analytic lit/shadowed pixel checks but failed two whole-image
restoration comparisons. The test cleared only its active viewport, leaving pixels from a
shifted viewport outside the next clear. Clearing the full fixture target fixed those checks;
production clearing was unchanged. The fixture now exercises all eight ground programs,
including a blended road layer, the generated solid receiver, nonzero viewport origin,
translated/scaled camera, non-default depth range, nested exclusions, view changes, live mode
changes, filter strength/bias/softness/sky fill, invalid parameters, and an old map sampled by
a queued draw before a different-sized empty map replaces it.

`receiver-tests-final.log` records 6/6 GPU/device/program-cache/render-hook tests passing with
NVIDIA Vulkan validation enabled, with no validation warning/error. This log predates the
additional object-tree/sorting metadata; the post-metadata verification is recorded below. `receiver-build-7.log` is the successful build for that test run. Build 1 caught
an enum conversion error, fixed before these tests. Intermediate test log 3 was launched
while the broader relink still ran and is superseded by `receiver-tests-final.log`.

The first real mapped solid run (`receivers/mapped-solid-1`) was **not receiving**: it differed
from the earlier capture-only image only in timer text. `CameraClass::Apply` defers its view
matrix in a separate wrapper cache, outside `Flush_Deferred_D3D9_State`. Explicitly publishing
the restored scene view before receiver setup fixes this; the later draw still gets the same
camera. `mapped-solid-2` reports receiving=1 and 9,891 solid / 193,200 ground draws cumulatively
at its last report. Its difference from `legacy-solid-1` is confined to building/shadow regions
and timer text: 36,446 changed pixels, 21,322 above 8/255, bounding box (0,59)-(1035,429).
These counts measure image change, not accuracy; images and amplified differences were inspected.
The scene's HUD, trees and other ground outside that box remain identical.

Retained receiver runs under `build-linux/shadow-phase1/receivers/`:

| Run | Capture / result |
| --- | --- |
| `mapped-solid-q0` | 1024², 456 caster draws, 30,205 occupied texels at last report; receiving active; CRC `0x5331E5B7` at 860. |
| `mapped-solid-2` | 2048², 456 caster draws, 121,032 occupied texels at last report; receiving active; same CRC. |
| `mapped-solid-q2` | 4096², 456 caster draws, 483,489 occupied texels at last report; receiving active; same CRC. |
| `mapped-helicopter-600` | 2048², 466 caster draws, 71,368 occupied texels; active solid/ground draws, softer displaced rotor/body shadow visible, CRC `0x646DC72B` at 660, matching legacy/capture-only. |
| `mapped-forest` | 2048², 423 caster draws, about 151,000 occupied texels; tree silhouettes, road and terrain visible; CRC `0xABAB5E80` at 1260. Predates object-tree metadata. |

Quality 0/2, helicopter and forest runs explicitly enabled Vulkan validation and reported no
validation warning/error. Earlier `mapped-solid-1/2` did not explicitly enable validation and
are visual/CRC evidence only. Each retained scene still has the pre-existing single
render-target sampling refusal also present in legacy. All runs here are diagnostic: readback,
validation and the screenshot clock exclude them from matched performance results.
The runner now offers `--quality 0|1|2` and `--gpu-debug`; it rejects debug validation with
`--measure` and removes an inherited debug flag when validation is not requested.

The post-metadata full build succeeds (`receiver-build-9.log`), and the focused selection
again passes 6/6 with validation (`receiver-metadata-tests.log`). Broader regression checks
pass 4/5 (`receiver-regressions.log`): the failure is the unchanged baseline reference
comparison, 2/1,675 pixels, worst (33,20), GPU (2,50,65,255) versus (3,53,65,255).
The combined selection is 10/11; no tolerance changed.

The final executable SHA-256 is
`9df3bcf61dc9de716596f6df97133f4c8f62cfa216febe246ae2e843fb797aff`.
Post-metadata runs explicitly enabled Vulkan validation and reported no warning/error:

- `capture-helicopter-final`: all 921,600 pixels are identical to the earlier settled legacy
  frame, including rotor, shadow and HUD. Frame-660 CRC remains `0x646DC72B`.
- `mapped-forest-final` / `legacy-forest`: both reach frame 1260 with `0xABAB5E80`.
  The amplified difference is around walls, a tower and their ground shadows; the selected
  tree region (700,50)-(1100,350), 120,000 pixels, is identical. This is fixture coverage,
  not proof for every tree model or overlapping unit/tree arrangement.
- `mapped-water` / `legacy-water` / `legacy-water-repeat`: all reach frame 300 with
  `0x1309B55E`. Shoreline, water, palm silhouettes and HUD remain visible. Mapped versus
  legacy changes 358,430 pixels, 77,894 above 8/255, mean max-channel difference 2.296/255.
  Legacy repeat changes 354,850 pixels, 50,945 above 8/255, mean 1.890/255. Both difference
  bounds are (0,0)-(609,720), covering animated water and shoreline; everything to the right
  is identical. The water source advances `m_modernWaveTime` from wall time because its
  separate capture clock is ineligible for this screenshot runner. The repeat establishes
  temporal variation but does not prove that all mapped differences are temporal. Strict
  water preservation therefore remains open. No unit shadow crosses the water in this
  fixture, so it does not establish the amount of lost stencil coverage there.

At the receiver checkpoint, the remaining gates were: a controlled water comparison and caster over water to document lost coverage,
bridge-deck receiving and its raster bias, more direct rotor/caster depth evidence,
shader/allocation failure fallback and real secondary-view integration,
replay validation, and final matched performance. Partial-frame receiver shader failure is not yet handled as a complete
stencil fallback. The feature remains opt-in and this checkpoint does not close the goal.

`04-receivers.patch` is saved against increments 1–3 through a temporary index. The real
staging boundary is preserved; no new commit was made.

## Scene ownership and reset increment

SDL now scopes shadow ownership to `RTS3DScene::Render`, including its final mesh and
sorting flushes. Each eligible outer scene starts with no usable capture. Nested scenes
(including otherwise eligible scenes with identical matrices) cannot capture, clear the
outer map, receive it or suppress their stencils. Deferred exclusion metadata includes
these nested scenes. Leaving the outer scene invalidates its ownership. Per-draw receiving
still requires the exact camera, viewport and target; the owning scene retains stencil
suppression across temporary camera-space draws so shadows cannot be applied twice.

Ordinary solid draws may set stencil markers for later occlusion. They now remain eligible
receivers. Explicit `DoShadows` exclusion still protects shadow passes; non-solid fill and
RGB-masked draws are excluded. This fixes missing receiving on the command-center model
in the solid fixture. It does not add trees or water to receiving.

New native checks in `posix_gpu_draw_selfcheck` exercise:

- both eligible and ineligible nested scenes, including Clear/Begin/Parameters attempts;
  the secondary image is unshadowed and the restored outer image is byte-identical;
- a separate color target with the same camera/viewport/depth, independent readback,
  untouched main color, and restoration of the original mapped image;
- ordinary stencil-marking solid geometry, temporary camera state, viewport ownership,
  scene expiry, invalid size fallback, and no-GPU queries;
- a queued draw followed by device Reset from 32x32 to 48x40, stale capture invalidation,
  a fresh 64x64 empty capture, unchanged green scene, and return to the original size.

The first test run found a real no-GPU null dereference in the ownership query. GDB traces
it through `Sun_Receives_View` to `Resolve_Target` with `Gpu == nullptr`
(`scene-ownership-crash-1.log`). The query now checks for GPU/capture readiness first.
The complete Release build (`scene-ownership-build-2.log`) then succeeds, and all **6/6**
focused checks pass with Vulkan validation (`scene-ownership-tests-2.log`), without a
validation error. The failed first test/build logs remain for diagnosis.

Real runs under `build-linux/shadow-phase1/views/` use executable SHA-256
`c5eb17be8b8698acce02d99e133a0af4b880f593bd7f5d10d879fd8c6e515061`:

- `capture-helicopter`: all 921,600 pixels exactly match the settled legacy frame-600
  helicopter image; both frame-660 CRCs are `0x646DC72B`, with 5 scenario actions run and
  none refused. This includes the scene-scope change in actual engine queues.
- `mapped-solid`: active 2048 capture, 456 caster draws, 121,032 occupied texels at the
  final report; cumulative receiving counts are 15,225 solid and 193,200 ground. CRC at
  frame 860 remains `0x5331E5B7`. Against the earlier mapped image, 33,968 pixels change
  (21,745 above 8/255), concentrated on the newly admitted building surfaces. The image
  and difference were inspected. Against legacy, 59,952 pixels change, 31,933 above 8/255.

Both runs enabled GPU validation and report the same pre-existing target-alias draw refusal
as legacy, with no new validation error. These diagnostic runs include capture readback
and are not performance measurements. The native nested-view tests do not by themselves
prove an actual reflected game view. Full optional-shader failure fallback, controlled
water/coverage comparisons, bridge-deck receiving, replay checks and matched performance
are still required. Linux remains opt-in.

`05-scene-ownership.patch` is saved against increments 1–4 through a temporary index;
the original staging boundary remains unchanged.

## Compatibility, bridge cutouts and replay increment

The diagnostic screenshot clock now also drives the existing water presentation clock and
legacy shoreline-track integration. `GameEngine_isShadowCaptureClock()` restricts this to
explicit turbo screenshot runs, excluding multiplayer and replay. Ordinary water timing
and gameplay remain on their existing paths. This removes wall-time phase differences that
previously made water comparisons inconclusive; it does not change the water shaders.
`sun-shadow-check.py` also accepts `--replay PATH`, copies that recording into isolated user
data and records its hash. Replay runs omit screenshots (the engine ignores scheduled shots
in playback) and require the limit-frame CRC. They reject `--logic-clock` and scenario cases.

The new `sun_water.txt` development fixture moves one Comanche from (600,1000) to (300,1000)
on Tournament Island, into water beside the fixed camera. The harness copies this file into
a private overlay. It does not alter the installed data or normal maps.

The later bridge shot at logic frame 1000 puts several of the 20 fixture tanks on the deck.
It exposed a real receiving gap: `W3DBridgeBuffer` uses a depth-writing, alpha-tested material
with SRCALPHA/INVSRCALPHA blending, while the first receiver rule rejected every blended
fixed-function draw. Fixed solids now admit that cutout combination. Plain translucent
materials, no-depth-write cutouts and additive blending remain excluded. Explicit tree,
water, shadow-pass and secondary-view exclusions take precedence. The native GPU fixture
checks all these branches with independent expected colors.

After the fix, `controlled/mapped-bridge-final` shows softened tank shadows on the deck and
shading on the bridge sides/rails. The before-fix mapped comparison changes 46,211 pixels
(37,981 above 8/255), all within (331,150)-(1026,596), the bridge region. The image and diff
were inspected: the previously absent deck shadows are visible, without obvious deck
striping at quality 1. This validates this bridge/material/camera, not every bridge slope.
Current legacy and mapped shots both reach frame 1060 with CRC `0x71E9790B`; all three fixture
actions run, none are refused, and 20 tanks spawn. The final mapped report records 602 caster
draws, 309,087 occupied texels in the 2048 map, and cumulative receiving counts of 68,509
solid / 333,540 ground draws. The shared bridge slope bias has not been retuned.

Controlled water comparisons under `build-linux/shadow-phase1/controlled/`:

| Fixture | Measured result |
| --- | --- |
| `legacy-water`, `mapped-water`, `legacy-water-repeat` | All 921,600 pixels identical, including water, shoreline, palm silhouettes and UI. All frame-300 CRCs `0x1309B55E`. |
| `{legacy,mapped}-water-caster` (modern) | Only 863 pixels differ, all on the helicopter within (275,92)-(403,124); 361 exceed 8/255. All other water/shore/UI pixels are identical. Both frame-660 CRCs `0xEFD8EA0D`; both fixture actions run without refusal. |
| `{legacy,mapped}-classic-water-final` | The same 863 main-helicopter pixels differ; everything outside that rectangle is identical, including the rendered reflection, water, surf and UI. The 11,700-pixel reflection ROI (300,500)-(430,590) is exact. Both frame-660 CRCs `0xEFD8EA0D`. |
| `legacy-classic-water-repeat` | Exactly identical to `legacy-classic-water-final`. |

The final-build forest run (`controlled/mapped-forest-final`) retains frame-1260 CRC
`0xABAB5E80`. The 120,000-pixel tree region (700,50)-(1100,350) is exactly identical to
both the legacy and earlier mapped forest images. The final image/difference were inspected;
new changes relative to the earlier mapped image are concentrated on edge structures and
the tower, with no change in that tree region. Final capture: 423 draws, 150,934 occupied
texels, cumulative receiving 38,974 solid / 189,600 ground. This verifies the visible tree
fixture after admitting cutout materials; it does not test every overlapping tree/caster.

The first classic-water pair, before the shoreline clock correction, differed along the
surf (58,561 pixels total). That run is retained and is not a preservation pass. The corrected
comparison reduces the difference to exactly the same helicopter pixels as modern water.
The reflected helicopter is the output of `renderMirror`'s offscreen scene/camera pass with
inverted culling and an oblique plane; its exact preservation exercises a real secondary
view, alongside the native nested-scene and render-target tests.

These fixtures show no measured loss outside the caster's own receiving surfaces. They do
not establish universal coverage on unsupported materials: when main-view unit stencil
volumes stand down, excluded tree/water surfaces have no new mapped-receiving path. An
excluded receiver overlapped by a former unit volume can therefore lose that contribution.
Terrain underneath transparent water can also change the visible water through the existing
scene/refraction input; exclusion of the water shader does not promise identical water pixels
when the terrain below it is newly shadowed. No per-material stencil replacement was added.
Keep the feature opt-in because unsupported materials retain this coverage limitation.

The baseline recording from `baseline/solid-2/user/Replays/00000000.rep` has SHA-256
`dfebfedad9b6f0a82d199454da9ec95660523b90e3e4f482d705c0456c27c900`.
`replays/{legacy,capture-only,mapped}-solid-final` all log actual replay startup and reach
frame 860 with the original live CRC **`0x5331E5B7`**. Capture-only reports occupied depth and
zero receiving; mapped reports active receiving. `controlled/mapped-replay-final` repeats
that same CRC on the final bridge/shore-clock build. These are same-machine replay checks,
not cross-platform or network determinism claims. Input, executable and command hashes are
retained in manifests and `replays/comparison.json`.

The final Release build succeeds (`compatibility-build.log`). The shader/GPU/layout/water
selection passes **11/12** (`compatibility-tests.log`); the only failure remains exactly the
baseline two-pixel `ffref_capture_selfcheck` mismatch, with no tolerance changes. No Vulkan
validation error was reported in these tests or scene runs. The same pre-existing scene
render-target-alias refusal remains in both modes. Final executable SHA-256 is
`76c6623942cddadd3d4478a56dc768bf0722971dc3d4fa3448087007792c2cc0`.
The earlier modern-water and initial replay results use
`152e30afc41049aebe161763b20ec5879cd0ba51cf1afb10ec62abca4bb651dd`, before the bridge-cutout
and shoreline-track fixes; the final bridge/classic-water/mapped-replay runs use the final
hash. Diagnostic timings include GPU validation and map readback and are not performance
measurements.

At this checkpoint, optional shader/pipeline and allocation fallback, direct rotor evidence
and matched performance remained open. The following increments complete those checks.

`06-compatibility-and-replay.patch` is saved against increments 1–5 through a temporary
index, preserving the original staging boundary.

## Failure fallback increment

Optional receiver shader or pipeline creation failure retries the affected geometry with
its ordinary shader. A failure before any accepted receiver draw hands stencil ownership
back immediately. If mapped draws have already been accepted or submitted, the backend
retains ownership for the rest of that scene to avoid applying stencil darkening twice.
Remaining geometry uses ordinary shading; the next scene uses legacy shadows. **One
partially shadowed scene is possible after a late failure.** No whole-scene rollback is
claimed. A receiver refusal remains latched until device recreation, including across
`Reset`, because program/pipeline refusal entries remain cached. Capture-only can still
exercise the caster pass while the mapped path is latched off.

Failed depth allocation leaves the old allocation alive for queued readers and uses legacy
for the failed capture. Allocation can recover on a later attempt; it does not permanently
disable receiving. Capture reports distinguish unavailable capture and receiver refusal.

The diagnostic runner accepts `--fault target|receiver-program|receiver-pipeline`, excluded
from `--measure`. It controls `ZH_TEST_SUN_SHADOW_FAILURE` and clears an inherited value by
default. The shader case adds a real compiler error to a previously unseen receiver program;
the other two cases simulate allocation/pipeline creation returning null. These exercise the
production refusal paths, not actual VRAM exhaustion. Cached successes are left alone.

`fallback-tests-1.log` passes **6/6** focused tests under Vulkan validation. New pixel checks
cover early and late shader/pipeline failure, actual submission before late failure, ordinary
geometry retry, no draw refusal, stencil ownership within and across scene boundaries,
repeated parameters, reset, capture-only, and recovery through device recreation. The
allocation test checks initial failure, replacement failure with an old queued receiver,
subsequent successful allocation, and an empty new map.

The five `fallback/{legacy,target,program,pipeline,mapped}-solid` runs all reach frame 860
with CRC **`0x5331E5B7`**. At frame 800, the three failure runs differ from legacy only on
73–102 countdown-text pixels; all pixels outside the two timer rectangles are identical.
`fallback/image-comparison.json` retains counts and rectangles; the text comparison was
visually inspected. The normal mapped control has active receiving and visible shadow
changes. The real shader/pipeline failures occur after terrain has received, log the
next-scene fallback, and recover as specified. Diagnostic timing is not performance data.

The full Release build passes (`fallback-build-1.log`); the broader selection remains
**11/12**, with the identical baseline two-pixel reference mismatch
(`fallback-regressions.log`). No Vulkan validation error was reported. Executable SHA-256:
`95406cb151ec783ef10cecd4637ef0292f452a7cf939f3f8772827c00735bb23`.
`07-fallback.patch` is saved against increments 1–6 through a temporary index; the actual
staging boundary is unchanged.

## Final validation and measurements

The final Release build succeeds (`diagnostic-depth-build.log`). The final binary passes
**11/12** focused shader/GPU/layout/render/water tests (`final-regressions.log`), including
all sun-specific cases. Its sole failure is the unchanged baseline `ffref_capture_selfcheck`
two-pixel mismatch described above. The separate baseline minimap assertion remains
recorded in the caster checkpoint. No tolerances were weakened. Vulkan validation reports
no error in the final tests or rotor scene. Final executable SHA-256:
`d1153666e4c3dc02f8c28511fdbf8122ae229df27375c313cb3d8ee47e0eb3e3`.

`coverage/mapped-helicopter-depth` checks quality 2 (4096²), ending at frame 660 with CRC
**`0x646DC72B`**, five actions run and none refused. The latest report has
466 caster draws and **285,497 occupied texels**. The native map crop
`rotor-depth.png` visibly contains the radial rotor blades and the fuselage. Its 150×140
texel crop at `(2170,1830)–(2320,1970)` contains 3,471 occupied texels; the preview enlarges
these with nearest-neighbor scaling and local depth contrast only. The full map, native
crop coordinates, hashes and screenshot are retained beside `depth-evidence.json`.
The report also counts 26 excluded light meshes; synthetic GPU cases separately verify
that additive light writes no depth and alpha-tested/translucent matter does.

The runner's `--depth-dump --report` exports the latest reported depth map as a 16-bit PGM
with a world-to-shadow-clip matrix comment. This is diagnostic quantization only; sampling
still uses native D32. The file is overwritten at each report and therefore is not promised
to correspond to the screenshot frame. It is excluded from measurement runs and the harness
clears inherited dump requests. Neither readback nor file writing runs in ordinary play.

### Matched performance

`performance/` contains **18 sequential runs / 32,382 measured frames**: three repeats of
legacy, capture-only and mapped for each of the solid and Modern-water scenes. Each uses
1,799 measured frames after logic frame 60 and ends at 1860. Modes rotate order between
rounds: legacy/capture-only/mapped, capture-only/mapped/legacy, mapped/legacy/capture-only.
No build or other GPU test ran concurrently. Validation, screenshots, report readback,
fixed diagnostic clocks and injected faults were disabled.

Hardware/settings: Core i7-12700, RTX 4060, NVIDIA 595.99.02, Linux 7.0.0-38,
Release, SDL3 Vulkan, 1280×720, quality 1 (2048²), no MSAA or dynamic LOD, offscreen and
unpaced. Clocks were not locked. Per-run pre-launch GPU temperature/clock observations are
retained; they are not sustained in-run frequency measurements.

The table gives the median of the three **per-run** means and p95s, plus each metric's
observed run range, in milliseconds. Percentiles are not pooled across runs.

| Scene | Mode | Median mean ms | Mean range ms | Median p95 ms | p95 range ms |
| --- | --- | ---: | ---: | ---: | ---: |
| Solid | legacy | 14.91 | 14.53–15.13 | 19.75 | 19.75–20.00 |
| Solid | capture-only | 16.01 | 14.89–16.08 | 21.00 | 19.75–21.25 |
| Solid | mapped | 15.99 | 14.72–15.99 | 20.50 | 19.00–21.25 |
| Modern water | legacy | 32.79 | 32.10–34.34 | 38.50 | 37.25–41.50 |
| Modern water | capture-only | 34.77 | 32.37–34.94 | 41.25 | 37.25–41.50 |
| Modern water | mapped | 33.53 | 33.47–37.06 | 39.50 | 39.00–44.25 |

Paired within each round, mapped minus legacy mean time is **+0.86, +1.08, +0.19 ms**
for solid and **+2.72, +0.74, +1.37 ms** for water. Their paired medians are +0.86 and
+1.37 ms. This records the measured whole-loop CPU/GPU cost on these fixtures, not a GPU
pass timestamp or a general performance guarantee. Run variation is substantial, especially
for water; overlapping ranges do not establish that mapped is faster than capture-only.
The earlier untouched-executable measurements are historical context, not a speedup control.

All nine solid runs match baseline CRC **`0xE38AB30C`** and all nine water runs match
**`0x6A9F4C99`** at frame 1860. `performance/audit.json` verifies equal executable, source
and options hashes, expected counts/CRCs, disabled diagnostics and no reported fallback.
Every mode retains the same pre-existing render-target-alias draw refusal. `runs.json`
contains every result and `summary.json` the aggregation; `run-performance.py` in the
validation root records the serial invocation sequence.

`comparison.png` in the validation root places controlled legacy/mapped building and
bridge captures side by side. `08-final-validation.patch` contains the report-only depth
export and this final evidence record, against increments 1–7. The full patch chain is
verified against the tracked and new source files through a temporary index; actual staging
is preserved. A prior signed-commit attempt was blocked by a locked signing key; no
unsigned commit was substituted.

## Baseline environment

Captured on 2026-10-16 (Asia/Manila): Ubuntu 26.04.1, GCC/G++ 16 Release,
NVIDIA RTX 4060, driver 595.99.02, SDL3 Vulkan, 1280x720, no MSAA, no dynamic LOD,
isolated settings, offscreen and unpaced. Existing dependencies were used; none installed.

The untouched executable is preserved locally at
`build-linux/shadow-phase1/baseline/generals`, SHA-256
`d03eb4395673788678dea47a70efac95b62b6f25c6ad2af7eb9b21b1c1920f40`.
`baseline/build.log` confirms the existing build needed no work.

The original shader/GPU selection passed **8/9** tests. `ffref_capture_selfcheck` repeats
the known NVIDIA reference mismatch; its tolerance is unchanged. See `baseline/gpu-tests.log`.
The other selected tests cover frame batching, pipeline translation, program cache, draw
resolution, GPU drawing, the fixed-function reference, render hooks and shader generation.

## Reproduce a scene

From the repository root, with an existing prepared file-link root and build overlay:

```sh
python3 GeneralsMD/Code/Tools/sun-shadow-check.py \
  --generals build-linux/shadow-phase1/baseline/generals \
  --root build-linux/water-validation/root --overlay build-linux/overlay \
  --out build-linux/shadow-phase1/example-solid \
  --case solid --mode legacy \
  --icd /usr/share/vulkan/icd.d/nvidia_icd.json
```

The root above is local test preparation, not part of a fresh clone. Supply the equivalent
file-link root for owned game data. The runner refuses to overwrite results, isolates user
settings and copies the engine log. Its manifest records executable/options/scenario hashes,
source revision, command, environment, exit code, picture hashes and timing/CRC summaries.
`--case` also accepts `forest`, `bridge`, `helicopter`, `water` and `water-caster`; `--water classic` checks
the other water style. `--frame` overrides the photograph's logic frame. Development
scenario files are copied into a private overlay; the shipped overlay and install stay intact.

| Retained run under `baseline/` | Observed result |
| --- | --- |
| `solid-2`, `solid-repeat` | Same frame-860 CRC `0x5331E5B7`; buildings, trees, shadows and UI inspected at frame 800. |
| `forest-repeat` | Forest silhouettes and road inspected; frame-1260 CRC `0xABAB5E80`. |
| `bridge-fixture` | Bridge deck/abutments, road and water inspected; 20 tanks spawned, all 3 fixture actions ran; frame-660 CRC `0x28A58486`. The frame-600 shot does not yet show a tank on the deck. |
| `helicopter-fixture` | Helicopter, rotor and rotor shadow visible; 5 scheduled actions ran, 0 refused, 5 units spawned; frame-360 CRC `0x67482401`. |
| `water-repeat`, `water-classic` | Both water styles and shoreline inspected; both frame-300 CRCs `0x1309B55E`. |

Two failed harness attempts are retained as diagnostic evidence: `solid-1` used `-control`,
which bypasses scheduled screenshots and frame-limit reporting in `updateHeadlessRun`;
`bridge-repeat` and `helicopter-repeat` had absent scenario files. These are not coverage passes.

### Repeatability and measurements

The solid repeats agree in simulation state, but they are not pixel-identical. Of 921,600
pixels, 504,818 differ at any channel level, 31,771 by more than 2 levels, and 9,513 by
more than 8 levels. Mean maximum-channel difference is 0.967/255. The scene inspection
and source identify wall-clock W3D animation/cloud offsets and UI timers as noise sources.
Do not use pixel identity as a scene gate without first controlling those clocks; do not
attribute this baseline noise to shadow capture.

Use `--measure --frame 1800` for performance. It omits screenshot readback and its five-second
settling window. The engine's `HEADLESS FRAMETIME` histogram starts after logic frame 60;
the run ends at 1860. The separate `PosixDevice9 timing` probe includes loading and is only
diagnostic. Measurements must run sequentially, without concurrent builds or GPU tests.
Three untouched-executable repeats each of solid and Modern-water scenes are retained
as `perf-solid-{1,2,3}` and `perf-water-{1,2,3}`. Each contains exactly 1,799 measured frames.

| Scene / repeat | Mean ms | p50 ms | p95 ms | p99 ms | Final CRC, frame 1860 |
| --- | ---: | ---: | ---: | ---: | --- |
| Solid 1 | 15.76 | 15.00 | 20.75 | 23.25 | `0xE38AB30C` |
| Solid 2 | 16.68 | 16.00 | 22.25 | 24.75 | `0xE38AB30C` |
| Solid 3 | 17.56 | 16.75 | 23.75 | 27.00 | `0xE38AB30C` |
| Modern water 1 | 37.51 | 36.75 | 43.75 | 46.75 | `0x6A9F4C99` |
| Modern water 2 | 37.50 | 36.75 | 45.00 | 50.25 | `0x6A9F4C99` |
| Modern water 3 | 35.69 | 35.00 | 43.50 | 50.25 | `0x6A9F4C99` |

These are measured whole-loop CPU/GPU frame costs, not GPU timestamps. Do not derive a
shadow speedup from their spread. The matched implemented-build results above use equal
scene/frame/settings windows and interleave the three modes to expose run-order variation.

## Phase-one coverage boundary

Phase one supports registered unit/building casters and supported solid receivers, including
terrain, roads and bridge decks. Rotor geometry is captured; additive glows are excluded.
The existing sun camera, filter and quality levels are retained. Dynamic lighting capability
remains independent of sun shadow support.

Trees keep their flattened silhouettes and are excluded from mapped receiving. Water and
shoreline shaders are excluded too; smoke shadow maps, cascades and broader lighting changes
remain outside this phase. The forest tree region, modern/classic water surfaces and a real
classic-water reflection were preserved in the controlled comparisons above. Those fixtures
show no measured loss on their excluded surfaces, but cannot establish universal overlap
coverage. An excluded material overlapped by a former unit stencil volume can lose that
contribution once the owning main scene suppresses volumes. Water can also show changed
terrain shading through its scene input. No per-material stencil replacement was added.

Late optional receiver failure can produce one partially shadowed scene before subsequent
scenes use legacy. The refusal latch lasts until device recreation; target allocation refusal
can recover on a subsequent attempt. Initial/late failures, empty maps, mode/size changes,
queued-resource lifetime, reset and nested/secondary views are tested as described above.

Validation is local to NVIDIA Vulkan; AMD/Intel/Metal and Windows runtime behavior were not
executed here. Windows retains the existing backend/default through the shared interface.
The same-machine CRC/replay results do not establish cross-platform or multiplayer determinism.
The feature remains opt-in. The existing `shadow-check.ps1` compares mapped against
mapped-plus-stencil and was not used as a clean legacy baseline.


## Phase two: smoke and translucent/effect shadows (2026-10-16)

The mapped SDL path now implements the remaining requested smoke and translucent/effect
shadow behavior from the Windows backend. It stays independent of
`Direct3D11_Lights_Per_Pixel`, `Direct3D11_Is_Active` and the cosmetic draw-effect capability.
Linux still defaults to legacy; select `ZH_SUN_SHADOW_MODE=mapped` to use the new paths.

- `Platform/SunSmoke.h` shares the Windows splat shaders, projection, 16,384-caster limit,
  512-square resolution, self-shadow gain 0.8 and curve 16. The extracted vertex and pixel
  strings are byte-identical to the phase-one Windows source (`shared-shader-audit.json`).
  The projected input rejects non-finite/invalid casters. Windows uses this same helper.
- `SdlGpuFrameSmoke.cpp` owns an RGBA32F map: RGB accumulates optical-depth moments, alpha
  keeps the nearest smoke front with MIN blending, and sampling uses linear clamp.
  Refills flush queued readers first, cycle the upload/instance buffer, and submit in order.
  The map has independent attachments; neither scene colour nor scene depth is cleared.
  Resources remain alive through pending batches and are released with the GPU frame.
- Shared `VOLUMETRIC_SAMPLING` now runs on SDL ground and generated receivers. The constant
  block matches the Windows layout through `DrawEffectParameters`, with offset assertions.
  Smoke reconstruction includes viewport origin and the D3D9 half-pixel correction;
  depth-range reconstruction remains in the shared sun matrix. Linux headlight/blast counts
  stay zero, preserving its existing dynamic-light path.
- Ordinary alpha-blended geometry receives without requiring alpha-test or depth writes.
  Camera-space particles use the owning scene's captured camera and Windows' four-tap sun
  filter/self-shading. Additive and multiplicative effects remain excluded. Firelight is
  carried separately through the smoke vertex normals and added after shadowing, once.
  UI, trees, water, shoreline passes, excluded queues and secondary scenes stay excluded.
- Friendly cloaked casters use Windows' Bayer coverage from `DRAW_EFFECT_CLOAK_SHADOW`.
  The shadow-only dispatch does not enable unsupported cosmetic cloak/refraction effects.
  Beginning/ending a capture resets the caster effect, and the snapshot restores smoke-glow
  state with the other device state.

A smoke allocation, shader, buffer or pipeline failure returns false to the existing particle
collector, which retains its legacy blob-shadow path. It does not disable core mapped sun
shadows. The smoke refusal is latched for the GPU frame's lifetime to avoid retry/log spam;
empty/rejected fills do not expose a stale smoke map. The harness adds `smoke-target` and
`smoke-pipeline` creation faults. As with Windows, particle blob eligibility is updated by
client particle updates; a mid-run failure or mode change may take the next update to restore
those blobs. The phase-one late receiver-failure limitation still applies.

### Verification

Built with the existing Release configuration using `cmake --build build-linux -j 4`.
Final executable SHA-256:
`60b9decad32f09338298fbc0e86a20b94cb3f3a060e205de4ec01e31ed61a98a`.
The preserved phase-one executable is `build-linux/shadow-phase2/phase1-generals`, SHA-256
`d1153666e4c3dc02f8c28511fdbf8122ae229df27375c313cb3d8ee47e0eb3e3`.

`final-regressions.log` records 12/13 passing checks, including GPU draw/frame/program/pipeline,
render hooks, water grid/tracks, shader generation/layout and reference rendering. The only
failure is the unchanged baseline `ffref_capture_selfcheck`: 2 of 1,675 written pixels, worst
(33,20), GPU (2,50,65,255), reference (3,53,65,255). No tolerance was changed.
`final-smoke-tests.log` additionally passes the final off-centre map/receiver checks.
There were no Vulkan validation errors in these tests or the real scenes below.

The GPU draw fixture verifies independently calculated overlapping-disc moments and MIN
front depth, clear texels, off-centre X/Y orientation, non-finite/outside caster rejection,
the caster limit, viewport/depth-range reconstruction, front/back particle self-shading,
alpha receivers, additive/multiplicative exclusions, firelight after shading, retained
colour/depth across queued refills, empty maps, nested exclusion, and cloak coverage of
0%, 25% and 50% for opacity 0, 0.5 and 1. Clearing the effect restores full coverage.
Both native smoke creation faults keep the core depth shadow working and signal the
particle fallback. Existing scene/target/resize/reset and core failure tests also pass.

Real runs use isolated user data, 1280x720, quality 1, no MSAA/dynamic LOD, NVIDIA Vulkan
validation, and the explicit capture clock. `run-scenes.py`/`run-controls.py`, each directory's
`manifest.json`, screenshots and logs retain the commands and executable/settings hashes.

| Fixture | Result |
| --- | --- |
| Smoke (`rf_smoke`, frame 600 to 660) | All three modes, preserved phase-one controls and injected smoke-target failure keep CRC `0xF86BC92B`; all five actions run, none refused. The final mapped report holds 643 smoke casters, 79,324 cumulative camera-space and 84,124 alpha receiver draws. The later collector report records 635 casters from 16 systems. |
| Smoke repeatability | The two new mapped captures are pixel-identical. Relative to phase-one mapped, 250,602 pixels change (95,402 by more than 8/255); the bottom UI is exact. These changes include replacing particle blobs with the density map, smoke receiving/self-shading, and firelight preservation. |
| Legacy/capture-only controls | Each is pixel-identical to its corresponding phase-one executable capture. Their mutual 1,533-pixel difference (735 over 8/255), confined to animated flags, already exists in phase one; it is not a smoke-port regression. |
| Friendly cloak (`sun_cloak`, frame 300 to 360) | Two Burton units and a ranger spawn; both executables keep CRC `0x8824ABE1`. The new report records 484 accepted cloak-caster draws. 2,027 pixels change, with changes above 8/255 confined to the two cloaked units and their shadows; UI is exact. |
| Modern water with helicopter (frame 600 to 660) | CRC `0xEFD8EA0D`, both actions run. Against phase-one mapped, 1,730 pixels differ, all within the helicopter bounds (250,60)-(401,127); the rest of the image, including water/shore/UI, is exact. |
| Solid scene (frame 800 to 860) | CRC `0x5331E5B7`, matching phase one. |

`image-audit.json` contains the numeric image comparisons; `comparison.png` presents the
smoke and friendly-cloak before/after crops. The existing render-target-alias refusal during
startup occurs in baseline and new runs; no new ordinary draw refusal was observed.

This closes the requested smoke and shadow-related translucent/effect paths in the SDL
implementation. It is not a matched Windows runtime image certification: Windows was not
built or run here. Trees and water are excluded by the existing Windows shadow receiver too;
their separate rendering is not a newly discovered Linux-only shadow gap. Scene-depth soft
particle fading, cosmetic EMP/outage/reveal/cloak refraction, per-pixel lighting, cascades and
the Windows port of Linux's modern water remain outside this shadow change.

### Smoke-heavy timing and saved increment

Six interleaved unpaced runs (phase 1, phase 2, phase 2, phase 1, phase 1, phase 2) use
`rf_smoke`, mapped mode, screenshot frame argument 1,200 with `--measure`, and a frame limit
of 1,260. Diagnostics, validation, screenshot settling and the fixed capture clock are off.
They use the same CPU/GPU/settings as the phase-one measurements. Each measures 1,199
whole-loop frames; all retain CRC `0x2042A7DB` at 1,260, totaling 7,194 measured frames.

| Build | Mean frame times across three runs | Median of run means |
| --- | --- | --- |
| Phase one | 17.32, 17.44, 17.26 ms | 17.32 ms |
| Smoke/translucent extension | 18.95, 22.30, 20.65 ms | 20.65 ms |

The median difference is **+3.33 ms (+19.2%)** in this deliberately smoke-heavy fixture.
The new path pays for smoke-map construction and additional receiving/self-shading; these
are whole-loop measurements, not isolated GPU timings or a general game-wide estimate.
The spread of the new runs is retained rather than hidden behind a single FPS number.
Commands, hashes and individual reports are in `run-performance.py`, `performance.json`
and `performance-summary.json` under `build-linux/shadow-phase2/`.

The complete extension is saved as `build-linux/shadow-phase2/09-smoke-and-effects.patch`,
after the eight phase-one increments. `build-linux/shadow-phase2/patch-series.json` records
the chain, patch hash, final tree and executable hash. The chain is applied to a temporary
Git index and checked against the full working tree; the user's real staging remains unchanged.

## Default rendering rollout (Linux, 2026-10-16)

Normal Linux launches select mapped sun shadows. `ZH_SUN_SHADOW_MODE` is now an
optional diagnostic/legacy override; missing or empty selects mapped, explicit `legacy`
keeps stencils, and an unknown value still selects legacy. This selection remains separate
from `Direct3D11_Lights_Per_Pixel`. Modern water already defaulted on when an older
Options.ini omitted WaterStyle; explicit Classic water and existing shadow settings remain
supported. No Windows installation, build, runtime test, or visual comparison was used.

### Trees and state restoration

Batched trees draw their actual alpha-tested, swaying/toppling geometry into the sun map.
Per-type DoShadow and ShadowsForProps control contiguous caster index ranges. Scene and sun
visibility share the current buffers; motion advances once per frame. Object palms render
the held model meshes with their material alpha instead of projected flat copies. Successful
mapped ownership suppresses the old tree silhouette/blob passes; failed captures retain them.
Tree colour draws can receive the shared sun/smoke filter. UI and secondary views remain
outside receiving.

The model-tree capture uses the same conservative swept-sphere visibility test as ordinary
casters: extend to the lowest receiver, include the filter footprint and caster padding,
and retain the low-sun and applicable mirror guards. SDL reflections never receive this
map, so their presence does not disable main-view culling. Off-screen trunks still cast
when their shadows can reach visible receivers.

Native capture restores device constants behind the DX8 wrapper. Invalidating only cached
render states previously left the wrapper believing the tree shroud constants were still
uploaded; colour trees then sampled the wrong shroud location. Per-register validity in
both shader-constant caches now invalidates those assumptions after capture/state restore.
The dedicated forest capture confirmed the colour correction without changing its logic CRC.

### Water receiving and resource layout

Modern flat water and rivers now receive mapped sun and smoke shadows. The shared blocker
search/soft filter and smoke integral shade direct specular, scattering and foam. Reflected
sky stays independent; transmitted scene colour is already lit and is not shadowed twice.
Classic water, skybox, shoreline overlays, UI, additive effects and secondary scenes keep
their exclusions. Shoreline replacement still requires ShadowReceiving=false.

The six existing RGBA32F wave maps and every mip are copied into a six-layer texture array
on the GPU. Water fragments bind that array at t8, sun depth at t9 and smoke at t10;
foam history stays at t14 and the optional coastal experiment stays at t15. Vertex and
coastal samplers retain their individual wave maps. This stays within SDL's sixteen slots
and four fragment uniform buffers. Array updates submit previous queued readers first;
paused frames reuse the field and resize retains its phase. Analytic fallback binds an
array-typed placeholder and zero FFT weight. Unused intermediate slots bind safe placeholders.

The existing default wave displacement, filtering, refraction/depth, optical transport,
planar reflections, shore foam, hull contact, bow and wake paths remain intact. Experimental
`ZH_WATER_SURF_GPU` and `ZH_WATER_COAST_CREST` are separate optional experiments, not
requirements for the default renderer or this rollout.

### Linux validation

Evidence is under `build-linux/render-final/`. Runs use isolated settings and user data.
Rendering comparisons use fixed animation clocks; performance runs omit those clocks,
screenshots, GPU validation and depth readback, and run serially.

- The 13-test focused regression set passes 12 tests. `ffref_capture_selfcheck` retains its
  independently recorded pre-existing two-pixel mismatch: worst (33,20), GPU (2,50,65,255)
  versus reference (3,53,65,255), out of 1,675 written pixels. Tolerances were not changed.
- GPU tests cover actual alpha/sway tree capture and receiving; flat/river water receiving,
  lit-side identity and explicit exclusions; depth/smoke and cloak sampling; projection,
  viewport and depth-range offsets; queued replacement, reset, allocation/program/pipeline
  refusals; FFT array layers and fractional mips; shader caches and water uniform snapshots.
- All six array layers match independently computed mip samples within 6e-9 in the tested
  fields, including negative wrapped coordinates. The water/coastal/foam/geometry regression
  suite passes with the new layout.
- Winding River reports two `VUID-VkGraphicsPipelineCreateInfo-topology-08773` warnings
  during point-list pipeline creation. The saved pre-change executable reproduces the same
  two warnings. They are not introduced by the shadow/water changes. Other tested scenes
  report no Vulkan validation errors. The pre-existing startup render-target-alias draw
  refusal also remains distinguishable in the logs.

| Scene | Linux evidence |
| --- | --- |
| Four tree types / palms | Frame-360 CRC `0xB0604A0C`; 4/4 actions, foliage colours preserved, soft shadows replace hard flattened streaks. Target-allocation fallback image is byte-identical to explicit legacy. |
| Forest | Frame-1260 CRC `0xABAB5E80`, matching the preserved executable. |
| Bridge | Frame-660 CRC `0x28A58486`, matching the preserved executable. |
| Smoke / fire | Frame-660 CRC `0xF86BC92B`; 5/5 actions, smoke self-shading, fire and shadows inspected. |
| Friendly cloak | Frame-360 CRC `0x8824ABE1`; 2/2 actions, 3 units. |
| Helicopter | Frame-360 CRC `0x67482401`; rotor/caster and ordinary view inspected. |
| Water / helicopter | Modern, Classic, analytic fallback and failed sun target all keep frame-660 CRC `0xEFD8EA0D`; 2/2 actions. |
| Water / smoke / fire | Frame-660 CRC `0xEFD8EA0D`; 5/5 actions, water/shore/fire composition inspected. |
| River | Frame-300 CRC `0xE4F8BACF`, matching the saved executable. |
| Tree settings | No-props and hidden-tree runs reach frame 180 with CRC `0x590BD529`; 4/4 actions. |

The package was launched into a real 1280x720 Vulkan window without `-root`, `-overlay`,
`-offscreen`, shadow mode or water feature overrides. Its isolated older-style Options.ini
omitted WaterStyle. The saved install path and package overlay were discovered normally;
the engine reported mapped receiving and the modern trapezoid water program, and captured
the scene before exiting at its requested frame limit.

### Visibility optimization

Final water and river screenshots are pixel-identical to the completed but unculled
implementation, with unchanged simulation CRCs. At the same reported frames, the water
fixture's sun draws fall from **827 to 55**, and Winding River's from **900 to 80**.
The map's off-screen depth occupancy changes as expected; visible filtered shadows do not.
This includes the existing 30-unit caster margin, filter reach and low-sun guard. SDL's
excluded reflection scopes make it safe to apply the normal scene-frustum test on water maps.

### Final performance samples

Two serial 899-frame measurements per scene/build, from logic frame 60 through 960,
compare the saved phase-two mapped executable with the final default renderer. All solid
runs keep CRC `0x2B9DB8E9`; all water runs keep `0x3BB91E64`. This comparison includes the
new tree/water work; it is not a claim about every map or hardware configuration.

| Scene/build | Mean frame time, repeats (ms) | p50, repeats (ms) | p95, repeats (ms) |
| --- | --- | --- | --- |
| solid / before | 17.40, 17.34 | 15.75, 15.75 | 27.75, 27.50 |
| solid / final | 16.78, 16.28 | 16.00, 15.75 | 23.00, 21.50 |
| water / before | 33.82, 33.94 | 33.00, 33.50 | 39.25, 38.75 |
| water / final | 35.33, 34.61 | 34.75, 34.00 | 40.75, 39.75 |

The means average 17.37 to 16.53 ms for the solid scene and 33.88 to 34.97 ms for water.
The water samples include an outlier at frame 602: about 148 ms before and 150–156 ms
after. These short samples
are diagnostic, not a universal performance guarantee. Full logs and manifests are retained.

### Fallback and practical limits

Capture allocation failure falls back before the scene uses the map. Smoke allocation or
pipeline failure retains smoke blobs while solid mapped shadows can continue. A late
receiver-program/pipeline refusal cannot undo pixels already submitted: that scene keeps
its mapped ownership to avoid double-darkening, retries the affected geometry normally,
and latches legacy for subsequent scenes until device recreation. This bounded transition
is covered by fault tests; it is not a frame-atomic replay of the failed scene.

Validation covers the local NVIDIA Vulkan configuration, not every Linux GPU or driver.
The legacy/capture-only modes and existing quality settings remain available for diagnostics.
The original index/staging and unrelated working files are preserved. No dependencies were
installed, and no commit, push or merge was performed.

### Refreshed package

The final build and `build-linux/ZeroHourReforged/bin/generals` both have SHA-256
`1bc430d8066ecf7cb8d80c7ec9106f9164bf625cc4d7a727647ec2d43136574d`.
The existing package overlay link resolves to the freshly built `build-linux/overlay`.
The former packaged executable is retained as `build-linux/render-final/staged-before-generals`.

The final executable was checked in two real windowed launches, at the beach and supply
dock, without shadow/water environment overrides, WaterStyle in Options.ini, or explicit
root/overlay arguments. Both reached frame 300 with CRC `0x1309B55E` and no Vulkan
validation errors. The dock run reports `receiving=1` and increasing solid/ground receiver
counts; both runs use modern trapezoid water. The beach has tree shadow draws but no
ordinary casters after visibility culling, so its existing caster-gated diagnostic emits
no SHADOWMAP line. Logs, captures and commands are in `package-final/` and
`package-final-caster/` under the evidence directory.

Launch normally from the repository:

```sh
./build-linux/ZeroHourReforged/bin/generals
```

No rendering opt-in is needed. Explicit Classic water, disabled shadows, and diagnostic
environment overrides still take precedence over the defaults.

## Review fixes (2026-10-16)

The follow-up review fixes four rendering errors:

- Capture trees that are actually present, even when the ShowTrees preference is off.
  Multiplayer map loading can keep those trees visible. The shadow manager still honours
  the ordinary shadow options; a display preference must not independently hide their casters.
- Let object-based palms receive mapped sun and smoke shadows. The blanket shrubbery
  exclusion and its render-info/mesh metadata have been removed. Explicit pass exclusions
  for reflections and other non-receiving draws remain in place.
- Stop geometry capture, including bridge casters, when both ordinary shadow options are
  disabled. The independent volumetric-smoke option can retain an empty depth capture for
  smoke projection without turning geometry shadows back on. Unit casters also honour the
  current volume-shadow option instead of relying on the existing caster list.
- Preserve cloak coverage in WW3D sorting packets. Each queued draw owns its caster effect;
  different effects cannot merge into one draw, and submission restores the previous effect.
  The platform interface exposes only shadow-pass coverage to this queue. This also addresses
  the inherited sorted-cloak issue in the shared path; Windows validation was not performed.

`test_sun_shadow_sorting` exercises the real sorting pool with otherwise identical material
state and independently expected coverage of 0, 64, 128 and 256 pixels out of 256. Both
insertion orders pass, including recycled nodes and state restoration. Linking this test
against the saved pre-fix sorter fails six assertions: all four strips incorrectly cover
256 pixels. CTest registers the test as serial and uses the existing offscreen environment.

The Linux game builds. Of thirteen focused current checks, twelve pass; only
`ffref_capture_selfcheck` retains the independently recorded two-pixel baseline mismatch
described above. A closer log audit also found seven Vulkan view-type validation errors in
the water mesh-correction test despite its successful exit. That fixture bound a 2D white
texture to the production shader's array-texture slot. It now uses the typed array fallback;
the rerun passes with no Vulkan validation errors. Earlier claims of validation-clean test
logs were too broad. The corrected water test and the new sorting test both run with GPU
debugging and the installed SPIR-V validator.

Serial, isolated scene runs verify the settings and receiving changes:

| Comparison | Result |
| --- | --- |
| Multiplayer trees, ShowTrees yes/no | Identical fixed images; the pre-fix images differ at 104,499 pixels. The enabled-tree image is unchanged by the fix. Both reach frame 180 with CRC `0x590BD529`. |
| Bridge, ordinary and smoke shadows off | Fixed mapped/default output equals explicit legacy pixel-for-pixel; the pre-fix output differs at 65,415 pixels. CRC `0x0C57A5A2` at frame 180. |
| Smoke only, ordinary shadows off | Smoke capture remains active (635 casters from 16 systems in the recorded report). Enabling smoke changes 141,536 pixels; both runs keep CRC `0xF86BC92B` at frame 660. |
| Water, smoke and model palms | All 862 changed pixels lie on the palm canopy and become darker. The rest of the image is identical, and both executables keep CRC `0xEFD8EA0D` at frame 660. |

The harness now accepts `--trees`, `--geometry-shadows` and `--smoke-shadows` to reproduce
these combinations through private Options.ini files. Commands, manifests, logs, comparisons
and the incremental fix diff are retained in `build-linux/shadow-review-fixes/`.

These fixes are built in `build-linux/generals`. The packaged executable under
`build-linux/ZeroHourReforged/bin/` still has the pre-review SHA-256 recorded above and has
not been refreshed. The staged patch remains byte-identical to
`build-linux/shadow-phase1/01-baselines-and-modes.patch`; unrelated settings are unchanged.
No dependencies were installed and no commit was made. The broader shader-source generation
and duplicate FFT storage concerns remain separate architectural work.


## Upstream 3.3.0 renderer integration

The sync retains the Linux defaults, diagnostic shadow modes, four reflection modes,
modern-water receiving and the validation history above. Shared hazard-cloud state now uses
the backend-neutral core and capability checks; the isolated Linux preview keeps its existing
controls and cache stress measurements through an adapter. Ordinary SDL cloak casters use
the coverage-only shader helper, and sun-depth diagnostics use the independent extracted
allocation/readback path. Main-view tree batching reserves capacity for trees visible on screen
before admitting trees visible only to the sun camera.

Modern water keeps the fork's extended optical and projection block. The new Direct3D 11
modern-water backend binds the same six-layer FFT array layout and a typed analytic fallback.
Its water shadow constants remain neutral; Linux retains its mapped sun and smoke receiving.
Historical executable hashes and validation results above describe their recorded revisions;
they do not establish validation of this merge.
