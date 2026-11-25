// Experimental reflection geometry: opaque scene meshes and known terrain.
// Primary hits retain bounded texture detail; secondary albedo is sampled per triangle. Batched trees/bridge decks,
// particles, cutout/blended materials and procedural vertex effects are not exported.
#if defined(__linux__)
#include "W3DWaterPathScene.h"
#include "Platform/WaterPathCaptureScope.h"
#include "Platform/WaterPathModelDigest.h"
#include "Platform/WaterPathTerrainSnapshot.h"
#include "Platform/WaterPathStaticSpans.h"
#include "Platform/WaterPathSnapshotPolicy.h"
#include "Platform/WaterPathTerrainGroups.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "GameClient/Drawable.h"
#include "Common/KindOf.h"
#include "GameClient/GameClient.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ObserverCamera.h"
#include "GameLogic/PartitionManager.h"
#include "Common/PlayerList.h"
#include "scene.h"
#include "mesh.h"
#include "meshmdl.h"
#include "hlod.h"
#include "texture.h"
#include "vertmaterial.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <chrono>
#include <cstdlib>

// BaseType's legacy macros must not rewrite the standard algorithms below.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {
using WaterPathTracing::Triangle;
using WaterPathTracing::Vec3;
const size_t TriangleLimit = WaterPathTracing::MaximumTriangles;

Vec3 vector(const Vector3 &v) { return Vec3{v.X, v.Y, v.Z}; }
float linear(float value) { return std::pow(std::max(0.0f, std::min(value, 1.0f)), 2.2f); }

struct Hash {
    uint64_t value = UINT64_C(1469598103934665603);
    void Bytes(const void *data, size_t count) {
        const unsigned char *bytes = static_cast<const unsigned char *>(data);
        for (size_t i=0; i<count; ++i) { value ^= bytes[i]; value *= UINT64_C(1099511628211); }
    }
    template<class T> void Add(const T &v) { Bytes(&v, sizeof(v)); }
};

Vector3 rgb565(uint16_t bits) {
    return Vector3(float((bits >> 11) & 31)/31.0f, float((bits >> 5) & 63)/63.0f, float(bits & 31)/31.0f);
}

// Only the RGB block is needed: alpha-tested geometry is omitted before this sampler.
Vector3 pixel(const unsigned char *bytes, int pitch, D3DFORMAT format, unsigned x, unsigned y) {
    if (format == D3DFMT_DXT1 || format == D3DFMT_DXT3 || format == D3DFMT_DXT5) {
        const unsigned blockBytes = format == D3DFMT_DXT1 ? 8 : 16;
        const unsigned char *block = bytes + (y/4)*pitch + (x/4)*blockBytes + blockBytes-8;
        uint16_t c0, c1; uint32_t indices;
        std::memcpy(&c0,block,2); std::memcpy(&c1,block+2,2); std::memcpy(&indices,block+4,4);
        Vector3 colours[4] = {rgb565(c0),rgb565(c1),Vector3(0,0,0),Vector3(0,0,0)};
        if (c0>c1 || format!=D3DFMT_DXT1) {
            colours[2]=(colours[0]*2.0f+colours[1])/3.0f;
            colours[3]=(colours[0]+colours[1]*2.0f)/3.0f;
        } else colours[2]=(colours[0]+colours[1])*0.5f;
        return colours[(indices >> (2*((y&3)*4+(x&3)))) & 3];
    }
    const unsigned char *row = bytes+y*pitch;
    if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8) {
        const unsigned char *p=row+x*4;
        return Vector3(p[2]/255.0f,p[1]/255.0f,p[0]/255.0f);
    }
    if (format == D3DFMT_R8G8B8) {
        const unsigned char *p=row+x*3;
        return Vector3(p[2]/255.0f,p[1]/255.0f,p[0]/255.0f);
    }
    if (format == D3DFMT_R5G6B5 || format == D3DFMT_A1R5G5B5 || format == D3DFMT_X1R5G5B5
        || format == D3DFMT_A4R4G4B4) {
        uint16_t bits; std::memcpy(&bits,row+x*2,2);
        if (format == D3DFMT_R5G6B5) return rgb565(bits);
        if (format == D3DFMT_A4R4G4B4)
            return Vector3(float((bits>>8)&15)/15.0f,float((bits>>4)&15)/15.0f,float(bits&15)/15.0f);
        return Vector3(float((bits>>10)&31)/31.0f,float((bits>>5)&31)/31.0f,float(bits&31)/31.0f);
    }
    return Vector3(1,1,1);
}

struct TextureSample {
    IDirect3DTexture9 *resource = nullptr;
    unsigned width=0, height=0;
    std::vector<Vector3> colours;
    unsigned atlasOffset=unsigned(WaterPathTracing::MaximumPrimaryTexels);
    ~TextureSample() { if (resource) resource->Release(); }
    Vector3 Sample(float u, float v) const {
        if (!width || !height || !std::isfinite(u) || !std::isfinite(v)) return Vector3(1,1,1);
        u -= std::floor(u); v -= std::floor(v);
        unsigned x=std::min(unsigned(u*width),width-1), y=std::min(unsigned(v*height),height-1);
        return colours[y*width+x];
    }
};

// A static superset may omit camera planes only for the bounded, finite reflected
// ray envelope proven by CaptureScope. Keep its below-water rejection separately.
// Primary/normal offsets are 0.02 units, inside CaptureScope's one-unit guard.
bool staticSupersetSafe(const WaterPathTracing::View *view) {
    using namespace WaterPathTracing;
    if (!view || !Finite(view->origin) || !Finite(view->rayTopLeft) || !Finite(view->rayDx) || !Finite(view->rayDy)
        || !Finite(view->sunDirection) || !std::isfinite(view->waterLevel) || !(view->maxDistance>0)
        || !(view->secondaryDistance>=0) || !(view->shadowDistance>=0)
        || !std::isfinite(view->maxDistance+view->secondaryDistance+view->shadowDistance+2.0f)) return false;
    const Vec3 corners[]={view->rayTopLeft,Add(view->rayTopLeft,view->rayDx),
        Add(Add(view->rayTopLeft,view->rayDx),view->rayDy),Add(view->rayTopLeft,view->rayDy)};
    for (unsigned i=0;i<4;++i) {
        if (!Finite(corners[i]) || !(corners[i].z>1e-6f)) return false;
        const Vec3 normal=Cross(corners[i],corners[(i+1)%4]);
        if (!Finite(normal) || !(Dot(normal,normal)>0) || !std::isfinite(Dot(normal,normal))) return false;
        const double crossing=(double(view->waterLevel)-view->origin.z)/corners[i].z;
        if (!(crossing>=0) || !std::isfinite(crossing)) return false;
        const Vec3 point={float(view->origin.x+corners[i].x*crossing),
            float(view->origin.y+corners[i].y*crossing),view->waterLevel};
        if (!Finite(point)) return false;
    }
    return true;
}

struct RigidAppearance {
    uint64_t modelRevision = 0;
    float visibility = 0;
    bool sampleTextures = false;

    bool Matches(const RigidAppearance &other) const {
        return modelRevision && modelRevision == other.modelRevision
            && visibility == other.visibility && sampleTextures == other.sampleTextures;
    }
};

struct MeshInstance {
    MeshClass *mesh;
    MeshModelClass *model;
    Matrix3D transform;
    std::vector<Vector3> posed;
    float visibility;
    uint64_t fingerprint=0;
    bool staticGeometry=false, inScope=true;
    RigidAppearance appearance;
    Drawable *owner=nullptr;
};

// Borrowed only during a synchronous model operation. Null arrays mean the live single binding,
// exactly as MeshMatDescClass's per-element getters do; create=false never changes the model.
struct ModelBindings {
    VertexMaterialClass *const *Materials=nullptr;
    TextureClass *const *Textures=nullptr;
    const ShaderClass *Shaders=nullptr;
    VertexMaterialClass *SingleMaterial=nullptr;
    TextureClass *SingleTexture=nullptr;
    ShaderClass SingleShader;
    explicit ModelBindings(MeshModelClass *model) {
        if(auto *array=model->Get_Material_Array(0,false))Materials=array->Get_Array();
        else SingleMaterial=model->Peek_Single_Material();
        if(auto *array=model->Get_Texture_Array(0,0,false))Textures=array->Get_Array();
        else SingleTexture=model->Peek_Single_Texture();
        Shaders=model->Get_Shader_Array(0,false);
        if(!Shaders)SingleShader=model->Get_Shader(0);
    }
    VertexMaterialClass *Material(int vertex) const { return Materials?Materials[vertex]:SingleMaterial; }
    TextureClass *Texture(int polygon) const { return Textures?Textures[polygon]:SingleTexture; }
    ShaderClass Shader(int polygon) const { return Shaders?Shaders[polygon]:SingleShader; }
};

