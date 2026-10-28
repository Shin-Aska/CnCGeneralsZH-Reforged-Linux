// Dedicated planar capture shader for the optional water-only geometry path tracer.
#pragma once

namespace WaterPathTracing {

inline const char *VertexSource()
{
    return R"HLSL(
float4 main(uint vertex : SV_VertexID) : SV_Position
{
    float2 position=float2((vertex<<1)&2,vertex&2);
    return float4(position*float2(2,-2)+float2(-1,1),0,1);
}
)HLSL";
}

inline const char *FragmentSource()
{
    return R"HLSL(
Texture2D<float4> PathNodes : register(t0, space2);
Texture2D<float4> PathTriangles : register(t1, space2);
Texture2D<float4> PathPrevious : register(t2, space2);
Texture2D<float4> PathMaterials : register(t3, space2);
Texture2D<float4> PathTexels : register(t4, space2);
SamplerState PathNodesSampler : register(s0, space2);
SamplerState PathTrianglesSampler : register(s1, space2);
SamplerState PathPreviousSampler : register(s2, space2);
SamplerState PathMaterialsSampler : register(s3, space2);
SamplerState PathTexelsSampler : register(s4, space2);
cbuffer PathUniforms : register(b0, space3) {
    float4 OriginWater;
    float4 RayTopLeft;
    float4 RayDx;
    float4 RayDy;
    float4 SunDirection;
    float4 SunRadiance;
    float4 SkyLow;
    float4 SkyHigh;
    float4 Counts;
    float4 Output;
    float4 Transport;
};
static const float PathPi=3.14159265358979323846;
static const uint PathTextureWidth=2048u;
float4 pathNode(uint index) {
    return PathNodes.Load(int3(index%PathTextureWidth,index/PathTextureWidth,0));
}
float4 pathTriangle(uint index) {
    return PathTriangles.Load(int3(index%PathTextureWidth,index/PathTextureWidth,0));
}
uint pathHash(uint value) {
    value^=value>>16;value*=0x7feb352du;value^=value>>15;
    value*=0x846ca68bu;return value^(value>>16);
}
float pathUnit(uint value) { return float(pathHash(value)>>8)*(1.0/16777216.0); }
float2 pathSample(uint pixel,uint dimension) {
    float2 rotation=float2(pathUnit(pixel+dimension*0x9e3779b9u),
                           pathUnit(pixel+dimension*0x85ebca6bu+0x68bc21ebu));
    // Irrational increments decorrelate successive diffuse continuation samples.
    float2 increment=dimension==0u?float2(0.414213562,0.732050808)
                                  :float2(0.236067977,0.645751311);
    return frac(rotation+(Counts.z+1.0)*increment);
}
bool pathBounds(float3 low,float3 high,float3 origin,float3 inverseDirection,bool3 parallel,float maximum,
                out float minimum) {
    minimum=0;
    float3 first=(low-origin)*inverseDirection,last=(high-origin)*inverseDirection;
    [unroll] for(uint axis=0;axis<3u;++axis) {
        if(parallel[axis]) {
            if(origin[axis]<low[axis]||origin[axis]>high[axis])return false;
        } else {
            minimum=max(minimum,min(first[axis],last[axis]));
            maximum=min(maximum,max(first[axis],last[axis]));
            if(maximum<minimum)return false;
        }
    }
    return true;
}
bool pathTriangleHit(uint triangleIndex,float3 origin,float3 direction,float minimum,inout float maximum) {
    float3 a=pathTriangle(triangleIndex*4u).xyz;
    float3 edge1=pathTriangle(triangleIndex*4u+1u).xyz;
    float3 edge2=pathTriangle(triangleIndex*4u+2u).xyz;
    float3 p=cross(direction,edge2);
    float determinant=dot(edge1,p);
    if(abs(determinant)<=1e-8*sqrt(dot(edge1,edge1)*dot(edge2,edge2)))return false;
    float inverse=1.0/determinant;
    float3 distance=origin-a;
    float u=dot(distance,p)*inverse;
    // Adjacent faces must not both reject a ray on their shared edge due to
    // float roundoff. Fixed primary centres exercise these seams consistently.
    const float edgeTolerance=1e-6;
    if(u < -edgeTolerance || u > 1.0+edgeTolerance)return false;
    float3 q=cross(distance,edge1);
    float v=dot(direction,q)*inverse;
    if(v < -edgeTolerance || u+v > 1.0+edgeTolerance)return false;
    float t=dot(edge2,q)*inverse;
    if(!(t>=minimum&&t<maximum))return false;
    maximum=t;return true;
}
bool pathTrace(float3 origin,float3 direction,float minimum,inout float maximum,
               bool anyHit,uint nodeBudget,out uint triangleIndex,out bool exhausted) {
    triangleIndex=0u;exhausted=false;
    if((uint)Counts.x==0u)return false;
    bool3 parallel=abs(direction)<1e-20;
    float3 inverseDirection=1.0/float3(parallel.x?1.0:direction.x,
        parallel.y?1.0:direction.y,parallel.z?1.0:direction.z);
    float4 rootLow=pathNode(0u),rootHigh=pathNode(1u);
    float rootEntry;
    if(!pathBounds(rootLow.xyz,rootHigh.xyz,origin,inverseDirection,parallel,maximum,rootEntry))return false;
    uint node=0u,stackSize=0u,visited=0u;bool found=false;
    // The median-split CPU builder has at most 18 edges to a leaf at its 1<<20
    // triangle cap (four triangles per leaf). Each level defers at most one child.
    // Cache entry distances so a nearer hit can discard deferred nodes without
    // fetching their bounds again. The serialized threaded layout is unchanged.
    uint deferredNodes[20];
    float deferredEntries[20];
    // Keep descent and deferred-pop structured: divergent child-descent continues
    // made Intel skip later subtrees. The flag retains the same front-to-back order.
    [loop] while(true) {
        if(visited>=nodeBudget) { exhausted=true;break; }
        ++visited;
        float4 high=pathNode(node*2u+1u);
        uint tag=(uint)high.w;bool descended=false;
        if(tag!=0u) {
            uint packed=tag-1u,first=packed/4u,count=packed%4u+1u;
            [loop] for(uint i=first;i<min(first+count,(uint)Counts.y);++i) {
                if(pathTriangleHit(i,origin,direction,minimum,maximum)) {
                    found=true;triangleIndex=i;if(anyHit)return true;
                }
            }
        } else {
            uint left=node+1u;
            float4 leftLow=pathNode(left*2u),leftHigh=pathNode(left*2u+1u);
            uint right=(uint)leftLow.w;
            float4 rightLow=pathNode(right*2u),rightHigh=pathNode(right*2u+1u);
            float leftEntry,rightEntry;
            bool hitsLeft=pathBounds(leftLow.xyz,leftHigh.xyz,origin,inverseDirection,parallel,maximum,leftEntry);
            bool hitsRight=pathBounds(rightLow.xyz,rightHigh.xyz,origin,inverseDirection,parallel,maximum,rightEntry);
            if(hitsLeft&&hitsRight) {
                bool leftFirst=leftEntry<=rightEntry;
                deferredNodes[stackSize]=leftFirst?right:left;
                deferredEntries[stackSize]=leftFirst?rightEntry:leftEntry;
                ++stackSize;
                node=leftFirst?left:right;descended=true;
            } else if(hitsLeft||hitsRight) {
                node=hitsLeft?left:right;descended=true;
            }
        }
        if(!descended) {
            bool resume=false;
            [loop] while(stackSize>0u) {
                --stackSize;
                if(deferredEntries[stackSize]<maximum) {
                    node=deferredNodes[stackSize];resume=true;break;
                }
            }
            if(!resume)break;
        }
    }
    return found;
}
float3 pathSky(float3 direction) {
    return lerp(SkyLow.rgb,SkyHigh.rgb,saturate(direction.z));
}
float3 pathSecondarySky(float3 normal) {
    // Cheap cosine-hemisphere average of the vertical sky gradient. At the final
    // surface it replaces another visibility ray; enclosed surfaces can be too bright.
    float z=clamp(normal.z,-1.0,1.0);
    float upper=saturate(0.212206591+z*(0.333333333+0.121126742*z));
    return lerp(SkyLow.rgb,SkyHigh.rgb,upper);
}
void pathMaterial(uint triangleIndex,float3 ray,out float3 normal,out float3 albedo) {
    float3 edge1=pathTriangle(triangleIndex*4u+1u).xyz;
    float3 edge2=pathTriangle(triangleIndex*4u+2u).xyz;
    normal=normalize(cross(edge1,edge2));
    if(dot(normal,ray)>0)normal=-normal;
    albedo=saturate(pathTriangle(triangleIndex*4u+3u).xyz);
}
float4 pathMaterialRecord(uint index) {
    return PathMaterials.Load(int3(index%PathTextureWidth,index/PathTextureWidth,0));
}
float3 pathMaterialTexel(uint offset,int2 coordinate,int2 size) {
    // Texture coordinates repeat, including across negative coordinates/seams.
    coordinate=(coordinate%size+size)%size;
    uint index=offset+(uint)coordinate.y*(uint)size.x+(uint)coordinate.x;
    return PathTexels.Load(int3(index%PathTextureWidth,index/PathTextureWidth,0)).rgb;
}
float3 pathMaterialBilinear(uint offset,int2 size,float2 uv) {
    float2 pixel=frac(uv)*float2(size)-0.5;
    int2 first=(int2)floor(pixel);float2 weight=frac(pixel);
    float3 low=lerp(pathMaterialTexel(offset,first,size),
        pathMaterialTexel(offset,first+int2(1,0),size),weight.x);
    float3 high=lerp(pathMaterialTexel(offset,first+int2(0,1),size),
        pathMaterialTexel(offset,first+int2(1,1),size),weight.x);
    return lerp(low,high,weight.y);
}
float3 pathPrimaryAlbedo(uint triangleIndex,float3 hitPoint,float3 captureRay,float3 fallback) {
    float4 material=pathMaterialRecord(triangleIndex*3u+2u);
    if(material.w<1.0)return fallback;
    float4 uvAB=pathMaterialRecord(triangleIndex*3u);
    float4 uvCSize=pathMaterialRecord(triangleIndex*3u+1u);
    float3 a=pathTriangle(triangleIndex*4u).xyz;
    float3 e1=pathTriangle(triangleIndex*4u+1u).xyz;
    float3 e2=pathTriangle(triangleIndex*4u+2u).xyz;
    float3 n=cross(e1,e2),v=hitPoint-a;
    float inverse=1.0/dot(n,n);
    float b=dot(cross(v,e2),n)*inverse;
    float c=dot(cross(e1,v),n)*inverse;
    float2 uvB=uvAB.zw-uvAB.xy,uvC=uvCSize.xy-uvAB.xy;
    float2 uv=uvAB.xy+uvB*b+uvC*c;
    // Differentiate the camera ray's intersection with this hit plane. Screen
    // derivatives of neighbouring hit UVs would cross unrelated triangles/misses;
    // this footprint remains valid at silhouettes and material seams.
    float denominator=dot(n,captureRay);
    float planeDistance=dot(n,hitPoint-OriginWater.xyz);
    float3 rayDx=RayDx.xyz/Output.x,rayDy=RayDy.xyz/Output.y;
    float3 hitDx=(rayDx*denominator-captureRay*dot(n,rayDx))*(planeDistance/(denominator*denominator));
    float3 hitDy=(rayDy*denominator-captureRay*dot(n,rayDy))*(planeDistance/(denominator*denominator));
    float2 uvDx=(uvB*dot(cross(hitDx,e2),n)+uvC*dot(cross(e1,hitDx),n))*inverse;
    float2 uvDy=(uvB*dot(cross(hitDy,e2),n)+uvC*dot(cross(e1,hitDy),n))*inverse;
    float footprint=max(length(uvDx*uvCSize.zw),length(uvDy*uvCSize.zw));
    float lod=clamp(log2(max(1.0,footprint)),0.0,6.0);
    uint offset=(uint)material.w-1u;
    int2 size=(int2)uvCSize.zw;
    [loop] for(uint level=0;level<(uint)lod && any(size>1);++level) {
        offset+=(uint)(size.x*size.y);size=max(size/2,int2(1,1));
    }
    float3 colour=pathMaterialBilinear(offset,size,uv);
    if(any(size>1) && frac(lod)>0.0)
        colour=lerp(colour,pathMaterialBilinear(offset+(uint)(size.x*size.y),max(size/2,int2(1,1)),uv),frac(lod));
    return saturate(material.rgb*colour);
}
float3 pathCosine(float3 normal,float2 xi) {
    float radius=sqrt(xi.x),angle=2.0*PathPi*xi.y;
    float3 tangent=normalize(cross(abs(normal.z)<0.999?float3(0,0,1):float3(0,1,0),normal));
    float3 bitangent=cross(normal,tangent);
    return normalize(tangent*(radius*cos(angle))+bitangent*(radius*sin(angle))
                     +normal*sqrt(max(0.0,1.0-xi.x)));
}
float4 main(float4 position : SV_Position) : SV_Target0
{
    uint2 pixel=(uint2)position.xy;
    uint seed=pathHash(pixel.x+pixel.y*(uint)Output.x);
    // Keep first-hit coverage fixed while a dynamic scene restarts lighting
    // accumulation. Jittering this low-resolution primary ray made still objects
    // crawl between silhouettes and material faces on every history reset.
    float2 uv=(float2(pixel)+0.5)/Output.xy;
    float3 captureRay=RayTopLeft.xyz+RayDx.xyz*uv.x+RayDy.xyz*uv.y;
    float3 ray=normalize(captureRay);
    float epsilon=max(Output.z,0.0001);
    float crossing=ray.z>1e-6?(OriginWater.w-OriginWater.z)/ray.z:-1.0;
    float validity=crossing>=0.0?1.0:0.0;
    float3 radiance=float3(0,0,0);
    if(validity>0) {
        // Begin at the water interface. Geometry between the virtual camera and water is
        // below the reflection plane and must not occlude the reflected scene.
        float3 origin=OriginWater.xyz+ray*(crossing+epsilon);
        float distance=Counts.w;uint triangleIndex;bool exhausted;
        // Primary geometry keeps the full traversal budget. Cost limits affect lighting,
        // never the first-hit silhouette of the reflected object.
        if(!pathTrace(origin,ray,epsilon,distance,false,(uint)Counts.x,triangleIndex,exhausted)) {
            // Capture geometry only. The water material evaluates its environment
            // from the actual wave normal, consistently inside/outside this crop.
            // A miss contributes neither radiance nor geometry coverage.
            validity=0.0;
        } else {
            float3 normal,albedo;
            pathMaterial(triangleIndex,ray,normal,albedo);
            float3 hitPoint=origin+ray*distance;
            albedo=pathPrimaryAlbedo(triangleIndex,hitPoint,captureRay,albedo);
            float3 sun=SunDirection.xyz*rsqrt(max(dot(SunDirection.xyz,SunDirection.xyz),1e-20));
            float cosine=max(dot(normal,sun),0.0);
            if(cosine>0&&any(SunRadiance.rgb>0)) {
                float shadowDistance=Transport.y;uint blocker;bool shadowExhausted;
                bool blocked=pathTrace(hitPoint+normal*epsilon,sun,epsilon,shadowDistance,
                    true,(uint)Transport.w,blocker,shadowExhausted);
                // Running out of traversal work means unknown visibility, not sunlight.
                if(!blocked&&!shadowExhausted)
                    radiance+=albedo*SunRadiance.rgb*(cosine/PathPi);
            }
            if(any(albedo>0)) {
                // One sampled diffuse continuation carries real colour between surfaces.
                // Stop at its hit, using approximate local lighting instead of launching
                // a second sun ray and a further diffuse/environment visibility ray.
                float3 continuation=pathCosine(normal,pathSample(seed,1u));
                float secondaryDistance=Transport.x;uint secondary;bool secondaryExhausted;
                bool secondaryHit=pathTrace(hitPoint+normal*epsilon,continuation,epsilon,
                    secondaryDistance,false,(uint)Transport.z,secondary,secondaryExhausted);
                if(!secondaryExhausted) {
                    float3 incident;
                    if(secondaryHit) {
                        float3 secondaryNormal,secondaryAlbedo;
                        pathMaterial(secondary,continuation,secondaryNormal,secondaryAlbedo);
                        incident=secondaryAlbedo*(pathSecondarySky(secondaryNormal)
                            +SunRadiance.rgb*(max(dot(secondaryNormal,sun),0.0)/PathPi));
                    } else incident=pathSky(continuation);
                    // Lambertian BRDF / cosine-weighted PDF cancels to the first albedo.
                    radiance+=albedo*incident;
                }
            }
        }
    }
    float4 current=float4(max(radiance,0),validity);
    if(Counts.z>0) {
        float4 previous=PathPrevious.Load(int3(pixel,0));
        current=lerp(previous,current,1.0/(Counts.z+1.0));
    }
    // Keep the RGBA16F history linear through the water shader's bilinear and
    // roughness filtering. Encoding here used to darken small bright reflections
    // at silhouettes before the interface's Fresnel factor was even applied.
    return current;
}
)HLSL";
}

} // namespace WaterPathTracing
