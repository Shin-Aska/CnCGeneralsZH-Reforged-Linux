#include "WaterDx11SurfaceFixture.h"

namespace {
using namespace WaterDx11SurfaceTest;

bool Pattern(Texture &texture, ID3D11Device *device, const std::vector<Pixel> &pixels,
    unsigned width, unsigned height)
{
    Release(texture.view); Release(texture.surface);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; desc.Usage=D3D11_USAGE_IMMUTABLE;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data={pixels.data(),width*sizeof(Pixel),0};
    return SUCCEEDED(device->CreateTexture2D(&desc,&data,&texture.surface))
        && SUCCEEDED(device->CreateShaderResourceView(texture.surface,NULL,&texture.view));
}

void Sampler(Fixture &fixture, unsigned slot, DWORD address, DWORD filter)
{
    fixture.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSU,address);
    fixture.backend.Set_Sampler_State(slot,D3DSAMP_ADDRESSV,address);
    fixture.backend.Set_Sampler_State(slot,D3DSAMP_MINFILTER,filter);
    fixture.backend.Set_Sampler_State(slot,D3DSAMP_MAGFILTER,filter);
    fixture.backend.Set_Sampler_State(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
}

bool Scene(Fixture &fixture, bool river=false, float u=.125f, float v=.25f)
{
    if(!fixture.Scene({.32f,.42f,.52f,1})) {CHECK(false);return false;}
    fixture.request.inputs.surface.river=river;
    for(Vertex &vertex:fixture.vertices) {vertex.u=u;vertex.v=v;}
    Sampler(fixture,0,D3DTADDRESS_WRAP,D3DTEXF_POINT);
    Sampler(fixture,3,D3DTADDRESS_CLAMP,D3DTEXF_LINEAR);
    return true;
}

Pixel Draw(Fixture &fixture,const char *label,int mutate=-1)
{
    WaterRenderBridge::PreparedPatch prepared={};
    const auto status=fixture.water->PreparePatch(fixture.request,prepared);
    CHECK(status==WaterRenderBridge::PrepareStatus::Ready);
    if(status!=WaterRenderBridge::PrepareStatus::Ready)return {};
    if(mutate>=0)Sampler(fixture,unsigned(mutate),D3DTADDRESS_CLAMP,D3DTEXF_LINEAR);
    const auto receipt=fixture.water->SubmitPreparedPatch(prepared);
    CHECK(WaterRenderBridge::Truthful(receipt,prepared));
    CHECK(receipt.status==WaterRenderBridge::SubmitStatus::Issued);
    CHECK_EQ(receipt.commandsIssued,1u);
    std::vector<Pixel> pixels;CHECK(fixture.Read(pixels));
    CHECK_EQ(pixels.size(),Fixture::Width*Fixture::Height);
    if(pixels.size()!=Fixture::Width*Fixture::Height)return {};
    const Pixel result=pixels[(Fixture::Height/2)*Fixture::Width+Fixture::Width/2];
    CHECK(std::isfinite(result.r)&&std::isfinite(result.g)&&std::isfinite(result.b));
    std::printf("  sampler-pixel %s rgb=%.9f,%.9f,%.9f alpha=%.9f commands=%u mutated=%d\n",
        label,result.r,result.g,result.b,result.a,receipt.commandsIssued,mutate);
    std::fflush(stdout);return result;
}
float Sum(const Pixel &pixel) {return pixel.r+pixel.g+pixel.b;}
float Difference(const Pixel &a,const Pixel &b)
{return std::fabs(a.r-b.r)+std::fabs(a.g-b.g)+std::fabs(a.b-b.b);}
void Equal(const Pixel &actual,const Pixel &expected) {CHECK_NEAR(Difference(actual,expected),0,.0001f);}
bool Bind(Fixture &fixture,Texture &texture,WaterRenderPacket::Resource &resource)
{
    CHECK(texture.surface!=NULL && texture.view!=NULL);
    if(!texture.surface || !texture.view)return false;
    D3D11_TEXTURE2D_DESC desc={};texture.surface->GetDesc(&desc);
    fixture.water->Set_Test_Resource(&texture,texture.view);
    resource={&texture,WaterRenderPacket::ResourceType::Texture2D,true,
        desc.Width,desc.Height,desc.ArraySize,desc.Width*desc.Height*desc.ArraySize};
    return true;
}
void ProjectMask(Fixture &fixture,float u)
{
    float matrix[16]={};matrix[10]=matrix[15]=1;matrix[12]=u;matrix[13]=.5f;
    fixture.backend.Set_Transform(D3DTS_TEXTURE3,matrix);
}

bool Sun(Fixture &fixture,int pattern,float map_x)
{
    fixture.request.inputs.scene.sunDirection={0,.28f,.96f,0};
    float view[16],projection[16];WaterRenderPacket::Identity(view);WaterRenderPacket::Identity(projection);
    projection[0]=projection[5]=.0001f;projection[10]=.0001f;
    projection[12]=2*map_x-1;projection[14]=.25f;
    fixture.backend.Set_Transform(D3DTS_VIEW,view);fixture.backend.Set_Transform(D3DTS_PROJECTION,projection);
    if(!fixture.backend.Begin_Shadow_Map(64)) {CHECK(false);return false;}
    ID3D11DepthStencilView *depth=NULL;
    fixture.device.Get_Context()->OMGetRenderTargets(0,NULL,&depth);
    ID3D11Resource *target=NULL;if(depth)depth->GetResource(&target);Release(depth);
    fixture.backend.End_Shadow_Map();
    fixture.device.Get_Context()->OMSetRenderTargets(1,&fixture.colourView,fixture.depthView);
    if(!target) {CHECK(false);return false;}
    std::vector<unsigned> values(64*64);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        values[y*64+x]=(pattern==2?(x<32?0u:0x00ffffffu):(pattern==0?0u:0x00ffffffu));
    D3D11_TEXTURE2D_DESC desc={};desc.Width=desc.Height=64;
    desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data={values.data(),64*sizeof(unsigned),0};
    ID3D11Texture2D *source=NULL;
    const HRESULT created=fixture.device.Get_Device()->CreateTexture2D(&desc,&data,&source);
    CHECK(SUCCEEDED(created));
    if(source)fixture.device.Get_Context()->CopyResource(target,source);
    Release(source);Release(target);
    fixture.backend.Set_Shadow_Parameters(0,1,0,0,0,1,0);
    fixture.backend.Set_Transform(D3DTS_VIEW,fixture.request.inputs.scene.view);
    fixture.backend.Set_Transform(D3DTS_PROJECTION,fixture.request.inputs.scene.projection);
    D3D11_VIEWPORT viewport={.5f,.5f,float(Fixture::Width),float(Fixture::Height),0,1};
    fixture.device.Get_Context()->RSSetViewports(1,&viewport);
    return SUCCEEDED(created);
}
}

