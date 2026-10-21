// A bank supplies the fixed camera, opaque picture and production water draw inputs.
// The depth buffer is refilled from the banked depth; reflection/refraction sample the original GPU images.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "WaterPreviewColorPanel.h"
#include "WaterPreviewReflectionBar.h"
#include "Platform/EngineShaderName.h"
#include "Platform/WaterSceneBank.h"
#include "engineshader.h"
#include "SdlRenderMatrix.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace WaterPreviewScene {

class Scene {
public:
    WaterSceneBank::Frame bank;
    std::string path;
    bool loaded() const { return !bank.draws.empty(); }
    void load(const std::string &name)
    {
        path=name;
        if (name.find_first_of("/\\") == std::string::npos
            && (name.size()<6 || name.substr(name.size()-6)!=".wbank")) {
            const char *base=SDL_GetBasePath();
            if (!base) throw std::runtime_error("cannot resolve the executable directory");
            path=std::string(base)+"water-scenes/"+name+".wbank";
        }
        std::ifstream input(path,std::ios::binary);
        std::string error;
        if (!WaterSceneBank::Read(input,bank,error)) throw std::runtime_error("cannot read scene "+path+": "+error);
        for (const auto &d:bank.draws) {
            if (d.program!=ENGINE_SHADER_WATER_TRAPEZOID && d.program!=ENGINE_SHADER_WATER_RIVER)
                throw std::runtime_error("bank contains an unsupported water program");
            if (d.pixelConstants.size()>PosixDevice9::PIXEL_SHADER_CONSTANT_COUNT*4)
                throw std::runtime_error("bank needs a newer pixel constant register file");
        }
        std::printf("Banked scene: %s; %ux%u, %zu water draws; fixed camera %.2f %.2f %.2f\n",
            path.c_str(),bank.width,bank.height,bank.draws.size(),bank.camera[0],bank.camera[1],bank.camera[2]);
    }
    ~Scene() { release(); }
    void release()
    {
        for (auto *r:resources) r->Release();
        resources.clear(); draws.clear(); views.clear();
    }
    void initialize(PosixDevice9 *d)
    {
        device=d;
        background=texture(bank.colour);
        mirror=texture(bank.mirror);
        for (const auto &draw:bank.draws) {
            Resources r;
            r.vertices=vertexBuffer(draw.vertices.data(),draw.vertices.size(),draw.fvf);
            r.indices=indexBuffer(draw.indices);
            const RenderUInt32 tokens[]={0xffff0101u,0x0000ffffu};
            require(device->CreatePixelShader(tokens,&r.shader),"create bank water shader");
            resources.push_back(r.shader);
            PosixDevice_Name_Shader(r.shader,draw.program==ENGINE_SHADER_WATER_RIVER?"river water ps.1.1":"trapezoid water ps.1.1");
            for (unsigned stage=0;stage<16;++stage) r.textures[stage]=texture(draw.textures[stage]);
            draws.push_back(r);
        }
        createOcclusion();
        // Each comparison owns a bank-size target and its own exact water snapshot.
        for (unsigned i=0;i<3;++i) {
            View v;
            require(device->CreateTexture(bank.width,bank.height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,
                D3DPOOL_DEFAULT,&v.texture,nullptr),"create bank view");
            resources.push_back(v.texture);
            require(v.texture->GetSurfaceLevel(0,&v.surface),"get bank view surface"); resources.push_back(v.surface);
            require(device->CreateDepthStencilSurface(bank.width,bank.height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,1,
                &v.depth,nullptr),"create bank view depth"); resources.push_back(v.depth);
            views.push_back(v);
        }
    }

