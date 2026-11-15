// Renderer features describe implemented providers, independently of user options.
// Keep this header free of D3D/SDL headers: GameEngine consumes it directly.
#pragma once
#ifndef RENDERERCAPABILITIES_H
#define RENDERERCAPABILITIES_H

enum class RendererFeature
{
    BattleMarks,
    PerPixelLights,
    SoftParticles,
    ProceduralFire,
    ModelEffects,
    HemisphericAmbient,
    TeamGlow,
    PostProcessing,
    InternalHDR,
    AmbientOcclusion,
    AtmosphereHaze,
    HeatBlastDistortion,
    GpuParticles,
    Weather,
    ClassicWaterSplashes,
    BuildingCollapse,
    DistanceFieldReachOverlay,
    WaterFoam, // Classic-style foam provider; independent of Modern water
    Count
};

#if !defined(_WIN32)
struct IDirect3DDevice9;
// Null/headless devices and missing providers return false. Never reads user options.
bool PosixRenderer_Supports_Feature(IDirect3DDevice9 *device, RendererFeature feature);
// Program-lifetime provider registration; the query remains false when its module is not linked.
// Callbacks only inspect implemented hardware/backend support, never options or active scene state.
bool PosixRenderer_Register_Feature_Provider(RendererFeature feature, bool (*provider)(IDirect3DDevice9 *));
#endif
#endif
