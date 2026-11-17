# Post-4.0.0 upstream synchronization

Date: 17 November 2026.

The `sync/post-4.0.0-upstream` integration branch retains its published history
at `c5e32caff`. It first integrates the four new Linux `main` commits through
`458075944` (Linux 4.0.1) as `fd5d6bbfa`, then the 31 new upstream commits from
`761ee2ccc` through `419d88bee`. The local recovery branch
`maint/pre-sync-post-4.0.0-20261117` preserves the original sync tip.

## Incoming changes

- Linux 4.0.1 release tooling and documentation, Modern-water material controls,
  loading-speech sequencing, and Turbo Mode cinematic pacing from this fork's main.
- Upstream AI threat-map layers, safer routes and retreats, active unit/hero
  abilities, and foreign-faction bases built from stolen builders or captures.
- Generated, shrouded ranked Quick Match maps and score-screen result delivery
  that waits for the server's confirmation.
- Revised explosion art and effects, per-particle flipbook variants/mirroring,
  orange bloom and local contrast for the Reforged lighting style.
- Splinter camouflage replaces the gold skin's label while preserving its IDs
  and replacement-sheet filenames.

These gameplay updates change replay compatibility with older builds. Local
replay checks compare recordings and playback made by this integrated build;
they do not establish cross-platform compatibility or live ranked-server behavior.

## Reconciliation

- Preserve the Linux 4.0.1 release/version and workflow, the loading-speech queue,
  Modern-water materials, native effects, weather, waterline clipping, and preload work.
- Combine main's effective cinematic-rate snapshot with the sync branch's
  game-speed-aware retail pacing. Logic planning, catch-up, animation time and
  render pacing use the effective rate; the requested match speed is retained.
  Rendering during logic overlap reads the captured rate instead of live script state.
- Extend the existing cinematic regression across every selectable game speed,
  including authored slow scenes and restoration after input is released.
- Keep the extracted shared post-processing shaders. Port upstream's four bloom
  shader changes into that source, retaining the existing water-aware haze and
  explicit AO option handling. The four new source fingerprints were computed
  independently from upstream `419d88bee`'s literal shader strings.
- Carry bloom saturation and local contrast through the native settings and
  SDL passes. Contrast uses the first two blur pairs even with Glow off; later
  glow pairs preserve that luminance width. Classic/off routing retains its gates.
- Combine particle mirroring with native particle-size metadata and waterline clipping.
- Update the shipped-skin regression to expect `SKIN:Splinter`, matching the
  incoming INI and both language tables.
- Make the replay harness's idle control a one-frame match. Upstream's AI now
  builds on the old seed-1 narrow-map fixture; the control still requires the
  harness to reject a world with no construction. Full active-match playback,
  repeat-seed and checkpoint comparisons remain in the same test.

## Local evidence

Build, CTest, independent shader-baseline and focused rerun evidence is retained
under `build-linux/upstream-sync-20261117/`. Tests use private profiles and
temporary file-link farms of the installed game data. No dependencies or optional
art archives were installed, and no release or remote branch was published.

## Validation

- Full GCC 16 Release build passed, followed by a successful incremental rebuild
  after the two test-fixture corrections.
- Reconciled results across 201 CTest entries: **193 passed, 6 skipped, 2 failed**.
  `ctest.xml` retains the broad run; `focused-final.xml` retains the final engine,
  offline replay and network reruns. `validation-summary.json` uses the final
  outcome for each entry.
- The complete engine suite and offline replay harness pass. Active matches,
  repeat seeds, playback, checkpoint saves, deliberate replay corruption and
  the corrected idle control are covered.
- The broad native GPU checks pass under software Vulkan. The affected
  `test_sdl_post_gpu`, `test_renderer_particles_gpu` and `test_sun_shadow_sorting`
  checks also pass on NVIDIA Vulkan (`nvidia.xml`). The new post checks cover
  orange bloom, contrast with Glow off, flat-field preservation, restoring Off,
  fixed contrast width across glow levels and resource reuse.
- Shared shader fingerprints match the independently compiled upstream sources.
  The release tooling passes 21 fixtures. Manifest and whitespace checks pass.

Two failures remain visible:

- `packaging_resolution_check`: the Windows-shaped, package-shaped and
  self-discovering layouts resolve the same files, but the archive-order
  negative control requires the absent optional `ReforgedTextures.big`.
- `net_check`: paired live games agree at frame 1800 with no CRC mismatches,
  including the overshoot control, but their network replays do not reach the
  requested comparison frame. A retained 600-frame probe reproduces EOF at
  frame 594. The corruption control also finds the first CRC at frame 1504,
  while its assertion expects frame 1500. The recorder and network-harness
  paths involved are unchanged from `c5e32caff`. This does not qualify network
  replay endpoints; the ordinary offline replay harness passes separately.

The six skips are the macOS data-removal check, portable-container packaging,
MinGW program values, architecture differential, and the two Direct3D oracle
checks. Local socket unit tests pass outside the sandbox; the sandboxed broad
run could not bind their sockets. Windows/Metal runtime, live ranked servers
and new performance measurements remain unqualified.

Final source fingerprint: `7D784B87` over 5,665 files. Final executable SHA-256:
`48c2e7f5e70ebae02ca2c1451efe1e1ef2d538d66d733dfb9acb208a266b9e2b`.
