// Harmless toxin/radiation scene using the production W3D particle and unit paths.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "CloudPreviewScene.h"
#include "CloudPreviewVolume.h"
#include "CloudPreviewFlow.h"
#include "CloudPreviewSources.h"
#include "Common/DrawModule.h"
#include "GameClient/Drawable.h"
#include "WWMath/obbox.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/rendobj.h"
#include "WWMath/aabox.h"
#include "WWMath/matrix3d.h"
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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace CloudPreviewScene {
namespace {

const unsigned FIELD_COUNT = 2;
const unsigned UNIT_COUNT = 4;
const unsigned REPORT_INTERVAL = 5 * LOGICFRAMES_PER_SECOND;
const unsigned MOTION_GRACE = 3 * LOGICFRAMES_PER_SECOND;
const unsigned EMITTER_LIFETIME = 60 * LOGICFRAMES_PER_SECOND;
const Real FIELD_RADIUS = 115.0f;
const Real FIELD_OFFSET = 155.0f;
const Real WALK_HALF_WIDTH = 330.0f;
const Real ARRIVE_RADIUS = 12.0f;

struct Field {
    const char *name;
    ParticleSystemID id;
    Coord3D position;
    unsigned lastParticleFrame;
    Real radius;
};

struct Walker {
    ObjectID id;
    Coord3D endpoints[2];
    Coord3D previous;
    Real health;
    Real height;
    Real radius;
    Real distance;
    unsigned destination;
    unsigned turns;
    unsigned enteredFields;
};

Field fields[FIELD_COUNT] = {
    { "PoisonFieldLarge", INVALID_PARTICLE_SYSTEM_ID, {} },
    { "RadiationFieldLarge", INVALID_PARTICLE_SYSTEM_ID, {} }
};
Walker walkers[UNIT_COUNT] = {};
bool initialized = false;
Appearance appearance = Appearance::Volumetric;
bool failed = false;
unsigned styleStartFrame = 0;
unsigned startFrame = 0;
unsigned lastFrame = ~0u;
unsigned lastReport = 0;
Real infantryHeight = 13.0f;
unsigned activeFieldCount = FIELD_COUNT;
std::vector<RenderObjClass *> scenery;

void fail(const char *reason)
{
    if (!failed)
        std::fprintf(stderr, "cloud-preview: validation FAIL: %s\n", reason);
    failed = true;
}

Real distanceSquared(const Coord3D &a, const Coord3D &b)
{
    const Real x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}

Coord3D groundPosition(Real x, Real y)
{
    Coord3D position;
    position.set(x, y, TheTerrainLogic->getGroundHeight(x, y));
    return position;
}

void clearScenery()
{
    for (RenderObjClass *object : scenery) {
        object->Remove();
        object->Release_Ref();
    }
    scenery.clear();
}

bool createScenery(const Coord3D &centre)
{
    if (!WW3DAssetManager::Get_Instance() || !W3DDisplay::m_3DScene) return false;
    struct Sample { const char *model; float x, y, angle; bool betweenLanes; };
    const Sample samples[] = {
        // Buildings sit beyond the walking lanes. Their different heights let
        // the haze cover walls while taller rooflines remain readable.
        {"CBBrnShed", -175, 105, .12f, false},
        {"CBNShack", 0, 94, -.16f, false},
        {"CBNRiverHo", 175, 105, -.08f, false},
        // The comparison centers and central source have low props beneath the
        // cloud. These models never become gameplay objects or path obstacles.
        {"PMCrates01", -155, 0, 0, true},
        {"PMCrates01", 155, 0, 0, true},
        {"PMCrates01", 0, -32, 0, true},
        {"PMBarrel01", -178, 0, 0, true},
        {"PMBarrel01", 178, 0, 0, true},
        {"PMBarrel01", -22, -32, 0, true},
    };
    for (const Sample &sample : samples) {
        RenderObjClass *object = WW3DAssetManager::Get_Instance()->Create_Render_Obj(sample.model);
        if (!object) {
            std::fprintf(stderr, "cloud-preview: inspection model missing: %s\n", sample.model);
            return false;
        }
        AABoxClass bounds;
        object->Get_Obj_Space_Bounding_Box(bounds);
        // Keep actual model bounds clear of all four unchanged walking lanes.
        // Small props occupy the gaps, with six world units maximum half-width
        // in Y. Authored building scale remains unchanged behind the routes.
        const float cosine = std::cos(sample.angle), sine = std::sin(sample.angle);
        const float extentY = std::abs(sine) * bounds.Extent.X + std::abs(cosine) * bounds.Extent.Y;
        const float scale = sample.betweenLanes ? std::min(1.0f, 6.0f / std::max(extentY, .001f)) : 1.0f;
        const float centerX = scale * (cosine * bounds.Center.X - sine * bounds.Center.Y);
        const float centerY = scale * (sine * bounds.Center.X + cosine * bounds.Center.Y);
        const float centerTargetY = centre.y + (sample.betweenLanes ? sample.y
            : std::max(sample.y, 48.0f + 20.0f + extentY));
        const float x = centre.x + sample.x - centerX;
        const float y = centerTargetY - centerY;
        const float ground = TheTerrainLogic->getGroundHeight(centre.x + sample.x, centerTargetY);
        Matrix3D transform(1);
        transform.Rotate_Z(sample.angle);
        transform.Scale(scale);
        transform.Set_Translation(Vector3(x, y, ground - scale * (bounds.Center.Z - bounds.Extent.Z)));
        object->Set_Transform(transform);
        object->Set_Force_Visible(true);
        W3DDisplay::m_3DScene->Add_Render_Object(object);
        scenery.push_back(object);
        std::fprintf(stderr, "cloud-preview: inspection prop=%s centre=(%.1f,%.1f) height=%.1f render-only=1\n",
            sample.model, centre.x + sample.x, centerTargetY, scale * bounds.Extent.Z * 2);
    }
    std::fprintf(stderr, "cloud-preview: inspection yard props=%zu; all four walking lanes remain clear\n", scenery.size());
    return true;
}

void report()
{
    unsigned moving = 0, visits = 0, turns = 0, particles = 0, groundParticles = 0;
    Real health = 0.0f, originalHealth = 0.0f, travelled = 0.0f;
    for (unsigned i = 0; i < FIELD_COUNT; ++i) {
        ParticleSystem *system = TheParticleSystemManager->findParticleSystem(fields[i].id);
        if (system) {
            groundParticles += system->getParticleCount();
            particles += system->getParticleCount();
        }
    }
    for (unsigned i = 0; i < UNIT_COUNT; ++i) {
        const Walker &walker = walkers[i];
        Object *object = TheGameLogic->findObjectByID(walker.id);
        if (object && object->getBodyModule()) health += object->getBodyModule()->getHealth();
        originalHealth += walker.health;
        travelled += walker.distance;
        if (walker.distance > 3.0f) ++moving;
        visits += (walker.enteredFields & 1u) + ((walker.enteredFields >> 1u) & 1u);
        turns += walker.turns;
    }
    std::fprintf(stderr,
        "cloud-preview: frame=%u fields=%u particles=%u units=%u moving=%u "
        "health=%.1f/%.1f travelled=%.1f field-visits=%u/8 turns=%u ground-particles=%u "
        "damage-mode=disabled interaction=%s wake=%.3f bodies=%u cloud-height=%.2f scenario=%s phase=%s style=%s status=%s\n",
        TheGameLogic->getFrame() - startFrame, activeFieldCount, particles, UNIT_COUNT,
        moving, health, originalHealth, travelled, visits, turns, groundParticles,
        CloudPreviewVolume::isInteractionEnabled() ? "on" : "off",
        CloudPreviewVolume::maxDisturbance(), CloudPreviewVolume::activeBodies(), infantryHeight,
        scenarioName(), scenarioPhase(), appearanceName(), failed ? "FAIL" : "OK");
}

bool createField(Field &field);

void invalidateGroundFields(bool fadeIn = false)
{
    // This executable always uses the W3D manager. Refresh only its footprints:
    // a manager reset would also erase a bomb's in-progress release particles.
    static_cast<W3DParticleSystemManager *>(TheParticleSystemManager)->invalidateGroundFields(fadeIn);
}

bool clearField(Field &field)
{
    bool removed = false;
    if (ParticleSystem *system = TheParticleSystemManager->findParticleSystem(field.id)) {
        system->deleteInstance();
        removed = true;
    }
    field.id = INVALID_PARTICLE_SYSTEM_ID;
    return removed;
}

void clearFields()
{
    for (Field &field : fields) clearField(field);
    invalidateGroundFields();
}

void updateFields(bool immediate = false)
{
    CloudPreviewSources::Field source[FIELD_COUNT] = {};
    activeFieldCount = CloudPreviewSources::getFields(source, FIELD_COUNT);
    bool refresh = immediate;
    for (unsigned f = 0; f < FIELD_COUNT; ++f) {
        Field &field = fields[f];
        if (appearance == Appearance::Volumetric || f >= activeFieldCount || source[f].volume.density <= 0) {
            refresh = clearField(field) || refresh;
            continue;
        }
        const auto &state = source[f];
        field.name = state.particleTemplate;
        field.radius = state.originalRadius;
        if (distanceSquared(field.position, state.volume.ground) > 0.0001f) refresh = true;
        field.position = state.volume.ground;
        ParticleSystem *ground = TheParticleSystemManager->findParticleSystem(field.id);
        if (!ground) {
            if (!createField(field)) {
                fail("could not create appearance field");
                continue;
            }
            ground = TheParticleSystemManager->findParticleSystem(field.id);
            if (getScenario() != Scenario::Comparison) {
                // Ground fields are drawn from their emitter descriptor, not
                // individual sprites. Preserve the renderer's authored puddle
                // size before removing particle lifetime from its fade clock.
                // This lets the harmless source timeline own fade-in/out while
                // retaining the game's texture, palette, scatter and bubbling.
                auto mid = [](const GameClientRandomVariable &v) {
                    return .5f * (v.getMinimumValue() + v.getMaximumValue());
                };
                const Real half = .5f * mid(ground->m_lifetime);
                const Real rate = mid(ground->m_sizeRate);
                const Real damping = mid(ground->m_sizeRateDamping);
                const Real growth = damping < 1 ? rate * (1 - std::pow(damping, half)) / (1 - damping) : rate * half;
                const Real size = std::max(1.0f, mid(ground->m_startSize) + growth);
                ground->m_startSize.setRange(size, size);
                ground->m_sizeRate.setRange(0, 0);
                ground->m_lifetime.setRange(0, 0);
                ground->stop();
                refresh = true;
            }
        }
        ground->setPosition(&field.position);
        // The production objectless ground-field fade is remaining / 3 seconds.
        // Two frames keep a stopped descriptor alive through the engine update.
        const unsigned lifetime = getScenario() == Scenario::Comparison ? EMITTER_LIFETIME
            : std::max(2u, static_cast<unsigned>(std::ceil(state.volume.density * 3 * LOGICFRAMES_PER_SECOND)));
        ground->setFiniteSystemLifetime(lifetime);
    }
    if (refresh) invalidateGroundFields();
}

void applyCloudStyle()
{
    CloudPreviewVolume::setEnabled(appearance == Appearance::Volumetric);
    styleStartFrame = TheGameLogic->getFrame();
    clearFields();
    updateFields(true);
    std::fprintf(stderr, "cloud-preview: appearance=%s (preview only)\n", appearanceName());
}

bool createField(Field &field)
{
    const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(field.name);
    if (!tmpl) return false;
    // This directly creates client particles. No weapon, FXList, OCL or hazard
    // object is fired, so the preview has no contamination damage mechanism.
    ParticleSystem *system = TheParticleSystemManager->createParticleSystem(tmpl, FALSE);
    if (!system) return false;
    field.id = system->getSystemID();
    field.lastParticleFrame = TheGameLogic->getFrame();
    if (system->getPriority() != AREA_EFFECT || !system->m_isGroundAligned
        || (system->getEmisionVolumeType() != ParticleSystemInfo::SPHERE
            && system->getEmisionVolumeType() != ParticleSystemInfo::CYLINDER)) {
        std::fprintf(stderr, "cloud-preview: incompatible field template '%s'\n", field.name);
        return false;
    }
    system->setPosition(&field.position);
    // Retail fields normally receive this radius from their weapon's FX caller.
    // A bare visual emitter must set it itself, on the instance only.
    system->setEmissionVolumeSphereRadius(field.radius);
    system->setEmissionVolumeCylinderRadius(field.radius);
    system->setFiniteSystemLifetime(EMITTER_LIFETIME);
    system->setInitialDelay(0);
    system->setSaveable(FALSE);
    system->start();
    system->trigger();
    std::fprintf(stderr,
        "cloud-preview: visual field=%s id=%d texture=%s type=%d priority=%d "
        "ground-aligned=%d radius=%.1f position=(%.1f,%.1f,%.1f) gameplay-hazard=none\n",
        field.name, static_cast<int>(field.id), system->getParticleTypeName().str(),
        static_cast<int>(system->m_particleType), static_cast<int>(system->getPriority()),
        static_cast<int>(system->m_isGroundAligned), field.radius,
        field.position.x, field.position.y, field.position.z);
    return true;
}

// The preview is client-only. Read the scaled model bounds once, falling back
// to authored collision geometry if the model has not produced bounds yet.
Real visibleInfantryHeight(Object *object)
{
    const GeometryInfo &geometry = object->getGeometryInfo();
    Real result = geometry.getMaxHeightAbovePosition() + geometry.getMaxHeightBelowPosition();
    Real modelHeight = 0;
    if (Drawable *drawable = object->getDrawable()) {
        for (DrawModule **module = drawable->getDrawModules(); module && *module; ++module) {
            const ObjectDrawInterface *draw = (*module)->getObjectDrawInterface();
            OBBoxClass box;
            if (draw && draw->clientOnly_getRenderObjBoundBox(&box)) {
                Vector3 extent;
                box.Compute_Axis_Aligned_Extent(&extent);
                const Real height = 2.0f * extent.Z;
                if (std::isfinite(height) && height >= 5.0f && height <= 60.0f)
                    modelHeight = std::max(modelHeight, height);
            }
        }
    }
    if (modelHeight > 0) result = modelHeight;
    return std::max(5.0f, std::min(result, 60.0f));
}

bool createWalker(unsigned index, const ThingTemplate *tmpl, Team *team, const Coord3D &centre)
{
    Walker &walker = walkers[index];
    const Real lane = (static_cast<Real>(index) - 1.5f) * 32.0f;
    walker.endpoints[0] = groundPosition(centre.x - WALK_HALF_WIDTH, centre.y + lane);
    walker.endpoints[1] = groundPosition(centre.x + WALK_HALF_WIDTH, centre.y + lane);
    const unsigned origin = index & 1u;
    Object *object = TheThingFactory->newObject(tmpl, team,
        MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_NO_ATTACK));
    if (!object) return false;
    walker.id = object->getID();
    object->setPosition(&walker.endpoints[origin]);
    object->setOrientation(origin ? 3.14159265f : 0.0f);
    object->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_UNSELECTABLE));
    AIUpdateInterface *ai = object->getAI();
    BodyModuleInterface *body = object->getBodyModule();
    if (!ai || !body) return false;
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        if (!TheAI->pathfinder()->validMovementPosition(object->getCrusherLevel() > 0,
            object->getLayer(), ai->getLocomotorSet(), &walker.endpoints[endpoint])) {
            std::fprintf(stderr, "cloud-preview: unit %u endpoint %u is not traversable\n",
                index, endpoint);
            return false;
        }
    }
    body->setIndestructible(TRUE);
    for (BehaviorModule **module = object->getBehaviorModules(); *module; ++module) {
        if (CreateModuleInterface *create = (*module)->getCreate()) create->onBuildComplete();
    }
    team->setActive();
    TheAI->pathfinder()->addObjectToPathfindMap(object);
    ai->setAttitude(AI_PASSIVE);
    walker.health = body->getHealth();
    walker.height = visibleInfantryHeight(object);
    walker.radius = std::max(2.0f, object->getGeometryInfo().getBoundingCircleRadius());
    infantryHeight = std::max(infantryHeight, walker.height);
    std::fprintf(stderr, "cloud-preview: walker=%u rendered-height=%.2f interaction-radius=%.2f\n",
        index, walker.height, walker.radius);
    walker.previous = *object->getPosition();
    walker.destination = 1u - origin;
    ai->aiMoveToPosition(&walker.endpoints[walker.destination], CMD_FROM_SCRIPT);
    std::fprintf(stderr,
        "cloud-preview: walker=%u id=%d template=%s health=%.1f passive=1 no-attack=1 "
        "indestructible=1 route=(%.1f,%.1f)-(%.1f,%.1f)\n",
        index, static_cast<int>(walker.id), tmpl->getName().str(), walker.health,
        walker.endpoints[0].x, walker.endpoints[0].y, walker.endpoints[1].x, walker.endpoints[1].y);
    return true;
}

} // namespace

