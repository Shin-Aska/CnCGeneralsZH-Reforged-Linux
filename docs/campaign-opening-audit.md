# Campaign opening audio and control audit

**Status: static review covers all 36 selected slots; targeted runtime traces cover selected cases; all three shared fixes are implemented and validated. A separate terrain startup crash fix is validated; the expanded startup/render batch passed all 36 selected mission starts (original 21, then Zero Hour 15).** This report covers the 21 original Generals campaign slots and 15 Zero Hour campaign slots. It does not claim all-mission cinematic/action-flow runtime coverage or audible playthrough.

## Campaign configuration and scope

The selectable original campaigns are `GEN_USA`, `GEN_GLA`, and `GEN_CHINA` from `CampaignReforged.ini`. The original game's archive defines the same campaign keys that Zero Hour uses (`USA`, `GLA`, `China`); those stock original keys are superseded by Zero Hour's definitions. Reforged renames the original campaigns to `GEN_*` so both sets can appear. Training, Generals Challenge, and demo campaigns are outside this 36-slot audit.

The mission order is the campaign slot order, not a numeric map sequence. Original USA Mission05 uses USA06, and original GLA skips Mission03. Each original slot includes its configured briefing voice, intro movie, and `VoiceLength`. Zero Hour's 15 selected slots have `IntroMovie` values but no `BriefingVoice` or `VoiceLength` fields in its campaign blocks.

### Original Generals (21 slots)

| Campaign | Slot | Map | IntroMovie | BriefingVoice | VoiceLength |
| --- | --- | --- | --- | --- | ---: |
| GEN_USA | Mission01 | `USA01/USA01.map` | USACampaign1 | BriefingUSA01 | 17 |
| GEN_USA | Mission02 | `USA02/USA02.map` | USACampaign2 | BriefingUSA02 | 12 |
| GEN_USA | Mission03 | `USA03/USA03.map` | USACampaign3 | BriefingUSA03 | 15 |
| GEN_USA | Mission04 | `USA04/USA04.map` | USACampaign4 | BriefingUSA04 | 14 |
| GEN_USA | Mission05 | `USA06/USA06.map` | USACampaign5 | BriefingUSA06 | 13 |
| GEN_USA | Mission06 | `USA07/USA07.map` | USACampaign6 | BriefingUSA07 | 12 |
| GEN_USA | Mission07 | `USA08/USA08.map` | USACampaign7 | BriefingUSA08 | 13 |
| GEN_GLA | Mission01 | `GLA01/GLA01.map` | GLACampaign1 | BriefingGLA01 | 26 |
| GEN_GLA | Mission02 | `GLA02/GLA02.map` | GLACampaign2 | BriefingGLA02 | 16 |
| GEN_GLA | Mission04 | `GLA04/GLA04.map` | GLACampaign4 | BriefingGLA04 | 15 |
| GEN_GLA | Mission05 | `GLA05/GLA05.map` | GLACampaign5 | BriefingGLA05 | 15 |
| GEN_GLA | Mission06 | `GLA06/GLA06.map` | GLACampaign6 | BriefingGLA06 | 13 |
| GEN_GLA | Mission07 | `GLA07/GLA07.map` | GLACampaign7 | BriefingGLA07 | 16 |
| GEN_GLA | Mission08 | `GLA08/GLA08.map` | GLACampaign8 | BriefingGLA08 | 19 |
| GEN_CHINA | Mission01 | `CHI01/CHI01.map` | ChinaCampaign1 | BriefingChina01 | 17 |
| GEN_CHINA | Mission02 | `CHI02/CHI02.map` | ChinaCampaign2 | BriefingChina02 | 13 |
| GEN_CHINA | Mission03 | `CHI03/CHI03.map` | ChinaCampaign3 | BriefingChina03 | 19 |
| GEN_CHINA | Mission04 | `CHI04/CHI04.map` | ChinaCampaign4 | BriefingChina04 | 18 |
| GEN_CHINA | Mission05 | `CHI05/CHI05.map` | ChinaCampaign5 | BriefingChina05 | 15 |
| GEN_CHINA | Mission06 | `CHI06/CHI06.map` | ChinaCampaign6 | BriefingChina06 | 16 |
| GEN_CHINA | Mission07 | `CHI07/CHI07.map` | ChinaCampaign7 | BriefingChina07 | 19 |

