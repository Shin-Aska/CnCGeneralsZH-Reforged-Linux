/* Render-only water displacement. No gameplay height, collision or navigation state. */
#pragma once
#include <cmath>
#include <string>
#include <cstdint>

// kx, ky (radians/world unit), amplitude (world units), angular frequency, initial phase.
// Differing dispersion and directions ensure this cannot be represented by scrolling a static map.
#define WATER_SURFACE_MODES(X) \
 X(0.1093f, 0.0395f, 1.30f, 0.94f, 0.17f) \
 X(0.1571f,-0.1264f, 0.85f, 1.24f, 2.71f) \
 X(0.2772f, 0.1810f, 0.50f, 1.59f, 4.13f) \
 X(-0.1524f,0.4835f, 0.24f, 1.97f, 1.39f) \
 X(0.6856f, 0.2624f, 0.095f,2.37f, 5.21f) \
 X(0.7915f,-0.8025f, 0.061f,2.94f, 3.47f) \
 X(-0.5575f,1.5408f,0.038f, 3.54f, 0.83f) \
 X(1.9218f, 0.6150f, 0.026f,3.93f, 4.79f) \
 X(2.4708f,-1.3037f, 0.016f,4.62f, 2.02f) \
 X(-1.7671f,3.0640f,0.011f, 5.20f, 5.91f) \
 X(4.0647f, 1.6195f, 0.007f,5.79f, 1.12f) \
 X(3.0073f,-4.1009f, 0.005f,6.24f, 3.86f) \
 X(0.3507f,-0.3705f, 0.110f,1.98f, 5.31f) \
 X(-0.4731f,-0.2409f,0.090f,2.02f, 0.93f) \
 X(0.6419f, 0.5344f, 0.085f,2.53f, 3.81f) \
 X(-0.3787f,0.9255f, 0.064f,2.77f, 2.19f) \
 X(1.0774f,-0.3441f, 0.060f,2.94f, 4.67f) \
 X(-1.1037f,-0.8015f,0.046f,3.23f, 1.87f) \
 X(0.1158f, 1.6119f, 0.040f,3.52f, 0.43f) \
 X(1.7408f,-0.8109f, 0.032f,3.84f, 5.63f) \
 X(-1.3733f,1.8788f, 0.025f,4.22f, 3.03f) \
 X(2.8292f, 0.0917f, 0.020f,4.66f, 1.71f) \
 X(-1.2031f,-2.9508f,0.016f,4.94f, 4.37f) \
 X(2.3156f, 3.0217f, 0.013f,5.40f, 2.57f)

