#include "test_harness.h"
#include "Platform/WaterPathTracingScene.h"
#include "Platform/WaterPathTracingMaterials.h"
#include "Platform/WaterPathTerrainSnapshot.h"
#include "Platform/WaterPathStaticSpans.h"
#include "Platform/WaterPathSnapshotPolicy.h"
#include "Platform/WaterPathTerrainGroups.h"
#include "WaterPathBuildReference.h"
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

TEST(water_path_motion_does_not_restore_resolution_between_camera_snapshots)
{
    // Camera snapshots change every 100 ms. Fast render loops must not mistake
    // eight calls with a held camera for a settled projection and resize twice.
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

TEST(water_path_reflection_detail_selects_requested_and_moving_capture_sizes)
{
    View view;
    CHECK_EQ(view.size,256u);
    CHECK_EQ(view.motionSize,128u);
    for(int detail:{1,0,1,-1,2,0}) {
        ApplyReflectionDetail(view,detail);
        CHECK_EQ(view.size,detail==0?256u:512u);
        CHECK_EQ(view.motionSize,detail==0?128u:512u);
    }
}

TEST(water_path_capture_projection_ignores_history_only_inputs)
{
    Uniforms base{};
    base.originWater={0,0,-5,0};base.rayTopLeft={-.5f,.5f,1,0};
    base.rayDx={1,0,0,0};base.rayDy={0,-1,0,0};base.output={256,256,.02f,128};
    CHECK(SameCaptureProjection(base,base));
    for(auto field:{&Uniforms::originWater,&Uniforms::rayTopLeft,&Uniforms::rayDx,&Uniforms::rayDy}) {
        for(unsigned component=0;component<4;++component) {
            Uniforms changed=base;
            Float4 &value=changed.*field;
            if(component==0) value.x+=1;
            if(component==1) value.y+=1;
            if(component==2) value.z+=1;
            if(component==3) value.w+=1;
            CHECK(!SameCaptureProjection(base,changed));
        }
    }
    Uniforms changed=base;changed.output.x=512;CHECK(!SameCaptureProjection(base,changed));
    changed=base;changed.output.y=512;CHECK(!SameCaptureProjection(base,changed));
    changed=base;changed.output.w=512;CHECK(!SameCaptureProjection(base,changed));
    for(auto field:{&Uniforms::sunDirection,&Uniforms::sunRadiance,&Uniforms::skyLow,
        &Uniforms::skyHigh,&Uniforms::counts,&Uniforms::transport}) {
        changed=base;changed.*field={1,2,3,4};
        CHECK(std::memcmp(&base,&changed,sizeof(base))!=0); // History must still notice this change.
        CHECK(SameCaptureProjection(base,changed));
    }
    changed=base;changed.output.z=.04f;
    CHECK(SameCaptureProjection(base,changed));
}

TEST(water_path_stationary_geometry_revisions_do_not_reduce_capture_density)
{
    MotionRecovery motion;
    Uniforms previous{};previous.output={256,256,.02f,0};
    for(unsigned revision=0;revision<100;++revision) {
        Uniforms current=previous;
        current.counts={float(revision+1),float(revision+2),0,1000};
        current.sunRadiance={float(revision),.2f,.3f,0};
        CHECK(!motion.Observe(revision*100,!SameCaptureProjection(current,previous)));
        previous=current;
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

    const CaptureScope primary(view,CaptureScope::Content::Primary);
    CHECK(primary.Matches({-1,-1,20},{1,1,22}));
    CHECK(primary.Matches({-500,-1,20},{500,1,22}));
    CHECK(!primary.Matches({99,-1,19},{101,1,21}));
    CHECK(!primary.Matches({499,-1,69},{501,1,71}));
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

namespace {
std::vector<Triangle> BuildParityGeometry(unsigned count, unsigned pattern)
{
    std::vector<Triangle> out;
    out.reserve(count);
    uint32_t random = 0x12345678;
    auto next = [&]() {
        random = random * 1664525u + 1013904223u;
        return float((random >> 8) & 65535) / 16 - 2048;
    };
    for (unsigned i = 0; i < count; ++i) {
        float x = next(), y = next(), z = next();
        if (pattern == 1) {
            x = float(i % 257);
            y = float((i / 257) % 251);
            z = float(i % 7);
        }
        if (pattern == 2) {
            x = float(i % 19) * 512;
            y = float(i % 31) * 256;
            z = float(i % 3);
        }
        Triangle t = {{x, y, z}, {x + 1 + (i % 5), y + .25f, z + .5f}, {x + .125f, y + 2, z - .25f},
            {float(i % 17) / 16, float(i % 7) / 6, float(i % 11) / 10}};
        if (pattern == 3) {
            // Equal centroids force the original 12-float tie comparator;
            // identical triangles also retain comparator-equivalent entries.
            const float n = 1 + float(i % 31);
            t.a = {-n, 0, 0};
            t.b = {n, 1, 0};
            t.c = {0, -1, 0};
            if (i % 7 == 0 && !out.empty())
                t = out.back();
        }
        if (pattern == 4) {
            // First-argument min/max semantics must preserve signed zero;
            // degenerate input slots must keep their original exclusion map.
            t.a.x = (i & 1) ? -0.f : 0.f;
            t.b.x = t.c.x = t.a.x;
            if (i % 5 == 0)
                t.b = t.c = t.a;
        }
        if (pattern == 5) {
            // Finite, nondegenerate yz faces with extreme absolute X test the
            // original scaled-centroid arithmetic without averaging rewrites.
            t.a = {std::numeric_limits<float>::max(), -1, 0};
            t.b = {std::numeric_limits<float>::max(), 1, 0};
            t.c = {std::numeric_limits<float>::max(), 0, 1 + float(i % 4)};
        }
        out.push_back(t);
    }
    return out;
}
void CheckBuildParity(const Scene &actual, const WaterPathBuildReference::Scene &original)
{
    CHECK_EQ(actual.fingerprint, original.fingerprint);
    CHECK_EQ(actual.nodeTexels.size(), original.nodeTexels.size());
    CHECK_EQ(actual.triangles.size(), original.triangles.size());
    CHECK_EQ(actual.triangleTexels.size(), original.triangleTexels.size());
    if (actual.nodeTexels.size() != original.nodeTexels.size()
        || actual.triangles.size() != original.triangles.size()
        || actual.triangleTexels.size() != original.triangleTexels.size())
        return;
    if (!actual.nodeTexels.empty())
        CHECK_MEM(actual.nodeTexels.data(), original.nodeTexels.data(), actual.nodeTexels.size() * sizeof(Float4));
    if (!actual.triangles.empty())
        CHECK_MEM(actual.triangles.data(), original.triangles.data(), actual.triangles.size() * sizeof(Triangle));
    if (!actual.triangleTexels.empty())
        CHECK_MEM(actual.triangleTexels.data(), original.triangleTexels.data(),
            actual.triangleTexels.size() * sizeof(Float4));
    for (unsigned i = 0; i < actual.triangles.size(); ++i)
        CHECK_EQ(actual.SourceTriangleIndex(i), original.SourceTriangleIndex(i));
}
}

TEST(water_path_build_precomputed_primitives_match_frozen_original_bytes)
{
    // Compare the actual production builder against the frozen recursive
    // evaluator, including every source-slot mapping and packed escape/leaf tag.
    // All six patterns cross empty/leaf/split boundaries and larger tree depths.
    for (unsigned count :{0u, 1u, 4u, 5u, 7u, 31u, 129u, 4097u, 60000u})
        for (unsigned pattern = 0; pattern < 6; ++pattern) {
            const auto input = BuildParityGeometry(count, pattern);
            Scene actual;
            WaterPathBuildReference::Scene original;
            CHECK(actual.Build(input));
            CHECK(original.Build(input));
            CheckBuildParity(actual, original);
            // Revision-backed callers bypass the content hash, retaining the same
            // packed scene even when a previous hashed build occupied the object.
            CHECK(actual.Build(input, false));
            CHECK(original.Build(input, false));
            CheckBuildParity(actual, original);
        }
}

TEST(water_path_build_precomputed_primitives_preserve_rejection_transaction)
{
    const auto input = BuildParityGeometry(31, 3);
    Scene actual;
    WaterPathBuildReference::Scene original;
    CHECK(actual.Build(input));
    CHECK(original.Build(input));
    const auto savedNodes = actual.nodeTexels, savedTriangles = actual.triangleTexels;
    for (float invalid :{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto bad = input;
        bad[17].a.x = invalid;
        CHECK(!actual.Build(bad));
        CHECK(!original.Build(bad));
        CheckBuildParity(actual, original);
        CHECK_MEM(actual.nodeTexels.data(), savedNodes.data(), savedNodes.size() * sizeof(Float4));
        CHECK_MEM(actual.triangleTexels.data(), savedTriangles.data(), savedTriangles.size() * sizeof(Float4));
    }
}


TEST(water_hardware_linear_pack_preserves_source_slots_without_bvh_nodes)
{
    std::vector<Triangle> input;
    for(unsigned i=0;i<17;++i) input.push_back(Face(float(17-i),float(i%5),{1.2f,-.3f,.4f}));
    input.insert(input.begin()+2,Triangle{{0,0,0},{0,0,0},{0,0,0},{1,1,1}});
    Scene packed;
    CHECK(packed.PackLinear(input));
    CHECK(packed.IsLinearPacked());
    CHECK_EQ(packed.NodeCount(),0u);
    CHECK_EQ(packed.triangles.size(),17u);
    CHECK_EQ(packed.fingerprint,Fingerprint(input));
    unsigned output=0;
    for(unsigned source=0;source<input.size();++source) {
        if(source==2)continue;
        CHECK_EQ(packed.SourceTriangleIndex(output),source);
        Scene single;
        CHECK(single.Build({input[source]}));
        CHECK_MEM(&packed.triangles[output],single.triangles.data(),sizeof(Triangle));
        CHECK_MEM(packed.triangleTexels.data()+output*TriangleTexels,single.triangleTexels.data(),TriangleTexels*sizeof(Float4));
        ++output;
    }
    const Triangle *triangleStorage=packed.triangles.data();
    const Float4 *texelStorage=packed.triangleTexels.data();
    input[5].a.z+=.25f;
    CHECK(packed.PackLinear(input,false));
    CHECK_EQ(packed.fingerprint,0u);
    CHECK(packed.triangles.data()==triangleStorage && packed.triangleTexels.data()==texelStorage);
    CHECK(!packed.Refit(input)); // A software caller must build real BVH nodes on mode reentry.
    CHECK(packed.Build(input));
    CHECK(!packed.IsLinearPacked() && packed.NodeCount()>0);
    CHECK(packed.PackLinear(input));
    CHECK(packed.IsLinearPacked() && packed.NodeCount()==0);
    for(unsigned i=0;i<17;++i)CHECK_EQ(packed.SourceTriangleIndex(i),i<2?i:i+1);
    // Public scene storage can itself be the next input, including a gap to compact.
    packed.triangles=input;
    CHECK(packed.PackLinear(packed.triangles));
    CHECK_EQ(packed.triangles.size(),17u);
    for(unsigned i=0;i<17;++i) {
        const unsigned source=i<2?i:i+1;
        CHECK_EQ(packed.SourceTriangleIndex(i),source);
        CHECK_EQ(packed.triangles[i].a.x,input[source].a.x);
        CHECK_EQ(packed.triangles[i].a.z,input[source].a.z);
    }
}

TEST(water_hardware_linear_pack_rejects_invalid_input_without_replacing_scene)
{
    std::vector<Triangle> input={Face(0,2),Face(1,3)};
    Scene scene;
    for(bool linear:{false,true}) {
        CHECK(linear ? scene.PackLinear(input) : scene.Build(input));
        const auto savedTriangles=scene.triangleTexels,savedNodes=scene.nodeTexels;
        const uint64_t savedFingerprint=scene.fingerprint;
        for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            auto bad=input;bad[1].b.y=invalid;
            CHECK(!scene.PackLinear(bad));
            CHECK_EQ(scene.IsLinearPacked(),linear);
            CHECK_EQ(scene.fingerprint,savedFingerprint);
            CHECK_MEM(scene.triangleTexels.data(),savedTriangles.data(),savedTriangles.size()*sizeof(Float4));
            CHECK_EQ(scene.nodeTexels.size(),savedNodes.size());
            if(!savedNodes.empty())CHECK_MEM(scene.nodeTexels.data(),savedNodes.data(),savedNodes.size()*sizeof(Float4));
        }
        auto overflow=input;
        overflow[1].a.x=std::numeric_limits<float>::max();
        overflow[1].b.x=-std::numeric_limits<float>::max();
        CHECK(!scene.PackLinear(overflow));
        CHECK_EQ(scene.fingerprint,savedFingerprint);
    }
    const std::vector<Triangle> degenerate={{{0,0,1},{0,0,1},{0,0,1},{.2f,.3f,.4f}}};
    CHECK(scene.PackLinear(degenerate));
    CHECK(scene.Empty() && scene.IsLinearPacked() && scene.NodeCount()==0);
    CHECK(scene.PackLinear({}));
    CHECK(scene.Empty() && scene.triangleTexels.empty());
}

TEST(water_hardware_material_validation_reuses_only_exact_validated_ranges)
{
    PrimaryMaterialPacket packet;
    packet.texels={{.2f,.4f,.6f,1},{.8f,.3f,.1f,1}};
    packet.triangles.resize(6);
    for(auto &m:packet.triangles){m.uvCSize={0,0,1,1};m.diffuseOffset={1,1,1,2};}
    PrimaryMaterialValidation cached;
    const auto check=[&](size_t prefix) {
        const bool expected=ValidPrimaryMaterials(packet,packet.triangles.size());
        CHECK_EQ(cached.Validate(packet,packet.triangles.size(),prefix),expected);
    };
    check(4);
    CHECK_EQ(cached.CheckedMaterials,6u);
    check(4);
    CHECK(cached.StaticReused);
    CHECK_EQ(cached.CheckedMaterials,8u);
    CHECK_EQ(cached.ReusedMaterials,4u);
    CHECK_EQ(cached.CheckedTexels,2u);
    for(unsigned n:{9u,5u,6u}) {
        packet.triangles.resize(n,packet.triangles.back());
        packet.triangles.back().uvAB.x+=.25f;
        check(4);
    }
    packet.texels[0].x=std::numeric_limits<float>::quiet_NaN();check(4);
    packet.texels[0].x=.2f;check(4);
    packet.triangles[0].diffuseOffset.w=3;check(4); // changed static binding exceeds atlas
    packet.triangles[0].diffuseOffset.w=2;check(4);
    packet.texels.resize(1);check(4); // byte-identical static bindings must be rechecked on shrink
    packet.texels.push_back({.8f,.3f,.1f,1});check(4);
    packet.triangles[2].uvCSize.z=1.5f;check(4);
    packet.triangles[2].uvCSize.z=1;check(4);
    packet.triangles[3].uvAB.y=std::numeric_limits<float>::infinity();check(4);
    packet.triangles[3].uvAB.y=0;check(4);
    check(2);check(0);check(4);
    CHECK(!cached.Validate(packet,packet.triangles.size(),packet.triangles.size()+1));
}

TEST(water_hardware_retained_prefix_proof_includes_albedo_and_degenerate_source_mapping)
{
    std::vector<Triangle> input={Triangle{},Face(0,2),Face(1,2),Face(2,2)};
    Scene scene,fresh;
    CHECK(scene.PackLinear(input,false,3));
    CHECK(!scene.LinearStaticPrefixReused());
    CHECK_EQ(scene.LinearStaticTriangleCount(),2u);
    input.back().a.x+=.1f;
    CHECK(scene.PackLinear(input,false,3));
    CHECK(scene.LinearStaticPrefixReused());
    input[1].albedo.x=.2f;
    CHECK(scene.PackLinear(input,false,3));
    CHECK(!scene.LinearStaticPrefixReused());
    std::swap(input[0],input[1]);
    CHECK(scene.PackLinear(input,false,3));
    CHECK(!scene.LinearStaticPrefixReused());
    CHECK(fresh.PackLinear(input,false));
    CHECK_MEM(scene.triangleTexels.data(),fresh.triangleTexels.data(),fresh.triangleTexels.size()*sizeof(Float4));
    for(unsigned i=0;i<fresh.triangles.size();++i)CHECK_EQ(scene.SourceTriangleIndex(i),fresh.SourceTriangleIndex(i));
}

TEST(water_hardware_terrain_snapshot_requires_exact_current_source_bytes)
{
    TerrainSnapshotKey key;key.Map=&key;key.Width=7;key.Height=5;key.Border=1;key.FlipRowBytes=1;
    key.HasView=true;key.WaterLevel=2;key.HasShroud=true;key.CellColumns=3;key.CellRows=2;
    key.CellWidth=20;key.CellHeight=30;
    std::vector<unsigned char> heights(35,7),flips(5,0),cells={0,80,255,30,120,200};
    TerrainSnapshotProof proof;
    CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));
    proof.Commit(key,heights.data(),heights.size(),flips.data(),flips.size(),cells,123);
    CHECK(proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));
    const auto unchanged=key;
    heights[3]++;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));heights[3]--;
    flips[2]=4;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));flips[2]=0;
    cells[4]++;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));cells[4]--;
    key.WaterLevel++;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));key=unchanged;
    key.CellWidth++;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));key=unchanged;
    key.Border++;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));key=unchanged;
    key.HasView=false;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));key=unchanged;
    key.Map=&proof;CHECK(!proof.Matches(key,heights.data(),heights.size(),flips.data(),flips.size(),cells));key=unchanged;
    key.FlipRowBytes=0;CHECK(!proof.Matches(key,heights.data(),heights.size(),nullptr,0,cells));
    proof.Commit(key,heights.data(),heights.size(),nullptr,0,cells,124);
    CHECK(proof.Matches(key,heights.data(),heights.size(),nullptr,0,cells));
}