Original map paths resolve in `ZH_Generals/maps.big`.

### Zero Hour (15 slots)

| Campaign | Slot | Map | IntroMovie |
| --- | --- | --- | --- |
| USA | Mission01 | `MD_USA01/MD_USA01.map` | MD_USA01 |
| USA | Mission02 | `MD_USA02/MD_USA02.map` | MD_USA02 |
| USA | Mission03 | `MD_USA03/MD_USA03.map` | MD_USA03 |
| USA | Mission04 | `MD_USA04/MD_USA04.map` | MD_USA04 |
| USA | Mission05 | `MD_USA05/MD_USA05.map` | MD_USA05 |
| GLA | Mission01 | `MD_GLA01/MD_GLA01.map` | MD_GLA01 |
| GLA | Mission02 | `MD_GLA02/MD_GLA02.map` | MD_GLA02 |
| GLA | Mission03 | `MD_GLA03/MD_GLA03.map` | MD_GLA03 |
| GLA | Mission04 | `MD_GLA04/MD_GLA04.map` | MD_GLA04 |
| GLA | Mission05 | `MD_GLA05/MD_GLA05.map` | MD_GLA05 |
| China | Mission01 | `MD_CHI01/MD_CHI01.map` | MD_China01 |
| China | Mission02 | `MD_CHI02/MD_CHI02.map` | MD_China02 |
| China | Mission03 | `MD_CHI03/MD_CHI03.map` | MD_China03 |
| China | Mission04 | `MD_CHI04/MD_CHI04.map` | MD_China04 |
| China | Mission05 | `MD_CHI05/MD_CHI05.map` | MD_China05 |

Zero Hour map paths resolve in `MapsZH.big`. Same-prefix `CINE`, `INTRO`, `END`, and `MID` map siblings were treated as candidate cutscene assets where present; sibling naming alone does not establish that campaign flow loads them. The campaign `IntroMovie` labels refer to video definitions, not proof that a same-prefix map is played.

## Findings and dispositions

- **Frozen opening timers:** the shared countdown-timer phase issue was exposed by the Zero Hour USA01 intro while the world was frozen; it was not a map-specific script mistake. The prior frozen-clock phase correction applies across openings that freeze the world. GLA01's opening wait is 300 script passes. At 40 script passes per second while the cinematic pace is set, that is 7.5 wall-clock seconds; the timer correction preserves the retail pass count.
- **Preserved cross-speaker chatter:** the user accepts overlaps between different speakers. Original USA01's runtime call order has Hawk04 at world frame 814 and Commander01 at frame 874; no authoring rewrite was made. Original China02 similarly calls RedChat06 then RedGuard101 30 logic frames later. These traces establish requests, not rendered audible waveforms.
- **Other original samples:** headless action traces cover original USA03's Crusader cues and GLA05's Chatter03/02. Calls from the same voice family can still refer to different actors; overlap alone does not establish an error. The other original missions received static script-flow review, not mission-script-flow runtime or audible verification; their later startup checks only cover initialization and early passes.
- **Zero Hour GLA02:** the protected early chain is safe at the configured 25 retail FPS. Later Kassad-demo dialogue depends on object/team routes and convoy events, so static timer edges do not prove a collision. No audio change is indicated by this review.
- **Zero Hour China03 completion wait:** the headless trace found `MisCHI03xBrf04` first polled at frame 2520 with a 10,922.667 ms deadline at frame 3175, then requested at frame 2914. Before the fix, the wait completed at 3175 and XO10 was requested at 3180, advancing dialogue before Brf04 could finish. After the fix, the deadline starts from the logical cue request: Brf04 completes at 3569 and XO10 is requested at 3574. Audio was disabled in the trace, so it proves logical timer ordering, not an audible cutoff. The fix invalidates only an existing exact-name completion wait on logical `SPEECH_PLAY`; it preserves the unplayed-name fallback and changes no device state. Focused validation: script engine 5 tests / 292 checks passed; the no-invalidation mutant failed 1 test / 39 checks. Combined validation is recorded below.
- **Shared resident-stream readiness fix:** a resident speech stream can bypass queueing behind an unrelated loader job. The held-worker regression test passes, and a mutant restoring the old path fails. The audio manager suite passed 13 tests / 111 checks; the asset-free readiness CTest passed.
- **Original Generals load-screen voice:** all 21 original campaign slots configure a nonempty `BriefingVoice`. Before the fix, the Linux path did not start it: the movie-frame `moveWindows(frame)` call was commented out and the minimum-spec start was absent. The shipped Generals path calls `moveWindows` and force-starts the external event at movie frame 255 (8.5 s at 30 FPS). Its distinct minimum-spec path starts the event before its narration wait at source line 558; 558 is a source line, not a movie frame. The implemented fix starts only configured voices, once, and budgets against remaining movie/load time without restoring legacy window animations. Before the fix, `VoiceLength` only delayed the low-memory path and did not start the configured voice. Now it contributes a minimum completion budget alongside measured localized audio length in both the movie and minimum-spec paths. All 36 configured intro movies have an audio stream, but that does not establish narration. For example, GLA01's 25.406 s external WAV is longer than the movie's 18.167 s remaining from frame 255. ZH campaign slots have no external `BriefingVoice`, and their loading path adds no hold.
- **Other reviewed Zero Hour cases:** USA02's permitted Cin01/Cin02 cross-talk is preserved; USA03 has object-triggered opening branches that need runtime evidence; USA04 has a small allowed-overlap timing candidate, not a demonstrated audible fault; USA05's normal protected gaps are adequate while turbo may drop later protected EVA cues. GLA01's chase exchanges are authored allowed overlaps; GLA03 has the same pre-query pattern as China03 but the normal two-second gap masks it; GLA04's pace-transition overlap is allowed; GLA05's alternate intro group is inactive. China01 and China02 show no normal timer shortfall; China04 has no active `SPEECH_PLAY` in its selected intro group; the normal path plays its movie, but embedded audio content was not inspected. China05's protected exposition continues after input returns without a following cue scheduled to truncate it.
- **No speculative changes:** original USA08's alternate-voice wait may add a pause after the alternate cue; this is outside the reported symptom and remains unchanged. Conditional or object-driven paths without runtime confirmation are not treated as proven failures.

