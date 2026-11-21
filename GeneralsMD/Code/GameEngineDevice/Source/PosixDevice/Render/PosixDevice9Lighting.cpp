// Scene light packets and immutable alternatives to legacy vertex lighting.
#include "PosixDevice9.h"
#include "SdlConstants.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
#include "SdlRenderMatrix.h"
#include "engineshader.h"
#include "ffshader.h"
#include <algorithm>
#include <cstring>
#include <cmath>

bool PosixDevice9::Set_Terrain_Heights(const unsigned char *data, unsigned width, unsigned height)
{
    if (!data && !width && !height) {
        TerrainRelight = false;
        Posix_Bind(TerrainHeights, (IDirect3DTexture9 *)nullptr);
        return true;
    }
    if (!data || !width || !height || !Gpu || !Mirrors || width > 16384 || height > 16384) return false;
    // A private managed source follows the renderer's normal upload/version/lifetime rules.
    // All channels carry the byte so the shared Texture2D<float> red load is format independent.
    IDirect3DTexture9 *source = nullptr;
    if (CreateTexture(width,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr) != D3D_OK) return false;
    D3DLOCKED_RECT lock = {};
    if (source->LockRect(0,&lock,nullptr,0) != D3D_OK) { source->Release(); return false; }
    for (unsigned y=0; y<height; ++y) {
        auto *row = reinterpret_cast<uint32_t *>(static_cast<unsigned char *>(lock.pBits)+y*lock.Pitch);
        for (unsigned x=0; x<width; ++x) row[x] = uint32_t(data[size_t(y)*width+x])*0x01010101u;
    }
    source->UnlockRect(0);
    std::string refusal;
    const bool ready = Mirrors->Texture(source,refusal) != nullptr;
    if (ready) Posix_Bind(TerrainHeights,source);
    source->Release();
    return ready;
}
void PosixDevice9::Set_Terrain_Sun(const float *light)
{
    if (!light) return;
    for (unsigned i=0; i<40; ++i) if (!std::isfinite(light[i])) return;
    std::memcpy(TerrainSun,light,sizeof(TerrainSun));
}
bool PosixDevice9::Build_Terrain_Sun_Constants(SdlEnginePixelConstants &constants) const
{
    if (!Gpu || !Viewport.Width || !Viewport.Height || Viewport.MaxZ<=Viewport.MinZ) return false;
    float clip[16], inverse[16], adjust[16] = {};
    multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,clip);
    if (!render_inverse(clip,inverse)) return false;
    // The shared relight/headlight helpers form NDC from absolute fragment coordinates.
    // Keep the shadow receiver's viewport scale intact; absorb its origin/depth range here.
    adjust[0]=adjust[5]=adjust[15]=1;
    adjust[10]=1/(Viewport.MaxZ-Viewport.MinZ);
    adjust[12]=-2.0f*(Viewport.X+.5f)/Viewport.Width;
    adjust[13]=2.0f*(Viewport.Y+.5f)/Viewport.Height;
    adjust[14]=-Viewport.MinZ*adjust[10];
    multiply(adjust,inverse,constants.WorldFromClip);
    constants.ShadowViewport[0]=1.0f/Viewport.Width;
    constants.ShadowViewport[1]=1.0f/Viewport.Height;
    // Contiguous shared ABI: rays, diffuse, sampling parameters, fade and water level.
    static_assert(offsetof(SdlEnginePixelConstants,ShadowFromClip)-offsetof(SdlEnginePixelConstants,NormalLightDirection)==40*sizeof(float),"terrain light packet packing");
    std::memcpy(constants.NormalLightDirection,TerrainSun,sizeof(TerrainSun));
    return true;
}
bool PosixDevice9::Terrain_Relight_Receives(int vertexProgram, int pixelProgram, const SdlTarget &target) const
{
    if (!Gpu || !TerrainRelight || !TerrainHeights || SunActive
        || EmissiveDraw || vertexProgram != ENGINE_SHADER_NONE || (FVF&D3DFVF_XYZRHW)
        || (pixelProgram != ENGINE_SHADER_NONE && !EngineShader_Paints_Ground(static_cast<EngineShaderProgram>(pixelProgram)))
        || !RenderStates[D3DRS_ZENABLE] || RenderStates[D3DRS_FILLMODE]!=D3DFILL_SOLID
        || (RenderStates[D3DRS_COLORWRITEENABLE]&7u)!=7u || !target.Colour) return false;
    // Later multiply passes inherit the first pass's illumination. Alpha terrain blends
    // carry their own sunless base and take the same relight exactly once.
    if (RenderStates[D3DRS_ALPHABLENDENABLE]
        && (RenderStates[D3DRS_SRCBLEND]!=D3DBLEND_SRCALPHA || RenderStates[D3DRS_DESTBLEND]!=D3DBLEND_INVSRCALPHA)) return false;
    SdlEnginePixelConstants view = {};
    return Build_Terrain_Sun_Constants(view);
}

