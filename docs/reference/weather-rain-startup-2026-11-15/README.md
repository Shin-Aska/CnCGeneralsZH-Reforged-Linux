# Randomized rain at map startup

Enhanced rain can now start clear or already rainy. Previously, every rain-capable map began its first spell immediately. The opening now samples a seeded point in the weather cycle, including its longer dry interval, so clear starts are more common. Later spells retain their 48–56-second duration, 6–8-second fades and roughly 138–178-second dry breaks.

Both the map and the match's immutable seed influence the schedule. A new match can have a different opening on the same map; a fixed `-seed` or recorded replay seed reproduces it. Reading that seed does not advance the gameplay or client random streams. The weather cache also detects a changed match seed when a map allocation is reused. Authored map precipitation retains its existing behavior and takes precedence.

The tested Linux executable is SHA `7a40e7d9e5a8630ef6021bbfa2cd2bb02d950dc4eadd6e685f23ed9a6910c50a`, source fingerprint `83496524/4804`. Its exact bytes are staged at `build-linux/ZeroHourReforged/bin/generals`; all 262 effective overlay files match the preceding package. The [verification record](evidence/verification.json) binds the tested source patch and binary. The source is committed locally on `main` as `0529ecef` (audio), `e12ff8b8` (weather, including randomized startup) and `f91069a2` (haze/lighting). Their combined code diff from the recorded base `9770afa0` exactly matches the tested patch. No push, upstream submission or installed-game update has been made.

## Checks

The Release build passed. Focused CTest passed 4/4: weather particle arithmetic, build fingerprint, fingerprint manifest and manifest self-check. Arithmetic covers 1,024 match seeds with both clear and rainy openings, fixed-seed reproducibility, continuous fades, dry-clock progression and later storm boundaries.

Five isolated native Vulkan runs passed with the real null audio mixer enabled, at 1280×720 on the same pond fixture. The opening pictures are at logic frame 60, approximately one simulated second; the later picture is at frame 1,800, approximately 30 seconds.

| Case | Observation | Gameplay checksum at quit |
| --- | --- | --- |
| Match seed 7 | [Clear opening](evidence/dry-7/picture-0.png) | `71E74433`, frame 360 |
| Match seed 5 | [Rainy opening](evidence/wet-5/picture-0.png) | `D324423C`, frame 360 |
| Match seed 7, repeated | [Identical clear image](evidence/dry-7-repeat/picture-0.png), matching image SHA | `71E74433`, frame 360 |
| Match seed 11 | [Rain present by 30 seconds](evidence/later-wet-11/picture-0.png); helper predicts a clear opening, rain beginning at 18.34 seconds and full strength at 24.63 seconds | `721973BC`, frame 1,920 |
| Previous v4, match seed 7 | [Rainy opening](evidence/v4-seed-7/picture-0.png), same gameplay checksum as current build | `71E74433`, frame 360 |

The seed-11 opening prediction is from the production helper, rather than an observed opening screenshot. Each directory retains the actual launch command/environment, result, log, generated map and profile. No debugger or timing collection ran in these checks. Windows source was updated but Windows build/runtime checks remain unrun.

## Benchmark provenance

The [27-run weather benchmark](../weather-enhancements-2026-11-15/performance.md) still measures the preceding v4 binary, SHA `917e182cbd5f9f7fff24a10c2331996cbeb26f25be82d05d779f5e028e8ef55c`. Its 253,800 raw intervals and original 445 evidence files remain byte-identical. The exact old schedule header is now frozen beside that evidence, and its checker/regeneration command uses that snapshot. Archive inventories were refreshed to include the snapshot and documentation changes.

The full timing matrix was **not repeated** for this startup-only change. Those numbers must not be labelled as measurements of the new executable. A new upstream timing collection must bind its current map, match seed, mixed weather seed and phase boundaries in a fresh admission. The new helper command `build-linux/test_weather_particle_math --rain-opening 2096055520 11` reports the fixture's mixed seed; `--rain-window SECONDS MIXED_SEED` gives its current phase metadata.

## Reproduce a startup check

Use the [existing private-fixture preparation command](../weather-enhancements-2026-11-15/reproduction/README.md#prepare-an-isolated-actual-game-check) with the new executable and without `--run`. For these checks, use `--shot 60 --last 240 --width 1280 --height 720`; retain `--audio --idle-ai` and the documented camera/ICD. Set the retail root and output directory to your own existing paths. Then run:

```sh
python3 docs/reference/weather-rain-startup-2026-11-15/reproduction/check_startup.py \
  /absolute/path/to/prepared-private-fixture --seed 7
```

The wrapper records and executes the exact modified `-seed` command, refuses timing fixtures or previously executed output, and checks exit status, screenshot, gameplay checksum presence and executable identity. Inspect the picture to classify clear/rainy presentation. For the seed-11 later picture, prepare a separate fixture with `--shot 1800 --last 1800`. Fixed-seed equality is checked only between matching scenarios; different match seeds need not have equal gameplay checksums.

The water-wave limitation is unchanged: rain's expanding contact rings sit on the authored water surface, including a river's slope. They do not move vertically with each animated FFT wave, so a crest or trough can intersect a ring. This describes the surface rings, not the falling rain.
