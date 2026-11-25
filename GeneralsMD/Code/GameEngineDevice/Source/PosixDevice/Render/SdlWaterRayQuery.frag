#version 460
#extension GL_EXT_ray_query : require

// Samples 1 to 32 match WaterScreenReflection.h. 33 denotes a miss.
uint ZHR_WaterRayFirstStep(vec3 rayStart, vec3 rayEnd, float jitter, uvec4 addresses)
{
    if ((addresses.x | addresses.y) == 0u)
        return 1u;
    vec3 rayDirection = rayEnd - rayStart;
    if (any(isnan(rayStart)) || any(isinf(rayStart)) || any(isnan(rayDirection)) ||
        any(isinf(rayDirection)) || dot(rayDirection, rayDirection) < 1e-8)
        return 1u;
    rayQueryEXT query;
    rayQueryInitializeEXT(query, accelerationStructureEXT(addresses.xy), 0, 0xff,
        rayStart, 0, rayDirection, 1);
    if (!rayQueryProceedEXT(query))
        return 33u;
    rayQueryTerminateEXT(query);

    // A second traversal pays off only on longer projected rays. Its range
    // covers the first half of WaterScreenReflection's march for any jitter
    // phase, with room beyond pow(0.5, 1.6) for rounding.
    if (dot(rayDirection.xy, rayDirection.xy) < 4096.0)
        return 1u;
    rayQueryInitializeEXT(query, accelerationStructureEXT(addresses.xy), 0, 0xff,
        rayStart, 0, rayDirection, 0.34);
    if (!rayQueryProceedEXT(query))
        return 17u;
    rayQueryTerminateEXT(query);
    return 1u;
}

layout(set=3,binding=0) uniform Arguments { uvec4 addresses; } args;
layout(location=0) out vec4 color;
void main()
{
    color = vec4(float(ZHR_WaterRayFirstStep(vec3(gl_FragCoord.xy, 0),
        vec3(gl_FragCoord.xy, 1), 0.5, args.addresses)));
}
