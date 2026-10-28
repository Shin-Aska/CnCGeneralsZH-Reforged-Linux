// The optional water tracer on the actual SDL GPU backend. No game or assets required.
#include "SdlGpuFrame.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterPathTracingMotion.h"
#include "Platform/WaterPathTracingMaterials.h"
#include "Platform/WaterOpticalModel.h"
#include "Platform/WaterFoamShading.h"
#include "../Libraries/Source/WWVegas/WW3D2/engineshader.h"
#include "../Libraries/Source/WWVegas/WW3D2/ffshader.h"
#include "../Libraries/Source/WWVegas/WW3D2/sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
unsigned failures=0,checks=0;
#define CHECK(value) do { ++checks; if(!(value)) { ++failures; \
    std::fprintf(stderr,"FAIL water_path_gpu:%d: %s\n",__LINE__,#value); } } while(0)

float Half(uint16_t bits)
{
    const float sign=(bits&0x8000u)?-1.f:1.f;
    const unsigned exponent=(bits>>10)&31u,mantissa=bits&1023u;
    if(exponent==0)return sign*std::ldexp(float(mantissa),-24);
    if(exponent==31)return mantissa?NAN:sign*INFINITY;
    return sign*std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15);
}

std::vector<float> Read(SdlGpuFrame *frame,unsigned size,SDL_GPUTexture *texture=nullptr,unsigned mip=0)
{
    std::vector<float> result;
    CHECK(frame->Flush());
    if(!texture)texture=frame->Water_Path_Texture();
    if(!texture)return result;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size=size*size*4*sizeof(uint16_t);
    SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(frame->Device(),&info);
    CHECK(transfer);
    if(!transfer)return result;
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
    CHECK(commands);
    if(commands) {
        SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
        CHECK(copy);
        if(copy) {
            SDL_GPUTextureRegion source{};
            source.texture=texture;source.w=size;source.h=size;source.d=1;source.mip_level=mip;
            SDL_GPUTextureTransferInfo destination{};
            destination.transfer_buffer=transfer;destination.pixels_per_row=size;destination.rows_per_layer=size;
            SDL_DownloadFromGPUTexture(copy,&source,&destination);
            SDL_EndGPUCopyPass(copy);
            SDL_GPUFence *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            CHECK(fence);
            if(fence) {
                CHECK(SDL_WaitForGPUFences(frame->Device(),true,&fence,1));
                SDL_ReleaseGPUFence(frame->Device(),fence);
                const uint16_t *mapped=static_cast<const uint16_t *>(
                    SDL_MapGPUTransferBuffer(frame->Device(),transfer,false));
                CHECK(mapped);
                if(mapped) {
                    result.resize(size*size*4);
                    for(unsigned i=0;i<result.size();++i)result[i]=Half(mapped[i]);
                    SDL_UnmapGPUTransferBuffer(frame->Device(),transfer);
                }
            }
        } else SDL_CancelGPUCommandBuffer(commands);
    }
    SDL_ReleaseGPUTransferBuffer(frame->Device(),transfer);
    return result;
}

using namespace WaterPathTracing;
void Quad(std::vector<Triangle> &scene,Vec3 a,Vec3 b,Vec3 c,Vec3 d,Vec3 colour)
{
    scene.push_back({a,b,c,colour});scene.push_back({a,c,d,colour});
}
void Horizontal(std::vector<Triangle> &scene,float z,Vec3 colour)
{
    Quad(scene,{-100,-100,z},{100,-100,z},{100,100,z},{-100,100,z},colour);
}
bool Approx(float actual,float expected,float tolerance=0.004f)
{
    return std::isfinite(actual)&&std::abs(actual-expected)<tolerance;
}
bool Contains(const std::string &text,const char *part) { return text.find(part)!=std::string::npos; }

void CheckCapture(SdlGpuFrame *frame)
{
    CHECK(frame->Water_Path_Texture()==nullptr);
    CHECK(Contains(frame->Water_Path_Report(),"inactive"));
    View view{};
    view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.sunDirection={0,0,0};view.sunRadiance={0,0,0}; // Zero sun must stay finite.
    view.skyLow=view.skyHigh={.1f,.2f,.3f};
    std::vector<Triangle> scene;
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(frame->Water_Path_Texture()!=nullptr);
    auto pixels=Read(frame,view.size);
    CHECK(pixels.size()==view.size*view.size*4);
    if(pixels.empty())return;
    for(unsigned i=0;i<pixels.size();i+=4) {
        CHECK(Approx(pixels[i],0));
        CHECK(Approx(pixels[i+1],0));
        CHECK(Approx(pixels[i+2],0));
        CHECK(Approx(pixels[i+3],0));
    }

    // Geometry below the water plane lies between the virtual camera and the water.
    // It must not replace the material's sky with an opaque black first hit.
    Horizontal(scene,-1,{0,0,0});
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto clipped=Read(frame,view.size);
    CHECK(clipped==pixels);

    // Neither a camera image nor a depth buffer is supplied: hit colour comes from geometry.
    scene.clear();Horizontal(scene,2,{.7f,.2f,.05f});Horizontal(scene,4,{.05f,.1f,.9f});
    Quad(scene,{1000,0,20},{1001,0,20},{1001,1,20},{1000,1,20},{.1f,.1f,.1f});
    view.skyLow=view.skyHigh={1,1,1};
    CHECK(frame->Prepare_Water_Path(scene,view));
    pixels=Read(frame,view.size);
    for(unsigned i=0;i<pixels.size();i+=4) {
        CHECK(Approx(pixels[i],.7f));
        CHECK(Approx(pixels[i+1],.2f));
        CHECK(Approx(pixels[i+2],.05f));
    }
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds"));
    for(unsigned sample=1;sample<64;++sample)CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"64/64 samples"));
    SDL_GPUTexture *frozen=frame->Water_Path_Texture();
    const auto converged=Read(frame,view.size);
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(frame->Water_Path_Texture()==frozen);
    CHECK(Read(frame,view.size)==converged);
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds"));

    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds"));
    scene[0].albedo=scene[1].albedo={.05f,.15f,.8f};
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds, 1 refits"));
    const auto changed=Read(frame,view.size);
    CHECK(changed!=pixels);
    if(!changed.empty())CHECK(Approx(changed[2],.8f));

    // Refit bounds and triangle texels together: the nearer blue-tinted plane moves
    // behind the far blue plane. Reversing source order must preserve that new image.
    for(unsigned i=0;i<2;++i) {
        scene[i].a.z+=6;scene[i].b.z+=6;scene[i].c.z+=6;
    }
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds, 2 refits"));
    const auto moved=Read(frame,view.size);
    CHECK(moved!=changed);
    for(unsigned i=0;i<moved.size();i+=4)CHECK(Approx(moved[i+2],.9f));
    std::reverse(scene.begin(),scene.end());
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds, 3 refits"));
    CHECK(Read(frame,view.size)==moved);

    view.rayTopLeft.z=-1; // Rays going farther below the plane have no valid reflection.
    CHECK(frame->Prepare_Water_Path(scene,view));
    pixels=Read(frame,view.size);
    for(float value:pixels)CHECK(Approx(value,0));
    view.rayTopLeft.z=1;scene.clear();Horizontal(scene,2,{0,0,0});
    CHECK(frame->Prepare_Water_Path(scene,view));pixels=Read(frame,view.size);
    for(unsigned i=0;i<pixels.size();i+=4) {
        CHECK(Approx(pixels[i],0)&&Approx(pixels[i+1],0)&&Approx(pixels[i+2],0));
        CHECK(Approx(pixels[i+3],1)); // real black geometry still occludes the environment
    }
    frame->Clear_Water_Path();
    CHECK(frame->Water_Path_Texture()==nullptr);
    CHECK(Contains(frame->Water_Path_Report(),"inactive"));
}

