#include "test_harness.h"
#include "Platform/WaterPathTracingScene.h"
#include "Platform/WaterPathTracingSchedule.h"
#include "Platform/WaterPathTracingMotion.h"
#include "Platform/WaterPathCaptureScope.h"
#include <limits>

using namespace WaterPathTracing;

namespace {
Triangle Face(float x,float z,Vec3 albedo={0.7f,0.4f,0.1f})
{
    return {{x-0.4f,-0.4f,z},{x+0.4f,-0.4f,z},{x,0.4f,z},albedo};
}
}

TEST(water_path_refresh_limits_stationary_export_independently_of_render_rate)
{
    // At 10, 30, 60 and 144 FPS, intermediate render frames can accumulate the
    // same snapshot without repeatedly walking the game's meshes and terrain.
    for(unsigned fps:{10u,30u,60u,144u}) {
        RefreshSchedule schedule;
        uint64_t last=0;
        unsigned refreshes=0,heldFrames=0;
        for(unsigned frame=0;frame<fps*2;++frame) {
            const uint64_t now=uint64_t(frame)*1000/fps;
            if(schedule.ShouldRefresh(now)) {
                if(refreshes) CHECK(now-last>=RefreshSchedule::IntervalMs);
                last=now;
                ++refreshes;
                schedule.MarkRefreshed(now);
            } else {
                CHECK(now-last<RefreshSchedule::IntervalMs);
                ++heldFrames;
            }
        }
        CHECK(refreshes<=20);
        CHECK(refreshes>=15); // Discrete render frames can round a deadline upward.
        if(fps>10) CHECK(heldFrames>0);
    }
}

TEST(water_path_refresh_view_changes_bypass_the_pose_latency_limit)
{
    RefreshSchedule schedule;
    CHECK(schedule.ShouldRefresh(5000));
    schedule.MarkRefreshed(5000);
    CHECK(!schedule.ShouldRefresh(5000));
    CHECK(!schedule.ShouldRefresh(5099));
    CHECK(schedule.ShouldRefresh(5100));
    // The caller reports any camera, crop, plane, light or size change here.
    // These forced refreshes may exceed 10 Hz to keep the capture responsive.
    for(uint64_t now=5001;now<5010;++now) {
        CHECK(schedule.ShouldRefresh(now,true));
        schedule.MarkRefreshed(now);
        CHECK(!schedule.ShouldRefresh(now));
    }
    CHECK(!schedule.ShouldRefresh(5108));
    CHECK(schedule.ShouldRefresh(5109));
}

TEST(water_path_refresh_failed_exports_rollback_and_reentry_remain_due)
{
    RefreshSchedule schedule;
    CHECK(schedule.ShouldRefresh(0));
    CHECK(schedule.ShouldRefresh(1)); // No successful export was committed.
    schedule.MarkRefreshed(1000);
    CHECK(!schedule.ShouldRefresh(1099));
    CHECK(schedule.ShouldRefresh(1100));
    CHECK(schedule.ShouldRefresh(1101)); // Failed periodic export is retried.
    CHECK(schedule.ShouldRefresh(999)); // A reset clock cannot underflow the interval.
    schedule.MarkRefreshed(999);
    CHECK(!schedule.ShouldRefresh(1000));
    CHECK(!schedule.ShouldRefresh(1080));
    CHECK(schedule.ShouldRefresh(1070)); // Rollback within the same refresh interval.
    CHECK(schedule.ShouldRefresh(1071)); // Keep invalidation pending until export succeeds.
    schedule.MarkRefreshed(1071);
    CHECK(!schedule.ShouldRefresh(1072));
    CHECK(schedule.ShouldRefresh(1072,true));
    CHECK(schedule.ShouldRefresh(1073)); // A failed forced-view export remains due too.
    schedule.Reset(); // Leaving/re-entering the option or changing maps.
    CHECK(schedule.ShouldRefresh(1000));
    schedule.MarkRefreshed(std::numeric_limits<uint64_t>::max()-50);
    CHECK(!schedule.ShouldRefresh(std::numeric_limits<uint64_t>::max()));
    CHECK(schedule.ShouldRefresh(0)); // Includes unsigned clock wrap.
}