// Everything read from the shared model by the old instance fingerprint. Posed skin
// positions, transform, visibility and mesh identity remain per instance below.
WaterPathModelDigest::Digest modelDigestReference(MeshModelClass *model) {
    WaterPathModelDigest::Hash digest;
    const bool skin = model->Get_Flag(MeshGeometryClass::SKIN);
    digest.Add(skin);
    digest.Add(model->Get_Vertex_Count());
    digest.Add(model->Get_Polygon_Count());
    // Skin geometry is hashed from each instance's world-posed vertices, as before.
    if (!skin)
        digest.Bytes(model->Get_Vertex_Array(), size_t(model->Get_Vertex_Count()) * sizeof(Vector3));
    digest.Bytes(model->Get_Polygon_Array(), size_t(model->Get_Polygon_Count()) * sizeof(TriIndex));
    const Vector2 *uv = model->Get_UV_Array();
    digest.Add(uv != nullptr);
    if (uv)
        digest.Bytes(uv, size_t(model->Get_Vertex_Count()) * sizeof(Vector2));
    const unsigned *colours = model->Get_DCG_Array(0);
    digest.Add(colours != nullptr);
    if (colours)
        digest.Bytes(colours, size_t(model->Get_Vertex_Count()) * sizeof(unsigned));
    for (int v = 0; v < model->Get_Vertex_Count(); ++v) {
        const VertexMaterialClass *material = model->Peek_Material(v);
        digest.Add(material != nullptr);
        if (material)
            digest.Add(material->Get_CRC());
    }
    for (int p = 0; p < model->Get_Polygon_Count(); ++p) {
        digest.Add(model->Get_Shader(p).Get_Bits());
        TextureClass *texture = model->Peek_Texture(p);
        digest.Add(texture);
        if (texture) {
            digest.Add(texture->Is_Initialized());
            digest.Add(texture->Peek_D3D_Texture());
        }
    }
    return {digest.value, digest.bytes};
}

template<class Sink> void modelFields(MeshModelClass *model,Sink &digest) {
    const bool skin = model->Get_Flag(MeshGeometryClass::SKIN);
    digest.Add(skin);
    digest.Add(model->Get_Vertex_Count());
    digest.Add(model->Get_Polygon_Count());
    // Skin geometry is hashed from each instance's world-posed vertices, as before.
    if (!skin)
        digest.Bytes(model->Get_Vertex_Array(), size_t(model->Get_Vertex_Count()) * sizeof(Vector3));
    digest.Bytes(model->Get_Polygon_Array(), size_t(model->Get_Polygon_Count()) * sizeof(TriIndex));
    const Vector2 *uv = model->Get_UV_Array();
    digest.Add(uv != nullptr);
    if (uv)
        digest.Bytes(uv, size_t(model->Get_Vertex_Count()) * sizeof(Vector2));
    const unsigned *colours = model->Get_DCG_Array(0);
    digest.Add(colours != nullptr);
    if (colours)
        digest.Bytes(colours, size_t(model->Get_Vertex_Count()) * sizeof(unsigned));
    const ModelBindings bindings(model);
    const VertexMaterialClass *lastMaterial=nullptr;unsigned long lastCRC=0;bool materialRead=false;
    for(int v=0;v<model->Get_Vertex_Count();++v) {
        const auto *material=bindings.Material(v);
        digest.Add(material!=nullptr);
        if(material) {
            if(!materialRead || material!=lastMaterial){lastCRC=material->Get_CRC();lastMaterial=material;materialRead=true;}
            digest.Add(lastCRC);
        }
    }
    TextureClass *lastTexture=nullptr;bool textureRead=false,initialized=false;
    IDirect3DTexture9 *resource=nullptr;
    for(int p=0;p<model->Get_Polygon_Count();++p) {
        digest.Add(bindings.Shader(p).Get_Bits());
        TextureClass *texture=bindings.Texture(p);digest.Add(texture);
        if(texture) {
            if(!textureRead || texture!=lastTexture){initialized=texture->Is_Initialized();resource=texture->Peek_D3D_Texture();lastTexture=texture;textureRead=true;}
            digest.Add(initialized);digest.Add(resource);
        }
    }
}

WaterPathModelDigest::Digest modelDigest(MeshModelClass *model,bool verify,WaterPathModelDigest::ExactCache *proof) {
    WaterPathModelDigest::Digest result;
    if(proof)result=proof->Get(model,[&](auto &sink){modelFields(model,sink);});
    else {WaterPathModelDigest::Hash hash;modelFields(model,hash);result={hash.value,hash.bytes};}
    if(verify) {
        const auto original=modelDigestReference(model);
        if(original.value!=result.value || original.bytes!=result.bytes) {
            std::fprintf(stderr,"[water-path-digest] live binding stream mismatch; using original digest\n");
            if(proof){++proof->stats.mismatches;proof->Clear();}
            return original;
        }
    }
    return result;
}

bool eligible(RenderObjClass *object, RenderObjClass *water, Drawable *draw) {
    if (!object || object == water || !object->Is_Not_Hidden_At_All()) return false;
    if (draw && (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud()
        || draw->isNoSaveForDraw() || draw->getEffectiveOpacity()<0.999f))
        return false;
    return !TheInGameUI || !static_cast<W3DInGameUI *>(TheInGameUI)->isWorldSpaceUI(object,draw);
}

struct ReleaseMeshReference {
    template<class T> void operator()(T *value) const {value->Release_Ref();}
};

struct MeshTriangles {
    uint64_t fingerprint=0;
    bool valid=false, moving=false, packetActive=false;
    // Retaining both objects prevents address recycling while this entry is active.
    std::unique_ptr<MeshClass,ReleaseMeshReference> mesh;
    std::unique_ptr<MeshModelClass,ReleaseMeshReference> model;
    Drawable *owner=nullptr;
    DrawableID ownerID{};

    bool Matches(MeshClass *liveMesh, MeshModelClass *liveModel, Drawable *liveOwner) const {
        return mesh.get()==liveMesh && model.get()==liveModel && owner==liveOwner
            && ownerID==(liveOwner ? liveOwner->getID() : DrawableID{});
    }
    void Bind(MeshClass *liveMesh, MeshModelClass *liveModel, Drawable *liveOwner) {
        liveMesh->Add_Ref();mesh.reset(liveMesh);
        liveModel->Add_Ref();model.reset(liveModel);
        owner=liveOwner;ownerID=liveOwner ? liveOwner->getID() : DrawableID{};
    }
    std::vector<Triangle> triangles;
    std::vector<WaterPathTracing::PrimaryMaterial> materials;
    RigidAppearance appearance;
    std::vector<TriIndex> rigidIndices;
    size_t skippedAlpha=0;
};

void collect(RenderObjClass *object, RenderObjClass *water, Drawable *draw,
    std::vector<MeshInstance> &meshes, Hash &hash, std::set<MeshClass *> &seen,
    const WaterPathTracing::CaptureScope *scope, WaterPathModelDigest::RefreshMemo &models,
    float visibility = 1.0f, bool hardwareStatic = false, std::map<MeshClass *,MeshTriangles> *meshCache = nullptr,
    bool ancestorAnimated = false, float waterLevel = 0) {
    if (!eligible(object,water,draw)) return;
    bool animated=ancestorAnimated;
    if (object->Peek_Animation()) {
        animated |= object->Class_ID()!=RenderObjClass::CLASSID_HLOD ||
            !static_cast<HLodClass *>(object)->Is_Animation_Complete();
    }
    if (object->Class_ID()==RenderObjClass::CLASSID_MESH) {
        MeshClass *mesh=static_cast<MeshClass *>(object);
        if (seen.count(mesh)) return;
        MeshModelClass *model=mesh->Peek_Model();
        if (!model || !model->Get_Vertex_Count() || !model->Get_Polygon_Count()
            || model->Get_Pass_Count()<1 || mesh->Get_Alpha_Override()<0.999f) return;
        seen.insert(mesh);
        // Check live identity before a retained moving classification can affect admission.
        auto cached=meshCache ? meshCache->find(mesh) : std::map<MeshClass *,MeshTriangles>::iterator{};
        if (meshCache && cached!=meshCache->end() && !cached->second.Matches(mesh,model,draw)) {
            meshCache->erase(cached);
            cached=meshCache->end();
        }
        const bool moving=meshCache && cached!=meshCache->end() && cached->second.moving;
        MeshInstance instance{mesh,model,mesh->Get_Transform(),{},visibility};
        instance.owner=draw;
        instance.staticGeometry=hardwareStatic && draw && draw->isKindOf(KINDOF_STRUCTURE) && !animated &&
            !model->Get_Flag(MeshGeometryClass::SKIN) && !moving;
        if (model->Get_Flag(MeshGeometryClass::SKIN)) {
            if (!mesh->Get_Container() || !mesh->Get_Container()->Get_HTree()) return;
            const auto poseStart=models.Timing()?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
            instance.posed.resize(model->Get_Vertex_Count());
            mesh->Get_Deformed_Vertices(instance.posed.data());
            if(models.Timing())models.stats.poseNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-poseStart).count();
            if (scope) {
                Vec3 low=vector(instance.posed[0]), high=low;
                for (const Vector3 &point:instance.posed) {
                    low=WaterPathTracing::Minimum(low,vector(point));
                    high=WaterPathTracing::Maximum(high,vector(point));
                }
                if (!scope->Matches(low,high)) return;
            }
        } else if (scope) {
            // Transform validation above updates hierarchy transforms. Ordinary
            // meshes use W3D's world bounds; skins need their actual posed bounds.
            const AABoxClass &box=mesh->Get_Bounding_Box();
            const Vec3 low=vector(box.Center-box.Extent),high=vector(box.Center+box.Extent);
            instance.inScope=scope->Matches(low,high);
            if (instance.staticGeometry) {
                if (high.z<waterLevel-0.05f) return;
            } else if (!instance.inScope) return;
        }
        uint64_t modelRevision = 0;
        const uint64_t shared = models.Get(model, [&]() {
            return modelDigest(model,models.Verifying(),models.Proof());
        }, &modelRevision);
        if (instance.posed.empty()) {
            instance.appearance = {modelRevision, visibility, !hardwareStatic || instance.inScope};
        }
        instance.fingerprint = WaterPathModelDigest::Instance(shared, mesh, model,
            &instance.transform, sizeof(instance.transform), visibility,
            instance.posed.data(), instance.posed.size() * sizeof(Vector3));
        // A new owner must refresh the packet even if its geometry is identical.
        Hash ownership;ownership.Add(instance.fingerprint);ownership.Add(draw);
        ownership.Add(draw ? draw->getID() : DrawableID{});
        instance.fingerprint=ownership.value;
        if (hardwareStatic) {
            // Scope admission changes appearance sampling, never the retained static positions.
            Hash appearance; appearance.Add(instance.fingerprint); appearance.Add(instance.inScope);
            instance.fingerprint=appearance.value;
            hash.Add(instance.staticGeometry);
        }
        hash.Add(instance.fingerprint);
        meshes.push_back(std::move(instance));
        return;
    }
    if (object->Class_ID()==RenderObjClass::CLASSID_HLOD) {
        HLodClass *lod=static_cast<HLodClass *>(object);
        const int level=lod->Get_LOD_Level();
        for (int i=0; i<lod->Get_Lod_Model_Count(level); ++i)
            collect(lod->Peek_Lod_Model(level, i), water, draw, meshes, hash, seen, scope, models, visibility, hardwareStatic, meshCache, animated, waterLevel);
        for (int i=0; i<lod->Get_Additional_Model_Count(); ++i)
            collect(lod->Peek_Additional_Model(i), water, draw, meshes, hash, seen, scope, models, visibility, hardwareStatic, meshCache, animated, waterLevel);
        return;
    }
    for (int i=0; i<object->Get_Num_Sub_Objects(); ++i) {
        RenderObjClass *child=object->Get_Sub_Object(i);
        collect(child, water, draw, meshes, hash, seen, scope, models, visibility, hardwareStatic, meshCache, animated, waterLevel);
        if (child) child->Release_Ref();
    }
}

