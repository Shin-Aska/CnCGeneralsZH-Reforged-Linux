#pragma once
#include "WaterDx11SurfaceFixture.h"
#include "Platform/WaterHullVisual.h"
#include "Platform/WaterHullVisualLayout.h"
#include "Platform/WaterGpuShaders.h"
#include "engineshader.h"
#include <d3dcompiler.h>
#include <algorithm>

namespace WaterDx11HullTest {
using namespace WaterDx11SurfaceTest;
using WaterShaderLayout::Float4;
const Pixel Background={.32f,.42f,.52f,1};
// Fixed before execution: two RGBA8 quanta, and a central support strip/distant strip.
constexpr float Contrast=2.f/255.f;
struct Vertex {float x,y,z,nx,ny,nz;unsigned colour;float u,v,damping,level;};
struct Pose {
    WaterHullVisual::Point3 vertices[4];
    Float4 mean[10]={},contact[10]={},metadata[1]={},packets[88]={};
    bool Set(float z,float shiftX=0) {
        vertices[0]={shiftX,-16,-8+z};vertices[1]={shiftX,16,-8+z};
        vertices[2]={shiftX,16,8+z};vertices[3]={shiftX,-16,8+z};
        WaterHullVisual::Builder builder(-64,64);WaterHullVisual::Packet mesh={};
        if(!builder.AddTriangle(vertices[0],vertices[1],vertices[2])
            ||!builder.AddTriangle(vertices[0],vertices[2],vertices[3])||!builder.Finish(mesh))return false;
        std::memcpy(metadata,mesh.metadata,sizeof(metadata));std::memcpy(packets,mesh.data,sizeof(packets));
        mean[0]={0,0,0,4};mean[1]={1,0,20,20};mean[2]={24,0,.375f,0};
        mean[4]={-20,-20,20,-20};mean[5]={20,20,-20,20};
        std::memcpy(contact,mean,sizeof(mean));return WaterHullVisual::Valid(mesh);
    }
    // Independent edge/plane intersections: no production Query/foam equation.
    unsigned Intersections(double &minY,double &maxY) const {
        const unsigned triangles[2][3]={{0,1,2},{0,2,3}};unsigned count=0;minY=1e9;maxY=-1e9;
        for(const auto &triangle:triangles)for(unsigned e=0;e<3;++e){
            const auto a=vertices[triangle[e]],b=vertices[triangle[(e+1)%3]];
            if((a.z<0&&b.z>0)||(a.z>0&&b.z<0)){
                const double t=-double(a.z)/(double(b.z)-a.z),y=a.y+t*(double(b.y)-a.y);
                const double x=a.x+t*(double(b.x)-a.x);CHECK_NEAR(x,vertices[0].x,1e-12);
                minY=(std::min)(minY,y);maxY=(std::max)(maxY,y);++count;
            }
        }return count;
    }
};
inline bool Scene(Fixture &f,Vertex (&v)[4],Pose *pose,bool mesh=true) {
    if(!f.Scene(Background,1))return false;
    if(!f.optical.Set(f.device.Get_Device(),{1,0,0,0}))return false;
    f.request.inputs.surface.opticalParameters.z=1;f.BindResources();
    const Vertex quad[4]={{-32,-32,0,0,0,1,0xffffffff,0,1,0,0},
        {-32,32,0,0,0,1,0xffffffff,0,0,0,0},{32,32,0,0,0,1,0xffffffff,1,0,0,0},
        {32,-32,0,0,0,1,0xffffffff,1,1,0,0}};
    std::memcpy(v,quad,sizeof(v));f.batches[0].vertices=v;f.batches[0].vertexBytes=sizeof(v);
    f.batches[0].vertexStride=sizeof(Vertex);f.batches[0].vertexLayout=0x252;
    for(auto &p:f.coverage){p.x*=32;p.y*=32;}
    auto &in=f.request.inputs;in.scene.view[14]=-96;in.scene.projection[0]=in.scene.projection[5]=1.f/32;
    in.scene.projection[10]=-1.f/128;in.surface.reflectionArea={-32,-32,32,32};
    in.surface.waterParameters={1.375f,0,1,0};f.request.fftEligibility=WaterRenderBridge::FftEligibility::Ineligible;
    if(pose){in.surface.hullParameters.x=1;in.surface.meanHulls={pose->mean,10,10};
        in.surface.contactHulls={pose->contact,10,10};in.surface.visualHullMetadata={pose->metadata,1,1};
        in.surface.visualHullPackets={pose->packets,88,88};in.surface.meshContactAvailable=mesh;}
    f.backend.Set_Transform(D3DTS_VIEW,in.scene.view);f.backend.Set_Transform(D3DTS_PROJECTION,in.scene.projection);
    for(unsigned slot:{0u,3u}){f.backend.Set_Sampler_State(slot,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        f.backend.Set_Sampler_State(slot,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        f.backend.Set_Sampler_State(slot,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
        f.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        f.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    return true;
}
inline float Difference(const Pixel &a,const Pixel &b) {
    return (std::max)({std::fabs(a.r-b.r),std::fabs(a.g-b.g),std::fabs(a.b-b.b),std::fabs(a.a-b.a)});
}
inline float Region(const std::vector<Pixel> &a,const std::vector<Pixel> &b,bool distant=false) {
    if(a.size()!=Fixture::Width*Fixture::Height||a.size()!=b.size()){CHECK(false);return 0;}
    float peak=0;for(unsigned y=16;y<32;++y)for(unsigned x=distant?56:28;x<(distant?60u:36u);++x)
        peak=(std::max)(peak,Difference(a[y*Fixture::Width+x],b[y*Fixture::Width+x]));return peak;
}
inline void Finite(const std::vector<Pixel> &pixels) {
    CHECK_EQ(pixels.size(),Fixture::Width*Fixture::Height);unsigned changed=0;
    for(const auto &p:pixels){CHECK(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a));
        changed+=Difference(p,Background)>.001f;}CHECK(changed>100);
}
// A test-only renderer of the unchanged shared surface PS with one intentional variant
// difference. Retain actual prepared VS/constant buffers BEFORE Submit consumes them.
class SharedControl {
    Fixture &f;HMODULE module=NULL;
    ID3D11VertexShader *vs=NULL;ID3D11PixelShader *ps[2]={};ID3D11InputLayout *layout=NULL;
    ID3D11Buffer *vb=NULL,*ib=NULL,*vc[2]={},*hc[3]={};
    ID3D11Texture2D *snapshot[2]={},*array=NULL;ID3D11ShaderResourceView *sv[2]={},*arrayView=NULL;
    ID3D11SamplerState *linear=NULL,*point=NULL,*repeat=NULL;
    ID3D11BlendState *blend=NULL;ID3D11DepthStencilState *depth=NULL;ID3D11RasterizerState *raster=NULL;
    bool Buffer(const void *data,unsigned bytes,unsigned bind,ID3D11Buffer **out){
        D3D11_BUFFER_DESC d={};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA s={data,0,0};return SUCCEEDED(f.device.Get_Device()->CreateBuffer(&d,&s,out));}
public:
    explicit SharedControl(Fixture &fixture):f(fixture){}
    ~SharedControl(){f.device.Get_Context()->ClearState();f.backend.Forget_Bindings();
        Release(vs);for(auto &p:ps)Release(p);Release(layout);Release(vb);Release(ib);
        for(auto &p:vc)Release(p);for(auto &p:hc)Release(p);for(auto &p:sv)Release(p);
        for(auto &p:snapshot)Release(p);Release(arrayView);Release(array);Release(linear);Release(point);Release(repeat);
        Release(blend);Release(depth);Release(raster);if(module)FreeLibrary(module);}
    bool Initialise(const WaterRenderBridge::PreparedPatch &patch,const Vertex (&v)[4]) {
        auto *device=f.device.Get_Device();auto *context=f.device.Get_Context();std::vector<unsigned char> code;
        if(!f.water->Get_Test_Prepared_Vertex_Bytecode(patch.patchId,code)||code.empty())return false;
        vc[0]=f.water->Get_Test_Prepared_Vertex_Constant_Buffer(patch.patchId);
        vc[1]=f.water->Get_Test_Prepared_Pixel_Constant_Buffer(patch.patchId);
        if(vc[0])vc[0]->AddRef();if(!vc[0]||!vc[1])return false;
        if(FAILED(device->CreateVertexShader(code.data(),code.size(),NULL,&vs)))return false;
        const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0},
            {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0}};
        if(FAILED(device->CreateInputLayout(elements,5,code.data(),code.size(),&layout))
            ||!Buffer(v,sizeof(v),D3D11_BIND_VERTEX_BUFFER,&vb)||!Buffer(f.indices,sizeof(f.indices),D3D11_BIND_INDEX_BUFFER,&ib))return false;
        WaterRenderPacket::DrawPacket packet={};if(!WaterRenderPacket::Pack(f.request.inputs,packet))return false;
        for(unsigned block=0;block<3;++block)if(!Buffer(packet.hullPackets.registers+WaterHullVisualLayout::BlockOffset(block),
            WaterHullVisualLayout::BlockRegisters(block)*sizeof(Float4),D3D11_BIND_CONSTANT_BUFFER,&hc[block]))return false;
        module=LoadLibraryA("d3dcompiler_47.dll");
        typedef HRESULT(WINAPI *Compile)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
        Compile compile=module?reinterpret_cast<Compile>(GetProcAddress(module,"D3DCompile")):NULL;if(!compile)return false;
        // Serial harness; full HLSL key, fixed d3dcompiler_47/main/ps_5_0/flags 0,0. CPU bytes only.
        static std::string cachedSource[2];static std::vector<unsigned char> cachedBytes[2];
        for(unsigned mesh=0;mesh<2;++mesh){WaterGpuShaders::Variant variant;variant.materialFvf=0x252;
            variant.shoreFoam=false;variant.meshContact=mesh!=0;PixelPipelineDescription pipeline={};pipeline.AlphaFunction=D3DCMP_ALWAYS;
            std::string source;if(!WaterGpuShaders::GenerateSurfacePixel(variant,pipeline,source))return false;
            const bool hit=cachedSource[mesh]==source&&!cachedBytes[mesh].empty();
            std::printf("  control bytecode variant=%u cache=%s\n",mesh,hit?"HIT":"MISS");
            if(!hit){ID3DBlob *blob=NULL,*errors=NULL;
                const HRESULT hr=compile(source.data(),source.size(),"task8-shared-control",NULL,NULL,"main","ps_5_0",0,0,&blob,&errors);
                if(errors){std::printf("  hull control compiler=%s\n",static_cast<const char*>(errors->GetBufferPointer()));Release(errors);}
                const bool ok=SUCCEEDED(hr)&&blob&&blob->GetBufferSize()>0;
                if(ok){const auto *bytes=static_cast<const unsigned char*>(blob->GetBufferPointer());
                    cachedBytes[mesh].assign(bytes,bytes+blob->GetBufferSize());cachedSource[mesh]=source;}
                Release(blob);if(!ok)return false;}
            if(FAILED(device->CreatePixelShader(cachedBytes[mesh].data(),cachedBytes[mesh].size(),NULL,&ps[mesh])))return false;}
        ID3D11Texture2D *sources[]={f.colourSurface,f.depthSurface};
        for(unsigned i=0;i<2;++i){D3D11_TEXTURE2D_DESC d={};sources[i]->GetDesc(&d);d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            if(FAILED(device->CreateTexture2D(&d,NULL,&snapshot[i])))return false;context->CopyResource(snapshot[i],sources[i]);
            D3D11_SHADER_RESOURCE_VIEW_DESC s={};s.Format=i?DXGI_FORMAT_R32_FLOAT:d.Format;s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;s.Texture2D.MipLevels=1;
            if(FAILED(device->CreateShaderResourceView(snapshot[i],&s,&sv[i])))return false;}
        D3D11_TEXTURE2D_DESC ad={};ad.Width=ad.Height=ad.MipLevels=ad.SampleDesc.Count=1;ad.ArraySize=6;
        ad.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;ad.Usage=D3D11_USAGE_IMMUTABLE;ad.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Pixel zero={};D3D11_SUBRESOURCE_DATA data[6]={};for(auto &d:data){d.pSysMem=&zero;d.SysMemPitch=sizeof(zero);}
        if(FAILED(device->CreateTexture2D(&ad,data,&array))||FAILED(device->CreateShaderResourceView(array,NULL,&arrayView)))return false;
        D3D11_SAMPLER_DESC s={};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
        if(FAILED(device->CreateSamplerState(&s,&linear)))return false;s.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        if(FAILED(device->CreateSamplerState(&s,&point)))return false;s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;
        if(FAILED(device->CreateSamplerState(&s,&repeat)))return false;
        D3D11_BLEND_DESC b={};auto &rt=b.RenderTarget[0];rt.BlendEnable=TRUE;rt.SrcBlend=D3D11_BLEND_SRC_ALPHA;rt.DestBlend=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ONE;rt.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;rt.BlendOpAlpha=D3D11_BLEND_OP_ADD;rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_DEPTH_STENCIL_DESC d={};d.DepthEnable=TRUE;d.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ZERO;d.DepthFunc=D3D11_COMPARISON_LESS_EQUAL;
        D3D11_RASTERIZER_DESC r={};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
        return SUCCEEDED(device->CreateBlendState(&b,&blend))&&SUCCEEDED(device->CreateDepthStencilState(&d,&depth))&&SUCCEEDED(device->CreateRasterizerState(&r,&raster));
    }
    bool Draw(bool mesh,std::vector<Pixel> &pixels){
        auto *c=f.device.Get_Context();c->ClearState();const float background[]={Background.r,Background.g,Background.b,Background.a};
        c->ClearRenderTargetView(f.colourView,background);c->OMSetRenderTargets(1,&f.colourView,f.depthView);
        D3D11_VIEWPORT viewport={.5f,.5f,float(Fixture::Width),float(Fixture::Height),0,1};c->RSSetViewports(1,&viewport);
        c->IASetInputLayout(layout);UINT stride=sizeof(Vertex),offset=0;c->IASetVertexBuffers(0,1,&vb,&stride,&offset);
        c->IASetIndexBuffer(ib,DXGI_FORMAT_R16_UINT,0);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs,NULL,0);c->VSSetConstantBuffers(0,2,vc);c->PSSetShader(ps[mesh?1:0],NULL,0);
        ID3D11Buffer *constants[]={vc[1],hc[0],hc[1],hc[2]};c->PSSetConstantBuffers(0,4,constants);
        ID3D11ShaderResourceView *resources[16]={f.base.view,f.mirror.view,f.height.view,f.edge.view,f.optical.view,sv[0],sv[1],f.bed.view,arrayView,f.base.view,f.mirror.view,NULL,NULL,NULL,f.mirror.view,f.mirror.view};
        ID3D11SamplerState *samplers[16];for(auto &p:samplers)p=linear;samplers[6]=samplers[9]=point;samplers[7]=repeat;
        c->PSSetShaderResources(0,16,resources);c->PSSetSamplers(0,16,samplers);
        const float factors[]={1,1,1,1};c->OMSetBlendState(blend,factors,~0u);c->OMSetDepthStencilState(depth,0);c->RSSetState(raster);
        c->DrawIndexed(6,0,0);ID3D11ShaderResourceView *empty[16]={};c->PSSetShaderResources(0,16,empty);
        return f.Read(pixels);
    }
};
struct Result {std::vector<Pixel> production,disabled,enabled;};
inline bool Render(Fixture &f,Pose *pose,Result &out,bool mesh=true){
    Vertex vertices[4];const bool scene=Scene(f,vertices,pose,mesh);CHECK(scene);if(!scene)return false;
    WaterRenderBridge::PreparedPatch patch={};const auto status=f.water->PreparePatch(f.request,patch);CHECK_EQ(status,WaterRenderBridge::PrepareStatus::Ready);
    if(status!=WaterRenderBridge::PrepareStatus::Ready)return false;
    SharedControl control(f);if(!control.Initialise(patch,vertices)){CHECK(false);return false;}
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK(WaterRenderBridge::Truthful(receipt,patch));CHECK_EQ(receipt.commandsIssued,1u);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);const bool read=f.Read(out.production);CHECK(read);if(receipt.status!=WaterRenderBridge::SubmitStatus::Issued||!read)return false;
    const bool disabled=control.Draw(false,out.disabled),enabled=control.Draw(true,out.enabled);CHECK(disabled);CHECK(enabled);if(!disabled||!enabled)return false;
    Finite(out.production);Finite(out.disabled);Finite(out.enabled);return true;
}
}
