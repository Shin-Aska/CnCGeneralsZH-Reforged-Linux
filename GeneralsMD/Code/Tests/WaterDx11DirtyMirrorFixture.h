#pragma once
#include "WaterDx11SurfaceFixture.h"
#include "dx11runtime.h"
#include "dx11texture.h"
#include "texture.h"
#include "surfaceclass.h"
#include <d3d9.h>
#include <algorithm>
#include <d3dcompiler.h>
#include "Platform/WaterFoamShading.h"
#include "Platform/WaterShoreFoam.h"

namespace WaterDx11DirtyMirrorTest {
using namespace WaterDx11SurfaceTest;
constexpr unsigned Size=8;
struct EngineTexture {
    TextureClass *engine=NULL;
    ~EngineTexture(){if(engine)engine->Release_Ref();}
    IDirect3DBaseTexture9 *Raw() const{return engine?engine->Peek_D3D_Base_Texture():NULL;}
    bool Create(IDirect3DDevice9 *device,unsigned height){
        IDirect3DTexture9 *raw=NULL;
        if(FAILED(device->CreateTexture(Size,height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&raw,NULL)))return false;
        engine=new TextureClass(static_cast<IDirect3DBaseTexture9*>(raw));raw->Release();return engine!=NULL;
    }
    bool Write(unsigned top,unsigned bottom=0){
        SurfaceClass *surface=engine?engine->Get_Surface_Level():NULL;if(!surface)return false;
        int pitch=0;void *bits=surface->Lock(&pitch);const bool ok=bits&&pitch>=int(Size*4);
        if(ok)for(unsigned y=0;y<engine->Get_Height();++y){auto *row=reinterpret_cast<unsigned*>(static_cast<char*>(bits)+y*pitch);
            for(unsigned x=0;x<Size;++x)row[x]=y<Size?top:bottom;}
        if(bits)surface->Unlock();surface->Release_Ref();return ok;
    }
    WaterRenderPacket::Resource Descriptor() const {
        const unsigned height=engine?engine->Get_Height():0;
        return {Raw(),WaterRenderPacket::ResourceType::Texture2D,Raw()!=NULL,Size,height,1,Size*height};
    }
};
struct Runtime {
    HWND window=NULL;IDirect3D9 *d3d9=NULL;IDirect3DDevice9 *source=NULL;
    DX11DeviceClass *device=NULL;DX11BackendClass *backend=NULL;DX11WaterClass *water=NULL;
    bool wasEnabled=false,wasPresent=false,ownsRuntime=false;unsigned frame=0;
    ID3D11Texture2D *colour=NULL,*depth=NULL;ID3D11RenderTargetView *rtv=NULL;ID3D11DepthStencilView *dsv=NULL;
    EngineTexture white,black,terrain,optical,bed;
    WaterRenderBridge::PrepareRequest request={};WaterRenderBridge::BatchRequest batch={};
    struct Vertex{float x,y,z,nx,ny,nz;unsigned colour;float u,v,damping,level;};
    Vertex vertices[4];unsigned short indices[6]={0,1,2,0,2,3};WaterRenderBridge::CoveragePoint coverage[4];
    ~Runtime(){
        if(water)water->End(frame);if(device)device->Get_Context()->ClearState();
        Release(rtv);Release(dsv);Release(colour);Release(depth);
        // EngineTexture members destruct after this body; release them before global device teardown.
        for(EngineTexture *t:{&white,&black,&terrain,&optical,&bed})if(t->engine){t->engine->Release_Ref();t->engine=NULL;}
        if(ownsRuntime){Direct3D11_Release();Direct3D11_Enable(wasEnabled);Direct3D11_Present_Enable(wasPresent);}
        Release(source);Release(d3d9);if(window)DestroyWindow(window);
    }
    bool Initialise(){
        CHECK(!Direct3D11_Is_Active());if(Direct3D11_Is_Active())return false;
        wasEnabled=Direct3D11_Is_Enabled();wasPresent=Direct3D11_Present_Is_Enabled();ownsRuntime=true;
        const char *cache=std::getenv("ZH_TEST_WATER_CACHE_DIR");CHECK(cache&&*cache);
        const DWORD attributes=cache&&*cache?GetFileAttributesA(cache):INVALID_FILE_ATTRIBUTES;
        CHECK(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0);
        if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)==0)return false;
        Direct3D11_Set_Shader_Cache_Directory(cache);Direct3D11_Enable(true);Direct3D11_Present_Enable(false);
        window=CreateWindowExA(0,"STATIC","water-dirty-mirror",WS_OVERLAPPED,0,0,64,48,NULL,NULL,GetModuleHandle(NULL),NULL);
        if(!window)return false;d3d9=Direct3DCreate9(D3D_SDK_VERSION);if(!d3d9)return false;
        D3DDISPLAYMODE mode={};if(FAILED(d3d9->GetAdapterDisplayMode(D3DADAPTER_DEFAULT,&mode)))return false;
        D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=48;p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;
        p.BackBufferFormat=mode.Format;p.hDeviceWindow=window;
        if(FAILED(d3d9->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&p,&source)))return false;
        if(!Direct3D11_Create(window,64,48))return false;
        Direct3D11_Device()->Request_Debug_Layer();Direct3D11_Release();
        if(!Direct3D11_Create(window,64,48))return false;
        device=Direct3D11_Device();backend=Direct3D11_Backend();water=backend?backend->Water_Owner():NULL;
        CHECK(device&&backend&&water);if(!device||!backend||!water)return false;
        CHECK(device->Has_Debug_Layer());if(!device->Has_Debug_Layer())return false;
        const char *expected=std::getenv("ZH_TEST_DX11_EXPECT_DRIVER");CHECK(expected!=NULL);
        if(!expected)return false;CHECK(_stricmp(expected,"warp")==0||_stricmp(expected,"hardware")==0);
        CHECK(device->Is_Software()==(_stricmp(expected,"warp")==0));
        std::printf("  dirty actual-driver=%s debug=%u global-active=%u D3D9-source=HAL/software-VP\n",device->Is_Software()?"warp":"hardware",device->Has_Debug_Layer(),Direct3D11_Is_Active());
        return white.Create(source,Size)&&black.Create(source,Size)&&terrain.Create(source,Size)&&optical.Create(source,Size)&&bed.Create(source,Size)
            &&white.Write(0xffffffff)&&black.Write(0xff000000)&&terrain.Write(0x0080ff80)&&optical.Write(0)&&bed.Write(0xff808073);
    }
    bool Scene(EngineTexture &texture,bool atlas=false,unsigned channels=7){
        water->End(frame);water->Advance_Presented_Frame();++frame;auto *c=device->Get_Context();c->ClearState();backend->Forget_Bindings();
        Release(rtv);Release(dsv);Release(colour);Release(depth);
        std::vector<Pixel> pixels(64*48,Pixel{.32f,.42f,.52f,1});std::vector<float> depths(64*48,1);
        D3D11_TEXTURE2D_DESC d={};d.Width=64;d.Height=48;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data={pixels.data(),64*sizeof(Pixel),0};
        if(FAILED(device->Get_Device()->CreateTexture2D(&d,&data,&colour))||FAILED(device->Get_Device()->CreateRenderTargetView(colour,NULL,&rtv)))return false;
        d.Format=DXGI_FORMAT_R32_TYPELESS;d.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;data.pSysMem=depths.data();data.SysMemPitch=64*sizeof(float);
        D3D11_DEPTH_STENCIL_VIEW_DESC dd={};dd.Format=DXGI_FORMAT_D32_FLOAT;dd.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        if(FAILED(device->Get_Device()->CreateTexture2D(&d,&data,&depth))||FAILED(device->Get_Device()->CreateDepthStencilView(depth,&dd,&dsv)))return false;
        c->OMSetRenderTargets(1,&rtv,dsv);D3D11_VIEWPORT vp={.5f,.5f,64,48,0,1};c->RSSetViewports(1,&vp);
        const Vertex quad[4]={{-32,-32,0,0,0,1,0xffffffff,0,1,0,0},{-32,32,0,0,0,1,0xffffffff,0,0,0,0},
            {32,32,0,0,0,1,0xffffffff,1,0,0,0},{32,-32,0,0,0,1,0xffffffff,1,1,0,0}};
        std::memcpy(vertices,quad,sizeof(quad));const WaterRenderBridge::CoveragePoint polygon[4]={{-32,-32},{32,-32},{32,32},{-32,32}};std::memcpy(coverage,polygon,sizeof(polygon));
        request={};request.frameId=frame;request.resourceGeneration=1;request.captureSerial=frame;request.patchId=frame;request.viewIdentity=0x12345678;
        request.targetWidth=64;request.targetHeight=48;request.batches=&batch;request.batchCount=request.batchCapacity=1;
        request.coverage=coverage;request.coverageCount=request.coverageCapacity=4;request.fftEligibility=WaterRenderBridge::FftEligibility::Ineligible;
        batch={};batch.partId=1;batch.vertexLayout=0x252;batch.vertexStride=sizeof(Vertex);batch.vertexCount=4;batch.vertexBytes=sizeof(vertices);batch.vertices=vertices;
        batch.indexFormat=WaterRenderBridge::IndexFormat::UInt16;batch.topology=WaterRenderBridge::Topology::TriangleList;batch.indices=indices;batch.indexBytes=sizeof(indices);batch.indexCount=6;batch.primitiveCount=2;
        auto &scene=request.inputs.scene;WaterRenderPacket::Identity(scene.view);WaterRenderPacket::Identity(scene.projection);
        scene.view[14]=-96;scene.projection[0]=scene.projection[5]=1.f/32;scene.projection[10]=-1.f/128;scene.viewport={0,0,64,48,0,1};
        scene.combiner[0]={1,1,1,1};scene.sunDirection={0,0,-1,0};scene.sunColour={1,1,1,0};for(unsigned i=0;i<4;++i)scene.shadowFromClip[i][i]=1;
        auto &s=request.inputs.surface;s.reflectionArea={-32,-32,32,32};s.waterParameters={1.375f,0,1,0};s.terrainParameters={0,64,-255.f/128,1};
        s.opticalParameters={8,.2f,0,1};s.hullParameters={0,.2f,.2f,.2f};s.authoredMaterialAvailable=s.reflectionAvailable=s.heightWaveCageAvailable=s.terrainAvailable=s.opticalAvailable=s.bedAvailable=true;
        s.authoredMaterial=white.Descriptor();s.reflection=black.Descriptor();s.heightWaveCage=(atlas?terrain:texture).Descriptor();s.terrainHeight=white.Descriptor();s.optical=optical.Descriptor();s.bed=bed.Descriptor();
        if(atlas){s.foamHistory=texture.Descriptor();s.foamHistoryAvailable=true;s.foamHistoryArea={-32,-32,1.f/64,1.f/64};s.foamHistoryParameters={float(channels),0,8,1.375f};}
        backend->Set_Transform(D3DTS_VIEW,scene.view);backend->Set_Transform(D3DTS_PROJECTION,scene.projection);backend->Set_Render_State(D3DRS_FOGENABLE,FALSE);
        backend->Set_Texture(3,Mirror(white));backend->Set_Texture_Stage_State(3,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACEPOSITION);backend->Set_Texture_Stage_State(3,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2);
        return true;
    }
    ID3D11ShaderResourceView *Mirror(EngineTexture &texture){return DX11Texture_Mirror(device->Get_Device(),device->Get_Context(),texture.Raw());} // borrowed
    bool ReadWords(ID3D11ShaderResourceView *view,std::vector<unsigned> &words){
        if(!view)return false;ID3D11Resource *resource=NULL;view->GetResource(&resource);if(!resource)return false;
        ID3D11Texture2D *sourceTexture=NULL;HRESULT hr=resource->QueryInterface(__uuidof(ID3D11Texture2D),reinterpret_cast<void**>(&sourceTexture));Release(resource);if(FAILED(hr))return false;
        D3D11_TEXTURE2D_DESC d={};sourceTexture->GetDesc(&d);CHECK_EQ(d.Format,DXGI_FORMAT_B8G8R8A8_UNORM);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;ID3D11Texture2D *staging=NULL;
        bool ok=SUCCEEDED(device->Get_Device()->CreateTexture2D(&d,NULL,&staging));if(ok){auto *c=device->Get_Context();c->CopyResource(staging,sourceTexture);D3D11_MAPPED_SUBRESOURCE m={};ok=SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&m));
            if(ok){words.resize(d.Width*d.Height);for(unsigned y=0;y<d.Height;++y)std::memcpy(words.data()+y*d.Width,static_cast<char*>(m.pData)+y*m.RowPitch,d.Width*4);c->Unmap(staging,0);}}
        Release(staging);Release(sourceTexture);return ok;
    }
    bool ReadPixels(std::vector<Pixel> &pixels){
        D3D11_TEXTURE2D_DESC d={};colour->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D *staging=NULL;if(FAILED(device->Get_Device()->CreateTexture2D(&d,NULL,&staging)))return false;
        auto *c=device->Get_Context();c->CopyResource(staging,colour);D3D11_MAPPED_SUBRESOURCE m={};bool ok=SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&m));
        if(ok){pixels.resize(64*48);for(unsigned y=0;y<48;++y)std::memcpy(pixels.data()+y*64,static_cast<char*>(m.pData)+y*m.RowPitch,64*sizeof(Pixel));c->Unmap(staging,0);}
        Release(staging);return ok;
    }
    bool Draw(std::vector<Pixel> &pixels,EngineTexture *expectedCage=NULL){
        WaterRenderBridge::PreparedPatch prepared={};const auto status=water->PreparePatch(request,prepared);CHECK_EQ(status,WaterRenderBridge::PrepareStatus::Ready);if(status!=WaterRenderBridge::PrepareStatus::Ready)return false;
        if(expectedCage){auto *retained=water->Get_Test_Prepared_Vertex_Resource(prepared.patchId,3);CHECK(retained!=NULL);CHECK_EQ(retained,Mirror(*expectedCage));}
        const auto receipt=water->SubmitPreparedPatch(prepared);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);CHECK(WaterRenderBridge::Truthful(receipt,prepared));
        const bool read=ReadPixels(pixels);CHECK(read);if(!read)return false;unsigned changed=0;
        for(auto &p:pixels){CHECK(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a));changed+=std::fabs(p.r-.32f)>.001f;}CHECK(changed>100);return true;
    }
};
inline void Words(Runtime &f,EngineTexture &texture,unsigned top,unsigned bottom=0){
    std::vector<unsigned> words;CHECK(f.ReadWords(f.Mirror(texture),words));CHECK_EQ(words.size(),Size*texture.engine->Get_Height());
    for(unsigned y=0;y<texture.engine->Get_Height();++y)for(unsigned x=0;x<Size;++x)if(y*Size+x<words.size())CHECK_EQ(words[y*Size+x],y<Size?top:bottom);
    std::printf("  dirty GPU words top=%08x bottom=%08x count=%zu\n",top,bottom,words.size());
}
inline float Contrast(const std::vector<Pixel> &a,const std::vector<Pixel> &b){
    CHECK_EQ(a.size(),64u*48u);CHECK_EQ(b.size(),a.size());if(a.size()!=64*48||b.size()!=a.size())return 0;
    float peak=0;for(unsigned y=16;y<32;++y)for(unsigned x=28;x<36;++x){const unsigned n=y*64+x;
        peak=(std::max)(peak,(std::max)({std::fabs(a[n].r-b[n].r),std::fabs(a[n].g-b[n].g),std::fabs(a[n].b-b[n].b)}));}return peak;
}
// Isolated known displacement, already independently characterized by Task7 DC raster.
// Only cage sampling/routing is claimed; this override does not prove spectrum generation.
struct DisplacementControl {
    Runtime &f;ID3D11Texture2D *maps[3]={},*array=NULL;ID3D11ShaderResourceView *views[3]={},*arrayView=NULL;
    explicit DisplacementControl(Runtime &runtime):f(runtime){}
    ~DisplacementControl(){if(f.water)f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL);for(auto &p:views)Release(p);for(auto &p:maps)Release(p);Release(arrayView);Release(array);}
    bool Initialise(){
        if(!f.water->GetCapability().Available())return false;
        D3D11_TEXTURE2D_DESC d={};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Pixel pixels[6]={};pixels[0].g=.02f;auto *device=f.device->Get_Device();
        for(unsigned i=0;i<3;++i){D3D11_SUBRESOURCE_DATA data={&pixels[i*2],sizeof(Pixel),0};
            if(FAILED(device->CreateTexture2D(&d,&data,&maps[i]))||FAILED(device->CreateShaderResourceView(maps[i],NULL,&views[i])))return false;}
        d.ArraySize=6;D3D11_SUBRESOURCE_DATA data[6]={};for(unsigned i=0;i<6;++i){data[i].pSysMem=&pixels[i];data[i].SysMemPitch=sizeof(Pixel);}
        if(FAILED(device->CreateTexture2D(&d,data,&array))||FAILED(device->CreateShaderResourceView(array,NULL,&arrayView)))return false;
        return f.water->Set_Test_Fft_Fields(views[0],views[1],views[2],arrayView);
    }
    void Geometry(){
        f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
        for(auto &v:f.vertices){v.nx=v.ny=v.nz=1;v.damping=1;}
        auto &p=f.request.inputs.scene.projection;p[9]=.25f;p[13]=24;
        f.backend->Set_Transform(D3DTS_PROJECTION,p);
    }
};
inline bool Background(const Pixel &p){return std::fabs(p.r-.32f)+std::fabs(p.g-.42f)+std::fabs(p.b-.52f)<.0001f;}
// Shared atlas tile/shore code rendered, not a CPU copy of either shader equation.
inline bool SampleHistory(Runtime &f,EngineTexture &texture,unsigned mask,Pixel (&samples)[3],float level=0){
    HMODULE module=LoadLibraryA("d3dcompiler_47.dll");
    typedef HRESULT(WINAPI *Compile)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    Compile compile=module?reinterpret_cast<Compile>(GetProcAddress(module,"D3DCompile")):NULL;
    ID3DBlob *vcode=NULL,*pcode=NULL,*errors=NULL;ID3D11VertexShader *vs=NULL;ID3D11PixelShader *ps=NULL;
    ID3D11Texture2D *target=NULL,*staging=NULL;ID3D11RenderTargetView *rtv=NULL;ID3D11Buffer *cb=NULL;ID3D11SamplerState *sampler=NULL;
    const char *vsource="float4 main(uint id:SV_VertexID):SV_Position{return float4(id==2?3:-1,id==1?3:-1,0,1);}";
    std::string psource="cbuffer C:register(b0){float4 FoamHistoryArea;float4 FoamHistoryParameters;}Texture2D Texture14:register(t14);SamplerState Sampler14:register(s14);";
    psource+=WaterFoamShading::HistorySamplingSource();psource+=WaterShoreFoam::HistorySource();
    psource+="float4 main(float4 p:SV_Position):SV_Target{if(p.x<1)return waterFoamHistoryTile(float2(.5,.5),0);if(p.x<2)return waterFoamHistoryTile(float2(.5,.5),1);return waterShoreHistory(float2(0,0),0,1.375).xxxx;}";
    bool ok=compile&&SUCCEEDED(compile(vsource,std::strlen(vsource),"dirty-tile-vs",NULL,NULL,"main","vs_5_0",0,0,&vcode,&errors));
    if(errors){std::printf("  dirty shader compiler=%s\n",static_cast<char*>(errors->GetBufferPointer()));Release(errors);}
    if(ok)ok=SUCCEEDED(compile(psource.data(),psource.size(),"dirty-shared-tile-ps",NULL,NULL,"main","ps_5_0",0,0,&pcode,&errors));
    if(errors){std::printf("  dirty shader compiler=%s\n",static_cast<char*>(errors->GetBufferPointer()));Release(errors);}
    auto *d=f.device->Get_Device();auto *c=f.device->Get_Context();
    if(ok)ok=SUCCEEDED(d->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),NULL,&vs))&&SUCCEEDED(d->CreatePixelShader(pcode->GetBufferPointer(),pcode->GetBufferSize(),NULL,&ps));
    D3D11_TEXTURE2D_DESC td={};td.Width=3;td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    if(ok)ok=SUCCEEDED(d->CreateTexture2D(&td,NULL,&target))&&SUCCEEDED(d->CreateRenderTargetView(target,NULL,&rtv));
    const WaterShaderLayout::Float4 values[2]={{-32,-32,1.f/64,1.f/64},{float(mask),level,8,1.375f}};
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=sizeof(values);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;D3D11_SUBRESOURCE_DATA initial={values,0,0};
    if(ok)ok=SUCCEEDED(d->CreateBuffer(&bd,&initial,&cb));D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
    if(ok)ok=SUCCEEDED(d->CreateSamplerState(&sd,&sampler));
    if(ok){c->ClearState();c->OMSetRenderTargets(1,&rtv,NULL);D3D11_VIEWPORT vp={0,0,3,1,0,1};c->RSSetViewports(1,&vp);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs,NULL,0);c->PSSetShader(ps,NULL,0);c->PSSetConstantBuffers(0,1,&cb);auto *view=f.Mirror(texture);c->PSSetShaderResources(14,1,&view);c->PSSetSamplers(14,1,&sampler);c->Draw(3,0);
        td.Usage=D3D11_USAGE_STAGING;td.BindFlags=0;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ok=SUCCEEDED(d->CreateTexture2D(&td,NULL,&staging));
        if(ok){c->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE m={};ok=SUCCEEDED(c->Map(staging,0,D3D11_MAP_READ,0,&m));if(ok){std::memcpy(samples,m.pData,sizeof(samples));c->Unmap(staging,0);}}}
    c->ClearState();f.backend->Forget_Bindings();Release(sampler);Release(cb);Release(rtv);Release(staging);Release(target);Release(ps);Release(vs);Release(pcode);Release(vcode);Release(errors);if(module)FreeLibrary(module);return ok;
}
}
