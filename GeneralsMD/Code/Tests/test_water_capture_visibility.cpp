#include "test_harness.h"
#include "Platform/WaterCaptureVisibility.h"

using namespace WaterCaptureVisibility;

namespace {
void Box(Plane (&planes)[6], double lowX, double lowY, double highX, double highY)
{
    planes[0] = {1,0,0,-lowX}; planes[1] = {-1,0,0,highX};
    planes[2] = {0,1,0,-lowY}; planes[3] = {0,-1,0,highY};
    planes[4] = {0,0,1,100}; planes[5] = {0,0,-1,100};
}
Result Classify9(Bounds bounds, double level, const Plane (&planes)[6],
    const std::vector<uint8_t> &heights, double vertical = 0, double horizontal = 0,
    WaterTerrainCulling::WetNodeCache *cache = nullptr)
{
    return Classify(bounds,level,planes,heights.data(),9,9,0,1,1,vertical,horizontal,cache,true);
}
}

TEST(water_capture_visibility_clips_each_support_before_terrain_bounds)
{
    Plane planes[6]; Box(planes,-20,-20,20,20);
    std::vector<uint8_t> heights(81,30);
    // The camera's footprint extends off the heightmap; this actual water area does not.
    CHECK(Classify9({2,2,6,6},10,planes,heights) == Result::Dry);
    // A visible skirt sea has no terrain proof even when the whole heightmap is dry.
    CHECK(Classify9({-5,2,6,6},10,planes,heights) == Result::OutsideTerrain);
    Box(planes,2,2,6,6);
    CHECK(Classify9({-20,-20,20,20},10,planes,heights) == Result::Dry);
    CHECK(Classify9({-20,-20,-10,-10},10,planes,heights) == Result::Offscreen);
    CHECK(Hidden(Result::Offscreen) && Hidden(Result::Dry));
    CHECK(!Hidden(Result::OutsideTerrain) && !Hidden(Result::UnknownSurface));
}

TEST(water_capture_visibility_keeps_ponds_linear_terrain_and_elevated_levels)
{
    Plane planes[6]; Box(planes,1,1,7,7);
    std::vector<uint8_t> heights(81,30);
    WaterTerrainCulling::WetNodeCache cache;
    CHECK(Classify9({2.2,2.2,5.8,5.8},10,planes,heights,0,0,&cache) == Result::Dry);
    // A wet interior cell cannot be missed by an endpoint or screen-corner sample.
    heights[4*9+4] = 0;
    CHECK(Classify9({2.2,2.2,5.8,5.8},10,planes,heights) == Result::PossibleWater);
    // A crossed cell's low corner matters even when no authored vertex is at that corner.
    heights.assign(81,30); heights[2*9+2] = 0;
    CHECK(Classify9({2.2,2.2,2.8,2.8},10,planes,heights) == Result::PossibleWater);
    heights.assign(81,30);
    CHECK(Classify9({2,2,6,6},10,planes,heights) == Result::Dry);
    CHECK(Classify9({2,2,6,6},30,planes,heights) == Result::PossibleWater);
    CHECK(Classify9({2,2,6,6},10,planes,heights,20) == Result::PossibleWater);
    // The complete vertical envelope remains live even when terrain damping fades waves.
    CHECK(Classify9({2,2,6,6},10,planes,heights,19) == Result::Dry);
}

TEST(water_capture_visibility_preserves_horizontal_chop_and_frustum_crests)
{
    Plane planes[6]; Box(planes,1,1,7,7);
    std::vector<uint8_t> heights(81,30);
    heights[3*9+5] = 0;
    CHECK(Classify9({2.2,2.2,3.8,3.8},10,planes,heights) == Result::Dry);
    CHECK(Classify9({2.2,2.2,3.8,3.8},10,planes,heights,0,1) == Result::PossibleWater);
    heights.assign(81,30);
    Box(planes,5,1,7,7);
    CHECK(Classify9({3,2,4,4},10,planes,heights) == Result::Offscreen);
    CHECK(Classify9({3,2,4,4},10,planes,heights,0,1.1) == Result::Dry);
    // A tilted side plane sees a crest whose rest point is outside the base-plane footprint.
    planes[0] = {1,0,1,-16};
    CHECK(Classify9({3,2,4,4},10,planes,heights) == Result::Offscreen);
    CHECK(Classify9({3,2,4,4},10,planes,heights,3) == Result::Dry);
    // On a camera move, classify the new support before deciding to keep a prior capture.
    Box(planes,1,1,7,7); heights[3*9+3] = 0;
    CHECK(Classify9({2,2,4,4},10,planes,heights) == Result::PossibleWater);
    Box(planes,5,5,7,7);
    CHECK(Classify9({2,2,4,4},10,planes,heights) == Result::Offscreen);
    Box(planes,1,1,7,7);
    CHECK(Classify9({2,2,4,4},10,planes,heights) == Result::PossibleWater);
}

