#include "WaterDx11DirtyMirrorFixture.h"

// Same four test-only W3D allocation entry points as test_ww3d2.cpp: real texture /
// surface classes run unchanged; no GameMemory globals or renderer calls are mocked.
void *createW3DMemPool(const char *,int){return reinterpret_cast<void*>(1);}
void *allocateFromW3DMemPool(void *,int size){auto *p=static_cast<char*>(::operator new(size_t(size)+16));std::memset(p,0,16);return p+16;}
void *allocateFromW3DMemPool(void *pool,int size,const char *,int){return allocateFromW3DMemPool(pool,size);}
void freeFromW3DMemPool(void *,void *p){::operator delete(static_cast<char*>(p)-16);}

namespace {
using namespace WaterDx11DirtyMirrorTest;
void CheckSample(const Pixel &p,unsigned word){CHECK_NEAR(p.r,((word>>16)&255)/255.f,.00001f);CHECK_NEAR(p.g,((word>>8)&255)/255.f,.00001f);CHECK_NEAR(p.b,(word&255)/255.f,.00001f);CHECK_NEAR(p.a,(word>>24)/255.f,.00001f);}
bool Init(Runtime &f){const bool ok=f.Initialise();CHECK(ok);return ok;}
bool Scene(Runtime &f,EngineTexture &texture,bool atlas=false,unsigned mask=7){const bool ok=f.Scene(texture,atlas,mask);CHECK(ok);return ok;}
bool Draw(Runtime &f,std::vector<Pixel> &pixels,EngineTexture *cage=NULL){const bool ok=f.Draw(pixels,cage);CHECK(ok);return ok;}
}

TEST(task8_dirty_mirror_engine_unlock_pin)
{
    Runtime f;if(!Init(f))return;EngineTexture cage;CHECK(cage.Create(f.source,Size));if(!cage.engine)return;
    CHECK(cage.Write(0x0080ff80));std::vector<Pixel> first,repeat;
    if(!Scene(f,cage)||!Draw(f,first,&cage))return;Words(f,cage,0x0080ff80);
    if(!Scene(f,cage)||!Draw(f,repeat,&cage))return;Words(f,cage,0x0080ff80);
    CHECK_NEAR(Contrast(first,repeat),0,.0001f);CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
    std::printf("  dirty PIN real engine managed source/Unlock/globalActive/Prepare/Submit finite; unchanged exact mirror bytes and raster\n");
}

TEST(task8_cage_dirty_mirror_readback)
{
    Runtime f;if(!Init(f))return;EngineTexture cage;CHECK(cage.Create(f.source,Size));if(!cage.engine)return;
    DisplacementControl fields(f);CHECK(fields.Initialise());std::vector<Pixel> original,blocked,reused,neutral;
    CHECK(cage.Write(0x0080ff80));if(!Scene(f,cage))return;fields.Geometry();
    std::printf("  cage first-before map0-override-equal=%u updates=%llu eligibility=%u\n",unsigned(f.water->Get_Test_Fft_Map(0)==fields.views[0]),static_cast<unsigned long long>(f.water->Get_Test_Fft_State().updates),unsigned(f.request.fftEligibility));
    if(!Draw(f,original,&cage))return;Words(f,cage,0x0080ff80);
    std::printf("  cage first-after map0-override-equal=%u updates=%llu eligibility=%u pixel2976=%.9g,%.9g,%.9g,%.9g\n",unsigned(f.water->Get_Test_Fft_Map(0)==fields.views[0]),static_cast<unsigned long long>(f.water->Get_Test_Fft_State().updates),unsigned(f.request.fftEligibility),double(original[2976].r),double(original[2976].g),double(original[2976].b),double(original[2976].a));
    CHECK(cage.Write(0xff800000));if(!Scene(f,cage))return;fields.Geometry();
    if(!Draw(f,blocked,&cage))return;Words(f,cage,0xff800000);
    if(!Scene(f,cage))return;fields.Geometry();if(!Draw(f,reused,&cage))return;
    Words(f,cage,0xff800000);CHECK_NEAR(Contrast(blocked,reused),0,.0001f);
    // Independent projection: clip.y=world.y/32+world.z/4. A positive .4-height
    // control lifts the lower edge >2 pixels. Zero radii/support1 returns z0.
    // Fixed strictly interior lower strip, chosen before any native run.
    CHECK(Background(original[46*64+32]));CHECK(!Background(blocked[46*64+32]));
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    if(!Scene(f,cage))return;
    std::printf("  cage neutral-before updates=%llu eligibility=%u\n",static_cast<unsigned long long>(f.water->Get_Test_Fft_State().updates),unsigned(f.request.fftEligibility));
    if(!Draw(f,neutral,&cage))return;
    std::printf("  cage neutral-after updates=%llu eligibility=%u\n",static_cast<unsigned long long>(f.water->Get_Test_Fft_State().updates),unsigned(f.request.fftEligibility));
    CHECK(!Background(neutral[46*64+32])); // no-displacement control proves visible base strip
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,0u); // override proof only, not spectrum generation
    CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
    std::printf("  cage measured-background pixel2976 original=%u blocked=%u neutral=%u\n",unsigned(Background(original[2976])),unsigned(Background(blocked[2976])),unsigned(Background(neutral[2976])));
}

