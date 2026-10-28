# Modern Water performance validation (Linux)

This note records the validation environment, historical host benchmark, live
settings checks and separate instrumented material experiments for Modern Water.
The Intel UHD 770 material series supplies aggregate Vulkan GPU timings; it
does not establish named water-pass timings, production FPS or an Iris Xe result.

## Available hardware and tools

Checked on 2026-10-26 in this workspace:

| Device | Driver | Evidence |
| --- | --- | --- |
| Intel UHD Graphics 770 (ADL-S GT1) | Intel open-source Mesa 26.0.8-1ubuntu0.3; Vulkan 1.4.335 | `vulkaninfo --summary` |
| NVIDIA GeForce RTX 4060 | NVIDIA 595.99.02; Vulkan 1.4.329 | `nvidia-smi`; `vulkaninfo --summary` |
| llvmpipe | Mesa 26.0.8-1ubuntu0.3 | `vulkaninfo --summary` |

This is not an Iris Xe machine; it does have a real Intel integrated GPU (UHD
770), so Intel Vulkan correctness/performance checks can run here and should be
reported separately from Iris Xe validation. SDL has no `SDL_GPU_DEVICE_NAME`
hint in the vendored API. `SDL_GPU_DRIVER=vulkan` selects the SDL backend, not a
specific adapter. SDL's Vulkan backend ranks suitable physical devices and
prefers the highest-ranked one, so changing enumeration order alone is not a
reliable adapter selector. This machine also has the implicit Mesa device
selection layer; `MESA_VK_DEVICE_SELECT=8086:4680! vulkaninfo --summary`
verified that the `!` form exposes only the UHD 770. `VK_ICD_FILENAMES` below
is the simpler, verified preview selector because it also isolates the driver.

For this machine, the verified per-adapter selector is the Vulkan loader's
`VK_ICD_FILENAMES`, set to one installed ICD manifest. It leaves only that ICD's
device visible to the process:

```sh
SDL_GPU_DRIVER=vulkan VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.json \
  ./build-linux/water_preview --offscreen --frames 180 --mode split --reflection hybrid

SDL_GPU_DRIVER=vulkan VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build-linux/water_preview --offscreen --frames 180 --mode split --reflection hybrid
```

Verified with `vulkaninfo --summary` under each variable: the Intel manifest
listed only Intel UHD 770 (vendor `8086`, device `4680`); the NVIDIA manifest
listed only RTX 4060 (vendor `10de`, device `2882`). Both bounded preview runs
reported Vulkan, 180 presents, 180 production FFT updates, 1,260 recorded draws
and no refused draws. `/usr/bin/time` reported 26.08 seconds wall for the Intel
run and 0.83 seconds for the NVIDIA run. These are single sanity runs, not a
controlled performance comparison; they started with different device state,
and the Intel GPU was also driving the desktop. Always save the exact ICD
selection alongside each result. `VK_ICD_FILENAMES` is specific to this host's
installed manifests; confirm paths with `ls /usr/share/vulkan/icd.d` on other
systems.

Among checked profiling commands, `/usr/bin/perf` and
`/usr/bin/intel_gpu_top` are installed. `perf list` exposes system GPU energy
and DRM render/copy/video engine counters, but those are CPU/system-wide or
device/engine counters, not pass attribution. `intel_gpu_top` has CSV/JSON
device-engine monitoring; `nvidia-smi` reports device-level activity. No
`nsys`, `ncu`, `renderdoccmd`, `qrenderdoc`, `gfxreconstruct`, or `apitrace`
command was found. No tool was installed for this investigation.

The vendored SDL GPU public API (`Libraries/Source/SDL3/include/SDL3/SDL_gpu.h`)
does not expose timestamp query pools or pass timestamp results, and the SDL
Vulkan backend has no `vkCmdWriteTimestamp`/query-pool implementation. The
installed `VK_LAYER_MESA_overlay` is an available diagnostic alternative for
Vulkan GPU timing. It accepts `gpu_timing`, `frame_timing`, and `output_file`
through `VK_LAYER_MESA_OVERLAY_CONFIG`; a bounded 60-frame Intel windowed preview
run created a CSV with `device, format, fps, frame_timing(us), gpu_timing(ns)`
fields. The layer uses Vulkan timestamp and pipeline-statistics query pools. This
is useful aggregate GPU timing evidence, but its output does not name individual
water passes. The layer needs a window/swapchain for its normal
overlay/reporting path and changes the workload with instrumentation and query
readback; use it as a separate diagnostic, not as an uninstrumented baseline.

