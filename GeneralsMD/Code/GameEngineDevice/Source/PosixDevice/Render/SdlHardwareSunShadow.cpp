#include "SdlHardwareSunShadow.h"
#include "SdlGpuFrame.h"
#include "sdl3shadercompile.h"
#if defined(__linux__)
#include "../../../../Libraries/Source/SDL_gpu_vulkan_rayquery.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <new>
#include <numeric>

namespace {
// Only orthogonal affine light volumes can be remapped without resampling rays.
// Double precision checks bound accumulated float inversion error in texel units.
struct SunBasis {
    double axis[3][3] = {}, dual[3][3] = {}, origin[3] = {}, squared[3] = {};
    bool Load(const float *inverse) {
        if (!inverse || !std::isfinite(inverse[15]) || std::fabs(inverse[15]-1.0f)>1e-5f) return false;
        for (unsigned c=0;c<3;++c) {
            if (std::fabs(inverse[c*4+3])>1e-7f) return false;
            for(unsigned k=0;k<3;++k) {
                axis[c][k]=double(inverse[c*4+k])/inverse[15];
                if(!std::isfinite(axis[c][k])) return false;
                squared[c]+=axis[c][k]*axis[c][k];
            }
            if(squared[c]<1e-16) return false;
            origin[c]=double(inverse[12+c])/inverse[15]; if(!std::isfinite(origin[c])) return false;
        }
        for(unsigned a=0;a<3;++a) for(unsigned b=a+1;b<3;++b) {
            double dot=0; for(unsigned k=0;k<3;++k) dot+=axis[a][k]*axis[b][k];
            if(std::fabs(dot)>1e-6*std::sqrt(squared[a]*squared[b])) return false;
        }
        // Invert the actual recorded float affine basis. Dot/length inversion
        // assumes exact orthogonality and turns harmless matrix rounding plus
        // motion along Z into apparent fractional XY motion.
        for(unsigned a=0;a<3;++a) {
            const unsigned b=(a+1)%3,c=(a+2)%3;
            for(unsigned k=0;k<3;++k)
                dual[a][k]=axis[b][(k+1)%3]*axis[c][(k+2)%3]-axis[b][(k+2)%3]*axis[c][(k+1)%3];
        }
        double determinant=0; for(unsigned k=0;k<3;++k) determinant+=axis[0][k]*dual[0][k];
        if(!std::isfinite(determinant) || std::fabs(determinant)<1e-20) return false;
        for(auto &row:dual) for(double &value:row) value/=determinant;
        return true;
    }
    double Direction(unsigned a,const double direction[3]) const {
        double value=0; for(unsigned k=0;k<3;++k) value+=direction[k]*dual[a][k]; return value;
    }
    double Coordinate(unsigned a,const double world[3]) const {
        double delta[3]; for(unsigned k=0;k<3;++k) delta[k]=world[k]-origin[k];
        return Direction(a,delta);
    }
    void Project(const std::array<float,6> &bounds,double lo[3],double hi[3]) const {
        // Each light coordinate is affine. Its extremum picks the endpoint
        // of each world axis independently, equivalent to testing all corners.
        for(unsigned a=0;a<3;++a) {
            lo[a]=hi[a]=0;
            for(unsigned k=0;k<3;++k) {
                const double start=(bounds[k]-origin[k])*dual[a][k];
                const double end=(bounds[k+3]-origin[k])*dual[a][k];
                lo[a]+=std::min(start,end);
                hi[a]+=std::max(start,end);
            }
        }
    }
    bool ContainsDepth(const std::array<float,6> &bounds) const {
        double lo[3],hi[3]; Project(bounds,lo,hi);
        // A strict interior margin avoids accepting a near/far clipping transition.
        return lo[2]>1e-6 && hi[2]<1.0-1e-6;
    }
};
bool sun_relevant_depth_contained(const SunBasis &current,const SunBasis &cache,
    const std::vector<std::array<float,6>> &bounds,unsigned width,unsigned height)
{
    const double padX=2.0/width,padY=2.0/height;
    for(const auto &box:bounds) {
        double lo[3],hi[3]; current.Project(box,lo,hi);
        if(hi[0]<-1-padX || lo[0]>1+padX || hi[1]<-1-padY || lo[1]>1+padY) continue;
        if(lo[2]<=1e-6 || hi[2]>=1.0-1e-6 || !cache.ContainsDepth(box)) return false;
    }
    return true;
}
using SunRectangle = std::array<uint32_t,4>;
std::vector<SunRectangle> sun_coverage(const SunBasis &basis,
    const std::vector<std::array<float,6>> &bounds,unsigned width,unsigned height,unsigned tileHint=0)
{
    // Small targets in the GPU proof use smaller tiles; production uses 128.
    const unsigned tile=tileHint ? tileHint : (width>=512 && height>=512 ? 128 : 8);
    const unsigned columns=(width+tile-1)/tile, rows=(height+tile-1)/tile;
    std::vector<unsigned char> occupied(size_t(columns)*rows,0);
    for(const auto &box:bounds) {
        double lo[3],hi[3]; basis.Project(box,lo,hi);
        // One-texel conservative expansion includes round-off and raster boundaries.
        const double x0=std::max(0.0,std::floor((lo[0]+1)*width*.5-.5)-1);
        const double y0=std::max(0.0,std::floor((1-hi[1])*height*.5-.5)-1);
        const double x1=std::min(double(width),std::ceil((hi[0]+1)*width*.5-.5)+2);
        const double y1=std::min(double(height),std::ceil((1-lo[1])*height*.5-.5)+2);
        if(x0>=x1 || y0>=y1) continue;
        const unsigned left=unsigned(x0)/tile, top=unsigned(y0)/tile;
        const unsigned right=(unsigned(x1)+tile-1)/tile, bottom=(unsigned(y1)+tile-1)/tile;
        for(unsigned y=top;y<bottom;++y) for(unsigned x=left;x<right;++x) occupied[y*columns+x]=1;
    }
    std::vector<SunRectangle> result;
    std::map<std::pair<unsigned,unsigned>,size_t> previous;
    for(unsigned y=0;y<rows;++y) {
        std::map<std::pair<unsigned,unsigned>,size_t> current;
        for(unsigned x=0;x<columns;) {
            if(!occupied[y*columns+x]) { ++x; continue; }
            const unsigned left=x;
            while(x<columns && occupied[y*columns+x]) ++x;
            const unsigned px=left*tile, pw=std::min(width,x*tile)-px;
            const auto key=std::make_pair(px,pw); const auto found=previous.find(key);
            size_t index;
            if(found!=previous.end()) {
                index=found->second; result[index][3]+=std::min(tile,height-y*tile);
            } else {
                index=result.size(); result.push_back({px,y*tile,pw,std::min(tile,height-y*tile)});
            }
            current.emplace(key,index);
        }
        previous.swap(current);
    }
    return result;
}
std::vector<SunRectangle> sun_merge_rectangles(const std::vector<SunRectangle> &cache,
    const double offset[2],const double stride[2],unsigned width,unsigned height)
{
    std::vector<SunRectangle> result;
    for(const auto &rect:cache) {
        const int x0=std::max(0,int(std::ceil((double(rect[0])-.5-offset[0])/stride[0])));
        const int y0=std::max(0,int(std::ceil((double(rect[1])-.5-offset[1])/stride[1])));
        const int x1=std::min(int(width),int(std::ceil((double(rect[0]+rect[2])-.5-offset[0])/stride[0])));
        const int y1=std::min(int(height),int(std::ceil((double(rect[1]+rect[3])-.5-offset[1])/stride[1])));
        if(x0<x1 && y0<y1) result.push_back({unsigned(x0),unsigned(y0),unsigned(x1-x0),unsigned(y1-y0)});
    }
    return result;
}
uint64_t sun_texels(const std::vector<SunRectangle> &rectangles)
{
    uint64_t count=0; for(const auto &r:rectangles) count+=uint64_t(r[2])*r[3]; return count;
}

using SunTriangle = std::array<float,9>;
using SunBounds = std::array<float,6>;

bool changed_triangle_bounds(const std::vector<SunTriangle> &previous, const std::vector<SunTriangle> &current,
    std::vector<SunBounds> &bounds)
{
    const auto same = [](const SunTriangle &a, const SunTriangle &b) {
        return std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
    };
    size_t first = 0, oldEnd = previous.size(), newEnd = current.size();
    while (first < oldEnd && first < newEnd && same(previous[first], current[first])) ++first;
    while (oldEnd > first && newEnd > first && same(previous[oldEnd - 1], current[newEnd - 1])) {
        --oldEnd;
        --newEnd;
    }
    // Both removed and added geometry can change the nearest hit. Bound the
    // edited range in small groups to preserve gaps between buildings.
    const auto addBounds = [&](const std::vector<SunTriangle> &triangles, size_t end) {
        constexpr size_t TrianglesPerBound = 16;
        for (size_t begin = first; begin < end; begin += TrianglesPerBound) {
            SunBounds box = {INFINITY, INFINITY, INFINITY, -INFINITY, -INFINITY, -INFINITY};
            const size_t limit = std::min(begin + TrianglesPerBound, end);
            for (size_t i = begin; i < limit; ++i) {
                for (size_t coordinate = 0; coordinate < triangles[i].size(); ++coordinate) {
                    const float value = triangles[i][coordinate];
                    if (!std::isfinite(value)) return false;
                    const size_t axis = coordinate % 3;
                    box[axis] = std::min(box[axis], value);
                    box[axis + 3] = std::max(box[axis + 3], value);
                }
            }
            bounds.push_back(box);
        }
        return true;
    };
    return addBounds(previous, oldEnd) && addBounds(current, newEnd);
}

enum SunReject { SUN_NO_CACHE, SUN_DEPTH_CLIP, SUN_BASIS, SUN_PITCH, SUN_LATTICE, SUN_COVERAGE };
const char *sun_reject_name(unsigned reason)
{
    static const char *names[]={"no-cache","depth-clip","basis","pitch","lattice","coverage"};
    return reason<6 ? names[reason] : "unknown";
}
// Current rays must be a subset of the same-or-finer cached lattice. Positive
// integer strides allow exact coarser zoom levels, without resampling visibility.
bool sun_reprojection(const SunBasis &cache,unsigned cacheWidth,unsigned cacheHeight,
    const SunBasis &current,unsigned width,unsigned height,double offset[2],double stride[2],
    float &depthScale,float &depthBias,unsigned &reason,double metrics[6])
{
    double mapped[3][2] = {};
    const double pixel[3][2]={{0,0},{double(width),0},{0,double(height)}};
    for(unsigned p=0;p<3;++p) {
        double world[3];
        for(unsigned k=0;k<3;++k) world[k]=current.origin[k]
            + current.axis[0][k]*((pixel[p][0]+.5)*2/width-1)
            + current.axis[1][k]*(1-(pixel[p][1]+.5)*2/height);
        mapped[p][0]=(cache.Coordinate(0,world)+1)*cacheWidth*.5-.5;
        mapped[p][1]=(1-cache.Coordinate(1,world))*cacheHeight*.5-.5;
    }
    metrics[0]=mapped[0][0]; metrics[1]=mapped[0][1];
    metrics[2]=(mapped[1][0]-mapped[0][0])/width;
    metrics[3]=(mapped[2][1]-mapped[0][1])/height;
    metrics[4]=(mapped[2][0]-mapped[0][0])/height;
    metrics[5]=(mapped[1][1]-mapped[0][1])/width;
    // Recorded game matrices invert with up to .0016 texel origin noise during
    // snapped pans. Allow five thousandths for float reconstruction.
    const double tolerance=.005;
    reason=SUN_BASIS;
    if(std::fabs(metrics[4])*height>tolerance || std::fabs(metrics[5])*width>tolerance) return false;
    reason=SUN_PITCH;
    for(unsigned k=0;k<2;++k) {
        if(metrics[2+k]<.5 || metrics[2+k]>8192) return false;
        const double integral=std::round(metrics[2+k]);
        if(integral<1 || std::fabs(metrics[2+k]-integral)*(k ? height : width)>tolerance) return false;
        stride[k]=integral;
    }
    reason=SUN_LATTICE;
    if(std::fabs(mapped[0][0])>65536 || std::fabs(mapped[0][1])>65536) return false;
    for(unsigned k=0;k<2;++k) offset[k]=std::round(mapped[0][k]);
    for(unsigned p=0;p<3;++p) for(unsigned k=0;k<2;++k)
        if(std::fabs(mapped[p][k]-(pixel[p][k]*stride[k]+offset[k]))>tolerance) return false;
    reason=SUN_BASIS;
    // True affine coordinates cancel recorded matrix roundoff for identical
    // bases; only actual lateral ray drift is rejected here.
    for(unsigned k=0;k<2;++k)
        if(std::fabs(current.Direction(k,cache.axis[2]))*(k ? height : width)*.5>tolerance) return false;
    for(unsigned a=0;a<2;++a)
        if(std::fabs(current.Direction(2,cache.axis[a]))>1e-7) return false;
    depthScale=float(current.Direction(2,cache.axis[2]));
    depthBias=float(current.Coordinate(2,cache.origin));
    if(depthScale<=0) return false;
    return std::isfinite(depthScale) && std::isfinite(depthBias);
}

const char *RayShader = R"glsl(#version 460
#extension GL_EXT_ray_query : require
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform accelerationStructureEXT scene;
layout(set=0,binding=1) uniform sampler2D sceneDepth;
layout(set=0,binding=2,r32f) uniform writeonly image2D visibility;
layout(push_constant) uniform Params {
    mat4 inverseViewProjection;
    vec4 lightDirection;
    float originBias;
    float maxDistance;
    uint width;
    uint height;
    vec4 padding[2];
} p;
void main() {
    uvec2 pixel=gl_GlobalInvocationID.xy;
    if(p.padding[0].x>0.5) {
        if(pixel.x>=uint(p.padding[0].w) || pixel.y>=uint(p.padding[1].x)) return;
        pixel+=uvec2(p.padding[0].yz);
    }
    if(pixel.x>=p.width || pixel.y>=p.height) return;
    vec2 uv=(vec2(pixel)+vec2(0.5))/vec2(p.width,p.height);
    // Sun mode traces the light's own orthographic near-to-far segment. Closest
    // hit distance maps linearly to the same D3D [0,1] depth used by raster casters.
    if(p.padding[0].x>0.5) {
        vec4 nearPoint=p.inverseViewProjection*vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,0.0,1.0);
        vec4 farPoint=p.inverseViewProjection*vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,1.0,1.0);
        vec3 origin=nearPoint.xyz/nearPoint.w;
        vec3 segment=farPoint.xyz/farPoint.w-origin;
        float distance=length(segment);
        float nearest=1.0;
        if(distance>0.0) {
            rayQueryEXT query;
            rayQueryInitializeEXT(query,scene,gl_RayFlagsOpaqueEXT,0xff,
                origin,0.0,segment/distance,distance);
            while(rayQueryProceedEXT(query)) {}
            if(rayQueryGetIntersectionTypeEXT(query,true)!=gl_RayQueryCommittedIntersectionNoneEXT)
                nearest=clamp(rayQueryGetIntersectionTEXT(query,true)/distance,0.0,1.0);
        }
        imageStore(visibility,ivec2(pixel),vec4(nearest));
        return;
    }
    float depth=texelFetch(sceneDepth,ivec2(pixel),0).r;
    float visible=1.0;
    if(depth<1.0) {
        vec4 world=p.inverseViewProjection*vec4(uv.x*2.0-1.0,1.0-uv.y*2.0,depth,1.0);
        vec3 origin=world.xyz/world.w;
        vec3 direction=normalize(p.lightDirection.xyz);
        rayQueryEXT query;
        rayQueryInitializeEXT(query,scene,gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsOpaqueEXT,
            0xff,origin+direction*p.originBias,0.0,direction,p.maxDistance);
        while(rayQueryProceedEXT(query)) {}
        visible=rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
    }
    imageStore(visibility,ivec2(pixel),vec4(visible));
}
)glsl";
}

