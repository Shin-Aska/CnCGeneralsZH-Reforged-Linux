# Debug Journal — Task6 native surface initialization timeout
Started: 2026-10-17T04:33:00+08:00
Goal: identify why test_dx11water_surface produced no observable before its 120-second WARP cap.

## Environment snapshot
- Runtime: native MSVC x64 PE, Release, D3D11 WARP selected by ZH_DX11_DRIVER=warp
- Entry: build64/Release/test_dx11water_surface.exe
- Repro: PID34268, 120s timeout, exit -1, reaped, empty stdout/stderr
- Git HEAD: 98729c02a37b8c4c784df0856fbf290c4daa9536; shared dirty worktree intentionally preserved
- References read: debugging/SKILL.md; runtimes/native-binary.md; methodology/00-setup.md; methodology/02-investigate.md

## Hypotheses
1. [OPEN] Eight generated water programs make D3DCompile exceed 120s — distinguish with flushed per-variant begin/end timestamps and CPU-only compilation timings — fix: reduce duplicates/cache.
2. [OPEN] Persistent compiler cache or initialization reentrancy stalls inside Compile_Program — distinguish with begin/end around compile and cache path/file timestamps — fix: initialization order.
3. [OPEN] Device creation or a later test hangs while buffered stdout hides progress — distinguish with flushed test/device/backend milestones — fix: actual blocking stage.

## Failed hypothesis round counter
- Round 1: pending

## Artifacts to revert
- [ ] dx11water.cpp temporary WATER_INIT_TRACE stderr milestones. Revert by removing trace calls after diagnosis.
- [ ] test_dx11water_surface.cpp temporary startup/device/backend stderr milestones. Revert after diagnosis.
- [ ] .debug-journal.md. Remove after final debug cleanup.

## Findings
- 2026-10-17T04:30:38+08:00: PID34268 timedOut=true exitCode=-1 reaped=true; stdout/stderr empty.
- 2026-10-17T04:40: CPU compile probe emitted `layout=242 river=0 fog=0 ok=1 ms=46937` and `fog=1 ok=1 ms=47532`, then timed out during the third variant. Confirms H1; refutes H2/H3 as cause of the original cap.
- 2026-10-17T04:45: WARP PID44196 trace: Probe 0ms, VS compile 78ms, PS compile 46735ms, create shaders/layout 78ms, capability Available; Prepare returned InvalidPacket. Lazy bootstrap fixed the timeout but exposed a separate fixture/packet path failure.
- 2026-10-17T04:48: CPU exact fixture input `WaterRenderPacket::Pack` returned ok=1 error=0, so the original input is structurally valid; staged/final snapshot packing remains to discriminate.
- Measurement correction: parent `started` timestamp is captured only after Start-Process returns and CIM lookup, while child GetTickCount trace begins earlier; comparing those durations directly omitted process startup time.

## Failed hypothesis round counter
- Round 1: H1 confirmed; no Oracle trigger.
