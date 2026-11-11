# Clean FFT Modern water experiment — 2026-11-10


## Clean FFT follows the heading field (2026-11-10)

Clean water now retains FFT displacement, slopes, mip handling and glints while
following the original `WaterWaveHeading` travel-time field at the main water
level. Its solve uses the default game's distance-to-shore speed model, without
the experimental effective-D override. Effective D still controls clean amplitude.
`WaterLocalWaveField` retains the original heading words in slots 0/1 and its
existing local metadata in slots 2–4.

At each hex vertex, `fftSwellField(vertex).yz` supplies `g`. The incoming playable
edges seed lower travel times; the old swell phase is `k*U-omega*t`, so a crest
advances toward larger U, along **+grad U**. The clean vertex rotation is
`heading_follow * delta(actualFftTravel, normalize(g))`, uncapped. Invalid or tiny
gradients give zero heading rotation. `heading_follow` is live, ranges 0–1 and
defaults to **1**. It uses spare `WaterTuningChop.z`, without growing the uniform
block. The tuner labels it **Heading**, with the plain wave-path hint, and copies
`ZH_WATER_CLEAN_HEADING_FOLLOW=value`. Legacy INIs/presets missing it load 1.

`shore_turn` remains **0** by default. When enabled, its existing adaptive
shoreward direction adds
`clamp(delta(fieldDirection, shoreward)*shore_turn*gate*fade, ±shore_max_turn)`
after the heading rotation. `fieldDirection` is normalized g (global travel when
invalid); the cap applies only to this extra rotation. Existing reach, confidence,
patch blending and band keys remain. `all` still rotates all three FFT bands.
The `clean-shore` hue uses the final applied heading-plus-shore direction.
With both strengths zero, live shaders return the original clean evaluator exactly;
nonlive shaders omit the rotation evaluator entirely.

Auto spectrum travel now comes from `WaterWaveHeading::Field::prevailing`,
including its original `BaseHeading` fallback for symmetric/open maps. The newer
distance/confidence shore estimator has been removed. A field-unavailable map
uses the configured angle. The chosen auto direction and effective mode are
logged once during map preparation and published to the existing tuner compass
sidecar. Its compatibility `dominant` flag now denotes field availability and
`strength=1`; it is not an estimator score. Manual `travel_angle` changes move
the solve's upwind edges, rebuild/upload the heading atlas from retained map
inputs and reseed the spectrum. Replacing the heading texture releases its old
reference; failed uploads retain the old texture and retry. Constant-only heading
and shore edits do not rebuild the field or spectrum.

The main clean atlas uses **seven RGBA8 words per cell** whenever heading or
shore rotation is compiled in (including live tuning). It uses six words with
both effects compiled out. Padded cells repeat the edge cells. Channel names
below describe sampled RGBA, although CPU words are A8R8G8B8:

| Word | Channels / meaning |
| --- | --- |
| 0 | RG: high/low bytes of U; B/A: signed gradient x/y, `(byte-128)*8/127`. |
| 1 | R: swell amplitude; A: validity; G/B: existing packed local bank vector when present. |
| 2–3 | Existing local route phases in RG and gradients in B/A. |
| 4 | R/G: local route amplitudes; B: local influence; A: residual envelope. |
| 5 | R/B: optional shoreward x/y; G: effective D/40; A: main-level validity plus shore confidence (`128+round(127*c)`). |
| 6 | R: independent shore distance/3000; other channels zero. |

The swell reader addresses words 0/1 at stride **seven**, while D stays at word
five. The gradient read costs eight point samples per hex vertex (four validity
weighted corners), or 24 per tile setup. Default shore pull skips its separate
24 reads; enabling it adds those reads. No sampler or binding was added.

Validation for this revision:

- Required `cmake --build build-linux --target generals -- -k 0`: passed;
  shader/water/dump/GPU-test targets also built. Compiler warnings remain.
- Full CPU suites: default **240 tests / 19,521,579 checks**, CLEAN+TUNING
  **240 / 19,521,580**, zero failures. Includes +grad-U phase sign, smooth island
  bending, manual seed-edge reversal and five-word transport, zero-follow identity,
  independent shore clamp, live parsing/env compatibility and legacy defaults.
- Eight full shader suites passed, **59 tests each**: default, wind, shared,
  unrotated clean, heading clean, manual/partial heading, live tuning, heading plus
  shore pull. Includes SPIR-V validation, MSL translation, both stages and texture
  representations. All **27** shore/profile cases and the force diagnostic passed.
- Before/after sampling hashes match: default `6ffc0ab0ecd4ecb7`, wind
  `68746f3e518fd87b`, shared `692f0c5c6af7c3d1`; clean with `heading_follow=0`
  and `shore_turn=0` matches the prior unrotated clean `a01824d29b9d341d`.
  All **71 SDL material dump files per protected mode**, including zero-control
  clean, are byte-identical. Live zero controls take the explicit original-evaluator
  branch; GPU surface comparison could not run.
- Python tuner: **8 passed, 2 skipped** (loopback sockets denied; Node unavailable).
  The range, legacy file/preset, copied environment and hint checks passed.
- GPU test: **77 / SKIP**, `SDL_Init: No available video device`. Metal linking,
  in-game motion and USA 2/SWANGLE visual acceptance remain unverified.
- `git diff --check`: passed. Test engine data and temporary files used scratch
  folders. No commits, branch changes, installs, main-checkout edits, or writes
  under `build-linux/ab/` or `build-linux/play-tune/`.

Short output: `/tmp/modern-water-heading-follow-report.md`; logs and hash/dump
evidence: `/tmp/water-heading-validation/`. Earlier revision notes below are historical.

Adaptive-shore / automatic-sea revision (2026-11-10): distance-selected 60/180/500/1000-unit gradients restore local bank normals; clean-only `travel_mode=auto|manual` defaults to auto with a 0.2 dominance threshold, preserved manual fallback, read-only tuner angle/auto compass, existing spectrum rebuild and unchanged 60° turn cap. Build, both full CPU suites, six shader suites, 27 profile cases, tuner checks, sanitizers and protected hashes passed; GPU execution skipped without a video device. Report: `/tmp/modern-water-auto-shore-report.md`.

Implemented in `/mnt/sandisk-2tb/worktrees/water-tempo-ab`, branch `experiment/water-tempo-ab`. Select with `ZH_WATER_CLEAN=1`. CLEAN implies the existing one-medium D build and **wins over SHARED**, including atlas allocation: no shared amplitude field is built. No commits, branches, installs, main-checkout edits, or changes under `build-linux/ab/`.

Prevailing-sea revision (2026-11-10): live `travel_angle` now shows actual map travel with a +x-right/+y-up compass (default −135° = 225°, legacy `wind_angle` compatible); `shore_max_turn` defaults to 60° and clamps the final weighted bend (180° restores the old result), while reach stays 800 with island-map guidance. Required build, CPU/tuner tests, six shader suites, 27 shore/profile cases and protected hashes passed; GPU execution skipped without a video device. Report: `/tmp/modern-water-prevailing-direction-report.md`.

Changed for this experiment:

