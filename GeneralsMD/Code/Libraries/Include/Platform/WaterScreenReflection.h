// Screen-space reflection for Modern map water. Render-only.
//
// Three ways to reflect the scene; ZH_WATER_SSR overrides the live setting when it names one:
//   "mirror", "0"          the planar mirror pass alone, as before
//   "ssr", "1", "replace"  no mirror pass; the reflected ray is traced through the pre-water scene snapshot
//   "hybrid"               the mirror pass, with a traced hit taking its place where one is found
//   "nomirror"             neither, sky only: times what the mirror costs
// Unset or unrecognised values use the setting, which starts at Hybrid.
//
// The shader always carries the trace; the material register's reserved lane (c95.w) turns it on
// per draw, so a mode needs no new program. ZH_WATER_DEBUG=ssr shows the traced colour over dark
// blue misses; ZH_WATER_DEBUG=ssr-hit shows confidence (red), travel (green), refusal (blue).
#pragma once
#include <cstdlib>
#include <cstring>

namespace WaterScreenReflection {

enum Mode { Mirror, ScreenSpace, Hybrid, SkyOnly };

// An absent or unknown value leaves mode alone and does not override the live setting.
inline bool Parse(const char *value, Mode &mode)
{
	if (!value) return false;
	if (std::strcmp(value, "mirror") == 0 || std::strcmp(value, "0") == 0) mode = Mirror;
	else if (std::strcmp(value, "ssr") == 0 || std::strcmp(value, "1") == 0 || std::strcmp(value, "replace") == 0)
		mode = ScreenSpace;
	else if (std::strcmp(value, "hybrid") == 0) mode = Hybrid;
	else if (std::strcmp(value, "nomirror") == 0) mode = SkyOnly;
	else return false;
	return true;
}

inline Mode Resolve(const char *environment, int setting)
{
	Mode mode;
	if (Parse(environment, mode)) return mode;
	switch (setting) {
		case 0: return Mirror;
		case 1: return ScreenSpace;
		default: return Hybrid;
	}
}

inline Mode Next(Mode mode)
{
	switch (mode) {
		case Mirror: return ScreenSpace;
		case ScreenSpace: return Hybrid;
		default: return Mirror;
	}
}

inline const char *CachedEnvironment()
{
	static const char *const environment = std::getenv("ZH_WATER_SSR");
	return environment;
}
inline bool Traces(Mode mode) { return mode == ScreenSpace || mode == Hybrid; }
inline bool UsesMirror(Mode mode) { return mode == Mirror || mode == Hybrid; }
inline const char *Name(Mode mode)
{
	switch (mode) {
		case Mirror: return "mirror";
		case ScreenSpace: return "screen space";
		case Hybrid: return "hybrid";
		default: return "sky only";
	}
}

// Needs the water shader's WorldFromClip, Viewport, CameraPosition, SceneParameters and MaterialParameters, the
// scene colour (Texture5) and depth (Texture6) snapshots, and waterScenePixel/scenePosition.
inline const char *ShaderSource() { return R"HLSL(
// The spike inverts WorldFromClip per pixel rather than adding a uniform; Phase 1 uploads it.
float4x4 waterSsrInverse(float4x4 m)
{
    float b00=m[0][0]*m[1][1]-m[0][1]*m[1][0], b01=m[0][0]*m[1][2]-m[0][2]*m[1][0];
    float b02=m[0][0]*m[1][3]-m[0][3]*m[1][0], b03=m[0][1]*m[1][2]-m[0][2]*m[1][1];
    float b04=m[0][1]*m[1][3]-m[0][3]*m[1][1], b05=m[0][2]*m[1][3]-m[0][3]*m[1][2];
    float b06=m[2][0]*m[3][1]-m[2][1]*m[3][0], b07=m[2][0]*m[3][2]-m[2][2]*m[3][0];
    float b08=m[2][0]*m[3][3]-m[2][3]*m[3][0], b09=m[2][1]*m[3][2]-m[2][2]*m[3][1];
    float b10=m[2][1]*m[3][3]-m[2][3]*m[3][1], b11=m[2][2]*m[3][3]-m[2][3]*m[3][2];
    float det=b00*b11-b01*b10+b02*b09+b03*b08-b04*b07+b05*b06;
    float s=abs(det)>1e-20?1.0/det:0.0;
    float4x4 r;
    r[0]=float4(m[1][1]*b11-m[1][2]*b10+m[1][3]*b09, m[0][2]*b10-m[0][1]*b11-m[0][3]*b09,
                m[3][1]*b05-m[3][2]*b04+m[3][3]*b03, m[2][2]*b04-m[2][1]*b05-m[2][3]*b03);
    r[1]=float4(m[1][2]*b08-m[1][0]*b11-m[1][3]*b07, m[0][0]*b11-m[0][2]*b08+m[0][3]*b07,
                m[3][2]*b02-m[3][0]*b05-m[3][3]*b01, m[2][0]*b05-m[2][2]*b02+m[2][3]*b01);
    r[2]=float4(m[1][0]*b10-m[1][1]*b08+m[1][3]*b06, m[0][1]*b08-m[0][0]*b10-m[0][3]*b06,
                m[3][0]*b04-m[3][1]*b02+m[3][3]*b00, m[2][1]*b02-m[2][0]*b04-m[2][3]*b00);
    r[3]=float4(m[1][1]*b07-m[1][0]*b09-m[1][2]*b06, m[0][0]*b09-m[0][1]*b07+m[0][2]*b06,
                m[3][1]*b01-m[3][0]*b03-m[3][2]*b00, m[2][0]*b03-m[2][1]*b01+m[2][2]*b00);
    return r*s;
}
float2 waterSsrPixel(float4 clip)
{
    float2 ndc=clip.xy/clip.w;
    return Viewport.xy+(ndc*float2(0.5,-0.5)+0.5)*Viewport.zw;
}
struct WaterSsrSample
{
    float3 colour;     // display-space scene colour at the hit
    float confidence;  // 0 for a miss
    float travel;      // fraction of the traced distance at the hit
    float refused;     // 1 when no trace was attempted
};
static const int WaterSsrSteps=32;
static const int WaterSsrRefinements=5;
static const float WaterSsrDistance=700.0;  // world units; a map cell is 10
// How far behind a depth sample a ray may pass and still hit it: a few world units, plus two
// pixels of the ray's own depth travel. A fixed allowance as wide as a building also accepts
// every ray passing behind a thin lintel or pillar, which fills the open water between them.
static const float WaterSsrThickness=3.0;
// The share of the wave normal the trace follows. The mirror only nudges its lookup by the
// normal; a fully rippled ray scatters a reflection into speckle over a long trace.
static const float WaterSsrNormalWeight=0.3;
// A downward reflected ray meets the water itself, and the snapshot holds no water. Rays leaving
// the screen, or passing behind everything, fall back to the caller's reflection.
WaterSsrSample waterScreenReflection(float3 world, float3 view, float3 normal, float roughness, float2 pixel)
{
    float3 ray=reflect(-view, normalize(lerp(float3(0,0,1), normal, WaterSsrNormalWeight)));
    WaterSsrSample result;
    result.colour=float3(0,0,0); result.confidence=0.0; result.travel=1.0; result.refused=1.0;
    [branch] if (!(MaterialParameters.w>0.5 && SceneParameters.x>0.5 && SceneParameters.w>0.5) || ray.z<0.02)
        return result;
    float4x4 clipFromWorld=waterSsrInverse(WorldFromClip);
    float4 c0=mul(float4(world,1),clipFromWorld);
    float4 c1=mul(float4(world+ray*WaterSsrDistance,1),clipFromWorld);
    [branch] if (!(c0.w>0.0)) return result;
    // Keep the segment in front of the camera.
    float nearW=c0.w*0.05;
    if (c1.w<nearW) c1=lerp(c0,c1,(c0.w-nearW)/max(c0.w-c1.w,1e-5));
    float2 p0=waterSsrPixel(c0), p1=waterSsrPixel(c1);
    float z0=c0.z/c0.w, z1=c1.z/c1.w;
    // Device depth is affine in screen space along a projected line, so it interpolates with the pixel.
    float2 lo=waterScenePixel(float2(-1e9,-1e9)), hi=waterScenePixel(float2(1e9,1e9));
    float2 d=p1-p0;
    float2 exitT=float2(d.x>0.0?(hi.x-p0.x)/d.x:(d.x<0.0?(lo.x-p0.x)/d.x:1.0),
                        d.y>0.0?(hi.y-p0.y)/d.y:(d.y<0.0?(lo.y-p0.y)/d.y:1.0));
    float span=saturate(min(exitT.x,exitT.y));
    p1=lerp(p0,p1,span); z1=lerp(z0,z1,span);
    [branch] if (length(p1-p0)<2.0) return result;
    result.refused=0.0;
    float jitter=frac(52.9829189*frac(dot(pixel,float2(0.06711056,0.00583715))));
    float previous=0.0, found=-1.0;
    [loop] for (int i=1; i<=WaterSsrSteps && found<0.0; ++i) {
        // Denser near the surface, where contact reflections are.
        float t=pow((float(i)-1.0+jitter)/float(WaterSsrSteps),1.6);
        float2 at=waterScenePixel(floor(lerp(p0,p1,t))+0.5);
        float rayDepth=lerp(z0,z1,t);
        float sceneDepth=Texture6.SampleLevel(Sampler6,at*SceneParameters.yz,0).r;
        [branch] if (sceneDepth<0.999999 && rayDepth>sceneDepth) {
            float a=previous, b=t;
            [unroll] for (int r=0; r<WaterSsrRefinements; ++r) {
                float m=(a+b)*0.5;
                float2 mp=waterScenePixel(floor(lerp(p0,p1,m))+0.5);
                float md=Texture6.SampleLevel(Sampler6,mp*SceneParameters.yz,0).r;
                if (md<0.999999 && lerp(z0,z1,m)>md) b=m; else a=m;
            }
            float2 hp=waterScenePixel(floor(lerp(p0,p1,b))+0.5);
            float hd=Texture6.SampleLevel(Sampler6,hp*SceneParameters.yz,0).r;
            float3 sceneHit=scenePosition(hp,hd);
            float3 rayHit=scenePosition(hp,lerp(z0,z1,b));
            float behind=length(rayHit-CameraPosition.xyz)-length(sceneHit-CameraPosition.xyz);
            float onePixel=1.0/max(length(p1-p0),1.0);
            float3 rayBefore=scenePosition(lerp(p0,p1,max(b-onePixel,0.0)),lerp(z0,z1,max(b-onePixel,0.0)));
            float pixelTravel=abs(length(rayHit-CameraPosition.xyz)-length(rayBefore-CameraPosition.xyz));
            // Under-water scene (the bed the refraction sees) cannot be met by an upward ray.
            if (hd<0.999999 && behind<=WaterSsrThickness+2.0*pixelTravel && sceneHit.z>world.z-1.0) found=b;
        }
        previous=t;
    }
    [branch] if (found<0.0) return result;
    float2 hit=lerp(p0,p1,found);
    float2 radius=float2(1.0,1.0)+saturate(roughness)*6.0;
    float3 colour=Texture5.SampleLevel(Sampler5,waterScenePixel(hit)*SceneParameters.yz,0).rgb*0.4;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit+float2(radius.x,0))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit-float2(radius.x,0))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit+float2(0,radius.y))*SceneParameters.yz,0).rgb*0.15;
    colour+=Texture5.SampleLevel(Sampler5,waterScenePixel(hit-float2(0,radius.y))*SceneParameters.yz,0).rgb*0.15;
    float2 border=min(hit-lo,hi-hit)/max(min(Viewport.z,Viewport.w)*0.06,1.0);
    result.colour=colour;
    result.travel=found*span;
    result.confidence=saturate(min(border.x,border.y))*(1.0-smoothstep(0.75,1.0,result.travel));
    return result;
}
)HLSL"; }

}