The engine also has source-reviewed opening start, return-to-control, FPS override, and world-freeze behavior across the campaign maps. Zero Hour's 15 openings were statically simulated in normal and turbo timing modes; only selected missions have action traces. Static simulation treats unsupported object/camera/AI predicates conservatively and does not run real audio loading. No all-36 cinematic/action-flow runtime pass or audible playthrough is claimed; the separate startup/render batch is reported below. Movie tracks were checked for audio streams, which does not establish that they contain mission narration. A focused GLA01 loading trace opened the movie and started `BriefingGLA01` once on the normal `skipMovie=false` path; the loader continued 26.067 wall-clock seconds afterward. Audio used the null backend, so this does not verify audibility. A Zero Hour USA01 trace observed zero forced external voices and no added hold.

## Per-mission opening review

This table records each selected main map’s opening entry point, control-return path, and reviewed pacing/disposition. `Action trace` means scripted calls/control were traced headlessly; it does not mean audible output was verified. For Zero Hour, a missing simulated return is labeled unresolved where object-triggered reachability prevents a static conclusion.

### Original Generals (21)

| Slot / map | Opening → control return | Pace | Finding | Evidence |
| --- | --- | --- | --- | --- |
| GEN_USA Mission01 / USA01 | Set Up Camera → Drop Bars | 20 -> 0 retail FPS | No explicit FREEZE_TIME. Hawk04→Commander01 is different-voice authored overlap, runtime call sequence verified; no rewrite. | Action trace |
| GEN_USA Mission02 / USA02 | 1.0_Setup → INTRO_End_Letterbox (Enable User) | 24 -> 0 retail FPS | Timed dialogue spans the opening. BurtonBriefing wait is gated by later selection and Eva01 threshold200, after return threshold315; no freeze found. | Static review |
| GEN_USA Mission03 / USA03 | INTROCameraSetup+Timers → INTROReturnControlToPlayer | 25 -> 0 retail FPS | Threshold dialogue; Crusader1xx same voice-family overlap confirmed by calls, actor identity/intent unresolved. Do not treat waveform overlap alone as defect. | Action trace |
| GEN_USA Mission04 / USA04 | 1.0_OpeningSceneSetup → 10.0_TempGameStart | 20 -> 0 retail FPS | Return script initially disabled, waits for camera completion; enabled through camera sequence. Scripted voices in Speech group include later paths; no full runtime graph proof. | Static review |
| GEN_USA Mission05 / USA06 | INTRO_SceneSetUp → Drop the Bars, Return Control | 20 -> 30 retail FPS | Snapshots explicitly freeze/unfreeze; shared script-pass phase fix applies. Named object/team callbacks affect dialogue reachability. | Static review |
| GEN_USA Mission06 / USA07 | INTRO_Setup (Set all start conditions - Disable User) → INTRO_End_Letterbox (Enable User) | 20 -> 30 retail FPS | Parallel initial JGS setup/end scripts also gate input; inactive dialogue alternatives excluded from default path, active battle chatter follows timers. | Static review |
| GEN_USA Mission07 / USA08 | INTRO_Setup → INTRO_End_Setup (E), INTRO_End_Setup (N), INTRO_End_Setup (H) | 20 -> 0 retail FPS | Frozen countdown fixed; post-intro General03 Alt wait may add a pause, unchanged. | Static review |
| GEN_GLA Mission01 / GLA01 | Set Up Camera → Drop Bars | 20 -> 0 retail FPS | Frozen opening; G01→G02 wait is 300 script passes (7.5 wall s at 40 passes/s). | Action trace |
| GEN_GLA Mission02 / GLA02 | INTROFadeUpFromBlack → INTROPlayerControl | 25 -> 0 retail FPS | Cin01 enables the Cin02→03→04→05 chain; the headless trace confirms Cin05→Cin06 calls 312 logic frames apart. Timing margins alone do not establish audible truncation, so the authored exchange is preserved. | Action trace |
| GEN_GLA Mission04 / GLA04 | INTRO_setup (Sc. 0) C → INTRO_resumeGamePlay (Sc.5) | 20 -> 0 retail FPS | Inactive INTRO_setup alternative distinguished from active C; radio chatter uses separate dispatch/unit events. Dialog General01/02 completion wait begins only after INTRO_DONE. | Static review |
| GEN_GLA Mission05 / GLA05 | INTRO_Setup (Set all start conditions - Disable User) → INTRO_End_Letterbox (Enable User) | 20 -> 0 retail FPS | Initially active Timers Hidden sets FPS20. Chatter03→02 confirmed at frame1800→1860; same voice family may be separate units; authored overlap permitted. | Action trace |
| GEN_GLA Mission06 / GLA06 | INTRO_Setup → INTRO_End (E), INTRO_End (N), INTRO_End (H) | 20 -> 0 retail FPS | Three return alternatives are mutually exclusive by difficulty; introductory General01–03 use timer thresholds. | Static review |
| GEN_GLA Mission07 / GLA07 | Moments 01_25 → Moments 14 | 20 -> 0 retail FPS | Timer-driven Moments camera/input sequence; briefing General13/09/10/11/12 uses separate Intro Dialog Timer, and later unit dialogue is conditional. | Static review |
| GEN_GLA Mission08 / GLA08 | INTRO_Setup (Set all start conditions - Disable User) → INTRO_End_Letterbox (Enable User) | 23 -> 0 retail FPS | Opening cues Jarmen01/03/04/05 are timer-triggered. Explicit freezes belong to separate later MiniCini, not opening. | Static review |
| GEN_CHINA Mission01 / CHI01 | INTRO_Setup → INTROReturnToPlayerControl | 23 -> 0 retail FPS | IntroTimer drives terrorist/officer/battlemaster chatter; MissionBriefing1 follows INTRO_DONE. Other dynamically enabled scripts remain contingent. | Static review |
| GEN_CHINA Mission02 / CHI02 | INTROSetup → INTROReturnToPlayerControl_alt | 23 -> 0 retail FPS | Inactive no-suffix return is distinct from active _alt. RedChat06→RedGuard101 actual call gap30 logic frames, permissible cross-speaker overlap. | Action trace |
| GEN_CHINA Mission03 / CHI03 | (v0.3)1.1_Setup (Scene 1) → (v0.3)3.1_End_Cinematic | 20 -> 0 retail FPS | The opening Cinematic group is active; its individual speech scripts start disabled and are enabled by timers. No opening completion-poll defect is established. Later Dam Break waits are outside this review. | Static review |
| GEN_CHINA Mission04 / CHI04 | INTRO1_Setup → MissionBegin | 25 -> 30 retail FPS | MissionBegin is separate Map_Initialize input return after StingerBriefingDelay; IntroDone may disable input again. Explicit FREEZE_TIME belongs to later MiG_Attack_ChemPlant. | Static review |
| GEN_CHINA Mission05 / CHI05 | JGS - Intro Extra Start → JGS - Intro Extra End, INTRO_resumeGamePlay (Sc.3.1) | 20 -> 30 retail FPS | Two return actions share INTRO_DONE sequencing; INTRO_AUDIO bomber-command chatter uses named aud timers. | Static review |
| GEN_CHINA Mission06 / CHI06 | INTRO_Setup (Set all start conditions - Disable User) → INTRO_End_Letterbox (Enable User) | 25 -> 0 retail FPS | No-train-spawn setup is initially inactive. Lotus replay cue is initially disabled and separately enabled from camera return; further conditions control whether it plays. | Static review |
| GEN_CHINA Mission07 / CHI07 | INTROSetupScripts → INTROReturnToPlayerControl | 23 -> 0 retail FPS | Return is alternative OR of launcher destroyed or timer expiry; inactive C return is not default. Audio follows named timers and unit destruction. | Static review |

