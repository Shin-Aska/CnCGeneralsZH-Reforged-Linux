# Linux sun and smoke shadows

Linux uses mapped sun shadows by default. This change reuses the existing sun
camera, caster selection and shadow filter, and adds sampleable sun depth and
smoke maps to the SDL renderer. Windows keeps its existing rendering path;
macOS keeps its existing scene shadows. The Linux shadow capability is separate
from per-pixel lighting.

This contribution builds on the Modern water contribution rooted in
`reference/3.1.0`. It uses that contribution's native draw, depth snapshot and
water uniform infrastructure. The volumetric hazard-cloud contribution is a
separate feature; this smoke map shades the existing particle systems.

## Behavior and settings

Supported casters include registered units and buildings, bridges, alpha-tested
foliage, object palms and sorted translucent meshes such as rotor discs. Friendly
cloak casters retain their per-draw coverage through the sorting queue. Eligible
solid, terrain, road, bridge, tree, particle and Linux Modern water draws receive
the mapped light. Reflections, nested views and excluded overlays retain their
own rendering behavior.

The existing shadow controls remain authoritative:

- `ShadowQuality = 0`, `1` or `2` selects a 1024, 2048 or 4096 square sun map.
  The filter's world-space width remains consistent across those sizes.
- Unit casters honor `UseShadowVolumes`. Trees and bridges honor the ordinary
  volume/decal shadow settings. Trees already loaded by a multiplayer map can
  cast even when the `ShowTrees` display preference is off.
- `VolumetricSmokeShadows` independently controls smoke projection. Smoke can
  retain an otherwise empty capture when ordinary geometry shadows are off.
- Explicit Classic water keeps the existing water path.

`ZH_SUN_SHADOW_MODE` is a Linux diagnostic environment override:

| Value | Result |
| --- | --- |
| unset or `mapped` | Capture and receive supported mapped shadows. |
| `capture-only` | Capture depth while retaining legacy scene shadows. |
| `legacy` or an unknown value | Skip capture and retain legacy scene shadows. |

A requested mode alone does not prove that capture or receiving succeeded. Use
`-shadowmapreport`, or the harness's `--report`, to inspect depth occupancy and
accepted receiver counts. Readback belongs to diagnostics and affects timing.

## State, lifetime and fallback

Each scene owns its capture by generation, target, viewport and camera. Nested
scenes and reflections cannot borrow the outer view's shadow map or suppress its
legacy shadows. Queued draws retain their own constants, receiver exclusions and
cloak effect. Capture restores the scene's device state, and replaced depth
textures remain alive until queued readers have been submitted.

Unsupported depth targets, invalid sizes and capture failures retain legacy
shadows. Smoke resource or pipeline failure retains particle blob shadows while
solid mapped shadows can continue. A failed optional receiver program or pipeline
retries ordinary geometry and latches legacy rendering for subsequent scenes
until device recreation. If mapped pixels were already submitted, that scene
keeps its ownership to avoid applying stencil darkening twice; fallback does not
replay the whole frame atomically.

Water shares the CPU upload contract in `SdlConstants.h` with the declarations in
`WaterFftSampling::UniformSource`. Both `WorldFromClip` and `ShadowFromClip` are
explicitly `row_major` HLSL matrices copied from row-major CPU arrays. glslang's
transposed HLSL representation decorates them as SPIR-V `ColMajor`, with a
16-byte matrix stride. Matrix order matters even when offsets and sizes agree.
The shadow fields add eight float4 registers before the two material registers:
water's common native prefix is 196 float4s and its fragment metadata extension
is 204. The water-only contribution uses 188 and 196 respectively. These native
blocks are distinct from the unchanged emulated engine register assignments.

## Reproduce a scene

Use an existing Release executable, its overlay, and a prepared file-link root
containing your own game data. The harness does not install dependencies or
modify the prepared root. Each output directory must be new. Run GPU workloads
serially.

```sh
python3 GeneralsMD/Code/Tools/sun-shadow-check.py \
  --generals "$PWD/build-linux/generals" \
  --root /absolute/path/to/prepared-game-root \
  --overlay "$PWD/build-linux/overlay" \
  --out "$PWD/build-linux/shadow-check-solid-mapped" \
  --case solid --mode mapped --logic-clock --gpu-debug --report
```

For a matched legacy capture, use the same executable, case and options, change
`--mode` to `legacy`, and choose a new output directory. `--logic-clock` holds
animation and water phases for screenshot comparisons; it is restricted to
unattended single-player capture and does not change multiplayer or replay logic.

Cases are `solid`, `smoke`, `cloak`, `trees`, `forest`, `bridge`, `helicopter`,
`water`, `river`, `water-caster` and `water-smoke`. The harness also accepts
`--water classic`, `--quality 0|1|2`, `--trees yes|no`,
`--geometry-shadows yes|no` and `--smoke-shadows yes|no`. Development scenarios
are copied into a private overlay only when the selected case needs them.

`--fault target|receiver-program|receiver-pipeline|smoke-target|smoke-pipeline`
requests a specific creation failure. `--depth-dump --report` saves the latest
reported depth map, which need not be the screenshot frame. `--replay` accepts an
existing recording for a case without scenario actions and without a fixed clock.

Each run records its command, explicit environment, executable hash, source
checkout SHA/diff, settings hash, scenario or replay hash, exit status, limit-frame
CRC, screenshot hashes and available timing/receiving diagnostics in
`manifest.json`. Source provenance is resolved from the harness's checkout,
independently of the caller's working directory. Verify the CRC and inspect the
images and logs; a successful process exit alone does not prove correct shadows.

For comparable performance diagnostics use `--measure`, the same scene, settings,
executable and driver, and a new output directory per run. This mode excludes
screenshots, reports, fixed clocks, fault injection and GPU validation. The
aggregate device probe includes loading; `HEADLESS FRAMETIME` starts after logic
frame 60. Neither value alone is a universal steady-state FPS result.

## Validation and limits

With the Release build already configured, the focused checks are:

```sh
cmake --build build-linux --target test_shader_sdl \
  posix_draw_resolve_selfcheck posix_gpu_draw_selfcheck \
  sdl_gpu_frame_selfcheck test_sun_shadow_sorting
ctest --test-dir build-linux --output-on-failure \
  -R '^(test_shader_sdl|posix_draw_resolve_selfcheck|posix_gpu_draw_selfcheck|sdl_gpu_frame_selfcheck|test_sun_shadow_sorting)$'
```

Compiled shader reflection checks every water member's offset and size, both
matrix layouts and strides, and the vertex/fragment block lengths. The native
GPU fixtures cover scene restoration, queued resource replacement, empty clears,
exclusions, smoke moments, cloak sorting, failure recovery and actual flat/river
water receiving through a translated, scaled camera. Linux-only runtime fixtures
are gated accordingly; portable mode parsing remains tested on other platforms.
GPU checks report a skip when no usable device is available.

These checks complement real-game scene and replay validation. They do not prove
visual parity on every Linux GPU or driver, or native Windows/macOS compatibility.
Use the contribution's validation record for the exact tested source SHA,
hardware, commands, failures and skips. This guide makes no historical benchmark
or cross-platform runtime claim.
