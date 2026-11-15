/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
// Modified 2025-2026 by Olcay Seygan for Zero Hour Reforged; see the git history.
// Modified 2026 by İlyas Akın for the macOS/Linux port; see NOTICE.md and the git history.

// FILE: W3DSnow.h /////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DSnow.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#if defined(__linux__)
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "Platform/RenderPost.h"
#include "Platform/RenderLighting.h"
#include "Platform/RendererCapabilities.h"
#endif
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "GameClient/View.h"
#include "GameLogic/ScriptEngine.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/camera.h"
#include "WW3D2/assetmgr.h"
#include "Platform/RenderTypes.h"


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif

#define D3DFVF_POINTVERTEX (D3DFVF_XYZ)
#define SNOW_BUFFER_SIZE 4096	//size of vertex buffer holding particles.
#define SNOW_BATCH_SIZE	2048	//we render at most this many particles per drawprimitive call.  This number * 6 must be less than 65536 to fit into index buffer.

struct POINTVERTEX
{
    Vector3 v;	//center of particle.
};

#if defined(__linux__)
namespace {
bool nativeWeatherAvailable(IDirect3DDevice9 *device)
{
    // Match the producer's stable admission, without options or a current post scene.
    return RenderPost::EnvironmentAvailable(device);
}
const bool weatherProviderRegistered = PosixRenderer_Register_Feature_Provider(
    RendererFeature::Weather, nativeWeatherAvailable);
}
#endif

W3DSnowManager::W3DSnowManager(void)
{
	m_indexBuffer=NULL;
	m_snowTexture=NULL;
	m_VertexBufferD3D=NULL;
	m_weatherDrops=NULL;
	for (Int i=0;i<WEATHER_LAYER_COUNT;++i) m_weatherTextures[i]=NULL;
	m_weatherContactTextures[0]=m_weatherContactTextures[1]=NULL;
	m_weatherContacts=NULL; m_weatherContactTracks=NULL;
    m_weatherDustCells=NULL; m_weatherDustEpoch=0; m_weatherDustRevision=~0U;
    m_weatherDustWater=NULL; m_weatherRainSeed=1; m_weatherRainMatchSeed=0;
	clearWeatherMotion();
	m_weatherTime=0.0;
	m_weatherMap=NULL;
	m_weatherMapData=NULL;
	m_weatherMapWidth=0;
#if defined(__linux__)
    m_weatherMapHeight=0;
#endif
	m_weatherSandCells=0;
	m_weatherCells=0;
}

W3DSnowManager::~W3DSnowManager()
{
	ReleaseResources();
	delete [] m_weatherDrops;
	delete [] m_weatherContacts;
	delete [] m_weatherContactTracks;
	delete [] m_weatherDustCells;
}

// Direct3D 11 has no point sprites at all: the fixed-function primitive that expands one vertex
// into a screen-aligned quad was taken out of the API, and the replacement is a geometry shader the
// backend does not have.  The snow already carries a second path that builds those quads on the CPU
// and draws them through DX8Wrapper, so a presenting D3D11 run takes that one and gets its snow.
// SDL3 GPU on Linux is the same: Vulkan sizes a point only from a PointSize the vertex program
// writes, the device's fixed-function programs write none, so a point list there is a one-pixel
// dot and a pipeline the validation layers reject (VUID-VkGraphicsPipelineCreateInfo-topology-08773).
static Bool snowWantsPointSprites(void)
{
	return TheWeatherSetting->m_usePointSprites
		&& DX8Wrapper::Get_Current_Caps()->Support_PointSprites();
}

static Bool snowUsesPointSprites(void)
{
#if defined(__linux__)
	return FALSE;
#else
	return !Direct3D11_Present_Is_Enabled() && snowWantsPointSprites();
#endif
}

void W3DSnowManager::init( void )
{
	SnowManager::init();
	ReAcquireResources();
}

/** Releases all W3D/D3D assets before a reset.. */
void W3DSnowManager::ReleaseResources(void)
{
	REF_PTR_RELEASE(m_snowTexture);
	for (Int i=0;i<WEATHER_LAYER_COUNT;++i) REF_PTR_RELEASE(m_weatherTextures[i]);
	REF_PTR_RELEASE(m_weatherContactTextures[0]);
	REF_PTR_RELEASE(m_weatherContactTextures[1]);

	if (m_VertexBufferD3D)
		m_VertexBufferD3D->Release();

	m_VertexBufferD3D=NULL;

	REF_PTR_RELEASE(m_indexBuffer);
}

/** (Re)allocates all W3D/D3D assets after a reset.. */
Bool W3DSnowManager::ReAcquireResources(void)
{
	ReleaseResources();

	if (!TheWeatherSetting->m_snowEnabled && m_weatherKind == WEATHER_KIND_NONE)
		return TRUE;	//no need for resources if snow is disabled.

	if (snowUsesPointSprites())
	{
		LPDIRECT3DDEVICE9 m_pDev=DX8Wrapper::_Get_D3D_Device();

		DEBUG_ASSERTCRASH(m_pDev, ("Trying to ReAquireResources on W3DSnowManager without device"));

		if (m_VertexBufferD3D == NULL)
		{	// Create vertex buffer

			if (Render_Failed(m_pDev->CreateVertexBuffer
			(
				SNOW_BUFFER_SIZE*sizeof(POINTVERTEX),
				D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC|D3DUSAGE_POINTS, 
				D3DFVF_POINTVERTEX,
				D3DPOOL_DEFAULT,
				&m_VertexBufferD3D,
				NULL	// pSharedHandle, D3D9's extra parameter, reserved and always null
			)))
				return FALSE;
		}
	}
	else
	{
		m_indexBuffer=NEW_REF(DX8IndexBufferClass,(SNOW_BATCH_SIZE *6));	//allocate 2 triangles per flake, each with 3 indices.

		// Fill up the IB with static vertex indices that will be used for all smudges.
		{
			DX8IndexBufferClass::WriteLockClass lockIdxBuffer(m_indexBuffer);
			UnsignedShort *ib=lockIdxBuffer.Get_Index_Array();
			//quad of 4 triangles:
			//	0-----3
			//  |\   /|
			//  |  X  |
			//	|/   \|
			//  1-----2
			Int vbCount=0;
			for (Int i=0; i<SNOW_BATCH_SIZE; i++)
			{
				//Top
				ib[0]=vbCount+3;
				ib[1]=vbCount;
				ib[2]=vbCount+2;
				//Bottom
				ib[3]=vbCount+2;
				ib[4]=vbCount;
				ib[5]=vbCount+1;
		
				vbCount += 4;
				ib+=6;
			}
		}
	}

	m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());

	m_dwBase = SNOW_BUFFER_SIZE;
	m_dwDiscard = SNOW_BUFFER_SIZE;
	m_dwFlush = SNOW_BATCH_SIZE;

	return TRUE;
}

void W3DSnowManager::updateIniSettings(void)
{
	//Call base class
	SnowManager::updateIniSettings();

	if (m_snowTexture && strcasecmp(m_snowTexture->Get_Texture_Name(),TheWeatherSetting->m_snowTexture.str()) != 0)
	{	
		REF_PTR_RELEASE(m_snowTexture);
		m_snowTexture = WW3DAssetManager::Get_Instance()->Get_Texture(TheWeatherSetting->m_snowTexture.str());
	}
}

