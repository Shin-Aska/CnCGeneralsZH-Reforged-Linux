# Linux play and validation

This guide covers launching the game and repeating this fork's Linux checks. The wider platform history in
[PORTING.md](../PORTING.md) and [CHANGELOG.md](../CHANGELOG.md) is inherited from upstream.

## How to play

Open a terminal in the repository root. After building with `./build-linux.sh Release`, launch:

```sh
./build-linux/ZeroHourReforged/bin/generals
```

If asked for game files, select the installed Zero Hour folder containing `INIZH.big`, with the
original Generals available in `ZH_Generals/` or in its own install folder. In the main menu,
choose **Solo Play → Skirmish**, select a map and your general, add an AI opponent, then click
**Start**. [README.md](../README.md#play) has the complete build-and-play steps.

For an existing development checkout with `build-linux/play-root` prepared, launch against that
file-link farm:

```sh
./build-linux/ZeroHourReforged/bin/generals -root "$PWD/build-linux/play-root"
```

The `./build-linux/play.sh` helper, when present in a local checkout, wraps this command. It is
not tracked by Git, so a fresh clone uses the staged executable directly. The gameplay commands
above open a window for interactive play; the automated checks later in this guide use isolated
user data and run without a visible window.

## Water reflections

**Options → Effects → Reflections → Water reflection** selects **Legacy**, **SSR**,
**Hybrid** or **Path traced (experimental)** for Modern water. **Accept** applies the choice during the current game and
saves it for later sessions; no restart is needed. Choose Modern under
**Graphics → Water style** to use these modes.

Hybrid is the default and combines the improved 1024×1024 planar capture with
screen-space reflections. Legacy preserves the original 512×512 planar rendering.
SSR uses only the current screen, so hidden or off-screen objects can be missing.
Path traced is an optional water-only experiment with approximate materials and incomplete
scene coverage. It builds a scene BVH on the CPU and traces on the GPU, accumulating
a stationary view over 64 frames. Hybrid remains the default.
See the [reflection guide](water-preview.md#reflections) for details and preview controls.

## Hazard clouds

**Options → Graphics → Cloud style**, directly below **Water style**, selects
**Classic** or **Volumetric**. Volumetric is the default; switching applies live and
persists as `CloudStyle = 0` or `CloudStyle = 1` in the player's Options.ini.
The Linux package uses the same renderer as the [isolated preview](cloud-preview.md),
including the approved green toxin, blue beta, purple gamma and amber nuclear colors.
Their transparency defaults are 67%, 77%, 74% and 0%, respectively. These reduce
optical thickness while preserving each hazard's footprint and timing; nuclear
heat distortion and embers remain independent.

The clouds read each live field's actual emission radius and remaining lifetime.
Toxin billows build outward; nuclear haze fades into its full footprint with darker
smoke, rising embers and heat distortion. Terrain samples keep the smoke close to
hills, while elevated emitters stay at their deck height. Visible infantry stir
the smoke without pushing units or changing damage, weapons, AI or simulation state.
The appearance of blast, spray and attached model effects remains authored by their
particle definitions.

The client admits at most eight visible cloud layers within a 192 MiB cache budget.
Fields outside that budget, unknown mod effects, or unavailable GPU resources retain
their Classic hazard appearance. Smoke caches and wakes reset on map/device changes.
The demo's color editor remains available for further tuning; its saved color file
is independent of the normal game's approved defaults.

Validation on 2026-10-18 used the real Linux executable with isolated profiles and
real hazard objects. All four palettes, nuclear owner continuity, overlapping
fields, hillside contact, Modern water, the eight-field budget and short-lived
fallout passed. Live Classic/Volumetric switching persisted, active fields restored
from a save, and the paired appearances produced the same gameplay-state CRC.
The Graphics menu was checked visually at 1280×800. GPU validation reported no
cloud shader or resource errors. These checks establish behavior and compatibility;
they are not a crowded-battle FPS benchmark.
The final approved transparency preset also passed 30 real-game checks covering
all four palettes, Modern water and equal Classic/Volumetric gameplay CRCs. Those
results are in `build-linux/cloud-preview/approved-transparency-game-check/`.

Repeat the checks with
[`hazard-cloud-check.py`](../GeneralsMD/Code/Tools/hazard-cloud-check.py); its
[runtime check guide](../GeneralsMD/Code/Tools/cloud_preview/runtime-check.md)
documents the fixtures. Its `--root` argument needs a prepared file-link root;
[Checks with game data](#checks-with-game-data) explains how to retain one with
`replay-check.sh --keep`. Substitute that path for the guide's local example.
Local results are indexed by
`build-linux/cloud-preview/production-validation-combined.json`, with exact
executable hashes, logs and screenshots in the referenced per-case manifests.
The published stripped executable and matching overlay also passed the four-palette
fixture. Both the installed Linux launcher target and the development package were
updated; their build IDs match the final build. The publisher retains the previous
installed executable and managed overlay in a `.thin-publish-backup.*` directory
inside the package and prints that recovery path after publishing.

## Boundary-aware water

Modern FFT water uses terrain-fitted wave cages at exposed shores. The same incoming FFT
phases drive the mesh and its lighting; fitted ellipsoids redirect motion along the bank.
This adapts [Jeschke et al. (2020), Making Procedural Water Waves Boundary-Aware](https://visualcomputing.ist.ac.at/publications/2020/MPWWBa/)
with equal tangential radii, bounded displacement, and a small material-based contact wash.
It does not simulate overturning breakers or the paper's spray particles.

Cages are enabled by default for supported projected water. Set `ZH_WATER_WAVE_CAGES=0`
to compare with the previous shoreline damping. `ZH_WATER_DEBUG=wave-cage` displays support
in red and the tangential/normal radii in green/blue; `ZH_WATER_DEBUG=cage-wash` displays
wash coverage, contact proximity, and the incoming crest gate. Rivers, enclosed pools,
and authored patch seams retain their existing wave treatment. Missing cage resources
fall back to ordinary terrain damping.

## Upstream synchronization: v3.3.0 (2026-10-24)

Merged upstream tag `v3.3.0` (`f036c761`) into the Linux fork, preserving its four
water reflection modes, Hybrid default, Modern water, Volumetric clouds, mapped
sun shadows, preview tools and Linux packaging. The merge retains upstream's
renderer capability checks, shared hazard-cloud core, shader work and gameplay fixes.

See [the v3.3.0 integration record](upstream-sync-3.3.0.md) for reconciliation decisions,
checks and remaining validation limits. The older records below describe earlier builds.

## Upstream synchronization (2026-10-15)

Merged upstream `e81b9f6a` (v3.0.1, 86 incoming commits) while preserving the Linux
build fixes and Modern/Classic water. The unified options menu retains the water selector.
Water capture timing now uses the 60 Hz logic rate; restoring the old division by 30
makes the new timing regression test fail. The source fingerprint includes the water files.

Upstream v3 rejects saves and replays from earlier versions. Validation used fresh matches;
existing player saves were left intact. The pre-sync main is retained locally at
`codex/before-upstream-sync-20261015` (`90cb6ad1`).

Validated on Ubuntu 26.04.1, GCC/G++ 16.0.1 (20260322), RTX 4060, NVIDIA 595.99.02,
and SDL3 Vulkan. Final code revision: `028f38b1`.

| Check | Result |
|:--|:--|
| Full Release build, existing dependencies | Passed |
| CTest, NVIDIA selected, game-data cache unset | 95 registered: 73 passed, 21 skipped, 1 failed |
| `ffref_capture_selfcheck` | Existing two-pixel mismatch in draw 1, worst pixel `(33, 20)`; tolerance unchanged |
| Archive filesystem check with isolated user data and game assets | Passed: 25,293 files compared byte for byte, 2 tests / 10 checks |
| Two-player AI matches, seeds 0 and 1, 24,000 frames each | Each match, its replay, and a second run agreed; install integrity check passed |
| Fresh Tournament Island, Modern and Classic water | Both rendered successfully; inspected 1920x1080 stills and 60-frame clips |

The replay and visual checks were captured before the final fingerprint/tooltip-text updates;
the full CTest run used the final code. CTest skips include asset-dependent, audio and external
oracle checks; the archive and replay checks above were run separately with assets. Windows,
macOS, and cross-machine network compatibility were not tested. Both water styles report the
same single rejected render-target feedback draw already present in the pre-sync capture.

Logs and capture manifests are retained locally under `build-linux/upstream-sync-20261015/`.
The final executable SHA-256 is
`ea6325ed15a4729f47ff4606ca4573b064543295df5f8e0fd38e834f3fb24888`.

## VS Code launch setup

The repository includes `.vscode/launch.json`, `.vscode/tasks.json` and an extension recommendation
for [Microsoft C/C++](https://code.visualstudio.com/docs/cpp/launch-json-reference). GDB is the
configured Linux debugger. The recommendation does not install the extension or debugger.
The normal launch profiles use VS Code's [built-in terminal launcher](https://github.com/microsoft/vscode-js-debug/blob/main/OPTIONS.md#node-terminal-launch)
to run the native executable directly. They need neither the C/C++ extension nor GDB; the
JavaScript debugger bundled with VS Code supplies the terminal launcher.

1. Open the repository root folder in VS Code.
2. Make sure `build-linux/play-root` contains the prepared file-link farm. It is already present
   in the local checkout used for this validation.
3. In **Run and Debug**, select **Zero Hour: Play (no debugger)** and press **F5** or click the
   green play button. Select **Zero Hour: AI skirmish (no debugger)** to watch an AI match instead.
4. For debugging, have the C/C++ extension and GDB available, then select **Zero Hour: Play (Linux)**
   in **Run and Debug** and press **F5**. **Zero Hour: AI skirmish (Linux)** runs the AI match under GDB.

The two normal launches are also available under **Terminal → Run Task** with the same names.
All four profiles and both launch tasks run `CC=gcc-16 CXX=g++-16 ./build-linux.sh Release`
before launching. The AI entries use seed 0, the `brutal` AI command-line setting, an observer
camera and a 12,000-frame limit.
Each entry passes `-root`, `-quickstart`, `-noshellmap` and `-multiInstance`. They use the staged
executable and overlay, and keep settings, saves and replays under `build-linux/vscode-user`.
Edit the `-root` argument in both `launch.json` and `tasks.json` if your prepared farm is elsewhere.
The normal profiles keep their arguments in `command`; the debugger profiles and process tasks
use `args`.
Release contains debug symbols on this build, with optimized code; for a Debug build, change the
build task's `Release` argument to `Debug`.

For a fresh clone, build first, then follow [Checks with game data](#checks-with-game-data) and
run the replay harness with `--keep`. When `build-linux/play-root` does not exist yet, copy its
kept file-link root into that location, replacing the example temporary path below:

```sh
cp -a /tmp/replay-check.REPLACE/root build-linux/play-root
```

This copies the directories and symbolic links, leaving the installed game archives where they
are. A fresh clone does not contain those archives or the prepared root.

## Verified environment


Validation date: **2026-10-07**. Native Linux x86_64 build; no Wine or Proton.

| Component | Tested version |
|:--|:--|
| Distribution | Ubuntu 26.04.1 LTS (Resolute Raccoon) |
| C and C++ compiler | GCC/G++ 16.0.1 20260322, experimental snapshot |
| Ubuntu compiler package | `16-20260322-1ubuntu1` |
| CMake | 4.2.3 |
| Ninja | 1.13.2 |
| Graphics card | NVIDIA GeForce RTX 4060 |
| NVIDIA driver | 595.91.07 |
| Renderer | SDL3 GPU using Vulkan |
| Secondary GPU comparison | Intel UHD 770, Mesa 26.0.8 |
| Build configuration | Release |
| Game data | Existing Steam install, including custom `.big` archives, with `ZH_Generals/` inside Zero Hour |

This records one machine's results. See [README.md](../README.md#build) for prerequisites and the
build command. Other drivers, distributions, GPUs and release packages need separate checks.

## Build fixes

- CMake now records the static-library dependencies needed by the allocator, platform CD factory
  and renderer hooks. The renderer link probe includes all platform objects.
- The bridge shadow depth-bias value uses the portable `uint32` type instead of the Windows-only
  `DWORD` name. It passes the same 32-bit float representation to the render-state API.
- Hotkey code and its tests use `u'A'`-style character literals, matching the engine's `char16_t` text type.
  `widechar_check` requires `unifdef`; shader validation uses `spirv-val` from `spirv-tools`.

## Validation results

The source checks below were repeated after incorporating the fork's existing `main` at
`45f18bd4` and applying the Linux fixes through `3aa1ee20`.

| Check | Result |
|:--|:--|
| `CC=gcc-16 CXX=g++-16 ./build-linux.sh Release` | Passed; executable and overlay staged |
| CTest, NVIDIA selected, `ZH_GAME_DATA` unset | 97 registered: 74 passed, 22 skipped, 1 failed |
| `test_fontchars`, `test_gameengine`, `widechar_check`, `test_shader_sdl` | Passed |
| `ffref_capture_selfcheck`, NVIDIA | Failed; two pixels outside tolerance in one draw |
| `ffref_capture_selfcheck`, Intel | Passed; intentional error control detected |

The 22 skipped checks need game data, packaging infrastructure or a platform-specific oracle.
The separate gameplay checks below supplied the installed assets; they do not turn those skipped
CTest checks into passes.

| Check with installed assets | Result |
|:--|:--|
| Seed 0, two AI players, 12,000 frames; recording, replay and second run | Passed; all three ended at CRC `0xE9015ED3` |
| AI activity in that match | 53 structures built after frame 0; both armies built units and fought |
| RTX 4060 skirmish rendering | Screenshot at frame 900 inspected; exited successfully at frame 1,000 |
| RTX 4060 main menu | Menu and button text inspected after the opening animation |
| Original install integrity | Passed; 393 entries unchanged by sizes, times and content hashes |

The replay CRC is an observation for this source and installed data, including its custom archives.
It is not a universal expected checksum for other installs or versions.

Test settings, saves and replays used separate user-data directories.

### Remaining NVIDIA comparison failure

`ffref_capture_selfcheck` compares captured draws with the independent, double-precision
fixed-function reference renderer. On the RTX 4060 with driver 595.91.07, `draw_00001.cap` has
**2 pixels outside tolerance out of 1,675 written pixels**. At the reported worst pixel `(33, 20)`,
the GPU gives RGBA `(2, 50, 65, 255)` and the nominal reference gives `(3, 53, 65, 255)`.

The mismatch repeats on NVIDIA. The same self-check passes on the Intel UHD 770 with Mesa 26.0.8;
its deliberate error control also fails as expected. The cause of the NVIDIA/reference difference
has not been established. The tolerance and test expectations have not been changed.

The NVIDIA test run reports this failure alongside successful menu and skirmish rendering. These
checks cover the scenes and draws exercised, rather than every graphics feature.

## Checks with game data

Commands below run from the repository root. Keep proprietary game files outside Git. Create a
data directory containing a `zerohour` link to your install; these links supply assets to the test
harnesses and are not used directly as the engine's runtime root:

```sh
mkdir -p build-linux/game-data
ln -s '/path/to/Command & Conquer Generals - Zero Hour' build-linux/game-data/zerohour
```

For a Steam install, `zerohour/ZH_Generals/` provides the base game. If your base game is installed
separately, arrange a test install with its `ZH_Generals/` pointing to those files. Do not move or
overwrite the original archives.

Configure asset-dependent CTest checks:

```sh
cmake -S GeneralsMD/Code -B build-linux -DZH_GAME_DATA="$PWD/build-linux/game-data"
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
```

Asset-dependent checks include install loading, replays and networking. They may take considerably
longer than the unit tests. Enabling them is not a claim that this session ran every such check.

### Repeat a match and its replay

```sh
GeneralsMD/Code/Tools/replay-check.sh \
  --generals "$PWD/build-linux/generals" \
  --data "$PWD/build-linux/game-data" \
  --seeds 0 --maxframes 12000 --keep
```

The harness builds a temporary file-link farm of the install, records a two-player AI skirmish,
plays its replay and repeats the seed. It compares the world checksum at the stopping frame and
checks that the AI built something. It also hashes the real install before and after; `--keep`
retains its logs, isolated user data and temporary root for inspection.

A passing comparison demonstrates same-machine, same-build determinism. Agreement with Windows,
another CPU architecture or another version requires separate evidence.

### Render a frame on NVIDIA

Use the temporary `root` path printed by the preceding `--keep` run. Set `play_root` below to that
path, or another file-link farm with real directories and individually linked files, rather than
the actual game install. The staged executable discovers its own overlay.

```sh
play_root=/tmp/replay-check.REPLACE/root
mkdir -p build-linux/render-check/user
ZH_UNATTENDED=1 \
ZH_USER_DATA_DIR="$PWD/build-linux/render-check/user" \
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
  ./build-linux/ZeroHourReforged/bin/generals \
    -offscreen -root "$play_root" -quickstart -noshellmap -multiInstance \
    -noFPSLimit -turbo -randommap 0 2 -autoskirmish 2 -aidiff brutal -seed 0 \
    -observer -autocamera 10 -screenshot 900 -maxframes 1000 -logPrefix rendercheck
```

This was the NVIDIA ICD path on the tested Ubuntu install; other systems may use a different path.
The screenshot is written as `sshotNNN.bmp` in the isolated user-data directory. `-offscreen` renders
through the GPU without opening a window; `-headless` runs the simulation without rendering.

For menu screenshots, `-screenshot` counts shell passes. Pace an offscreen menu with
`ZH_OFFSCREEN_HZ=60` so its opening animation has time to finish before capture.

### Compare the GPU self-checks

To reproduce the NVIDIA comparison with captures kept in a separate directory:

```sh
mkdir -p build-linux/gpu-check/nvidia
SDL_VIDEODRIVER=offscreen \
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json \
ZH_USER_DATA_DIR="$PWD/build-linux/gpu-check/nvidia" \
  ./build-linux/ffref_capture_selfcheck
```

On the tested machine, using `/usr/share/vulkan/icd.d/intel_icd.json` with a separate user-data
directory selects the Intel GPU for comparison. Passing on Intel does not resolve the NVIDIA
failure.