### Zero Hour (15)

| Slot / map | Opening → control return | Pace and reviewed finding | Evidence |
| --- | --- | --- | --- |
| USA MD_USA01 | CINE_MapSetup → ReturnToPlayerControl | 20 retail FPS; frozen-time countdown phase fixed; briefing cadence retained. | Trace-backed prior regression; static review after fix |
| USA MD_USA02 | INTRO_Setup C → INTRO_End CINE | 22 retail FPS; permitted Cin01/Cin02 cross-talk; protected follow-ups have margin. | Static review |
| USA MD_USA03 | CINE_Setup → object-triggered/conditional; static handoff unresolved | Conditional 23 retail FPS; object-triggered branches limit timing evidence; no fault proven. | Static review; object paths unresolved |
| USA MD_USA04 | Cinematic_Init → object-triggered/conditional; static handoff unresolved | Normal pace; ~0.067 s allowed-overlap candidate, no audible fault established. | Static review; candidate only |
| USA MD_USA05 | INTRO_Setup → Move camera to begin location | 30 retail FPS; normal protected gaps adequate; turbo may drop later protected EVA cues. | Static review |
| GLA MD_GLA01 | Scene Setup C → object-triggered/conditional; static handoff unresolved | 24 retail FPS; radio chase overlap allowed; six object predicates limit handoff review. | Static review; object handoff unresolved |
| GLA MD_GLA02 | CINE_Setup → object-triggered/conditional; static handoff unresolved | 25 retail FPS; protected Cin01–03 chain safe; later object/team dialogue needs runtime. | Static review; later path needs runtime |
| GLA MD_GLA03 | CINE_SetUp → CINE_ReturnControl | 24 retail FPS; pre-query wait exists, normal two-second gap masks the deadline issue. | Static review |
| GLA MD_GLA04 | CINE_Setup → CINE_ReturnToPlayer | 24 retail FPS then normal pace; allowed handoff overlap; no protected cue dropped. | Static review |
| GLA MD_GLA05 | INTRO_Setup → INTRO_End Setup | Normal pace; inactive alternative intro excluded; active chatter overlap is allowed. | Static review |
| China MD_CHI01 | CINE_Setup&Camera01 C → CINE_PlayerStart | 25 retail FPS; slow-pace dialogue chain safe; no completion poll or freeze. | Static review |
| China MD_CHI02 | Scene Setup C → Force Radar, Enable Input | 24→30 retail FPS; no normal timer shortfall; action/object cues remain conditional. | Static review; world/AI predicates omitted |
| China MD_CHI03 | CINE_Setup → CINE_PlayerControl | Normal 60 FPS; pre-query deadline proven by headless trace; fixed deadline follows cue request. | Headless logical trace; audio disabled |
| China MD_CHI04 | INTRO_SETUP C → INTRO_END | 24→30 retail FPS; no active SPEECH_PLAY in intro; movie audio content unverified. | Static review |
| China MD_CHI05 | CINE_Setup → EXPO_ReturnControl_TimerExpired | 25 retail FPS then normal pace; exposition continues after handoff; no next cue cuts it. | Static review |