unsigned char shroudAt(W3DShroud *shroud,float x,float y) {
    if (!shroud || !shroud->getShroudTexture() || shroud->getCellWidth()<=0 || shroud->getCellHeight()<=0)
        return 255;
    return shroud->getShroudLevel(int(std::floor(x/shroud->getCellWidth())),int(std::floor(y/shroud->getCellHeight())));
}
}

struct W3DWaterPathScene::State {
    uint64_t sceneHash=0, terrainHash=0;
    uint64_t revision=0;
    bool hasScene=false, hasTerrain=false;
    Capture requestedCapture=Capture::SoftwarePath;
    size_t staticTriangleCount=0,staticTerrainTriangleCount=0;
    bool staticPrefixValid=false;
    uint64_t staticPrefixTerrainHash=0, staticPrefixReuses=0, staticPrefixBuilds=0;
    uint64_t staticTerrainIndexSlotsSkipped=0,terrainPrefixReuses=0,terrainPrefixTrianglesRetained=0;
    WaterPathTracing::TerrainSnapshotProof terrainProof;
    WaterPathTracing::NativeSnapshotPolicy snapshotPolicy;
    WaterPathTracing::TerrainGroups terrainGroups;
    bool terrainGroupsValid=false;
    uint64_t terrainGroupTests=0,terrainGroupSkips=0,terrainGroupAccepts=0,terrainGroupChecks=0,terrainGroupMismatches=0;
    WaterPathModelDigest::ExactCache modelProof;
    const void *modelOwnerScene=nullptr,*modelOwnerMap=nullptr;
    std::vector<unsigned char> terrainVisibility;
    uint64_t terrainProofMismatches=0,packetVerifications=0,packetMismatches=0;
    uint64_t rigidReuses=0,rigidChecks=0,rigidMismatches=0;
    std::vector<WaterPathTracing::StaticSpan> staticSpans;
    uint64_t staticSpanPatchFrames=0,staticTriangleRowsPatched=0,staticMaterialRowsPatched=0,staticMeshRowsRetained=0;
    std::vector<Triangle> triangles, terrainTriangles;
    WaterPathTracing::PrimaryMaterialPacket materials;
    size_t materialsCommittedTexels=0;
    std::map<IDirect3DTexture9 *,std::unique_ptr<TextureSample>> textures;
    // When the atlas is full, retain only the last uncached source for secondary
    // centroid sampling. This bounds retained source pixels without changing that fallback.
    std::unique_ptr<TextureSample> transientTexture;
    struct TextureStatistics { uint64_t Calls=0,Hits=0,Decodes=0,Pixels=0;double DecodeMs=0; } textureStats;
    bool profileTextures=false;
    std::map<MeshClass *,MeshTriangles> meshTriangles;
    unsigned failure=0;
    std::vector<ObjectShroudStatus> propShroud;

    bool Fail(unsigned code,const char *reason) {
        if (failure!=code) std::fprintf(stderr,"WaterPathTracing: %s; using Hybrid fallback\n",reason);
        failure=code;
        return false;
    }

