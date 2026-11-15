# Reproducing the weather evidence

The archive has two uses: verify the completed measurements without launching the game, or collect a new private comparison on a qualified host. The frozen original-host admission records absolute paths and exact executable/helper hashes. It is historical evidence, not a portable configuration to copy blindly.

## Verify the archived result anywhere

From the repository root, with existing Python 3:

```sh
python3 docs/reference/weather-enhancements-2026-11-15/reproduction/check_export.py
```

This requires no game, retail assets, GPU, network access or installation. It verifies the evidence inventory, original admission/collector/source identities, all 27 artifact-labelled pairs, profile/log/CSV hashes, 253,800 consecutive finite rows and the exact 72 exported distributions. It uses the archived production-helper phase series, not a second implementation of the rain schedule. Mean uses all values, median uses the central value(s), and p95 uses nearest rank `ceil(.95*n)`. No outlier is removed. The [all-repeat table](../evidence/summary/all-repeats.md) and explicit `artifact_label` column distinguish baseline-on from enhanced-on even though both have Weather on.

To regenerate the phase series from production code with an existing C++17 compiler:

```sh
c++ -std=c++17 -O2 \
  -Idocs/reference/weather-enhancements-2026-11-15/evidence/source-snapshot \
  docs/reference/weather-enhancements-2026-11-15/reproduction/rain_series.cpp \
  -o /tmp/weather-rain-series
/tmp/weather-rain-series 1 25200 2096055520 > /tmp/weather-rain-phase-series.csv
cmp /tmp/weather-rain-phase-series.csv \
  docs/reference/weather-enhancements-2026-11-15/evidence/summary/rain-phase-series.csv
```

The frozen measured-v4 production header is SHA `9cffe39d0a62a874acd3499c908cc02483859b0b9bb31777fae05d36bcd23d82`. It is retained under `evidence/source-snapshot` so verification survives later schedule changes. A different compiler can produce a different ELF hash while still producing the identical schedule CSV. The original-host strict summarizer intentionally requires the originally admitted helper hashes; it is not made permissive to accommodate that difference. A new collection needs a new admission inventory with its actual helper hashes.

The current production window helper is built by the existing CMake target `test_weather_particle_math`; `--rain-window SECONDS WEATHER_SEED` returns its exact wet/plateau/fade metadata. The [startup follow-up](../../weather-rain-startup-2026-11-15/README.md) changes the seed and opening phase; current helpers must not relabel these archived v4 rows. CTest without arguments still runs the meaningful arithmetic tests. No additional game flag or environment variable is introduced.

## Prepare an isolated actual-game check

Use your own read-only retail root and an existing built package overlay. Do not use the installed game's writable preferences or alter its retail data. Set paths explicitly; the runner refuses an existing output directory.

```sh
REPRO="$PWD/docs/reference/weather-enhancements-2026-11-15/reproduction"
GEN_BIN="$PWD/build-linux/weather-enhancements-evidence/candidate-v4/generals"
GAME_ROOT='/path/to/your/Command & Conquer Generals - Zero Hour'
BASE_OVERLAY="$PWD/build-linux/weather-enhancements-evidence/candidate-v4/overlay"
RUN_OUT="$PWD/build-linux/weather-local-pond-check"
python3 "$REPRO/run_weather.py" \
  --candidate "$GEN_BIN" --overlay "$BASE_OVERLAY" --root "$GAME_ROOT" \
  --out "$RUN_OUT" --kind rain --variant on --audio --idle-ai \
  --shot 600 --last 720 --width 1280 --height 720 \
  --freecam 640 360 240 90 -60 --run
```

The sample uses the successful near-topdown flat-pond pose. It runs the real null audio mixer and saves a decoded screenshot with an endpoint CRC; physical listening is a separate check. On this NVIDIA host we passed `--vulkan-icd /usr/share/vulkan/icd.d/nvidia_icd.json`. Other hosts must select their actual ICD or omit that option and use the inherited `VK_DRIVER_FILES`/normal Vulkan loader. Do not copy the NVIDIA path onto an AMD/Intel host. No package installation is part of these commands.

`--night`, `--atmosphere off`, `--maplight off`, and `--variant classic`, `classic-off`, `authored`, `authored-off`, or `off` select the recorded boundary fixtures. Existing GDB scripts in [probes](probes) are untimed qualification examples with original paths/symbols; adapt their paths to your private outputs and smoke one before batching. They must not run during primary timing. The actual HDR case has its own [launch/result/profile/probe](../evidence/qualification/v4-hdr-smoke); its prepared pre-HDR manifest is not the final launch configuration.

## Admit a new timing collection

Keep the original archived JSON/rows immutable. Build or retain separate baseline and final executables and dereferenced package overlays. Our accepted pre-edit binary is not reconstructed from the different top-level build executable; its SHA is recorded in the baseline manifest. New builds are new identities and require new qualification.

