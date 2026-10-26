# Linux fork integration of upstream v3.4.2

Date: 26 October 2026.

Local mission-audio work was committed first as `3778135e`
(`perf(audio): preload mission audio to reduce playback stalls`). The merge
joins that commit with upstream `e19c19dc` (v3.4.2), bringing in 68 commits
since the previous upstream base, `f036c761` (v3.3.0). Both histories are retained.

## Changes for players

- Original Generals campaigns alongside Zero Hour, French text, and a full army
  picker in skirmish, LAN and online rooms with configurable random-army pools.
- A Random tab in map selection, map sizes based on the source template and room
  population, wider plateau exits, and more reliable river crossings. Lobby fixes
  preserve random-map selections and prevent a crash when leaving skirmish setup.
- Master volume, music that follows battle intensity, positional sound delay,
  air filtering, Doppler, map reverb, terrain occlusion, blast ducking, and limits
  on overlapping shots and replies. Legacy Sound restores the original mix.
- A performance/quality slider, artillery trajectory fixes, ally request hotkeys,
  Turkiye progression changes, and corrected tank-shell damage against infantry.
- Multiplayer resend/disconnect fixes, stronger handling of incomplete mod data,
  and rendering/device recovery fixes.
- The local audio cache retains up to 128 MiB of encoded mission audio during
  loading. It covers script, ambient, movement and effect sounds, preserves
  random streams, and gives each playback stream its own in-memory file.

The new random-army selection rules can change playback of older replays that
used Random armies. The incoming million-particle GPU backend and instanced
depth-only shadow path target Direct3D 11; they do not add those GPU paths to SDL.
Windows surround output also remains platform-specific.

## Reconciliation

- Retain Linux Modern water, Volumetric clouds, mapped shadows and all four
  reflection modes, including the default Hybrid mode, along with preview tools
  and portable release packaging.
- Retain upstream's quality presets: Low can select Classic clouds. Explicit
  saved cloud selections override the initial preset migration. Water style and
  reflection choices remain independent of the quality slider.
- Apply the upstream Classic-graphics water predicate to shader registration as
  well as construction and live switching, while preserving Linux reflection
  state and caches.
- Combine upstream shadow/smoke collection and lifecycle changes with Linux
  mapped-shadow controls, debug capture and cache restoration.
- Keep upstream depth-format capability selection and safe resize handling,
  preserving fork render targets, water resources and snapshots.
- Keep the preload cache beneath upstream audio processing, so decoding,
  effects, mixing and playback scheduling continue to use the upstream behavior.
- Preserve Linux documentation and combine tests from both histories.
- Repair test integration issues: duplicate filesystem stubs, standard-header
  ordering, portable diagnostic timing, and Unicode literal encoding. Remove a
  pre-existing double deletion in a water test fixture that the incoming memory
  ownership checks exposed.
- Fit the expanded Effects options without overlaps: move Debris Trails into
  Extras and move the Reflections heading and selector down one row. All 211
  named windows and all 56 catalog controls remain present.

## Validation

Build and test logs are retained locally under `build-linux/upstream-sync-3.4.2/`.

- Full GCC 16 Release build passed (`build-validated.log`). Both `water_preview`
  and `cloud_preview` compiled (`previews-build.log`).
- Final CTest: 111 tests, **89 passed, 21 skipped, 1 failed**, in 35.88 seconds
  (`ctest-final.log` and `ctest-final.xml`).
- The sole failure is the previously documented NVIDIA
  `ffref_capture_selfcheck`: two pixels outside its reference tolerance, matching
  the earlier failure. Its tolerance and reference image were not changed.
- Engine, water-grid, water GPU, GPU reset/depth, audio effects/mix policy,
  filesystem, Unicode, options layout, lobby layout and map layout checks passed.
- The initial run found the test cleanup/Unicode and options-overlap issues
  described above. They were repaired, rebuilt and verified by the final run.
- The tracked build-fingerprint manifest and whitespace checks passed; the
  whitespace check accepts the existing CRLF source file's line endings.

Asset-dependent tests, including `test_milesaudiomanager`, skipped because this
build has no game-data directory configured. This sync does not establish
campaign playback, audible output, replay/network runtime compatibility, or
Windows/macOS compatibility. The incoming audio effects and mix-policy tests
use synthetic data and passed independently of those assets.

No software or dependencies were installed. CTest's temporary Git repositories
used `commit.gpgsign=false` only through that test process's environment; normal
Git configuration and real commit signing were unchanged.
