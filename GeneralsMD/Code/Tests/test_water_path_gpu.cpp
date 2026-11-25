// The optional water tracer on the actual SDL GPU backend. No game or assets required.
#include "SdlGpuFrame.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterHardwarePathTracingShader.h"
#include "Platform/WaterPathTracingMotion.h"
#include "Platform/WaterPathTracingMaterials.h"
#include "Platform/WaterPathCaptureScope.h"
#include "Platform/HardwareRaySun.h"
#include "matrix3d.h"
#include "vector2.h"
#include "frustum.h"
#include "aabox.h"
#include "colmath.h"
#if defined(__linux__)
#include "../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#endif
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

std::vector<float> Read(SdlGpuFrame *frame,unsigned size,SDL_GPUTexture *texture=nullptr,unsigned mip=0,bool fullFloat=false)
{
    std::vector<float> result;
    CHECK(frame->Flush());
    if(!texture)texture=frame->Water_Path_Texture();
    if(!texture)return result;
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size=size*size*4*(fullFloat ? sizeof(float) : sizeof(uint16_t));
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
                const void *mapped=SDL_MapGPUTransferBuffer(frame->Device(),transfer,false);
                CHECK(mapped);
                if(mapped) {
                    result.resize(size*size*4);
                    for(unsigned i=0;i<result.size();++i)
                        result[i]=fullFloat ? static_cast<const float *>(mapped)[i]
                            : Half(static_cast<const uint16_t *>(mapped)[i]);
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
unsigned NativeCounter(SdlGpuFrame *frame, const char *name)
{
    const std::string report=frame->Water_Path_Report();
    const size_t at=report.find(name);
    CHECK(at!=std::string::npos);
    return at==std::string::npos ? 0u : unsigned(std::stoul(report.substr(at+std::strlen(name))));
}

void CheckHardwareRetry(SdlGpuFrame *frame, const std::vector<Triangle> &scene, const View &view,
    const PrimaryMaterialPacket *materials, bool path)
{
    const unsigned creates = NativeCounter(frame,"native pipeline creates=");
    View invalid = view; invalid.maxDistance = 0;
    CHECK(!frame->Prepare_Water_Path(scene,invalid,0,materials,true,path));
    CHECK(!frame->Water_Hardware_Available(path));
    CHECK(!frame->Prepare_Water_Path(scene,view,0,materials,true,path));
    CHECK(NativeCounter(frame,"native pipeline creates=")==creates);
    const bool enabled = HardwareRayTracing::Requested();
    const uint64_t generation = HardwareRayTracing::RequestGeneration();
    HardwareRayTracing::SetRequested(enabled);
    CHECK(HardwareRayTracing::RequestGeneration()==generation);
    CHECK(!frame->Water_Hardware_Available(path));
    HardwareRayTracing::SetRequested(!enabled);
    HardwareRayTracing::SetRequested(enabled);
    CHECK(frame->Water_Hardware_Available(path));
    CHECK(frame->Prepare_Water_Path(scene,view,0,materials,true,path));
    CHECK(frame->Water_Path_Texture()!=nullptr);
    // Recovery retires the possibly ambiguous native scene before recreating.
    CHECK(NativeCounter(frame,"native pipeline creates=")==creates+1);
}


void CheckHardwareReflection(SdlGpuFrame *frame)
{
    View view{};
    view.size=view.motionSize=32;
    view.origin={0,0,-5}; view.waterLevel=0;
    view.rayTopLeft={-.5f,.5f,1}; view.rayDx={1,0,0}; view.rayDy={0,-1,0};
    view.sunRadiance={0,0,0}; view.skyLow=view.skyHigh={1,1,1};
    // The real camera looks down -Z from z=5. This roof at z=8 is behind its
    // near plane, but the mirrored camera below the water looks upward into it.
    Matrix3D camera(true); camera.Set_Translation(Vector3(0,0,5));
    FrustumClass mainFrustum;
    mainFrustum.Init(camera,Vector2(-.5f,-.5f),Vector2(.5f,.5f),.1f,100.f);
    const AABoxClass building(Vector3(0,0,8),Vector3(3,3,.01f));
    const bool mainRejected=CollisionMath::Overlap_Test(mainFrustum,building)==CollisionMath::OUTSIDE;
    CHECK(mainRejected);
    CaptureScope reflectedFrustum(view,CaptureScope::Content::Primary);
    CHECK(reflectedFrustum.Matches({-3,-3,7.99f},{3,3,8.01f}));
    const std::vector<Triangle> empty;
    CHECK(!frame->Prepare_Water_Path(empty,view,99,nullptr,true));
    CHECK(frame->Water_Hardware_Available()); // Empty startup export is transient, not sticky failure.
    std::vector<Triangle> scene;
    Quad(scene,{-3,-3,8},{3,-3,8},{3,3,8},{-3,3,8},{0,0,0});
    Quad(scene,{-3,-3,12},{3,-3,12},{3,3,12},{-3,3,12},{0,0,0});
    Horizontal(scene,-1,{0,1,0}); // Below-water geometry must never become the primary hit.
    PrimaryMaterialPacket materials;
    materials.triangles.resize(scene.size());
    materials.texels={{.8f,.1f,.05f,1},{.05f,.2f,.9f,1}};
    for(unsigned i=0;i<4;++i) {
        materials.triangles[i].uvAB={0,0,1,0};
        materials.triangles[i].uvCSize={1,1,1,1};
        materials.triangles[i].diffuseOffset={.5f,1,1,i<2 ? 1.f : 2.f};
    }
    CHECK(frame->Prepare_Water_Path(scene,view,1,&materials,true));
    CHECK(frame->Water_Hardware_Hits()!=nullptr);
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"AS builds=")==1);
    auto hits=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
    auto pixels=Read(frame,32);
    CHECK(hits.size()==4096 && pixels.size()==4096);
    if(hits.size()!=4096 || pixels.size()!=4096)return;
    Scene packed; CHECK(packed.PackLinear(scene));
    unsigned reflectedPixels=0,misses=0;
    for(unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x) {
        const size_t at=(y*32+x)*4;
        const bool expected=x>=9 && x<23 && y>=9 && y<23;
        CHECK(Approx(pixels[at+3],expected ? 1.f:0.f));
        if(!expected) { CHECK(hits[at+1]==-1); ++misses; continue; }
        ++reflectedPixels;
        CHECK(hits[at+1]>=0 && hits[at+1]==std::floor(hits[at+1]));
        const unsigned primitive=unsigned(hits[at+1]);
        CHECK(primitive<packed.triangles.size());
        if(primitive>=packed.triangles.size())continue;
        CHECK(packed.SourceTriangleIndex(primitive)<2); // Closest roof, never farther blue roof.
        const Triangle &triangle=packed.triangles[primitive];
        const float u=hits[at+2],v=hits[at+3];
        CHECK(u>=-.00001f && v>=-.00001f && u+v<=1.00001f);
        const Vec3 baryPoint=Add(triangle.a,Add(Scale(Sub(triangle.b,triangle.a),u),Scale(Sub(triangle.c,triangle.a),v)));
        const Vec3 ray=Normalized(Add(view.rayTopLeft,Add(Scale(view.rayDx,(x+.5f)/32),Scale(view.rayDy,(y+.5f)/32))));
        const float crossing=5/ray.z;
        const Vec3 queryOrigin=Add(view.origin,Scale(ray,crossing+.02f));
        const Vec3 rayPoint=Add(queryOrigin,Scale(ray,hits[at]));
        CHECK(Approx(baryPoint.x,rayPoint.x,.0002f) && Approx(baryPoint.y,rayPoint.y,.0002f));
        CHECK(Approx(rayPoint.z,8,.0002f));
        CHECK(Approx(pixels[at],.4f) && Approx(pixels[at+1],.1f) && Approx(pixels[at+2],.05f));
    }
    CHECK(reflectedPixels==196 && misses==828);
    const auto mip=Read(frame,16,frame->Water_Path_Texture(),1);
    CHECK(mip.size()==1024);
    CHECK(mip.size()==1024 && mip[(8*16+8)*4+3]>.99f);
    // Material revision changes hit appearance without replacing the off-camera fixture.
    materials.texels[0]={.1f,.8f,.2f,1};
    CHECK(frame->Prepare_Water_Path(scene,view,2,&materials,true));
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"AS builds=")==1); // Material-only revision must skip native geometry rebuild.
    CHECK(NativeCounter(frame,"unchanged geometry skips=")==1);
    pixels=Read(frame,32);
    const size_t center=(16*32+16)*4;
    CHECK(pixels.size()==4096 && Approx(pixels[center],.05f) &&
        Approx(pixels[center+1],.8f) && Approx(pixels[center+2],.2f));
    // Unchanged revision still changes packing layout on hardware/software reentry.
    CHECK(frame->Prepare_Water_Path(scene,view,2,&materials));
    const auto softwareSameRevision=Read(frame,32);
    CHECK(softwareSameRevision.size()==4096 && Approx(softwareSameRevision[center+3],1));
    CHECK(frame->Prepare_Water_Path(scene,view,2,&materials,true));
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"AS builds=")==1); // Source-order geometry matches the retained native scene.
    CHECK(NativeCounter(frame,"unchanged geometry skips=")==2);
    pixels=Read(frame,32);
    CHECK(pixels.size()==4096 && Approx(pixels[center],.05f) && Approx(pixels[center+1],.8f));
    // Shared CPU packets can advance while hardware mode is inactive. Re-entering
    // hardware at that same revision must rebuild its AS from the new packed epoch.
    for(unsigned i=0;i<2;++i) { scene[i].a.z+=2; scene[i].b.z+=2; scene[i].c.z+=2; }
    CHECK(frame->Prepare_Water_Path(scene,view,3,&materials));
    CHECK(frame->Prepare_Water_Path(scene,view,3,&materials,true));
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"AS builds=")==2); // Changed pose rebuilds the AS using the same pipeline.
    hits=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
    CHECK(hits.size()==4096);
    if(hits.size()==4096) {
        const Vec3 ray=Normalized(Add(view.rayTopLeft,Add(Scale(view.rayDx,16.5f/32),Scale(view.rayDy,16.5f/32))));
        const float worldZ=(.02f+hits[center])*ray.z;
        CHECK(Approx(worldZ,10,.0002f));
    }
    const std::vector<Triangle> degenerate={{{0,0,1},{0,0,1},{0,0,1},{1,0,0}}};
    CHECK(!frame->Prepare_Water_Path(degenerate,view,4,nullptr,true));
    CHECK(frame->Water_Hardware_Hits()==nullptr);
    CHECK(frame->Water_Hardware_Available());
    CHECK(frame->Prepare_Water_Path(scene,view,5,&materials,true));
    view.maxDistance=.1f;
    CHECK(frame->Prepare_Water_Path(scene,view,5,&materials,true));
    hits=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
    pixels=Read(frame,32);
    for(unsigned i=0;i<1024 && hits.size()==4096 && pixels.size()==4096;++i) {
        CHECK(hits[i*4+1]==-1); CHECK(pixels[i*4+3]==0);
    }
    const unsigned buildsBeforeCountChange=NativeCounter(frame,"AS builds=");
    const unsigned allocationsBeforeGrowth=NativeCounter(frame,"geometry allocations=");
    view.maxDistance=1000;
    Quad(scene,{1000,0,20},{1001,0,20},{1001,1,20},{1000,1,20},{.2f,.2f,.2f});
    materials.triangles.resize(scene.size());
    CHECK(frame->Prepare_Water_Path(scene,view,6,&materials,true));
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"AS builds=")==buildsBeforeCountChange+1);
    CHECK(NativeCounter(frame,"geometry allocations=")==allocationsBeforeGrowth+1);
    pixels=Read(frame,32);
    CHECK(pixels.size()==4096 && Approx(pixels[center],.05f) && Approx(pixels[center+1],.8f));

    const unsigned allocations=NativeCounter(frame,"geometry allocations=");
    const unsigned reuses=NativeCounter(frame,"geometry count reuses=");
    const unsigned capacity=NativeCounter(frame,"geometry triangle capacity=");
    const size_t originalCount=scene.size()-2;
    const auto trimRoof = [&] {
        scene.resize(originalCount);
        materials.triangles.resize(originalCount);
    };
    const auto checkCenterHeight = [&](float height) {
        const auto result=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
        CHECK(result.size()==4096);
        if (result.size()==4096) {
            const Vec3 ray=Normalized(Add(view.rayTopLeft,Add(Scale(view.rayDx,16.5f/32),Scale(view.rayDy,16.5f/32))));
            CHECK(result[center+1]>=0 && Approx((.02f+result[center])*ray.z,height,.0002f));
        }
    };
    trimRoof();
    CHECK(frame->Prepare_Water_Path(scene,view,7,&materials,true));
    checkCenterHeight(10);
    Quad(scene,{-3,-3,6},{3,-3,6},{3,3,6},{-3,3,6},{1,0,0});
    materials.triangles.resize(scene.size());
    CHECK(frame->Prepare_Water_Path(scene,view,8,&materials,true));
    checkCenterHeight(6);
    trimRoof();
    CHECK(frame->Prepare_Water_Path(scene,view,9,&materials,true));
    checkCenterHeight(10);
    CHECK(NativeCounter(frame,"geometry allocations=")==allocations);
    CHECK(NativeCounter(frame,"geometry count reuses=")==reuses+3);
    CHECK(NativeCounter(frame,"geometry triangle capacity=")==capacity);
    CHECK(NativeCounter(frame,"geometry active triangles=")==originalCount);

    const unsigned textureAllocations=NativeCounter(frame,"data texture allocations=");
    while (scene.size()*3<=DataTextureWidth) {
        const float x=1000.0f+float(scene.size());
        Quad(scene,{x,0,20},{x+1,0,20},{x+1,1,20},{x,1,20},{0,0,0});
    }
    Quad(scene,{-3,-3,6},{3,-3,6},{3,3,6},{-3,3,6},{0,0,0});
    materials.triangles.resize(scene.size());
    materials.triangles[scene.size()-2]=materials.triangles[2];
    materials.triangles[scene.size()-1]=materials.triangles[3];
    CHECK(frame->Prepare_Water_Path(scene,view,10,&materials,true));
    CHECK(NativeCounter(frame,"data texture allocations=")==textureAllocations+2);
    checkCenterHeight(6);
    pixels=Read(frame,32);
    CHECK(pixels.size()==4096 && Approx(pixels[center],.025f) && Approx(pixels[center+2],.9f));
    const unsigned triangleRows=NativeCounter(frame,"triangle texture rows=");
    const unsigned materialRows=NativeCounter(frame,"material texture rows=");
    CHECK(triangleRows>1 && materialRows>1);

    trimRoof();
    CHECK(frame->Prepare_Water_Path(scene,view,11,&materials,true));
    CHECK(NativeCounter(frame,"data texture allocations=")==textureAllocations+2);
    CHECK(NativeCounter(frame,"triangle texture rows=")==triangleRows);
    CHECK(NativeCounter(frame,"material texture rows=")==materialRows);
    checkCenterHeight(10);
    pixels=Read(frame,32);
    CHECK(pixels.size()==4096 && Approx(pixels[center],.05f) && Approx(pixels[center+1],.8f));

    // A validated atlas still requires valid bindings for every active triangle.
    materials.triangles[0].diffuseOffset.w=float(materials.texels.size()+1);
    CHECK(!frame->Prepare_Water_Path(scene,view,12,&materials,true));
    CHECK(Contains(frame->Water_Path_Report(),"invalid primary materials"));
    CHECK(frame->Water_Path_Texture()==nullptr);
    materials.triangles[0].diffuseOffset.w=1;
    // The existing material refusal also remains latched until a real toggle.
    CheckHardwareRetry(frame,scene,view,&materials,false);
    std::printf("hardware_water: main_frustum_rejected=%u reflected_pixels=%u misses=%u closest_id_barycentrics=checked material_revision=checked empty_recovery=checked mode_reentry=checked native_pipeline_reuse=checked unchanged_geometry_skip=checked same_revision_layout_reentry=checked changed_count=checked mip=checked short_ray=checked\n",
        mainRejected,reflectedPixels,misses);
    frame->Clear_Water_Path();
}