## Modern Water crash and mission-start validation

The user's original USA01 launch crash log matches the older ELF and the Modern Water `DistanceWord` path: two empty vectors compared equal, allowing a null sample read. The fix changes only the in-range sample predicate in `Libraries/Include/Platform/WaterCleanShore.h`; it does not change mission logic or simulation. The regression test covers nine checks: the old predicate reproduces SIGSEGV (-11), while the corrected predicate passes. The final Release build for this fix has SHA-256 `099cc9177fc629113021289939008a1d50d8c7ca7f549de6f5bb89a34c984284`. Full CTest reported 139 passed, 23 skipped, and two sandbox socket failures; both socket checks passed on the outside-sandbox rerun. Source diff check passed.

The user's crash and old-unit regression prove the defect, but the isolated batch has not reproduced that exact active level-0 SIGSEGV. Its purpose is a bounded startup check: confirm the expected map initializes, prewarm returns, at least three Vulkan `Present` calls succeed, scripts advance after initialization even when world time is frozen, drawing advances, and the engine exits cleanly. This checks startup/render stability, not mission completion, campaign-menu briefing behavior, or audible dialogue.

The batch runs the original 21 missions first, then Zero Hour's 15, using copied user settings (Modern Water; classic off; shore turning and filtering on; FFT quality 2 / 256; turbo on) in isolated 800x600 runs. `ZH_RENDER_PRELOAD=0` skips optional shader warmup only; map terrain/water prewarm still runs. Rendering uses Vulkan software because physical GPU access is unavailable. No game settings, assets, or user data were edited. The batch uses the final crash-fix ELF above. The criterion records successful present returns, rather than attempted frames.