void W3DSnowManager::reset( void )
{
	SnowManager::reset();
	clearWeatherMotion();
#if defined(__linux__)
    // Client/map reset is authoritative even when an allocator reuses every map
    // and sample address. Ordinary snow and the Windows weather lifecycle stay unchanged.
    W3DShaderManager::invalidateNativePostEnvironment();
    m_weatherOn = FALSE; m_weatherKind = WEATHER_KIND_NONE; m_weatherTime = 0.0;
    m_weatherMap = NULL; m_weatherMapData = NULL; m_weatherMapWidth = m_weatherMapHeight = 0;
    m_weatherSandCells = m_weatherCells = 0;
    for (Int i=0;i<WEATHER_LAYER_COUNT;++i) REF_PTR_RELEASE(m_weatherTextures[i]);
#endif
}

void W3DSnowManager::update(void)
{

	m_time += WW3D::Get_Frame_Time() / 1000.0f;

	//find current time offset, adjusting for overflow
	m_time=fmod(m_time,m_fullTimePeriod);

#if defined(__linux__)
    m_weatherOn = TheGlobalData->m_weatherEffects && !TheGlobalData->m_classicGraphics && RenderPost::EnvironmentAvailable();
#else
	m_weatherOn = TheGlobalData->m_weatherEffects && !TheGlobalData->m_classicGraphics && Direct3D11_Present_Is_Enabled();
#endif
	if (m_weatherOn && m_isVisible)
		updateWeather();
	else if (m_weatherOn)
	{
		setWeatherPresentation(0.0f,0.0f,WW3D::Get_Frame_Time()/1000.0f,0.0f);
		if (m_weatherTime!=0.0) clearWeatherMotion();
	}
	else
	{
		m_weatherKind = WEATHER_KIND_NONE;
		setWeatherPresentation(0.0f,0.0f,WW3D::Get_Frame_Time()/1000.0f,0.0f);
		if (m_weatherTime!=0.0) clearWeatherMotion();
	}
}

/* The Weather option (Options > Effects, Direct3D 11, off under Classic).  A client's picture and no
	more: quads, a wind on the trees and on rising smoke, the still haze.  Nothing here reaches logic,
	sight or the shroud, so two players may see different weather in one match.

	Each layer keeps a fixed number of quads in a square tile that follows the camera; a quad wraps
	round the tile, which leaves it where it is in the world while the camera pans.  So the cost is the
	count, whatever the zoom.  A quad covers no pixel the texture would not, and the program is the one
	EA's snow draws with. */
struct WeatherLayer
{
	const char *texture;
	Int count;			// quads on a tile of WEATHER_TILE_STEP; a smaller tile takes fewer
	Real along;			// world units a second down the wind
	Real down;			// and toward the ground
	Real length;		// a streak's length along its flight, or a card's height
	Real width;
	UnsignedInt colour;	// taken down with the map's light
	Real height;		// the layer's depth over the ground the camera looks at; 0 falls from the camera
	Bool facesCamera;	// a card instead of a streak along the flight
};

// The battlefield reads first: motion is what says weather, so a layer stays sparse and faint.  Sand's
// layers remain over verified dry ground; small low cards cannot wash across the water.
static const WeatherLayer WEATHER_LAYERS[WEATHER_KIND_COUNT][3] =
{
    { { NULL }, { NULL }, { NULL } },
    { { "EXRainDrop.tga", 6000, 45.0f, 1100.0f, 44.0f, 3.0f, 0xb8d8e4f0, 0.0f, FALSE }, { NULL }, { NULL } },
    { { "EXRainDrop.tga", 2200, 145.0f, 19.0f, 40.0f, 1.8f, 0x64ecd4a4, 95.0f, FALSE },
      { "EXRainDrop.tga", 1000, 95.0f, 10.0f, 30.0f, 1.2f, 0x48d8c8a4, 200.0f, FALSE },
      { "EXCloud01.tga", 60, 52.0f, 3.0f, 20.0f, 52.0f, 0x20d0ac7c, 36.0f, TRUE } },
    { { "EXSnowFlake1.tga", 2800, 11.0f, 48.0f, 2.0f, 2.0f, 0xc8e8eef4, 0.0f, TRUE },
      { "EXSnowFlake1.tga", 2000, 19.0f, 34.0f, 3.2f, 3.2f, 0xb0e8eef4, 0.0f, TRUE },
      { "EXSnowFlake1.tga", 1000, 6.0f, 63.0f, 1.3f, 1.3f, 0x98d8e2ee, 0.0f, TRUE } }
};

// Stable render-local seeds: weather must not consume the gameplay/random particle stream.
static Real weatherSeed(UnsignedInt n)
{
    n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16;
    return Real(n & 0x00ffffffU) / 16777216.0f;
}

void W3DSnowManager::clearWeatherMotion(void)
{
    for (Int i=0;i<WEATHER_LAYER_COUNT;++i) {
        m_weatherMotion[i].reset();
    }
    m_weatherTime=0.0;
    m_weatherRainEnvelope=0.0f; m_weatherParticleDensity=0.0f;
    ++m_weatherDustEpoch; m_weatherDustFrame=~0U; m_weatherDustQueries=0;
    m_weatherContactNext=m_weatherContactCursor=0;
    m_weatherContactFrame=~0U;
    if (m_weatherContacts) for (Int i=0;i<WEATHER_CONTACT_COUNT;++i) m_weatherContacts[i].born=-10.0;
    if (m_weatherContactTracks) for (Int i=0;i<WEATHER_CONTACT_TRACKS;++i) m_weatherContactTracks[i].valid=FALSE;
}

static const Int WEATHER_MAX_DROPS = 8192;
static const Real WEATHER_TILE_STEP = 2048.0f;
static const Real WEATHER_TILE_MIN = 512.0f;
static const Real WEATHER_TILE_MAX = 4096.0f;
static const Real WEATHER_FALL_PER_TILE = 0.75f;	// how far a falling layer reaches, by the tile
static const Real WEATHER_EYE_CLEARANCE = 0.55f;	// of the eye's height: nothing falls against the lens, where a streak would cross the screen

