#include "WaterDx11VesselFixture.h"

namespace {
using namespace WaterDx11VesselTest;
using Vertex=WaterDx11VesselTest::Vertex;
constexpr float Contrast=2.f/255.f; // fixed before any runtime observation
constexpr float NumericalAbsolute=1e-4f,NumericalRelative=1e-3f;
struct Hulls {
    Float4 mean[10]={},contact[10]={};
    Hulls(float speed=26.3f,float level=0,float shift=0){
        mean[0]={0,0,0,4};mean[1]={1,0,20,10};mean[2]={speed,0,.375f,0};
        mean[3]={-20,0,20,0};mean[4]={-20,-10,20,-10};mean[5]={20,10,-20,10};
        contact[0]={shift,0,level,4};contact[1]={1,0,10,4};contact[2]={speed,0,.375f,0};
        contact[3]={-10,0,10,0};contact[4]={-10,-4,10,-4};contact[5]={10,4,-10,4};
    }
};
struct BowResult {CapturedVertex native[4]={},on[4]={},off[4]={},oldShoulder[4]={};};
bool Bow(Fixture &f,BowResult &r,bool present=true,float freeDamping=1,float speed=26.3f,float level=0,float shift=0){
    Vertex vertices[4];const bool scene=Scene(f,vertices,true);CHECK(scene);if(!scene)return false;
    // Alpha zero disables the independent cage term; zero FFT fields isolate bow lift.
    CHECK(f.edge.Set(f.device.Get_Device(),{1,1,1,0}));f.BindResources();
    const float xs[4]={11.5f,11.5f,12,12},ys[4]={0,.25f,.25f,0};
    for(unsigned i=0;i<4;++i){vertices[i].x=xs[i];vertices[i].y=ys[i];vertices[i].nx=1;vertices[i].ny=freeDamping;vertices[i].nz=1;}
    Hulls hulls(speed,level,shift);auto &surface=f.request.inputs.surface;
    if(present){surface.hullParameters.x=1;surface.meanHulls={hulls.mean,10,10};surface.contactHulls={hulls.contact,10,10};}
    surface.fftParameters.y=1; // real vessel-wave gate; zero maps are not a generated-spectrum claim
    ZeroFields fields;const bool fieldsReady=fields.Set(f);CHECK(fieldsReady);if(!fieldsReady)return false;std::vector<unsigned char> on,off,old;
    const bool compiled=CompileVertex(true,true,on)&&CompileVertex(false,true,off)&&CompileVertex(true,false,old);CHECK(compiled);if(!compiled)return false;
    WaterRenderBridge::PreparedPatch patch={};const auto status=f.water->PreparePatch(f.request,patch);
    CHECK_EQ(status,WaterRenderBridge::PrepareStatus::Ready);if(status!=WaterRenderBridge::PrepareStatus::Ready)return false;
    SavedInputs saved;const bool captured=saved.Capture(f,patch);CHECK(captured);if(!captured)return false;
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);
    const bool ok=CaptureVertices(f,vertices,saved,r.native)&&CaptureVertices(f,vertices,saved,r.on,&on)
        &&CaptureVertices(f,vertices,saved,r.off,&off)&&CaptureVertices(f,vertices,saved,r.oldShoulder,&old);CHECK(ok);
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    if(ok)for(unsigned i=0;i<4;++i){for(float v:r.native[i].clip)CHECK(std::isfinite(v));
        CHECK_NEAR(r.native[i].originalXY[0],xs[i],1e-5f);CHECK_NEAR(r.native[i].originalXY[1],ys[i],1e-5f);}
    return ok;
}
double Height(const CapturedVertex &v){return (.5-double(v.clip[2]))/.25;}
void NearHeight(const CapturedVertex &v,double expected){CHECK_NEAR(Height(v),expected,NumericalAbsolute+NumericalRelative*std::fabs(expected));}
struct ShoreResult {std::vector<Pixel> production,enabled,disabled;};
bool Shore(Fixture &f,ShoreResult &r,float alpha,unsigned mask=6,float level=0,bool available=true){
    Vertex vertices[4];const bool scene=Scene(f,vertices);CHECK(scene);if(!scene)return false;Texture atlas;const bool atlasReady=Atlas(atlas,f.device.Get_Device(),alpha);CHECK(atlasReady);if(!atlasReady)return false;
    auto &surface=f.request.inputs.surface;surface.foamHistoryAvailable=available;
    if(available){f.water->Set_Test_Resource(&atlas,atlas.view);
        surface.foamHistory={&atlas,WaterRenderPacket::ResourceType::Texture2D,true,1,2,1,2};
        surface.foamHistoryArea={-32,-32,1.f/64,1.f/64};surface.foamHistoryParameters={float(mask),level,1,1.375f};}
    WaterRenderBridge::PreparedPatch patch={};const auto status=f.water->PreparePatch(f.request,patch);
    CHECK_EQ(status,WaterRenderBridge::PrepareStatus::Ready);if(status!=WaterRenderBridge::PrepareStatus::Ready)return false;
    ShoreControl control(f,atlas);const bool controlReady=control.Initialise(patch,vertices);CHECK(controlReady);if(!controlReady)return false;
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);
    CHECK(f.Read(r.production));CHECK(control.Draw(false,r.disabled));CHECK(control.Draw(true,r.enabled));
    Finite(r.production);Finite(r.disabled);Finite(r.enabled);return true;
}
}

