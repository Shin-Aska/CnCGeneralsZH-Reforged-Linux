#pragma once

namespace WaterDx11FftTest {
inline void FailureSweep(DX11WaterClass::FftTestFailure stage,const char *name)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    const std::vector<float> initial=SparseInitial(true);
    CHECK(f.water->Set_Test_Fft_Initial_Spectra(initial.data(),initial.size()));
    CHECK(f.water->Prepare_Test_Fft(1.375f));
    const auto complete=f.water->Get_Test_Fft_State();
    const unsigned count=stage==DX11WaterClass::FftTestFailure::Allocation?complete.allocationCount:
        stage==DX11WaterClass::FftTestFailure::Compile?complete.compileCount:
        stage==DX11WaterClass::FftTestFailure::Draw?26:102;
    const auto independent=IndependentInputs(initial);
    std::array<std::array<double,8>,3> golden;
    const double periods[3]={450,103,13};
    for(unsigned cascade=0;cascade<3;++cascade)golden[cascade]=WaterFftReference::EvaluatePoint(independent[cascade],256,periods[cascade],double(1.375f*.4f),13,71);
    CHECK(count>0);CHECK(count<256);
    if(count==0||count>=256)return;
    ID3D11Buffer *sentinelConstant=NULL;ID3D11SamplerState *sentinelSampler=NULL;
    CHECK(SeedContext(f,sentinelConstant,sentinelSampler));
    if(!sentinelConstant||!sentinelSampler){Release(sentinelConstant);Release(sentinelSampler);return;}
    for(unsigned ordinal=1;ordinal<=count;++ordinal){
        CHECK(f.water->Set_Test_Fft_Initial_Spectra(initial.data(),initial.size()));
        f.water->Set_Test_Fft_Failure(stage,ordinal);
        const auto before=f.water->Get_Test_Fft_State();const auto context=FullContext(f.device.Get_Context());
        const unsigned snapshots=f.water->Snapshot_Count();
        CHECK(!f.water->Prepare_Test_Fft(1.375f));
        const auto failed=f.water->Get_Test_Fft_State();
        CHECK(!failed.ready);CHECK_EQ(failed.time,before.time);CHECK_EQ(failed.updates,before.updates);
        CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));
        for(unsigned map=0;map<6;++map)CHECK_EQ(f.water->Get_Test_Fft_Map(map),static_cast<ID3D11ShaderResourceView *>(NULL));
        CHECK(FullContext(f.device.Get_Context())==context);CHECK_EQ(f.water->Snapshot_Count(),snapshots);
        std::printf("  fft failure stage=%s ordinal=%u/%u ready=%u updates-before=%llu updates-after=%llu unpublished=%u\n",
            name,ordinal,count,failed.ready,static_cast<unsigned long long>(before.updates),static_cast<unsigned long long>(failed.updates),f.water->Get_Test_Fft_Array()==NULL);std::fflush(stdout);
        FftVertex vertices[4];geometry(f,vertices,0);
        f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
        f.request.inputs.surface.waterParameters.x=1.375f;f.request.patchId+=1;
        WaterRenderBridge::PreparedPatch degraded={};
        CHECK_EQ(f.water->PreparePatch(f.request,degraded),WaterRenderBridge::PrepareStatus::Ready);
        const auto receipt=f.water->SubmitPreparedPatch(degraded);
        CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);
        CHECK_EQ(receipt.commandsIssued,1u);CHECK(!receipt.coveragePublished);
        std::vector<Pixel> analytic;CHECK(f.Read(analytic));
        if(!analytic.empty())CHECK(changed(analytic[24*Fixture::Width+32]));
        std::printf("  fft failure stage=%s ordinal=%u analytic-issued=%u ownership=%u\n",name,ordinal,receipt.commandsIssued,receipt.coveragePublished);
        f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::None,0);
        CHECK(f.water->Prepare_Test_Fft(1.375f));CHECK(f.water->Get_Test_Fft_State().ready);
        for(unsigned layer=0;layer<6;++layer){Image recovered;
            const bool read=Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Array(),layer,0,recovered);CHECK(read);
            if(read)for(unsigned channel=0;channel<4;++channel)CHECK(WaterFftReference::WithinFp32(recovered.pixels[(71*256+13)*4+channel],golden[layer/2][(layer%2)*4+channel]));
        }
    }
    f.device.Get_Context()->ClearState();f.backend.Forget_Bindings();Release(sentinelConstant);Release(sentinelSampler);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}
}
TEST(fft_failure_each_allocation)
{
    WaterDx11FftTest::FailureSweep(DX11WaterClass::FftTestFailure::Allocation,"allocation");
}
TEST(fft_failure_each_compile)
{
    WaterDx11FftTest::FailureSweep(DX11WaterClass::FftTestFailure::Compile,"compile");
}
TEST(fft_failure_each_draw)
{
    WaterDx11FftTest::FailureSweep(DX11WaterClass::FftTestFailure::Draw,"draw");
}
TEST(fft_failure_each_copy)
{
    WaterDx11FftTest::FailureSweep(DX11WaterClass::FftTestFailure::Copy,"copy");
}