void W3DSnowManager::updateWeather(void)
{
	const Real dt=WWMath::Clamp(WW3D::Get_Frame_Time()/1000.0f,0.0f,0.1f);
	// Never multiply a changing wind by elapsed time: integrate displacement once per update.
	m_weatherTime += dt;

	WorldHeightMap *map = TheTerrainRenderObject != NULL ? TheTerrainRenderObject->getMap() : NULL;
    const UnsignedInt matchSeed=GetGameLogicRandomSeed();
#if defined(__linux__)
    const Bool mapChanged = map != NULL && (map != m_weatherMap || map->getDataPtr() != m_weatherMapData
        || map->getXExtent() != m_weatherMapWidth || map->getYExtent() != m_weatherMapHeight
        || matchSeed != m_weatherRainMatchSeed);
    if (mapChanged)
#else
	if (map != NULL && (map != m_weatherMap || map->getDataPtr() != m_weatherMapData
        || map->getXExtent() != m_weatherMapWidth || matchSeed != m_weatherRainMatchSeed))
#endif
	{
		clearWeatherMotion();
		m_weatherMap = map;
		m_weatherMapData = map->getDataPtr();
		m_weatherMapWidth = map->getXExtent();
#if defined(__linux__)
        m_weatherMapHeight = map->getYExtent();
#endif
        map->countSandCells(&m_weatherSandCells, &m_weatherCells);
        m_weatherRainSeed=WeatherParticleMath::hash(UnsignedInt(map->getXExtent())^
            (UnsignedInt(map->getYExtent())<<16)^UnsignedInt(m_weatherSandCells));
        const Int samples=map->getXExtent()*map->getYExtent();
        for (Int i=0;i<8 && samples>0;++i)
            m_weatherRainSeed=WeatherParticleMath::hash(m_weatherRainSeed^map->getDataPtr()[(samples-1)*i/7]);
        m_weatherRainMatchSeed=matchSeed;
        m_weatherRainSeed=WeatherParticleMath::rainSeedForMatch(m_weatherRainSeed,matchSeed);
		DEBUG_LOG(("WEATHER map is %d sand of %d cells\n", m_weatherSandCells, m_weatherCells));
	}

	const WeatherKind kind = map == NULL ? WEATHER_KIND_NONE : weatherKindForMap(TheWeatherSetting->m_snowEnabled,
		TheGlobalData->m_weather == WEATHER_SNOWY, m_weatherSandCells, m_weatherCells);
	if (kind != m_weatherKind)
	{
		m_weatherKind = kind;
		clearWeatherMotion();
		for (Int i=0;i<WEATHER_LAYER_COUNT;++i) REF_PTR_RELEASE(m_weatherTextures[i]);
	}
    const Bool active=m_isVisible && map!=NULL;
    const WeatherParticleMath::Presentation sample=WeatherParticleMath::sample(m_weatherTime);
    const Real gust=kind==WEATHER_KIND_NONE ? 1.0f : sample.gust;
    const Real intensity=kind==WEATHER_KIND_NONE ? 1.0f : sample.intensity;
    if (!active) clearWeatherMotion();
    m_weatherRainEnvelope=kind==WEATHER_KIND_RAIN ?
        WeatherParticleMath::rainSpell(m_weatherTime,m_weatherRainSeed).envelope : 1.0f;
    m_weatherParticleDensity=intensity;
    // A dry interval changes presentation only: the selected rain kind and its clock keep advancing.
    setWeatherPresentation(active ? gust : 0.0f,active ? intensity*m_weatherRainEnvelope : 0.0f,dt,
        active ? m_weatherRainEnvelope : 0.0f);
    const Coord2D &wind=weatherBreezeInfo().m_directionVec;
    for (Int i=0;i<WEATHER_LAYER_COUNT;++i) {
        const WeatherLayer &layer=WEATHER_LAYERS[kind][i];
        if (!layer.texture) continue;
        m_weatherMotion[i].advance(dt,wind.x,wind.y,layer.along,layer.down,getWeatherGust());
    }
}

void W3DSnowManager::renderWeather(RenderInfoClass &rinfo)
{
    if (m_weatherKind==WEATHER_KIND_RAIN && m_weatherRainEnvelope<=0.0f) return;
    if (m_weatherKind==WEATHER_KIND_SAND) {
        const UnsignedInt revision=TheWaterRenderObj ? TheWaterRenderObj->weatherSurfaceRevision() : 0;
        if (m_weatherDustWater!=TheWaterRenderObj || m_weatherDustRevision!=revision) {
            m_weatherDustWater=TheWaterRenderObj; m_weatherDustRevision=revision; ++m_weatherDustEpoch;
        }
        if (m_weatherDustFrame!=WW3D::Get_Frame_Count()) {
            m_weatherDustFrame=WW3D::Get_Frame_Count(); m_weatherDustQueries=0;
        }
    }
	if (m_weatherDrops == NULL)
	{
		m_weatherDrops = NEW WeatherDrop[WEATHER_MAX_DROPS];
		for (Int i = 0; i < WEATHER_MAX_DROPS; ++i)
		{
			m_weatherDrops[i].x = weatherSeed(i*4U+1U);
			m_weatherDrops[i].y = weatherSeed(i*4U+2U);
			m_weatherDrops[i].phase = weatherSeed(i*4U+3U);
			m_weatherDrops[i].size = 0.65f+0.7f*weatherSeed(i*4U+4U);
		}
	}
	if (!m_indexBuffer)
		ReAcquireResources();
	if (!m_indexBuffer) return;

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, identity);
	Matrix3D view;
	rinfo.Camera.Get_View_Matrix(&view);
	DX8Wrapper::Set_Transform(D3DTS_VIEW, view);
	VertexMaterialClass *vmat = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);
	DX8Wrapper::Set_Index_Buffer(m_indexBuffer, 0);

	const GlobalData::TerrainLighting &sun = TheGlobalData->m_terrainLighting[TheGlobalData->m_timeOfDay][0];
	const Real light = WWMath::Clamp(0.299f * (sun.ambient.red + sun.diffuse.red) + 0.587f * (sun.ambient.green + sun.diffuse.green)
		+ 0.114f * (sun.ambient.blue + sun.diffuse.blue), 0.3f, 1.0f);

	// No sun's shadow on what falls: with it every pixel of every quad pays the blocker search, for a
	// shade nobody could see on a streak.
#if defined(__linux__)
    RenderLighting::SetEmissive(true);
#else
	Direct3D11_Set_Emissive(true);
#endif
	for (Int i=0;i<WEATHER_LAYER_COUNT;++i)
		if (WEATHER_LAYERS[m_weatherKind][i].texture) drawWeatherLayer(rinfo,i,light);
	if (m_weatherKind==WEATHER_KIND_RAIN) drawWeatherContacts(rinfo,light);
#if defined(__linux__)
    RenderLighting::SetEmissive(false);
#else
	Direct3D11_Set_Emissive(false);
#endif
}