There is no currently verified per-water-pass GPU timing path. A future
implementation can add Vulkan timestamp instrumentation around SDL's logical
water passes (or use an installed, validated external GPU profiler with named
pass markers). Until then, report isolated water-pass GPU timings as unavailable.

## Existing profiling and capture paths

- `ZH_WATER_PROFILE=1` enables `[water-profile]` and sampled CPU phase reports
  in `W3DWater.cpp`. Its `submit_ms` is CPU submission time, not GPU execution.
- `ZH_GPU_TIMING="delay,count"` reports per-frame wall time and CPU device draw,
  flush, present, swapchain/offscreen waits, plus the draw count. It also prints
  a coarse total fence-wait field when `ZH_GPU_TIMING_SYNC=1`; synchronization
  serializes CPU and GPU work and changes normal frame timing. Neither mode
  measures isolated GPU water passes.
- `water_preview` supports deterministic GPU-backed runs without game assets:
  `--offscreen --frames N`, optional `--reflection ...`, and `--screenshot`.
  `--help` lists the full options. Its scene is synthetic unless `--scene` is
  supplied.
- Existing scene banks are present at
  `build-linux/water-scenes/sw-general.wbank` and
  `build-linux/water-scenes/sw-general-oldmirror.wbank`. They replay a fixed
  camera and banked game colour/depth snapshots, not a live map or moving units.
  The documented game-bank capture path uses `ZH_WATER_BANK=PATH` and
  `ZH_WATER_BANK_FRAMES=300` during a game run; bank capture is not a headless
  game GPU path.
- `-headless` creates no SDL GPU device. It cannot validate Modern Water GPU
  rendering. Use the native preview's offscreen mode for repeatable GPU-backed
  rendering, and keep production-game validation separate.
- To collect the Mesa layer's instrumented aggregate Vulkan timings from a
  windowed preview, for example:

  ```sh
  SDL_GPU_DRIVER=vulkan VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.json \
    VK_INSTANCE_LAYERS=VK_LAYER_MESA_overlay \
    VK_LAYER_MESA_OVERLAY_CONFIG='gpu_timing,frame_timing,output_file=/tmp/mesa-water.csv' \
    ./build-linux/water_preview --frames 60 --mode material --reflection hybrid
  ```

  Check that the CSV was produced and contains frame and GPU timing columns.
  Its GPU timing is an aggregate for submitted Vulkan command buffers, not a
  labeled per-water-pass value. SDL batching can combine logical passes in a
  command buffer, so do not describe this as water-pass timing. Layer-instrumented
  values belong in their own diagnostic series.

An inventory sanity run completed:

```sh
./build-linux/water_preview --offscreen --frames 180 --mode split --reflection hybrid
```

It reported Vulkan, 1280×800, 180 presents, 180 production FFT updates, 1,260
recorded draws and no refused draws. `/usr/bin/time` reported 2.36 seconds wall
time on the default adapter-selection run. This is an end-to-end preview process measurement (startup,
CPU work, GPU work/overlap and offscreen waits). Do not interpret it as a
per-frame gameplay result, a GPU time, or a before/after setting gain.

## Controlled comparison protocol

Use this matrix after the selected controls are implemented. Keep build,
resolution, driver, adapter, power mode, process state, scene, camera and
reflection mode fixed. Run sequentially with no concurrent GPU tests or builds.
For synthetic preview runs, use the same binary and same scene options for each
row; for scene-bank runs, use the exact same `.wbank` file. Preserve screenshots
separately from timed runs because screenshot readback can perturb timings.

| Case | Preview arguments | What it isolates or compares |
| --- | --- | --- |
| Material-only baseline | `--mode clean` then `--mode material` | Final surface/material contribution against the clean scene |
| Reflection path | `--reflection legacy`, `ssr`, `hybrid` | Reflection mode contribution (synthetic scene has the documented planar target sizes) |
| Fixed banked game view | `--scene build-linux/water-scenes/sw-general.wbank --reflection legacy|ssr|hybrid` | Same camera and captured game colour/depth inputs across modes |
| Resolution/material load | repeat at 1280×800 and a fixed higher or lower resolution | Pixel/work sensitivity; do not change resolution within a comparison pair |
| Future settings | each setting's default and changed value, one at a time | Attribute changes without confounding simultaneous settings |

For each case, do one untimed warm-up process to allow shader/pipeline creation,
then five measured repetitions. A practical whole-process timing command is:

```sh
for run in 1 2 3 4 5; do
  /usr/bin/time -f "run=$run wall_s=%e user_s=%U sys_s=%S" \
    ./build-linux/water_preview --offscreen --frames 1200 --mode material --reflection hybrid
done
```

Use the same arguments for every variant except the one setting under test.
Record each repetition, report the median and p95 of the per-frame observations
when collecting `ZH_GPU_TIMING`, and also report the median and p95 of the five
whole-process wall times if using `/usr/bin/time`. Five process-level samples
are too few for a stable p95; label that p95 descriptive and retain all raw
values. A better frame-time p95 comes from at least 600 post-warm-up frame
samples in one run; for `ZH_GPU_TIMING`, set `delay` beyond startup and `count`
to at least 600, and ensure the process stays alive long enough to collect all
samples. Report wall time, frame time, CPU device timings, and any fence wait as
distinct fields.

Capture and report for every measurement: date; source revision and dirty state;
binary; GPU name and driver from that run; Vulkan version; power mode and AC
state; display/pacing state; resolution; scene/bank; reflection mode; settings;
warm-up procedure; raw repetitions; median and p95; and any validation or
capture overhead. Vulkan validation, screenshot readback, synchronous fence
timing and external frame capture are diagnostic modes, not ordinary performance
runs. Do not combine their timings with uninstrumented runs.

Per-pass GPU timing remains a separate acceptance item. Once a validated query
or profiler path exists, collect reflection, FFT, scene snapshots/copies and
final water shading independently, without adding synchronous waits to normal
production rendering. Until that path is available, state explicitly that
GPU-pass timing and the associated GPU-cost claims remain pending.

## Existing relevant checks

Configured build targets include `test_water_path_gpu`, `test_water_path_tracing`,
`test_water_scene_bank`, `test_water_preview_preset`, `test_modern_water_config`,
`test_water_grid`, and `test_water_tracks`. `build-linux/` contains built
executables for these targets. This inventory does not claim that they were run
as part of the performance investigation.

## Implementation validation, 2026-10-26

The preview now exposes Scene reflections On/Off, Wave detail Low/High and
Refraction Full/Simple/Off. Missing preview arguments retain On/High/Full.
`--reflection-detail low|high` is the selected planar-only follow-up: High retains
the original Legacy 512 and Hybrid 1024 sides; Low halves those sides. It does
not change SSR or the successful Path traced capture. Banks retain captured
planar resolution and report that limitation when Low is requested.

The configured Release build uses the existing GCC 16 and vendored dependencies;
no package was installed. Raw captures, command/environment records, frozen
binaries, frame CSVs and diagnostic logs are kept in the ignored directory
`build-linux/water-performance-validation/`. Bank captures remain untracked.

### Initial control correctness and default appearance

A bounded two-frame synthetic matrix ran on each isolated ICD at 640×400:
all twelve combinations of reflections On/Off, Wave detail Low/High and
refraction Full/Simple/Off under Hybrid, plus Legacy/SSR/Path traced each On/Off
with refraction Off. All 36 cases completed with no refused draws. Exact work
and active-resolution assertions passed for every row:

| Work per synthetic frame | High waves | Low waves |
| --- | ---: | ---: |
| FFT updates | 1 | 1 |
| FFT passes, including mip generation | 26 | 23 |
| FFT copies | 102 | 90 |
| Active FFT side | 256 | 128 |

With scene reflections Off, planar renders and path preparations were zero.
Refraction Off with Hybrid/SSR On retained one colour capture per frame;
refraction Off with scene reflections Off used zero colour captures and retained
one depth capture. Opaque Legacy and a successful Path traced capture also
needed no colour snapshot. Full/Simple still captured colour for transmission.
These are actual recorded work counts, not inferred GPU timing savings.

Separate 1280×800, two-frame screenshots used time 18, fixed default camera,
material mode, Hybrid, no editor controls and the same staged material INI.
On **each** GPU the preserved pre-change binary and the new omitted-argument
default were pixel-identical across all 1,024,000 pixels. Omitted arguments and
explicit On/High/Full were also pixel-identical. Additional captures cover each
changed setting, white clear water and the same banked game view. Full, Simple
and Off clear-water captures were visually inspected: Simple retains clear
undistorted transmission; Off removes underwater transmission and seabed detail.
Low waves change seeded wave phases while retaining animation; changing detail
keeps the simulation clock running rather than resetting it.

The default-image comparison used the build after correcting opaque coverage:
Refraction Off now supplies authored opaque coverage independently of whether
a scene-colour texture exists, while retaining shore fade. A production GPU
regression exercised the final alpha/blend path on both adapters, including
armed controls for the old dependence on scene colour.

