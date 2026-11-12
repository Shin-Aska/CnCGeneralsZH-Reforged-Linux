/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
// Fresh-process probe: compare baked shader literals with the real SDL tuning block.
#include "Platform/WaterFftSampling.h"
#include "SdlConstants.h"
#include <fstream>
#include <stdexcept>

static void Require(bool condition,const char *message) {
    if(!condition)throw std::runtime_error(message);
}
static void Read(const std::string &source,const char *prefix,float *values,unsigned count) {
    const auto at=source.find(prefix);Require(at!=std::string::npos,prefix);
    const char *text=source.c_str()+at+std::strlen(prefix);
    for(unsigned i=0;i<count;++i) {
        char *end=nullptr;values[i]=std::strtof(text,&end);
        Require(end!=text && std::isfinite(values[i]),prefix);
        text=end;if(i+1<count){Require(*text==',',prefix);++text;}
    }
}
int main(int argc,char **argv) {
    try {
        Require(argc==3,"usage: water_clean_defaults_probe snapshot map-angle|fallback");
        Require(WaterCleanSpectrum::Enabled(),"clean mode must be enabled");
        if(std::strcmp(argv[2],"fallback")!=0)WaterTuning::SetMapDirection(std::atof(argv[2]),.8,true);
        if(WaterTuning::Enabled()) {
            // A missing/ignored file must fail this comparison, even though its
            // intended values now equal startup defaults. Poison every key first.
            WaterTuning::Values sentinel;
            for(int i=0;i<WaterTuning::Count;++i) {
                const auto &key=WaterTuning::Keys[i];
                sentinel.value[i]=key.initial==key.low?key.high:key.low;
            }
            WaterTuning::Apply(sentinel);
            WaterTuning::Watcher watcher;watcher.Poll(std::getenv("ZH_USER_DATA_DIR"));
        }
        Require(WaterCleanShore::Enabled(),"default clean shore/heading tile path must be compiled");
        std::ofstream out(argv[1],std::ios::binary);Require(bool(out),"open snapshot");
        const auto write=[&](const auto &value){out.write(reinterpret_cast<const char*>(&value),sizeof(value));};
        for(unsigned resolution:{64u,128u,256u}) {
            const auto config=WaterFftSpectrum::ForResolution(resolution);
            const auto spectrum=WaterFftSpectrum::InitialSpectrum(config);
            out.write(reinterpret_cast<const char*>(spectrum.data()),spectrum.size()*sizeof(float));
            SdlWaterTuningConstants data{};
            WaterCleanShader::SetDrawValues(data,WaterTuning::Configured());
            const auto rms=WaterFftSpectrum::SpectrumHeightRms(spectrum);
            const auto &variance=WaterFftFiltering::SpectrumVariance(config);
            for(unsigned c=0;c<3;++c) {
                data.WaterTuningRms[c]=rms[c];
                for(unsigned l=0;l<9;++l)data.WaterTuningVariance[l][c]=variance[l][c];
            }
            const auto vertex=WaterCleanShader::VertexSource(),pixel=WaterCleanShader::PixelSource(config);
            const auto shore=WaterCleanShore::Source();
            if(WaterTuning::Enabled()) {
                Require(vertex.find("float3(-WaterTuningChop.x,-WaterTuningChop.x,1)")!=std::string::npos,"live long chop");
                Require(pixel.find("fftCleanBand(value,samples.b,samples.bd,-WaterTuningChop.y,mediumGain)")!=std::string::npos,"live short chop");
                Require(pixel.find("#define fftCleanLook WaterTuningLook.xyz")!=std::string::npos,"live look");
                Require(shore.find("#define fftHeadingFollow WaterTuningChop.z")!=std::string::npos,"live heading");
            } else {
                // Decode the constants actually emitted into the nonlive shaders.
                float chop[2];
                Read(vertex,"a.xzy*float3(-",chop,1);data.WaterTuningChop[0]=chop[0];
                Read(vertex,"b.xzy*float3(-",chop,1);data.WaterTuningChop[1]=chop[0];
                Read(pixel,"longGain=fftMediumGain(depth,",data.WaterTuningDepth,1);
                Read(pixel,"mediumGain=fftMediumGain(depth,",data.WaterTuningDepth+1,1);
                Read(pixel,"fineGain=fftMediumGain(depth,",data.WaterTuningDepth+2,1);
                Read(pixel,"fftCleanRms=float3(",data.WaterTuningRms,3);
                Read(pixel,"fftCleanLook=float3(",data.WaterTuningLook,3);
                Read(shore,"fftHeadingFollow=",data.WaterTuningChop+2,1);
                Read(shore,"fftShoreSettings=float4(",data.WaterTuningShore,4);
                Read(shore,"fftShoreMaxTurn=",data.WaterTuningHeading+3,1);
                Require(shore.find("fftShoreLong=true;")!=std::string::npos && shore.find("fftShoreFine=true;")!=std::string::npos,"default all shore bands");
                data.WaterTuningHeading[2]=2;
                Require(shore.find("cos(FftParameters.w+0.7853981633974483)")!=std::string::npos,"baked heading offset");
                const auto source=WaterFftFiltering::Source(config);
                auto at=source.find("={\n");Require(at!=std::string::npos,"baked slope variance");
                for(unsigned l=0;l<config.levels;++l) {
                    at=source.find("float3(",at+1);Require(at!=std::string::npos,"variance row");
                    Read(source.substr(at),"float3(",data.WaterTuningVariance[l],3);
                }
            }
            write(data);
        }
        const float phase=WaterFftSampling::PhaseTime(10);write(phase);
        const auto values=WaterTuning::Serialize(WaterTuning::Configured());out<<values;
        Require(bool(out),"write snapshot");
        return 0;
    } catch(const std::exception &e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
