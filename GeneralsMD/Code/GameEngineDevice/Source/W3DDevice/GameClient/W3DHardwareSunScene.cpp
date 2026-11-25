// Static-building ray scene: CPU world triangles are copied under the live-model lock.
// The renderer receives no W3D object, model, vertex or Drawable pointers.
#if defined(__linux__)
#include "W3DHardwareSunScene.h"
#include "Platform/HardwareRaySun.h"
#include "Common/GlobalData.h"
#include "Common/LogicThread.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameClient.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/scene.h"
#include "WW3D2/hlod.h"
#include "WW3D2/mesh.h"
#include "WW3D2/meshmdl.h"
#include "WW3D2/vertmaterial.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr size_t MaximumTriangles = 131072;
constexpr unsigned MaximumDepth = 32;
// A model pointer is not a content revision: several W3D callers mutate its arrays
// in place. Keep exact inputs and recheck them under g_liveModelLock. A camera-only
// frame then scans contiguous source data but does no transforms, triangle expansion,
// index generation, or full flattened-scene comparison/copy.
using MeshKey = std::pair<RenderObjClass *, Drawable *>;
struct MeshKeyHash {
    size_t operator()(const MeshKey &key) const {
        const size_t object = std::hash<RenderObjClass *>{}(key.first);
        const size_t owner = std::hash<Drawable *>{}(key.second);
        return object ^ (owner + 0x9e3779b9u + (object << 6) + (object >> 2));
    }
};
struct OwnerEntry {
    Drawable *drawable;
    uint64_t revision;
};
using Owners = std::unordered_map<const void *, OwnerEntry>;

struct ModelGeometry {
    std::vector<Vector3> vertices;
    std::vector<TriIndex> polygons;
    std::vector<unsigned char> vertexOpaque, polygonOpaque;
    uint64_t generation = 0, seen = 0;
};
struct CachedMesh {
    MeshModelClass *model = nullptr;
    uint64_t geometry = 0;
    Matrix3D transform;
    std::vector<float> positions;
    uint64_t seen = 0;
    bool complete = false;
};
std::unordered_map<MeshKey, CachedMesh, MeshKeyHash> meshCache;
// Instances can share model data, while each has its own world transform.
// Recheck the live model once per capture under g_liveModelLock. The generation
// then identifies the exact geometry and opacity used by every cached instance.
std::unordered_map<MeshModelClass *, ModelGeometry> modelCache;
uint64_t modelGeneration = 0;
uint64_t collection = 0;
// Pointer identity alone is not a live-owner proof. Compare the complete current
// ordered list under the live-model lock before reusing any address lookup.
struct OwnerIdentity {
    Drawable *drawable;
    const void *data;

    bool operator==(const OwnerIdentity &other) const {
        return drawable == other.drawable && data == other.data;
    }
};
std::vector<OwnerIdentity> ownerIdentities;
std::vector<OwnerIdentity> nextOwnerIdentities;
Owners ownerLookup;
uint64_t ownerRevision = 0;
std::unordered_map<MeshKey, uint64_t, MeshKeyHash> visited;

const Owners &currentOwners()
{
    nextOwnerIdentities.clear();
    bool changed = false;
    size_t index = 0;
    size_t prefix = 0;
    for (Drawable *draw = TheGameClient->firstDrawable(); draw; draw = draw->getNextDrawable(), ++index) {
        const OwnerIdentity identity = {draw, draw->getDrawableInfo()};
        if (!changed && (index >= ownerIdentities.size()
            || !(ownerIdentities[index] == identity))) {
            prefix = index;
            nextOwnerIdentities.assign(ownerIdentities.begin(), ownerIdentities.begin() + prefix);
            changed = true;
        }
        if (changed) nextOwnerIdentities.push_back(identity);
    }
    if (!changed) {
        if (index == ownerIdentities.size()) return ownerLookup;
        prefix = index;
        nextOwnerIdentities.assign(ownerIdentities.begin(), ownerIdentities.begin() + prefix);
    }

    // A prepend or removal can leave most of the roster unchanged. Keep its
    // lookup entries and reconcile only the range between the matching ends.
    // DrawableInfo is embedded in its Drawable, so each live owner has one key.
    size_t before = ownerIdentities.size();
    size_t after = nextOwnerIdentities.size();
    while (before > prefix && after > prefix
        && ownerIdentities[before - 1] == nextOwnerIdentities[after - 1]) {
        --before;
        --after;
    }
    const uint64_t revision = ++ownerRevision;
    for (size_t i = prefix; i < after; ++i) {
        const OwnerIdentity &owner = nextOwnerIdentities[i];
        ownerLookup.insert_or_assign(owner.data, OwnerEntry{owner.drawable, revision});
    }
    for (size_t i = prefix; i < before; ++i) {
        const auto entry = ownerLookup.find(ownerIdentities[i].data);
        if (entry != ownerLookup.end() && entry->second.revision != revision) ownerLookup.erase(entry);
    }
    ownerIdentities.swap(nextOwnerIdentities);
    return ownerLookup;
}

