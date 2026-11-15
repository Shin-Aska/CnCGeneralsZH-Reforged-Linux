// Owner-keyed global providers must not be released by another/retired frame.
// --gpu exercises actual partial preparation, queued readers and frame teardown.
#include "SdlGpuParticles.h"
#include "SdlReachOverlay.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
static unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(0)
static bool refuseReaders=false;
static bool flush(SdlGpuFrame *owner){return !refuseReaders && owner->Flush();}
static bool mirror(IDirect3DBaseTexture9 *,SdlGpuParticleTextureView &view,std::string &){view={};return true;}
static SdlGpuParticlesContext particles(SdlGpuFrame &frame) {
    SdlGpuParticlesContext context;context.Frame=&frame;context.DeviceGeneration=frame.Scene_Generation();
    context.FrameSerial=frame.Scene_Frame();context.MirrorTexture=mirror;return context;
}
template<unsigned N> static bool spare(SdlGpuFrame *){return true;}
static SdlGpuFrame::OwnerReleaseHook spareHooks[]={spare<0>,spare<1>,spare<2>,spare<3>,spare<4>,spare<5>,spare<6>,spare<7>};
struct CaptureOwner {SdlGpuFrame *Frame=nullptr;SDL_GPUTexture *Texture=nullptr;};
static CaptureOwner captures[2];
static bool release_capture(SdlGpuFrame *owner) {
    for(auto &held:captures)if(held.Frame==owner){if(held.Texture)SDL_ReleaseGPUTexture(owner->Device(),held.Texture);held={};}
    return true;
}
static SDL_GPUTexture *capture(SdlGpuFrame &owner) {
    auto target=owner.Back_Buffer_Target();owner.Set_Target(target);owner.Clear_Back_Buffer(true,true,false,0x5f203040,.5f,0);
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=target.Width;info.height=target.Height;info.layer_count_or_depth=info.num_levels=1;
    auto *depth=SDL_CreateGPUTexture(owner.Device(),&info);CHECK(depth);if(depth)CHECK(owner.Capture_Scene_Depth(target,depth));return depth;
}
static SDL_GPUTexture *refuse_texture(SDL_GPUDevice *,const SDL_GPUTextureCreateInfo *) {SDL_SetError("owner fixture partial reach refusal");return nullptr;}
static bool reach(SdlGpuFrame &owner,SDL_GPUTexture *depth,bool refuseTexture=false) {
    SdlReachOverlay::Inputs in;in.Eligible=true;in.Target=owner.Back_Buffer_Target();in.DeviceGeneration=owner.Scene_Generation();in.FrameSerial=owner.Scene_Frame();in.ViewKey=91;
    in.ViewProjection[0]=in.ViewProjection[5]=in.ViewProjection[10]=in.ViewProjection[15]=1;memcpy(in.WorldFromClip,in.ViewProjection,sizeof(in.WorldFromClip));
    in.Viewport[2]=in.Target.Width;in.Viewport[3]=in.Target.Height;in.Viewport[5]=1;in.Scissor[2]=in.Target.Width;in.Scissor[3]=in.Target.Height;
    in.TerrainDepth=depth;in.DepthWidth=in.Target.Width;in.DepthHeight=in.Target.Height;in.DepthFormat=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    in.DepthCapture.Source=in.Target;in.DepthCapture.Frame=in.FrameSerial;in.DepthCapture.Generation=owner.Scene_Generation();in.DepthCapture.ViewKey=in.ViewKey;in.DepthCapture.Phase=SdlSceneCapturePhase::TerrainOverlay;in.DepthCapture.Depth=true;
    float distances[9]={8,8,8,8,8,8,8,8,8};SdlReachOverlay::Field field;field.Identity=1;field.Version=1;field.Width=field.Height=3;field.Step=1;field.OriginX=field.OriginY=-1;field.Colour=0x20a0ff;field.Distance=distances;field.DistanceCount=9;
    SdlReachOverlay::Policies policy;policy.FlushReaders=flush;if(refuseTexture)policy.CreateTexture=refuse_texture;
    return SdlReachOverlay::Draw(owner,in,&field,1,SdlReachOverlay::DefaultLook(),policy);
}
static void gpu() {
    std::string error;std::unique_ptr<SdlGpuFrame> a(SdlGpuFrame::Create(nullptr,64,48,error)),b(SdlGpuFrame::Create(nullptr,64,48,error));CHECK(a && b);if(!a || !b){std::printf("GPU ERROR: %s\n",error.c_str());return;}
    std::printf("GPU: driver=%s device=%s\n",SDL_GetGPUDeviceDriver(a->Device()),SDL_GetStringProperty(SDL_GetGPUDeviceProperties(a->Device()),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"));
    auto ca=particles(*a),cb=particles(*b);bool fresh=false;
    CHECK(SdlGpuParticles::Prepare(ca,8,&fresh,flush));CHECK(fresh);
    captures[0]={a.get(),capture(*a)};CHECK(a->Register_Owner_Release_Hook(release_capture));CHECK(reach(*a,captures[0].Texture));
    CHECK(SdlGpuParticles::TryReleaseForFrame(b.get()));CHECK(SdlReachOverlay::TryReleaseForFrame(b.get()));
    CHECK(SdlGpuParticles::Prepare(ca,8,&fresh,flush));CHECK(!fresh);CHECK(SdlReachOverlay::Stats().CachedBatches==1);
    // A full foreign registry refuses BEFORE provider replacement or partial GPU
    // allocation; the previous native owner and its queued packet remain intact.
    for(auto hook:spareHooks)CHECK(b->Register_Owner_Release_Hook(hook));
    CHECK(!SdlGpuParticles::Prepare(cb,8,&fresh,flush));CHECK(!fresh);CHECK(SdlGpuParticles::Stats().Capacity==8);
    auto *refusedDepth=capture(*b);CHECK(!reach(*b,refusedDepth));CHECK(SdlReachOverlay::Stats().CachedBatches==1);SDL_ReleaseGPUTexture(b->Device(),refusedDepth);
    for(auto hook:spareHooks)b->Unregister_Owner_Release_Hook(hook);
    CHECK(SdlGpuParticles::Prepare(ca,8,&fresh,flush));CHECK(!fresh);
    CHECK(SdlGpuParticles::TryReleaseForFrame(a.get()));CHECK(SdlReachOverlay::TryReleaseForFrame(a.get()));
    CHECK(SdlGpuParticles::Prepare(cb,8,&fresh,flush));CHECK(fresh);
    captures[1]={b.get(),capture(*b)};CHECK(b->Register_Owner_Release_Hook(release_capture));CHECK(reach(*b,captures[1].Texture));
    const auto reachBytes=SdlReachOverlay::Stats().OwnedTextureBytes;
    CHECK(SdlGpuParticles::TryReleaseForFrame(a.get()));CHECK(SdlReachOverlay::TryReleaseForFrame(a.get()));
    CHECK(SdlGpuParticles::Prepare(cb,8,&fresh,flush));CHECK(!fresh);CHECK(SdlReachOverlay::Stats().OwnedTextureBytes==reachBytes);
    // Persistent hooks left on A after early release must harmlessly ignore the
    // new B owner during A's real destruction, while B has an unsubmitted draw.
    a.reset();CHECK(!captures[0].Texture);CHECK(SdlGpuParticles::Prepare(cb,8,&fresh,flush));CHECK(!fresh);CHECK(SdlReachOverlay::Stats().CachedBatches==1);
    CHECK(b->Has_Pending_Work());refuseReaders=true;
    CHECK(!SdlGpuParticles::TryReleaseForFrame(b.get()));CHECK(!SdlReachOverlay::TryReleaseForFrame(b.get()));
    CHECK(SdlGpuParticles::Stats().Capacity==8 && SdlReachOverlay::Stats().OwnedTextureBytes==reachBytes);
    // The owning frame itself submits/discards CPU packets before hooks while
    // its device lives. Empty-reader callbacks bypass the refused ordinary hook.
    b.reset();CHECK(!captures[1].Texture);CHECK(SdlGpuParticles::Stats().Capacity==0 && SdlReachOverlay::Stats().CachedBatches==0);
    refuseReaders=false;CHECK(SdlGpuParticles::TryReleaseForFrame(nullptr));CHECK(SdlReachOverlay::TryReleaseForFrame(nullptr));
    // Fail after partial particle buffers/shaders/sprite allocation: the first
    // sprite clear refuses its queued-reader flush. The registered callback must
    // still own this incomplete state through actual frame teardown.
    std::unique_ptr<SdlGpuFrame> c(SdlGpuFrame::Create(nullptr,64,48,error));CHECK(c);
    if(c){
        auto cc=particles(*c);c->Set_Target(c->Back_Buffer_Target());c->Clear_Back_Buffer(true,true,false,0,1,0);
        refuseReaders=true;CHECK(!SdlGpuParticles::Prepare(cc,4,&fresh,flush));CHECK(SdlGpuParticles::Stats().Capacity==4);
        captures[0]={c.get(),capture(*c)};CHECK(c->Register_Owner_Release_Hook(release_capture));
        // Reach programs/dummy/pipeline have been prepared before field texture
        // refusal; their owner hook must also have existed before those objects.
        CHECK(!reach(*c,captures[0].Texture,true));CHECK(SdlReachOverlay::Stats().CachedBatches==0);
        c.reset();CHECK(!captures[0].Texture);CHECK(SdlGpuParticles::Stats().Capacity==0 && SdlReachOverlay::Stats().CachedBatches==0);
    }
    refuseReaders=false;
    // A later real device/frame recreates both providers without stale pointers.
    std::unique_ptr<SdlGpuFrame> d(SdlGpuFrame::Create(nullptr,64,48,error));CHECK(d);
    if(d){auto cd=particles(*d);CHECK(SdlGpuParticles::Prepare(cd,4,&fresh,flush));CHECK(fresh);
        captures[0]={d.get(),capture(*d)};CHECK(d->Register_Owner_Release_Hook(release_capture));CHECK(reach(*d,captures[0].Texture));
        d.reset();CHECK(!captures[0].Texture);CHECK(SdlGpuParticles::Stats().Capacity==0 && SdlReachOverlay::Stats().CachedBatches==0);}
}
int main(int argc,char **argv) {
    CHECK(SdlGpuParticles::TryReleaseForFrame(nullptr));CHECK(SdlReachOverlay::TryReleaseForFrame(nullptr));
    CHECK(SdlGpuParticles::Stats().Capacity==0);CHECK(SdlReachOverlay::Stats().CachedBatches==0);
    if(argc>1 && !strcmp(argv[1],"--gpu")){if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("GPU UNAVAILABLE: %s\n",SDL_GetError());return 77;}gpu();SDL_Quit();}
    std::printf("SDL owner release: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
