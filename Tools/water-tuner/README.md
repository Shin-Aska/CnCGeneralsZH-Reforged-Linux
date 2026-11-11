# Clean water tuner

From the worktree root, start the game windowed with:

```sh
ZH_WATER_CLEAN=1 ZH_WATER_TUNING=1 ./build-linux/generals -win
```

Then run `python3 Tools/water-tuner/water_tuner.py` and open
<http://127.0.0.1:8765> beside the game. Python 3 needs no extra packages.
The game creates `water-tuning.ini` in its user data directory. Both programs
honour `ZH_USER_DATA_DIR`, `ZH_WATER_TUNING_FILE`, and the Linux XDG data location.
For an explicit file, use the same path in both commands:

```sh
ZH_WATER_CLEAN=1 ZH_WATER_TUNING=1 ZH_WATER_TUNING_FILE=/tmp/water-tuning.ini ./build-linux/generals -win
python3 Tools/water-tuner/water_tuner.py --file /tmp/water-tuning.ini --port 8765
```

Drag sliders or type values; changes save up to five times per second and the game
checks twice per second. Spectrum edits may briefly hitch. Look, chop, depth and
speed, Heading and Shore edits avoid spectrum rebuilds. Mixed seas edits rebuild the seeded spectrum. Keep both programs running; existing tuning
files take precedence over environment start values. Reset buttons use the clean
mode defaults. Presets are named entries in `water-tuning.presets.json` beside the
INI (or `<custom-name>.presets.json` for a custom filename).

The copy box includes spectrum/look/split/speed and the tuning-only start variables
`ZH_WATER_CLEAN_TRAVEL_ANGLE`, `ZH_WATER_CLEAN_HEIGHTS`, `ZH_WATER_CLEAN_CHOP`,
`ZH_WATER_CLEAN_DEPTHS`. Use a fresh tuning file when starting from this line.
With spectrum split, the three height controls set the combined RMS budget;
the spectrum chooses each band's share. Manual INI edits need a newline at the
end of each line; an unfinished final line is ignored until completed.

GPU-free checks: `python3 Tools/water-tuner/test_water_tuner.py`.
The live HTTP check skips if a sandbox forbids local sockets; the HTTP handler,
validation, presets and atomic writes still run without sockets.

## One command

`Tools/water-tuner/play.sh [-loadsave 00000006]` starts the tuner and the game windowed, with
the Steam data root and overlay already set. It uses its own settings folder
`build-linux/play-tune` (seeded once from your normal settings and saves), so your real profile
is untouched. Open <http://127.0.0.1:8765>. Closing the game stops the tuner.

`-loadsave` (like `-replay`) starts the game without its menus, so leaving the match quits the game
instead of returning to the main menu; that is the engine's normal behaviour, not a crash. To move
between maps and the menu, start `play.sh` with no arguments and use Load / Skirmish from the menu.

## Mixed seas and Shore

**Direction mode** defaults to `auto`, using `WaterWaveHeading::Field::prevailing`
from the original map heading solve. It selects near-shore exposure-weighted
normals, falling back to the original southwest `BaseHeading` on symmetric or
open maps. The **Sea direction** angle and compass show this result read-only.
Switch to `manual` to edit/use `travel_angle`; changing it recalculates travel
paths from the new upwind playable edges and reseeds the FFT (a short hitch is
expected). Switching modes preserves the saved manual angle.
Old INIs and presets without `travel_mode` select auto. Startup/copy alias:
`ZH_WATER_CLEAN_TRAVEL_MODE=auto|manual`. If the field is unavailable, auto uses
the configured angle. Auto applies only in clean water.

**Heading** (`heading_follow`, 0–1, default 1) controls how strongly waves follow
the map's wave paths around islands and into bays. It uses the travel-time
field's positive gradient, with no maximum-turn cap; invalid or tiny gradients
keep the global direction. It updates live without reseeding the spectrum.
The startup/copy alias is `ZH_WATER_CLEAN_HEADING_FOLLOW=value`. Older INIs and
presets without this key use 1. Set both `heading_follow=0` and `shore_turn=0`
for the exact unrotated clean surface.