    void draw(unsigned width,unsigned height,int reflection,float elapsed,bool edited,
        const WaterPreviewColor::Values &values,bool bubbles,int materialMode,float clarity=-1.f)
    {
        if (reflection==WaterPreviewReflection::PathTraced)
            throw std::runtime_error("path tracing needs complete scene geometry, which this image bank does not contain");
        IDirect3DSurface9 *colour=nullptr,*depth=nullptr;
        require(device->GetRenderTarget(0,&colour),"get preview colour");
        require(device->GetDepthStencilSurface(&depth),"get preview depth");
        const bool compare=reflection==WaterPreviewReflection::Compare;
        for (unsigned i=0;i<(compare?3u:1u);++i) {
            const auto &v=views[i];
            require(device->SetRenderTarget(0,v.surface),"set bank target");
            require(device->SetDepthStencilSurface(v.depth),"set bank depth");
            const D3DVIEWPORT9 full={0,0,bank.width,bank.height,0,1};
            require(device->SetViewport(&full),"set bank viewport");
            require(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0),"clear bank target");
            screenState();
            quad(background,0,0,float(bank.width),float(bank.height));
            occlusion();
            if (!device->Inject_Water_Snapshot(bank.colour.bgra,bank.depth))
                throw std::runtime_error("inject original water colour/depth snapshot");
            water(compare?int(i):reflection,elapsed,edited,values,bubbles,compare && materialMode==3?2:materialMode,clarity);
        }
        require(device->SetRenderTarget(0,colour),"restore preview colour");
        require(device->SetDepthStencilSurface(depth),"restore preview depth");
        colour->Release(); if (depth) depth->Release();
        const D3DVIEWPORT9 full={0,0,width,height,0,1};
        require(device->SetViewport(&full),"set preview viewport");
        require(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff0b1015,1,0),"clear letterbox");
        screenState();
        for (unsigned i=0;i<(compare?3u:1u);++i) {
            const unsigned left=compare?width/3*i:0;
            const unsigned available=compare?(i==2?width-2*(width/3):width/3):width;
            const float scale=std::min(float(available)/bank.width,float(height)/bank.height);
            const float w=bank.width*scale,h=bank.height*scale;
            quad(views[i].texture,left+(available-w)*.5f,(height-h)*.5f,w,h);
        }
        screenState();
    }

