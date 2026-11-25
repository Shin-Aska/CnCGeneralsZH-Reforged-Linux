// Hardware intersections for the existing water path tracer's bounded transport.
#pragma once
#include "Platform/WaterHardwareReflectionShader.h"
namespace WaterPathTracing {
inline const char *HardwarePathQuerySource()
{
    return R"glsl(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0,rgba32f) uniform writeonly image2D primaryHits;
layout(set=0,binding=1) uniform accelerationStructureEXT scene;
layout(set=0,binding=2) uniform sampler2D triangles;
layout(set=0,binding=3,rgba32f) uniform writeonly image2D transportHits;
layout(push_constant) uniform Params {
    vec4 OriginWater, RayTopLeft, RayDx, RayDy, SunDirection;
    float maxDistance, shadowDistance, secondaryDistance, epsilon;
    uint sampleIndex, padding0, padding1, padding2;
} p;
vec3 triangleRecord(uint index) {
    uint width=uint(textureSize(triangles,0).x);
    return texelFetch(triangles,ivec2(index%width,index/width),0).xyz;
}
vec3 windingNormal(uint index) {
    return normalize(cross(triangleRecord(index*4u+1u),triangleRecord(index*4u+2u)));
}
uint pathHash(uint value) {
    value^=value>>16; value*=0x7feb352du; value^=value>>15;
    value*=0x846ca68bu; return value^(value>>16);
}
float pathUnit(uint value) { return float(pathHash(value)>>8)*(1.0/16777216.0); }
vec2 continuationSample(uint pixel) {
    vec2 rotation=vec2(pathUnit(pixel+0x9e3779b9u),pathUnit(pixel+0x85ebca6bu+0x68bc21ebu));
    return fract(rotation+(float(p.sampleIndex)+1.0)*vec2(0.236067977,0.645751311));
}
vec3 pathCosine(vec3 normal,vec2 xi) {
    float radius=sqrt(xi.x),angle=6.28318530717958647692*xi.y;
    vec3 tangent=normalize(cross(abs(normal.z)<0.999?vec3(0,0,1):vec3(0,1,0),normal));
    vec3 bitangent=cross(normal,tangent);
    return normalize(tangent*(radius*cos(angle))+bitangent*(radius*sin(angle))
        +normal*sqrt(max(0.0,1.0-xi.x)));
}
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy),size=imageSize(primaryHits);
    if(any(greaterThanEqual(pixel,size))) return;
    vec4 primary=vec4(0,-1,0,0),transport=vec4(-1,0,1,0);
    vec2 uv=(vec2(pixel)+vec2(0.5))/vec2(size);
    vec3 ray=normalize(p.RayTopLeft.xyz+p.RayDx.xyz*uv.x+p.RayDy.xyz*uv.y);
    float epsilon=max(p.epsilon,0.0001);
    float crossing=ray.z>1e-6?(p.OriginWater.w-p.OriginWater.z)/ray.z:-1.0;
    if(crossing>=0.0 && p.maxDistance>=epsilon) {
        vec3 origin=p.OriginWater.xyz+ray*(crossing+epsilon);
        rayQueryEXT query;
        rayQueryInitializeEXT(query,scene,gl_RayFlagsOpaqueEXT,0xff,origin,epsilon,ray,p.maxDistance);
        while(rayQueryProceedEXT(query)) {}
        if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionTriangleEXT) {
            float distance=rayQueryGetIntersectionTEXT(query,true);
            uint triangle=rayQueryGetIntersectionInstanceCustomIndexEXT(query,true)
                +rayQueryGetIntersectionPrimitiveIndexEXT(query,true);
            primary=vec4(distance,float(triangle),rayQueryGetIntersectionBarycentricsEXT(query,true));
            vec3 hitPoint=origin+ray*distance;
            vec3 normal=windingNormal(triangle);
            if(dot(normal,ray)>0.0) normal=-normal;
            vec3 sun=p.SunDirection.xyz*inversesqrt(max(dot(p.SunDirection.xyz,p.SunDirection.xyz),1e-20));
            if(p.SunDirection.w>0.0 && dot(normal,sun)>0.0) {
                // A range shorter than tMin has no possible blocker; avoid invalid query initialization.
                transport.w=1.0;
                if(p.shadowDistance>=epsilon) {
                    rayQueryEXT shadow;
                    rayQueryInitializeEXT(shadow,scene,gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT,
                        0xff,hitPoint+normal*epsilon,epsilon,sun,p.shadowDistance);
                    while(rayQueryProceedEXT(shadow)) {}
                    transport.w=rayQueryGetIntersectionTypeEXT(shadow,true)==gl_RayQueryCommittedIntersectionNoneEXT?1.0:0.0;
                }
            }
            uint seed=pathHash(uint(pixel.x)+uint(pixel.y)*uint(size.x));
            vec3 continuation=pathCosine(normal,continuationSample(seed));
            transport.y=continuation.z;
            if(p.secondaryDistance>=epsilon) {
                rayQueryEXT secondary;
                rayQueryInitializeEXT(secondary,scene,gl_RayFlagsOpaqueEXT,0xff,
                    hitPoint+normal*epsilon,epsilon,continuation,p.secondaryDistance);
                while(rayQueryProceedEXT(secondary)) {}
                if(rayQueryGetIntersectionTypeEXT(secondary,true)==gl_RayQueryCommittedIntersectionTriangleEXT) {
                    uint secondaryTriangle=rayQueryGetIntersectionInstanceCustomIndexEXT(secondary,true)
                        +rayQueryGetIntersectionPrimitiveIndexEXT(secondary,true);
                    transport.x=float(secondaryTriangle);
                    transport.z=dot(windingNormal(secondaryTriangle),continuation)>0.0?-1.0:1.0;
                }
            }
        }
    }
    imageStore(primaryHits,pixel,primary);
    imageStore(transportHits,pixel,transport);
}
)glsl";
}
inline std::string HardwarePathFragmentSource()
{
    return std::string("#define WATER_HARDWARE_REFLECTION 1\n#define WATER_HARDWARE_PATH 1\n") + FragmentSource();
}
}