void CheckHardwarePath(SdlGpuFrame *frame)
{
    View view{}; view.size=view.motionSize=32; view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1}; view.rayDx={.2f,0,0}; view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={0,0,0}; view.sunDirection=Normalized({1,0,.5f});
    view.sunRadiance={3.14159265f,3.14159265f,3.14159265f};
    std::vector<Triangle> scene;
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{.8f,.8f,.8f});
    Quad(scene,{-4,-100,-100},{-4,100,-100},{-4,100,100},{-4,-100,100},{.8f,.02f,.02f});
    for(unsigned sample=0;sample<64;++sample) CHECK(frame->Prepare_Water_Path(scene,view));
    const auto software=Read(frame,32);
    frame->Clear_Water_Path();
    const std::vector<Triangle> empty;
    CHECK(!frame->Prepare_Water_Path(empty,view,99,nullptr,true,true));
    CHECK(frame->Water_Hardware_Available(true));
    for(unsigned sample=0;sample<64;++sample) {
        CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
        if(sample<2 || sample==63) {
            const std::string expected=std::to_string(sample+1)+"/64 samples";
            CHECK(Contains(frame->Water_Path_Report(),expected.c_str()));
        }
    }
    const auto hardware=Read(frame,32);
    CHECK(software.size()==4096 && hardware.size()==4096);
    double meanError=0,maxError=0,red=0,green=0;
    if(software.size()==hardware.size() && hardware.size()==4096) {
        for(unsigned i=0;i<hardware.size();++i) {
            const double error=std::abs(software[i]-hardware[i]); meanError+=error; maxError=std::max(maxError,error);
            CHECK(std::isfinite(hardware[i]));
            if(i%4==0)red+=hardware[i]; if(i%4==1)green+=hardware[i];
        }
        meanError/=hardware.size();
        CHECK(meanError<.003 && maxError<.04); CHECK(red>10 && red>green*2);
    }
    const unsigned fenceWaitsAtConvergence=NativeCounter(frame,"profiled fence waits=");
    const unsigned noWorkAtConvergence=NativeCounter(frame,"profiled no-work returns=");
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    CHECK(Contains(frame->Water_Path_Report(),"64/64 samples"));
    CHECK(NativeCounter(frame,"profiled fence waits=")==fenceWaitsAtConvergence);
    CHECK(NativeCounter(frame,"profiled no-work returns=")==noWorkAtConvergence+1);
    auto primary=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
    auto transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
    CHECK(primary.size()==4096 && transport.size()==4096);
    Scene packed; CHECK(packed.PackLinear(scene));
    unsigned secondaryHits=0,secondaryMisses=0;
    if(primary.size()==4096 && transport.size()==4096) for(unsigned i=0;i<1024;++i) {
        const size_t at=i*4;
        CHECK(primary[at+1]>=0 && unsigned(primary[at+1])<packed.triangles.size());
        if(primary[at+1]>=0 && unsigned(primary[at+1])<packed.triangles.size())
            CHECK(packed.SourceTriangleIndex(unsigned(primary[at+1]))<2);
        CHECK(transport[at+1]>=-1.00001f && transport[at+1]<=1.00001f);
        CHECK(transport[at+2]==1 || transport[at+2]==-1);
        CHECK(transport[at+3]==0); // Primary faces away from the sun.
        if(transport[at]>=0) {
            ++secondaryHits;
            CHECK(transport[at]==std::floor(transport[at]) && unsigned(transport[at])<packed.triangles.size());
            if(unsigned(transport[at])<packed.triangles.size()) CHECK(packed.SourceTriangleIndex(unsigned(transport[at]))>=2);
        } else { ++secondaryMisses; CHECK(transport[at]==-1); }
    }
    CHECK(secondaryHits>0);
    view.secondaryDistance=.001f; // tMax < epsilon: continuation is an unobstructed sky sample.
    view.skyLow=view.skyHigh={.2f,.3f,.4f};
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    auto pixels=Read(frame,32);
    transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
    bool sky=true;
    for(unsigned i=0;i<1024 && pixels.size()==4096 && transport.size()==4096;++i)
        sky &= transport[i*4]==-1 && Approx(pixels[i*4],.16f) && Approx(pixels[i*4+1],.24f) && Approx(pixels[i*4+2],.32f);
    CHECK(sky);
    // A roof beyond the first wall blocks only its sun ray, not its primary silhouette.
    view.skyLow=view.skyHigh={0,0,0}; view.sunDirection=Normalized({-1,0,.5f});
    scene.resize(2);
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    const auto sunny=Read(frame,32);
    Quad(scene,{-100,-100,6},{100,-100,6},{100,100,6},{-100,100,6},{.1f,.1f,.1f});
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    pixels=Read(frame,32); transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
    bool blocked=true;
    for(unsigned i=0;i<1024 && pixels.size()==4096 && transport.size()==4096;++i)
        blocked &= transport[i*4+3]==0 && pixels[i*4]==0 && pixels[i*4+1]==0 && pixels[i*4+2]==0;
    CHECK(blocked);
    view.shadowDistance=.001f;
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    pixels=Read(frame,32); transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
    bool shortSun=true;
    for(unsigned i=0;i<1024 && pixels.size()==4096 && transport.size()==4096 && sunny.size()==4096;++i)
        shortSun &= transport[i*4+3]==1 && Approx(pixels[i*4],sunny[i*4]) && pixels[i*4]>.6f;
    CHECK(shortSun);
    view.sunDirection=Normalized({1,0,-.5f});
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    pixels=Read(frame,32); transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
    bool reverseSun=true;
    for(unsigned i=0;i<1024 && pixels.size()==4096 && transport.size()==4096;++i)
        reverseSun &= transport[i*4+3]==0 && pixels[i*4]==0;
    CHECK(reverseSun);
    view.maxDistance=.001f;
    CHECK(frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
    primary=Read(frame,32,frame->Water_Hardware_Hits(),0,true); pixels=Read(frame,32);
    bool shortPrimary=true;
    for(unsigned i=0;i<1024 && primary.size()==4096 && pixels.size()==4096;++i)
        shortPrimary &= primary[i*4+1]==-1 && pixels[i*4+3]==0;
    CHECK(shortPrimary);
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    CHECK(NativeCounter(frame,"native updates=")>=2);
    std::printf("hardware_water_path: samples=64 parity_mean=%.7f parity_max=%.7f secondary_hits=%u secondary_misses=%u sky_range=%u sun_block=%u sun_range=%u sun_reverse=%u primary_range=%u\n",
        meanError,maxError,secondaryHits,secondaryMisses,sky,blocked,shortSun,reverseSun,shortPrimary);
    CheckHardwareRetry(frame,scene,view,nullptr,true);
    frame->Clear_Water_Path();
}