TEST(dx11water_sampler_material_authored_repeat_filter_and_prepared_snapshot)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    Texture pattern,first,last,blend;
    CHECK(Pattern(pattern,f.device.Get_Device(),{{.4f,.4f,.5f,0},{.4f,.4f,.5f,1},
        {.4f,.4f,.5f,.4f},{.4f,.4f,.5f,.8f}},4,1));
    CHECK(first.Set(f.device.Get_Device(),{.4f,.4f,.5f,0}));
    CHECK(last.Set(f.device.Get_Device(),{.4f,.4f,.5f,.8f}));
    CHECK(blend.Set(f.device.Get_Device(),{.4f,.4f,.5f,.25f}));
    if(!pattern.view || !first.view || !last.view || !blend.view)return;
    auto draw=[&](const char *name,Texture &texture,float u,DWORD address,DWORD filter,bool mutate=false) {
        if(!Scene(f,true,u))return Pixel{};
        if(!Bind(f,texture,f.request.inputs.surface.authoredMaterial))return Pixel{};
        Sampler(f,0,address,filter);
        return Draw(f,name,mutate?0:-1);
    };
    const Pixel low=draw("ps0-solid-first",first,.125f,D3DTADDRESS_WRAP,D3DTEXF_POINT);
    const Pixel high=draw("ps0-solid-last",last,.125f,D3DTADDRESS_WRAP,D3DTEXF_POINT);
    const Pixel mixed=draw("ps0-solid-alpha-quarter",blend,.125f,D3DTADDRESS_WRAP,D3DTEXF_POINT);
    CHECK(Sum(high)>.03f);CHECK(Difference(low,high)>.01f);CHECK(Difference(low,mixed)>.01f);
    Equal(draw("ps0-wrap-base",pattern,.125f,D3DTADDRESS_WRAP,D3DTEXF_POINT),low);
    Equal(draw("ps0-wrap-period",pattern,1.125f,D3DTADDRESS_WRAP,D3DTEXF_POINT),low);
    Equal(draw("ps0-clamp-period",pattern,1.125f,D3DTADDRESS_CLAMP,D3DTEXF_POINT),high);
    Equal(draw("ps0-point",pattern,.1875f,D3DTADDRESS_WRAP,D3DTEXF_POINT),low);
    Equal(draw("ps0-linear",pattern,.1875f,D3DTADDRESS_WRAP,D3DTEXF_LINEAR),mixed);
    Equal(draw("ps0-prepared-point-wrap",pattern,1.1875f,D3DTADDRESS_WRAP,D3DTEXF_POINT,true),low);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(dx11water_sampler_river_edge_authored_v_repeat_and_prepared_snapshot)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    Texture pattern,opaque,transparent;
    CHECK(Pattern(pattern,f.device.Get_Device(),{{0,0,0,1},{0,0,0,0},{0,0,0,.5f},{0,0,0,0}},1,4));
    CHECK(opaque.Set(f.device.Get_Device(),{0,0,0,1}));CHECK(transparent.Set(f.device.Get_Device(),{0,0,0,0}));
    if(!pattern.view || !opaque.view || !transparent.view)return;
    auto draw=[&](const char *name,Texture &texture,DWORD address,bool mutate=false) {
        if(!Scene(f,true,.125f,1.125f))return Pixel{};f.backend.Set_Texture(3,texture.view);
        Sampler(f,3,address,D3DTEXF_POINT);return Draw(f,name,mutate?3:-1);
    };
    const Pixel visible=draw("ps3-river-solid-opaque",opaque,D3DTADDRESS_WRAP);
    const Pixel hidden=draw("ps3-river-solid-transparent",transparent,D3DTADDRESS_WRAP);
    CHECK(Sum(visible)>.03f);CHECK(Difference(visible,hidden)>.03f);
    Equal(draw("ps3-river-wrap-v",pattern,D3DTADDRESS_WRAP),visible);
    Equal(draw("ps3-river-clamp-v",pattern,D3DTADDRESS_CLAMP),hidden);
    Equal(draw("ps3-river-retained-wrap-v",pattern,D3DTADDRESS_WRAP,true),visible);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(dx11water_sampler_flat_projected_shroud_authored_point_and_linear)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    Texture pattern,white,black,quarter;
    CHECK(Pattern(pattern,f.device.Get_Device(),{{0,0,0,1},{1,1,1,1},{.3f,.3f,.3f,1},{.7f,.7f,.7f,1}},4,1));
    CHECK(white.Set(f.device.Get_Device(),{1,1,1,1}));CHECK(black.Set(f.device.Get_Device(),{0,0,0,1}));
    CHECK(quarter.Set(f.device.Get_Device(),{.25f,.25f,.25f,1}));
    if(!pattern.view || !white.view || !black.view || !quarter.view)return;
    auto draw=[&](const char *name,Texture &texture,DWORD filter,bool mutate=false) {
        if(!Scene(f))return Pixel{};ProjectMask(f,.1875f);f.backend.Set_Texture(3,texture.view);
        Sampler(f,3,D3DTADDRESS_CLAMP,filter);return Draw(f,name,mutate?3:-1);
    };
    const Pixel lit=draw("ps3-flat-solid-white",white,D3DTEXF_POINT);
    const Pixel dark=draw("ps3-flat-solid-black",black,D3DTEXF_POINT);
    const Pixel blend=draw("ps3-flat-solid-quarter",quarter,D3DTEXF_POINT);
    CHECK(Sum(lit)>.03f);CHECK(Difference(lit,dark)>.03f);CHECK(Difference(blend,dark)>.001f);
    Equal(draw("ps3-flat-projected-point",pattern,D3DTEXF_POINT),dark);
    Equal(draw("ps3-flat-projected-linear",pattern,D3DTEXF_LINEAR),blend);
    Equal(draw("ps3-flat-prepared-point",pattern,D3DTEXF_POINT,true),dark);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(dx11water_sampler_bed_repeat_matches_same_world_solid_texel)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    auto draw=[&](const char *name,bool patterned,float red) {
        if(!Scene(f))return Pixel{};
        for(Vertex &vertex:f.vertices)vertex.x+=60;
        f.request.inputs.scene.view[12]=-60;
        f.backend.Set_Transform(D3DTS_VIEW,f.request.inputs.scene.view);
        if(patterned) {
            std::vector<Pixel> pixels(8,Pixel{.9f,.45f,.5f,.5f});
            pixels[1]=pixels[2]={.2f,.45f,.5f,.5f};
            CHECK(Pattern(f.bed,f.device.Get_Device(),pixels,8,1));
        } else CHECK(f.bed.Set(f.device.Get_Device(),{red,.45f,.5f,.5f}));
        f.BindResources();
        if(!Bind(f,f.bed,f.request.inputs.surface.bed))return Pixel{};
        return Draw(f,name);
    };
    const Pixel low=draw("ps7-solid-repeat-plateau",false,.2f);
    const Pixel high=draw("ps7-solid-clamp-edge",false,.9f);
    CHECK(Sum(low)>.03f);CHECK(Difference(low,high)>.01f);
    Equal(draw("ps7-pattern-repeat",true,0),low);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(dx11water_sampler_sun_depth_point_manual_compare_at_step)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    auto draw=[&](const char *name,int pattern,float map_x) {
        if(!Scene(f) || !Sun(f,pattern,map_x))return Pixel{};return Draw(f,name);
    };
    const Pixel shadow=draw("ps9-solid-zero-shadow",0,.499f);
    const Pixel lit=draw("ps9-solid-one-lit",1,.499f);
    CHECK(Sum(lit)>.03f);CHECK(Difference(lit,shadow)>.01f);
    Equal(draw("ps9-step-left-point-shadow",2,.499f),shadow);
    Equal(draw("ps9-step-right-point-lit",2,.501f),lit);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(dx11water_sampler_bed_trilinear_fractional_lod_matches_solid_blend)
{
    Fixture f;const bool ready=f.Initialise();CHECK(ready);if(!ready)return;
    auto draw=[&](const char *name,bool mipped,float red) {
        const bool scene=f.Scene({.32f,.42f,.52f,1},1);CHECK(scene);if(!scene)return Pixel{};
        const float extent=6*std::sqrt(2.f);
        for(Vertex &vertex:f.vertices)vertex.x*=extent;
        f.request.inputs.scene.projection[0]=1/extent;
        f.backend.Set_Transform(D3DTS_PROJECTION,f.request.inputs.scene.projection);
        if(mipped) {
            Release(f.bed.view);Release(f.bed.surface);
            std::vector<Pixel> levels[9];D3D11_SUBRESOURCE_DATA data[9]={};
            for(unsigned mip=0;mip<9;++mip) {
                const unsigned width=256>>mip;
                levels[mip].assign(width,Pixel{mip==0?.2f:.8f,.45f,.5f,.5f});
                data[mip].pSysMem=levels[mip].data();data[mip].SysMemPitch=width*sizeof(Pixel);
            }
            D3D11_TEXTURE2D_DESC desc={};desc.Width=256;desc.Height=1;desc.MipLevels=9;
            desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
            desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            CHECK(SUCCEEDED(f.device.Get_Device()->CreateTexture2D(&desc,data,&f.bed.surface)));
            if(!f.bed.surface)return Pixel{};
            CHECK(SUCCEEDED(f.device.Get_Device()->CreateShaderResourceView(f.bed.surface,NULL,&f.bed.view)));
        } else CHECK(f.bed.Set(f.device.Get_Device(),{red,.45f,.5f,.5f}));
        f.BindResources();
        if(!Bind(f,f.bed,f.request.inputs.surface.bed))return Pixel{};
        return Draw(f,name);
    };
    const Pixel low=draw("ps7-mip-solid-level0",false,.2f);
    const Pixel high=draw("ps7-mip-solid-level1",false,.8f);
    const Pixel blend=draw("ps7-mip-solid-lod-half",false,.5f);
    CHECK(Sum(blend)>.03f);CHECK(Difference(low,blend)>.01f);CHECK(Difference(high,blend)>.01f);
    const Pixel result=draw("ps7-mip-trilinear-lod-half",true,0);
    CHECK_NEAR(Difference(result,blend),0,.001f);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

