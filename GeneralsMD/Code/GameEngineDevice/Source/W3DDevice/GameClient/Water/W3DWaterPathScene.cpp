// Experimental reflection geometry: opaque scene meshes and known terrain.
// Materials are sampled once per triangle. Batched trees/bridge decks,
// particles, cutout/blended materials and procedural vertex effects are not exported.
#if defined(__linux__)
#include "W3DWaterPathScene.h"
#include "Platform/WaterPathCaptureScope.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/W3DInGameUI.h"
#include "W3DDevice/GameClient/W3DPropBuffer.h"
#include "GameClient/Drawable.h"
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
#include <set>

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
    ~TextureSample() { if (resource) resource->Release(); }
    Vector3 Sample(float u, float v) const {
        if (!width || !height || !std::isfinite(u) || !std::isfinite(v)) return Vector3(1,1,1);
        u -= std::floor(u); v -= std::floor(v);
        unsigned x=std::min(unsigned(u*width),width-1), y=std::min(unsigned(v*height),height-1);
        return colours[y*width+x];
    }
};

struct MeshInstance {
    MeshClass *mesh;
    MeshModelClass *model;
    Matrix3D transform;
    std::vector<Vector3> posed;
    float visibility;
    uint64_t fingerprint=0;
};

bool eligible(RenderObjClass *object, RenderObjClass *water, Drawable *draw) {
    if (!object || object == water || !object->Is_Not_Hidden_At_All()) return false;
    if (draw && (draw->isDrawableEffectivelyHidden() || draw->getFullyObscuredByShroud()
        || draw->isNoSaveForDraw() || draw->getEffectiveOpacity()<0.999f))
        return false;
    return !TheInGameUI || !static_cast<W3DInGameUI *>(TheInGameUI)->isWorldSpaceUI(object,draw);
}

void collect(RenderObjClass *object, RenderObjClass *water, Drawable *draw,
    std::vector<MeshInstance> &meshes, Hash &hash, std::set<MeshClass *> &seen,
    const WaterPathTracing::CaptureScope *scope, float visibility=1.0f) {
    if (!eligible(object,water,draw)) return;
    if (object->Class_ID()==RenderObjClass::CLASSID_MESH) {
        MeshClass *mesh=static_cast<MeshClass *>(object);
        if (seen.count(mesh)) return;
        MeshModelClass *model=mesh->Peek_Model();
        if (!model || !model->Get_Vertex_Count() || !model->Get_Polygon_Count()
            || model->Get_Pass_Count()<1 || mesh->Get_Alpha_Override()<0.999f) return;
        MeshInstance instance{mesh,model,mesh->Get_Transform(),{},visibility};
        if (model->Get_Flag(MeshGeometryClass::SKIN)) {
            if (!mesh->Get_Container() || !mesh->Get_Container()->Get_HTree()) return;
            instance.posed.resize(model->Get_Vertex_Count());
            mesh->Get_Deformed_Vertices(instance.posed.data());
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
            if (!scope->Matches(vector(box.Center-box.Extent),vector(box.Center+box.Extent))) return;
        }
        seen.insert(mesh);
        Hash signature;
        signature.Add(mesh); signature.Add(model); signature.Add(instance.transform);
        signature.Add(visibility);
        signature.Add(model->Get_Vertex_Count()); signature.Add(model->Get_Polygon_Count());
        signature.Bytes(instance.posed.empty() ? model->Get_Vertex_Array() : instance.posed.data(),
            size_t(model->Get_Vertex_Count())*sizeof(Vector3));
        signature.Bytes(model->Get_Polygon_Array(),size_t(model->Get_Polygon_Count())*sizeof(TriIndex));
        const Vector2 *uv=model->Get_UV_Array();
        signature.Add(uv!=nullptr);
        if (uv) signature.Bytes(uv,size_t(model->Get_Vertex_Count())*sizeof(Vector2));
        const unsigned *colours=model->Get_DCG_Array(0);
        signature.Add(colours!=nullptr);
        if (colours) signature.Bytes(colours,size_t(model->Get_Vertex_Count())*sizeof(unsigned));
        for (int v=0; v<model->Get_Vertex_Count(); ++v) {
            const VertexMaterialClass *material=model->Peek_Material(v);
            signature.Add(material!=nullptr);
            if (material) signature.Add(material->Get_CRC());
        }
        for (int p=0; p<model->Get_Polygon_Count(); ++p) {
            signature.Add(model->Get_Shader(p).Get_Bits());
            TextureClass *texture=model->Peek_Texture(p);
            signature.Add(texture);
            if (texture) {
                signature.Add(texture->Is_Initialized());
                signature.Add(texture->Peek_D3D_Texture());
            }
        }
        instance.fingerprint=signature.value;
        hash.Add(instance.fingerprint);
        meshes.push_back(std::move(instance));
        return;
    }
    if (object->Class_ID()==RenderObjClass::CLASSID_HLOD) {
        HLodClass *lod=static_cast<HLodClass *>(object);
        const int level=lod->Get_LOD_Level();
        for (int i=0; i<lod->Get_Lod_Model_Count(level); ++i)
            collect(lod->Peek_Lod_Model(level,i),water,draw,meshes,hash,seen,scope,visibility);
        for (int i=0; i<lod->Get_Additional_Model_Count(); ++i)
            collect(lod->Peek_Additional_Model(i),water,draw,meshes,hash,seen,scope,visibility);
        return;
    }
    for (int i=0; i<object->Get_Num_Sub_Objects(); ++i) {
        RenderObjClass *child=object->Get_Sub_Object(i);
        collect(child,water,draw,meshes,hash,seen,scope,visibility);
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
    std::vector<Triangle> triangles, terrainTriangles;
    std::map<IDirect3DTexture9 *,std::unique_ptr<TextureSample>> textures;
    struct MeshTriangles {
        uint64_t fingerprint=0;
        bool valid=false;
        std::vector<Triangle> triangles;
        size_t skippedAlpha=0;
    };
    std::map<MeshClass *,MeshTriangles> meshTriangles;
    unsigned failure=0;
    std::vector<ObjectShroudStatus> propShroud;

    bool Fail(unsigned code,const char *reason) {
        if (failure!=code) std::fprintf(stderr,"WaterPathTracing: %s; using Hybrid fallback\n",reason);
        failure=code;
        return false;
    }

    const TextureSample *Texture(TextureClass *texture) {
        if (!texture || !texture->Is_Initialized()) return nullptr;
        IDirect3DTexture9 *resource=texture->Peek_D3D_Texture();
        if (!resource) return nullptr;
        auto found=textures.find(resource);
        if (found!=textures.end()) return found->second.get();
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
            for (unsigned y=0; y<sample->height; ++y)
                for (unsigned x=0; x<sample->width; ++x)
                    sample->colours[y*sample->width+x]=pixel(static_cast<const unsigned char *>(locked.pBits),
                        locked.Pitch,description.Format,x*description.Width/sample->width,y*description.Height/sample->height);
            resource->UnlockRect(level);
        }
        const TextureSample *result=sample.get();
        textures.emplace(resource,std::move(sample));
        return result;
    }
};

