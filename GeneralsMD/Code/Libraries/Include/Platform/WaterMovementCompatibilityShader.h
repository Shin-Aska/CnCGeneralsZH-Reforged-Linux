/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once
#include <string>
// v3.5.1 sector evaluator, using the existing raw FFT decoder and declarations.
// Entry names and the negative compatibility-mode gate avoid modern collisions.
namespace WaterMovementCompatibilityShader {
inline std::string HeadingSource() {
    return R"HLSL(
struct FftCompatibilityHeading {float2 rotation0,rotation1,weights;};
FftCompatibilityHeading fftCompatibilityHeading(float2 p) {
    float4 packed=FftHeadingTexture.SampleLevel(FftHeadingSampler,(p+TerrainParameters.xx+5.0)/max(TerrainParameters.y,1.0),0);
    float2 heading=packed.rg*2.0-1.0;
    FftCompatibilityHeading value;
    // Invalid/degenerate texels use the per-level prevailing rotation, without sector blending.
    if(packed.a<=0.0 || dot(heading,heading)<0.0025) {
        sincos(FftParameters.w,value.rotation0.y,value.rotation0.x);
        value.rotation1=value.rotation0;value.weights=float2(1,0);return value;
    }
    float phi=atan2(fftBaseHeading.x*heading.y-fftBaseHeading.y*heading.x,dot(fftBaseHeading,heading));
    float sector=phi/0.7853981633974483,k=floor(sector),t=frac(sector);
    float u=saturate((t-0.25)/0.5);
    sincos(k*0.7853981633974483,value.rotation0.y,value.rotation0.x);
    sincos((k+1.0)*0.7853981633974483,value.rotation1.y,value.rotation1.x);
    // Explicit endpoints make the zero-weight branch exact (cos(pi/2) need not be zero).
    value.weights=u<=0.0?float2(1,0):u>=1.0?float2(0,1):float2(cos(u*1.5707963267948966),sin(u*1.5707963267948966));
    return value;
}
)HLSL";
}
inline std::string VertexSource() {
    return R"HLSL(
float3 fftCompatibilityDisplacement(float2 p) {
    [branch] if(FftParameters.z>=-0.5) return fftUnrotatedDisplacement(p);
    FftCompatibilityHeading heading=fftCompatibilityHeading(p);float3 result=0;
    [branch] if(heading.weights.x>0.0)
        result+=heading.weights.x*fftRotateVector(fftUnrotatedDisplacement(fftRotate(p,heading.rotation0*float2(1,-1))),heading.rotation0);
    [branch] if(heading.weights.y>0.0)
        result+=heading.weights.y*fftRotateVector(fftUnrotatedDisplacement(fftRotate(p,heading.rotation1*float2(1,-1))),heading.rotation1);
    return result;
}
)HLSL";
}
inline std::string PixelSource() {
    return R"HLSL(
void fftCompatibilityAccumulateHeading(inout FftSurface result,FftSurface value,float2 cs,float weight) {
    result.displacement+=weight*fftRotateVector(value.displacement,cs);
    // Chain rule for q=R(-a)p, then R(a) on displacement XY. The smooth CPU field
    // permits ignoring spatial derivatives of the sector weights and rotations.
    result.derivativeX+=weight*fftRotateVector(cs.x*value.derivativeX-cs.y*value.derivativeY,cs);
    result.derivativeY+=weight*fftRotateVector(cs.y*value.derivativeX+cs.x*value.derivativeY,cs);
    result.contactIncident+=weight*float3(value.contactIncident.x,fftRotate(value.contactIncident.yz,cs));
    result.incidentCompression+=weight*(value.incidentCompression-1.0);
}
FftSurface fftCompatibilitySurface(float2 p,float3 lod) {
    [branch] if(FftParameters.z>=-0.5) return fftUnrotatedSurface(p,lod);
    FftCompatibilityHeading heading=fftCompatibilityHeading(p);FftSurface result=(FftSurface)0;result.incidentCompression=1;
    [branch] if(heading.weights.x>0.0)
        fftCompatibilityAccumulateHeading(result,fftUnrotatedSurface(fftRotate(p,heading.rotation0*float2(1,-1)),lod),heading.rotation0,heading.weights.x);
    [branch] if(heading.weights.y>0.0)
        fftCompatibilityAccumulateHeading(result,fftUnrotatedSurface(fftRotate(p,heading.rotation1*float2(1,-1)),lod),heading.rotation1,heading.weights.y);
    return result;
}
FftSurface fftCompatibilitySurface(float2 p) {return fftCompatibilitySurface(p,float3(0,0,0));}
void fftCompatibilityAndShadingSurface(float2 p,float3 lod,out FftSurface physical,out FftSurface shading) {
    [branch] if(FftParameters.z>=-0.5) {fftUnrotatedAndShadingSurface(p,lod,physical,shading);return;}
    FftCompatibilityHeading heading=fftCompatibilityHeading(p);
    physical=(FftSurface)0;physical.incidentCompression=1;shading=physical;
    FftSurface a,b;
    [branch] if(heading.weights.x>0.0) {
        fftUnrotatedAndShadingSurface(fftRotate(p,heading.rotation0*float2(1,-1)),lod,a,b);
        fftCompatibilityAccumulateHeading(physical,a,heading.rotation0,heading.weights.x);
        fftCompatibilityAccumulateHeading(shading,b,heading.rotation0,heading.weights.x);
    }
    [branch] if(heading.weights.y>0.0) {
        fftUnrotatedAndShadingSurface(fftRotate(p,heading.rotation1*float2(1,-1)),lod,a,b);
        fftCompatibilityAccumulateHeading(physical,a,heading.rotation1,heading.weights.y);
        fftCompatibilityAccumulateHeading(shading,b,heading.rotation1,heading.weights.y);
    }
}
)HLSL";
}
}
