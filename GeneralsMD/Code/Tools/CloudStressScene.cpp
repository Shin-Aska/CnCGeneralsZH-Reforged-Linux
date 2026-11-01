// Real terrain, engine cameras and harmless units around an isolated cloud renderer.
#include "PreRTS.h"
#include "CloudStressScene.h"
#include "CloudStressRenderer.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/View.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/CreateModule.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/VictoryConditions.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/shader.h"
#include "WW3D2/ww3d.h"
#include "WWMath/aabox.h"
#include "WWMath/sphere.h"
#include "../GameEngineDevice/Source/PosixDevice/Render/PosixDevice9.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace CloudStressScene {
namespace {
Settings settings;
CloudStress::Model model;
CloudStress::Renderer renderer;
std::vector<std::unique_ptr<HazardCloud::Layer>> references;
std::vector<RenderObjClass *> scenery;
struct Walker { ObjectID id = INVALID_ID; Coord3D endpoints[2], previous; unsigned destination = 0; float health = 0; };
std::array<Walker, 4> walkers;
std::array<HazardCloud::Body, 4> bodies;
bool initialized = false, failed = false;
unsigned startFrame = 0, lastFrame = ~0u, lastReport = 0, renderedFrames = 0;
unsigned peakActive = 0, peakSubmitted = 0;
Uint64 startTicks = 0;
float seconds = 0;
bool cameraReported = false;

void fail(const std::string &reason)
{
    if (!failed) std::fprintf(stderr, "cloud-stress: FAIL: %s\n", reason.c_str());
    failed = true;
}

HazardCloud::Camera sceneCamera(CameraClass &camera)
{
    HazardCloud::Camera result = {};
    Matrix4x4 projection;
    camera.Get_D3D_Projection_Matrix(&projection);
    Matrix3D view;
    camera.Get_View_Matrix(&view);
    const Matrix4x4 d3dProjection = projection.Transpose();
    const Matrix4x4 d3dView = Matrix4x4(view).Transpose();
    std::memcpy(result.view, &d3dView, sizeof(result.view));
    std::memcpy(result.projection, &d3dProjection, sizeof(result.projection));
    int width, height, bits;
    bool windowed;
    WW3D::Get_Render_Target_Resolution(width, height, bits, windowed);
    const ViewportClass &viewport = camera.Get_Viewport();
    result.x = unsigned(viewport.Min.X * width);
    result.y = unsigned(viewport.Min.Y * height);
    result.width = unsigned((viewport.Max.X - viewport.Min.X) * width);
    result.height = unsigned((viewport.Max.Y - viewport.Min.Y) * height);
    camera.Get_Depth_Range(&result.minZ, &result.maxZ);
    return result;
}

unsigned activeCount()
{
    unsigned count = 0;
    for (const auto &source : model.sources()) if (source.active && source.field.density > .00001f) ++count;
    return count;
}

void report()
{
    const auto &stats = renderer.stats();
    const double elapsed = double(SDL_GetTicks() - startTicks) / 1000.0;
    unsigned palettes[4] = {};
    for (const auto &source : model.sources()) ++palettes[unsigned(source.field.palette)];
    std::uint64_t memory = stats.volumeBytes + stats.metadataBytes;
    if (settings.reference) for (const auto &layer : references) memory += layer->estimatedCacheBytes();
    std::fprintf(stderr,
        "cloud-stress: requested=%u retained=%zu active=%u submitted=%u peak-active=%u peak-submitted=%u "
        "logic-frame=%u seconds=%.3f render-frames=%u wall-seconds=%.3f fps=%.2f memory-bytes=%llu volume-bytes=%llu bakes=%u "
        "maximum-bin-sources=%u baked-sources=%u pending-slices=%u max-slices-per-frame=%u publication-age=%.6f "
        "palettes=green:%u,blue:%u,gamma:%u,nuclear:%u mode=%s damage-simulation=absent\n",
        settings.sources.count, model.sources().size(), activeCount(),
        settings.reference ? activeCount() : stats.activeSources, peakActive, peakSubmitted,
        TheGameLogic ? TheGameLogic->getFrame() - startFrame : 0, seconds, renderedFrames,
        elapsed, elapsed > 0 ? renderedFrames / elapsed : 0,
        static_cast<unsigned long long>(memory), static_cast<unsigned long long>(stats.volumeBytes), stats.bakes, stats.maximumBinSources,
        stats.bakedSources, stats.pendingSlices, stats.maxSlicesPerFrame, stats.publicationAgeSeconds,
        palettes[0], palettes[2], palettes[3], palettes[1], settings.reference ? "reference" : "shared");
}

class StressRenderObject final : public RenderObjClass {
public:
    StressRenderObject() { Set_Force_Visible(true); }
    RenderObjClass *Clone() const override { return new StressRenderObject; }
    int Class_ID() const override { return CLASSID_LAST + 48; }
    const char *Get_Name() const override { return "CloudStressVolume"; }
    void Get_Obj_Space_Bounding_Box(AABoxClass &box) const override
    {
        const auto &b = model.bounds();
        box.Center.Set((b.minX+b.maxX)*.5f, (b.minY+b.maxY)*.5f, (b.minZ+b.maxZ)*.5f);
        box.Extent.Set((b.maxX-b.minX)*.5f, (b.maxY-b.minY)*.5f, (b.maxZ-b.minZ)*.5f);
    }
    void Get_Obj_Space_Bounding_Sphere(SphereClass &sphere) const override
    {
        AABoxClass box; Get_Obj_Space_Bounding_Box(box);
        sphere.Center = box.Center; sphere.Radius = box.Extent.Length();
    }
    void Render(RenderInfoClass &info) override
    {
        if (!initialized || failed || !W3DDisplay::m_3DScene) return;
        if (ShaderClass::Is_Backface_Culling_Inverted()
            || W3DDisplay::m_3DScene->getCustomPassMode() != SCENE_PASS_DEFAULT) return;
        if (WW3D::Are_Static_Sort_Lists_Enabled()) {
            WW3D::Add_To_Static_Sort_List(this, 1);
            return;
        }
        const auto camera = sceneCamera(info.Camera);
        if (!cameraReported && activeCount()) {
            unsigned centresVisible = 0;
            float firstX = 0, firstY = 0, firstDepth = 0;
            for (unsigned i = 0; i < model.sources().size(); ++i) {
                const auto &field = model.sources()[i].field;
                const float world[4] = {field.ground.x, field.ground.y,
                    field.ground.z + field.height * .5f, 1};
                float view[4] = {}, clip[4] = {};
                for (unsigned column = 0; column < 4; ++column)
                    for (unsigned row = 0; row < 4; ++row) view[column] += world[row] * camera.view[row*4+column];
                for (unsigned column = 0; column < 4; ++column)
                    for (unsigned row = 0; row < 4; ++row) clip[column] += view[row] * camera.projection[row*4+column];
                const float x = clip[0] / clip[3], y = clip[1] / clip[3], z = clip[2] / clip[3];
                if (clip[3] > 0 && std::abs(x) <= 1 && std::abs(y) <= 1 && z >= 0 && z <= 1) ++centresVisible;
                if (!i) { firstX = camera.x + (x+1)*camera.width*.5f; firstY = camera.y + (1-y)*camera.height*.5f; firstDepth = z; }
            }
            const auto &field = model.sources().front().field;
            std::fprintf(stderr, "cloud-stress: camera source-centres-in-frustum=%u/%zu viewport=%u,%u,%u,%u "
                "source0-world=%.2f,%.2f,%.2f source0-pixel=%.2f,%.2f source0-depth=%.8f density=%.6f formation=%.6f\n",
                centresVisible, model.sources().size(), camera.x, camera.y, camera.width, camera.height,
                field.ground.x, field.ground.y, field.ground.z, firstX, firstY, firstDepth, field.density, field.formation);
            cameraReported = true;
        }
        if (settings.reference) {
            for (unsigned i = 0; i < references.size(); ++i) {
                const auto &source = model.sources()[i];
                auto &layer = references[i];
                layer->setFields(&source.field, 1);
                layer->update(source.sourceAge);
                if (source.active && source.field.density > .00001f && !layer->render(camera)) {
                    fail("reference renderer refused an active source"); return;
                }
            }
        } else {
            std::string error;
            if (!renderer.render(camera, model, seconds, error)) { fail(error); return; }
            if (renderer.stats().sources != settings.sources.count) {
                fail("renderer omitted a retained source"); return;
            }
            if (renderer.stats().activeSources != activeCount()) {
                fail("renderer active submissions differ from active source count"); return;
            }
        }
        ++renderedFrames;
        peakActive = std::max(peakActive, activeCount());
        peakSubmitted = std::max(peakSubmitted, settings.reference ? activeCount() : renderer.stats().activeSources);
        const unsigned logicFrame = TheGameLogic->getFrame();
        if (logicFrame - lastReport >= LOGICFRAMES_PER_SECOND) { lastReport = logicFrame; report(); }
    }
};
StressRenderObject *renderObject = nullptr;

Coord3D groundPosition(float x, float y)
{
    Coord3D p; p.set(x, y, TheTerrainLogic->getGroundHeight(x, y)); return p;
}

bool createWalkers(Player *player)
{
    const ThingTemplate *tmpl = TheThingFactory->findTemplate("AmericaInfantryRanger", FALSE);
    if (!tmpl) return false;
    Team *team = player->getDefaultTeam();
    for (unsigned i = 0; i < walkers.size(); ++i) {
        auto &walker = walkers[i];
        const float y = settings.sources.centreY + (float(i) - 1.5f) * 32;
        walker.endpoints[0] = groundPosition(settings.sources.centreX - 330, y);
        walker.endpoints[1] = groundPosition(settings.sources.centreX + 330, y);
        Object *object = TheThingFactory->newObject(tmpl, team, MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_NO_ATTACK));
        if (!object) return false;
        walker.id = object->getID();
        object->setPosition(&walker.endpoints[i & 1]);
        object->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_UNSELECTABLE));
        auto *ai = object->getAI();
        auto *body = object->getBodyModule();
        if (!ai || !body) return false;
        body->setIndestructible(TRUE);
        walker.health = body->getHealth();
        walker.previous = *object->getPosition();
        for (BehaviorModule **module = object->getBehaviorModules(); *module; ++module)
            if (auto *create = (*module)->getCreate()) create->onBuildComplete();
        team->setActive();
        TheAI->pathfinder()->addObjectToPathfindMap(object);
        ai->setAttitude(AI_PASSIVE);
        walker.destination = 1 - (i & 1);
        ai->aiMoveToPosition(&walker.endpoints[walker.destination], CMD_FROM_SCRIPT);
    }
    return true;
}