void clearIdentityCaches()
{
    ownerIdentities.clear();
    nextOwnerIdentities.clear();
    ownerLookup.clear();
    ownerRevision = 0;
    visited.clear();
}

bool firstVisit(RenderObjClass *object, Drawable *owner)
{
    const auto entry = visited.try_emplace(MeshKey(object, owner), collection);
    if (entry.second) return true;
    if (entry.first->second == collection) return false;
    entry.first->second = collection;
    return true;
}

struct BuildingMeshSnapshot {
    std::vector<MeshKey> meshes;
    std::map<const RenderObjClass *, DrawableID> completeRoots;
    std::string firstMesh;
    size_t triangles = 0;
    size_t reusedMeshes = 0;
    bool rebuilt = false;
    bool overflow = false;
};
struct Previous {
    std::vector<float> positions;
    std::vector<uint32_t> meshTriangleEnds;
    std::vector<uint32_t> buildingTriangleEnds;
    std::vector<MeshKey> meshes;
    std::map<const RenderObjClass *, DrawableID> completeRoots;
    uintptr_t map = 0;
    SceneClass *scene = nullptr;
    uint64_t revision = 0;
    uint64_t snapshotId = 0;
    bool valid = false;
    bool published = false;
} previous;

bool finite(const Vector3 &v) { return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z); }

// Peek_Animation reports only SINGLE_ANIM, and returns null for blended/combo
// motion. Use a protected member pointer (as CasterAnimationClock does) rather
// than interpreting a null animation pointer as proof of an unanimated hierarchy.
class StaticHierarchyProof : public HLodClass {
public:
    static bool BasePose(const HLodClass *object) {
        int Animatable3DObjClass::*mode = &StaticHierarchyProof::CurMotionMode;
        return object->*mode == NONE || object->*mode == BASE_POSE;
    }
};

bool vertexOpaque(MeshModelClass *model, int vertex)
{
    const VertexMaterialClass *material = model->Peek_Material(vertex);
    return !material || (std::isfinite(material->Get_Opacity()) && material->Get_Opacity() >= 0.999f);
}
bool polygonOpaque(MeshModelClass *model, int polygon)
{
    const ShaderClass shader = model->Get_Shader(polygon);
    return shader.Get_Alpha_Test() == ShaderClass::ALPHATEST_DISABLE
        && shader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE
        && shader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO;
}

