# Independent verification: private causal lifecycle gate

**Verdict: confirmed for the sealed private helper qualification.**  
**Code quality: acceptable within the declared helper scope, with integration conditions below.** This is not actual Game, native-producer, visual, GPU, or Task 8 completion evidence. No test was rerun and no helper was imported.

## 1. Scope and PIN → RED → GREEN

Reviewed `lifecycle-causal-gate-private-01`, including its complete 716-entry seal, all historical attempts and source archives, the final 19-case GREEN-r2, and cleanup. The earlier lifecycle controller PIN/RED/GREEN is inherited only for its exact scope.

The old fixed callback PIN completed the actual helper teleport. In evidence mode, hidden/shroud restore occurred before a corresponding native-render release; that is the causal RED. The first teleport-red attempt failed before a socket was accepted because of an artificial one-second startup limit and is correctly excluded as behavioral RED. The preserved red-r1 reached the actual teleport command before the render-release event within the original four-second timeout and failed the causal ordering assertion. The subsequent REDs also correspond to observed behavior: 58C5 missed later canonical Wave eligibility; CC9594 rejected a completed 161,271-byte burst due to aggregate-buffer accounting. Their frozen source archives are retained. The corresponding GREEN revisions accept a later actual Wave under the same owner and bound only pending/unconsumed data, respectively. I found no indication that the old tests or parser were weakened to manufacture the final pass.

## 2. Independent artifact and source verification

- Stream-hashed all 716 sealed entries (2,065,172 bytes): zero mismatches.
- Checked all 73 attempt-source archive mappings against their archived/current bytes: zero unresolved entries and zero mismatches.
- Checked all 11 final frozen inputs and the three current ScenarioDrill/header/particle-system pins listed in cleanup: zero mismatches.
- Independently read `actual-observations-final.json` and cross-checked final raw wire events, controller events, native rows, monitor diagnostics, per-case durable outcomes, registration, and cleanup. The final process receipt records 19 cases, exit 0, 32.089 seconds, no outer timeout, and reap under the 120,000 ms cap. Teardown records 20 exact process identities and resource release; it is not a broad process census.
- No basedpyright, Ruff, full-suite, Game, or GPU run is claimed. The source claim labels its syntax/size checks accurately.

The final invocation recorded by the run is `pwsh.exe -NoProfile -File <sealed namespace>/run-cpu.ps1 -Phase green-r2`; the wrapper used the pinned Python with `-B qualify.py green-r2` and the bounded process runner. I re-read the evidence and did not execute it.

## 3. Behavior and code review

`make_lifecycle_monitor` retains its previous positional interface and defaults `evidence=False`; the conditional startup and native waits are opt-in. With evidence enabled it waits for `inGame=true` on the same Control connection before discovery, binds the fresh named owner’s positive ObjectID/DrawableID, then waits for full-key native evidence after the relevant mutation and before restore/next phase. The causal gate consumes the existing native rows without another socket or manager parser. It joins same-frame/capture/patch route issuance, ownership generation and hardware-device rows with a stable particle full key, actual owner position, logic/render pair, positive Rest/Wave system IDs and bounded provenance. It distinguishes current live-observed systems from those in the initial inventory and does not infer birth, current view, GPU completion, or visual proof.

Raw rows show the requested hidden/shroud semantic explicitly: both cases had actual 0/0 admission/skipped values while hidden/shrouded, then actual 1/1 on restore. The returned `NativeProof.presentation` is only the requested mode flag; the actual admission/skipped values remain in the retained raw `run.log` and must be consulted for adjudication. It would be misleading to treat that mode flag as observed admission.

The final evidence-mode path waits for a native proof before the corresponding next mutation. The separate no-hook compatibility case retains the old behavior, including mutation before render-release; it is correctly a compatibility PIN rather than a causal pass. The startup case shows false status frames followed by true on the same connection before discovery. Later-Wave and empty-initial-inventory cases show actual IDs 141/241 under the exact bound owner with live-observed provenance; no initial-manager refresh or birth claim is made.

The parser drains complete rows incrementally, bounds unconsumed data and row lengths, and rejects malformed, stale, duplicate/out-of-order, incomplete-key, capped, or unsupported evidence as UNKNOWN/error. The long complete-burst case succeeds after draining the large completed stream; the unfinished case rejects its oversized pending row. The case named `teleport-capped` is a duplicate/out-of-order canonical record rejection; it is not exhaustive proof of every exact 512-row boundary. Wrong-owner, stale, mirror-only, and no-eligible scenarios end at the original four-second deadline; their `LaunchResult.timed_out=true`, no successful monitor outcome, and no quit are correctly represented as bounded negative outcomes, not normal successful exits. Disconnect and a real `KeyboardInterrupt` during native wait both show no returned success outcome and owned cleanup.

The callback’s first native wait starts only after the named-owner inspection and expects a healthy actual state in its input stream. Thus a Native binding must establish a matching healthy in-game/water baseline before relying on this helper. The run is a local helper fixture, not a built/running Native integration; current source pins do not establish runtime producer compatibility.

## 4. Manual QA matrix (artifact-backed re-read; no rerun)

| Scenario | Surface and invocation evidenced by run | Verdict | Evidence |
|---|---|---|---|
| Final 19-case causal gate | Pinned Windows PowerShell wrapper, `-Phase green-r2`, then actual Popen/WebSocket/stream fixture process | PASS for private helper scope | `green-r2-process.json`, `actual-observations-final.json`, all per-case raw logs/events/results |
| Teleport/hidden/shroud/replacement ordering | Same run; inspect actual raw wire events and render-release/native row ordering | PASS for fixture ordering | `green-r2/{teleport,hidden-healthy,presentation-shrouded-healthy,destroy-replacement-healthy}/` |
| Startup, later Wave, empty initial inventory, large complete stream | Same run; actual helper protocol and native-format rows | PASS for fixture path | `green-r2/{teleport-startup,teleport-later-wave,teleport-empty-inventory,teleport-complete-burst}/` |
| Wrong/stale/mirror/no-eligible, malformed/capped, disconnect, interrupt | Same run; expected bounded errors and cleanup, not successful evidence | PASS for rejection/cleanup behavior | matching `green-r2/teleport-*/{wire-events.jsonl,events.jsonl,control-monitor.json,actual-result.json}`; interrupt has explicit interrupt receipts and no `actual-result` success |
| Actual Game/native/video proof | No Game or GPU invocation authorized or performed | NOT RUN; remains an integration gate | no native runtime artifact in this private helper package |

## 5. Adversarial and limits

Stale-state, misleading acknowledgement versus actual state, delayed row availability, malformed rows, ordering errors, no-eligible cases, disconnected socket, interrupt propagation, capped/unfinished data, and teardown are represented in the sealed fixture evidence. The evidence does not probe concurrent log replacement at runtime, native logging contention under an actual Game, every exact ring/cap boundary, flaky repeats, or a real producer’s field cadence. Those remain open for the Native integration; they do not turn this private helper qualification into Game evidence. Prompt injection is not applicable to these fixed local protocol inputs. Dirty/stale artifact checks here are bounded to the sealed package and source pins, not a repository-wide worktree assertion.

## 6. Cleanup

This review created only this report and its adjacent cleanup receipt. It spawned no process, port, listener, environment, or temporary file; no import, test, build, Game, or GPU execution occurred. Read holds are released. No existing attempt or prior artifact was modified.