### Validation layer and existing Intel test failures

The installed Khronos layer was used separately from timings through
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and `ZH_GPU_DEBUG=1`. On both
ICDs, eight-frame Low/reflections Off/refraction Off and High/On/Full previews,
plus the bounded full SDL GPU selfcheck containing settings/FFT transitions,
produced **zero validation/VUID warning or error lines**. Both previews completed
without refused draws. The NVIDIA full selfcheck passed.

The Intel UHD 770 full selfcheck still reports 489 assertion failures. A focused
baseline reconstruction using the original `317f23ba` shader generator, FFT
helpers and original fixtures reproduced the exact same expressions and counts:
8 sky-integral tolerance checks, 1 cosine-integration convergence check and
480 strict paired-FFT-sampling zero checks. No unrelated tolerance was widened.
This establishes those failures as pre-existing; it does not claim a clean Intel
full-suite result. New FFT128 checks passed (direct Fourier maximum error about
`6.31e-7`), as did the new opaque coverage/blending regression on both GPUs.
The exact reconstruction, commands, sources, logs and failure comparison are
retained under `build-linux/water-performance-validation/baseline/README.md`.

### Planar Reflection detail follow-up

An explicit `water_preview` target build is required because the tool is
excluded from the default full build. The final preview was built separately,
then frozen for the comparison series. Its default captures again matched the
pre-change and explicit High/default settings pixel-for-pixel on each GPU.

Thirty-two additional bounded cases covered both GPUs: Hybrid High/Low planar
detail at both wave levels in the default and a fixed panned/closer clear-water
view, plus Legacy, SSR, successful Path traced and the same scene bank. Logs
confirmed Legacy High/Low targets of 512/256 and Hybrid 1024/512. SSR and
successful Path traced allocated no synthetic planar fallback; their High/Low
images were pixel-identical, as were bank High/Low images. A bank requested at
Low explicitly reported that its captured planar resolution was retained.
Panned thin-column/striped-pole reflections were inspected at High and Low;
Low was softer while keeping a coherent reflection in the checked views.
These are fixed-view visual checks, not a moving-camera performance claim.
An additional Low planar/Low wave preview under the Khronos layer on each GPU
reported no validation warning/error or VUID line.

### Uniform host benchmark protocol

The final comparison uses the frozen final preview at **640×400**, synthetic
material mode, Hybrid, fixed default camera and initial time 18. Each case gets
a separate 120-frame warm-up process, then five measured processes of
**360 total frames: 120 warm-up + 240 retained frames**. This yields 1,200
retained host-frame observations per case/device. An initial 720-frame pilot
was stopped and kept separately under `pilot-720/`; none of its observations
are mixed into the final series. The shorter uniform process length limits
elapsed desktop/thermal drift while preserving all cases and repetitions.

The eight cases are default On/High/Full/High; reflections Off; Wave detail Low;
refraction Simple; refraction Off; reflections Off + refraction Off; planar
Reflection detail Low at High waves; and planar Reflection detail Low at Low
waves. The existing default and Wave Low rows provide the corresponding High
planar-detail references. Every other argument stays fixed. The final binary's
SHA-256 is `b20f897418e3ac1cbb8598876e335d5b1cfdab31c2ea137b1c030a562652f513`.
For example, the default measured process is:

```sh
SDL_GPU_DRIVER=vulkan VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/intel_icd.json \
  build-linux/water-performance-validation/stage1/water_preview \
  --offscreen --hide-controls --config build-linux/overlay/Data/INI/ModernWater.ini \
  --time 18 --width 640 --height 400 --mode material --reflection hybrid \
  --benchmark --frames 360 --benchmark-warmup 120 --benchmark-csv result.csv
```

The series runs sequentially, after builds and GPU tests finish, without
screenshots, GPU validation/profiling layers or synchronous timing. Observed
power settings were `balanced` and CPU governor `powersave`; neither GPU clocks
nor thermals were locked. This KDE/Wayland desktop exports no battery/AC supply
status. Start/end metadata retain load averages, process-name/CPU snapshots,
GPU clock/activity observations, driver/ICD details and source/binary identity.
Normal desktop background activity remains uncontrolled. Treat these as
controlled-workload host observations, not isolated GPU-pass or gameplay FPS
measurements. The synthetic preview updates its planar target each frame;
the production game has its own stationary-camera reuse and scene workload.


### Host results and interpretation