TEST(water_path_motion_does_not_restore_resolution_between_moving_snapshots)
{
    // Geometry moves on every 100 ms export. Fast render loops must not mistake
    // eight calls with a held revision for a settled scene and resize twice.
    for(unsigned fps:{30u,60u,120u,144u}) {
        RefreshSchedule refresh;
        MotionRecovery motion;
        bool initial=true,seenChange=false;
        for(unsigned frame=0;frame<fps*2;++frame) {
            const uint64_t now=uint64_t(frame)*1000/fps;
            bool changed=false;
            if(refresh.ShouldRefresh(now)) {
                changed=!initial;
                initial=false;
                refresh.MarkRefreshed(now);
            }
            seenChange|=changed;
            CHECK_EQ(motion.Observe(now,changed),seenChange);
        }
    }
}

TEST(water_path_motion_recovers_only_after_both_quiet_time_and_frames)
{
    MotionRecovery motion;
    CHECK(!motion.Observe(0,false)); // Initial still capture uses requested size.
    CHECK(motion.Observe(1000,true));
    for(unsigned frame=1;frame<=MotionRecovery::QuietFrames;++frame)
        CHECK(motion.Observe(1000+frame,false));
    CHECK(motion.Observe(1000+MotionRecovery::QuietMs-1,false));
    CHECK(!motion.Observe(1000+MotionRecovery::QuietMs,false));
    CHECK(!motion.Observe(2000,false));

    CHECK(motion.Observe(3000,true));
    for(unsigned frame=1;frame<MotionRecovery::QuietFrames;++frame)
        CHECK(motion.Observe(3000+frame*MotionRecovery::QuietMs,false));
    CHECK(!motion.Observe(3000+MotionRecovery::QuietFrames*MotionRecovery::QuietMs,false));

    CHECK(motion.Observe(5000,true));
    CHECK(motion.Observe(4900,false)); // Rollback restarts the quiet interval.
    for(unsigned frame=1;frame<=MotionRecovery::QuietFrames;++frame)
        CHECK(motion.Observe(4900+frame,false));
    CHECK(motion.Observe(4900+MotionRecovery::QuietMs-1,false));
    CHECK(!motion.Observe(4900+MotionRecovery::QuietMs,false));
}

TEST(water_path_capture_scope_keeps_reflected_hits_and_nearby_shadow_contributors)
{
    View view;
    view.origin={0,0,-10};
    view.rayTopLeft={-.5f,.5f,1};
    view.rayDx={1,0,0};
    view.rayDy={0,-1,0};
    view.sunDirection={1,0,.1f};
    const CaptureScope scope(view);
    // These bounds are in the reflected frustum; main-camera visibility is not
    // an input, so off-screen structures can still appear in the water.
    CHECK(scope.Matches({-1,-1,20},{1,1,22}));
    CHECK(scope.Matches({-500,-1,20},{500,1,22})); // Test box extent, not only its centre.
    // A sun ray from (0,0,20) reaches (500,0,70) within the 600-unit budget.
    // That blocker is outside both the primary frustum and the indirect margin.
    CHECK(scope.Matches({499,-1,69},{501,1,71}));
    view.shadowDistance=0;
    CHECK(!CaptureScope(view).Matches({499,-1,69},{501,1,71}));
    // The same allowance is not needed on the opposite side from the sun.
    CHECK(!scope.Matches({-501,-1,69},{-499,1,71}));
    CHECK(scope.Matches({99,-1,19},{101,1,21})); // Nearby indirect contributor.
}

TEST(water_path_capture_scope_rejects_distant_and_fully_submerged_bounds)
{
    View view;
    view.origin={0,0,-10};
    view.rayTopLeft={-.5f,.5f,1};
    view.rayDx={1,0,0};
    view.rayDy={0,-1,0};
    const CaptureScope scope(view);
    CHECK(!scope.Matches({-1,-1,4999},{1,1,5001})); // In the cone, beyond the ray budget.
    CHECK(!scope.Matches({-1,999,20},{1,1001,22})); // Outside the expanded side planes.
    CHECK(!scope.Matches({-1,-1,-2},{1,1,-.051f}));
    CHECK(scope.Matches({-1,-1,-2},{1,1,.01f})); // A shoreline-crossing box must survive.
    CHECK(scope.Matches({-1,-1,-.049f},{1,1,-.049f})); // Water-plane tolerance.
}

