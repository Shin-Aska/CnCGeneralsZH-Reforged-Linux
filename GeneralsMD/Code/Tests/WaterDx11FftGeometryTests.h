#pragma once
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include "Platform/WaterFftGeometry.h"

namespace WaterDx11FftTest {
struct CapturedVertex { float clip[4],metadata[4],originalXY[2]; };

struct SavedPreparedVertex {
    std::vector<unsigned char> code;
    ID3D11Buffer *constants[2];
    ID3D11ShaderResourceView *maps[3];
    SavedPreparedVertex(){std::memset(constants,0,sizeof(constants));std::memset(maps,0,sizeof(maps));}
    ~SavedPreparedVertex(){for(auto &v:constants)Release(v);for(auto &v:maps)Release(v);}
    bool Capture(Fixture &f,const WaterRenderBridge::PreparedPatch &patch){
        if(!f.water->Get_Test_Prepared_Vertex_Bytecode(patch.patchId,code)||code.empty())return false;
        constants[0]=f.water->Get_Test_Prepared_Vertex_Constant_Buffer(patch.patchId);
        constants[1]=f.water->Get_Test_Prepared_Pixel_Constant_Buffer(patch.patchId);
        for(auto *v:constants)if(v)v->AddRef();
        for(unsigned slot=0;slot<3;++slot){maps[slot]=f.water->Get_Test_Prepared_Vertex_Resource(patch.patchId,slot);if(maps[slot])maps[slot]->AddRef();}
        return constants[0]&&constants[1]&&maps[0]&&maps[1]&&maps[2];
    }
};
inline bool CaptureVertices(Fixture &f,const WaterRenderBridge::PreparedPatch &patch,
    const FftVertex (&vertices)[4],ID3D11ShaderResourceView *const (&maps)[3],CapturedVertex (&captured)[4],const SavedPreparedVertex *saved=NULL)
{
    std::vector<unsigned char> code;
    if(saved)code=saved->code;
    else if(!f.water->Get_Test_Prepared_Vertex_Bytecode(patch.patchId,code)||code.empty())return false;
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
    ID3D11Buffer *constants[2]={saved?saved->constants[0]:f.water->Get_Test_Prepared_Vertex_Constant_Buffer(patch.patchId),saved?saved->constants[1]:f.water->Get_Test_Prepared_Pixel_Constant_Buffer(patch.patchId)};
    ok=ok&&constants[0]&&constants[1];
    if(ok){
        context->ClearState();context->IASetInputLayout(layout);UINT vertexStride=sizeof(FftVertex),offset=0;
        context->IASetVertexBuffers(0,1,&vb,&vertexStride,&offset);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        context->VSSetShader(vs,NULL,0);context->VSSetConstantBuffers(0,2,constants);context->GSSetShader(gs,NULL,0);
        ID3D11ShaderResourceView *resources[4]={maps[0],maps[1],maps[2],f.height.view};
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
}

TEST(fft_production_vertex_streamout_metadata_and_bounds)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());FftVertex vertices[4];geometry(f,vertices,.1f);
    for(auto &v:vertices)v.level=.05f;
    vertices[0].nx=0;vertices[2].nx=.5f;vertices[2].ny=.25f;vertices[2].nz=.5f;
    vertices[3].ny=0;vertices[3].nz=0;
    Fields fields;CHECK(fields.Initialise(f));if(!fields.arrayView)return;
    auto spectrum=WaterFftReference::MakePackedSpectra(4);
    spectrum[0][10].real=.01;spectrum[1][10].real=.02;spectrum[2][10].real=.015;
    const auto independent=WaterFftReference::TransformPackedFields(spectrum,4);
    const Pixel texel={float(independent[0].pixels[0]),float(independent[0].pixels[1]),float(independent[0].pixels[2]),0};
    CHECK(fields.displacement[0].Set(f.device.Get_Device(),texel));
    CHECK(f.water->Set_Test_Fft_Fields(fields.displacement[0].view,fields.displacement[1].view,fields.displacement[2].view,fields.arrayView));
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    WaterRenderBridge::PreparedPatch patch={};CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    WaterDx11FftTest::CapturedVertex actual[4]={};
    ID3D11ShaderResourceView *maps[3]={fields.displacement[0].view,fields.displacement[1].view,fields.displacement[2].view};
    CHECK(WaterDx11FftTest::CaptureVertices(f,patch,vertices,maps,actual));
    const double horizontal=double(WaterFftGeometry::MaxHorizontalDisplacement)/std::sqrt(2.0);
    const double vertical=WaterFftGeometry::MaxVerticalDisplacement;
    const double dx=horizontal*std::tanh(-1.2*double(texel.r)*20/horizontal);
    const double dy=horizontal*std::tanh(-1.2*double(texel.b)*20/horizontal);
    const double dz=vertical*std::tanh(double(texel.g)*20/vertical);
    for(unsigned i=0;i<4;++i){const FftVertex &v=vertices[i];
        const double z=v.nx>0?v.level+(v.z-v.level)*(1-v.nx)+dz*v.ny*v.nx:v.z;
        const double scale=v.nx*v.ny*v.nz;
        const double expected[4]={v.x+dx*scale,v.y+dy*scale+z,(z-2)*-.25,1};
        for(unsigned j=0;j<4;++j)CHECK(WaterFftReference::WithinFp32(actual[i].clip[j],expected[j]));
        CHECK_EQ(actual[i].originalXY[0],v.x);CHECK_EQ(actual[i].originalXY[1],v.y);
        CHECK_EQ(actual[i].metadata[0],v.nx);CHECK_EQ(actual[i].metadata[1],v.ny);
        CHECK_NEAR(actual[i].metadata[2],v.z-v.level,1e-7);CHECK_EQ(actual[i].metadata[3],v.nz);
        std::printf("  fft actual-production-VS vertex=%u clip=%.9g,%.9g,%.9g,%.9g expected=%.17g,%.17g,%.17g,%.17g originalXY=%.9g,%.9g metadata=%.9g,%.9g,%.9g,%.9g override=geometry-only\n",
            i,actual[i].clip[0],actual[i].clip[1],actual[i].clip[2],actual[i].clip[3],expected[0],expected[1],expected[2],expected[3],
            actual[i].originalXY[0],actual[i].originalXY[1],actual[i].metadata[0],actual[i].metadata[1],actual[i].metadata[2],actual[i].metadata[3]);
    }
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));f.water->Clear_Test_Resources();
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}








