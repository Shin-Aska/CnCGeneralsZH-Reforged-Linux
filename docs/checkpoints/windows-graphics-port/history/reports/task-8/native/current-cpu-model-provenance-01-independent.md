# Fresh current CPU model provenance — independent review

**AdversarialVerify: confirmed. codeQualityStatus: WATCH; recommendation: APPROVE; blockers: none for this bounded CPU compile/full-suite scope.** The eleven-header gap is closed for new binary8C4B, not retroactively for old79279DCD. Task8 GAME/lifecycle/native GPU/parity acceptance remains separate. Prior C642 criterion audit is unchanged.

## Identity and observed build

Read full sibling current-cpu-model-provenance-01/DoneClaim5C30FBA1 and raw evidence. Seal2EE67C24 verifies42/42 sizes/hashes; result8766D164 agrees except warning qualification below. Full hashes in receipt.

Actual build37556 parent35820,47.554s,exit0/reaped/no timeout,600000ms cap. CIM argv is MSBuild.exe build64/test_water_grid.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false /m:1. Raw output shows exactly the three target TUs compiled and target linked: test_water_grid.cpp, test_gameengine_stubs.cpp, debug_build_stubs.cpp. Referenced projects appear as GetProjectInfoForReference, not fresh linked-library recompilation; this is not a whole transitive GAME graph proof. Qualified runnerC78A26C2 normal WaitForExit/reap read; unused timeout branch is not evidence.

Saved property-evaluation output has GeneratedFilesDir empty, IntDir test_water_grid.dir/Release/, TLogLocation test_water_grid.dir/Release/test_water_grid.tlog/, OutDir build64/Release. Independently resolved IntDir/TLog inside build64; no metadata command was rerun. ProjectA25CC43C and CMake2DFC6F61 protected. generate.stamp.depend declares29 inputs and exactly matches stamp-dependencies.json: all saved input times <= original00:52:55.851Z stamp. Earlier freshness-preflight.json lists28 because CMake itself is separately included in the29-entry artifact; no silent29/28 equivalence. Actual build touches stamp03:18:09.884Z; no claim that this generated timestamp stayed unchanged.

## Header and full-suite proof

Compiler raw stdout09679ADA prints /showIncludes paths for all eleven headers, independently matched against header-provenance.json and direct current byte hashes. UTF16 CL.read.1.tlog3D8FFEED has three distinct TU groups and all eleven exact paths, with actual current tlog bytes/hash equal saved copy; mtime03:18:38.091Z lies inside build03:18:06.724–03:18:54.277Z. Thus this is fresh observed include use, not source-only include inference.

Verified headers: WaterFftGeometry, WaterFoamHistory, WaterFoamParticlePolicy, WaterHullContactFoam, WaterHullModel, WaterOpticalContext, WaterProjectedMesh, WaterSurfaceModel, WaterWaveCage, WaterWaveCageMesh and Tests/WaterWaveCageTest. Full hashes appear in sibling independent receipt and original header-provenance.jsonC7A9672A. All100 protected entries before/after/current match: selected82/source3E6AE6F5, grid96E4303D/threeTUs/headers/CMake/project/built-stage5C358861.

Direct binary read: build64/Release/test_water_grid.exe **14034432B / 8C4B98F7337C50B3161AB568346226D239E6C862770E3F866963EA1E74074A7F**. One no-argument full test45952 parent35820,4.286s,exit0/reaped/no timeout,600000ms cap. Raw stdoutD3421313 ends **173 tests,10281391 checks,0 failed**; no FAIL/SKIP; stderr0. Current TU has173 TEST definitions; test_main.cpp64–126 with no args runs every registered test, so absence of per-test RUN/DONE when progress is unset is legitimate. Raw hull/shore/coastal numerical observations provide additional behavior witnesses. No separate focused filters or repeat test is counted.

Closes current CPU hull/foam/window/motion/reset/atlas/cage model coverage, including same-time/visibility/replacement model inputs. Real owner lifecycle/native changed-view history/mirror point-retention/GAME teleport or reused-ID remain open. Linked libraries reused; only3TUs/known includes freshly attested.

## Closure, perspectives and findings

Supervisor35820 birth03:18:04.581482Z; actual tracked ancestry35820→37556→Tracker10720→CL42648 and CL children mspdb7472/VCTIP45676 is explicit. Saved close intent/script checks parent/full executable/birth, then process-object birth before stopping only those two helpers. Cleanup records16 original identities absent, both exact actions, monitor normal exit and cache absent; direct filesystem check confirms own cache absent. Test monitor path sampling was null for one row, but separate actual test-process CIM includes path; polling cannot prove a global child census. Empty before-helper pipeline emitted no helpers-before file: retained as UNKNOWN baseline, not fabricated empty snapshot. Saved env/CWD equality is supported by capture/finally logic; no live reviewer env query or cleanup.

CRITICAL/HIGH/MEDIUM: none established in this bounded review. LOW: DoneClaim says16 conversion warnings/result classesC4267,C4305; raw build actually has15 conversion warnings plus D9030 (/showIncludes disables /MP), total16; repeated summary lines are not32 warnings.0errors remains true. This reporting omission does not invalidate include observation or tests. Identity sampling/linked-graph limits above remain explicit.

Programming/remove-ai-slops perspectives ran: meaningful numerical/policy tests; no removal/prose/tautological tests or needless production complexity added. No code edits.

Manual auxiliary channel: Node fs/crypto reads of original scripts, raw stdout/stderr, process/preregistration, tlog, headers, source manifests, binary and cleanup. Stale compile/tlog/binary, dirty-source preservation, zero-test/misleading-success/current-vs-historical graph and capped/reaped-owned-helper classes checked. Fixed local data, unchanged model tests and no new runtime mean malformed external/prompt/cancel/flaky/interruption probes N/A. A snapshot schema lookup and one REPL syntax error were corrected read-only. Only this new report/receipt written; zero reviewer processes/tests/build/GPU/GAME/ports/env/temp/farm/PDB/REF copies/deletes/retries. Read holds released.
