Measured on NVIDIA GeForce RTX 4060 8 GB, driver 595.112.07, Intel Core i7-12700, Linux 7.3.4-x64v3-xanmod1/glibc 2.43. Resolution 1920×1080, MSAA 0, native Vulkan offscreen, no pacing, validation/debugger/creation tracing disabled. Read-only pre/post thermal/power-state/clock snapshots are retained for every run; no clock or power settings were changed. Reported power draw was unavailable on this device. These snapshots are not continuous thermal monitoring.

| Comparison | Variant | Mean ms | Median ms | p95 ms | Samples |
|---|---|---:|---:|---:|---:|
| Option bundle | off | 2.469 | 1.854 | 3.836 | 1800 |
| Option bundle | on | 3.217 | 2.720 | 5.285 | 1800 |
| Matched-total native overflow: 5,000 particles | cpu | 1.586 | 1.537 | 1.795 | 1800 |
| Matched-total native overflow: 5,000 particles | gpu | 1.292 | 1.239 | 1.653 | 1800 |

The option bundle increases the pooled median interval from 1.854 to 2.720 ms. This is the cost of the selected controllable options in this authored scene, with identical non-Classic assets and model/ordinary-particle policy; it is not the cost of the entire Linux port. The fixed 5,000 comparison lowers the pooled mean interval by 18.5% and median by 19.4%, from 5,000 CPU particles to 2 CPU + 4,998 native particles. It includes ordinary manager/bookkeeping costs and requires a different CPU ceiling to exercise the unchanged overflow policy. It does not establish a break-even load, hardware GPU execution time, or a 100k/million-particle speedup.

All repeats are retained. OFF repeat 1 contains a 297.976 ms outlier; its mean is 3.126 ms versus about 2.14 ms in the later OFF repeats. No sample was trimmed and no scheduling/driver cause is asserted. The per-repeat medians and p95 are below; raw min/max and repeat-mean spread are also in `results.csv`.

| Suite | Order/variant | Repeat | Mean ms | Median ms | p95 ms |
|---|---|---:|---:|---:|---:|
| bundle | 1: off | 1 | 3.126 | 1.847 | 3.815 |
| bundle | 2: on | 1 | 3.377 | 2.748 | 5.315 |
| bundle | 3: on | 2 | 3.108 | 2.673 | 5.290 |
| bundle | 4: off | 2 | 2.141 | 1.865 | 3.808 |
| bundle | 5: off | 3 | 2.142 | 1.851 | 3.860 |
| bundle | 6: on | 3 | 3.167 | 2.724 | 5.222 |
| particles-fixed | 7: cpu | 1 | 1.575 | 1.531 | 1.754 |
| particles-fixed | 8: gpu | 1 | 1.302 | 1.250 | 1.655 |
| particles-fixed | 9: gpu | 2 | 1.279 | 1.224 | 1.713 |
| particles-fixed | 10: cpu | 2 | 1.574 | 1.530 | 1.750 |
| particles-fixed | 11: cpu | 3 | 1.608 | 1.555 | 1.828 |
| particles-fixed | 12: gpu | 3 | 1.297 | 1.247 | 1.597 |

Each variant has three fresh-process repeats with 600 consecutive samples after warmup (1,800 pooled samples). Bundle actual-frame window 2700–3299, stop 3400, all CRCs B35E379B. Particle window 900–1499, stop 1600, all CRCs 2AECE11D. Every timed particle repeat rechecked six common count frames for exactly 5,000 total, either 2 CPU + 4,998 native or 5,000 CPU, with zero live evictions; qualification separately proves 20/20 emitters, no sort/per-system drops, actual compute/draw work and visible output. The visible domes overlap 250 particles per emitter, so the image is not a claim of 5,000 individually distinguishable sprites.

The original same-CPU-cap 16,384 qualification is preserved as rejected because it admitted zero native particles. Its images are pixel-identical; an initial mistaken scaled-preview invisibility impression was withdrawn after exact zero-pixel difference verification. Qualification and its post-window screenshots are excluded from published timing distributions.

Accepted local native Linux renderer benchmarks. Candidate SHA-256: `c7503c364f05364026d1b485ae21d9ff6aaa68bd71dd7fa95d799486cf381cfd`.

`results.csv` contains all primary repeats pooled by suite/variant, with mean/median/p95 and repeat-mean spread. The original per-run samples and summaries remain in `runs/`. These are native end-to-end offscreen present intervals at 1920×1080/MSAA 0, not hardware GPU timestamps or displayed FPS. SYNC rows, if present, are separately labelled serialized diagnostics. Every primary variant has exactly three repeats; no best-run selection was used.

The non-Classic option-bundle pair holds assets, water, shadows, model-state policy and GPU particles fixed. Only the documented controllable enhancement options/post chain differ. The fixed 5,000 particle pair compares CPU cap 16,384/ring 0 against the explicitly documented lower CPU ceiling/native ring policy, because the unchanged manager sends only CPU overflow to the GPU. It passed exact stable retained-population/admission/drop/CRC checks and explicit post-window visibility inspection before timing. Qualification images and exact qualification JSON are included. Small-load GPU regressions, if measured, are part of the result; no unmeasured break-even or 100k speed claim follows.

Exact runner/parser/helper snapshots and original authored camera/scenario inputs are in `reproduction/`. Each run includes its exact private profile, scenario/camera files, environment and complete portable launch command. For particles, the fixture metadata records the original installed template hash and exact field overrides; the runner reads the reader's own retail archive without changing it. Provide your matching original game root and final packaged overlay; retail art is not redistributed here. Python 3/Pillow and the normal game SDL/Vulkan runtime are required, as on the test host.

Set `GENERALS`, `GAME_ROOT`, `BASE_OVERLAY`, `RUN_OUT` and `QUALIFICATION` explicitly before using a run's `reproduce-command.sh` from this directory. To regenerate qualification first, use the exact runner with `--suite particles-fixed --repeat 0 --run --approved-final`: run `--variant cpu` at its default CPU cap of 16,384, then `--variant gpu --native-cpu-cap 1`. Supply the same candidate/root/base-overlay arguments shown in the per-run commands, and choose fresh output folders. Run the exact parser on those folders with `--particle-qualification`, inspect both post-window images, then rerun with `--visual-confirmed` only if visible. The qualification must use this same candidate/fixture/profile. Timed particle runs require its accepted `comparison.json`. Do not supply visual confirmation for an invisible workload.

No installation, clock/power tuning, publication, commits or original game-data writes were performed. Raw rejected/qualification runs remain in the local build evidence tree; they are excluded from the published primary distributions. See the upstream local tracker and final report for feature scope and remaining platform limitations.