bool collectMesh(MeshClass *mesh, Drawable *owner, BuildingMeshSnapshot &out)
{
    MeshModelClass *model = mesh->Peek_Model();
    if (!model || model->Get_Vertex_Count() <= 0 || model->Get_Polygon_Count() <= 0
        || model->Get_Pass_Count() != 1 || model->Get_Flag(MeshGeometryClass::SKIN)
        || !std::isfinite(mesh->Get_Alpha_Override()) || mesh->Get_Alpha_Override() < 0.999f
        || mesh->Is_Alpha() || mesh->Is_Translucent()) return false;
    const Vector3 *vertices = model->Get_Vertex_Array();
    const TriIndex *polygons = model->Get_Polygon_Array();
    if (!vertices || !polygons) return false;
    const Matrix3D transform = mesh->Get_Transform();
    const size_t vertexCount = size_t(model->Get_Vertex_Count());
    const size_t polygonCount = size_t(model->Get_Polygon_Count());
    // Uniform material/shader descriptions are common for building meshes. Checking
    // their one live value is exact and avoids per-face/per-vertex function calls.
    const bool uniformVertices = model->Get_Material_Array(0, false) == nullptr;
    const bool uniformPolygons = model->Get_Shader_Array(0, false) == nullptr;

    ModelGeometry &geometry = modelCache[model];
    if (geometry.seen != collection) {
        const size_t materialCount = uniformVertices ? 1 : vertexCount;
        const size_t shaderCount = uniformPolygons ? 1 : polygonCount;
        const auto unchanged = [&] {
            if (geometry.vertices.size() != vertexCount || geometry.polygons.size() != polygonCount
                || geometry.vertexOpaque.size() != materialCount || geometry.polygonOpaque.size() != shaderCount
                || std::memcmp(geometry.vertices.data(), vertices, vertexCount * sizeof(Vector3)) != 0
                || std::memcmp(geometry.polygons.data(), polygons, polygonCount * sizeof(TriIndex)) != 0)
                return false;
            for (size_t v = 0; v < materialCount; ++v)
                if (geometry.vertexOpaque[v] != vertexOpaque(model, int(v))) return false;
            for (size_t p = 0; p < shaderCount; ++p)
                if (geometry.polygonOpaque[p] != polygonOpaque(model, int(p))) return false;
            return true;
        };
        if (!unchanged()) {
            geometry.vertices.assign(vertices, vertices + vertexCount);
            geometry.polygons.assign(polygons, polygons + polygonCount);
            geometry.vertexOpaque.resize(materialCount);
            geometry.polygonOpaque.resize(shaderCount);
            for (size_t v = 0; v < materialCount; ++v) geometry.vertexOpaque[v] = vertexOpaque(model, int(v));
            for (size_t p = 0; p < shaderCount; ++p) geometry.polygonOpaque[p] = polygonOpaque(model, int(p));
            geometry.generation = ++modelGeneration;
        }
        geometry.seen = collection;
    }
    const MeshKey key(mesh, owner);
    CachedMesh &cached = meshCache[key];
    cached.seen = collection;
    if (cached.model != model || cached.geometry != geometry.generation || cached.transform != transform) {
        cached.model = model;
        cached.geometry = geometry.generation;
        cached.transform = transform;
        cached.positions.clear();
        cached.complete = true;
        for (size_t p = 0; p < polygonCount; ++p) {
            if (!geometry.polygonOpaque[uniformPolygons ? 0 : p]) { cached.complete = false; continue; }
            Vector3 world[3];
            bool opaque = true;
            for (int k = 0; k < 3; ++k) {
                const unsigned index = polygons[p][k];
                if (index >= vertexCount || !geometry.vertexOpaque[uniformVertices ? 0 : index]) { opaque = false; break; }
                Matrix3D::Transform_Vector(transform, vertices[index], &world[k]);
                if (!finite(world[k])) { opaque = false; break; }
            }
            if (!opaque) { cached.complete = false; continue; }
            Vector3 area;
            Vector3::Cross_Product(world[1] - world[0], world[2] - world[0], &area);
            if (!std::isfinite(area.Length2()) || area.Length2() <= 1.0e-12f) {
                cached.complete = false; continue;
            }
            if (out.triangles + cached.positions.size() / 9 >= MaximumTriangles) {
                out.overflow = true; return false;
            }
            for (const Vector3 &v : world) {
                cached.positions.push_back(v.X); cached.positions.push_back(v.Y); cached.positions.push_back(v.Z);
            }
        }
        out.rebuilt = true;
    } else {
        ++out.reusedMeshes;
    }
    if (out.triangles + cached.positions.size() / 9 > MaximumTriangles) {
        out.overflow = true; return false;
    }
    if (!cached.positions.empty()) {
        out.meshes.push_back(key);
        out.triangles += cached.positions.size() / 9;
        if (out.firstMesh.empty() && mesh->Get_Name()) out.firstMesh = mesh->Get_Name();
    }
    return cached.complete && !cached.positions.empty();
}

// The mapped pass poses each root before collection, holding the live-model
// lock across the depth capture. Only a complete, exactly selected hierarchy
// can replace its mapped draw. An uncertain subtree rejects the whole root.
bool collect(RenderObjClass *object, Drawable *owner, const Owners &owners,
    BuildingMeshSnapshot &out, unsigned depth)
{
    if (!object || out.overflow || depth > MaximumDepth) return false;
    if (depth) {
        // Child data can be a material override. Recognize a different owner
        // only by membership in the live DrawableInfo table.
        const auto found = owners.find(object->Get_User_Data());
        if (found != owners.end() && found->second.drawable != owner) return false;
    }
    if (!firstVisit(object, owner) || !object->Is_Not_Hidden_At_All()) return false;
    if (object->Class_ID() == RenderObjClass::CLASSID_MESH)
        return collectMesh(static_cast<MeshClass *>(object), owner, out);
    if (object->Class_ID() != RenderObjClass::CLASSID_HLOD) return false;
    HLodClass *lod = static_cast<HLodClass *>(object);
    const int level = lod->Get_LOD_Count() - 1;
    if (level < 0 || lod->Get_LOD_Level() != level || !StaticHierarchyProof::BasePose(lod)
        || !lod->Get_Lod_Model_Count(level)
        || (lod->Is_Sub_Objects_Match_LOD_Enabled() && lod->Get_Additional_Model_Count() > 0))
        return false;
    if (depth) lod->Pose_For_Render();
    const size_t before = out.triangles;
    for (int i = 0; i < lod->Get_Lod_Model_Count(level); ++i) {
        RenderObjClass *child = lod->Peek_Lod_Model(level, i);
        if (child && child->Class_ID() == RenderObjClass::CLASSID_OBBOX) continue;
        if (!collect(child, owner, owners, out, depth + 1)) return false;
    }
    for (int i = 0; i < lod->Get_Additional_Model_Count(); ++i)
        if (!collect(lod->Peek_Additional_Model(i), owner, owners, out, depth + 1)) return false;
    return out.triangles > before;
}

