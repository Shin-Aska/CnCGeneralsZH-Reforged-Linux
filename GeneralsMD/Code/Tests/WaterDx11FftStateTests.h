#pragma once

namespace WaterDx11FftTest {
inline std::vector<std::uintptr_t> FullContext(ID3D11DeviceContext *context)
{
    auto result=ContextState(context);
    typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*RG)(UINT,UINT,ID3D11ShaderResourceView **);
    typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*BG)(UINT,UINT,ID3D11Buffer **);
    const RG resources[]={&ID3D11DeviceContext::VSGetShaderResources,&ID3D11DeviceContext::HSGetShaderResources,
        &ID3D11DeviceContext::DSGetShaderResources,&ID3D11DeviceContext::GSGetShaderResources,&ID3D11DeviceContext::PSGetShaderResources,&ID3D11DeviceContext::CSGetShaderResources};
    const BG buffers[]={&ID3D11DeviceContext::VSGetConstantBuffers,&ID3D11DeviceContext::HSGetConstantBuffers,
        &ID3D11DeviceContext::DSGetConstantBuffers,&ID3D11DeviceContext::GSGetConstantBuffers,&ID3D11DeviceContext::PSGetConstantBuffers,&ID3D11DeviceContext::CSGetConstantBuffers};
    for(unsigned stage=0;stage<6;++stage){
        ID3D11ShaderResourceView *views[112]={};ID3D11Buffer *constants[10]={};
        (context->*resources[stage])(16,112,views);(context->*buffers[stage])(4,10,constants);
        for(auto *object:views){result.push_back(reinterpret_cast<std::uintptr_t>(object));if(object)object->Release();}
        for(auto *object:constants){result.push_back(reinterpret_cast<std::uintptr_t>(object));if(object)object->Release();}
    }
    ID3D11UnorderedAccessView *uavs[8]={};context->CSGetUnorderedAccessViews(0,8,uavs);
    for(auto *object:uavs){result.push_back(reinterpret_cast<std::uintptr_t>(object));if(object)object->Release();}
    return result;
}

inline bool SeedContext(Fixture &f,ID3D11Buffer *&constant,ID3D11SamplerState *&sampler)
{
    D3D11_BUFFER_DESC bd={};bd.ByteWidth=16;bd.Usage=D3D11_USAGE_DEFAULT;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sd={};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
    if(FAILED(f.device.Get_Device()->CreateBuffer(&bd,NULL,&constant))||FAILED(f.device.Get_Device()->CreateSamplerState(&sd,&sampler)))return false;
    typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*RS)(UINT,UINT,ID3D11ShaderResourceView *const *);
    typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*BS)(UINT,UINT,ID3D11Buffer *const *);
    typedef void (STDMETHODCALLTYPE ID3D11DeviceContext::*SS)(UINT,UINT,ID3D11SamplerState *const *);
    const RS resources[]={&ID3D11DeviceContext::VSSetShaderResources,&ID3D11DeviceContext::HSSetShaderResources,
        &ID3D11DeviceContext::DSSetShaderResources,&ID3D11DeviceContext::GSSetShaderResources,&ID3D11DeviceContext::PSSetShaderResources,&ID3D11DeviceContext::CSSetShaderResources};
    const BS buffers[]={&ID3D11DeviceContext::VSSetConstantBuffers,&ID3D11DeviceContext::HSSetConstantBuffers,
        &ID3D11DeviceContext::DSSetConstantBuffers,&ID3D11DeviceContext::GSSetConstantBuffers,&ID3D11DeviceContext::PSSetConstantBuffers,&ID3D11DeviceContext::CSSetConstantBuffers};
    const SS samplers[]={&ID3D11DeviceContext::VSSetSamplers,&ID3D11DeviceContext::HSSetSamplers,
        &ID3D11DeviceContext::DSSetSamplers,&ID3D11DeviceContext::GSSetSamplers,&ID3D11DeviceContext::PSSetSamplers,&ID3D11DeviceContext::CSSetSamplers};
    ID3D11ShaderResourceView *views[128];ID3D11Buffer *constants[14];ID3D11SamplerState *states[16];
    for(auto &v:views)v=f.edge.view;for(auto &v:constants)v=constant;for(auto &v:states)v=sampler;
    for(unsigned stage=0;stage<6;++stage){
        (f.device.Get_Context()->*resources[stage])(0,128,views);
        (f.device.Get_Context()->*buffers[stage])(0,14,constants);
        (f.device.Get_Context()->*samplers[stage])(0,16,states);
    }
    return true;
}
}