TEST(task8_foam_dirty_mirror_readback)
{
    Runtime f;if(!Init(f))return;EngineTexture atlas;CHECK(atlas.Create(f.source,Size*2));if(!atlas.engine)return;
    const unsigned oldTop=0x00402010,oldDirection=0x80404020,newTop=0xc0804020,newDirection=0xc0c06030;
    Pixel samples[3]={};std::vector<Pixel> oldPixels,newPixels,repeat,maskOnly;
    CHECK(atlas.Write(oldTop,oldDirection));CHECK(SampleHistory(f,atlas,7,samples));
    CheckSample(samples[0],oldTop);CheckSample(samples[1],oldDirection);CHECK_NEAR(samples[2].r,0,.00001f);
    if(!Scene(f,atlas,true,5)||!Draw(f,oldPixels))return;Words(f,atlas,oldTop,oldDirection);
    CHECK(atlas.Write(newTop,newDirection));CHECK(SampleHistory(f,atlas,7,samples));
    CheckSample(samples[0],newTop);CheckSample(samples[1],newDirection);CHECK_NEAR(samples[2].r,192.f/255,.00001f);
    if(!Scene(f,atlas,true,5)||!Draw(f,newPixels))return;Words(f,atlas,newTop,newDirection);
    if(!Scene(f,atlas,true,5)||!Draw(f,repeat))return;CHECK_NEAR(Contrast(newPixels,repeat),0,.0001f);
    const float changed=Contrast(oldPixels,newPixels);CHECK(changed>2.f/255);
    // Direction alone must not invent vessel RGB or shore alpha, and correct shore
    // bit must survive direction-tile packing. These invoke the actual shared HLSL.
    for(unsigned mask:{6u,5u,4u}){CHECK(SampleHistory(f,atlas,mask,samples));CheckSample(samples[0],newTop);CheckSample(samples[1],newDirection);
        CHECK_NEAR(samples[2].r,(mask&2u)?192.f/255:0,.00001f);}
    CHECK(SampleHistory(f,atlas,7,samples,2));CHECK_NEAR(samples[2].r,0,.00001f); // wrong water level
    auto disabledHistory=[&](std::vector<Pixel> &pixels){
        if(!Scene(f,atlas,true,5))return false;auto &surface=f.request.inputs.surface;
        surface.foamHistoryAvailable=false;surface.foamHistory={};
        surface.foamHistoryArea={};surface.foamHistoryParameters={};return Draw(f,pixels);
    };
    CHECK(atlas.Write(0,oldDirection));if(!disabledHistory(maskOnly))return;
    CHECK(atlas.Write(newTop,newDirection));if(!disabledHistory(repeat))return;
    CHECK_NEAR(Contrast(maskOnly,repeat),0,.0001f); // coherent unavailable history ignores unrelated changed atlas bytes
    CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
    std::printf("  dirty atlas actual PS14 material ROI x[28,36)y[16,32) delta=%.9g threshold=%.9g; shared shorealpha/direction bits validated separately\n",double(changed),double(2.f/255));
}

