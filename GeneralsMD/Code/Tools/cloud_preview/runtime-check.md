# Production hazard cloud checks

`../hazard-cloud-check.py` runs the actual `generals` executable against real
hazard objects. It needs an existing executable, prepared retail data root, and
runtime overlay. It installs nothing and uses a separate profile and overlay
for each case. It does not alter the retail root or the normal player profile.
Prepare the game-data root using the replay harness's `--keep` option in
[Checks with game data](../../../../docs/linux.md#checks-with-game-data), then
use its retained `root` directory below. The example's `build-linux/water-validation/root`
is an existing local fixture, not a directory created by the build.

From the repository root:

```sh
python3 GeneralsMD/Code/Tools/hazard-cloud-check.py \
  --generals build-linux/generals \
  --root build-linux/water-validation/root \
  --overlay build-linux/overlay \
  --out build-linux/cloud-preview/production-check \
  --live --crc-pair --gpu-debug
```

The output directory must be new. Add `--prepare-only` to write all fixtures and
commands without starting the game. Use repeated `--case` arguments to select
`palettes`, `nuclear`, `overlap`, `hillside`, `water`, `budget`, or `lifecycle`. Runs are sequential;
reserve the GPU before starting them.

- **Palettes:** toxin, beta, gamma, and nuclear fields, with infantry crossing
  the toxin field. Hazard weapons and damage remain active.
- **Nuclear:** initial radiation particles and a living large radiation owner
  at the same position. Checks owner continuity and the 200-unit hazard radius.
- **Overlap:** two intersecting toxin fields retain distinct owners.
- **Hillside:** toxin and radiation on the Twilight Flame slope use volumetric
  rendering without terrain fallback.
- **Water:** hazards near the Tournament Island shoreline render while modern
  water reports active patches.
- **Budget:** nine overlapping hazards admit eight volumes and retain the
  Classic effect for the extra field.
- **Lifecycle:** a short-lived radiation field expires through its normal
  gameplay lifetime; its surviving particles retain the same identity and
  continue fading without restarting the cloud's age or increasing its density.
- **`--crc-pair`:** repeats the palette fixture in Classic mode and compares
  game-state CRCs at the same logic frame.
- **`--live`:** uses the existing local control socket to change CloudStyle
  through the game console, captures both modes, saves active fields, then
  relaunches the game and checks that the save restores their volumes.

`summary.json` and per-case manifests retain commands, executable and input
hashes, diagnostic records, check results, and screenshot paths. Engine logs and
GPU validation output are retained beside them. The flat cases use the existing
preview map generator, registered only in their private overlays.
Vision aircraft reveal the fixture cells through normal gameplay visibility;
they are immune to the ground hazards. Hiding the displayed shroud alone does
not reveal these cells to hazard FX.

Passing checks establish execution, hazard coverage, mode switching, and logic
consistency. Review the screenshots for appearance, terrain contact, heat haze,
and overlap; the harness does not score image quality or measure performance.