static const uint32_t *sun_indices(const uint32_t *indices, uint32_t vertexCount, uint32_t indexCount,
    std::vector<uint32_t> &sequential)
{
    if (indices || vertexCount != indexCount || indexCount < 3 || indexCount % 3) return indices;
    sequential.resize(indexCount);
    std::iota(sequential.begin(), sequential.end(), 0u);
    return sequential.data();
}

SdlHardwareSunShadow *SdlHardwareSunShadow::Create(SdlGpuFrame &frame, const float *positions,
    uint32_t vertexCount, const uint32_t *indices, uint32_t indexCount, std::string &error,
    const std::vector<uint32_t> &meshTriangleEnds, const std::vector<uint32_t> &buildingTriangleEnds)
{
    char diagnostics[1024] = {};
    if (!ZHR_SDLRayQuerySupported(frame.Device(), diagnostics, sizeof(diagnostics))) {
        error = diagnostics[0] ? diagnostics : SDL_GetError(); return nullptr;
    }
    std::vector<unsigned char> spirv;
    if (!SDL3_Compile_Ray_Query_GLSL(RayShader, spirv, error)) return nullptr;
    auto *shadow = new SdlHardwareSunShadow(frame);
    shadow->DeviceDiagnostics = diagnostics;
    try {
        if (!indices && vertexCount == indexCount && !buildingTriangleEnds.empty()) {
            shadow->Scene = ZHR_SDLRayCreateSunBuildingScene(frame.Device(), positions, vertexCount,
                buildingTriangleEnds.data(), uint32_t(buildingTriangleEnds.size()), spirv.data(), spirv.size());
            shadow->BuildingScene = shadow->Scene != nullptr;
        }
        if (!shadow->Scene) {
            std::vector<uint32_t> sequential;
            shadow->Scene = ZHR_SDLRayCreateScene(frame.Device(), positions, vertexCount,
                sun_indices(indices, vertexCount, indexCount, sequential), indexCount, spirv.data(), spirv.size());
        }
        if (!shadow->Scene) { error=SDL_GetError(); delete shadow; return nullptr; }
        shadow->BuildGeometryBounds(positions, indices, indexCount, meshTriangleEnds);
    } catch (const std::bad_alloc &) {
        error = "unable to store hardware sun geometry bounds";
        delete shadow;
        return nullptr;
    }
    error.clear(); return shadow;
}