    const TextureSample *Texture(TextureClass *texture) {
        ++textureStats.Calls;
        if (!texture || !texture->Is_Initialized()) return nullptr;
        IDirect3DTexture9 *resource=texture->Peek_D3D_Texture();
        if (!resource) return nullptr;
        auto found=textures.find(resource);
        if (found!=textures.end()){++textureStats.Hits;return found->second.get();}
        if(transientTexture && transientTexture->resource==resource){++textureStats.Hits;return transientTexture.get();}
        const auto decodeStart=profileTextures?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        struct DecodeTimer {
            State &state;std::chrono::steady_clock::time_point start;
            ~DecodeTimer(){if(state.profileTextures)state.textureStats.DecodeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
        } decodeTimer{*this,decodeStart};
        ++textureStats.Decodes;
        std::unique_ptr<TextureSample> sample(new TextureSample);
        sample->resource=resource; resource->AddRef();
        unsigned level=0;
        D3DSURFACE_DESC description={};
        while (level+1<resource->GetLevelCount()) {
            if (resource->GetLevelDesc(level,&description)!=D3D_OK) break;
            if (description.Width<=64 && description.Height<=64) break;
            ++level;
        }
        D3DLOCKED_RECT locked={};
        if (resource->GetLevelDesc(level,&description)==D3D_OK
            && !(description.Usage&D3DUSAGE_RENDERTARGET)
            && resource->LockRect(level,&locked,nullptr,D3DLOCK_READONLY)==D3D_OK) {
            sample->width=std::min(description.Width,64u);
            sample->height=std::min(description.Height,64u);
            sample->colours.resize(sample->width*sample->height);
            textureStats.Pixels+=sample->colours.size();
            for (unsigned y=0; y<sample->height; ++y)
                for (unsigned x=0; x<sample->width; ++x)
                    sample->colours[y*sample->width+x]=pixel(static_cast<const unsigned char *>(locked.pBits),
                        locked.Pitch,description.Format,x*description.Width/sample->width,y*description.Height/sample->height);
            resource->UnlockRect(level);
        }
        const size_t count=WaterPathTracing::PrimaryTextureTexelCount(sample->width,sample->height);
        if(count && count<=WaterPathTracing::MaximumPrimaryTexels-materials.texels.size()) {
            sample->atlasOffset=unsigned(materials.texels.size());
            std::vector<WaterPathTracing::Float4> level;
            level.reserve(sample->colours.size());
            for(const Vector3 &colour:sample->colours)
                level.push_back({linear(colour.X),linear(colour.Y),linear(colour.Z),1});
            materials.texels.insert(materials.texels.end(),level.begin(),level.end());
            unsigned width=sample->width,height=sample->height;
            while(width>1||height>1) {
                const unsigned nextWidth=std::max(1u,width/2),nextHeight=std::max(1u,height/2);
                std::vector<WaterPathTracing::Float4> next(size_t(nextWidth)*nextHeight);
                for(unsigned y=0;y<nextHeight;++y)for(unsigned x=0;x<nextWidth;++x) {
                    // Average the complete source area. A 3-to-1 or 5-to-2
                    // reduction must retain the last source row/column too.
                    const double left=double(x)*width/nextWidth,right=double(x+1)*width/nextWidth;
                    const double top=double(y)*height/nextHeight,bottom=double(y+1)*height/nextHeight;
                    double rgb[3]={},weight=0;
                    for(unsigned sy=unsigned(std::floor(top));sy<std::min(height,unsigned(std::ceil(bottom)));++sy)
                        for(unsigned sx=unsigned(std::floor(left));sx<std::min(width,unsigned(std::ceil(right)));++sx) {
                            const double area=(std::min(right,double(sx+1))-std::max(left,double(sx)))
                                *(std::min(bottom,double(sy+1))-std::max(top,double(sy)));
                            const auto &texel=level[size_t(sy)*width+sx];
                            rgb[0]+=texel.x*area;rgb[1]+=texel.y*area;rgb[2]+=texel.z*area;weight+=area;
                        }
                    const auto unit=[&](unsigned channel) {
                        return float(std::max(0.0,std::min(1.0,rgb[channel]/weight)));
                    };
                    next[size_t(y)*nextWidth+x]={unit(0),unit(1),unit(2),1};
                }
                materials.texels.insert(materials.texels.end(),next.begin(),next.end());
                level.swap(next);width=nextWidth;height=nextHeight;
            }
        } else {
            // Disabled primary material uses the existing secondary albedo. Do not
            // grow a second unbounded cache of source textures after reaching the cap.
            transientTexture=std::move(sample);
            return transientTexture.get();
        }
        const TextureSample *result=sample.get();
        textures.emplace(resource,std::move(sample));
        return result;
    }
};

W3DWaterPathScene::W3DWaterPathScene() : m_state(new State) {}
W3DWaterPathScene::~W3DWaterPathScene() = default;

const std::vector<Triangle> &W3DWaterPathScene::Triangles() const { return m_state->triangles; }
const WaterPathTracing::PrimaryMaterialPacket &W3DWaterPathScene::Materials() const { return m_state->materials; }
uint64_t W3DWaterPathScene::Revision() const { return m_state->revision; }
size_t W3DWaterPathScene::StaticTriangleCount() const { return m_state->staticTriangleCount; }
W3DWaterPathScene::Capture W3DWaterPathScene::RequestedCapture() const { return m_state->requestedCapture; }

void W3DWaterPathScene::takeReadsForRender(BaseHeightMapRenderObjClass *terrain)
{
    m_state->propShroud.clear();
    const W3DPropBuffer *props=terrain?terrain->getPropBuffer():nullptr;
    if (props) for (int i=0;i<props->getPropCount();++i) {
        const TProp *prop=props->getProp(i);
        ObjectShroudStatus status=OBJECTSHROUD_CLEAR;
        if (prop && ThePlayerList && ThePartitionManager)
            status=ThePartitionManager->getPropShroudStatusForPlayer(TheObserverCamera.getShroudPlayerIndex(),&prop->location);
        m_state->propShroud.push_back(status);
    }
}

bool W3DWaterPathScene::Update(SceneClass *scene, BaseHeightMapRenderObjClass *terrain, RenderObjClass *water,
    const WaterPathTracing::View *view, Capture capture)
{
    bool hardwareStatic=capture==Capture::HardwarePath && staticSupersetSafe(view);
    // Exact snapshots still apply when the native capture needs camera-scoped geometry.
    const bool useTerrainSnapshot=capture!=Capture::SoftwarePath;
    if (!scene || !terrain || !terrain->getMap()) {
        m_state->meshTriangles.clear();m_state->modelOwnerScene=nullptr;m_state->modelOwnerMap=nullptr;
        m_state->hasScene=false;m_state->staticPrefixValid=false;
        m_state->modelProof.Clear();m_state->snapshotPolicy.Reset();return m_state->Fail(1,"scene or terrain is unavailable");
    }
    const auto capacityFailure = [&](unsigned code, const char *reason) {
        if (!hardwareStatic) return m_state->Fail(code,reason);
        // Retry the old scoped combined native packet before considering any visual fallback.
        const bool ready=Update(scene,terrain,water,view,Capture::ScopedHardwarePath);
        if (ready) m_state->requestedCapture=capture;
        return ready;
    };
    WorldHeightMap *map=terrain->getMap();
    const int width=map->getXExtent(), height=map->getYExtent(), border=map->getBorderSizeInline();
    if (width<2 || height<2 || !map->getDataPtr())
        return m_state->Fail(2,"terrain has no usable height data");
    // The production capture is scoped; unscoped callers retain the original limit.
    if (!view && uint64_t(width-1)*uint64_t(height-1)*2>TriangleLimit)
        return m_state->Fail(3,"terrain exceeds the experimental triangle limit");
    std::unique_ptr<WaterPathTracing::CaptureScope> scope;
    if (view) {
        using Content=WaterPathTracing::CaptureScope::Content;
        scope.reset(new WaterPathTracing::CaptureScope(*view,
            capture==Capture::HardwareReflection ? Content::Primary : Content::FullTransport));
    }
    const bool verify = std::getenv("ZH_WATER_PATH_VERIFY")
        && std::strcmp(std::getenv("ZH_WATER_PATH_VERIFY"), "1") == 0;
    const bool profile = std::getenv("ZH_WATER_PROFILE") != nullptr;
    const bool useModelProof=useTerrainSnapshot;
    if(m_state->modelOwnerScene!=scene || m_state->modelOwnerMap!=map) {
        m_state->meshTriangles.clear();
        m_state->hasScene=false;
        m_state->staticPrefixValid=false;
    }
    if(!useModelProof || m_state->modelOwnerScene!=scene || m_state->modelOwnerMap!=map)m_state->modelProof.Clear();
    m_state->modelOwnerScene=scene;m_state->modelOwnerMap=map;
    if(useModelProof)m_state->modelProof.BeginRefresh();
    struct ProofScope {WaterPathModelDigest::ExactCache *cache;~ProofScope(){if(cache)cache->EndRefresh();}} proofScope{useModelProof?&m_state->modelProof:nullptr};
    m_state->textureStats={};m_state->profileTextures=profile || verify;
    WaterPathModelDigest::RefreshMemo models(verify,profile || verify,useModelProof?&m_state->modelProof:nullptr); // Deliberately discarded at the end of this Update.
    struct DigestReport {
        WaterPathModelDigest::RefreshMemo &models;
        State::TextureStatistics &textures;
        bool enabled;
        std::chrono::steady_clock::time_point started, fingerprintDone{}, terrainDone{}, collectDone{},generationStarted{},generationDone{};
        void Mark(std::chrono::steady_clock::time_point & phase) {
            if (enabled)
                phase = std::chrono::steady_clock::now();
        }
        ~DigestReport() {
            if (!enabled)
                return;
            const auto &s = models.stats;
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = [](auto a, auto b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            const bool fingerprinted = fingerprintDone != std::chrono::steady_clock::time_point{};
            const bool terrain = terrainDone != std::chrono::steady_clock::time_point{};
            const bool collected = collectDone != std::chrono::steady_clock::time_point{};
            std::fprintf(stderr, "[water-path-model] models=%zu hits=%zu hashed_bytes=%zu saved_bytes=%zu verified=%zu exact_mismatches=%zu verification_bytes=%zu digest_xor=%016llx terrain_fingerprint_ms=%.4f terrain_build_ms=%.4f collect_ms=%.4f export_ms=%.4f update_ms=%.4f\n",
                s.misses, s.hits, s.hashedBytes, s.savedBytes, s.verified, s.mismatches, s.verificationBytes,
                (unsigned long long) s.digestXor,
                elapsed(started, fingerprinted ? fingerprintDone : now), terrain ? elapsed(fingerprintDone, terrainDone) : 0,
                collected ? elapsed(terrainDone, collectDone) : 0, collected ? elapsed(collectDone, now) : 0,
                elapsed(started, now));
            const bool generated=generationStarted!=std::chrono::steady_clock::time_point{};
            const bool finishedGeneration=generationDone!=std::chrono::steady_clock::time_point{};
            std::fprintf(stderr,"[water-path-detail] model_digest_ms=%.4f skin_pose_ms=%.4f texture_calls=%llu texture_hits=%llu texture_decodes=%llu decoded_pixels=%llu texture_decode_ms=%.4f cache_prepare_ms=%.4f generate_ms=%.4f commit_ms=%.4f\n",
                double(s.computeNs)*1e-6,double(s.poseNs)*1e-6,(unsigned long long)textures.Calls,(unsigned long long)textures.Hits,
                (unsigned long long)textures.Decodes,(unsigned long long)textures.Pixels,textures.DecodeMs,
                generated?elapsed(collectDone,generationStarted):0,generated?elapsed(generationStarted,finishedGeneration?generationDone:now):0,
                finishedGeneration?elapsed(generationDone,now):0);
            if(auto *proof=models.Proof())std::fprintf(stderr,"[water-path-exact-model] hits=%zu misses=%zu compared_bytes=%zu stored_bytes=%zu bypasses=%zu mismatches=%zu\n",
                proof->stats.hits,proof->stats.misses,proof->stats.comparedBytes,proof->StoredBytes(),proof->stats.bypasses,proof->stats.mismatches);
        }
    } digestReport{models, m_state->textureStats, profile || verify,
        (profile || verify) ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}};
    Hash ground;
    std::vector<unsigned char> scopedVisibility;
    auto &visibility=useTerrainSnapshot ? m_state->terrainVisibility : scopedVisibility;
    W3DShroud *shroud=terrain->getShroud();
    WaterPathTracing::TerrainSnapshotKey terrainKey;
    std::vector<unsigned char> shroudCells;
    size_t heightBytes=size_t(width)*height,flipBytes=0;
    const unsigned char *flipData=nullptr;
    bool terrainRetained=false;
    if (useTerrainSnapshot) {
        terrainKey.Map=map;terrainKey.Shroud=shroud;terrainKey.Width=width;terrainKey.Height=height;terrainKey.Border=border;
        terrainKey.HasView=view!=nullptr;terrainKey.WaterLevel=view ? view->waterLevel : 0;
        terrainKey.FlipRowBytes=map->getFlipStateRowBytes();terrainKey.TerrainCellSize=MAP_XY_FACTOR;
        flipData=map->getFlipStateData();
        flipBytes=flipData ? size_t(terrainKey.FlipRowBytes)*height : 0;
        terrainKey.HasShroud=shroud && shroud->getShroudTexture() && shroud->getCellWidth()>0 && shroud->getCellHeight()>0;
        if (terrainKey.HasShroud) {
            terrainKey.CellColumns=shroud->getNumShroudCellsX();terrainKey.CellRows=shroud->getNumShroudCellsY();
            terrainKey.CellWidth=shroud->getCellWidth();terrainKey.CellHeight=shroud->getCellHeight();
            shroudCells.resize(size_t(terrainKey.CellColumns)*terrainKey.CellRows);
            for(int y=0;y<terrainKey.CellRows;++y)for(int x=0;x<terrainKey.CellColumns;++x)
                shroudCells[size_t(y)*terrainKey.CellColumns+x]=shroud->getShroudLevel(x,y);
        }
        terrainRetained=m_state->terrainProof.Matches(terrainKey,map->getDataPtr(),heightBytes,flipData,flipBytes,shroudCells);
        if (terrainRetained) {ground.value=m_state->terrainProof.Digest;++m_state->terrainProof.Reuses;}
    }
    if (!terrainRetained || verify) {
        Hash fresh;
        fresh.Add(map);fresh.Add(width);fresh.Add(height);fresh.Add(border);fresh.Add(view!=nullptr);
        if(view)fresh.Add(view->waterLevel);
        fresh.Bytes(map->getDataPtr(),heightBytes);
        std::vector<unsigned char> reference;
        auto &current=verify && terrainRetained ? reference : visibility;
        if(useTerrainSnapshot && !verify)WaterPathTracing::ExpandTerrainShroud(terrainKey,shroudCells,current);
        else {
            current.resize(heightBytes);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x)
                current[size_t(y)*width+x]=shroudAt(shroud,(x-border)*MAP_XY_FACTOR,(y-border)*MAP_XY_FACTOR);
        }
        for(int y=0;y+1<height;++y)for(int x=0;x+1<width;++x)fresh.Add(map->getFlipState(x,y));
        fresh.Bytes(current.data(),current.size());
        if(terrainRetained && (fresh.value!=ground.value || current!=visibility)) {
            ++m_state->terrainProofMismatches;
            std::fprintf(stderr,"[water-terrain-proof] exact mismatch; refreshed current terrain snapshot\n");
            visibility.swap(reference);
        }
        ground=fresh;
        if(useTerrainSnapshot)m_state->terrainProof.Commit(terrainKey,map->getDataPtr(),heightBytes,flipData,flipBytes,shroudCells,ground.value);
    }
    digestReport.Mark(digestReport.fingerprintDone);
    if (!m_state->hasTerrain || m_state->terrainHash!=ground.value) {
        auto &out=m_state->terrainTriangles;
        out.clear(); out.reserve(std::min(size_t(width-1)*(height-1)*2,TriangleLimit));
        const auto emit=[&](int x,int y,int step,unsigned shade) {
            if (!shade) return; // no unexplored geography in reflections
            const float wx=(x-border)*MAP_XY_FACTOR, wy=(y-border)*MAP_XY_FACTOR;
            const float span=step*MAP_XY_FACTOR;
            Vec3 corners[4]={{wx,wy,map->getHeight(x,y)*MAP_HEIGHT_SCALE},
                {wx+span,wy,map->getHeight(x+step,y)*MAP_HEIGHT_SCALE},
                {wx+span,wy+span,map->getHeight(x+step,y+step)*MAP_HEIGHT_SCALE},
                {wx,wy+span,map->getHeight(x,y+step)*MAP_HEIGHT_SCALE}};
            if (view && std::max(std::max(corners[0].z,corners[1].z),
                std::max(corners[2].z,corners[3].z))<view->waterLevel-0.05f) return;
            RGBColor colour;
            map->getTerrainColorAt(wx+span*0.5f,wy+span*0.5f,&colour);
            const float visible=shade/255.0f;
            Vec3 albedo{linear(colour.red*visible),linear(colour.green*visible),linear(colour.blue*visible)};
            if (map->getFlipState(x,y)) {
                out.push_back({corners[1],corners[3],corners[0],albedo});
                out.push_back({corners[1],corners[2],corners[3],albedo});
            } else {
                out.push_back({corners[0],corners[2],corners[3],albedo});
                out.push_back({corners[0],corners[1],corners[2],albedo});
            }
        };
        const int stride=view?2:1;
        for (int y=0; y+1<height; y+=stride) for (int x=0; x+1<width; x+=stride) {
            bool coarse=stride==2 && x+2<width && y+2<height;
            const int sizeX=std::min(stride,width-1-x),sizeY=std::min(stride,height-1-y);
            float low=map->getHeight(x,y)*MAP_HEIGHT_SCALE,high=low;
            const unsigned firstShade=visibility[size_t(y)*width+x];
            for (int py=0;py<=sizeY;++py) for (int px=0;px<=sizeX;++px) {
                const float z=map->getHeight(x+px,y+py)*MAP_HEIGHT_SCALE;
                low=std::min(low,z);high=std::max(high,z);
                if (visibility[size_t(y+py)*width+x+px]!=firstShade) coarse=false;
            }
            if (view && high<view->waterLevel-0.05f) continue;
            // Keep original cells at banks and shroud changes. Elsewhere the
            // experimental capture uses 20-unit terrain patches (one quarter of the faces).
            if (view && low<=view->waterLevel+0.05f && high>=view->waterLevel-0.05f) coarse=false;
            // A coarse edge must pass through the original middle vertex, so
            // neighbouring fine cells cannot open a crack at that edge.
            if (coarse && (2*map->getHeight(x+1,y)!=map->getHeight(x,y)+map->getHeight(x+2,y)
                || 2*map->getHeight(x+1,y+2)!=map->getHeight(x,y+2)+map->getHeight(x+2,y+2)
                || 2*map->getHeight(x,y+1)!=map->getHeight(x,y)+map->getHeight(x,y+2)
                || 2*map->getHeight(x+2,y+1)!=map->getHeight(x+2,y)+map->getHeight(x+2,y+2)))
                coarse=false;
            if (coarse) emit(x,y,2,firstShade);
            else for (int py=0;py<sizeY;++py) for (int px=0;px<sizeX;++px) {
                const size_t at=size_t(y+py)*width+x+px;
                const unsigned shade=std::min(std::min(visibility[at],visibility[at+1]),
                    std::min(visibility[at+width],visibility[at+width+1]));
                emit(x+px,y+py,1,shade);
            }
        }
        m_state->terrainGroupsValid=false;
        m_state->terrainHash=ground.value; m_state->hasTerrain=true;
    }