void PosixDevice9::Set_Headlights(const float *lights, unsigned count, const float gain[3])
{
    HeadlightCount = lights && gain ? std::min(count, unsigned(HEADLIGHT_SLOTS)) : 0;
    if (HeadlightCount) {
        std::memcpy(Headlights, lights, HeadlightCount * sizeof(Headlights[0]));
        std::memcpy(HeadlightGain, gain, sizeof(HeadlightGain));
    } else std::memset(HeadlightGain, 0, sizeof(HeadlightGain));
}
void PosixDevice9::Set_Blast_Lights(const float *lights, unsigned count)
{
    BlastLightCount = lights ? std::min(count, unsigned(BLAST_LIGHT_SLOTS)) : 0;
    if (BlastLightCount) std::memcpy(BlastLights, lights, BlastLightCount * sizeof(BlastLights[0]));
}
bool PosixDevice9::Lights_Per_Pixel()
{
    return Gpu && Sun_Owns_Scene_Shadows();
}
static D3DLIGHT9 native_light(const RenderLighting::VertexLight &source)
{
    D3DLIGHT9 result = {};
    result.Type=source.Type==RenderLighting::LightType::Point ? D3DLIGHT_POINT
        : source.Type==RenderLighting::LightType::Spot ? D3DLIGHT_SPOT : D3DLIGHT_DIRECTIONAL;
    std::memcpy(&result.Diffuse,source.Diffuse,sizeof(source.Diffuse));
    std::memcpy(&result.Specular,source.Specular,sizeof(source.Specular));
    std::memcpy(&result.Ambient,source.Ambient,sizeof(source.Ambient));
    std::memcpy(&result.Position,source.Position,sizeof(source.Position));
    std::memcpy(&result.Direction,source.Direction,sizeof(source.Direction));
    result.Range=source.Range;result.Falloff=source.Falloff;
    result.Attenuation0=source.Attenuation[0];result.Attenuation1=source.Attenuation[1];result.Attenuation2=source.Attenuation[2];
    result.Theta=source.Theta;result.Phi=source.Phi;
    return result;
}
void PosixDevice9::Set_Light_Environments(const RenderLighting::VertexEnvironment &full,
    const RenderLighting::VertexEnvironment *staticOnly)
{
    StaticLightingAvailable=staticOnly && full.Count<=4 && staticOnly->Count<=4;
    if (!StaticLightingAvailable) return;
    FullEnvironmentCount=full.Count;
    FullEnvironmentAmbient=full.Ambient;
    StaticAmbient=staticOnly->Ambient;
    std::memset(StaticLightsEnabled,0,sizeof(StaticLightsEnabled));
    for (unsigned i=0;i<full.Count;++i) FullEnvironmentLights[i]=native_light(full.Lights[i]);
    for (unsigned i=0;i<staticOnly->Count;++i) {
        StaticLights[i]=native_light(staticOnly->Lights[i]);
        StaticLightsEnabled[i]=true;
    }
}
bool PosixDevice9::Static_Lighting_Matches() const
{
    if (!StaticLightingAvailable || RenderStates[D3DRS_AMBIENT]!=FullEnvironmentAmbient) return false;
    for (unsigned i=0;i<LIGHT_COUNT;++i) {
        if (LightsEnabled[i] != (i<FullEnvironmentCount)) return false;
        if (i<FullEnvironmentCount && std::memcmp(&Lights[i],&FullEnvironmentLights[i],sizeof(Lights[i]))) return false;
    }
    return true;
}
void PosixDevice9::Build_Light_Constants(SdlEnginePixelConstants &constants, bool pointLights) const
{
    constants.HeadlightParameters[0] = float(HeadlightCount);
    std::memcpy(constants.HeadlightParameters + 1, HeadlightGain, sizeof(HeadlightGain));
    for (unsigned i=0; i<HeadlightCount; ++i) {
        std::memcpy(constants.HeadlightPosition[i], Headlights[i], 4*sizeof(float));
        std::memcpy(constants.HeadlightDirection[i], Headlights[i]+4, 4*sizeof(float));
    }
    // Camera-space smoke already receives CPU Firelight. Multiplicative overlays inherit
    // the base pass's illumination. Neither may gain the point lights a second time.
    const bool multiply = RenderStates[D3DRS_ALPHABLENDENABLE]
        && RenderStates[D3DRS_SRCBLEND] == D3DBLEND_ZERO
        && RenderStates[D3DRS_DESTBLEND] == D3DBLEND_SRCCOLOR;
    const unsigned count = !pointLights || Sun_Camera_Space() || multiply ? 0 : BlastLightCount;
    constants.BlastLightParameters[0] = float(count);
    for (unsigned i=0; i<count; ++i) {
        std::memcpy(constants.BlastLightPosition[i], BlastLights[i], 4*sizeof(float));
        std::memcpy(constants.BlastLightDiffuse[i], BlastLights[i]+4, 4*sizeof(float));
        std::memcpy(constants.BlastLightAmbient[i], BlastLights[i]+8, 4*sizeof(float));
    }
}