void CheckStablePrimaryCoverage(SdlGpuFrame *frame)
{
    View view{};view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};view.sunRadiance={0,0,0};
    const Vec3 albedo={.35f,.65f,.9f};
    const auto plane=[](float x,float y) { return Vec3{x,y,2+.05f*x}; };
    std::vector<Triangle> scene={
        {plane(-2.83f,-2.59f),plane(2.73f,2.57f),plane(2.96f,2.22f),albedo},
        {plane(-2.69f,2.87f),plane(2.53f,-2.43f),plane(2.81f,-2.13f),albedo},
        {{1000,1000,50},{1001,1000,50},{1000,1001,50},{.1f,.2f,.3f}}
    };
    Scene oracle;CHECK(oracle.Build(scene));
    const auto hitAt=[&](unsigned x,unsigned y,float ox,float oy) {
        return oracle.Trace(view.origin,{-.5f+(x+ox)/32,.5f-(y+oy)/32,1}).found;
    };
    std::vector<int> expected(32*32,-1);
    unsigned robustHits=0,robustMisses=0,sensitive=0;
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
        const bool centre=hitAt(x,y,.5f,.5f);
        bool robust=true;
        for(float oy:{.45f,.5f,.55f}) for(float ox:{.45f,.5f,.55f})
            robust&=hitAt(x,y,ox,oy)==centre;
        if(robust) {
            expected[y*32+x]=centre?1:0;
            if(centre)++robustHits;else ++robustMisses;
        }
        if(hitAt(x,y,.125f,.125f)!=hitAt(x,y,.875f,.875f)
            ||hitAt(x,y,.125f,.875f)!=hitAt(x,y,.875f,.125f))++sensitive;
    }
    CHECK(robustHits>16 && robustMisses>512);
    CHECK(sensitive>16); // The thin silhouettes arm the former subpixel-primary-jitter control.
    std::vector<float> reference;
    for(unsigned epoch=0;epoch<3;++epoch) {
        if(epoch) {
            // Moving remote geometry invalidates lighting history without changing
            // any visible first hit or bounded secondary illumination.
            scene.back().a.x+=1;scene.back().b.x+=1;scene.back().c.x+=1;
        }
        for(unsigned sample=0;sample<8;++sample) {
            CHECK(frame->Prepare_Water_Path(scene,view));
            const std::string state="32x32, "+std::to_string(sample+1)+"/64 samples";
            CHECK(Contains(frame->Water_Path_Report(),state.c_str()));
            const auto pixels=Read(frame,32);CHECK(pixels.size()==32*32*4);
            if(pixels.size()!=32*32*4)continue;
            if(reference.empty())reference=pixels;
            CHECK(pixels==reference);
            for(unsigned i=0;i<expected.size();++i) if(expected[i]>=0) {
                const float coverage=float(expected[i]);
                CHECK(Approx(pixels[i*4+3],coverage));
                CHECK(Approx(pixels[i*4],albedo.x*coverage));
                CHECK(Approx(pixels[i*4+1],albedo.y*coverage));
                CHECK(Approx(pixels[i*4+2],albedo.z*coverage));
            }
        }
    }
    frame->Clear_Water_Path();
}