- New `Libraries/Include/Platform/WaterCleanSpectrum.h` and `WaterCleanShader.h`.
- `Libraries/Include/Platform/WaterFftSpectrum.h`, `WaterFftFiltering.h`, `WaterFftSampling.h`.
- `Libraries/Source/WWVegas/WW3D2/engineshader.cpp`.
- `GameEngineDevice/Include/W3DDevice/GameClient/W3DWater.h`, `GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`, and `GameEngineDevice/Source/PosixDevice/Render/PosixDevice9Draw.cpp`.
- New `Tests/WaterCleanSpectrumTest.h`; updated `Tests/test_water_grid.cpp`, `Tests/test_shader_sdl.cpp`, and `CMakeLists.txt`.
- This report. Code paths above are relative to `GeneralsMD/Code/`. Other pre-existing experiment files/changes were retained.

**Spectrum and units.** `ZH_WATER_CLEAN_SPECTRUM="windSpeed,fetch,swell"` defaults to `8,20000,0.7`: U10 in m/s, fetch in metres, dimensionless Horvath swell. One reference metre maps to `WorldScale=20` game units, including FFT wavelength/displacement. The 20 km fetch describes an offshore wind history extending beyond the playable map; it gives a dominant wavelength about 357 game units, useful for testing broad irregular crests at RTS scale. It is an explicit global default, not an estimate of each map's fetch; use the tuple to tune individual maps. The retained 0.4 FFT tempo scales simulation seconds, so apparent world speed is also scaled by 20×0.4. Dispersion remains exactly `omega=sqrt(9.81*k)` everywhere.