The table pools all 1,200 retained frame observations per row. Median uses the
middle observation(s); p95 uses nearest rank. Process wall is the median of
five complete process times, including startup and teardown. All 80 processes
completed without refused draws and passed exact setting-specific work-count
assertions. Raw per-run medians, p95s, five wall times and a descriptive
five-sample wall p95 are retained in `results.json`; the five-sample wall p95
is not a stable tail estimate. `benchmark-summary.md` and all frame CSVs remain
beside it.

| Device | Case | Host median ms | Host p95 ms | Process wall median s |
| --- | --- | ---: | ---: | ---: |
| intel | default | 50.150 | 69.543 | 19.171 |
| intel | reflections-off | 37.816 | 54.997 | 14.417 |
| intel | wave-low | 44.869 | 61.567 | 17.127 |
| intel | simple | 42.158 | 60.066 | 16.150 |
| intel | opaque | 37.789 | 53.184 | 14.431 |
| intel | minimal | 32.639 | 47.421 | 12.532 |
| intel | reflection-low | 49.798 | 68.822 | 18.996 |
| intel | wave-low-reflection-low | 44.686 | 60.254 | 17.313 |
| nvidia | default | 1.337 | 1.853 | 0.818 |
| nvidia | reflections-off | 1.172 | 1.666 | 0.754 |
| nvidia | wave-low | 0.989 | 1.475 | 0.684 |
| nvidia | simple | 1.285 | 1.792 | 0.800 |
| nvidia | opaque | 1.178 | 1.678 | 0.755 |
| nvidia | minimal | 0.966 | 1.441 | 0.677 |
| nvidia | reflection-low | 1.331 | 1.855 | 0.818 |
| nvidia | wave-low-reflection-low | 0.986 | 1.466 | 0.674 |


`minimal` means reflections Off + refraction Off, with High waves and High
planar detail remembered. `opaque` means refraction Off with Hybrid reflections
still On. `reflection-low` retains High waves; `wave-low-reflection-low` is the
corresponding Low-wave pair.

Per measured process, High waves performed 240 FFT updates, 6,240 FFT passes
and 24,480 copies; Low waves used 5,520 passes and 21,600 copies. Reflections On
performed 240 planar renders and 960 recorded draws; Off performed zero planar
renders and 720 draws. Every row retained 240 depth captures. Colour captures
were 240 except in `minimal`, where they were zero. Reflection detail Low
changed target dimensions while retaining the same draw/pass counts. Path
preparations and bank injections were zero throughout this synthetic series.

The combined reflections Off/refraction Off row had a 34.9% lower host median
on this UHD 770 workload and 27.7% on this RTX 4060 workload. These are observed
host draw+Present changes under the recorded desktop conditions, not GPU-pass
savings or a gameplay FPS promise. Wave Low changed the host median by about
10.5% and 26.0% respectively; the resource reduction alone would have predicted
neither result. No Iris Xe result is established.

Planar Reflection detail Low changed host medians by less than 1% at both wave
levels on both GPUs, within the variation of this synthetic workload. No
meaningful host-time improvement from that setting was established here. The
smaller target remains a quality/resource choice; an actual game's reflected
geometry, camera motion and capture reuse can have different costs. The banked
view cannot supply that missing live reflection workload.

Per-water-pass GPU timing, live moving-camera/gameplay performance and a
validated Water shading Low implementation remain separate acceptance items.
The later material diagnostic below investigates shader cost without exposing
a Water shading Low control or establishing equivalent-quality savings.


### Final build and durable regressions

The final combined Release build explicitly included both `all` and
`water_preview` and passed. The complete NVIDIA CTest run registered 111 checks.
After the fingerprint selfcheck's process-local signing retest, results were
**89 passed, 21 skipped and 1 known failure**. Skips require absent game assets
or other platforms. `ffref_capture_selfcheck` retained the existing two-pixel
reference mismatch (2 of 1,675 written pixels, worst at 33,20), reproduced by
the baseline; no rendering tolerance was changed. The fingerprint check's first
temporary-repository run encountered the user's global signing configuration;
it passed in 0.20 seconds when commit signing was disabled for that test process
only. No global Git setting was changed. Full build, CTest and retest logs are
copied under `build-linux/water-performance-validation/final-build/`.

