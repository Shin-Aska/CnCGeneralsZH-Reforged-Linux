# Linux render / logic-thread audit

Audited against `main` at `af9006aa`, after the upstream v3.2.0 merge
`9fd2fd4a`. The comparison with `upstream/main` included the fork's guarded
and unguarded render additions, not just the named entry points.
Fixes and the polygon regression are committed as `97d6e2a6` on
`codex/linux-render-logic-snapshots`.

`W3DDisplay.cpp::takeReadsForRender` runs before `OverlapWindow::open`.
The tick can then change or delete Objects while the main thread renders.
Drawable deletion, model updates, particle effects, and map/shroud updates
are deferred until the window closes. A Drawable or W3D mesh retained by
the client is therefore usable there; its live Object is not.

Source locations below identify the audited pre-fix code at `af9006aa`.
`client/` means `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/`;
`engine/` means `GeneralsMD/Code/GameEngine/`.

## Confirmed hazards and fixes

| Location before the fix | Read and reason it races | Resolution |
| --- | --- | --- |
| `client/W3DParticleSys.cpp:138`, `replaceLegacyVesselFoam` | Both particle draw loops resolve an attached Drawable's live Object and ID. The tick can detach/delete that Object. | **Fixed.** Capture the owner ID in the existing `SystemView` at the particle sync point; both loops pass that ID to the foam policy. |
| `client/Water/W3DWater.cpp:506`, `isModernWaterMirrorForOwner` | `findObjectByID`, destruction status, Drawable, Object position and `TerrainLogic::isUnderwater` are read during the mirror pass. | **Fixed.** Look up copied owner eligibility, Drawable and water level. The current W3D model bounds still determine mirror admission. |
| `client/Water/W3DWater.cpp:557`, `updateModernWaterHulls` | Walks the logic object list and reads kinds, destruction, position, ID, physics velocity and water level during `Render`. Concurrent movement, deletion and water-height changes invalidate those reads. | **Fixed.** Copy object values and motion inputs at sync. Hull selection, slicing and foam history still use the displayed W3D pose, camera, and motion phase during drawing. |
| `client/Water/W3DWater.cpp:3324`, obstacle gathering | Walks Objects and reads position, geometry, orientation, status and ID for shore displacement. | **Fixed.** Copy obstacle geometry and IDs at sync; join them to the frame's selected hull IDs during drawing. |
| `client/Water/W3DWater.cpp:1518,2953,3255,3819,5088`, optical polygons, reflection bounds, patch gathering, polygon and river rendering | Traverses live `PolygonTrigger` points, center/radius and river data. `engine/Source/GameLogic/Map/TerrainLogic.cpp:2410` changes polygon points through `setWaterHeight`; bounds are mutable too. These reads are not covered by the existing window counters. | **Fixed.** Copy polygon values at sync and use those copies throughout Linux water rendering. Preserve the polygon's numeric identity for bow admission, without dereferencing it during drawing. |
| `client/Water/W3DWaterTracks.cpp:492`, coastal surf | `render` asks `getWaterHandle` and `isUnderwater` at the animated wave tail while logic can change water heights. | **Fixed.** Prepare Linux wave pose and height samples at sync; GPU submission uses the saved values. The non-Linux path retains the original calculations at draw time. |
| `client/Water/W3DWaterPathScene.cpp:393`, reflected props | Path-tracing export asks `PartitionManager::getPropShroudStatusForPlayer`, which reads/fills logic-owned visibility data. | **Fixed.** Copy each prop's shroud status at sync; export uses the copied statuses and client-owned prop transforms. |
| `client/W3DScene.cpp:510,2240` and `client/Water/W3DWaterPathScene.cpp:119` | Modern reflection filtering calls `testDrawableStatus(DRAWABLE_STATUS_NO_SAVE)` on status bits the tick can write. | **Fixed.** Add Linux `noSave` to upstream's `LookForDraw`, initialize it, and consume it through `isNoSaveForDraw`. |
| `client/W3DTreeBuffer.cpp:2196`, reached by `BaseHeightMap.cpp:2961` sun-tree pass | `drawTrees` reads ScriptEngine breeze and script freeze state. Scripts can change them during the overlapping tick. | **Fixed.** Existing sync-point `stepTrees` captures breeze and pause before its early returns. Both sun and main tree passes use the same copy. |

