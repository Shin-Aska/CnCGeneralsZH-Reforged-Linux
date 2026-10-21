/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "test_harness.h"
#include "WaterDx11SurfaceFixture.h"
#include "dx11backend.h"
#include "dx11water.h"
#include "engineshader.h"
#include "Platform/WaterGpuShaders.h"

#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>
#include <windows.h>

namespace {

typedef HRESULT (WINAPI *CompileFunction)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO *,
	ID3DInclude *,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob **,ID3DBlob **);

struct SurfaceVertex
{
	float x,y,z,nx,ny,nz;
	unsigned diffuse;
	float u1,v1,u2,v2;
};

struct GridVertex
{
	float x,y,z;
	unsigned diffuse;
	float u1,v1,u2,v2;
};

WaterRenderBridge::ViewState identity_view(unsigned width,unsigned height)
{
	WaterRenderBridge::ViewState result={};
	result.view[0]=result.view[5]=result.view[10]=result.view[15]=1;
	result.projection[0]=result.projection[5]=result.projection[10]=result.projection[15]=1;
	result.viewport.width=static_cast<float>(width);
	result.viewport.height=static_cast<float>(height);
	result.viewport.maxDepth=1;
	return result;
}

void set_identity_inputs(WaterRenderPacket::Inputs &inputs,unsigned width,unsigned height)
{
	WaterRenderPacket::Identity(inputs.scene.view);
	WaterRenderPacket::Identity(inputs.scene.projection);
	inputs.scene.viewport={0,0,static_cast<float>(width),static_cast<float>(height),0,1};
	inputs.scene.combiner[0]={1,1,1,1};
	inputs.scene.sunDirection={0,0,-1,0};
	inputs.scene.sunColour={1,1,1,0};
	for(unsigned i=0;i<4;++i)inputs.scene.shadowFromClip[i][i]=1;
	inputs.surface.reflectionArea={-1,-1,1,1};
	inputs.surface.waterParameters={0,0,0,0};
	inputs.surface.opticalParameters={8,0.2f,0,1};
	inputs.surface.hullParameters={0,0.2f,0.2f,0.2f};
}

CompileFunction load_compiler(HMODULE &module)
{
	module=LoadLibraryA("d3dcompiler_47.dll");
	return module?reinterpret_cast<CompileFunction>(GetProcAddress(module,"D3DCompile")):NULL;
}

bool make_texture(ID3D11Device *device,const unsigned char *pixels,unsigned width,
	ID3D11Texture2D **texture,ID3D11ShaderResourceView **view)
{
	D3D11_TEXTURE2D_DESC description={};
	description.Width=width;description.Height=1;description.MipLevels=1;description.ArraySize=1;
	description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;description.SampleDesc.Count=1;
	description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA data={pixels,width*4,0};
	return SUCCEEDED(device->CreateTexture2D(&description,&data,texture))
		&&SUCCEEDED(device->CreateShaderResourceView(*texture,NULL,view));
}

