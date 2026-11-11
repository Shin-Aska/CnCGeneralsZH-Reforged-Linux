/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <cstdlib>
#include "Platform/WaterMediumDepth.h"
#include "Platform/WaterCleanProfile.h"
#include "Platform/WaterCleanCollapse.h"
#include "Platform/WaterCleanStream.h"
#include "Platform/WaterTuning.h"
#include <string>
#include <cstdio>
namespace WaterCleanShore {
struct Settings {
    float turn=WaterTuning::Keys[WaterTuning::ShoreTurn].initial,
        reach=WaterTuning::Keys[WaterTuning::ShoreReach].initial,
        tile=WaterTuning::Keys[WaterTuning::ShoreTile].initial,
        sharp=WaterTuning::Keys[WaterTuning::ShoreSharp].initial;
    int bands=int(WaterTuning::Keys[WaterTuning::ShoreBands].initial);
    float maxTurn=WaterTuning::Keys[WaterTuning::ShoreMaxTurn].initial,
        follow=WaterTuning::Keys[WaterTuning::HeadingFollow].initial;
};
inline Settings Get() {
    Settings s;
    const auto &v=WaterTuning::Configured();
    s.turn=v[WaterTuning::ShoreTurn];s.reach=v[WaterTuning::ShoreReach];s.tile=v[WaterTuning::ShoreTile];s.sharp=v[WaterTuning::ShoreSharp];s.bands=int(v[WaterTuning::ShoreBands]);s.maxTurn=v[WaterTuning::ShoreMaxTurn];s.follow=v[WaterTuning::HeadingFollow];
    s.turn=(std::max)(0.f,(std::min)(1.f,s.turn));s.reach=(std::max)(100.f,(std::min)(3000.f,s.reach));s.tile=(std::max)(100.f,(std::min)(3000.f,s.tile));s.sharp=(std::max)(1.f,(std::min)(8.f,s.sharp));return s;
}
inline bool Enabled() {return !WaterCleanProfile::Get().noshore && !WaterCleanProfile::Get().nodepth && (WaterTuning::Enabled() || Get().follow>0 || Get().turn>0);}
constexpr float DistanceCap = 3000.0f;
constexpr float SmoothRadii[] = {60,180,500,1000};
// Interpolate unnormalized gradients, never angles. Smoothstep gives continuous
// weights and derivatives when the cell's requested support crosses a scale.
inline float ScaleWeight(float distance,int scale) {
    const float radius=.75f*distance;
    if(radius<=SmoothRadii[0])return scale==0?1.f:0.f;
    for(int k=0;k<3;++k)if(radius<SmoothRadii[k+1]) {
        const float t=(radius-SmoothRadii[k])/(SmoothRadii[k+1]-SmoothRadii[k]);
        const float blend=t*t*(3-2*t);
        return scale==k?1-blend:scale==k+1?blend:0.f;
    }
    return scale==3?1.f:0.f;
}
inline uint8_t EncodeDistance(float distance) {
    return uint8_t(std::lround((std::max)(0.f,(std::min)(DistanceCap,distance))*(255.f/DistanceCap)));
}
// Independent of effective D. Eight-neighbour chamfer distances include every dry cell;
// no map-edge land is invented. A land-free map stays at DistanceCap with zero gradient.
inline bool Build(WaterMediumDepth::Field &f,const uint8_t *wet,float spacing) {
    f.shoreX.clear();f.shoreY.clear();f.shoreConfidence.clear();f.shoreDistance.clear();
    const int w=f.width,h=f.height;const size_t n=size_t(w)*h;
    if(w<2 || h<2 || w>WaterMediumDepth::MaxDimension || h>WaterMediumDepth::MaxDimension
        || n>WaterMediumDepth::MaxCells || !wet || !std::isfinite(spacing) || spacing<=0 || f.depth.size()!=n)return false;
    std::vector<float> distance(n,DistanceCap),smooth,scratch(n);
    for(size_t i=0;i<n;++i)if(!wet[i])distance[i]=0;
    const float step=(std::min)(spacing,DistanceCap),diagonal=(std::min)(step*1.41421356237f,DistanceCap);
    const auto relax=[&](int x,int y,int xx,int yy,float cost) {
        if(xx>=0 && xx<w && yy>=0 && yy<h) {
            auto &d=distance[size_t(y)*w+x];d=(std::min)(d,distance[size_t(yy)*w+xx]+cost);
        }
    };
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        relax(x,y,x-1,y,step);relax(x,y,x,y-1,step);
        relax(x,y,x-1,y-1,diagonal);relax(x,y,x+1,y-1,diagonal);
    }
    for(int y=h-1;y>=0;--y)for(int x=w-1;x>=0;--x) {
        relax(x,y,x+1,y,step);relax(x,y,x,y+1,step);
        relax(x,y,x-1,y+1,diagonal);relax(x,y,x+1,y+1,diagonal);
    }
    f.shoreDistance=distance;
    std::vector<float> gradientX(n,0),gradientY(n,0);
    std::vector<double> prefix(size_t((std::max)(w,h))+1);
    // Four independent Gaussian approximations, each with three separable boxes.
    // Prefix sums keep map-load work O(cells), independent of support radius.
    // Include dry distances so opposing banks cancel before normalization.
    for(int scale=0;scale<4;++scale) {
        smooth=distance;
        const int radius=int((std::max)(1.,(std::min)(1000000.,std::round(SmoothRadii[scale]/(3.*spacing)))));
        for(int pass=0;pass<3;++pass)for(int axis=0;axis<2;++axis) {
            const int length=axis==0?w:h,lines=axis==0?h:w;
            for(int line=0;line<lines;++line) {
                const auto index=[&](int k){return axis==0?size_t(line)*w+k:size_t(k)*w+line;};
                prefix[0]=0;for(int k=0;k<length;++k)prefix[k+1]=prefix[k]+smooth[index(k)];
                for(int k=0;k<length;++k) {
                    const int lo=(std::max)(0,k-radius),hi=(std::min)(length-1,k+radius);
                    const double sum=prefix[hi+1]-prefix[lo]
                        +(std::max)(0,radius-k)*double(smooth[index(0)])
                        +(std::max)(0,k+radius-length+1)*double(smooth[index(length-1)]);
                    scratch[index(k)]=float(sum/(2*radius+1));
                }
            }
            smooth.swap(scratch);
        }
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const size_t i=size_t(y)*w+x;const float weight=ScaleWeight(distance[i],scale);
            if(!wet[i] || weight==0)continue;
            const int x0=(std::max)(0,x-1),x1=(std::min)(w-1,x+1),y0=(std::max)(0,y-1),y1=(std::min)(h-1,y+1);
            gradientX[i]-=weight*(smooth[size_t(y)*w+x1]-smooth[size_t(y)*w+x0])/((x1-x0)*spacing);
            gradientY[i]-=weight*(smooth[size_t(y1)*w+x]-smooth[size_t(y0)*w+x])/((y1-y0)*spacing);
        }
    }
    f.shoreX.resize(n);f.shoreY.resize(n);f.shoreConfidence.resize(n);
    for(size_t i=0;i<n;++i) {
        const float gx=gradientX[i],gy=gradientY[i],magnitude=std::hypot(gx,gy);
        // A straight coast has unit gradient. At medial axes opposite normals
        // cancel continuously; confidence vanishes before direction can flip.
        const float confidence=(std::min)(1.f,magnitude);
        f.shoreX[i]=uint8_t(std::lround(128+127*(magnitude>1e-5f?gx/magnitude:1.f)));
        f.shoreY[i]=uint8_t(std::lround(128+127*(magnitude>1e-5f?gy/magnitude:0.f)));
        f.shoreConfidence[i]=uint8_t(std::lround(127*confidence));
    }
    return true;
}
struct Sample {float x,y,confidence,distance;};
// CPU reference of the shader's bilinear, confidence-weighted vector lookup.
inline Sample SampleField(const WaterMediumDepth::Field &f,float gx,float gy,float windX,float windY) {
    const size_t n=size_t(f.width)*f.height;
    if(f.width<2 || f.height<2 || f.shoreDistance.size()!=n || f.shoreX.size()!=n
        || f.shoreY.size()!=n || f.shoreConfidence.size()!=n || !std::isfinite(gx) || !std::isfinite(gy))return {windX,windY,0,DistanceCap};
    gx=(std::max)(0.f,(std::min)(float(f.width-1),gx));gy=(std::max)(0.f,(std::min)(float(f.height-1),gy));
    const int x=(std::min)(int(gx),f.width-2),y=(std::min)(int(gy),f.height-2);
    const float tx=gx-x,ty=gy-y;float vx=0,vy=0,confidence=0,distance=0;
    for(int j=0;j<2;++j)for(int k=0;k<2;++k) {
        const size_t i=size_t(y+j)*f.width+x+k;const float weight=(k?tx:1-tx)*(j?ty:1-ty),c=f.shoreConfidence[i]/127.f;
        vx+=(int(f.shoreX[i])-128)/127.f*c*weight;vy+=(int(f.shoreY[i])-128)/127.f*c*weight;
        confidence+=c*weight;distance+=EncodeDistance(f.shoreDistance[i])*(DistanceCap/255.f)*weight;
    }
    const float magnitude=std::hypot(vx,vy);
    return {magnitude>1e-5f?vx/magnitude:windX,magnitude>1e-5f?vy/magnitude:windY,(std::min)(confidence,magnitude),distance};
}
inline float VertexAngle(float windX,float windY,const Sample &field,const Settings &s) {
    const float cross=windX*field.y-windY*field.x,cosine=windX*field.x+windY*field.y;
    // Exactly antiparallel has two equal shortest turns: consistently choose CCW,
    // including signed-zero cross products, in both CPU and shader.
    const float delta=cross==0 && cosine<0?3.141592654f:std::atan2(cross,cosine);
    const float t=(std::max)(0.f,(std::min)(1.f,field.distance/s.reach));
    // Confidence gates out channel centrelines. Limit the final weighted turn,
    // applied after heading following; it never caps the heading-field rotation.
    const float c=(std::max)(0.f,(std::min)(1.f,(field.confidence-.02f)/(.15f-.02f))),gate=c*c*(3-2*c);
    const float maximum=s.maxTurn*(3.141592654f/180.f);
    return (std::max)(-maximum,(std::min)(maximum,delta*s.turn*gate*(1-t*t*(3-2*t))));
}
// CPU reference of the clean tile's two rotations. The field gradient points
// toward increasing travel time: k*U-omega*t advances along +grad(U).
inline float HeadingAngle(float baseX,float baseY,float gx,float gy,float follow) {
    const float length=std::hypot(gx,gy);
    if(!(length>1e-5f))return 0;
    const float cross=baseX*gy-baseY*gx,cosine=baseX*gx+baseY*gy;
    return follow*(cross==0 && cosine<0?3.141592654f:std::atan2(cross,cosine));
}
inline float AppliedAngle(float baseX,float baseY,float gx,float gy,const Sample &shore,const Settings &s) {
    const float length=std::hypot(gx,gy);
    const float fieldX=length>1e-5f?gx/length:baseX,fieldY=length>1e-5f?gy/length:baseY;
    return HeadingAngle(baseX,baseY,gx,gy,s.follow)+VertexAngle(fieldX,fieldY,shore,s);
}
inline uint32_t DistanceWord(const WaterMediumDepth::Field &f,size_t i) {
    return f.shoreDistance.size()==f.depth.size()?uint32_t(EncodeDistance(f.shoreDistance[i]))<<16:0x00ff0000u;
}
inline uint32_t Word(const WaterMediumDepth::Field &f,size_t i) {
    uint32_t d=uint32_t(WaterMediumDepth::Encode(f.depth[i]))<<8;
    if(f.shoreX.size()!=f.depth.size())return 0x80000000u|d|0x00800080u;
    return (uint32_t(128+f.shoreConfidence[i])<<24)|(uint32_t(f.shoreX[i])<<16)|d|f.shoreY[i];
}
inline std::string Source(bool cleanMain=false) {
    char c[1024];const auto s=Get();
    if(WaterTuning::Enabled())std::snprintf(c,sizeof(c),"#define fftHeadingFollow WaterTuningChop.z\n#define fftShoreSettings WaterTuningShore\n#define fftShoreWind (-WaterTuningHeading.xy)\n#define fftShoreLong (WaterTuningHeading.z>0.5)\n#define fftShoreFine (WaterTuningHeading.z>1.5)\n#define fftShoreMaxTurn WaterTuningHeading.w\n");
    else std::snprintf(c,sizeof(c),"static const float fftHeadingFollow=%.9g;\nstatic const float4 fftShoreSettings=float4(%.9g,%.9g,%.9g,%.9g);\n#define fftShoreWind (-float2(cos(FftParameters.w+0.7853981633974483),sin(FftParameters.w+0.7853981633974483)))\nstatic const bool fftShoreLong=%s;\nstatic const bool fftShoreFine=%s;\nstatic const float fftShoreMaxTurn=%.9g;\n",s.follow,s.turn,s.reach,s.tile,s.sharp,s.bands>=1?"true":"false",s.bands>=2?"true":"false",s.maxTurn*(3.141592654f/180.f));
    // A/B experiment only: ZH_WATER_SHORE_FORCE=1 aligns every tile fully with the shoreward direction (diagnostic).
    std::string source=std::string(c)+(std::getenv("ZH_WATER_SHORE_FORCE")?"#define FFT_SHORE_FORCE 1\n":"")+R"HLSL(
float2 fftShoreRotate(float2 p,float a) {float s,c;sincos(a,s,c);return float2(c*p.x-s*p.y,s*p.x+c*p.y);}
float4 fftShoreField(float2 p) {
    uint width,height;FftHeadingTexture.GetDimensions(width,height);int2 size=int2(width/7,height);
    if(any(size<2) || TerrainParameters.w<0.5)return float4(fftShoreWind,0,3000);
    float scale=(width/7)/max(TerrainParameters.y,1.0);
    float2 cell=clamp((p+TerrainParameters.xx)*scale,0,float2(size-1));int2 lo=min(int2(floor(cell)),size-2);float2 f=cell-lo;
    float4 field=0;
    [unroll]for(int y=0;y<2;++y)[unroll]for(int x=0;x<2;++x) {
        float2 uv=(float2((lo.x+x)*7+5,lo.y+y)+.5)/float2(width,height);
        float4 a=FftHeadingTexture.SampleLevel(FftHeadingSampler,uv,0);
        float distance=FftHeadingTexture.SampleLevel(FftHeadingSampler,uv+float2(1.0/width,0),0).r*3000;
        float confidence=saturate((a.a*255-128)/127)*step(.5,a.a);
        float2 direction=(a.rb*255-128)/127;
        float weight=(x?f.x:1-f.x)*(y?f.y:1-f.y);
        field+=float4(direction*confidence,confidence,distance)*weight;
    }
    float lengthDirection=length(field.xy);float2 direction=lengthDirection>1e-5?field.xy/lengthDirection:fftShoreWind;
    // Vector cancellation between banks lowers confidence without a 180-degree flip.
    return float4(direction,min(field.z,lengthDirection),field.w);
}
struct FftShoreTiles {float2 vertex[3];float2 rotation[3];float angle[3];float3 weight;float3 dx;float3 dy;bool active;};
FftShoreTiles fftShoreTiles(float2 p) {
    FftShoreTiles t=(FftShoreTiles)0;
    if(fftHeadingFollow==0 && fftShoreSettings.x==0)return t;
    float tile=fftShoreSettings.z;float2 uv=float2(p.x-p.y*.577350269, p.y*1.154700538)/tile;
    float2 cell=floor(uv),f=frac(uv),indices[3];float3 b;float3 bx,by;
    float2 ux=float2(1,-.577350269)/tile,uy=float2(0,1.154700538)/tile;
    if(f.x+f.y<=1) {indices[0]=cell;indices[1]=cell+float2(1,0);indices[2]=cell+float2(0,1);b=float3(1-f.x-f.y,f.x,f.y);bx=float3(-ux.x-uy.x,ux.x,uy.x);by=float3(-ux.y-uy.y,ux.y,uy.y);}
    else {indices[0]=cell+1;indices[1]=cell+float2(0,1);indices[2]=cell+float2(1,0);b=float3(f.x+f.y-1,1-f.x,1-f.y);bx=float3(ux.x+uy.x,-ux.x,-uy.x);by=float3(ux.y+uy.y,-ux.y,-uy.y);}
    b=saturate(b);float sharp=fftShoreSettings.w;float3 a=pow(b,sharp);
    float3 powerDerivative=sharp*pow(max(b,1e-12),sharp-1);float3 ax=powerDerivative*bx,ay=powerDerivative*by;
    float inverseNorm=rsqrt(max(dot(a,a),1e-20));
    t.weight=a*inverseNorm;t.dx=(ax-t.weight*dot(t.weight,ax))*inverseNorm;t.dy=(ay-t.weight*dot(t.weight,ay))*inverseNorm;
    uint depthWidth,depthHeight,rotationWidth,rotationHeight;
    FftCleanDepth.GetDimensions(depthWidth,depthHeight);FftCleanRotation.GetDimensions(rotationWidth,rotationHeight);
    float border=TerrainParameters.x,spacing=TerrainParameters.y/max(float(depthWidth),1.0);
    float low=-border,high=(depthWidth-1)*spacing-border;
    int2 origin=int2(floor(float2(low-.577350269*high,1.154700538*low)/tile))-1;
    [unroll]for(int i=0;i<3;++i) {
        t.vertex[i]=float2(indices[i].x+.5*indices[i].y,.866025404*indices[i].y)*tile;
        int2 cacheIndex=int2(indices[i])-origin;
        if(depthWidth>=2 && rotationWidth>=2 && rotationHeight>=2 && all(cacheIndex>=0) && all(cacheIndex<int2(rotationWidth,rotationHeight))) {
            float4 cached=FftCleanRotation.SampleLevel(FftCleanRotationSampler,(float2(cacheIndex)+.5)/float2(rotationWidth,rotationHeight),0);
            if(cached.w>.5) {
                t.rotation[i]=cached.xy;t.angle[i]=cached.z;
                t.active=t.active || abs(t.angle[i])>1e-6;
                continue;
            }
        }
        // Projected or reflected positions outside the finite cache retain the exact evaluator.
        // fftSwellField decodes words 0/1, including validity, at stride seven.
        // Build seeds the upwind edges; k*U-omega*t travels along +grad(U).
        float2 g=fftSwellField(t.vertex[i]).yz;
        float2 fieldDirection=length(g)>1e-5?normalize(g):fftShoreWind;
        float cross=fftShoreWind.x*fieldDirection.y-fftShoreWind.y*fieldDirection.x,cosine=dot(fftShoreWind,fieldDirection);
        float delta=cross==0 && cosine<0?3.141592654:atan2(cross,cosine);
        t.angle[i]=fftHeadingFollow*delta; // One coherent wavefront field: no turn cap.
        if(fftShoreSettings.x>0) {
            float4 field=fftShoreField(t.vertex[i]);
            cross=fieldDirection.x*field.y-fieldDirection.y*field.x;cosine=dot(fieldDirection,field.xy);
            delta=cross==0 && cosine<0?3.141592654:atan2(cross,cosine);
            float shoreAngle=clamp(delta*fftShoreSettings.x*smoothstep(.02,.15,field.z)*(1-smoothstep(0,fftShoreSettings.y,field.w)),-fftShoreMaxTurn,fftShoreMaxTurn);
#ifdef FFT_SHORE_FORCE
            shoreAngle=clamp(delta,-fftShoreMaxTurn,fftShoreMaxTurn);
#endif
            t.angle[i]+=shoreAngle;
        }
        float sine,cosineAngle;sincos(t.angle[i],sine,cosineAngle);t.rotation[i]=float2(cosineAngle,sine);
        t.active=t.active || abs(t.angle[i])>1e-6;
    }
    return t;
}
float3 fftShoreDebug(FftShoreTiles t) {
    if(fftHeadingFollow==0 && fftShoreSettings.x==0)return 0;
    float angle=dot(t.weight,float3(t.angle[0],t.angle[1],t.angle[2]))/max(dot(t.weight,float3(1,1,1)),1e-5);
    float2 direction=fftShoreRotate(fftShoreWind,angle);float hue=atan2(direction.y,direction.x)/6.283185307+.5;
    float3 rgb=saturate(abs(frac(hue+float3(0,2.0/3,1.0/3))*6-3)-1);
    float edge=.03*(1-smoothstep(0,.035,min(t.weight.x,min(t.weight.y,t.weight.z))));
    return rgb*saturate(abs(angle)/0.785398163)+edge;
}
float3 fftShoreDebug(float2 p) {return fftShoreDebug(fftShoreTiles(p));}
)HLSL";
    if(cleanMain) {
        // The draw already selected clean water. A missing/out-of-cache angle is
        // identity, never an invitation to run legacy heading/local wave code.
        const size_t begin=source.find("        // Projected or reflected positions outside");
        const size_t end=source.find("\n    }\n    return t;",begin);
        source.replace(begin,end-begin,"        t.rotation[i]=float2(1,0);t.angle[i]=0;");
        const std::string entry="bool active;";
        source.replace(source.find(entry),entry.size(),
            "bool active,collapsed;float2 meanRotation,meanVertex,vertexX,vertexY,angleGradient;float3 scale;");
        char threshold[96];std::snprintf(threshold,sizeof(threshold),"static const float fftTileCollapseAngle=%.9g;\n",double(WaterCleanOpt::Get().collapse*3.14159265358979323846f/180.f));
        source=std::string(threshold)+source;
        const std::string finish="    return t;\n}";
        const std::string collapse=R"HLSL(    // Unwrap relative angles across +/-pi before computing the weighted mean.
    float3 angles=float3(0,t.angle[1]-t.angle[0],t.angle[2]-t.angle[0]);
    angles.yz-=6.283185307*floor((angles.yz+3.141592654)/6.283185307);
    // One read stands in for the three only where all three samples land near p: each vertex
    // rotates about itself, so equal nonzero turns still sample tile-sized distances apart.
    float3 turn=float3(t.angle[0],t.angle[1],t.angle[2]);
    turn-=6.283185307*floor((turn+3.141592654)/6.283185307);
    t.collapsed=max(abs(turn.x),max(abs(turn.y),abs(turn.z)))<fftTileCollapseAngle;
    if(t.active && t.collapsed) {
        float sum=dot(t.weight,float3(1,1,1)),sx=dot(t.dx,float3(1,1,1)),sy=dot(t.dy,float3(1,1,1));
        float offset=dot(t.weight,angles)/sum,angle=t.angle[0]+offset;
        t.angleGradient=float2(dot(t.dx,angles)-offset*sx,dot(t.dy,angles)-offset*sy)/sum;
        t.meanVertex=(t.weight.x*t.vertex[0]+t.weight.y*t.vertex[1]+t.weight.z*t.vertex[2])/sum;
        t.vertexX=(t.dx.x*t.vertex[0]+t.dx.y*t.vertex[1]+t.dx.z*t.vertex[2]-t.meanVertex*sx)/sum;
        t.vertexY=(t.dy.x*t.vertex[0]+t.dy.y*t.vertex[1]+t.dy.z*t.vertex[2]-t.meanVertex*sy)/sum;
        sincos(angle,t.meanRotation.y,t.meanRotation.x);t.scale=float3(sum,sx,sy);
    }
    return t;
})HLSL";
        source.replace(source.find(finish),finish.size(),collapse);
        if(WaterCleanOpt::Get().stream)source.replace(source.find("struct FftShoreTiles"),std::string::npos,WaterCleanStream::Tiles());
        if(WaterCleanOpt::Get().loop)WaterCleanOpt::Replace(source,"[unroll]for(int i=0;i<3;++i)","[loop]for(int i=0;i<3;++i)");
    }
    return source;
}
}