Appearance getAppearance() { return appearance; }

const char *appearanceName()
{
    switch (appearance) {
        case Appearance::Original: return "original";
        case Appearance::Volumetric: return "volumetric";
    }
    return "unknown";
}

void setAppearance(Appearance selected)
{
    if (appearance == selected) return;
    appearance = selected;
    if (initialized) applyCloudStyle();
}

void setScenario(Scenario selected)
{
    if (initialized) {
        clearFields();
        styleStartFrame = TheGameLogic->getFrame();
    }
    CloudPreviewSources::select(selected);
    if (initialized) updateFields(true);
}
Scenario getScenario() { return CloudPreviewSources::selected(); }
const char *scenarioName() { return CloudPreviewSources::name(); }
const char *scenarioPhase() { return CloudPreviewSources::phase(); }
void replayScenario()
{
    if (initialized) {
        clearFields();
        styleStartFrame = TheGameLogic->getFrame();
    }
    CloudPreviewSources::replay();
    if (initialized) updateFields(true);
}
void setLooping(bool value) { CloudPreviewSources::setLooping(value); }
bool isLooping() { return CloudPreviewSources::isLooping(); }

bool initialize()
{
    if (initialized) return !failed;
    failed = false;
    for (unsigned i = 0; i < UNIT_COUNT; ++i) walkers[i].id = INVALID_ID;
    if (!TheGameLogic || !TheGameLogic->isInGame() || TheGameLogic->isInShellGame()
        || !TheTerrainLogic || !ThePartitionManager || !ThePlayerList || !TheThingFactory
        || !TheParticleSystemManager || !TheTacticalView || !TheAI || !TheAI->pathfinder()) {
        fail("scene initialized before map/session subsystems were ready");
        return false;
    }
    const ThingTemplate *infantry = TheThingFactory->findTemplate("AmericaInfantryRanger", FALSE);
    if (!infantry) {
        fail("AmericaInfantryRanger template missing from mounted game data");
        return false;
    }
    for (unsigned i = 0; i < FIELD_COUNT; ++i) {
        const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(fields[i].name);
        if (!tmpl) {
            std::fprintf(stderr, "cloud-preview: missing field template '%s'\n", fields[i].name);
            fail("required retail contamination template unavailable");
            return false;
        }
    }
    Region3D extent;
    TheTerrainLogic->getExtent(&extent);
    const Real width = extent.hi.x - extent.lo.x, height = extent.hi.y - extent.lo.y;
    if (width < 900.0f || height < 600.0f) {
        std::fprintf(stderr,
            "cloud-preview: scene readiness frame=%u loading=%d extent=(%.1f,%.1f)-(%.1f,%.1f)\n",
            TheGameLogic->getFrame(), static_cast<int>(TheGameLogic->isLoadingMap()),
            extent.lo.x, extent.lo.y, extent.hi.x, extent.hi.y);
        fail("preview map is too small for the two cloud fields and walking routes");
        return false;
    }
    const Coord3D centre = groundPosition((extent.lo.x + extent.hi.x) * 0.5f,
        (extent.lo.y + extent.hi.y) * 0.5f);
    Player *player = ThePlayerList->getLocalPlayer();
    Team *team = player->getDefaultTeam();

    // The executable owns this dedicated session. Remove the skirmish startup
    // structures/units without death weapons, then retain only our samples.
    if (TheVictoryConditions) TheVictoryConditions->setVictoryConditions(0);
    for (Object *object = TheGameLogic->getFirstObject(); object; ) {
        Object *next = object->getNextObject();
        TheGameLogic->destroyObject(object);
        object = next;
    }
    TheParticleSystemManager->reset();
    TheParticleSystemManager->setLocalPlayerIndex(player->getPlayerIndex());
    ThePartitionManager->revealMapForPlayerPermanently(player->getPlayerIndex());
    ThePartitionManager->refreshShroudForLocalPlayer();
    TheTacticalView->setAngleAndPitchToDefault();
    TheTacticalView->lookAt(&centre);
    TheTacticalView->setZoomToHeight(520.0f);
    if (TheControlBar) TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_HIDDEN);
    if (TheInGameUI) {
        // Camera pan/rotate/zoom also uses this input gate. Sample units are
        // unselectable and cannot attack, so leave camera input available.
        TheInGameUI->setInputEnabled(TRUE);
        TheInGameUI->setSuperweaponDisplayEnabledByScript(FALSE);
    }

    startFrame = TheGameLogic->getFrame();
    lastReport = startFrame;
    infantryHeight = 0;
    lastFrame = ~0u;
    initialized = true; // Partial construction is also cleaned by shutdown().
    if (!createScenery(centre)) {
        fail("could not create inspection-yard scenery");
        return false;
    }
    for (unsigned i = 0; i < FIELD_COUNT; ++i) {
        fields[i].position = groundPosition(centre.x + (i ? FIELD_OFFSET : -FIELD_OFFSET), centre.y);
        fields[i].id = INVALID_PARTICLE_SYSTEM_ID;
    }
    for (unsigned i = 0; i < UNIT_COUNT; ++i) {
        if (!createWalker(i, infantry, team, centre)) {
            fail("could not create a walking preview unit");
            return false;
        }
    }
    if (!CloudPreviewVolume::initialize(fields[0].position, fields[1].position)) {
        fail("could not initialize the isolated volume renderer");
        return false;
    }
    CloudPreviewVolume::setInfantryHeight(infantryHeight);
    if (!CloudPreviewSources::initialize(centre, infantryHeight)) {
        fail("could not initialize source scenarios");
        return false;
    }
    applyCloudStyle();
    std::fprintf(stderr,
        "cloud-preview: initialized centre=(%.1f,%.1f) extent=(%.1f,%.1f)-(%.1f,%.1f) "
        "field-radius=%.1f damage-simulation=absent\n",
        centre.x, centre.y, extent.lo.x, extent.lo.y, extent.hi.x, extent.hi.y, FIELD_RADIUS);
    report();
    return !failed;
}