void CheckHardwareSplitPath(SdlGpuFrame *frame)
{
    View view{}; view.size=view.motionSize=32; view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1}; view.rayDx={.2f,0,0}; view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={0,0,0}; view.sunDirection=Normalized({1,0,.5f});
    view.sunRadiance={3.14159265f,3.14159265f,3.14159265f};
    std::vector<Triangle> scene(1); // A removed degenerate must not inflate the dynamic ID base.
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{.8f,.8f,.8f});
    Quad(scene,{-4,-100,-100},{-4,100,-100},{-4,100,100},{-4,-100,100},{.8f,.02f,.02f});
    PrimaryMaterialPacket materials;
    const auto setMaterials = [&]() {
        materials.triangles.assign(scene.size(),{});
        materials.texels={{1,1,1,1}};
        for (auto &material : materials.triangles) {
            material.uvCSize={0,0,1,1}; material.diffuseOffset={.8f,.8f,.8f,1};
        }
    };
    setMaterials();
    uint64_t revision=1;
    const auto prepare = [&](size_t staticCount) {
        CHECK(frame->Prepare_Water_Path(scene,view,revision++,&materials,true,true,staticCount));
        CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    };
    const auto checkIds = [&](unsigned firstPrimary,unsigned primaryCount,unsigned firstSecondary,unsigned secondaryCount) {
        const auto primary=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
        const auto transport=Read(frame,32,frame->Water_Hardware_Transport(),0,true);
        CHECK(primary.size()==4096 && transport.size()==4096);
        unsigned secondaryHits=0;
        for (unsigned i=0;i<1024 && primary.size()==4096 && transport.size()==4096;++i) {
            const float id=primary[i*4+1],secondary=transport[i*4];
            CHECK(id==std::floor(id) && id>=float(firstPrimary) && id<float(firstPrimary+primaryCount));
            if (secondary>=0) {
                ++secondaryHits;
                CHECK(secondary==std::floor(secondary) && secondary>=float(firstSecondary)
                    && secondary<float(firstSecondary+secondaryCount));
            } else CHECK(secondary==-1);
        }
        if (secondaryCount) CHECK(secondaryHits>0);
        else CHECK(secondaryHits==0);
    };
    prepare(3); checkIds(0,2,2,2);
    CHECK(NativeCounter(frame,"static AS builds=")==1);
    CHECK(NativeCounter(frame,"dynamic AS builds=")==1);
    auto pixels=Read(frame,32);
    double red=0,green=0;
    for (unsigned i=0;i<1024 && pixels.size()==4096;++i) { red+=pixels[i*4]; green+=pixels[i*4+1]; }
    CHECK(red>1 && red>green*2); // Continuation must shade the red dynamic material.

    // Changing both primary attributes and secondary albedo resets shading without touching either AS.
    for (unsigned i=3;i<scene.size();++i) scene[i].albedo={.02f,.8f,.02f};
    materials.texels[0]={.5f,1,1,1};
    prepare(3); checkIds(0,2,2,2);
    CHECK(NativeCounter(frame,"static AS builds=")==1);
    CHECK(NativeCounter(frame,"dynamic AS builds=")==1);
    pixels=Read(frame,32); red=green=0;
    for (unsigned i=0;i<1024 && pixels.size()==4096;++i) { red+=pixels[i*4]; green+=pixels[i*4+1]; }
    CHECK(green>1 && green>red*2);
    for (unsigned i=3;i<scene.size();++i) { scene[i].a.x-=.25f; scene[i].b.x-=.25f; scene[i].c.x-=.25f; }
    prepare(3); checkIds(0,2,2,2);
    CHECK(NativeCounter(frame,"static AS builds=")==1);
    CHECK(NativeCounter(frame,"dynamic AS builds=")==2);
    CHECK(NativeCounter(frame,"static AS reuses=")==2);
    CHECK(NativeCounter(frame,"static native pack skips=")>=2);
    CHECK(NativeCounter(frame,"material records reused=")>=6);
    CHECK(NativeCounter(frame,"static attribute records retained=")>=4);
    // Dynamic count growth: four triangles reserve five primitive slots; the
    // fifth must reuse that allocation. At x=-1, these rays have z=.3556..2.2317,
    // so the temporary occluder extends to z=0 to cover every primary ray. A wall
    // starting at z=1 correctly leaves 416/1024 pixels hitting the static wall.
    // Shrinking must remove this near wall even while its bytes remain reserved.
    const auto checkCountRange=[&](const char *stage,unsigned first,unsigned count) {
        const auto hits=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
        unsigned histogram[8]={},misses=0,unexpected=0;
        bool correct=hits.size()==4096;
        float nearest=INFINITY,farthest=0;
        for(unsigned i=0;i<1024 && hits.size()==4096;++i) {
            const float id=hits[i*4+1];
            if(!std::isfinite(id) || id!=std::floor(id) || id<0) ++misses;
            else if(id<8) ++histogram[unsigned(id)];
            else ++unexpected;
            correct &= id>=first && id<first+count;
            nearest=std::min(nearest,hits[i*4]); farthest=std::max(farthest,hits[i*4]);
        }
        std::printf("dynamic count %s: expected IDs=%u..%u hits[0..7]=%u,%u,%u,%u,%u,%u,%u,%u miss=%u other=%u distance=%.6f..%.6f\n",
            stage,first,first+count-1,histogram[0],histogram[1],histogram[2],histogram[3],
            histogram[4],histogram[5],histogram[6],histogram[7],misses,unexpected,nearest,farthest);
        CHECK(correct && !misses && !unexpected);
    };
    Quad(scene,{-1,-10,0},{-1,10,0},{-1,10,5},{-1,-10,5},{.1f,.2f,.8f});
    setMaterials(); prepare(3); checkCountRange("grow4",4,2);
    scene.push_back({{100000,0,100000},{100001,0,100000},{100000,1,100000},{.1f,.1f,.1f}});
    setMaterials(); prepare(3); checkCountRange("grow5-in-capacity",4,2);
    scene.resize(5); setMaterials(); prepare(3); checkCountRange("shrink2-no-stale-tail",0,2);
    checkIds(0,2,2,2);
    CHECK(NativeCounter(frame,"static AS builds=")==1);

    scene.resize(3); materials.triangles.resize(3);
    prepare(3); checkIds(0,2,0,0); // Explicit empty dynamic mesh removes its old occluder.
    CHECK(NativeCounter(frame,"static AS builds=")==1);

    // Reverse the partitions so the primary material also needs a nonzero instance base.
    scene.resize(1);
    Quad(scene,{-4,-100,-100},{-4,100,-100},{-4,100,100},{-4,-100,100},{.8f,.02f,.02f});
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{0,0,0});
    setMaterials(); prepare(3); checkIds(2,2,0,2);
    pixels=Read(frame,32); red=green=0;
    for (unsigned i=0;i<1024 && pixels.size()==4096;++i) { red+=pixels[i*4]; green+=pixels[i*4+1]; }
    CHECK(red>1 && red>green*2); // Primary gets its textured material despite zero fallback albedo.
    const unsigned dynamicBuilds=NativeCounter(frame,"dynamic AS builds=");
    scene.erase(scene.begin()+1); materials.triangles.erase(materials.triangles.begin()+1);
    prepare(2); checkIds(1,2,0,1); // Static count changes the TLAS base, preserving the dynamic BLAS.
    CHECK(NativeCounter(frame,"dynamic AS builds=")==dynamicBuilds);
    scene.erase(scene.begin(),scene.begin()+2); materials.triangles.erase(materials.triangles.begin(),materials.triangles.begin()+2);
    prepare(0); checkIds(0,2,0,0);
    CHECK(NativeCounter(frame,"dynamic AS builds=")==dynamicBuilds);
    // Reclassifying identical source geometry is observable even when its revision is unchanged.
    const unsigned staticBuilds=NativeCounter(frame,"static AS builds=");
    CHECK(frame->Prepare_Water_Path(scene,view,revision-1,&materials,true,true,1));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    checkIds(0,2,0,0);
    CHECK(NativeCounter(frame,"static AS builds=")==staticBuilds+1);
    CHECK(NativeCounter(frame,"native pipeline creates=")==1);
    frame->Clear_Water_Path();
}