A later preview-only correction declares all comparison columns' snapshot
consumers after opaque rendering and before any water. Otherwise the first
opaque Legacy column could freeze a depth-only snapshot before later SSR/Hybrid
columns needed colour. This affects `--reflection compare`; none of the timed
cases uses that mode. The frozen benchmark executable remains at
`stage1/water_preview` with the verified `b20f8974…52f513` SHA-256 above. The
executable at that validation stage includes this comparison fix and refreshed build metadata;
its SHA-256 is
`70a839466fa15cef150e96ea6ed955e614dcd36f2fa14bc44f940a72085e27f1`.
The newly added settings header is explicitly archived beside the frozen
binary because the first source diff predated its Git intent-to-add entry.
Final build fingerprints should not be substituted for the recorded benchmark
binary identity.

The durable checker passed all **27 cases on each GPU** using the final
executable:

```sh
python3 GeneralsMD/Code/Tools/water-performance-check.py \
  --preview build-linux/water_preview \
  --icd /usr/share/vulkan/icd.d/intel_icd.json \
  --output build-linux/water-performance-validation/intel/portable-final

python3 GeneralsMD/Code/Tools/water-performance-check.py \
  --preview build-linux/water_preview \
  --icd /usr/share/vulkan/icd.d/nvidia_icd.json \
  --output build-linux/water-performance-validation/nvidia/portable-final
```

The Compare/opaque/reflections On regression recorded one colour and one depth
capture per frame, while its Legacy/opaque/On control recorded zero colour
captures. Compare/opaque/reflections Off recorded zero colour and one depth
capture per frame. Separate Khronos-layer On/Off Compare runs on both GPUs
reported no validation warning/error or VUID lines. These final diagnostic runs
are excluded from timing observations.


### Live settings and paused game checks

The real Options Water page was exercised at 800×600 on NVIDIA Vulkan with
staged game assets and an isolated user profile. The nine captures and
`live-game/ui-roundtrip/findings.json` establish that pending combo/checkbox
changes leave `Options.ini` byte-identical; Cancel followed by quit/restart
retains the initial values; Accept writes the selected values; and a fresh
process displays the accepted selections. The scene-reflections checkbox was
checked separately through both Cancel/restart and Accept/restart. Its Off
selection preserves Hybrid (`WaterReflection=2`) and the other water choices.
The accepted set was Wave Low, Refraction Simple, Reflection detail Low and
Scene reflections Off. All game processes started for this check exited cleanly,
and the original window focus and pointer position were restored. This is the
modern menu check; the optional Classic-menu live check was not run.

`live-game/ui-paused/manifest.json` records five in-match Options/Accept/resume
cycles on Tournament Island: Low/Simple/Low, High/Full/High, Low/Off/Low,
High/Full/High and Low/Simple/Low (Wave/Refraction/Reflection detail). Scene
reflections remained Off and the remembered mode remained Hybrid throughout.
Each cycle checked unchanged preferences while changes were pending, the saved
values after Accept, a stable simulation frame while Options was open and
advancing simulation frames after resume. The simulation-frame pairs while
paused were 142/142, 276/276, 413/413, 548/548 and 682/682; the corresponding
post-resume pairs were 191/233, 325/368, 461/506, 596/638 and 731/774. The run
completed with exit code zero. These checks cover repeated live setting changes
and pause/resume, not an in-match performance comparison.

Water labels, enum choices and tooltips are present in the French, German,
Russian, Turkish and Ukrainian string files alongside English. Runtime captures
at 800×600 cover all five languages. The Linux tab helper measures the displayed
string and shrinks it as needed; long Russian and Ukrainian Reflection detail
labels were shortened after the first captures exposed wrapping. Final Water
labels fit. Evidence is under `ui-paused/`: `french-fit-water.png`,
`german-fit-water.png`, `turkish-fit-water.png`, `russian-final-water.png` and
`ukrainian-fit-final2-water.png`, with corresponding manifests and exit code zero.
The existing Ukrainian Defaults button still wraps its long translation; that
pre-existing caption was not changed by this work.

`live-game/camera-resize/manifest.json` records an actual console free-camera
run with Wave Low, Refraction Simple, Reflection detail Low and Scene reflections
Off. Holding and releasing D/A moved the camera across the shoreline; the palm
trees and shore move visibly between `before-pan.png`, `pan-right.png` and
`pan-left.png`. The existing resolution drill then exercised the real Options
device-reset path at logic frame 400, changing 1280×720 to 800×600 and accepting
the new mode. Screenshot dimensions confirm the change. Water remains visible
after the reset and subsequent simulation frames advance from 529 through 657.
The 19.27-second run exited zero with no Vulkan validation error/VUID lines.
Its saved water preferences retain Low/Simple/Low, reflections Off and Hybrid.
Earlier arrow-key and minimap attempts, including captures named `postpan`, did
not establish camera movement and are excluded from this evidence. This check
does not establish navigating-boat or production frame-time performance.

