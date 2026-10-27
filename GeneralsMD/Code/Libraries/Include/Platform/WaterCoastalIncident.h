/* GPU-only production-field sampler for the optional coastal history atlas. */
#pragma once
#include "Platform/WaterCoastalSurf.h"
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterShoreFoam.h"
#include "Platform/WaterSurfaceModel.h"
#include <string>

namespace WaterCoastalIncident {
// Domain slots 0..3: world, coast, surface, admission. Slots 4..7 are the existing
// ocean maps 0..3 (broad/medium displacement+derivatives), with the ocean sampler.
// Ocean maps 4/5 are fine ripples and deliberately have no source binding.
constexpr unsigned SamplerCount=8,UniformBytes=16;
struct Parameters {float waterTime=0,nodeCount=0,padding0=0,padding1=0;};
static_assert(sizeof(Parameters)==UniformBytes,"Incident uniform is one float4");
inline const char *VertexSource(){return WaterFftSpectrum::VertexHlsl();}
inline bool Slice(const std::string &source,const char *begin,const char *end,std::string &out){
    const size_t a=source.find(begin),b=a==std::string::npos?std::string::npos:source.find(end,a);
    if(a==std::string::npos||b==std::string::npos||b<=a)return false;
    out=source.substr(a,b-a);return true;
}
inline bool ReplaceOnce(std::string &text,const std::string &old,const std::string &replacement){
    const size_t at=text.find(old);if(at==std::string::npos||text.find(old,at+old.size())!=std::string::npos)return false;
    text.replace(at,old.size(),replacement);return true;
}
inline bool PixelSource(std::string &result,bool textureArray=false){
    result.clear();
    // Extract the actual production decoder and map-coordinate convention. A changed
    // source contract fails optional creation; it cannot silently invent another ocean.
    const std::string production=WaterFftSampling::PixelSource(false,false);
    std::string structure,sampling,decode,legacy;
    if(!Slice(production,"struct FftSurface {","struct FftMapSamples",structure)
        ||!Slice(production,"struct FftMapSamples","FftSurface fftDecodeSamples(",sampling)
        ||!Slice(production,"FftSurface fftDecodeSamples(","FftSurface fftRawSurface(",decode)
        ||!Slice(WaterShoreFoam::Source(),"float4 waterShoreWave(","float waterShoreRate(",legacy))return false;
    if(!ReplaceOnce(sampling,"samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod[2]);","samples.c=0;")
        ||!ReplaceOnce(sampling,"samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod[2]);","samples.cd=0;"))return false;
    for(unsigned i=0;i<4;++i){
        if(!ReplaceOnce(sampling,"Texture"+std::to_string(8+i),"IncidentTexture"+std::to_string(4+i))
            ||!ReplaceOnce(sampling,"Sampler"+std::to_string(8+i),"IncidentSampler"+std::to_string(4+i)))return false;
    }
    if(textureArray) for(unsigned layer=0;layer<4;++layer) {
        const std::string slot=std::to_string(4+layer),cascade=std::to_string(layer/2);
        const std::string coordinate="p/("+std::string(layer<2?"450.0":"103.0")+"*fftWorldScale)";
        const std::string sample="IncidentTexture"+slot+".SampleLevel(IncidentSampler"+slot+","+coordinate+",lod["+cascade+"])",
            arraySample="IncidentTexture"+slot+".SampleLevel(IncidentSampler"+slot+",float3("+coordinate+","+std::to_string(layer)+"),lod["+cascade+"])";
        if(!ReplaceOnce(sampling,sample,arraySample)) return false;
    }
    std::string source;
    for(unsigned i=0;i<SamplerCount;++i){const std::string n=std::to_string(i);
        source+=std::string(textureArray && i>=4?"Texture2DArray<float4> IncidentTexture":"Texture2D<float4> IncidentTexture")+n+" : register(t"+n+",space2);\n"
            +"SamplerState IncidentSampler"+n+" : register(s"+n+",space2);\n";}
    source+="cbuffer IncidentParameters:register(b0,space3){float4 Parameters;};\n";
    source+=WaterFftSampling::CommonSource()+structure+sampling+decode
        +WaterCoastalSurf::IncidentSource()+WaterCoastalSurf::Source()+legacy
        +WaterSurfaceModel::OceanShaderSource();
    source+=R"HLSL(
float4 main(float4 pixel:SV_Position):SV_Target0 {
    uint2 cell=(uint2)pixel.xy;uint index=cell.y*256+cell.x;
    if(!all(isfinite(Parameters))||Parameters.y<0||Parameters.y>65536
        ||floor(Parameters.y)!=Parameters.y||index>=(uint)Parameters.y)return float4(0,0,0,1);
    float2 uv=(float2(cell)+.5)/256.0;
    float4 world=IncidentTexture0.SampleLevel(IncidentSampler0,uv,0);
    WaterSurfContext context;
    context.coast=IncidentTexture1.SampleLevel(IncidentSampler1,uv,0);
    context.surface=IncidentTexture2.SampleLevel(IncidentSampler2,uv,0);
    context.admission=IncidentTexture3.SampleLevel(IncidentSampler3,uv,0);
    if(!all(isfinite(world))||!waterSurfContextFinite(context)||context.admission.x<.5
        ||context.admission.y<.5||context.surface.x<=0||context.coast.x<=.15)return float4(0,0,0,1);
    float offshore=context.surface.y,weight=context.surface.x;
    // Exactly the existing selected analytic components; the ocean helper retains
    // its original warp and packet envelope. Exposed shallow water shares the FFT
    // phase even at zero optical R. No second TimeScale, blend-weight gradient,
    // damping derivative, vessel pressure or decorative swell enters this source.
    float3 legacy=waterShoreWave(world.xy,Parameters.x).xyz;
    float4 oceanSwell;waterOceanSurface(world.xy,Parameters.x,oceanSwell);
    float4 analytic=float4(lerp(legacy,oceanSwell.xyz,offshore),1);
    float4 spectral=waterSurfIncident(fftMapSamples(world.xy,float3(0,0,0)));
    float4 incident=waterSurfBlend(analytic,spectral,weight);
    // Horizontal chop still fades with optical R; a zero-chop shore must not
    // acquire a compression breaker just because its vertical wave is spectral.
    incident.w=lerp(1.0,spectral.w,saturate(weight*offshore));
    return incident;
}
)HLSL";
    result.swap(source);return true;
}
} // namespace WaterCoastalIncident
