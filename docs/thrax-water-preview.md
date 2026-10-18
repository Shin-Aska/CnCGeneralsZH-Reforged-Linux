# Dr Thrax water material proposal

> Historical investigation and validation notes for the original isolated experiment.
> The maintained tool is now [Water Preview](water-preview.md), which uses the shared
> production material shader and INI rules. The build/run, persistence and prospective
> integration descriptions below record the earlier prototype and no longer describe
> the maintained implementation. Existing captures and experimental sources are preserved.

This standalone native viewer explores contaminated **Modern** water. It uses the game's
POSIX renderer, three-cascade GPU FFT, displacement and modern water shader. The proposal
adds olive-green suspended material and sparse small surface bubbles that grow, pop and
leave a fading ripple. The bubbles are procedural surface shading, not a volumetric fluid
or gas simulation.

## Why the two water styles differ

The locally inspected save named **Dr Thrax Stage** references `GC_ChemGeneral.map`.
The retail `MapsZH.big` member `Maps\GC_ChemGeneral\map.ini` deliberately specifies:

```ini
TransparentWaterMinOpacity = 0.8
TransparentWaterDepth = 2.0
StandingWaterTexture = TSWater03.tga
StandingWaterColor = R:50 G:75 B:50
RadarWaterColor = R:100 G:196 B:100
```

Classic water multiplies the authored texture by the water vertex colour. Modern water
uses the texture's alpha but gets its body colour from the blue optical presets in
`WaterOpticalModel.h`. Its vertex colour also carries obstacle attenuation metadata,
so restoring the tint by multiplying Modern water by that vertex colour would be wrong.

The green material in this viewer is an artistic proposal inspired by those map settings,
not an exact recreation of the classic texture. It retains the modern waves, lighting,
reflections and refraction. There are no terrain, units, HUD or game-data dependencies.

The selected preset is **#2D3A0B** (display RGB **45, 58, 11**), **67% brightness**,
and **80% pollution**, from the user's color-panel reference. These are the viewer's
startup and Reset color defaults. The displayed RGB percentages are rounded; the hex
code supplies the precise channel values. Game integration has not yet been applied.

## Build and run

With this checkout's existing configured build and dependencies:

```sh
cmake --build build-linux --target thrax_water_preview -j 4
./build-linux/thrax_water_preview
./build-linux/thrax_water_preview --help
```

Building this target does not install packages or replace the game executable. The viewer
does not read or write saves, game settings or the retail install.

Press **1** for clean water, **2** for the proposal, or **3** for a split comparison
(clean on the left). **B** toggles bubbles, **Space** pauses, dragging outside the panel
or the arrow keys orbits the camera, and the wheel or **+ / -** zooms. **[ / ]** adjust contamination,
**R** resets the camera, **S** saves a PNG in the working directory, and **Escape** exits.

The visible color panel changes the water immediately: drag **R / G / B** for display-space
color channels (0–100%), **Brightness** (10–100%, default 67%) or **Pollution** (0–100%,
default 80%). The swatch shows the tint's hex code. **H** hides or shows the panel, and
**Reset color** restores the proposal's defaults.

Click **Save color** to keep the panel settings for future windowed launches. Until saved,
changes apply only to the current session. This writes only the preview's `colors.ini`
under SDL's per-user preferences directory for `CnCGeneralsZH-Reforged/ThraxWaterPreview`;
it does not change game settings. Offscreen captures ignore saved presets.

For a repeatable GPU capture without opening a window:

```sh
./build-linux/thrax_water_preview --offscreen --time 12 --mode split \
  --screenshot /tmp/thrax-water-split.png
```

The preset uses the map's depth 2 / minimum opacity 0.8, with a synthetic flat submerged
bed and controlled lighting. It is a water-material study, not a reproduction of the map.
`--no-bubbles` isolates the tint and `--strength 0` removes the proposed material.
Use `--color RRGGBB`, `--brightness 0.1..1` and `--strength 0..1` to set capture values;
`--hide-controls` hides the color panel.
`--frames 180` runs three seconds of simulation with a deterministic 60 Hz capture clock.

## Isolation and eventual integration

`Tools/ThraxWaterPreviewShader.cpp` compiles a private copy of the production shader
generator into this executable. `Tools/ThraxWaterPollution.h` adds material hooks to the
generated modern-water text. Its CMake target links the existing device archive with
explicit renderer dependencies, excluding the ordinary shader archive. The ordinary game
target uses the unmodified shader library.
Hook mismatches fail shader creation rather than silently omitting the proposal.

A gameplay integration would need an explicit authored water-material parameter, carried
from map configuration to the modern renderer, plus validation on clear-water maps,
shorelines, rivers and saves. Matching the existing tint alone is a contained material
change; bubbles are a separate visual choice. No map-name detection or new gameplay
simulation is needed for the proposed effect.

The retail `MD_USA05` map has the same active texture, standing-water colour, depth,
opacity and radar-water colour as `GC_ChemGeneral`. Those legacy values therefore do not
uniquely identify Dr Thrax. To limit this selected proposal to his challenge, add explicit
optional modern-material fields to that map's `WaterTransparency` override, with pollution
disabled in the global defaults. The engine already loads map overrides (including the
original map path on save restoration) and removes them when resetting the match.

## Local validation (2026-10-18)

- Built the dedicated target using the existing GCC 16 / SDL3 / Vulkan dependencies.
- Captured clean/polluted split and polluted-with/without-bubbles at the same time and camera.
- Ran 180 offscreen animated frames: 180 production FFT updates, no refused draws.
- Ran 120 windowed frames: all 120 presented to a visible drawable, no refused draws.
- Inspected the captures; bubble changes occupy small scattered regions. The 1280×800
  paired capture has 2,432 pixels with a bubble-induced channel difference above 4/255.
- Checked invalid numeric input rejection and `git diff --check`.
- Checked panel slider bounds, dragging outside the panel, **H**, **Reset color**, and
  **Save color**; a fresh process restored the exact saved values using isolated preferences.
- Rendered the panel at 1280×800 and 320×240, including an alternate tint; both two-frame
  GPU checks completed with no refused draws.

Local PNGs and logs are under `build-linux/thrax-water-proposal/`. This validates the
isolated Linux proposal; integration into the actual campaign map remains separate work.