bool render_classic_multiplier(DX11DeviceClass &owner,ID3D11ShaderResourceView *mirror,
	unsigned width,std::vector<unsigned char> &pixels)
{
	ID3D11Device *device=owner.Get_Device();ID3D11DeviceContext *context=owner.Get_Context();
	HMODULE compiler_module=NULL;CompileFunction compile=load_compiler(compiler_module);
	if(!compile)return false;
	const char *vertex_source=
		"struct O{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
		"O main(uint id:SV_VertexID){O o;float2 p=id==0?float2(-1,-1):id==1?float2(-1,3):float2(3,-1);"
		"o.p=float4(p,0,1);o.uv=float2((p.x+1)*.5,(1-p.y)*.5);return o;}";
	const char *pixel_source=
		"Texture2D M:register(t0);SamplerState S:register(s0);"
		"float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
		"float3 c=M.Sample(S,uv).rgb;float coverage=saturate((1-dot(c,float3(.333333,.333333,.333333)))*4);"
		"return float4((1-.4*coverage).xxx,1);}";
	ID3DBlob *vertex_code=NULL,*pixel_code=NULL,*errors=NULL;
	bool ok=SUCCEEDED(compile(vertex_source,std::strlen(vertex_source),"mirror-red-vs",NULL,NULL,
		"main","vs_5_0",0,0,&vertex_code,&errors));
	if(errors){errors->Release();errors=NULL;}
	ok=ok&&SUCCEEDED(compile(pixel_source,std::strlen(pixel_source),"mirror-red-ps",NULL,NULL,
		"main","ps_5_0",0,0,&pixel_code,&errors));
	if(errors)errors->Release();
	ID3D11VertexShader *vs=NULL;ID3D11PixelShader *ps=NULL;ID3D11SamplerState *sampler=NULL;
	ID3D11Texture2D *target=NULL,*staging=NULL;ID3D11RenderTargetView *rtv=NULL;
	if(ok)ok=SUCCEEDED(device->CreateVertexShader(vertex_code->GetBufferPointer(),vertex_code->GetBufferSize(),NULL,&vs));
	if(ok)ok=SUCCEEDED(device->CreatePixelShader(pixel_code->GetBufferPointer(),pixel_code->GetBufferSize(),NULL,&ps));
	D3D11_SAMPLER_DESC sampler_description={};sampler_description.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	sampler_description.AddressU=sampler_description.AddressV=sampler_description.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_description.ComparisonFunc=D3D11_COMPARISON_NEVER;sampler_description.MaxLOD=D3D11_FLOAT32_MAX;
	if(ok)ok=SUCCEEDED(device->CreateSamplerState(&sampler_description,&sampler));
	D3D11_TEXTURE2D_DESC target_description={};target_description.Width=width;target_description.Height=1;
	target_description.MipLevels=1;target_description.ArraySize=1;target_description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
	target_description.SampleDesc.Count=1;target_description.Usage=D3D11_USAGE_DEFAULT;
	target_description.BindFlags=D3D11_BIND_RENDER_TARGET;
	if(ok)ok=SUCCEEDED(device->CreateTexture2D(&target_description,NULL,&target));
	if(ok)ok=SUCCEEDED(device->CreateRenderTargetView(target,NULL,&rtv));
	if(ok){
		D3D11_VIEWPORT viewport={0,0,static_cast<float>(width),1,0,1};
		context->OMSetRenderTargets(1,&rtv,NULL);context->RSSetViewports(1,&viewport);
		context->IASetInputLayout(NULL);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(vs,NULL,0);context->PSSetShader(ps,NULL,0);
		context->PSSetShaderResources(0,1,&mirror);context->PSSetSamplers(0,1,&sampler);
		context->Draw(3,0);
		ID3D11ShaderResourceView *empty=NULL;context->PSSetShaderResources(0,1,&empty);
		target_description.Usage=D3D11_USAGE_STAGING;target_description.BindFlags=0;
		target_description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
		ok=SUCCEEDED(device->CreateTexture2D(&target_description,NULL,&staging));
		if(ok){context->CopyResource(staging,target);D3D11_MAPPED_SUBRESOURCE mapped={};
			ok=SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped));
			if(ok){pixels.assign(static_cast<unsigned char *>(mapped.pData),
				static_cast<unsigned char *>(mapped.pData)+width*4);context->Unmap(staging,0);}}
	}
	if(staging)staging->Release();if(rtv)rtv->Release();if(target)target->Release();
	if(sampler)sampler->Release();if(ps)ps->Release();if(vs)vs->Release();
	if(pixel_code)pixel_code->Release();if(vertex_code)vertex_code->Release();
	FreeLibrary(compiler_module);return ok;
}