The water snapshot is called after `stepTrees` and before the particle snapshot
in `W3DDisplay::takeReadsForRender`. It also handles paused frames. Extra pictures
drawn during one tick reuse the same snapshots. Snapshot storage is cleared at
water reset. All new state and changed runtime behavior are Linux-only.

## Investigated reads that are already safe

| Location | Reason |
| --- | --- |
| `client/W3DParticleSys.cpp:1005,1331`, cloud walkers and owner lifetime | `updateCloudBodies`, owner searches, position/geometry/health/shroud tests and `LifetimeUpdate::getDieFrame` already run in `updateGroundFields` at sync. Cloud drawing consumes `cloudDieFrame`, field data and walker copies. |
| `client/W3DParticleSys.cpp:1047,1070,1154`, cloud terrain samples | `BaseHeightMap::logicHeightsOrNull` in `GameEngineDevice/Include/W3DDevice/GameClient/BaseHeightMap.h:333` selects the drawn map inside the overlap window. `getHeightMapHeight` is therefore safe despite its name. |
| `client/Water/W3DWater.cpp:488`, `hasModernVesselFoam` and `m_modernHullObjectIDs` | The query reads renderer-owned submission IDs and render frame. The unsafe inputs to their population were fixed in hull gathering. |
| `GameEngineDevice/Include/W3DDevice/GameClient/SunShadowCasterCull.h:44` | Pure camera/frustum, sphere and scalar math; no Object or partition access. |
| `client/W3DTreeBuffer.cpp:1862`, `drawModelShadowCasters` | Uses client model lists, retained mesh references, W3D transforms/bounds and cached visibility. Logic defers model creation/removal until sync. Toppling-tree partition reads run from `stepTrees`, not caster submission. |
| `client/Shadow/W3DVolumetricShadow.cpp:4361,4370` and shadow setup in `W3DScene.cpp` | Caster collection uses W3D bounds/poses and upstream Drawable look snapshots. Map extents and W3D shroud textures belong to the renderer. No additional live Object reads were found in the fork's sun pass. |
| `client/Water/W3DWater.cpp` and `W3DWaterPathScene.cpp`, render terrain/model data | `WorldHeightMap`, terrain colors/flips, W3D shroud, mesh poses and `clientOnly_getRenderMotionSamplePhase` are client data. They remain in draw so reflections and hulls follow the displayed model pose. |
| `client/Drawable/Draw/W3DModelDraw.cpp:2381`, capture-only carrier fixture | Called through `W3DView::update -> Drawable::draw -> doDrawModule`, before the window, or through deferred transform notifications flushed after the tick. |
| `client/W3DDisplay.cpp:2513`, capture clock; water `update`; grid/load/resource preparation | Run before the overlap window. Frame and pause getters used during rendering already redirect to upstream's `takeFrameForDraw` copies. |
| Fork changes to terrain/flat-terrain/background rendering, `W3DInGameUI::isWorldSpaceUI`, POSIX GPU/shader/sorting code | Operate on client buffers, cameras, settings and render-object identities. They do not traverse logic Objects. |

This is a correctness audit of the fork's additions and the existing routines
those additions newly call. It is not a claim that every upstream render path
is race-free. Runtime counters cover their instrumented accessors; static review
also covers uninstrumented reads such as polygons and ScriptEngine breeze.

## Verification

Local logs, profiles, scenarios, exact command manifests and captures are retained in
`build-linux/render-logic-audit/`. Each run uses private user data and the existing
prepared root `build-linux/water-validation/root`; no installation is changed.

