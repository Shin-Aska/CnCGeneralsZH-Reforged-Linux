# Independent verification: private lifecycle controller CPU qualification

**Verdict: confirmed for the sealed helper-level GREEN claim.**  
**Code quality status: acceptable within the declared private helper scope; Native binding is not startable from this callback alone until an actual healthy in-game/water baseline is established.** No Game or GPU behavior is proved. No test was rerun, no private helper was imported, and no product or shared test file was changed.

## 1. Goal and ownership

Independently review the sealed `lifecycle-game-controller-private-01` CPU qualification, including prior PIN/RED and both preserved failed GREEN attempts, raw TCP/log evidence, final source freeze, and teardown. This is read-only; private sources were not applied or altered.

## 2. PIN and RED evidence

The canonical default monitor PIN is a real `Popen`/Control fixture run: one connection, status frames 300/600/900, screenshots at 300 and 600, then acknowledged quit; served lifecycle inspections are zero. Actual RED exercises teleport against that unchanged monitor: it returns frame 900/quit but leaves position at (10,20) and performs zero inspections. `red/teleport-healthy` plus `red-process.json` show a behavioral failure (child exit 1, reaped), not an import or construction error.

The RED archive table lists reconstructed fixture/test/qualifier/runner files; archive hashes match. The archive procedure and frozen pre-run hashes are in `red-source-archives.json` and `red-freeze.json`. The first GREEN failed before child launch because test setup constructed `Case("default")` unconditionally; diagnosis identifies this setup defect. GREEN-r1's positive flows and default PIN passed, but its duplicate fixture first emitted a complete valid group and only later emitted the duplicate. The controller could not predict future bytes. Its preserved diagnosis records that fixture flaw and later typed rejection. GREEN-r2 split the fixture inside the header and exposed both complete groups together while the request was pending. `lifecycle_monitor.py` and `inspector_log.py` are byte-identical between r1/r2; corrections changed fixture/test/runner inputs. These earlier attempts remain failures, not passes.

## 3. Constraints and Native startability

The private callback is a typed, bounded, one-connection monitor. It reads a new complete log group after each action, carries forward positive object/drawable IDs, and never treats queued acknowledgement as state. It uses one dummy 1/1 discovery query, exact schema fields, manager/nontruncation checks, 64-system and 65,536-byte fresh-stream caps, file identity/size checks, and advancing logic. Four fixed flows check teleport XY, hidden 1→0, presentation-shroud 1→0, and destroy/lookup-absence/replacement with distinct IDs. It does not claim game/object birth, current view, native-issued geometry, screenshots, or renderer success.

There is an integration prerequisite: after connecting, the callback immediately calls `Session.inspect`, whose first operation calls `status()` and requires `inGame=true`. It does not wait for a match transition, healthy water, or initial foam before the discovery inspection. The helper fixture is already live when the callback starts. Thus this GREEN is valid for the helper, but Native must establish and record an actual healthy in-game/water baseline before invoking this callback (or separately prove a wait seam); process/listener readiness alone is insufficient. The claim assigns healthy baseline/render checks to Native, so this is an integration precondition, not a contradiction in the helper result.

The reader rejects incomplete trailing rows and oversized new output. Runtime file replacement was not injected. Its stat/open sequence has a narrow race if another writer replaces the path between identity check and open; under an owned single-writer run this is reduced, but it is not a same-handle proof against concurrent replacement. Native must bound log volume, keep one owned writer, and preserve strict failure on changed/truncated files. Exact 64/65-row and 65,536-byte boundaries were not runtime-probed. Teleport uses exact float equality while native positions log three decimals; Native coordinates must be representable at that precision or be compared against emitted precision. The replacement flow validates fresh name and IDs but does not compare replacement XY to requested XY; do not claim replacement placement verification.

## 4. Verification performed

- Independently SHA-256 checked all 194 `executedEvidenceArtifacts` (141,018 bytes), all 47 `topLevelArtifacts` (159,546 bytes), and all 13 GREEN-r2 frozen inputs: zero mismatches.
- Current canonical inputs match frozen hashes: `isolation.py` F5B6F418D7E9F539C1827D317EA1C770CE3D5336B2C6C3FB71C49399EC190CE9; `control_monitor.py` CC345CCA65E878D866B447754A5DB73BD49A2F25C92EF7BE20DC248A9F818315; `control_client.py` 7BC287D6062494E24178A9495555560923C89692F52041BC4BF2DAD2067E3F55. Scenario source pins are ScenarioDrill 92F5AB59732450192A16995207BFFA6986E3897E4F190D757C54D339AFFD2437 and header 8F317B5DA89E20C61D6336C67B999BB14AB70D299201BA110F3C8D7F2BB5C55B.
- Correlated all 12 GREEN-r2 cases from raw wire/controller events, served state, returned result, and monitor outcome. Every case shows one accepted connection, release, and zero premature requests. Default PIN records 300/600/900, screenshot requests 300/600 and quit. Four positive flows record expected action/state progression and durable returned result. Seven negative inputs (duplicate, malformed, truncated, stale, missing-manager, summary-truncated, bad-id) yield typed errors, `outcome_returned=false`, and no quit.
- Stale/truncated use bounded 1.5-second monitor deadlines inside the six-second parent cap. GREEN-r2 process evidence records exit 0, reaped, 13.990s under 120,000ms. Teardown records 13 exact process identities (supervisor plus 12 fixture children), listener/release evidence, original births absent, CWD restored, environment unchanged, and bytecode absent. This is bounded evidence, not a machine-wide census.
- Static review confirms exact field sets, duplicate rejection, bounded typed identity parsing, manager/truncation/row-count validation, and explicit `UNKNOWN` values. Basedpyright and Ruff were not run.

