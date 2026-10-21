#include "WaterDx11PointGroupFixture.h"

namespace {
using namespace WaterDx11PointGroupTest;
void CheckFinite(const State &s)
{
    for(unsigned i=0;i<16;++i){CHECK(std::isfinite(s.nativeView[i]));CHECK(std::isfinite(s.logicalView[i]));CHECK(std::isfinite(s.projection[i]));}
}
}

TEST(task8_point_group_restores_live_water_view)
{
    const bool enabledBefore=Direct3D11_Is_Enabled(),presentBefore=Direct3D11_Present_Is_Enabled();
    {
        Fixture fixture;const bool ready=fixture.Initialise();CHECK(ready);if(!ready)return;
        Runtime &f=*fixture.runtime;
        const bool captured=fixture.Capture();CHECK(captured);if(!captured)return;
        const State pin=ReadState(f);PrintState("captured-pin",0,pin);CheckFinite(pin);
        CHECK(pin.valid);CHECK(pin.snapshots>0);if(!pin.valid)return;
        CHECK_MEM(pin.logicalView,pin.nativeView,sizeof(pin.nativeView));
        float identity[16];WaterRenderPacket::Identity(identity);
        CHECK(std::memcmp(pin.nativeView,identity,sizeof(identity))!=0);
        // The real one-point inputs are at world origin: this fixture maps z to .75 in clip space.
        const float viewZ=f.request.inputs.scene.view[14];
        const float clipZ=viewZ*f.request.inputs.scene.projection[10]+f.request.inputs.scene.projection[14];
        const float clipW=viewZ*f.request.inputs.scene.projection[11]+f.request.inputs.scene.projection[15];
        CHECK(std::isfinite(clipZ)&&std::isfinite(clipW)&&clipW>0&&clipZ>=0&&clipZ<=clipW);
        std::printf("  point-view input world=(0,0,0) size=2 clip=(0,0,%.9g,%.9g)\n",clipZ,clipW);

        PointInputs inputs;PointGroupClass group;CameraClass camera;RenderInfoClass info(camera);
        group.Set_Texture(f.white.engine);group.Set_Shader(ShaderClass::_PresetAlphaSpriteShader);
        group.Set_Point_Mode(PointGroupClass::QUADS);group.Set_Billboard(false);
        group.Set_Flag(PointGroupClass::TRANSFORM,true);group.Set_Point_Frame(0);
        inputs.Bind(group,0);group.Render(info);
        const State zero=ReadState(f);PrintState("zero-point-pin",0,zero);CheckFinite(zero);
        CHECK(zero.valid);CHECK(SameCurrent(pin.current,zero.current));CheckStableContext(pin,zero);
        CHECK_MEM(zero.nativeView,pin.nativeView,sizeof(pin.nativeView));
        CHECK_MEM(zero.logicalView,pin.logicalView,sizeof(pin.logicalView));
        CHECK_EQ(zero.made,pin.made);CHECK_EQ(zero.refused,pin.refused);

        inputs.Bind(group,1);
        for(unsigned iteration=1;iteration<=2;++iteration){
            const State before=ReadState(f);PrintState("before-point",iteration,before);CheckFinite(before);
            CHECK(before.valid);CHECK(SameCurrent(pin.current,before.current));
            if(!before.valid)return;
            group.Render(info);
            const State after=ReadState(f);PrintState("after-point",iteration,after);CheckFinite(after);
            CHECK(after.made>before.made);CHECK_EQ(after.refused,before.refused);
            CheckStableContext(before,after);
            CHECK_MEM(after.logicalView,before.logicalView,sizeof(before.logicalView));
            // Permanent regression assertions: do not hide failure when the later cause control succeeds.
            CHECK_MEM(after.nativeView,before.nativeView,sizeof(before.nativeView));
            CHECK(after.valid);
            if(after.valid)CHECK(SameCurrent(before.current,after.current));

            DX8Wrapper::Apply_Render_State_Changes(); // Cause control only: no draw, target or capture.
            const State flushed=ReadState(f);PrintState("after-existing-flush",iteration,flushed);CheckFinite(flushed);
            CHECK(flushed.valid);CHECK(SameCurrent(pin.current,flushed.current));
            CheckStableContext(after,flushed);
            CHECK_MEM(flushed.nativeView,pin.nativeView,sizeof(pin.nativeView));
            CHECK_MEM(flushed.logicalView,pin.logicalView,sizeof(pin.logicalView));
            CHECK_EQ(flushed.made,after.made);CHECK_EQ(flushed.refused,after.refused);
            CHECK_EQ(WaterDx11SurfaceTest::DebugWarnings(f.device->Get_Device()),0u);
        }
    }
    CHECK(!Direct3D11_Is_Active());CHECK(!DX8Wrapper::Is_Initted());
    CHECK_EQ(Direct3D11_Is_Enabled(),enabledBefore);CHECK_EQ(Direct3D11_Present_Is_Enabled(),presentBefore);
}
