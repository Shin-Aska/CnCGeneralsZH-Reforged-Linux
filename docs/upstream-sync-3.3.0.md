# Linux fork integration of upstream v3.3.0

Date: 24 October 2026.

The merge joins Linux fork `43a79e86` with upstream tag `v3.3.0`,
`f036c761e8a8b9af`. Their shared base is upstream v3.2.0, `153d636c`;
149 upstream commits enter the fork. Both histories remain intact.

## Reconciliation decisions

- Preserve the Linux defaults: Modern water, Volumetric clouds and mapped sun
  shadows. Explicit saved choices continue to win. Other platforms retain
  upstream's Classic water/cloud defaults and capability checks.
- Keep all four Linux reflection modes: Legacy, SSR, Hybrid (default) and
  experimental Path traced. Preserve saved selection, live switching, water
  preview controls, scene banks and path-tracing caches.
- Keep authored shoreline surf, shallow-water detail/clarity, the material optics
  register, water receiving sun/smoke shadows and the fork's render snapshots.
  Merge upstream's water capability and wrapper-state fixes into those paths.
- Use upstream's shared hazard-cloud core and SDL backend lifecycle, retaining the
  fork's palettes, cloud preview interface, cache diagnostics and forced-rebuild
  comparison. Normal rendering uses upstream's cache scheduling.
- Keep upstream's tree culling, shader/fire work and engine race fixes. Preserve
  the fork's Linux shadow controls and validation tools. Water CPU/shader layouts
  are reconciled across the SDL and Direct3D 11 paths; Linux tests also exercise
  the shared CPU layout, optics and viewport-depth projection.
- Retain Linux branding, release CI, host packaging, no-art dependency mode,
  Steam data discovery and preview targets. The native build's newly copied
  overlay dereferences art symlinks so it can move with the executable.
- Keep all upstream gameplay, AI, rank, language and data updates. The archive test
  links the fork's real path helpers instead of upstream's duplicate stubs.

## Validation

Local logs and captures are retained under `build-linux/upstream-sync-3.3.0/`;
proprietary game assets and captures remain outside Git.

- Full Release build and both preview targets passed.
- Final CTest run: 107 tests, 83 passed, 21 skipped, 3 failed (126.72 seconds).
  The failures are the same ones recorded before the sync:
  `ffref_capture_selfcheck` (two NVIDIA pixels outside tolerance),
  `lobbyroom_selfcheck` and `mapselect_selfcheck` (committed layouts differ from
  their generators). Tolerances and generated layouts were not changed to hide
  them. `ctest-final.log` and `ctest-final.xml` retain the complete results and skips.
- Legacy, SSR, Hybrid and Path traced each rendered 64 frames in the production
  water preview with GPU validation, no refused draws and an inspected capture.
- In-game four-palette clouds and clouds alongside Modern water passed their
  runtime checks and were inspected visually. The Classic/Volumetric comparison
  agreed at frame 420 (`0x7E6A6678`).
- The mapped smoke-shadow fixture completed all five scenario actions, reached
  frame 660, and produced an inspected capture without GPU validation errors.
- Two-player AI seeds 0 and 1 each agreed across recording, playback and a repeated
  run at frame 24000: `0xC1F28A6B` and `0xBD853DA2`. The original install's 370
  entries were verified unchanged. This is same-machine determinism.
- Standalone cloud preview passed 180 logic frames with GPU validation and forced
  rebuilding: 103 rebuilds / 103 volume frames, 206 field bakes, active walker wakes
  and unchanged 720/720 infantry health. Nuclear ember reporting returned OK.
- Final production cloud rerun (`clouds-final/summary.json`) passed palettes,
  Modern-water coexistence, Classic fallback reporting and equal gameplay CRCs.
- Options layout, no-art dependency mode and thin-publishing rollback checks passed.

The shader source test was updated for upstream's new names: explicit Classic
identities select the preserved Classic programs on both backends. It also checks
original arithmetic and rejects Modern-only shader features in those programs.

The checks use the existing GCC 16 Release configuration and NVIDIA Vulkan driver.
No software or dependencies were installed. The scratch Git repositories created
by the fingerprint self-test ran with `commit.gpgsign=false` in that test process's
environment; the user's Git configuration and real commit-signing preference were
left intact.

Final game executable SHA-256:
`2f4ce3bd68c0734bc32b4bae72fcdfcf72c2ee02b2eab617a4d06dcc30b9af0c`.
Replay evidence is also retained in `/tmp/replay-check.fOrfV0`.

## Scope limits

The Linux checks do not establish Windows or macOS build/runtime compatibility.
Direct3D 11 resource integration was reviewed statically; its Modern-water
sun/smoke receiving remains unsupported. Path traced reflections retain their
existing experimental scene/material limitations. No cross-platform network or
performance claim follows from this sync.
