#include "WaterDx11DirtyMirrorFixture.h"
#include "Platform/WaterFoamHistory.h"

namespace {
using namespace WaterDx11DirtyMirrorTest;
using Words32=std::vector<std::uint32_t>;
const WaterFoamHistory::Mapping WorldWindow={-16,-16,32,32,Size,Size};

WaterHullModel::Hull MovingHull(double time)
{
    WaterHullModel::Hull h={};h.data[0][0]=float(-4+8*time);h.data[0][3]=4;
    h.data[1][0]=1;h.data[1][2]=4;h.data[1][3]=4;h.data[2][0]=8;
    h.data[3][0]=-4;h.data[3][2]=4;
    const float contour[8]={-4,-4,4,-4,4,4,-4,4};
    std::memcpy(h.data[4],contour,sizeof(contour));return h;
}
Words32 Atlas(const WaterFoamHistory::History &history)
{
    Words32 top,direction;const bool built=history.BuildAtlas(WorldWindow,0,top,&direction);
    CHECK(built);CHECK_EQ(top.size(),Size*Size);CHECK_EQ(direction.size(),top.size());
    if(!built)return {};top.insert(top.end(),direction.begin(),direction.end());return top;
}
bool Upload(Runtime &f,EngineTexture &texture,const Words32 &words)
{
    CHECK_EQ(words.size(),Size*Size*2);if(words.size()!=Size*Size*2)return false;
    SurfaceClass *surface=texture.engine->Get_Surface_Level();CHECK(surface!=NULL);if(!surface)return false;
    int pitch=0;void *bits=surface->Lock(&pitch);const bool ok=bits&&pitch>=int(Size*4);CHECK(ok);
    if(ok)for(unsigned y=0;y<Size*2;++y)std::memcpy(static_cast<char*>(bits)+y*pitch,words.data()+y*Size,Size*4);
    if(bits)surface->Unlock();surface->Release_Ref();if(!ok)return false;
    std::vector<unsigned> actual;const bool read=f.ReadWords(f.Mirror(texture),actual);CHECK(read);
    CHECK(actual==words);std::printf("  history engine=%p mirror=%p GPU-words=%zu exact=%u\n",
        static_cast<void*>(texture.Raw()),static_cast<void*>(f.Mirror(texture)),actual.size(),unsigned(actual==words));
    return read&&actual==words;
}
bool Scene(Runtime &f,EngineTexture &atlas,float viewX,double time,bool enabled=true,bool wrongWindow=false)
{
    const bool ok=f.Scene(atlas,true,5);CHECK(ok);if(!ok)return false;
    auto &scene=f.request.inputs.scene;scene.view[12]=viewX;
    f.backend->Set_Transform(D3DTS_VIEW,scene.view);
    auto &surface=f.request.inputs.surface;surface.waterParameters.x=float(time);surface.fftParameters.y=1;
    surface.foamHistoryArea={wrongWindow?1000.f:-16.f,-16,1.f/32,1.f/32};
    surface.foamHistoryParameters.w=float(time);
    if(!enabled){surface.foamHistoryAvailable=false;surface.foamHistory={};
        surface.foamHistoryArea={};surface.foamHistoryParameters={};}
    return true;
}
bool Submit(Runtime &f,const WaterRenderBridge::PreparedPatch &patch,std::vector<Pixel> &pixels)
{
    WaterRenderBridge::CurrentView current={};const bool valid=f.water->QueryCurrentView(current);CHECK(valid);
    if(!valid)return false;
    CHECK_EQ(current.frameId,patch.frameId);CHECK_EQ(current.resourceGeneration,patch.resourceGeneration);
    CHECK_EQ(current.captureSerial,patch.captureSerial);CHECK_EQ(current.viewIdentity,patch.viewIdentity);
    CHECK_EQ(current.colourIdentity,patch.colourIdentity);CHECK_EQ(current.depthIdentity,patch.depthIdentity);
    CHECK(WaterRenderBridge::SameViewState(current.viewState,patch.viewState));
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);
    CHECK_EQ(receipt.commandsIssued,1u);CHECK(WaterRenderBridge::Truthful(receipt,patch));
    std::printf("  history frame=%llu gen=%llu capture=%llu patch=%llu view=%p colour=%p depth=%p target=%ux%u/%u/%u viewX=%.9g time=%.9g commands=%u\n",
        static_cast<unsigned long long>(patch.frameId),static_cast<unsigned long long>(patch.resourceGeneration),
        static_cast<unsigned long long>(patch.captureSerial),static_cast<unsigned long long>(patch.patchId),
        reinterpret_cast<void*>(patch.viewIdentity),reinterpret_cast<void*>(patch.colourIdentity),reinterpret_cast<void*>(patch.depthIdentity),
        patch.targetWidth,patch.targetHeight,patch.targetFormat,patch.targetSamples,double(current.viewState.view[12]),
        double(f.request.inputs.surface.waterParameters.x),receipt.commandsIssued);
    const bool read=f.ReadPixels(pixels);CHECK(read);CHECK_EQ(pixels.size(),64u*48u);
    for(const auto &p:pixels)CHECK(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a));
    return read&&pixels.size()==64*48&&receipt.status==WaterRenderBridge::SubmitStatus::Issued;
}
bool Render(Runtime &f,EngineTexture &atlas,float x,double time,std::vector<Pixel> &pixels,bool enabled=true,bool wrong=false)
{
    if(!Scene(f,atlas,x,time,enabled,wrong))return false;WaterRenderBridge::PreparedPatch patch={};
    const auto status=f.water->PreparePatch(f.request,patch);CHECK_EQ(status,WaterRenderBridge::PrepareStatus::Ready);
    return status==WaterRenderBridge::PrepareStatus::Ready&&Submit(f,patch,pixels);
}
struct Difference {double mass=0,x=0,y=0;float peak=0;};
Difference DifferenceField(const std::vector<Pixel> &a,const std::vector<Pixel> &b)
{
    Difference result;CHECK_EQ(a.size(),64u*48u);CHECK_EQ(b.size(),a.size());if(a.size()!=64*48||b.size()!=a.size())return result;
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){const unsigned n=y*64+x;
        const float delta=(std::max)({std::fabs(a[n].r-b[n].r),std::fabs(a[n].g-b[n].g),std::fabs(a[n].b-b[n].b)});
        result.peak=(std::max)(result.peak,delta);result.mass+=delta;result.x+=delta*(x+.5);result.y+=delta*(y+.5);}
    if(result.mass>0){result.x/=result.mass;result.y/=result.mass;}return result;
}
Difference Visible(Runtime &f,EngineTexture &atlas,float x,double time)
{
    std::vector<Pixel> enabled,disabled,wrong;
    if(!Render(f,atlas,x,time,enabled)||!Render(f,atlas,x,time,disabled,false)||!Render(f,atlas,x,time,wrong,true,true))return {};
    const auto foam=DifferenceField(enabled,disabled);const auto control=DifferenceField(wrong,disabled);
    CHECK(foam.peak>0.0001f);CHECK(foam.mass>0.01);CHECK_NEAR(control.peak,0,.0001f);
    std::printf("  history world-field viewX=%.9g time=%.9g peak=%.9g mass=%.9g centroid=(%.9g,%.9g) wrong-window-peak=%.9g\n",
        double(x),time,double(foam.peak),foam.mass,foam.x,foam.y,double(control.peak));return foam;
}
}