## 5. Manual QA matrix (artifact-backed re-read; no rerun)

| Scenario | Criterion | Surface / exact invocation evidenced by sealed run | Verdict | Artifact references |
|---|---|---|---|---|
| `private-green-r2` | fixed lifecycle helper CPU behavior | Windows PowerShell 7: `pwsh.exe -NoProfile -File <sealed namespace>/run-cpu.ps1 -Phase green-r2`; wrapper invokes pinned Python with `-B <namespace>/qualify.py green-r2`, 120000 ms cap. Re-read only. | PASS for helper scope | `green-r2-process.json`, `green-r2/proof.json`, case raw streams/results |
| `default-pin-and-teleport-red` | unchanged default behavior and failing-first proof | Earlier sealed phase `-Phase red`; actual default and teleport fixture artifacts reviewed, not rerun. | PASS as PIN/RED evidence | `red/default-healthy/*`, `red/teleport-healthy/*`, `red-source-archives.json` |
| `native-binding-readiness` | integration precondition | Source path only: `make_lifecycle_monitor` → callback → `Session.inspect` → `status()` requires `inGame=true` immediately. No Native invocation. | NOT RUN; prerequisite identified | `lifecycle_monitor.py`, `DoneClaim.md` |

Adversarial matrix:

| Scenario | Criterion | Class | Expected behavior | Verdict | Artifact references |
|---|---|---|---|---|---|
| `bad-streams` | strict inspection | malformed/duplicate/truncated/stale/missing manager/incomplete summary/out-of-range ID | Typed failure; no successful outcome or quit | PASS for seven specific GREEN-r2 fixtures | corresponding `green-r2/teleport-*/{controller-events.jsonl,wire-events.jsonl,control-monitor.json,launch-result.json}` |
| `deadline` | bounded waits | stale/incomplete stream | Typed deadline inside parent cap; close/reap | PASS for recorded bounded cases | `green-r2/teleport-stale/*`, `green-r2/teleport-truncated/*`, process/cleanup |
| `file-replacement` | log identity guard | concurrent log replacement | Reject rather than accept stale data | NOT RUN; source guard only | `lifecycle_monitor.py:62-80`; no runtime artifact |
| `cancel-disconnect` | control/lifecycle cleanup | cancel or disconnect | Preserve typed error and cleanup | NOT RUN; do not inherit prior Route evidence | no private runtime artifact |
| `parser-boundaries` | bounded parser | 64/65 rows and 65,536-byte limit | Reject overflow; accept supported complete inventory | NOT RUN at boundary; source bounds reviewed | `inspector_log.py:82-85,143-161`; no boundary artifact |
| `prompt-injection` | test inputs | prompt injection | N/A: fixed local protocol cases have no external prompt/instruction-bearing input | NOT APPLICABLE | `fixture_game.py`, fixed test strings |
| `dirty-source` | sealed source consistency | stale/dirty source | Freeze and seal match reviewed sources | PASS for sealed inputs; no repo-wide dirty-state inspection performed | `green-r2-freeze.json`, `seal.json` |

## 6. UltraQA notes

Stale-state and misleading-success triggers were exercised: stale complete bytes before request do not become fresh proof, ACK does not advance the controller, and an unreturned default outcome is explicitly distinct from a returned result. Malformed/truncated/missing-manager/duplicate and oversized identity inputs are typed failures. Deadline and original-process cleanup are evidenced. File replacement, disconnect, cancellation, and exact buffer/row boundaries were not runtime-injected and are not passed here. No prompt-injection surface exists in these fixed protocol cases. No flakiness/retry claim follows from one GREEN-r2 run. Actual Game/menu/FOW/native rendering and healthy foam remain outside scope.

## 7. Artifacts and cleanup

Only this report and its JSON cleanup receipt were created. No process, listener, port, environment, or temporary artifact was spawned; no imports/build/test/Game/GPU operation was performed. Source read holds are released. No prior artifact or cleanup state was modified.
