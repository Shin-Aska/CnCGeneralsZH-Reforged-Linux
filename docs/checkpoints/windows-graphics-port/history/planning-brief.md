---
slug: windows-modern-water-d3d11
status: reviewed
intent: clear
review_required: true
pending-action: High-accuracy review approved; await an explicit separate implementation start. Do not implement in this planning session.
approach: Share water math/HLSL and explicit draw packets; implement a native D3D11 water resource owner without changing legacy fixed-function/D3D9 contracts.
---

# Draft: windows-modern-water-d3d11

## Components (topology ledger)
| id | outcome | status | evidence |
| --- | --- | --- | --- |
| shared | Verified guards, shared geometry/shaders and explicit ABI | verified | water_source_inventory terminal result; parent checked ABI, flags, packing and shader dispatch |
| native | Native FFT/capture/bindings/lifecycle | verified | native_gpu_design terminal result; parent checked stage limits, void draw/refusal, depth ownership, FFT format/passes |
| settings | Defaults, capability UI, Accept/Cancel/persistence | verified | settings_plan terminal result; parent checked menu transaction and actual backend reporting |
| validation | Executable build/runtime/parity/performance gates | verified | validation_commands terminal result; CTest enumeration only, parent checked test registration/cache recorder/capture eligibility |
| compatibility | Render-only invariants and dirty-worktree safeguards | verified | compatibility_risks terminal result; parent confirmed all seven intake hashes unchanged |
<!-- Lock the SHAPE before depth. One row per top-level component that can succeed or fail independently. -->
<!-- id | outcome (one line) | status: active|deferred | evidence path -->

## Open assumptions (announced defaults)
The outcome remains clear. The user explicitly requested high-accuracy review on 2026-10-16; review_required is now true. Plan edits and review infrastructure are authorized, implementation is not. Tests accompany each future milestone; final parity requires actual runtime evidence. Keep production defaults, diagnostics and opt-in experiments distinct. Mandatory Metis analysis from plan creation remains recorded below.
<!-- Record any default you adopt instead of asking, so the user can veto it at the gate. -->
<!-- assumption | adopted default | rationale | reversible? -->

## Findings (cited - path:lines)
Planning baseline: HEAD 98729c02a37b8c4c784df0856fbf290c4daa9536, 2026-10-16. The preceding goal turn is classified as progress: source inspection produced the implementation brief. This turn continues by verifying current state and producing the plan.

Existing user-owned files (SHA-256 at intake):
- GeneralsMD/Code/CMakeLists.txt: 768fbc69ddb7de86b613e7666f87d2888ffd8374fe2b49eb76348f9c03c324d1
- GeneralsMD/Code/Libraries/Include/Platform/WaterBedMaterial.h: 6066326cbab65622d092af52e23bb524f4e449613482da5ee48d1d36a4a351c3
- GeneralsMD/Code/Libraries/Include/Platform/WaterOpticalModel.h: 4cc9512f7b19c8c925df78b2c2042d35824cd4082418565b46b358a6e8035cc1
- GeneralsMD/Code/Libraries/Include/Platform/WaterShoreSource.h: 312af84d4fac3700d0155926b9e930571b567a5a5a1fd46e6091fb2dd9c69efb
- GeneralsMD/Code/Tests/WaterCoastalCapacityTest.h: 078bd63a18a0d3bbb48bd546b3e0e31bc49074014a940f22db44b6f81d03b5c2
- GeneralsMD/Code/Tests/test_water_grid.cpp: 96e4303d6ec2d8184765d72873ffe37c1e41d237b5e3bbb0f28c7d523b41a021
- GeneralsMD/Code/Tests/test_water_optical_model.cpp (untracked): 08a93de04bdc1c68f338341bf4caa1ddd228d2e1685b5f2ab3f5d2ec7e9eb779

Independent parent verification: OptionsMenu.cpp:2956-2990 stages selection until Accept and discards on Back; W3DDisplay.cpp:4768-4784 reports actual backend; test_water_grid is Windows-stubbed at CMakeLists.txt:2305-2319; test_main.cpp:133-136 rejects zero matching tests; WaterHullVisualLayout.h:8-10 confirms 896 scratch registers and 194/202 common uniform sizes; engineshader.cpp:1656/1701 gates modern HLSL to SDL; dx11backend.cpp:1763-1796 retains non-main depth only as a DSV.

Read-only ctest enumeration first failed because Git Bash PATH lacks ctest. The validation lane located the bundled Visual Studio CMake/CTest executables and enumerated current targets. No build or test execution is claimed.

Primary API sources checked: Microsoft PSSetShaderResources (overlapping outputs bind NULL), CopyResource (compatible dimensions/formats/sample counts), ResizeBuffers (all back-buffer references must be released), CheckFormatSupport, and SM5.1 register-space documentation. Final plan will link these where used.

## Decisions (with rationale)

