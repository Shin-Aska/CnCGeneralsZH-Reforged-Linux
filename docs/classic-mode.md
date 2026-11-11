# Classic interface: the 3.5.1 HUD in 3.8.0

A plan to bring back this fork's 3.5.1 in-match interface on a 3.8.0 build whenever Classic graphics is on, and to
take Turkiye out of play. Gameplay, AI and balance stay as they are in 3.8.0.

This plan is for this fork only and isn't meant to go upstream: it adds what upstream's maintainer turns down, a
second HUD path to maintain alongside theirs.

Last updated 2026-11-08, against `v3.8.0`.

## Decisions

- **Classic graphics gives the Classic interface.** There is no separate checkbox: when Classic graphics is on,
  the HUD is 3.5.1's; when it is off, it's 3.8.0's. Classic graphics already makes new profiles start in Classic.
- **Turkiye is turned off.** See [Turkiye](#turkiye).
- **Defence reach painting keeps 3.8.0's behaviour** in both modes: only the selected defence, or the one being
  placed, is painted.

## Scope

**In:** what a player sees and clicks during a match, and the army screen, lobby and score screen art that changed
between `v3.5.1` and `v3.8.0`.

**Out:**

- Gameplay, AI, balance, veterancy, handicaps and the five extra colours. They stay as they are in 3.8.0.
- The "One interface" rework and the centred ControlBar Pro bar. Both were already in 3.5.1, so they are the
  baseline, not something to undo. Going back to EA's 2003 bar is a separate, much larger job.
- Hotkeys page and key binding changes. They are settings pages, not the HUD.

## Rules the switch has to follow

- **Client-only.** No file it swaps may be part of the INI checksum, so players on different interfaces can share a
  match, the same as Classic graphics. `ControlBarProReforged.ini`, `ControlBarSchemeReforged.ini`, the `.wnd`
  files and the HUD art are not checksummed. `CommandSetReforged.ini` is (`GameEngine.cpp:1316`), so it must not
  be swapped. The idle-button entries 3.8.0 added to it are tooltip text only, so they can stay loaded and be ignored.
- **Read at startup.** The command bar scheme and the windows load once (`ControlBarScheme.cpp:1089`), and
  `m_classicGraphics` is read at startup too, so a change applies on the next start, as Classic graphics already does.
- **One setting, no new knob.** The HUD follows `TheGlobalData->m_classicGraphics` (`GlobalData.h:234`), the way
  `FXListReforged.ini` already does (`TheFXListStore->setClassicDropsBlocks`, `GameEngine.cpp`). No new checkbox,
  `OptionsCatalog` row or `ZH_*` switch. A test in `test_gameengine.cpp` should pin which HUD files each mode loads.

## Inventory

How hard each change is to revert:

- **Data swap:** load a 3.5.1 copy of the files under the switch.
- **Gate:** the 3.5.1 behaviour still exists; skip the new code under the switch.
- **Restore:** 3.8.0 deleted or rewrote the 3.5.1 code, so it has to come back from `v3.5.1` beside the new code.

| # | Change in 3.8.0 | Commit(s) | Where it lives | Revert | Notes |
|---|---|---|---|---|---|
| 1 | Every side's command bar redrawn in its own style; Turkiye gets its own bar | `3f07be03` | `ReforgedHud{America,China,Gla}CommandBar[Wide].dds`, `...Powers.dds`, mapped-image INIs | Data swap | The art was **replaced in place** (same file names), so 3.5.1's textures must ship under new names, e.g. `ReforgedHud351*.dds`, with their own mapped-image INIs. In 3.5.1 Turkiye used America's bar, and it would again under the switch. |
| 2 | Radar takes the map's shape at a fixed height; black bands removed | `548352ff`, `6c54be98` | `ControlBar.cpp`, `InGameUI.cpp`, `W3DRadar.cpp`, `Radar.h` | Gate | The radar's input mapping is shared; check that clicks still land where they should in both modes. |
| 3 | Four idle buttons (worker, infantry, vehicle, aircraft) at the right of the grid; idle worker button moved there | `548352ff` | `ControlBar.wnd`, `ControlBarProReforged.ini`, `ControlBar.cpp`, `ControlBarCallback.cpp`, `CommandSetReforged.ini` (tooltips) | Data swap + gate | Restore 3.5.1's `ControlBar.wnd` and its idle-worker spot. Keep the command-set tooltip entries; they are checksummed. |
| 4 | General's powers three to a row on the right edge, stepped tray | `548352ff` | `ControlBarProReforged.ini`, `ControlBarScheme.cpp` | Data swap + gate | 3.5.1 grew a column per F key off the console. |
| 5 | Production queue as pictures in a steel tray over the radar | `548352ff` | `InGameUI.cpp` (≈1,100 lines rewritten), `ControlBarScheme.cpp/.h` | **Restore** | 3.5.1's queue on the console over the selection was rewritten, not left beside the new one. This is the largest item. |
| 6 | Superweapon countdowns as pictures in a tray of their own | `548352ff` | `InGameUI.cpp` | **Restore** | Same rewrite as #5. |
| 7 | Side-styled tooltip card with price, build time and stat icons; EA's figure lines and "Power Required" removed | `31527760` | `ControlBarPopupDescription.cpp/.wnd`, `W3DControlBar.cpp`, `ReforgedHudHelpbox.dds`, `Patch.str` and every language's `Generals.str` | **Restore** + data swap | The strings were edited in place in five languages. 3.5.1's text has to be kept under other labels, or the old card would show the new, shorter descriptions. The `Armor`/`Locomotor` additions only read stats for the card; leave them. |
| 8 | Observer console: graphite bar, player lamps, spectator rack with camera lever and stat dial | `8883f46c` | `ControlBarObserver.cpp`, `InGameUI.cpp` (+570), `ReforgedHudObs*.dds` (some replaced in place), `MetaEvent.cpp` | Gate + data swap | New pieces can be skipped; `ReforgedHudObsCommandBar.dds` and `...ObsPlayerWindow.dds` were replaced in place, so they need 3.5.1 copies like #1. |
| 9 | Faction emblems on the army screen, lobby, player card and score screen | `3c1d2d43` | `FactionPicker.cpp`, `GameInfoWindow.cpp`, `PopupPlayerInfo.cpp`, `ScoreScreenGraphs.cpp`, `ReforgedFactionEmblems.dds` | Gate | Additive art, with EA's faces and logos still in the data. |
| 10 | Squares over a factory's white bar for each queued order; seconds removed | `386cf950` | `Drawable.cpp` | Gate | Small. |
| 11 | Defence reach painted for one selected defence only, new look | `0d3cbcc9` | `ReachOverlay.cpp`, `W3DReachOverlay.cpp`, `DX11ReachOverlay.cpp` | Leave | Decided: 3.8.0's behaviour in both modes. |
| 12 | Mouse wheel sizes the guard ring | `e3ec9ff0` | `InGameUI.cpp`, `RadiusDecal.cpp`, `CommandXlat.cpp` | Leave | Input, not HUD layout. |

Already switchable, nothing to do:

- Order lines, target marker and click ring: Options → Interface → Order Lines (`3693c4ab`).
- Aircraft altitude lines: Options checkbox (`9bf42430`).
- Camera shake: Options → Controls.

Fixes to keep in both modes: army screen scrolling (`28e9b6f9`), Contra's powers key and promotion slots
(`0b4b7b8a`), and the hotkeys page rows (`564bbb22`).

## Suggested order

1. **Wiring:** pick the HUD file set from `m_classicGraphics` where the scheme and windows load, with a test.
   It changes nothing yet, because the 3.5.1 files don't exist yet.
2. **Data swap:** ship 3.5.1's `ControlBarProReforged.ini`, `ControlBar.wnd`, `ControlBarPopupDescription.wnd`
   and the replaced textures under new names, and load them under the switch (#1, #3, #4, #8's art). Check that
   the 3.8.0 C++ tolerates the 3.5.1 windows: anything that looks up a window only 3.8.0 has must skip it cleanly.
3. **Gates:** #2, #9, #10, then #8's new pieces.
4. **Restores:** #5 and #6 together, then #7 with its strings.
5. **Turkiye:** separate from the HUD work; see below.
6. **Check:** compare HUD screenshots at 1920×1080, 4:3 and 32:9 against a real `v3.5.1` build for all three sides,
   and do an observer pass and a replay pass. Run a two-player match with one player in Classic graphics and one
   not, and confirm there is no mismatch.

## Upkeep

Every upstream sync that touches `InGameUI.cpp`, `ControlBar*.cpp` or `ControlBarProReforged.ini` can break
the 3.5.1 path, and upstream won't test it. Keeping the restored code in its own files (for example
`InGameUIClassicQueue.cpp`), not as `if` branches through upstream's functions, keeps those merges smaller.

## Turkiye

Decided: off. Unlike the HUD, this can't follow Classic graphics, because Turkiye's data is in the INI checksum:
two players whose games loaded different files can't join each other. It has to be the same on every machine.

- **How it hides:** don't load `TurkiyeReforged.ini` (`GameEngine.cpp:1225`) or `TurkiyeWeaponReforged.ini`
  (`GameEngine.cpp:1191`). `PlayerTemplateStore::postProcessLoad` (`PlayerTemplate.cpp:316`) already drops a
  faction whose start building doesn't exist, and every faction list and the Random draw skip a dropped one.
- **Leftovers to check:** `PlayerTemplateReforged.ini` (`FactionTurkiye`), `CommandSetReforged.ini`,
  `AIDataReforged.ini`, `UpgradeReforged.ini`, `ScienceReforged.ini`, `SpecialPowerReforged.ini`,
  `ObjectCreationListReforged.ini` and `ControlBarProReforged.ini` also name Turkiye. Each reference to an object
  that no longer loads has to fail quietly, not stop the game at startup.
- **Still to decide: the gate.** The simplest is to stop loading the files outright in this fork's builds, so every
  copy of the fork agrees. A setting is the alternative, but a player who changed it could no longer join anyone
  who hadn't, so it doesn't buy much.
- **Side effects:** replays and saved games that have a Turkiye player won't load. Turkiye's HUD art and emblems
  (`ReforgedHudTurkiye*.dds`) become unused.