void CheckPrimaryMaterials(SdlGpuFrame *frame)
{
    View view{};
    view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};
    view.sunDirection=view.sunRadiance={0,0,0};
    std::vector<Triangle> scene;
    Quad(scene,{-4,-4,2},{4,-4,2},{4,4,2},{-4,4,2},{.09f,.1f,.15f});
    PrimaryMaterialPacket materials;
    // Separate atlas ranges contain the same texture. Different vertex UV layouts
    // still expose material/source-index confusion after BVH reorder or compaction.
    materials.texels={{0,0,1,1},{0,0,1,1},{0,0,1,1},{0,0,1,1},
        {.1f,.2f,.9f,1},{.9f,.1f,.2f,1},{.2f,.8f,.1f,1},{.8f,.9f,.7f,1},
        {.5f,.5f,.475f,1}, // First texture's linear 1x1 mip.
        {.1f,.2f,.9f,1},{.9f,.1f,.2f,1},{.2f,.8f,.1f,1},{.8f,.9f,.7f,1},
        {.5f,.5f,.475f,1}};
    materials.triangles={
        {{-.75f,-1.25f,2.25f,-1.25f},{2.25f,1.75f,2,2},{.8f,.6f,.9f,5}},
        {{-.75f,-1.25f,2.25f,1.75f},{-.75f,1.75f,2,2},{.8f,.6f,.9f,10}}};
    CHECK(PrimaryTextureTexelCount(2,2)==5);
    const auto expected=[&](unsigned x,unsigned y,const PrimaryMaterialPacket &packet) {
        const float hitX=7*((x+.5f)/view.size-.5f);
        const float hitY=7*(.5f-(y+.5f)/view.size);
        const unsigned face=hitY<=hitX?0:1;
        // The affine UV field is shared across the diagonal, including negative
        // and repeated coordinates. Bilinear wrapping uses texel-centre semantics.
        const PrimaryMaterial &material=packet.triangles[face];
        const float baryB=face?(hitX+4)/8:(hitX-hitY)/8;
        const float baryC=face?(hitY-hitX)/8:(hitY+4)/8;
        const float baryA=1-baryB-baryC;
        const float u=material.uvAB.x*baryA+material.uvAB.z*baryB+material.uvCSize.x*baryC;
        const float v=material.uvAB.y*baryA+material.uvAB.w*baryB+material.uvCSize.y*baryC;
        const float tx=2*u-.5f,ty=2*v-.5f;
        const int ix=int(std::floor(tx)),iy=int(std::floor(ty));
        const float fx=tx-ix,fy=ty-iy;
        const unsigned offset=unsigned(material.diffuseOffset.w)-1;
        const auto texel=[&](int cx,int cy) -> const Float4 & {
            return packet.texels[offset+unsigned((cy%2+2)%2)*2+unsigned((cx%2+2)%2)];
        };
        const Float4 a=texel(ix,iy),b=texel(ix+1,iy),c=texel(ix,iy+1),d=texel(ix+1,iy+1);
        const auto channel=[&](float aa,float bb,float cc,float dd) {
            return (aa*(1-fx)+bb*fx)*(1-fy)+(cc*(1-fx)+dd*fx)*fy;
        };
        return Vec3{channel(a.x,b.x,c.x,d.x)*material.diffuseOffset.x,
            channel(a.y,b.y,c.y,d.y)*material.diffuseOffset.y,
            channel(a.z,b.z,c.z,d.z)*material.diffuseOffset.z};
    };
    const auto checkImage=[&](const std::vector<float> &pixels,const PrimaryMaterialPacket &packet) {
        CHECK(pixels.size()==view.size*view.size*4);
        if(pixels.size()!=view.size*view.size*4)return;
        bool matches=true;
        for(unsigned y=0;y<view.size;++y)for(unsigned x=0;x<view.size;++x) {
            const unsigned pixel=(y*view.size+x)*4;
            const Vec3 colour=expected(x,y,packet);
            matches&=Approx(pixels[pixel],colour.x,.006f)
                &&Approx(pixels[pixel+1],colour.y,.006f)
                &&Approx(pixels[pixel+2],colour.z,.006f)&&Approx(pixels[pixel+3],1);
        }
        CHECK(matches); // Includes face interiors and both sides of the diagonal seam.
    };
    const auto sameImage=[](const std::vector<float> &a,const std::vector<float> &b) {
        if(a.empty()||a.size()!=b.size())return false;
        for(unsigned i=0;i<a.size();++i)if(!Approx(a[i],b[i],.0008f))return false;
        return true;
    };
    const uint64_t revision=0; // Hash mutable geometry and material payloads.
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    const auto first=Read(frame,view.size);checkImage(first,materials);
    for(unsigned sample=1;sample<4;++sample)
        CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(Contains(frame->Water_Path_Report(),"4/64 samples"));
    const auto accumulated=Read(frame,view.size);checkImage(accumulated,materials);
    CHECK(sameImage(accumulated,first)); // Primary material detail must not jitter between samples.

    // This UV field spans only .164 texels per capture pixel. The analytic checks
    // therefore verify magnified level-zero detail, including both sides of seams.
    // Keep the geometry and pointer unchanged: material changes alone
    // must reset history, including edits to atlas texels and base diffuse colour.
    for(auto &material:materials.triangles)material.diffuseOffset.x=.4f;
    for(unsigned offset:{4u,9u}) {
        materials.texels[offset].y=.65f;
        materials.texels[offset+4].y=.6125f; // Preserve the linear mip average.
    }
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    auto changed=Read(frame,view.size);checkImage(changed,materials);
    CHECK(changed!=first);

    for(auto &material:materials.triangles) {
        material.uvAB.x+=.25f;material.uvAB.z+=.25f;material.uvCSize.x+=.25f;
    }
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    const auto shifted=Read(frame,view.size);checkImage(shifted,materials);
    CHECK(shifted!=changed);changed=shifted;

    // Raising the UV frequency requests a 5.25-texel footprint per output pixel.
    // Each 2x2 texture must resolve to its linear 1x1 mean, across both faces.
    PrimaryMaterialPacket minifiedMaterials=materials;
    for(auto &material:minifiedMaterials.triangles) {
        material.uvAB.x*=32;material.uvAB.y*=32;
        material.uvAB.z*=32;material.uvAB.w*=32;
        material.uvCSize.x*=32;material.uvCSize.y*=32;
    }
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&minifiedMaterials));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    const auto minified=Read(frame,view.size);
    CHECK(minified.size()==view.size*view.size*4);
    bool filteredMean=!minified.empty();
    for(unsigned i=0;i<minified.size();i+=4)
        filteredMean&=Approx(minified[i],.5f*.4f,.006f)
            &&Approx(minified[i+1],.6125f*.6f,.006f)
            &&Approx(minified[i+2],.475f*.9f,.006f)&&Approx(minified[i+3],1);
    CHECK(filteredMean);
    // Subtexel motion changes the aliased base-level sample but cannot change
    // the correctly minified texture's average.
    for(auto &material:minifiedMaterials.triangles) {
        material.uvAB.x+=.25f;material.uvAB.z+=.25f;material.uvCSize.x+=.25f;
    }
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&minifiedMaterials));
    CHECK(sameImage(Read(frame,view.size),minified));
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    checkImage(Read(frame,view.size),materials); // Return to sharp magnified detail.

    std::reverse(scene.begin(),scene.end());
    std::reverse(materials.triangles.begin(),materials.triangles.end());
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    const auto reordered=Read(frame,view.size);
    CHECK(sameImage(reordered,changed));
    scene.insert(scene.begin(),Triangle{{0,0,2},{0,0,2},{0,0,2},{1,0,1}});
    materials.triangles.insert(materials.triangles.begin(),
        PrimaryMaterial{{0,0,0,0},{0,0,2,2},{1,1,1,1}});
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(sameImage(Read(frame,view.size),changed)); // Dropped faces cannot shift the UV/material association.

    PrimaryMaterialPacket invalid=materials;
    invalid.triangles.pop_back();
    CHECK(!frame->Prepare_Water_Path(scene,view,revision,&invalid));
    CHECK(frame->Water_Path_Texture()==nullptr);
    CHECK(Contains(frame->Water_Path_Report(),"unavailable"));
    frame->Clear_Water_Path();
    invalid=materials;invalid.triangles.back().diffuseOffset.w=float(invalid.texels.size()+1);
    CHECK(!frame->Prepare_Water_Path(scene,view,revision,&invalid));
    CHECK(frame->Water_Path_Texture()==nullptr);
    // Leaving/re-entering the mode clears the deliberately latched failure.
    frame->Clear_Water_Path();
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(sameImage(Read(frame,view.size),changed));
    // The legacy path and per-face fallback retain exported triangle albedo.
    CHECK(frame->Prepare_Water_Path(scene,view,revision,nullptr));
    const auto fallback=Read(frame,view.size);
    CHECK(fallback.size()==view.size*view.size*4);
    bool constantFallback=!fallback.empty();
    for(unsigned i=0;i<fallback.size();i+=4)
        constantFallback&=Approx(fallback[i],.09f)&&Approx(fallback[i+1],.1f)
            &&Approx(fallback[i+2],.15f)&&Approx(fallback[i+3],1);
    CHECK(constantFallback);
    for(auto &material:materials.triangles)material.diffuseOffset.w=0;
    CHECK(frame->Prepare_Water_Path(scene,view,revision,&materials));
    CHECK(sameImage(Read(frame,view.size),fallback));
    frame->Clear_Water_Path();
}

