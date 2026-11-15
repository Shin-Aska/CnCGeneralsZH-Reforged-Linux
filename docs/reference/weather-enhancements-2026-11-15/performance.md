# Measured weather cost

The added presentation increases active-storm cost on this scene. Across three repeats, the median present interval rises about 0.29–0.31 ms (about 3%); the mean rises 0.41–0.42 ms and p95 rises about 0.92–0.98 ms. Rain fades cost more because the early weather and complementary late warm-haze passes both run. Dry rain is close to the final Weather off control. These are accepted feature costs, not an optimization or a duty-cycle speedup.

All values below are native end-to-end **offscreen present intervals in milliseconds**, including fixed AI thinking, audio mixing and HUD work. They are neither hardware GPU timestamps nor FPS. Each cell reports the median across the three run statistics, with the full minimum–maximum repeat range in parentheses; the Mean column averages the three run means. Samples are never pooled to disguise repeat variability.

| Kind / phase | Artifact label | Samples per repeat | Mean ms | Median ms (repeat range) | p95 ms (repeat range) |
|---|---|---:|---:|---:|---:|
| rain / active-30s | baseline-on | 1,800 | 9.683 | 9.572 (9.560–9.586) | 10.281 (10.271–10.327) |
| rain / active-30s | enhanced-on | 1,800 | 10.088 | 9.859 (9.849–9.883) | 11.241 (11.154–11.258) |
| rain / active-30s | enhanced-off | 1,800 | 9.665 | 9.565 (9.548–9.573) | 10.225 (10.213–10.280) |
| sand / active-30s | baseline-on | 1,800 | 9.625 | 9.518 (9.515–9.523) | 10.208 (10.185–10.231) |
| sand / active-30s | enhanced-on | 1,800 | 10.044 | 9.828 (9.802–9.851) | 11.169 (11.121–11.226) |
| sand / active-30s | enhanced-off | 1,800 | 9.641 | 9.531 (9.523–9.547) | 10.216 (10.178–10.233) |
| snow / active-30s | baseline-on | 1,800 | 9.653 | 9.534 (9.516–9.553) | 10.247 (10.226–10.304) |
| snow / active-30s | enhanced-on | 1,800 | 10.076 | 9.838 (9.831–9.844) | 11.225 (11.161–11.259) |
| snow / active-30s | enhanced-off | 1,800 | 9.661 | 9.560 (9.509–9.565) | 10.251 (10.230–10.280) |
| rain / fade-in | baseline-on | 437 | 9.665 | 9.572 (9.550–9.577) | 10.196 (10.195–10.222) |
| rain / fade-in | enhanced-on | 437 | 10.212 | 10.036 (10.006–10.045) | 11.136 (11.072–11.215) |
| rain / fade-in | enhanced-off | 437 | 9.674 | 9.575 (9.531–9.582) | 10.258 (10.211–10.354) |
| rain / fade-out | baseline-on | 813 | 9.681 | 9.580 (9.560–9.600) | 10.271 (10.215–10.317) |
| rain / fade-out | enhanced-on | 813 | 10.302 | 10.117 (10.097–10.148) | 11.300 (11.287–11.317) |
| rain / fade-out | enhanced-off | 813 | 9.667 | 9.568 (9.563–9.584) | 10.165 (10.145–10.294) |
| rain / dry | baseline-on | 18,674 | 9.706 | 9.606 (9.590–9.608) | 10.291 (10.274–10.292) |
| rain / dry | enhanced-on | 18,674 | 9.697 | 9.595 (9.593–9.597) | 10.250 (10.250–10.269) |
| rain / dry | enhanced-off | 18,674 | 9.692 | 9.587 (9.585–9.604) | 10.240 (10.237–10.273) |
| rain / complete-cycle | baseline-on | 12,600 | 9.734 | 9.630 (9.620–9.630) | 10.309 (10.309–10.317) |
| rain / complete-cycle | enhanced-on | 12,600 | 9.850 | 9.721 (9.719–9.739) | 10.857 (10.847–10.881) |
| rain / complete-cycle | enhanced-off | 12,600 | 9.722 | 9.621 (9.609–9.630) | 10.271 (10.265–10.311) |

