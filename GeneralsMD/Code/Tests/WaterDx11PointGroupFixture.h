#pragma once
#include "WaterDx11DirtyMirrorFixture.h"
#include "dx8wrapper.h"
#include "formconv.h"
#include "pointgr.h"
#include "camera.h"
#include "rinfo.h"
#include "ww3d.h"
#include <memory>
#include <string>

namespace WaterDx11PointGroupTest {
using WaterDx11DirtyMirrorTest::Runtime;
using WaterDx11SurfaceTest::Pixel;

// Fresh single-filter process: keep the runtime cache-directory setting untouched.
inline bool OwnedWorkingDirectory()
{
    const char *cache=std::getenv("ZH_TEST_WATER_CACHE_DIR");
    char expected[MAX_PATH]={},actual[MAX_PATH]={};
    if(!cache||!*cache)return false;
    const DWORD n=GetFullPathNameA(cache,MAX_PATH,expected,NULL);
    const DWORD m=GetCurrentDirectoryA(MAX_PATH,actual);
    if(!n||n>=MAX_PATH||!m||m>=MAX_PATH)return false;
    const DWORD attributes=GetFileAttributesA(expected);
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    std::string a=expected,b=actual;
    for(char &c:a)if(c=='/')c='\\';
    for(char &c:b)if(c=='/')c='\\';
    while(a.size()>3&&a.back()=='\\')a.pop_back();
    while(b.size()>3&&b.back()=='\\')b.pop_back();
    return _stricmp(a.c_str(),b.c_str())==0;
}

struct Fixture {
    HWND window=NULL;
    bool entered=false,wrapperAttempted=false,wasEnabled=false,wasPresent=false;
    std::unique_ptr<Runtime> runtime;
    ~Fixture()
    {
        runtime.reset(); // End/targets/textures/source references precede wrapper/native teardown.
        if(wrapperAttempted)DX8Wrapper::Shutdown();
        else if(entered&&Direct3D11_Is_Active())Direct3D11_Release();
        if(entered){Direct3D11_Enable(wasEnabled);Direct3D11_Present_Enable(wasPresent);}
        if(window)DestroyWindow(window);
    }
    bool Initialise()
    {
        CHECK(!Direct3D11_Is_Active());CHECK(!DX8Wrapper::Is_Initted());CHECK(OwnedWorkingDirectory());
        if(Direct3D11_Is_Active()||DX8Wrapper::Is_Initted()||!OwnedWorkingDirectory())return false;
        wasEnabled=Direct3D11_Is_Enabled();wasPresent=Direct3D11_Present_Is_Enabled();entered=true;
        Direct3D11_Enable(true);Direct3D11_Present_Enable(false);
        window=CreateWindowExA(0,"STATIC","water-point-view",WS_OVERLAPPED,0,0,64,48,NULL,NULL,GetModuleHandle(NULL),NULL);
        if(!window||!Direct3D11_Create(window,64,48))return false;
        Direct3D11_Device()->Request_Debug_Layer();Direct3D11_Release();
        wrapperAttempted=true;
        Init_D3D_To_WW3_Conversion();
        if(!DX8Wrapper::Init(window,false))return false;
        if(WW3D::Set_Render_Device(0,64,48,32,1,false,false,false)!=WW3D_ERROR_OK)return false;
        runtime.reset(new Runtime);
        Runtime &f=*runtime;
        f.source=DX8Wrapper::_Get_D3D_Device();if(f.source)f.source->AddRef();
        f.device=Direct3D11_Device();f.backend=Direct3D11_Backend();
        f.water=f.backend?f.backend->Water_Owner():NULL;
        CHECK(f.source&&f.device&&f.backend&&f.water);
        if(!f.source||!f.device||!f.backend||!f.water)return false;
        CHECK(f.device->Has_Debug_Layer());if(!f.device->Has_Debug_Layer())return false;
        const char *expected=std::getenv("ZH_TEST_DX11_EXPECT_DRIVER");
        CHECK(expected&&(_stricmp(expected,"warp")==0||_stricmp(expected,"hardware")==0));
        if(!expected||(_stricmp(expected,"warp")!=0&&_stricmp(expected,"hardware")!=0))return false;
        CHECK(f.device->Is_Software()==(_stricmp(expected,"warp")==0));
        if(f.device->Is_Software()!=(_stricmp(expected,"warp")==0))return false;
        std::printf("  point-view actual-driver=%s debug=%u wrapper=%u cache-config-unchanged=1 owned-cwd=1\n",
            f.device->Is_Software()?"warp":"hardware",unsigned(f.device->Has_Debug_Layer()),unsigned(DX8Wrapper::Is_Initted()));
        return f.white.Create(f.source,8)&&f.black.Create(f.source,8)&&f.terrain.Create(f.source,8)
            &&f.optical.Create(f.source,8)&&f.bed.Create(f.source,8)&&f.white.Write(0xffffffff)
            &&f.black.Write(0xff000000)&&f.terrain.Write(0x0080ff80)&&f.optical.Write(0)&&f.bed.Write(0xff808073);
    }
    bool Capture()
    {
        Runtime &f=*runtime;
        if(!f.Scene(f.terrain))return false;
        Matrix4x4 view,projection;
        std::memcpy(&view,f.request.inputs.scene.view,sizeof(view));
        std::memcpy(&projection,f.request.inputs.scene.projection,sizeof(projection));
        DX8Wrapper::Set_Transform(D3DTS_WORLD,Matrix4x4(true));
        DX8Wrapper::Set_Transform(D3DTS_VIEW,view.Transpose());
        DX8Wrapper::Set_Transform(D3DTS_PROJECTION,projection.Transpose());
        DX8Wrapper::Apply_Render_State_Changes();
        std::vector<Pixel> pixels;
        return f.Draw(pixels);
    }
};

struct PointInputs {
    ShareBufferClass<Vector3> *locations=NEW_REF(ShareBufferClass<Vector3>,(1,"point-view locations"));
    ShareBufferClass<Vector4> *colours=NEW_REF(ShareBufferClass<Vector4>,(1,"point-view colours"));
    ShareBufferClass<float> *sizes=NEW_REF(ShareBufferClass<float>,(1,"point-view sizes"));
    ShareBufferClass<unsigned char> *angles=NEW_REF(ShareBufferClass<unsigned char>,(1,"point-view angles"));
    PointInputs()
    {
        locations->Get_Array()[0].Set(0,0,0);
        colours->Get_Array()[0].Set(1,1,1,1);
        sizes->Get_Array()[0]=2;angles->Get_Array()[0]=0;
    }
    ~PointInputs(){locations->Release_Ref();colours->Release_Ref();sizes->Release_Ref();angles->Release_Ref();}
    void Bind(PointGroupClass &group,int count)
    {
        group.Set_Arrays(locations,colours,NULL,sizes,angles,NULL,count);
    }
};

struct State {
    bool valid=false;
    WaterRenderBridge::CurrentView current={};
    float nativeView[16]={},projection[16]={},logicalView[16]={};
    D3D11_VIEWPORT viewport={};
    std::uintptr_t colour=0,depth=0;
    unsigned snapshots=0,pipelines=0,render=0;
    unsigned long long made=0,refused=0;
};
inline State ReadState(Runtime &f)
{
    State s;s.valid=f.water->QueryCurrentView(s.current);
    f.backend->Current_View_Projection(s.nativeView,s.projection);
    Matrix4x4 logical;DX8Wrapper::Get_Transform(D3DTS_VIEW,logical);
    const Matrix4x4 transposed=logical.Transpose();std::memcpy(s.logicalView,&transposed,sizeof(s.logicalView));
    UINT count=1;f.device->Get_Context()->RSGetViewports(&count,&s.viewport);CHECK_EQ(count,1u);
    ID3D11RenderTargetView *colour=NULL;ID3D11DepthStencilView *depth=NULL;
    f.device->Get_Context()->OMGetRenderTargets(1,&colour,&depth);
    s.colour=reinterpret_cast<std::uintptr_t>(colour);s.depth=reinterpret_cast<std::uintptr_t>(depth);
    if(colour)colour->Release();if(depth)depth->Release();
    s.snapshots=f.water->Snapshot_Count();s.render=WW3D::Get_Frame_Count();
    Direct3D11_Statistics(s.pipelines,s.made,s.refused);return s;
}
inline bool SameCurrent(const WaterRenderBridge::CurrentView &a,const WaterRenderBridge::CurrentView &b)
{
    return a.frameId==b.frameId&&a.resourceGeneration==b.resourceGeneration&&a.captureSerial==b.captureSerial
        &&a.colourIdentity==b.colourIdentity&&a.depthIdentity==b.depthIdentity&&a.viewIdentity==b.viewIdentity
        &&a.targetWidth==b.targetWidth&&a.targetHeight==b.targetHeight&&a.targetFormat==b.targetFormat
        &&a.targetSamples==b.targetSamples&&WaterRenderBridge::SameViewState(a.viewState,b.viewState);
}
inline void CheckStableContext(const State &before,const State &after)
{
    CHECK_EQ(after.render,before.render);CHECK_EQ(after.snapshots,before.snapshots);
    CHECK_EQ(after.colour,before.colour);CHECK_EQ(after.depth,before.depth);
    CHECK_MEM(&after.viewport,&before.viewport,sizeof(before.viewport));
    CHECK_MEM(after.projection,before.projection,sizeof(before.projection));
}
inline void PrintState(const char *phase,unsigned iteration,const State &s)
{
    std::printf("  point-view phase=%s iteration=%u valid=%u render=%u snapshots=%u frame=%llu generation=%llu capture=%llu colour=%llu depth=%llu view=%llu target=(%u,%u,%u,%u) bound=(%llu,%llu) viewport=(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g) pipelines=%u made=%llu refused=%llu\n",
        phase,iteration,unsigned(s.valid),s.render,s.snapshots,(unsigned long long)s.current.frameId,
        (unsigned long long)s.current.resourceGeneration,(unsigned long long)s.current.captureSerial,
        (unsigned long long)s.current.colourIdentity,(unsigned long long)s.current.depthIdentity,
        (unsigned long long)s.current.viewIdentity,s.current.targetWidth,s.current.targetHeight,s.current.targetFormat,
        s.current.targetSamples,(unsigned long long)s.colour,(unsigned long long)s.depth,s.viewport.TopLeftX,
        s.viewport.TopLeftY,s.viewport.Width,s.viewport.Height,s.viewport.MinDepth,s.viewport.MaxDepth,
        s.pipelines,s.made,s.refused);
    for(unsigned row=0;row<4;++row)std::printf("  point-view matrices row=%u logical=(%.9g,%.9g,%.9g,%.9g) native=(%.9g,%.9g,%.9g,%.9g) projection=(%.9g,%.9g,%.9g,%.9g)\n",
        row,s.logicalView[row*4],s.logicalView[row*4+1],s.logicalView[row*4+2],s.logicalView[row*4+3],
        s.nativeView[row*4],s.nativeView[row*4+1],s.nativeView[row*4+2],s.nativeView[row*4+3],
        s.projection[row*4],s.projection[row*4+1],s.projection[row*4+2],s.projection[row*4+3]);
}
}