TEST(water_hardware_terrain_shroud_expansion_matches_original_per_vertex_sampling)
{
    for(float width:{7.5f,10.f,37.5f,40.f})for(float height:{8.f,25.f,40.f})for(int border:{0,3,8}) {
        TerrainSnapshotKey key;key.Width=41;key.Height=29;key.Border=border;
        key.HasShroud=true;key.CellColumns=7;key.CellRows=5;key.CellWidth=width;key.CellHeight=height;
        std::vector<unsigned char> cells(35),actual;
        for(unsigned i=0;i<cells.size();++i)cells[i]=static_cast<unsigned char>((i*73)%256);
        ExpandTerrainShroud(key,cells,actual);
        CHECK_EQ(actual.size(),size_t(key.Width)*key.Height);
        for(int y=0;y<key.Height;++y)for(int x=0;x<key.Width;++x) {
            const int sx=int(std::floor((x-border)*10.f/width)),sy=int(std::floor((y-border)*10.f/height));
            const unsigned char expected=sx>=0 && sy>=0 && sx<7 && sy<5 ? cells[size_t(sy)*7+sx] : 0;
            CHECK_EQ(actual[size_t(y)*key.Width+x],expected);
        }
        key.HasShroud=false;ExpandTerrainShroud(key,cells,actual);
        for(unsigned char value:actual)CHECK_EQ(value,255);
    }
}