TEST(water_path_capture_scope_handles_horizon_and_invalid_inputs_conservatively)
{
    View view;
    view.origin={0,0,-10};
    view.rayTopLeft={-.5f,.5f,-.1f};
    view.rayDx={1,0,0};
    view.rayDy={0,-1,1.2f};
    // Top rays point below the horizon; interior rays can cross water far beyond
    // the corner intersections. An incorrect finite footprint drops this box.
    CHECK(CaptureScope(view).Matches({-1,4199,0},{1,4201,2}));
    view.rayDx={0,0,0}; // Degenerate ray quad cannot establish a safe culling volume.
    CHECK(CaptureScope(view).Matches({4999,4999,4999},{5001,5001,5001}));
    view.origin.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(CaptureScope(view).Matches({4999,4999,4999},{5001,5001,5001}));
    view.origin={0,0,-10};
    view.rayTopLeft={-.5f,.5f,1};
    view.rayDx={1,0,0};
    view.rayDy={0,-1,0};
    const float unknown=std::numeric_limits<float>::quiet_NaN();
    CHECK(CaptureScope(view).Matches({unknown,0,0},{1,1,1}));
}

TEST(water_path_scene_hits_offscreen_geometry_and_nearest_surface)
{
    Scene scene;
    CHECK(scene.Build({Face(100,8),Face(100,4)}));
    const Hit hit=scene.Trace({100,0,0},{0,0,1});
    CHECK(hit.found);
    CHECK_NEAR(hit.distance,4,1e-5);
    CHECK_NEAR(hit.albedo.x,0.7,1e-5);
    CHECK_NEAR(hit.normal.z,-1,1e-5);
    CHECK(!scene.Trace({100,2,0},{0,0,1}).found);
    CHECK(!scene.Trace({100,0,0},{0,0,1},3).found);
    CHECK(scene.Trace({100,0,12},{0,0,-1}).found); // Two-sided.
}

TEST(water_path_scene_threaded_bvh_matches_brute_force)
{
    std::vector<Triangle> geometry;
    for(unsigned i=0;i<128;++i)
        geometry.push_back(Face(static_cast<float>(i)-64,1+static_cast<float>((i*17)%13)));
    Scene scene;
    CHECK(scene.Build(geometry));
    CHECK(scene.NodeCount()>1);
    CHECK_EQ(static_cast<unsigned>(scene.nodeTexels[0].w),scene.NodeCount());
    std::vector<unsigned> visits(scene.triangles.size(),0);
    for(unsigned node=0;node<scene.NodeCount();++node) {
        const Float4 lo=scene.nodeTexels[node*2],hi=scene.nodeTexels[node*2+1];
        CHECK(lo.w>node&&lo.w<=scene.NodeCount());
        const unsigned tag=static_cast<unsigned>(hi.w);
        if(tag) {
            CHECK(LeafCount(tag)>=1&&LeafCount(tag)<=4);
            for(unsigned i=LeafFirst(tag);i<LeafFirst(tag)+LeafCount(tag);++i) {
                CHECK(i<visits.size());
                if(i<visits.size())++visits[i];
            }
        }
    }
    for(unsigned count:visits)CHECK_EQ(count,1u);
    for(int x=-66;x<=66;++x) {
        const Vec3 origin={static_cast<float>(x),0,0},direction={0,0,1};
        float distance=1000;bool found=false;
        for(const Triangle &triangle:geometry)
            found=IntersectTriangle(triangle,origin,direction,1e-4f,distance)||found;
        const Hit hit=scene.Trace(origin,direction,1000);
        CHECK_EQ(hit.found,found);
        if(found)CHECK_NEAR(hit.distance,distance,1e-5);
    }
}

