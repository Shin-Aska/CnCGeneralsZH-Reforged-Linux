# Upstream submission plan for 4.1.0

Use `references/4.1.0` as the starting point for each worktree. It pins upstream's
[`fa98637f3`](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/commit/fa98637f3dd9fd33b57615a787993ea03fe17f79)
release snapshot. Each row gets one branch named `feat/4-1-0/<slug>` and one worktree named
`4-1-0-<slug>`. The branch prefix is shared across features and fixes. Commit subjects use the type
and scope appropriate to the change.

Start with DX11 effects already in 4.1.0, then take campaign fixes and the reskin startup check. Follow
with underwater clarity, map-aware wave heading, Clean FFT water and toxic-water materials. There
are 26 planned slots, including the reskin check, and one optional slot for the severe-explosion
camera kick. The numbers are plan identifiers. Reproduce the reskin false report on upstream before
submitting a fix.

Underwater clarity is independent of the wave-heading work. Slot 23 changes shallow-water
transmission. Slot 24 supplies the field that Clean FFT reads in slot 25. Slot 26 follows slot 25
so the material controls can be verified against Clean FFT's wave displacement and shading.

## Worktrees and proposed commits

| Slot | Slug | Proposed commit subject | Scope and prerequisites |
|---|---|---|---|
| 01 | `battle-marks` | `feat(gameclient): enable battle marks on sdl gpu` | Enable existing craters, stains and treads through renderer capability admission. Cover the option, fading, underwater exclusion and Classic Graphics. |
| 02 | `native-post-processing` | `feat(w3ddevice): port hdr post-processing to sdl gpu` | Group DX11 Glow, Internal HDR and Reforged local contrast with the main-view color target and final composite. Cover HUD exclusion, resize, sample-count admission and copy-through fallback. |
| 03 | `ambient-occlusion` | `feat(w3ddevice): enable ambient occlusion on sdl gpu` | After 02, add the main-view depth capture with the opaque AO pass. Exercise option-off behavior, alpha preservation and excluded views. |
| 04 | `soft-particles-firelook` | `feat(w3ddevice): port soft particles and firelook to sdl gpu` | After 03, share the sorted-billboard metadata and capture depth before transparent draws. Cover soft fades, hard edges and the stock procedural fire look. |
| 05 | `distance-field-range-overlays` | `feat(w3ddevice): port distance-field range overlays to sdl gpu` | After 03, capture terrain-phase depth for the overlay and exercise the existing tiled fallback. |
| 06 | `classic-water-foam` | `feat(w3ddevice): port classic-water foam to sdl gpu` | After 03, share the opaque-depth and shader path for ponds and rivers. Cover both water styles, HDR and Classic Graphics. |
| 07 | `classic-water-splashes` | `feat(gameclient): enable classic-water splashes on sdl gpu` | Enable the separate splash, foam-ring and spray emitters through their own option. Uses the baseline particle path. |
| 08 | `building-collapse` | `feat(gameclient): enable building collapse feedback on sdl gpu` | Enable the existing collapse model and large-building jolt. Cover shroud and Classic Graphics behavior. |
| 08a | `explosion-camera-kick` | `feat(gameclient): enable severe explosion camera kicks on sdl gpu` | Optional follow-up for the existing severe explosion kick. Exercise shake range and falloff. |
| 09 | `per-pixel-firelight` | `feat(w3ddevice): port dynamic lighting and firelight to sdl gpu` | Group headlights, blast lights and Firelight with native per-pixel lighting and the fallback. Verify that dynamic lights are applied once. |
| 10 | `hemispheric-ambient` | `feat(w3ddevice): port hemispheric ambient lighting to sdl gpu` | After 09, reuse the lighting inputs for the sky and ground ambient option. |
| 11 | `model-status-effects` | `feat(w3ddevice): port model status effects to sdl gpu` | After 02, carry EMP, outage, cloak and reveal through the shared draw state. Full cloak gets the main-view color capture it needs. |
| 12 | `team-glow` | `feat(w3ddevice): port team glow to sdl gpu` | After 02, use the HDR post pass's additive gain for the existing team-color effect. |
| 13 | `atmosphere-warm-light` | `feat(w3ddevice): port atmosphere and warm light to sdl gpu` | After 03, group sky, horizon fade and Warm Light haze through the environment pass. Use copied scene inputs and backend capability checks. |
| 14 | `baseline-weather` | `feat(gameclient): enable existing weather effects on sdl gpu` | After 13, port 4.1.0 rain, sand, snow and visual wind. Check authored precipitation and the existing precedence of weather haze over Warm Light. |
| 15 | `heat-blast-distortion` | `feat(w3ddevice): port heat haze and blast distortion to sdl gpu` | After 03, share the distortion resource lifecycle. Heat uses depth before final post-processing, and blast warp follows the completed post pass. |
| 16 | `gpu-particles` | `feat(w3ddevice): add native gpu particles to sdl gpu` | After 04, group compute, buffer ownership, admission and CPU overflow fallback. Compare equal retained particle counts. |
| 17 | `campaign-speech-wait` | `fix(gameengine): restart waits for matching speech cues` | Reproduce early completion after a pre-query, then reset only the matching wait. Cover repeat requests and the unplayed-cue fallback. |
| 18 | `campaign-frozen-countdowns` | `fix(gameengine): preserve countdown timing while the world is frozen` | Adapt script-pass timing to upstream's 30 Hz clock. Cover all four game speeds, freeze and resume, equality crossing and save/load. |
| 19 | `campaign-briefing-narration` | `fix(gameclient): play original generals briefing voices` | Start configured `GEN_*` narration once across movie and fallback loading paths. Add wait handling if restored audio gets cut off in upstream testing. |
| 20 | `campaign-scripted-camera` | `fix(w3ddevice): smooth scripted camera motion` | Interpolate turn, zoom and pitch on upstream's clock. Cover cuts, cancellation, pause and input handoff. |
| 21 | `campaign-repeated-frame-animation` | `fix(w3ddevice): advance animation on repeated render frames` | Give normal repeated pictures the correct animation clock. Exercise pause, recording, replay and render-session exclusions. |
| 22 | `reskin-startup-validation` | `fix(gameengine): validate inherited reskin behavior by module tag` | First check explicit `RemoveModule` and later parent additions on upstream. If reproduced, validate copy-time behavior identities by type and tag, with a control for a genuine missing module. |
| 23 | `underwater-clarity` | `feat(w3ddevice): add optional underwater clarity to modern water` | Default off. Capture matching views of submerged ruins, a flat offshore floor and a coast. Measure the incremental visual cost on SDL. |
| 24 | `map-aware-wave-heading` | `feat(w3ddevice): steer modern water waves using map depth` | Port the heading field used by Clean FFT. Show that disabling the option removes its GPU cost and measure open-sea and coastal scenes. |
| 25 | `clean-fft-water` | `feat(w3ddevice): add optional clean fft waves to modern water` | After 24, port the shared spectrum, depth-aware waves, glints and foam. Include the USA01 shoreline startup guard and measure the whole candidate on upstream SDL. |
| 26 | `toxic-water-materials` | `feat(w3ddevice): add toxic-water material controls` | After 25, extend the color- and texture-matched material rules with turbidity, surface roughness, ripple damping and wind resistance. Check neutral defaults, the physical wind-wave response and independent bubbles in matched toxic and clear water scenes. |

## Validation and submission

- Use the pinned DX11 renderer for visual comparisons. For cost, compare two matching SDL builds
  with the same prerequisites and isolate the change in the current PR. Exercise supported options,
  Classic Graphics, and capability or resource fallback where applicable. Report actual platform and
  sample-count coverage.
- Reproduce each campaign or startup symptom on upstream with a failing regression before the fix.
  Run Release tests, check deterministic script and replay behavior where affected, and add a
  player-facing changelog entry for visible changes.
- Use the worktrees to validate each slice and check the integration in a combined build. Submit one
  change per PR against current upstream `main`. Rebase dependent branches as their prerequisites
  land so each submitted diff contains its own change.

[Upstream contribution rules](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/blob/fa98637f3dd9fd33b57615a787993ea03fe17f79/CONTRIBUTING.md) ·
[Campaign handoff](campaign-fixes-upstream-handoff.md) ·
[Maintainer's clarity review](https://github.com/olcayseygan/CnCGeneralsZH-Reforged/pull/172#issuecomment-6674027127) ·
[Water material guide](water-preview.md)