private:
    struct Resources {
        IDirect3DVertexBuffer9 *vertices=nullptr;
        IDirect3DIndexBuffer9 *indices=nullptr;
        IDirect3DPixelShader9 *shader=nullptr;
        std::array<IDirect3DTexture9 *,16> textures{};
    };
    struct View { IDirect3DTexture9 *texture; IDirect3DSurface9 *surface,*depth; };
    struct ScreenDepth { float x,y,z,rhw; };
    struct ScreenVertex { float x,y,z,rhw,u,v; };
    PosixDevice9 *device=nullptr;
    // The common D3D9 interface owns each resource's creation reference.
    std::vector<D3D9PosixUnknown *> resources;
    std::vector<Resources> draws;
    std::vector<View> views;
    IDirect3DTexture9 *background=nullptr,*mirror=nullptr;
    IDirect3DVertexBuffer9 *occlusionVertices=nullptr;
    IDirect3DIndexBuffer9 *occlusionIndices=nullptr;
    unsigned occlusionVertexCount=0,occlusionIndexCount=0;

    static void require(RenderResult result,const char *operation)
    {
        if (result!=D3D_OK) throw std::runtime_error(std::string(operation)+" failed ("+std::to_string(result)+")");
    }
    IDirect3DTexture9 *texture(const WaterSceneBank::Image &image)
    {
        if (image.bgra.empty()) return nullptr;
        IDirect3DTexture9 *t=nullptr;
        require(device->CreateTexture(image.width,image.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr),"create bank texture");
        resources.push_back(t);
        D3DLOCKED_RECT lock={}; require(t->LockRect(0,&lock,nullptr,0),"lock bank texture");
        for (unsigned y=0;y<image.height;++y)
            std::memcpy(static_cast<uint8_t *>(lock.pBits)+y*lock.Pitch,image.bgra.data()+size_t(y)*image.width*4,image.width*4);
        require(t->UnlockRect(0),"unlock bank texture");
        return t;
    }
    IDirect3DVertexBuffer9 *vertexBuffer(const void *bytes,size_t size,uint32_t fvf)
    {
        IDirect3DVertexBuffer9 *v=nullptr;
        require(device->CreateVertexBuffer(unsigned(size),D3DUSAGE_WRITEONLY,fvf,D3DPOOL_MANAGED,&v,nullptr),"create bank vertices");
        resources.push_back(v);
        void *p=nullptr; require(v->Lock(0,0,&p,0),"lock bank vertices"); std::memcpy(p,bytes,size); require(v->Unlock(),"unlock bank vertices");
        return v;
    }
    IDirect3DIndexBuffer9 *indexBuffer(const std::vector<uint32_t> &indices)
    {
        IDirect3DIndexBuffer9 *v=nullptr;
        require(device->CreateIndexBuffer(unsigned(indices.size()*4),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&v,nullptr),"create bank indices");
        resources.push_back(v);
        void *p=nullptr; require(v->Lock(0,0,&p,0),"lock bank indices"); std::memcpy(p,indices.data(),indices.size()*4); require(v->Unlock(),"unlock bank indices");
        return v;
    }
    void screenState()
    {
        device->SetPixelShader(nullptr); device->SetVertexShader(nullptr);
        for (unsigned s=0;s<16;++s) device->SetTexture(s,nullptr);
        device->SetRenderState(D3DRS_LIGHTING,0); device->SetRenderState(D3DRS_FOGENABLE,0);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE); device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE); device->SetRenderState(D3DRS_ZWRITEENABLE,0);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,0); device->SetRenderState(D3DRS_ALPHABLENDENABLE,0);
        device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,0); device->SetRenderState(D3DRS_STENCILENABLE,0);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE,0); device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);
        device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    }
    void quad(IDirect3DTexture9 *texture,float x,float y,float w,float h)
    {
        // Integer D3D9 pixel centres: the quad edges sit half a pixel outside the image.
        x-=.5f; y-=.5f;
        const ScreenVertex a={x,y,1,1,0,0},b={x+w,y,1,1,1,0},c={x+w,y+h,1,1,1,1},d={x,y+h,1,1,0,1};
        const ScreenVertex vertices[]={a,b,c,a,c,d};
        device->SetTexture(0,texture); device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        require(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(ScreenVertex)),"draw bank image");
    }
    // The banked depth goes back into the depth buffer exactly: one pretransformed vertex sits on
    // every pixel centre with that pixel's sampled depth as its z, so every centre rasterises to
    // its own vertex's value whatever the depth does between neighbours. A ring of clamped
    // vertices one pixel outside keeps the edge centres inside triangles.
    void createOcclusion()
    {
        const unsigned w=bank.width,h=bank.height,nx=w+2,ny=h+2;
        std::vector<ScreenDepth> vertices; std::vector<uint32_t> indices;
        vertices.reserve(size_t(nx)*ny); indices.reserve(size_t(nx-1)*(ny-1)*6);
        for (unsigned y=0;y<ny;++y) for (unsigned x=0;x<nx;++x) {
            const unsigned px=std::min(std::max(int(x)-1,0),int(w)-1),py=std::min(std::max(int(y)-1,0),int(h)-1);
            const float z=bank.depth[size_t(py)*w+px];
            vertices.push_back({float(x)-1.f,float(y)-1.f,std::isfinite(z)?std::min(std::max(z,0.f),1.f):1.f,1.f});
        }
        for (unsigned y=0;y+1<ny;++y) for (unsigned x=0;x+1<nx;++x) {
            const uint32_t a=y*nx+x,b=a+1,c=a+nx,e=c+1;
            indices.insert(indices.end(),{a,b,c,b,e,c});
        }
        occlusionVertexCount=unsigned(vertices.size()); occlusionIndexCount=unsigned(indices.size());
        occlusionVertices=vertexBuffer(vertices.data(),vertices.size()*sizeof(ScreenDepth),D3DFVF_XYZRHW);
        occlusionIndices=indexBuffer(indices);
    }
    void occlusion()
    {
        // Pretransformed z is written as it stands, so the viewport keeps the full depth range;
        // the banked samples already include the game's MinZ/MaxZ.
        const D3DVIEWPORT9 v={0,0,bank.width,bank.height,0,1};
        device->SetViewport(&v);
        device->SetTexture(0,nullptr); device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,0);
        device->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE); device->SetRenderState(D3DRS_ZWRITEENABLE,1);
        device->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);
        device->SetFVF(D3DFVF_XYZRHW); device->SetStreamSource(0,occlusionVertices,0,sizeof(ScreenDepth)); device->SetIndices(occlusionIndices);
        require(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,occlusionVertexCount,0,occlusionIndexCount/3),"draw bank occlusion depth");
    }
    void water(int reflection,float elapsed,bool edited,const WaterPreviewColor::Values &values,bool bubbles,int mode,float clarity)
    {
        for (size_t i=0;i<draws.size();++i) {
            const auto &d=bank.draws[i]; const auto &r=draws[i];
            for (unsigned s=0;s<256;++s) device->SetRenderState(D3DRENDERSTATETYPE(s),d.renderStates[s]);
            for (unsigned s=0;s<16;++s) {
                device->SetTexture(s,r.textures[s]);
                for (unsigned n=0;n<14;++n) device->SetSamplerState(s,D3DSAMPLERSTATETYPE(n),d.samplerStates[s][n]);
                if (s<PosixDevice9::TEXTURE_STAGE_COUNT)
                    for (unsigned n=0;n<33;++n) device->SetTextureStageState(s,D3DTEXTURESTAGESTATETYPE(n),d.stageStates[s][n]);
                D3DMATRIX matrix; std::memcpy(&matrix,d.textureMatrices[s].data(),sizeof(matrix));
                device->SetTransform(D3DTRANSFORMSTATETYPE(D3DTS_TEXTURE0+s),&matrix);
            }
            D3DMATRIX world,view,projection;
            std::memcpy(&world,d.world.data(),sizeof(world)); std::memcpy(&view,d.view.data(),sizeof(view)); std::memcpy(&projection,d.projection.data(),sizeof(projection));
            device->SetTransform(D3DTS_WORLD,&world); device->SetTransform(D3DTS_VIEW,&view); device->SetTransform(D3DTS_PROJECTION,&projection);
            const D3DVIEWPORT9 viewport={d.viewport.x,d.viewport.y,d.viewport.width,d.viewport.height,d.viewport.minZ,d.viewport.maxZ};
            device->SetViewport(&viewport);
            device->SetPixelShader(r.shader); device->SetVertexShader(nullptr);
            require(device->SetPixelShaderConstantF(0,d.pixelConstants.data(),unsigned(d.pixelConstants.size()/4)),"restore bank pixel constants");
            // Older banks can end before the optional optics register. Clear it explicitly
            // so a previous draw or comparison cannot leak its material into this replay.
            float optics[4]={};
            if(d.pixelConstants.size()>=(ModernWaterMaterial::RegisterOptics+1)*4)
                std::memcpy(optics,d.pixelConstants.data()+ModernWaterMaterial::RegisterOptics*4,sizeof(optics));
            if(clarity>=0.f) optics[0]=clarity;
            if (!d.vertexConstants.empty()) require(device->SetVertexShaderConstantF(0,d.vertexConstants.data(),unsigned(d.vertexConstants.size()/4)),"restore bank vertex constants");
            float water[4]; std::memcpy(water,d.pixelConstants.data()+4,sizeof(water)); water[0]+=elapsed;
            IDirect3DTexture9 *reflectionTexture=d.pixelConstants[6]>.5f && r.textures[1]?r.textures[1]:mirror;
            const bool usesMirror=mirror && reflection!=WaterPreviewReflection::ScreenSpace && reflectionTexture;
            water[2]=usesMirror?1.f:0.f;
            device->SetTexture(1,usesMirror?reflectionTexture:nullptr); device->SetPixelShaderConstantF(1,water,1);
            float parameters[4]; std::memcpy(parameters,d.pixelConstants.data()+95*4,sizeof(parameters));
            parameters[3]=reflection==WaterPreviewReflection::Mirror?0.f:1.f;
            if (edited) {
                const float colour[]={values.red,values.green,values.blue,values.brightness};
                device->SetPixelShaderConstantF(94,colour,1);
                parameters[0]=values.red<1 || values.green<1 || values.blue<1 || values.brightness<1?1.f:0.f;
                parameters[1]=values.strength; parameters[2]=bubbles?1.f:0.f;
            }
            const auto render=[&](bool baseline) {
                device->SetPixelShaderConstantF(95,parameters,1);
                const float baselineOptics[4]={};
                device->SetPixelShaderConstantF(ModernWaterMaterial::RegisterOptics,baseline?baselineOptics:optics,1);
                device->SetFVF(d.fvf); device->SetStreamSource(0,r.vertices,0,d.stride); device->SetIndices(r.indices);
                require(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,unsigned(d.vertices.size()/d.stride),0,unsigned(d.indices.size()/3)),"replay bank water");
            };
            if (mode==3) {
                const float material[4]={parameters[0],parameters[1],parameters[2],parameters[3]};
                for (unsigned half=0;half<2;++half) {
                    const unsigned left=std::max(viewport.X,half?bank.width/2:0u);
                    const unsigned right=std::min(viewport.X+viewport.Width,half?bank.width:bank.width/2);
                    if (right<=left) continue;
                    const D3DVIEWPORT9 crop={left,viewport.Y,right-left,viewport.Height,viewport.MinZ,viewport.MaxZ};
                    const float scale=float(viewport.Width)/crop.Width;
                    const float shift=(float(viewport.Width)-2.f*(left-viewport.X)-crop.Width)/crop.Width;
                    D3DMATRIX project=projection;
                    for (unsigned row=0;row<4;++row) project.m[row][0]=projection.m[row][0]*scale+projection.m[row][3]*shift;
                    device->SetTransform(D3DTS_PROJECTION,&project); device->SetViewport(&crop);
                    float area[4]; std::memcpy(area,d.pixelConstants.data()+8,sizeof(area));
                    area[0]=area[0]*scale+shift; area[2]=area[2]*scale+shift;
                    device->SetPixelShaderConstantF(2,area,1);
                    std::memcpy(parameters,material,sizeof(parameters));
                    if (!half) parameters[0]=parameters[1]=parameters[2]=0;
                    render(!half);
                }
            } else {
                if (mode==1) parameters[0]=parameters[1]=parameters[2]=0;
                render(mode==1);
            }
        }
    }
};
} // namespace WaterPreviewScene