void CheckIndirectBounce(SdlGpuFrame *frame)
{
    View view{};
    view.size=32;view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1};view.rayDx={.2f,0,0};view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={0,0,0};
    view.sunDirection=Normalized({1,0,.5f});
    view.sunRadiance={3.14159265f,3.14159265f,3.14159265f};
    std::vector<Triangle> scene;
    // The first wall faces away from the sun. Its only illumination comes from a
    // second, coloured wall behind the water-plane origins, through a sampled bounce.
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{.8f,.8f,.8f});
    Quad(scene,{-4,-100,-100},{-4,100,-100},{-4,100,100},{-4,-100,100},{.8f,.02f,.02f});
    Quad(scene,{1000,0,20},{1001,0,20},{1001,1,20},{1000,1,20},{.1f,.1f,.1f});
    for(unsigned sample=0;sample<64;++sample)CHECK(frame->Prepare_Water_Path(scene,view));
    const auto red=Read(frame,view.size);
    double redSum=0,greenSum=0,blueSum=0;
    for(unsigned i=0;i<red.size();i+=4) {
        redSum+=red[i];greenSum+=red[i+1];blueSum+=red[i+2];
        CHECK(std::isfinite(red[i])&&std::isfinite(red[i+1])&&std::isfinite(red[i+2]));
    }
    CHECK(redSum>10);
    CHECK(redSum>greenSum*2&&redSum>blueSum*2);
    scene[2].albedo=scene[3].albedo={.02f,.02f,.8f};
    for(unsigned sample=0;sample<64;++sample)CHECK(frame->Prepare_Water_Path(scene,view));
    const auto blue=Read(frame,view.size);
    double changedRed=0,changedBlue=0;
    for(unsigned i=0;i<blue.size();i+=4) { changedRed+=blue[i];changedBlue+=blue[i+2]; }
    CHECK(changedBlue>10&&changedBlue>changedRed*2);
    CHECK(std::abs(changedBlue-redSum)<1); // Same geometry, swapped linear colour channels.
    // The continuation cannot reach the coloured wall within this distance.
    view.secondaryDistance=.01f;
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto shortBounce=Read(frame,view.size);
    bool shortDark=true;
    for(unsigned i=0;i<shortBounce.size();i+=4)
        shortDark&=Approx(shortBounce[i],0)&&Approx(shortBounce[i+1],0)&&Approx(shortBounce[i+2],0);
    CHECK(shortDark);
    // A one-node budget cannot resolve the nontrivial BVH. It must contribute no
    // unknown secondary light; the primary surface and its coverage remain valid.
    view.secondaryDistance=160;view.secondaryNodeBudget=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto boundedBounce=Read(frame,view.size);
    bool boundedDark=true;
    for(unsigned i=0;i<boundedBounce.size();i+=4)
        boundedDark&=Approx(boundedBounce[i],0)&&Approx(boundedBounce[i+1],0)
            &&Approx(boundedBounce[i+2],0)&&Approx(boundedBounce[i+3],1);
    CHECK(boundedDark);
    std::printf("water_path_gpu: indirect wall sums red %.3f, blue %.3f; changed blue %.3f\n",
                redSum,blueSum,changedBlue);
    frame->Clear_Water_Path();
}

void CheckMotionResolution(SdlGpuFrame *frame)
{
    View view{};
    view.size=256;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};
    view.sunRadiance={0,0,0};
    std::vector<Triangle> scene;
    uint64_t revision=0;
    Horizontal(scene,2,{.7f,.2f,.05f});
    const auto checkImage=[&](unsigned size,Vec3 linear) {
        const std::string dimensions=std::to_string(size)+"x"+std::to_string(size)+", ";
        const bool matching=Contains(frame->Water_Path_Report(),dimensions.c_str());
        CHECK(matching);
        if(!matching)return; // Do not issue a readback beyond an incorrectly sized target.
        const auto pixels=Read(frame,size);
        CHECK(pixels.size()==size*size*4);
        bool finite=true,correct=true;
        for(unsigned i=0;i<pixels.size();i+=4) {
            for(unsigned channel=0;channel<4;++channel)finite&=std::isfinite(pixels[i+channel]);
            correct&=Approx(pixels[i],linear.x)&&Approx(pixels[i+1],linear.y)
                &&Approx(pixels[i+2],linear.z)&&Approx(pixels[i+3],1);
        }
        CHECK(finite&&correct);
    };

    // The initial still capture starts at requested quality.
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.7f,.2f,.05f});

    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"128x128, 1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"1 BVH builds, 0 refits"));
    checkImage(128,{.7f,.2f,.05f});
    for(unsigned unchanged=1;unchanged<8;++unchanged) {
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
        const std::string state="128x128, "+std::to_string(unchanged+1)+"/64 samples";
        CHECK(Contains(frame->Water_Path_Report(),state.c_str()));
    }
    // A held camera snapshot must satisfy both the quiet time and render count;
    // deterministic fast-render coverage lives in the CPU policy tests.
    SDL_Delay(WaterPathTracing::MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.7f,.2f,.05f});
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 2/64 samples"));

    // Fresh scene revisions reset accumulation without lowering stationary-view quality.
    scene[0].albedo=scene[1].albedo={.05f,.3f,.8f};
    for(unsigned update=0;update<4;++update) {
        ++revision;
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
        CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
        checkImage(256,{.05f,.3f,.8f});
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
        CHECK(Contains(frame->Water_Path_Report(),"256x256, 2/64 samples"));
    }

    // A lighting change has a different image and must discard the previous mean,
    // while retaining the requested camera resolution.
    view.skyLow=view.skyHigh={.5f,.5f,.5f};
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.025f,.15f,.4f});
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 2/64 samples"));
    view.secondaryDistance+=1;
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.025f,.15f,.4f});

    // A very fast loop can converge the reduced capture before the quiet timer
    // expires. The sample cap must still permit a later resolution restoration.
    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    for(unsigned sample=1;sample<64;++sample)
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
    SDL_Delay(WaterPathTracing::MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkImage(256,{.025f,.15f,.4f});
    frame->Clear_Water_Path();
}