**Batch result: original 21/21 and Zero Hour 15/15 passed (36/36 total); 0 failed, timed out, or remained unverified. Terra independently audited all 36 results.** The results use one immutable executable SHA-256 (`099cc9177fc629113021289939008a1d50d8c7ca7f549de6f5bb89a34c984284`); originals ran first, followed by Zero Hour. Each pass recorded the expected terrain logic map, a successful real-water prewarm return and map initialization, at least four successful Vulkan Present returns, at least six post-init script passes, positive post-init draw growth, and clean engine exit code 0. Across the batch: minimum four successful Present returns, minimum six script passes, minimum 312 new draws, 727.35 wall-clock seconds total (15.19–45.54 s per map). This is a startup/render pass for all selected maps, not a cinematic/control-handoff, audible, or full-playthrough pass.

| Order | Campaign slot | Map | Result | Script passes | Successful presents | New draws | Wall seconds |
| ---: | --- | --- | --- | ---: | ---: | ---: | ---: |
| 1 | GEN_USA Mission01 | `Maps/USA01/USA01.map` | PASS | 6 | 4 | 400 | 15.19 |
| 2 | GEN_USA Mission02 | `Maps/USA02/USA02.map` | PASS | 8 | 4 | 778 | 21.75 |
| 3 | GEN_USA Mission03 | `Maps/USA03/USA03.map` | PASS | 7 | 4 | 312 | 16.39 |
| 4 | GEN_USA Mission04 | `Maps/USA04/USA04.map` | PASS | 7 | 4 | 3823 | 19.5 |
| 5 | GEN_USA Mission05 | `Maps/USA06/USA06.map` | PASS | 6 | 4 | 1172 | 40.33 |
| 6 | GEN_USA Mission06 | `Maps/USA07/USA07.map` | PASS | 6 | 4 | 1711 | 17.64 |
| 7 | GEN_USA Mission07 | `Maps/USA08/USA08.map` | PASS | 10 | 4 | 1067 | 18.8 |
| 8 | GEN_GLA Mission01 | `Maps/GLA01/GLA01.map` | PASS | 6 | 4 | 818 | 40.28 |
| 9 | GEN_GLA Mission02 | `Maps/GLA02/GLA02.map` | PASS | 7 | 4 | 2041 | 45.54 |
| 10 | GEN_GLA Mission04 | `Maps/GLA04/GLA04.map` | PASS | 11 | 4 | 6408 | 29.26 |
| 11 | GEN_GLA Mission05 | `Maps/GLA05/GLA05.map` | PASS | 6 | 4 | 1226 | 17.34 |
| 12 | GEN_GLA Mission06 | `Maps/GLA06/GLA06.map` | PASS | 7 | 4 | 580 | 18.7 |
| 13 | GEN_GLA Mission07 | `Maps/GLA07/GLA07.map` | PASS | 6 | 4 | 1013 | 16.59 |
| 14 | GEN_GLA Mission08 | `Maps/GLA08/GLA08.map` | PASS | 7 | 4 | 1840 | 18.1 |
| 15 | GEN_CHINA Mission01 | `Maps/CHI01/CHI01.map` | PASS | 8 | 4 | 2327 | 18.3 |
| 16 | GEN_CHINA Mission02 | `Maps/CHI02/CHI02.map` | PASS | 8 | 4 | 1219 | 20.4 |
| 17 | GEN_CHINA Mission03 | `Maps/CHI03/CHI03.map` | PASS | 6 | 4 | 1046 | 16.29 |
| 18 | GEN_CHINA Mission04 | `Maps/CHI04/CHI04.map` | PASS | 7 | 4 | 771 | 16.59 |
| 19 | GEN_CHINA Mission05 | `Maps/CHI05/CHI05.map` | PASS | 6 | 4 | 737 | 16.19 |
| 20 | GEN_CHINA Mission06 | `Maps/CHI06/CHI06.map` | PASS | 7 | 4 | 1064 | 17.29 |
| 21 | GEN_CHINA Mission07 | `Maps/CHI07/CHI07.map` | PASS | 7 | 4 | 669 | 15.94 |
| 22 | USA Mission01 | `Maps/MD_USA01/MD_USA01.map` | PASS | 7 | 6 | 1884 | 17.94 |
| 23 | USA Mission02 | `Maps/MD_USA02/MD_USA02.map` | PASS | 8 | 4 | 3356 | 17.29 |
| 24 | USA Mission03 | `Maps/MD_USA03/MD_USA03.map` | PASS | 8 | 4 | 547 | 16.04 |
| 25 | USA Mission04 | `Maps/MD_USA04/MD_USA04.map` | PASS | 9 | 4 | 1683 | 16.74 |
| 26 | USA Mission05 | `Maps/MD_USA05/MD_USA05.map` | PASS | 8 | 4 | 883 | 17.14 |
| 27 | GLA Mission01 | `Maps/MD_GLA01/MD_GLA01.map` | PASS | 11 | 4 | 2530 | 16.54 |
| 28 | GLA Mission02 | `Maps/MD_GLA02/MD_GLA02.map` | PASS | 9 | 4 | 1124 | 16.19 |
| 29 | GLA Mission03 | `Maps/MD_GLA03/MD_GLA03.map` | PASS | 9 | 4 | 922 | 17.64 |
| 30 | GLA Mission04 | `Maps/MD_GLA04/MD_GLA04.map` | PASS | 9 | 6 | 1932 | 17.24 |
| 31 | GLA Mission05 | `Maps/MD_GLA05/MD_GLA05.map` | PASS | 9 | 4 | 2693 | 20.5 |
| 32 | China Mission01 | `Maps/MD_CHI01/MD_CHI01.map` | PASS | 8 | 4 | 836 | 19.1 |
| 33 | China Mission02 | `Maps/MD_CHI02/MD_CHI02.map` | PASS | 7 | 4 | 1481 | 21.85 |
| 34 | China Mission03 | `Maps/MD_CHI03/MD_CHI03.map` | PASS | 8 | 4 | 685 | 23.8 |
| 35 | China Mission04 | `Maps/MD_CHI04/MD_CHI04.map` | PASS | 7 | 4 | 1007 | 16.49 |
| 36 | China Mission05 | `Maps/MD_CHI05/MD_CHI05.map` | PASS | 7 | 4 | 1189 | 16.44 |

