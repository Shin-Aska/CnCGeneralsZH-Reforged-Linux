# windows-modern-water-d3d11 - Work Plan

## TL;DR (For humans)
<!-- Fill this LAST, after the detailed plan below is written, so it summarizes the REAL plan. -->
<!-- Plain English for a non-engineer: NO file paths, NO todo numbers, NO wave/agent/tool names. -->

**What you'll get:** The complete Linux modern-water experience in the native Windows renderer, enabled during ordinary launches. Classic remains selectable, with correct settings persistence and a working Direct3D 9 path.

**Why this approach:** Share the existing water models and shaders, while giving Windows explicit ownership of its GPU resources. Prepare complete water patches before drawing so failures cannot hide legacy wakes or leave partial transparent surfaces.

**What it will NOT do:** Change gameplay, save formats or production defaults for experiments; discard existing work; or call a build or settings value proof that the water works.

**Effort:** XL
**Risk:** High — shared shader extraction, native GPU state and water ownership cross several existing rendering paths.
**Decisions to sanity-check:** No blocking product decision remains. A fixed frame-rate target or minimum supported GPU would refine the proposed comparative performance budgets.

Your next move: Complete the requested high-accuracy review, then explicitly start a separate implementation session when ready. This delivery contains planning only.

---

> TL;DR (machine): XL; high risk; full native D3D11 modern-water parity, production defaults, capability-aware settings, preserved Classic/D3D9 and POSIX behavior, with runtime evidence gates.

## Scope
### Must have
- Implement the full current Linux feature set: surface lighting, coloured planar reflections, depth/bed optics and refraction, FFT displacement, shoreline effects, persistent foam, vessel wakes, bow/shoulder waves, wave cages and mesh hull contact. Port the existing optional coastal experiments with their existing opt-in policy.
- Ordinary supported Windows launches render Modern without a water flag or environment variable. WaterStyle remains 0=Classic, 1=Modern; fresh/missing-key defaults are Modern; explicit Classic persists. Accept commits, Back/Esc cancels, and live switches work while paused.
- Keep native D3D11, real D3D9, SDL/Vulkan and SDL/Metal distinct backends behind a small shared water contract. Preserve gameplay, grid evolution, save format, replay determinism and existing user edits.
- Deliver source, focused tests, an isolated cross-platform capture/comparison runner, packaged launch evidence, performance results and an evidence-backed final checklist. Intermediate milestones are not the endpoint.

Planning baseline: 2026-10-16, HEAD 98729c02a37b8c4c784df0856fbf290c4daa9536 plus the seven user-owned dirty paths recorded with SHA-256 in .omo/drafts/windows-modern-water-d3d11.md. This document is a plan, not implementation or runtime evidence. Planning ran source inspection and CTest enumeration only.

### Verified source map

All paths below are relative to <REPO>. These aliases are exact prefixes used in task references; line numbers refer to the planning baseline and must be relocated by symbol after edits.

| Alias | Exact path |
| --- | --- |
| C | GeneralsMD/Code |
| P | GeneralsMD/Code/Libraries/Include/Platform |
| W | GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water |
| WI | GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient |
| D | GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2 |
| S | GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render |
| G | GeneralsMD/Code/GameEngine/Source/Common |
| M | GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp |
| T | GeneralsMD/Code/Tests |

| Area | Verified anchors and implication |
| --- | --- |
| Reusable water | P/WaterSurfaceModel.h:37,154,198; WaterProjectedMesh.h:77,146; WaterFftGeometry.h:8; WaterFftSpectrum.h:12,94,106; WaterFftSampling.h:25,151; WaterOpticalContext.h:13,49; WaterFoamHistory.h:111; WaterShoreHistory.h:187. Reuse equations, meshes, bounds, deterministic spectrum, histories and polygon ownership. |
| SDL-specific transport | S/SdlConstants.h:79-115 and PosixDevice9Draw.cpp:510-563,1096-1188,1494-1514 pack/bind the real shader ABI. S/SdlGpuFrame.cpp:453-504,788-829,1075-1134 owns FFT, coastal passes and immutable snapshots. P/WaterCoastalRender.h:4 and WaterShoreOverlay.h:124 expose POSIX-only bridges. |
| Shader/native blockers | D/engineshader.cpp:1656,1701 routes modern shaders only to SDL; native falls through to legacy. D/dx11backend.h:62 and dx11backend.cpp:43,1351 limit ordinary stages to four. D/dx8wrapper.h:102,1118 caches eight PS registers and does not mirror them. High stages bypass native mirroring at :1402. Do not expand this ordinary contract. |
| Native reuse | D/dx11twin.h:42-111 and dx8wrapper.cpp:2909,5593,5614 already mirror geometry. D/dx11device.cpp:265-293 has sampleable main depth; D/dx11backend.cpp:1763-1796 has secondary DSV-only depth keyed by size. D/dx11runtime.cpp:400,527 redirects the main scene target. |
| Defaults/settings | G/GlobalData.cpp:665,698 defaults to D3D11 and Modern. G/OptionsCatalog.cpp:401,626-637 preserves missing-key defaults. M:1066-1074 hides Water style on all Windows; :1111-1151 reads widgets without capability filtering; :2956-2990 handles Back/Accept. G/UserPreferences.cpp:132-179 reads/writes actual disk settings. |
| Runtime/lifetimes | W/W3DWater.cpp:1146,2027,2414,2499,2530,2560,2723 separates style, resource refresh, map reset and clocks. C/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp:4768 reports the actual backend, including fallback; :906-954 rolls back failed display changes. |
| Ownership | W/W3DWater.cpp:456,4839 marks vessel admission; D/dx8wrapper.cpp:2908 discards native draw success. C/GameEngineDevice/Source/W3DDevice/GameClient/W3DParticleSys.cpp:112 and P/WaterFoamParticlePolicy.h:18 suppress only exact eligible wake draws. P/WaterShoreOverlay.h:17-51 requires exact view/target/level/coverage ownership. |
| Coverage gaps | C/CMakeLists.txt:2305-2319 gives Windows test_water_grid CPU stubs; :1587-1591 registers test_water_tracks only on POSIX; :1411 registers SDL GPU water readbacks. T/test_gameengine.cpp:14204 tests in-memory preferences, not menu/disk/relaunch. windows-ci.ps1:112 omits most native GPU tests from its session-zero exclusion list. |

### Windows exclusion inventory

Audit every listed water guard; replace platform checks with effective water capability where appropriate, retaining actual platform APIs behind bridges. Do not globally delete all _WIN32 guards in this file.

| W/W3DWater.cpp guard location | Required result |
| --- | --- |
| 458,468,500-927 | Enable vessel admission, mirror ownership, pose/contour/contact/mesh packets on capable native D3D11. |
| 952,1015,1113 | Share shore/foam CPU histories and route coastal GPU bindings through the backend bridge. |
| 1148,1175,1281,1365,1393-1455,1515 | Share style synchronization, typed shader inputs, bed/height/optical textures, polygon refresh and constructor state. |
| 2560 | Keep shore histories advancing independently of visibility through the existing client-update clock. |
| 2848,2925,2962,3218,3242,3260 | Preserve Modern mirror clear/alpha, render scope state, sun exclusions and portable profiling. |
| 3571,3930 | Skip legacy feather copies only for Modern flat patches; preserve grid UV/height metadata. |
| 4362-4856,4874,5119,5279 | Enable projected/fallback flat geometry and modern rivers; suppress legacy multiplicative reflection only when modern composition succeeds. |

### Architecture decisions

1. **Shared inputs, separate backend resources.** Add P/WaterRenderPacket.h, P/WaterShaderLayout.h and P/WaterRenderBridge.h. SurfaceInput contains time, level, material/terrain/optical inputs, hull contours/contact packets, histories and geometry metadata. SceneInput contains inverse camera/viewport, lights, actual target descriptor and snapshot validity. Their packer produces immutable DrawPacket constants and water-specific material handles. Backend implementations live in D/dx11runtime.cpp and the existing POSIX water bridge locations; D/dx11runtime_posix.cpp supplies inactive native stubs. No D3D11 type escapes into W3DWater. SDL may adapt the typed packet to its existing emulated bank initially; real D3D9 never receives the 896-register scratch transport or high water stages.
2. **Native ownership and geometry lifetime.** Add D/dx11water.h/.cpp, owned by DX11BackendClass. It owns COM resources for FFT, snapshots, coastal history/view, internal shaders, samplers, constant buffers and retained prepared-patch geometry. Engine TextureClass inputs remain engine-owned with explicit held references where necessary; CPU texture edits follow D/dx11texture.cpp:630 dirty tracking and preserve bed mips. A PreparedPatch contains topology, FVF/layout, vertex stride, retained vertex/index allocations, index format, vertex/base/index offsets and counts, original coverage polygon, water level, patch/part identity and all batch descriptors. Preserve the existing geometry calculations; reuse twin-buffer upload machinery where safe, but do not retain references to ranges that the dynamic ring can overwrite. Assemble all batches first, reserve/upload disjoint native water buffer ranges, and hold those ranges/references through submission. The bridge exposes PreparePatch, SubmitPreparedPatch and End, plus resize/device/map invalidation.
3. **Atomic patch submission and truthful receipts.** Prepare the whole patch: preflight shaders, textures, constants and every retained geometry batch; validate/capture its complete pre-water scene pair; then issue all draws without further allocation, compilation or validation that could ordinarily refuse. Any pre-emission refusal renders the whole original patch through Classic. There is no same-frame partial Classic fallback after emission because projected batches have no equivalent Classic-span mapping. D3D11 DrawIndexed is void: the receipt means commands issued after successful preflight, not GPU completion. Device loss after emission invalidates this frame's admissions and retries only on a recovered valid device/frame. Keep ordinary Draw_Triangles behavior unchanged.
4. **Shader reuse.** Factor canonical modern river/flat/shore HLSL and raster FFT/coastal utility generators into P/WaterGpuShaders.h and the existing model headers; retain one set of equations. Select native versus SDL declarations explicitly, including SDL register spaces. Native water uses vs_5_0/ps_5_0 on the existing feature-level-11 device; ordinary generated shaders remain vs_4_0/ps_4_0. Reuse WaterFftSampling::WrapVertex and generated unlit FVF layouts. No compute rewrite or unrelated renderer refactor.
5. **Variant keys.** Snapshot launch-time feature switches once. Include target API, water/river/shore kind, analytic/FFT VS, hull layout, optional coastal/crest, debug mode and material/FVF requirements in in-memory keys. Preserve persistent compilation hashing of full HLSL and profile at D/dx11backend.cpp:1684; route internal FFT/coastal utility shaders through this same cache, not an uncached private compiler. Native shipped-cache generation must enumerate the new production variants; cold compilation must work without a shipped cache. Style changes choose variants; they do not reread environment variables.
6. **Preference versus capability.** Add a typed capability query to C/GameEngine/Include/GameClient/Display.h and W3DDisplay, deriving from the actual active backend and verified required formats/shaders. Track requested style, effective style and available feature bits/reason separately. Supported D3D11 shows the existing selector. D3D9/unavailable backend shows a disabled Classic selector with a localized availability explanation; widget readback must explicitly skip it so unrelated Accept never overwrites requested Modern. Preserve the saved preference for later recovery. Use the explicit failure policy below; no missing production feature can pass parity.