With `wp=22*(g*g/(U*F))^(1/3)`, `alpha=0.076*(U*U/(g*F))^0.22`, use
`S(w)=alpha*g*g*w^-5*exp(-1.25*(wp/w)^4)*3.3^r`, where
`r=exp(-(w-wp)^2/(2*sigma^2*wp^2))` and sigma is 0.07 below the peak / 0.09 above it.
For q=w/wp, Hasselmann's exponent is `s=6.97*q^4.06` below the peak and
`9.77*q^(-2.33-1.45*(U*wp/g-1.17))` above it. Add Horvath's
`16*tanh(wp/w)*swell^2`. Directional power is `C(s)*abs(cos(theta/2))^(2*s)`, with exact
`C(s)=Gamma(s+1)/(2*sqrt(pi)*Gamma(s+0.5))`. Convert frequency/angular power to Cartesian k-cell power using `(dw/dk)/k * dk^2`, then apply the existing radial `BandPower` split. These follow [Horvath 2015](https://doi.org/10.1145/2791261.2791267); the implemented spreading formula was cross-checked against [GodotOceanWaves' spectrum shader](https://github.com/2Retr0/GodotOceanWaves/blob/master/assets/shaders/compute/spectrum_compute.glsl). No TMA depth factor is used.

The deterministic RNG, zero DC/Nyquist axes and exact `h0(k),conjugate(h0(-k))` packing remain. Default clean normalization preserves seeded height RMS 1.50 / 0.75 / 0.18 game units. `ZH_WATER_CLEAN_SPLIT=spectrum` instead applies one global normalization to total RMS **1.68668314**, letting the spectrum allocate the bands:

| Resolution | Long RMS | Medium RMS | Fine RMS |
| --- | ---: | ---: | ---: |
| 256 | 1.51724965 | 0.724174552 | 0.135737632 |
| 128 | 1.50751665 | 0.743552171 | 0.139369003 |

These are seeded/ensemble RMS budgets. Full directional Tessendorf synthesis has both propagation branches; instantaneous spatial RMS can fluctuate with their interference. CLEAN ignores `ZH_WATER_FFT_DIRECTION` for its JONSWAP spectrum; Hasselmann plus swell controls direction instead. Retained fallback Phillips paths still honor that older override. Invalid tuples fall back atomically to defaults; accepted ranges are U=1..50, fetch=100..1,000,000, swell=0..2. Settings are read at map load, shader generation or spectrum initialization and then cached, with no per-frame environment reads for the new switches.

**One liquid surface.** Main-level vertex displacement and pixel tangents sample all three FFT cascades at unrotated world coordinates: tile sizes 9000 / 2060 / 260 game units. The existing global spectral wind axis is retained. The native draw now requests `AllCascades`, including cascade zero. The sixth atlas word supplies the same D and bilinear gradient to both stages; each band receives `smoothstep(0,24/12/4,D)`, including the displacement-times-gain-gradient product in tangents. Direct/cached CPU terrain damping and SafeDamping's separate floor ramp are bypassed there. Coastal spectral-support and horizontal offshore gains are also bypassed. D is applied once.

On that elevation CLEAN bypasses analytic swell, sector rotation/cross-fades, local arrivals/residual envelopes/current advection, shared sine profiles, near-shore analytic remainder and offshore wave-model selection. Painted `mediumBreakFoam` is zero. Vessels/wakes, hull reflection/damping, physical cage/floor constraints, seabed optics, scene reflection and refraction remain; the existing shared-level forward-plane refraction fix is reused. Other elevations retain the old evaluator; explicit MEDIUM=wind retains its behavior there. Geometry boundary collars, obstacle/hull suppression and cages remain independent physical/mesh constraints, not extra shallow-water response curves. CLEAN requires the native Modern FFT path; CPU-only fallback remains the existing water model.

**Top-down shading.** The CPU integrates each seeded cascade's slope power removed by each box mip into the existing generated `fftLostSlopeVariance` constants. No new uniform block, texture or binding is needed. Full-mip fixed-split slope variances at 256 are 0.000895645216 / 0.00249621477 / 0.00714611878. CLEAN interpolates the lost power at each sampled LOD, multiplies by the corresponding depth gain squared, and retains existing limiter/mesh/hull factors. The isotropic GGX transfer is `roughness^4 += 0.5*glint*unresolvedVariance`, capped at one, without a footprint cutoff. Clean base roughness removes the old offshore-dependent base term. This is a spectrum-derived variance approximation, not a LEAN covariance texture or anisotropic solver.

Crest subsurface colour uses the filtered FFT height divided by the local root-sum-square band RMS, a smooth positive-crest response, sun-behind-face and view-angle terms. It gently raises the scattering source toward green, before optical body composition. It has no independent temporal noise; filtered height prevents small unresolved crests from flickering.

Instantaneous whitecaps use the incident FFT horizontal Jacobian after depth products and the displacement limiter. With uniform gain it is `(1+dDx/dx)*(1+dDy/dy)-(dDx/dy)^2`; with a varying gain, mixed derivatives differ, so the implementation correctly uses their product. A smooth threshold with derivative-based pixel antialiasing feeds the **existing** crest-foam path. Hull/obstacle/cage masks remain. No new foam history texture is added; existing vessel history remains.

`ZH_WATER_CLEAN_LOOK="sss,foamThreshold,glint"` defaults to `0.16,0.72,1`. SSS/glint are gains; zero foam threshold disables clean whitecaps. Valid ranges are 0..1 / 0..1 / 0..4; invalid tuples use defaults. Debug views: `clean-bands` = band gain × seeded band RMS in RGB; `clean-jacobian` = clamped J grey, red proportional to whitecap coverage; `clean-roughness` = final GGX roughness grey. They are black outside clean's active level.

All existing text-replacement anchors remain available when ALIVE/FACET/REFRACT_FORWARD/GLITTER/FOAM_TEX run. Clean substitutions run afterward. FOAM_TEX remains syntactically usable but its painted-breaking input is deliberately zero on the clean level. No anchor was removed from another experiment's path.

**Cost and limits.** Vertex incident sampling is three FFT reads plus four D reads. Pixel incident sampling is six FFT reads plus four D reads, with up to six additional reads for unresolved cascades; paired evaluation reuses already-resolved samples. This removes sector/local/analytic evaluation, but computing the previously omitted cascade zero adds about 50% to the headed FFT field-output work/bandwidth. Net GPU time is unmeasured. The atlas has six words/cell, 20% above default and equal to the wind experiment, with no shared amplitude rows and no new per-frame uploads. The spectrum/variance work happens on the CPU during initialization/shader generation. Waves do not turn toward beaches; fixed tile repetition, mesh interpolation/collars, instantaneous-foam popping and the isotropic variance approximation remain possible visual limits. Visual tuning and motion continuity need a GPU A/B run.

**Validation.** Required `cmake --build build-linux --target generals -- -k 0`: passed; `test_shader_sdl` and `test_water_grid` builds passed. Existing compiler warnings remain.

- Full water test, clean unset and `=1`: each **238 tests / 13,140,462 checks**, zero failures. New CPU coverage tests deterministic generation, exact Hermitian packing and evolved reality, both resolutions/splits, fixed band RMS and spectrum total, angular normalization, swell narrowing, JONSWAP peak/fetch response, slope-table monotonicity and invalid tuple fallback. Standalone ASan/UBSan: **206 checks**, passed (`detect_leaks=0` for the sandbox).
- Full shader suite: default **57 / 693,454**, wind **57 / 693,467**, shared **57 / 693,531**, clean **57 / 693,493**, clean+spectrum split **57 / 693,490**; all passed. Includes vertex/pixel compilation, SPIR-V validation, MSL translation and slot checks. Registered `test_shader_sdl_water_clean` also passed with CLEAN=1 and SHARED=1, explicitly testing precedence in a fresh process.
- Combined CLEAN+SHARED+ALIVE+FACET+REFRACT_FORWARD=0+GLITTER+FOAM_TEX view smoke: **288 checks**, passed, no missing-anchor warnings on the main native material.
- Pre/post sampling hashes match exactly: default `6ffc0ab0ecd4ecb7`, wind `68746f3e518fd87b`, shared `692f0c5c6af7c3d1`. Original Phillips coefficient hash remains `b37ae9a8e860392b`. Complete material-source comparisons against the saved pre-edit generator also match: default `0e5f73a7ce77d95d`, wind `f21adde495fb2b9f`, shared `76d0bfa2d64ec017`; combined older switches also match. Frozen unheaded/Direct3D checks pass with CLEAN enabled.
- `git diff --check`: passed. Scratch user-data/TMPDIR folders were used for direct test runs. Metal acceptance/pipeline linking and the reflection-allocation GPU test were skipped because this sandbox has no GPU/video device. No visual recordings or GPU timings were attempted.

An identical output copy is at `/tmp/modern-water-clean-fft-report.md`; detailed build/test logs are under `/tmp/water-clean-tests/` and `/tmp/water-clean-*-build*.log`.

## Live tuning

Added 2026-11-10 in the same worktree and branch. Enable both `ZH_WATER_CLEAN=1`
and `ZH_WATER_TUNING=1`; start `python3 Tools/water-tuner/water_tuner.py` and open
<http://127.0.0.1:8765>. See [the tuner README](../Tools/water-tuner/README.md) for
windowed launch commands and explicit file paths. No packages were installed.

`WaterTuning.h` holds the shared C++ definitions, bounded parser, previous-value
patch semantics and watcher. `W3DDisplay::draw` checks before render passes, at most
once per 500 ms using a monotonic clock. The watcher reads `ZH_WATER_TUNING_FILE`,
or the game's user-data `water-tuning.ini`; exclusive startup creation preserves
existing files and seeds new ones from the existing spectrum/look/split/time
environment variables. Unknown/malformed/nonfinite lines log once per changed file;
finite numbers clamp to the requested ranges. Missing keys retain their previous
values. An unfinished final line, binary/oversized file, or file changing during
its read cannot inject incomplete values. Manual lines must end in a newline.
Each accepted change logs `water tuning: applied N values`.

| Parameters | Live application |
| --- | --- |
| `wind_speed`, `fetch`, `swell`, `wind_angle` | Regenerate seeded JONSWAP coefficients, seeded RMS and lost-slope variance; re-upload initial FFT textures at a frame boundary. |
| `height_long`, `height_medium`, `height_fine`, `split` | Same spectrum update. Fixed split sets individual RMS; spectrum split allocates their combined RMS budget across bands. |
| `chop_long`, `chop_short` | Existing constant block, matching vertex displacement and pixel derivatives/Jacobian. |
| `depth_long`, `depth_medium`, `depth_fine` | Existing constant block, matching vertex/pixel depth gain and gradient; also updates crest normalization and diagnostics. |
| `crest_color`, `foam_threshold`, `glint` | Existing constant block, including zero-threshold whitecap disable. |
| `wave_speed` | CPU FFT phase clock; preserves phase continuity at speed changes. |

The tuning-only uniform tail adds 13 float4 registers, including the variance table;
legacy block layouts/uploads remain intact when tuning is off. Spectrum updates
reuse all render pipelines and field storage, cycle seed texture storage for
in-flight readers, and retry a failed upload on the next frame. No slider edit
recompiles shaders. At 256, an isolated optimized CPU sample measured ~35 ms per
seed generation and ~21 ms per variance integration; the current update regenerates
the seed three times, so expect roughly 0.1–0.2 seconds of CPU hitch plus upload/
submission cost. GPU time and actual in-game latency remain unmeasured. Other
controls only refresh constants or the clock. Unchanged frames reuse cached data;
file stat runs at 2 Hz, with no shader/spectrum rebuilds. With tuning disabled there
is no watcher, file I/O or clock polling.

The Python stdlib server serves inline HTML/CSS/JS and a JSON API on loopback only.
The compact page has all 17 controls, hints, four group resets, global reset,
200 ms drag throttling, named JSON presets and a copyable environment line. INI
and preset writes use a same-directory temporary file, flush/fsync and atomic
rename. The new tuning-only start aliases (`ZH_WATER_CLEAN_WIND_ANGLE`, `HEIGHTS`,
`CHOP`, `DEPTHS`, each with the same prefix) round-trip the copied settings when
starting with a fresh file. Existing files override environment start values.

Incremental files: new `Platform/WaterTuning.h`, `Tests/WaterTuningTest.h` and the
three `Tools/water-tuner/` files; updated `WaterCleanSpectrum.h`, `WaterCleanShader.h`,
`WaterFftSpectrum.h`, `WaterFftFiltering.h`, `WaterFftSampling.h`, `engineshader.cpp`,
`W3DDisplay.cpp`, `PosixDevice9Draw.cpp`, `SdlConstants.h`, `SdlGpuFrame.{h,cpp}`,
`test_water_grid.cpp`, `test_shader_sdl.cpp`, `CMakeLists.txt` and this report.

Validation:

- Required `cmake --build build-linux --target generals -- -k 0`: passed. Water,
  shader and dump targets also built. Existing compiler warnings remain.
- Full water suite, default and CLEAN+TUNING: **240 tests / 13,140,556 checks each,
  zero failures**. Includes parser edge cases and deterministic live-file tests
  for startup creation, the 500 ms limit, partial writes and rename detection.
- Full shader suite with `spirv-val`: default **58 / 693,454**, CLEAN **58 / 693,493**,
  CLEAN+TUNING **58 / 694,048**, zero failures. Non-default settings compile both
  stages; both split modes/resolutions, uniform packing (including hull metadata),
  variance invalidation, source stability and speed continuity pass. Dedicated
  CTest entry: `test_shader_sdl_water_tuning`.
- Before/after sampling-source hashes match: default `6ffc0ab0ecd4ecb7`, wind
  `68746f3e518fd87b`, shared `692f0c5c6af7c3d1`, CLEAN without tuning
  `a01824d29b9d341d`. TUNING without CLEAN also matches default. Frozen Direct3D
  material hashes pass even with tuning enabled.
- Python: **4 tests passed, 1 live-server test skipped** because the sandbox
  rejects loopback socket creation with EPERM. Real HTTP-handler parsing, GET/POST,
  all parameter ranges, invalid payload rejection, atomic replacement/failure,
  partial files and preset/env round trips passed without sockets. The same script
  runs the live GET/POST check on an unrestricted machine; its CTest entry passed.
  Inline JavaScript syntax and a full copied-line round trip into C++ passed.
- `git diff --check`: passed. Test binaries used scratch `ZH_USER_DATA_DIR` and
  separate TMPDIRs. Metal/GPU execution, visual layout inspection and in-game
  motion/latency checks were unavailable; no GPU failure was chased.

No commits, branch changes, main-checkout edits, or changes under `build-linux/ab/`.
Concise output: `/tmp/modern-water-live-tuning-report.md`; test/build logs:
`/tmp/water-tuning-tests/`. The full report copy remains at
`/tmp/modern-water-clean-fft-report.md`.

## Mixed seas

Added 2026-11-10. The tuner now exposes 27 keys. `sea2_share` (default 0,
range 0..0.9), `sea2_angle` (135°, -180..180), `sea2_wavelength` (600 game
units, 100..1500), and `sea2_spread` (12, 1..40) add a secondary directional
swell to the long and medium cascades. Its wavelength density is
`exp(-0.5*(log(wavelength/peak)/0.35)^2)` times the normalized half-angle
`cos^(2s)` directional density. Both components are normalized on the actual
seeded lattice before mixing power as `(1-share)*primary + share*secondary`;
share is therefore an energy fraction in each affected band. One Gaussian
realization, three existing cascades, and the original dispersion remain.
The fine cascade keeps the primary sea. Final fixed/spectrum RMS normalization
retains the existing height controls. No extra FFT or texture binding is added.

`spread` (0..1, default 0) mixes the primary angular density with isotropic
`1/(2*pi)`. `ZH_WATER_CLEAN_SEA2="share,angle,wavelength,spread"` and
`ZH_WATER_CLEAN_SPREAD=value` supply cached startup values; the copied tuner
line includes these and all shore controls. Spectrum-affecting changes rebuild
the seeded spectrum/RMS/variance, while shore controls update constants only.

`sea2_angle` denotes absolute world-space travel direction. The original
`wind_angle` convention is preserved for baseline compatibility: with the
existing positive time phase and positive-sign inverse FFT, its main lobe
travels at `wind_angle + 180°`. Secondary construction accounts for this sign;
shore turning uses the actual main travel vector. Main wind behavior with all
new defaults remains byte-for-byte unchanged.

## Shore turning

Long-reach revision, 2026-11-10. `shore_turn` defaults to 0 (0..1),
`shore_reach` to **800 game units** (100..3000), `shore_tile` to **300**
(100..3000), and `shore_sharp` to 3 (1..8). `shore_bands` defaults to `all`
(long + medium + fine); old `medium` and `long_medium` values still load. The copy/startup aliases are
`ZH_WATER_CLEAN_SHORE="turn,reach,tile,sharp"` and
`ZH_WATER_CLEAN_SHORE_BANDS=medium|long_medium|all`. Old `shore_depth` INI lines
are ignored with an informational message, including malformed legacy values;
they do not change reach or generate a parse error. The tuner omits that key.

When shore is compiled in (live tuning or nonzero startup turn, unless
`noshore`/`nodepth`), map-load preparation builds a separate distance field from
the main water level's wet mask. Dry cells start at zero, wet cells at 3000;
forward/backward eight-neighbour chamfer sweeps use world spacing for cardinal
steps and sqrt(2) times spacing for diagonals. Distance is capped at **3000**,
independent of effective D, terrain depth and the optical 100-unit shore byte.
A land-free map stays at 3000; map boundaries do not invent land.

Direction blends `-grad(smoothedDistance)` at **60, 180, 500, 1000 world-unit
support radii**, targeting `0.75 * distanceToLand`, clamped to those supports.
Each scale starts independently from the original distance field and uses three
separable box passes per axis (`round(radius/(3*spacing))`, at least one cell).
Prefix sums keep construction O(cells) regardless of radius; borders extend the
endpoint value. Wet **and dry** samples participate. Adjacent scales blend
unnormalized centered-difference gradients with smoothstep weights, so both
weight and derivative are continuous at scale boundaries. Normalization follows
blending; `confidence = saturate(length(gradient))` (zero on dry cells).
Opposing banks cancel before normalization at medial axes. A nearby bank uses
its local normal while distant water retains broad direction. Reach edits still
update constants without rebuilding the cached field. This increases map-load
CPU filtering to four scales, without additional GPU reads, packing or bindings.

Clean map preparation also estimates a prevailing travel direction from the
packed shore field, using `distance * confidence` weights over wet cells. Strength
is `|sum(weight * direction)| / sum(weight * |direction|)`; below **0.2** it falls
back to the configured angle. `travel_mode=auto|manual` defaults to **auto**,
including old files/presets without the key; manual uses `travel_angle`.
The effective direction controls spectrum heading and shore turning, and changes
use the existing spectrum revision/upload path even without live tuning. It only
applies in clean mode. The nonlive shore shader carries its heading offset in
the existing `FftParameters.w`; live tuning uses its existing heading constants.
The tuner displays a read-only effective angle and an “auto” compass label; the
engine atomically writes map metadata to the tuning INI's `.auto` sidecar so the
manual/fallback angle remains intact. Auto angle, strength, selection and mode
are logged once per map preparation. `shore_max_turn` remains **60°**.

The sixth RGBA8 atlas word is full: **R/B = signed normalized XY**,
**G = original D/40**, **A = 128 + round(127*confidence)**. Its upper alpha bit
retains main-level validity (`step(0.5,A)`). Therefore shore compilation adds a
**seventh word**, **R = round(255*distance/3000)** (other bytes zero), using the
same texture and sampler. Distance resolution is 11.765 units, maximum rounding
error 5.883 units. Inactive levels carry invalid D and distance 3000. Heading and
D lookups use stride seven but D stays at word index five. With shore absent,
the six-word layout, original bytes, shader text and bindings remain unchanged;
default/wind/shared/clean-with-defaults hashes are verified separately below.

Both shader stages keep the existing world-anchored triangular lattice with
hexagonal vertex neighbourhoods. Each vertex bilinearly samples distance and
confidence-weighted direction vectors, then normalizes direction and reduces
confidence by any interpolation cancellation. Its rotation is

`angle_i = shortestSignedDelta(actualMainTravel, shoreward_i) * shore_turn * smoothstep(0.02,0.15,confidence_i) * (1-smoothstep(0,shore_reach,distance_i))`.

Confidence only gates near-zero directions; it no longer scales the full turn above 0.15.

`actualMainTravel` is the negative legacy spectral heading. Cross/dot `atan2`
selects the shortest signed turn; exactly antiparallel vectors (including signed
zero cross products) consistently choose **+pi, counterclockwise**. Rotation
never exceeds the angle to shore and monotonically improves shore alignment.
With partial strength/confidence/distance weight, initially offshore waves may
remain offshore until sufficient rotation accumulates; that follows the specified
formula. Exactly 180 degrees cannot have a unique continuous shortest-turn sense,
and nearby opposite sides of the branch cut can still choose different senses.
`VertexAngle` and `SampleField` provide CPU references, including transport
quantization, of the per-vertex shader calculation.

Each copy still samples `q_i = vertex_i + R(-angle_i)*(p-vertex_i)` and rotates
horizontal displacement back by `R(angle_i)`. Its Jacobian is
`R(angle_i)*J_i*R(-angle_i)`; vertical slope uses the same coordinate chain.
For barycentric b, use `a_i=b_i^sharp`, `w_i=a_i/sqrt(sum(a_j^2))`, including
`dw_i=(da_i-w_i*dot(w,da))/sqrt(sum(a_j^2))` and `displacement_i*dw_i` in pixel
derivatives. There are no decorrelating offsets. Pixel-local D and its gradient
still control amplitude, independently of shore distance; selected bands keep the
same physical mip-zero, shading LOD and variance construction. With `all`, the
13-unit fine cascade also rotates vertex displacement, pixel normals and glints;
the other selections retain its global coordinates. The hex blending and exact-zero-angle global-sample fallback
are unchanged. Correlation between rotated copies can increase RMS; no covariance
correction or GPU visual-continuity proof is claimed.

`clean-shore` now uses applied travel direction as hue and
`saturate(abs(blendedAngle)/(pi/4))` as brightness: a 45-degree turn is fully
bright. Faint lattice edges remain. The diagnostic is written **after** the
ordinary lighting/display mapping, fog and shroud pipeline so it is readable;
shore off/zero turn remains black. `ZH_WATER_SHORE_STATS` retains confidence
buckets `<16,16..47,48..95,>=96` and now reports raw distance buckets
`<100,100..399,400..999,>=1000` game units over wet cells.

Cost: shore-active atlas storage grows from six to seven words/cell (+16.7%,
one extra RGBA8 texel/cell including padded edges), only at map-load upload.
No sampler, constant register or per-frame upload is added. Nonzero turn setup
now reads **24 atlas texels** (three vertices, four corners, direction/D plus
distance), in addition to four pixel-local D reads. Excluding vessel/reflection
work: disabled turning remains vertex 3 / pixel 6 FFT reads; active medium
turning is vertex 5 / pixel 10, long+medium vertex 7 / pixel 14, all vertex 9 /
pixel 18 (**+6 vertex / +12 pixel** over disabled turning, or +2 / +4 over
long+medium). Per pixel incident
evaluation this is 10 total reads with shore off, 34 with nonzero turn outside
reach, 38 for active medium, 42 for active long+medium and 46 for all; worst paired
physical/shading evaluation reaches 48 / 56 / 64 with shared tile setup (20 / 28 /
36 FFT reads). Debug repeats
tile setup. No GPU timing is inferred.

Validation for this revision is recorded in the output report
`/tmp/modern-water-long-reach-shore-report.md`, with logs under
`/tmp/water-shore-validation/`. CPU cases cover parallel/offshore wind, exact
180 degrees, bounded monotone turns, beyond-reach identity, open water, medial
cancellation, both sides of a 1000-unit channel/bay at reach 800+, D independence,
packing, and live/legacy parsing. The exact centre intentionally has near-zero
turn even when both halves of the bay are within reach.

Final results: required `cmake --build build-linux --target generals -- -k 0`
and the shader/water/GPU-test targets built successfully. Full CPU water suites
passed in default and CLEAN+TUNING modes (**240 tests / 19,473,591 checks each**).
All seven full shader runs passed (default, wind, shared, clean, tuning, active
medium shore and active long+medium shore), including SPIR-V validation and MSL
translation. All ten shore/profile CTest cases passed. Focused CPU/tuning tests
also passed ASan+UBSan in default and tuning modes. Python: **5 passed, 1 loopback
socket test skipped** by the sandbox. Sampling-source hashes and all **71 SDL
material dump files per protected mode** are byte-identical to this revision's
pre-edit snapshots: default `dfad943f831c4131`, wind `7b15c3b2145f512d`, shared
`b0d13c24f75feb1b`, clean with defaults `554751362b106ecf` (sampling hash harness
covers both stages, headings, array options and resolutions). `git diff --check`
passed. The GPU test returned **77 / SKIP: SDL_Init: No available video device**;
Metal acceptance/linking and USA 2/SWANGLE visual checks remain unverified.

Fine-ripple revision, 2026-11-10: `all` is now the engine/tuner default and turns
all three bands in both stages, including shading LOD and lost variance. Required
build, six full shader suites, 27 shore/profile cases, both CPU suites and tuner
checks passed; GPU execution skipped with no video device. Details and read costs:
`/tmp/modern-water-fine-shore-report.md`.

## Profiling switches

`ZH_WATER_CLEAN_PROFILE` is a cached comma-separated list, logged once when
nonempty. Whitespace is accepted and duplicate active names are logged once.
An empty list leaves the prior source unchanged. All switches affect clean
water only:

| Flag | Removed work / behavior |
| --- | --- |
| `nolong` | Removes cascade-zero texture reads in both stages and requests `HeadedCascades`; long RMS and lost variance are omitted from clean shading. Coastal/full-field retry paths cannot request cascade zero. |
| `nomips` | Fixes clean shading LOD to zero and removes lost-slope-variance transfer. |
| `nodepth` | Replaces D sampling with unit gains/zero gradients and compiles shore out. Main-level validity is carried in existing heading-angle metadata (+16 sentinel), decoded without an atlas read; all old rotation uses subtract the sentinel. Other levels retain fallback scope. |
| `nofoam` | Omits clean Jacobian whitecap evaluation and suppresses its main-level foam result. Vessel foam remains. |
| `nosss` | Omits the clean crest-color contribution. |
| `noshore` | Compiles the shore evaluator and metadata reads out. |
| `flatvertex` | Returns zero main-clean incident vertex displacement while preserving the pixel evaluator. |

These are diagnostic ablations, not recommended visual presets or performance
claims. `nolong` also leaves coastal incident sampling unavailable when that
consumer requires the broad field; it does not silently regenerate cascade zero.
Fresh-process CTest entries cover each flag and their combined list for all three
shore band selections. Focused shader tests compile native array and independent-map
sampling, both stages, SPIR-V validation when available, MSL translation and
sampler contracts. CPU tests cover straight beaches, inward island normals,
long-reach bay/channel turning and medial cancellation, transport packing, normalized weight derivatives at sharpness 1/2/8, and a
finite-difference check of the full rotated vector/weight/depth-gain chain.

Historical validation for the earlier direction/shore/profile increment (before the long-reach revision):

- Full water suites passed after rebuilding the final CPU header: default **240 tests / 19,039,020 checks**, CLEAN+TUNING **240 / 19,039,023**, zero failures.

- Required `CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-linux --target generals -- -k 0`: passed. Shader, water-test and dump targets also built. Existing warnings remain; nothing was installed.
- Full shader suites passed: default **59 tests / 693,464 checks**, wind **59 / 693,480**, shared **59 / 693,548**, clean **59 / 693,581**, tuning **59 / 694,206**, clean+spectrum **59 / 693,578**. Tests include SPIR-V validation, MSL translation, both stage contracts and optional tuning-tail reflection.
- Fresh-process targeted shore tests passed for both band choices (**70 checks each**). Profile checks passed: nolong **90**, nodepth **74**, nomips/nofoam/nosss/noshore **72 each**, flatvertex **74**, combined **104**, combined+tuning **104**. These include both array and independent-map pixel sampling, vertex compilation, diagnostics, MSL/slot checks and validator runs. Each active profile list logged once.
- Focused current-header shore tests passed normally and under ASan+UBSan in default and CLEAN+TUNING modes (`detect_leaks=0`). Separate spectrum/tuner sanitizers passed. New checks cover the nondefault map-load wind fallback and nonfinite-spacing rejection as well as the field/derivative cases above.
- All eight original seeded spectrum hashes match at 128/256 and both split modes. Sampling-source hashes remain default `6ffc0ab0ecd4ecb7`, wind `68746f3e518fd87b`, shared `692f0c5c6af7c3d1`, untuned clean `a01824d29b9d341d`; TUNING without CLEAN matches default. Complete before/after material dumps are byte-identical for all **184 files per default/wind/shared/clean target**, with SHA256 evidence in `material-baseline-final.log`.
- All 27 copied settings, including nondefault values for the ten new keys, round-trip into C++. Python: **5 passed / 1 live-socket test skipped** because the sandbox rejects loopback creation. Browser layout was checked structurally at 420 px; CUA was unavailable and headless launch was blocked. Metal acceptance/pipeline linking, GPU scene execution, motion recordings and GPU timings remain unverified.
- `git diff --check`: passed. Logs: `/tmp/water-direction-tests/`; concise report: `/tmp/modern-water-direction-profile-report.md`. Existing dirty work was preserved. No commits, branch/worktree changes, main-checkout edits, installs, or writes under `build-linux/ab/` or `play-tune` were made.
2026-11-10: Approved tuning is now the clean-mode default for all 30 controls, including spectrum split, second-sea share .33, chop 1.83/1.92, shore turn 1/reach 1500 and heading follow 1; fresh-process tests prove exact 128/256 coefficient and baked/live shader-constant equality with the approved file (fallback and two map headings), generals and 45 suite runs pass, GPU skips for no video device, and default/wind/shared hashes plus all 184 material files per mode are unchanged. Report: `/tmp/modern-water-clean-defaults-report.md`; evidence: `/tmp/water-defaults-validation/`.

## Precomputation

2026-11-10: clean water now caches the quantized map depth and hex-lattice
rotations on the CPU. The depth image is padded like the existing heading atlas;
RGBA32F nodes contain `(D, d10-d00, d01-d00, d11-d10-d01+d00)`. One hardware-linear
R sample reconstructs D and one exact-centre GBA sample reconstructs its exact
bilinear gradient. The source values still pass through the existing eight-bit
D encoding before entering the float image. RGBA32F avoids another depth or
angle quantization step. Gradients use each cell's polynomial coefficients;
averaging nodal gradients would not reproduce the bilinear product rule.

Depth has the map's lifetime. Rotation invalidation keys are `shore_tile`,
`heading_follow`, `shore_turn`, `shore_reach`, `shore_max_turn`, effective travel
angle and its mode, heading generation, forced turning and live/baked mode.
`shore_sharp`, shore band selection, depth gains, chop and appearance controls do
not invalidate the angle image. CPU payload is `16 * side² + 16 * lattice_cells
+ 20 * raw_cells` bytes for the depth image, rotation image and retained packed
heading words, excluding container overhead and existing source fields. GPU
payload is `16 * side² + 16 * lattice_cells` bytes. The angle cache stores `(cos(angle), sin(angle), angle, valid)` for each hex vertex and
retains the existing confidence, shore-distance quantization, antiparallel turn
choice, turn limit, heading following and `ZH_WATER_SHORE_FORCE` diagnostic.

One explicit context holds D and the three selected vertex rotations for the
primary evaluation position. Material depth/gains, surface geometry, filtered
lighting and the shore diagnostic reuse it. Every reflected evaluation position
builds its own context. No global mutable shader context can accidentally reuse
the primary position's values. Rotation-cache bounds include the padded terrain
and a lattice halo; projected or reflected positions beyond it use the original
heading/shore evaluator without clamping the lattice. Missing cache textures use
a 1x1 sentinel; missing depth falls back to the original packed-D lookup, and
missing rotation falls back to the original vertex-angle evaluator.

Native array-material bindings are pixel slots 12/13 and vertex slots 5/6 for
depth/rotation, using linear/point samplers respectively. Independent-map shader
fixtures use pixel slots 5/6 and move heading to 14, avoiding FFT slots 8–13. No
uniform fields or existing constant-block tails were added. `nodepth` has no D or
rotation reads, while `noshore` retains D and omits rotation evaluation.

The paired surface preserves mip-zero physical samples for the exact Jacobian,
contact crest and foam. A band's zero-LOD shading reuses that band's physical
samples; a nonzero LOD still needs its separate filtered pair. The computation
order of the depth product rule, rotated derivatives and band accumulation is
retained. Replacing physical compression or crest with a filtered approximation
would change the approved appearance and was not done.

The following are source-level texture-instruction counts for a valid primary
clean position, all three shore bands active, without hull reflections or fallback.
They are not GPU timings or measured bandwidth; compiler dead-code elimination,
cache behavior and diagnostic/profile settings can change executed work.

| Evaluation | Before | After |
| --- | ---: | ---: |
| Pixel metadata (material D, surface D, three vertex angles) | 56 | 5 (2 D + 3 rotation) |
| Pixel physical FFT | 18 | 18 |
| Pixel physical + shading FFT, at least one filtered band | 36 | 18 + 6 per filtered band |
| Pixel metadata + paired FFT, only fine filtered | 92 | 29 |
| Pixel metadata + paired FFT, all bands filtered | 92 | 41 |
| Incident vertex metadata + displacement | 61 | 13 after unused gradient removal |
| Wrapped vertex total, including main-level D lookup | 65 | 13 after unused gradient removal |

The old vertex wrapper also read D separately for main-level detection (four
additional reads); the prepared vertex context removes that duplicate. Compiler
common-subexpression elimination may already remove the old repeated lookup;
vertex totals assume removal of the unused gradient coefficient read. With no
filtered band, the primary pixel total is 74 → 23 reads. Medium-only and
long+medium rotation selections have fewer FFT reads; out-of-cache or allocation
fallback retains the original evaluator's costs. Each reflected position incurs
its own corresponding evaluation.

Shader checks cover array and independent maps, both stages, sampler binding
uniqueness and the 16-slot SDL limit, unchanged constant-block offsets/tails,
clean height/bands/Jacobian/roughness/shore diagnostics, profiles and forced shore
turning. CPU tests compare the packed reference against cached D/gradients and
rotations, padding, lattice bounds and cache invalidation. The required
`generals` build passed. CPU cache tests passed ASan+UBSan in default and live
modes; the tuner checked all 30 controls and its Python suite ran 11 tests with
2 skips (Node unavailable and sandbox loopback-socket restrictions). Protected
sampling hashes remain default `6ffc0ab0ecd4ecb7`, wind
`68746f3e518fd87b` and shared `692f0c5c6af7c3d1`; all 184 material dump files per
mode are unchanged. Evidence is under `/tmp/water-precomputation-validation/`.
Final integration finished with **46 passing jobs, zero failures and one GPU
skip**, plus six passing supplemental heading-binding runs with real SPIR-V
validation and MSL translation. Full CPU water runs passed: default/clean
**241 tests / 19,541,707 checks** each, live tuning **241 / 19,541,708**. Full
shader suites passed in clean (**59 / 695,135**) and tuning (**59 / 695,837**)
modes before the final test-only binding/validator strengthening; the six
supplemental processes passed those final assertions. Final artifact hashes and
detailed evidence are in
`/tmp/modern-water-precomputation-report.md`. GPU execution returned skip code
77 because SDL reported no available video device. MSL translation passed;
Metal driver acceptance and pipeline linking remain unavailable. GPU luma
comparisons, visual acceptance and ABBA timing runs remain pending; no GPU image
equality or frame-time improvement is claimed.

## Optimization pass 2

2026-11-11. This pass addresses the legacy material code retained behind the
per-pixel clean-validity branch, coherent tile sampling, and tiny shading LODs.
The input GPU measurements were Claude's synced ABBA runs: the precomputation
build took 9.92 ms on SWANGLE and 19.45 ms on USA 2; USA 2's profile run took
19.48 ms, with `nodepth` saving 7.45 ms and `noshore` saving 2.44 ms. These are
measurements of the preceding build. This pass has no GPU timing result.

**Variants and selection.** The original SDL material and FFT vertex variants
remain available with `waterCleanMain=false` (the API default). This covers
default/wind/shared modes, rivers, other water elevations, and draws without a
prepared FFT. Their generated shader text is unchanged. Linux main clean water
also has an explicit `waterCleanMain=true` material and matching vertex program.
`setupFlatWaterShader` marks the draw when clean mode is enabled, it is not a
river, and `abs(opticalLevel-mediumLevel)<0.01`. This flag is independent of
successful optional cache uploads. `Gpu_Draw` selects the specialised pixel
program only for an accepted FFT trapezoid draw outside the sun pass; prepared
FFT scopes snapshot and check the same main-level flag for their vertex program.

The pixel cache includes the flag in its byte lookup and adds `:clean-main` to
its key. The vertex cache uses separate main-clean lookup tables and a
`water-clean-main:` prefix for both array and independent-map representations.
Existing resolution, refraction, reflection, material-profile, fog/alpha and
open-sea distinctions remain. Main-clean open-sea and ordinary clean sampling
emit the same incident evaluator because neither uses local arrivals.

The specialised source omits analytic surface/swell function bodies, sector and
medium legacy chop, local arrivals, residual analytic waves, legacy shore waves
and their breaking foam. Material clean activation is constant; **there is no
`mediumDepthField.w`, `cleanDepth.w`, or `context.depth.w` read** in either
specialised stage. Clean gains still depend on D, including its exact gradient.
Missing/out-of-depth-cache positions use unit gains and zero depth gradients;
missing/out-of-rotation-cache vertices use identity rotations. Neither case
selects a legacy wave system. Caustics now use the existing physical clean slopes
rather than evaluating an analytic wave at the bed. The legacy sector and shore
source diagnostics show zero in this variant; heading/phase, clean diagnostics,
vessel/cage foam and optional GPU coastal presentation remain available.

**Tile collapse.** `WaterCleanCollapse::AngleDegrees` is **1.5 degrees**, using a
strict `<` test on the complete span of the three periodically unwrapped angles.
For coherent nonzero tiles, set `S=sum(w)`, `u=w/S`, mean angle `a=sum(u*angle)`
and mean pivot `v=sum(u*vertex)`. Each turned band samples once at
`q=v+R(-a)*(p-v)` and returns `S*R(a)*d(q)`. Both stages use that same surface.
Pixel derivatives include the quotient-rule derivatives of a and v, the
coordinate Jacobian, output-vector rotation, S, and the existing D gain product
rule. Angle unwrapping uses floor arithmetic; only the mean rotation needs a
sine/cosine evaluation. Noncoherent tiles retain the original three-copy blend;
exactly inactive rotations retain the original global-sampling identity path.

Equal input angles produce exactly that mean angle and zero angle gradient in
the CPU reference. This does **not** imply equality to the old three-copy FFT
waveform: equal nonzero rotations around different vertex pivots sample different
phases. The mean-pivot collapse is an approximation, preserves the blend's
amplitude weight, and needs GPU visual review, particularly where adjacent tiles
cross the collapse threshold. CPU finite differences verify the complete
mean-pivot/rotation/weight/depth derivative chain.

**Shading LOD.** `WaterCleanCollapse::ShadingLod` is **0.25**. Each band below it
uses LOD zero for shading and lost-variance accounting, reusing the physical pair.
LOD exactly 0.25 or above retains the separate filtered pair. Physical geometry,
contact crest, Jacobian and foam continue to use mip-zero samples; the existing
filtered crest-colour shading remains in place. All seven profile switches,
all shore band selections, live tuning, `ZH_WATER_SHORE_FORCE`, texture formats,
bindings and precomputation invalidation rules are retained. No new flag or
player control was added.

Expected source-level reads per primary pixel in coherent, actively turned open
water, with all three bands selected, excluding vessels/reflections and other
material textures:

| Work | Previous pass | Pass 2 |
| --- | ---: | ---: |
| D/rotation metadata | 5 | 5 |
| Physical FFT | 18 | 6 |
| Additional shading FFT per filtered band | 6 | 2 |
| Metadata + FFT, no filtered bands | 23 | 11 |
| Metadata + FFT, only fine filtered | 29 | 13 |
| Metadata + FFT, all bands filtered | 41 | 17 |

A filtered band now means LOD >=0.25. The analogous incident vertex total is
13 -> 7, assuming the unused depth-gradient lookup is eliminated. Noncoherent
tiles retain previous FFT read counts. These counts describe source texture
instructions, not measured execution time, bandwidth or occupancy. In addition,
the specialised material no longer carries the legacy wave paths or their
per-pixel model-selection branches.

Validation results and build evidence are recorded in
`/tmp/modern-water-optimization-pass2-report.md` and
`/tmp/water-pass2-validation/`. The default/wind/shared sampling hashes remain
`6ffc0ab0ecd4ecb7` / `68746f3e518fd87b` / `692f0c5c6af7c3d1`; a new shader test
pins those hashes. All 71 SDL material dump files per protected mode, including
the unspecialised clean fallback, match this pass's pre-edit snapshots.

Final validation: **46 passing jobs, zero failures, one GPU skip**. Full shader
suites pass in default/wind/shared/clean/live modes; clean has 60 tests and
701,902 checks, live tuning 702,639. Coverage includes specialised source
exclusion, all public material tiers, both stages/map representations, bow
variants, all shore/profile combinations, diagnostics, SPIR-V validation and MSL
translation. Full water suites pass in default/clean (242 tests / 19,541,762
checks) and live mode (19,541,763); collapse checks include threshold/wrapping,
equal-angle mean exactness and finite-difference derivatives. The required
`generals` build and `git diff --check` pass. The direct tuner command passes
11 tests with 2 environmental skips (Node unavailable and sandbox loopback
restrictions). Every test uses scratch `ZH_USER_DATA_DIR`. GPU execution skips
with code 77: **SDL_Init: No available video device**. Synced GPU timings, visual
acceptance and Metal driver/pipeline validation remain pending. No installs,
commits, branch changes, main-checkout or frozen-binary edits were made.

## Optimization pass 3

2026-11-11. Main-level clean water now has independent, cached startup options in
`ZH_WATER_CLEAN_OPT`. Unset selects **`stream`** (changed from `stream,fused,loop` after timing, 2026-11-11), with the existing
**`collapse=1.5`** degree threshold. The active, canonical list is printed once
when clean mode starts. An explicit comma-separated list **replaces** the default;
empty or `none` restores the pass-2 evaluator. Unknown tokens and malformed,
nonfinite or out-of-range collapse values are ignored. Other water variants keep
their existing generated shader text.

| Token | Default | Change and expected effect |
| --- | --- | --- |
| `stream` | on | Replace the main-clean tile arrays with a compact triangle/barycentric/normalisation context. Reconstruct one vertex, weight and weight gradient at a time; read its rotation's cos/sin from the angle cache and accumulate all selected bands directly. Share its rotated coordinate across bands in both stages. Reduces persistent tile state and repeated coordinate work. |
| `fused` | on | Fetch mip zero and any required shading LOD inside the same vertex walk, using the same coordinate and chain-rule frame. Reuse the physical contribution for LOD zero. Independently works with the pass-2 array context. Reduces repeat traversal and transformation work. |
| `loop` | on | Emit `[loop]` instead of `[unroll]` for the three-vertex walks. Gives the driver a smaller-code option; omit it to compare unrolling. |
| `finelite` | off | For turned fine-band pixel samples, retain height and rotated slope, omit horizontal displacement and its derivative/Jacobian terms. Vertex geometry stays unchanged. No sub-1% Jacobian/foam bound has been established, so visual review is required. |
| `half` | off | Use `min16float3` for tile-blend accumulators or decoded contributions; keep coordinates, normalisation, collapse, depth gains and final surfaces in fp32. SPIR-V validation checks `RelaxedPrecision`; native packed fp16 execution is driver-dependent. |
| `collapse=<deg>` | 1.5 | Set the existing strict full-angle-span collapse threshold, in degrees, range 0..180. Zero disables collapse; 3 and 5 are explicit visual experiments. |

Examples for timing: `ZH_WATER_CLEAN_OPT=stream` tests only streaming;
`ZH_WATER_CLEAN_OPT=stream,fused` compares unrolling against the default loop;
`ZH_WATER_CLEAN_OPT=stream,fused,loop,collapse=3` changes only the default's
collapse threshold. Restart between lists. The existing `ZH_WATER_CLEAN_PROFILE`
list remains independent.

The streamed context has **25 scalar components versus pass 2's 39**, before
compiler optimisation; this is not a hardware-register measurement. It carries
three barycentric values and the normalisation/gradient scalars instead of
retaining all three weight gradients. The power weights still use
`1/sqrt(sum(a*a))`, including their complete normalisation derivatives. The
collapse surface, its pivot/angle/weight derivative chain, and the 0.25 shading
LOD cutoff are retained. The complete depth-gain product rule applies to every
contribution. Accumulation order changes can introduce fp32 roundoff; the default
is expected to preserve the approved look, pending GPU frame comparison.

Preserving the existing collapse requires three angle checks before the sample
walk. A noncollapsed streamed tile rereads three tiny rotation-cache entries at
their points of use instead of retaining their vectors. Collapsed tiles keep
pass 2's read count. The sampling walk itself visits each vertex once with
`stream,fused`; no per-vertex `sincos` is used. The collapsed mean rotation and
existing heading debug colour retain their existing trigonometry. Streaming
alone uses a second walk only for filtered bands. This explicitly trades small
metadata/weight recomputation for shorter live ranges; GPU timing is needed to
judge the tradeoff.

All profile flags, debug views, tuner controls, shore band selections,
`ZH_WATER_SHORE_FORCE`, cache invalidation, bindings and texture formats remain.
Validation and the final compiler-size comparison are recorded in
`/tmp/modern-water-optimization-pass3-report.md`; detailed logs are in
`/tmp/water-pass3-validation/`.

Final compiler comparison for the standard specialised material: pass-2 baseline
**100,521 words / 23,180 SPIR-V instructions / ID bound 17,148**, default pass 3
**100,302 / 23,127 / 17,080**. Driver register allocation and GPU timing remain
unmeasured. The primary intended improvement is the shorter tile-state lifetime.

Final validation: **50 passing CTest entries, zero failures, one GPU skip**.
Default and clean CPU water suites each pass 243 tests / 19,661,768 checks,
including 4,000 randomized streamed/array blend comparisons with independently
reconstructed weight gradients, both triangle orientations, edge weights,
random angles/sharpness, depth-gain derivatives and physical/shading samples.
The shader matrix covers each token alone, baseline, two combinations,
shore/profile/force variants, debug views, both texture representations and
vertex stages through compilation, `spirv-val` and MSL translation. `half` also
checks SPIR-V relaxed-precision decorations. Full wind/shared shader suites and
final default/wind/shared hash probes pass, retaining
`6ffc0ab0ecd4ecb7` / `68746f3e518fd87b` / `692f0c5c6af7c3d1`.
The tuner Python tests pass (11 tests; two sandbox socket skips), the requested
`cmake --build build-linux --target generals -- -k 0` passes, and
`git diff --check` passes. GPU execution returns skip code 77 because SDL has
no available video device; RTX 4060 frame comparison/timing and Metal driver
validation remain pending. All 11,038 excluded files retain size/mtime, and the
three frozen executable SHA-256 hashes match the initial snapshot. No commits,
branches, installs or main-checkout edits were made.
