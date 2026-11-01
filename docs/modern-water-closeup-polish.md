# Water polish: preserve channel direction

This change preserves the wind/current balance from
`5b8c0fbc` and retains the rendering optimizations. The extra fine-wind gain from
the first close-up polish was withdrawn after the user reported that SWANGLE2
channels had lost their visible direction.

## Correction

The rejected polish added `0.20 * I * (1 - g)` to the fine FFT gain. In fully
sheltered water this raised its amplitude from 0.06–0.12 to 0.248–0.296, about
2.5–4.1 times the committed strength. Those ripples follow an independent wind
heading, so increasing them could compete with the local current cues. Preserving
the current direction field alone was insufficient to preserve its appearance.

Vertex displacement, physical and filtered surface tangents, and unresolved
variance now use the committed composition again: `g * original + localWaves`.
The extra fine-gain helper, derivative terms, variance terms and retained fine
outputs are removed. No analyzer or route changes are needed for this correction.

## Retained rendering changes

The paired FFT evaluator reuses a cascade's mip-zero samples when it needs no
further filtering. Local arrivals share their clock and footprint, and stream
noise evaluates before the FFT surfaces to reduce intermediate lifetimes.
Vessel and coastal foam share an identical sky-lighting calculation. Opt-in
logic-frame timing and buffered CSV output remain available for matched runs.

A fresh close-view ABBA check of the corrected executable used 600 consecutive
animated logic frames per run. Baseline means were 16.589 and 16.149 ms; corrected
means were 16.801 and 16.524 ms. The averages are 16.369 versus 16.662 ms, a
measured increase of 0.293 ms (1.79%). This short whole-engine comparison does not
show a speedup or measure isolated GPU water time. All windows completed with
zero rejected frames. Commands, frame CSVs and the report are retained under
`build-linux/water-polish-timing/direction-correction/`.

## Regression coverage

The mixed/fractional-LOD checks remain. New composite checks use low residual
wind gains 0.06 and 0.12, with currents opposing and crossing the synthetic fine
wave's actual travel direction. They compare full surface endpoints and temporal
changes against the committed-budget oracle. Both fine-wave motion and current
motion must be present to arm the test. This extends the earlier tests that
checked current advection by itself. An isolated mutant reintroduces the rejected
wind boost and must fail these new assertions.

The corrected Release build, shader tests and full NVIDIA GPU selfcheck pass.
The composite endpoint/temporal oracle's maximum error is `3.14e-8`; each of the
four direction controls has 24 active cases. Reintroducing the boost produces
2,592 failures specifically in those new checks, with maximum error `0.1504`,
while the independent mixed-LOD check remains within `2.39e-7`. The mutation
does not alter production source or binaries. The full CTest suite was not rerun.

## Evidence

The original close-up polish clips and timing reports under
`build-linux/water-polish-closeup/` and `build-linux/water-polish-timing/` describe
the rejected boosted-wind build, not the current correction. New evidence is
kept under `build-linux/water-direction-correction/`. Matching normal-zoom views
cover the SWANGLE2 northern channel at (1030,2450) and island junction at
(1250,1500), using the same saved game, camera, picture clock and logic frames.
The isolated boost mutation is retained separately under
`build-linux/water-polish-closeup/validation/direction-mutant/`.

All six native clips (committed baseline, rejected polish and correction at two
views) contain 481 consecutive frames at 60 fps with zero missed pictures.
Within a fixed water-only polygon at the junction, the lossless first/last
corrected frames differ from the committed reference by mean RGB levels
0.072/0.097 out of 255; the rejected polish differs by 1.436/1.452. This supports
appearance restoration, not a direction estimate from stills. The midpoint
comparison is separately labelled as H.264-compression limited. The 16-second
reels retain eight seconds per view for motion inspection.

The packaged local executable has been rebuilt and updated from the validated
correction. Its previous binary is retained in the correction's `package/`
directory. Raw executable SHA-256:
`b672a48f3fa96805dfcae18c4788e5fa4c116d62ddf06c237b93c08daad74092`.

Original user saves and profiles are unchanged.