void invalidate(IDirect3DDevice9 *device, const char *reason)
{
    previous.published = false;
    meshCache.clear();
    modelCache.clear();
    clearIdentityCaches();
    HardwareRaySun::Invalidate(device);
    if (previous.valid) {
        std::fprintf(stderr, "HardwareRaySun: static buildings invalidated (%s), revision=%llu\n",
            reason, static_cast<unsigned long long>(++previous.revision));
        previous.valid = false;
        previous.positions.clear(); previous.meshes.clear();
        previous.meshTriangleEnds.clear();
        previous.buildingTriangleEnds.clear();
        previous.completeRoots.clear(); previous.map = 0; previous.scene = nullptr;
    }
}
}

void W3DHardwareSunPrepare(SceneClass *scene, RenderObjClass *const *roots, std::size_t count)
{
    previous.published = false;
    if (!HardwareRayTracing::Requested() && !HardwareRaySun::Requested()) return;
    IDirect3DDevice9 *device = DX8Wrapper::_Get_D3D_Device();
    // Unsupported or nested views do not disturb the last eligible main-view snapshot.
    // Available includes the global same-device activation proof (or explicit CLI override).
    if (!HardwareRaySun::Available(device)) return;
    if (!scene || (count && !roots) || !TheGlobalData || !TheGameClient || !TheTerrainRenderObject
        || !TheTerrainRenderObject->getMap() || !TheW3DShadowManager || !TheGlobalData->m_shadowMap
        || !TheGlobalData->m_useShadowVolumes || TheGlobalData->m_timeOfDay == TIME_OF_DAY_NIGHT) {
        invalidate(device, "no eligible main sun view"); return;
    }
    const uintptr_t map = reinterpret_cast<uintptr_t>(TheTerrainRenderObject->getMap());
    if (previous.map != map || previous.scene != scene) {
        meshCache.clear();
        modelCache.clear();
        clearIdentityCaches();
    }
    ++collection;
    BuildingMeshSnapshot current;
    Vector3 toSun;
    {
        std::unique_lock<std::recursive_mutex> liveModels(LogicThread::g_liveModelLock);
        const auto &owners = currentOwners();
        // The mapped pass supplies the roots it has culled and posed for this view.
        // Only a complete opaque building may replace its mapped draw.
        for (size_t i = 0; i < count && !current.overflow; ++i) {
            RenderObjClass *root = roots[i];
            if (!root || root->Get_Container()) continue;
            const auto found = owners.find(root->Get_User_Data());
            if (found == owners.end()) continue;
            Drawable *owner = found->second.drawable;
            if (!owner->isKindOf(KINDOF_STRUCTURE) || !owner->getShadowsEnabled()
                || owner->isDrawableEffectivelyHidden() || owner->getFullyObscuredByShroud()
                || owner->isNoSaveForDraw() || owner->getStealthLook() != STEALTHLOOK_NONE)
                continue;
            const float opacity = owner->getEffectiveOpacity();
            if (!std::isfinite(opacity) || opacity < 0.999f) continue;
            const size_t meshFirst = current.meshes.size();
            const size_t trianglesBefore = current.triangles;
            const size_t reusedBefore = current.reusedMeshes;
            const bool rebuiltBefore = current.rebuilt;
            if (collect(root, owner, owners, current, 0)) {
                current.completeRoots.emplace(root, owner->getID());
            } else {
                // The complete original raster caster remains in the map.
                current.meshes.resize(meshFirst);
                current.triangles = trianglesBefore;
                current.reusedMeshes = reusedBefore;
                current.rebuilt = rebuiltBefore;
                if (!meshFirst) current.firstMesh.clear();
            }
        }
        for (auto mesh = meshCache.begin(); mesh != meshCache.end();) {
            if (mesh->second.seen != collection) mesh = meshCache.erase(mesh);
            else ++mesh;
        }
        for (auto model = modelCache.begin(); model != modelCache.end();) {
            if (model->second.seen != collection) model = modelCache.erase(model);
            else ++model;
        }
        // Retain only this collection's nodes: no historical/dangling identities
        // accumulate across destruction, model replacement, or owner changes.
        for (auto node = visited.begin(); node != visited.end();) {
            if (node->second != collection) node = visited.erase(node);
            else ++node;
        }
        toSun = TheW3DShadowManager->getLightPosWorld(0);
    }
    // The native dispatch and depth merge have fixed cost. A small set
    // stays mapped when there are too few complete draws to replace.
    const size_t requiredCasters = HardwareRaySun::Requested() ? 1 : 4;
    if (!current.overflow && current.completeRoots.size() < requiredCasters) return;
    if (current.overflow || current.triangles == 0 || !finite(toSun) || toSun.Length2() <= 1.0e-12f) {
        invalidate(device, current.overflow ? "triangle limit" : "empty geometry or invalid sunlight"); return;
    }
    toSun.Normalize();
    if (toSun.Z <= 0.0f) { invalidate(device, "sun below horizon"); return; }
    const bool changed = !previous.valid || previous.map != map || previous.scene != scene
        || current.rebuilt || previous.meshes != current.meshes;
    const bool provenanceChanged = changed || previous.completeRoots != current.completeRoots;
    if (changed) {
        // This set is diagnostic only. Avoid rebuilding its nodes on every
        // unchanged frame; count unique accepted owners while flattening a change.
        std::set<Drawable *> buildings;
        previous.positions.clear();
        previous.meshTriangleEnds.clear();
        previous.buildingTriangleEnds.clear();
        previous.positions.reserve(current.triangles * 9);
        previous.meshTriangleEnds.reserve(current.meshes.size());
        previous.buildingTriangleEnds.reserve(current.completeRoots.size());
        Drawable *owner = nullptr;
        for (const MeshKey &key : current.meshes) {
            buildings.insert(key.second);
            if (owner && owner != key.second)
                previous.buildingTriangleEnds.push_back(uint32_t(previous.positions.size() / 9));
            owner = key.second;
            const std::vector<float> &positions = meshCache.find(key)->second.positions;
            previous.positions.insert(previous.positions.end(), positions.begin(), positions.end());
            previous.meshTriangleEnds.push_back(uint32_t(previous.positions.size() / 9));
        }
        previous.buildingTriangleEnds.push_back(uint32_t(previous.positions.size() / 9));
        previous.meshes.swap(current.meshes);
        previous.map = map; previous.scene = scene; previous.valid = true; ++previous.revision;
        std::fprintf(stderr, "HardwareRaySun: accepted %zu static buildings, %zu triangles, revision=%llu firstMesh=%s completeRoots=%zu reusedMeshes=%zu enabledRoots=%zu\n",
            buildings.size(), previous.positions.size() / 9,
            static_cast<unsigned long long>(previous.revision), current.firstMesh.c_str(),
            current.completeRoots.size(), current.reusedMeshes, count);
    }
    if (provenanceChanged) {
        previous.completeRoots.swap(current.completeRoots);
        ++previous.snapshotId;
        if (!previous.snapshotId) ++previous.snapshotId;
    }
    const float direction[3] = {toSun.X, toSun.Y, toSun.Z};
    const float strength = TheGlobalData->m_shadowMapStrength > 0.0f ? TheGlobalData->m_shadowMapStrength
        : (TheGlobalData->m_lightingStyle == LIGHTING_STYLE_REFORGED ? 0.52f : 0.45f);
    previous.published = HardwareRaySun::PrepareScene(device, previous.positions.data(), uint32_t(previous.positions.size() / 3),
        previous.revision, direction, strength, previous.meshTriangleEnds, previous.buildingTriangleEnds);
}

uint64_t W3DHardwareSunSnapshotId()
{
    return previous.published ? previous.snapshotId : 0;
}

std::size_t W3DHardwareSunCompleteCasterCount(uint64_t snapshotId)
{
    return previous.published && snapshotId != 0 && previous.snapshotId == snapshotId
        ? previous.completeRoots.size() : 0;
}

bool W3DHardwareSunHasCompleteCaster(const RenderObjClass *root, uint64_t snapshotId)
{
    return previous.published && snapshotId != 0 && previous.snapshotId == snapshotId
        && previous.completeRoots.find(root) != previous.completeRoots.end();
}
#endif
