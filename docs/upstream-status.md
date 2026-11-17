# Upstream status of this fork's features

This Linux-only experimental fork of [Zero Hour Reforged / zerohour.gg](https://zerohour.gg)
develops graphics work before offering selected changes to the
[upstream repository](https://github.com/olcayseygan/CnCGeneralsZH-Reforged). Acceptance is tracked
per feature: an accepted foundation does not mean every later extension was also taken.

**Status reviewed 2026-11-16.** GitHub reports #119, #120, #121, #170, #172 and #173 merged;
#169 and #171 are closed without merging. Source spot checks use upstream `main` pinned at
[`e4341a1f`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f).
The author's public pull-request history was also checked; the later parity, weather, campaign
and water-material work has no new submission there. This was a status/source review, not an
upstream build or runtime validation. Historical benchmarks retain their original dates and binaries.

The detailed campaign-fix audit remains pinned to its own 2026-11-14 review of
[`2788b259`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/2788b25956f81fc49031a9025fd3c738a8440299).
The current source still lacks the fork's speech-request wait reset in
[`doSpeechPlay`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptActions.cpp#L2860).
This spot check does not replace that audit's port boundaries or establish an upstream campaign bug at runtime.

## Current fork-only feature overview

| Feature | Upstream disposition / evidence |
|---|---|
| Native Linux ports of the broader DX11 effects, including weather, Firelight, AO, HDR, soft/GPU particles, distortion, range overlays, Battle Marks and Classic-water foam/splashes | Not offered upstream. These are the native-provider changes validated on 2026-11-15; the earlier sun/smoke-shadow port is already accepted separately. Upstream's [SDL renderer directory](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/tree/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f/GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render) does not contain these new providers. |
| Expanded weather: gusts, intermittent rain, snow/sand layers, contacts, ambience and haze/sky coupling | Not offered upstream; extends this fork's Linux effects port. See the dated weather evidence below. |
| Underwater clarity | Left out of #172. Upstream's [material configuration](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f/GeneralsMD/Code/Libraries/Include/Platform/ModernWaterConfig.h) has no clarity field. |
| Reworked FFT water (Clean FFT) | Not offered upstream. Reworks the original Modern-water wave pipeline with a shared FFT ocean, JONSWAP spectrum, depth-aware attenuation, filtered glints and wave-driven foam. The accepted baseline Modern-water port does not include this rework. See the [implementation report](modern-water-clean-fft-report.md). |
| Map-aware water movement | Not offered upstream. The newer wave-heading and depth/rotation fields remain fork work; the baseline Modern-water port is already accepted. |
| Toxic-material turbidity, roughness, ripple damping and wind resistance; live production-water preview | Committed in this fork as `6e49914d` on 2026-11-16, not offered upstream. These controls are absent from the pinned upstream material configuration. |
| Path-traced water reflections | Not offered upstream. Upstream's [reflection modes](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f/GeneralsMD/Code/Libraries/Include/Platform/WaterScreenReflection.h#L8) include Mirror, ScreenSpace, Hybrid and SkyOnly, without this path-tracing mode. |
| Volumetric Contaminate, toxin impacts and attached puddles | #171 closed without merging. The shared renderer itself was accepted as #170; upstream's [recognition policy](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/e4341a1f6bff1c4296340388a0ebbb9dd5c9105f/GeneralsMD/Code/Libraries/Include/Platform/HazardCloudPolicy.h) retains the narrower source set. |
| Asset Preloading: encoded-audio cache and renderer pipeline warmup | #169 closed without merging. This is separate from accepted water-specific cache preparation in #173. |
| Campaign narration, speech timing, cinematic smoothing and the fork's Modern-water startup guard | No new upstream submission. The detailed handoff below distinguishes adaptable candidates from fork-only dependencies; the USA01 crash guard fixes this fork's water helper, which is absent upstream. |

## Local upstream integration (2026-11-16)

Local branch `sync/post-4.0.0-upstream` integrates upstream `main` snapshot
`761ee2ccce81f5249b8d1ab44cd789057dc7b265` with this fork's baseline
`dbaa972641c8b9eb048ac3b599491513cd468bd5`. Validation below used the resolved working tree
**before the merge commit**, recorded as `dbaa972-dirty`. This local
integration does not change the historical upstream acceptance classifications below.

The native SDL3/Vulkan additions are height-derived terrain sun lighting, subdivided terrain and
Ground Foliage, with provider capability checks. Ground Foliage stays off by default and is Ultra-only
among the presets; full-detail subdivided terrain is required, and Classic graphics excludes it.
The merge retains the Linux fork's particles, lighting, model effects, HDR/AO/post effects,
atmosphere/weather, heat/blast distortion, range overlays, battle marks, shared clouds, Classic-water
foam/splashes, underwater fire/tree handling and the existing High preset. EA's four unit veterancy
levels and this fork's removal of mod support remain unchanged; general talent ranks are separate.

The simulation now ticks at 30 Hz with 100%, 125%, 150% (default) and 200% game-speed choices.
Countdowns retain frozen-world progress and equality crossing at those speeds; fractional cinematic
poses, repeated-frame animation, campaign briefings and encoded-audio/preloading work remain.
Classic water gains upstream's bounded mirror cache while Modern water retains Clean FFT,
Legacy/SSR/Hybrid/Path Traced modes and their resource lifecycles. Packaging now includes the incoming
loose audio and W3D models alongside textures, with the overlay's INI CRLF conversion retained.

The reskin startup validator records successful explicit RemoveModule tags and an immutable copy-time
baseline of the behavior type/tag identities actually inherited. It excludes later parent-only additions
while continuing to detect unintended losses, including the unremoved tag of two same-type modules.
Overrides copy the provenance; a new reskin baseline resets it. No gameplay INI or module payload changed.

The pre-commit Release validation source fingerprint is `761A2673` over 5,629 files. The raw executable SHA-256 is
`e309e89449d1ee3f0e08c8d71ee0c065444b59083698ac8401373b90fc9ad822` and the stripped package executable is
`ace2ec525a632cda364bb0a4a65ccb98769966b858ec35914bc886a6a2ee20f1`. Release build and the final full game-engine/fingerprint
selection passed (3/3 in 49.59 s). New reskin tests passed 3 tests / 41 checks and the replacement regression
1 test / 10 checks; the actual startup oracle passed with 58 AI replacements / 0 lost locomotors and 324 reskins /
0 missing inherited behaviors in 87.05 s.

The reconciled 201-case ledger is **191 passed, 2 failed, 8 skipped**. The GPU/mixed subset is 24/25:
the known unchanged two-pixel fixed-function reference mismatch remains failed. The other failed case,
`packaging_resolution_check`, cannot arm its negative control without the absent optional genuine
`ReforgedTextures.big`; it is not counted as a pass. Positive package checks passed 26,726 resolved
paths, INI/EXE CRC agreement, 370 unchanged install files and implicit overlay discovery. The final
overlay matches all 335 audio files, 305 W3D models and 314 textures and retains INI CRLF. Corrected
private audio/texture fixtures passed, with all 7,342 textures loaded without errors.

Nine final native cases passed on the RTX 4060 / Vulkan, driver 595.112.07: seven renderer cases
(terrain/grass/sun controls, Classic water and Modern SSR/Hybrid/PT) shared `8B87FBA8@360`; two
Classic/Red Alert UI captures with real P/Q input and packaged-overlay discovery shared
`9880A8AE@360`. Fresh recording/replay/repeat and save/reload runs exited cleanly with
`88768E56@2400`. These establish bounded same-machine, same-build results, not cross-platform or
cross-version network proof. Debug application runtime, real remote multiplayer, exhaustive
GUI/campaign completion and long-duration stability remain unrun. Upstream's new formats reject
older saves/replays; existing player files were not modified.

Local CI preflight also passed all five shell syntax checks, three mocked no-art vendor checks and
14 release-helper fixture tests. The workflow now checks each shell script individually; an armed
malformed-second-script control fails with exit 2. The full Sniper/Ubuntu 22.04 software-rendering,
packaging/Flatpak and distro-install pipeline has **not run locally**. The remaining checks are
planned for GitHub; local CI-tool installation was not approved and is not being pursued. These
preflight results are not a full CI pass; evidence is under
`build-linux/ci-local/evidence/`.

The final handoff (`verification-handoff.txt` and `.json`), ledger and runtime artifacts are in
`build-linux/upstream-parity-evidence/acceptance/`;
focused diagnoses and the startup oracle remain in the sibling `final-validation/` directory.
[The Linux validation addendum](linux.md#current-upstream-integration-validation-2026-11-16) records
the checks, skipped-case limits and evidence names. The reconciled ledger retains earlier failed
attempts and unchanged-fixture evidence alongside final affected reruns. Older 60 Hz history,
benchmarks and upstream acceptance classifications below retain their original identities and scope.

## Accepted

| Feature | Pull request | Status |
|---|---|---|
| Linux mapped sun/smoke shadows and shadow parity, including tree silhouettes | [#119](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/119) | Merged 2026-10-23 as `1713e26d`. This is separate from the newer broad DX11-effects parity work. |
| Linux Modern water: waves, shore foam, ship wakes and material rules | [#120](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/120) | Merged 2026-10-23 as `6b3eeba0`. Later map-aware movement, clarity, path tracing and toxic-material controls were not part of this acceptance. |
| Volumetric toxin, anthrax and radiation fields | [#121](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/121) | Merged 2026-10-23 as `d5d90427`. The baseline includes nuclear embers and heat shimmer; extended toxin-source recognition remains separate. |
| Shared volumetric cloud volume: no eight-field limit | [#170](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/170) | Ported as `b0e9f004`. Direct3D 11 keeps its eight-layer path. |
| Modern water reflection modes (Legacy, SSR, Hybrid), the Water tab and quality controls | [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172) | Ported as `c6f4f4ca1`, merged by `234d55a20`. Turkish strings rewritten, and Legacy is "Orijinal" in Turkish. Underwater clarity left out (see below). |
| Modern water render caches and load-time preparation | [#173](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/173) | Ported as `fcd05658f`, merged by `0244f2a9f`. |

The three November entries (#170, #172 and #173) were ported by hand by the maintainer, with this fork's author
as co-author. They reached upstream's public `main` on 2026-11-06, which marked each pull request merged,
and they ship in the release after v3.8.0. Upstream's v3.8.0 does not have those three later changes.
They were prepared against v3.8.0 (`e3e1a898`); the earlier October ports use their own bases.
The maintainer could not run Linux or macOS for those November ports, so the SDL side shipped on this fork's testing.

## Not included in the accepted ports

| Feature | Pull request | Why it wasn't taken | What would be considered |
|---|---|---|---|
| Asset Preloading: audio preload and renderer pipeline warmup | [#169](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/169), closed | No measured load-time gain. The audio half reads up to 256 MiB at every map load, and upstream already streams sounds with a 64 MB cache. | A separate pull request for pipeline warmup alone, limited to Linux and macOS, with measurements |
| Underwater clarity: shallow seabed and ruins show through Modern water | Commit `5d8a58e7` in [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172), left out | It changes how white water looks as soon as Modern is picked, with no option to keep the old look, and the owner wants to see it first. No `ClearWater` rule ships. | Its own pull request, with before and after shots of the same views: the ruins, a flat offshore floor and a coast |
| Volumetric Contaminate, toxin impact clouds and attached toxin puddles | [#171](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/171), closed | More volumetric effects add GPU work to the heaviest frames, and nothing measured it. An earlier volumetric smoke feature was removed from the game over GPU cost. | A separate pull request with just these effects, plus SDL GPU frame times from a crowded toxin fight, with and without them |

## Not offered upstream

### Toxic-water materials and production-water preview (2026-11-16)

Fork commit [`6e49914d`](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/commit/6e49914d62aeb85f84a89abfeb286bbdabc79b54)
adds independent `Turbidity`, `SurfaceRoughness`, `RippleDamping` and
`WindResistance` material controls to the game, shared INI parser and native water preview. Wind
resistance scales actual wind-wave displacement, chop and glint response by `(1 - resistance)^2`;
vessel waves and the bubble indicator stay independent. Surface scum has been removed; old exported
scum fields remain readable and are ignored. The preview now binds the current production water's
depth/direction/rotation fields and verifies accepted main-water draws instead of showing the older path.

The `PollutedWater` rule uses tint `#2D3A0B`, brightness 0.55, pollution 0.70, turbidity 1.00,
roughness 0.69, ripple damping 0.34 and wind resistance 0.55, with bubbles enabled. The shipped INI,
built-in fallback and preview's Toxic Look preset agree. See [the material guide](water-preview.md).

The game and preview rebuilt, and the existing `test_modern_water_config`, `test_water_preview_preset`
and `test_water_grid` CTests passed. Default and Toxic Look captures were pixel-identical, with an
accepted production main-water draw and no refused draws. Local captures, exported rules and the
verification record are under `build-linux/water-approved-look-validation/`; earlier motion checks
are under `build-linux/water-wind-resistance-validation/`. This is committed fork work, with no
upstream pull request or upstream runtime/performance qualification claimed. The later local CI
repair updates the source-file manifest and five map-test expectations for these defaults;
the four affected CTest entries, including the full `test_gameengine` suite, passed locally.

### Underwater tree sway (2026-11-15) — locally validated

Enhanced graphics stills submerged vertices in the batched tree renderer, smoothly restoring
wind over three world units above the local waterline. Exposed branches retain their motion.
Reflections, mapped sun shadows and projected silhouettes share the weight; the projected
silhouette also accounts for the branch's vertical displacement. Water is sampled before
logic/render overlap and updates the affected vertices as flooding changes. Classic graphics,
legacy Windows D3D9 and the save format retain their existing behavior.

The Linux Release build passed along with `test_tree_water_sway`, `test_weather_particle_math`
and `posix_gpu_draw_selfcheck`. GPU readbacks verify opposite wind phases, dry/partial/submerged
coverage, alpha gaps and both shadow paths. A private China 3 load verified actual vertex data:
tree 67 has 16 submerged, eight transitioning and 26 fully exposed vertices; all 45 vertices
of tree 75 receive zero wind, while dry tree 0 retains full wind. Final native Vulkan captures
with Classic and Modern water exited normally after frame 39450, with no validation messages
in those game runs. The broader GPU draw selfcheck also logs two point-list `PointSize`
validation messages outside the tree checks, and intentionally injects shader failures.

The tested final executable SHA is
`459e87d5a8a91368ae132a7d263d2fb160c6eef645313ad94eec0422ad4cee7a`.
Local evidence is under `build-linux/tree-water-evidence/`, including `vertex-verification.json`
and the two final capture manifests. The original save checksum is unchanged. This work has
**not been offered upstream**. Windows/Metal runtime qualification and performance comparisons
remain unrun.

### Submerged fire, smoke and steam (2026-11-15) — locally validated

Enhanced graphics now fades submerged additive/emissive `FireLook` sources and clips their flame
billboards at each particle's local waterline. The same source fade reaches reflections, Firelight,
smoke illumination and heat haze. World emission heights preserve fires on roofs, bridges and vessels
above flooded terrain; water queries include polygon water and China mission 3's moving flood grid.
Known combustion-smoke sources stop emitting underwater before CPU or GPU allocation, while existing
smoke disperses over its remaining lifetime. Dust, toxins, exhaust and unknown mod systems retain
their behavior.

An observed dry-to-wet crossing produces a brief white steam puff above the surface, also clipped at
the waterline. Nearby flame sources share a cooldown, and the first observation after loading a
flooded save stays quiet. Steam is client-only and transient; Classic graphics, fire damage,
mission scripts and the save format remain unchanged.

The Release build and six focused CTests passed: `test_fire_lights`, `test_renderer_particles`,
`test_gameengine`, `test_water_grid`, `test_renderer_particles_gpu` and `test_sun_shadow_sorting`.
Native Vulkan checks include clipping readback, and a private China 3 save load reached frame 39700.
A simulated crossing in the private session verified steam placement, clipping and expiry; the
original save checksum stayed unchanged. The tested executable SHA is
`965d49f9feebde249ac6399185bf7bc603c888151581122592cc5806b09fb472`.
Local evidence is under `build-linux/fire-water-evidence/`, with the option 2 verification record in
`option2/verification.json`.

The source is committed as `43a0c976` and has **not been offered upstream**. Porting needs adaptation
to the target renderer's sorted-billboard and soft-particle payloads, with water sampled before
logic/render overlap.
Windows and Metal runtime validation and a performance comparison remain unrun.

### Windows DX11 AO option regression found during code review (2026-11-15)

Review of `9f965499` through `343477ff` found a Windows regression introduced by this fork's
`f91069a2` weather integration. With Classic graphics off and a DX11 post-processing scene active,
the opaque-phase dispatch could apply world ambient occlusion even when **Ambient Occlusion** was off.

The local fix passes the AO preference explicitly through `RenderPost::OpaqueScene(bool allowWorldAO)`.
Windows now gates the AO call while retaining independent pre-water weather haze. The native provider
also requires both the caller's permission and its saved AO setting. Regression coverage in
`test_sun_shadow_sorting.cpp` exercises the actual wrapper, AO on/off, independent haze, alpha preservation
and excluded render scopes.

Fix validation passed the Release build, 11 CPU tests and five GPU tests on each of NVIDIA Vulkan and
Lavapipe. Windows compilation and runtime remain untested; the Windows call chain received source review.
Local logs are under `build-linux/review-fixes-evidence/`.

This fix is committed as `72df8189` and has not been offered upstream. Carry it with
the renderer/weather integration; an equivalent defect in upstream's current Windows code has not been
established.

### Expanded weather presentation (2026-11-15) — locally qualified, not offered upstream

The local weather change adds gusts, layered snow, dry-ground blowing sand, intermittent
enhanced rain with land/water contacts, ambient audio and coupled haze/sky lighting. Authored precipitation
keeps its existing behavior. Weather remains optional and presentation-only; Classic/off/provider gates remain.
Water-aware dust and pre-water haze composition address shoreline readability. Rain fades crossfade the
existing warm haze when enabled, with explicit native and Windows phase ownership.

The accepted staged baseline is preserved separately from this work. The measured v4 executable SHA `917e182cbd5f9f7fff24a10c2331996cbeb26f25be82d05d779f5e028e8ef55c`
passed scoped private-profile Vulkan/audio/lifecycle/HDR qualification and 27 matched baseline/final/off
runs (253,800 raw intervals). Active weather costs about +0.3 ms median, +0.4 ms mean and +0.9–1.0 ms p95
on the controlled scene; dry duty cycle is reported separately. The later randomized rain-startup follow-up
allows clear or rainy map openings, reproducible from the map and match seeds. Its executable SHA
`7a40e7d9e5a8630ef6021bbfa2cd2bb02d950dc4eadd6e685f23ed9a6910c50a` passed 4 focused CTests and 5 native
GPU checks, including repeated opening pixels and unchanged same-seed gameplay CRC versus v4. It is staged
only in the build-tree package. The 27-run timings remain v4 measurements; they were not repeated for this
follow-up. See the [startup evidence](reference/weather-rain-startup-2026-11-15/README.md) and
[v4 weather evidence guide](reference/weather-enhancements-2026-11-15/README.md) for exact identities,
results and boundaries. Local source commits are `0529ecef` (audio), `e12ff8b8` (weather) and `f91069a2`
(haze/lighting), all on `main`. Windows and physical-speaker qualification remain unrun. This work has
**not been offered upstream**. No push, pull request or upstream runtime validation is claimed. Existing
history below is retained unchanged.

### Native Linux renderer parity validated 2026-11-15 — local, not offered upstream

This fork now implements the renderer enhancements through native SDL3/Vulkan: soft particles and procedural
fire, GPU particles, per-pixel/Firelight and hemispheric lighting, model status effects and team glow,
HDR/post-processing, ambient occlusion, sky/haze/weather, fire/beam/exhaust heat and blast distortion,
distance-field range overlays, Classic-water foam/splashes, building collapse and the associated camera effects.
Battle Marks, shared volumetric clouds and the existing Modern-water paths remain available. Options query
actual providers; native support does not report a false Direct3D 11 identity. This work has **not been offered
upstream**; no new pull request or upstream acceptance is claimed.

The validated local candidate is
`c7503c364f05364026d1b485ae21d9ff6aaa68bd71dd7fa95d799486cf381cfd`.
Evidence includes source-equivalence/translation and numeric GPU tests, plus actual game scenes with passive
queued-packet inspection, observed screenshots and matching gameplay checksums for paired scenarios:

- Sand, Rain and Snow producers; visible sky and distant-ground fade with live disable/restore; isolated
  day/night Warm Light, preserving weather precedence.
- Ordinary soft/fire packets, fire/beam/exhaust heat, blast warp, contact AO, HDR, shared clouds, terrain marks
  and readable HUD. The four firelook controls share `D9AE416E@960`; the same-actor combined pair shares
  `AE779F5A@960`. Dynamic bundle images are contrasts, not isolated per-pixel effect oracles.
- Severe explosion kick and a separate large-building collapse jolt. The collapse/Classic pair shares
  `63B8C63C@540`; the native STRONG shake and consumed presented-camera offset were observed.
- Authored model status effects, same-subject EMP recovery, team glow and Classic controls; real range-overlay
  placement after the camera-state correction; native particle admission, overflow, ordering and lifetime checks.
- Pond and river foam, including HDR and Classic/Modern exclusions; actual Modern-water SSR/Hybrid/PT smokes.
  The final GPU/mixed test suite passed 24/24 checks.
- Full-provider options passed 42 actual UI checks with 16 observed screenshots, covering toggles, Accept/reopen,
  Classic and the original High preset. An earlier limited-provider candidate separately proved unsupported-row
  value preservation. English Water Foam hover text was checked; Turkish text received source/key checks only.

The staged package contains this exact binary and its matching 262-entry overlay. A real HDR water scene ran
without an explicit `-overlay` argument, recorded 903 enhanced foam packets, captured frame 240, exited normally
with `A00B3F2F@300`, and matched the explicit-overlay reference image exactly. No Vulkan validation errors were
reported in that smoke. The canonical local acceptance ledger is
`build-linux/feature-parity-evidence/acceptance-ledger.json`; commands, source identities, rejected fixture runs,
images and limits remain under that evidence tree. Runtime validation here is NVIDIA RTX 4060/Vulkan on Linux;
Windows and Metal runtime/performance validation is not claimed. Native post-processing currently uses actual
single-sample scene targets. These bounded scenes do not imply exhaustive map/campaign coverage.

Validation limits also remain visible: the preceding broad CPU selection passed 141 checks and skipped two
(`test_bigfilesystem`: no `ZH_DATA_DIR`; `ffprogram_values_check`: no MinGW compiler). The narrow final foam-owner
change separately passed five CPU checks and 2,108 GPU assertions before the final 24/24 GPU/mixed suite.
A separate default fixed-function capture roundtrip **failed its nominal reference comparison** at 2 of 1,675
written pixels (worst RGB: GPU 2/50/65 versus reference 3/53/65 at 33,20). Capture replay agrees, and the failing
draw's generated HLSL/SPIR-V is byte-identical to the baseline; the numerical cause remains unresolved. This is
not reported as a pass or attributed to a driver. Exact results are retained in
`build-linux/feature-parity-evidence/final-validation/ffref-default-roundtrip/result.json`.

The [benchmark report](reference/linux-renderer-parity-2026-11-15/benchmarks/README.md),
[results CSV](reference/linux-renderer-parity-2026-11-15/benchmarks/results.csv) and
[accepted method/identities](reference/linux-renderer-parity-2026-11-15/benchmarks/acceptance.json) retain all
12 fresh-process timed runs, raw samples, settings, commands and reproduction scripts. Measurements use the same
candidate at 1920×1080/MSAA 0 on an RTX 4060 8 GB (driver 595.112.07), Intel Core i7-12700 and Linux
7.3.4-x64v3-xanmod1. They are **native end-to-end offscreen present intervals in milliseconds**, not hardware GPU
timestamps or displayed FPS. Three alternating pairs per comparison supply 600 post-warmup samples per repeat,
1,800 per variant; all six paired gameplay checksums and particle count audits passed.

| Comparison / variant | Mean ms | Median ms | p95 ms |
|---|---:|---:|---:|
| Controllable option bundle off | 2.469 | 1.854 | 3.836 |
| Controllable option bundle on | 3.217 | 2.720 | 5.285 |
| Matched total 5,000 particles: CPU only | 1.586 | 1.537 | 1.795 |
| Matched total 5,000 particles: 2 CPU + 4,998 native | 1.292 | 1.239 | 1.653 |

The bundle comparison holds nonClassic assets, water, shadows, model-state policy and GPU-particle policy fixed;
it measures selected controllable options in one authored scene, not the cost of the entire port or each effect.
The particle result is an **overflow-policy comparison**, with CPU ceiling 16,384/native ring disabled versus
CPU ceiling 1/native ring 8,192. Actual retained counts are 5,000 CPU versus 2 CPU + 4,998 native: not equal CPU
caps and not a pure-GPU workload. All 20 emitters were retained without drops; visible output and exact counts
were qualified before timing. Mean interval was 18.5% lower in this measured workload. No 100k/million-particle
speedup or break-even load is inferred. All repeats and the OFF repeat-1 297.976 ms outlier are retained without
trimming or best-run selection. No clock/power tuning was performed; read-only pre/post snapshots are retained,
not continuous thermal monitoring. These measurements do not replace the older water/upstream results below.

### Campaign, cinematic and startup fixes audited 2026-11-14

The detailed evidence, pinned source links, port boundaries and upstream validation plan are in
[`campaign-fixes-upstream-handoff.md`](campaign-fixes-upstream-handoff.md). These classifications are source-review
results only; no upstream build or runtime campaign validation was performed.

| Fork commit / slice | Tracker status for future work |
|---|---|
| `6732c980` frozen-world countdown phase | Candidate that needs adaptation to upstream's native 30 Hz clock and game-speed-scaled retail-tick accounting. Test 100%, 125%, 150% and 200%; do not cherry-pick the fork modulo helper. |
| `6732c980` matching speech-request wait reset | Candidate: upstream's lazy speech wait can predate the matching cue; add exact-name invalidation and a deterministic pre-query/request regression. |
| `6732c980` original `GEN_*` briefing voice start | Candidate load-screen integration gap. The campaign content/config already exists upstream; movie, minimum-spec and no-movie start paths need review. A post-voice wait budget is supporting work only after playback is restored. |
| `dd607c49` resident encoded-audio readiness | Blocked on separately porting the fork's encoded RAM cache. Not a standalone fix to upstream's ordinary asynchronous cold-file loader. |
| `f7e94b27` A1 fractional camera pose; A2 repeated-frame animation clock | Reachable candidates, but adapt the scaled camera clock and preserve upstream render/session exclusions. Validate on upstream clocks and new traces. |
| `f7e94b27` B live-submission eligibility | Fork's specific 60 Hz/44 Hz false-negative does not apply: upstream's static 60 Hz gate passes at its native 30 Hz logic rate. A measured live policy is only a conditional improvement pending an upstream case. |
| `52cbad0c` Clean FFT shoreline distance guard | Fork-only helper is absent upstream with its Clean FFT dependency. No standalone upstream port or upstream crash claim. Revisit only with a separate Clean FFT port or an upstream-owned reproduction. |

This addendum records local dispositions only. It is not a search for existing public pull requests or issues.

These stay fork-only, with no pull request so far:

- **Map-aware water movement:** waves turn toward the shore and follow channels. It's the code behind the GPU cost on the
  carrier save, and turning it off doesn't remove most of that cost.
- **Path-traced water reflections:** experimental.
- **Reworked FFT water (Clean FFT)** (2026-11-11): one global FFT ocean (JONSWAP spectrum, per-band depth fade, filtered glint,
  Jacobian foam) with waves turning toward the shore. Current controls separate Shore wave turning and Wave
  filtering, with Wave detail Low (64), Medium (128) and High (256). It's now the fork's Modern water on Linux;
  `ZH_WATER_CLEAN=0` restores the previous water. The measurements below retain their historical control labels.
- **Small fixes:** finding Steam's Generals folder beside Zero Hour, path cache invalidation, and portable hotkey literals.
- **Release packaging, CI and the developer tools** that only this fork uses.

## Ideas for later pull requests

Each would follow the maintainer's notes above, and each needs measurements first:

- **Underwater clarity:** open it as its own pull request with before and after shots of the same views: the ruins,
  a flat offshore floor and a coast. Making it an option, off by default, would answer the "no way back to the old
  look" objection. The commit is `5d8a58e7` on branch `feat/water-reflections-and-controls`. Measured cost
  (2026-11-11, RTX 4060, 1080p, the same build with and without the clarity code): **+1.5%** GPU time on both an
  open-sea and a shore-heavy scene (12.17 vs 11.99 ms, 5.99 vs 5.90 ms). Evidence:
  [`reference/water-penalty-2026-11-11/`](reference/water-penalty-2026-11-11/README.md).
- **Clean FFT water:** measured against upstream `main` (`bda651dd`) on the same scenes: **+8.3%** GPU time on open
  sea (11.97 vs 11.06 ms) and **−7.5%** on a shore-heavy map (6.18 vs 6.68 ms). Water quality Low (no shore turning)
  saved 21% on open sea, below upstream. These compare the whole fork build with upstream, so an exact figure needs
  the water ported onto upstream `main`; upstream would also want the testing switches and tuner removed and the new
  look off by default. It depends on the wave heading field from map-aware movement. Evidence:
  [`reference/water-penalty-2026-11-11/`](reference/water-penalty-2026-11-11/README.md) and
  [`reference/water-benchmark-2026-11-11/`](reference/water-benchmark-2026-11-11/README.md).
- **Volumetric toxin effects:** just the delta over the shared volume, with SDL GPU frame times from a crowded toxin
  fight, with and without it. Upstream already has a GPU timer (`ZH_GPU_TIMING` with `ZH_GPU_TIMING_SYNC=1`) and a
  scripted, repeatable battle (`-scenario`, files in `Data/Scenarios/`). A crowded-cloud save is kept in the devlog's
  2026-11-05 benchmark session. The work is on branch `feat/clouds-contaminate-effects`.
- **Pipeline warmup only, Linux and macOS:** needs a measurement that shows first-use shader stalls going away, which
  plain load-time numbers didn't. The work is on branch `perf/preloading-and-warmup`.
- **Map-aware water movement:** only after its shader cost is fixed, with a real off path that removes the cost.
- **Test the merged features on Linux** in upstream's next release, since nobody upstream can run them.

## What upstream's maintainer expects

These come from the reviews above:

- Measurements with every performance or visual-cost change, before and after
- No new `ZH_*` environment switches or diagnostic output
- Renderer reads taken at the sync point, not from live game logic
- New looks off by default, gated on a backend capability rather than `__linux__`
- One change per pull request, rebased onto current `main`