TEST(water_capture_visibility_declines_unknown_inputs_and_matches_cached_proof)
{
    Plane planes[6]; Box(planes,1,1,7,7);
    std::vector<uint8_t> heights(81,30);
    WaterTerrainCulling::WetNodeCache cache;
    const Bounds bounds = {2,2,6,6};
    CHECK(Classify9(bounds,10,planes,heights,0,0,&cache) == Classify9(bounds,10,planes,heights));
    CHECK(Classify9(bounds,30,planes,heights,0,0,&cache) == Classify9(bounds,30,planes,heights));
    CHECK(Classify9(bounds,10,planes,heights,-1) == Result::UnknownSurface);
    CHECK(Classify9(bounds,10,planes,heights,0,-1) == Result::UnknownSurface);
    CHECK(Classify9({6,2,2,6},10,planes,heights) == Result::UnknownSurface);
    planes[0].a = std::numeric_limits<double>::quiet_NaN();
    CHECK(Classify9(bounds,10,planes,heights) == Result::UnknownSurface);
    Box(planes,1,1,7,7);
    CHECK(Classify(bounds,10,planes,nullptr,9,9,0,1,1,0,0) == Result::UnknownSurface);
}

TEST(water_capture_visibility_admits_water_when_near_clip_removes_hiding_terrain)
{
    std::vector<uint8_t> heights(81,30);
    CHECK(WaterTerrainCulling::CameraAboveTerrain(heights.data(),9,9,0,1,1,4,4,40));
    // Eye z=40 looks down with near distance 20. Terrain z=30 is in front of
    // the near plane and therefore cannot establish rasterized water occlusion.
    Point3 near[4] = {{2,2,20},{6,2,20},{2,6,20},{6,6,20}};
    Plane planes[6]; Box(planes,1,1,7,7);
    planes[4] = {0,0,-1,20};
    const bool clear = NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1);
    CHECK(!clear);
    CHECK(Classify({2,2,6,6},0,planes,heights.data(),9,9,0,1,1,0,0,nullptr,clear)
        == Result::CameraUnproven);
    // Proven emptiness does not depend on the camera/near-plane terrain proof.
    CHECK(Classify({-20,-20,-10,-10},0,planes,heights.data(),9,9,0,1,1,0,0,nullptr,clear)
        == Result::Offscreen);
    for (auto &point : near) point.z = 35;
    const bool clearBeforeTerrain = NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1);
    CHECK(clearBeforeTerrain);
    planes[4].d = 35;
    CHECK(Classify({2,2,6,6},0,planes,heights.data(),9,9,0,1,1,0,0,nullptr,clearBeforeTerrain)
        == Result::Dry);
}

TEST(water_capture_visibility_near_air_proof_includes_tilted_planes_and_interior_cliffs)
{
    std::vector<uint8_t> heights(81,0);
    Point3 near[4] = {{2,2,32},{6,2,45},{2,6,32},{6,6,45}};
    CHECK(NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
    // Checking only terrain under the four near corners misses this high cliff
    // inside the near rectangle. A ray may emerge below its clipped plateau.
    heights[4*9+4] = 35;
    CHECK(!NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
    heights[4*9+4] = 32; // Touching is also unproven, including linear terrain boundaries.
    CHECK(!NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
    heights[4*9+4] = 31;
    CHECK(NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
    near[0].x = -1;
    CHECK(!NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
    near[0].x = std::numeric_limits<double>::quiet_NaN();
    CHECK(!NearPlaneAboveTerrain(near,heights.data(),9,9,0,1,1));
}
