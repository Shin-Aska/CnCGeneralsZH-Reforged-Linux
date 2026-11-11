# Upstream status of this fork's features

Which Linux features from this fork have been offered to upstream
([olcayseygan/CnCGeneralsZH-Reforged](https://github.com/olcayseygan/CnCGeneralsZH-Reforged)), and what
happened to them. Anything not accepted upstream stays in this fork as it is.

Last updated 2026-11-11. Each pull request was cut from upstream v3.8.0 (`e3e1a898`). Their branches are kept on this
fork for later work.

## Accepted

| Feature | Pull request | Status |
|---|---|---|
| Shared volumetric cloud volume: no eight-field limit | [#170](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/170) | Ported as `b0e9f004`. Direct3D 11 keeps its eight-layer path. |
| Modern water reflection modes (Legacy, SSR, Hybrid), the Water tab and quality controls | [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172) | Ported as `c6f4f4ca1`, merged by `234d55a20`. Turkish strings rewritten, and Legacy is "Orijinal" in Turkish. Underwater clarity left out (see below). |
| Modern water render caches and load-time preparation | [#173](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/173) | Ported as `fcd05658f`, merged by `0244f2a9f`. |

All three were ported by hand by the maintainer, with this fork's author as co-author. They reached upstream's public
`main` on 2026-11-06, which marked each pull request merged, and they ship in the release after v3.8.0. Upstream's
v3.8.0 doesn't have them. The maintainer can't run Linux or macOS, so the SDL side of all three ships on our testing.

## Not accepted: stays in this fork only

| Feature | Pull request | Why it wasn't taken | What would be considered |
|---|---|---|---|
| Asset Preloading: audio preload and renderer pipeline warmup | [#169](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/169), closed | No measured load-time gain. The audio half reads up to 256 MiB at every map load, and upstream already streams sounds with a 64 MB cache. | A separate pull request for pipeline warmup alone, limited to Linux and macOS, with measurements |
| Underwater clarity: shallow seabed and ruins show through Modern water | Commit `5d8a58e7` in [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172), left out | It changes how white water looks as soon as Modern is picked, with no option to keep the old look, and the owner wants to see it first. No `ClearWater` rule ships. | Its own pull request, with before and after shots of the same views: the ruins, a flat offshore floor and a coast |
| Volumetric Contaminate, toxin impact clouds and attached toxin puddles | [#171](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/171), closed | More volumetric effects add GPU work to the heaviest frames, and nothing measured it. An earlier volumetric smoke feature was removed from the game over GPU cost. | A separate pull request with just these effects, plus SDL GPU frame times from a crowded toxin fight, with and without them |

## Not offered upstream

These stay fork-only, with no pull request so far:

- **Map-aware water movement:** waves turn toward the shore and follow channels. It's the code behind the GPU cost on the
  carrier save, and turning it off doesn't remove most of that cost.
- **Path-traced water reflections:** experimental.
- **Clean FFT water** (2026-11-11): one global FFT ocean (JONSWAP spectrum, per-band depth fade, filtered glint,
  Jacobian foam) with waves turning toward the shore, plus a Water quality option (Low, Medium, High). It's now the
  fork's Modern water on Linux; `ZH_WATER_CLEAN=0` restores the previous water. Measured cost is below.
- **Small fixes:** finding Steam's Generals folder beside Zero Hour, path cache invalidation, and portable hotkey literals.
- **Release packaging, CI and the developer tools** that only this fork uses.

## Ideas for later pull requests

Each would follow the maintainer's notes above, and each needs measurements first:

- **Underwater clarity:** open it as its own pull request with before and after shots of the same views: the ruins,
  a flat offshore floor and a coast. Making it an option, off by default, would answer the "no way back to the old
  look" objection. The commit is `5d8a58e7` on branch `feat/water-reflections-and-controls`. Measured cost
  (2026-11-11, RTX 4060, 1080p, the same build with and without the clarity code): **+1.5%** GPU time on both an
  open-sea and a shore-heavy scene (12.17 vs 11.99 ms, 5.99 vs 5.90 ms). Evidence:
  [`reference/water-penalty-2026-11-11/`](reference/water-penalty-2026-11-11/README.md).
- **Clean FFT water:** measured against upstream `main` (`bda651dd`) on the same scenes: **+8.3%** GPU time on open
  sea (11.97 vs 11.06 ms) and **−7.5%** on a shore-heavy map (6.18 vs 6.68 ms). Water quality Low (no shore turning)
  saved 21% on open sea, below upstream. These compare the whole fork build with upstream, so an exact figure needs
  the water ported onto upstream `main`; upstream would also want the testing switches and tuner removed and the new
  look off by default. It depends on the wave heading field from map-aware movement. Evidence:
  [`reference/water-penalty-2026-11-11/`](reference/water-penalty-2026-11-11/README.md) and
  [`reference/water-benchmark-2026-11-11/`](reference/water-benchmark-2026-11-11/README.md).
- **Volumetric toxin effects:** just the delta over the shared volume, with SDL GPU frame times from a crowded toxin
  fight, with and without it. Upstream already has a GPU timer (`ZH_GPU_TIMING` with `ZH_GPU_TIMING_SYNC=1`) and a
  scripted, repeatable battle (`-scenario`, files in `Data/Scenarios/`). A crowded-cloud save is kept in the devlog's
  2026-11-05 benchmark session. The work is on branch `feat/clouds-contaminate-effects`.
- **Pipeline warmup only, Linux and macOS:** needs a measurement that shows first-use shader stalls going away, which
  plain load-time numbers didn't. The work is on branch `perf/preloading-and-warmup`.
- **Map-aware water movement:** only after its shader cost is fixed, with a real off path that removes the cost.
- **Test the merged features on Linux** in upstream's next release, since nobody upstream can run them.

## What upstream's maintainer expects

These come from the reviews above:

- Measurements with every performance or visual-cost change, before and after
- No new `ZH_*` environment switches or diagnostic output
- Renderer reads taken at the sync point, not from live game logic
- New looks off by default, gated on a backend capability rather than `__linux__`
- One change per pull request, rebased onto current `main`