TEST(fft_unsupported_format_preserves_analytic_core)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    FftVertex vertices[4];geometry(f,vertices,0);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::FormatUnsupported,0);
    WaterRenderBridge::PreparedPatch patch={};
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    const auto receipt=f.water->SubmitPreparedPatch(patch);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);CHECK(!receipt.coveragePublished);
    CHECK(f.water->GetCapability().Available());CHECK(!f.water->Get_Test_Fft_State().ready);
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,0u);CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));
    std::vector<Pixel> pixels;CHECK(f.Read(pixels));if(!pixels.empty())CHECK(changed(pixels[24*Fixture::Width+32]));
    std::printf("  fft simulated unsupported-format core-available=1 analytic-issued=%u ownership=%u ready=%u\n",receipt.commandsIssued,receipt.coveragePublished,f.water->Get_Test_Fft_State().ready);
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::None,0);
    CHECK(f.water->Prepare_Test_Fft(1.375f));CHECK(f.water->Get_Test_Fft_State().ready);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_simulated_device_loss_refuses_frame_recovers_next)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    FftVertex vertices[4];geometry(f,vertices,0);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::DeviceLostDuringFft,2);
    WaterRenderBridge::PreparedPatch patch={};
    DrawCounter counter(f);
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Unavailable);
    CHECK(!patch.complete);CHECK_EQ(counter.End(),1u);
    const auto failed=f.water->Get_Test_Fft_State();CHECK(failed.deviceLost);CHECK(!failed.ready);CHECK_EQ(failed.updates,0u);
    CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));CHECK_EQ(f.water->Snapshot_Count(),0u);
    DrawCounter sameFrame(f);
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Unavailable);
    CHECK_EQ(sameFrame.End(),0u);
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::None,0);
    CHECK(f.Scene());geometry(f,vertices,0);f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);
    CHECK_EQ(receipt.commandsIssued,1u);CHECK(f.water->Get_Test_Fft_State().ready);CHECK(!f.water->Get_Test_Fft_State().deviceLost);
    std::printf("  fft simulated-device-loss drawOrdinal=2 partial-fullscreen-primitives=1 surface-emissions=0 same-frame-refused=1 next-frame-issued=%u; no physical driver reset claimed\n",receipt.commandsIssued);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_surface_pipeline_failure_analytic_fallback)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    FftVertex vertices[4];geometry(f,vertices,0);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.request.inputs.surface.waterParameters.x=1.375f;
    CHECK(f.water->Prepare_Test_Fft(1.375f));const auto ready=f.water->Get_Test_Fft_State();CHECK(ready.ready);
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::SurfacePipeline,0);
    WaterRenderBridge::PreparedPatch patch={};
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!patch.packet.resources.fftAvailable);
    const auto receipt=f.water->SubmitPreparedPatch(patch);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);CHECK(!receipt.coveragePublished);
    CHECK(f.water->GetCapability().Available());CHECK(f.water->Get_Test_Fft_State().ready);
    CHECK_EQ(f.water->Get_Test_Fft_State().draws,ready.draws);CHECK_EQ(f.water->Get_Test_Fft_State().copies,ready.copies);
    std::vector<Pixel> pixels;CHECK(f.Read(pixels));
    if(!pixels.empty()){CHECK(changed(pixels[24*Fixture::Width+32]));CHECK(!changed(pixels[14*Fixture::Width+32]));}
    std::printf("  fft simulated-surface-pipeline-failure actual-analytic-issued=%u ownership=%u fields-remain-ready=%u FFTpasses-reused=%llu\n",receipt.commandsIssued,receipt.coveragePublished,f.water->Get_Test_Fft_State().ready,static_cast<unsigned long long>(ready.draws));
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::None,0);
    CHECK(f.Scene());geometry(f,vertices,0);f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.request.inputs.surface.waterParameters.x=1.375f;
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);CHECK(patch.packet.resources.fftAvailable);
    CHECK_EQ(f.water->Get_Test_Fft_State().draws,ready.draws);CHECK_EQ(f.water->Get_Test_Fft_State().copies,ready.copies);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

