#pragma once
#include "WaterDx11SurfaceFixture.h"
#include "Platform/WaterGpuShaders.h"
#include "Platform/WaterHullVisual.h"
#include "Platform/WaterHullVisualLayout.h"
#include "engineshader.h"
#include "Platform/WaterFftGeometry.h"
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <algorithm>

namespace WaterDx11VesselTest {
using namespace WaterDx11SurfaceTest;
using WaterShaderLayout::Float4;
const Pixel Background={.32f,.42f,.52f,1};
struct Vertex{float x,y,z,nx,ny,nz;unsigned colour;float u,v,damping,level;};
struct CapturedVertex {float clip[4],metadata[4],originalXY[2];};
struct SavedInputs {
    std::vector<unsigned char> code;ID3D11Buffer *constants[2]={};ID3D11ShaderResourceView *maps[4]={};
    ~SavedInputs(){for(auto &p:constants)Release(p);for(auto &p:maps)Release(p);}
    bool Capture(Fixture &f,const WaterRenderBridge::PreparedPatch &patch){
        if(!f.water->Get_Test_Prepared_Vertex_Bytecode(patch.patchId,code)||code.empty())return false;
        constants[0]=f.water->Get_Test_Prepared_Vertex_Constant_Buffer(patch.patchId);if(constants[0])constants[0]->AddRef();
        constants[1]=f.water->Get_Test_Prepared_Pixel_Constant_Buffer(patch.patchId); // owned getter
        for(unsigned i=0;i<4;++i){maps[i]=f.water->Get_Test_Prepared_Vertex_Resource(patch.patchId,i);if(maps[i])maps[i]->AddRef();}
        return constants[0]&&constants[1]&&maps[0]&&maps[1]&&maps[2]&&maps[3];
    }
};
inline bool CaptureVertices(Fixture &f,const Vertex (&vertices)[4],const SavedInputs &saved,CapturedVertex (&captured)[4],const std::vector<unsigned char> *canonical=NULL)
{
    const std::vector<unsigned char> &code=canonical?*canonical:saved.code;
    ID3D11Device *device=f.device.Get_Device();ID3D11DeviceContext *context=f.device.Get_Context();
    ID3D11VertexShader *vs=NULL;ID3D11GeometryShader *gs=NULL;ID3D11InputLayout *layout=NULL;
    ID3D11Buffer *vb=NULL,*output=NULL,*staging=NULL;ID3D11SamplerState *sampler=NULL;
    ID3DBlob *gsCode=NULL,*errors=NULL;
    HMODULE module=LoadLibraryA("d3dcompiler_47.dll");
    typedef HRESULT (WINAPI *Compile)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO *,ID3DInclude *,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob **,ID3DBlob **);
    Compile compile=module?reinterpret_cast<Compile>(GetProcAddress(module,"D3DCompile")):NULL;
    const char *source="struct V{float4 p:SV_Position;float4 d:COLOR0;float4 s:COLOR1;float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float2 xy:TEXCOORD2;float2 uv3:TEXCOORD3;float fog:FOG;};[maxvertexcount(1)]void main(point V input[1],inout PointStream<V> output){output.Append(input[0]);}";
    bool ok=compile&&SUCCEEDED(compile(source,std::strlen(source),"fft-test-streamout",NULL,NULL,"main","gs_5_0",0,0,&gsCode,&errors));
    if(errors){std::printf("  fft streamout compiler=%s\n",static_cast<const char *>(errors->GetBufferPointer()));Release(errors);}
    if(ok){
        typedef HRESULT (WINAPI *Reflect)(LPCVOID,SIZE_T,REFIID,void **);
        Reflect reflect=reinterpret_cast<Reflect>(GetProcAddress(module,"D3DReflect"));
        ID3D11ShaderReflection *vertexReflection=NULL,*geometryReflection=NULL;
        const bool reflected=reflect&&SUCCEEDED(reflect(code.data(),code.size(),__uuidof(ID3D11ShaderReflection),reinterpret_cast<void **>(&vertexReflection)))
            &&SUCCEEDED(reflect(gsCode->GetBufferPointer(),gsCode->GetBufferSize(),__uuidof(ID3D11ShaderReflection),reinterpret_cast<void **>(&geometryReflection)));
        CHECK(reflected);
        if(reflected){
            D3D11_SHADER_DESC vd={},gd={};vertexReflection->GetDesc(&vd);geometryReflection->GetDesc(&gd);
            bool linked=true;
            for(unsigned i=0;i<vd.OutputParameters;++i){D3D11_SIGNATURE_PARAMETER_DESC v={};vertexReflection->GetOutputParameterDesc(i,&v);
                bool matching=false;
                for(unsigned j=0;j<gd.InputParameters;++j){D3D11_SIGNATURE_PARAMETER_DESC g={};geometryReflection->GetInputParameterDesc(j,&g);
                    if(_stricmp(v.SemanticName,g.SemanticName)==0&&v.SemanticIndex==g.SemanticIndex)
                        matching=v.Register==g.Register&&v.Mask==g.Mask;}
                if(!matching)std::printf("  fft linkage mismatch semantic=%s index=%u VS-register=%u mask=%u\n",v.SemanticName,v.SemanticIndex,v.Register,v.Mask);
                CHECK(matching);linked=linked&&matching;
            }
            ok=ok&&linked;std::printf("  fft reflect actual-VS/full-GS register-mask-linkage=%u\n",linked);
            std::fflush(stdout);
        }
        Release(vertexReflection);Release(geometryReflection);
    }
    const D3D11_SO_DECLARATION_ENTRY declaration[]={{0,"SV_Position",0,0,4,0},{0,"COLOR",1,0,4,0},{0,"TEXCOORD",2,0,2,0}};
    const UINT stride=sizeof(CapturedVertex);
    if(ok)ok=SUCCEEDED(device->CreateVertexShader(code.data(),code.size(),NULL,&vs))
        &&SUCCEEDED(device->CreateGeometryShaderWithStreamOutput(gsCode->GetBufferPointer(),gsCode->GetBufferSize(),declaration,3,&stride,1,D3D11_SO_NO_RASTERIZED_STREAM,NULL,&gs));
    const D3D11_INPUT_ELEMENT_DESC elements[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,24,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,28,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,0,36,D3D11_INPUT_PER_VERTEX_DATA,0}};
    if(ok)ok=SUCCEEDED(device->CreateInputLayout(elements,5,code.data(),code.size(),&layout));
    D3D11_BUFFER_DESC desc={};desc.ByteWidth=sizeof(vertices);desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data={vertices,0,0};
    if(ok)ok=SUCCEEDED(device->CreateBuffer(&desc,&data,&vb));
    desc.ByteWidth=sizeof(captured);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_STREAM_OUTPUT;
    if(ok)ok=SUCCEEDED(device->CreateBuffer(&desc,NULL,&output));
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(ok)ok=SUCCEEDED(device->CreateBuffer(&desc,NULL,&staging));
    D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(ok)ok=SUCCEEDED(device->CreateSamplerState(&sd,&sampler));
    ID3D11Buffer *constants[2]={saved.constants[0],saved.constants[1]};
    ok=ok&&constants[0]&&constants[1];
    if(ok){
        context->ClearState();context->IASetInputLayout(layout);UINT vertexStride=sizeof(Vertex),offset=0;
        context->IASetVertexBuffers(0,1,&vb,&vertexStride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs,NULL,0);context->VSSetConstantBuffers(0,2,constants);context->GSSetShader(gs,NULL,0);
        ID3D11ShaderResourceView *resources[4]={saved.maps[0],saved.maps[1],saved.maps[2],saved.maps[3]};
        ID3D11SamplerState *samplers[4]={sampler,sampler,sampler,sampler};context->VSSetShaderResources(0,4,resources);context->VSSetSamplers(0,4,samplers);
        context->SOSetTargets(1,&output,&offset);context->Draw(4,0);
        ID3D11Buffer *empty=NULL;context->SOSetTargets(1,&empty,&offset);
        context->CopyResource(staging,output);D3D11_MAPPED_SUBRESOURCE mapped={};
        ok=SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped));
        if(ok){std::memcpy(captured,mapped.pData,sizeof(captured));context->Unmap(staging,0);}
    }
    context->ClearState();f.backend.Forget_Bindings();
    Release(sampler);Release(staging);Release(output);Release(vb);Release(layout);Release(gs);Release(vs);Release(gsCode);
    if(module)FreeLibrary(module);return ok;
}
class ShoreControl {
    Fixture &f;Texture &history;HMODULE module=NULL;
    ID3D11VertexShader *vs=NULL;ID3D11PixelShader *ps[2]={};ID3D11InputLayout *layout=NULL;
    ID3D11Buffer *vb=NULL,*ib=NULL,*vc[2]={},*hc[3]={};
    ID3D11Texture2D *snapshot[2]={},*array=NULL;ID3D11ShaderResourceView *sv[2]={},*arrayView=NULL;
    ID3D11SamplerState *linear=NULL,*point=NULL,*repeat=NULL;
    ID3D11BlendState *blend=NULL;ID3D11DepthStencilState *depth=NULL;ID3D11RasterizerState *raster=NULL;
    bool Buffer(const void *data,unsigned bytes,unsigned bind,ID3D11Buffer **out){
        D3D11_BUFFER_DESC d={};d.ByteWidth=bytes;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=bind;
        D3D11_SUBRESOURCE_DATA s={data,0,0};return SUCCEEDED(f.device.Get_Device()->CreateBuffer(&d,&s,out));}
public:
    ShoreControl(Fixture &fixture,Texture &atlas):f(fixture),history(atlas){}
    ~ShoreControl(){f.device.Get_Context()->ClearState();f.backend.Forget_Bindings();
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
            variant.shoreFoam=mesh!=0;variant.meshContact=WaterHullVisual::Enabled();PixelPipelineDescription pipeline={};pipeline.AlphaFunction=D3DCMP_ALWAYS;
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
        ID3D11ShaderResourceView *resources[16]={f.base.view,f.mirror.view,f.height.view,f.edge.view,f.optical.view,sv[0],sv[1],f.bed.view,arrayView,f.base.view,f.mirror.view,NULL,NULL,NULL,history.view,f.mirror.view};
        ID3D11SamplerState *samplers[16];for(auto &p:samplers)p=linear;samplers[6]=samplers[9]=point;samplers[7]=repeat;
        c->PSSetShaderResources(0,16,resources);c->PSSetSamplers(0,16,samplers);
        const float factors[]={1,1,1,1};c->OMSetBlendState(blend,factors,~0u);c->OMSetDepthStencilState(depth,0);c->RSSetState(raster);
        c->DrawIndexed(6,0,0);ID3D11ShaderResourceView *empty[16]={};c->PSSetShaderResources(0,16,empty);
        return f.Read(pixels);
    }
};