TEST(task8_vessel_zero_field_geometry_pin){
    Fixture f;CHECK(f.Initialise());if(!f.water)return;BowResult r;if(!Bow(f,r,false))return;
    for(unsigned i=0;i<4;++i){NearHeight(r.native[i],0);NearHeight(r.on[i],0);NearHeight(r.off[i],0);}
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}
TEST(task8_bow_lift_native_geometry){
    Fixture f;CHECK(f.Initialise());if(!f.water)return;BowResult wet;if(!Bow(f,wet))return;
    // Independently selected peak: beam=4 gives head=3.6, speed response=1/2;
    // exterior shoulder (11.5,0) makes both Gaussian arguments zero. Mean hull
    // encloses this point (D=0), free damping F=1, zero ambient FFT => V*tanh(1.8/V).
    const double bound=WaterFftGeometry::MaxVerticalDisplacement;
    const double expected=bound*std::tanh(1.8/bound);
    const double oldExpected=bound*std::tanh(1.8*std::exp(-std::pow(1.5/10.8,2))/bound);
    NearHeight(wet.on[0],expected);NearHeight(wet.off[0],0);NearHeight(wet.oldShoulder[0],oldExpected);
    CHECK(std::fabs(Height(wet.on[0])-Height(wet.oldShoulder[0]))>.005);
    std::printf("  vessel geometry isolated-zero-FFT actual=%.9g canonical=%.9g disabled=%.9g scalar=%.9g shoulder-disabled=%.9g\n",
        Height(wet.native[0]),Height(wet.on[0]),Height(wet.off[0]),expected,Height(wet.oldShoulder[0]));
    NearHeight(wet.native[0],expected); // intended native bow-policy RED only after healthy controls
    BowResult stopped,wrongLevel,shifted,damped;
    if(!Bow(f,stopped,true,1,0)||!Bow(f,wrongLevel,true,1,26.3f,2)||!Bow(f,shifted,true,1,26.3f,0,128)||!Bow(f,damped,true,0))return;
    for(const auto *r:{&stopped,&wrongLevel,&shifted,&damped}){NearHeight(r->on[0],0);NearHeight(r->native[0],0);}
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
    // Global implemented-program support; joined real feature readbacks qualify these bits.
    using WaterRenderBridge::Feature;
    const auto interactionSupport=Feature::VesselWaves|Feature::WaveCages|Feature::FoamHistory
        |Feature::ShoreHistory|Feature::ShoreFoam|Feature::MeshContact;
    const auto published=f.water->GetCapability().features;
    std::printf("  task8 support published=%llx required=%llx isolated-FFT-updates=%llu\n",static_cast<unsigned long long>(published),static_cast<unsigned long long>(interactionSupport),static_cast<unsigned long long>(f.water->Get_Test_Fft_State().updates));
    CHECK(WaterRenderBridge::HasAll(published,interactionSupport));
}
TEST(task8_shore_history_material_pin){
    Fixture f;CHECK(f.Initialise());if(!f.water)return;ShoreResult a,b,absent;
    if(!Shore(f,a,0)||!Shore(f,b,0)||!Shore(f,absent,.75f,6,0,false))return;
    CHECK_NEAR(Region(a.production,b.production),0,1e-4f);CHECK_NEAR(Region(a.production,a.disabled),0,1e-4f);
    CHECK_NEAR(Region(a.enabled,a.disabled),0,1e-4f);CHECK_NEAR(Region(absent.enabled,absent.disabled),0,1e-4f);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}
TEST(task8_default_shore_alpha_native_material){
    Fixture f;CHECK(f.Initialise());if(!f.water)return;ShoreResult dry,wet,channel,level,absent;
    if(!Shore(f,dry,0)||!Shore(f,wet,.75f)||!Shore(f,channel,.75f,5)||!Shore(f,level,.75f,6,2)||!Shore(f,absent,.75f,6,0,false))return;
    const float shared=Region(wet.enabled,dry.enabled),native=Region(wet.production,dry.production);
    CHECK_NEAR(Region(wet.disabled,dry.disabled),0,1e-4f);CHECK(shared>Contrast);
    for(const auto *r:{&channel,&level,&absent}){CHECK_NEAR(Region(r->enabled,dry.enabled),0,1e-4f);CHECK_NEAR(Region(r->production,dry.production),0,1e-4f);}
    std::printf("  vessel shore PS14 top RGB=0 alpha=.75 bottom RGBA=0 mask6; ROI x[28,36)y[16,32) shared=%.9g native=%.9g threshold=%.9g\n",double(shared),double(native),double(Contrast));
    CHECK(native>Contrast); // intended production material RED, never auxiliary history sampling
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,0u);CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}
