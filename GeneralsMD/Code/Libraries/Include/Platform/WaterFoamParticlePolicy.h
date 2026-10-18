#pragma once
#include <cmath>

// Only the shipped surface-wake effects are replaced. Matching an arbitrary wave texture or
// every particle on a boat would also remove spray, weapons, smoke and mod-authored effects.
namespace WaterFoamParticlePolicy {
inline bool SameResource(const char *a,const char *b)
{
    if(!a || !b) return false;
    while(*a && *b) {
        const unsigned char ac=static_cast<unsigned char>(*a++),bc=static_cast<unsigned char>(*b++);
        const unsigned char lowerA=ac>='A' && ac<='Z' ? ac+('a'-'A') : ac;
        const unsigned char lowerB=bc>='A' && bc<='Z' ? bc+('a'-'A') : bc;
        if(lowerA!=lowerB) return false;
    }
    return *a==*b;
}
inline bool KnownSurfaceFoam(const char *name,const char *texture)
{
    return (SameResource(name,"AmphibWave") && SameResource(texture,"EXWave01.tga")) ||
        (SameResource(name,"AmphibWaveRest") && SameResource(texture,"EXShockWav.tga"));
    // AirCarrierWaterRipples and BattleShipWaterRipples are the authored splash-ring
    // layer. Keep them alongside modern hull foam, including on stationary ships.
}
// Explicit opt-in isolation for attribution captures only. Normal rendering retains
// the splash rings. A nonempty value alone must never enable diagnostic suppression.
inline bool RippleDiagnosticEnabled(const char *value)
{
    return SameResource(value,"ripples");
}
inline bool KnownDiagnosticRipple(const char *name,const char *texture)
{
    return SameResource(texture,"EXShockWav.tga") &&
        (SameResource(name,"AirCarrierWaterRipples") || SameResource(name,"BattleShipWaterRipples"));
}
inline bool IsolateDiagnosticRipple(const char *name,const char *texture,bool enabled,
    bool ordinaryGroundParticle,bool attachedOwner,bool currentModernHull,
    bool mirrorPass,bool sameModernMirrorSurface)
{
    return enabled && ordinaryGroundParticle && attachedOwner && KnownDiagnosticRipple(name,texture) &&
        (mirrorPass?sameModernMirrorSurface:currentModernHull);
}
inline bool Replace(const char *name,const char *texture,bool ordinaryGroundParticle,
    bool attachedOwner,bool currentModernHull,bool mirrorPass)
{
    return ordinaryGroundParticle && attachedOwner && currentModernHull && !mirrorPass &&
        KnownSurfaceFoam(name,texture);
}
// Surface foam belongs to the reflecting interface itself. Drawing it into that
// interface's mirror duplicates it, even when the main view uses legacy particles.
inline bool SameMirrorSurface(bool activeModernMirror,bool visibleWetOwner,float ownerLevel,float mirrorLevel)
{
    return activeModernMirror && visibleWetOwner && std::isfinite(ownerLevel) &&
        std::isfinite(mirrorLevel) && std::abs(ownerLevel-mirrorLevel)<=0.1f;
}
inline bool ExcludeSelfReflection(const char *name,const char *texture,bool ordinaryGroundParticle,
    bool attachedOwner,bool sameModernMirrorSurface)
{
    return ordinaryGroundParticle && attachedOwner && sameModernMirrorSurface && KnownSurfaceFoam(name,texture);
}
}