void W3DSnowManager::drawWeatherLayer(RenderInfoClass &rinfo, Int layerIndex, Real light)
{
	const WeatherLayer &layer = WEATHER_LAYERS[m_weatherKind][layerIndex];
	if (m_weatherTextures[layerIndex] == NULL)
		m_weatherTextures[layerIndex] = WW3DAssetManager::Get_Instance()->Get_Texture(layer.texture);
	DX8Wrapper::Set_Texture(0, m_weatherTextures[layerIndex]);
	// Dust cards use ordinary depth testing, so units/terrain can occlude the drifting veil.
	ShaderClass shader(ShaderClass::_PresetAlphaShader);

	// A near vertical streak past the point under the camera turns its back to it at a steep tilt.
	shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	DX8Wrapper::Set_Shader(shader);

	CameraClass &camera = rinfo.Camera;
	const Vector3 eye = camera.Get_Position();

	// The tile: a power of two times the step, so it holds through a pan and only a change of zoom
	// moves the drops, and centred ahead of the camera as far as it may go and still hold the near ground.
	AABoxClass visible;
	TheTerrainRenderObject->getMaximumVisibleBox(camera.Get_Frustum(), &visible, TRUE);
	const Real wanted = 2.0f * max(visible.Extent.X, visible.Extent.Y);
	Real tile = WEATHER_TILE_MIN;
	while (tile < wanted && tile < WEATHER_TILE_MAX)
		tile *= 2.0f;
	Vector2 ahead(visible.Center.X - eye.X, visible.Center.Y - eye.Y);
	const Real farthest = tile * 0.45f;
	if (ahead.Length() > farthest)
		ahead *= farthest / ahead.Length();
	const Real centreX = eye.X + ahead.X;
	const Real centreY = eye.Y + ahead.Y;
	const Real originX = centreX - tile * 0.5f;
	const Real originY = centreY - tile * 0.5f;

	const Real ground = TheTerrainRenderObject->getHeightMapHeight(centreX, centreY, NULL);
	const Real depth = layer.height > 0.0f ? layer.height : tile * WEATHER_FALL_PER_TILE;
	const Real top = layer.height > 0.0f ? ground + layer.height : eye.Z - (eye.Z - ground) * WEATHER_EYE_CLEARANCE;

	const Coord2D &wind = weatherBreezeInfo().m_directionVec;
	const Real shiftX = (Real)fmod(m_weatherMotion[layerIndex].x, (double)tile);
	const Real shiftY = (Real)fmod(m_weatherMotion[layerIndex].y, (double)tile);
	const Real fallen = (Real)fmod(m_weatherMotion[layerIndex].fall / depth, 1.0);

    if (m_weatherKind==WEATHER_KIND_RAIN && layerIndex==0 && m_weatherContactFrame!=WW3D::Get_Frame_Count()) {
        m_weatherContactFrame=WW3D::Get_Frame_Count();
        if (!m_weatherContacts) {
            m_weatherContacts=NEW WeatherContact[WEATHER_CONTACT_COUNT];
            m_weatherContactTracks=NEW WeatherContactTrack[WEATHER_CONTACT_TRACKS];
            for (Int i=0;i<WEATHER_CONTACT_COUNT;++i) m_weatherContacts[i].born=-10.0;
            for (Int i=0;i<WEATHER_CONTACT_TRACKS;++i) m_weatherContactTracks[i].valid=FALSE;
        }
        // Bound height/water queries separately from the falling-particle budget. These are the
        // same trajectories as up to the first 256 rendered drops. At low frame rates a smaller
        // active set keeps every revisit below .24 seconds and half a fall cycle.
        WorldHeightMap *map=TheTerrainRenderObject->getMap();
        W3DShroud *shroud=TheTerrainRenderObject->getShroud();
        const Real contactDt=WWMath::Clamp(WW3D::Get_Frame_Time()/1000.0f,0.0f,0.1f);
        const Int tracks=WeatherParticleMath::contactTrackCount(contactDt,depth,layer.down);
        for (Int attempt=0;attempt<48;++attempt) {
            const Int index=m_weatherContactCursor%tracks;
            m_weatherContactCursor=(index+1)%tracks;
            const WeatherDrop &drop=m_weatherDrops[index];
            WeatherContactTrack &track=m_weatherContactTracks[index];
            const Real phase=drop.phase+fallen;
            const Vector3 now(weatherWrap(drop.x*tile+shiftX,originX,tile),
                weatherWrap(drop.y*tile+shiftY,originY,tile),top-(phase-(Int)phase)*depth);
            const Vector3 previous=track.position;
            const double elapsed=m_weatherTime-track.sampled;
            const Bool hadSample=track.valid && track.tile==tile;
            track.position=now; track.sampled=m_weatherTime; track.tile=tile; track.valid=TRUE;
            if (!hadSample || now.Z>=previous.Z || elapsed<=0.0 || elapsed>=0.3
                || fabs(now.X-previous.X)>=80.0f || fabs(now.Y-previous.Y)>=80.0f
                || weatherSeed(index+913U)>getWeatherIntensity()) continue;
            const Real border=map->getBorderSize()*MAP_XY_FACTOR;
            if (now.X< -border || now.Y< -border ||
                now.X>=(map->getXExtent()-1)*MAP_XY_FACTOR-border ||
                now.Y>=(map->getYExtent()-1)*MAP_XY_FACTOR-border) continue;
            if (shroud && shroud->getShroudLevel(REAL_TO_INT_FLOOR(now.X/shroud->getCellWidth()),
                REAL_TO_INT_FLOOR(now.Y/shroud->getCellHeight()))<240) continue;
            Coord3D terrainNormal;
            Real surface=TheTerrainRenderObject->getHeightMapHeight(now.X,now.Y,&terrainNormal);
            Vector3 normal(terrainNormal.x,terrainNormal.y,terrainNormal.z);
            Real waterHeight=surface;
            Vector3 waterNormal;
            const Bool hasWater=TheWaterRenderObj && TheWaterRenderObj->getWeatherSurfaceHeight(now.X,now.Y,&waterHeight,&waterNormal);
            const WeatherParticleMath::Surface hit=WeatherParticleMath::surface(surface,hasWater,waterHeight);
            if (hit.water) normal=waterNormal;
            if (!WeatherParticleMath::crossesSurface(previous.Z,now.Z,hit.height,elapsed,
                now.X-previous.X,now.Y-previous.Y)) continue;
            WeatherContact &contact=m_weatherContacts[m_weatherContactNext];
            m_weatherContactNext=(m_weatherContactNext+1)%WEATHER_CONTACT_COUNT;
            contact.position.Set(now.X,now.Y,hit.height+0.45f);
            contact.normal=normal; contact.born=m_weatherTime; contact.water=hit.water;
        }
    }

	// A streak lies along its flight and takes its width across the view, so it never turns edge on.
	const Matrix3D &eyeFrame = camera.Get_Transform();
	Vector3 axis;
	Vector3 side;
	if (layer.facesCamera)
	{
		axis = eyeFrame.Get_Y_Vector() * -1.0f;
		side = eyeFrame.Get_X_Vector();
	}
	else
	{
		axis.Set(wind.x * layer.along*getWeatherGust(), wind.y * layer.along*getWeatherGust(), -layer.down);
		axis.Normalize();
		Vector3::Cross_Product(eyeFrame.Get_Z_Vector(), axis, &side);
		side.Normalize();
	}
	axis *= layer.length * 0.5f;
	side *= layer.width * 0.5f;
	const Vector3 corners[4] = { -axis - side, axis - side, axis + side, -axis + side };
	static const Real CORNER_U[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	static const Real CORNER_V[4] = { 0.0f, 1.0f, 1.0f, 0.0f };

	const UnsignedInt colour = (layer.colour & 0xff000000)
		| ((UnsignedInt)(((layer.colour >> 16) & 0xff) * light) << 16)
		| ((UnsignedInt)(((layer.colour >> 8) & 0xff) * light) << 8)
		| (UnsignedInt)((layer.colour & 0xff) * light);

	const Int count = min((Int)(layer.count * tile * tile / (WEATHER_TILE_STEP * WEATHER_TILE_STEP)), layer.count);
	Int done = 0;
	while (done < count)
	{
		const Int batch = min(count - done, (Int)SNOW_BATCH_SIZE);
		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8, dynamic_fvf_type, batch * 4);
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2 *verts = lock.Get_Formatted_Vertex_Array();
			for (Int i = 0; i < batch; ++i)
			{
				const Int dropIndex=done+i;
				const WeatherDrop &drop = m_weatherDrops[(dropIndex+layerIndex*2017)%WEATHER_MAX_DROPS];
				const Real drift=m_weatherKind==WEATHER_KIND_SNOW ?
                    Real(sin(m_weatherTime*(0.6f+drop.size*0.35f)+drop.phase*6.283185f))*9.0f*drop.size : 0.0f;
                const Real x = weatherWrap(drop.x * tile + shiftX + drift, originX, tile);
                const Real y = weatherWrap(drop.y * tile + shiftY + drift*0.6f, originY, tile);
				const Real phase = drop.phase + fallen;
				Real z = top - (phase - (Int)phase) * depth;
                if (m_weatherKind==WEATHER_KIND_SAND && layerIndex==2)
                    z=TheTerrainRenderObject->getHeightMapHeight(x,y,NULL)+10.0f+(1.0f-(phase-(Int)phase))*20.0f;
				Real density=WWMath::Clamp((m_weatherParticleDensity-weatherSeed(dropIndex+913U))*8.0f,0.0f,1.0f)
                    *m_weatherRainEnvelope;
                const Real scale=drop.size*(0.94f+0.06f*getWeatherGust());
                if (m_weatherKind==WEATHER_KIND_SAND) {
                    Real minX=x,maxX=x,minY=y,maxY=y;
                    for (Int corner=0;corner<4;++corner) {
                        const Real px=x+corners[corner].X*scale,py=y+corners[corner].Y*scale;
                        minX=min(minX,px); maxX=max(maxX,px); minY=min(minY,py); maxY=max(maxY,py);
                    }
                    density*=weatherDustFootprintFade(minX,minY,maxX,maxY);
                }
                const UnsignedInt dropColour=(colour&0x00ffffffU) | (UnsignedInt((colour>>24)*density)<<24);
                for (Int corner = 0; corner < 4; ++corner)
                {
                    verts->x = x + corners[corner].X*scale;
                    verts->y = y + corners[corner].Y*scale;
                    verts->z = z + corners[corner].Z*scale;
					verts->nx = 0;	//keep AGP write-combining active
					verts->ny = 0;
					verts->nz = 0;
					verts->diffuse = dropColour;
					verts->u1 = CORNER_U[corner];
					verts->v1 = CORNER_V[corner];
					verts->u2 = 0;
					verts->v2 = 0;
					verts++;
				}
			}
		}
		DX8Wrapper::Set_Vertex_Buffer(vb_access);
		DX8Wrapper::Draw_Triangles(0, batch * 2, 0, batch * 4);
		done += batch;
	}
}