**Failure policy:** global absence of the native backend, core water shader/layout or required format support makes effective style Classic and the selector unavailable. A target-specific invalid scene colour/depth pair refuses that patch before emission and uses Classic; it does not globally toggle the preference or flicker the selector. Keep Linux's existing analytic fallback intent: FFT failure on an otherwise valid flat patch may render analytic Modern, and missing local foam/hull/optical resources may render the remaining Modern material, but mark these draws Modern-degraded in diagnostics with exact missing bits. They publish no vessel/shore replacement ownership. Experiments missing their resources disable only that experiment. Non-applicability is not failure: rivers are analytically modern by design, no visible hull means an empty valid packet, and mirror recursion intentionally omits certain scene reads. Log once per reason/target epoch; clear global failure latches on device recreation and target failures on resource-generation invalidation. The UI remains the two existing choices; a persistent global inability shows the unavailable explanation, while target-local degradation is diagnostic. Production acceptance allows no degradation in its eligible fixture draws.

**Receipt aggregation:** key accepted native coverage by presented frame, device/resource generation, actual colour/depth identities, capture serial, view/viewport, water level and patch ID. Publish the complete original coverage polygon only after every batch of that patch was issued. Before suppressing an eligible owner, prove its clipped, finite replacement footprint (hull plus the existing wake/contact source support bounds) is fully covered by the union of accepted same-view/same-level polygons; do not infer coverage from equal level or any accepted hull. Use conservative polygon subtraction/containment; uncertainty retains the legacy wake. Owner identity/pose is current-frame only. A shore track requires whole-track coverage by the same valid union and matching original depth. Test disjoint patches where one fails and adjacent patches whose union is sufficient. Tightened native admission does not change POSIX particle/history policy during this port.

**WaterStyle Accept transaction:** collect the requested widget value into pending state and the candidate preference map without invoking the catalog's live WaterStyle setter. Leave the current requested/effective water state and histories unchanged until the candidate Options.ini is committed. Make G/UserPreferences.cpp::write write a unique, exclusively created sibling temporary file, check every formatted write, flush/sync and close, then atomically replace the destination (ReplaceFileW for an existing Windows file, a non-overwriting move for a new one; atomic rename on POSIX). Preserve the existing file's permissions/metadata as supported and the existing key/value serialization. A failure before replacement preserves the old file byte-for-byte, including the no-file case, and removes only the owned temporary file. Atomic visibility is required; do not claim a new guarantee against filesystem/power failure.

Only after write returns committed does Accept update the requested WaterStyle and schedule the existing effective-style synchronization; then it may finish the normal hotkey/menu-close flow. On failure, keep the options layout and pending choice open, show a localized save error, and permit retry or Back/Esc; do not save hotkeys, close the layout or claim persistence success through that Accept path. Unavailable water controls contribute the original requested value. This transaction protects WaterStyle and the preference file; it does not redesign existing non-water audio/display preview or resolution-confirmation semantics. Tests explicitly assert the water state/history/file invariants, rather than claiming all existing previews roll back.

### Exact shader contract

Shared layout must have sizeof/offsetof checks and native shader-reflection tests, not matching-looking declarations. The scratch c-register numbers are input transport, not native cbuffer offsets.

| Stage/binding | Layout |
| --- | --- |
| VS b0 | Existing fixed vertex constants, unchanged. |
| VS b1 / PS b0 prefix | 194 float4 = 3104 bytes; PS adds 8 visual-hull metadata float4 when mesh contact is enabled, making 202 = 3232 bytes. Both stages consume the same immutable common snapshot. |
| PS b1 / b2 / b3 | Hull packet arrays of 256 / 256 / 192 float4 = 4096 / 4096 / 3072 bytes. |
| Shore wrapper b0 | Separate 11-float4 (176-byte) authored-track layout; never treat it as the surface layout. |
| Common float4 offsets | 0-2 combiner; 3-6 inverse clip matrix; 7 camera; 8-9 sun; 10 viewport; 11 reflection; 12-16 water/terrain/scene/optical/hull; 17-96 mean hulls; 97 FFT; 98-99 history; 100-179 contact hulls; 180-183 fill lights; 184-185 coastal; 186-189 shadow matrix; 190-192 shadow parameters; 193 smoke; PS 194-201 mesh metadata. |
| Legacy scratch input | c1-7 parameters, c8-87 hulls, c88-89 history, c90-93 lights, c96-175 contact, c176-183 metadata, c192-895 packets. Extract once into the typed packet, never expand native ordinary PS storage to this size. |

| Resource | VS | PS | Sampler/meaning |
| --- | --- | --- | --- |
| FFT displacement maps 0,2,4 | t0-2 / s0-2 | via array | Repeat, linear; identical cascade/channel order. |
| Height/wave cage | t3 / s3 | t2 / s2 | Clamp, linear, no mip. |
| Authored material, mirror, shroud/river edge | — | t0, t1, t3 | Preserve authored t0/t3 state; mirror clamp. |
| Optical context | — | t4 / s4 | Clamp, linear. |
| Pre-water colour/depth | — | t5 / s5, t6 / s6 | Clamp colour linear; depth POINT, no mip. |
| Bed material | — | t7 / s7 | Wrap, linear/trilinear, complete mip chain. |
| FFT six-layer array | — | t8 / s8 | Repeat, linear with explicit mip behavior; fallback must also be an array. |
| Sun/smoke | — | t9 / s9, t10 / s10 | Reuse existing water shadow sampling. |
| Unused holes | — | t11-13 | Null/neutral, never stale ordinary bindings. |
| Packed foam/direction atlas; optional coastal view | — | t14 / s14, t15 / s15 | Clamp, linear. |

Flat projected geometry transports FFT weight, hull-free damping and offshore data in normal XYZ; UV1 carries damping/base level; diffuse red carries obstacle data; UV0.x carries flat correction (W/W3DWater.cpp:4641-4731). The wrapped VS preserves original XY in TexCoord2, metadata in Specular and shroud in TexCoord3 (P/WaterFftSampling.h:151-198). Rivers retain authored UVs and analytic modern behavior. Grid water retains gameplay-driven deformation; neither receives accidental flat-ocean FFT eligibility.

### GPU order, snapshots and lifetime

For each render view: finish reflection production; prepare FFT once for the current water time; update eligible optional coastal history/view; prepare/preflight the complete patch; validate then capture paired scene colour/depth before the first modern main-surface emission; restore scene targets/viewport; issue every prepared batch; publish the patch receipt; draw later authored shore overlays using the already captured depth; restore ordinary state before terrain/particles/UI. Snapshot failure refuses the modern patch before emission. If any first-patch refusal causes Classic water to draw before a valid pair exists, mark Modern unavailable for that entire view/frame; later patches must not take a contaminated snapshot after Classic water. A later patch refusal reuses the original valid pair without recapture. This view-only latch expires at the next presented frame; resource/device failure latches retain their separate lifetime. Reflection recursion must not advance the presented-frame serial or capture its own water as input.

FFT keeps the SDL raster algorithm: three deterministic 256x256 spectra, 8x256 butterfly lookup, two sets of six ping textures, six mipmapped maps and a six-layer array, all RGBA32F as in S/SdlGpuFrame.cpp:376-406. Evolve into six MRTs, run eight horizontal and eight vertical butterfly stages, normalize/sign-correct, build eight mips with the existing four-texel average through scratch, then copy all six layers/nine mips. Commit ready/time only after success. Pause reuses the same maps; production has no GPU readback. Optional coastal order is incident → reaction → diffusion for Sample, incident → reaction for GapDecay, with existing reset/no-op semantics and exact FFT phase/revision checks.

Snapshots use actual colour/depth resources and subresources, generation, formats, dimensions, sample counts, viewport and view transform; null CurrentTarget means the actual scene/HDR view, not an assumed backbuffer. Use a bounded eight-entry cache with frame/capture serials. Capture once per source/view epoch before water; a late shore lookup cannot create a new snapshot. Clear, target replacement and resize invalidate the affected entry. A colour/depth pair becomes valid atomically.

Make secondary depth records own typeless texture+DSV+SRV, matching main-depth semantics. Preserve existing engine depth-sharing relationships, including equal-size main-target sharing; descriptors expose the actual shared identity, so equal size never substitutes for identity. Current native swapchain/scene/depth SampleDesc.Count is 1 (D/dx11device.cpp:121,248,272). Use compatible single-sample texture copies for exact depth and colour, retaining HDR format. Validate sample counts; an unexpected multisampled source must explicitly refuse the snapshot with a reason, never perform an invalid copy or claim depth parity. Exercise user MSAA settings while recording the actual native sample count; adding a new native MSAA implementation is outside this port.

Before internal render/copy passes, detach conflicting OM outputs and unbind overlapping VS/PS SRVs. Restore actual RTV/DSV, viewport/depth range, scissor, pipeline and backend bookkeeping on every exit; extend Forget_Bindings and cached state for VS resources/b1 and PS high slots/b1-b3. Explicitly clear water bindings on the next ordinary draw. D/dx11backend.cpp:2847 mutable Readable_Texture, cloak colour and particle depth caches cannot substitute for immutable pre-water snapshots. Microsoft confirms automatic alias nulling in [PSSetShaderResources](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources); [CopyResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource) requires compatible resource descriptions.