### Frozen native vessel bank and replay limits

The native Tournament Island capture under `live-game/boats-open/` completed
with exit code zero. Its `boats-current.wbank` SHA-256 is
`3c77960a41b83ec09b83c5ac0a4035fc992481b1b6f1b206c7e5f4377d0ce17e`.
`bank-inspection.json` records 1280×720 colour/depth, a 1024×1024 planar image
and two water draws with three active vessel contact entries (`hull.x=3`). The
bank retains those hull/contact inputs and frozen interaction-history data for
material replay. The command log includes spawned battleships and a queued
`move ships` request, but the battleships did not navigate; a queued order is
not proof of movement. This fixture is a stationary three-vessel/shore view,
not a navigating-vessel test.

Separate three-frame frozen replays of this same bank with Scene reflections
Off and Refraction Simple exercised both wave sizes. Both exited zero with no
refused draws or Vulkan validation errors. The Low replay reports requested and
active FFT side 128, 23 FFT passes and 90 copies for its one initial update;
High uses 256, 26 passes and 102 copies. Captures `vessel-wave-low.png` and
`vessel-wave-high.png` retain the visible hull/shore coverage. These are fixed
bank checks, with the native replay limits below, rather than moving-vessel
acceptance or timing measurements.

The bank's water draw viewport remains 1280×720 even when the preview window is
requested at 640×400. Its replay cost must not be compared directly with the
640×400 synthetic fixtures as a resolution-normalized result. Native coastal
foam in texture slot 15, the resolved sun state and smoke are not fully
serializable in the current bank format; the inspected slot 15 is empty.
Consequently the bank cannot establish complete native coastal/smoke parity or
reproduce live reflection/capture producers. Moving units, wakes under motion,
live camera/resize transitions and complete native-scene parity remain separate
checks.

### Instrumented material cost experiment

The later `material-profile/series/` is a separate Intel UHD 770 diagnostic,
not an extension of the historical 80 host runs. It uses preview SHA-256
`9a35061b8e3ce2963fbab402a2da1d60143649b9d6a423d906542c221b135ea7`,
Mesa 26.0.8-1ubuntu0.3 and the installed `VK_LAYER_MESA_overlay`. The recorded
start is 2026-10-26 11:34:28 UTC, with CPU governor `powersave`. Exact commands,
GPU identity and per-run work counts are in `metadata.json`; `summary.json`,
`summary.md` and `timing-statistics.csv` retain all run medians, p95s and paired
deltas. No profiler was installed.

There are **63 sequential processes**: three fixtures × seven shader profiles ×
three repetitions. Each runs 60 frames with 20 whole warm-up frames excluded,
leaving 40 single-frame timing buckets, or **2,520 retained observations**. The
layer was configured with `frame,gpu_timing,frame_timing,fps_sampling_period=0,
no_display=1`; cumulative emitted frame counts select the retained frames and
every retained bucket has `frame=1`. All retained timestamps are positive and
below one second (0.629–136.843 ms across the series). Captures are separate
from timing runs.

All fixtures use Hybrid, material mode, hidden controls and frozen time. The
synthetic shore uses the default view at time 18; synthetic open water uses
`--no-props --target 0 -650 --map-color FFFFFF --map-texture TWWater01.tga`.
The third fixture replays the exact vessel bank above at its captured target
size. Frozen-time assertions confirm zero measured FFT updates, passes and
copies after warm-up. Across every profile, the 40 measured frames retain the
same fixture-specific work: shore has 160 draws, 40 colour/depth captures and
40 planar renders; open has 80 draws, 40 colour/depth captures and no planar
renders; bank has 200 draws and 40 bank injections with no live scene captures
or planar renders. No fixture prepares a path-traced capture.

The reported GPU value is the sum of Vulkan command-buffer timestamp durations
per present for **all submitted preview work**. The external layer waits for
query results at presentation, serializing CPU/GPU work; `no_display=1` only
removes overlay drawing. Paired Full-minus-ablation deltas estimate aggregate
frame GPU cost under this instrumentation. They are not named water-pass times,
normal unsynchronized frame latency or production FPS gains, and must not be
mixed with the earlier host timings.

Full-shader median-of-run-medians GPU times are 40.321 ms (shore), 70.181 ms
(open) and 131.574 ms (bank). Their median-of-run-p95 values are 41.191,
70.887 and 134.077 ms. The table gives the median of the three paired
Full-minus-profile differences, with the observed paired range in parentheses;
it is not the subtraction of separately pooled medians.