TEST(fft_seeded_all_stage_state_and_snapshot_identity)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    FftVertex vertices[4];geometry(f,vertices,0);
    WaterRenderBridge::PreparedPatch patch={};CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    WaterRenderBridge::CurrentView before={};CHECK(f.water->QueryCurrentView(before));
    const unsigned snapshots=f.water->Snapshot_Count();
    ID3D11Buffer *constant=NULL;ID3D11SamplerState *sampler=NULL;
    CHECK(WaterDx11FftTest::SeedContext(f,constant,sampler));
    if(!constant||!sampler){Release(constant);Release(sampler);return;}
    const auto state=WaterDx11FftTest::FullContext(f.device.Get_Context());
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));CHECK(f.water->Prepare_Test_Fft(1.375f));
    CHECK(WaterDx11FftTest::FullContext(f.device.Get_Context())==state);
    WaterRenderBridge::CurrentView after={};CHECK(f.water->QueryCurrentView(after));
    CHECK_EQ(before.frameId,after.frameId);CHECK_EQ(before.resourceGeneration,after.resourceGeneration);
    CHECK_EQ(before.captureSerial,after.captureSerial);CHECK_EQ(before.colourIdentity,after.colourIdentity);
    CHECK_EQ(before.depthIdentity,after.depthIdentity);CHECK_EQ(before.viewIdentity,after.viewIdentity);
    CHECK(WaterRenderBridge::SameViewState(before.viewState,after.viewState));CHECK_EQ(f.water->Snapshot_Count(),snapshots);
    std::printf("  fft seeded-state six-stages SRVs=128 CBs=14 samplers=16 each exact-restored snapshots=%u unchanged; OM/CS UAVs initially empty and FFT uses no UAV\n",snapshots);
    f.device.Get_Context()->ClearState();f.backend.Forget_Bindings();Release(constant);Release(sampler);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_new_device_epoch_resets_and_refuses_old_patch)
{
    WaterRenderBridge::PreparedPatch old={};
    {Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
        FftVertex vertices[4];geometry(f,vertices,0);f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
        f.request.inputs.surface.waterParameters.x=1.375f;
        CHECK_EQ(f.water->PreparePatch(f.request,old),WaterRenderBridge::PrepareStatus::Ready);
        CHECK(f.water->Get_Test_Fft_State().ready);}
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    const auto fresh=f.water->Get_Test_Fft_State();CHECK(!fresh.ready);CHECK_EQ(fresh.draws,0u);CHECK_EQ(fresh.copies,0u);CHECK_EQ(fresh.updates,0u);
    CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));
    DrawCounter counter(f);const auto receipt=f.water->SubmitPreparedPatch(old);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Refused);CHECK_EQ(receipt.commandsIssued,0u);CHECK_EQ(counter.End(),0u);
    CHECK(f.water->Prepare_Test_Fft(1.375f));const auto updated=f.water->Get_Test_Fft_State();
    CHECK(updated.ready);CHECK_EQ(updated.draws,26u);CHECK_EQ(updated.copies,102u);CHECK_EQ(updated.updates,1u);
    WaterDx11FftTest::ReportState("fresh-device-first-update",updated);
    f.water->Shutdown();CHECK(f.water->Initialise(&f.device,&f.backend));
    const auto reinitialised=f.water->Get_Test_Fft_State();
    CHECK(!reinitialised.ready);CHECK_EQ(reinitialised.time,0.0f);CHECK_EQ(reinitialised.draws,0u);
    CHECK_EQ(reinitialised.copies,0u);CHECK_EQ(reinitialised.updates,0u);
    CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));
    CHECK(f.water->Prepare_Test_Fft(1.375f));const auto reused=f.water->Get_Test_Fft_State();
    CHECK(reused.ready);CHECK_EQ(reused.draws,26u);CHECK_EQ(reused.copies,102u);CHECK_EQ(reused.updates,1u);
    WaterDx11FftTest::ReportState("same-owner-reinitialised-first-update",reused);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