void CheckReflectionDetailResolution(SdlGpuFrame *frame)
{
    View view{};
    ApplyReflectionDetail(view,1);
    view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};view.sunRadiance={0,0,0};
    std::vector<Triangle> scene;
    Vec3 colour={.7f,.2f,.05f};
    Horizontal(scene,2,colour);
    uint64_t revision=1;
    const auto checkCapture=[&](unsigned size,unsigned samples) {
        const std::string state=std::to_string(size)+"x"+std::to_string(size)+", "
            +std::to_string(samples)+"/64 samples";
        const bool matching=Contains(frame->Water_Path_Report(),state.c_str());
        CHECK(matching);
        if(!matching)return;
        // The uniformly lit plane has the same colour in every mip. Read a small
        // mip to verify the capture and its generated pyramid without large transfers.
        unsigned mip=0,mipSize=size;
        while(mipSize>32) {mipSize/=2;++mip;}
        const auto pixels=Read(frame,mipSize,nullptr,mip);
        CHECK(pixels.size()==mipSize*mipSize*4);
        bool correct=true;
        for(unsigned i=0;i<pixels.size();i+=4)
            correct&=Approx(pixels[i],colour.x)&&Approx(pixels[i+1],colour.y)
                &&Approx(pixels[i+2],colour.z)&&Approx(pixels[i+3],1);
        CHECK(correct);
    };

    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(512,1);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(512,2);

    // High must retain at least 512 during repeated camera and scene updates.
    for(unsigned update=0;update<3;++update) {
        view.origin.x+=1;
        colour=update&1?Vec3{.7f,.2f,.05f}:Vec3{.05f,.3f,.8f};
        scene[0].albedo=scene[1].albedo=colour;
        ++revision;
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
        checkCapture(512,1);
    }
    colour={.15f,.6f,.2f};
    scene[0].albedo=scene[1].albedo=colour;
    ++revision;
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(512,1);

    // A live quality switch discards the previous capture's mean. Low retains
    // its original moving target and recovers the settled target after quiet.
    ApplyReflectionDetail(view,0);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(128,1);
    for(unsigned unchanged=1;unchanged<MotionRecovery::QuietFrames;++unchanged)
        CHECK(frame->Prepare_Water_Path(scene,view,revision));
    SDL_Delay(MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(256,1);

    ApplyReflectionDetail(view,1);
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(512,1);
    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view,revision));
    checkCapture(512,1);
    frame->Clear_Water_Path();
}

void CheckShadowAndPrimaryLimits(SdlGpuFrame *frame)
{
    View view{};
    view.size=32;view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1};view.rayDx={.2f,0,0};view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={0,0,0};
    view.sunDirection=Normalized({-1,0,.5f});
    view.sunRadiance={3.14159265f,3.14159265f,3.14159265f};
    view.secondaryDistance=.01f;view.shadowDistance=10;
    std::vector<Triangle> scene;
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{.8f,.8f,.8f});
    Quad(scene,{-20,-100,-100},{-20,100,-100},{-20,100,100},{-20,-100,100},{.1f,.1f,.1f});
    Quad(scene,{1000,0,20},{1001,0,20},{1001,1,20},{1000,1,20},{.1f,.1f,.1f});
    const auto checkColour=[&](Vec3 expected,float coverage=1) {
        const auto pixels=Read(frame,view.size);
        CHECK(pixels.size()==view.size*view.size*4);
        bool correct=true;
        for(unsigned i=0;i<pixels.size();i+=4)
            correct&=Approx(pixels[i],expected.x)&&Approx(pixels[i+1],expected.y)
                &&Approx(pixels[i+2],expected.z)&&Approx(pixels[i+3],coverage);
        CHECK(correct);
    };
    CHECK(frame->Prepare_Water_Path(scene,view));
    const float lit=.8f*-view.sunDirection.x;
    checkColour({lit,lit,lit}); // Blocker is farther than the short shadow distance.

    view.shadowDistance=100;
    CHECK(frame->Prepare_Water_Path(scene,view));
    checkColour({0,0,0}); // The longer shadow ray must find it.

    view.shadowDistance=10;view.shadowNodeBudget=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    checkColour({0,0,0}); // An exhausted shadow traversal never invents sunlight.

    view.shadowNodeBudget=512;view.secondaryNodeBudget=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    checkColour({lit,lit,lit}); // Auxiliary budgets do not remove primary geometry.

    view.maxDistance=.05f;
    view.skyLow=view.skyHigh={.15f,.2f,.3f};
    CHECK(frame->Prepare_Water_Path(scene,view));
    checkColour({0,0,0},0); // A primary distance miss leaves sky evaluation to the water material.
    frame->Clear_Water_Path();
}

void CheckAuthoredDiffuseLighting(SdlGpuFrame *frame)
{
    View view{};
    view.size=32;view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1};view.rayDx={.2f,0,0};view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={0,0,0};
    view.sunDirection={-1,0,0};
    view.sunRadiance=DisplayDiffuseToIrradiance({.88f,.81f,.66f});
    std::vector<Triangle> scene;
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{1,1,1});
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto pixels=Read(frame,view.size);
    CHECK(pixels.size()==view.size*view.size*4);
    bool matchesAuthored=true;
    const Vec3 expected=DisplayToLinear({.88f,.81f,.66f});
    for(unsigned i=0;i<pixels.size();i+=4)
        matchesAuthored&=Approx(pixels[i],expected.x)&&Approx(pixels[i+1],expected.y)
            &&Approx(pixels[i+2],expected.z)&&Approx(pixels[i+3],1);
    // A white wall facing the light must retain the map's diffuse colour. Passing
    // display-decoded colour as irradiance used to dim this by pi in linear light.
    CHECK(matchesAuthored);
    frame->Clear_Water_Path();
}

void CheckLinearReflectionFiltering(SdlGpuFrame *frame)
{
    View view{};
    view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};view.sunRadiance={0,0,0};
    std::vector<Triangle> scene;
    Quad(scene,{-100,-100,2},{0,-100,2},{0,100,2},{-100,100,2},{.1f,.2f,.3f});
    Quad(scene,{0,-100,2},{100,-100,2},{100,100,2},{0,100,2},{.5f,.8f,1});
    // Exercise the stored history after accumulation, then hardware-filter across
    // a silhouette. The interface needs the mean radiance, not a gamma-space mean.
    for(unsigned sample=0;sample<64;++sample)CHECK(frame->Prepare_Water_Path(scene,view));
    if(!frame->Water_Path_Texture())return;
    SDL_GPUTextureCreateInfo info{};
    info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=info.height=info.layer_count_or_depth=info.num_levels=1;
    SDL_GPUTexture *filtered=SDL_CreateGPUTexture(frame->Device(),&info);
    CHECK(filtered);
    if(filtered) {
        SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(frame->Device());
        CHECK(commands);
        if(commands) {
            SDL_GPUBlitInfo blit{};
            blit.source.texture=frame->Water_Path_Texture();
            blit.source.w=blit.source.h=view.size;
            blit.destination.texture=filtered;blit.destination.w=blit.destination.h=1;
            blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(commands,&blit);
            CHECK(SDL_SubmitGPUCommandBuffer(commands));
            const auto pixels=Read(frame,1,filtered);
            CHECK(pixels.size()==4);
            if(pixels.size()==4) {
                CHECK(Approx(pixels[0],.3f));
                CHECK(Approx(pixels[1],.5f));
                CHECK(Approx(pixels[2],.65f));
                CHECK(Approx(pixels[3],1));
            }
        }
        SDL_ReleaseGPUTexture(frame->Device(),filtered);
    }
    frame->Clear_Water_Path();
}

