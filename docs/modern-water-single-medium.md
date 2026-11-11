# Modern water: one medium (design proposal)

Status: proposal, 2026-11-10. Step 3 is prototyped behind a test switch (see the evidence README); nothing is on main. Evidence and the test-only switches used to
gather it are in [docs/reference/water-single-medium-2026-11-10/](reference/water-single-medium-2026-11-10/).

## The problem

Since Modern water started turning waves toward coastlines, sunlit water shows areas that don't
belong together: rippled patches that stop at blotchy edges, flat water beside busy water, and
textures that seem to change direction for no reason. Blending the edges (the smoothed-depth and
wider-range tries in [water-seams-2026-11-10](reference/water-seams-2026-11-10/README.md)) only hides
them, because the cause isn't the edges.

The goal is water that behaves like one liquid: waves are defined once, travel through all of it,
and change gradually wherever the water changes, with no per-area rules joined by blend masks.

## What the water does now

The surface is built from layers. Each layer decides on its own where the water is shallow and which
way it goes, and they disagree:

| Layer | Decides | Reads as "shallow" | Where |
| --- | --- | --- | --- |
| Model switch (`offshore`) | open-sea FFT waves or the older near-shore waves | distance from land, ramped 30-100 units, ocean-connected water only | `WaterOpticalContext::Offshore`, `opticalContext.r` |
| Wave strength (`attenuation`) | how much of the wave shows | real terrain depth, full at 6.88 units, in 0.625-unit terrace steps | `terrainWaveAttenuation` in `engineshader.cpp` |
| Direction field | which way long waves and medium chop travel | a second distance from land, capped at 480 units; the header says terrain depth is too thin to steer waves | `WaterWaveHeading.h` |
| Long swell | 4 analytic waves (287-1091 units) | follows the direction field's travel-time phase, coherently | `fftSwell` in `WaterFftSampling.h` |
| Medium chop | FFT cascade 1 (24-256 units) | the direction field, snapped to eight 45-degree sectors; neighbouring rotations of the same tile are cross-faded | `fftAccumulateChop`, `fftMediumSurface` |
| Fine ripples | FFT cascade 2 (<40 units) | nothing: one prevailing rotation for the whole map | `fftWindSurface` |

`WaterMovementBehavior = yes`, the default and the user's setting, selects this map-aware path; `no`
selects the v3.5.1 sector heading, which has the same sector cross-fade.

## Evidence

`fields-pier-crop.jpg` (the sunlit pier on the Superweapon General map, save `00000006.sav`, camera
+270 degrees) and `fields.jpg` (the whole view) show each field on its own:

1. **Model switch:** round halos around every island and coast.
2. **Real depth:** blotchy sand-bank terraces. Nothing in common with 1.
3. **Direction:** turns through every heading within short distances, with singular points near the
   rocks where all directions meet.
4. **Long swell crests:** smooth arcs that bend around the islands. The one layer that is already
   coherent.
5. **Medium chop:** a quilt. Even where 3 looks smooth, the sector index and its cross-fade
   (white) change every few dozen pixels, so the visible chop is two rotated copies of one tile
   being swapped and blended all over the water. The direction texture stores its gradient in 8
   bits, so a heading near a sector boundary probably wobbles across it (likely, not verified).

So near the pier the waves switch model along map 1, weaken along map 2, and the chop turns and
cross-fades along a noisy version of map 3. No blend can reconcile three different shorelines.

## Principle

One medium, one source of waves:

- One field describes the water everywhere: an **effective depth** `D`.
- Waves are defined once, offshore, and carried inward through `D`. Every property that changes
  near land (direction, wavelength, height, breaking, foam) is a function of `D`, so where `D` changes
  smoothly the waves change smoothly. No layer has its own idea of the coast.
- Physics decides which waves respond. Waves feel the bottom only where `D` is less than about
  half their wavelength. Long swell bends toward the shore and steepens; short chop and ripples
  hardly care and keep the wind direction. Real beaches look like this: swell crests parallel to the
  sand, wind chop crossing them.

## Design

### 1. The medium: effective depth `D`

Built once per water level at map load, on the CPU, next to the optical context:

- Combine the two inputs the layers already use: real terrain depth (scaled up, since map water is
  a thin cosmetic sheet) and distance from land (so open water far from any coast is deep even
  over a shallow-authored seabed). A first form: `D = blur(min(ShoreSlope * distanceToLand,
  DepthScale * terrainDepth))`, clamped to a deep-water cap.
- Blur over a few terrain cells so the 0.625-unit terraces disappear before anything reads them.
- Store it in the existing heading texture or a new single-channel texture. Add a `ZH_WATER_DEBUG`
  view for it.

`DepthScale`, `ShoreSlope` and the blur radius are tuned by eye against fields 1-3; the target is
one field that reads as the true shape of the shallows.

### 2. Everything reads `D`

- **Long swell:** solve travel time with speeds from `D` (shallow-water speed `sqrt(g D)` blending
  to deep-water speed) instead of `ShoreSlope * distance`. The existing eikonal and wavefront code
  stays; only its speed input changes. Height follows shoaling (grows slightly as `D` falls) and
  is capped by breaking (height at most a fixed fraction of `D`).
- **Medium chop and fine ripples:** follow the wind everywhere. Drop the eight-sector rotation and
  its cross-fade. Their strength comes from `D` through a per-band depth response (a band fades as
  `D` falls below a fraction of its wavelength), so short waves carry further into the shallows than
  long ones, smoothly.
- **Model switch:** removed. One wave model everywhere (rivers keep their own). `offshore` stops
  gating waves.
- **Breaking and foam:** from swell height against `D`. Foam appears where the swell actually breaks,
  not at a fixed distance from land.
- **Currents in channels:** later, drift (advect) the chop along the current instead of rotating
  tiles toward it, so channels keep a readable direction without a quilt.

### 3. What stays

- **Seabed visibility** (the murky patches seen through the water) stays on real depth: it shows
  what is actually under the water. If its edges still look hard, it can read the same blurred depth.
- Reflections, glints, the FFT spectrum itself (including the chosen 2/6/4 direction powers) and
  vessel wakes are unchanged.

## What it removes

The model switch, the sector quilt and its cross-fade, the second distance-from-land field, and the
terraced wave strength. That is three of the five disagreeing fields; the remaining two (`D` and
the travel time derived from it) agree by construction.

## Cost

CPU at map load: one blur and one change of speed input to an existing solve. GPU: fewer
samples per water pixel, since the second rotated medium-chop read and the near-shore legacy wave
evaluation go away. Expected to be no slower than now, possibly faster; to be measured.

## Risks and open questions

- **Building `D`:** how much real depth against distance from land. If the seabed authoring is too
  noisy, `D` may need to lean mostly on distance from land plus a gentle depth term.
- **Readable channels:** the earlier close-up polish lost SWANGLE2's channel direction. Without the
  sector rotation, channel direction must come from currents (step 2's drift) or from the swell. Needs
  a channel save to check; the SWANGLE saves are gone.
- **2/6/4 waves:** with the quilt in place, a more directional medium cascade makes every sector
  change easier to see. Under this design the medium cascade follows the wind, so 2/6/4 and this work
  fit together. Shipping 2/6/4 alone first needs a check near coasts, not only open sea.
- **Long swell visibility:** v20 showed that broad swell reads as blotches from the RTS camera. Keep
  it subtle; most of the visible detail stays in the medium and fine bands.

## Plan

Each step A/B'd with the existing harness (`-loadsave`, `-video`, fixed or turning camera, the
sunlit pier, open sea and a channel), with evidence in `docs/reference/`:

1. Build `D` and its debug view. No visual change. Compare against fields 1-3.
2. Feed `D` to the travel-time solve. Check fields 3-4 for fewer singular points and smoother turns.
3. Medium chop and ripples follow the wind with a per-band depth response; remove the sector quilt.
   This is the step that should fix the seams.
4. Remove the model switch.
5. Breaking and foam from `D`.
6. Currents drift the chop in channels.

## The alternative: revert

Going back to underwater clarity with uniform open-sea waves gives one consistent rule, so no seams.
It gives up waves that react to coasts at all. Step 3 alone (wind-following chop, one depth field
for strength) is close to that revert in look, and keeps the direction-aware long swell, so it is
also the cheapest point at which to judge whether the rest is worth doing.
