#include "GameClient/TreeWaterSway.h"
#include <cstdio>
#include <initializer_list>
#include <limits>

static int failures=0;
static void expect(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; }
}

int main()
{
    using namespace TreeWaterSway;
    expect(weight(true,8,10)==0 && weight(true,10,10)==0,
        "submerged vertices and the waterline receive no wind");
    expect(weight(true,11.5f,10)==0.5f && weight(true,13,10)==1 && weight(true,80,10)==1,
        "exposed vertices ease into full wind within three world units");
    expect(weight(false,-80,10)==1,"dry trees retain wind even below another water body's height");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    expect(weight(true,nan,10)==1 && weight(true,10,nan)==1,
        "invalid water or vertex heights cannot poison the vertex payload");
    float previous=0;
    for (int sample=0; sample<=300; ++sample) {
        const float value=weight(true,10+sample*.01f,10);
        expect(value>=previous && value>=0 && value<=1,"waterline fade is bounded and monotonic");
        previous=value;
    }
    expect(weight(true,10.001f,10)<0.000001f && 1-weight(true,12.999f,10)<0.000001f,
        "both ends of the fade have a flat tangent rather than a velocity snap");
    expect(weight(true,12,10)>weight(true,12,11) && weight(true,12,12)==0,
        "rising flood water progressively stills a fixed branch");
    for (int type=0; type<=10; ++type) {
        expect(pack(type,false,-100,10)==float(type) && pack(type,true,100,10)==float(type),
            "dry and fully exposed payloads retain the exact original sway index");
        for (int sample=0; sample<=300; ++sample) {
            const float z=10+sample*.01f;
            const float payload=pack(type,true,z,10);
            expect(int(payload+.5f)==type,"all water weights preserve the shader constant index");
            const float decoded=1-4*(payload-std::floor(payload));
            expect(std::fabs(decoded-weight(true,z,10))<0.000004f,
                "fractional payload preserves per-vertex wind weight across all ten sway types");
        }
    }
    for (float windWeight:{0.0f,.5f,1.0f}) {
        for (float phase:{-1.0f,0.0f,1.0f}) {
            const float height=25,stretchX=.8f,stretchY=-.6f;
            const float windX=phase*.2f,windY=phase*.3f,windZ=-phase*phase*.1f;
            const auto projected=silhouette(stretchX,stretchY,windX,windY,windZ);
            // First deform the branch, then project its new height along the sun direction.
            const float visibleZ=height+height*windZ*windWeight;
            const float referenceX=height*windX*windWeight+visibleZ*stretchX;
            const float referenceY=height*windY*windWeight+visibleZ*stretchY;
            const float shadowX=height*(stretchX+(projected.x-stretchX)*windWeight);
            const float shadowY=height*(stretchY+(projected.y-stretchY)*windWeight);
            expect(std::fabs(shadowX-referenceX)<0.00001f && std::fabs(shadowY-referenceY)<0.00001f
                && height+height*projected.z==0,
                "flat shadows project the same filtered branch, including its vertical drop");
        }
    }
    if (!failures) std::puts("tree water sway: all checks passed");
    return failures ? 1 : 0;
}