// Renderer/history seam only: explicit simulation times, flat geometry, no ordinary-game pause or FFT-generation claim.
TEST(task8_foam_history_tracks_camera_and_offscreen_return)
{
    Runtime f;const bool initialised=f.Initialise();CHECK(initialised);if(!initialised)return;
    EngineTexture atlas;CHECK(atlas.Create(f.source,Size*2));if(!atlas.engine)return;
    WaterFoamHistory::History history;
    for(unsigned step=0;step<=4;++step){const double time=step*.25;CHECK(history.Advance(time));
        const bool emitted=history.Observe(7,MovingHull(time),time);CHECK_EQ(emitted,step!=0);}
    CHECK_EQ(history.SegmentCount(),4u);CHECK(history.HasFoam());
    const auto frozen=Atlas(history);if(!Upload(f,atlas,frozen))return;
    const auto fftBefore=f.water->Get_Test_Fft_State().updates;
    const auto origin=Visible(f,atlas,0,1);const auto shifted=Visible(f,atlas,12,1);
    // Orthographic projection: screenX=32.5+worldX+viewX, screenY=24.5-.75*worldY.
    // Stern endpoints x[-8,0]; age-expanded production kernels and one atlas filter cell fit x[-20,12], y[-12,12].
    CHECK(origin.x>=12.5&&origin.x<=44.5);CHECK(origin.y>=15.5&&origin.y<=33.5);
    CHECK_NEAR(shifted.x-origin.x,12,1.5);CHECK_NEAR(shifted.y,origin.y,1.5);
    CHECK_NEAR(history.Time(),1,0);CHECK_EQ(history.SegmentCount(),4u);CHECK(Atlas(history)==frozen);
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,fftBefore); // IneligibleFFT, not FFT reuse coverage.

    if(!Scene(f,atlas,0,1))return;WaterRenderBridge::PreparedPatch stale={};
    const auto ready=f.water->PreparePatch(f.request,stale);CHECK_EQ(ready,WaterRenderBridge::PrepareStatus::Ready);
    if(ready!=WaterRenderBridge::PrepareStatus::Ready)return;
    f.request.inputs.scene.view[12]=12;f.backend->Set_Transform(D3DTS_VIEW,f.request.inputs.scene.view);
    WaterRenderBridge::CurrentView changed={};CHECK(!f.water->QueryCurrentView(changed));
    const auto refused=f.water->SubmitPreparedPatch(stale);CHECK_EQ(refused.status,WaterRenderBridge::SubmitStatus::Refused);
    CHECK_EQ(refused.commandsIssued,0u); // Actual backend-only view change; target/frame/capture untouched.
    f.request.patchId+=100;WaterRenderBridge::PreparedPatch fresh={};
    CHECK_EQ(f.water->PreparePatch(f.request,fresh),WaterRenderBridge::PrepareStatus::Ready);
    CHECK_NE(fresh.viewIdentity,stale.viewIdentity);std::vector<Pixel> recovered;
    if(!Submit(f,fresh,recovered))return;

    // Move the real view wholly off the quad. History advances by the existing .25s clock while it is offscreen.
    std::vector<Pixel> offscreen;
    for(unsigned step=5;step<=8;++step){const double time=step*.25;CHECK(history.Advance(time));
        CHECK(history.Observe(7,MovingHull(time),time));const auto words=Atlas(history);if(!Upload(f,atlas,words))return;
        if(!Render(f,atlas,128,time,offscreen))return;
        for(const auto &pixel:offscreen)CHECK(Background(pixel));}
    CHECK_EQ(history.SegmentCount(),8u);CHECK_NEAR(history.Time(),2,0);
    const auto returnedWords=Atlas(history);CHECK(returnedWords!=frozen);
    CHECK(!history.Advance(2));CHECK(!history.Observe(7,MovingHull(2),2));CHECK(Atlas(history)==returnedWords);
    const auto returned=Visible(f,atlas,0,2);
    // At t=2 the stern ends at x=8; both new and retained trail remain inside the fixed world window.
    CHECK(returned.x>=12.5&&returned.x<=52.5);CHECK(returned.y>=15.5&&returned.y<=33.5);
    std::vector<Pixel> once,repeat;if(!Render(f,atlas,0,2,once)||!Render(f,atlas,0,2,repeat))return;
    CHECK_NEAR(DifferenceField(once,repeat).peak,0,.0001f);CHECK(Atlas(history)==returnedWords);
    CHECK_EQ(history.SegmentCount(),8u);CHECK_NEAR(history.Time(),2,0);

    CHECK(history.Advance(26.25));CHECK(!history.HasFoam());CHECK_EQ(history.SegmentCount(),0u);
    const auto expired=Atlas(history);CHECK(std::all_of(expired.begin(),expired.end(),[](std::uint32_t word){return word==0;}));
    if(!Upload(f,atlas,expired))return;std::vector<Pixel> noGhost,noAtlas;
    if(!Render(f,atlas,0,26.25,noGhost)||!Render(f,atlas,0,26.25,noAtlas,false))return;
    CHECK_NEAR(DifferenceField(noGhost,noAtlas).peak,0,.0001f);
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,fftBefore);CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
    std::printf("  history offscreen-return: exact dirty atlas, eight cadence sections, duplicate-time unchanged, expired atlas/no-ghost; ordinary pause UNRUN\n");
}
