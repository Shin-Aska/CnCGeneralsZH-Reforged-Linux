// One hardware closest-hit reflection. Reuses the water capture's material/footprint helpers,
// but launches no sun-visibility or diffuse-continuation rays.
#pragma once
#include "Platform/WaterPathTracingShader.h"
#include <string>
namespace WaterPathTracing {
inline const char *HardwareQuerySource()
{
    return R"glsl(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0,rgba32f) uniform writeonly image2D hits;
layout(set=0,binding=1) uniform accelerationStructureEXT scene;
layout(push_constant) uniform Params {
    vec4 OriginWater, RayTopLeft, RayDx, RayDy;
    float maxDistance;
    float padding0, padding1, padding2;
} p;
void main() {
    ivec2 pixel=ivec2(gl_GlobalInvocationID.xy), size=imageSize(hits);
    if(any(greaterThanEqual(pixel,size))) return;
    vec2 uv=(vec2(pixel)+vec2(0.5))/vec2(size);
    vec3 ray=normalize(p.RayTopLeft.xyz+p.RayDx.xyz*uv.x+p.RayDy.xyz*uv.y);
    float crossing=ray.z>1e-6 ? (p.OriginWater.w-p.OriginWater.z)/ray.z : -1.0;
    vec4 result=vec4(0.0,-1.0,0.0,0.0);
    if(crossing>=0.0 && p.maxDistance>=0.02) {
        vec3 origin=p.OriginWater.xyz+ray*(crossing+0.02);
        rayQueryEXT query;
        rayQueryInitializeEXT(query,scene,gl_RayFlagsOpaqueEXT,0xff,
            origin,0.02,ray,p.maxDistance);
        while(rayQueryProceedEXT(query)) {}
        if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionTriangleEXT) {
            result=vec4(rayQueryGetIntersectionTEXT(query,true),
                float(rayQueryGetIntersectionPrimitiveIndexEXT(query,true)),
                rayQueryGetIntersectionBarycentricsEXT(query,true));
        }
    }
    imageStore(hits,pixel,result);
}
)glsl";
}

inline std::string HardwareFragmentSource()
{
    return std::string("#define WATER_HARDWARE_REFLECTION 1\n") + FragmentSource();
}
}