#if defined(__linux__)
void CheckHardwareDynamicStorage(SdlGpuFrame *frame)
{
    std::vector<unsigned char> spirv; std::string error;
    CHECK(SDL3_Compile_Ray_Query_GLSL(HardwarePathQuerySource(),spirv,error));
    if(spirv.empty()) return;
    const float staticXYZ[]={0,0,0,1,0,0,0,1,0}; const uint32_t staticIndices[]={0,1,2};
    ZHR_SDLRayMeshView staticMesh={staticXYZ,3,staticIndices,3};
    std::vector<float> xyz; std::vector<uint32_t> indices;
    const auto mesh=[&](unsigned triangles) {
        xyz.resize(size_t(triangles)*9); indices.resize(size_t(triangles)*3);
        for(unsigned t=0;t<triangles;++t) {
            const float vertices[]={float(t*2),0,1,float(t*2+1),0,1,float(t*2),1,1};
            std::copy(vertices,vertices+9,xyz.begin()+t*9);
            for(unsigned k=0;k<3;++k) indices[t*3+k]=t*3+k;
        }
        return ZHR_SDLRayMeshView{xyz.data(),triangles*3,indices.data(),triangles*3};
    };
    auto dynamicMesh=mesh(8);
    ZHR_SDLRayScene *scene=ZHR_SDLRayCreateWaterPathSplitScene(frame->Device(),&staticMesh,&dynamicMesh,spirv.data(),spirv.size());
    CHECK(scene); if(!scene) return;
    ZHR_SDLRayWaterStorageStats stats={};
    CHECK(ZHR_SDLRayGetWaterStorageStats(scene,&stats));
    CHECK(stats.dynamicAllocations==1 && stats.activeVertices==24 && stats.activePrimitives==8);
    CHECK(stats.vertexCapacity==30 && stats.primitiveCapacity==10);
    const auto update=[&](unsigned count,Uint64 allocations) {
        dynamicMesh=mesh(count); bool staticChanged=true,dynamicChanged=false;
        CHECK(ZHR_SDLRayUpdateWaterPathSplitScene(scene,nullptr,&dynamicMesh,&staticChanged,&dynamicChanged));
        CHECK(!staticChanged && dynamicChanged);
        CHECK(ZHR_SDLRayGetWaterStorageStats(scene,&stats));
        CHECK(stats.activeVertices==count*3 && stats.activePrimitives==count);
        CHECK(stats.dynamicAllocations==allocations);
    };
    update(10,1); update(2,1); update(9,1);
    CHECK(stats.dynamicCountReuses==3 && stats.dynamicRebuildReuses==3);
    update(11,2); CHECK(stats.vertexCapacity==41 && stats.primitiveCapacity==13);
    // Validation failure must leave active counts, capacity and allocation epoch unchanged.
    const auto before=stats; indices[0]=dynamicMesh.vertexCount;
    CHECK(!ZHR_SDLRayUpdateWaterPathSplitScene(scene,nullptr,&dynamicMesh,nullptr,nullptr));
    CHECK(ZHR_SDLRayGetWaterStorageStats(scene,&stats));
    CHECK(std::memcmp(&before,&stats,sizeof(stats))==0);
    std::printf("hardware dynamic storage: allocations=%llu count-reuses=%llu active=%u/%u capacity=%u/%u\n",
        (unsigned long long)stats.dynamicAllocations,(unsigned long long)stats.dynamicCountReuses,
        stats.activeVertices,stats.activePrimitives,stats.vertexCapacity,stats.primitiveCapacity);
    ZHR_SDLRayDestroyScene(scene);
}