void SdlHardwareSunShadow::BuildGeometryBounds(const float *positions, const uint32_t *indices, uint32_t indexCount,
    const std::vector<uint32_t> &meshTriangleEnds)
{
    // Keep clusters inside each mesh so their bounds leave gaps between casters.
    constexpr uint32_t trianglesPerCluster = 128;
    const uint32_t triangleCount = indexCount / 3;
    SceneBounds = {INFINITY,INFINITY,INFINITY,-INFINITY,-INFINITY,-INFINITY};
    GeometryBounds.clear();
    GeometryTriangles.resize(triangleCount);
    auto meshEnd = meshTriangleEnds.begin();
    for (uint32_t first = 0; first < triangleCount; ) {
        while (meshEnd != meshTriangleEnds.end() && *meshEnd <= first) ++meshEnd;
        uint32_t end = std::min(triangleCount, first + trianglesPerCluster);
        if (meshEnd != meshTriangleEnds.end()) end = std::min(end, *meshEnd);
        Bounds bounds = {INFINITY,INFINITY,INFINITY,-INFINITY,-INFINITY,-INFINITY};
        for (uint32_t i = first * 3; i < end * 3; ++i) {
            const float *vertex = positions + size_t(indices ? indices[i] : i) * 3;
            std::copy_n(vertex, 3, GeometryTriangles[i / 3].begin() + (i % 3) * 3);
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float value = vertex[axis];
                bounds[axis] = std::min(bounds[axis], value);
                bounds[axis + 3] = std::max(bounds[axis + 3], value);
            }
        }
        for (unsigned axis = 0; axis < 3; ++axis) {
            SceneBounds[axis] = std::min(SceneBounds[axis], bounds[axis]);
            SceneBounds[axis + 3] = std::max(SceneBounds[axis + 3], bounds[axis + 3]);
        }
        GeometryBounds.push_back(bounds);
        first = end;
    }
}

