// Plain capture values, also used by legacy renderer headers with min/max macros.
#pragma once

namespace WaterPathTracing {

struct Vec3 { float x, y, z; };

// World-space directions spanning the cropped reflected camera, with dy down.
struct View {
    Vec3 origin{}, rayTopLeft{}, rayDx{}, rayDy{};
    Vec3 sunDirection{0,0,1}, sunRadiance{1,1,1};
    Vec3 skyLow{0.20f,0.30f,0.38f}, skyHigh{0.04f,0.10f,0.18f};
    float waterLevel = 0;
    float maxDistance = 1000; // From the primary ray's water-plane origin.
    float secondaryDistance = 160;
    float shadowDistance = 600;
    unsigned secondaryNodeBudget = 384;
    unsigned shadowNodeBudget = 512;
    unsigned size = 256;
};
static_assert(sizeof(View)==31*sizeof(float),"View change detection requires no padding");

}
