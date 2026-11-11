/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterFftSpectrum.h"
#include "Platform/WaterCleanShore.h"
#include <string>
#include <cstdio>

// A/B experiment only: the same unrotated, depth-only incident surface in both stages.
namespace WaterCleanShader {
inline std::string ShoreVertexSource(bool cleanMain=false) {
    if(cleanMain && WaterCleanOpt::Get().stream)return WaterCleanShore::Source(true)+WaterCleanStream::Vertex();
    std::string source=WaterCleanShore::Source(cleanMain)+R"HLSL(
float3 fftShoreVertexBand(float2 p,FftShoreTiles tiles,int band) {
    float3 result=0;
    [unroll]for(int i=0;i<3;++i) {
        float2 rotation=tiles.rotation[i],inverseRotation=rotation*float2(1,-1);float2 q=tiles.vertex[i]+fftRotate(p-tiles.vertex[i],inverseRotation);
        float3 a=0;
        if(band==0)a=FftDisplacement0.SampleLevel(FftSampler0,q/(450.0*fftWorldScale),0).xyz;
        else if(band==1)a=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
        else a=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
        float3 d=a.xzy;d.xy=fftRotate(d.xy,rotation);result+=tiles.weight[i]*d;
    }
    return result;
}
float3 fftCleanPreparedDisplacement(float2 p,float4 depth,FftShoreTiles tiles) {
    // Exact identity prevents coherent near-zero tiles from changing amplitude/read count.
    if(!tiles.active)return fftCleanBaseDisplacement(p,depth);
    float3 a=0;
    if(fftShoreLong)a=fftShoreVertexBand(p,tiles,0);
    else a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xzy;
    float3 b=fftShoreVertexBand(p,tiles,1);
    float3 c=0;
    if(fftShoreFine)c=fftShoreVertexBand(p,tiles,2);
    else c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xzy;
    return (a*float3(-FFT_CLEAN_CHOP_LONG,-FFT_CLEAN_CHOP_LONG,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG).x+b*float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM).x+c*float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE).x)*fftWorldScale;
}
float3 fftCleanDisplacement(float2 p,float4 depth) {return fftCleanPreparedDisplacement(p,depth,fftShoreTiles(p));}
)HLSL";
    if(cleanMain) {
        const std::string entry="    float3 result=0;";
        const std::string collapsed=R"HLSL(    [branch] if(tiles.collapsed) {
        float2 rotation=tiles.meanRotation,inverseRotation=rotation*float2(1,-1);
        float2 q=tiles.meanVertex+fftRotate(p-tiles.meanVertex,inverseRotation);
        float3 a=0;
        if(band==0)a=FftDisplacement0.SampleLevel(FftSampler0,q/(450.0*fftWorldScale),0).xyz;
        else if(band==1)a=FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xyz;
        else a=FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xyz;
        float3 d=a.xzy;d.xy=fftRotate(d.xy,rotation);return tiles.scale.x*d;
    }
)HLSL";
        source.insert(source.find(entry),collapsed);
    }
    if(cleanMain && WaterCleanOpt::Get().loop)WaterCleanOpt::Replace(source,"[unroll]for(int i=0;i<3;++i)","[loop]for(int i=0;i<3;++i)");
    return source;
}
inline std::string ShorePixelSource(bool cleanMain=false) {
    std::string source=WaterCleanShore::Source(cleanMain)+R"HLSL(
void fftShorePixelBand(inout FftSurface value,float2 p,FftShoreTiles tiles,int band,float lod,float horizontal,float3 gain) {
    float3 displacement=0,dx=0,dy=0;
    [unroll]for(int i=0;i<3;++i) {
        float2 rotation=tiles.rotation[i],inverseRotation=rotation*float2(1,-1);float2 q=tiles.vertex[i]+fftRotate(p-tiles.vertex[i],inverseRotation);
        float4 a,derivatives;
        if(band==0) {a=Texture8.SampleLevel(Sampler8,q/(450.0*fftWorldScale),lod);derivatives=Texture9.SampleLevel(Sampler9,q/(450.0*fftWorldScale),lod);}
        else if(band==1) {a=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod);derivatives=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod);}
        else {a=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);derivatives=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);}
        float3 d=a.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
        float3 x=float3(derivatives.z,a.w,derivatives.x)*float3(horizontal,horizontal,1);
        float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
        // q=vertex+R(-angle)(p-vertex), output horizontal vector=R(angle)d.
        float2 qx=fftRotate(float2(1,0),inverseRotation),qy=fftRotate(float2(0,1),inverseRotation);
        float3 xx=x*qx.x+y*qx.y,yy=x*qy.x+y*qy.y;
        d.xy=fftRotate(d.xy,rotation);xx.xy=fftRotate(xx.xy,rotation);yy.xy=fftRotate(yy.xy,rotation);
        displacement+=tiles.weight[i]*d;
        dx+=tiles.weight[i]*xx+tiles.dx[i]*d;dy+=tiles.weight[i]*yy+tiles.dy[i]*d;
    }
    fftMediumScale(displacement,dx,dy,gain);
    value.displacement+=displacement;value.derivativeX+=dx;value.derivativeY+=dy;
}
FftSurface fftShoreSurface(float2 p,float4 depth,float3 lod,FftShoreTiles tiles) {
    FftMapSamples samples=(FftMapSamples)0;
    if(!fftShoreLong) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);}
    if(!fftShoreFine) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);}
    FftSurface value=fftCleanDecode(samples,depth,lod);
    if(fftShoreLong)fftShorePixelBand(value,p,tiles,0,lod.x,-FFT_CLEAN_CHOP_LONG,fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG));
    fftShorePixelBand(value,p,tiles,1,lod.y,-FFT_CLEAN_CHOP_SHORT,fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM));
    if(fftShoreFine)fftShorePixelBand(value,p,tiles,2,lod.z,-FFT_CLEAN_CHOP_SHORT,fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE));
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
    return value;
}
FftSurface fftCleanSurface(float2 p,float4 depth,float3 lod) {
    FftShoreTiles tiles=fftShoreTiles(p);if(!tiles.active)return fftCleanBaseSurface(p,depth,lod);
    return fftShoreSurface(p,depth,lod,tiles);
}
void fftShoreFinish(inout FftSurface value) {
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
}
void fftShorePairedBand(inout FftSurface physical,inout FftSurface shading,float2 p,FftShoreTiles tiles,int band,float lod,float horizontal,float3 gain) {
    FftSurface physicalBand=(FftSurface)0;
    fftShorePixelBand(physicalBand,p,tiles,band,0,horizontal,gain);
    FftSurface shadingBand=physicalBand;
    if(lod>0) {
        shadingBand=(FftSurface)0;
        fftShorePixelBand(shadingBand,p,tiles,band,lod,horizontal,gain);
    }
    physical.displacement+=physicalBand.displacement;physical.derivativeX+=physicalBand.derivativeX;physical.derivativeY+=physicalBand.derivativeY;
    shading.displacement+=shadingBand.displacement;shading.derivativeX+=shadingBand.derivativeX;shading.derivativeY+=shadingBand.derivativeY;
}
void fftCleanPreparedAndShadingSurface(float2 p,float4 depth,float3 lod,FftShoreTiles tiles,out FftSurface physical,out FftSurface shading) {
    if(!tiles.active) {fftCleanBaseAndShadingSurface(p,depth,lod,physical,shading);return;}
    FftMapSamples samples=(FftMapSamples)0;
    if(!fftShoreLong) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),0);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),0);}
    if(!fftShoreFine) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),0);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),0);}
    physical=fftCleanDecode(samples,depth,0);shading=physical;
    if(any(lod>0)) {
        if(!fftShoreLong && lod.x>0) {samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);}
        if(!fftShoreFine && lod.z>0) {samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);}
        shading=fftCleanDecode(samples,depth,lod);
    }
    // Keep each band's mip-zero physical contribution; only its unresolved shading pair is read again.
    if(fftShoreLong)fftShorePairedBand(physical,shading,p,tiles,0,lod.x,-FFT_CLEAN_CHOP_LONG,fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG));
    fftShorePairedBand(physical,shading,p,tiles,1,lod.y,-FFT_CLEAN_CHOP_SHORT,fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM));
    if(fftShoreFine)fftShorePairedBand(physical,shading,p,tiles,2,lod.z,-FFT_CLEAN_CHOP_SHORT,fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE));
    fftShoreFinish(physical);fftShoreFinish(shading);
}
void fftCleanAndShadingSurface(float2 p,float4 depth,float3 lod,out FftSurface physical,out FftSurface shading) {
    fftCleanPreparedAndShadingSurface(p,depth,lod,fftShoreTiles(p),physical,shading);
}

)HLSL";
    if(cleanMain) {
        const std::string entry="    float3 displacement=0,dx=0,dy=0;";
        const std::string collapsed=R"HLSL(    [branch] if(tiles.collapsed) {
        float2 rotation=tiles.meanRotation,inverseRotation=rotation*float2(1,-1);
        float2 relative=fftRotate(p-tiles.meanVertex,inverseRotation);
        float2 q=tiles.meanVertex+relative;
        float4 a,derivatives;
        if(band==0) {a=Texture8.SampleLevel(Sampler8,q/(450.0*fftWorldScale),lod);derivatives=Texture9.SampleLevel(Sampler9,q/(450.0*fftWorldScale),lod);}
        else if(band==1) {a=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod);derivatives=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod);}
        else {a=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);derivatives=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);}
        float3 d=a.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
        float3 x=float3(derivatives.z,a.w,derivatives.x)*float3(horizontal,horizontal,1);
        float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
        // Differentiate q=v+R(-angle)(p-v), then S*R(angle)*d(q).
        float2 perpendicular=float2(-relative.y,relative.x);
        float2 qx=tiles.vertexX+fftRotate(float2(1,0)-tiles.vertexX,inverseRotation)-perpendicular*tiles.angleGradient.x;
        float2 qy=tiles.vertexY+fftRotate(float2(0,1)-tiles.vertexY,inverseRotation)-perpendicular*tiles.angleGradient.y;
        float3 xx=x*qx.x+y*qx.y,yy=x*qy.x+y*qy.y;
        float2 turn=float2(-d.y,d.x);
        xx.xy+=turn*tiles.angleGradient.x;yy.xy+=turn*tiles.angleGradient.y;
        d.xy=fftRotate(d.xy,rotation);xx.xy=fftRotate(xx.xy,rotation);yy.xy=fftRotate(yy.xy,rotation);
        float3 dx=tiles.scale.x*xx+tiles.scale.y*d,dy=tiles.scale.x*yy+tiles.scale.z*d;
        d*=tiles.scale.x;fftMediumScale(d,dx,dy,gain);
        value.displacement+=d;value.derivativeX+=dx;value.derivativeY+=dy;return;
    }
)HLSL";
        source.insert(source.find(entry),collapsed);
    }
    if(cleanMain) {
        const auto &opt=WaterCleanOpt::Get();
        if(opt.stream || opt.fused) {
            const size_t finish=source.find("void fftShoreFinish("),paired=source.find("void fftShorePairedBand(");
            const size_t prepared=source.find("void fftCleanPreparedAndShadingSurface(");
            const std::string finishSource=source.substr(finish,paired-finish),preparedSource=source.substr(prepared);
            if(opt.stream)source=WaterCleanShore::Source(true);else source.erase(finish);
            source+=WaterCleanStream::PixelHelpers(opt.fused)+finishSource;
            if(!opt.stream) {
                source+=R"HLSL(
void fftShorePairedBand(inout FftSurface physical,inout FftSurface shading,float2 p,FftShoreTiles tiles,int band,float lod,float horizontal,float3 gain) {
    FFT_TILE_LOOP for(int i=0;i<(tiles.collapsed?1:3);++i) {
        FftShorePoint v=fftShorePoint(p,tiles,i);
        fftShoreAddPair(physical,shading,v,band,lod,horizontal,gain);
    }
}
)HLSL";
            }
            source+=preparedSource;
            if(opt.stream) {
                const size_t begin=source.find("    // Keep each band's mip-zero physical contribution;");
                const size_t end=source.find("    fftShoreFinish(physical);",begin);
                std::string walk;
                if(opt.fused)walk=R"HLSL(    float3 longGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG),mediumGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM),fineGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE);
    FFT_TILE_LOOP for(int i=0;i<(tiles.collapsed?1:3);++i) {
        FftShorePoint v=fftShorePoint(p,tiles,i);
        if(fftShoreLong)fftShoreAddPair(physical,shading,v,0,lod.x,-FFT_CLEAN_CHOP_LONG,longGain);
        fftShoreAddPair(physical,shading,v,1,lod.y,-FFT_CLEAN_CHOP_SHORT,mediumGain);
        if(fftShoreFine)fftShoreAddPair(physical,shading,v,2,lod.z,-FFT_CLEAN_CHOP_SHORT,fineGain);
    }
)HLSL";
                else walk="    fftShoreStreamSurface(physical,shading,p,depth,lod,tiles,false);\n    if(any(lod>0))fftShoreStreamSurface(physical,shading,p,depth,lod,tiles,true);\n";
                source.replace(begin,end-begin,walk);
            }
            WaterCleanOpt::Replace(source,"FFT_TILE_LOOP",opt.loop?"[loop]":"[unroll]");
        }
        if(opt.finelite && !opt.stream && !opt.fused) {
            WaterCleanOpt::Replace(source,"        float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);",
                "        float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);\n        if(band==2) {d.xy=0;x.xy=0;y.xy=0;}");
        }
        if(opt.half) {
            WaterCleanOpt::Replace(source,"float3 displacement=0,dx=0,dy=0;","min16float3 displacement=0,dx=0,dy=0;");
            WaterCleanOpt::Replace(source,"float3 d,dx,dy;fftShoreDecodePoint", "min16float3 d,dx,dy;fftShoreDecodePoint");
        }
        if(opt.loop)WaterCleanOpt::Replace(source,"[unroll]for(int i=0;i<3;++i)","[loop]for(int i=0;i<3;++i)");
    }
    return source;
}



