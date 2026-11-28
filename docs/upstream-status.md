# Upstream status of this fork's features

This Linux-only experimental fork of [Zero Hour Reforged / zerohour.gg](https://zerohour.gg)
develops graphics work before offering selected changes to the
[upstream repository](https://github.com/olcayseygan/CnCGeneralsZH-Reforged).

The [4.1.0 upstream submission plan](upstream-submission-plan-4.1.0.md) records the PR order,
worktree branch names and acceptance checks for that submission.

**November 20–21, 2026 submission.** All 24 PRs from #241 through #264 merged on November 20–21 UTC.
Their merge commits and the manual port of #266 are on upstream `main` after the v4.2.0 release.
[Source snapshot `2fc8ef983`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/2fc8ef983d73049549c6064a02d221e84cccfdb2) ·
[Changes since upstream v4.2.0](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/compare/v4.2.0...2fc8ef983d73049549c6064a02d221e84cccfdb2)

## Linux 4.3.2

The Linux 4.3.2 update integrates upstream v4.3.2 and later commits through
`079bc531d`. It carries the Direct Strike updates, Apocalypse survival, Day and Night
matches, and ranked ELO displays. [Upstream release](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/tag/v4.3.2) ·
[Integrated source](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/079bc531da9481311df8578b9a688f06b128d89c) ·
[Linux release notes](releases/v4.3.2.md)

In this release, experimental path-traced water skips hidden captures and reuses scene data.
The Vulkan path updates moving geometry between acceleration-structure rebuilds. Mapped
sun shadows batch repeated opaque models into fewer draws. [Linux renderer PR #7](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/pull/7)

## Accepted

### Included in upstream v4.5.0

Upstream merged these five Linux contributions into `main` on November 28, 2026.
All five merge commits are in the v4.5.0 release. [Upstream release](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/tag/v4.5.0)

| Change | Upstream merge | Pull request |
|---|---|---|
| Reuse completed Vulkan upload buffers during play | `abe36f158` | [#280](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/280) |
| Keep the Cold Fusion Reactor's upgrade glow in step with its control rods | `c7c25cb28` | [#281](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/281) |
| Tune the stock polluted-water preset for murkier water, duller reflections and less wind response | `eea2ab078` | [#282](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/282) |
| Add depth-aware Clean FFT water on Linux, enabled with `CleanFftWater = yes` in `Options.ini` and off by default | `eeace0ce4` | [#283](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/283) |
| Fix the Linux terrain-skirt colour-mask type and the fracture renderer's case-sensitive include | `6d0b21077` | [#286](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/286) |

### After upstream v4.2.0

| Feature | Pull request | Upstream result |
|---|---|---|
| Battle Marks on SDL GPU | [#241](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/241) | `0e9619694`. Enables craters, stains and treads. |
| Native scene post-processing | [#242](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/242) | `23fd5bb25`. Ports Glow, Internal HDR and Reforged local contrast. The maintainer reworked the shader move in `86c323c39`. |
| Native ambient occlusion | [#243](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/243) | `575dfdc08`. Adds the SDL opaque-scene AO pass. |
| Soft particles and procedural FireLook | [#244](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/244) | `0edc4e5ae`. Adds depth fades and the procedural fire effect to SDL GPU. |
| Defence-range overlays | [#245](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/245) | `64b1fd82f`. Ports the smooth distance-field rings. |
| Classic-water foam | [#246](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/246) | `e9969f03d`. Ports shore foam for ponds and rivers. |
| Classic-water splashes | [#247](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/247) | `93832653a`. Enables impact spray, foam and rings. |
| Building collapse feedback | [#248](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/248) | `4e1427075`. Enables the collapse effect and large-building camera jolt. |
| Per-pixel dynamic lighting and Firelight | [#249](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/249) | `ff6a3d258`. Ports headlights, blast lighting and Firelight. |
| Hemispheric ambient lighting | [#250](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/250) | `4faaa368f`. Enables Sky and Ground Light. |
| Model status effects | [#251](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/251) | `1c357124b`. Ports EMP, outage, cloak and reveal effects. |
| Team Glow | [#252](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/252) | `eec12000e`. Enables the team-colour glow on SDL GPU. |
| Atmosphere and Warm Light | [#253](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/253) | `dea86201d`. Enables sky, horizon fade and haze on Linux. |
| Baseline weather | [#254](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/254) | `a4c1190d7`. Enables the existing rain, sand, snow and visual wind on Linux. |
| Heat haze and blast distortion | [#255](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/255) | `4f05fa27a`. On SDL, the Heat Haze checkbox is greyed out and `-heathaze` enables the effect with post-processing active. |
| Native GPU particles | [#256](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/256) | `f7f0c4361`. Adds the SDL GPU overflow path with a CPU fallback. |
| Matching speech-cue wait reset | [#257](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/257) | `337a2ab45`. Restarts the wait for the requested line. |
| Frozen-world countdown timing | [#258](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/258) | `c03937468`. Keeps countdowns at the selected game speed while the world is frozen. |
| Briefing narration | [#259](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/259) | `3ede49e98`. Restores original Generals briefing voices and the Zero Hour Training voice. |
| Scripted camera smoothing | [#260](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/260) | `f4dec205a`. Interpolates camera motion in single-player missions. |
| Repeated-frame animation | [#261](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/261) | `0b89fa1df`. Smooths model animation between ticks during single-player missions. |
| Reskin startup validation | [#262](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/262) | `b4f882a65`. Partial port. Upstream exempts explicit `RemoveModule` tags and still reports later parent additions missing from the reskin. The fork checks the copy-time baseline. [Maintainer review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/262#issuecomment-7049271501) |
| Underwater Clarity | [#263](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/263) | `ea3383647`. Linux Water-tab option, off by default. This is the rework requested in #172. |
| Map-Aware Waves | [#264](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/264) | `9fdc3fa94`. Linux Water-tab option, off by default. Waves follow shorelines, channels and shelter. |
| Optional toxic-water material controls | [#266](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/266), closed | Custom rules gained `Turbidity`, `SurfaceRoughness`, `RippleDamping` and `WindResistance`, each defaulting to zero. The original port left the stock rules unset. The tuned preset followed in #282. [Manual port](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/60ba30689b336cb05e4085d4954c0f40128aeac4) |

The maintainer combined Battle Marks, Classic-water splashes and collapse in `b998791fe` and recorded
the three PR merges above. For the renderer stack, the maintainer ran Windows x64 Release checks.
The SDL evidence in the PRs comes from earlier Linux builds. The rebased Linux CI runs stopped at the
`test_bigfilesystem` link. [Combined port review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/241#issuecomment-7052436774) ·
[Renderer stack review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/254#issuecomment-7052435876)

### Earlier contributions

| Feature | Pull request | Upstream result |
|---|---|---|
| Preserve composed model poses when pausing | [#124](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/124) | Merged 2026-10-22 as `20981c9b5`. |
| Linux mapped sun and smoke shadows, including tree silhouettes | [#119](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/119) | Merged 2026-10-23 as `1713e26d`. |
| Linux Modern water waves, shore foam, ship wakes and material rules | [#120](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/120) | Merged 2026-10-23 as `6b3eeba0`. |
| Volumetric toxin, anthrax and radiation fields | [#121](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/121) | Merged 2026-10-23 as `d5d90427`. The baseline includes nuclear embers and heat shimmer. |
| POSIX D3D9 reset and depth compatibility | [#134](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/134) | Merged 2026-10-25 as `bfe73a5c0`. |
| Shared volumetric cloud volume | [#170](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/170) | Ported as `b0e9f004` and merged 2026-11-06 as `acae6731c`. The shared volume removes the eight-field limit on SDL. Direct3D 11 keeps its eight-layer path. |
| Modern-water reflections (Legacy, SSR and Hybrid), the Water tab and quality controls | [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172) | Ported as `c6f4f4ca1` and merged 2026-11-06 as `234d55a20`. The maintainer rewrote the Turkish strings, with Legacy labelled "Orijinal". The original clarity commit `5d8a58e7` was held for an opt-in rework that landed in #263. |
| Modern-water render caches and load-time preparation | [#173](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/173) | Ported as `fcd05658f` and merged 2026-11-06 as `0244f2a9f`. |

The maintainer ported #170, #172 and #173 by hand and credited this fork's author as co-author.
They were prepared against v3.8.0 (`e3e1a898`) and shipped in the following release. The SDL side
relied on this fork's testing.

## Current fork-only feature overview

| Feature | Upstream disposition / evidence |
|---|---|
| Expanded weather | Gusts, intermittent rain, snow and sand layers, land/water contacts, ambience and haze/sky coupling extend the baseline SDL weather port. [Weather guide](linux.md#weather) |
| Vulkan water optimizations after Linux v4.3.2 | The fork skips work outside shoreline and hull-contact regions, adds an opt-in compute FFT, and records GPU times for individual water passes. [Implementation and measurements](#vulkan-water-update-2026-11-28) |
| Live water material preview | The standalone Linux preview uses the game renderer to edit material rules and includes a Toxic Look preset. [Material guide](water-preview.md) |
| Path-traced water reflections | The fork offers an experimental path-traced mode. Upstream v4.5.0 lists Mirror, ScreenSpace, Hybrid and SkyOnly. [Upstream reflection modes](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/v4.5.0/GeneralsMD/Code/Libraries/Include/Platform/WaterScreenReflection.h#L8) |
| Volumetric Contaminate, toxin impacts and attached puddles | #171 closed without merging. The shared volume for toxin and radiation fields landed in #170. [Upstream recognition policy](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/2fc8ef983d73049549c6064a02d221e84cccfdb2/GeneralsMD/Code/Libraries/Include/Platform/HazardCloudPolicy.h) |
| Asset Preloading | #169 closed without merging. It covered the encoded-audio cache, which supports ready-to-play briefings, and renderer pipeline warmup. |
| Submerged trees, fire, smoke and steam | The fork dampens tree sway and combustion effects below the waterline, with steam on a dry-to-wet crossing. See the local validation below. |

Other work in the fork includes its Linux packaging, CI and developer tools, along with changes to
adjacent Steam Generals folder discovery, path cache invalidation and portable hotkey literals.

## Closed proposals

| Feature | Pull request | Maintainer's reason | What would be considered |
|---|---|---|---|
| Asset Preloading | [#169](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/169) | No measured load-time gain. The audio preload read up to 256 MiB at each map load, and upstream already used a 64 MB streaming cache. | A Linux/macOS pipeline-warmup PR with measurements. |
| Volumetric Contaminate, toxin impacts and attached puddles | [#171](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/171) | The effects add GPU work when the screen is already busy. The PR had no timings for that case. Upstream had previously removed volumetric smoke over GPU cost. | A smaller PR with SDL GPU frame times from a crowded toxin fight. |
| Original Clean FFT proposal | [#265](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/265) | In the submitted comparison on earlier RTX 4060 Release builds, the carrier scene's p50 Present interval rose from 8.43 to 12.40 ms. The maintainer also flagged shader source text replacements. [Maintainer review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/265#issuecomment-7050110786) | The follow-up used explicit shader branches and measured a 0.157 ms median increase in the carrier scene. [Merged PR #283](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/283) |

## Local upstream integration (2026-11-16)

Local branch `sync/post-4.0.0-upstream` integrated upstream `main` snapshot
`761ee2ccce81f5249b8d1ab44cd789057dc7b265` with this fork's baseline
`dbaa972641c8b9eb048ac3b599491513cd468bd5`. Validation below used the resolved working tree
**before the merge commit**, recorded as `dbaa972-dirty`.

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
attempts and unchanged-fixture evidence alongside final affected reruns.

## Local feature validation

### Vulkan water update (2026-11-28)

Linux `main` keeps the raster FFT as the default. The optional Vulkan compute FFT uses the
selected wave resolution and recovers through the raster path if a compute update fails.
At 256 × 256, a full wave update takes 14 passes with compute and 26 with raster.
[Fork commit `dbe9021e5`](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/commit/dbe9021e5e0042b2f64f3664a4a34c77c3ddb4ad) ·
[Runtime options](linux.md#modern-water-options)

On an RTX 4060 at 1920 × 1080, both paths reproduced every RGB pixel in the frozen carrier
capture rendered with the v4.3.2 water shader. With the waves animated, median host
draw-and-present time was 3.55 ms with that shader, 3.44 ms with the updated shader and
raster FFT, and 3.33 ms with compute. Each figure includes 1,000 measured frames across two
runs. In a separate 120-frame GPU timing sample, the median FFT update took 0.50 ms with
raster and 0.28 ms with compute. [Preview benchmark method](water-preview.md#bounded-host-benchmark)

### SDL GPU upload cleanup on Vulkan (2026-11-22)

The Linux 4.2.1 AppImage and the fork both retained GPU upload memory after panning in USA 2.
In `SdlGpuFrame::Submit_Flush`, the fork now calls `SDL_WaitForGPUFences` after a fence signals
completion and before releasing it. This lets SDL retire completed command buffers and reuse
their upload memory while pending work continues asynchronously.
[Fork fix `428e6a376`](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/commit/428e6a376b3cf43c474bbfb1bdc848be23e1aa07) ·
[Upstream flush path at `aa37ddbaa`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/aa37ddbaaf696f1389b69788c9a9c4c6229fd0f7/GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp#L851-L856)

On an RTX 4060 at 1920 × 1080 with all 14 HD archives, each run loaded the manual USA 2 save,
panned twice at the saved zoom, then returned to the original view for three minutes. The table
shows growth in the game's proportional set size (PSS) from before panning to the end of the
wait. The pre-fix runs used GPU pacing during loading only. The fixed runs were asynchronous from launch.

| Route | Growth before fix | Growth with fix | Final PSS with fix |
|---|---:|---:|---:|
| Northwest circuit | 1,965 MiB | 91 MiB | 5.66 GiB |
| Broad map sweep | 1,875 MiB | 102 MiB | 5.68 GiB |

A separate northwest comparison used identical loading and camera input on both builds. The
one-line fix cut game PSS growth from 1,965 MiB to 83 MiB (96%). The original 12–13 GB
reading in KDE covered the whole desktop.

The fixed game completed 25 broad circuits at the saved zoom in 15 min 15 s. Game PSS rose
from 5.591 to 5.807 GiB (+221.38 MiB). A parked camera added 38.97 MiB over the same
number of intervals. Both runs protected one convoy truck after the fifth interval to keep
the mission active.

In a separate allocation trace, Vulkan descriptor pools and sets accounted for about 64.1 MiB
of growth from pass 5 to pass 25. The two water mesh caches accounted for about 0.4 MiB.
In a second traced session, SDL held 513 descriptor pools from intervals 5 through 25 with
the camera parked. Panning raised the count to 1,029 in five circuits. It held there for three more circuits, and
all 897 sampled descriptor allocations were freed at GPU shutdown. The fence fix resolves
the multi-GiB upload retention reproduced in USA 2. The descriptor pools serve later draws
at the same load, so the fork keeps SDL's existing cache policy.
[SDL descriptor cache reuse](https://github.com/libsdl-org/SDL/blob/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c/src/gpu/vulkan/SDL_gpu_vulkan.c#L5112-L5164)

Local evidence is under `build-linux/benchmarks/usa2-memory-9ab3b716a/pan-investigation/`.
`SCROLLING.md` records the matched fence comparison, `repeated-main-broad-extended-01/REPORT.md`
records all 25 returns, and `heap-sources/REPORT.md` records the allocation and shutdown traces.

At 1440 × 810, paired median clean-water frame time improved by 0.093 ms on the carrier and
0.024 ms on the coast compared with the pre-fix fork. Across eight clean-versus-ordinary Modern
water pairs on the fixed build, the largest added cost was 0.021 ms at p50 and 0.329 ms at p95.
The SDL GPU frame, water GPU path and water-grid checks passed. Local measurements are in
`build-linux/benchmarks/main-gpu-upload-cleanup-9ab3b716a/REPORT.md`.

### Toxic-water materials and production-water preview (2026-11-16)

Fork commit [`6e49914d`](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/commit/6e49914d62aeb85f84a89abfeb286bbdabc79b54)
adds independent `Turbidity`, `SurfaceRoughness`, `RippleDamping` and
`WindResistance` material controls to the game, shared INI parser and native water preview. Wind
resistance scales wind-wave displacement, chop and glint response by `(1 - resistance)^2`.
Vessel waves and the bubble indicator stay independent. Old exports can still be loaded, with
their scum fields ignored. The preview binds the production renderer's depth, direction and
rotation fields and verifies accepted main-water draws.

The `PollutedWater` rule uses tint `#2D3A0B`, brightness 0.55, pollution 0.70, turbidity 1.00,
roughness 0.69, ripple damping 0.34 and wind resistance 0.55, with bubbles enabled. The shipped INI,
built-in fallback and preview's Toxic Look preset agree. See [the material guide](water-preview.md).

The game and preview rebuilt, and the existing `test_modern_water_config`, `test_water_preview_preset`
and `test_water_grid` CTests passed. Default and Toxic Look captures were pixel-identical, with an
accepted production main-water draw and no refused draws. Local captures, exported rules and the
verification record are under `build-linux/water-approved-look-validation/`; earlier motion checks
are under `build-linux/water-wind-resistance-validation/`. The later local CI repair updated the
source-file manifest and five map-test expectations for these defaults. The four affected CTest
entries, including the full `test_gameengine` suite, passed locally.

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

### Expanded weather presentation (2026-11-15)

The local weather change adds gusts, layered snow, dry-ground blowing sand, intermittent
enhanced rain with land/water contacts, ambient audio and coupled haze/sky lighting. Authored precipitation
keeps its existing behavior. Weather remains optional and presentation-only; Classic/off/provider gates remain.
Water-aware dust and pre-water haze composition address shoreline readability. Rain fades crossfade the
existing warm haze when enabled, with explicit native and Windows phase ownership.

The measured v4 executable SHA `917e182cbd5f9f7fff24a10c2331996cbeb26f25be82d05d779f5e028e8ef55c`
passed scoped private-profile Vulkan/audio/lifecycle/HDR qualification and 27 matched baseline/final/off
runs (253,800 raw intervals). Active weather costs about +0.3 ms median, +0.4 ms mean and +0.9–1.0 ms p95
on the controlled scene; dry duty cycle is reported separately. The later randomized rain-startup follow-up
allows clear or rainy map openings, reproducible from the map and match seeds. Its executable SHA
`7a40e7d9e5a8630ef6021bbfa2cd2bb02d950dc4eadd6e685f23ed9a6910c50a` passed 4 focused CTests and 5 native
GPU checks, including repeated opening pixels and unchanged same-seed gameplay CRC versus v4. The
follow-up was staged in the build-tree package at the time. The 27-run timings apply to the v4
executable. Local source commits are `0529ecef` (audio), `e12ff8b8` (weather) and `f91069a2` (haze/lighting).
Windows and physical-speaker qualification remain unrun.
[Startup evidence](reference/weather-rain-startup-2026-11-15/README.md) ·
[v4 weather evidence guide](reference/weather-enhancements-2026-11-15/README.md)

### Native Linux renderer parity validated 2026-11-15

The fork's November 15 build used native SDL3/Vulkan for soft particles and procedural fire,
GPU particles, per-pixel/Firelight and hemispheric lighting, model status effects and team glow,
HDR/post-processing, ambient occlusion, sky/haze/weather, fire/beam/exhaust heat and blast distortion,
distance-field range overlays, Classic-water foam/splashes, building collapse and camera effects.
It also ran Battle Marks, shared volumetric clouds and the existing Modern-water paths. Options
queried renderer capabilities. The upstream ports landed later, as recorded in the acceptance table.
[Post-v4.2.0 ports](#after-upstream-v420)

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

The November 14 handoff reviewed source at upstream commit `2788b259` and records the evidence,
port boundaries and validation plan for that snapshot.
[Campaign handoff](campaign-fixes-upstream-handoff.md) ·
[`2788b259`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/2788b25956f81fc49031a9025fd3c738a8440299)

| Fork commit / slice | Upstream disposition |
|---|---|
| `6732c980` frozen-world countdown phase | Merged in #258. Upstream saves the phase in script snapshot version 7. [Port review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/258#issuecomment-7049270492) |
| `6732c980` matching speech-request wait reset | Merged in #257. `doSpeechPlay` now calls `notifySpeechRequested(speechName)` before playback. [Pinned source](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/2fc8ef983d73049549c6064a02d221e84cccfdb2/GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/ScriptActions.cpp#L2853-L2855) |
| `6732c980` original `GEN_*` briefing voice start | Merged in #259. The voice starts once at the movie cue, with fallback playback if the movie is missing, fails or is skipped in minimum-spec mode. Upstream kept the briefing movie running on loss of focus. [Port review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/259#issuecomment-7049270940) |
| `dd607c49` resident encoded-audio readiness | The fork's encoded RAM cache supplies this readiness path. The cache proposal #169 closed without merging. |
| `f7e94b27` fractional camera pose and repeated-frame animation clock | Merged in #260 and #261. The maintainer adapted the tests to use a standalone cinematic camera suite on Windows. [Camera review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/260#issuecomment-7053318166) · [Animation review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/261#issuecomment-7054254321) |
| `f7e94b27` live-submission eligibility | The November 14 source review found that upstream's static eligibility gate passed at its 30 Hz logic rate. The fork's 60 Hz/44 Hz case remains in the handoff for reference. |
| `52cbad0c` Clean FFT shoreline distance guard | First proposed in #265. The merged Clean FFT follow-up also checks the sample index before reading shoreline distance. [Upstream guard in v4.5.0](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/v4.5.0/GeneralsMD/Code/Libraries/Include/Platform/WaterCleanShore.h#L132-L137) |

### Modern-water benchmarks (2026-11-11)

These timings use the earlier builds and settings documented in the benchmark reports.

- **Underwater clarity** added 1.5% GPU time at 1080p on an RTX 4060. Open sea measured 12.17 ms with
  clarity and 11.99 ms without it. The shore-heavy scene measured 5.99 ms and 5.90 ms, respectively.
  [Clarity measurements](reference/water-penalty-2026-11-11/README.md)
- **Clean FFT water** measured 11.97 ms on open sea against upstream `bda651dd` at 11.06 ms (+8.3%).
  The shore-heavy scene measured 6.18 ms against 6.68 ms (−7.5%). These compare the whole fork build
  with upstream. Low water quality with shore turning disabled saved 21% on open sea in the fork
  benchmark. [Build comparison](reference/water-penalty-2026-11-11/README.md) ·
  [Quality benchmark](reference/water-benchmark-2026-11-11/README.md)

## Ideas for later pull requests

- **Volumetric toxin effects.** Measure the delta over the shared volume in a crowded toxin fight.
  Use the SDL GPU timer (`ZH_GPU_TIMING` with `ZH_GPU_TIMING_SYNC=1`) and a repeatable battle
  (`-scenario`, files in `Data/Scenarios/`). The devlog's 2026-11-05 benchmark session has a crowded-cloud
  save. The work is on branch `feat/clouds-contaminate-effects`.
- **Pipeline warmup for Linux and macOS.** Measure first-use shader stalls with and without warmup.
  The work is on branch `perf/preloading-and-warmup`.
- **Linux validation of the merged SDL ports.** Run the renderer checks against the upstream commits
  and record the build, driver, settings and scene for each result.

## What upstream's maintainer expects

These come from the reviews above:

- Measurements with every performance or visual-cost change, before and after
- No new `ZH_*` environment switches or diagnostic output
- Renderer reads taken at the sync point, not from live game logic
- New looks off by default, gated on a backend capability rather than `__linux__`
- One change per pull request, rebased onto current `main`