    if(hardwareStatic) {
        WaterPathTracing::NativeSnapshotKey key;
        key.Map=map;key.Data=map->getDataPtr();key.Owner=terrain;
        key.Width=width;key.Height=height;key.Border=border;
        hardwareStatic=m_state->snapshotPolicy.RetainFull(key,m_state->terrainTriangles.size());
        if(m_state->snapshotPolicy.Changed)std::fprintf(stderr,
            "[water-native-snapshot] policy=%s terrain_rows=%zu peak_rows=%zu row_budget=%zu source_row_budget_bytes=%zu reason=%s\n",
            hardwareStatic?"retained-static":"conservative-reflected-scope",m_state->terrainTriangles.size(),m_state->snapshotPolicy.Peak(),
            WaterPathTracing::NativeSnapshotPolicy::TerrainRowBudget,
            WaterPathTracing::NativeSnapshotPolicy::TerrainRowBudget*(sizeof(Triangle)+sizeof(WaterPathTracing::PrimaryMaterial)),
            hardwareStatic?"within-source-row-budget":"dense-terrain-latched-until-map-reset");
    }
    digestReport.Mark(digestReport.terrainDone);
    Hash fingerprint;
    fingerprint.Add(ground.value);
    if (hardwareStatic) fingerprint.Add(hardwareStatic);
    using TerrainGroups=WaterPathTracing::TerrainGroups;
    std::vector<TerrainGroups::Span> terrainSpans;
    size_t terrainCount=m_state->terrainTriangles.size();
    if (hardwareStatic) {
        // Every terrain slot is retained, in order. Its validated ground digest
        // already covers the content; hashing/allocating the same 0..N index list
        // on each camera change adds no information to this packet key.
        fingerprint.Add(terrainCount);
        m_state->staticTerrainIndexSlotsSkipped+=terrainCount;
    } else {
        const auto selectOriginal=[&](std::vector<TerrainGroups::Span> &spans) {
            spans.clear();
            for(size_t i=0;i<m_state->terrainTriangles.size();++i) {
                const Triangle &triangle=m_state->terrainTriangles[i];
                if(scope && !scope->Matches(WaterPathTracing::Minimum(triangle.a,WaterPathTracing::Minimum(triangle.b,triangle.c)),
                    WaterPathTracing::Maximum(triangle.a,WaterPathTracing::Maximum(triangle.b,triangle.c))))continue;
                TerrainGroups::Append(spans,i,i+1);
            }
        };
        if(useTerrainSnapshot && scope) {
            if(!m_state->terrainGroupsValid){m_state->terrainGroups.Build(m_state->terrainTriangles);m_state->terrainGroupsValid=true;}
            size_t tested=0,skipped=0,accepted=0;
            m_state->terrainGroups.Select(m_state->terrainTriangles,*scope,terrainSpans,tested,skipped,accepted);
            m_state->terrainGroupTests+=tested;m_state->terrainGroupSkips+=skipped;m_state->terrainGroupAccepts+=accepted;
            if(verify) {
                std::vector<TerrainGroups::Span> original;selectOriginal(original);++m_state->terrainGroupChecks;
                if(original!=terrainSpans) {
                    ++m_state->terrainGroupMismatches;terrainSpans.swap(original);
                    std::fprintf(stderr,"[water-terrain-groups] ordered scope mismatch; using original triangle walk\n");
                }
            }
        } else selectOriginal(terrainSpans);
        terrainCount=0;
        for(const auto &span:terrainSpans) {
            fingerprint.Add(span.First);
            fingerprint.Add(span.End);
            terrainCount+=span.End-span.First;
        }
    }
    if (terrainCount>TriangleLimit)
        return capacityFailure(3,"capture terrain exceeds the experimental triangle limit");
    std::vector<MeshInstance> meshes;
    std::set<MeshClass *> seen;
    // User_Data is untyped: roots commonly hold DrawableInfo, while tank treads
    // and ghost children hold Material_Override instead. Match only addresses
    // owned by live Drawables, then inherit that owner without reading child data.
    std::map<const void *,Drawable *> owners;
    for (Drawable *draw=TheGameClient ? TheGameClient->firstDrawable() : nullptr;
        draw; draw=draw->getNextDrawable())
        owners.emplace(draw->getDrawableInfo(),draw);
    const auto ownerOf=[&owners](RenderObjClass *object)->Drawable * {
        if (!object) return nullptr;
        const auto found=owners.find(object->Get_User_Data());
        return found==owners.end() ? nullptr : found->second;
    };
    SceneIterator *iterator=scene->Create_Iterator(false);
    if (!iterator) return m_state->Fail(4,"scene enumeration failed");
    for (iterator->First(); !iterator->Is_Done(); iterator->Next())
        collect(iterator->Current_Item(), water, ownerOf(iterator->Current_Item()), meshes, fingerprint, seen,
            scope.get(), models, 1.0f, hardwareStatic, &m_state->meshTriangles, false, view ? view->waterLevel : 0);
    scene->Destroy_Iterator(iterator);
    // Prop clones are outside RenderList. Read their world transforms and current
    // shroud status, ignoring the camera-specific TProp::visible bit.
    const W3DPropBuffer *props=terrain->getPropBuffer();
    if (props) for (int i=0; i<props->getPropCount(); ++i) {
        const TProp *prop=props->getProp(i);
        if (!prop || !prop->m_robj) continue;
        const ObjectShroudStatus status=size_t(i)<m_state->propShroud.size()
            ?m_state->propShroud[i]:OBJECTSHROUD_INVALID;
        if (status<=OBJECTSHROUD_INVALID || status>=OBJECTSHROUD_SHROUDED) continue;
        const float visible=shroudAt(shroud,prop->location.x,prop->location.y)/255.0f;
            if (visible > 0)
                collect(prop->m_robj, water, ownerOf(prop->m_robj), meshes, fingerprint, seen, scope.get(), models, visible, hardwareStatic, &m_state->meshTriangles, false, view ? view->waterLevel : 0);
        }
    if (hardwareStatic) {
        // Bound the expanded attempt before texture snapshots can consume the shared atlas.
        size_t maximum=terrainCount;
        for (const MeshInstance &instance:meshes) {
            if (size_t(instance.model->Get_Polygon_Count())>TriangleLimit-maximum)
                return capacityFailure(5,"static superset exceeds the experimental triangle limit");
            maximum+=size_t(instance.model->Get_Polygon_Count());
        }
    }
    // Prune even when the resulting packet fingerprint has not changed. Only
    // moved live meshes retain identity across panning; their off-scope payload is freed.
    for (auto &entry:m_state->meshTriangles) entry.second.packetActive=false;
    for (const MeshInstance &instance:meshes) {
        auto found=m_state->meshTriangles.find(instance.mesh);
        if(found!=m_state->meshTriangles.end())found->second.packetActive=true;
    }
    for (auto it=m_state->meshTriangles.begin(); it!=m_state->meshTriangles.end(); ) {
        auto &cached=it->second;
        if (!seen.count(it->first) || (!cached.packetActive && !cached.moving)) {
            it=m_state->meshTriangles.erase(it);
            continue;
        }
        if (!cached.packetActive) {
            std::vector<Triangle>().swap(cached.triangles);
            std::vector<WaterPathTracing::PrimaryMaterial>().swap(cached.materials);
            std::vector<TriIndex>().swap(cached.rigidIndices);
            cached.valid=false;cached.fingerprint=0;cached.appearance={};cached.skippedAlpha=0;
        }
        ++it;
    }
    digestReport.Mark(digestReport.collectDone);
    if (m_state->hasScene && m_state->sceneHash==fingerprint.value
        && m_state->materialsCommittedTexels==m_state->materials.texels.size()) {
        m_state->requestedCapture=capture;
        m_state->failure=0; return true;
    }