The full-settings 1920 USA01 anchor reached script input/UI enable at frame 3803 and a separate present frame 301. Heading-level samples 7 and 21 were inactive because their source was absent or fields were empty. The sweep did not log active level-0 execution for those paths, so it does not claim to reproduce the specific runtime branch that triggered the user's crash. Machine-readable batch output and per-run logs are under ignored `build-linux/campaign-crash-evidence/batch/` (`manifest.json`, `results.json`, `results.csv`, `summary.json`, `summary.md`, and `*.log`). The verified executable is staged at `build-linux/ZeroHourReforged/bin/generals` with the same SHA-256 as the batch. The actual home/Steam installation remains unchanged; no user installation was modified.

## Sources and reproduction artifacts

- Campaign definitions: `GeneralsMD/Code/Data/INI/CampaignReforged.ini`; original `ZH_Generals/INI.big:Data/INI/Campaign.ini`; Zero Hour `INIZH.big:Data/INI/Campaign.ini`.
- Speech definitions: `GeneralsMD/Code/Data/INI/SpeechReforged.ini` supplements events omitted by Zero Hour's base `Speech.ini`. The base original `Speech.ini` shares the `Data/INI/Speech.ini` path and is shadowed by Zero Hour's file in the combined install; effective event definitions must account for the Reforged overlay. All original campaign briefing event names resolve through the active event definitions.
- Source map archives: original `ZH_Generals/maps.big`; Zero Hour `MapsZH.big`. The archive list/extract utility is `GeneralsMD/Code/Tools/bigfile.py`; shipped EAR maps can be unpacked with `Tools/RandomMapLab/mapstyle.py::unpack_map`.
- Load-screen source: `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/LoadScreen.cpp` contains the restored movie and minimum-spec briefing starts. The frame-255 cue is a movie frame. The distinct minimum-spec start before the narration wait is in the original Generals source at `Generals/Code/GameEngine/Source/GameClient/GUI/LoadScreen.cpp:558`; this line number does not refer to the Reforged source file. A temporary `ffprobe` audit checked stream metadata; audio-stream presence does not establish speech content. The probe and its metadata were under `/tmp` and were lost during USB recovery.
- Durable validation logs are under ignored `build-linux/campaign-audit-evidence/`, including `final-ctest.log`, `final-network-rerun.log`, `final-replay.log`, `gla-focus.log`, `zh-focus.log`, and `manager-tests.log`. The earlier `/tmp/campaign-openings-audit` maps, probe, and machine-readable audit files were cleared during USB recovery; they are not available. The per-mission conclusions and tables above are the retained summary.
- Runtime shortcut: `-file <path.map>` launches a map and suppresses the shell intro (`GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp` and `GameEngine.cpp`). This bypasses campaign selection context, so it is not evidence for campaign-menu briefing/video behavior. Runtime action traces used isolated user data and the installed file-link root; audio probes used the null backend, so they cannot establish audibility.