bool SdlHardwareSunShadow::UpdateGeometry(const float *positions, uint32_t vertexCount,
    const uint32_t *indices, uint32_t indexCount, std::string &error,
    const std::vector<uint32_t> &meshTriangleEnds, const std::vector<uint32_t> &buildingTriangleEnds)
{
    if (!Scene || SubmissionFailed) {
        error = "hardware sun scene needs recreation"; return false;
    }
    if (!SubmitQueuedSunMerge(error) || !WaitForQuery(error)) return false;
    bool changed = false;
    try {
        bool updated;
        if (BuildingScene) {
            if (indices || indexCount != vertexCount) {
                error = "sun building scenes require consecutive world triangles"; return false;
            }
            updated = ZHR_SDLRayUpdateSunBuildingScene(Scene, positions, vertexCount, buildingTriangleEnds.data(),
                uint32_t(buildingTriangleEnds.size()), &changed);
        } else {
            std::vector<uint32_t> sequential;
            updated = ZHR_SDLRayUpdateScene(Scene, positions, vertexCount,
                sun_indices(indices, vertexCount, indexCount, sequential), indexCount, &changed);
        }
        if (!updated) { error = SDL_GetError(); return false; }
    } catch (const std::bad_alloc &) {
        error = "unable to allocate hardware sun indices";
        return false;
    }
    if (changed) {
        SunBasis cache;
        const bool canRetain = SunCacheValid && cache.Load(SunDepthInverse);
        std::vector<SunTriangle> previous;
        if (canRetain) previous.swap(GeometryTriangles);
        SunCacheValid = DepthOutput = SceneSubmitted = false;
        SunMergeRectangles.clear();
        try {
            BuildGeometryBounds(positions, indices, indexCount, meshTriangleEnds);
            std::vector<SunBounds> changedBounds;
            if (canRetain && changed_triangle_bounds(previous, GeometryTriangles, changedBounds)) {
                const unsigned tile = SunTiles.TileSize();
                // Preserve initialized depths only outside the old and new
                // footprints, in the lattice that originally traced them.
                const auto dirty = sun_coverage(cache, changedBounds, Width, Height, tile);
                const auto occupied = sun_coverage(cache, GeometryBounds, Width, Height, tile);
                SunTiles.UpdateOccupancy(occupied, dirty);
                SunCacheRectangles = SunTiles.Ready();
                SunCacheZContained = cache.ContainsDepth(SceneBounds);
                SunCacheValid = true;
            }
        } catch (const std::bad_alloc &) {
            error = "unable to update hardware sun geometry bounds";
            return false;
        }
    }
    error.clear(); return true;
}

SdlHardwareSunShadow::~SdlHardwareSunShadow()
{
    Frame.Flush();
    // The private scene destructor waits for GPU completion before releasing native resources.
    if (Scene) ZHR_SDLRayDestroyScene(Scene);
    if (InFlight) SDL_ReleaseGPUFence(Frame.Device(), InFlight);
    if (DepthMergePipeline) SDL_ReleaseGPUGraphicsPipeline(Frame.Device(), DepthMergePipeline);
    if (DepthMergePixel) SDL_ReleaseGPUShader(Frame.Device(), DepthMergePixel);
    if (CompositePipeline) SDL_ReleaseGPUGraphicsPipeline(Frame.Device(), CompositePipeline);
    if (CompositeVertex) SDL_ReleaseGPUShader(Frame.Device(), CompositeVertex);
    if (CompositePixel) SDL_ReleaseGPUShader(Frame.Device(), CompositePixel);
    if (CompositeSampler) SDL_ReleaseGPUSampler(Frame.Device(), CompositeSampler);
    if (Depth) SDL_ReleaseGPUTexture(Frame.Device(), Depth);
    if (Visibility) SDL_ReleaseGPUTexture(Frame.Device(), Visibility);
}

bool SdlHardwareSunShadow::Render(const SdlTarget &source, const float *inverseViewProjection,
    const float *lightDirection, float originBias, float maxDistance, std::string &error)
{
    return RenderInternal(source, nullptr, inverseViewProjection, lightDirection, originBias, maxDistance, error);
}

bool SdlHardwareSunShadow::RenderResolved(const SdlTarget &source, SDL_GPUTexture *resolvedDepth,
    const float *inverseViewProjection, const float *lightDirection,
    float originBias, float maxDistance, std::string &error)
{
    if (!resolvedDepth) { DepthOutput = SunCacheValid = false; error = "missing resolved scene depth"; return false; }
    return RenderInternal(source, resolvedDepth, inverseViewProjection, lightDirection, originBias, maxDistance, error);
}

bool SdlHardwareSunShadow::WaitForQuery(std::string &error)
{
    // Native descriptors, AS inputs and build scratch belong to one submission at a time.
    if (!InFlight) return true;
    if (!SDL_WaitForGPUFences(Frame.Device(), true, &InFlight, 1)) {
        error = SDL_GetError(); return false;
    }
    SDL_ReleaseGPUFence(Frame.Device(), InFlight);
    InFlight = nullptr;
    return true;
}