namespace WaterSurfaceModel {
struct Sample { float height, dx, dy; };
inline Sample Evaluate(float x, float y, float seconds) {
    Sample value = {0,0,0};
#define WATER_MODE_EVAL(kx,ky,amplitude,omega,phase) { \
    const float p = x*kx+y*ky-seconds*omega+phase; \
    const float s = std::sin(p), c = std::cos(p); \
    value.height += amplitude*(s+0.13f*2.0f*s*c); \
    const float derivative = amplitude*(c+0.26f*(c*c-s*s)); \
    value.dx += derivative*kx; value.dy += derivative*ky; }
    WATER_SURFACE_MODES(WATER_MODE_EVAL)
#undef WATER_MODE_EVAL
    return value;
}
inline const char *ShaderSource() {
#define WATER_MODE_HLSL(kx,ky,amplitude,omega,phase) \
    "{ float p=dot(pxy,float2(" #kx "," #ky "))-seconds*" #omega "+" #phase ";" \
    "float s=sin(p), c=cos(p); value.x+=" #amplitude "*(s+0.26*s*c);" \
    "value.yz+=" #amplitude "*(c+0.26*(c*c-s*s))*float2(" #kx "," #ky "); }\n"
    return "float3 waterWave(float2 pxy,float seconds) { float3 value=0;\n"
        WATER_SURFACE_MODES(WATER_MODE_HLSL)
        "return value; }\n";
#undef WATER_MODE_HLSL
}
inline void AppendHLSL(std::string &source) { source += ShaderSource(); }
inline float AdvanceTime(float seconds, float elapsed, bool paused, bool scriptFrozen, bool debugFrozen) {
    if (paused || scriptFrozen || debugFrozen) return seconds;
    return seconds + (elapsed < 0 ? 0 : (elapsed > 0.1f ? 0.1f : elapsed));
}
inline float ShoreDamping(float depth) {
    float t = depth/9.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t*t*(3-2*t);
}
#define WATER_MODE_BOUND(kx,ky,amplitude,omega,phase) + amplitude * 1.13f
constexpr float MaxDisplacement = 0.0f WATER_SURFACE_MODES(WATER_MODE_BOUND);
#undef WATER_MODE_BOUND
constexpr int MeshCells = 128;
constexpr int MeshVertices = (MeshCells+1)*(MeshCells+1);
constexpr int MeshTriangles = MeshCells*MeshCells*2;
// This is a whole-map bound, before frustum rejection; each draw is still safely 16-bit indexed.
// It permits 64 cells on multi-segment rivers and 128 on broad oceans, so near-camera silhouettes
// resolve the dominant waves instead of relying on their shading to hide a sparse mesh.
constexpr int VertexBudget = 262144;
inline int IndexBatchRows(int cells) { return 65535/(cells*6); }
inline int CellsForPatches(int patches) {
    if (patches<=0) return MeshCells;
    int cells=MeshCells;
    while (cells>1 && static_cast<long long>(patches)*(cells+1)*(cells+1)>VertexBudget) cells/=2;
    return cells;
}
// All patches use one camera-height LOD, so both sides of a shared edge change together.
// Between two resolutions, the fine mesh morphs onto the coarse triangles before changing
// topology. Full detail is retained below 120 world units above the highest authored water.
struct MeshLOD { int cells; float morph; };
constexpr float FullDetailHeight = 120.0f;
constexpr float LODHeightRatio = 1.5f;
constexpr int MinimumLODCells = 8;
inline MeshLOD LODForCameraHeight(int baseCells, float height) {
    MeshLOD lod={baseCells,0.0f};
    const int minimum=baseCells<MinimumLODCells?baseCells:MinimumLODCells;
    while (height>=LODHeightRatio*FullDetailHeight && lod.cells>minimum) {
        height/=LODHeightRatio;
        lod.cells/=2;
    }
    if (height>FullDetailHeight && lod.cells>minimum) {
        const float t=(height-FullDetailHeight)/(FullDetailHeight*(LODHeightRatio-1.0f));
        lod.morph=t*t*(3.0f-2.0f*t);
    }
    return lod;
}
// Every fine vertex is a coarse vertex or the midpoint of a coarse edge/diagonal. The same
// a-to-d diagonal is used by the renderer's triangle indices. Endpoints are always even/even,
// allowing an in-place morph pass without overwriting samples that another vertex still needs.
struct CoarsePair { int first,second; };
inline CoarsePair CoarsePairForVertex(int i,int j,int cells) {
    const int side=cells+1,self=j*side+i;
    if (cells<2) return {self,self};
    const int ox=i&1,oy=j&1;
    return {(j-oy)*side+i-ox,(j+oy)*side+i+ox};
}
inline float MorphCoordinate(float fine,float a,float b,float morph) {
    if (morph<=0.0f) return fine;
    const float coarse=(a+b)*0.5f;
    if (morph>=1.0f) return coarse;
    return fine+(coarse-fine)*morph;
}
inline std::uint32_t MorphColour(std::uint32_t fine,std::uint32_t a,std::uint32_t b,float morph) {
    if (morph<=0.0f) return fine;
    std::uint32_t result=0;
    for (int shift=0;shift<32;shift+=8) {
        const float value=MorphCoordinate(float((fine>>shift)&255),float((a>>shift)&255),float((b>>shift)&255),morph);
        result|=std::uint32_t(value+0.5f)<<shift;
    }
    return result;
}
struct Point { float x,y,z; };
inline Point Lerp(const Point &a, const Point &b, float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
// A camera-dependent density warp moves continuously and keeps exact edge endpoints. Every patch
// computes a shared edge in canonical endpoint order, so even reversed edges yield identical bytes.
inline Point EdgePoint(Point a, Point b, float t, const Point &camera) {
    if (a.x>b.x || (a.x==b.x && (a.y>b.y || (a.y==b.y && a.z>b.z)))) {
        const Point swap=a; a=b; b=swap; t=1-t;
    }
    if (t<=0) return a;
    if (t>=1) return b;
    const float dx=b.x-a.x,dy=b.y-a.y,len2=dx*dx+dy*dy;
    if (len2<0.0001f) return Lerp(a,b,t);
    float focus=((camera.x-a.x)*dx+(camera.y-a.y)*dy)/len2;
    focus=focus<0?0:(focus>1?1:focus);
    const Point close=Lerp(a,b,focus);
    const float ex=camera.x-close.x,ey=camera.y-close.y,ez=camera.z-close.z;
    float radius=0.25f*std::sqrt((ex*ex+ey*ey+ez*ez)/len2);
    radius=radius<0.025f?0.025f:(radius>0.65f?0.65f:radius);
    const float lo=std::asinh(-focus/radius),hi=std::asinh((1-focus)/radius);
    const float concentrated=focus+radius*std::sinh(lo+(hi-lo)*t);
    return Lerp(a,b,0.35f*t+0.65f*concentrated);
}
// Coons blending preserves all four common boundary curves, including the degenerate edge of a
// triangle. A modest warp preserves a broad uniform component, limiting stretched triangles.
inline Point PatchPoint(const Point corners[4], const Point edges[4], float u, float v) {
    if (v==0) return edges[0];
    if (u==1) return edges[1];
    if (v==1) return edges[2];
    if (u==0) return edges[3];
    const Point a=Lerp(edges[0],edges[2],v),b=Lerp(edges[3],edges[1],u);
    const Point linear=Lerp(Lerp(corners[0],corners[1],u),Lerp(corners[3],corners[2],u),v);
    return {a.x+b.x-linear.x,a.y+b.y-linear.y,a.z+b.z-linear.z};
}
}