void createProps()
{
    if (!WW3DAssetManager::Get_Instance()) return;
    for (unsigned i = 0; i < 8; ++i) {
        RenderObjClass *object = WW3DAssetManager::Get_Instance()->Create_Render_Obj(i & 1 ? "PMBarrel01" : "PMCrates01");
        if (!object) continue;
        AABoxClass box; object->Get_Obj_Space_Bounding_Box(box);
        Coord3D p = groundPosition(settings.sources.centreX + (float(i % 4) - 1.5f) * 150,
            settings.sources.centreY + (i < 4 ? -110 : 110));
        Matrix3D transform(1);
        transform.Set_Translation(Vector3(p.x, p.y, p.z - box.Center.Z + box.Extent.Z));
        object->Set_Transform(transform); object->Set_Force_Visible(true);
        W3DDisplay::m_3DScene->Add_Render_Object(object); scenery.push_back(object);
    }
}
}

bool initialize(const Settings &requested)
{
    if (initialized) return !failed;
    settings = requested; failed = false;
    if (!TheGameLogic || !TheTerrainLogic || !ThePlayerList || !ThePartitionManager
        || !TheThingFactory || !TheTacticalView || !TheParticleSystemManager || !TheAI
        || !TheAI->pathfinder() || !W3DDisplay::m_3DScene) { fail("scene subsystems unavailable"); return false; }
    if (settings.reference && settings.sources.count > 2) { fail("reference mode requires --fields 1 or 2"); return false; }
    Region3D extent; TheTerrainLogic->getExtent(&extent);
    settings.sources.centreX = (extent.lo.x + extent.hi.x) * .5f;
    settings.sources.centreY = (extent.lo.y + extent.hi.y) * .5f;
    settings.sources.groundZ = TheTerrainLogic->getGroundHeight(settings.sources.centreX, settings.sources.centreY);
    if (!model.configure(settings.sources)) { fail("invalid source configuration"); return false; }
    const auto &bounds = model.bounds();
    if (bounds.minX < extent.lo.x || bounds.maxX > extent.hi.x
        || bounds.minY < extent.lo.y || bounds.maxY > extent.hi.y) {
        fail("source layout exceeds the map; reduce --spacing or --radius"); return false;
    }
    if (TheVictoryConditions) TheVictoryConditions->setVictoryConditions(0);
    for (Object *object = TheGameLogic->getFirstObject(); object; ) {
        Object *next = object->getNextObject(); TheGameLogic->destroyObject(object); object = next;
    }
    TheParticleSystemManager->reset();
    Player *player = ThePlayerList->getLocalPlayer();
    if (!player) { fail("local player unavailable"); return false; }
    TheParticleSystemManager->setLocalPlayerIndex(player->getPlayerIndex());
    ThePartitionManager->revealMapForPlayerPermanently(player->getPlayerIndex());
    ThePartitionManager->refreshShroudForLocalPlayer();
    TheTacticalView->setAngleAndPitchToDefault();
    const Coord3D centre = groundPosition(settings.sources.centreX, settings.sources.centreY);
    TheTacticalView->lookAt(&centre);
    TheTacticalView->setZoomLimited(FALSE);
    TheTacticalView->setZoomToHeight(settings.zoom > 0 ? settings.zoom
        : settings.sources.count > 2 && settings.sources.layout == CloudStress::Layout::Grid ? 1100.f : 520.f);
    if (TheControlBar) TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_HIDDEN);
    if (TheInGameUI) { TheInGameUI->setInputEnabled(TRUE); TheInGameUI->setSuperweaponDisplayEnabledByScript(FALSE); }
    initialized = true;
    startFrame = lastReport = TheGameLogic->getFrame(); lastFrame = ~0u;
    renderedFrames = peakActive = peakSubmitted = 0; cameraReported = false; seconds = 0; startTicks = SDL_GetTicks();
    auto *device = dynamic_cast<PosixDevice9 *>(DX8Wrapper::_Get_D3D_Device());
    if (!device || !device->Get_Gpu()) { fail("SDL GPU device unavailable"); return false; }
    if (settings.reference) {
        for (unsigned i = 0; i < settings.sources.count; ++i) {
            auto layer = HazardCloud::CreateLayer();
            if (!layer || !layer->initialize()) { fail("reference renderer initialization failed"); return false; }
            layer->setInteractionEnabled(true);
            const auto &field = model.sources()[i].field;
            const float reach = field.radius + field.fringe + 24;
            layer->setFlowDomain(field.ground.x - reach, field.ground.y - reach,
                field.ground.x + reach, field.ground.y + reach);
            references.push_back(std::move(layer));
        }
    } else {
        std::string error;
        CloudStress::RendererConfig config; config.volumeSide = settings.volumeSide; config.slices = settings.slices;
        config.incrementalBaking = settings.incrementalBaking; config.bakeSlicesPerFrame = settings.bakeSlicesPerFrame;
        if (!renderer.initialize(device->Get_Gpu(), config, error)) { fail(error); return false; }
        constexpr unsigned side = 129;
        std::vector<float> heights(side * side);
        for (unsigned y = 0; y < side; ++y) for (unsigned x = 0; x < side; ++x)
            heights[y * side + x] = TheTerrainLogic->getGroundHeight(
                bounds.minX + (bounds.maxX - bounds.minX) * x / (side - 1),
                bounds.minY + (bounds.maxY - bounds.minY) * y / (side - 1));
        renderer.setTerrain(heights.data(), side);
    }
    model.launch(0);
    renderObject = new StressRenderObject;
    W3DDisplay::m_3DScene->Add_Render_Object(renderObject);
    createProps();
    if (!createWalkers(player)) { fail("could not create passive Ranger inspection routes"); return false; }
    startTicks = SDL_GetTicks(); // Exclude shader compilation and scene construction from live FPS.
    std::fprintf(stderr, "cloud-stress: initialized requested=%u layout=%s palette=%s source-lifetime=%.2f stagger=%.3f damage-simulation=absent\n",
        settings.sources.count, settings.sources.layout == CloudStress::Layout::Grid ? "grid" : "overlap",
        paletteName(), settings.sources.lifetime, settings.sources.stagger);
    return true;
}

