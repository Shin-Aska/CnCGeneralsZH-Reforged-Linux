#pragma once
#include "WaterDx11FftOracle.h"
#include "Platform/WaterFftSpectrum.h"
#include <limits>

namespace WaterDx11FftTest {
inline void ReportState(const char *label,const DX11WaterClass::FftTestState &s)
{
    std::printf("  fft state=%s ready=%u phase=%.9g draws=%llu copies=%llu updates=%llu generation=%llu allocations=%u compiles=%u lastDraw=%u lastCopy=%u\n",
        label,s.ready,double(s.time),static_cast<unsigned long long>(s.draws),static_cast<unsigned long long>(s.copies),
        static_cast<unsigned long long>(s.updates),static_cast<unsigned long long>(s.generation),
        s.allocationCount,s.compileCount,s.drawCount,s.copyCount);std::fflush(stdout);
}

inline void CompareAll(WaterDx11SurfaceTest::Fixture &f,const std::vector<float> &input,const char *label)
{
    bool wrongSignRejected=false,wrongNormalizationRejected=false,wrongChannelRejected=false,wrongLayerRejected=false;
    const auto initial=IndependentInputs(input);
    const std::array<double,3> periods={{450,103,13}};
    for(unsigned cascade=0;cascade<3;++cascade){
        std::printf("  fft oracle-start=%s cascade=%u rawTime=1.375 phase=0.55 override=off\n",label,cascade);std::fflush(stdout);
        const WaterFftReference::Pyramid expected=std::strcmp(label,"production-seed")==0 ? ProductionMipOracle(f,initial[cascade],cascade,periods[cascade],double(1.375f*.4f)) : SparseOracle(initial[cascade],periods[cascade],double(1.375f*.4f));
        CHECK_EQ(expected.size(),9u);if(expected.size()!=9)return;
        for(unsigned map=0;map<2;++map)for(unsigned mip=0;mip<9;++mip){
            const unsigned layer=WaterFftReference::ArrayLayer(cascade,map);Image actualMap,actualArray;
            const bool mapRead=Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Map(layer),0,mip,actualMap);
            const bool arrayRead=Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Array(),layer,mip,actualArray);
            CHECK(mapRead);CHECK(arrayRead);
            if(mapRead)CHECK(Compare(actualMap,expected[mip][map],label,layer,mip));
            if(arrayRead)CHECK(Compare(actualArray,expected[mip][map],"array",layer,mip));
            if(mip==0&&arrayRead){
                const auto &truth=expected[0][map].pixels;
                const auto &otherLayer=expected[0][1-map].pixels;
                for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)for(unsigned channel=0;channel<4;++channel){
                    const std::size_t at=(y*256+x)*4+channel;
                    const std::size_t reversed=(((256-y)%256)*256+(256-x)%256)*4+channel;
                    const std::size_t rotated=(y*256+x)*4+(channel+1)%4;
                    const double actual=actualArray.pixels[at];
                    auto reject=[&](bool &observed,double wrong,const char *reason){
                        if(!observed&&!WaterFftReference::WithinFp32(actual,wrong)){
                            observed=true;std::printf("  fft negative-witness case=%s mutation=%s layer=%u xy=%u,%u channel=%u actual=%.17g wrong=%.17g\n",label,reason,layer,x,y,channel,actual,wrong);
                        }
                    };
                    reject(wrongSignRejected,truth[reversed],"reversed-DFT-sign");
                    reject(wrongNormalizationRejected,truth[at]/(256.0*256.0),"extra-normalization");
                    reject(wrongChannelRejected,truth[rotated],"rotated-channel");
                    reject(wrongLayerRejected,otherLayer[at],"adjacent-layer");
                }
            }
            if(mapRead&&arrayRead)CHECK_MEM(actualMap.pixels.data(),actualArray.pixels.data(),actualMap.pixels.size()*sizeof(float));
        }
    }
    if(std::strcmp(label,"zero")!=0&&std::strcmp(label,"singular-DC-omitted")!=0){
        CHECK(wrongSignRejected);CHECK(wrongNormalizationRejected);CHECK(wrongChannelRejected);CHECK(wrongLayerRejected);
        std::printf("  fft GPU-readback negative-controls case=%s reversed-DFT-sign-rejected=%u wrong-normalization-rejected=%u rotated-channel-rejected=%u adjacent-layer-rejected=%u\n",
            label,wrongSignRejected,wrongNormalizationRejected,wrongChannelRejected,wrongLayerRejected);
    }else std::printf("  fft GPU-readback negative-controls case=%s not-discriminating: mathematically zero output; covered by signed/asymmetric fixtures\n",label);
}