bool SdlHardwareSunShadow::RenderInternal(const SdlTarget &source, SDL_GPUTexture *resolvedDepth,
    const float *inverseViewProjection, const float *lightDirection,
    float originBias, float maxDistance, std::string &error, bool sunDepth,
    const std::vector<Rectangle> *rectangles)
{
    if (!SubmitQueuedSunMerge(error)) return false;
    DepthOutput = SunCacheValid = false;
    if (SubmissionFailed) { error = "hardware scene submission previously failed; rebuild required"; return false; }
    if (!source.Width || !source.Height || !inverseViewProjection || !lightDirection ||
        originBias<0 || !(maxDistance>0)) { error="invalid ray-query view"; return false; }
    if (!WaitForQuery(error)) return false;
    if (Width!=source.Width || Height!=source.Height || DepthIsSunDummy!=sunDepth) {
        // The first mapped query uses fresh textures. Leave the D32 clear queued
        // with the merge and raster casters while the query writes its own R32 output.
        const bool firstSunDepth = sunDepth && !Depth && !Visibility;
        if (!firstSunDepth && (!Frame.Flush() || !SDL_WaitForGPUIdle(Frame.Device()))) {
            error = SDL_GetError();
            return false;
        }
        SDL_GPUTextureCreateInfo info = {};
        info.type=SDL_GPU_TEXTURETYPE_2D; info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.width=sunDepth ? 1 : source.Width; info.height=sunDepth ? 1 : source.Height;
        info.layer_count_or_depth=info.num_levels=1;
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        SDL_GPUTexture *depth=SDL_CreateGPUTexture(Frame.Device(),&info);
        info.usage=SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width=source.Width; info.height=source.Height;
        SDL_GPUTexture *mask=SDL_CreateGPUTexture(Frame.Device(),&info);
        if (!depth || !mask) {
            error=SDL_GetError();
            if(depth) SDL_ReleaseGPUTexture(Frame.Device(),depth);
            if(mask) SDL_ReleaseGPUTexture(Frame.Device(),mask);
            return false;
        }
        if(Depth) SDL_ReleaseGPUTexture(Frame.Device(),Depth);
        if(Visibility) SDL_ReleaseGPUTexture(Frame.Device(),Visibility);
        Depth=depth; Visibility=mask; Width=source.Width; Height=source.Height; DepthIsSunDummy=sunDepth;
    }
    if (!sunDepth && !resolvedDepth && !Frame.Capture_Scene_Depth(source,Depth)) { error=SDL_GetError(); return false; }
    ZHR_SDLRayParams params = {};
    static_assert(sizeof(params)==128,"ray-query push constant ABI");
    std::memcpy(params.inverseViewProjection,inverseViewProjection,16*sizeof(float));
    std::memcpy(params.lightDirection,lightDirection,3*sizeof(float));
    params.originBias=originBias; params.maxDistance=maxDistance; params.width=Width; params.height=Height;
    params.padding[0] = sunDepth ? 1.0f : 0.0f;
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(Frame.Device());
    if(!commands) { error=SDL_GetError(); return false; }
    const bool recorded = sunDepth && rectangles && !rectangles->empty()
        ? ZHR_SDLRayRecordSunDepth(Scene,commands,Depth,Visibility,Width,Height,&params,
            rectangles->front().data(),uint32_t(rectangles->size()))
        : !sunDepth && ZHR_SDLRayRecord(Scene,commands,resolvedDepth ? resolvedDepth : Depth,
            Visibility,Width,Height,&params);
    if(!recorded) {
        error=SDL_GetError(); SDL_CancelGPUCommandBuffer(commands); return false;
    }
    InFlight = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    if (!InFlight) { SubmissionFailed = true; error=SDL_GetError(); return false; }
    SceneSubmitted = true;
    DepthOutput = sunDepth;
    error.clear(); return true;
}

bool SdlHardwareSunShadow::ReadyFor(const SdlTarget &target) const
{
    return SceneSubmitted && !SubmissionFailed && Depth && Visibility &&
        Width == target.Width && Height == target.Height;
}

bool SdlHardwareSunShadow::PrepareComposite(const SdlTarget &target, std::string &error)
{
    const unsigned format = Frame.Target_Colour_Format(target);
    if (CompositePipeline && CompositeFormat == format) return true;
    if (!Frame.Flush() || !SDL_WaitForGPUIdle(Frame.Device())) { error = SDL_GetError(); return false; }
    if (CompositePipeline) SDL_ReleaseGPUGraphicsPipeline(Frame.Device(), CompositePipeline);
    CompositePipeline = nullptr;
    const char *vertex = R"hlsl(
struct Output { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    Output output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Position = float4(output.UV * float2(2,-2) + float2(-1,1),0,1);
    return output;
})hlsl";
    const char *pixel = R"hlsl(
Texture2D<float> Visibility : register(t0, space2);
SamplerState PointSampler : register(s0, space2);
cbuffer Parameters : register(b0, space3) { float4 Tuning; };
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target0 {
    float factor = 1.0 - Tuning.x * (1.0 - Visibility.SampleLevel(PointSampler,uv,0));
    return float4(factor,factor,factor,1);
})hlsl";
    std::vector<unsigned char> spirv;
    if (!CompositeVertex) {
        if (!SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error)) return false;
        CompositeVertex = SDL3_Create_Shader(Frame.Device(), spirv, true, error);
        if (!CompositeVertex) return false;
    }
    if (!CompositePixel) {
        if (!SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error)) return false;
        CompositePixel = SDL3_Create_Shader(Frame.Device(), spirv, false, error);
        if (!CompositePixel) return false;
    }
    if (!CompositeSampler) {
        SDL_GPUSamplerCreateInfo sampler = {};
        sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        CompositeSampler = SDL_CreateGPUSampler(Frame.Device(), &sampler);
        if (!CompositeSampler) { error = SDL_GetError(); return false; }
    }
    SDL_GPUColorTargetDescription colour = {};
    colour.format = SDL_GPUTextureFormat(format);
    colour.blend_state.enable_blend = true;
    colour.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colour.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_COLOR;
    colour.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colour.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    colour.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo pipeline = {};
    pipeline.vertex_shader = CompositeVertex;
    pipeline.fragment_shader = CompositePixel;
    pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.color_target_descriptions = &colour;
    pipeline.target_info.num_color_targets = 1;
    CompositePipeline = SDL_CreateGPUGraphicsPipeline(Frame.Device(), &pipeline);
    if (!CompositePipeline) { error = SDL_GetError(); return false; }
    CompositeFormat = format;
    error.clear(); return true;
}