void update()
{
    if (!initialized || failed || !TheGameLogic) return;
    const unsigned frame = TheGameLogic->getFrame();
    if (frame < startFrame) { fail("session reset while stress scene active"); return; }
    if (frame == lastFrame) return;
    const float dt = lastFrame == ~0u ? 0 : float(frame - lastFrame) / LOGICFRAMES_PER_SECOND;
    lastFrame = frame; seconds = settings.freezeTime >= 0 ? settings.freezeTime
        : float(frame - startFrame) / LOGICFRAMES_PER_SECOND;
    model.update(seconds);
    if (model.sources().size() != settings.sources.count) { fail("model omitted a requested source"); return; }
    for (unsigned i = 0; i < walkers.size(); ++i) {
        auto &walker = walkers[i];
        Object *object = TheGameLogic->findObjectByID(walker.id);
        if (!object || !object->getBodyModule() || std::fabs(object->getBodyModule()->getHealth() - walker.health) > .01f) {
            fail("passive inspection unit disappeared or lost health"); return;
        }
        const Coord3D &p = *object->getPosition(), &target = walker.endpoints[walker.destination];
        auto &body = bodies[i];
        body.x = p.x; body.y = p.y; body.z = p.z;
        body.radius = std::max(2.f, object->getGeometryInfo().getBoundingCircleRadius()); body.height = 13;
        body.vx = dt > 0 ? (p.x - walker.previous.x) / dt : 0;
        body.vy = dt > 0 ? (p.y - walker.previous.y) / dt : 0;
        if (body.vx*body.vx + body.vy*body.vy > 10000) body.vx = body.vy = 0;
        walker.previous = p;
        const float dx = p.x - target.x, dy = p.y - target.y;
        if (dx*dx + dy*dy < 144) {
            walker.destination ^= 1;
            object->getAI()->aiMoveToPosition(&walker.endpoints[walker.destination], CMD_FROM_SCRIPT);
        }
    }
    const unsigned bodyCount = settings.freezeTime >= 0 ? 0 : unsigned(bodies.size());
    renderer.setBodies(bodies.data(), bodyCount);
    for (auto &layer : references) layer->setBodies(bodies.data(), bodyCount);
    if (settings.replayFrame && frame - startFrame == settings.replayFrame) replay();
    if (settings.paletteFrame && frame - startFrame == settings.paletteFrame) setPalette(settings.nextPalette);
    for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject())
        if (!object->isDestroyed() && object->isKindOf(KINDOF_CLEANUP_HAZARD)) fail("unexpected gameplay hazard");
    if (TheControlBar) TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_HIDDEN);
}