The shared Tournament Island scenario spawns two `CivilianVehiclePTBoat`s,
toxin and nuclear hazard fields, four moving infantry, and scouting helicopters.
Modern water, Volumetric clouds and mapped sun shadows are enabled. It ends at
logic frame 300 and captures frame 210. The baseline image was inspected to
confirm visible vessel, clouds, walkers and shadows. All ten scenario actions
ran, with eleven spawned units and no refused actions.

Before the fix, `-logicthread 1` produced 142 windows and 140 overlapping ticks:

```text
drawableobject 1320 | findobject 220 | objectlist 280 | logicframe 0
thing 57704 | shroud 0 | partition 0 | terrain 115744
player 0 | radarobject 0 | drawablefield 2039 | playerstate 0
```

The baseline frame-300 gameplay CRC was `0xE3B67570`.

The fixed executable completed all four controls successfully:

| Water / logic mode | Windows | Ticks beside render | All window-read counters | Frame-300 CRC |
| --- | ---: | ---: | --- | --- |
| Modern Hybrid / `-logicthread 1` | 132 | 130 | Zero | `0xE3B67570` |
| Modern Path traced / `-logicthread 1` | 170 | 140 | Zero | `0xE3B67570` |
| Modern Hybrid / `-logicthread 2` (jitter) | 166 | 134 | Zero | `0xE3B67570` |
| Classic water / `-logicthread 1` | 193 | 186 | Zero | `0xE3B67570` |

Every run also had zero logic-thread client census counts, ten completed
scenario actions, no refused actions, and eleven spawned units. The Modern
runs' `ZH_WATER_HULL_TRACE=1` output identifies both PT boat owners, 413 and
414, with valid hull slices. `ZH_CLOUD_REPORT=1` confirms toxin and nuclear
palettes, nuclear embers, and four interacting infantry. `-shadowmapreport`
confirms active mapped geometry/smoke shadow passes. The Path traced run
entered the scene exporter; its initial loading-scene log reports zero
triangles and is not a final scene-size measurement.

Baseline and fixed Hybrid screenshots were inspected: vessel, shore, toxin,
nuclear cloud, infantry and sun shadows remain present. These are ordinary
overlapping runs, so wall-clock animation phases differ; this is not a
pixel-identical image claim. GPU validation found no validation or shader
compilation errors. Each run retained the same single render-target feedback
draw rejection seen in the baseline. No extra pictures were drawn during a
single tick in these runs, so that branch is supported by source review,
not claimed as runtime coverage.

`summary.json` records the parsed counters and coverage evidence; each case's
`manifest.json`, `engine.log`, `run.log`, private `Options.ini`, and screenshot
record the exact inputs and outputs. All four fixed cases used the same
executable SHA-256. The shared scenario is retained in
`baseline/overlay/Scenarios/hazard_check.txt`. The baseline totaled 177,307
instrumented window reads; the fixed cases had none.

The added regression in `GeneralsMD/Code/Tests/test_water_grid.cpp:5478`
uses a real `PolygonTrigger`: capture, change its XY coordinates and height,
delete it, and consume the optical polygons inside the overlap window after
each operation. The old snapshot remains valid until the next capture. The
focused run passed all 57 checks.

Build command: `cmake --build build-linux -j 20` (existing Ninja Release tree).
Test command: `cd build-linux && ctest -j 10 --timeout 600`.
The final full build passed. CTest completed 106 tests in 50.83 seconds:
82 passed, 21 skipped, and only the three predeclared failures:
`ffref_capture_selfcheck`, `lobbyroom_selfcheck`, and `mapselect_selfcheck`.
Water-grid/snapshot, water-path CPU/GPU, hazard-cloud policy and sun-shadow
sorting tests passed. Skips are reported separately, not counted as passes.
`fingerprint-manifest.sh` and `--check` passed (4,200 entries); the added
repository documentation is outside the fingerprint's `GeneralsMD/Code` scope,
so the manifest's contents did not change.
No dependencies were installed. Windows and macOS were source-reviewed for
unchanged behavior, but were not built or run in this Linux workspace.