// Keep baked clean constants and the live draw block on the same configured values.
inline void ApplySettings(std::string &source) {
    using namespace WaterTuning;
    const auto &v=Configured();
    const auto scalar=[&](Key key,const char *live) {
        if(Enabled())return std::string(live);
        char number[32];std::snprintf(number,sizeof(number),"%.9g",v[key]);return std::string(number);
    };
    const auto longChop=scalar(ChopLong,"WaterTuningChop.x"),shortChop=scalar(ChopShort,"WaterTuningChop.y");
    const std::pair<std::string,std::string> replacements[]={
        {"FFT_CLEAN_CHOP_LONG",longChop},{"FFT_CLEAN_CHOP_SHORT",shortChop},
        {"FFT_CLEAN_DEPTH_LONG",scalar(DepthLong,"WaterTuningDepth.x")},
        {"FFT_CLEAN_DEPTH_MEDIUM",scalar(DepthMedium,"WaterTuningDepth.y")},
        {"FFT_CLEAN_DEPTH_FINE",scalar(DepthFine,"WaterTuningDepth.z")}};
    for(const auto &r:replacements)for(size_t p=source.find(r.first);p!=std::string::npos;p=source.find(r.first,p+r.second.size()))
        source.replace(p,r.first.size(),r.second);
}
// Shared by the actual SDL upload and the clean-default equivalence test.
template<class Constants> inline void SetDrawValues(Constants &d,const WaterTuning::Values &v) {
    using namespace WaterTuning;
    d.WaterTuningDepth[0]=v[DepthLong];d.WaterTuningDepth[1]=v[DepthMedium];d.WaterTuningDepth[2]=v[DepthFine];
    d.WaterTuningChop[0]=v[ChopLong];d.WaterTuningChop[1]=v[ChopShort];d.WaterTuningChop[2]=v[HeadingFollow];
    d.WaterTuningShore[0]=v[ShoreTurn];d.WaterTuningShore[1]=v[ShoreReach];d.WaterTuningShore[2]=v[ShoreTile];d.WaterTuningShore[3]=v[ShoreSharp];
    const double wind=WindAngle(v)*WaterFftSpectrum::Pi/180;
    d.WaterTuningHeading[0]=std::cos(wind);d.WaterTuningHeading[1]=std::sin(wind);d.WaterTuningHeading[2]=v[ShoreBands];
    d.WaterTuningHeading[3]=v[ShoreMaxTurn]*WaterFftSpectrum::Pi/180;
    d.WaterTuningLook[0]=v[CrestColor];d.WaterTuningLook[1]=v[FoamThreshold];d.WaterTuningLook[2]=v[Glint];
}
inline std::string ContextSource(bool cleanMain=false) {
    std::string source="struct FftCleanContext {float4 depth;";
    if(WaterCleanShore::Enabled())source+="FftShoreTiles tiles;";
    source+="};\nFftCleanContext fftCleanContext(float2 p) {FftCleanContext context=(FftCleanContext)0;context.depth=fftMediumDepthField(p);";
    if(WaterCleanShore::Enabled())source+=cleanMain?"context.tiles=fftShoreTiles(p);":"if(context.depth.w>.5)context.tiles=fftShoreTiles(p);";
    return source+"return context;}\n";
}
inline std::string VertexSource(bool cleanMain=false) {
    std::string source=R"HLSL(
// A/B experiment only: D changes amplitude, never coordinates or dispersion.
float3 fftCleanDisplacement(float2 p,float4 depth) {
    float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;
    float3 b=FftDisplacement1.SampleLevel(FftSampler1,p/(103.0*fftWorldScale),0).xyz;
    float3 c=FftDisplacement2.SampleLevel(FftSampler2,p/(13.0*fftWorldScale),0).xyz;
    return (a.xzy*float3(-FFT_CLEAN_CHOP_LONG,-FFT_CLEAN_CHOP_LONG,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG).x
        +b.xzy*float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM).x
        +c.xzy*float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE).x)*fftWorldScale;
}
)HLSL";
    if(WaterCleanShore::Enabled()) {
        const std::string from="float3 fftCleanDisplacement(",to="float3 fftCleanBaseDisplacement(";
        source.replace(source.find(from),from.size(),to);source+=ShoreVertexSource(cleanMain);
    }
    ApplySettings(source);
    if(WaterCleanProfile::Get().nolong) {
        const char *lines[]={"float3 a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xyz;","else a=FftDisplacement0.SampleLevel(FftSampler0,p/(450.0*fftWorldScale),0).xzy;","if(fftShoreLong)a=fftShoreVertexBand(p,tiles,0);"};
        const char *to[]={"float3 a=0;","else a=0;","if(fftShoreLong)a=0;"};
        for(int i=0;i<3;++i)for(size_t p=source.find(lines[i]);p!=std::string::npos;p=source.find(lines[i],p+std::strlen(to[i])))source.replace(p,std::strlen(lines[i]),to[i]);
    }
    if(WaterCleanProfile::Get().flatvertex) {
        const std::string entry=WaterCleanShore::Enabled()?"float3 fftCleanPreparedDisplacement(float2 p,float4 depth,FftShoreTiles tiles) {":"float3 fftCleanDisplacement(float2 p,float4 depth) {";
        source.insert(source.find(entry)+entry.size(),"\n    return 0;\n");
    }
    return source+ContextSource(cleanMain);
}
inline std::string PixelSource(WaterFftSpectrum::Configuration configuration,bool cleanMain=false) {
    const auto rms=WaterFftSpectrum::SpectrumHeightRms(WaterFftSpectrum::InitialSpectrum(configuration));
    const auto &look=WaterCleanSpectrum::Appearance();char constants[512];
    std::snprintf(constants,sizeof(constants),
        "// A/B experiment only: actual seeded band RMS, in game units.\n"
        "static const float3 fftCleanRms=float3(%.9g,%.9g,%.9g);\n"
        "static const float3 fftCleanLook=float3(%.9g,%.9g,%.9g);\n",
        WaterCleanProfile::Get().nolong?0:float(rms[0]),float(rms[1]),float(rms[2]),float(look.sss),float(look.foamThreshold),float(look.glint));
    std::string source=std::string(constants)+R"HLSL(
// A/B experiment only: retain the D-gradient product rule for all displacement derivatives.
void fftCleanBand(inout FftSurface value,float4 map,float4 derivatives,float horizontal,float3 gain) {
    float3 d=map.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
    float3 x=float3(derivatives.z,map.w,derivatives.x)*float3(horizontal,horizontal,1);
    float3 y=float3(map.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
    fftMediumScale(d,x,y,gain);
    value.displacement+=d;value.derivativeX+=x;value.derivativeY+=y;
}
FftSurface fftCleanDecode(FftMapSamples samples,float4 depth,float3 lod) {
    FftSurface value=(FftSurface)0;
    float3 longGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG),mediumGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM),fineGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE);
    fftCleanBand(value,samples.a,samples.ad,-FFT_CLEAN_CHOP_LONG,longGain);
    fftCleanBand(value,samples.b,samples.bd,-FFT_CLEAN_CHOP_SHORT,mediumGain);
    fftCleanBand(value,samples.c,samples.cd,-FFT_CLEAN_CHOP_SHORT,fineGain);
    float3 limit=fftLimitDerivative(value.displacement);
    float2 x=value.derivativeX.xy*limit.xy,y=value.derivativeY.xy*limit.xy;
    // True horizontal-displacement determinant. With spatial amplitude gradients the
    // mixed derivatives need not be equal, so use their product rather than a square.
    value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;
    value.contactIncident=float3(fftLimit(value.displacement).z,
        float2(value.derivativeX.z,value.derivativeY.z)*limit.z);
    value.localInfluence=1;
    // CPU spectrum integration gives total-minus-mip-resolved slope power per band.
    // Amplitude gains multiply variance quadratically. Existing outer limiter/hull
    // factors are applied once by fftSurface and the material.
    value.localVariance=fftShadingVariance(float3(lod.x,0,0))*longGain.x*longGain.x
        +fftShadingVariance(float3(0,lod.y,0))*mediumGain.x*mediumGain.x
        +fftShadingVariance(float3(0,0,lod.z))*fineGain.x*fineGain.x;
    return value;
}
FftSurface fftCleanSurface(float2 p,float4 depth,float3 lod) {
    return fftCleanDecode(fftMapSamples(p,lod),depth,lod);
}
void fftCleanAndShadingSurface(float2 p,float4 depth,float3 lod,out FftSurface physical,out FftSurface shading) {
    FftMapSamples samples=fftMapSamples(p,float3(0,0,0));
    physical=fftCleanDecode(samples,depth,float3(0,0,0));shading=physical;
    if(any(lod>0.0)) {
        if(lod.x>0) {
            samples.a=Texture8.SampleLevel(Sampler8,p/(450.0*fftWorldScale),lod.x);
            samples.ad=Texture9.SampleLevel(Sampler9,p/(450.0*fftWorldScale),lod.x);
        }
        if(lod.y>0) {
            samples.b=Texture10.SampleLevel(Sampler10,p/(103.0*fftWorldScale),lod.y);
            samples.bd=Texture11.SampleLevel(Sampler11,p/(103.0*fftWorldScale),lod.y);
        }
        if(lod.z>0) {
            samples.c=Texture12.SampleLevel(Sampler12,p/(13.0*fftWorldScale),lod.z);
            samples.cd=Texture13.SampleLevel(Sampler13,p/(13.0*fftWorldScale),lod.z);
        }
        shading=fftCleanDecode(samples,depth,lod);
    }
}
)HLSL";
    if(WaterCleanShore::Enabled()) {
        const std::pair<const char*,const char*> names[]={{"FftSurface fftCleanSurface(","FftSurface fftCleanBaseSurface("},{"void fftCleanAndShadingSurface(","void fftCleanBaseAndShadingSurface("}};
        for(auto &n:names)source.replace(source.find(n.first),std::strlen(n.first),n.second);
        source+=ShorePixelSource(cleanMain);
    }
    if(WaterTuning::Enabled()) {
        source.replace(0,std::strlen(constants),WaterCleanProfile::Get().nolong?"#define fftCleanRms float3(0,WaterTuningRms.yz)\n#define fftCleanLook WaterTuningLook.xyz\n":"#define fftCleanRms WaterTuningRms.xyz\n#define fftCleanLook WaterTuningLook.xyz\n");
    }
    ApplySettings(source);
    if(WaterCleanProfile::Get().nolong) {
        const std::string from="fftShadingVariance(float3(lod.x,0,0))*longGain.x*longGain.x";
        for(size_t p=source.find(from);p!=std::string::npos;p=source.find(from,p+1))source.replace(p,from.size(),"0");
    }
    if(WaterCleanProfile::Get().nofoam) {
        const std::string from="value.incidentCompression=(1+x.x)*(1+y.y)-x.y*y.x;",to="value.incidentCompression=1;";
        for(size_t p=source.find(from);p!=std::string::npos;p=source.find(from,p+to.size()))source.replace(p,from.size(),to);
    }
    return source+ContextSource(cleanMain);
}
}