inline bool CompileVertex(bool lift,bool shoulder,std::vector<unsigned char> &code){
    VertexPipelineDescription vd={};vd.FVF=0x252;vd.StageCount=4;
    vd.Stages[0].TextureCoordinateIndex=D3DTSS_TCI_PASSTHRU;vd.Stages[1].TextureCoordinateIndex=D3DTSS_TCI_PASSTHRU|1;
    vd.Stages[2]=vd.Stages[1];vd.Stages[3].TextureCoordinateIndex=D3DTSS_TCI_CAMERASPACEPOSITION;vd.Stages[3].TextureTransformFlags=D3DTTFF_COUNT2;
    std::string base,source;WaterGpuShaders::Variant variant;variant.materialFvf=0x252;variant.vertex=WaterGpuShaders::Vertex::Fft;
    variant.bowLift=lift;variant.bowShoulder=shoulder;variant.shoreFoam=false;
    if(!VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,base)||!WaterGpuShaders::GenerateVertex(variant,base,source))return false;
    HMODULE module=LoadLibraryA("d3dcompiler_47.dll");typedef HRESULT(WINAPI *Compile)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
    Compile compile=module?reinterpret_cast<Compile>(GetProcAddress(module,"D3DCompile")):NULL;ID3DBlob *blob=NULL,*errors=NULL;
    bool ok=compile&&SUCCEEDED(compile(source.data(),source.size(),"vessel-canonical-vs",NULL,NULL,"main","vs_5_0",0,0,&blob,&errors));
    if(errors){std::printf("  vessel canonical compiler=%s\n",static_cast<char*>(errors->GetBufferPointer()));Release(errors);}
    if(ok){const auto *bytes=static_cast<unsigned char*>(blob->GetBufferPointer());code.assign(bytes,bytes+blob->GetBufferSize());}
    Release(blob);if(module)FreeLibrary(module);return ok;
}
struct ZeroFields {
    ID3D11Texture2D *map=NULL,*array=NULL;ID3D11ShaderResourceView *mapView=NULL,*arrayView=NULL;
    ~ZeroFields(){Release(mapView);Release(arrayView);Release(map);Release(array);}
    bool Set(Fixture &f){D3D11_TEXTURE2D_DESC d={};d.Width=d.Height=d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        Pixel zero={};D3D11_SUBRESOURCE_DATA data[6]={};for(auto &v:data){v.pSysMem=&zero;v.SysMemPitch=sizeof(zero);}
        auto *device=f.device.Get_Device();if(FAILED(device->CreateTexture2D(&d,data,&map))||FAILED(device->CreateShaderResourceView(map,NULL,&mapView)))return false;
        d.ArraySize=6;if(FAILED(device->CreateTexture2D(&d,data,&array))||FAILED(device->CreateShaderResourceView(array,NULL,&arrayView)))return false;
        return f.water->Set_Test_Fft_Fields(mapView,mapView,mapView,arrayView);
    }
};
inline bool Atlas(Texture &texture,ID3D11Device *device,float alpha){
    Release(texture.view);Release(texture.surface);D3D11_TEXTURE2D_DESC d={};d.Width=1;d.Height=2;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.Usage=D3D11_USAGE_IMMUTABLE;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    const Pixel pixels[2]={{0,0,0,alpha},{0,0,0,0}};D3D11_SUBRESOURCE_DATA data={pixels,sizeof(Pixel),0};
    return SUCCEEDED(device->CreateTexture2D(&d,&data,&texture.surface))&&SUCCEEDED(device->CreateShaderResourceView(texture.surface,NULL,&texture.view));
}
inline bool Scene(Fixture &f,Vertex (&vertices)[4],bool bow=false){
    if(!f.Scene(Pixel{.32f,.42f,.52f,1},.7f))return false;
    const Vertex v[4]={{-32,-32,0,0,0,1,0xffffffff,0,1,0,0},{-32,32,0,0,0,1,0xffffffff,0,0,0,0},
        {32,32,0,0,0,1,0xffffffff,1,0,0,0},{32,-32,0,0,0,1,0xffffffff,1,1,0,0}};
    std::memcpy(vertices,v,sizeof(v));f.batches[0].vertices=vertices;f.batches[0].vertexBytes=sizeof(v);f.batches[0].vertexStride=sizeof(Vertex);f.batches[0].vertexLayout=0x252;
    for(auto &p:f.coverage){p.x*=32;p.y*=32;}
    auto &in=f.request.inputs;in.scene.projection[0]=in.scene.projection[5]=1.f/32;in.surface.reflectionArea={-32,-32,32,32};
    in.surface.waterParameters={1.375f,0,1,0};in.surface.opticalParameters.z=1;
    f.request.fftEligibility=bow?WaterRenderBridge::FftEligibility::ProjectedFlat:WaterRenderBridge::FftEligibility::Ineligible;
    f.backend.Set_Transform(D3DTS_PROJECTION,in.scene.projection);
    for(unsigned slot:{0u,3u}){f.backend.Set_Sampler_State(slot,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        f.backend.Set_Sampler_State(slot,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);f.backend.Set_Sampler_State(slot,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
        f.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);f.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
    return true;
}
inline float Difference(const Pixel &a,const Pixel &b){return (std::max)({std::fabs(a.r-b.r),std::fabs(a.g-b.g),std::fabs(a.b-b.b)});}
inline float Region(const std::vector<Pixel> &a,const std::vector<Pixel> &b){
    CHECK_EQ(a.size(),64u*48u);CHECK_EQ(b.size(),a.size());if(a.size()!=64*48||b.size()!=a.size())return 0;
    float peak=0;for(unsigned y=16;y<32;++y)for(unsigned x=28;x<36;++x)peak=(std::max)(peak,Difference(a[y*64+x],b[y*64+x]));return peak;
}
inline void Finite(const std::vector<Pixel> &pixels){CHECK_EQ(pixels.size(),64u*48u);unsigned visible=0;
    for(const auto &p:pixels){CHECK(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a));visible+=std::fabs(p.r-.32f)>.001f;}CHECK(visible>100);}
}