TEST(water_hardware_static_albedo_update_retains_exact_edges_and_source_slots)
{
    std::vector<Triangle> input={Triangle{},Face(0,2),Face(1,3),Face(2,4)};
    Scene incremental,fresh;
    CHECK(incremental.PackLinear(input,false,3));
    const auto edges=incremental.triangleTexels;
    input[0].albedo={2,-1,.5f}; // Degenerate sources still participate in validation.
    input[1].albedo={1.4f,-.2f,.9f};input[2].albedo={.1f,.2f,.3f};input[3].a.z+=.3f;
    CHECK(incremental.PackLinear(input,false,3));
    CHECK(incremental.LinearStaticGeometryReused());CHECK(!incremental.LinearStaticPrefixReused());
    CHECK(fresh.PackLinear(input,false));
    CHECK_MEM(incremental.triangleTexels.data(),fresh.triangleTexels.data(),fresh.triangleTexels.size()*sizeof(Float4));
    for(unsigned i=0;i<2;++i) {
        CHECK_MEM(incremental.triangleTexels.data()+i*4,edges.data()+i*4,3*sizeof(Float4));
        CHECK_EQ(incremental.SourceTriangleIndex(i),i+1);
    }
    const auto published=incremental.triangleTexels;
    input[0].albedo.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!incremental.PackLinear(input,false,3));
    CHECK_MEM(incremental.triangleTexels.data(),published.data(),published.size()*sizeof(Float4));
    input[0].albedo.x=2;input[2].a.x+=.01f;
    CHECK(incremental.PackLinear(input,false,3));CHECK(!incremental.LinearStaticGeometryReused());
    CHECK(fresh.PackLinear(input,false));
    CHECK_MEM(incremental.triangleTexels.data(),fresh.triangleTexels.data(),fresh.triangleTexels.size()*sizeof(Float4));
    std::swap(input[0],input[1]);
    CHECK(incremental.PackLinear(input,false,3));CHECK(!incremental.LinearStaticGeometryReused());
    CHECK(fresh.PackLinear(input,false));
    for(unsigned i=0;i<fresh.triangles.size();++i)CHECK_EQ(incremental.SourceTriangleIndex(i),fresh.SourceTriangleIndex(i));
}