// World-anchored 96-unit cells cover the full dust-card footprint, with a 32-unit soft
// dry-side shore margin. Unknown/stale cells deny dust; only 32 cells may inspect water per frame.
Bool W3DSnowManager::weatherDustCellDry(Int x,Int y,Real *arrival)
{
    *arrival=0.0f;
    if (!m_weatherDustCells) {
        m_weatherDustCells=NEW WeatherDustCell[WEATHER_DUST_CACHE_DIM*WEATHER_DUST_CACHE_DIM];
        for (Int i=0;i<WEATHER_DUST_CACHE_DIM*WEATHER_DUST_CACHE_DIM;++i) m_weatherDustCells[i].epoch=0;
    }
    const UnsignedInt slot=(UnsignedInt(y)&63U)*WEATHER_DUST_CACHE_DIM+(UnsignedInt(x)&63U);
    WeatherDustCell &cell=m_weatherDustCells[slot];
    const Bool wasDry=cell.epoch==m_weatherDustEpoch && cell.x==x && cell.y==y && cell.dry;
    const double previousAdmission=wasDry ? cell.admitted : m_weatherTime;
    if (cell.epoch==m_weatherDustEpoch && cell.x==x && cell.y==y && m_weatherTime-cell.sampled<10.0) {
        *arrival=WeatherParticleMath::dustArrival(m_weatherTime-cell.admitted,cell.dry);
        return cell.dry;
    }
    if (m_weatherDustQueries>=32) return FALSE;
    ++m_weatherDustQueries;
    cell.x=x; cell.y=y; cell.epoch=m_weatherDustEpoch; cell.sampled=m_weatherTime;
    cell.admitted=previousAdmission; cell.dry=FALSE;
    WorldHeightMap *map=TheTerrainRenderObject->getMap();
    if (!map) return FALSE;
    const Real minX=x*96.0f,minY=y*96.0f,maxX=minX+96.0f,maxY=minY+96.0f;
    const Int border=map->getBorderSize();
    const Int lowX=REAL_TO_INT_FLOOR(minX/MAP_XY_FACTOR)+border;
    const Int lowY=REAL_TO_INT_FLOOR(minY/MAP_XY_FACTOR)+border;
    const Int highX=REAL_TO_INT_CEIL(maxX/MAP_XY_FACTOR)+border;
    const Int highY=REAL_TO_INT_CEIL(maxY/MAP_XY_FACTOR)+border;
    if (lowX<0 || lowY<0 || highX>=map->getXExtent() || highY>=map->getYExtent()) return FALSE;
    Real terrainMinimum=1.0e30f;
    // Every terrain vertex in/enclosing the cell gives a conservative lower bound, not just
    // a center sample that could miss a small pond or the bend of a river.
    for (Int iy=lowY;iy<=highY;++iy) for (Int ix=lowX;ix<=highX;++ix)
        terrainMinimum=min(terrainMinimum,map->getHeight(ix,iy)*MAP_HEIGHT_SCALE);
    cell.dry=!TheWaterRenderObj || !TheWaterRenderObj->hasWeatherWaterInBox(minX,minY,maxX,maxY,terrainMinimum);
    cell.admitted=previousAdmission;
    *arrival=WeatherParticleMath::dustArrival(m_weatherTime-cell.admitted,cell.dry);
    return cell.dry;
}

Real W3DSnowManager::weatherDustFootprintFade(Real minX,Real minY,Real maxX,Real maxY)
{
    const Real margin=32.0f;
    const Int lowX=REAL_TO_INT_FLOOR((minX-margin)/96.0f),lowY=REAL_TO_INT_FLOOR((minY-margin)/96.0f);
    const Int highX=REAL_TO_INT_FLOOR((maxX+margin)/96.0f),highY=REAL_TO_INT_FLOOR((maxY+margin)/96.0f);
    Real fade=1.0f;
    for (Int y=lowY;y<=highY;++y) for (Int x=lowX;x<=highX;++x) {
        Real arrival;
        if (weatherDustCellDry(x,y,&arrival)) { fade=min(fade,arrival); continue; }
        fade=min(fade,WeatherParticleMath::footprintShoreFade(minX,minY,maxX,maxY,
            x*96.0,y*96.0,(x+1)*96.0,(y+1)*96.0,margin));
        if (fade<=0.0f) return 0.0f;
    }
    return fade;
}