TEST(water_path_scene_texture_layout_and_fingerprint_are_stable)
{
    std::vector<Triangle> input={Face(0,1),Face(2,3),Face(0,5),Face(-8,2),Face(5,4)};
    Scene a,b;
    CHECK(a.Build(input)&&b.Build(input));
    CHECK_EQ(a.fingerprint,Fingerprint(input));
    CHECK_EQ(a.fingerprint,b.fingerprint);
    CHECK_EQ(a.nodeTexels.size(),b.nodeTexels.size());
    CHECK_EQ(a.triangleTexels.size(),a.triangles.size()*4u);
    CHECK_MEM(a.nodeTexels.data(),b.nodeTexels.data(),a.nodeTexels.size()*sizeof(Float4));
    for(unsigned i=0;i<a.triangles.size();++i) {
        const Triangle &t=a.triangles[i];
        CHECK_NEAR(a.triangleTexels[i*4].x,t.a.x,1e-5);
        CHECK_NEAR(a.triangleTexels[i*4+1].x,t.b.x-t.a.x,1e-5);
        CHECK_NEAR(a.triangleTexels[i*4+2].y,t.c.y-t.a.y,1e-5);
        CHECK_NEAR(a.triangleTexels[i*4+3].z,t.albedo.z,1e-5);
    }
    input[0].albedo.x+=0.01f;
    CHECK_NE(Fingerprint(input),a.fingerprint);
    input[0].a.x+=1;
    CHECK_NE(Fingerprint(input),a.fingerprint);
    CHECK_EQ(LeafFirst((MaximumTriangles-1u)*4u+4u),MaximumTriangles-1u);
    CHECK_EQ(LeafCount((MaximumTriangles-1u)*4u+4u),4u);
    CHECK_EQ(static_cast<unsigned>(static_cast<float>(MaximumTriangles*4u)),MaximumTriangles*4u);
}

TEST(water_path_scene_rejects_invalid_geometry_and_preserves_previous)
{
    Scene scene;
    CHECK(scene.Build({Face(0,1)}));
    const uint64_t previous=scene.fingerprint;
    auto invalid=Face(0,1);
    invalid.a.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!scene.Build({invalid}));
    CHECK_EQ(scene.fingerprint,previous);
    CHECK(scene.Trace({0,0,0},{0,0,1}).found);
    CHECK(!scene.Trace({0,0,0},{0,0,0}).found);
    CHECK(!scene.Trace({0,0,0},{0,0,std::numeric_limits<float>::infinity()}).found);
    auto degenerate=Face(0,1);degenerate.c=degenerate.a;
    CHECK(scene.Build({degenerate}));
    CHECK(scene.Empty());
    CHECK_EQ(scene.NodeCount(),0u);
    CHECK(scene.Build({Face(0,1,{-1,2,0.5f})}));
    CHECK_NEAR(scene.triangles[0].albedo.x,0,1e-5);
    CHECK_NEAR(scene.triangles[0].albedo.y,1,1e-5);
}

TEST(water_path_bounds_parallel_and_boundary_rays_are_finite)
{
    CHECK(IntersectBounds({0,0,0},{1,1,1},{0,0,-1},{0,0,1},10));
    CHECK(!IntersectBounds({0,0,0},{1,1,1},{-1,0,-1},{0,0,1},10));
    CHECK(!IntersectBounds({0,0,0},{1,1,1},{0,0,2},{0,0,1},10));
    CHECK(!IntersectBounds({0,0,0},{1,1,1},{0,0,-2},{0,0,1},1));
}

