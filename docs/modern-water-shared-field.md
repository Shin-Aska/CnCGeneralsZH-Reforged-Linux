# Modern water: one shared wave field (design, experiment)

Status: started 2026-11-10, experiment only, behind `ZH_WATER_SHARED=1` in the worktree
`/mnt/sandisk-2tb/worktrees/water-tempo-ab`. Follows [the single-medium design](modern-water-single-medium.md)
and its evidence in [docs/reference/water-single-medium-2026-11-10/](reference/water-single-medium-2026-11-10/).

## Why

Every attempt so far kept several wave systems and blended them: analytic swell on a travel-time
field, medium FFT chop rotated to eight sectors, near-shore legacy waves behind an offshore switch,
local wave arrivals, and depth masks on top. Wherever their inputs differ, the water splits into
areas that behave differently, and blending only hides the seam. The user's goal: the same waves
everywhere, with each place only changing how much of them it gets, so differences between areas
normalise smoothly instead of meeting at an edge.

## Idea: shared phase, local amplitude

After Jeschke, Skrivan, Müller-Fischer, Chentanez, Macklin and Wojtan, *Water Surface Wavelets*
(SIGGRAPH 2018), without their simulation:

- **Shared phase.** A fixed set of directions `theta_i` (16) and a few wavelength bands. For each band
  there is one periodic 1D wave profile `P_b(s, t)`: a sum of several wavelengths with deep-water
  dispersion. The phase of direction `i` at point `x` is `P_b(dot(x, dir_i), t)`: the same function of
  world position everywhere on the map. Directions are global constants, so nothing rotates absolute
  coordinates per pixel (the cause of the sector quilt and the marble stripes seen in earlier tries).
- **Local amplitude.** A map-wide field `A(x, theta_i, b)` says how much of each direction and band a
  place receives. All local behaviour lives here: island shadows, shelter, shoaling, depth fade,
  turning toward the shore. Because it is a smooth field and the phases are shared, two neighbouring
  areas with different amplitudes blend like two strengths of the same sea, not like two different seas.
- **Surface.** Height and slope are `sum_i sum_b A(x, i, b) * P_b(dot(x, dir_i), t)`, plus the global
  fine FFT ripples (wind detail, shared, never rotated) faded by depth.

## Amplitude field, built once at map load (CPU)

Inputs: the one-medium effective depth `D` (already built at map load), the wet mask, the wind
direction and the existing spectrum's direction powers.

1. **Open-sea seed.** Each direction's amplitude follows the wind spectrum's angular weight
   (`cos^p` around the wind, per band), entering from the upwind map edges.
2. **Propagation.** For each direction, march across the grid along that direction. Land sets the
   amplitude to zero behind it; water carries it on. A small angular blur between neighbouring
   directions and a spatial blur across the march direction model diffraction, so island shadows
   have soft edges and fill in with distance.
3. **Depth.** Band-dependent response to `D`: long waves fade first in shallow water, short ones
   carry further in (as in the single-medium plan); a mild shoaling gain before the fade.
4. **Turning toward shore.** In shallow `D`, move amplitude between neighbouring direction bins
   toward the local depth-gradient direction, so waves arrive roughly perpendicular to beaches.
5. **Smoothing and storage.** Blur the result over a few cells; store 8 bits per (cell, direction,
   band) in an atlas sampled bilinearly.

## Per-frame profiles

`P_b(s, t)` is recomputed each frame for a small number of samples per band (for example 2048 over one
period) and uploaded, or evaluated in the shader if that is cheaper. The shader then does one
texture read per direction and band for height and slope.

## What it replaces (in shared mode only)

The analytic swell and its sector rotation, the medium FFT chop and its eight-sector cross-fade,
the near-shore legacy waves and the offshore model switch, local wave arrivals, and the one-medium
band gains (folded into `A`). It keeps vessel wakes and hull cages, reflections, refraction (with
the forward-plane fix), seabed optics and crest foam from surface steepness.

## How it is judged

The same harness as before: save `00000006.sav`, the sunlit pier view (+270 degrees) and one full
turn, 1:1 crops of the user's red, green and purple areas, motion maps, GPU timing in ABBA order,
and the default path unchanged. Comparisons are by eye first; the per-frame change metric
rewarded noise in earlier rounds and is only a supporting number.