TEST(water_hardware_static_span_layout_retains_ids_only_with_exact_source_rows)
{
    int a=0,b=0,c=0;
    const std::vector<StaticSpan> original={{&a,100,4,11},{&b,104,0,12},{&c,104,7,13}};
    auto current=original;
    current[0].Fingerprint=21;current[2].Fingerprint=23;
    CHECK(SameStaticSpanLayout(original,current)); // appearance changes can be staged in place
    current[0].Identity=&b;CHECK(!SameStaticSpanLayout(original,current));current=original;
    std::swap(current[0],current[1]);CHECK(!SameStaticSpanLayout(original,current));current=original;
    current[2].Count--;CHECK(!SameStaticSpanLayout(original,current));current=original;
    current[0].First--;CHECK(!SameStaticSpanLayout(original,current));current=original;
    current[1].Identity=&a;CHECK(!SameStaticSpanLayout(original,current));current=original;
    current.pop_back();CHECK(!SameStaticSpanLayout(original,current));
    CHECK(SameStaticSpanLayout({},{}));
}


TEST(water_hardware_snapshot_policy_bounds_terrain_pressure_without_plane_or_shroud_thrash)
{
    int map=0,data=0,owner=0,nextMap=0,nextData=0;
    NativeSnapshotKey key;key.Map=&map;key.Data=&data;key.Owner=&owner;key.Width=512;key.Height=512;key.Border=30;
    NativeSnapshotPolicy policy;
    CHECK(policy.RetainFull(key,0));CHECK(policy.Changed);
    CHECK(policy.RetainFull(key,82326));CHECK(!policy.Changed); // Island stays on retained native geometry.
    CHECK(policy.RetainFull(key,NativeSnapshotPolicy::TerrainRowBudget));
    CHECK(!policy.RetainFull(key,NativeSnapshotPolicy::TerrainRowBudget+1));CHECK(policy.Changed);
    CHECK(!policy.RetainFull(key,111316));CHECK(!policy.Changed); // USA2 uses original reflected scope.
    CHECK(!policy.RetainFull(key,0));CHECK(!policy.Changed); // Shroud/source pressure decrease cannot toggle allocations.
    CHECK_EQ(policy.Peak(),111316u);
    // A flyover can alternate water planes with49K/111K terrain rows. Pressure remains map-wide;
    // the independent TerrainSnapshotKey above still invalidates geometry on exact plane changes.
    for(size_t rows:{49324u,111316u,49324u,111316u}) {
        CHECK(!policy.RetainFull(key,rows));CHECK(!policy.Changed);
    }
    key.Map=&nextMap;CHECK(policy.RetainFull(key,0));CHECK(policy.Changed);
    CHECK(!policy.RetainFull(key,111316));
    key.Data=&nextData;CHECK(policy.RetainFull(key,82326));CHECK(policy.Changed);
    CHECK(!policy.RetainFull(key,111316));
    policy.Reset();CHECK(policy.RetainFull(key,82326));CHECK(policy.Changed);
}