void update()
{
    if (!initialized || !TheGameLogic || !TheParticleSystemManager) return;
    const unsigned frame = TheGameLogic->getFrame();
    if (frame == lastFrame) return;
    if (frame < startFrame) {
        fail("preview session was reset while its scene was active");
        return;
    }
    const Real dt = lastFrame != ~0u && frame > lastFrame
        ? static_cast<Real>(frame - lastFrame) / LOGICFRAMES_PER_SECOND : 0.0f;
    lastFrame = frame;
    CloudPreviewFlow::Body bodies[UNIT_COUNT] = {};
    unsigned bodyCount = 0;
    if (appearance == Appearance::Volumetric && frame - styleStartFrame >= MOTION_GRACE
        && !CloudPreviewVolume::validationPassed())
        fail("volume rendering or scene-depth capture failed");
    for (unsigned i = 0; i < FIELD_COUNT; ++i) {
        // Source fields deliberately use stopped descriptors for the production
        // terrain renderer; only the steady comparison needs sprite starvation
        // checks. The source controller owns field creation and expiry below.
        if (appearance == Appearance::Volumetric || getScenario() != Scenario::Comparison) continue;
        ParticleSystem *system = TheParticleSystemManager->findParticleSystem(fields[i].id);
        if (!system || system->isDestroyed()) {
            fail("a preview particle system disappeared");
            continue;
        }
        // Keep this exact instance alive. The field renderer reads this remaining
        // lifetime when there is deliberately no gameplay hazard object to follow.
        system->setFiniteSystemLifetime(EMITTER_LIFETIME);
        // The ground renderer draws a stable procedural footprint and can limit
        // an emitter to one live particle. A normal gap between its death and
        // the next burst is not a missing field; require sustained starvation.
        if (system->getParticleCount() > 0) fields[i].lastParticleFrame = frame;
        if (frame - fields[i].lastParticleFrame > 2 * LOGICFRAMES_PER_SECOND)
            fail("a preview field is not emitting particles");
    }
    for (unsigned i = 0; i < UNIT_COUNT; ++i) {
        Walker &walker = walkers[i];
        Object *object = TheGameLogic->findObjectByID(walker.id);
        if (!object || object->isDestroyed() || !object->getBodyModule()) {
            fail("a walking preview unit disappeared");
            continue;
        }
        if (std::fabs(object->getBodyModule()->getHealth() - walker.health) > 0.01f)
            fail("preview unit health changed");
        const Coord3D &position = *object->getPosition();
        walker.distance += std::sqrt(distanceSquared(position, walker.previous));
        CloudPreviewFlow::Body &body = bodies[bodyCount++];
        body.x = position.x; body.y = position.y; body.z = position.z;
        body.radius = walker.radius; body.height = walker.height;
        if (dt > 0) {
            body.vx = (position.x - walker.previous.x) / dt;
            body.vy = (position.y - walker.previous.y) / dt;
            // A teleport must not inject an unbounded impulse into a visual wake.
            if (body.vx * body.vx + body.vy * body.vy > 100.0f * 100.0f)
                body.vx = body.vy = 0;
        }
        walker.previous = position;
        for (unsigned f = 0; f < FIELD_COUNT; ++f) {
            if (distanceSquared(position, fields[f].position) <= FIELD_RADIUS * FIELD_RADIUS)
                walker.enteredFields |= 1u << f;
        }
        if (distanceSquared(position, walker.endpoints[walker.destination]) <= ARRIVE_RADIUS * ARRIVE_RADIUS) {
            walker.destination ^= 1u;
            ++walker.turns;
            if (AIUpdateInterface *ai = object->getAI())
                ai->aiMoveToPosition(&walker.endpoints[walker.destination], CMD_FROM_SCRIPT);
        }
    }
    CloudPreviewVolume::setBodies(bodies, bodyCount);
    CloudPreviewSources::update(dt);
    if (!CloudPreviewSources::valid()) fail("source scenario validation failed");
    updateFields();
    CloudPreviewVolume::update(static_cast<float>(frame - startFrame) / LOGICFRAMES_PER_SECOND);
    for (Object *object = TheGameLogic->getFirstObject(); object; object = object->getNextObject()) {
        if (!object->isDestroyed() && object->isKindOf(KINDOF_CLEANUP_HAZARD))
            fail("unexpected gameplay hazard object exists in preview");
    }
    if (TheControlBar) TheControlBar->switchControlBarStage(CONTROL_BAR_STAGE_HIDDEN);
    if (frame - lastReport >= REPORT_INTERVAL) {
        lastReport = frame;
        report();
    }
}

bool validationPassed()
{
    if (!initialized || !TheGameLogic || !TheParticleSystemManager) return false;
    update();
    if (!CloudPreviewVolume::validationPassed()) fail("volume renderer did not pass validation");
    if (TheGameLogic->getFrame() - startFrame >= MOTION_GRACE) {
        for (unsigned i = 0; i < UNIT_COUNT; ++i)
            if (walkers[i].distance <= 3.0f) fail("a preview unit did not move after three seconds");
    }
    report();
    return !failed;
}

void shutdown()
{
    CloudPreviewSources::shutdown();
    CloudPreviewVolume::shutdown();
    clearScenery();
    if (!initialized) return;
    if (TheParticleSystemManager) {
        clearFields();
    }
    if (TheGameLogic) {
        for (unsigned i = 0; i < UNIT_COUNT; ++i) {
            if (walkers[i].id == INVALID_ID) continue;
            if (Object *object = TheGameLogic->findObjectByID(walkers[i].id))
                TheGameLogic->destroyObject(object);
        }
    }
    std::memset(walkers, 0, sizeof(walkers));
    initialized = false;
}

} // namespace CloudPreviewScene