TEST(water_path_median_bvh_fits_gpu_near_first_stack)
{
    // Each median split leaves at most ceil(count/2) triangles in either child.
    // Following that worst child bounds deferred stack entries for every valid scene.
    unsigned remaining=MaximumTriangles,maximumDepth=0;
    while(remaining>4) { remaining=(remaining+1)/2;++maximumDepth; }
    CHECK_EQ(maximumDepth,18u);
    CHECK(maximumDepth<20);

    // Check the actual packed tree too, including uneven child sizes.
    for(unsigned count:{5u,7u,17u,129u,1025u}) {
        std::vector<Triangle> input;
        for(unsigned i=0;i<count;++i)input.push_back(Face(float(i),float(i%7)));
        Scene scene;
        CHECK(scene.Build(input));
        std::vector<unsigned> ends;
        unsigned deepest=0;
        for(unsigned node=0;node<scene.NodeCount();++node) {
            while(!ends.empty()&&ends.back()==node)ends.pop_back();
            deepest=std::max(deepest,static_cast<unsigned>(ends.size()));
            const auto low=scene.nodeTexels[node*2],high=scene.nodeTexels[node*2+1];
            if(high.w==0) {
                const unsigned left=node+1,right=static_cast<unsigned>(scene.nodeTexels[left*2].w);
                CHECK(left<right&&right<static_cast<unsigned>(low.w));
                ends.push_back(static_cast<unsigned>(low.w));
            }
        }
        unsigned expected=0;
        while(count>4) { count=(count+1)/2;++expected; }
        CHECK_EQ(deepest,expected);
    }
}

TEST(water_path_refit_moves_reorders_and_recolours_without_changing_topology)
{
    std::vector<Triangle> input;
    for(unsigned i=0;i<41;++i)input.push_back(Face(float(i)*2,1+float(i%7)));
    Scene scene;
    CHECK(scene.Build(input));
    const auto oldNodes=scene.nodeTexels;
    const Triangle *triangleStorage=scene.triangles.data();
    const Float4 *nodeStorage=scene.nodeTexels.data(),*texelStorage=scene.triangleTexels.data();
    for(unsigned i=0;i<input.size();++i) {
        input[i]=Face(float((i*7)%41)*2-100,10+float(i%5),
                      {float(i)/41,0.25f,1-float(i)/41});
    }
    std::reverse(input.begin(),input.end());
    CHECK(scene.Refit(input));
    CHECK_EQ(scene.fingerprint,Fingerprint(input));
    CHECK_EQ(scene.triangles.data(),triangleStorage);
    CHECK_EQ(scene.nodeTexels.data(),nodeStorage);
    CHECK_EQ(scene.triangleTexels.data(),texelStorage);
    CHECK_EQ(scene.nodeTexels.size(),oldNodes.size());
    for(unsigned i=0;i<oldNodes.size();++i)CHECK_EQ(scene.nodeTexels[i].w,oldNodes[i].w);
    Scene rebuilt;
    CHECK(rebuilt.Build(input));
    for(int x=-103;x<0;++x) {
        const auto hit=scene.Trace({float(x),0,0},{0,0,1});
        const auto expected=rebuilt.Trace({float(x),0,0},{0,0,1});
        CHECK_EQ(hit.found,expected.found);
        if(hit.found&&expected.found) {
            CHECK_NEAR(hit.distance,expected.distance,1e-5);
            CHECK_NEAR(hit.albedo.x,expected.albedo.x,1e-5);
            CHECK_NEAR(hit.albedo.z,expected.albedo.z,1e-5);
        }
    }
    for(unsigned i=0;i<scene.triangles.size();++i) {
        const Triangle &t=scene.triangles[i];
        CHECK_NEAR(scene.triangleTexels[i*4].x,t.a.x,1e-5);
        CHECK_NEAR(scene.triangleTexels[i*4].z,t.a.z,1e-5);
        CHECK_NEAR(scene.triangleTexels[i*4+1].x,t.b.x-t.a.x,1e-5);
        CHECK_NEAR(scene.triangleTexels[i*4+2].y,t.c.y-t.a.y,1e-5);
        CHECK_NEAR(scene.triangleTexels[i*4+3].x,t.albedo.x,1e-5);
    }
    // A second refit still follows original input slots rather than the last packed order.
    std::rotate(input.begin(),input.begin()+13,input.end());
    for(auto &t:input) { t.a.z+=50;t.b.z+=50;t.c.z+=50; }
    CHECK(scene.Refit(input));
    CHECK_NEAR(scene.nodeTexels[0].z,60,1e-5);
    CHECK_NEAR(scene.nodeTexels[1].z,64,1e-5);
}