| Ablation | Shore delta ms | Open delta ms | Bank delta ms |
| --- | ---: | ---: | ---: |
| Constant surface | 38.961 (38.940–39.563) | 68.907 (68.588–69.092) | 123.371 (123.164–126.546) |
| No foam lighting | 7.238 (7.172–8.006) | 12.643 (12.624–13.286) | 14.423 (13.834–16.102) |
| No sky reflection | 14.377 (13.999–14.960) | 24.332 (24.063–24.410) | 29.282 (25.792–30.462) |
| No terrain shadow | 3.091 (2.422–3.493) | 5.075 (4.643–5.081) | 6.419 (3.364–7.323) |
| No seabed detail | 2.587 (2.424–3.086) | 4.760 (4.674–5.157) | 5.564 (3.981–7.139) |
| No caustics | 2.500 (1.611–2.820) | 4.086 (3.967–4.117) | 7.605 (3.834–8.142) |

These developer-only ablations deliberately produce different images. Constant
surface replaces surface radiance while preserving raster depth, base/edge
coverage, fog and alpha; the other profiles remove the named shading work while
retaining the existing capture/producer policy. `no-sky-reflection` removes
clear/foam sky, planar and screen-space evaluation, not their producers.
`no-terrain-shadow` retains independent scene-shadow visibility. Frozen replay
also omits ongoing simulation and producer work. The differences support
investigating reflection and foam shading first in these fixtures, but the
ablations overlap, are not additive, and are not equivalent-quality settings.
No validated Water shading Low mode has been implemented or exposed. A useful
player-facing follow-up still needs a defined image-quality contract, visual
checks and fresh measurements of that actual implementation.

### GPU timing availability and remaining acceptance gaps

`material-profile/layer-inventory.json` records installed layer SHA-256
`82960803fcc6f265516280f9f93bb09771be189192a5a6043472704145848c71`,
source inspection and matching binary disassembly. Its timestamp mask is
`(1ull << timestampValidBits) - 1`. The RTX 4060 graphics queue reports 64 valid
bits; the installed x86-64 variable shift wraps that mask to zero, and all
NVIDIA pilot GPU timestamps were zero. **NVIDIA GPU timing is unavailable**
through this installed layer, not zero-cost rendering. Intel reports 36 valid
bits and produced the positive aggregates used above. The existing NVIDIA host
benchmark and rendering checks remain valid evidence of their respective kinds.

Named per-water-pass GPU timings, an Iris Xe result, production gameplay FPS,
navigating-vessel coverage and complete native bank parity remain unestablished
by this report. The existing baseline failures
also remain explicit: NVIDIA CTest has the one reproduced
`ffref_capture_selfcheck` two-pixel mismatch (89 passed, 21 skipped, 1 failed),
and Intel's full GPU selfcheck has the reproduced 489 failures (8 sky-integral,
1 cosine-integration and 480 paired-FFT zero assertions). Neither diagnostic
series turns those suites into a clean pass. The original performance plan is
unchanged; the evidence above records completed checks and remaining limits.

### Final build and regression checks

The Release game and preview were rebuilt with the existing dependencies. Final
CTest coverage was split into 76 CPU-oriented tests and the remaining 35 tests
under the NVIDIA ICD, covering all 111 registered tests: **89 passed, 21 skipped
and the one unchanged `ffref_capture_selfcheck` baseline failure** described above.
The CPU shader binary passed 45 tests / 692,279 checks, including 737 material
profile checks; all 69 generated SPIR-V programs passed `spirv-val`. The CPU
water-grid selection passed 178 tests / 10,281,643 checks. The durable work-counter
checker then passed all **27 cases on Intel and 27 on NVIDIA** using the current
preview. Exact commands and logs are in `final-checks/summary.txt`,
`gpu-summary.txt`, `run-remaining-checks.sh` and `portable-{intel,nvidia}/report.json`.
The final Ukrainian caption-only adjustment was followed by an overlay/build and
fingerprint check; shader and renderer code were unchanged after these runs.

All native game runs used an isolated user directory and a staged symlink tree
that excludes the installation's real user-data link. The original Zero Hour
installation was checked against the before-run inventory: all 370 entries
retain the same sizes, modification times and content hashes. The original plan
also matches the supplied attachment byte for byte. No dependency or profiler
was installed. Changes remain uncommitted on `feat/linux-water-performance`.