namespace RenderLighting {
static PosixDevice9 *native(IDirect3DDevice9 *device) { return static_cast<PosixDevice9 *>(device); }
void SetHeadlights(IDirect3DDevice9 *d,const float *l,unsigned n,const float g[3]) { if(d) native(d)->Set_Headlights(l,n,g); }
void SetBlastLights(IDirect3DDevice9 *d,const float *l,unsigned n) { if(d) native(d)->Set_Blast_Lights(l,n); }
bool LightsPerPixel(IDirect3DDevice9 *d) { return d && native(d)->Lights_Per_Pixel(); }
bool PrepareTerrainLighting(IDirect3DDevice9 *d) { return d && native(d)->Commit_Terrain_Lighting(); }
void UseTerrainCpuLighting(IDirect3DDevice9 *d) { if(d) native(d)->Use_Terrain_Cpu_Lighting(); }
bool SetTerrainHeights(IDirect3DDevice9 *d,const unsigned char *h,unsigned w,unsigned n) { return d && native(d)->Set_Terrain_Heights(h,w,n); }
bool HoldsTerrainHeights(IDirect3DDevice9 *d) { return d && native(d)->Holds_Terrain_Heights(); }
void SetTerrainLight(IDirect3DDevice9 *d,const float *l) { if(d) native(d)->Set_Terrain_Sun(l); }
void SetTerrainRelight(IDirect3DDevice9 *d,bool b) { if(d) native(d)->Set_Terrain_Relight(b); }
void SetHemiAmbient(IDirect3DDevice9 *d,bool b) { if(d) native(d)->Set_Hemi_Ambient(b); }
void SetEmissive(IDirect3DDevice9 *d,bool b) { if(d) native(d)->Set_Emissive(b); }
void SetTeamGlow(IDirect3DDevice9 *d,bool b) { if(d) native(d)->Set_Team_Glow(b); }
void AllowTeamGlow(IDirect3DDevice9 *d,bool b) { if(d) native(d)->Allow_Team_Glow(b); }
void SetEnvironments(IDirect3DDevice9 *d,const VertexEnvironment &full,const VertexEnvironment *alternate)
{ if(d) native(d)->Set_Light_Environments(full,alternate); }
}

bool PosixPerPixelLightingAvailable(IDirect3DDevice9 *device)
{ return device && static_cast<PosixDevice9 *>(device)->Get_Gpu(); }
bool PosixHemisphericAmbientAvailable(IDirect3DDevice9 *device)
{ return device && static_cast<PosixDevice9 *>(device)->Get_Gpu(); }
