# Independent 3C-only current-parent source review

Verdict: **confirmed — bounded source composition only**. The delta remains UNAPPLIED. No compiler, runtime, availability RED, or Task 8 pass is claimed. Applying it still requires a meaningful enabled MAIN availability RED, separate approval, and Native ownership. The sorted E588/combined delta remains separate.

## Exact inputs and scenario

Reviewed all five files in negative-3c-only-current-author; independently checked its four sealed entries, seal hash, and all nine direct input pins. This is nine-pin scope, not a full cohort/executable closure. Actual invocation was persistent Node fs.readFile, crypto.createHash('sha256').update(actualBytes).digest('hex'), and in-memory literal hunk matching/reconstruction; no candidate import, git apply/check, child, or runtime invocation. Binary observables are hash equality, exact byte equality, unique literal contexts, and refused stale contexts.

- Claim: 2D9522D977D08303E02BECDA31C1636447756BE6FA9CBFE45708F43A6F2BC043
- Delta (7684 bytes): FAACCBBA9ABBA89735D714BC68C6C059DA14215006830C052475BC41D05DEF07
- Manifest: 91100396C90CB4B8C54B92C620D1E2C9677E780A54997A52CA74416603ABDB79
- Author cleanup: 2F519E32AB9EA91E20AC59EF463C420EDAFCC4DEF71F020CAE82A959551BA415
- Seal: C683C6D0E4FD64B27E0F8BFACFFCE0A3E8A4B9CDB6E0617532784958A3D87229

## Direct reconstruction

The saved delta has exactly one W3DParticleSys.cpp path, six hunks, 72 additions and five removals, CRLF with no bare LF. All old hunk contexts occur uniquely in the actual 87821-byte current source DC9B619BAA055901731602DCACE0DED0A59584564AD9E50B57418293B6D596AA. Forward reconstruction gives exactly 93147 bytes / 1995 CRLF and SHA 1C62321725FDF420DC54052E51081D0C5FB1F5912C5F1A760EFFA9299CBA943E. Reverse reconstruction is byte-identical to the actual current source (1928 CRLF). No source copy was written.

Independently reversing actual F912C664607ED580557A33D9697EB99FB5AC9D8907CC32128EDE30A1015FA883 recovers D414FCE9C8C8CF824EDE40302B2C9038ED83405FCCAC8A824910AA157D24240E; applying actual old 5C500F90DA456A2C93F62AFFD2200607EBF55619E741833CFE3956242E58814A to that baseline yields the exact same new postimage. Actual 3C1C910C1E0F49E5234496F04F74CCC5BC574EA1119606095E90B8B2A83F7FDE separately reconstructs the old 366ADCB49F9DAE003781264345E97FAB708C0E167774A616D899A7A5A4745C75 target. Each of D414, 366ADC, and already-applied 1C623 fails unique old-context matching for the new delta. These are source applicability controls, not executed apply/tests.

## Preserved semantics and boundaries

The complete new candidate helper is byte-identical after LF normalization to the reconstructed 3C donor (4659 bytes, F1FDB4770FC52BE361EEF2BEE88D3B7FC637BEA735376E2935F0728D51AFCE7B). The full current B08 probe class, A57 canonical observer, and replaceLegacyVesselFoam function compare byte-identically. There is still exactly one m_pointGroup->Render call, with identical arguments; Insert_Sorted_Billboards arguments are identical. No sortingrenderer.cpp or dx8wrapper.cpp patch is present; both actual pins remain unchanged. Prior combined independent report B0F72DBA79A9948699ACCC2B7CF003711A42772E701EE6902A4D9ED1C3429C9E provides donor-chain context; the new delta was independently reconstructed here.

Virtual postimage lines 206–265 keep the Windows-guarded, exact template-filter, cached trace=1, capture/control/live single-player/native-active gates. Both raw attachment IDs must be INVALID before view queries/log budget consumption. Canonical cap512 stays in the identical observer; candidate cap128/reservation remain donor-exact. The resolver restores/flushed view in Decision at 1686 before either decision trace, skip, or draw. The zero-initialized local span at 1922 is sampled once iff canonical/candidate/probe requests it; all fields are populated before conditional returned callbacks at 1936–1938. Pointer use is synchronous/local; no new lifetime or backend API is introduced. The sole draw and queue retain their original ordering/arguments. Default-off diagnostics and non-Windows branches remain source-preserved.

At 1688 MAIN/point-decision precedes fade rejection (1700–1702) and point building; the helper has no positive particle-count gate. A SCENARIO created=1 receipt therefore cannot establish eligible native-positive MAIN availability. Future enabled fresh observations must establish actual template/attachment/gates and uncapped rows; start-frame is not birth, missing/capped/unsampled rows are not native zero, sorted-queued-unattributed is not an issued draw, and issued span is not GPU completion. No future eligibility contract is changed by this review.

## Adversarial and cleanup disposition

PASS source checks: stale/current/already-applied parents, dirty-input preservation through final pin reread, two-versus-three-donor scope, independent128/512 caps, local span initialization and conditional use, source-only versus compiled/runtime distinction. Runtime malformed-input, prompt execution, cancellation, long command, flaky test and interruption scenarios were not run: no external process or untrusted text execution occurred. This is not runtime coverage.

Captured results and all nine full pins are in the adjacent independent cleanup receipt. Only this report and that receipt were created; zero processes/ports/environment/CWD/cache/temp/copies/deletions or historical cleanup retries. Read holds released.
