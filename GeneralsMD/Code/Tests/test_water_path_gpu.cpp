// The optional water tracer on the actual SDL GPU backend. No game or assets required.
#include "SdlGpuFrame.h"
#include "Platform/WaterPathTracing.h"
#include "Platform/WaterPathTracingMotion.h"
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

std::vector<float> Read(SdlGpuFrame *frame,unsigned size)
{
    std::vector<float> result;
    CHECK(frame->Flush());
    if(!frame->Water_Path_Texture())return result;
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
            source.texture=frame->Water_Path_Texture();source.w=size;source.h=size;source.d=1;
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
float Display(float linear) { return std::pow(linear,1.f/2.2f); }

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
        CHECK(Approx(pixels[i],Display(.1f)));
        CHECK(Approx(pixels[i+1],Display(.2f)));
        CHECK(Approx(pixels[i+2],Display(.3f)));
        CHECK(Approx(pixels[i+3],1));
    }

    // Geometry below the water plane lies between the virtual camera and the water.
    // It must not replace the sky with an opaque black first hit.
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
        CHECK(Approx(pixels[i],Display(.7f)));
        CHECK(Approx(pixels[i+1],Display(.2f)));
        CHECK(Approx(pixels[i+2],Display(.05f)));
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
    if(!changed.empty())CHECK(Approx(changed[2],Display(.8f)));

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
    for(unsigned i=0;i<moved.size();i+=4)CHECK(Approx(moved[i+2],Display(.9f)));
    std::reverse(scene.begin(),scene.end());
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"3 BVH builds, 3 refits"));
    CHECK(Read(frame,view.size)==moved);

    view.rayTopLeft.z=-1; // Rays going farther below the plane have no valid reflection.
    CHECK(frame->Prepare_Water_Path(scene,view));
    pixels=Read(frame,view.size);
    for(float value:pixels)CHECK(Approx(value,0));
    frame->Clear_Water_Path();
    CHECK(frame->Water_Path_Texture()==nullptr);
    CHECK(Contains(frame->Water_Path_Report(),"inactive"));
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
            correct&=Approx(pixels[i],Display(linear.x))&&Approx(pixels[i+1],Display(linear.y))
                &&Approx(pixels[i+2],Display(linear.z))&&Approx(pixels[i+3],1);
        }
        CHECK(finite&&correct);
    };

    // The initial still capture starts at requested quality.
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.7f,.2f,.05f});

    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"128x128, 1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"1 BVH builds, 0 refits"));
    checkImage(128,{.7f,.2f,.05f});
    for(unsigned unchanged=1;unchanged<8;++unchanged) {
        CHECK(frame->Prepare_Water_Path(scene,view));
        const std::string state="128x128, "+std::to_string(unchanged+1)+"/64 samples";
        CHECK(Contains(frame->Water_Path_Report(),state.c_str()));
    }
    // Recovery also needs a quiet interval longer than the game's held scene
    // snapshot; deterministic fast-render coverage lives in the CPU policy tests.
    SDL_Delay(WaterPathTracing::MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.7f,.2f,.05f});
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 2/64 samples"));

    // Geometry/material movement has the same bounded reduction and clean history reset.
    scene[0].albedo=scene[1].albedo={.05f,.3f,.8f};
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"128x128, 1/64 samples"));
    CHECK(Contains(frame->Water_Path_Report(),"1 BVH builds, 1 refits"));
    checkImage(128,{.05f,.3f,.8f});
    for(unsigned unchanged=1;unchanged<8;++unchanged)
        CHECK(frame->Prepare_Water_Path(scene,view));
    SDL_Delay(WaterPathTracing::MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 1/64 samples"));
    checkImage(256,{.05f,.3f,.8f});
    CHECK(frame->Prepare_Water_Path(scene,view));
    CHECK(Contains(frame->Water_Path_Report(),"256x256, 2/64 samples"));

    // A very fast loop can converge the reduced capture before the quiet timer
    // expires. The sample cap must still permit a later resolution restoration.
    view.origin.x+=1;
    CHECK(frame->Prepare_Water_Path(scene,view));
    for(unsigned sample=1;sample<64;++sample)
        CHECK(frame->Prepare_Water_Path(scene,view));
    SDL_Delay(WaterPathTracing::MotionRecovery::QuietMs);
    CHECK(frame->Prepare_Water_Path(scene,view));
    checkImage(256,{.05f,.3f,.8f});
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
    const auto checkColour=[&](Vec3 expected) {
        const auto pixels=Read(frame,view.size);
        CHECK(pixels.size()==view.size*view.size*4);
        bool correct=true;
        for(unsigned i=0;i<pixels.size();i+=4)
            correct&=Approx(pixels[i],Display(expected.x))&&Approx(pixels[i+1],Display(expected.y))
                &&Approx(pixels[i+2],Display(expected.z))&&Approx(pixels[i+3],1);
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
    checkColour(view.skyLow); // The explicit primary distance cutoff reaches only sky.
    frame->Clear_Water_Path();
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
    CheckIndirectBounce(frame);
    CheckMotionResolution(frame);
    CheckShadowAndPrimaryLimits(frame);
    delete frame;SDL_Quit();
    std::printf("water_path_gpu: %u checks, %u failed\n",checks,failures);
    return failures?1:0;
}