#endif

void CheckHardwareDirtyRows(SdlGpuFrame *frame)
{
    View view{}; view.size=view.motionSize=32; view.origin={-5,0,-5};
    view.rayTopLeft={.55f,-.2f,1}; view.rayDx={.2f,0,0}; view.rayDy={0,.4f,0};
    view.skyLow=view.skyHigh={1,1,1}; view.sunRadiance={0,0,0}; view.secondaryDistance=.001f;
    std::vector<Triangle> scene(1);
    Quad(scene,{0,-10,1},{0,10,1},{0,10,5},{0,-10,5},{0,0,0});
    const Triangle lastPrimary=scene.back(); scene.pop_back();
    const auto filler = [](unsigned i) {
        const float x=1000.f+float(i)*2;
        return Triangle{{x,0,1000},{x+1,0,1000},{x,1,1000},{.3f,.4f,.5f}};
    };
    while (scene.size()<701) scene.push_back(filler(unsigned(scene.size())));
    scene.push_back(lastPrimary);
    const size_t staticCount=scene.size(); // 701 packed triangles: both data layouts end mid-row.
    Quad(scene,{-4,-100,-100},{-4,100,-100},{-4,100,100},{-4,-100,100},{.8f,.02f,.02f});
    const Triangle dynamicA=scene[staticCount],dynamicB=scene[staticCount+1];
    PrimaryMaterialPacket materials;
    materials.texels={{1,1,1,1}};
    materials.triangles.resize(scene.size());
    for (unsigned i : {1u,701u}) {
        materials.triangles[i].uvCSize={0,0,1,1};
        materials.triangles[i].diffuseOffset={.4f,.7f,.9f,1};
    }
    uint64_t revision=1;
    Scene incremental;
    const auto prepare = [&]() {
        Scene fresh;
        CHECK(incremental.PackLinear(scene,false,unsigned(staticCount)));
        CHECK(fresh.PackLinear(scene,false));
        CHECK(incremental.triangleTexels.size()==fresh.triangleTexels.size());
        if (incremental.triangleTexels.size()==fresh.triangleTexels.size())
            CHECK(std::memcmp(incremental.triangleTexels.data(),fresh.triangleTexels.data(),fresh.triangleTexels.size()*sizeof(Float4))==0);
        for (unsigned i=0;i<fresh.triangles.size();++i)
            CHECK(incremental.SourceTriangleIndex(i)==fresh.SourceTriangleIndex(i));
        CHECK(frame->Prepare_Water_Path(scene,view,revision++,&materials,true,true,staticCount));
    };
    const auto checkPixels = [&](Vec3 lastColour, Vec3 firstColour=Vec3{.4f,.7f,.9f}) {
        const auto hits=Read(frame,32,frame->Water_Hardware_Hits(),0,true);
        const auto pixels=Read(frame,32);
        CHECK(hits.size()==4096 && pixels.size()==4096);
        unsigned firstHits=0,lastHits=0;
        for (unsigned i=0;i<1024 && hits.size()==4096 && pixels.size()==4096;++i) {
            const float id=hits[i*4+1];
            CHECK(id==0 || id==700);
            if (id==0) ++firstHits; if (id==700) ++lastHits;
            const Vec3 colour=id==700 ? lastColour : firstColour;
            CHECK(Approx(pixels[i*4],colour.x) && Approx(pixels[i*4+1],colour.y)
                && Approx(pixels[i*4+2],colour.z) && pixels[i*4+3]==1);
        }
        CHECK(firstHits>0 && lastHits>0); // Visible static records occupy an untouched row and the shared boundary row.
    };
    prepare(); checkPixels({.4f,.7f,.9f});
    unsigned allocations=NativeCounter(frame,"data texture allocations=");
    const unsigned uploadRows=NativeCounter(frame,"texture upload rows=");
    for (size_t i=staticCount;i<scene.size();++i) { scene[i].a.x-=.25f; scene[i].b.x-=.25f; scene[i].c.x-=.25f; }
    prepare(); checkPixels({.4f,.7f,.9f});
    CHECK(NativeCounter(frame,"texture upload rows=")==uploadRows+1);
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations);
    CHECK(NativeCounter(frame,"unchanged texture skips=")>=1);
    const auto resizeDynamic = [&](unsigned count) {
        scene.resize(staticCount); scene.push_back(dynamicA); scene.push_back(dynamicB);
        while (scene.size()<staticCount+count) scene.push_back(filler(unsigned(scene.size())));
        materials.triangles.resize(scene.size());
        prepare(); checkPixels({.4f,.7f,.9f});
    };
    resizeDynamic(100); resizeDynamic(2);
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations);
    resizeDynamic(600); // Triangle texture grows from two rows to four; attributes still fit two.
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations+1);
    allocations=NativeCounter(frame,"data texture allocations=");
    resizeDynamic(2); resizeDynamic(650);
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations);
    resizeDynamic(900); // Attribute texture now also grows to four rows.
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations+1);
    allocations=NativeCounter(frame,"data texture allocations=");
    resizeDynamic(2);
    CHECK(NativeCounter(frame,"data texture allocations=")==allocations);
    const unsigned staticBuilds=NativeCounter(frame,"static AS builds=");
    scene[701].albedo={1,0,0}; // Exact static-prefix retention must include shading data.
    materials.triangles[701].diffuseOffset={.8f,.2f,.1f,1};
    prepare(); checkPixels({.8f,.2f,.1f});
    materials.triangles[701].diffuseOffset.w=0;
    prepare(); checkPixels({1,0,0});
    CHECK(NativeCounter(frame,"static AS builds=")==staticBuilds);
    // Both triangle and material rows are recorded, then the whole command
    // buffer is cancelled. Their CPU snapshots must not suppress the retry.
    scene[701].albedo={0,1,0};
    materials.triangles[1].diffuseOffset={.2f,.3f,.4f,1};
    frame->Fail_Next_Water_Path_Upload_For_Test(2);
    const uint64_t failedRevision=revision++;
    CHECK(!frame->Prepare_Water_Path(scene,view,failedRevision,&materials,true,true,staticCount));
    CHECK(Contains(frame->Water_Path_Report(),"injected water path upload refusal"));
    CHECK(!frame->Prepare_Water_Path(scene,view,failedRevision,&materials,true,true,staticCount));
    const bool requested=HardwareRayTracing::Requested();
    HardwareRayTracing::SetRequested(!requested);
    HardwareRayTracing::SetRequested(requested);
    CHECK(frame->Prepare_Water_Path(scene,view,failedRevision,&materials,true,true,staticCount));
    checkPixels({0,1,0},{.2f,.3f,.4f});
    const auto oldTexels=incremental.triangleTexels;
    scene[1].a.x=NAN;
    CHECK(!incremental.PackLinear(scene,false,unsigned(staticCount)));
    CHECK(std::memcmp(oldTexels.data(),incremental.triangleTexels.data(),oldTexels.size()*sizeof(Float4))==0);
    frame->Clear_Water_Path();
}