The engine atomically publishes the auto angle to `water-tuning.ini.auto` beside
the tuning INI (also with `ZH_WATER_TUNING_FILE`). The tuner reads it separately,
preserving the manual angle and presets. The existing `dominant` metadata flag
now means the heading field is available (`strength=1`), not the removed shore
estimator's confidence. One log per map preparation names the heading field,
its auto angle and the effective angle. Manual edits do not repeat this log.

Travel is in map XY: 0° right (+x), 90° up (+y), like the in-game minimap.
The second sea appears in orange whenever its share is positive. The configured
default −135° equals 225°. Old `wind_angle` and `ZH_WATER_CLEAN_WIND_ANGLE`
convert to normalized wind + 180°. Valid INI lines apply in order; explicit
`ZH_WATER_CLEAN_TRAVEL_ANGLE` takes precedence over the legacy environment alias.
The internal FFT heading and phase convention are unchanged.

The Mixed seas group adds `sea2_share` (0–0.9, default 0), `sea2_angle`
(−180–180 degrees, default 135), `sea2_wavelength` (100–1500 game units,
default 600), `sea2_spread` (1–40, default 12), and `spread` (0–1, default 0).
The share assigns that fraction of long and medium energy to the second sea;
each component is normalized on its seeded band before powers are mixed. It uses
the same FFT passes as the original clean sea. Higher second-sea spread values
make narrower crests. Main spread blends the normalized directional density
toward equal energy in every direction. Zero share and zero main spread retain
the original clean spectrum exactly. Both sea angles are absolute travel
directions in map XY.

The Shore group adds `shore_turn` (0–1, default 0), `shore_reach` (100–3000 game units,
default 800), `shore_tile` (100–3000 game units, default 300), `shore_sharp`
(1–8, default 3), `shore_max_turn` (0–180 degrees, default 60), and `shore_bands`
(`all`, the default, `medium`, or `long_medium`).
`all` turns long waves, medium waves and fine ripples, including their normals and glints.
Shore turn is an optional extra rotation from the field direction toward the shoreline, fading away by Turn reach. The distance field is independent of the shallow-water depth fades.
Near banks its normal uses local smoothing; farther out it smoothly blends
60/180/500/1000-unit supports, targeting 0.75 times the distance to land.
Confidence falls to zero where opposing banks cancel on channel centrelines.
Maximum turn clamps only the extra shore rotation to ±60° by default;
0 disables this extra rotation. It never limits heading following.
Turn reach stays at 800.
Legacy `shore_depth` INI lines are ignored with one informational log line.
Patch size sets the size of hexagonal regions sharing a local wave direction;
Blend sharpness narrows the transition between nearby directions. These controls
update shader constants without reseeding the spectrum. Both new groups support
the existing group reset, Reset all, saved presets, and copy box. Older INIs and
presets can omit every new key; mixed seas and extra shore pull start disabled, while heading following starts at 1.

The copy line includes `ZH_WATER_CLEAN_SEA2=share,angle,wavelength,spread`,
`ZH_WATER_CLEAN_SPREAD=blend`, `ZH_WATER_CLEAN_SHORE=turn,reach,tile,sharp`, and
`ZH_WATER_CLEAN_SHORE_BANDS=medium|long_medium|all`, plus the separate
`ZH_WATER_CLEAN_SHORE_MAX_TURN=degrees` alias. The four-value shore tuple stays
compatible. These five aliases also work
with clean mode when live tuning is disabled. Existing legacy tuning-only aliases
and `ZH_WATER_CLEAN_TRAVEL_ANGLE` require live tuning. Use a fresh INI to seed
live tuning from the copy line.

For controlled visual comparisons, keep a separate tuning INI and launch profile
for each experiment. Set `ZH_WATER_TUNING_FILE` to the intended INI before
starting either the game or tuner; the copy line records its complete settings.
