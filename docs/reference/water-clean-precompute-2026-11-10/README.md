# Clean FFT optimization (2026-11-10/11)

Clean FFT water with the user's approved values (now the defaults), `water-tempo-ab` worktree, built by Codex.
GPU per frame, synced, ABBA, idle machine (`gpu-timing-precompute2.txt`, `gpu-timing-pass2.txt`).
An earlier run made while a video was playing was discarded.

| | SWANGLE | USA 2 (open sea, carrier group) |
| --- | ---: | ---: |
| current game | 7.62 / 7.65 | 11.09 / 11.12 |
| clean before optimization | 10.64 | 20.70 |
| pass 1: map-load precompute (filtered depth texture, hex-vertex angle texture, one context per pixel) | 9.92 | 19.45 |
| pass 2: clean-only main-level shader (legacy wave code compiled out), 1-read tiles where turning is uniform (<1.5 deg), no extra shading read below LOD 0.25 | **8.34** | **15.83** |

Pass 2 profile on USA 2 (15.83): `nodepth` 9.25, `noshore` 9.36, `nomips` 9.91. Each of these alone drops
below the current game and they are not additive, so the remaining cost is mostly shader size / register
pressure (occupancy) in the turned-wave (hex tile) path, not memory reads.

Look: frames from each build compared with the frozen pre-optimization build. Mean luma differences of
2-4/255 on water (whitecaps and crests shift slightly, because rotation angles are now computed more precisely on
the CPU); side-by-side crops show the same look, with no seams or tile edges. `usa2-before-after-1to1.mp4`,
`swangle-before-after-1to1.mp4` (pass 1), `usa2-difference-heat.jpg`.

## Pass 3: smaller turned-wave path (2026-11-11)

Codex rewrote the hex-tile sampling so it no longer keeps per-tile arrays. Each technique has its own token in
`ZH_WATER_CLEAN_OPT`, and the default is `stream,fused,loop,collapse=1.5`. Timing used the short method:
12 s after the save loads, then 600 frames, about 35 s per run. Runs were synced, ABBA, on an idle machine
(confirmed by the user). Raw output is in `gpu-timing-pass3.txt`.

| | SWANGLE | USA 2 |
| --- | ---: | ---: |
| current game | 7.53 / 7.57 | 10.97 / 11.00 |
| pass 2 | 8.38 / 8.38 | 15.82 / 15.79 |
| **pass 3 (default)** | **6.41 / 6.41** | **11.85 / 11.87 / 11.90** |

The pass-2 figures match the earlier long method within 0.04 ms, so the short method agrees with it.

Tokens on USA 2 (one run each):

| `ZH_WATER_CLEAN_OPT` | ms |
| --- | ---: |
| `none` (pass-2 evaluator in the pass-3 binary) | 15.82 |
| `stream` | **11.71** |
| `fused` | 12.56 |
| `stream,fused` (unrolled) | 11.89 |
| default + `collapse=3` | 11.86 |
| default + `collapse=5` | 11.82 |

- Streaming is the win: it removes about 4.1 ms on its own.
- `fused` helps without `stream`, but adds nothing on top of it.
- Loop and unroll are the same.
- Wider collapse thresholds save at most 0.05 ms, which is not worth any change in look.
- `stream` alone is faster than the default, confirmed by an ABBA run on both saves (`gpu-timing-pass3-stream.txt`):
  USA 2 11.73 vs 11.91, SWANGLE 6.17 vs 6.48. So `fused` and `loop` cost a little once streaming is on.
- `finelite` and `half` stay off because they lose ripples and foam.

Look: the pass-3 default matches pass 2 by eye on both saves.