void CheckHardwareRefusalAccumulation(SdlGpuFrame *frame)
{
    if (frame->Water_Hardware_Available()) return;
    frame->Clear_Water_Path();
    View view{}; view.size=view.motionSize=32; view.origin={0,0,-5};
    view.rayTopLeft={-.5f,.5f,1}; view.rayDx={1,0,0}; view.rayDy={0,-1,0};
    view.sunRadiance={0,0,0}; view.skyLow=view.skyHigh={1,1,1};
    std::vector<Triangle> scene; Horizontal(scene,2,{.4f,.2f,.1f});
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    for(unsigned sample=2;sample<=3;++sample) {
        CHECK(!frame->Prepare_Water_Path(scene,view,0,nullptr,true));
        CHECK(!frame->Prepare_Water_Path(scene,view,0,nullptr,true,true));
        CHECK(frame->Water_Path_Texture()!=nullptr); // Refusal preserves the published software capture.
        CHECK(frame->Prepare_Water_Path(scene,view));
        const std::string expected=std::to_string(sample)+"/64 samples";
        CHECK(Contains(frame->Water_Path_Report(),expected.c_str()));
    }
    std::printf("water_path_gpu: hardware refusal preserves software accumulation through 3/64 samples\n");
    frame->Clear_Water_Path();
}

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