## Validation status

The earlier campaign-opening timing/audio validation used Release SHA-256 `1dfbff0be69a830e65dc7ec90a88dc255a42fcf918f1a6ac5547a65156c2dbbd`; it passed after the three shared fixes. The later terrain crash-fix Release build used SHA-256 `099cc9177fc629113021289939008a1d50d8c7ca7f549de6f5bb89a34c984284` and passed full Release build validation. Focused checks passed: script engine 5 tests / 292 checks; loading 4 / 37; real-asset audio manager 13 / 111; asset-free readiness CTest passed. The behavioral mutants failed as intended: old frozen timer, 2 tests / 240 checks / 52 failures; no invalidation, 1 / 39 / 4; old resident-readiness path, 1 / 6 / 1; missing loading cue, 4 / 37 / 17.

The earlier campaign-opening CTest run reported 164 tests: 139 passed, 23 skipped, and 2 network checks failed inside the sandbox. Both network checks passed on that build’s outside-sandbox rerun (2 / 2). `ffref_capture_selfcheck` passed. This is not described as an all-green raw CTest run. Replay live run, playback, and repeat matched at frame 12,000 with CRC `0x6C6CF1BA` and 22 structures; this same-machine evidence belongs to the earlier campaign-opening build and was not rerun after the render-only terrain fix. Its read-only install integrity guard passed: 370 entries retained the same sizes, timestamps, and contents. The earlier Release executable was staged in the workspace at that checkpoint; the newer crash-fix executable is now staged at `build-linux/ZeroHourReforged/bin/generals` with SHA-256 `099cc9177fc629113021289939008a1d50d8c7ca7f549de6f5bb89a34c984284`. Neither is a home or Steam installation. No all-36 audible playthrough is claimed.
