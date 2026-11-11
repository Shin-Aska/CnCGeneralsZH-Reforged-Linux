# Modern water: how FFT oceans are usually done (research, 2026-11-10)

Why this exists: the shared-field experiment (`ZH_WATER_SHARED=1`) replaced the FFT for the long and
medium waves with four sine waves per direction, and the user found the larger waves "odd". This note
collects how games and papers build realistic FFT water, especially as seen from above, and compares
that with what Modern water does today.

## What Modern water does now

- Spectrum: Phillips, three cascades of 256x256 (tile sizes 450 / 103 / 13, times `WorldScale` 20),
  each band normalised to a fixed RMS height (1.50 / 0.75 / 0.18). Directional spreading is `cos^p`
  (default 2 / 1 / 0.01). Deep-water dispersion only (`omega = sqrt(g k)`).
- Long waves (default game): not FFT. Four analytic sine waves travelling along a precomputed
  travel-time field (`fftSwell` in `WaterFftSampling.h`), with value noise along the crests.
- Medium waves: FFT, but the tile is rotated into 8 heading sectors that follow the coast and
  cross-faded between them.
- Fine ripples: FFT, global.
- Local wave arrivals, an offshore model switch, near-shore analytic waves and painted breaking foam
  sit on top, each with its own idea of where water is "shallow".

## What the established FFT approach looks like

1. **One global FFT ocean.** All cascades are computed once per frame and sampled with world
   coordinates, unrotated. Phases are global, so the whole map is one surface. Several cascades with
   non-integer size ratios hide tiling (Tessendorf 2001; Arc Blanc 2025; GodotOceanWaves; Sea of
   Thieves, SIGGRAPH 2018).
2. **A measured spectrum, not Phillips.** Current practice is JONSWAP, or TMA (JONSWAP with a
   finite-depth factor), with Hasselmann or Donelan-Banner directional spreading, plus Horvath's
   "swell" parameter, which stretches long waves into long parallel crests (Horvath, DigiPro 2015).
   JONSWAP has a sharp peak, which gives a visible dominant wave size. Phillips has no peak, which
   tends to give blobby long waves with no clear wavelength. Fetch (distance the wind has blown over
   water) sets the peak, so a sheltered map gets shorter waves without any local tricks.
3. **Local variation is amplitude only, per band, from depth.** Crest (Unity) keeps a top-down
   depth cache and fades each wavelength once depth is below about half of it ("Shallows Max Depth"
   smooths the fade). Nothing rotates and nothing changes phase, so there are no seams; shallow water
   simply calms, long waves first. Smooth seabed slopes matter: steps in depth show as steps in waves.
4. **Shore waves are a separate, explicit layer.** Crest adds shoreline waves as Gerstner waves
   along authored splines, blended over the FFT. They are not derived by bending the FFT.
5. **If waves must turn, rotate tiles smoothly and keep the statistics.** Ubisoft La Forge (Lutz,
   Schoentgen, Gilet, HPG 2024) samples the FFT through hexagonal tiles: each tile picks a random
   offset and a rotation from a flow map, and three tiles are blended with a covariance-preserving
   blend. This removes tiling and lets direction change gradually, at about the cost of noise
   blending. A plain linear blend of two unrelated wave patterns lowers contrast in the blend zone
   (at 50/50, RMS drops to about 71%), which is the "normalised in between" look the user described.
   A variance-preserving blend avoids that (Heitz and Neyret 2018).
6. **Foam from the surface itself.** Foam appears where the displacement Jacobian goes below a
   threshold (crests folding over). It accumulates and decays over frames, so it trails behind waves
   (Tessendorf; Atlas, GDC 2019; GodotOceanWaves).

## What matters specifically from above (RTS camera)

- Looking down, water reflects only a few percent of the sky, so wave shape reads mainly through
  three things: the sun glint, crest colour and foam.
- **Glint must be filtered, not cut off.** Bruneton, Neyret and Holzschuch (2010) move detail that is
  too small for a pixel from the normal into the BRDF (slope variance). Near the camera you see
  individual glints. Far away the same energy becomes a smooth, wide highlight. LEAN mapping (Olano
  and Baker, Firaxis, 2010) does the same with mipmapped slope moments. This targets the "red
  divide": that edge was the boundary of the sun-highlight lobe, which an unfiltered, anisotropic
  slope field makes sharp.
