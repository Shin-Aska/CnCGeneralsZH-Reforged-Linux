Shared-wave Modern water experiment — 2026-11-10

Implemented behind `ZH_WATER_SHARED=1` in `/mnt/sandisk-2tb/worktrees/water-tempo-ab`, branch `experiment/water-tempo-ab`. Shared mode implies the map-load D build. No commits, branches, installs, main-checkout edits, or changes under `build-linux/ab/`.

Files changed for this experiment:

- New `GeneralsMD/Code/Libraries/Include/Platform/WaterSharedField.h` and `WaterSharedShader.h`.
- `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWater.h` and `Source/W3DDevice/GameClient/Water/W3DWater.cpp`.
- `GeneralsMD/Code/Libraries/Include/Platform/WaterFftSampling.h` and `Libraries/Source/WWVegas/WW3D2/engineshader.cpp`.
- New `GeneralsMD/Code/Tests/WaterSharedFieldTest.h`; updated `test_water_grid.cpp` and `test_shader_sdl.cpp`; this report. All other pre-existing experiment edits were preserved.

A is built once alongside D, using the same main/source-level selection and cache invalidation. It uses the terrain grid, 16 fixed world directions and two bands. Seeds are square roots of the spectrum's angular power, normalized to unit squared amplitude per band, using its wind direction and `ZH_WATER_FFT_DIRECTION` powers. Zero power gives isotropic seeds. Two directional sweeps carry water amplitudes from playable edges, reset them on land, and diffuse across the march direction and neighbouring direction bins. Angular exchange preserves the open-water seed. Depth gains fade long waves over D=0..24 and medium over 0..12, with up to 12% shoaling. In shallow water, up to 35% moves one bin toward the shoreward depth gradient, `-grad(D)`. A wet-only radius-two Gaussian finishes the field; floats retain the exact 8-bit quantized values.

Delivery uses the existing heading binding, pixel t11 / vertex t4. The original six words occupy the first atlas plane; eight RGBA8 amplitude words occupy two further rows per cell. Dimensions are `6*side` by `3*side`, avoiding an over-wide 14-word horizontal atlas. No new texture or sampler binding, no per-frame upload. Manual bilinear reads also provide amplitude gradients. This triples the one-medium atlas allocation: 72 MiB at padded side 1024, including unused row padding. Retained CPU amplitudes cost 128 bytes per terrain cell. Synthetic O2 all-water builds took 139 ms at 256² and 692 ms at 512² here; these are single CPU measurements, excluding other map preparation.

Profiles are evaluated in the shader. Each band has four equal-power spatial harmonics: long wavelengths 1056/528/264/150.86, medium 240/80/40/24 world units. One spatial sincos and six complex products recover each four-component profile; components retain separate deep-water dispersion using the existing gravity, world scale and tempo. Seeds and generated profile gains target `HeightRms[0/1]` (1.50/0.75) before the existing limiter. Height derivatives include `P*grad(A)`. Footprint filtering and unresolved slope variance share the same profiles.

Expected ordinary shared-field cost, excluding vessels/cages/optics: 32 spatial sincos pairs plus eight uniform time pairs, Gaussian filtering/arithmetic, 32 packed amplitude reads, and depth/fine reads. The integrated main pixel path uses about 42 reads, or 44 with filtered fine samples; the vertex wrapper uses about 41. Physical and filtered profiles are evaluated together. This trades arithmetic for avoiding 32 additional profile-table reads and dynamic atlas uploads; actual GPU speed remains unmeasured.

On the main elevation, both stages bypass the analytic swell, sector rotations/cross-fades, medium FFT, local arrivals/residual gains, current advection, near-shore analytic remainder, and offshore wave selection. A already contains long/medium depth response; fine FFT stays globally unrotated and fades over D=0..4. The CPU supplies obstacle/hull damping without another depth fade. Painted `mediumBreakFoam` is zero. Vessel wakes, hull reflections, cages, optics, seabed and steepness-driven crest foam remain. The forward-plane refraction fix applies on the shared elevation. Other levels retain their prior evaluator, including movement compatibility mode; explicitly requesting `ZH_WATER_MEDIUM=wind` preserves its behaviour there.

Debug views: `shared-amplitude` = long sum/4, medium sum/4, fine depth gain; `shared-direction` = medium amplitude-weighted heading hue, brightness total/4; `shared-height` = profile plus fine height before vessels/limiting, grey 0.5 at zero, range ±4 units. Inactive views are black.

Validation:

- Required `cmake --build build-linux --target generals -- -k 0`: passed; both test targets built. Existing compiler warnings remain.
- Full `test_water_grid`, shared unset and `=1`: each 237 tests / 13,140,219 checks passed.
- Full `test_shader_sdl`: default 57 tests / 693,424 checks; wind 57 / 693,428; shared 57 / 693,480; all passed. Includes vertex/pixel compilation, SPIR-V validation, MSL translation, binding checks and all three shared debug views.
- Standalone shared builder under ASan/UBSan: 268,543 checks passed, with LeakSanitizer disabled for the sandbox. Covers wind/uniformity, soft island shadows and downstream refill, depth fade, shore turning, quantization/packing, determinism and invalid inputs.
- Before/after generated FFT source hashes across all tested option combinations match exactly: default `dfad943f831c4131`, wind `7b15c3b2145f512d`. Existing complete default-source frozen checks also pass. Existing replacement anchor lines were retained.
- Combined ALIVE, FACET, REFRACT_FORWARD=0, GLITTER and FOAM_TEX debug-view smoke runs: shared 221 checks and wind 169 checks passed, with no missing-anchor warnings.
- `git diff --check`: passed. Initial parallel validator runs collided on the test harness's temporary filenames; isolated TMPDIR runs above passed.

Limitations: static amplitude transport is an approximation, not a wave simulation. The experiment requires the native Modern FFT path; its existing CPU-only fallback still uses legacy waves. Full-resolution storage and per-pixel profile arithmetic may be expensive on large maps. No GPU is available here: Metal acceptance/linking are skipped, and visual A/B recordings, motion/crop comparisons, and ABBA GPU timings remain untested.