TEST(water_path_refit_failures_leave_previous_scene_and_mapping_intact)
{
    std::vector<Triangle> input;
    for(unsigned i=0;i<11;++i)input.push_back(Face(float(i)*2,1+float(i)));
    Scene scene;
    CHECK(scene.Build(input));
    const auto oldTriangles=scene.triangles;
    const auto oldNodes=scene.nodeTexels,oldTexels=scene.triangleTexels;
    const uint64_t oldFingerprint=scene.fingerprint;
    const auto unchanged=[&]() {
        CHECK_EQ(scene.fingerprint,oldFingerprint);
        CHECK_MEM(scene.triangles.data(),oldTriangles.data(),oldTriangles.size()*sizeof(Triangle));
        CHECK_MEM(scene.nodeTexels.data(),oldNodes.data(),oldNodes.size()*sizeof(Float4));
        CHECK_MEM(scene.triangleTexels.data(),oldTexels.data(),oldTexels.size()*sizeof(Float4));
    };
    auto bad=input;
    bad.pop_back();
    CHECK(!scene.Refit(bad));unchanged();
    bad=input;
    bad[0]=Face(-100,5); // Earlier valid changes must not be committed before a later invalid one.
    bad.back().albedo.z=std::numeric_limits<float>::infinity();
    CHECK(!scene.Refit(bad));unchanged();
    bad=input;bad.back().a.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!scene.Refit(bad));unchanged();
    bad=input;bad.back().c=bad.back().a;
    CHECK(!scene.Refit(bad));unchanged();
    CHECK(scene.Refit(input)); // The failed calls did not corrupt original-slot mapping.
    CHECK_EQ(scene.fingerprint,oldFingerprint);
}

TEST(water_path_refit_retains_existing_degenerate_slots_and_handles_empty_scene)
{
    auto line=Face(0,1);line.c=line.a;
    std::vector<Triangle> input={Face(10,2),line,Face(20,3),Face(30,4)};
    Scene scene;
    CHECK(scene.Build(input));
    CHECK_EQ(scene.triangles.size(),3u);
    input[1].a.x+=5;input[1].c=input[1].a;
    input[2]=Face(200,9,{-1,2,.4f});
    CHECK(scene.Refit(input));
    const auto moved=scene.Trace({200,0,0},{0,0,1});
    CHECK(moved.found);
    CHECK_NEAR(moved.distance,9,1e-5);
    CHECK_NEAR(moved.albedo.x,0,1e-5);
    CHECK_NEAR(moved.albedo.y,1,1e-5);
    CHECK(!scene.Trace({20,0,0},{0,0,1}).found);
    const auto fingerprint=scene.fingerprint;
    input[1]=Face(50,6); // A formerly dropped input now needs a leaf: rebuild is required.
    CHECK(!scene.Refit(input));
    CHECK_EQ(scene.fingerprint,fingerprint);
    CHECK(scene.Build(input));
    CHECK_EQ(scene.triangles.size(),4u);
    CHECK(scene.Trace({50,0,0},{0,0,1}).found);

    CHECK(scene.Build({}));
    CHECK(scene.Refit({}));
    CHECK(scene.Empty()&&scene.NodeCount()==0);
    CHECK(!scene.Refit({Face(0,1)}));
    CHECK(scene.Build({line,line}));
    line.a.z+=3;line.c=line.a;
    CHECK(scene.Refit({line,line}));
    CHECK(scene.Empty()&&scene.NodeCount()==0);
}

