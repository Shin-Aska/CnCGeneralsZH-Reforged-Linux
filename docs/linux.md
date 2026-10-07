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