1. Record the current source revision/patch, executable hashes, complete effective overlay inventory, runtime asset inventory, hardware, compiler/helper identities and private profile. Fully hash the retail files at admission and again after collection; never export Registry/CD keys or retail art/audio. The runner's `inventory_digest()` defines the overlay hash serialization. `retail-assets.json` shows the content-hash/size schema.
2. Build the current production `rain-series` and `test_weather_particle_math` helpers. Pin their actual hashes, the production header and `rain_series.cpp`. Probe actual `m_weatherRainSeed` on the exact generated Rain map; link that observation to the map SHA, match seed, final executable SHA and helper SHA. The archived [seed probe](probes/weather-v4-seed.gdb) and [actual proof](../evidence/qualification/admission-visible-final-rain/run.log) show the boundary. Do not assume another map/river fixture or match has the same seed. Our pond's v4 map-only seed was 2096055520; the startup follow-up additionally mixes the match seed.
3. Use `run_weather.py` **without `--run`**, `--repeat 1`, `--sha256 ACTUAL_SHA`, and the intended `--label` to prepare each of the nine full endpoint profiles. Rain uses first 601/last 25200, Sand/Snow first 601/last 2400, all at 1920×1080 with `--audio --idle-ai`. Read the generated manifest and run its command after replacing `-offscreen` with existing `-headless` and adding `-noaudio`; remove the timing environment keys and `ZH_ALLOW_AUDIO` for that admission-only headless run. Save this actual modified command/environment, new engine log and return code separately from the prepared manifest. Require all three labels to have the same CRC/frame per kind. The archived [headless results](../evidence/headless-admission-results.json) and `historical/prepare-weather-admission.py` record the exact original procedure.
4. Qualify the ordinary visible observer with actual world pixels, shroud 255 and live weather. Compare actual visible baseline/final CRC at the same short endpoint and match a corresponding headless short run. Our short endpoint is frame 820, CRC A64E68FC. Capture-clock time must be checked at actual current draw, not screenshot-request entry. The qualified formula is `floor((logic_frame-1)*1000/60)/1000`. Do not use `-takeover`: its human-seat fog changes visibility. Release ignores debug-only `-noshroud`. `--idle-ai` uses one cash, not the launcher's ignored zero override.
5. Create a **new local admission JSON**, following the archived schema, with `qualified: true` only after those checks. Replace all host paths, executable/helper/retail hashes and measured CRC/frame/seed proof, retaining the exact runner/generator identities and case settings actually used. `cases` must bind each map/scenario/common-profile/window/resolution/audio/idle-AI identity. `artifacts` must bind baseline-on/enhanced-on/enhanced-off to actual executable hashes and option variants. `rain_seed_proof` must point at the actual qualified final log/manifest and their hashes. `vulkan_icd` must match your explicit loader choice (null if using normal discovery). Freeze this local JSON before sampling. The archived `historical/freeze-weather-admission.py` documents how the original checks populated each field; its original paths, log-recovery code and seed constant are historical, and must not be executed unchanged on another host.

The strict collector enforces the frozen identities, complete windows, exact expected CRC/frame, fresh log provenance, unchanged executable and overlay, and successful timer completion. Do not relax those gates merely to admit a different map/profile or failing run. Keep rejected runs outside the admitted matrix and investigate mismatches.

## Collect and export the paired matrix

For each kind and each of the three repeats, pass the **new local inventory** and exact label, executable, overlay and variant. This example is Rain final on; use `--last 2400` for Sand/Snow:

```sh
LOCAL_ADMISSION='/absolute/path/to/new-local-admission.json'
GEN_SHA='actual-sha256-of-this-final-executable'
python3 "$REPRO/run_weather.py" \
  --candidate "$GEN_BIN" --overlay "$BASE_OVERLAY" --root "$GAME_ROOT" \
  --out '/new/private/matrix/rain-enhanced-on-r1' \
  --kind rain --variant on --repeat 1 --first 601 --last 25200 \
  --width 1920 --height 1080 --audio --idle-ai --sha256 "$GEN_SHA" \
  --admission "$LOCAL_ADMISSION" --label enhanced-on --run
```

Pass `--vulkan-icd` if your admission pins one. Repeat 1 order is baseline-on, enhanced-on, enhanced-off; repeat 2 is off, on, baseline; repeat 3 is on, baseline, off. Do not overlap GPU/audio/debugger work or heavy CPU/I/O with sampling. The historical sequential batch script and actual [run order](../evidence/primary-run-order.json) are retained. Rain's 410-second sampling window is simulated time, not wall time, and includes a complete later slot. Inert screenshot arming occurs beyond quit; no screenshot/readback is taken in a timed run. `ZH_GPU_TIMING_SYNC=0` keeps synchronization diagnostics separate.

Finally run the strict exporter against the **new** matrix/inventory and helpers:

```sh
python3 "$REPRO/summarize_weather.py" '/new/private/matrix' \
  --admission "$LOCAL_ADMISSION" \
  --helper '/path/to/your/test_weather_particle_math' \
  --series-helper '/path/to/your/rain-series' --out '/new/private/summary'
```

It requires all 27 unique accepted repeats, the actual map-bound rain seed and admitted helper/source hashes, and refuses missing/duplicate pairs or changed raw CSV. Export all repeats and their raw rows, active/fade/dry costs, the complete-slot occupancy, hardware/settings/commands and postflight hashes. Report native present intervals honestly; never call them hardware GPU timestamps/FPS or average dry time into an active-weather improvement claim.
