// Source choreography for the harmless cloud-preview laboratory.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PreRTS.h"
#include "CloudPreviewSources.h"
#include "CloudPreviewVolume.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "W3DDevice/GameClient/W3DDisplay.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/hanim.h"
#include "WW3D2/htree.h"
#include "WW3D2/rendobj.h"
#include "WWMath/matrix3d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <strings.h>
#include <vector>

namespace CloudPreviewSources {
namespace {
using Scenario = CloudPreviewScene::Scenario;
struct Definition {
    const char *name;
    const char *model;
    const char *rubble;
    const char *particleTemplate;
    float radius, palette, buildup, linger, dissipate;
};
// Radii are the retail field radii. Timing is deliberately compressed for the
// inspection loop; these values never replace a gameplay weapon or lifetime.
const Definition definitions[] = {
    {"Cloud comparison", nullptr, nullptr, nullptr, 115, 0, 0, 0, 0},
    {"Toxin Tractor / Contaminate", "UVToxinTrk", nullptr, "PoisonFieldMedium", 80, 0, 8, 10, 6},
    {"Anthrax Bomb", "AVBomber_B", nullptr, "AnthraxFieldLarge", 300, 2, 5, 12, 7},
    {"Dr. Thrax / Gamma Bomb", "AVBomber_B", nullptr, "GC_Chem_AnthraxFieldGammaLarge", 300, 3, 5, 12, 7},
    {"Dr. Thrax / SCUD Storm destruction", "UBScudStrm", "UBScudStrm_E", "PoisonFieldMedium", 80, 0, 3, 12, 7},
    {"Nuclear missile", "NBNRocket", nullptr, "RadiationFieldLarge", 200, 1, 6, 12, 8},
    {"Nuclear cannon", "NVNukeCn", nullptr, "RadiationFieldMedium", 50, 1, 3, 10, 6},
    {"Nuclear reactor destruction", "NBPwrPlant", "NBPwrPlant_E", "RadiationFieldMedium", 50, 1, 3, 12, 7}
};
float sourceFringes[sizeof(definitions) / sizeof(definitions[0])] = {};
float comparisonFringes[2] = {};
std::map<std::string, float> templateFringes;
Scenario scenario = Scenario::Comparison;
Coord3D centre = {};
float fullHeight = 18.0f, age = 0, nextSpray = 0;
bool ready = false, healthy = true, looping = true, released = false, delayedBurst = false;
RenderObjClass *prop = nullptr, *projectile = nullptr;
std::vector<ParticleSystemID> effects;
Field fields[2] = {};
unsigned fieldCount = 0;
const float Arrival = 2.0f;

const Definition &definition() { return definitions[static_cast<unsigned>(scenario)]; }
float smooth(float value) { value = std::clamp(value, 0.0f, 1.0f); return value * value * (3 - 2 * value); }
void fail(const char *reason, const char *asset)
{
    if (healthy) std::fprintf(stderr, "cloud-preview: source FAIL: %s: %s\n", reason, asset);
    healthy = false;
}

float cacheTemplateFringe(const char *name)
{
    const auto cached = templateFringes.find(name);
    if (cached != templateFringes.end()) return cached->second;
    const ParticleSystemTemplate *tmpl = TheParticleSystemManager
        ? TheParticleSystemManager->findTemplate(name) : nullptr;
    if (!tmpl) { fail("footprint particle template missing", name); return 0; }
    // The template's info base is protected. A temporary unstarted instance
    // exposes the same loaded values without changing the engine's public API.
    ParticleSystem *sample = TheParticleSystemManager->createParticleSystem(tmpl, FALSE);
    if (!sample) { fail("footprint particle descriptor creation failed", name); return 0; }
    sample->stop();
    const auto midpoint = [](const GameClientRandomVariable &value) {
        return 0.5f * (value.getMinimumValue() + value.getMaximumValue());
    };
    // Match collectGroundFields' half-life puddle size, using the loaded values
    // after INI overlays and frame-rate conversion. Each scattered centre can
    // reach the hazard radius; its rotated square extends sqrt(2) half-sizes
    // farther, including the renderer's maximum 4% breathing expansion. This
    // conservative visual fringe is separate from the unchanged hazard radius.
    const float halfLife = 0.5f * midpoint(sample->m_lifetime);
    const float rate = midpoint(sample->m_sizeRate);
    const float damping = midpoint(sample->m_sizeRateDamping);
    const float growth = damping < 1.0f
        ? rate * (1.0f - std::pow(damping, halfLife)) / (1.0f - damping) : rate * halfLife;
    const float halfSize = std::max(midpoint(sample->m_startSize) + growth, 1.0f);
    sample->deleteInstance();
    const float fringe = std::sqrt(2.0f) * 1.04f * halfSize;
    if (!std::isfinite(fringe) || fringe < 0) {
        fail("invalid original particle footprint", name);
        return 0;
    }
    templateFringes.emplace(name, fringe);
    std::fprintf(stderr, "cloud-preview: footprint template=%s puddle-half-size=%.2f fringe=%.2f\n",
        name, halfSize, fringe);
    return fringe;
}

void removeProp(RenderObjClass *&object)
{
    if (object) { object->Remove(); object->Release_Ref(); object = nullptr; }
}
void clearVisuals()
{
    removeProp(prop); removeProp(projectile);
    if (TheParticleSystemManager) for (ParticleSystemID id : effects)
        if (ParticleSystem *system = TheParticleSystemManager->findParticleSystem(id))
            // destroy() only stops emission; deleteInstance() also removes all
            // live particles, so replay/switching is clean even while paused.
            system->deleteInstance();
    effects.clear();
}

bool propPartMatches(const char *name, const char *mask, bool numbered)
{
    if (!name) return false;
    // Mesh names include their model prefix, e.g. NVNUKECN.MUZZLEFX01.
    if (const char *separator = std::strrchr(name, '.')) name = separator + 1;
    const size_t length = std::strlen(mask);
    if (strncasecmp(name, mask, length) != 0) return false;
    const char *suffix = name + length;
    if (!*suffix) return true;
    if (!numbered) return false;
    // WeaponMuzzleFlash names expand to numbered bones in W3DModelDraw.
    for (; *suffix; ++suffix) if (*suffix < '0' || *suffix > '9') return false;
    return true;
}

void setPropBranchHidden(RenderObjClass *object, bool hidden)
{
    object->Set_Hidden(hidden);
    for (int i = 0; i < object->Get_Num_Sub_Objects(); ++i)
        if (RenderObjClass *part = object->Get_Sub_Object(i)) {
            setPropBranchHidden(part, hidden);
            part->Release_Ref();
        }
}

void setPropPartHidden(RenderObjClass *object, const char *mask, bool hidden, bool numbered = false)
{
    const HTreeClass *tree = object->Get_HTree();
    std::vector<int> roots;
    if (tree) for (int bone = 1; bone < tree->Num_Pivots(); ++bone)
        if (propPartMatches(tree->Get_Bone_Name(bone), mask, numbered)) roots.push_back(bone);

    // Authored HideSubObject also controls meshes attached below that mesh's
    // bone. Use the pointer overload: Get_Num_Sub_Objects spans every LOD, so
    // treating its index as a LOD-zero index can address the wrong attachment.
    const int count = object->Get_Num_Sub_Objects();
    for (int i = 0; i < count; ++i)
        if (RenderObjClass *part = object->Get_Sub_Object(i)) {
            if (tree && propPartMatches(part->Get_Name(), mask, numbered)) {
                const int bone = object->Get_Sub_Object_Bone_Index(part);
                if (bone > 0 && bone < tree->Num_Pivots()) roots.push_back(bone);
            }
            part->Release_Ref();
        }

    for (int i = 0; i < count; ++i)
        if (RenderObjClass *part = object->Get_Sub_Object(i)) {
            bool matched = propPartMatches(part->Get_Name(), mask, numbered);
            int bone = object->Get_Sub_Object_Bone_Index(part);
            for (int depth = 0; !matched && tree && bone > 0
                && bone < tree->Num_Pivots() && depth < tree->Num_Pivots(); ++depth) {
                matched = std::find(roots.begin(), roots.end(), bone) != roots.end();
                if (!matched) bone = tree->Get_Parent_Index(bone);
            }
            if (matched) setPropBranchHidden(part, hidden);
            else if (part->Get_Num_Sub_Objects()) setPropPartHidden(part, mask, hidden, numbered);
            part->Release_Ref();
        }
}

RenderObjClass *makeProp(const char *model)
{
    if (!model) return nullptr;
    RenderObjClass *object = WW3DAssetManager::Get_Instance()->Create_Render_Obj(model);
    if (!object) { fail("model missing", model); return nullptr; }
    object->Set_Force_Visible(true);
    if (std::strcmp(model, "UVToxinTrk") == 0) {
        setPropPartHidden(object, "Turret", false);
        setPropPartHidden(object, "TurretUP01", true);
        setPropPartHidden(object, "TurretUP02", true);
    }
    if (std::strcmp(model, "NVNukeCn") == 0) {
        setPropPartHidden(object, "TURRET01", false);
        setPropPartHidden(object, "TURRETFRONT", true);
        setPropPartHidden(object, "TURRETBACK", true);
        // The source explosion is rendered separately; no weapon fires this
        // prop, so its muzzle flash must stay hidden for the entire scenario.
        setPropPartHidden(object, "MuzzleFX", true, true);
    }
    W3DDisplay::m_3DScene->Add_Render_Object(object);
    return object;
}
void place(RenderObjClass *object, float x, float y, float z, float angle = 0, bool downward = false)
{
    if (!object) return;
    Matrix3D transform(1);
    transform.Rotate_Z(angle);
    if (downward) transform.Rotate_X(3.14159265f);
    transform.Set_Translation(Vector3(x, y, z));
    object->Set_Transform(transform);
}
void emit(const char *particle, float z = 0, float x = 0, float y = 0, float angle = 0)
{
    const ParticleSystemTemplate *tmpl = TheParticleSystemManager->findTemplate(particle);
    if (!tmpl) { fail("particle template missing", particle); return; }
    ParticleSystem *system = TheParticleSystemManager->createParticleSystem(tmpl, FALSE);
    if (!system) { fail("particle creation failed", particle); return; }
    Coord3D position = centre; position.x += x; position.y += y; position.z += z;
    Matrix3D transform(1); transform.Rotate_Z(angle);
    transform.Set_Translation(Vector3(position.x, position.y, position.z));
    system->setLocalTransform(&transform);
    system->setSaveable(FALSE);
    // Keep authored particle motion, emission and lifetime. Only an accidentally
    // perpetual emitter is bounded; the entire scenario owns and cleans it up.
    if (system->isSystemForever()) system->setFiniteSystemLifetime(1);
    system->start(); system->trigger();
    effects.push_back(system->getSystemID());
}
void nuclearBurst(bool large)
{
    const char *largeNames[] = {"NukeFlare", "NukeMushroomExplosion", "NukeMushroomCloudRing",
        "NukeRing", "NukeBlastWave", "NukeShockwaveInverted", "NukeMushroomStem",
        "NukeShockwave", "NukeMushroomRing"};
    const char *smallNames[] = {"NukeCannonFlare", "NukeCannonMushroomExplosion", "NukeCannonMushroomCloudRing",
        "NukeCannonRing", "NukeCannonBlastWave", "NukeCannonShockwaveInverted", "NukeCannonMushroomStem",
        "NukeCannonShockwave", "NukeCannonMushroomRing"};
    for (unsigned i = 0; i < 9; ++i) emit(large ? largeNames[i] : smallNames[i]);
}
void release()
{
    released = true;
    switch (scenario) {
    case Scenario::AnthraxBomb:
        removeProp(prop);
        emit("AnthraxBombExplosion"); emit("AnthraxBombSpray"); emit("AnthraxBombLenzflare");
        break;
    case Scenario::AnthraxGamma:
        removeProp(prop);
        emit("AnthraxGammaBombExplosion"); emit("AnthraxGammaBombSpray"); emit("AnthraxGammaBombLenzflare");
        break;
    case Scenario::ToxinBuilding:
        removeProp(prop); prop = makeProp(definition().rubble);
        emit("BombTruckHEBioExplosionArms", 2); emit("BombTruckHEBioLenzFlare", 10);
        emit("BombTruckHEBioShockwave", 1);
        break;
    case Scenario::NuclearMissile: removeProp(prop); nuclearBurst(true); break;
    case Scenario::NuclearCannon: removeProp(projectile); nuclearBurst(false); break;
    case Scenario::NuclearReactor:
        removeProp(prop); prop = makeProp(definition().rubble); nuclearBurst(false); break;
    default: break;
    }
    std::fprintf(stderr, "cloud-preview: source-release scenario=%s visual-only=1\n", definition().name);
}
void updateProps()
{
    float approach = smooth(age / Arrival);
    if (scenario == Scenario::AnthraxBomb || scenario == Scenario::AnthraxGamma)
        place(prop, centre.x, centre.y, centre.z + 260 * (1 - approach) + 5, 0, true);
    else if (scenario == Scenario::NuclearMissile)
        place(prop, centre.x, centre.y, centre.z + 350 * (1 - approach) + 10, 0, true);
    else if (scenario == Scenario::Tractor) {
        place(prop, centre.x, centre.y - 35, centre.z, 1.57079633f);
        if (prop) {
            int turret = prop->Get_Bone_Index("Turret");
            if (turret > 0) {
                Matrix3D turn(1);
                turn.Rotate_Z(std::max(0.0f, std::min(age - Arrival, definition().buildup)) * 3.2f - 1.57079633f);
                prop->Capture_Bone(turret); prop->Control_Bone(turret, turn);
            }
        }
    }
    else if (scenario == Scenario::NuclearCannon) {
        place(prop, centre.x - 180, centre.y - 65, centre.z, .35f);
        place(projectile, centre.x - 180 * (1 - approach), centre.y - 65 * (1 - approach),
            centre.z + 12 + std::sin(approach * 3.14159265f) * 150, .35f);
    } else place(prop, centre.x, centre.y, centre.z);
}
void updateVolume()
{
    CloudPreviewVolume::FieldState state[2] = {};
    if (scenario == Scenario::Comparison) {
        for (unsigned i = 0; i < 2; ++i) {
            state[i].ground = centre; state[i].ground.x += i ? 155 : -155;
            state[i].radius = 115; state[i].height = fullHeight;
            state[i].density = state[i].formation = 1; state[i].palette = float(i);
            state[i].fringe = comparisonFringes[i];
            fields[i] = {state[i], i ? "RadiationFieldLarge" : "PoisonFieldLarge", 115, 60};
        }
        fieldCount = 2;
        CloudPreviewVolume::setFields(state, fieldCount);
        return;
    }
    const Definition &d = definition();
    float t = age - Arrival;
    float formation = smooth(t / d.buildup);
    float decay = smooth((t - d.buildup - d.linger) / d.dissipate);
    state[0].ground = centre;
    state[0].radius = d.radius;
    state[0].height = fullHeight;
    state[0].density = smooth(t / .6f) * (1 - decay);
    state[0].palette = d.palette; state[0].formation = formation;
    state[0].release = t >= 0 && t < d.buildup ? 1 - 0.65f * formation : 0;
    state[0].fringe = sourceFringes[static_cast<unsigned>(scenario)];
    fields[0] = {state[0], d.particleTemplate, d.radius,
        std::max(0.0f, Arrival + d.buildup + d.linger + d.dissipate - age)};
    if (d.palette >= 0.5f && d.palette < 1.5f) {
        // Fallout appears gradually across the entire footprint, rather than
        // advancing from the impact point like toxin. Keep the original-effect
        // descriptor above on its existing clock for an unchanged comparison.
        state[0].density = formation * (1 - decay);
        state[0].formation = 1;
    }
    fieldCount = 1;
    CloudPreviewVolume::setFields(state, fieldCount);
}
} // namespace

unsigned getFields(Field *out, unsigned capacity)
{
    if (!out) return 0;
    const unsigned count = std::min(capacity, fieldCount);
    std::copy_n(fields, count, out);
    return count;
}

bool initialize(const Coord3D &position, float height)
{
    centre = position; fullHeight = height; ready = true; healthy = true;
    templateFringes.clear();
    comparisonFringes[0] = cacheTemplateFringe("PoisonFieldLarge");
    comparisonFringes[1] = cacheTemplateFringe("RadiationFieldLarge");
    for (unsigned i = 1; i < sizeof(definitions) / sizeof(definitions[0]); ++i)
        sourceFringes[i] = cacheTemplateFringe(definitions[i].particleTemplate);
    if (!healthy) { ready = false; return false; }
    replay();
    return healthy;
}
void shutdown() { clearVisuals(); fieldCount = 0; ready = false; }
void replay()
{
    age = nextSpray = 0; released = delayedBurst = false;
    if (!ready) return;
    clearVisuals();
    CloudPreviewVolume::resetFlow();
    prop = makeProp(definition().model);
    if (scenario == Scenario::NuclearCannon) {
        projectile = makeProp("AVTankShel");
        if (prop) if (HAnimClass *animation = WW3DAssetManager::Get_Instance()->Get_HAnim("NVNukeCn.NVNukeCn")) {
            prop->Set_Animation(animation, float(animation->Get_Num_Frames() - 1), RenderObjClass::ANIM_MODE_MANUAL);
            animation->Release_Ref();
        }
    }
    updateProps(); updateVolume();
    std::fprintf(stderr, "cloud-preview: source-replay scenario=%s loop=%d\n", name(), int(looping));
    if (TheInGameUI) {
        UnicodeString message; message.translate(AsciiString(name()));
        TheInGameUI->message(message);
    }
}
void select(Scenario value)
{
    if (static_cast<unsigned>(value) >= sizeof(definitions) / sizeof(definitions[0])) return;
    scenario = value; replay();
}
Scenario selected() { return scenario; }
const char *name() { return definition().name; }
const char *phase()
{
    if (scenario == Scenario::Comparison) return "steady clouds";
    if (age < Arrival) return "approach / ready";
    float t = age - Arrival;
    if (t < definition().buildup) return scenario == Scenario::Tractor ? "spraying / buildup" : "release / buildup";
    if (t < definition().buildup + definition().linger) return "lingering";
    if (t < definition().buildup + definition().linger + definition().dissipate) return "dissipating";
    return "clear / B to replay";
}
void update(float deltaSeconds)
{
    if (!ready || scenario == Scenario::Comparison || deltaSeconds <= 0) return;
    age += deltaSeconds;
    float t = age - Arrival;
    if (t >= 0 && !released) release();
    if (scenario == Scenario::Tractor && t >= 0 && t < definition().buildup && t >= nextSpray) {
        // The nozzle's outward particles are from the game's own template.
        // Rotating the source around the tank illustrates Contaminate's sweep.
        float angle = t * 3.2f;
        emit("ToxinSpray", 12, std::cos(angle) * 8, -35 + std::sin(angle) * 8, angle);
        nextSpray = t + .12f;
    }
    if (scenario == Scenario::ToxinBuilding && t >= .1f && !delayedBurst) {
        emit("BombTruckHEBioExplosionTrailArms", 25); delayedBurst = true;
    }
    updateProps(); updateVolume();
    if (looping && t > definition().buildup + definition().linger + definition().dissipate + 2)
        replay();
    // Finished effects are engine-owned; keep our cleanup list small in long runs.
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](ParticleSystemID id) {
        return !TheParticleSystemManager->findParticleSystem(id);
    }), effects.end());
}
void setLooping(bool value) { looping = value; }
bool isLooping() { return looping; }
bool valid() { return healthy; }
} // namespace CloudPreviewSources
