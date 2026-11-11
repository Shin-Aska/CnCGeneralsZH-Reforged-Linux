/* Copyright 2026. SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "Platform/WaterCleanOpt.h"

namespace WaterCleanStream {
// No per-vertex arrays: keep the triangle and three scalar weights. Rotations
// are read at the point of use, and the same point serves every selected band.
inline std::string Tiles() {return R"HLSL(
struct FftShoreTiles {
    float2 cell;bool upper,active,collapsed;
    float3 barycentric,normalization;
    float2 meanRotation,meanVertex,vertexX,vertexY,angleGradient;
    float3 scale;float debugAngle;
};
float2 fftTileIndex(FftShoreTiles t,int i) {
    float2 offset=i==0?float2(0,0):i==1?float2(1,0):float2(0,1);
    return t.cell+(t.upper?1-offset:offset);
}
float2 fftTileVertex(FftShoreTiles t,int i) {
    float2 index=fftTileIndex(t,i);
    return float2(index.x+.5*index.y,.866025404*index.y)*fftShoreSettings.z;
}
float3 fftTileWeight(FftShoreTiles t,int i) {
    float b=t.barycentric[i],sharp=fftShoreSettings.w;
    float weight=pow(b,sharp)*t.normalization.x;
    float derivative=sharp*pow(max(b,1e-12),sharp-1);
    float2 gradient=i==0?float2(-1,-.577350269):i==1?float2(1,-.577350269):float2(0,1.154700538);
    gradient*=t.upper?-1:1;gradient/=fftShoreSettings.z;
    return float3(weight,(derivative*gradient-weight*t.normalization.yz)*t.normalization.x);
}
float4 fftTileRotation(FftShoreTiles t,int i) {
    uint depthWidth,depthHeight,width,height;
    FftCleanDepth.GetDimensions(depthWidth,depthHeight);FftCleanRotation.GetDimensions(width,height);
    float spacing=TerrainParameters.y/max(float(depthWidth),1.0),low=-TerrainParameters.x;
    float high=(depthWidth-1)*spacing-TerrainParameters.x;
    int2 origin=int2(floor(float2(low-.577350269*high,1.154700538*low)/fftShoreSettings.z))-1;
    int2 index=int2(fftTileIndex(t,i))-origin;
    if(depthWidth>=2 && width>=2 && height>=2 && all(index>=0) && all(index<int2(width,height))) {
        float4 cached=FftCleanRotation.SampleLevel(FftCleanRotationSampler,(float2(index)+.5)/float2(width,height),0);
        if(cached.w>.5)return cached;
    }
    return float4(1,0,0,1);
}
FftShoreTiles fftShoreTiles(float2 p) {
    FftShoreTiles t=(FftShoreTiles)0;
    if(fftHeadingFollow==0 && fftShoreSettings.x==0)return t;
    float tile=fftShoreSettings.z;float2 uv=float2(p.x-p.y*.577350269,p.y*1.154700538)/tile;
    t.cell=floor(uv);float2 f=frac(uv);t.upper=f.x+f.y>1;
    float2 ux=float2(1,-.577350269)/tile,uy=float2(0,1.154700538)/tile;
    float3 b=t.upper?float3(f.x+f.y-1,1-f.x,1-f.y):float3(1-f.x-f.y,f.x,f.y);
    float3 bx=float3(-ux.x-uy.x,ux.x,uy.x),by=float3(-ux.y-uy.y,ux.y,uy.y);
    if(t.upper) {bx=-bx;by=-by;}
    b=saturate(b);float sharp=fftShoreSettings.w;float3 a=pow(b,sharp);
    float3 powerDerivative=sharp*pow(max(b,1e-12),sharp-1),ax=powerDerivative*bx,ay=powerDerivative*by;
    float inverseNorm=rsqrt(max(dot(a,a),1e-20));
    float3 weight=a*inverseNorm;
    t.barycentric=b;t.normalization=float3(inverseNorm,dot(weight,ax),dot(weight,ay));
    // Only the collapse decision needs all angles. The stream never carries
    // three positions/rotations/angles through the FFT sampling block.
    float3 angles=float3(fftTileRotation(t,0).z,fftTileRotation(t,1).z,fftTileRotation(t,2).z);
    t.active=any(abs(angles)>1e-6);
    float sum=dot(weight,float3(1,1,1));
    t.debugAngle=dot(weight,angles)/max(sum,1e-5);
    float first=angles.x;angles-=first;
    angles.yz-=6.283185307*floor((angles.yz+3.141592654)/6.283185307);
    t.collapsed=max(angles.x,max(angles.y,angles.z))-min(angles.x,min(angles.y,angles.z))<fftTileCollapseAngle;
    float offset=dot(weight,angles)/sum;
    if(t.active && t.collapsed) {
        float3 dx=(ax-weight*t.normalization.y)*inverseNorm,dy=(ay-weight*t.normalization.z)*inverseNorm;
        float sx=dot(dx,float3(1,1,1)),sy=dot(dy,float3(1,1,1));
        t.angleGradient=float2(dot(dx,angles)-offset*sx,dot(dy,angles)-offset*sy)/sum;
        float2 v0=fftTileVertex(t,0),v1=fftTileVertex(t,1),v2=fftTileVertex(t,2);
        t.meanVertex=(weight.x*v0+weight.y*v1+weight.z*v2)/sum;
        t.vertexX=(dx.x*v0+dx.y*v1+dx.z*v2-t.meanVertex*sx)/sum;
        t.vertexY=(dy.x*v0+dy.y*v1+dy.z*v2-t.meanVertex*sy)/sum;
        sincos(first+offset,t.meanRotation.y,t.meanRotation.x);t.scale=float3(sum,sx,sy);
    }
    return t;
}
float3 fftShoreDebug(FftShoreTiles t) {
    if(fftHeadingFollow==0 && fftShoreSettings.x==0)return 0;
    float angle=t.debugAngle;float2 direction=fftShoreRotate(fftShoreWind,angle);
    float hue=atan2(direction.y,direction.x)/6.283185307+.5;
    float3 rgb=saturate(abs(frac(hue+float3(0,2.0/3,1.0/3))*6-3)-1);
    float minimum=pow(min(t.barycentric.x,min(t.barycentric.y,t.barycentric.z)),fftShoreSettings.w)*t.normalization.x;
    float edge=.03*(1-smoothstep(0,.035,minimum));
    return rgb*saturate(abs(angle)/0.785398163)+edge;
}
float3 fftShoreDebug(float2 p) {return fftShoreDebug(fftShoreTiles(p));}
)HLSL";}

// One point's coordinate and chain rule, shared across bands and LODs.
inline std::string Point(bool stream) {
    std::string s=R"HLSL(
struct FftShorePoint {float2 q,rotation,qx,qy,angleGradient;float3 weight;};
FftShorePoint fftShorePoint(float2 p,FftShoreTiles tiles,int i) {
    FftShorePoint v=(FftShorePoint)0;
    if(tiles.collapsed) {
        v.rotation=tiles.meanRotation;float2 inverseRotation=v.rotation*float2(1,-1);
        float2 relative=fftRotate(p-tiles.meanVertex,inverseRotation);
        v.q=tiles.meanVertex+relative;
        float2 perpendicular=float2(-relative.y,relative.x);
        v.qx=tiles.vertexX+fftRotate(float2(1,0)-tiles.vertexX,inverseRotation)-perpendicular*tiles.angleGradient.x;
        v.qy=tiles.vertexY+fftRotate(float2(0,1)-tiles.vertexY,inverseRotation)-perpendicular*tiles.angleGradient.y;
        v.angleGradient=tiles.angleGradient;v.weight=tiles.scale;
    } else {
        FFT_STREAM_POINT
        float2 inverseRotation=v.rotation*float2(1,-1);
        v.q=vertex+fftRotate(p-vertex,inverseRotation);
        v.qx=fftRotate(float2(1,0),inverseRotation);v.qy=fftRotate(float2(0,1),inverseRotation);
        FFT_STREAM_WEIGHT
    }
    return v;
}
)HLSL";
    WaterCleanOpt::Replace(s,"FFT_STREAM_POINT",stream?
        "float2 vertex=fftTileVertex(tiles,i);v.rotation=fftTileRotation(tiles,i).xy;":
        "float2 vertex=tiles.vertex[i];v.rotation=tiles.rotation[i];");
    WaterCleanOpt::Replace(s,"FFT_STREAM_WEIGHT",stream?"v.weight=fftTileWeight(tiles,i);":"v.weight=float3(tiles.weight[i],tiles.dx[i],tiles.dy[i]);");
    return s;
}

inline std::string Vertex() {
    std::string s=R"HLSL(
float3 fftShoreVertexSample(float2 q,int band) {
    if(band==0)return FftDisplacement0.SampleLevel(FftSampler0,q/(450.0*fftWorldScale),0).xzy;
    if(band==1)return FftDisplacement1.SampleLevel(FftSampler1,q/(103.0*fftWorldScale),0).xzy;
    return FftDisplacement2.SampleLevel(FftSampler2,q/(13.0*fftWorldScale),0).xzy;
}
float3 fftCleanPreparedDisplacement(float2 p,float4 depth,FftShoreTiles tiles) {
    if(!tiles.active)return fftCleanBaseDisplacement(p,depth);
    float3 longGain=float3(-FFT_CLEAN_CHOP_LONG,-FFT_CLEAN_CHOP_LONG,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG).x;
    float3 mediumGain=float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM).x;
    float3 fineGain=float3(-FFT_CLEAN_CHOP_SHORT,-FFT_CLEAN_CHOP_SHORT,1)*fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE).x;
    float3 a=0,b=0,c=0;
    FFT_TILE_LOOP for(int i=0;i<(tiles.collapsed?1:3);++i) {
        float2 rotation=tiles.collapsed?tiles.meanRotation:fftTileRotation(tiles,i).xy;
        float2 vertex=tiles.collapsed?tiles.meanVertex:fftTileVertex(tiles,i);
        float2 q=vertex+fftRotate(p-vertex,rotation*float2(1,-1));
        float weight=tiles.collapsed?tiles.scale.x:fftTileWeight(tiles,i).x;
        if(fftShoreLong) {float3 d=fftShoreVertexSample(q,0);d.xy=fftRotate(d.xy,rotation);a+=weight*d;}
        {float3 d=fftShoreVertexSample(q,1);d.xy=fftRotate(d.xy,rotation);b+=weight*d;}
        if(fftShoreFine) {float3 d=fftShoreVertexSample(q,2);d.xy=fftRotate(d.xy,rotation);c+=weight*d;}
    }
    if(!fftShoreLong)a=fftShoreVertexSample(p,0);
    if(!fftShoreFine)c=fftShoreVertexSample(p,2);
    return (a*longGain+b*mediumGain+c*fineGain)*fftWorldScale;
}
float3 fftCleanDisplacement(float2 p,float4 depth) {return fftCleanPreparedDisplacement(p,depth,fftShoreTiles(p));}
)HLSL";
    WaterCleanOpt::Replace(s,"FFT_TILE_LOOP",WaterCleanOpt::Get().loop?"[loop]":"[unroll]");
    return s;
}

inline std::string PixelHelpers(bool fused) {
    std::string s=Point(WaterCleanOpt::Get().stream)+R"HLSL(
void fftShoreMaps(float2 q,int band,float lod,out float4 a,out float4 derivatives) {
    if(band==0) {a=Texture8.SampleLevel(Sampler8,q/(450.0*fftWorldScale),lod);derivatives=Texture9.SampleLevel(Sampler9,q/(450.0*fftWorldScale),lod);}
    else if(band==1) {a=Texture10.SampleLevel(Sampler10,q/(103.0*fftWorldScale),lod);derivatives=Texture11.SampleLevel(Sampler11,q/(103.0*fftWorldScale),lod);}
    else {a=Texture12.SampleLevel(Sampler12,q/(13.0*fftWorldScale),lod);derivatives=Texture13.SampleLevel(Sampler13,q/(13.0*fftWorldScale),lod);}
}
void fftShoreDecodePoint(FftShorePoint v,int band,float4 a,float4 derivatives,float horizontal,float3 gain,
    out float3 d,out float3 dx,out float3 dy) {
    d=a.xzy*float3(horizontal,horizontal,1)*fftWorldScale;
    float3 x=float3(derivatives.z,a.w,derivatives.x)*float3(horizontal,horizontal,1);
    float3 y=float3(a.w,derivatives.w,derivatives.y)*float3(horizontal,horizontal,1);
    FFT_FINE_LITE
    float3 xx=x*v.qx.x+y*v.qx.y,yy=x*v.qy.x+y*v.qy.y;
    float2 turn=float2(-d.y,d.x);
    xx.xy+=turn*v.angleGradient.x;yy.xy+=turn*v.angleGradient.y;
    d.xy=fftRotate(d.xy,v.rotation);xx.xy=fftRotate(xx.xy,v.rotation);yy.xy=fftRotate(yy.xy,v.rotation);
    dx=v.weight.x*xx+v.weight.y*d;dy=v.weight.x*yy+v.weight.z*d;d*=v.weight.x;
    fftMediumScale(d,dx,dy,gain);
}
void fftShoreAddPoint(inout FftSurface value,FftShorePoint v,int band,float lod,float horizontal,float3 gain) {
    float4 a,derivatives;fftShoreMaps(v.q,band,lod,a,derivatives);
    float3 d,dx,dy;fftShoreDecodePoint(v,band,a,derivatives,horizontal,gain,d,dx,dy);
    value.displacement+=d;value.derivativeX+=dx;value.derivativeY+=dy;
}
void fftShoreAddPhysical(inout FftSurface physical,inout FftSurface shading,FftShorePoint v,int band,float lod,float horizontal,float3 gain) {
    float4 a,derivatives;fftShoreMaps(v.q,band,0,a,derivatives);
    float3 d,dx,dy;fftShoreDecodePoint(v,band,a,derivatives,horizontal,gain,d,dx,dy);
    physical.displacement+=d;physical.derivativeX+=dx;physical.derivativeY+=dy;
    if(lod<=0) {shading.displacement+=d;shading.derivativeX+=dx;shading.derivativeY+=dy;}
}
void fftShoreAddPair(inout FftSurface physical,inout FftSurface shading,FftShorePoint v,int band,float lod,float horizontal,float3 gain) {
    float4 a,derivatives;fftShoreMaps(v.q,band,0,a,derivatives);
    float3 d,dx,dy;fftShoreDecodePoint(v,band,a,derivatives,horizontal,gain,d,dx,dy);
    physical.displacement+=d;physical.derivativeX+=dx;physical.derivativeY+=dy;
    if(lod>0) {
        fftShoreMaps(v.q,band,lod,a,derivatives);
        fftShoreDecodePoint(v,band,a,derivatives,horizontal,gain,d,dx,dy);
    }
    shading.displacement+=d;shading.derivativeX+=dx;shading.derivativeY+=dy;
}
)HLSL";
    WaterCleanOpt::Replace(s,"FFT_FINE_LITE",WaterCleanOpt::Get().finelite?"if(band==2) {d.xy=0;x.xy=0;y.xy=0;}":"");
    if(!fused) { // A single LOD walk, used twice when a band is filtered.
        s+=R"HLSL(
void fftShoreStreamSurface(inout FftSurface physical,inout FftSurface shading,float2 p,float4 depth,float3 lod,FftShoreTiles tiles,bool filtered) {
    float3 longGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_LONG),mediumGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_MEDIUM),fineGain=fftMediumGain(depth,FFT_CLEAN_DEPTH_FINE);
    FFT_TILE_LOOP for(int i=0;i<(tiles.collapsed?1:3);++i) {
        FftShorePoint v=fftShorePoint(p,tiles,i);
        if(!filtered) {
            if(fftShoreLong)fftShoreAddPhysical(physical,shading,v,0,lod.x,-FFT_CLEAN_CHOP_LONG,longGain);
            fftShoreAddPhysical(physical,shading,v,1,lod.y,-FFT_CLEAN_CHOP_SHORT,mediumGain);
            if(fftShoreFine)fftShoreAddPhysical(physical,shading,v,2,lod.z,-FFT_CLEAN_CHOP_SHORT,fineGain);
        } else {
            if(fftShoreLong && lod.x>0)fftShoreAddPoint(shading,v,0,lod.x,-FFT_CLEAN_CHOP_LONG,longGain);
            if(lod.y>0)fftShoreAddPoint(shading,v,1,lod.y,-FFT_CLEAN_CHOP_SHORT,mediumGain);
            if(fftShoreFine && lod.z>0)fftShoreAddPoint(shading,v,2,lod.z,-FFT_CLEAN_CHOP_SHORT,fineGain);
        }
    }
}
)HLSL";
    }
    WaterCleanOpt::Replace(s,"FFT_TILE_LOOP",WaterCleanOpt::Get().loop?"[loop]":"[unroll]");
    return s;
}
}