int main(int argc,char **argv)
{
    const bool hardwarePath=argc>1 && std::strcmp(argv[1],"--hardware-path")==0;
    const bool hardware=hardwarePath || (argc>1 && std::strcmp(argv[1],"--hardware")==0);
    if(hardware) {
        SDL_setenv_unsafe("ZH_GPU_RAY_QUERY","1",1);
        SDL_setenv_unsafe("ZH_HARDWARE_WATER_PROFILE","1",0);
    }
    if(!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("water_path_gpu: SKIP - SDL_Init: %s\n",SDL_GetError());return 77;
    }
    std::string error;
    SdlGpuFrame *frame=SdlGpuFrame::Create(nullptr,32,32,error);
    if(!frame) {
        std::printf("water_path_gpu: SKIP - no GPU: %s\n",error.c_str());SDL_Quit();return 77;
    }
    if(hardware) {
#if defined(__linux__)
        char diagnostic[1024] = {};
        if(!ZHR_SDLRayQuerySupported(frame->Device(),diagnostic,sizeof(diagnostic))) {
            std::printf("hardware_water: SKIP - %s\n",diagnostic);
            delete frame; SDL_Quit(); return 77;
        }
        std::printf("hardware_water: %s\n",diagnostic);
        if(hardwarePath) { CheckHardwarePath(frame); CheckHardwareSplitPath(frame); CheckHardwareDynamicStorage(frame); CheckHardwareDirtyRows(frame); }
        else CheckHardwareReflection(frame);
        delete frame; SDL_Quit();
        std::printf("hardware_water: %u checks, %u failed\n",checks,failures);
        return failures?1:0;
#else
        delete frame; SDL_Quit(); return 77;
#endif
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
    CheckHardwareRefusalAccumulation(frame);
    delete frame;SDL_Quit();
    std::printf("water_path_gpu: %u checks, %u failed\n",checks,failures);
    return failures?1:0;
}