unsigned debug_warnings(ID3D11Device *device,const char *phase)
{
	ID3D11InfoQueue *queue=NULL;if(FAILED(device->QueryInterface(__uuidof(ID3D11InfoQueue),
		reinterpret_cast<void **>(&queue))))return ~0u;
	unsigned result=0;const UINT64 count=queue->GetNumStoredMessagesAllowedByRetrievalFilter();
	for(UINT64 i=0;i<count;++i){SIZE_T size=0;queue->GetMessage(i,NULL,&size);std::vector<unsigned char> storage(size);
		D3D11_MESSAGE *message=reinterpret_cast<D3D11_MESSAGE *>(&storage[0]);
		if(SUCCEEDED(queue->GetMessage(i,message,&size))&&message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING){
			++result;
			std::printf("  D3D11 phase=%s id=%u category=%u severity=%u: %s\n",phase,
				static_cast<unsigned>(message->ID),static_cast<unsigned>(message->Category),
				static_cast<unsigned>(message->Severity),message->pDescription);
			std::fflush(stdout);
		}}
	queue->Release();return result;
}

void compile_probe()
{
	HMODULE module=NULL;CompileFunction compile=load_compiler(module);if(!compile)ExitProcess(91);
	const unsigned layouts[2]={0x242u,0x252u};
	for(unsigned vertex=0;vertex<2;++vertex)for(unsigned river=0;river<2;++river)for(unsigned fog=0;fog<2;++fog){
		VertexPipelineDescription vd={};vd.FVF=layouts[vertex];vd.StageCount=2;
		vd.Stages[0].TextureCoordinateIndex=D3DTSS_TCI_PASSTHRU;vd.Stages[1].TextureCoordinateIndex=D3DTSS_TCI_PASSTHRU|1;
		vd.Stages[0].TextureTransformFlags=vd.Stages[1].TextureTransformFlags=D3DTTFF_DISABLE;
		vd.FogEnabled=fog!=0;vd.FogVertexMode=fog?D3DFOG_LINEAR:D3DFOG_NONE;
		std::string base,vs,ps;WaterGpuShaders::Variant v;v.surface=river?WaterGpuShaders::Surface::River:WaterGpuShaders::Surface::Flat;
		v.shoreFoam=false;v.materialFvf=layouts[vertex];PixelPipelineDescription pd={};pd.AlphaFunction=D3DCMP_ALWAYS;pd.FogEnabled=fog!=0;
		const ULONGLONG started=GetTickCount64();bool ok=VertexShader_Generate(vd,VERTEX_SHADER_TARGET_D3D11,base)
			&&WaterGpuShaders::GenerateVertex(v,base,vs)&&WaterGpuShaders::GenerateSurfacePixel(v,pd,ps);
		ID3DBlob *a=NULL,*b=NULL,*errors=NULL;if(ok)ok=SUCCEEDED(compile(vs.data(),vs.size(),"probe-vs",NULL,NULL,"main","vs_5_0",0,0,&a,&errors));
		if(errors){errors->Release();errors=NULL;}if(ok)ok=SUCCEEDED(compile(ps.data(),ps.size(),"probe-ps",NULL,NULL,"main","ps_5_0",0,0,&b,&errors));
		if(errors)errors->Release();if(a)a->Release();if(b)b->Release();
		std::fprintf(stderr,"compile-probe layout=%x river=%u fog=%u ok=%u ms=%llu vs=%zu ps=%zu\n",layouts[vertex],river,fog,ok?1u:0u,
			static_cast<unsigned long long>(GetTickCount64()-started),vs.size(),ps.size());std::fflush(stderr);if(!ok)ExitProcess(92);
	}
	FreeLibrary(module);ExitProcess(0);
}