void W3DSnowManager::drawWeatherContacts(RenderInfoClass &rinfo, Real light)
{
    if (!m_weatherContacts) return;
    // Both are licensed EA assets already used by the water splash effect. A low-alpha
    // expanding surface ring reads as a raindrop; the land card is a brief upward spray.
    const char *textures[2]={ "EXWater04.tga", "EXShockWav.tga" };
    const Matrix3D &eye=rinfo.Camera.Get_Transform();
    static const Real u[4]={0.0f,0.0f,1.0f,1.0f}, v[4]={0.0f,1.0f,1.0f,0.0f};
    for (Int pass=0;pass<2;++pass) {
        if (!m_weatherContactTextures[pass])
            m_weatherContactTextures[pass]=WW3DAssetManager::Get_Instance()->Get_Texture(textures[pass]);
        DX8Wrapper::Set_Texture(0,m_weatherContactTextures[pass]);
        ShaderClass shader(ShaderClass::_PresetAlphaShader);
        if (pass) shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
        shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
        DX8Wrapper::Set_Shader(shader);
        DynamicVBAccessClass vb(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,WEATHER_CONTACT_COUNT*4);
        Int count=0;
        {
            DynamicVBAccessClass::WriteLockClass lock(&vb);
            VertexFormatXYZNDUV2 *verts=lock.Get_Formatted_Vertex_Array();
            for (Int i=0;i<WEATHER_CONTACT_COUNT;++i) {
                const WeatherContact &contact=m_weatherContacts[i];
                const Real age=Real(m_weatherTime-contact.born), lifetime=pass ? 0.75f : 0.24f;
                if (contact.water!=(pass!=0) || age<0.0f || age>=lifetime) continue;
                W3DShroud *shroud=TheTerrainRenderObject->getShroud();
                if (shroud && shroud->getShroudLevel(REAL_TO_INT_FLOOR(contact.position.X/shroud->getCellWidth()),
                    REAL_TO_INT_FLOOR(contact.position.Y/shroud->getCellHeight()))<240) continue;
                const Real size=pass ? 2.4f+age*13.0f : 1.8f+age*8.0f;
                AABoxClass bounds(contact.position,Vector3(size,size,size));
                if (CollisionMath::Overlap_Test(rinfo.Camera.Get_Frustum(),bounds)==CollisionMath::OUTSIDE) continue;
                Vector3 side,axis;
                if (pass) {
                    // Rings lie in the exact authored triangle plane, including sloping rivers.
                    Vector3::Cross_Product(Vector3(0.0f,1.0f,0.0f),contact.normal,&side);
                    side.Normalize(); Vector3::Cross_Product(contact.normal,side,&axis);
                    side*=size; axis*=size;
                } else {
                    side=eye.Get_X_Vector()*size;
                    axis.Set(0.0f,0.0f,size*(1.0f-age/lifetime));
                }
                const Vector3 corners[4]={-side-axis,-side+axis,side+axis,side-axis};
                const Real fade=(1.0f-age/lifetime)*(pass ? 0.65f : 0.70f)*m_weatherRainEnvelope;
                const UnsignedInt colour=(UnsignedInt(255.0f*fade)<<24) |
                    (UnsignedInt(208.0f*light)<<16) | (UnsignedInt(224.0f*light)<<8) | UnsignedInt(238.0f*light);
                const Vector3 center=contact.position+(pass ? Vector3(0,0,0) : Vector3(0,0,size*0.5f));
                for (Int corner=0;corner<4;++corner) {
                    const Vector3 point=center+corners[corner];
                    verts->x=point.X; verts->y=point.Y; verts->z=point.Z;
                    verts->nx=verts->ny=verts->nz=0.0f; verts->diffuse=colour;
                    verts->u1=u[corner]; verts->v1=v[corner]; verts->u2=verts->v2=0.0f;
                    ++verts;
                }
                ++count;
            }
        }
        if (count) { DX8Wrapper::Set_Vertex_Buffer(vb); DX8Wrapper::Draw_Triangles(0,count*2,0,count*4); }
    }
}

#define MAXIMUM_CAMERA_DISTANCE 100000	//maximum distance of camera position from world origin.
#define ISPOW2(x)  (x && (x & (x-1)) == 0)	//is a number a power of 2?
#define MODPOW2(x,y) ((x) & (y-1))		//mod '%' operator for powers of 2.

// Helper function to stuff a FLOAT into a DWORD argument
inline UnsignedInt FtoDW( float f ) { return *((UnsignedInt*)&f); }

/*Recursively subdivide the large snow box enclosing the camera until we reach some predefined leaf size.  This
method is used so that very few off-screen particles end up getting rendered.  Culling them individually would
be too expensive since we're dealing with 1000's for this effect.*/
void W3DSnowManager::renderSubBox(RenderInfoClass &rinfo, Int originX, Int originY, Int cubeDimX, Int cubeDimY )
{
	//check if this box is too large and needs subdivision
	Int boxDimX=cubeDimX - originX;
	Int boxDimY=cubeDimY - originY;
	Int halfX=REAL_TO_INT_CEIL(boxDimX*0.5f);
	Int halfY=REAL_TO_INT_CEIL(boxDimY*0.5f);

	CameraClass &camera=rinfo.Camera;
	MinMaxAABoxClass mmbox;

	if (boxDimX > m_leafDim)
	{	//subdivide the box
		if (boxDimY > m_leafDim)
		{	//subdivide in both directions
			//Upper left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, (originY + halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX, originY + halfY, originX + halfX, cubeDimY);
			//Upper right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, (originY + halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY + halfY,cubeDimX, cubeDimY);
			//Lower left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX,originY,originX + halfX, originY + halfY);
			//Lower right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY, cubeDimX, originY + halfY);
			return;
		}
		else
		{	//only subdivide in x direction.
			//Left
			mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set((originX + halfX)*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX, originY, originX + halfX, cubeDimY);
			//Right
			mmbox.MinCorner.Set((originX + halfX)*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
			mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
			if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
				renderSubBox(rinfo, originX + halfX, originY, cubeDimX, cubeDimY);
			return;
		}
	}
	else
	if (boxDimY > m_leafDim)
	{	//only subdivide in y direction
		//Top
		mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, (originY+halfY)*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
		mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, cubeDimY*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
		if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
			renderSubBox(rinfo, originX, originY+halfY,cubeDimX, cubeDimY);
		//Bottom
		mmbox.MinCorner.Set(originX*m_emitterSpacing-m_cullOverscan, originY*m_emitterSpacing-m_cullOverscan, m_snowCeiling-m_boxDimensions);
		mmbox.MaxCorner.Set(cubeDimX*m_emitterSpacing+m_cullOverscan, (originY + halfY)*m_emitterSpacing+m_cullOverscan, m_snowCeiling);
		if (CollisionMath::Overlap_Test(camera.Get_Frustum(),mmbox) != CollisionMath::OUTSIDE)
			renderSubBox(rinfo, originX, originY, cubeDimX, originY + halfY);
		return;
	}

	//Box too small to subdivide so render it.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-originY)*(cubeDimX-originX);

	if (!totalPart)
		return;	//nothing to render.

	Int y=originY;	//loop counter.
	Int cubeOriginXRemainder = originX;	//loop counter - adjusted when not all particles fit into render buffer.
	Vector3 snowCenter;

	m_totalRendered += totalPart;

	while (totalPart)
	{
		Int batchSize=totalPart;

		if (batchSize > m_dwFlush)
			batchSize = m_dwFlush;

		if((m_dwBase + batchSize) > m_dwDiscard)
			m_dwBase = 0;

		POINTVERTEX* verts;

		if(m_VertexBufferD3D->Lock(m_dwBase * sizeof(POINTVERTEX), batchSize * sizeof(POINTVERTEX),
			(void **) &verts, m_dwBase ? D3DLOCK_NOOVERWRITE : D3DLOCK_DISCARD) != D3D_OK )
			return;	//couldn't lock buffer.

		Int numberInBatch=0;

		for (;y<cubeDimY; y++)
		{
			for (Int x=cubeOriginXRemainder; x<cubeDimX; x++)
			{
				if (numberInBatch >= batchSize)
				{	cubeOriginXRemainder = x;
					goto flush_particles;
				}

				//Get initial height from noise table.  We add a large value to make sure it's positive.  Then
				//modulate by table dimensions to find a value.
				Int noiseOffset=MODPOW2(x+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_X)+MODPOW2(y+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_Y)*SNOW_NOISE_X;
				if (noiseOffset > (SNOW_NOISE_X * SNOW_NOISE_Y))
					noiseOffset = 0;	//this should never happen but check to prevent buffer over/under flow.

				//find current height
				Real h0=m_snowCeiling-fmod(m_heightTraveled+m_startingHeights[noiseOffset],m_boxDimensions);

				//find world-space position of snow flake
				snowCenter.Set(x*m_emitterSpacing,y*m_emitterSpacing,h0);

				//Adjust position so snow flakes don't fall straight down.
				snowCenter.X += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleX + (Real)x); 
				snowCenter.Y += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleY + (Real)y); 

				*(Vector3 *)verts=snowCenter;
				verts++;

				numberInBatch++;
			}
			//getting here means we did not overflow the render buffer, so reset x origin to normal.
			cubeOriginXRemainder = originX;	//reset to normal amount
		}

flush_particles:
		m_VertexBufferD3D->Unlock();
		//Render any particles that may be queued up.
		if (numberInBatch)
		{
			Debug_Statistics::Record_DX8_Polys_And_Vertices(numberInBatch*2,numberInBatch*4,ShaderClass::_PresetOpaqueShader);
			DX8Wrapper::_Get_D3D_Device()->DrawPrimitive( D3DPT_POINTLIST, m_dwBase, numberInBatch);
			totalPart -= numberInBatch;
			m_dwBase += numberInBatch;
		}
	}
}

