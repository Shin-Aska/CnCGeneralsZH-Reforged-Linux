#pragma once

namespace WaterDx11FftTest {
inline std::array<double,8> SampleIndependent(const WaterFftReference::InitialSpectrum &initial,
    double period,double phase,double worldX,double worldY)
{
    const double tx=worldX/(period*20)*256-.5,ty=worldY/(period*20)*256-.5;
    const int ix=int(std::floor(tx)),iy=int(std::floor(ty));
    const double fractionX=tx-ix,fractionY=ty-iy;
    std::array<double,8> result={};
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){
        const unsigned px=unsigned(((ix+int(x))%256+256)%256),py=unsigned(((iy+int(y))%256+256)%256);
        const auto point=WaterFftReference::EvaluatePoint(initial,256,period,phase,px,py);
        const double weight=(x?fractionX:1-fractionX)*(y?fractionY:1-fractionY);
        for(unsigned field=0;field<8;++field)result[field]+=point[field]*weight;
    }
    return result;
}

inline std::array<double,4> ExpectedGeneratedClip(const FftVertex &vertex,
    const std::array<WaterFftReference::InitialSpectrum,3> &initial,bool wrongMap)
{
    const double periods[3]={450,103,13},chop[3]={-1.2,-1.5,-1.5};
    double rawX=0,rawY=0,rawZ=0;
    for(unsigned c=0;c<3;++c){
        const auto fields=SampleIndependent(initial[c],periods[c],double(1.375f*.4f),vertex.x,vertex.y);
        const unsigned offset=wrongMap?4:0;
        rawX+=fields[offset]*chop[c]*20;rawY+=fields[offset+2]*chop[c]*20;rawZ+=fields[offset+1]*20;
    }
    const double horizontal=double(WaterFftGeometry::MaxHorizontalDisplacement)/std::sqrt(2.0),vertical=WaterFftGeometry::MaxVerticalDisplacement;
    const double x=vertex.x+horizontal*std::tanh(rawX/horizontal)*vertex.nx*vertex.ny*vertex.nz;
    const double y=vertex.y+horizontal*std::tanh(rawY/horizontal)*vertex.nx*vertex.ny*vertex.nz;
    const double z=vertex.nx>0?vertex.level+(vertex.z-vertex.level)*(1-vertex.nx)
        +vertical*std::tanh(rawZ/vertical)*vertex.ny*vertex.nx:vertex.z;
    return {{x*.01,y*.01+z,(z-2)*-.25,1}};
}
}