TEST(task8_dirty_mirror_locked_refresh_refuses_stale_and_recovers)
{
    Runtime f;if(!Init(f))return;EngineTexture cage;CHECK(cage.Create(f.source,Size));if(!cage.engine)return;
    CHECK(cage.Write(0x0080ff80));Words(f,cage,0x0080ff80);
    CHECK(cage.Write(0xff800000));SurfaceClass *held=cage.engine->Get_Surface_Level();CHECK(held!=NULL);if(!held)return;
    int pitch=0;void *locked=held->Lock(&pitch);CHECK(locked!=NULL);if(!locked){held->Release_Ref();return;}
    // A stale cache answer cannot truthfully stand in for failed new source bytes.
    // Current mirror ignores failed refresh_copy and returns old nonnull SRV: intended RED.
    ID3D11ShaderResourceView *stale=f.Mirror(cage);CHECK(stale==NULL);
    if(stale){std::vector<unsigned> words;CHECK(f.ReadWords(stale,words));CHECK(!words.empty());if(!words.empty())CHECK_EQ(words[0],0x0080ff80u);}
    if(Scene(f,cage)){WaterRenderBridge::PreparedPatch patch={};const auto status=f.water->PreparePatch(f.request,patch);
        CHECK_NE(status,WaterRenderBridge::PrepareStatus::Ready);
        if(status==WaterRenderBridge::PrepareStatus::Ready){const auto receipt=f.water->SubmitPreparedPatch(patch);
            std::printf("  dirty STALE actual surface emission commands=%u old-SRV=%p\n",receipt.commandsIssued,stale);}}
    held->Unlock();held->Release_Ref();Words(f,cage,0xff800000); // actual Unlock re-dirties and recovers
    std::vector<Pixel> pixels;if(!Scene(f,cage)||!Draw(f,pixels,&cage))return;
    CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);

    // One real engine write/Unlock marks dirty. Releasing a later READONLY raw
    // lock must not need another dirty notification to recover a failed refresh.
    const unsigned retryWord=0xc04080ffu;const bool wrote=cage.Write(retryWord);CHECK(wrote);if(!wrote)return;
    struct RawReadLock {
        IDirect3DTexture9 *texture=NULL;bool held=false;
        ~RawReadLock(){if(held)CHECK(SUCCEEDED(texture->UnlockRect(0)));Release(texture);}
        bool Unlock(){const HRESULT hr=texture->UnlockRect(0);CHECK(SUCCEEDED(hr));if(SUCCEEDED(hr))held=false;return SUCCEEDED(hr);}
    } raw;
    HRESULT hr=cage.Raw()->QueryInterface(IID_IDirect3DTexture9,reinterpret_cast<void**>(&raw.texture));
    CHECK(SUCCEEDED(hr)&&raw.texture!=NULL);if(FAILED(hr)||!raw.texture)return;
    D3DLOCKED_RECT read={};hr=raw.texture->LockRect(0,&read,NULL,D3DLOCK_READONLY);
    CHECK(SUCCEEDED(hr));if(FAILED(hr))return;raw.held=true;
    CHECK(read.pBits!=NULL&&read.Pitch>=int(Size*sizeof(unsigned)));
    if(!read.pBits||read.Pitch<int(Size*sizeof(unsigned)))return;
    for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){const auto *row=reinterpret_cast<const unsigned*>(static_cast<const char*>(read.pBits)+y*read.Pitch);CHECK_EQ(row[x],retryWord);}
    auto *firstLocked=f.Mirror(cage);CHECK(firstLocked==NULL);
    auto *secondLocked=f.Mirror(cage);CHECK(secondLocked==NULL);
    if(!Scene(f,cage))return;WaterRenderBridge::PreparedPatch retryPatch={};
    const auto retryStatus=f.water->PreparePatch(f.request,retryPatch);CHECK_NE(retryStatus,WaterRenderBridge::PrepareStatus::Ready);
    std::printf("  dirty retry READONLY-lock mirror-nonnull=%u,%u prepare-status=%u source-word=%08x\n",unsigned(firstLocked!=NULL),unsigned(secondLocked!=NULL),unsigned(retryStatus),retryWord);
    if(!raw.Unlock())return; // raw UnlockRect only: NO new SurfaceClass dirty hook
    Words(f,cage,retryWord); // exact64 new GPU words after retry without notification
    if(!Scene(f,cage)||!Draw(f,pixels,&cage))return;
    CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
}