bool SdlHardwareSunShadow::Composite(const SdlTarget &target, float strength, std::string &error, const int32_t *scissor)
{
    if (!Visibility || !CompositePipeline || Frame.Target_Colour_Format(target) != CompositeFormat ||
        target.Width != Width || target.Height != Height) { error = "unprepared hardware shadow composite"; return false; }
    SdlRecordedDraw draw = {};
    draw.Pipeline = CompositePipeline;
    draw.Count = 3;
    draw.Viewport[2] = float(Width); draw.Viewport[3] = float(Height); draw.Viewport[5] = 1;
    draw.Scissor[2] = Width; draw.Scissor[3] = Height;
    if (scissor) {
        if (scissor[0] < 0 || scissor[1] < 0 || scissor[2] <= 0 || scissor[3] <= 0 ||
            uint32_t(scissor[0]) > Width || uint32_t(scissor[1]) > Height ||
            uint32_t(scissor[2]) > Width - uint32_t(scissor[0]) ||
            uint32_t(scissor[3]) > Height - uint32_t(scissor[1])) {
            error = "invalid main-view composite scissor"; return false;
        }
        std::memcpy(draw.Scissor, scissor, sizeof(draw.Scissor));
    }
    draw.SamplerCount = 1;
    draw.Textures[0] = Visibility; draw.Samplers[0] = CompositeSampler;
    const float tuning[4] = {strength, 0, 0, 0};
    const SdlNativeUniformBlock block = {1, 0, tuning, sizeof(tuning)};
    // No depth attachment: multiply only opaque scene RGB while preserving its depth for water.
    SdlTarget colour = target; colour.Depth = nullptr; colour.DepthFormat = 0;
    if (!Frame.Record_Native_Draw(colour, draw, &block, 1)) { error = "hardware shadow composite record refused"; return false; }
    error.clear(); return true;
}


bool SdlHardwareSunShadow::RenderSunDepth(const SdlTarget &target, const float *inverseSunViewProjection,
    std::string &error)
{
    if (!SubmitQueuedSunMerge(error)) return false;
    DepthOutput=false; SunMergeRectangles.clear();
    SunBasis current;
    if (!target.Depth || !target.DepthFormat || !target.Width || !target.Height ||
        target.Width>8192 || target.Height>8192 || !current.Load(inverseSunViewProjection)) {
        SunCacheValid=false; error="missing sun attachment or nonorthographic inverse"; return false;
    }
    SunTargetWidth=target.Width; SunTargetHeight=target.Height;
    std::vector<Rectangle> needed;
    bool projectedTarget=false;
    const auto targetCoverage=[&]() -> const std::vector<Rectangle> & {
        if(!projectedTarget) {
            needed=sun_coverage(current,GeometryBounds,target.Width,target.Height);
            projectedTarget=true;
        }
        return needed;
    };
    SunBasis cache;
    double offset[2]={},stride[2]={1,1}; float scale=1,bias=0;
    double metrics[6]={}; unsigned reason=SUN_NO_CACHE;
    const bool exact=SunSourceWidth==target.Width && SunSourceHeight==target.Height &&
        std::memcmp(SunSourceInverse,inverseSunViewProjection,sizeof(SunSourceInverse))==0;
    bool reuse=SunCacheValid && !SubmissionFailed && cache.Load(SunDepthInverse);
    if(reuse) reuse=sun_reprojection(cache,Width,Height,current,target.Width,target.Height,
        offset,stride,scale,bias,reason,metrics);
    if(reuse && !exact && !(SunCacheZContained && current.ContainsDepth(SceneBounds))
        && !sun_relevant_depth_contained(current,cache,GeometryBounds,target.Width,target.Height)) {
        reason=SUN_DEPTH_CLIP; reuse=false;
    }
    const auto fitsCache=[&](unsigned x,unsigned y,unsigned width,unsigned height) {
        return x*stride[0]+offset[0]>=0 && y*stride[1]+offset[1]>=0
            && (x+width-1)*stride[0]+offset[0]<Width && (y+height-1)*stride[1]+offset[1]<Height;
    };
    if(reuse && !fitsCache(0,0,target.Width,target.Height)) {
        for(const auto &rect:targetCoverage()) {
            if(!fitsCache(rect[0],rect[1],rect[2],rect[3])) {
                reason=SUN_COVERAGE; reuse=false; break;
            }
        }
    }
    if(reuse) {
        auto missing=SunTiles.Missing(offset,stride,target.Width,target.Height);
        const uint64_t missingTexels=sun_texels(missing);
        if(!missing.empty() && missingTexels>std::min(uint64_t(target.Width)*target.Height,
                sun_texels(targetCoverage())+131072)) { reason=SUN_COVERAGE; reuse=false; }
        else if(!missing.empty()) {
            SdlTarget output=target; output.Width=Width; output.Height=Height;
            const float unusedLight[3]={0,0,1};
            // RenderInternal first submits queued borrowed readers, then waits
            // this scene's descriptor fence. It never clears untouched texels.
            if(!RenderInternal(output,nullptr,SunDepthInverse,unusedLight,0,1,error,true,&missing)) {
                SunTiles.Invalidate(); SunCacheValid=false; return false;
            }
            SunTiles.Submitted(missing); SunCacheRectangles=SunTiles.Ready();
            SunCacheValid=true; ++SunFills; SunFillTexels+=missingTexels;
            SunTraceTexels+=missingTexels;
        }
    }
    if(reuse) {
        for(unsigned k=0;k<2;++k) { SunSampleOffset[k]=offset[k]; SunSampleStride[k]=stride[k]; }
        SunDepthScale=scale; SunDepthBias=bias;
        SunMergeRectangles=sun_merge_rectangles(SunCacheRectangles,offset,stride,target.Width,target.Height);
        DepthOutput=true; ++SunReplays; error.clear(); return true;
    }
    if(targetCoverage().empty()) { DepthOutput=true; ++SunReplays; error.clear(); return true; }
    ++SunRejects[reason];
    if(reason!=SunLastReject) {
        double lo[3],hi[3]; current.Project(SceneBounds,lo,hi);
        std::fprintf(stderr,"HardwareRaySun: cache miss reason=%s pitch-current=%.9g,%.9g pitch-cache=%.9g,%.9g map-offset=%.9g,%.9g map-stride=%.9g,%.9g map-cross=%.9g,%.9g static-z=%.9g..%.9g cache-z-contained=%u\n",
            sun_reject_name(reason),2*std::sqrt(current.squared[0])/target.Width,
            2*std::sqrt(current.squared[1])/target.Height,
            Width ? 2*std::sqrt(cache.squared[0])/Width : 0,
            Height ? 2*std::sqrt(cache.squared[1])/Height : 0,
            metrics[0],metrics[1],metrics[2],metrics[3],metrics[4],metrics[5],lo[2],hi[2],unsigned(SunCacheZContained));
        SunLastReject=reason;
    }
    // Fresh planning replaces the CPU tile epoch before any GPU dispatch. A
    // planning/allocation failure must not leave the previous epoch reusable.
    SunCacheValid=false;
    // Allocate the guard lattice but populate only occupied tiles touching this
    // target. Tile-aligned margins preserve the target's tile grid exactly.
    const unsigned tile=target.Width>=512 && target.Height>=512 ? 128 : 8;
    unsigned guardX=std::min(target.Width/4,(8192-std::min(target.Width,8192u))/2)/tile*tile;
    unsigned guardY=std::min(target.Height/4,(8192-std::min(target.Height,8192u))/2)/tile*tile;
    SunLastTargetTexels=sun_texels(targetCoverage());
    const float unusedLight[3]={0,0,1};
    bool rendered=false;
    for(unsigned attempt=0;attempt<2 && !rendered;++attempt) {
        const unsigned gx=attempt ? 0 : guardX, gy=attempt ? 0 : guardY;
        SdlTarget output=target; output.Width=target.Width+gx*2; output.Height=target.Height+gy*2;
        float inverse[16]; std::memcpy(inverse,inverseSunViewProjection,sizeof(inverse));
        for(unsigned k=0;k<3;++k) {
            inverse[k]*=float(output.Width)/target.Width;
            inverse[4+k]*=float(output.Height)/target.Height;
        }
        SunBasis volume;
        if(!volume.Load(inverse)) { error="invalid guarded sun volume"; return false; }
        const auto occupied=sun_coverage(volume,GeometryBounds,output.Width,output.Height,tile);
        SunTiles.Reset(output.Width,output.Height,tile,occupied);
        const double initialOffset[2]={double(gx),double(gy)}, initialStride[2]={1,1};
        const auto rectangles=SunTiles.Missing(initialOffset,initialStride,target.Width,target.Height);
        if(rectangles.empty()) { error="empty guarded sun coverage"; return false; }
        // Projection roundoff can add a boundary tile. Bound this small initial
        // excess before committing; never eagerly trace all occupied guard cells.
        const uint64_t initialBudget=std::min(uint64_t(target.Width)*target.Height,SunLastTargetTexels+131072);
        if(sun_texels(rectangles)>initialBudget && (gx || gy)) { ++SunGuardLimited; continue; }
        if(!RenderInternal(output,nullptr,inverse,unusedLight,0,1,error,true,&rectangles)) {
            if(SubmissionFailed || (!gx && !gy)) return false;
            continue; // Allocation pressure can fall back to target-sized bounded tracing.
        }
        SunTiles.Submitted(rectangles); SunCacheRectangles=SunTiles.Ready();
        std::memcpy(SunDepthInverse,inverse,sizeof(inverse));
        std::memcpy(SunSourceInverse,inverseSunViewProjection,sizeof(SunSourceInverse));
        SunSourceWidth=target.Width; SunSourceHeight=target.Height;
        SunCacheZContained=volume.ContainsDepth(SceneBounds); SunCacheValid=true;
        SunSampleOffset[0]=int(gx); SunSampleOffset[1]=int(gy);
        SunSampleStride[0]=SunSampleStride[1]=1;
        SunDepthScale=1; SunDepthBias=0;
        SunMergeRectangles=sun_merge_rectangles(SunCacheRectangles,SunSampleOffset,SunSampleStride,target.Width,target.Height);
        SunLastTraceTexels=sun_texels(rectangles);
        SunTraceTexels+=SunLastTraceTexels; ++SunTraces; rendered=true;
    }
    return rendered;
}