void W3DSnowManager::render(RenderInfoClass &rinfo)
{
	// Weather is composed once in the main view; reflected cameras cannot advance contacts.
	if (m_weatherKind!=WEATHER_KIND_NONE && TheWaterRenderObj && TheWaterRenderObj->isRenderingReflection()) return;
	if (m_weatherKind != WEATHER_KIND_NONE && m_isVisible)
	{
		renderWeather(rinfo);
		return;
	}

	if (!TheWeatherSetting->m_snowEnabled || !m_isVisible)
		return;

	Int usePointSprites = snowUsesPointSprites();

	//make sure the noise table is powers of 2 in dimensions.
	WWASSERT(ISPOW2(SNOW_NOISE_X) && ISPOW2(SNOW_NOISE_Y));

	//CameraClass &camera=rinfo.Camera;

	const Coord3D &cPos=TheTacticalView->get3DCameraPosition();
	Vector3 camPos(cPos.x,cPos.y,cPos.z);

	//Number of emitters from cube center to edge of visible extent.
	Int mumEmittersInHalf=(Int)floor(m_boxDimensions / m_emitterSpacing * 0.5f);

	//Find origin of visible cube surrounding camera.
	Int cubeCenterX=(Int)floor(camPos.X/m_emitterSpacing);
	Int cubeCenterY=(Int)floor(camPos.Y/m_emitterSpacing);

	//Find extents of visible cube surrounding camera.
	Int cubeOriginX=cubeCenterX - mumEmittersInHalf;	//top/left extents.
	Int cubeOriginY=cubeCenterY - mumEmittersInHalf;
	Int cubeDimX=cubeCenterX + mumEmittersInHalf;		//bottom/right extents.
	Int cubeDimY=cubeCenterY + mumEmittersInHalf;

 	const FrustumClass & frustum = rinfo.Camera.Get_Frustum();
	AABoxClass bbox;

	//Get a bounding box around our visible universe.  Bounded by terrain and the sky
	//so much tighter fitting volume than what's actually visible.  This will cull
	//particles falling under the ground.

 	TheTerrainRenderObject->getMaximumVisibleBox(frustum, &bbox, TRUE);

	//Particles move outside the visible box as a result of local sine movement
	//so adjust bounding box to include them.
	bbox.Extent.X += m_amplitude+m_quadSize;
	bbox.Extent.Y += m_amplitude+m_quadSize;

	//Clip our visible snow rendering box
	if ((cubeOriginX * m_emitterSpacing ) < (bbox.Center.X - bbox.Extent.X))
		cubeOriginX = (Int)floor ((bbox.Center.X - bbox.Extent.X)/m_emitterSpacing);

	if ((cubeOriginY * m_emitterSpacing ) < (bbox.Center.Y - bbox.Extent.Y))
		cubeOriginY = (Int)floor ((bbox.Center.Y - bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimX * m_emitterSpacing ) > (bbox.Center.X + bbox.Extent.X))
		cubeDimX = (Int)floor ((bbox.Center.X + bbox.Extent.X)/m_emitterSpacing);

	if ((cubeDimY * m_emitterSpacing ) > (bbox.Center.Y + bbox.Extent.Y))
		cubeDimY = (Int)floor ((bbox.Center.Y + bbox.Extent.Y)/m_emitterSpacing);

	if ((cubeDimY - cubeOriginY) < 0 || (cubeDimX-cubeOriginX) < 0)
		return;	//entire snow box is culled by either x or y screen boundary.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	if (totalPart <= 0)
		return;	//nothing to render.

	//Height at the top of the cube with camera at center.
	m_snowCeiling = camPos.Z + m_boxDimensions/2.0f;

	//Offset to allow cube extents to move with camera.
	Real cameraOffset = fmod (camPos.Z,m_boxDimensions);
	m_heightTraveled=m_time*m_velocity+cameraOffset;	//height that snow flake traveled this frame.

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_WORLD,identity);	

	DX8Wrapper::Set_Shader(ShaderClass::_PresetAlphaShader);

	VertexMaterialClass *vmat=VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
	DX8Wrapper::Set_Material(vmat);
	REF_PTR_RELEASE(vmat);

	//make sure we have all the resources we need
	if (usePointSprites && !m_VertexBufferD3D)
		ReAcquireResources();

	if (!usePointSprites && !m_indexBuffer)
		ReAcquireResources();

	DX8Wrapper::Set_Texture(0,m_snowTexture);

	if (!usePointSprites)
	{	
		renderAsQuads(rinfo,cubeOriginX,cubeOriginY,cubeDimX,cubeDimY);
		return;
	}

	Vector3 snowCenter;

	DX8Wrapper::Apply_Render_State_Changes();

    // Set the render states for using point sprites
	DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSPRITEENABLE, TRUE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALEENABLE,  TRUE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE,     FtoDW(m_pointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE_MIN, FtoDW(m_minPointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSIZE_MAX, FtoDW(m_maxPointSize) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_A,  FtoDW(0.00f) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_B,  FtoDW(0.00f) );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALE_C,  FtoDW(1.00f) );

	DX8Wrapper::_Get_D3D_Device()->SetStreamSource( 0, m_VertexBufferD3D, 0, sizeof(POINTVERTEX) );
    DX8Wrapper::_Get_D3D_Device()->SetFVF( D3DFVF_POINTVERTEX );
	m_dwBase = SNOW_BUFFER_SIZE;	//start with a new vertex buffer each frame.

	m_leafDim = 45;	//cull boxes that are 20x20 emitters in size. Making them much smaller will result in too many draw calls.
	m_totalRendered = 0;	//keep track of how many particles were rendered.

	//Particle centers can deviate from center by by amplitude of sine offset.  They also have radius m_quadSize.
	//Enlarge culling bounds to compensate.
	m_cullOverscan = m_amplitude+m_quadSize;
	renderSubBox(rinfo,cubeOriginX,cubeOriginY,cubeDimX,cubeDimY);

	// Reset render states
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSPRITEENABLE, FALSE );
    DX8Wrapper::Set_DX8_Render_State( D3DRS_POINTSCALEENABLE,  FALSE );

}