- Retained, atomic native patch preparation replaces the original proposed partial Classic fallback: projected batches have no Classic-span mapping and DrawIndexed has no success return. Pre-emission failure uses the whole original Classic patch; device loss after emission invalidates admission and recovers next frame.
- Native accepted coverage is keyed by frame/target/view/capture/level/patch; wake suppression needs complete footprint coverage, not same-level admission. Disjoint failed-patch and adjacent-union cases are explicit.
- Core backend/format/shader failure means effective Classic; valid analytic/material degradation is explicitly reported and earns no native replacement ownership. Target snapshot failure is patch/view-local. Saved preference is unchanged.
- Preserve current single-sample native targets and validate actual sample counts. Do not invent a new native MSAA implementation in this water port.
- Numerical FFT oracle is a test-only independent scalar transform and fixed goldens, not a call into the same production helpers.
- Suggested relative performance gates are explicit engineering proposals. No blocking owner decision; a fixed FPS/minimum-GPU target would refine them.

## Gap review and handoff audit

Metis /root/plan_gap_review returned six actionable gaps: partial fallback contradiction, circular snapshot timing, unspecified degradation policy, missing retained geometry ABI, self-comparison oracle risk, and incomplete owner coverage aggregation. Parent verified W3DWater.cpp:4813-4845, dx11backend.cpp:2908 and SdlGpuFrame.cpp:376-406 before incorporating the changes.

The same reviewer rechecked the updated plan and reported those six gaps substantively resolved, with one remaining wording inconsistency about late submit refusal. Parent corrected both stale locations to later-batch preflight/capture refusal and simulated device loss before/after emission, and explicitly prohibited submit-time resource creation. This is a gap-analysis receipt, not an unconditional high-accuracy approval.

Final structural/source-preservation audit:
- 15 column-zero implementation tasks, four column-zero final tasks, three implementation waves.
- All template headers in required order; every implementation task includes scope, dependencies, exact references, acceptance, happy/failure QA, evidence and commit policy.
- No unfinished template placeholders. Existing and proposed commands/targets are distinguished.
- All seven intake source-file SHA-256 values still match; only .omo planning artifacts were added.
- Plan SHA-256: d4601e922dd155a39435432c7aa39a438f5f4ae148c6fb2d3cd25bb77821acbd.
- No build, test execution, game launch, source implementation or commit occurred. The only test-tool operation was read-only CTest enumeration.
- Linux/macOS runtime access, native modern-water runtime results and performance evidence remain future implementation gates, not planning claims.

## Scope IN
All five numbered areas of the active goal, full production parity, optional experimental parity under existing opt-in policy, exact tasks/files/interfaces/commands and a final acceptance checklist. Plan-only writes under .omo.

## Scope OUT (Must NOT have)
No product-code edits, implementation, commits, game launches, builds, installs, deployment, PRs, or changes to existing user modifications in this planning turn. No scope reduction to a shader-only or settings-only result.

## Open questions
No blocking owner decision. Performance target will be a declared comparison configuration and regression budget in the plan, not a claim about all GPUs.

## Approval gate
status: approved
The previous turn delivered the source-backed brief. The current user goal explicitly requests using that brief to produce a complete implementation-ready plan and prohibits implementation. This authorizes plan creation now; no further approval request is needed. Execution is not authorized.
<!-- When exploration is exhausted and unknowns are answered, set status: awaiting-approval. -->
<!-- That durable record is the loop guard: on a later turn read it and resume at the gate instead of re-running exploration. -->

## High-accuracy review round 2
Round 1: Momus OKAY; independent CHANGES_REQUESTED. Reports and full state archived beside this draft. All four source-verified gaps repaired in plan: atomic WaterStyle/file commit; isolated command environments and normalized data; repaired existing shader-recorder routing plus variant manifest; explicit replay control with verified mutation and specific CRC divergence. Baseline now precedes product edits. PowerShell/Bash command syntax checked without execution. Round 2 plan digest: ef75ad4c0aefbb19bae58255fc8ae72785bed67094c2be4cae725bbf2fab3cd9. Fresh isolated root: <USERPROFILE>/AppData/Local/Temp/zh-water-review-r2-yq2xJC. Both lanes pending; no implementation authorized.

## High-accuracy review completed
Both round-2 lanes returned unconditional OKAY for ef75ad4c0aefbb19bae58255fc8ae72785bed67094c2be4cae725bbf2fab3cd9. Native session /root/momus_water_r2; independent gpt-5.6-sol/xhigh CLI thread 01a1453e-4b39-7de0-9b31-f33408aad039, PID 36628, exit 0. Full reports: windows-modern-water-d3d11-review-r2-momus.md and windows-modern-water-d3d11-review-r2-independent.md. The live plan passed the same canonical descriptor-relative no-follow hash check immediately before handoff. All seven intake source hashes remain unchanged. No product implementation, build, game launch, runtime tests or commit occurred. The revised plan remains 15 implementation tasks across three waves plus four final verification tasks. No blocking owner decision remains.