- **Crest subsurface colour.** Atlas (GDC 2019) and Sea of Thieves brighten and tint wave crests
  with a cheap fake scattering term driven by wave height and view and sun angles. That makes waves
  visible outside the glint, which was the earlier "outside the glint the water looks frozen"
  problem, without adding noise.
- **Jacobian foam** gives breaking that follows actual crests, unlike the painted breaking band.

## Comparison

| Concern | Modern water now | Shared field | Established FFT approach |
| --- | --- | --- | --- |
| Long waves | 4 analytic sines | 4 sines x 16 directions | FFT cascade (450 tile), JONSWAP + swell |
| Medium waves | FFT, 8 rotated sectors | 4 sines x 16 directions | FFT, global, unrotated |
| Shallow water | several disagreeing masks | amplitude field `A` | one depth, per-band fade |
| Direction near shore | follows coast (source of the seams) | shore turning in `A` | global; separate shore waves, or flow-map tiling |
| Glint far away | sharp lobe edge | sharp lobe edge | filtered (slope variance) |
| Foam | painted band + steepness | steepness | Jacobian, accumulated |

## Recommended experiment ("clean FFT")

Behind a new switch, in the `water-tempo-ab` worktree:

1. **Global FFT for all three bands.** Use the existing 450 cascade for long waves instead of the
   analytic swell; no sectors, no shared-field sines, no local arrivals, no offshore model switch.
2. **JONSWAP / TMA spectrum with Hasselmann spreading and Horvath swell**, fetch set per map from
   open-water size. Keep the RMS heights so strength stays comparable.
3. **Per-band fade from the one-medium depth `D`** (already built), using the "depth below half the
   wavelength" rule.
4. **Shading from above:** filtered glint from FFT slope variance, crest subsurface colour, and
   Jacobian foam with decay.
5. Only if the coast then looks too static: shore waves as a separate layer, or flow-map tiling with
   covariance-preserving blends. Keep this out of the first build.

Expected cost: below the current game. It drops the analytic swell, the sector cross-fade (two
medium reads) and local arrivals, and adds one long-cascade read. The spectrum change is CPU-side at
map load.

What it gives up: waves no longer turn toward beaches. The user noted that the disconnect started
when waves began following coastlines, so this is the trade to test first.

## Sources

- Tessendorf, "Simulating Ocean Water" (2001).
- Horvath, "Empirical directional wave spectra for computer graphics", DigiPro 2015,
  <https://doi.org/10.1145/2791261.2791267>.
- Bouws et al., TMA finite-depth spectrum, JGR 1985,
  <https://earthref.org/ERR/46790>.
- Crest Ocean System, "Shorelines and Shallows",
  <https://crest.readthedocs.io/en/stable/user/shallows-and-shorelines.html>.
- Lutz, Schoentgen, Gilet, "Fast orientable aperiodic ocean synthesis using tiling and blending",
  HPG 2024, <https://doi.org/10.1145/3675388>; Ubisoft La Forge summary,
  <https://www.ubisoft.com/en-us/studio/laforge/news/5WHMK3tLGMGsqhxmWls1Jw/making-waves-in-ocean-surface-rendering-using-tiling-and-blending>.
- Heitz and Neyret, "High-Performance By-Example Noise using a Histogram-Preserving Blending
  Operator", 2018, <https://hal.archives-ouvertes.fr/LJK/hal-01824773v1>.
- Bruneton, Neyret, Holzschuch, "Real-time Realistic Ocean Lighting using Seamless Transitions from
  Geometry to BRDF", CGF 2010, <https://hal.archives-ouvertes.fr/inria-00443630v3>.
- Olano and Baker, "LEAN Mapping", I3D 2010, <https://userpages.cs.umbc.edu/olano/papers/lean/>.
- Mihelich and Tcheblokov, "Wakes, Explosions and Lighting: Interactive Water Simulation in
  'Atlas'", GDC 2019, <https://gdcvault.com/play/1025819/Advanced-Graphics-Techniques-Tutorial-Wakes>.
- Ang, "The Technical Art of Sea of Thieves", SIGGRAPH 2018,
  <https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf>.
- Algis et al., "Arc Blanc: a real time ocean simulation framework", JCGT 2025,
  <https://arxiv.org/abs/2503.03326>.
- GodotOceanWaves (JONSWAP/TMA, Hasselmann, Jacobian foam, cascades),
  <https://github.com/2Retr0/GodotOceanWaves>.
- Jeschke et al., "Water Surface Wavelets", SIGGRAPH 2018 (basis of the shared-field experiment).
