#pragma once
#include "WaterDx11FftReadback.h"

namespace WaterDx11FftTest {
inline WaterFftReference::Pyramid SparseOracle(const WaterFftReference::InitialSpectrum &initial,
    double period,double phase)
{
    const unsigned side=256;
    const auto fields=WaterFftReference::Evolve(initial,side,period,phase);
    WaterFftReference::LayerPair base;
    for(auto &map:base){map.side=side;map.pixels.resize(std::size_t(side)*side*4);}
    for(unsigned field=0;field<8;++field){
        std::vector<unsigned> populated;
        for(unsigned at=0;at<side*side;++at)
            if(fields[field][at].real!=0||fields[field][at].imaginary!=0)populated.push_back(at);
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x){
            WaterFftReference::Complex sum;
            for(unsigned at:populated){
                const int kx=int(at%side)-128,ky=int(at/side)-128;
                const double angle=2*WaterFftReference::Pi*(double(kx)*x+double(ky)*y)/side;
                sum=WaterFftReference::Add(sum,WaterFftReference::Multiply(fields[field][at],WaterFftReference::Unit(angle)));
            }
            CHECK(std::fabs(sum.imaginary)<=1e-9*(1+std::fabs(sum.real)));
            base[field/4].pixels[(std::size_t(y)*side+x)*4+field%4]=sum.real;
        }
    }
    const unsigned points[][2]={{0,0},{13,71},{129,255}};
    for(const auto &point:points){
        const auto expected=WaterFftReference::EvaluatePoint(initial,side,period,phase,point[0],point[1]);
        for(unsigned field=0;field<8;++field)CHECK_NEAR(base[field/4].pixels[(point[1]*side+point[0])*4+field%4],expected[field],1e-10);
    }
    return WaterFftReference::BuildMipPyramid(base);
}

inline WaterFftReference::Pyramid ProductionMipOracle(WaterDx11SurfaceTest::Fixture &f,
    const WaterFftReference::InitialSpectrum &initial,unsigned cascade,double period,double phase)
{
    WaterFftReference::LayerPair base;
    for(unsigned map=0;map<2;++map){
        Image actual;const bool ok=Read(f.device.Get_Device(),f.device.Get_Context(),f.water->Get_Test_Fft_Map(cascade*2+map),0,0,actual);
        CHECK(ok);if(!ok)return {};
        base[map].side=actual.side;base[map].pixels.assign(actual.pixels.begin(),actual.pixels.end());
    }
    const unsigned points[][2]={{0,0},{1,0},{0,1},{13,71},{128,128},{129,255},{255,255}};
    for(const auto &point:points){
        const auto expected=WaterFftReference::EvaluatePoint(initial,256,period,phase,point[0],point[1]);
        for(unsigned field=0;field<8;++field){
            const double actual=base[field/4].pixels[(point[1]*256+point[0])*4+field%4];
            std::printf("  fft production-independent-point cascade=%u xy=%u,%u field=%u actual=%.17g expected=%.17g\n",cascade,point[0],point[1],field,actual,expected[field]);
            CHECK(WaterFftReference::WithinFp32(actual,expected[field]));
        }
    }
    std::printf("  fft production mip oracle uses readback base only after independent selected-base checks; full-base DFT not claimed\n");
    return WaterFftReference::BuildMipPyramid(base);
}
}