    const bool reuseTerrainPrefix=hardwareStatic && m_state->staticPrefixValid
        && m_state->staticPrefixTerrainHash==ground.value && m_state->staticTerrainTriangleCount==terrainCount
        && terrainCount<=m_state->staticTriangleCount && terrainCount<=m_state->triangles.size()
        && terrainCount<=m_state->materials.triangles.size();
    size_t retainedPrefix=0;
    std::vector<Triangle> output;
    std::vector<WaterPathTracing::PrimaryMaterial> primaryOutput;
    if(!hardwareStatic) {
        output.reserve(terrainCount);
        for(const auto &span:terrainSpans)
            output.insert(output.end(),m_state->terrainTriangles.begin()+span.First,m_state->terrainTriangles.begin()+span.End);
        primaryOutput.resize(terrainCount);
    }
    const auto append = [&](const MeshTriangles &mesh) {
        output.insert(output.end(),mesh.triangles.begin(),mesh.triangles.end());
        primaryOutput.insert(primaryOutput.end(),mesh.materials.begin(),mesh.materials.end());
    };
    size_t skippedAlpha=0;
    digestReport.Mark(digestReport.generationStarted);
    for (const MeshInstance &instance:meshes) {
        auto &cached=m_state->meshTriangles[instance.mesh];
        if (!cached.Matches(instance.mesh,instance.model,instance.owner)) {
            cached=MeshTriangles{};
            cached.Bind(instance.mesh,instance.model,instance.owner);
        }
        if (cached.valid && cached.fingerprint==instance.fingerprint) {
            if(!hardwareStatic) {
                if(output.size()+cached.triangles.size()>TriangleLimit)return capacityFailure(5,"scene exceeds the experimental triangle limit");
                append(cached);
            }
            skippedAlpha+=cached.skippedAlpha;
            continue;
        }
        MeshModelClass *model=instance.model;
        const Vector3 *vertices=instance.posed.empty() ? model->Get_Vertex_Array() : instance.posed.data();
        const auto markMoving = [&](const std::vector<Triangle> &positions) {
            if (!instance.staticGeometry || !cached.valid) return;
            bool moved = cached.triangles.size() != positions.size();
            for (size_t i = 0; !moved && i < positions.size(); ++i) {
                moved = std::memcmp(&cached.triangles[i].a, &positions[i].a, 9 * sizeof(float)) != 0;
            }
            if (moved) cached.moving=true;
        };

        std::vector<Triangle> transformed;
        const bool reuseAppearance = instance.appearance.Matches(cached.appearance)
            && cached.valid && cached.rigidIndices.size() == cached.triangles.size();
        if (reuseAppearance) {
            // The verified model field bytes, current visibility and texture
            // admission match. Only the world vertex positions need refreshing.
            transformed = cached.triangles;
            for (size_t i = 0; i < transformed.size(); ++i) {
                const TriIndex &indices = cached.rigidIndices[i];
                Vector3 points[3];
                for (unsigned k = 0; k < 3; ++k) {
                    Matrix3D::Transform_Vector(instance.transform, vertices[indices[k]], &points[k]);
                }
                transformed[i].a = vector(points[0]);
                transformed[i].b = vector(points[1]);
                transformed[i].c = vector(points[2]);
            }
            if (!verify) {
                if (!hardwareStatic && output.size() + transformed.size() > TriangleLimit)
                    return capacityFailure(5, "scene exceeds the experimental triangle limit");
                markMoving(transformed);
                cached.triangles.swap(transformed);
                cached.fingerprint = instance.fingerprint;
                ++m_state->rigidReuses;
                skippedAlpha += cached.skippedAlpha;
                if (!hardwareStatic) append(cached);
                continue;
            }
        }
        std::vector<Triangle> generated;
        std::vector<WaterPathTracing::PrimaryMaterial> primaryGenerated;
        std::vector<TriIndex> rigidIndices;
        bool retainAppearance = instance.appearance.modelRevision != 0;
        size_t meshSkipped=0;
        if(hardwareStatic){generated.reserve(model->Get_Polygon_Count());primaryGenerated.reserve(model->Get_Polygon_Count());}
        const TriIndex *polys=model->Get_Polygon_Array();
        const Vector2 *uv=model->Get_UV_Array();
        const unsigned *vertexColours=model->Get_DCG_Array(0);
        const ModelBindings bindings(model);
        VertexMaterialClass *lastMaterial=nullptr;bool materialRead=false,vertexColour=false;
        float opacity=1;Vector3 materialDiffuse(1,1,1);
        TextureClass *lastTexture=nullptr;const TextureSample *lastSample=nullptr;bool textureRead=false;
        for (int p=0; p<model->Get_Polygon_Count(); ++p) {
            const ShaderClass shader=bindings.Shader(p);
            if (shader.Get_Alpha_Test()!=ShaderClass::ALPHATEST_DISABLE
                || shader.Get_Dst_Blend_Func()!=ShaderClass::DSTBLEND_ZERO
                || shader.Get_Src_Blend_Func()!=ShaderClass::SRCBLEND_ONE) { ++meshSkipped; continue; }
            const TriIndex &indices=polys[p];
            Vector3 points[3], diffuse(0,0,0);
            bool opaque=true;
            for (int k=0; k<3; ++k) {
                const unsigned index=indices[k];
                if (index>=unsigned(model->Get_Vertex_Count())) { opaque=false; break; }
                // Skin vertices already contain the hierarchy's world transform.
                if (!instance.posed.empty()) points[k]=vertices[index];
                else Matrix3D::Transform_Vector(instance.transform,vertices[index],&points[k]);
                VertexMaterialClass *material=bindings.Material(index);
                Vector3 colour(1,1,1);
                if (material) {
                    if(!materialRead || material!=lastMaterial) {
                        opacity=material->Get_Opacity();vertexColour=material->Get_Diffuse_Color_Source()==VertexMaterialClass::COLOR1;
                        if(!vertexColour || !vertexColours)material->Get_Diffuse(&materialDiffuse);
                        lastMaterial=material;materialRead=true;
                    }
                    if (opacity<0.999f) { opaque=false; break; }
                    if (vertexColour && vertexColours) {
                        const unsigned packed=vertexColours[index];
                        colour.Set(float((packed>>16)&255)/255,float((packed>>8)&255)/255,float(packed&255)/255);
                    } else colour=materialDiffuse;
                }
                diffuse+=colour*(1.0f/3.0f);
            }
            if (!opaque) { ++meshSkipped; continue; }
            const Vector3 baseDiffuse=diffuse*instance.visibility;
            WaterPathTracing::PrimaryMaterial primary{};
            // Do not let unreachable static-superset meshes consume primary atlas capacity.
            // On admission their fingerprint changes and they take the original scoped texture path.
            if (shader.Get_Texturing()!=ShaderClass::TEXTURING_DISABLE && uv && (!hardwareStatic || instance.inScope)) {
                TextureClass *source=bindings.Texture(p);
                if(!textureRead || source!=lastTexture){lastSample=m_state->Texture(source);lastTexture=source;textureRead=true;}
                const TextureSample *texture=lastSample;
                // An uncached source can become ready or enter the primary atlas
                // later, even when its binding and the rest of the model match.
                if (source && (!texture || texture->atlasOffset >= WaterPathTracing::MaximumPrimaryTexels))
                    retainAppearance = false;
                if (texture) {
                    Vector2 centre=(uv[indices[0]]+uv[indices[1]]+uv[indices[2]])*(1.0f/3.0f);
                    Vector3 texel=texture->Sample(centre.X,centre.Y);
                    diffuse.X*=texel.X; diffuse.Y*=texel.Y; diffuse.Z*=texel.Z;
                    if(texture->atlasOffset<WaterPathTracing::MaximumPrimaryTexels) {
                        const Vector2 &a=uv[indices[0]],&b=uv[indices[1]],&c=uv[indices[2]];
                        // UVs address the same normalized/repeating source as the
                        // centroid fallback, now retaining its spatial variation.
                        if(std::isfinite(a.X)&&std::isfinite(a.Y)&&std::isfinite(b.X)&&std::isfinite(b.Y)
                            &&std::isfinite(c.X)&&std::isfinite(c.Y)) {
                            primary.uvAB={a.X,a.Y,b.X,b.Y};
                            primary.uvCSize={c.X,c.Y,float(texture->width),float(texture->height)};
                            primary.diffuseOffset={linear(baseDiffuse.X),linear(baseDiffuse.Y),linear(baseDiffuse.Z),
                                float(texture->atlasOffset+1)};
                        }
                    }
                }
            }
            if (output.size()+generated.size()>=TriangleLimit)
                return capacityFailure(5,"scene exceeds the experimental triangle limit");
            diffuse*=instance.visibility;
            generated.push_back({vector(points[0]),vector(points[1]),vector(points[2]),
                {linear(diffuse.X),linear(diffuse.Y),linear(diffuse.Z)}});
            primaryGenerated.push_back(primary);
            if (retainAppearance) rigidIndices.push_back(indices);
        }
        if (reuseAppearance) {
            // Verification takes the full export above and publishes its rows.
            ++m_state->rigidChecks;
            const bool same = meshSkipped == cached.skippedAlpha && generated.size() == transformed.size()
                && primaryGenerated.size() == cached.materials.size()
                && (generated.empty() || std::memcmp(generated.data(), transformed.data(), generated.size() * sizeof(Triangle)) == 0)
                && (primaryGenerated.empty() || std::memcmp(primaryGenerated.data(), cached.materials.data(),
                    primaryGenerated.size() * sizeof(WaterPathTracing::PrimaryMaterial)) == 0);
            if (!same) {
                ++m_state->rigidMismatches;
                retainAppearance = false;
            }
        }
        markMoving(generated);
        cached.triangles.swap(generated);
        cached.materials.swap(primaryGenerated);
        cached.fingerprint=instance.fingerprint; cached.valid=true;
        cached.appearance = retainAppearance ? instance.appearance : RigidAppearance{};
        if (!retainAppearance) rigidIndices.clear();
        cached.rigidIndices.swap(rigidIndices);
        cached.skippedAlpha=meshSkipped;
        skippedAlpha+=meshSkipped;
        if(!hardwareStatic)append(cached);
    }
    digestReport.Mark(digestReport.generationDone);
    std::vector<WaterPathTracing::StaticSpan> currentSpans;
    std::vector<const MeshTriangles *> staticMeshes,movingMeshes;
    struct StaticPatch {const MeshTriangles *Mesh;size_t First,Count;bool Triangles,Materials;};
    std::vector<StaticPatch> patches;
    size_t staticTriangleCount=0,movingCount=0;
    bool sameStaticLayout=false,patchedTriangles=false,patchedMaterials=false;
    if(hardwareStatic) {
        staticTriangleCount=terrainCount;currentSpans.reserve(meshes.size());staticMeshes.reserve(meshes.size());movingMeshes.reserve(meshes.size());
        for(const auto &instance:meshes) {
            const auto &cached=m_state->meshTriangles.at(instance.mesh);
            if(!cached.valid || cached.materials.size()!=cached.triangles.size())return m_state->Fail(9,"inconsistent native mesh appearance rows");
            const bool keepStatic=instance.staticGeometry && !m_state->meshTriangles.at(instance.mesh).moving;
            if(keepStatic) {
                currentSpans.push_back({instance.mesh,staticTriangleCount,cached.triangles.size(),instance.fingerprint});
                staticMeshes.push_back(&cached);staticTriangleCount+=cached.triangles.size();
            } else if(instance.inScope){movingMeshes.push_back(&cached);movingCount+=cached.triangles.size();}
        }
        if(staticTriangleCount>TriangleLimit || movingCount>TriangleLimit-staticTriangleCount)
            return capacityFailure(5,"scene exceeds the experimental triangle limit");
        sameStaticLayout=reuseTerrainPrefix && staticTriangleCount==m_state->staticTriangleCount
            && staticTriangleCount<=m_state->triangles.size() && staticTriangleCount<=m_state->materials.triangles.size()
            && WaterPathTracing::SameStaticSpanLayout(m_state->staticSpans,currentSpans);
        retainedPrefix=sameStaticLayout ? staticTriangleCount : reuseTerrainPrefix ? terrainCount : 0;
        output.reserve(staticTriangleCount-retainedPrefix+movingCount);
        primaryOutput.reserve(staticTriangleCount-retainedPrefix+movingCount);
        if(!retainedPrefix){output.insert(output.end(),m_state->terrainTriangles.begin(),m_state->terrainTriangles.end());primaryOutput.resize(terrainCount);}
        for(size_t i=0;i<currentSpans.size();++i) {
            const auto &span=currentSpans[i];
            const auto &cached=*staticMeshes[i];
            if(!sameStaticLayout){append(cached);continue;}
            if(span.Fingerprint==m_state->staticSpans[i].Fingerprint)continue;
            const bool triangles=span.Count && std::memcmp(cached.triangles.data(),m_state->triangles.data()+span.First,span.Count*sizeof(Triangle))!=0;
            const bool materials=span.Count && std::memcmp(cached.materials.data(),m_state->materials.triangles.data()+span.First,span.Count*sizeof(WaterPathTracing::PrimaryMaterial))!=0;
            if(triangles || materials){patches.push_back({&cached,span.First,span.Count,triangles,materials});patchedTriangles|=triangles;patchedMaterials|=materials;}
        }
        for(const auto *mesh:movingMeshes)append(*mesh);
    }
    const size_t totalTriangles=retainedPrefix+output.size();
    if(verify && hardwareStatic) {
        std::vector<Triangle> reference=m_state->terrainTriangles;
        std::vector<WaterPathTracing::PrimaryMaterial> referenceMaterials(terrainCount);
        for(bool staticPart:{true,false})for(const auto &instance:meshes) {
            const bool keepStatic=instance.staticGeometry && !m_state->meshTriangles.at(instance.mesh).moving;
            if(keepStatic!=staticPart || (!keepStatic && !instance.inScope))continue;
            const auto &cached=m_state->meshTriangles.at(instance.mesh);
            reference.insert(reference.end(),cached.triangles.begin(),cached.triangles.end());
            referenceMaterials.insert(referenceMaterials.end(),cached.materials.begin(),cached.materials.end());
        }
        std::vector<Triangle> candidatePrefix;
        std::vector<WaterPathTracing::PrimaryMaterial> candidateMaterials;
        const Triangle *prefix=m_state->triangles.data();
        const WaterPathTracing::PrimaryMaterial *prefixMaterials=m_state->materials.triangles.data();
        if(!patches.empty()) {
            candidatePrefix.assign(m_state->triangles.begin(),m_state->triangles.begin()+retainedPrefix);
            candidateMaterials.assign(m_state->materials.triangles.begin(),m_state->materials.triangles.begin()+retainedPrefix);
            for(const auto &patch:patches) {
                if(patch.Triangles)std::memcpy(candidatePrefix.data()+patch.First,patch.Mesh->triangles.data(),patch.Count*sizeof(Triangle));
                if(patch.Materials)std::memcpy(candidateMaterials.data()+patch.First,patch.Mesh->materials.data(),patch.Count*sizeof(WaterPathTracing::PrimaryMaterial));
            }
            prefix=candidatePrefix.data();prefixMaterials=candidateMaterials.data();
        }
        ++m_state->packetVerifications;
        const bool same=reference.size()==totalTriangles && referenceMaterials.size()==totalTriangles
            && (!retainedPrefix || (std::memcmp(reference.data(),prefix,retainedPrefix*sizeof(Triangle))==0
                && std::memcmp(referenceMaterials.data(),prefixMaterials,retainedPrefix*sizeof(WaterPathTracing::PrimaryMaterial))==0))
            && (output.empty() || (std::memcmp(reference.data()+retainedPrefix,output.data(),output.size()*sizeof(Triangle))==0
                && std::memcmp(referenceMaterials.data()+retainedPrefix,primaryOutput.data(),primaryOutput.size()*sizeof(WaterPathTracing::PrimaryMaterial))==0));
        if(!same){++m_state->packetMismatches;return m_state->Fail(8,"retained native terrain prefix differs from full current packet");}
    }
    const bool first=!m_state->hasScene;
    // A changed material on an omitted transparent mesh need not reset accumulation.
    // Triangle contains exactly twelve floats, so comparison includes no padding.
    static_assert(sizeof(Triangle)==12*sizeof(float),"Triangle comparison requires packed float members");
    const bool materialChanged=patchedMaterials || totalTriangles!=m_state->materials.triangles.size()
        || (!primaryOutput.empty() && std::memcmp(primaryOutput.data(),m_state->materials.triangles.data()+retainedPrefix,
            primaryOutput.size()*sizeof(WaterPathTracing::PrimaryMaterial))!=0)
        || m_state->materialsCommittedTexels!=m_state->materials.texels.size();
    if (first || patchedTriangles || materialChanged || staticTriangleCount!=m_state->staticTriangleCount || totalTriangles!=m_state->triangles.size()
        || (!output.empty() && std::memcmp(output.data(),m_state->triangles.data()+retainedPrefix,output.size()*sizeof(Triangle))!=0))
        ++m_state->revision;
    // All validation and packet comparison above precede mutation of published source rows.
    for(const auto &patch:patches) {
        if(patch.Triangles){std::memcpy(m_state->triangles.data()+patch.First,patch.Mesh->triangles.data(),patch.Count*sizeof(Triangle));m_state->staticTriangleRowsPatched+=patch.Count;}
        if(patch.Materials){std::memcpy(m_state->materials.triangles.data()+patch.First,patch.Mesh->materials.data(),patch.Count*sizeof(WaterPathTracing::PrimaryMaterial));m_state->staticMaterialRowsPatched+=patch.Count;}
    }
    m_state->staticTriangleCount=staticTriangleCount;
    m_state->staticTerrainTriangleCount=hardwareStatic ? terrainCount : 0;
    m_state->requestedCapture=capture;
    if (retainedPrefix) {
        m_state->triangles.resize(retainedPrefix);
        m_state->triangles.insert(m_state->triangles.end(),output.begin(),output.end());
        m_state->materials.triangles.resize(retainedPrefix);
        m_state->materials.triangles.insert(m_state->materials.triangles.end(),primaryOutput.begin(),primaryOutput.end());
    } else {
        m_state->triangles.swap(output);
        m_state->materials.triangles.swap(primaryOutput);
    }
    if(sameStaticLayout) {
        if(patches.empty())++m_state->staticPrefixReuses;else ++m_state->staticSpanPatchFrames;
        size_t patched=0;for(const auto &patch:patches)patched+=patch.Count;
        m_state->staticMeshRowsRetained+=staticTriangleCount-terrainCount-patched;
    } else if(hardwareStatic)++m_state->staticPrefixBuilds;
    m_state->staticSpans.swap(currentSpans);
    if(reuseTerrainPrefix){++m_state->terrainPrefixReuses;m_state->terrainPrefixTrianglesRetained+=terrainCount;}
    m_state->staticPrefixValid=hardwareStatic;
    m_state->staticPrefixTerrainHash=ground.value;
    m_state->materialsCommittedTexels=m_state->materials.texels.size();
    m_state->sceneHash=fingerprint.value; m_state->hasScene=true;
    if (profile || first) std::fprintf(stderr,"[water-path-partition] static=%zu moving=%zu capture=%s prefix_reused=%u prefix_reuses=%llu prefix_builds=%llu terrain_index_slots_skipped=%llu terrain_snapshot_reuses=%llu terrain_snapshot_refreshes=%llu terrain_snapshot_mismatches=%llu terrain_prefix_reuses=%llu terrain_prefix_triangles_retained=%llu packet_verifications=%llu packet_mismatches=%llu static_patch_frames=%llu static_triangle_rows_patched=%llu static_material_rows_patched=%llu static_mesh_rows_retained=%llu terrain_scope_tests=%llu terrain_scope_skips=%llu terrain_scope_bulk_accepts=%llu terrain_scope_checks=%llu terrain_scope_mismatches=%llu\n",
        staticTriangleCount,m_state->triangles.size()-staticTriangleCount,hardwareStatic ? "conservative-static-superset" : "original-scoped",
        unsigned(sameStaticLayout),(unsigned long long)m_state->staticPrefixReuses,(unsigned long long)m_state->staticPrefixBuilds,
        (unsigned long long)m_state->staticTerrainIndexSlotsSkipped,(unsigned long long)m_state->terrainProof.Reuses,
        (unsigned long long)m_state->terrainProof.Refreshes,(unsigned long long)m_state->terrainProofMismatches,
        (unsigned long long)m_state->terrainPrefixReuses,(unsigned long long)m_state->terrainPrefixTrianglesRetained,
        (unsigned long long)m_state->packetVerifications,(unsigned long long)m_state->packetMismatches,
        (unsigned long long)m_state->staticSpanPatchFrames,(unsigned long long)m_state->staticTriangleRowsPatched,
        (unsigned long long)m_state->staticMaterialRowsPatched,(unsigned long long)m_state->staticMeshRowsRetained,
        (unsigned long long)m_state->terrainGroupTests,(unsigned long long)m_state->terrainGroupSkips,
        (unsigned long long)m_state->terrainGroupAccepts,(unsigned long long)m_state->terrainGroupChecks,
        (unsigned long long)m_state->terrainGroupMismatches);
    if (useModelProof && (profile || verify)) std::fprintf(stderr,
        "[water-path-rigid] reuses=%llu checks=%llu mismatches=%llu\n",
        (unsigned long long)m_state->rigidReuses,(unsigned long long)m_state->rigidChecks,
        (unsigned long long)m_state->rigidMismatches);
    m_state->failure=0;
    if (first) std::fprintf(stderr,"WaterPathTracing: scene %zu triangles (%zu terrain), %zu transparent triangles omitted; primary textured materials; batched trees and bridge decks unsupported\n",
        m_state->triangles.size(),terrainCount,skippedAlpha);
    return true;
}
#endif
