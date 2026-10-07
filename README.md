# Zero Hour Reforged Linux

A Linux-focused fork of [Zero Hour Reforged](https://github.com/olcayseygan/CnCGeneralsZH-Reforged),
which rebuilds Command & Conquer: Generals Zero Hour from EA's released source. The native 64-bit
Linux game uses SDL3 and Vulkan and reads the Generals and Zero Hour files you already own.

[![last commit](https://img.shields.io/github/last-commit/Shin-Aska/CnCGeneralsZH-Reforged-Linux/main?style=for-the-badge)](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/commits/main)
[![license](https://img.shields.io/badge/license-GPL--3.0%20%2B%20EA%20terms-0d1117?style=for-the-badge)](LICENSE.md)

[Build](#build) · [Play](#play) · [Tests](#tests) · [Linux validation](docs/linux.md) ·
[Changes](CHANGELOG.md) · [Contributing](CONTRIBUTING.md)

This fork's development and verification focus on Linux. Windows and macOS sources and historical
porting notes are inherited from upstream. The current tested machine runs **Ubuntu 26.04.1 LTS,
GCC 16 and an NVIDIA GeForce RTX 4060**. See [the validation record](docs/linux.md#verified-environment)
for exact versions, checks and limitations.

The fork inherits upstream's gameplay, AI, interface and renderer improvements. Their history is in
[CHANGELOG.md](CHANGELOG.md), and [PORTING.md](PORTING.md) describes the engine's platform layer.
Game data and upscaled art are not included; you need your own installed copy of Zero Hour and
the original Generals.

**Play after building:** open a terminal in the repository folder and run
`./build-linux/ZeroHourReforged/bin/generals`. Then choose **Solo Play → Skirmish** to start a game
against the AI. [The steps below](#play) cover choosing your game files and starting a match.

## Build

### Ubuntu prerequisites

The Ubuntu 26.04 build uses these packages. Install missing prerequisites before building:

```sh
sudo apt-get install g++ gcc-16 g++-16 cmake ninja-build git curl unzip xz-utils make pkg-config python3 \
  libx11-dev libxext-dev libxcursor-dev libxi-dev libxfixes-dev libxrandr-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libegl-dev libdrm-dev libgbm-dev libvulkan-dev
```

For validation, install `unifdef` for `widechar_check` and `spirv-tools` for the shader validator:

```sh
sudo apt-get install unifdef spirv-tools
```

The machine also needs a working Vulkan graphics driver. The tested NVIDIA driver is **595.91.07**.
The build script checks tools and development headers and prints a package command when something
is missing. It does not install system packages.

### Clone and compile

```sh
git clone https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux.git
cd CnCGeneralsZH-Reforged-Linux
CC=gcc-16 CXX=g++-16 ./build-linux.sh Release
```

The script fetches pinned third-party sources, configures with Ninja, builds and stages the game at
`build-linux/ZeroHourReforged/bin/generals`. The tested GCC 16 package is an experimental snapshot,
`16.0.1 20260322`; GCC 16 is the compiler used for this validation, not an enforced version requirement.
Without `CC` and `CXX`, a fresh CMake configuration selects the system's default compiler.

CMake retains its compiler choice in an existing build tree. Changing `CC` and `CXX` alone does
not switch an already configured tree. Use a new build directory through the git-ignored
`build.local.sh` when changing compilers; it can also override CMake and the generator.

Other build commands:

```sh
./build-linux.sh Debug              # Debug configuration
./build-linux.sh Release generals   # build only the executable; does not stage it
./build-linux.sh Release test       # build, test, then stage if the tests pass
```

The staged overlay is a link into the build tree, so this folder is for local use. The inherited
packaging scripts are described in [PORTING.md](PORTING.md#linux-packages); release packages and
other distributions have not been validated in this fork's Ubuntu/NVIDIA check.

## Play

### Launch the game

1. Install your own copy of **Generals and Zero Hour**, for example through Steam.
2. Open a terminal in this repository's root folder, the folder containing `build-linux.sh`.
3. Build Release if you have not built it yet, then start the staged executable:

```sh
CC=gcc-16 CXX=g++-16 ./build-linux.sh Release
./build-linux/ZeroHourReforged/bin/generals
```

On later launches, run only the second command. It opens the game window and main menu for
keyboard and mouse play.

On the first launch, the game searches known install locations, including Steam libraries. If
the folder chooser appears, select the **Zero Hour install folder containing `INIZH.big`**.
For a Steam install, this is normally `steamapps/common/Command & Conquer Generals - Zero Hour/`.
Keep the original Generals files available in `ZH_Generals/` inside that folder or beside it.
If the game asks separately for Generals, select its install folder too. Your selections are saved
for later launches.

### Start a skirmish

1. In the main menu, click **Solo Play**, then **Skirmish**.
2. Choose a map and your faction/general. Add at least one computer opponent and choose its difficulty.
3. Click **Start** to load the match.
4. Left-click to select units and buildings; right-click to issue movement and attack orders.

For a campaign, choose a faction's campaign under **Solo Play**. Use **Options** from the main menu
to adjust display, sound and controls before starting.

### Launch a prepared development install

If you already have a file-link farm at `build-linux/play-root`, as used in this fork's local
validation, you can select it explicitly:

```sh
./build-linux/ZeroHourReforged/bin/generals -root "$PWD/build-linux/play-root"
```

Some local development checkouts also have `./build-linux/play.sh` wrapping that command. That helper
is generated locally under the ignored build directory and is not included in a fresh clone.
The executable command above works without it. [The validation guide](docs/linux.md#checks-with-game-data)
explains how the harness prepares a file-link farm.

### Saves and game files

The engine reads the installed archives and searches the fork's staged overlay first. It refuses
writes to the install root. Settings, saves and replays go to
`$XDG_DATA_HOME/Command and Conquer Generals Zero Hour Data`, or
`~/.local/share/Command and Conquer Generals Zero Hour Data` when `XDG_DATA_HOME` is unset.
`ZH_USER_DATA_DIR` overrides that location.

For automated development checks, use a file-link farm and separate test user data as described in
[the Linux validation guide](docs/linux.md#checks-with-game-data). Those checks verify the original
install's contents before and after running.

Upscaled art is optional. Put existing `Reforged*.big` archives in `ReforgedArt/` inside the user
data folder to use it; without them the game uses the original textures.

## Tests

Build and stage first, then run the tests separately:

```sh
ctest --test-dir build-linux --output-on-failure
```

The focused checks for the build fixes are:

```sh
ctest --test-dir build-linux -R '^(test_fontchars|test_gameengine|widechar_check)$' --output-on-failure
```

Tests that require game assets skip unless `ZH_GAME_DATA` is configured. GPU and packaging checks
also have their own environment requirements. [docs/linux.md](docs/linux.md) explains how to supply
game data and repeat the skirmish, replay and screenshot checks.

**Known NVIDIA check failure:** `ffref_capture_selfcheck` reports two pixels outside its reference
tolerance in one captured draw on the RTX 4060 with driver 595.91.07. The same check passes on this
machine's Intel UHD 770. The main menu and a rendered skirmish were checked on NVIDIA, and a longer
headless match replayed consistently. The full NVIDIA test run is therefore not entirely passing;
`./build-linux.sh Release test` currently stops before staging when it reaches that failure.

Replay checks establish repeatability on the tested machine and build. Cross-platform multiplayer,
other GPUs, other distributions and Linux release packages were not checked in this session.

## Contributing and credits

Linux fixes and reproducible reports are welcome. Include your distribution, compiler, GPU,
driver version and the relevant build or test output. Follow [CONTRIBUTING.md](CONTRIBUTING.md)
for commit messages and validation.

Electronic Arts released the Generals and Zero Hour source in 2025. This fork builds on
[olcayseygan/CnCGeneralsZH-Reforged](https://github.com/olcayseygan/CnCGeneralsZH-Reforged), including
the macOS, Linux and Windows ARM64 port by İlyas Akın (ilyasakin). Work inherited from
[TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode) remains credited
in its source and commits. [NOTICE.md](NOTICE.md) lists third-party notices and
[LICENSE.md](LICENSE.md) contains GPLv3 and EA's additional terms.
