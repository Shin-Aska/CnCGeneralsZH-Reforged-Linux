# Production hazard cloud checks

[`hazard-cloud-check.py`](../hazard-cloud-check.py) runs the actual Linux `generals`
executable against real hazard objects. It requires a built executable, its runtime
overlay, Python 3 and a prepared retail file-link root. Each case gets a separate
profile and fixture overlay; the harness does not change the normal player profile
or retail data. Hazard weapons and damage remain active. The harmless preview's
choreography and health protection are not part of these runs.

Run the commands below from the repository root, using the build directory for
the revision under review. A configured Linux Release build with existing
dependencies can build the required targets explicitly:

```sh
cmake --build build-linux --target generals zh_overlay
```

The production game does not require the `cloud_preview` executable or inspection
map. The harness calls the existing preview map generator directly for its private
flat fixtures. See the [cloud guide](../../../../docs/cloud-preview.md) for preview
builds, controls and standalone CPU tests.

## Prepare a retail file-link root

The `--root` argument is a prepared file-link directory, distinct from the raw
installation. Retain one using the existing
[`replay-check.sh`](../replay-check.sh) harness:

```sh
GeneralsMD/Code/Tools/replay-check.sh \
  --generals "$PWD/build-linux/generals" \
  --data "/path/to/game-data" \
  --seeds "0" --keep
```

Replace `/path/to/game-data` with the parent directory containing `zerohour/`.
That directory contains the Zero Hour retail data, with the base Generals data
under `zerohour/ZH_Generals` as expected by this harness. This `--data` layout
differs from the preview executable's `--data`, which names the Zero Hour folder
itself.

The replay harness creates fresh directories and links each retail file into a
temporary root, stages a private overlay, and runs its headless replay checks. It
checks the retail files before and after the run. `--keep` retains the work
directory and prints `kept: /tmp/replay-check.XXXXXX, and the logs ...` (or a path
beneath `TMPDIR`). Use that directory's `root` subdirectory for the cloud checks:

```sh
CLOUD_TEST_ROOT="/tmp/replay-check.REPLACE/root"
```

Replace the example with the printed path. The prepared root and its linked retail
files must remain available for the cloud runs. Inspect the replay harness's own
verdict separately; retaining a directory is not evidence that its checks passed.
Do not use the raw installation as `--root` or copy private game assets into Git.

## Run the production fixtures

Choose a new output directory for each run and run only one GPU workload at a
time. The harness selects the Vulkan backend, so a working Vulkan device/driver
and the built SDL renderer are required even though its windows are offscreen.

```sh
python3 GeneralsMD/Code/Tools/hazard-cloud-check.py \
  --generals build-linux/generals \
  --root "$CLOUD_TEST_ROOT" \
  --overlay build-linux/overlay \
  --out build-linux/cloud-preview/production-check \
  --live --crc-pair --gpu-debug
```

Add `--prepare-only` to create fixtures, isolated profiles and command manifests
without launching the game. A `prepared_only` summary records preparation, not
executed runtime or GPU checks. Both modes require the executable, prepared root
and overlay paths to exist. Use a different output directory for the subsequent
runtime run.

Repeated `--case` arguments select individual fixtures; omitting them runs all
seven. Runs are sequential. The cases cover:

| Case | Expected behavior |
| --- | --- |
| `palettes` | Toxin, beta, gamma and nuclear fields use their palettes; infantry cross the toxin field while damage remains active. |
| `nuclear` | Initial radiation particles and a live large radiation owner share one identity and a 200-unit hazard radius. |
| `overlap` | Two intersecting toxin fields, whose owners are 120 units apart, keep distinct volume layers. |
| `hillside` | Toxin and radiation follow the Twilight Flame slope through the volumetric path. |
| `water` | Hazards near the Tournament Island shoreline render while Modern water reports active patches. |
| `budget` | Nine overlapping hazards admit eight volumes and retain Classic fallback for the extra field. |
| `lifecycle` | Short-lived radiation expires normally; surviving particles keep the same identity without restarting cloud formation. |

`--crc-pair` repeats the palette fixture in Classic mode and compares game-state
CRCs at the same logic frame. It tests appearance independence on this build;
it does not establish determinism across different platforms or executables.

`--live` uses the local control socket to switch CloudStyle through the game
console, captures both appearances, saves active fields, then relaunches and checks
save restoration. Its manifests also record the mode persisted in Options.ini.
The harness chooses a local port automatically; `--port` selects one explicitly.
`--timeout` bounds unattended runs and control-socket startup, defaulting to 180
seconds. Individual live actions also have their own shorter timeouts.

## Inspect results and remaining limits

`summary.json` and per-case `manifest.json` files retain commands, executable/input
hashes, diagnostic records, checks and screenshot paths. Engine logs and GPU
validation output remain beside them. Treat failed checks and missing-assets/GPU
failures as unresolved; inspect the corresponding logs before rerunning in a new
directory.

Flat fixtures register their generated map only in their private overlays.
Vision aircraft reveal fixture cells through normal gameplay visibility and are
immune to the ground hazards. Hiding the displayed shroud alone does not reveal
the cells to hazard FX.

Review captures for appearance, terrain contact, field overlap, nuclear heat and
embers, and infantry wakes. Exercise hidden/stealthed infantry and camera movement
when reviewing those visibility paths. Check Classic fallback and live switching
with both Water styles. The height-map fixture does not cover cliffs or crossing
decks; the overlap fixture does not cover exactly coincident same-palette owners.
Resource allocation failure is not injected by these fixtures.

Passing checks establish the listed runtime behaviors and same-build CRC
consistency. They do not score image quality or measure full-battle performance.
For performance comparisons, record GPU/driver, resolution, map/save, camera,
graphics settings, build/executable hashes and frame-time distributions under
comparable conditions. Keep private assets, saves and generated captures outside
the proposed commits.
