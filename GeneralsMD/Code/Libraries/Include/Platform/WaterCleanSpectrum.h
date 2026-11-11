/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "Platform/WaterTuning.h"
#include "Platform/WaterCleanOpt.h"

// A/B experiment only: immutable clean-ocean settings, independent of the Phillips path.
namespace WaterCleanSpectrum {
constexpr double Pi=3.14159265358979323846, Gravity=9.81;
struct Parameters {
    double windSpeed=WaterTuning::Keys[WaterTuning::WindSpeed].initial,
        fetch=WaterTuning::Keys[WaterTuning::Fetch].initial, swell=WaterTuning::Keys[WaterTuning::Swell].initial;
    double sea2Share=WaterTuning::Keys[WaterTuning::Sea2Share].initial,
        sea2Angle=WaterTuning::Keys[WaterTuning::Sea2Angle].initial,
        sea2Wavelength=WaterTuning::Keys[WaterTuning::Sea2Wavelength].initial,
        sea2Spread=WaterTuning::Keys[WaterTuning::Sea2Spread].initial, spread=WaterTuning::Keys[WaterTuning::Spread].initial;
};
inline bool Enabled() {
#if defined(__linux__)
    static const bool enabled=[] {const char *v=std::getenv("ZH_WATER_CLEAN");const bool clean=!(v && std::strcmp(v,"0")==0); // default on; ZH_WATER_CLEAN=0 restores the previous Modern water
       if(clean)WaterCleanOpt::Get();return clean;}();
    return enabled;
#else
    return false;
#endif
}
// Metres and metres/second in the reference domain: one metre = WorldScale game units.
// Parse the complete tuple atomically; malformed/nonfinite/out-of-range input uses defaults.
inline Parameters Parse(const char *value) {
    Parameters p;char trailing;
    if(!value || std::sscanf(value,"%lf,%lf,%lf %c",&p.windSpeed,&p.fetch,&p.swell,&trailing)!=3
        || !std::isfinite(p.windSpeed) || p.windSpeed<1 || p.windSpeed>50
        || !std::isfinite(p.fetch) || p.fetch<100 || p.fetch>1000000
        || !std::isfinite(p.swell) || p.swell<0 || p.swell>2) return Parameters{};
    return p;
}
inline const Parameters &Settings() {
    if(WaterTuning::Enabled()) {
        static Parameters live;const auto &v=WaterTuning::Current();
        live={v[WaterTuning::WindSpeed],v[WaterTuning::Fetch],v[WaterTuning::Swell],
            v[WaterTuning::Sea2Share],v[WaterTuning::Sea2Angle],v[WaterTuning::Sea2Wavelength],
            v[WaterTuning::Sea2Spread],v[WaterTuning::Spread]};return live;
    }
    static const Parameters p=[] {
        auto result=Parse(std::getenv("ZH_WATER_CLEAN_SPECTRUM"));
        const auto v=WaterTuning::StartValues();
        result.sea2Share=v[WaterTuning::Sea2Share];result.sea2Angle=v[WaterTuning::Sea2Angle];
        result.sea2Wavelength=v[WaterTuning::Sea2Wavelength];result.sea2Spread=v[WaterTuning::Sea2Spread];
        result.spread=v[WaterTuning::Spread];return result;
    }();return p;
}
inline bool SpectrumSplit() {
    return WaterTuning::Configured()[WaterTuning::Split]!=0;
}
inline double PeakFrequency(const Parameters &p) {return 22.0*std::cbrt(Gravity*Gravity/(p.windSpeed*p.fetch));}
inline double Jonswap(double omega,const Parameters &p) {
    if(!(omega>0))return 0;
    const double peak=PeakFrequency(p),sigma=omega<=peak?0.07:0.09;
    const double r=std::exp(-0.5*std::pow((omega-peak)/(sigma*peak),2));
    const double alpha=0.076*std::pow(p.windSpeed*p.windSpeed/(Gravity*p.fetch),0.22);
    return alpha*Gravity*Gravity*std::pow(omega,-5)*std::exp(-1.25*std::pow(peak/omega,4))*std::pow(3.3,r);
}
// Hasselmann/Longuet-Higgins, Horvath 2015: s_xi=16*tanh(wp/w)*xi^2.
// Exact log-gamma normalization over [-pi,pi], rather than an approximate fit.
inline double SpreadingExponent(double omega,const Parameters &p) {
    const double peak=PeakFrequency(p),ratio=omega/peak;
    const double s=omega<=peak ? 6.97*std::pow(ratio,4.06)
        : 9.77*std::pow(ratio,-2.33-1.45*(p.windSpeed*peak/Gravity-1.17));
    return s+16.0*std::tanh(peak/omega)*p.swell*p.swell;
}
inline double Spreading(double theta,double omega,const Parameters &p) {
    if(!(omega>0))return 0;
    const double s=SpreadingExponent(omega,p);
    const double cosine=std::abs(std::cos(theta*0.5));
    if(cosine<1e-15)return p.spread==0?0:p.spread/(2*Pi);
    const double directional=std::exp(std::lgamma(s+1)-std::lgamma(s+0.5)-std::log(2*std::sqrt(Pi))+2*s*std::log(cosine));
    // Preserve the original arithmetic exactly when broadening is disabled.
    return p.spread==0?directional:(1-p.spread)*directional+p.spread/(2*Pi);
}
// Secondary swell uses a log-normal wavelength envelope and normalized cos^(2s)
// half-angle directional density. Wavelength is expressed in game-world units.
inline double SecondaryDensity(double wavelength,double theta,const Parameters &p) {
    const double cosine=std::abs(std::cos(theta*.5));
    if(cosine<1e-15 || !(wavelength>0))return 0;
    const double radial=std::log(wavelength/p.sea2Wavelength)/.35;
    return std::exp(-.5*radial*radial+std::lgamma(p.sea2Spread+1)
        -std::lgamma(p.sea2Spread+.5)-std::log(2*std::sqrt(Pi))
        +2*p.sea2Spread*std::log(cosine));
}
struct Look {
    double sss=WaterTuning::Keys[WaterTuning::CrestColor].initial,
        foamThreshold=WaterTuning::Keys[WaterTuning::FoamThreshold].initial,
        glint=WaterTuning::Keys[WaterTuning::Glint].initial;
};
inline Look ParseLook(const char *value) {
    Look p;char trailing;
    if(!value || std::sscanf(value,"%lf,%lf,%lf %c",&p.sss,&p.foamThreshold,&p.glint,&trailing)!=3
        || !std::isfinite(p.sss) || p.sss<0 || p.sss>1
        || !std::isfinite(p.foamThreshold) || p.foamThreshold<0 || p.foamThreshold>1
        || !std::isfinite(p.glint) || p.glint<0 || p.glint>4)return Look{};
    return p;
}
inline const Look &Appearance() {static const Look look=ParseLook(std::getenv("ZH_WATER_CLEAN_LOOK"));return look;}
}