void SdlHardwareSunShadow::ReportSunCache() const
{
    if(SunTraces+SunReplays!=1 && (SunTraces+SunReplays)%60) return;
    if(SunFills) std::fprintf(stderr,"HardwareRaySun: lazy fills=%llu fill-texels=%llu initialized-texels=%llu\n",
        (unsigned long long)SunFills,(unsigned long long)SunFillTexels,
        (unsigned long long)sun_texels(SunCacheRectangles));
    std::fprintf(stderr,"HardwareRaySun: bounded cache traces=%llu replays=%llu trace-texels=%llu merge-texels=%llu cache=%ux%u target=%ux%u merge-rects=%zu last-trace=%llu target-bound=%llu guard-limited=%llu rejects(no-cache/z/basis/pitch/lattice/coverage)=%llu/%llu/%llu/%llu/%llu/%llu\n",
        (unsigned long long)SunTraces,(unsigned long long)SunReplays,(unsigned long long)SunTraceTexels,
        (unsigned long long)SunMergeTexels,Width,Height,SunTargetWidth,SunTargetHeight,SunMergeRectangles.size(),
        (unsigned long long)SunLastTraceTexels,(unsigned long long)SunLastTargetTexels,
        (unsigned long long)SunGuardLimited,(unsigned long long)SunRejects[0],(unsigned long long)SunRejects[1],
        (unsigned long long)SunRejects[2],(unsigned long long)SunRejects[3],
        (unsigned long long)SunRejects[4],(unsigned long long)SunRejects[5]);
}

bool SdlHardwareSunShadow::SubmitQueuedSunMerge(std::string &error)
{
    if (!SunMergeQueued) return true;
    // End (or an earlier full batch) normally submitted it already. If a caller
    // starts another query sooner, submit its borrowed-texture readers first.
    if (Frame.Flushes_Total() == SunMergeQueuedFlush && !Frame.Flush()) {
        error = "queued sun depth readers could not be submitted"; return false;
    }
    SunMergeQueued = false;
    return true;
}