W3DWaterPathScene::W3DWaterPathScene() : m_state(new State) {}
W3DWaterPathScene::~W3DWaterPathScene() = default;

const std::vector<Triangle> &W3DWaterPathScene::Triangles() const { return m_state->triangles; }
uint64_t W3DWaterPathScene::Revision() const { return m_state->revision; }

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
    const WaterPathTracing::View *view)
{
    if (!scene || !terrain || !terrain->getMap())
        return m_state->Fail(1,"scene or terrain is unavailable");
    WorldHeightMap *map=terrain->getMap();
    const int width=map->getXExtent(), height=map->getYExtent(), border=map->getBorderSizeInline();
    if (width<2 || height<2 || !map->getDataPtr())
        return m_state->Fail(2,"terrain has no usable height data");
    // The production capture is scoped; unscoped callers retain the original limit.
    if (!view && uint64_t(width-1)*uint64_t(height-1)*2>TriangleLimit)
        return m_state->Fail(3,"terrain exceeds the experimental triangle limit");
    std::unique_ptr<WaterPathTracing::CaptureScope> scope;
    if (view) scope.reset(new WaterPathTracing::CaptureScope(*view));
    Hash ground;
    ground.Add(map); ground.Add(width); ground.Add(height); ground.Add(border);
    ground.Add(view!=nullptr);
    if (view) ground.Add(view->waterLevel);
    ground.Bytes(map->getDataPtr(),size_t(width)*height);
    std::vector<unsigned char> visibility(size_t(width)*height);
    W3DShroud *shroud=terrain->getShroud();
    for (int y=0; y<height; ++y) for (int x=0; x<width; ++x) {
        visibility[size_t(y)*width+x]=shroudAt(shroud,(x-border)*MAP_XY_FACTOR,(y-border)*MAP_XY_FACTOR);
        if (x+1<width && y+1<height) ground.Add(map->getFlipState(x,y));
    }
    ground.Bytes(visibility.data(),visibility.size());
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
        m_state->terrainHash=ground.value; m_state->hasTerrain=true;
    }

    Hash fingerprint;
    fingerprint.Add(ground.value);
    std::vector<size_t> terrainIndices;
    terrainIndices.reserve(m_state->terrainTriangles.size());
    for (size_t i=0;i<m_state->terrainTriangles.size();++i) {
        const Triangle &triangle=m_state->terrainTriangles[i];
        if (scope && !scope->Matches(WaterPathTracing::Minimum(triangle.a,WaterPathTracing::Minimum(triangle.b,triangle.c)),
            WaterPathTracing::Maximum(triangle.a,WaterPathTracing::Maximum(triangle.b,triangle.c)))) continue;
        terrainIndices.push_back(i);
        fingerprint.Add(i);
    }
    if (terrainIndices.size()>TriangleLimit)
        return m_state->Fail(3,"capture terrain exceeds the experimental triangle limit");
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
        collect(iterator->Current_Item(),water,ownerOf(iterator->Current_Item()),meshes,fingerprint,seen,scope.get());
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
        if (visible>0) collect(prop->m_robj,water,ownerOf(prop->m_robj),meshes,fingerprint,seen,scope.get(),visible);
    }
    if (m_state->hasScene && m_state->sceneHash==fingerprint.value) { m_state->failure=0; return true; }

    for (auto it=m_state->meshTriangles.begin(); it!=m_state->meshTriangles.end(); ) {
        if (!seen.count(it->first)) it=m_state->meshTriangles.erase(it);
        else ++it;
    }
    std::vector<Triangle> output;
    output.reserve(terrainIndices.size());
    for (size_t i:terrainIndices) output.push_back(m_state->terrainTriangles[i]);
    size_t skippedAlpha=0;
    for (const MeshInstance &instance:meshes) {
        auto &cached=m_state->meshTriangles[instance.mesh];
        if (cached.valid && cached.fingerprint==instance.fingerprint) {
            if (output.size()+cached.triangles.size()>TriangleLimit)
                return m_state->Fail(5,"scene exceeds the experimental triangle limit");
            output.insert(output.end(),cached.triangles.begin(),cached.triangles.end());
            skippedAlpha+=cached.skippedAlpha;
            continue;
        }
        std::vector<Triangle> generated;
        size_t meshSkipped=0;
        MeshModelClass *model=instance.model;
        const Vector3 *vertices=instance.posed.empty() ? model->Get_Vertex_Array() : instance.posed.data();
        const TriIndex *polys=model->Get_Polygon_Array();
        const Vector2 *uv=model->Get_UV_Array();
        const unsigned *vertexColours=model->Get_DCG_Array(0);
        for (int p=0; p<model->Get_Polygon_Count(); ++p) {
            const ShaderClass shader=model->Get_Shader(p);
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
                VertexMaterialClass *material=model->Peek_Material(index);
                Vector3 colour(1,1,1);
                if (material) {
                    if (material->Get_Opacity()<0.999f) { opaque=false; break; }
                    if (material->Get_Diffuse_Color_Source()==VertexMaterialClass::COLOR1 && vertexColours) {
                        const unsigned packed=vertexColours[index];
                        colour.Set(float((packed>>16)&255)/255,float((packed>>8)&255)/255,float(packed&255)/255);
                    } else material->Get_Diffuse(&colour);
                }
                diffuse+=colour*(1.0f/3.0f);
            }
            if (!opaque) { ++meshSkipped; continue; }
            if (shader.Get_Texturing()!=ShaderClass::TEXTURING_DISABLE && uv) {
                const TextureSample *texture=m_state->Texture(model->Peek_Texture(p));
                if (texture) {
                    Vector2 centre=(uv[indices[0]]+uv[indices[1]]+uv[indices[2]])*(1.0f/3.0f);
                    Vector3 texel=texture->Sample(centre.X,centre.Y);
                    diffuse.X*=texel.X; diffuse.Y*=texel.Y; diffuse.Z*=texel.Z;
                }
            }
            if (output.size()+generated.size()>=TriangleLimit)
                return m_state->Fail(5,"scene exceeds the experimental triangle limit");
            diffuse*=instance.visibility;
            generated.push_back({vector(points[0]),vector(points[1]),vector(points[2]),
                {linear(diffuse.X),linear(diffuse.Y),linear(diffuse.Z)}});
        }
        cached.triangles.swap(generated);
        cached.fingerprint=instance.fingerprint; cached.valid=true;
        cached.skippedAlpha=meshSkipped;
        skippedAlpha+=meshSkipped;
        output.insert(output.end(),cached.triangles.begin(),cached.triangles.end());
    }
    const bool first=!m_state->hasScene;
    // A changed material on an omitted transparent mesh need not reset accumulation.
    // Triangle contains exactly twelve floats, so comparison includes no padding.
    static_assert(sizeof(Triangle)==12*sizeof(float),"Triangle comparison requires packed float members");
    if (first || output.size()!=m_state->triangles.size()
        || (!output.empty() && std::memcmp(output.data(),m_state->triangles.data(),output.size()*sizeof(Triangle))!=0))
        ++m_state->revision;
    m_state->triangles.swap(output);
    m_state->sceneHash=fingerprint.value; m_state->hasScene=true;
    m_state->failure=0;
    if (first) std::fprintf(stderr,"WaterPathTracing: scene %zu triangles (%zu terrain), %zu transparent triangles omitted; per-triangle materials; batched trees and bridge decks unsupported\n",
        m_state->triangles.size(),terrainIndices.size(),skippedAlpha);
    return true;
}
#endif