inline void ExerciseSpectrum(const std::vector<float> &input,const char *label)
{
    WaterDx11SurfaceTest::Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    CHECK(f.water->Set_Test_Fft_Fields(NULL,NULL,NULL,NULL));
    CHECK(f.water->Set_Test_Fft_Initial_Spectra(input.data(),input.size()));
    const auto before=f.water->Get_Test_Fft_State();
    const auto context=WaterDx11SurfaceTest::ContextState(f.device.Get_Context());
    CHECK(f.water->Prepare_Test_Fft(1.375f));
    const auto after=f.water->Get_Test_Fft_State();ReportState(label,after);
    CHECK(after.ready);CHECK_EQ(after.time,1.375f*.4f);
    CHECK_EQ(after.draws-before.draws,26u);CHECK_EQ(after.copies-before.copies,102u);CHECK_EQ(after.updates-before.updates,1u);
    CHECK(WaterDx11SurfaceTest::ContextState(f.device.Get_Context())==context);
    CompareAll(f,input,label);
    const auto repeatedBefore=f.water->Get_Test_Fft_State();
    CHECK(f.water->Prepare_Test_Fft(1.375f));
    const auto repeated=f.water->Get_Test_Fft_State();ReportState("same-time",repeated);
    CHECK_EQ(repeated.draws,repeatedBefore.draws);CHECK_EQ(repeated.copies,repeatedBefore.copies);CHECK_EQ(repeated.updates,repeatedBefore.updates);
    ID3D11ShaderResourceView *identity=f.water->Get_Test_Fft_Array();
    Image prior;CHECK(Read(f.device.Get_Device(),f.device.Get_Context(),identity,0,0,prior));
    f.water->Before_Resize();f.water->After_Resize();
    CHECK_EQ(f.water->Get_Test_Fft_Array(),identity);
    CHECK_EQ(f.water->Get_Test_Fft_State().time,repeated.time);
    Image resized;CHECK(Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Array(),0,0,resized));
    CHECK_EQ(Fingerprint(prior),Fingerprint(resized));
    CHECK_EQ(WaterDx11SurfaceTest::DebugWarnings(f.device.Get_Device()),0u);
}

inline std::vector<float> SparseInitial(bool asymmetric)
{
    const unsigned n=256;std::vector<float> result(std::size_t(3)*n*n*4,0);
    for(unsigned c=0;c<3;++c){
        const unsigned x=129+c,y=130+2*c,oppositeX=n-x,oppositeY=n-y;
        const std::size_t a=(std::size_t(c)*n*n+y*n+x)*4,b=(std::size_t(c)*n*n+oppositeY*n+oppositeX)*4;
        result[a]=.001f*(c+1);result[a+1]=asymmetric?.0007f*(c+1):0;
        result[b+2]=result[a];result[b+3]=-result[a+1];
        if(asymmetric){
            const std::size_t p=(std::size_t(c)*n*n+125*n+132)*4,q=(std::size_t(c)*n*n+131*n+124)*4;
            result[p]=-.0006f*(c+1);result[p+1]=.0002f;result[q+2]=result[p];result[q+3]=-result[p+1];
        }
    }
    return result;
}
}

TEST(fft_production_seed_all_fields_all_mips)
{
    WaterDx11FftTest::ExerciseSpectrum(WaterFftSpectrum::InitialSpectrum(),"production-seed");
}
TEST(fft_zero_fields_all_mips)
{
    WaterDx11FftTest::ExerciseSpectrum(std::vector<float>(std::size_t(3)*256*256*4,0),"zero");
}
TEST(fft_dc_rejection_all_mips)
{
    std::vector<float> input(std::size_t(3)*256*256*4,0);
    for(unsigned c=0;c<3;++c)input[(std::size_t(c)*256*256+128*256+128)*4]=.125f*(c+1);
    WaterDx11FftTest::ExerciseSpectrum(input,"singular-DC-omitted");
}
TEST(fft_signed_frequency_all_fields_all_mips)
{
    WaterDx11FftTest::ExerciseSpectrum(WaterDx11FftTest::SparseInitial(false),"signed-frequency");
}
TEST(fft_asymmetric_fields_all_mips)
{
    WaterDx11FftTest::ExerciseSpectrum(WaterDx11FftTest::SparseInitial(true),"asymmetric");
}
TEST(fft_malformed_initial_and_time)
{
    WaterDx11SurfaceTest::Fixture f;CHECK(f.Initialise());if(!f.water)return;adapter(f);
    CHECK(f.Scene());if(!f.colourSurface)return;
    std::vector<float> input(std::size_t(3)*256*256*4,0);
    CHECK(!f.water->Set_Test_Fft_Initial_Spectra(input.data(),input.size()-1));
    input[170]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!f.water->Set_Test_Fft_Initial_Spectra(input.data(),input.size()));
    const auto before=f.water->Get_Test_Fft_State();
    CHECK(!f.water->Prepare_Test_Fft(std::numeric_limits<float>::quiet_NaN()));
    CHECK(!f.water->Prepare_Test_Fft(std::numeric_limits<float>::infinity()));
    const auto after=f.water->Get_Test_Fft_State();
    CHECK_EQ(after.draws,before.draws);CHECK_EQ(after.copies,before.copies);CHECK_EQ(after.updates,before.updates);
    CHECK(!after.ready);CHECK_EQ(f.water->Get_Test_Fft_Array(),static_cast<ID3D11ShaderResourceView *>(NULL));
    WaterDx11FftTest::ReportState("malformed-rejected",after);
}