TEST(fft_generated_fields_retained_binding_production_vertex_end_to_end)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);CHECK(f.Scene());
    FftVertex vertices[4];geometry(f,vertices,0);
    const float xy[4][2]={{45,30},{45,45},{70,45},{70,30}};
    for(unsigned i=0;i<4;++i){vertices[i].x=xy[i][0];vertices[i].y=xy[i][1];}
    f.request.inputs.scene.projection[0]=.01f;f.request.inputs.scene.projection[5]=.01f;
    f.backend.Set_Transform(D3DTS_PROJECTION,f.request.inputs.scene.projection);
    f.request.inputs.surface.reflectionArea={0,0,100,100};
    f.request.fftEligibility=WaterRenderBridge::FftEligibility::ProjectedFlat;
    f.request.inputs.surface.waterParameters.x=1.375f;
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    const std::vector<float> input=WaterDx11FftTest::SparseInitial(true);
    CHECK(f.water->Set_Test_Fft_Initial_Spectra(input.data(),input.size()));
    const auto before=f.water->Get_Test_Fft_State();
    WaterRenderBridge::PreparedPatch patch={};CHECK_EQ(f.water->PreparePatch(f.request,patch),WaterRenderBridge::PrepareStatus::Ready);
    CHECK(patch.packet.resources.fftAvailable);
    const auto ready=f.water->Get_Test_Fft_State();CHECK(ready.ready);
    CHECK_EQ(ready.draws-before.draws,26u);CHECK_EQ(ready.copies-before.copies,102u);CHECK_EQ(ready.updates-before.updates,1u);
    ID3D11ShaderResourceView *retained[3]={};
    for(unsigned slot=0;slot<3;++slot){retained[slot]=f.water->Get_Test_Prepared_Vertex_Resource(patch.patchId,slot);
        CHECK(retained[slot]!=NULL);CHECK_EQ(retained[slot],f.water->Get_Test_Fft_Map(slot*2));}
    WaterDx11FftTest::SavedPreparedVertex saved;CHECK(saved.Capture(f,patch));
    // Submit through the actual production binding path before replaying the same retained VS+CBs+SRVs.
    const auto receipt=f.water->SubmitPreparedPatch(patch);CHECK_EQ(receipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(receipt.commandsIssued,1u);
    std::vector<Pixel> pixels;CHECK(f.Read(pixels));unsigned rendered=0;
    for(const auto &pixel:pixels){CHECK(std::isfinite(pixel.r)&&std::isfinite(pixel.g)&&std::isfinite(pixel.b));if(changed(pixel))++rendered;}
    CHECK(rendered>0);
    WaterDx11FftTest::CapturedVertex actual[4]={};CHECK(WaterDx11FftTest::CaptureVertices(f,patch,vertices,saved.maps,actual,&saved));
    const auto independent=WaterDx11FftTest::IndependentInputs(input);
    bool disabledRejected=false,wrongBindingRejected=false;
    for(unsigned i=0;i<4;++i){
        const auto expected=WaterDx11FftTest::ExpectedGeneratedClip(vertices[i],independent,false);
        const auto wrong=WaterDx11FftTest::ExpectedGeneratedClip(vertices[i],independent,true);
        const double analytic[4]={vertices[i].x*.01,vertices[i].y*.01,.5,1};
        for(unsigned channel=0;channel<4;++channel){
            CHECK(WaterFftReference::WithinFp32(actual[i].clip[channel],expected[channel]));
            disabledRejected=disabledRejected||!WaterFftReference::WithinFp32(actual[i].clip[channel],analytic[channel]);
            wrongBindingRejected=wrongBindingRejected||!WaterFftReference::WithinFp32(actual[i].clip[channel],wrong[channel]);
        }
        const double actualZ=2-4*double(actual[i].clip[2]),expectedZ=2-4*expected[2];
        const double actualDeltaX=double(actual[i].clip[0])/.01-vertices[i].x,expectedDeltaX=expected[0]/.01-vertices[i].x;
        const double actualDeltaY=(double(actual[i].clip[1])-actualZ)/.01-vertices[i].y,expectedDeltaY=(expected[1]-expectedZ)/.01-vertices[i].y;
        CHECK(WaterFftReference::WithinFp32(actualDeltaX,expectedDeltaX));
        CHECK(WaterFftReference::WithinFp32(actualDeltaY,expectedDeltaY));CHECK(WaterFftReference::WithinFp32(actualZ,expectedZ));
        std::printf("  fft generated-retained-world vertex=%u actual-delta=%.17g,%.17g,%.17g independent-delta=%.17g,%.17g,%.17g\n",i,actualDeltaX,actualDeltaY,actualZ,expectedDeltaX,expectedDeltaY,expectedZ);
        CHECK_EQ(actual[i].originalXY[0],vertices[i].x);CHECK_EQ(actual[i].originalXY[1],vertices[i].y);
        CHECK_EQ(actual[i].metadata[0],1.0f);CHECK_EQ(actual[i].metadata[1],1.0f);CHECK_EQ(actual[i].metadata[2],0.0f);CHECK_EQ(actual[i].metadata[3],1.0f);
        std::printf("  fft generated-retained-VS override=off vertex=%u actual-clip=%.9g,%.9g,%.9g,%.9g independent-clip=%.17g,%.17g,%.17g,%.17g wrong-map-clip=%.17g,%.17g,%.17g,%.17g originalXY=%.9g,%.9g\n",
            i,actual[i].clip[0],actual[i].clip[1],actual[i].clip[2],actual[i].clip[3],expected[0],expected[1],expected[2],expected[3],wrong[0],wrong[1],wrong[2],wrong[3],actual[i].originalXY[0],actual[i].originalXY[1]);
    }
    CHECK(disabledRejected);CHECK(wrongBindingRejected);
    std::printf("  fft generated-retained-VS disabled-displacement-rejected=%u wrong-map-rejected=%u actual-raster-pixels=%u draws=%llu copies=%llu updates=%llu override=off\n",
        disabledRejected,wrongBindingRejected,rendered,static_cast<unsigned long long>(ready.draws-before.draws),static_cast<unsigned long long>(ready.copies-before.copies),static_cast<unsigned long long>(ready.updates-before.updates));
    // Restore the real caller target after streamout, then exercise paused-resize admission.
    f.device.Get_Context()->OMSetRenderTargets(1,&f.colourView,f.depthView);
    D3D11_VIEWPORT viewport={.5f,.5f,float(Fixture::Width),float(Fixture::Height),0,1};
    f.device.Get_Context()->RSSetViewports(1,&viewport);
    WaterRenderBridge::CurrentView viewBefore={};CHECK(f.water->QueryCurrentView(viewBefore));
    ID3D11ShaderResourceView *arrayIdentity=f.water->Get_Test_Fft_Array();
    WaterDx11FftTest::Image prior;CHECK(WaterDx11FftTest::Read(f.device.Get_Device(),f.device.Get_Context(),arrayIdentity,0,0,prior));
    f.water->Before_Resize();f.water->After_Resize();
    const auto resized=f.water->Get_Test_Fft_State();CHECK(resized.ready);CHECK_EQ(resized.time,ready.time);
    CHECK_EQ(f.water->Get_Test_Fft_Array(),arrayIdentity);
    WaterDx11FftTest::Image preserved;CHECK(WaterDx11FftTest::Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Array(),0,0,preserved));
    CHECK_EQ(WaterDx11FftTest::Fingerprint(prior),WaterDx11FftTest::Fingerprint(preserved));
    CHECK(f.water->GetCapability().Available());
    f.request.patchId+=1;WaterRenderBridge::PreparedPatch paused={};
    const auto pausedStatus=f.water->PreparePatch(f.request,paused);
    CHECK_EQ(pausedStatus,WaterRenderBridge::PrepareStatus::Ready);
    WaterRenderBridge::PreparedPatch secondPaused={};const auto secondStatus=f.water->PreparePatch(f.request,secondPaused);
    CHECK_EQ(secondStatus,WaterRenderBridge::PrepareStatus::Ready);
    const auto pausedReceipt=f.water->SubmitPreparedPatch(paused);
    CHECK_EQ(pausedReceipt.status,WaterRenderBridge::SubmitStatus::Issued);CHECK_EQ(pausedReceipt.commandsIssued,1u);
    const auto pausedState=f.water->Get_Test_Fft_State();
    CHECK_EQ(pausedState.draws,ready.draws);CHECK_EQ(pausedState.copies,ready.copies);CHECK_EQ(pausedState.updates,ready.updates);
    WaterRenderBridge::CurrentView viewAfter={};const bool live=f.water->QueryCurrentView(viewAfter);CHECK(live);
    if(live){CHECK_EQ(viewAfter.colourIdentity,viewBefore.colourIdentity);CHECK_EQ(viewAfter.depthIdentity,viewBefore.depthIdentity);
        CHECK_EQ(viewAfter.captureSerial,viewBefore.captureSerial);CHECK(WaterRenderBridge::SameViewState(viewAfter.viewState,viewBefore.viewState));}
    std::printf("  fft paused-resize core-available=%u reason=%u ready=%u phase=%.9g array-preserved=%u hash-preserved=%u prepare=%u repeat-prepare=%u surface-commands=%u live-view=%u capture=%llu passes-delta=%llu,%llu,%llu\n",
        f.water->GetCapability().Available(),unsigned(f.water->GetCapability().reason),pausedState.ready,double(pausedState.time),f.water->Get_Test_Fft_Array()==arrayIdentity,
        WaterDx11FftTest::Fingerprint(prior)==WaterDx11FftTest::Fingerprint(preserved),unsigned(pausedStatus),unsigned(secondStatus),pausedReceipt.commandsIssued,live,static_cast<unsigned long long>(viewAfter.captureSerial),
        static_cast<unsigned long long>(pausedState.draws-ready.draws),static_cast<unsigned long long>(pausedState.copies-ready.copies),static_cast<unsigned long long>(pausedState.updates-ready.updates));
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}



