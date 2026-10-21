#pragma once

TEST(fft_prepared_patch_time_lifetime)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    FftVertex vertices[4];geometry(f,vertices,0);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.request.inputs.surface.waterParameters.x=1.375f;
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    WaterRenderBridge::PreparedPatch a={};CHECK_EQ(f.water->PreparePatch(f.request,a),WaterRenderBridge::PrepareStatus::Ready);
    const auto original=f.water->Get_Test_Fft_State();CHECK(original.ready);
    CHECK(f.water->Prepare_Test_Fft(2.375f));
    const auto next=f.water->Get_Test_Fft_State();CHECK_NE(next.time,original.time);
    DrawCounter count(f);const auto receipt=f.water->SubmitPreparedPatch(a);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Refused);CHECK_EQ(receipt.commandsIssued,0u);CHECK_EQ(count.End(),0u);
    std::printf("  fft lifetime prepared-phase=%.9g current-phase=%.9g stale-refused=%u commands=%u\n",double(original.time),double(next.time),receipt.status==WaterRenderBridge::SubmitStatus::Refused,receipt.commandsIssued);
    f.request.patchId+=1;f.request.inputs.surface.waterParameters.x=2.375f;
    WaterRenderBridge::PreparedPatch retryStale={};
    CHECK_EQ(f.water->PreparePatch(f.request,retryStale),WaterRenderBridge::PrepareStatus::Ready);
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::Draw,2);
    CHECK(!f.water->Prepare_Test_Fft(3.375f));
    f.water->Set_Test_Fft_Failure(DX11WaterClass::FftTestFailure::None,0);
    CHECK(f.water->Prepare_Test_Fft(4.375f));
    DrawCounter retryCount(f);const auto staleReceipt=f.water->SubmitPreparedPatch(retryStale);
    CHECK_EQ(staleReceipt.status,WaterRenderBridge::SubmitStatus::Refused);
    CHECK_EQ(staleReceipt.commandsIssued,0u);CHECK_EQ(retryCount.End(),0u);
    std::printf("  fft lifetime failed-partial-update+retry original-patch refused before surface emission\n");
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_invalid_eligibility_and_indices_preflight)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    FftVertex original[4];geometry(f,original,0);
    f.request.fftEligibility=static_cast<WaterRenderBridge::FftEligibility>(255);
    WaterRenderBridge::PreparedPatch prepared={};const auto before=f.water->Get_Test_Fft_State();
    CHECK_NE(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!prepared.complete);CHECK_EQ(f.water->Get_Test_Fft_State().draws,before.draws);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    std::vector<FftVertex> vertices(65535,original[0]);
    vertices[65532]=original[0];vertices[65533]=original[1];vertices[65534]=original[2];
    unsigned short indices[3]={65532,65533,65534};
    f.batches[0].vertices=vertices.data();f.batches[0].vertexBytes=vertices.size()*sizeof(FftVertex);
    f.batches[0].vertexCount=65535;f.batches[0].indices=indices;f.batches[0].indexCount=3;
    f.batches[0].indexBytes=sizeof(indices);f.batches[0].primitiveCount=1;
    CHECK_EQ(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(prepared.complete);
    const auto valid=f.water->Get_Test_Fft_State();
    indices[2]=65535;f.request.patchId+=1;f.request.inputs.surface.waterParameters.x=9.375f;
    DrawCounter invalidCount(f);
    CHECK_NE(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!prepared.complete);CHECK_EQ(invalidCount.End(),0u);
    CHECK_EQ(f.water->Get_Test_Fft_State().draws,valid.draws);
    vertices.push_back(original[2]);f.batches[0].vertices=vertices.data();
    f.batches[0].vertexCount=65536;f.batches[0].vertexBytes=vertices.size()*sizeof(FftVertex);
    f.request.patchId+=1;DrawCounter oversizedCount(f);
    CHECK_NE(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!prepared.complete);CHECK_EQ(oversizedCount.End(),0u);
    CHECK_EQ(f.water->Get_Test_Fft_State().draws,valid.draws);
    f.batches[0].vertexCount=65535;indices[2]=65534;
    vertices[65532].x=std::numeric_limits<float>::quiet_NaN();
    const std::vector<FftVertex> bytesBefore=vertices;
    f.request.patchId+=1;DrawCounter nanCount(f);
    CHECK_NE(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!prepared.complete);CHECK_EQ(nanCount.End(),0u);
    CHECK_EQ(f.water->Get_Test_Fft_State().draws,valid.draws);
    CHECK_MEM(vertices.data(),bytesBefore.data(),vertices.size()*sizeof(FftVertex));
    vertices[65532].x=original[0].x;
    f.batches[0].indexCount=4;f.request.patchId+=1;
    CHECK_NE(f.water->PreparePatch(f.request,prepared),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(!prepared.complete);CHECK_EQ(f.water->Get_Test_Fft_State().draws,valid.draws);
    std::printf("  fft index-preflight valid-count=65535 max-valid-index=65534 invalid-index=65535 oversized-count=65536 all-rejections-before-draw\n");
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(fft_river_grid_analytic_no_update)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    for(unsigned river=0;river<2;++river){
        CHECK(f.Scene());FftVertex vertices[4];geometry(f,vertices,0);
        if(!river){
            for(unsigned i=0;i<4;++i){f.vertices[i].x*=.5f;f.vertices[i].y*=.25f;}
            f.batches[0].vertices=f.vertices;f.batches[0].vertexBytes=sizeof(f.vertices);
            f.batches[0].vertexLayout=0x242;f.batches[0].vertexStride=sizeof(Vertex);
        }
        f.request.inputs.surface.river=river!=0;
        f.request.fftEligibility=WaterRenderBridge::FftEligibility::Ineligible;
        const auto before=f.water->Get_Test_Fft_State();std::vector<Pixel> pixels;
        CHECK(render(f,river?"river-analytic":"grid-analytic",pixels));
        const auto after=f.water->Get_Test_Fft_State();
        CHECK_EQ(after.draws,before.draws);CHECK_EQ(after.copies,before.copies);CHECK_EQ(after.updates,before.updates);
        CHECK(changed(pixels[24*Fixture::Width+32]));CHECK(!changed(pixels[14*Fixture::Width+32]));
    }
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}



TEST(fft_disabled_launch_snapshot_analytic_no_ownership)
{
    struct RestoreEnvironment {
        std::string old;
        RestoreEnvironment():old(std::getenv("ZH_WATER_FFT")?std::getenv("ZH_WATER_FFT"):""){}
        ~RestoreEnvironment(){_putenv_s("ZH_WATER_FFT",old.c_str());}
    } restore;
    CHECK_EQ(_putenv_s("ZH_WATER_FFT","0"),0);
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK_EQ(_putenv_s("ZH_WATER_FFT","1"),0);
    CHECK(f.Scene());FftVertex vertices[4];geometry(f,vertices,0);
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.request.inputs.surface.waterParameters.x=1.375f;
    WaterRenderBridge::PreparedPatch patch={};
    CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    const auto receipt=f.water->SubmitPreparedPatch(patch);
    CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);CHECK(!receipt.coveragePublished);
    const auto state=f.water->Get_Test_Fft_State();CHECK(!state.ready);
    CHECK_EQ(state.draws,0u);CHECK_EQ(state.copies,0u);CHECK_EQ(state.updates,0u);
    std::vector<Pixel> pixels;CHECK(f.Read(pixels));
    if(!pixels.empty()){CHECK(changed(pixels[24*Fixture::Width+32]));CHECK(!changed(pixels[14*Fixture::Width+32]));}
    std::printf("  fft launch-snapshot initial-env=0 later-env=1 owner-still-disabled=1 actual-analytic-issued=%u ownership=%u updates=%llu\n",receipt.commandsIssued,receipt.coveragePublished,static_cast<unsigned long long>(state.updates));
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