TEST(water_hardware_terrain_groups_preserve_original_ordered_scope_indices)
{
    std::vector<Triangle> terrain;
    for(unsigned y=0;y<81;++y)for(unsigned x=0;x<93;++x) {
        const float z=float((x+y)%9)+(y%3==0 ? -2 : 2);
        terrain.push_back({{float(x)*10,float(y)*10,z},{float(x)*10+10,float(y)*10,z},
            {float(x)*10,float(y)*10+10,z},{.3f,.4f,.5f}});
    }
    std::vector<View> views;
    for(unsigned i=0;i<40;++i) {
        View view;view.origin={float(i)*23-100,float(i%7)*90,-20};
        view.rayTopLeft={-.3f,.3f,1};view.rayDx={.6f,0,0};view.rayDy={0,-.6f,0};
        view.maxDistance=80+float(i)*3;view.secondaryDistance=float(i%3)*40;view.shadowDistance=float(i%4)*70;
        view.sunDirection={.3f,-.7f,1};
        views.push_back(view);
    }
    // The wider view also contains whole above-water groups.
    View overview=views.back();
    overview.origin={460,400,-1500};
    views.push_back(overview);

    TerrainGroups groups;groups.Build(terrain);
    size_t totalSkipped=0,totalAccepted=0;
    for(const View &view:views) {
        CaptureScope scope(view);
        std::vector<TerrainGroups::Span> spans;
        std::vector<size_t> actual,original;
        size_t tested=0,skipped=0,accepted=0;
        groups.Select(terrain,scope,spans,tested,skipped,accepted);
        for (const auto &span:spans)
            for (size_t t=span.First;t<span.End;++t) actual.push_back(t);
        for(size_t t=0;t<terrain.size();++t) {
            const auto &v=terrain[t];
            if(scope.Matches(Minimum(v.a,Minimum(v.b,v.c)),Maximum(v.a,Maximum(v.b,v.c))))original.push_back(t);
        }
        CHECK(actual==original);CHECK_EQ(tested+skipped+accepted,terrain.size());
        totalSkipped+=skipped;
        totalAccepted+=accepted;
    }
    CHECK(totalSkipped>terrain.size());
    CHECK(totalAccepted>0);
    terrain.resize(17);groups.Build(terrain);
    View invalid;invalid.rayTopLeft={0,0,0};CaptureScope unrestricted(invalid);
    std::vector<TerrainGroups::Span> selected;
    size_t tested=0,skipped=0,accepted=0;
    groups.Select(terrain,unrestricted,selected,tested,skipped,accepted);
    CHECK_EQ(selected.size(),1u);CHECK_EQ(skipped+tested,0u);
    CHECK_EQ(accepted,terrain.size());
    if (!selected.empty()) { CHECK_EQ(selected[0].First,0u); CHECK_EQ(selected[0].End,terrain.size()); }
    terrain.clear();groups.Build(terrain);
    groups.Select(terrain,unrestricted,selected,tested,skipped,accepted);
    CHECK(selected.empty());CHECK_EQ(tested+skipped+accepted,0u);
}
