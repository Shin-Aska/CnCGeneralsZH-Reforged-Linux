#pragma once

#include <cstddef>

namespace WaterShaderLayout {

struct alignas(16) Float4
{
    float x,y,z,w;
    float &operator[](std::size_t index) { return (&x)[index]; }
    const float &operator[](std::size_t index) const { return (&x)[index]; }
};

struct alignas(16) CommonConstants
{
    Float4 Combiner[3];
    Float4 WorldFromClip[4];
    Float4 CameraPosition;
    Float4 SunDirection;
    Float4 SunColour;
    Float4 Viewport;
    Float4 ReflectionArea;
    Float4 WaterParameters;
    Float4 TerrainParameters;
    Float4 SceneParameters;
    Float4 OpticalParameters;
    Float4 HullParameters;
    Float4 HullData[80];
    Float4 FftParameters;
    Float4 FoamHistoryArea;
    Float4 FoamHistoryParameters;
    Float4 ContactHullData[80];
    Float4 FoamFillLights[4];
    Float4 CoastalArea;
    Float4 CoastalParams;
    Float4 ShadowFromClip[4];
    Float4 ShadowParameters;
    Float4 ShadowViewport;
    Float4 ShadowSoftness;
    Float4 VolumeParameters;
};

struct alignas(16) PixelConstants
{
    CommonConstants Common;
    Float4 VisualHullMeta[8];
};

struct alignas(16) HullPacketBlock0 { Float4 registers[256]; };
struct alignas(16) HullPacketBlock1 { Float4 registers[256]; };
struct alignas(16) HullPacketBlock2 { Float4 registers[192]; };
struct alignas(16) HullPacketConstants { Float4 registers[704]; };

struct alignas(16) ShoreConstants
{
    Float4 Combiner[3];
    Float4 WorldFromClip[4];
    Float4 Viewport;
    Float4 Terrain;
    Float4 Scene;
    Float4 Surface;
};

static_assert(sizeof(Float4)==16,"water float4 ABI");
static_assert(sizeof(CommonConstants)==194*16,"water common b0/b1 ABI");
static_assert(sizeof(PixelConstants)==202*16,"water pixel extension ABI");
static_assert(sizeof(HullPacketBlock0)==256*16,"water hull b1 ABI");
static_assert(sizeof(HullPacketBlock1)==256*16,"water hull b2 ABI");
static_assert(sizeof(HullPacketBlock2)==192*16,"water hull b3 ABI");
static_assert(sizeof(HullPacketConstants)==704*16,"complete water hull packets");
static_assert(sizeof(ShoreConstants)==11*16,"shore b0 ABI");
static_assert(offsetof(CommonConstants,WorldFromClip)==3*16,"inverse clip offset");
static_assert(offsetof(CommonConstants,CameraPosition)==7*16,"camera offset");
static_assert(offsetof(CommonConstants,SunDirection)==8*16,"sun offset");
static_assert(offsetof(CommonConstants,Viewport)==10*16,"viewport offset");
static_assert(offsetof(CommonConstants,ReflectionArea)==11*16,"reflection offset");
static_assert(offsetof(CommonConstants,WaterParameters)==12*16,"surface fields offset");
static_assert(offsetof(CommonConstants,TerrainParameters)==13*16,"terrain offset");
static_assert(offsetof(CommonConstants,SceneParameters)==14*16,"scene offset");
static_assert(offsetof(CommonConstants,OpticalParameters)==15*16,"optical offset");
static_assert(offsetof(CommonConstants,HullParameters)==16*16,"hull header offset");
static_assert(offsetof(CommonConstants,HullData)==17*16,"mean hull offset");
static_assert(offsetof(CommonConstants,FftParameters)==97*16,"FFT offset");
static_assert(offsetof(CommonConstants,FoamHistoryArea)==98*16,"history offset");
static_assert(offsetof(CommonConstants,FoamHistoryParameters)==99*16,"history parameters offset");
static_assert(offsetof(CommonConstants,ContactHullData)==100*16,"contact hull offset");
static_assert(offsetof(CommonConstants,FoamFillLights)==180*16,"fill lights offset");
static_assert(offsetof(CommonConstants,CoastalArea)==184*16,"coastal offset");
static_assert(offsetof(CommonConstants,CoastalParams)==185*16,"coastal parameters offset");
static_assert(offsetof(CommonConstants,ShadowFromClip)==186*16,"shadow matrix offset");
static_assert(offsetof(CommonConstants,ShadowParameters)==190*16,"shadow parameters offset");
static_assert(offsetof(CommonConstants,ShadowViewport)==191*16,"shadow viewport offset");
static_assert(offsetof(CommonConstants,ShadowSoftness)==192*16,"shadow softness offset");
static_assert(offsetof(CommonConstants,VolumeParameters)==193*16,"smoke offset");
static_assert(offsetof(PixelConstants,VisualHullMeta)==194*16,"mesh metadata offset");
static_assert(offsetof(ShoreConstants,WorldFromClip)==3*16,"shore inverse clip offset");
static_assert(offsetof(ShoreConstants,Viewport)==7*16,"shore viewport offset");
static_assert(offsetof(ShoreConstants,Terrain)==8*16,"shore terrain offset");
static_assert(offsetof(ShoreConstants,Scene)==9*16,"shore scene offset");
static_assert(offsetof(ShoreConstants,Surface)==10*16,"shore surface offset");

}