TEST(water_path_trusted_revision_can_skip_hashing_without_skipping_geometry_updates)
{
    std::vector<Triangle> input;
    for(unsigned i=0;i<16;++i) input.push_back(Face(float(i)*2,2+float(i)));
    Scene scene;
    CHECK(scene.Build(input,false));
    CHECK_EQ(scene.fingerprint,uint64_t(0));
    const Hit initial=scene.Trace({0,0,0},{0,0,1});
    CHECK(initial.found);
    CHECK_NEAR(initial.distance,2,1e-5);
    CHECK(scene.NodeCount()>1);

    // Move and recolour a primitive beyond the original bounds, then reorder
    // source slots. Trusted revisions still require a correct BVH and GPU data.
    input[0]=Face(1000,7,{.1f,.2f,.8f});
    std::reverse(input.begin(),input.end());
    CHECK(scene.Refit(input,false));
    CHECK_EQ(scene.fingerprint,uint64_t(0));
    CHECK(!scene.Trace({0,0,0},{0,0,1}).found);
    const Hit moved=scene.Trace({1000,0,0},{0,0,1});
    CHECK(moved.found);
    CHECK_NEAR(moved.distance,7,1e-5);
    CHECK_NEAR(moved.albedo.z,.8,1e-5);
    if(moved.found) {
        CHECK_NEAR(scene.triangleTexels[moved.triangle*4].z,7,1e-5);
        CHECK_NEAR(scene.triangleTexels[moved.triangle*4+3].z,.8,1e-5);
    }

    auto invalid=input;
    invalid[0].a.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!scene.Refit(invalid,false));
    CHECK(!scene.Build(invalid,false));
    CHECK_EQ(scene.fingerprint,uint64_t(0));
    CHECK(scene.Trace({1000,0,0},{0,0,1}).found);

    CHECK(scene.Build(input)); // Default callers still receive a content hash.
    CHECK_EQ(scene.fingerprint,Fingerprint(input));
    CHECK(scene.Refit(input,false));
    CHECK_EQ(scene.fingerprint,uint64_t(0)); // Do not leave a stale earlier hash.
    CHECK(scene.Build({},false));
    CHECK(scene.Refit({},false));
    CHECK_EQ(scene.fingerprint,uint64_t(0));
    CHECK(scene.Empty()&&scene.NodeCount()==0);
}

TEST(water_path_lighting_decodes_display_colour_before_transport)
{
    const Vec3 grey=DisplayToLinear({.5f,.5f,.5f});
    CHECK_NEAR(grey.x,.21763764,1e-6);
    CHECK_NEAR(grey.y,.21763764,1e-6);
    CHECK_NEAR(grey.z,.21763764,1e-6);

    // Authored tint ratios must change with the decode, not just brightness.
    const Vec3 tint=DisplayToLinear({.8f,.4f,.2f});
    CHECK_NEAR(tint.x,.61206560,1e-6);
    CHECK_NEAR(tint.y,.13320851,1e-6);
    CHECK_NEAR(tint.z,.02899119,1e-6);
    CHECK_NEAR(tint.x/tint.y,4.59479342,1e-5);
    CHECK_NEAR(tint.y/tint.z,4.59479342,1e-5);

    // Match waterLinear's pow(max(rgb,0),2.2), including values above display white.
    const Vec3 endpoints=DisplayToLinear({0,1,2});
    CHECK_EQ(endpoints.x,0);
    CHECK_EQ(endpoints.y,1);
    CHECK_NEAR(endpoints.z,4.59479342,1e-5);
    const Vec3 negative=DisplayToLinear({-1,-.5f,-.01f});
    CHECK_EQ(negative.x,0);
    CHECK_EQ(negative.y,0);
    CHECK_EQ(negative.z,0);
}

TEST(water_path_authored_diffuse_matches_lambertian_transport_units)
{
    const Vec3 irradiance=DisplayDiffuseToIrradiance({.88f,.81f,.66f});
    constexpr float pi=3.14159265358979323846f;
    // Albedo/pi restores the authored white-surface response without changing
    // the independent sky-radiance conversion or clamping HDR light values.
    CHECK_NEAR(std::pow(irradiance.x/pi,1.0f/2.2f),.88f,1e-6);
    CHECK_NEAR(std::pow(irradiance.y/pi,1.0f/2.2f),.81f,1e-6);
    CHECK_NEAR(std::pow(irradiance.z/pi,1.0f/2.2f),.66f,1e-6);
    const Vec3 endpoints=DisplayDiffuseToIrradiance({-1,1,2});
    CHECK_EQ(endpoints.x,0);
    CHECK_NEAR(endpoints.y,pi,1e-6);
    CHECK_NEAR(endpoints.z/pi,4.59479342,1e-5);
}