void replay()
{
    if (!initialized || failed) return;
    const unsigned cancelled = renderer.stats().pendingSlices, previousBaked = renderer.stats().bakedSources;
    model.launch(seconds);
    renderer.reset();
    for (auto &layer : references) layer->reset();
    std::fprintf(stderr, "cloud-stress: replay seconds=%.3f retained=%zu cancelled-slices=%u previous-baked=%u\n",
        seconds, model.sources().size(), cancelled, previousBaked);
}
void setPalette(int palette)
{
    if (!initialized || failed || palette < -1 || palette > 3) return;
    settings.sources.palette = palette;
    std::fprintf(stderr, "cloud-stress: palette seconds=%.3f palette=%d\n", seconds, palette);
    if (!model.configure(settings.sources)) { fail("palette reconfiguration failed"); return; }
    replay();
}
bool healthy() { return initialized && !failed; }
bool validationPassed()
{
    if (!initialized || failed) return false;
    if (!renderedFrames) { fail("renderer never drew the stress scene"); return false; }
    if (model.sources().size() != settings.sources.count) { fail("source records were lost before teardown"); return false; }
    report(); return true;
}
const char *paletteName()
{
    switch (settings.sources.palette) {
        case 0: return "toxin green"; case 2: return "anthrax blue";
        case 3: return "gamma purple"; case 1: return "nuclear"; default: return "mixed";
    }
}
void shutdown()
{
    if (renderObject) { renderObject->Remove(); renderObject->Release_Ref(); renderObject = nullptr; }
    for (auto &layer : references) layer->shutdown(); references.clear();
    renderer.shutdown();
    for (auto *object : scenery) { object->Remove(); object->Release_Ref(); } scenery.clear();
    if (TheGameLogic) for (auto &walker : walkers) {
        if (walker.id != INVALID_ID) if (Object *object = TheGameLogic->findObjectByID(walker.id)) TheGameLogic->destroyObject(object);
        walker = {};
    }
    initialized = false;
}
}