bool SdlHardwareSunShadow::MergeSunDepth(const SdlTarget &target, std::string &error, bool submit)
{
    if (!DepthOutput || target.Width!=SunTargetWidth || target.Height!=SunTargetHeight ||
        !target.Depth || target.Colour || !target.DepthFormat ||
        (!SunMergeRectangles.empty() && (!SunCacheValid || SubmissionFailed || !Visibility))) {
        error = "unprepared ray-query sun depth merge"; return false;
    }
    if(SunMergeRectangles.empty()) { ReportSunCache(); error.clear(); return true; }
    // Separate graphics pipeline: no colour output or multiplicative composite.
    if (!DepthMergePipeline || DepthMergeFormat != target.DepthFormat) {
        // An existing pipeline may still be referenced by queued or submitted draws.
        if (DepthMergePipeline) {
            if (!Frame.Flush() || !SDL_WaitForGPUIdle(Frame.Device())) {
                error = SDL_GetError();
                return false;
            }
            SDL_ReleaseGPUGraphicsPipeline(Frame.Device(), DepthMergePipeline);
            DepthMergePipeline = nullptr;
        }
        const char *vertex = R"hlsl(
struct Output { float4 Position : SV_Position; float2 UV : TEXCOORD0; };
Output main(uint id : SV_VertexID) {
    Output output;
    output.UV = float2((id << 1) & 2, id & 2);
    output.Position = float4(output.UV * float2(2,-2) + float2(-1,1),0,1);
    return output;
})hlsl";
        const char *pixel = R"hlsl(
Texture2D<float> RayDepth : register(t0, space2);
SamplerState PointSampler : register(s0, space2);
cbuffer Parameters : register(b0, space3) { float4 Cache; float4 Extent; };
float main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Depth {
    float depth=RayDepth.SampleLevel(PointSampler,(position.xy*Extent.zw+Cache.xy+.5*(1-Extent.zw))/Extent.xy,0);
    // Miss is a sentinel, not a point on the old far plane to be reprojected.
    if(depth>=1) return 1;
    depth=depth*Cache.z+Cache.w;
    return depth>=0 && depth<=1 ? depth : 1;
})hlsl";
        std::vector<unsigned char> spirv;
        if (!CompositeVertex) {
            if (!SDL3_Compile_HLSL_To_SPIRV(vertex, true, spirv, error)) return false;
            CompositeVertex = SDL3_Create_Shader(Frame.Device(), spirv, true, error);
            if (!CompositeVertex) return false;
        }
        if (!DepthMergePixel) {
            if (!SDL3_Compile_HLSL_To_SPIRV(pixel, false, spirv, error)) return false;
            DepthMergePixel = SDL3_Create_Shader(Frame.Device(), spirv, false, error);
            if (!DepthMergePixel) return false;
        }
        if (!CompositeSampler) {
            SDL_GPUSamplerCreateInfo sampler = {};
            sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
            sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
            sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            CompositeSampler = SDL_CreateGPUSampler(Frame.Device(), &sampler);
            if (!CompositeSampler) { error = SDL_GetError(); return false; }
        }
        SDL_GPUGraphicsPipelineCreateInfo pipeline = {};
        pipeline.vertex_shader = CompositeVertex;
        pipeline.fragment_shader = DepthMergePixel;
        pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipeline.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline.depth_stencil_state.enable_depth_test = true;
        pipeline.depth_stencil_state.enable_depth_write = true;
        pipeline.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
        pipeline.target_info.has_depth_stencil_target = true;
        pipeline.target_info.depth_stencil_format = SDL_GPUTextureFormat(target.DepthFormat);
        DepthMergePipeline = SDL_CreateGPUGraphicsPipeline(Frame.Device(), &pipeline);
        if (!DepthMergePipeline) { error = SDL_GetError(); return false; }
        DepthMergeFormat = target.DepthFormat;
    }
    SdlRecordedDraw draw = {};
    draw.Pipeline = DepthMergePipeline; draw.Count = 3;
    draw.Viewport[2] = float(target.Width); draw.Viewport[3] = float(target.Height); draw.Viewport[5] = 1;
    draw.SamplerCount = 1;
    draw.Textures[0] = Visibility; draw.Samplers[0] = CompositeSampler;
    const float tuning[8]={float(SunSampleOffset[0]),float(SunSampleOffset[1]),SunDepthScale,SunDepthBias,
        float(Width),float(Height),float(SunSampleStride[0]),float(SunSampleStride[1])};
    const SdlNativeUniformBlock block={1,0,tuning,sizeof(tuning)};
    std::vector<SdlNativeDrawPacket> packets;
    packets.reserve(SunMergeRectangles.size());
    for(const auto &rect:SunMergeRectangles) {
        for(unsigned k=0;k<4;++k) draw.Scissor[k]=int32_t(rect[k]);
        packets.push_back({target,draw,&block,1});
    }
    if (!Frame.Record_Native_Draw_Batch(packets.data(),unsigned(packets.size()))) {
        error = "ray-query sun depth merge record refused"; return false;
    }
    SunMergeQueued = true;
    SunMergeQueuedFlush = Frame.Flushes_Total();
    if (submit && !SubmitQueuedSunMerge(error)) return false;
    SunMergeTexels+=sun_texels(SunMergeRectangles); ReportSunCache();
    error.clear(); return true;
}

#else
SdlHardwareSunShadow *SdlHardwareSunShadow::Create(SdlGpuFrame &, const float *, uint32_t,
    const uint32_t *, uint32_t, std::string &error, const std::vector<uint32_t> &,
    const std::vector<uint32_t> &)
{
    error="hardware Vulkan ray queries are available only on Linux"; return nullptr;
}
SdlHardwareSunShadow::~SdlHardwareSunShadow() = default;
bool SdlHardwareSunShadow::UpdateGeometry(const float *, uint32_t, const uint32_t *, uint32_t,
    std::string &, const std::vector<uint32_t> &, const std::vector<uint32_t> &) { return false; }
bool SdlHardwareSunShadow::Render(const SdlTarget &, const float *, const float *, float, float, std::string &error)
{
    error="hardware Vulkan ray queries are available only on Linux"; return false;
}
bool SdlHardwareSunShadow::ReadyFor(const SdlTarget &) const { return false; }
bool SdlHardwareSunShadow::RenderSunDepth(const SdlTarget &, const float *, std::string &) { return false; }
bool SdlHardwareSunShadow::MergeSunDepth(const SdlTarget &, std::string &, bool) { return false; }
bool SdlHardwareSunShadow::PrepareComposite(const SdlTarget &, std::string &) { return false; }
bool SdlHardwareSunShadow::RenderResolved(const SdlTarget &, SDL_GPUTexture *, const float *, const float *,
    float, float, std::string &) { return false; }
bool SdlHardwareSunShadow::Composite(const SdlTarget &, float, std::string &, const int32_t *) { return false; }

#endif
