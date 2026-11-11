# Water benchmark: v3.5.1 vs clean FFT, v3.4.2 underwater clarity, settings (2026-11-11)

## Method

- GPU time per frame, synced fences, offscreen at 1920x1080, unpaced, RTX 4060 (Vulkan), on an idle machine
  (confirmed by the user). 15 s after start, then 600 frames, about 35 s per run.
- The user's saves don't load in v3.4.2 or v3.5.1. Every build starts the same scene fresh instead:
  - the lighting-preview sandbox skirmish maps `LightingSandboxUSA02` (open sea, carrier group;
    `scene-usa02.jpg`) and `LightingSandboxSuperweaponGeneral` (rivers and shallow shores; `scene-superweapon.jpg`);
  - `-autoskirmish 2 -observer`, seed 7, fixed camera, the user's Options.ini (Modern graphics, Hybrid
    reflections, refraction full, FFT 256).
- Builds:
  - v3.4.2 and v3.5.1: tags, built in `worktrees/bench-v342` and `bench-v351`, each with its own overlay.
  - "old water": today's main build with `ZH_WATER_CLEAN=0`.
  - "clean FFT": today's main build with no settings (clean water, `ZH_WATER_CLEAN_OPT=stream`).
- Runs were made in alternating order. The draw count per frame was checked for every run (`draws-campaign.txt`).
- In the first pass, eight settings runs on USA 2 and several on SWANGLE skipped the Hybrid mirror pass for part
  of the measurement (about 1,700 instead of 2,520 draws on USA 2). Those runs were redone, and every redo
  had the full draw count (`gpu-timing-redo*.txt`). The cause was not found; repeats did not reproduce it.
- These are sandbox scenes, not the user's saves, so absolute numbers differ from earlier timings.
- Scripts: `tools/`.

## 1. v3.5.1 vs clean FFT (for upstream)

Mean of two runs, ms per frame:

| | v3.5.1 | Today, old water | Today, clean FFT | Clean vs v3.5.1 | Clean vs old water |
| --- | ---: | ---: | ---: | ---: | ---: |
| USA 2 (open sea) | 10.40 | 11.24 | 11.89 | **+1.49 ms (+14%)** | +0.65 ms (+6%) |
| SWANGLE (rivers, shores) | 5.71 | 7.50 | 6.00 | **+0.29 ms (+5%)** | −1.49 ms (−20%) |

- The old water already cost 8% (USA 2) and 31% (SWANGLE) more than v3.5.1. The clean FFT gives back most of
  that on shore-heavy maps and adds 6% on open sea.
- Today's game also draws slightly more than v3.5.1 for things other than water (about 2,520 vs 2,452 draws on
  USA 2), so not all of the difference is water.

## 2. v3.4.2 underwater clarity

Same v3.4.2 build, `UnderwaterClarity = 0.90` (as shipped) vs `0.0` (`tools/ModernWater-v342-clarity-off.ini`
as an extra overlay). Clarity is visible in `v342-clarity-on-left-off-right.jpg`.

| | Clarity on | Clarity off | Cost |
| --- | ---: | ---: | ---: |
| USA 2 | 11.50 | 11.55 | none (within noise) |
| SWANGLE | 7.18 | 7.02 | +0.16 ms (+2%) |

## 3. What each setting saves on the clean FFT water

Single runs (base repeated). Base: USA 2 about 11.85, SWANGLE about 6.09 (run-to-run spread up to 0.2).

| Setting | USA 2 | SWANGLE | Look |
| --- | ---: | ---: | --- |
| Wave detail 128 (existing) | −0.62 (−5%) | −0.46 (−8%) | Finer ripples get coarser |
| Refraction simple (existing) | −0.33 (−3%) | −0.33 (−5%) | |
| Refraction off (existing) | −0.65 (−5%) | −0.51 (−8%) | No see-through |
| Screen-space reflections (existing) | −0.41 (−3%) | −0.14 (−2%) | |
| Reflections off (existing) | about −2.7 (−22%) | −1.13 (−19%) | Sky only |
| Reflection detail low (existing) | 0 | 0 | |
| Foam off | 0 | 0 | |
| Crest colour off | 0 | 0 | |
| Shore turning on the big waves only | 0 | 0 | |
| Simpler far-away glint | −0.76 (−6%) | −0.46 (−8%) | Glint harsher at distance |
| No shore turning | −2.51 (−21%) | −0.97 (−16%) | One global wave direction |
| No depth fade | −2.65 (−22%) | −0.85 (−14%) | Waves don't calm in shallows |
| No shore turning + wave detail 128 | −2.86 (−24%) | −0.99 (−16%) | |

Notes:

- Foam and crest colour cost nothing measurable. A foam repeat on USA 2 gave 11.92 with foam vs 11.91 without
  (`gpu-timing-foam-repeat.txt`). The in-game Foam option does not control clean foam yet.
- Turning only the big waves saves nothing: the cost is the turning path itself, not how many bands use it.
- With no shore turning and wave detail 128, USA 2 runs at 8.99 ms, faster than v3.5.1 (10.40).

## 4. Water quality option (added after this benchmark)

`WaterQuality` in Options.ini and the menu: High (default) is the clean water above, Medium = simpler far-away
glint (`nomips`), Low = Medium + no shore turning (`noshore`). Graphics presets: Low for the four lowest steps,
Medium for the next three, High for the top four. Applies live on Accept. `water-quality-high-medium-low.jpg`
shows USA 2 at each level (frame 400, same seed): High and Medium differ by 0.35/255 mean luma, Low by 4.3/255
(different wave pattern, no turning).