void CheckReflectionMinification(SdlGpuFrame *frame)
{
    View view{};view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={1,1,1};view.sunRadiance={0,0,0};
    std::vector<Triangle> scene;
    // One constant-colour stripe per capture texel. Primary jitter remains within
    // its stripe; averaging here would erase real level-zero material detail.
    for(unsigned x=0;x<32;++x) {
        const float left=-3.5f+7.0f*x/32,right=-3.5f+7.0f*(x+1)/32;
        const Vec3 colour=x%2?Vec3{.9f,.6f,1}:Vec3{.1f,.2f,.3f};
        Quad(scene,{left,-100,2},{right,-100,2},{right,100,2},{left,100,2},colour);
    }
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto original=Read(frame,32);
    CHECK(original.size()==32*32*4);
    if(original.size()!=32*32*4)return;
    if(original.size()==32*32*4) for(unsigned x=0;x<32;++x)
        CHECK(Approx(original[(16*32+x)*4],x%2?.9f:.1f));
    for(unsigned mip=1,size=16;size;size/=2,++mip) {
        const auto filtered=Read(frame,size,nullptr,mip);
        CHECK(filtered.size()==size*size*4);
        for(unsigned i=0;i<filtered.size();i+=4) {
            CHECK(Approx(filtered[i],.5f));CHECK(Approx(filtered[i+1],.4f));
            CHECK(Approx(filtered[i+2],.65f));CHECK(Approx(filtered[i+3],1));
        }
    }
    struct Resources {
        SDL_GPUDevice *device;
        SDL_GPUShader *vertex=nullptr,*pixel=nullptr;
        SDL_GPUGraphicsPipeline *pipeline=nullptr;
        SDL_GPUTexture *target=nullptr;
        ~Resources() {
            if(target)SDL_ReleaseGPUTexture(device,target);
            if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(pixel)SDL_ReleaseGPUShader(device,pixel);
            if(vertex)SDL_ReleaseGPUShader(device,vertex);
        }
    } resources{frame->Device()};
    const char *vertexSource="float4 main(uint id:SV_VertexID):SV_Position {"
        "return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    const char *pixelSource=R"HLSL(
Texture2D Capture:register(t0,space2);
SamplerState CaptureSampler:register(s0,space2);
float4 main(float4 p:SV_Position):SV_Target0 {
    float2 uv=p.xy/4.0+float2((p.y<2.0?.5:1.5)/32.0,0);
    // Match the production water material's implicit-derivative Sample lookup.
    // Mip zero in alpha is an armed control for unfiltered subpixel shimmer.
    return float4(Capture.Sample(CaptureSampler,uv).rgb,
        Capture.SampleLevel(CaptureSampler,uv,0).r);
})HLSL";
    std::vector<unsigned char> spirv;std::string error;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);
    CHECK(compiled);if(!compiled)return;
    resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);
    CHECK(resources.vertex);if(!resources.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);
    CHECK(compiled);if(!compiled)return;
    resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);
    CHECK(resources.pixel);if(!resources.pixel)return;
    SDL_GPUColorTargetDescription description{};description.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader=resources.vertex;pipeline.fragment_shader=resources.pixel;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.color_target_descriptions=&description;pipeline.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pipeline);
    CHECK(resources.pipeline);if(!resources.pipeline)return;
    SDL_GPUTextureCreateInfo target{};target.type=SDL_GPU_TEXTURETYPE_2D;target.format=description.format;
    target.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;target.width=target.height=4;
    target.layer_count_or_depth=target.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&target);
    CHECK(resources.target);if(!resources.target)return;
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);
    CHECK(commands);if(!commands)return;
    SDL_GPUColorTargetInfo attachment{};attachment.texture=resources.target;
    attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,nullptr);
    CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
    SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
    SDL_GPUTextureSamplerBinding binding{frame->Water_Path_Texture(),frame->Water_Path_Sampler()};
    SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    CHECK(SDL_SubmitGPUCommandBuffer(commands));
    const auto minified=Read(frame,4,resources.target);
    CHECK(minified.size()==4*4*4);
    for(unsigned i=0;i<minified.size();i+=4) {
        CHECK(Approx(minified[i],.5f));CHECK(Approx(minified[i+1],.4f));CHECK(Approx(minified[i+2],.65f));
    }
    if(minified.size()==64) CHECK(std::abs(minified[3]-minified[2*4*4+3])>.2f);
    // A second representation has the same visible stripes and unoccluded
    // environment transport, but shorter triangles change the BVH's centroid
    // split axis. Keep the original assertions: this control distinguishes
    // planar ray/BVH coverage errors from filtering or a weak numeric oracle.
    std::vector<Triangle> compact;
    for(unsigned x=0;x<32;++x) {
        const float left=-3.5f+7.0f*x/32,right=-3.5f+7.0f*(x+1)/32;
        const Vec3 colour=x%2?Vec3{.9f,.6f,1}:Vec3{.1f,.2f,.3f};
        Quad(compact,{left,-4,2},{right,-4,2},{right,4,2},{left,4,2},colour);
    }
    Scene cpuScene,longCpuScene;CHECK(cpuScene.Build(compact));CHECK(longCpuScene.Build(scene));
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
        const Vec3 direction={-.5f+(x+.5f)/32,.5f-(y+.5f)/32,1};
        const Hit hit=cpuScene.Trace(view.origin,direction);
        const Hit longHit=longCpuScene.Trace(view.origin,direction);
        CHECK(hit.found);
        CHECK(longHit.found);
        if(hit.found)CHECK(Approx(hit.albedo.x,x%2?.9f:.1f));
        if(longHit.found)CHECK(Approx(longHit.albedo.x,x%2?.9f:.1f));
    }
    frame->Clear_Water_Path(); // Force rebuilding the control's different centroid hierarchy.
    CHECK(frame->Prepare_Water_Path(compact,view));
    const auto compactPixels=Read(frame,32);CHECK(compactPixels.size()==32*32*4);
    if(compactPixels.size()==32*32*4) for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
        const unsigned i=(y*32+x)*4;
        CHECK(Approx(compactPixels[i],x%2?.9f:.1f));
        CHECK(Approx(compactPixels[i+3],1));
    }
    // Geometry misses are premultiplied zero radiance. Average hit coverage with
    // radiance, so the material can supply the same sky through every uncovered tap.
    scene.clear();
    Quad(scene,{-100,-100,2},{0,-100,2},{0,100,2},{-100,100,2},{.2f,.4f,.6f});
    CHECK(frame->Prepare_Water_Path(scene,view));
    const auto coverage=Read(frame,1,nullptr,5);CHECK(coverage.size()==4);
    if(coverage.size()==4) {
        CHECK(Approx(coverage[0],.1f));CHECK(Approx(coverage[1],.2f));
        CHECK(Approx(coverage[2],.3f));CHECK(Approx(coverage[3],.5f));
    }
    frame->Clear_Water_Path();
}