void packet_probe()
{
	WaterRenderPacket::Inputs inputs={};set_identity_inputs(inputs,64,64);
	static int a,b;inputs.surface.authoredMaterialAvailable=true;
	inputs.surface.authoredMaterial={&a,WaterRenderPacket::ResourceType::Texture2D,true,1,1,1,1};
	inputs.surface.reflectionAvailable=true;
	inputs.surface.reflection={&b,WaterRenderPacket::ResourceType::Texture2D,true,1,1,1,1};
	WaterRenderPacket::DrawPacket packet={};WaterRenderPacket::ValidationError error=WaterRenderPacket::ValidationError::None;
	const bool initial=WaterRenderPacket::Pack(inputs,packet,&error);
	std::fprintf(stderr,"packet-probe initial=%u error=%u\n",initial?1u:0u,static_cast<unsigned>(error));
	inputs.scene.snapshotAvailable=true;inputs.scene.sceneColour={&a,WaterRenderPacket::ResourceType::Texture2D,true,64,64,1,4096};
	inputs.scene.sceneDepth={&b,WaterRenderPacket::ResourceType::Texture2D,true,64,64,1,4096};error=WaterRenderPacket::ValidationError::None;
	const bool staged=WaterRenderPacket::Pack(inputs,packet,&error);
	std::fprintf(stderr,"packet-probe staged=%u error=%u\n",staged?1u:0u,static_cast<unsigned>(error));std::fflush(stderr);
	ExitProcess(initial&&staged?0:93);
}

}

TEST(dx11water_surface_black_mirror_is_not_a_classic_fallback)
{
	if(getenv("ZH_WATER_COMPILE_PROBE_ONLY"))compile_probe();
	if(getenv("ZH_WATER_PACKET_PROBE_ONLY"))packet_probe();
	std::fprintf(stderr,"surface-test mirror begin\n");std::fflush(stderr);
	DX11DeviceClass device;device.Request_Debug_Layer();CHECK(device.Create_Offscreen());
	std::fprintf(stderr,"surface-test mirror device-ready\n");std::fflush(stderr);
	CHECK(device.Has_Debug_Layer());
	const char *expected=getenv("ZH_TEST_DX11_EXPECT_DRIVER");
	if(expected&&_stricmp(expected,"warp")==0)CHECK(device.Is_Software());
	if(expected&&_stricmp(expected,"hardware")==0)CHECK(!device.Is_Software());
	static const unsigned WIDTH=8;
	const unsigned char black[WIDTH*4]={
		0,0,0,0, 0,0,0,0, 128,16,8,64, 255,32,16,192,
		16,192,48,192, 8,96,24,64, 0,0,0,0, 0,0,0,0};
	const unsigned char white[WIDTH*4]={
		255,255,255,0, 255,255,255,0, 192,143,135,64, 255,64,48,192,
		64,208,96,192, 193,215,199,64, 255,255,255,0, 255,255,255,0};
	ID3D11Texture2D *black_texture=NULL,*white_texture=NULL;
	ID3D11ShaderResourceView *black_view=NULL,*white_view=NULL;
	CHECK(make_texture(device.Get_Device(),black,WIDTH,&black_texture,&black_view));
	CHECK(make_texture(device.Get_Device(),white,WIDTH,&white_texture,&white_view));
	std::vector<unsigned char> black_result,white_result;
	CHECK(render_classic_multiplier(device,black_view,WIDTH,black_result));
	CHECK(render_classic_multiplier(device,white_view,WIDTH,white_result));
	CHECK_EQ(black_result.size(),white_result.size());
	CHECK(memcmp(&black_result[0],&white_result[0],black_result.size())!=0);
	std::printf("  mirror-red adapter=%s black=",device.Is_Software()?"warp":"hardware");
	for(size_t i=0;i<black_result.size();i+=4)std::printf("%02x%02x%02x%02x",black_result[i],black_result[i+1],black_result[i+2],black_result[i+3]);
	std::printf(" white=");for(size_t i=0;i<white_result.size();i+=4)std::printf("%02x%02x%02x%02x",white_result[i],white_result[i+1],white_result[i+2],white_result[i+3]);
	std::printf("\n");
	if(white_view)white_view->Release();if(black_view)black_view->Release();
	if(white_texture)white_texture->Release();if(black_texture)black_texture->Release();
	CHECK_EQ(debug_warnings(device.Get_Device(),"mirror-final"),0u);
}

