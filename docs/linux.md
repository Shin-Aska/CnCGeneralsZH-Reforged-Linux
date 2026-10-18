

## Modern water contribution

On Linux, Water style selects Modern or Classic live. Modern is the default.
Windows keeps its original water shader identities; macOS uses the legacy SDL
water programs and hides the Linux selector. Material rules are loaded from
`Data/INI/ModernWater.ini` through the game filesystem. Source edits are staged
into the build overlay; invalid or missing game rules use built-in defaults.

Build the standalone editor explicitly: `cmake --build build-linux --target water_preview`.
See [water-preview.md](water-preview.md) for controls, matching, explicit exports
and the distinction between game fallback and preview parse failures.

The reference baseline fails `widechar_check` on hotkey character literals and a
tooltip arrow. The prerequisite text fixes use `u` character literals and a
`\u2192` escape, retaining the same character values while satisfying the
portable source checks. These fixes do not enable new Windows rendering.

## Linux sun shadows

Sun maps are the normal Linux path. The existing shadow preferences still apply.
For validation, `ZH_SUN_SHADOW_MODE=legacy`, `capture-only` and `mapped` select
fallback, capture-only and receiving passes. Unknown values retain fallback.
See [sdl-sun-shadows.md](sdl-sun-shadows.md) for the scene harness and diagnostics.
Windows/macOS compilation and runtime remain unverified for this proposal.

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

Terrain validation currently covers height-map hills and planar elevated emitters.
Cliffs and crossing decks have no dedicated production fixture yet; the single
height per XY sample cannot represent multiple support surfaces within one field.
Exact co-location of same-palette hazard owners is also outside the overlap
fixture: production associates unattached emitters with the nearest matching owner,
so coincident owners cannot be distinguished by that position lookup.

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
