# Water performance penalties: underwater clarity and clean FFT water (2026-11-11)

Evidence for upstream. Two questions:

1. What does underwater clarity cost?
2. What does the new (clean FFT) Modern water cost compared with upstream's Modern water?

## Results

GPU ms per frame, mean of two runs in alternating order (lower is better).

| Scene | Upstream `main` | Fork, old water | Fork, clean FFT | Clean FFT vs upstream |
| --- | ---: | ---: | ---: | ---: |
| USA 2 (open sea, carrier group) | 11.06 | 11.10 | 11.97 | **+0.91 ms (+8.3%)** |
| SWANGLE (rivers, shallow shores) | 6.68 | 7.76 | 6.18 | **−0.50 ms (−7.5%)** |

| Scene | Without clarity code | With clarity | Clarity penalty |
| --- | ---: | ---: | ---: |
| USA 2 | 11.99 | 12.17 | **+0.17 ms (+1.5%)** |
| SWANGLE | 5.90 | 5.99 | **+0.09 ms (+1.5%)** |

In short:

- **Underwater clarity costs about 1.5%** on both scenes. With clarity, the shallow shelves near the islands
  look clearer (`swangle-clarity-on-left-off-right.jpg`). An earlier v3.4.2 test that turned the
  material value off measured +2% on SWANGLE and nothing on USA 2 (`../water-benchmark-2026-11-11/`).
- **Clean FFT water costs 8% more than upstream on open sea and is 7.5% cheaper on shore-heavy maps.** Its cost
  scales with how much open water is in view; shore turning and the depth fade are the expensive parts (see
  `../water-benchmark-2026-11-11/`). The new Water quality option brings open sea below upstream: Low (no shore
  turning) saved 21% on USA 2.
- The fork's old water costs the same as upstream on open sea and 16% more on shores. That extra is the fork's
  map-aware movement, which upstream does not have.

## Method

- **Hardware and settings:** NVIDIA RTX 4060 (Vulkan), 1920x1080 offscreen, unpaced, GPU time from synced fences
  (`ZH_GPU_TIMING`, `ZH_GPU_TIMING_SYNC=1`). Each run waits 15 s after the first frame, then measures 600 frames.
  The machine was idle, confirmed by the user.
- **Scenes:** the lighting-preview sandbox skirmish maps `LightingSandboxUSA02` (`scene-usa02.jpg`) and
  `LightingSandboxSuperweaponGeneral` (`scene-superweapon.jpg`), started with `-autoskirmish 2 -observer`, seed 7.
  The camera stays still, and the user's Options.ini applies (Modern graphics, Hybrid reflections, full
  refraction, FFT 256). Both builds draw the same number of draws per frame on each scene (2,519/2,520 and
  2,784-2,787), so they render the same scene and the difference is the water.
- **Builds:**
  - Upstream: `upstream/main` at `bda651dd` (`upstream-commit.txt`), with its own data overlay.
  - Fork: main as of 2026-11-11 (v4.0.0 plus fork work, with clean FFT water uncommitted), using its default
    settings. "Old water" is the same binary run with `ZH_WATER_CLEAN=0`.
  - No clarity: the same fork source with the single call `waterTerrainClarity(...)` replaced by `0.0`, so the
    compiler drops the clarity code entirely. This measures the feature's whole cost, including the shader space
    its code takes up, not only its runtime branch. The source was restored afterwards, and the rebuilt binary
    matched the original apart from its build ID and timestamp.
- **Order:** upstream, clean, old, old, clean, upstream, then clarity, no clarity, no clarity, clarity, on each
  scene (`tools/campaign-penalty.sh`). Raw output is in `gpu-timing.txt`.
- **Noise:**
  - Repeat runs of the same build differ by up to 0.15 ms (USA 2) and 0.23 ms (upstream on SWANGLE).
  - The same binary also drifted by about 0.2 ms between the first and second half of the campaign. Compare
    numbers only within their own table, whose runs alternate.
  - The clarity difference is smaller than that drift but was consistent within each pair: both clarity runs
    were slower than both no-clarity runs on both scenes.

## Caveats for an upstream PR

- The clean FFT number compares upstream with the whole fork build, not with a clean-water-only patch on
  upstream. Draw counts match, but other shader differences between the trees are included. An exact number
  needs clean water ported onto `upstream/main`.
- One GPU, one resolution, still camera. Panning and other GPUs were not measured.