void CheckCaptureEnvironmentBoundary(SdlGpuFrame *frame)
{
    // Compile the actual generated main-water lookup/composition and production
    // foam sampler. This catches a capture contract change without inventing a
    // second reflection implementation as the GPU oracle.
    PixelPipelineDescription description{};std::string production;
    CHECK(EngineShader_Pixel_Program(ENGINE_SHADER_WATER_TRAPEZOID,description,production,
        COMBINER_SHADER_TARGET_SDL3_GPU));
    const auto body=[&](const char *start,const char *end) {
        const size_t a=production.find(start);
        const size_t brace=a==std::string::npos?a:production.find('{',a);
        const size_t first=brace==std::string::npos?brace:brace+1;
        const size_t last=first==std::string::npos?first:production.find(end,first);
        CHECK(first!=std::string::npos && last!=std::string::npos && last>=first);
        return first!=std::string::npos && last!=std::string::npos && last>=first
            ?production.substr(first,last-first):std::string();
    };
    const std::string sampling=body("} else if (WaterParameters.z>0.5) {","\n    }\n#endif");
    const std::string blend=body("} else if (pathTraced) {","} else reflection=");
    const size_t coverageStart=production.find("float2 border = min(reflectionUV, 1.0 - reflectionUV);");
    const size_t coverageEnd=production.find("float3 skyRadiance=waterLinear(sky);",coverageStart);
    CHECK(coverageStart!=std::string::npos && coverageEnd!=std::string::npos);
    if(sampling.empty() || blend.empty() || coverageStart==std::string::npos || coverageEnd==std::string::npos)return;
    const std::string pixelSource=std::string(R"HLSL(
Texture2D Texture1:register(t0,space2);
SamplerState Sampler1:register(s0,space2);
cbuffer Fixtures:register(b0,space3) {float4 Cases[16];};
float3 mainWater(float3 skyRadiance,float2 reflectionUV,float2 distortion,float2 blur) {
    float4 mirror=0;float2 mirrorFilterSupport=abs(blur);
    bool pathTraced=true,filteredPlanar=false;
    float4 WaterParameters=float4(0,0,1,0);float baseLevel=0;
)HLSL")+sampling+production.substr(coverageStart,coverageEnd-coverageStart)
        +"float3 reflection;\n"+blend+"return reflection;}\n"
        +WaterFoamShading::MaterialSource()+R"HLSL(
float4 main(float4 p:SV_Position):SV_Target0 {
    uint index=(uint)p.y*4u+(uint)p.x;
    float4 q=Cases[index];float3 sky=float3(.15,.4,.75);
    float3 value=index<8u?mainWater(sky,q.xy,float2(q.w,0),float2(q.z*.008,q.z*.008))
        :waterFoamPlanarReflection(sky,q.xy,float2(q.w,0),float2(1,1),q.z,1,true);
    return float4(value,1);
})HLSL";
    struct Resources {
        SDL_GPUDevice *device;SDL_GPUShader *vertex=nullptr,*pixel=nullptr;
        SDL_GPUGraphicsPipeline *pipeline=nullptr;SDL_GPUTexture *target=nullptr;
        ~Resources() {
            if(target)SDL_ReleaseGPUTexture(device,target);
            if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(pixel)SDL_ReleaseGPUShader(device,pixel);
            if(vertex)SDL_ReleaseGPUShader(device,vertex);
        }
    } resources{frame->Device()};
    const char *vertexSource="float4 main(uint id:SV_VertexID):SV_Position {"
        "return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    std::vector<unsigned char> spirv;std::string error;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);CHECK(compiled);if(!compiled)return;
    resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);CHECK(resources.vertex);if(!resources.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);CHECK(compiled);
    if(!compiled){std::fprintf(stderr,"Path boundary shader: %s\n",error.c_str());return;}
    resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);CHECK(resources.pixel);if(!resources.pixel)return;
    SDL_GPUColorTargetDescription targetDescription{};targetDescription.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader=resources.vertex;pipeline.fragment_shader=resources.pixel;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.color_target_descriptions=&targetDescription;pipeline.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pipeline);CHECK(resources.pipeline);if(!resources.pipeline)return;
    SDL_GPUTextureCreateInfo target{};target.type=SDL_GPU_TEXTURETYPE_2D;target.format=targetDescription.format;
    target.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;target.width=target.height=4;target.layer_count_or_depth=target.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&target);CHECK(resources.target);if(!resources.target)return;
    View view{};view.size=32;view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1};view.rayDx={1,0,0};view.rayDy={0,-1,0};
    view.skyLow=view.skyHigh={.9f,.01f,.02f}; // armed mismatch: the capture's lighting sky differs from the material
    view.sunRadiance={0,0,0};std::vector<Triangle> scene;
    const float coordinates[8]={-.05f,0,.01f,.25f,.75f,.99f,1,1.05f};
    for(unsigned geometry=0;geometry<2;++geometry) {
        if(geometry)Quad(scene,{-100,-100,2},{0,-100,2},{0,100,2},{-100,100,2},{0,0,0});
        CHECK(frame->Prepare_Water_Path(scene,view));
        if(!frame->Water_Path_Texture())return;
        for(unsigned coordinate=0;coordinate<8;++coordinate) {
            float cases[16][4]{};
            // Keep the lookup uniform within derivative quads. Unrelated UV fixtures
            // in adjacent pixels would legitimately request the coarsest capture mip.
            for(unsigned i=0;i<16;++i) {
                cases[i][0]=coordinates[coordinate];cases[i][1]=.5f;
                cases[i][2]=geometry?0:.3f;cases[i][3]=geometry?0:.015f;
            }
            SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);CHECK(commands);if(!commands)return;
            SDL_GPUColorTargetInfo attachment{};attachment.texture=resources.target;
            attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,nullptr);CHECK(pass);
            if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
            SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
            SDL_GPUTextureSamplerBinding binding{frame->Water_Path_Texture(),frame->Water_Path_Sampler()};
            SDL_BindGPUFragmentSamplers(pass,0,&binding,1);SDL_PushGPUFragmentUniformData(commands,0,cases,sizeof(cases));
            SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(commands));
            const auto pixels=Read(frame,4,resources.target);CHECK(pixels.size()==64);if(pixels.size()!=64)return;
            for(unsigned i=0;i<16;++i) {
                const float skyShare=geometry?(coordinate==2?.7f:coordinate==3?0:1):1;
                CHECK(Approx(pixels[i*4],.15f*skyShare));CHECK(Approx(pixels[i*4+1],.4f*skyShare));
                CHECK(Approx(pixels[i*4+2],.75f*skyShare));CHECK(Approx(pixels[i*4+3],1));
            }
        }
    }
    frame->Clear_Water_Path();
}