TEST(task8_dirty_mirror_prepared_resource_lifetime)
{
    Runtime f;if(!Init(f))return;EngineTexture oldTexture,newTexture;
    CHECK(oldTexture.Create(f.source,Size));CHECK(newTexture.Create(f.source,Size));if(!oldTexture.engine||!newTexture.engine)return;
    CHECK(oldTexture.Write(0x0080ff80));CHECK(newTexture.Write(0xff800000));if(!Scene(f,oldTexture))return;
    WaterRenderBridge::PreparedPatch a={};CHECK_EQ(f.water->PreparePatch(f.request,a),WaterRenderBridge::PrepareStatus::Ready);
    auto *oldView=f.water->Get_Test_Prepared_Vertex_Resource(a.patchId,3);CHECK(oldView!=NULL);if(!oldView)return;oldView->AddRef();
    f.request.patchId+=100;f.request.inputs.surface.heightWaveCage=newTexture.Descriptor();WaterRenderBridge::PreparedPatch b={};
    CHECK_EQ(f.water->PreparePatch(f.request,b),WaterRenderBridge::PrepareStatus::Ready);
    oldTexture.engine->Release_Ref();oldTexture.engine=NULL; // native prepared SRV, not engine object, holds old resource
    std::vector<unsigned> words;CHECK(f.ReadWords(oldView,words));for(unsigned word:words)CHECK_EQ(word,0x0080ff80u);
    // Remove only actual context bindings of this view; preserve RT/depth/viewport/stamp.
    auto *context=f.device->Get_Context();unsigned removed=0;
    for(unsigned stage=0;stage<2;++stage){ID3D11ShaderResourceView *bound[16]={};
        if(stage==0)context->VSGetShaderResources(0,16,bound);else context->PSGetShaderResources(0,16,bound);
        for(unsigned slot=0;slot<16;++slot){if(bound[slot]==oldView){ID3D11ShaderResourceView *empty=NULL;
                if(stage==0)context->VSSetShaderResources(slot,1,&empty);else context->PSSetShaderResources(slot,1,&empty);++removed;}
            Release(bound[slot]);}}
    std::printf("  dirty old-resource lifetime removed-context-bindings=%u; releasing diagnostic reference BEFORE SubmitA\n",removed);
    Release(oldView); // sole persistent owners are now prepared resource sidecars
    auto receipt=f.water->SubmitPreparedPatch(a);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);
    std::vector<Pixel> pixels;CHECK(f.ReadPixels(pixels));CHECK_EQ(pixels.size(),64u*48u);unsigned visible=0;
    for(const auto &p:pixels){CHECK(std::isfinite(p.r)&&std::isfinite(p.g)&&std::isfinite(p.b)&&std::isfinite(p.a));visible+=std::fabs(p.r-.32f)>.001f;}CHECK(visible>100);
    receipt=f.water->SubmitPreparedPatch(b);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);
    CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
    // This separate-object case proves object lifetime, not immutable content after same-resource rewrite.
}

TEST(task8_dirty_mirror_same_resource_updates_retained_prepared_content)
{
    Runtime f;if(!Init(f))return;EngineTexture cage;CHECK(cage.Create(f.source,Size));if(!cage.engine)return;
    CHECK(cage.Write(0x0080ff80));if(!Scene(f,cage))return;
    WaterRenderBridge::PreparedPatch a={},b={};CHECK_EQ(f.water->PreparePatch(f.request,a),WaterRenderBridge::PrepareStatus::Ready);
    auto *view=f.water->Get_Test_Prepared_Vertex_Resource(a.patchId,3);CHECK(view!=NULL);if(!view)return;view->AddRef();
    CHECK(cage.Write(0xff800000));f.request.patchId+=100;
    CHECK_EQ(f.water->PreparePatch(f.request,b),WaterRenderBridge::PrepareStatus::Ready);
    CHECK_EQ(view,f.water->Get_Test_Prepared_Vertex_Resource(b.patchId,3));
    std::vector<unsigned> words;CHECK(f.ReadWords(view,words));CHECK_EQ(words.size(),Size*Size);
    for(unsigned word:words)CHECK_EQ(word,0xff800000u); // same retained resource is mutable, COM lifetime only
    CHECK_EQ(f.water->SubmitPreparedPatch(a).status,WaterRenderBridge::SubmitStatus::Issued);
    CHECK_EQ(f.water->SubmitPreparedPatch(b).status,WaterRenderBridge::SubmitStatus::Issued);
    Release(view);CHECK_EQ(DebugWarnings(f.device->Get_Device()),0u);
}
