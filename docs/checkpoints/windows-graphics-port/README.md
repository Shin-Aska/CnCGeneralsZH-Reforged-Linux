# Windows graphics port — shelved checkpoint

Archived on 2026-10-21 to `feat/windows-graphics-port` at the owner’s request before deleting the local checkout. **Shelved and incomplete. Do not resume implementation automatically.** This branch preserves work in progress, not an approved release or a merge-ready port. No PR or merge is requested.

## Restore

```sh
git clone --branch feat/windows-graphics-port --single-branch https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux.git
cd CnCGeneralsZH-Reforged-Linux
```

The archive starts from `98729c02a37b8c4c784df0856fbf290c4daa9536`, the actual implementation baseline. It deliberately does not rebase onto newer `main`. Integrating subsequent upstream changes is separate work. Read this checkpoint, [plan](plan.md), [execution overrides](execution-overrides.md), [pause record](history/pause-20261018.md), [task state](state/tasks.json) and [history ledger](history/ledger.jsonl) before resuming. Earlier delivery rules in historical records describe the old session; the owner later explicitly authorized this archival branch, commits and push.

## Implementation and verification status

**7 of 19 top-level gates are complete; Task 8 is the first unfinished gate. Full parity is not verified.**

| Area | Status at shelving |
| --- | --- |
| Shared packets, shader ABI/generators and native owner | Implemented; bounded native tests passed |
| Immutable scene colour/depth and native surface shading | Implemented; bounded snapshot/optics/reflection tests passed |
| Water style capability and atomic preference persistence | Implemented with tests; ordinary packaged menu/relaunch acceptance remains open |
| Native raster FFT and displaced geometry | Task 7 complete; independent hardware/WARP numerical and geometry tests passed |
| Hull, vessel waves, cages and persistent foam | Integrated with substantial GPU/native evidence; Task 8 runtime acceptance incomplete |
| Shoreline/coastal, lifecycle/cache completion | Tasks 9–10 open; earlier preparation and some shared/default-shore work exist |
| Ordinary defaults, final visual parity, gameplay/save/replay/D3D9, performance, CI and final review | Tasks 11–15 and F1–F4 remain open; partial supporting implementations are preserved |

The port reuses shared Linux shader/math code and provides native D3D11 resources/bindings. Preserve Classic and real D3D9. Production defaults remain WaterStyle=1 with FFT/vessel/cage/foam/shore features enabled; SURF_GPU/COAST_CREST are opt-in experiments. Diagnostic/capture flags do not establish ordinary-launch acceptance. Linux/macOS native hosts were unavailable and execution was explicitly waived by the owner; these lanes were **not run**, never passed. No final cross-OS appearance/performance claim follows from numerical FFT success.

## Next work, only after an explicit resume

1. Rebuild from this branch with configured Windows tools and legally supplied game data. New builds have new fingerprints; do not reuse old executable hashes as current validation. Use `build.bat Release test` and the focused commands in the plan as appropriate. This shelving operation runs archive-integrity checks, not another build/game suite.
2. Reconcile Task 8 with [the minimum remaining map](history/reports/task-8/readiness/task8-minimal-remaining-reconciliation.md). Current default-off/stale-view pair is confirmed; do not repeat all FFT or motion work merely to recreate the old orchestration. Recheck source applicability and reacquire any raw proof needed for final acceptance.
3. Finish and independently review the partial owner-lifecycle and Debug normal/wireframe launch bindings in `resume-artifacts/readiness/`. They are **unsealed, unexecuted, not ready to launch**. Pause/reset actual bindings were never authored. The qualified private pause/reset and lifecycle helper sources are retained.
4. Complete actual teleport/hidden/shrouded/replacement, ordinary pause/resume, map reset/natural identity reuse, wireframe retention and detached/mod/unrelated particle attribution. NativeProof.presentation is a mode flag; adjudicate actual raw admission/skipped state. An absent/capped record is UNKNOWN. Old particle observer cap is 512; do not raise/reset it to manufacture proof.
5. The 3C-only diagnostic delta and sorted witness patch are **unapplied proposals**, not fixes to apply blindly. Actual MAIN availability RED is still required before 3C application. Its MAIN hook precedes particle-count/fade rejection, so positive geometry is not required for that availability-only RED; later sorted/native-retention proof does require real eligible geometry and an attributed issued draw. Sorted instrumentation requires its own later RED.
6. Finish Tasks 9–15 and all final gates; preserve source/behavior and run only justified affected checks. The final global review/debugging gate has not passed.

Existing named-union history first shows a healthy Rest/Wave pair at logic 15, render 111, frame 112, view 4 (records 7 and 9), not only the later logic 162 sample. Use fresh early eligibility, not a new delay API/map or a larger cap. P callback spawns its owner: use a distinct `WaterPauseFixture` with the existing union geometry and no scenario; the preplaced `WaterFixtureUnion` is a different identity. Wireframe can legitimately skip Modern Prepare/FFT: require actual native activity, queried WIREFRAME enum 2, false admission and a real retained point span; label driver UNKNOWN when unobserved, and retain strict hardware/FFT proof for its normal reference.

## What is preserved

- All 136 relevant modified/new source, test, fixture and validation-tool files from the original checkout, including all seven pre-existing water-related inputs. They remain integrated; those initial edits were not discarded or falsely attributed as all-new port work. `source-inventory.json` records their original raw byte hashes.
- Plan, execution overrides, task state, debug history, ledger and selected independent reports. These document bounded historical results and their qualifications.
- Current small private lifecycle/pause/reset helpers, partial bindings, the unapplied diagnostic proposals and authored named-map fixtures. These are archival development artifacts, not production inputs.
- Source manifests and archive inventory with original/archived SHA-256 values. Git can normalize line endings; Git blob IDs recorded in `source-inventory.json` provide the committed representation. The build fingerprint manifest is refreshed for committed source paths.

Local workspace/profile paths in archived history are replaced with `<REPO>` / `<USERPROFILE>`. Old launchers and input manifests therefore require deliberate rebinding, new private outputs and current source/tool/data/executable hashes. Even unchanged historical manifests reference omitted files and must not be mistaken for complete evidence packages. Archived copies with redactions do not hash to the original seals; `archive-inventory.json` distinguishes both hashes.

## Deliberately not backed up here

Builds/EXE/PDB/object files, full game-data farms, shader/runtime caches, real user settings, raw GPU readbacks/log trees, screenshots and the 2 GB image sequence/derived preview video are excluded. The external Linux reference folder is also excluded. Deleting the original checkout removes any of these located inside it; historical ledger/report claims alone cannot replace their raw evidence. Restore external game data and any desired captures separately. No fresh/full runtime validation is claimed by this archive.

The accidental root `--check` map and PowerShell ModuleAnalysisCache are not source and are excluded. The old root `.debug-journal.md` is preserved as `history/task6-initial-debug-journal.md` instead of adding root clutter. No original files, original index, original main checkout or history are reset/stashed/cleaned/deleted by this operation. The temporary archival worktree is removed only after a verified push and clean-tree check.

Archive integrity results are recorded in [archive-verification.json](archive-verification.json). The preserved source has 15 extra blank-line-at-EOF findings from `git diff --cached --check`; these were retained instead of silently cleaning up the WIP code. This is not a fresh code-quality or runtime approval.

Implementation snapshot commit: `f623a7bc0c09dcfce1839d8ada0bfec3e919fbf0`. The following documentation commit adds this checkpoint without changing production code.
