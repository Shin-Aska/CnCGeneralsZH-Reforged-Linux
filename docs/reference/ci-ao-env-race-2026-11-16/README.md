# Native post/AO recovery test environment race

The [2026-11-15 CI job](https://github.com/Shin-Aska/CnCGeneralsZH-Reforged-Linux/actions/runs/43730637654/job/132862063424)
failed `build_fingerprint_manifest` and crashed in `test_sdl_ao_gpu`. The manifest lacked
four fire/tree files; regenerating it fixes that check. The GPU crash is a separate test
infrastructure problem that also appeared in earlier AO/post jobs.

## Reproduction and cause

Matching Mesa alone did not reproduce the failure on the newer host C library. The
reproduction therefore uses the CI SDK and Ubuntu 22.04 userspace in isolated workspace
directories, with no original game data:

| Component | Identity |
| --- | --- |
| Steam Runtime Sniper SDK | `registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e` |
| Compiler | GCC 14.2.0; Release; static C++ and GCC runtimes |
| CMake | CI's pinned 3.31.12 distribution |
| Ubuntu base | 22.04.5 amd64 |
| glibc | `2.35-0ubuntu3.18` |
| Mesa Vulkan drivers | `23.2.1-1ubuntu3.1~22.04.4` |
| LLVM | `1:15.0.7-0ubuntu0.22.04.3` |
| Vulkan loader | `1.3.204.1-2` |

The unchanged AO test segfaulted on run 8 of the initial repetition, immediately after
`injected AO program failure`, matching CI's final diagnostic. Another ordinary run
segfaulted on run 40. Live debugger launches did not reproduce it, and the sandbox did
not retain a core even with an unlimited core-size limit. A diagnostic preload then
froze an actual SIGSEGV for attachment; it did not inject the fault. CLion attached to
that process and captured the original fault state (source locations refer to the
pre-fix `2923cfad` executable):

- A driver worker was in glibc `getenv`, looking up `DRAW_USE_LLVM`.
- The instruction at `getenv+173` was `cmp bp, WORD PTR [r12]`; `r12` was `0x300`, an
  unreadable environment-entry pointer.
- The main thread was in `SdlGpuFrame::Read_Back`, called from the recovery fixture
  just after its `setenv("ZH_TEST_SDL_AO_FAILURE", ...)` call.

The tests changed the process environment after GPU worker threads started. Older
glibc can invalidate an environment array while an unrelated driver `getenv` reader
still uses it. The [older implementation](https://github.com/bminor/glibc/blob/glibc-2.35/stdlib/getenv.c)
and [newer concurrent-reader implementation](https://github.com/bminor/glibc/blob/glibc-2.41/stdlib/getenv.c)
help explain why the newer host runtime masked the problem. The same unsafe pattern
was present in the post-processing recovery test.

## Fix

`SdlPostProcess` and `SdlPostOcclusion` now hold typed failure controls per instance.
The tests enable and clear those controls directly, without `setenv`/`unsetenv`.
Defaults disable injection. All existing failure and recovery cases remain, including
partial target allocation. Additional checks exercise another module while the first
module's fault is active, ensuring failures do not leak between instances.

## Local evidence and reproduction notes

Qualification passed:

- 100 consecutive `test_sdl_ao_gpu` runs and 100 consecutive `test_sdl_post_gpu` runs
  through CTest in the isolated Ubuntu 22.04/Mesa 23.2.1 environment.
- The four AO/post CPU and GPU checks under CI's Lavapipe/offscreen/Xvfb launch wrapper.
- The native Linux Release build and nine focused CTests on NVIDIA/Vulkan, with Vulkan
  validation enabled: fingerprint generation/manifest/selfcheck, AO CPU/GPU, post CPU/GPU,
  and resource-owner release CPU/GPU.

Local artifacts are under `build-linux/ci-failure-evidence/`:

- `fetch-sdk.py`, `sniper-sdk-manifest.json`, `fetch-runtime.py`, and
  `runtime-packages.json` record the SDK and checksum-verified Ubuntu packages.
- `build-ci.py` builds the two tests with the pinned SDK/compiler. Its local interpreter
  and library-search paths select the isolated Ubuntu runtime; they are diagnostic ELF
  metadata, not changes to the test or renderer implementation.
- `run-ci.py`, `ci-baseline/007.log`, and `ci-core-baseline/039.log` retain the original
  crash reproductions. `ao-ci-before` and `post-ci-before` preserve the pre-fix programs.
- `pause-on-fault.c`, `freeze-repro.py`, and `root-cause.json` retain the attach procedure,
  faulting registers/instruction, and main-thread stack.
- `debug-runtime-adjustment.json` records a temporary LLVM RUNPATH adjustment needed
  for the IDE's startup shell. The original Ubuntu LLVM bytes were restored before
  qualification; `libLLVM-original-runpath.txt` records their hash.
- `ci-stress.log` / `ci-stress.xml` and `native-fix-tests.log` record qualification.

An initial Xvfb replay passed its tests but could not start its display sockets in the
restricted sandbox, so the wrapper's cleanup returned failure. The accepted replay
uses a private network namespace and exits zero; its server log is empty. See
`ci-xvfb.log`, `ci-xvfb.xml`, and `xvfb-server-final.log`.

The temporary IDE run configuration and agent breakpoints were removed after debugging.
