/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterFftFiltering.h"
#include <complex>
#include <cstring>

// A/B experiment only: exercise clean generation explicitly even with its runtime switch unset.
namespace WaterCleanSpectrumTest {
inline void Check() {
    using namespace WaterFftSpectrum;
    // Map heading feeds the actual seeded clean spectrum in live and nonlive
    // modes. Phillips generation stays identical even while an auto map is set.
    const auto savedMap=WaterTuning::MapDirection();
    const auto configuration=ForResolution(128);
    const auto original=InitialSpectrum(configuration,true),phillips=InitialSpectrum(configuration,false);
    const auto revision=WaterTuning::SpectrumRevision();
    WaterTuning::SetMapDirection(90,.8,true);
    const auto automatic=InitialSpectrum(configuration,true);
    const auto &values=WaterTuning::Configured();
    const double heights[]={values[WaterTuning::HeightLong],values[WaterTuning::HeightMedium],values[WaterTuning::HeightFine]};
    CHECK(automatic==CleanInitialSpectrum(configuration,WaterCleanSpectrum::Settings(),WaterCleanSpectrum::SpectrumSplit(),
        heights,-Pi/2));
    CHECK(automatic!=original && WaterTuning::SpectrumRevision()==revision+1);
    CHECK(InitialSpectrum(configuration,false)==phillips);
    WaterTuning::SetMapDirection(90,.01,false);
    CHECK(InitialSpectrum(configuration,true)==original);
    WaterTuning::MapDirection()=savedMap;
    const WaterCleanSpectrum::Parameters defaults;
    for(const char *invalid:{"", "bad", "8,20000", "8,20000,0.7junk", "nan,20000,0.7", "8,inf,0.7",
        "0,20000,0.7", "8,-1,0.7", "8,20000,-1", "8,20000,3"}) {
        auto p=WaterCleanSpectrum::Parse(invalid);
        CHECK(p.windSpeed==defaults.windSpeed && p.fetch==defaults.fetch && p.swell==defaults.swell);
    }
    auto parsed=WaterCleanSpectrum::Parse("12,4000,1.2");
    CHECK(parsed.windSpeed==12 && parsed.fetch==4000 && parsed.swell==1.2);
    CHECK(WaterCleanSpectrum::Parse(nullptr).fetch==defaults.fetch);
    CHECK(WaterCleanSpectrum::ParseLook("nan,0.7,1").sss==WaterCleanSpectrum::Look{}.sss);
    CHECK(WaterCleanSpectrum::ParseLook("0,0,0").glint==0);
    // Independent numerical angular integration and circular width, including long swell.
    const double peak=WaterCleanSpectrum::PeakFrequency(defaults);
    for(double ratio:{0.25,0.75,1.0,2.0,8.0}) {
        double unswelledWidth=0,swelledWidth=0;
        for(double swell:{0.0,0.7,1.5}) {
            auto p=defaults;p.swell=swell;double integral=0,width=0;
            const unsigned steps=16384;
            for(unsigned i=0;i<steps;++i) {
                const double theta=-Pi+(i+0.5)*2*Pi/steps;
                const double d=WaterCleanSpectrum::Spreading(theta,ratio*peak,p)*2*Pi/steps;
                integral+=d;width+=d*(1-std::cos(theta));
            }
            // Midpoint quadrature converges slowly at the unswelled endpoint cusp.
            CHECK_NEAR(integral,1.0,2e-6);
            if(swell==0)unswelledWidth=width;if(swell==0.7)swelledWidth=width;
        }
        CHECK(swelledWidth<unswelledWidth);
    }
    for(double blend:{.25,.75,1.0}) {
        auto p=defaults;p.spread=blend;double integral=0;
        for(unsigned i=0;i<16384;++i)integral+=WaterCleanSpectrum::Spreading(-Pi+(i+.5)*2*Pi/16384,peak,p)*2*Pi/16384;
        CHECK_NEAR(integral,1.0,2e-6);
        CHECK_NEAR(WaterCleanSpectrum::Spreading(Pi,peak,p),blend/(2*Pi),1e-15);
        if(blend==1)CHECK(WaterCleanSpectrum::Spreading(0,peak,p)==WaterCleanSpectrum::Spreading(1.5,peak,p));
    }
    double broadWidth=0,narrowWidth=0;
    for(double exponent:{1.,12.,40.}) {
        auto p=defaults;p.sea2Spread=exponent;double integral=0,width=0;
        for(unsigned i=0;i<16384;++i) {
            const double theta=-Pi+(i+.5)*2*Pi/16384;
            const double weight=WaterCleanSpectrum::SecondaryDensity(p.sea2Wavelength,theta,p)*2*Pi/16384;
            integral+=weight;width+=weight*(1-std::cos(theta));
        }
        CHECK_NEAR(integral,1.,2e-12);
        if(exponent==1)broadWidth=width;if(exponent==40)narrowWidth=width;
    }
    CHECK(narrowWidth<broadWidth*.1);
    auto unswelled=defaults;unswelled.swell=0;
    CHECK(WaterCleanSpectrum::SpreadingExponent(0.5*peak,defaults)-WaterCleanSpectrum::SpreadingExponent(0.5*peak,unswelled)
        >WaterCleanSpectrum::SpreadingExponent(4*peak,defaults)-WaterCleanSpectrum::SpreadingExponent(4*peak,unswelled));
    // JONSWAP peaks near the fetch-derived frequency; fetch lengthens the dominant waves.
    CHECK(WaterCleanSpectrum::Jonswap(peak,defaults)>WaterCleanSpectrum::Jonswap(0.5*peak,defaults));
    CHECK(WaterCleanSpectrum::Jonswap(peak,defaults)>WaterCleanSpectrum::Jonswap(2*peak,defaults));
    CHECK(WaterCleanSpectrum::PeakFrequency({8,40000,0.7})<peak);
    for(unsigned resolution:{128u,256u})for(bool split:{false,true}) {
        // Keep the pre-controls single-sea golden hashes as an explicit legacy override.
        const WaterCleanSpectrum::Parameters defaults{8,20000,.7,0};
        const auto configuration=ForResolution(resolution);
        const auto initial=CleanInitialSpectrum(configuration,defaults,split);
        CHECK(initial==CleanInitialSpectrum(configuration,defaults,split));
        // FNV-1a over explicit float bits, captured from the pre-controls baseline.
        uint64_t hash=14695981039346656037ull;
        for(float f:initial){uint32_t bits;std::memcpy(&bits,&f,4);for(unsigned j=0;j<4;++j){hash^=(bits>>(8*j))&255;hash*=1099511628211ull;}}
        const uint64_t expected=resolution==128?(split?0xe8322fa7da387261ull:0xa86ecb9b7ba91145ull)
            :(split?0xce0408516046cb9dull:0x9cbfae87fd46d6b9ull);
        CHECK(hash==expected);
        auto dormant=defaults;dormant.sea2Angle=-55;dormant.sea2Wavelength=1400;dormant.sea2Spread=37;
        CHECK(initial==CleanInitialSpectrum(configuration,dormant,split));
        for(double share:{.3,.9})for(double spread:{0.,.6,1.}) {
            auto p=defaults;p.sea2Share=share;p.sea2Angle=0;p.spread=spread;
            const auto mixed=CleanInitialSpectrum(configuration,p,split);
            CHECK(mixed==CleanInitialSpectrum(configuration,p,split));
            const auto mixedRms=SpectrumHeightRms(mixed);double budget=0;
            for(unsigned c=0;c<Cascades;++c) {
                budget+=mixedRms[c]*mixedRms[c];
                if(!split)CHECK_NEAR(mixedRms[c],HeightRms[c],HeightRms[c]*2e-7);
                for(unsigned y=0;y<resolution;++y)for(unsigned x=0;x<resolution;++x) {
                    const unsigned at=c*resolution*resolution+y*resolution+x;
                    const unsigned opposite=c*resolution*resolution+((resolution-y)%resolution)*resolution+(resolution-x)%resolution;
                    CHECK(std::isfinite(mixed[at*4]) && std::isfinite(mixed[at*4+1]));
                    CHECK(mixed[at*4]==mixed[opposite*4+2] && mixed[at*4+1]==-mixed[opposite*4+3]);
                }
            }
            CHECK_NEAR(budget,HeightRms[0]*HeightRms[0]+HeightRms[1]*HeightRms[1]+HeightRms[2]*HeightRms[2],1e-6);
            if(!split && spread==0)CHECK(std::equal(initial.begin()+2*resolution*resolution*4,initial.end(),mixed.begin()+2*resolution*resolution*4));
        }
        // Independently check the power-mixture law on the normalized seeded lattice.
        auto second=defaults;second.sea2Share=1;second.sea2Angle=0;
        const auto pure=CleanInitialSpectrum(configuration,second,split);
        second.sea2Share=.4;const auto blend=CleanInitialSpectrum(configuration,second,split);
        double mixtureError=0,mixturePower=0,forward=0,backward=0;
        for(unsigned c=0;c<2;++c)for(unsigned y=1;y<resolution;++y)for(unsigned x=1;x<resolution;++x) {
            const size_t at=(c*resolution*resolution+y*resolution+x)*4;
            const auto power=[&](const std::vector<float> &v){return double(v[at])*v[at]+double(v[at+1])*v[at+1];};
            const double wanted=.6*power(initial)+.4*power(pure);
            mixtureError+=std::abs(power(blend)-wanted);mixturePower+=wanted;
            // Evolved height covariance at x +/- d after dt: the shader uses
            // exp(+i omega t), and the inverse FFT uses exp(+i k.x).
            const double kx=2*Pi*(int(x)-int(resolution/2))/Sizes[c];
            const double ky=2*Pi*(int(y)-int(resolution/2))/Sizes[c];
            const double omega=std::sqrt(9.81*std::hypot(kx,ky)),dt=.3,d=12.;
            forward+=power(pure)*std::cos(omega*dt+kx*d/WorldScale);
            backward+=power(pure)*std::cos(omega*dt-kx*d/WorldScale);
        }
        CHECK(mixtureError/mixturePower<3e-7);CHECK(forward>backward);
        // A longer requested swell moves the long-band energy to longer waves.
        double meanWavelength[2]={};
        for(unsigned trial=0;trial<2;++trial) {
            auto p=defaults;p.sea2Share=1;p.sea2Wavelength=trial?1200:300;
            const auto wave=CleanInitialSpectrum(configuration,p,split);double weight=0;
            for(unsigned y=1;y<resolution;++y)for(unsigned x=1;x<resolution;++x) {
                const size_t at=(y*resolution+x)*4;
                const double power=double(wave[at])*wave[at]+double(wave[at+1])*wave[at+1];
                const double wavelength=Sizes[0]*WorldScale/std::hypot(int(x)-int(resolution/2),int(y)-int(resolution/2));
                if(std::isfinite(wavelength)){weight+=power;meanWavelength[trial]+=power*wavelength;}
            }
            meanWavelength[trial]/=weight;
        }
        CHECK(meanWavelength[1]>meanWavelength[0]*2);
        const auto rms=SpectrumHeightRms(initial);double total=0,target=0;
        const unsigned n=resolution,texels=n*n;bool hermitian=true,special=true,finite=true;
        for(unsigned c=0;c<Cascades;++c) {
            total+=rms[c]*rms[c];target+=HeightRms[c]*HeightRms[c];
            if(!split)CHECK_NEAR(rms[c],HeightRms[c],HeightRms[c]*2e-7);
            for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x) {
                const float *h=&initial[(c*texels+y*n+x)*4];
                const float *opposite=&initial[(c*texels+((n-y)%n)*n+(n-x)%n)*4];
                hermitian=hermitian && h[0]==opposite[2] && h[1]==-opposite[3];
                for(unsigned j=0;j<4;++j) {
                    finite=finite && std::isfinite(h[j]);
                    if(x==0 || y==0 || (x==n/2 && y==n/2))special=special && h[j]==0;
                }
            }
            // Independently evolve the packed field: conjugate symmetry at several times.
            for(double time:{0.0,2.75,61.0}) {
                double error=0;
                for(unsigned y=1;y<n;++y)for(unsigned x=1;x<n;++x) {
                    const float *h=&initial[(c*texels+y*n+x)*4];
                    const float *other=&initial[(c*texels+((n-y)%n)*n+(n-x)%n)*4];
                    const double k=2*Pi*std::hypot(int(x)-int(n/2),int(y)-int(n/2))/Sizes[c];
                    auto phase=std::polar(1.0,std::sqrt(9.81*k)*time);
                    auto value=std::complex<double>(h[0],h[1])*phase+std::complex<double>(h[2],h[3])*std::conj(phase);
                    auto opposite=std::complex<double>(other[0],other[1])*phase+std::complex<double>(other[2],other[3])*std::conj(phase);
                    error=(std::max)(error,std::abs(value-std::conj(opposite)));
                }
                CHECK(error<1e-12);
            }
        }
        CHECK(hermitian && special && finite);CHECK_NEAR(total,target,target*3e-7);
        const auto variance=WaterFftFiltering::UnresolvedSlopeVariance(initial,configuration);
        for(unsigned c=0;c<Cascades;++c) {
            CHECK(variance[0][c]==0);CHECK(variance[configuration.phases][c]>0);
            for(unsigned l=1;l<configuration.levels;++l)CHECK(variance[l][c]>=variance[l-1][c]);
        }
        printf("  clean %u %s RMS %.9g / %.9g / %.9g; slope variance %.9g / %.9g / %.9g\n",
            resolution,split?"spectrum":"fixed",rms[0],rms[1],rms[2],variance[configuration.phases][0],variance[configuration.phases][1],variance[configuration.phases][2]);
    }
}
}
