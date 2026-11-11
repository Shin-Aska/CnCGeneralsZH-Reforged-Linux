One-medium Modern water experiment — 2026-11-10

Worktree: `/mnt/sandisk-2tb/worktrees/water-tempo-ab`, branch `experiment/water-tempo-ab`. All behavior changes are gated on `ZH_WATER_MEDIUM=wind`. No commits, branches, installs, or main-checkout edits.

Files changed for this work:

- `GeneralsMD/Code/Libraries/Include/Platform/WaterMediumDepth.h` (new CPU field).
- `GeneralsMD/Code/GameEngineDevice/{Include/W3DDevice/GameClient/W3DWater.h,Source/W3DDevice/GameClient/Water/W3DWater.cpp}` (map snapshot, upload, caches, vertex damping/weight).
- `GeneralsMD/Code/Libraries/Include/Platform/{WaterFftSampling.h,WaterWaveHeading.h,WaterLocalWaveField.h}` (sampling, bands, travel speed, currents).
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/engineshader.cpp` (pixel damping, foam, debug views).
- `GeneralsMD/Code/Tests/{WaterMediumDepthTest.h,test_water_grid.cpp,test_shader_sdl.cpp}` and this report.

The pre-existing `W3DDisplay.cpp` and `WaterFftSpectrum.h` experiment edits were preserved without further changes. Existing tempo, spectrum, depth-fix and field-debug switches remain available.

1. **CPU D: done.** Start with `min(0.4*distanceToLand, 4*max(level-height,0))`, using the optical context's 0..100 distance bytes and scaled terrain heights. Apply a separable five-tap Gaussian `[1,4,6,4,1]`, radius two terrain cells, normalized over wet neighbors only. Dry cells stay zero and do not broaden the shallow halo. Clamp/quantize to 0..40 in 8 bits; retain the corresponding float values for matching bilinear CPU/GPU samples. Build alongside main-source optical selection, once per optical-map revision/prewarm; existing terrain, polygon, map and device invalidation clears it. The environment is snapshotted once for shader generation and once at map load, never in per-frame/vertex work.

   **Channel conflict and delivery:** `terrainHeight()` reads R, but pixel `Texture2` is replaced by the cage texture, and `WaterWaveCageShader.h` reads both G/B as cage radii; vertex `WaveCageTerrain` reads those same channels. Neither channel can safely be repurposed in the cage texture. G of the height-only texture receives D as requested. For reliable sampling with cages bound, a sixth word containing the same D is appended to the existing heading atlas (pixel t11, vertex t4). No new texture bindings; all original cage RGBA data remains intact. Wind-mode atlas addressing uses six words instead of five.

2. **D consumers: done.** Pixel `localTerrain`, direct and cached CPU terrain damping use the fine response to D. The old additional `SafeDamping` real-depth floor ramp is bypassed for this experiment's main-level FFT vertices/refinement budgets; cage floor constraints remain. The old real-depth lighting-slope restoration is disabled there. Swell travel-time speed also uses D. Only the exported vertex FFT offshore weight changes from optical R to ocean connectivity; optical texture R, reflection/fresnel rules, CPU obstacle/hull decisions and cage algorithms retain their existing inputs/rules. Rivers retain their terrain rule.

3. **Band response: done in both shader stages.** Swell/medium/fine gains are `smoothstep(0,24,D)`, `smoothstep(0,12,D)`, `smoothstep(0,4,D)`. CPU vertices supply the common fine envelope; spectral bands divide that envelope out before composition, avoiding a second fade. The quotient has an analytic finite limit at D=0. Physical/filtered displacement, tangents and unresolved slope power are scaled consistently. The bilinear D-gradient product term is included; it is not omitted.

4. **Breaking foam: done.** Incoming positive swell crests are compared with `0.20*D` before the band fade, with smooth threshold/wet-edge fades. Coverage moves with the existing swell phase and joins `foam`/`foamWithoutShore` alongside existing foam sources. It fades out under cage support and honors obstacle/hull wetness. Gamma targets typical coastal distances of approximately 5–30 units, but that visual placement needs a GPU A/B run.

5. **Channel currents: done.** Medium and fine samples drift using the same direction/energy-derived, bank-projected current reconstructed by `fftLocalArrival`; this adds no simulation-current texture. Both vertex/pixel stages use two 12-second advection intervals, half a period apart, with complementary triangle weights. Coordinates reset only at zero weight, so stretching cannot accumulate. Pixel tangents include the velocity Jacobian. Zero-current water retains one unadvected sample.

6. **Debug views: done.** `medium-depth` shows D/40 grey; `band-gains` shows swell/medium/fine gains in RGB; `break-foam` shows new breaking coverage. Tests also compile the existing field and mask views.

Limitations and assumptions:

- One D field covers the main ocean/source elevation selected by the existing source classifier. Other elevations receive inactive/deep atlas metadata and retain their existing terrain damping; their debug depth appears white. A per-level D would need per-level retained fields/uploads.
- Wind explicitly selects the map-aware evaluator even with `WaterMovementBehavior=no`; the default retains that compatibility setting and its original shader source.
- Mesh sampling/interpolation, local-water residual envelopes, hulls, cages, seabed optics and existing foam systems remain. In particular, physical cage constraints can still suppress waves independently of D.
- The heading atlas grows by 20% in wind mode. GPU cost, visual continuity, foam tuning and channel appearance have not been measured here.

Validation:

- `cmake --build build-linux --target generals -- -k 0`: passed (existing compiler warnings).
- `test_shader_sdl` and `test_water_grid` builds: passed. Shader CTest passed with wind (17.69 s) and unset (16.16 s), including all ten new/existing debug views. Default source hashes pass unchanged; wind bypasses only the deliberately changed frozen-source comparisons and still compiles the numerical fixture.
- CPU depth/heading test: 485 checks passed under AddressSanitizer/UBSan. LeakSanitizer was disabled because this sandbox uses ptrace.
- Final linked-binary CPU checks: 9 tests / 1,366,565 checks passed (depth, heading, static terrain/cache, cache invalidation and local waves). Separate blur/default and range/wind shader smoke checks also passed (37 checks each).
- `git diff --check`: passed.
- ShaderCross HLSL-to-SPIR-V, spirv-val, MSL translation and binding checks are available. Metal device acceptance/pipeline linking and visual GPU A/B are unavailable in this Linux sandbox.