/**For hardware that doesn't support point sprites*/
void W3DSnowManager::renderAsQuads(RenderInfoClass &rinfo, Int cubeOriginX, Int cubeOriginY, Int cubeDimX, Int cubeDimY)
{

	Matrix4x4 proj;
	Matrix3D view;
	Vector3 snowCenter;
	Vector3 snowCenterVS;

	CameraClass &camera=rinfo.Camera;

	camera.Get_View_Matrix(&view);
	camera.Get_Projection_Matrix(&proj);

	Vector3 vertex_offsets[4] = {
		Vector3(-0.5f, 0.5f, 0.0f),
		Vector3(-0.5f, -0.5f, 0.0f),
		Vector3(0.5f, -0.5f, 0.0f),
		Vector3(0.5f, 0.5f, 0.0f)
	};

	Vector2 quad_uvs[4] = {
		Vector2(0.0f, 0.0f),
		Vector2(0.0f, 1.0f),
		Vector2(1.0f, 1.0f),
		Vector2(1.0f, 0.0f)
	};


	// A snowy map tunes its flakes for point sprites: SnowPointSize with a SnowMaxPointSize of 10 or
	// 16 pixels, and SnowQuadSize mostly left at the 0.5 default.  Taken as a world size, that 0.5
	// grows without limit on a flake next to the camera, which is how D3D11 drew snowflakes the size
	// of a building.  When the map asked for sprites the quad gets the size D3D9 gave the sprite:
	// SnowPointSize * viewport height / eye distance pixels, clamped, turned back into view units.
	Bool sizeLikePointSprites = snowWantsPointSprites();
	Int deviceWidth, deviceHeight, deviceBits;
	Bool deviceWindowed;
	WW3D::Get_Device_Resolution(deviceWidth, deviceHeight, deviceBits, deviceWindowed);
	Real viewportHeight = (Real)deviceHeight;

	Matrix4x4 identity(true);
	DX8Wrapper::Set_Transform(D3DTS_VIEW,identity);	

	DX8Wrapper::Set_Index_Buffer(m_indexBuffer,0);

	Int y=cubeOriginY;	//loop counter.
	Int cubeOriginXRemainder = cubeOriginX;	//loop counter - adjusted when not all particles fit into render buffer.

	//Find total number of particles that need rendering.
	Int totalPart=(cubeDimY-cubeOriginY)*(cubeDimX-cubeOriginX);

	m_totalRendered += totalPart;

	while (totalPart)
	{
		Int batchSize=totalPart;

		if (batchSize > SNOW_BATCH_SIZE)
			batchSize = SNOW_BATCH_SIZE;

		Int numberInBatch=0;

		DynamicVBAccessClass vb_access(BUFFER_TYPE_DYNAMIC_DX8,dynamic_fvf_type,batchSize*4);	//allocate 4 verts per flake
		{
			DynamicVBAccessClass::WriteLockClass lock(&vb_access);
			VertexFormatXYZNDUV2* verts=lock.Get_Formatted_Vertex_Array();

			for (;y<cubeDimY; y++)
			{
				for (Int x=cubeOriginXRemainder; x<cubeDimX; x++)
				{
					if (numberInBatch >= batchSize)
					{	cubeOriginXRemainder = x;
						goto flush_particles;
					}

					//Get initial height from noise table.  We add a large value to make sure it's positive.  Then
					//modulate by table dimensions to find a value.
					Int noiseOffset=MODPOW2(x+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_X)+MODPOW2(y+MAXIMUM_CAMERA_DISTANCE,SNOW_NOISE_Y)*SNOW_NOISE_X;
					if (noiseOffset > (SNOW_NOISE_X * SNOW_NOISE_Y))
						noiseOffset = 0;	//this should never happen but check to prevent buffer over/under flow.

					//find current height
					Real h0=m_snowCeiling-fmod(m_heightTraveled+m_startingHeights[noiseOffset],m_boxDimensions);

					//find world-space position of snow flake
					snowCenter.Set(x*m_emitterSpacing,y*m_emitterSpacing,h0);

					//Get view-space position
					Matrix3D::Transform_Vector(view,snowCenter,&snowCenterVS);

					//Adjust position so snow flakes don't fall straight down.
					snowCenterVS.X += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleX + (Real)x);
					snowCenterVS.Y += m_amplitude * WWMath::Fast_Sin( h0 * m_frequencyScaleY + (Real)y);

					Real flakeSize = m_quadSize;
					if (sizeLikePointSprites)
					{
						Real flakePixels = WWMath::Clamp(m_pointSize * viewportHeight / snowCenterVS.Length(), m_minPointSize, m_maxPointSize);
						flakeSize = flakePixels * 2.0f * -snowCenterVS.Z / (proj[1][1] * viewportHeight);
					}

					for (Int i=0; i<4; i++)
					{
						*(Vector3 *)verts=snowCenterVS + vertex_offsets[i] * flakeSize;
						verts->nx=0;	//keep AGP write-combining active
						verts->ny=0;
						verts->nz=0;
						verts->diffuse=0xffffffff;	//set to opaque
						verts->u1=quad_uvs[i].X;
						verts->v1=quad_uvs[i].Y;
						verts->u2=0;	//keep AGP write-combining active
						verts->v2=0;
						verts++;
					}

					numberInBatch++;
				}
				//getting here means we did not overflow the render buffer, so reset x origin to normal.
				cubeOriginXRemainder = cubeOriginX;	//reset to normal amount
			}
flush_particles:
			numberInBatch;	//need something at goto destination - stupid c compiler.
		}

		//Render any particles that may be queued up.
		if (numberInBatch)
		{
			DX8Wrapper::Set_Vertex_Buffer(vb_access);
			DX8Wrapper::Draw_Triangles(	0,numberInBatch*2, 0, numberInBatch*4);	
			totalPart -= numberInBatch;
		}
	}
}