Resize releases all source/backbuffer references before ResizeBuffers; rebuild view-sized resources lazily, preserve FFT time/maps and camera-independent CPU/coastal histories. A new device clears all native resources, shader failure latches and old-device texture mirrors by device epoch. Map reset clears world histories/ownership; style switch clears derived histories/admissions and refreshes reflections while retaining wave/capture/gameplay time. Shutdown destroys the water owner before context/device. Follow [ResizeBuffers reference rules](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers) and [CheckFormatSupport](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkformatsupport); do not rely on inferred capabilities.

### Production defaults and optional controls

| Policy | Exact controls/behavior |
| --- | --- |
| Production, enabled without overrides | WaterStyle=1; ZH_WATER_FFT, ZH_WATER_VESSEL_WAVES, ZH_WATER_WAVE_CAGES, ZH_WATER_FOAM_HISTORY, ZH_WATER_SHORE_HISTORY, ZH_WATER_SHORE_FOAM default on; retain their existing disable semantics. ZH_WATER_BOW_LIFT defaults to floor, ZH_WATER_BOW_SHOULDER to contour, ZH_WATER_HULL_CONTACT to mesh. |
| Experimental, off unless explicitly enabled | ZH_WATER_SURF_GPU=1, ZH_WATER_COAST_CREST=1. Port and test them separately; do not turn them on to satisfy ordinary-launch evidence. |
| Diagnostics/capture, not prerequisites | ZH_WATER_DEBUG, PROFILE, MESH_STATS and the existing HULL/FOAM/SHORE/VESSEL_STATE traces and contact dumps; ZH_WATER_CAPTURE_CLOCK=picture and ZH_WATER_CAPTURE_FIXTURE=carrier remain gated test controls. Preserve names and behavior, freeze shader-changing controls in variant keys. |

### Principal risks and prescribed mitigation

| Risk | Required mitigation/evidence |
| --- | --- |
| Silent legacy/analytic fallback masquerading as success | Manifest requested/effective style, actual backend/driver, every feature bit and refusal reason; parity requires all production features active and a native draw count above zero. NULL driver/headless never counts as renderer evidence. |
| Binding/packing errors and leaked state | ABI reflection plus numerical readbacks and water→terrain→road→particles→UI debug-layer sequences. |
| Refraction feedback, stale captures, reflection contamination | Immutable paired snapshots with exact ownership; adversarial two-target, two-view, multi-level, clear/resize tests. |
| Foam suppression without visible water | Accepted draw receipts and exact eligible owner/track policy; later-batch preflight/capture refusal plus simulated device loss before/after emission. |
| Simulation drift from render-dependent work | Preserve logic-owned grid step and three separate clocks; seeded CRC and direct grid-state tests, save/load and replay checks. |
| Missing or misleading test fixtures | Validate water ROI, vessel template/route and capture-clock eligibility; fail on zero tests, missing screenshot, skipped required GPU job or stale binary. |
| Dirty files/stale Run assets | Preserve intake patch and hashes; hash built and staged executables/overlay; selective staging only. |

### Must NOT have (guardrails, anti-slop, scope boundaries)
- No implementation, build, game launch or commit during this planning turn. Future execution must not reset, stash, clean or overwrite the user's edits. Recheck the seven intake paths before changes; surgical overlapping edits must retain their portability/test work.
- No global expansion of fixed-function stages/PS registers, real-D3D9 modern transport, duplicate water equations, compute-FFT rewrite, new gameplay physics, changed save schema or broad renderer cleanup.
- No water opt-in required for ordinary Windows play, no forced Modern over explicit Classic, no experimental controls promoted to production defaults, and no capability fallback written into the saved user preference.
- No fake completion from WaterStyle=1, source grep, a successful build, CPU-only tests, headless/NULL rendering, fallback images, UI-only screenshots, or unverified Linux/macOS results.

## Verification strategy
Verification is agent-executed against real binaries and render surfaces. Unavailable GPU/OS hosts are reported as unavailable and leave the corresponding implementation acceptance gate open.
- Test decision: tests alongside implementation using the existing TEST/CHECK C++ harness and CTest; boundary/failure tests precede the corresponding fix. Reuse existing model tests; add only coverage for the new transport, native GPU, settings and lifecycle boundaries.
- Evidence root E = .omo/evidence/windows-modern-water-d3d11/<unique-attempt>. Each task writes E/task-N/{commands.log,results.json,manifest.json} plus named captures/readbacks. Never overwrite a baseline or reuse a previous attempt's output as a new pass. Store source revision and dirty-patch digest, executable and asset hashes, driver/adapter/OS, requested/actual backend and sample count, settings/environment, shader-cache state, scene/camera/time, test count and exit status.
- The final implementation report distinguishes passed, failed, skipped and unavailable. Required GPU tests skipped in session zero are not passes. Existing known failures remain independently documented and must not increase.

### Executable command catalogue

These are future execution commands, not commands run while writing this plan. W0/W1 and normal W3/L0/L1 use existing entry points. W2 is a corrected isolation wrapper around the existing game switches. N0/H, the task-13 VerifyControl modes and the repaired task-10 cache recorder are explicitly future work; do not run proposed modes before their task lands. Every test substring below must select at least one test; T/test_main.cpp:133-136 already rejects zero matches.

**W0, Windows full Release/Debug:** from repository-root PowerShell:

~~~powershell
.\build.bat Release test
.\build.bat Debug test
~~~

**W1, focused existing tests and staged game:**

~~~powershell
$BuildDir = (Resolve-Path -LiteralPath '.\build64').Path
$CMakeEntry = Select-String -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt') -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
if (-not $CMakeEntry) { throw 'Configure with build.bat first; CMAKE_COMMAND is missing' }
$CMake = $CMakeEntry.Matches[0].Groups[1].Value
$CTest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $CMake) -or -not (Test-Path -LiteralPath $CTest)) { throw 'Configured CMake/CTest tools are missing' }
& $CMake --build build64 --config Release --target generals test_gameengine test_water_grid test_water_optical_model test_dx11device test_dx11state test_dx11layout test_dx11pipeline test_dx11resource test_dx11backend test_dx11post test_dx11twin test_dx11texture test_ffshader test_ffshadercompile test_ffvertex test_ffvertexcompile test_engineshader test_pixelcentre
& $CTest --test-dir build64 -C Release --output-on-failure -R '^(test_water_optical_model|test_water_grid|test_dx11(device|state|layout|pipeline|resource|backend|post|twin|texture)|test_ffshader|test_ffshadercompile|test_ffvertex|test_ffvertexcompile|test_engineshader|test_pixelcentre|test_gameengine|optionsmenu_selfcheck)$'
& .\build64\Release\test_gameengine.exe water_style
& .\build64\Release\test_gameengine.exe option_catalog
~~~

The selected build cache is the authority for CMake/CTest. Use the same configuration and architecture for the staged executable and all test binaries; do not use an assumed PATH or a hardcoded VS edition.

**N0, proposed new targets and focused groups:**

~~~powershell
& $CMake --build build64 --config Release --target test_water_render_packet test_water_shore_policy test_dx11water
& $CTest --test-dir build64 -C Release --output-on-failure -R '^(test_water_render_packet|test_water_shore_policy|test_dx11water)$'
& .\build64\Release\test_dx11water.exe compile
& .\build64\Release\test_dx11water.exe scene
& .\build64\Release\test_dx11water.exe surface
& .\build64\Release\test_dx11water.exe fft
& .\build64\Release\test_dx11water.exe hull
& .\build64\Release\test_dx11water.exe shore
& .\build64\Release\test_dx11water.exe coastal
& .\build64\Release\test_dx11water.exe lifecycle
~~~

Offscreen native tests run on real hardware and WARP. Add a controlled test-device debug-layer option; fail that lane if required debug tooling is unavailable instead of silently omitting it. WARP is numerical/functional evidence, not hardware performance evidence.

**W2, ordinary-launch acceptance:** run from repository-root PowerShell. Each call creates a GUID-scoped user/output directory. Reject inherited water/GPU/backend overrides rather than silently accepting a contaminated default run. Change only the child environment, so the caller's variable existence and values remain untouched even on failure. H implements the same policy, checks the expected executable/asset manifest hashes from the task-1/current-build inventory before launch, and records a before/after environment comparison.

~~~powershell
$RunDir = (Resolve-Path -LiteralPath '.\GeneralsMD\Run').Path
$Exe = Join-Path $RunDir 'generals.exe'
if (-not (Test-Path -LiteralPath $Exe)) { throw 'Staged game is missing' }
$Overrides = @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(ZH_WATER_|ZH_DX11_|ZH_D3D9_|ZH_GPU_|SDL_GPU_)' })
if ($Overrides.Count) { throw ('Default evidence rejects inherited overrides: ' + ($Overrides.Name -join ', ')) }
function Invoke-IsolatedWaterGame([string]$Arguments = '', [bool]$Unattended = $false) {
    $Out = Join-Path $env:TEMP ('zh-water-default-' + [guid]::NewGuid().ToString('N'))
    $null = New-Item -ItemType Directory -Path $Out
    $Start = New-Object System.Diagnostics.ProcessStartInfo
    $Start.FileName = $Exe
    $Start.WorkingDirectory = $RunDir
    $Start.UseShellExecute = $false
    $Start.Arguments = $Arguments
    $Start.EnvironmentVariables['ZH_USER_DATA_DIR'] = $Out
    $Start.EnvironmentVariables.Remove('ZH_UNATTENDED')
    if ($Unattended) { $Start.EnvironmentVariables['ZH_UNATTENDED'] = '1' }
    $Hash = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
    $Process = New-Object System.Diagnostics.Process
    $Process.StartInfo = $Start
    try {
        if (-not $Process.Start()) { throw 'Game did not start' }
        if (-not $Process.WaitForExit(600000)) { $Process.Kill(); $Process.WaitForExit(); throw 'Water evidence launch timed out' }
        if ($Process.ExitCode -ne 0) { throw ('Game exit code ' + $Process.ExitCode) }
        [pscustomobject]@{ UserData = $Out; Executable = $Exe; SHA256 = $Hash; Arguments = $Arguments }
    } finally { $Process.Dispose() }
}
$Ordinary = Invoke-IsolatedWaterGame
~~~

Drive the real menu to a water map, observe it, and quit that process within the timeout. The ordinary call has no command-line arguments and writes no WaterStyle. Then use a different fresh user directory for the automated no-water-opt-in capture:

~~~powershell
$Capture = Invoke-IsolatedWaterGame '-win -xres 1280 -yres 720 -msaa 0 -quickstart -noshellmap -multiInstance -noaudio -map "Maps\Tournament Island\Tournament Island.map" -autoskirmish 2 -seed 1 -observer -side 0 FactionAmerica -side 1 FactionChina -camera 400 1000 -screenshot 240 -maxframes 300 -logPrefix water_default_' $true
~~~

Require $Capture.UserData/sshot*.bmp and the run's water_default_DebugLogFile.txt (Debug naming differs). H uses a unique logPrefix per call and copies logs into that call's private evidence. For motion replace the screenshot/maxframes switches with -video 120 720 water_tournament -recordfps 60 -maxframes 780. MP4 is optional when ffmpeg is unavailable; retained BMP frames are valid. No desktop-grab fallback or socket “requested” response counts as a saved native image. Do not run headless or ZH_DX11_DRIVER=null for this gate. The helper's failure tests compare the caller environment before/after launch failure, timeout and success, including pre-existing/absent values; any process-level mutation elsewhere must be restored in an outer finally.

**H, proposed isolated helper:** create C/Tools/water-check.py and C/Tools/water-check/ fixtures in task 1. Its exact common CLI is:

~~~text
python GeneralsMD/Code/Tools/water-check.py --platform windows --exe GeneralsMD/Run/generals.exe --root GeneralsMD/Run --suite defaults --out <E>/task-11/defaults
python GeneralsMD/Code/Tools/water-check.py --platform windows --exe GeneralsMD/Run/generals.exe --root GeneralsMD/Run --suite settings --out <E>/task-11/settings
python GeneralsMD/Code/Tools/water-check.py --platform windows --exe GeneralsMD/Run/generals.exe --root GeneralsMD/Run --suite production --out <E>/task-12/windows
python GeneralsMD/Code/Tools/water-check.py --platform linux --exe build-linux/generals --root <prepared-root> --overlay build-linux/overlay --suite production --out <E>/task-12/linux
python GeneralsMD/Code/Tools/water-check.py --compare <E>/task-12/linux/manifest.json <E>/task-12/windows/manifest.json --out <E>/task-12/comparison
~~~

Other required --suite values are fixture-selfcheck, lifecycle, experiments, failures, classic, gameplay and performance. Lifecycle/settings use the real menu/control input and native resource tests, not direct preference mutation as the only proof. Performance additionally requires --baseline-exe <path> --repetitions 5. A comparison validates manifest identity/time/settings before images. The helper creates a new private overlay/user folder, validates all paths, refuses existing output directories, restores environment, applies timeouts and records commands. It never deletes real Documents screenshots. On macOS use --platform macos with build-mac/generals and build-mac/overlay; choose Metal instead of hardcoded Vulkan.

Matched visuals use a fixed live single-player fixture, control port, video range and ZH_WATER_CAPTURE_CLOCK=picture; assert WaterCaptureClock::Enabled actually returned true and matching picture/water times are logged. Do not weaken its multiplayer/replay eligibility. Ordinary-launch evidence is a separate run with this control unset. The existing sun-shadow-check.py screenshot invocation lacks those eligibility conditions and cannot establish temporal parity.

**W3, deterministic gameplay/replay:** use a verified data path holding zerohour and base-game data, unique output directories and baseline-measured CRC pins. Run this block in repository-root PowerShell. The outer finally restores the prior existence and value of every environment variable the current Windows scripts mutate. Do not dot-source those scripts. Reuse the CI-created private farm for the replay invocation, never the real install:

~~~powershell
$WaterDataCandidate = if ($env:ZH_GAME_DATA) { $env:ZH_GAME_DATA } else { $env:ZH_DATA_DIR }
if (-not $WaterDataCandidate) { throw 'Set ZH_GAME_DATA or ZH_DATA_DIR to the verified game-data root' }
$WaterData = (Resolve-Path -LiteralPath $WaterDataCandidate).Path
if (-not (Test-Path -LiteralPath (Join-Path $WaterData 'zerohour') -PathType Container)) { throw 'Missing zerohour data' }
$WaterEvidence = Join-Path $env:TEMP ('zh-water-gameplay-' + [guid]::NewGuid().ToString('N'))
$WaterCi = Join-Path $WaterEvidence 'ci'
$WaterEnvBefore = @{}
foreach ($Name in @('ZH_UNATTENDED','ZH_USER_DATA_DIR','ZH_PLATFORM','PATH')) {
    $WaterEnvBefore[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
}
try {
    & .\windows-ci.ps1 -DataDir $WaterData -SkipBuild -Runs '0@2400','0@24000','1@24000' -WorkDir $WaterCi
    if ($LASTEXITCODE -ne 0) { throw 'Windows CI failed' }
    $WaterFarm = (Resolve-Path -LiteralPath (Join-Path $WaterCi 'farm0')).Path
    & .\replay-check.ps1 -Seeds 0,1 -Players 2 -Difficulty brutal -MaxFrames 24000 -RunDir $WaterFarm -UserDataDir (Join-Path $WaterEvidence 'replay')
    if ($LASTEXITCODE -ne 0) { throw 'Replay agreement failed' }
} finally {
    foreach ($Name in $WaterEnvBefore.Keys) { [Environment]::SetEnvironmentVariable($Name, $WaterEnvBefore[$Name], 'Process') }
}
~~~

Validate base-game discovery and the task-1 data/executable manifest before the block; missing assets fail setup. Append -ExpectCrc baseline values to CI before using it as a regression gate. Preserve the fresh path and all logs in E/task-13. Headless runs prove gameplay only. Supplement with rendered Modern/Classic runs and direct water-grid snapshots because W3DWater.cpp:5683 has an empty water CRC function. H applies the same default-override rejection as W2; diagnostic runs explicitly record their allowed overrides. Never reuse an output root from a prior invocation.

**L0/L1, native Linux/macOS:** run on each native OS, with game data configured through ZH_GAME_DATA or ZH_DATA_DIR:

~~~bash
(
  set -eu
  WATER_GAME_DATA="${ZH_GAME_DATA:-${ZH_DATA_DIR:-}}"
  test -n "$WATER_GAME_DATA" || { echo 'Set ZH_GAME_DATA or ZH_DATA_DIR' >&2; exit 2; }
  WATER_GAME_DATA="$(cd "$WATER_GAME_DATA" && pwd -P)"
  test -d "$WATER_GAME_DATA/zerohour" || { echo 'Missing zerohour data' >&2; exit 2; }
  export ZH_GAME_DATA="$WATER_GAME_DATA" ZH_DATA_DIR="$WATER_GAME_DATA"
  ./build-linux.sh Release test
  ctest --test-dir build-linux --output-on-failure -R '^(test_water_optical_model|test_water_grid|test_water_tracks|test_water_render_packet|test_water_shore_policy|test_sun_shadow_sorting|sdl_gpu_frame_selfcheck|sdl_program_cache_selfcheck|sdl_pipeline_state_selfcheck|posix_gpu_draw_selfcheck|ffref_gpu_selfcheck|ffref_capture_selfcheck|replay_check)$'
  bash GeneralsMD/Code/Tools/replay-check.sh --generals "$PWD/build-linux/generals" --data "$WATER_GAME_DATA" --seeds "0 1" --players 2 --aidiff brutal --maxframes 24000 --keep
)
~~~

On macOS replace build-linux with build-mac and use ./build-macos.sh Release test. Missing data/GPU means unavailable or skipped, not passed. Record the README.md:195 known NVIDIA ffref_capture_selfcheck mismatch separately. The existing sun-shadow-check.py --measure can supply POSIX whole-frame data but hardcodes Vulkan; ai-batch.ps1 always adds headless and is not a renderer benchmark.

**R0, proposed replay negative control (after task 13):** add `-VerifyControl` to replay-check.ps1 and `--verify-control` to C/Tools/replay-check.sh. Existing normal behavior and POSIX `--control` remain compatible. Run the Windows command inside W3's try/finally after its normal replay command, with `-VerifyControl` and a separate `-UserDataDir (Join-Path $WaterEvidence 'replay-control')`; all other arguments remain identical. Run the POSIX command inside L0/L1's subshell with `--verify-control --keep` appended; it creates its own mktemp root. Both new modes must first prove the live run, untouched replay and repeat live run agree with valid non-idle results. Copy the untouched replay inside the private evidence folder, locate exactly one valid SD= decimal header field, change only its final digit modulo ten, and independently verify the before/after hashes, unchanged length and exactly one intended changed byte. Retain both copies. Execute the mutated, still valid-format replay: success is a completed run with a valid result at the expected terminal frame and a different CRC. An unexpected loader rejection, crash, timeout, missing result, unchanged CRC, failed/ambiguous mutation or failed positive run is a control failure. Return 0 only when every requested control is detected; otherwise nonzero. Record machine-readable classifications, exit codes, frame/CRC pairs and mutation evidence; do not infer control success from an arbitrary nonzero process exit. Add parser/classifier tests covering each failure, and preserve install guards. Windows harness changes use child-only environment or an outer finally for every exit path.

**K0, repaired shader recorder (after task 10):** the existing `-UserData`, `-WorkDir`, `-Out` parameters already exist; task 10 repairs their semantics. With `$WaterData` normalized as above, invoke:

~~~powershell
$WaterCacheRoot = Join-Path $env:TEMP ('zh-water-shaders-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $WaterCacheRoot
& .\GeneralsMD\Code\Tools\record-dx11-shaders.ps1 -DataDir $WaterData -RunDir $RunDir -UserData (Join-Path $WaterCacheRoot 'user') -WorkDir (Join-Path $WaterCacheRoot 'work') -Out (Join-Path $WaterCacheRoot 'dx11shaders.shipped') -Seeds 0,1 -Maps 'Tournament Island','Winding River','Golden Oasis'
if ($LASTEXITCODE -ne 0) { throw 'Water shader recording failed' }
~~~

The repaired recorder must refuse existing recording roots/userdata and the real Documents user directory, direct each child to the exact private `ZH_USER_DATA_DIR`, use child-only unattended/environment settings, and never move any real player cache. It runs the task-1 production water fixtures in addition to existing map/shell runs and emits a manifest beside Out with each expected production variant, full source/profile hash, shader profile, compilation result and cache entry. Internal native FFT/coastal utility shaders use the same persistent compiler cache. Cold recording must compile every required production variant successfully; fresh-user shipped replay and subsequent warm replay of the same fixture matrix must resolve all those hashes with zero missing production entries. Keep optional/diagnostic variants in a separate manifest section. A timeout, failed game, absent cache, incomplete manifest or missing production hash is failure, even with KeepShipped. Verify the real player cache hash/existence and caller environment are unchanged on success and injected failure; keep all recording evidence under the private root. Do not use the current unmodified recorder as isolation evidence.

### Observable acceptance and measurement rules

| Evidence | Gate |
| --- | --- |
| Shader/CPU ABI | Exact sizes, offsets, declared resource types and register bindings; all production, experimental and diagnostic variants compile for their intended target; no SDL register spaces in native output. |
| Numerical GPU readbacks | Use a test-only T/WaterFftReference.h scalar double-precision transform with explicit complex arithmetic and fixed input spectra, calling no production spectrum/packing/HLSL helper. Include zero/DC/single-frequency and asymmetric channel fixtures, selected independently reviewed production-seed goldens, normalization/sign correction, all six fields and all nine mips/layers. Other packing/depth/hull tests use hand-computed points and analytic boundary cases. All outputs finite; FP32 abs <=1e-4 + rel 1e-3, FP16 abs <=2e-3 + rel 4e-3 unless an existing test has a tighter bound. Check displacement bounds, mip averages, array layers, depth unprojection, clipping and hull contact, not just nonzero pixels. These are proposed port gates; investigate failures rather than loosening them to match output. |
| Image parity | Matching data/settings/camera/projection/picture-time, pre-postprocess water ROI plus normal final-image captures. Proposed controlled-fixture gate: water ROI mean RGB error <=3/255, SSIM >=0.97; exclude only declared 1-pixel raster edge masks. Retain heatmaps and per-feature crops. A metric pass still needs semantic checks for coloured reflection, visible underwater objects/refraction, shoreline transition, wakes and hull intersection. Existing cross-backend baseline differences outside water are recorded separately. |
| Motion | At least ten seconds of water-visible frames at fixed camera, plus pan/zoom and moving vessel sequences. Verify moving geometry/crests, stable world foam tails, no shoreline boiling/permanent rim, no ghost hull/wake after teleport/reset, and pause/resume without catch-up. No single screenshot proves animation. |
| Performance | Release/hardware/default production features, fixed scene/settings/resolution (1920x1080 baseline, plus 1280x720 stress comparison), no capture/debug/readback overhead, warm-up 600 logic frames then 3600 measured frames, >=5 alternating runs. Report CPU frame mean/p50/p95/p99, GPU water-pass timestamp costs, draw/resource counts and memory high-water mark. Initial proposed regression gate: unchanged Classic and Linux Modern median mean/p95 <=5% over their same-machine baseline; Windows Modern incremental water GPU cost <=max(1.25x matched Linux cost, Linux cost+0.5ms) on the same physical GPU. Investigate variance above the gate; do not hide it with different settings. Cross-OS hardware unavailable leaves that comparison open. |
| Resource stability | Repeat 20 resize/style/map/device cycles and a 15-minute vessel/coast run; bounded cache entries, no monotonically growing live-resource count after warm-up, no stale-device references, no water-related debug-layer warnings. |

No product FPS or minimum-GPU promise is inferred. The relative budgets above are explicit engineering acceptance proposals; a fixed FPS/minimum-hardware release target is the only owner choice still useful, and does not block implementation planning.

## Execution strategy
### Parallel execution waves
Three waves of five tasks, with the dependency order below enforced inside each wave. Parallelize only disjoint owners after their dependencies pass; never edit the shared shader/packet files from two workers concurrently.

- Wave 1, foundations (1-5): reproducible baseline/fixtures, shared contract, native resource/snapshot owner, W3D shared routing, settings capability. Ends with tested infrastructure and reachable code, not a parity claim.
- Wave 2, complete rendering (6-10): native analytic surfaces, FFT displacement, hull/wake/foam, shoreline and optional coastal resources, then lifecycle/cache/failure integration. Ends with all production features and existing experiments implemented.
- Wave 3, evidence (11-15): ordinary-launch/settings/package proof, matched water comparisons, compatibility/regression, performance, then CI/evidence reconciliation. Each task may repair the specific gaps its tests reveal.
- Final wave F1-F4: independent verification of the exact finished revision and evidence manifest.

### Dependency matrix
| Todo | Depends on | Blocks | Can parallelize with |
| --- | --- | --- | --- |
| 1 | — | 2,11-15 | — |
| 2 | 1 | 3-5 | — |
| 3 | 2 | 6,7,10 | 4,5 |
| 4 | 2 | 6,8,9 | 3,5 |
| 5 | 2 | 10,11 | 3,4 |
| 6 | 3,4 | 7-10 | 5 if unfinished |
| 7 | 6 | 8-10 | — |
| 8 | 4,7 | 9,10 | — |
| 9 | 6,7,8 | 10 | — |
| 10 | 3,5,6,7,8,9 | 11-15 | — |
| 11 | 1,10 | 15 | 12,13,14 on separate hosts/workloads |
| 12 | 1,10 | 15 | 11,13; no simultaneous GPU measurements |
| 13 | 1,10 | 15 | 11,12,14 on separate hosts |
| 14 | 1,10 | 15 | CPU-only work; exclusive measured GPU |
| 15 | 11,12,13,14 | F1-F4 | — |
| F1-F4 | 1-15 | Completion | Each other, read-only |

## Todos
> Implementation + Test = ONE todo. Never separate.
<!-- APPEND TASK BATCHES BELOW THIS LINE WITH edit/apply_patch - never rewrite the headers above. -->
- [x] 1. Preserve the baseline and make real water fixtures reproducible

  What to do / Must NOT do: Record the intake dirty patch/untracked optical test, executable/data/cache hashes, baseline CRCs and available hosts. Add C/Tools/water-check.py, C/Tools/water-check/manifest.schema.json, scenes.json and private fixture overlays. Validate Tournament Island (400,1000, frame 240), Winding River (1300,1700, frame 240), Golden Oasis (1750,1500, frame 600), plus an isolated authored water test map for concave/disconnected polygons, multiple levels, grid and shore ownership. Create the missing disposable WaterCaptureCarrier INI template and verified navigable movement/stop/reverse/teleport route. Use existing scenario grammar and capture gates; never install fixture assets into the user's game data. Preserve original production surface/particle defaults. Add helper self-tests for manifest identity, output isolation, timeout, image presence and temporal eligibility, inherited override rejection, executable/data hash mismatch, and caller environment preservation on success, launch failure and timeout. Validate normalized game-data/base-game paths before launching.

  Parallelization: Wave 1 | Blocked by: none | Blocks: 2,11-15. Finish baseline capture before any product edits.

  References: C/Tools/sun-shadow-check.py; P/WaterCaptureClock.h:8-14; P/WaterCaptureFixture.h; C/GameEngine/Source/GameLogic/System/ScenarioDrill.cpp; T/test_gameengine.cpp:16367; C/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp:3942; dx11-check.ps1:49-86; draft intake hashes.

  Acceptance criteria: H --suite fixture-selfcheck creates validated fresh manifests and real scene/vessel visibility evidence using current Linux Modern where available and Windows Classic baseline; baseline unavailable hosts remain explicit. W3 measures pins before product edits. The template demonstrably moves through water; parser acceptance alone is insufficient. The runner implements the documented H CLI and returns nonzero for failed/missing evidence.

  QA scenarios: Happy: run H fixture-selfcheck and W2 baseline capture, inspect landmarks/water masks and at least two vessel poses. Failure: helper self-tests feed mismatched picture time, empty/black image, no water ROI, missing template, stale executable hash, existing output directory and rejected scenario; each must fail without touching real userdata. Evidence: E/task-1/fixtures, baseline, helper-tests.

  Commit: Y, selective new helper/fixtures only | test(water): add isolated parity fixtures and baseline manifests.

- [x] 2. Extract the shared packet, shader ABI and target-aware generators

  What to do / Must NOT do: Add P/WaterRenderPacket.h, WaterShaderLayout.h, WaterRenderBridge.h and WaterGpuShaders.h. Move common packing from SDL constants/draw code into one tested packer with explicit availability fields and finite/bounded inputs. Preserve the exact ABI table above and public production switches. Share canonical modern/shore/FFT/coastal HLSL with explicit SDL/native declarations; retain current generated vertex semantics. Add native compile/reflection tests and cross-platform packet tests, registering proposed test_water_render_packet and the initial test_dx11water target. Do not rewrite math, grow ordinary four-stage constants or send the scratch bank to D3D9.

  Parallelization: Wave 1 | Blocked by: 1 | Blocks: 3-5.

  References: S/SdlConstants.h:79-115; S/PosixDevice9Draw.cpp:510-563,1494-1514; P/WaterHullVisualLayout.h:5-27; P/WaterFftSampling.h:25-62,151-198; P/WaterFftSpectrum.h:106-164; P/WaterCoastalIncident.h:44; S/SdlGpuFrame.cpp:613,878; D/engineshader.cpp:499,507-521,1656-1705; D/dx11backend.cpp:40,1684; C/CMakeLists.txt.

  Acceptance criteria: N0 compile and test_water_render_packet pass with exact offsets/sizes/resource types and every feature variant; existing W1 shader generators/compilers and Linux/macOS SDL shader self-checks still pass. Shared SDL output remains semantically identical; changed text is accounted for in cache keys.

  QA scenarios: Happy: N0 compile reflects flat/river/FFT/shore/mesh/experimental/debug variants and verifies pixel-centre/depth-range matrices with independent values. Failure: tests reject overflowed packet counts, wrong resource type, missing packet cbuffer and native source containing SDL register spaces. Evidence: E/task-2/abi.json, reflected-shaders, commands.log.

  Commit: Y, excluding unrelated dirty hunks | refactor(water): share render packets and shader layouts.

- [x] 3. Add the native water owner and immutable scene snapshots

  What to do / Must NOT do: Add D/dx11water.h/.cpp, backend ownership/runtime forwarding/stubs, required-format probing and water resource descriptors. Replace secondary depth values with typeless texture/DSV/SRV records preserving existing sharing. Implement the eight-entry paired snapshot cache, exact resource-generation keys, invalidation and pass scope. Use native single-sample copies; retain HDR and viewport depth range. Do not reuse cloak/particle/mutable target copies or take late shore snapshots.

  Parallelization: Wave 1 | Blocked by: 2 | Blocks: 6,7,10 | Can run with: 4,5.

  References: D/dx11device.cpp:248-293,308; D/dx11backend.cpp:1763-1796,2670,2847-2891; D/dx11runtime.cpp:119,400,527,690; D/dx11runtime.h; D/dx11runtime_posix.cpp; S/SdlGpuFrame.cpp:1075-1134; P/WaterShoreOverlay.h:17-51; T/test_dx11backend.cpp:754,840; T/test_dx11device.cpp:105; C/CMakeLists.txt.

  Acceptance criteria: N0 scene and existing test_dx11backend/test_dx11device pass on hardware and WARP. Readbacks prove immutable colour and depth across two water draws, different targets, clear, same-size depth sharing, view changes and target address reuse. Snapshot pair validity is atomic; native debug layer has no alias warnings.

  QA scenarios: Happy: N0 scene renders distinct opaque patterns/depth into HDR scene, backbuffer and secondary targets, snapshots then mutates water output, and verifies the original values remain. Failure: inject failed colour/depth allocation, unexpected sample count and stale generation; refuse safely, restore exact OM/viewport/cache state and never publish half a pair. Evidence: E/task-3/scene-readbacks, debug.log, failures.json.

  Commit: Y | feat(dx11): add water resource ownership and scene snapshots.

- [x] 4. Route shared water preparation through effective backend capability

  What to do / Must NOT do: Adapt W/W3DWater.cpp and WI/W3DWater.h to typed packets/bridges using the complete guard inventory. Reuse CPU height/bed/optical textures, polygon refresh, projected meshes, hull/shore/history preparation and render scopes. Update P/WaterCoastalRender.h and WaterShoreOverlay.h bridges plus their POSIX implementations. Keep actual platform timing/COM/SDL APIs guarded. Preserve rivers, grids, legacy sea types and Classic dispatch. Add cross-platform lifecycle/geometry transport tests; move pure shore coverage tests into proposed T/test_water_shore_policy.cpp so Windows can run them without POSIX linkage.

  Parallelization: Wave 1 | Blocked by: 2 | Blocks: 6,8,9 | Can run with: 3,5.

  References: W/W3DWater.cpp guard inventory, especially :1175-1455,2530-2566,3571,3930,4362-4856,4874,5279; WI/W3DWater.h:49-61,239-331,380-395; P/WaterProjectedMesh.h; P/WaterOpticalContext.h; S/PosixDevice9Draw.cpp:70-99 bridge definitions; T/test_water_grid.cpp:1442-1686,1975-2143,3982-4074; T/test_water_tracks.cpp; C/CMakeLists.txt:1587,2305.

  Acceptance criteria: test_water_render_packet, test_water_shore_policy, test_water_grid and test_water_optical_model pass on Windows and POSIX. Native-capable paths produce complete packets; Classic/D3D9 invokes no high-register/slot bridge. POSIX rendering captures retain current behavior.

  QA scenarios: Happy: packet tests cover flat patches, rivers, grid deformation, reversed/concave edges, multiple levels and paused updates. Failure: empty terrain, missing optical texture, offscreen polygons, capacity limit and unsupported backend retain defined legacy/fallback behavior without changing simulation. Evidence: E/task-4/guards-audit.md, packet-tests, posix-regression.

  Commit: Y, preserve intake portability edits | refactor(water): share preparation across supported renderers.

- [x] 5. Make Water style capability-aware without changing preference semantics

  What to do / Must NOT do: Add typed Display/W3DDisplay capability reporting and replace blanket Windows hiding with the specified supported/disabled behavior. Add explicit availability filtering to readCatalogWidgets; preserve requested WaterStyle through D3D9/fallback and unrelated Accept. Retain m_waterStyle=1 and missing-key handling. Implement the WaterStyle Accept transaction specified above: collect without the live setter, commit the candidate preference file atomically with checked write/flush/sync/close/replace, then update requested style and schedule effective synchronization. Back/Esc leaves water runtime/history/disk untouched. Check the Bool write result before hotkey save or menu close; on failure keep the pending selection open with a localized error and retry/cancel. This does not redesign non-water preview/resolution confirmation. Add an unavailable tooltip through the existing localization fallback; regenerate/stage window assets only if their source generator needs a change. Update SDL-only comments, not the setting key/value format.

  Parallelization: Wave 1 | Blocked by: 2 | Blocks: 10,11 | Can run with: 3,4.

  References: C/GameEngine/Include/GameClient/Display.h:198; C/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp:1140,4768; G/GlobalData.cpp:665,698; G/OptionsCatalog.cpp:99,401,626-637; G/UserPreferences.cpp:132-180; C/GameEngine/Include/Common/UserPreferences.h:66; M:1066-1074,1111-1151,1339-1353,2799-2813,2956-2990; C/Tools/optionsmenu_layout.py:168,342; T/test_gameengine.cpp:14204,14230,14305; C/CMakeLists.txt:1753,1782,2660.

  Acceptance criteria: W1 water_style/option_catalog and optionsmenu_selfcheck pass; new tests cover requested/effective separation, actual D3D11 fallback, missing key, explicit Classic, Accept/Back/Esc and disabled-widget readback. No inactive or cancelled selection changes disk or live water style/history. Fault injection at temp open, formatted/short write, flush/sync, close and atomic replacement preserves prior file bytes (or absence), requested/effective water state and history generation; no owned temp remains. Retry commits and relaunch reads the selected value. Existing non-water serialization and preview behavior remain covered.

  QA scenarios: Happy: preferences unit/integration tests load fresh/missing/0/1 files, stage/accept/cancel and reopen; H settings gains real-menu cases for task 11. Failure: unavailable backend and each injected persistence failure preserve water state and old file, report save failure, keep the menu open and skip hotkey persistence/menu close; retry and Back/Esc are tested. Check existing and absent Options.ini on Windows and POSIX, including unrelated preference keys/metadata. Evidence: E/task-5/settings-tests.json, asset-check.log.

  Commit: Y | feat(options): expose water style for capable Windows renderers.

- [x] 6. Render native modern analytic surfaces, optics and coloured reflections

  What to do / Must NOT do: Connect packet binding to dedicated native water pipelines and atomic prepared-patch submission, retaining all geometry ranges until emission. Bind all required resources/samplers/common constants, including sun/smoke at water slots. Enable modern river/flat shading, scene depth/refraction, bed/terrain optics, shroud/fog and coloured planar mirror composition. Preserve Modern mirror transparent background and force refresh on transition; replace the flat legacy DESTALPHA blend with the SDL modern SRCALPHA behavior. Keep the explicit failure policy above; no FFT completeness claim yet.

  Parallelization: Wave 2 | Blocked by: 3,4 | Blocks: 7-10 | Can run with: 5 if unfinished.

  References: D/engineshader.cpp:499-1430,1656; S/PosixDevice9Draw.cpp:953-1003,1043-1051,1096-1188; W/W3DWater.cpp:2848,3076,4839,4874,5119; D/dx11backend.cpp:2073-2123,2745-2790,2908; D/dx8wrapper.cpp:2908; T/test_dx11pipeline.cpp.

  Acceptance criteria: N0 surface/scene and existing native backend/pipeline tests pass. Controlled GPU fixtures independently verify coloured reflection, transparent mirror background, object depth/refraction, edge alpha and shroud; accepted receipts are truthful. Water→ordinary draw sequence matches its no-water state reference.

  QA scenarios: Happy: two levels with distinct hull/time packets followed by a river and terrain/UI, with post on/off; inspect colour/depth readbacks and captures. Failure: inject missing texture, compiler/buffer failure and refusal while preparing a later batch: zero modern batches may emit and the whole original patch falls back to Classic with no ownership receipt. Simulated device loss after emission invalidates admissions and recovers next frame; no impossible same-frame partial fallback is asserted. Evidence: E/task-6/surface-readbacks, reflections, refusal-tests.

  Commit: Y | feat(dx11): render modern water optics and reflections.

- [x] 7. Port the raster FFT and displaced geometry

  What to do / Must NOT do: Implement the exact FFT pass/mip/array sequence in the native owner, sharing deterministic spectrum and HLSL. Add the independent T/WaterFftReference.h oracle described above. Add FFT vertex variants and bind displacement maps/height cage. Preserve bounds, original-XY sampling, projected fallback coverage, 16-bit batch limits and flat-only eligibility. Skip repeated same-time updates; commit ready/time only after successful preparation. Keep analytic fallback but mark it as degraded in diagnostics with no replacement ownership.

  Parallelization: Wave 2 | Blocked by: 6 | Blocks: 8-10 | Can run with: no overlapping shader/resource work.

  References: P/WaterFftSpectrum.h:12,49,94,106,164; P/WaterFftSampling.h:99,151-198; P/WaterFftGeometry.h:8-14; S/SdlGpuFrame.cpp:370,453-504; S/SdlProgramCache.cpp:238-240; S/PosixDevice9Draw.cpp:953-959,1108,1494; W/W3DWater.cpp:4591,4641-4731,4811; T/test_water_grid.cpp.

  Acceptance criteria: N0 fft plus existing FFT model tests satisfy numerical bounds for spectra, phase, all six outputs, mip averages and array layers. Native geometry readbacks prove displacement, not merely normal animation. Same-time reuse performs no additional FFT update; required production runs report FFT active.

  QA scenarios: Happy: known seed/time and subsequent time, paused reuse, large/high-angle camera and fallback mesh; compare independent expected points and Linux outputs. Failure: allocation/compile/pass failure at every stage leaves no partially ready array and restores render state; overflow/capacity tests preserve water coverage. Evidence: E/task-7/fft-readbacks, displacement-clip, pass-counts.json.

  Commit: Y | feat(dx11): port spectral water waves and displacement.

- [ ] 8. Enable hull interaction, vessel waves and persistent foam

  What to do / Must NOT do: Carry mean/contact/mesh hull packets into both shader stages; port bow lift, shoulder waves, wave cages, hull clearance and foam lighting/history atlas sampling at production defaults. Mirror CPU texture dirty updates and all metadata blocks. Replace wake rendering only through current-frame accepted owner receipts using the existing exact policy. Do not alter particle emission, aging, IDs, gameplay movement or unrelated splash/spray/smoke.

  Parallelization: Wave 2 | Blocked by: 4,7 | Blocks: 9,10.

  References: W/W3DWater.cpp:456-927,1015-1108,1209,4839; P/WaterHullVisual.h; WaterHullVisualLayout.h; WaterVesselWaves.h; WaterBowLift.h; WaterBowShoulder.h; WaterFoamHistory.h; WaterFoamWindow.h; WaterFoamParticlePolicy.h:18; C/GameEngineDevice/Source/W3DDevice/GameClient/W3DParticleSys.cpp:112; T/WaterHullContactFoamTest.h; T/WaterWaveCageTest.h; D/dx11texture.cpp:562,600,630.

  Acceptance criteria: N0 hull and reused hull/foam/grid tests pass; carrier fixture shows hull clearance, bow/shoulder response, moving wake and persistent world-space trail matching Linux. Suppression is limited to exact shipped eligible surface wake pairs after full accepted coverage.

  QA scenarios: Happy: moving/stopped/reversing/rotating vessels, two hulls at different levels, offscreen return, paused camera movement and an owner footprint spanning two accepted adjacent patches. Failure: two disjoint same-level patches with one refused must admit only fully covered owners; teleport, hidden/shrouded/replaced owner, reused ID, degraded draw, incomplete native patch, wireframe, mirror view and detached/mod effect must not inherit admission or erase legitimate legacy effects. Evidence: E/task-8/hull-readbacks, carrier-frames, admission.json.

  Commit: Y | feat(water): enable native hull interaction and vessel foam.

- [ ] 9. Complete shoreline ownership and optional coastal GPU parity

  What to do / Must NOT do: Bind/update default shore history/foam and port the authored-track wrapper with exact original-scene depth and whole-track convex coverage. Record accepted regions only after successful water draws. Implement optional coastal domain uploads, incident/reaction/diffusion/view resolve and crest variants with shared shaders, immutable revision validation, exact phase and history-clock semantics. Keep SURF_GPU/COAST_CREST off by default.

  Parallelization: Wave 2 | Blocked by: 6,7,8 | Blocks: 10.

  References: W/W3DWater.cpp:952-1133,2560; W/W3DWaterTracks.cpp:546; P/WaterShoreOverlay.h:17-140; P/WaterShoreHistory.h; WaterShoreSource.h; WaterShoreFoam.h; WaterCoastalDomain.h; WaterCoastalView.h; WaterCoastalIncident.h; WaterCoastalSurf.h; WaterCoastalCrest.h; S/SdlGpuFrame.cpp:686,788-829,942,992,1044; S/PosixDevice9Draw.cpp:980-1003,1573-1589; T/test_water_tracks.cpp; T/WaterCoastalCapacityTest.h.

  Acceptance criteria: N0 shore/coastal and test_water_shore_policy pass; H experiments validates each opt-in alone and combined, then a clean default run proves they are off. Default shoreline already works without these experiments. World histories survive camera movement and resize, respect levels and dry boundaries and stay within capacity.

  QA scenarios: Happy: irregular wet/dry coast with moving occluder, disjoint levels and authored tracks inside accepted water; verify original depth occlusion and preserved fog/shroud/alpha. Failure: partial coverage, wrong level/winding, stale view/capture/revision/FFT time, missing depth, failed water draw, editor preview and allocation failure retain legacy tracks or safely disable only unavailable composition; coastal clock never advances on failed update. Evidence: E/task-9/shore-readbacks, experiments, ownership.json.

  Commit: Y | feat(dx11): port shoreline composition and coastal passes.

- [ ] 10. Close lifecycle, cache, capability and state-restoration boundaries

  What to do / Must NOT do: Integrate all water paths with resize/minimize, display rollback, map reset, style transitions, device recreation, shutdown and texture-mirror epochs. Preserve correct distinct lifetimes and clocks. Finalize variant keys, cache population and the explicit capability/failure policy. Repair C/Tools/record-dx11-shaders.ps1's existing UserData/WorkDir/Out parameters as specified by K0: bind actual child userdata, refuse real/existing roots, eliminate player-cache moves, preserve caller environment and add fixture/variant manifests. Route native utility shader compilation through the persistent cache and enumerate production variants. Never move the real player's shader cache. Stage C/Data/dx11shaders.shipped using the existing CMake copy step. Inject later-batch preflight/capture refusals and simulated device loss before/after emission; prohibit submit-time resource creation. Add leak/state tests. Do not change save schema or persist effective fallback.

  Parallelization: Wave 2 | Blocked by: 3,5,6,7,8,9 | Blocks: 11-15.

  References: W/W3DWater.cpp:1146-1170,2027-2037,2414-2425,2499,2530,2723; D/dx11backend.cpp:1684,2073-2123,2670; D/dx11runtime.cpp:119,690; D/dx11texture.cpp:562,600; C/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp:906-954; T/test_water_grid.cpp:4052,4499-4602,5365-5445; T/test_dx11device.cpp:105; C/Tools/record-dx11-shaders.ps1:46-84,104-128; C/CMakeLists.txt:1811.

  Acceptance criteria: N0 lifecycle and H lifecycle/failures pass 20 cycles on actual native device plus WARP unit coverage. No water-related debug-layer warning, stale COM reference, unbounded cache growth, resource loss after recreation or incorrect requested/effective style. K0 cold/warm/shipped variants all produce the same features with exact source/profile/cache hashes and zero missing production entries. Private cache routing is demonstrated and real player cache/environment hashes are unchanged, including on failure.

  QA scenarios: Happy: paused Modern→Classic→Modern with stationary camera; resize accept and revert using -resdrill 600 1600 900 with/without -resdrillkeep; new map, save/load, device release/recreate. Failure: allocation/compiler failure, unsupported required format and failed resolution change restore valid state; followed terrain/road/particle/UI images remain correct. Exercise K0 success, game timeout/compile failure and incomplete manifest; verify no false shipped-cache success and no player-cache changes. Do not record a fixed-size movie across a resize. Evidence: E/task-10/lifecycle, failures, live-resources.json, debug.log.

  Commit: Y | fix(dx11): preserve water state across renderer lifecycle changes.

- [ ] 11. Prove ordinary Windows defaults, menu semantics and staged packaging

  What to do / Must NOT do: Run W0/W1, W2 and H defaults/settings against the actually staged executable and assets, correcting only uncovered port defects. Test fresh userdata, an older Options.ini without WaterStyle, explicit Classic, saved Modern, unrelated Accept on D3D9 and automatic D3D11 fallback, and restoration to supported D3D11. Exercise real menu Accept, Back/Esc, same-selection, repeated toggles, paused toggle, relaunch and each persistence failure boundary from task 5. A failed Accept keeps the menu/pending choice open, preserves old file plus water state/history, skips hotkey save/close, supports retry or Back/Esc, and relaunch still reads the old value until a successful commit. Capture supported and unavailable UI in normal packaged layout/localization.

  Parallelization: Wave 3 | Blocked by: 1,10 | Blocks: 15 | Can run with: 12-14 on separate hosts/workloads.

  References: M:1066,1111,1353,2956-2990; G/UserPreferences.cpp:132-179; C/CMakeLists.txt:1733-1811; C/Tools/optionsmenu_layout.py; C/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp:3942,4768; W2/H command catalogue.

  Acceptance criteria: No-opt-in ordinary launch shows actual D3D11, active full production Modern and visible animated water. Fresh/missing-key files yield Modern; explicit 0 yields Classic. Cancel changes neither file nor runtime; Accept survives reopen/relaunch. D3D9 remains playable Classic, shows unavailable Modern truthfully, and retains a previously requested Modern value.

  QA scenarios: Happy: W2 and H defaults/settings retain water-visible images/video, real menu screenshots and before/after Options.ini diffs. Failure: inherited water override contamination, NULL/headless backend, missing staged assets, blank screenshot or save failure makes the helper fail the corresponding gate. Evidence: E/task-11/defaults, settings, package-manifest.json.

  Commit: Y if repairs needed; otherwise evidence only | test(water): verify Windows default and settings behavior.

- [ ] 12. Establish full Linux-to-Windows visual and numerical parity

  What to do / Must NOT do: Run H production on matched Linux/Windows builds with the same data/fixtures and eligible picture clock, then H compare; run experiments separately. Exercise ocean, pool, river, concave/disconnected/multiple-level patches, grid and supported legacy branches; different light counts, shroud/fog, low/high/custom detail and soft-water-edge settings. Include normal post and post off, requested MSAA 0/4 with actual native sample count recorded, window/fullscreen, reflections of coloured objects, submerged objects and vessel routes. Review image crops, heatmaps and temporal clips. Missing a feature cannot be excused by analytic fallback.

  Parallelization: Wave 3 | Blocked by: 1,10 | Blocks: 15 | Can run with: 11,13; measurements use separate GPU time.

  References: W/W3DWater.cpp:3076,3571,3930,4591,4875; S/PosixDevice9Draw.cpp:953-959; P/WaterCaptureClock.h; C/Tools/sun-shadow-check.py existing case definitions; N0/H and numerical/image gates above.

  Acceptance criteria: N0 full native suite passes; matched manifests prove feature/timestamp equivalence; numerical/image gates and semantic/motion checks all pass. Preserve Modern-versus-Classic Windows pairs as intentional differences, not a D3D9 equality test. Record unverified hosts honestly and leave parity open until available.

  QA scenarios: Happy: H production/compare shows moving displacement, coloured reflections, refraction, shore transitions/foam, bow response, hull clearance and trailing wake under ordinary defaults. Failure: comparer rejects wrong time/camera/data/backend, missing feature bits, blank/non-water screenshots, stale cache artifacts and a deliberately damaged readback. Evidence: E/task-12/{linux,windows,comparison,experiments}, clips and feature-checklist.json.

  Commit: Y if focused repairs needed; otherwise evidence only | test(water): verify native modern-water parity.

- [ ] 13. Verify gameplay, saves, replay and Linux/macOS compatibility

  What to do / Must NOT do: Implement R0 modes in replay-check.ps1 and C/Tools/replay-check.sh with mutation/classifier tests and Windows environment restoration, retaining normal and existing --control semantics. Run W3, L0/L1, R0, rendered gameplay H suites and reused CPU water tests. Compare baseline/current direct grid state under one render, multiple renders and no-render catch-up per logic frame; different camera/render caps must not change simulation. Save during active grid motion, reload in-process and fresh-process, and compare subsequent evolution; verify prior-map presentation histories are cleared/rebuilt. Preserve save version and ordinary wall-clock scenery semantics in replay.

  Parallelization: Wave 3 | Blocked by: 1,10 | Blocks: 15 | Can run with: 11,12,14 on separate hosts.

  References: C/GameEngine/Source/GameLogic/System/GameLogic.cpp:4631; C/GameEngine/Source/GameLogic/Map/WaterGridMotion.cpp:32; W/W3DWater.cpp:2530,2601,5683-5746; T/test_water_grid.cpp:121,4499; P/WaterCaptureClock.h:8; replay-check.ps1:27-90; C/Tools/replay-check.sh:42-53,125,296-351; R0 contract; README.md:195.

  Acceptance criteria: Baseline-pinned CRC/frame and direct grid-state checks agree for Classic/Modern, native/POSIX, live/replay and save/load; R0 negative replay control passes only for the independently verified valid mutation and completed expected CRC divergence; arbitrary failure never counts as detection. Linux Vulkan and macOS Metal compile/tests and representative visual/lifecycle captures pass with original production defaults. Required unavailable hosts remain open evidence gates.

  QA scenarios: Happy: W3/L1 matched seeds/replays plus H gameplay and pause/script-pause/debug-pause resume; same eligible picture time reconstructs visuals without advancing gameplay. Failure: R0 rejects unchanged/ambiguous mutation, matching corrupted CRC, wrong frame, crash, timeout, missing output or failed positive replay. Rewind, nonzero-frame load and consecutive equal-dimension maps expose incorrect persistence; grid assertions catch changes even if overall CRC omits water. Evidence: E/task-13/crc-pins, grid-snapshots, saves, linux, macos.

  Commit: Y if regression repairs needed; otherwise evidence only | test(water): preserve gameplay and cross-platform behavior.

- [ ] 14. Measure production cost and resource stability

  What to do / Must NOT do: Add diagnostic-only GPU timestamp instrumentation for native FFT/capture/coastal/surface passes and comparable SDL water timing; default path must not wait for query/readback. Run H performance with the declared warm-up/duration/repetitions, one GPU workload, cold/warm caches, simple coast and vessel-heavy stress. Compare baseline/current Windows Classic and Linux Modern, then matched-hardware Linux/Windows Modern. Report incremental Modern cost, CPU preparation/upload cost and memory separately. Repair port-caused budget failures; do not disable production features.

  Parallelization: Wave 3 | Blocked by: 1,10 | Blocks: 15 | Exclusive use of measured GPU.

  References: W/W3DWater.cpp:2968,3260; S/SdlGpuFrame.cpp:453-504; C/Tools/sun-shadow-check.py --measure; ai-batch.ps1 headless limitation; H performance CLI and measurement rules above.

  Acceptance criteria: Five alternating runs meet the declared relative budgets with raw logs and variance; 20 lifecycle cycles plus 15-minute stress have bounded native resources and caches. Hardware/driver differences and unavailable same-GPU cross-OS comparison are explicitly reported, not normalized away.

  QA scenarios: Happy: H performance --baseline-exe <archived-baseline> --repetitions 5 records CPU/GPU tables and memory histories without capture/debug overhead. Failure: helper rejects WARP/NULL/headless, active screenshot/video/debug controls, concurrent measurement workloads and missing timestamp samples; forced resource churn is detected. Evidence: E/task-14/performance, raw-runs, resource-history.csv.

  Commit: Y for diagnostic harness changes | perf(water): measure native water cost and resource lifetime.

- [ ] 15. Integrate CI gates and reconcile the final evidence set

  What to do / Must NOT do: Register all new shared/native tests and correct windows-ci.ps1 GPU/session exclusions so GPU jobs run on a real suitable host and unavailable jobs report honestly. Adapt dx11-check.ps1 to isolated userdata/output and pin WaterStyle=0 for its existing D3D9-vs-D3D11 post-off comparison. Keep modern Linux parity in the new helper. Verify source/staged/package executable, overlay and shader-cache identities, isolated command environments/data/output roots, K0 production variant coverage, and R0 explicit result classifications; document defaults, experiments, fallback reasons and exact reproduction commands. Collect final evidence against one revision plus known dirty-patch identity; rerun only affected gates after repairs, then full required build suites.

  Parallelization: Wave 3 | Blocked by: 11,12,13,14 | Blocks: F1-F4.

  References: C/CMakeLists.txt:1411,1587,1733-1811,2140,2305,2660; windows-ci.ps1:112; dx11-check.ps1:49-86; build.bat; build-linux.sh; build-macos.sh; all task manifests.

  Acceptance criteria: W0, W1, N0 and available native L0/L1 lanes run nonzero tests; final matrix links each required feature/scenario to current numerical/runtime evidence. Classic backend comparison retains its existing tolerance. No green-only CI claim replaces actual default-launch/visual evidence; unavailable mandatory gates keep implementation incomplete.

  QA scenarios: Happy: clean and warm shader-cache staged launches, complete test enumeration/results, isolated Classic compare, final manifest verification. Failure: missing DLL/shader/asset, stale staged executable, skipped GPU job, mismatched evidence revision and absent task artifact fail reconciliation. Evidence: E/task-15/final-matrix.md, package-manifest.json, CI logs.

  Commit: Y, selective integration/docs only | ci(water): gate native parity and preserve classic coverage.

## Final verification wave
These are future implementation gates, not reviews claimed by this planning document. Run in parallel after tasks 1-15; each reviewer reads the exact source revision/dirty-patch digest and independently checks its evidence. All must approve before reporting implementation complete. The user's current request authorizes only this plan; it does not authorize implementation, merge or publication.

- [ ] F1. Plan compliance audit

  Scope/references: this plan, final feature/default/capability matrix, all task manifests and source diff. Confirm every feature, exclusion, setting scenario, experimental policy and compatibility constraint is implemented. Reject missing runtime evidence or stale-revision claims.

  Acceptance/QA: independently run the task-15 manifest verifier and spot-check its file hashes, test counts and feature bits; a deliberately missing artifact or mismatched revision must fail. Evidence: E/final/F1-compliance.md. Commit: N.

- [ ] F2. Code quality review

  Scope/references: shared packet/HLSL, dx11water owner, native caches/lifecycle and settings diff; Microsoft binding/copy/resize contracts linked above. Review ownership, bounds, failure/partial-draw paths, cache keys and cleanup without accepting executor summaries as proof.

  Acceptance/QA: reproduce N0 hardware/WARP boundary tests and inspect debug-layer output; injected alias/packing/refusal cases must be detected. No unresolved actionable correctness defect. Evidence: E/final/F2-quality.md. Commit: N.

- [ ] F3. Real manual QA

  Scope/references: staged game, W2/H commands, actual Options UI and matched native runtime artifacts. The verifier personally observes ordinary launch, visible animated Modern water, Classic, all required surface/shore/vessel interactions, Accept/Cancel, pause, resize and map changes.

  Acceptance/QA: rerun a fresh default launch and representative matched Linux/Windows clip, inspect water ROIs and active-feature evidence; reject a UI-only/blank screenshot or a feature fallback. Evidence: E/final/F3-runtime.md plus fresh screenshots/clips. Commit: N.

- [ ] F4. Scope fidelity and regression audit

  Scope/references: intake hashes/diff, final source diff, W3/L0/L1, save/grid evidence, performance tables and unavailable-host list. Confirm original user edits remain, D3D9 Classic and Linux/macOS behavior are preserved, experiments remain opt-in and no simulation/save-format change slipped in.

  Acceptance/QA: compare intake/final hunks and independently inspect baseline/current CRC/grid/performance results; missing required host evidence or a silent default change prevents approval. Evidence: E/final/F4-fidelity.md. Commit: N.

## Commit strategy

No commits are made while planning. During authorized execution, use focused commits at the task boundaries above only after their attached tests pass. Read repository commit guidance then; do not stage the whole worktree. Preserve and record the seven intake dirty paths, including the untracked optical test; selectively stage only the port's own hunks unless the user separately authorizes incorporating their existing work. If a task needs a separate worktree, carry an explicit copy of the intake patch/untracked input into it and verify hashes, rather than silently starting from clean HEAD and losing the preparation. Do not reset/stash the original checkout. No PR, merge, publishing or broad cleanup is part of this plan.

When shared extraction touches an intake file, record before/after diff attribution. Evidence binds to the full revision plus any still-uncommitted patch digest; a commit alone is insufficient in this workspace. Tasks 11-14 may produce evidence without source commits; never manufacture empty implementation changes for a commit label.

## Success criteria

Implementation completion requires every item below, with direct evidence links in E/task-15/final-matrix.md:

- Ordinary staged Windows launch reaches actual native D3D11 and full production Modern with no water opt-ins; water is visible and moving in native captures.
- Surface shading, coloured reflections, bed/depth optics, refraction, FFT generation/sampling/displacement, shoreline transition/foam, persistent history, vessel wakes and hull interaction all pass numerical and semantic evidence.
- Existing SURF_GPU/COAST_CREST experiments work only when explicitly enabled; diagnostic/capture controls remain optional and cannot supply the only default proof.
- Fresh/missing-key Modern, explicit Classic, supported/unavailable UI, Accept, Back/Esc, disk persistence/relaunch, same-selection, paused switching and D3D9 preference retention all pass.
- Actual-target snapshots, multiple levels/views, accepted-draw ownership, partial rejection, state cleanup, cold/warm/shipped shader variants, resize/revert/device recreation/map changes and resource stability pass.
- Classic D3D9/native regression, Linux Vulkan and macOS Metal checks pass; gameplay CRC plus direct grid/save/replay invariants hold.
- Matched Linux/Windows visual and temporal comparisons and declared performance/resource gates pass; raw measurements and limitations are retained.
- All required tests execute nonzero cases, required GPU/OS lanes are available, and F1-F4 approve the exact finished source/evidence identity.
- Existing user changes remain preserved. No fallback, skipped test or unavailable host is reported as parity.

Planning completion is separate: source anchors/commands verified, all 15 tasks and four final gates have dependencies and executable evidence requirements, the gap review is incorporated, and only .omo planning artifacts changed. No product implementation, runtime parity or test execution is claimed in this planning turn.