TEST(dx11water_surface_prepare_retains_grid_metadata_and_reaches_ready)
{
	std::fprintf(stderr,"surface-test grid begin\n");std::fflush(stderr);
	WaterDx11SurfaceTest::Fixture fixture;CHECK(fixture.Initialise());
	debug_warnings(fixture.device.Get_Device(),"after-initialise");
	CHECK(fixture.Scene());
	debug_warnings(fixture.device.Get_Device(),"after-scene");
	std::fprintf(stderr,"surface-test grid backend-ready\n");std::fflush(stderr);
	WaterRenderBridge::PreparedPatch prepared={};
	debug_warnings(fixture.device.Get_Device(),"before-prepare");
	const WaterRenderBridge::PrepareStatus status=fixture.water->PreparePatch(fixture.request,prepared);
	debug_warnings(fixture.device.Get_Device(),"after-prepare");
	std::printf("  grid-green status=%u stride=%u fvf=%x xyz=%g,%g,%g u2=%g v2=%g capability=%u reason=%u\n",
		static_cast<unsigned>(status),fixture.batches[0].vertexStride,fixture.batches[0].vertexLayout,
		fixture.vertices[0].x,fixture.vertices[0].y,fixture.vertices[0].z,fixture.vertices[0].damping,fixture.vertices[0].level,
		fixture.water->GetCapability().Available()?1u:0u,static_cast<unsigned>(fixture.water->GetCapability().reason));
	CHECK(status==WaterRenderBridge::PrepareStatus::Ready);
	CHECK(prepared.complete);
	const WaterRenderBridge::SubmissionReceipt receipt=fixture.water->SubmitPreparedPatch(prepared);
	debug_warnings(fixture.device.Get_Device(),"after-submit");
	CHECK(receipt.status==WaterRenderBridge::SubmitStatus::Issued);
	CHECK_EQ(receipt.commandsIssued,1u);CHECK_EQ(receipt.batchesIssued,1u);CHECK(receipt.coveragePublished);
	CHECK(WaterRenderBridge::Truthful(receipt,prepared));
	CHECK_EQ(debug_warnings(fixture.device.Get_Device(),"grid-final"),0u);
}

