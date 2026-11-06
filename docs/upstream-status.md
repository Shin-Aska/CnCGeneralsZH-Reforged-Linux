# Upstream status of this fork's features

Which Linux features from this fork have been offered to upstream
([olcayseygan/CnCGeneralsZH-Reforged](https://github.com/olcayseygan/CnCGeneralsZH-Reforged)), and what
happened to them. Anything not accepted upstream stays in this fork as it is.

Last updated 2026-11-06. Each pull request was cut from upstream v3.8.0 (`e3e1a898`).

## Accepted

| Feature | Pull request | Status |
|---|---|---|
| Shared volumetric cloud volume: no eight-field limit | [#170](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/170) | Accepted. The maintainer ported it as `b0e9f004`, with this fork's author as co-author, for upstream's next release. It isn't on upstream's public `main` yet. Direct3D 11 keeps its eight-layer path. The maintainer can't run Linux or macOS, so it ships on our testing. |

## Waiting for review

| Feature | Pull request | Status |
|---|---|---|
| Modern water reflection modes (Legacy, SSR, Hybrid), the Water tab, quality controls and underwater clarity | [#172](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172) | Open |
| Modern water render caches | [#173](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/173) | Open, depends on #172 |

## Not accepted: stays in this fork only

| Feature | Pull request | Why it wasn't taken | What would be considered |
|---|---|---|---|
| Asset Preloading: audio preload and renderer pipeline warmup | [#169](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/169), closed | No measured load-time gain. The audio half reads up to 256 MiB at every map load, and upstream already streams sounds with a 64 MB cache. | A separate pull request for pipeline warmup alone, limited to Linux and macOS, with measurements |
| Volumetric Contaminate, toxin impact clouds and attached toxin puddles | [#171](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/171), closed | More volumetric effects add GPU work to the heaviest frames, and nothing measured it. An earlier volumetric smoke feature was removed from the game over GPU cost. | A separate pull request with just these effects, plus SDL GPU frame times from a crowded toxin fight, with and without them |

## Not offered upstream

These stay fork-only, with no pull request so far:

- **Map-aware water movement:** waves turn toward the shore and follow channels. It's the code behind the GPU cost on the
  carrier save, and turning it off doesn't remove most of that cost.
- **Path-traced water reflections:** experimental.
- **Small fixes:** finding Steam's Generals folder beside Zero Hour, path cache invalidation, and portable hotkey literals.
- **Release packaging, CI and the developer tools** that only this fork uses.

## What upstream's maintainer expects

These come from the reviews above:

- Measurements with every performance or visual-cost change, before and after
- No new `ZH_*` environment switches or diagnostic output
- Renderer reads taken at the sync point, not from live game logic
- New looks off by default, gated on a backend capability rather than `__linux__`
- One change per pull request, rebased onto current `main`