void CheckTerrainClarityEligibility(SdlGpuFrame *frame)
{
    using namespace WaterOpticalModel;
    // Each pixel carries terrain depth, deepest surrounding wet terrain, offshore
    // blend and actual scene-ray length. Hull/prop rays never replace terrain depth.
    const float fixtures[16][4]={
        {1.875f,5,1,3.2f}, {3.275f,5,1,5}, {1.875f,1.875f,1,3.2f}, {1.875f,2.375f,1,3.2f},
        {6,6.625f,1,.1f}, {7,7.625f,1,.5f}, {50,150,0,.1f}, {2,2,0,3.2f},
        {2,2,.5f,3.2f}, {2.5f,5,1,4}, {4,6,1,6}, {5.5f,7,1,8},
        {2,3,1,3.2f}, {2,3.5f,1,3.2f}, {NAN,5,1,3.2f}, {2,INFINITY,1,3.2f}
    };
    struct Resources {
        SDL_GPUDevice *device;
        SDL_GPUShader *vertex=nullptr,*pixel=nullptr;
        SDL_GPUGraphicsPipeline *pipeline=nullptr;
        SDL_GPUTexture *target=nullptr;
        ~Resources() {
            if(target)SDL_ReleaseGPUTexture(device,target);
            if(pipeline)SDL_ReleaseGPUGraphicsPipeline(device,pipeline);
            if(pixel)SDL_ReleaseGPUShader(device,pixel);
            if(vertex)SDL_ReleaseGPUShader(device,vertex);
        }
    } resources{frame->Device()};
    const char *vertexSource="float4 main(uint id:SV_VertexID):SV_Position {"
        "return float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1);}";
    const std::string pixelSource=std::string("cbuffer Samples:register(b0,space3) {float4 Fixtures[16];};\n")
        +GetHlslSource()+"float4 main(float4 p:SV_Position):SV_Target0 {"
        "float4 f=Fixtures[(uint)p.y*4+(uint)p.x];"
        "float e=WaterOpticalClarityEligibility(f.x,f.y,f.z);"
        "return float4(e,WaterOpticalTransmission(f.w,0,1,3,1,.9*e).g,"
        "WaterOpticalTransmission(f.w,0,1,3,1,0).g,1);}";
    std::vector<unsigned char> spirv;
    std::string error;
    bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertexSource,true,spirv,error);
    CHECK(compiled);if(!compiled){std::fprintf(stderr,"%s\n",error.c_str());return;}
    resources.vertex=SDL3_Create_Shader(resources.device,spirv,true,error);
    CHECK(resources.vertex);if(!resources.vertex)return;
    compiled=SDL3_Compile_HLSL_To_SPIRV(pixelSource,false,spirv,error);
    CHECK(compiled);if(!compiled){std::fprintf(stderr,"%s\n",error.c_str());return;}
    resources.pixel=SDL3_Create_Shader(resources.device,spirv,false,error);
    CHECK(resources.pixel);if(!resources.pixel)return;
    SDL_GPUColorTargetDescription description{};
    description.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    SDL_GPUGraphicsPipelineCreateInfo pipeline{};
    pipeline.vertex_shader=resources.vertex;pipeline.fragment_shader=resources.pixel;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.color_target_descriptions=&description;pipeline.target_info.num_color_targets=1;
    resources.pipeline=SDL_CreateGPUGraphicsPipeline(resources.device,&pipeline);
    CHECK(resources.pipeline);if(!resources.pipeline)return;
    SDL_GPUTextureCreateInfo target{};
    target.type=SDL_GPU_TEXTURETYPE_2D;target.format=description.format;
    target.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    target.width=target.height=4;target.layer_count_or_depth=target.num_levels=1;
    resources.target=SDL_CreateGPUTexture(resources.device,&target);
    CHECK(resources.target);if(!resources.target)return;
    CHECK(frame->Flush());
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(resources.device);
    CHECK(commands);if(!commands)return;
    SDL_GPUColorTargetInfo attachment{};
    attachment.texture=resources.target;attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;
    attachment.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&attachment,1,nullptr);
    CHECK(pass);if(!pass){SDL_CancelGPUCommandBuffer(commands);return;}
    SDL_BindGPUGraphicsPipeline(pass,resources.pipeline);
    SDL_PushGPUFragmentUniformData(commands,0,fixtures,sizeof(fixtures));
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    CHECK(SDL_SubmitGPUCommandBuffer(commands));
    const auto pixels=Read(frame,4,resources.target);
    CHECK(pixels.size()==64);if(pixels.size()!=64)return;
    for(unsigned i=0;i<16;++i) {
        const auto &f=fixtures[i];
        const float eligibility=ClarityEligibility(f[0],f[1],f[2]);
        const float clear=Transmission(f[3],0,1,3,1,.9f*eligibility).g;
        const float original=Transmission(f[3],0,1,3,1).g;
        CHECK(Approx(pixels[i*4],eligibility,.0006f));
        CHECK(Approx(pixels[i*4+1],clear,.0006f));
        CHECK(Approx(pixels[i*4+2],original,.0006f));
        CHECK(Approx(pixels[i*4+3],1,.0006f));
        if(eligibility==0)CHECK(pixels[i*4+1]==pixels[i*4+2]);
    }
    CHECK(pixels[0]>.99f && pixels[4]>.8f); // both raised ruin shelves qualify
    CHECK(pixels[8]==0 && pixels[16]==0 && pixels[20]==0); // flat sea and carrier basins do not
}
}

int main()
{
    if(!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("water_path_gpu: SKIP - SDL_Init: %s\n",SDL_GetError());return 77;
    }
    std::string error;
    SdlGpuFrame *frame=SdlGpuFrame::Create(nullptr,32,32,error);
    if(!frame) {
        std::printf("water_path_gpu: SKIP - no GPU: %s\n",error.c_str());SDL_Quit();return 77;
    }
    CheckCapture(frame);
    CheckStablePrimaryCoverage(frame);
    CheckPrimaryMaterials(frame);
    CheckIndirectBounce(frame);
    CheckMotionResolution(frame);
    CheckReflectionDetailResolution(frame);
    CheckShadowAndPrimaryLimits(frame);
    CheckAuthoredDiffuseLighting(frame);
    CheckLinearReflectionFiltering(frame);
    CheckReflectionMinification(frame);
    CheckCaptureEnvironmentBoundary(frame);
    CheckTerrainClarityEligibility(frame);
    delete frame;SDL_Quit();
    std::printf("water_path_gpu: %u checks, %u failed\n",checks,failures);
    return failures?1:0;
}