TEST(dx11water_surface_flat_shroud_uses_projected_mask_and_retains_prepared_state)
{
	using WaterDx11SurfaceTest::Fixture;
	using WaterDx11SurfaceTest::Pixel;
	Fixture fixture;
	const bool initialised=fixture.Initialise();CHECK(initialised);if(!initialised)return;
	WaterDx11SurfaceTest::Texture dark_height,mask;
	CHECK(dark_height.Set(fixture.device.Get_Device(),{0,0,0,0}));
	const unsigned char mask_pixels[16]={0,0,0,255,0,0,0,255,255,255,255,255,255,255,255,255};
	CHECK(make_texture(fixture.device.Get_Device(),mask_pixels,4,&mask.surface,&mask.view));
	if(dark_height.view==NULL || mask.view==NULL)return;
	auto draw=[&](const char *label,bool black_height,ID3D11ShaderResourceView *shroud,
		bool flip,bool mutate_after_prepare)->std::vector<Pixel> {
		std::vector<Pixel> pixels;
		const bool scene=fixture.Scene({0,0,0,1});CHECK(scene);if(!scene)return pixels;
		CHECK_EQ(fixture.batches[0].vertexLayout,0x242u);
		CHECK_EQ(fixture.batches[0].vertexStride,32u);
		if(black_height) {
			fixture.water->Set_Test_Resource(&dark_height,dark_height.view);
			fixture.request.inputs.surface.terrainHeight={&dark_height,
				WaterRenderPacket::ResourceType::Texture2D,true,1,1,1,1};
		}
		float projection[16]={};
		projection[0]=flip?-0.5f:0.5f;projection[5]=0.5f;projection[10]=projection[15]=1;
		projection[12]=projection[13]=0.5f;
		fixture.backend.Set_Texture(3,shroud!=NULL?shroud:fixture.edge.view);
		fixture.backend.Set_Texture_Stage_State(3,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_CAMERASPACEPOSITION);
		fixture.backend.Set_Texture_Stage_State(3,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_COUNT2);
		fixture.backend.Set_Transform(D3DTS_TEXTURE3,projection);
		WaterRenderBridge::PreparedPatch prepared={};
		const WaterRenderBridge::PrepareStatus status=fixture.water->PreparePatch(fixture.request,prepared);
		CHECK(status==WaterRenderBridge::PrepareStatus::Ready);
		if(status!=WaterRenderBridge::PrepareStatus::Ready)return pixels;
		if(mutate_after_prepare) {
			fixture.backend.Set_Texture(3,fixture.edge.view);
			projection[0]=-projection[0];
			fixture.backend.Set_Transform(D3DTS_TEXTURE3,projection);
			fixture.backend.Set_Texture_Stage_State(3,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_PASSTHRU);
			fixture.backend.Set_Texture_Stage_State(3,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
			WaterDx11SurfaceTest::Release(mask.view);
			WaterDx11SurfaceTest::Release(mask.surface);
		}
		const WaterRenderBridge::SubmissionReceipt receipt=fixture.water->SubmitPreparedPatch(prepared);
		CHECK(WaterRenderBridge::Truthful(receipt,prepared));
		CHECK(receipt.status==WaterRenderBridge::SubmitStatus::Issued);
		CHECK_EQ(receipt.commandsIssued,1u);CHECK_EQ(receipt.batchesIssued,1u);CHECK(receipt.coveragePublished);
		CHECK(fixture.Read(pixels));
		std::printf("  shroud-grid label=%s frame=%u black-height=%u flip=%u mutated=%u pixels=%zu\n",
			label,fixture.frame,black_height?1u:0u,flip?1u:0u,mutate_after_prepare?1u:0u,pixels.size());
		return pixels;
	};
	const std::vector<Pixel> control=draw("white-control",false,NULL,false,false);
	const unsigned count=Fixture::Width*Fixture::Height;
	CHECK_EQ(control.size(),count);if(control.size()!=count)return;
	const unsigned sample[2]={(Fixture::Height/2)*Fixture::Width+Fixture::Width/4,
		(Fixture::Height/2)*Fixture::Width+3*Fixture::Width/4};
	float baseline[2]={};
	for(unsigned i=0;i<2;++i) {
		const Pixel &pixel=control[sample[i]];baseline[i]=pixel.r+pixel.g+pixel.b;
		std::printf("  shroud-grid control sample=%u rgb=%.7f,%.7f,%.7f sum=%.7f\n",
			i,pixel.r,pixel.g,pixel.b,baseline[i]);
		CHECK(std::isfinite(baseline[i]));CHECK(baseline[i]>0.03f);
	}
	if(!(baseline[0]>0.03f && baseline[1]>0.03f))return;
	const std::vector<Pixel> white=draw("black-height-white-shroud",true,NULL,false,false);
	const std::vector<Pixel> projected=draw("projected-mask",true,mask.view,false,false);
	const std::vector<Pixel> flipped=draw("flipped-mask",true,mask.view,true,false);
	const std::vector<Pixel> retained=draw("retained-mask",true,mask.view,false,true);
	const std::vector<Pixel> *images[4]={&white,&projected,&flipped,&retained};
	// At these grid pixels view X is -0.5/+0.5: the projector gives U=0.25/0.75.
	// The four-texel mask is constant black/white there; flipping the projector swaps them.
	const bool clear[4][2]={{true,true},{false,true},{true,false},{false,true}};
	for(unsigned image=0;image<4;++image) {
		CHECK_EQ(images[image]->size(),count);if(images[image]->size()!=count)continue;
		for(unsigned i=0;i<2;++i) {
			const Pixel &pixel=(*images[image])[sample[i]];
			const float ratio=(pixel.r+pixel.g+pixel.b)/baseline[i];
			std::printf("  shroud-grid image=%u sample=%u baseline=%.7f ratio=%.7f expected-clear=%u\n",
				image,i,baseline[i],ratio,clear[image][i]?1u:0u);
			CHECK(std::isfinite(ratio));
			if(clear[image][i])CHECK(ratio>0.98f && ratio<1.02f);
			else CHECK(ratio>=-0.0001f && ratio<0.02f);
		}
	}
	const bool missing_scene=fixture.Scene({0,0,0,1});CHECK(missing_scene);
	if(missing_scene) {
		fixture.backend.Set_Texture(3,NULL);
		WaterRenderBridge::PreparedPatch prepared={};
		CHECK(fixture.water->PreparePatch(fixture.request,prepared)==WaterRenderBridge::PrepareStatus::Unavailable);
		CHECK(!prepared.complete);
	}
	CHECK_EQ(debug_warnings(fixture.device.Get_Device(),"flat-shroud-final"),0u);
	CHECK(fixture.Scene({0,0,0,1}));
	fixture.backend.Set_Texture_Stage_State(3,D3DTSS_TEXCOORDINDEX,D3DTSS_TCI_PASSTHRU);
	WaterRenderBridge::PreparedPatch invalid={};
	CHECK(fixture.water->PreparePatch(fixture.request,invalid)==WaterRenderBridge::PrepareStatus::InvalidGeometry);
	CHECK(!invalid.complete);
}

TEST(dx11water_surface_river_uses_authored_edge_alpha_independent_of_terrain)
{
	using namespace WaterDx11SurfaceTest;
	Fixture fixture; const bool initialised=fixture.Initialise(); CHECK(initialised); if(!initialised)return;
	Texture terrain,opaque,transparent,pattern;
	CHECK(terrain.Set(fixture.device.Get_Device(),{0,0,0,0}));
	CHECK(opaque.Set(fixture.device.Get_Device(),{0,0,0,1}));
	CHECK(transparent.Set(fixture.device.Get_Device(),{1,1,1,0}));
	const unsigned char edge_pixels[16]={0,0,0,255,0,0,0,255,255,255,255,0,255,255,255,0};
	CHECK(make_texture(fixture.device.Get_Device(),edge_pixels,4,&pattern.surface,&pattern.view));
	if(!terrain.view || !opaque.view || !transparent.view || !pattern.view)return;
	auto draw=[&](const char *label,ID3D11ShaderResourceView *edge,bool mutate)->Pixel {
		CHECK(fixture.Scene({0,0,0,1}));
		fixture.request.inputs.surface.river=true;
		fixture.water->Set_Test_Resource(&terrain,terrain.view);
		fixture.request.inputs.surface.terrainHeight={&terrain,WaterRenderPacket::ResourceType::Texture2D,true,1,1,1,1};
		fixture.backend.Set_Texture(3,edge);
		float unrelated_projection[16]={};unrelated_projection[15]=1;
		fixture.backend.Set_Transform(D3DTS_TEXTURE3,unrelated_projection);
		WaterRenderBridge::PreparedPatch prepared={};
		const auto status=fixture.water->PreparePatch(fixture.request,prepared);
		CHECK(status==WaterRenderBridge::PrepareStatus::Ready);
		if(status!=WaterRenderBridge::PrepareStatus::Ready)return {};
		if(mutate)fixture.backend.Set_Texture(3,transparent.view);
		const auto receipt=fixture.water->SubmitPreparedPatch(prepared);
		CHECK(WaterRenderBridge::Truthful(receipt,prepared));
		CHECK(receipt.status==WaterRenderBridge::SubmitStatus::Issued);
		CHECK_EQ(receipt.commandsIssued,1u); CHECK(receipt.coveragePublished);
		std::vector<Pixel> pixels; CHECK(fixture.Read(pixels));
		if(pixels.size()!=Fixture::Width*Fixture::Height)return {};
		const Pixel result=pixels[(Fixture::Height/2)*Fixture::Width+Fixture::Width/2];
		if(edge==pattern.view) {
			const Pixel &left=pixels[(Fixture::Height/2)*Fixture::Width+Fixture::Width/8];
			const Pixel &right=pixels[(Fixture::Height/2)*Fixture::Width+3*Fixture::Width/8];
			std::printf("  river-edge authored-uv samples=%.7f,%.7f\n",left.r+left.g+left.b,right.r+right.g+right.b);
			CHECK(left.r+left.g+left.b>0.03f);CHECK(right.r+right.g+right.b<0.0001f);
		}
		std::printf("  river-edge label=%s rgb=%.7f,%.7f,%.7f alpha=%.7f mutated=%u\n",label,result.r,result.g,result.b,result.a,mutate?1u:0u);
		return result;
	};
	const Pixel visible=draw("opaque-black-edge-black-terrain",opaque.view,false);
	const Pixel hidden=draw("transparent-white-edge-black-terrain",transparent.view,false);
	const Pixel retained=draw("retained-opaque-edge",opaque.view,true);
	const float sums[]={visible.r+visible.g+visible.b,hidden.r+hidden.g+hidden.b,retained.r+retained.g+retained.b};
	for(float sum:sums)CHECK(std::isfinite(sum));
	CHECK(sums[0]>0.03f); CHECK(sums[1]>=0 && sums[1]<0.0001f); CHECK(sums[2]>0.03f);
	CHECK_NEAR(sums[0],sums[2],0.0001f);
	draw("authored-uv-alpha-pattern",pattern.view,false);
	CHECK_EQ(DebugWarnings(fixture.device.Get_Device()),0u);
}

TEST(dx11water_surface_flat_rejects_nonfinite_shroud_projection)
{
	WaterDx11SurfaceTest::Fixture fixture;
	const bool initialised=fixture.Initialise();CHECK(initialised);if(!initialised)return;
	CHECK(fixture.Scene());
	float projection[16];WaterRenderPacket::Identity(projection);
	projection[0]=std::numeric_limits<float>::quiet_NaN();
	fixture.backend.Set_Transform(D3DTS_TEXTURE3,projection);
	WaterRenderBridge::PreparedPatch prepared={};
	const auto status=fixture.water->PreparePatch(fixture.request,prepared);
	std::printf("  shroud-nonfinite status=%u complete=%u\n",unsigned(status),prepared.complete?1u:0u);
	CHECK(status==WaterRenderBridge::PrepareStatus::InvalidPacket);
	CHECK(!prepared.complete);
	CHECK_EQ(WaterDx11SurfaceTest::DebugWarnings(fixture.device.Get_Device()),0u);
}

TEST(dx11water_surface_prepare_converts_logical_viewport_once_and_rejects_other_view_changes)
{
	WaterDx11SurfaceTest::Fixture fixture; CHECK(fixture.Initialise()); CHECK(fixture.Scene());
	WaterRenderBridge::PreparedPatch prepared = {};
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::Ready);
	CHECK_NEAR(prepared.viewState.viewport.x, 0.5f, 0.00001f);
	CHECK_NEAR(prepared.viewState.viewport.y, 0.5f, 0.00001f);
	CHECK_NEAR(prepared.packet.constants.Common.Viewport.x, 0.5f, 0.00001f);
	CHECK_NEAR(prepared.packet.constants.Common.Viewport.y, 0.5f, 0.00001f);

	const WaterRenderPacket::SceneInput original = fixture.request.inputs.scene;
	const std::uint32_t original_width = fixture.request.targetWidth;
	fixture.request.inputs.scene.viewport.x = 0.25f;
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::InvalidGeometry);
	fixture.request.inputs.scene = original;
	fixture.request.inputs.scene.projection[0] += 0.25f;
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::InvalidGeometry);
	fixture.request.inputs.scene = original;
	fixture.request.inputs.scene.view[12] += 0.25f;
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::InvalidGeometry);
	fixture.request.inputs.scene = original;
	fixture.request.targetWidth = original_width - 1;
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::InvalidGeometry);
	fixture.request.targetWidth = original_width;
	fixture.request.inputs.scene.viewport.maxDepth = 0.5f;
	CHECK(fixture.water->PreparePatch(fixture.request, prepared) == WaterRenderBridge::PrepareStatus::InvalidGeometry);
	CHECK_EQ(debug_warnings(fixture.device.Get_Device(),"viewport-boundary-final"),0u);
}