The fade and dry rows above cover all admitted samples at those phases within frames 601–25200, at identical absolute times for baseline/final/off. The complete-cycle row isolates the entire second slot, [210, 420) simulated seconds, independently of the partial first slot. Per repeat it contains 12,600 samples: 2,367 plateau (18.79%), 437 fade-in (3.47%), 437 fade-out (3.47%) and 9,359 dry (74.28%). Mean storm envelope is 0.2225285723; nonzero-rain occupancy is 25.72%. That dry duty cycle explains why the complete-slot mean increase is smaller than active/fade cost. It does not reduce the active cost.

The full [72-distribution table](evidence/summary/all-repeats.md) retains each repeat, mean, median, p95, maximum and sample count. [Machine-readable phase results](evidence/summary/phase-results.json), [CSV](evidence/summary/phase-results.csv), [all 253,800 raw rows](evidence/primary-runs) and [exact production phase output](evidence/summary/rain-phase-series.csv) are archived. Every outlier remains in its original CSV and all applicable distributions. No samples were trimmed, no arbitrary performance threshold was applied, and all 27 primary runs were admitted; rejected harness attempts are outside this timing matrix.

## Regression investigation

The repeatable increase is consistent with added rendering and composition work, rather than an isolated bad repeat. Active Sand and Snow have five median native flushes versus four at baseline/off. Their median device-flush CPU intervals increase roughly 0.26 and 0.31 ms, respectively, while median device draw-call intervals and offscreen present intervals remain similar. Rain adds contact/layer work, and fades admit the extra complementary warm-haze pass. The [component investigation](evidence/summary/component-investigation.json) records these counters and the phase aggregates. These CPU-side intervals overlap submission/synchronization behavior; they do not isolate hardware GPU execution time or establish a causal per-feature budget. An A/B disabling individual new features was not performed because it would require different production candidates.

## Controlled setup

Intel Core i7-12700, NVIDIA GeForce RTX 4060 (8,188 MiB reported), NVIDIA driver 595.112.07, Vulkan, Linux 7.3.4-x64v3-xanmod1, glibc 2.43. Each manifest records actual pre/post GPU temperature, clocks, P-state and memory use. No clock/power tuning was performed. No other team GPU/audio workloads or heavy CPU/I/O tasks ran during collection.

1920×1080, MSAA 0, native offscreen Vulkan, Modern water on, Atmosphere on, MapLight/Warm Light on, internal HDR and optional post chain off, real null-backend audio mixer on. Full profiles, runtime arguments, environment and generated map/scenario hashes are recorded in every run. The same observer fixture uses seed 7, one starting cash and ten-minute peace; retained AI thinking is part of the result. Particle cap is 3,000; GPU particles are off. Camera is fixed by the recorded fixture and `-camera 640 600` command.

The three repeats vary baseline-on/final-on/final-off order (B/E/O, O/E/B, E/B/O). This is not complete position counterbalancing; three repeats provide limited coverage of order, thermal and scheduling effects. The many correlated frames are not independent trials. Rain runs warm up 600 logic frames then retain every frame 601–25200 (24,600 samples, 410 simulated seconds). Sand/Snow retain frames 601–2400 (1,800 samples, 30 simulated seconds). Warmup and fixed complete windows are enforced. At this host speed, simulated time is not wall time; manifests retain actual wall duration. The active 30-second window contains multiple gust periods and is wholly inside the first plateau. No debug probes, screenshots, audio recording, synchronization diagnostics or image conversions ran inside primary timing.

Existing capture-clock admission is explicitly armed with an inert screenshot after quit. Current-draw weather time is `floor((logic_frame−1)*1000/60)/1000`, qualified at multiple actual render points. The exact production helper supplies phase boundaries for the pond map seed 2096055520; its source/binary and actual seed probe are pinned in [admission](evidence/benchmark-admission.json). Baseline continuous rain and final off use that same schedule only as labels for paired absolute scene times.

The primary collector is frozen at SHA b92c34f710f3d8208b0b33b424613470db6298caf0df58981d1b5f809a8d6309. Each run requires a new log prefix, exact executable and profile/fixture identities, consecutive complete CSV, timer completion, matching simulation CRC/frame and unchanged overlay before/after. Rain CRC is 29B3E8BA at frame 25320; Sand/Snow F7D76419 at frame 2520. All 27 passed. The retail inventory was fully content-hashed before and after all runs; per-run checks pinned that inventory and file sizes. See [final verification](evidence/final-verification.json).

The single HDR smoke is separate qualification: it uses 1280×720, InternalHDR on and Bloom+FXAA, so it is not included in these statistics. Synchronization diagnostics and earlier provisional images likewise contribute no timing rows.
