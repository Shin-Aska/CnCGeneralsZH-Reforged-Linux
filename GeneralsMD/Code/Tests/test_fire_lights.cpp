// test_fire_lights.cpp: the grouping and the cap of the Firelight option.

#include "test_harness.h"

#include "W3DDevice/GameClient/FireLights.h"
#include "Platform/FireSmokePolicy.h"
#include "Platform/FireWaterEffects.h"
#include <limits>

using namespace FireLights;

static Flames flamesAt(float weight, float x, float y, float z)
{
	Flames flames;
	flames.add(weight, x, y, z);
	return flames;
}

static const float VIEW_CENTER[3] = { 0.0f, 0.0f, 0.0f };
static const float VIEW_EXTENT[3] = { 1000.0f, 1000.0f, 1000.0f };

TEST(flooded_terrain_does_not_hide_a_fire_above_the_water)
{
	CHECK_EQ(waterVisibility(false, 0.0f, 100.0f), 1.0f);
	CHECK_EQ(waterVisibility(true, 110.0f, 100.0f), 1.0f);
	CHECK_EQ(waterVisibility(true, 100.0f, 100.0f), 1.0f);
	CHECK_EQ(waterVisibility(true, 90.0f, 100.0f), 0.0f);
}

TEST(rising_and_receding_water_fades_a_fire_continuously_at_its_source)
{
	float previous = 1.0f;
	for (int i = 0; i <= 400; ++i)
	{
		const float waterZ = 99.0f + 0.01f * i;
		const float shown = waterVisibility(true, 100.0f, waterZ);
		CHECK(shown >= 0.0f && shown <= previous);
		CHECK(previous - shown < 0.01f);
		previous = shown;
	}
	CHECK_EQ(previous, 0.0f);
	CHECK_NEAR(waterVisibility(true, 100.0f, 101.0f), 0.5f, 0.0001f);
	// A loaded submerged source starts hidden, with no old-map fade history to restore.
	CHECK_EQ(waterVisibility(true, 100.0f, 130.0f), 0.0f);
	CHECK_EQ(waterVisibility(true, 100.0f, 99.0f), 1.0f);
	CHECK_EQ(waterVisibility(true, 100.0f, std::numeric_limits<float>::quiet_NaN()), 1.0f);
}

TEST(submerged_flames_contribute_no_firelight)
{
	Gather gather;
	Flames submerged = flamesAt(FULL_WEIGHT, 0.0f, 0.0f, 90.0f);
	submerged.scale(waterVisibility(true, 90.0f, 100.0f));
	gather.add(submerged);
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 0);
	Flames rooftop = flamesAt(FULL_WEIGHT, 0.0f, 0.0f, 110.0f);
	rooftop.scale(waterVisibility(true, 110.0f, 100.0f));
	gather.add(rooftop);
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 1);
}

TEST(water_extinguishes_combustion_smoke_without_hiding_other_cloud_effects)
{
	CHECK(FireSmokePolicy::knownSource("SmokeFactionSmall"));
	CHECK(FireSmokePolicy::knownSource("SmokeFactionMedium"));
	CHECK(FireSmokePolicy::knownSource("smokefactionlarge"));
	CHECK(FireSmokePolicy::knownSource("SmolderingSmokeHuge"));
	CHECK(FireSmokePolicy::knownSource("ReforgedStructureSmoke"));
	CHECK(!FireSmokePolicy::knownSource("ToxicExplosionLargeSmoke"));
	CHECK(!FireSmokePolicy::knownSource("TankSmallMuzzleFlashSmoke"));
	CHECK(!FireSmokePolicy::knownSource("CounterMeasureSmoke"));
	CHECK(!FireSmokePolicy::knownSource("WaterSplashThrow"));
	CHECK(!FireSmokePolicy::knownSource("SmokeFactionLargeCustom"));
	CHECK(!FireSmokePolicy::knownSource(NULL));
}

TEST(smoke_emission_uses_the_source_elevation_and_the_current_waterline)
{
	CHECK(!FireSmokePolicy::submerged(false, 0.0f, 100.0f));
	CHECK(!FireSmokePolicy::submerged(true, 110.0f, 100.0f));
	CHECK(!FireSmokePolicy::submerged(true, 100.0f, 100.0f));
	CHECK(!FireSmokePolicy::submerged(true, 100.0f, 100.05f));
	CHECK(FireSmokePolicy::submerged(true, 100.0f, 100.2f));
	CHECK(FireSmokePolicy::submerged(true, 90.0f, 100.0f));
	CHECK(!FireSmokePolicy::submerged(true, 90.0f, std::numeric_limits<float>::quiet_NaN()));
}

struct ClipVertex { float x, y, z, u, v, width, age; };
static ClipVertex clipBlend(const ClipVertex &a, const ClipVertex &b, float t)
{
	return {a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t,
		a.u + (b.u-a.u)*t, a.v + (b.v-a.v)*t, a.width, a.age};
}
static void waterQuad(ClipVertex *q)
{
	q[0] = {-1,-1,2,0,0,2,17.7f}; q[1] = {1,-1,2,1,0,2,17.7f};
	q[2] = {1,1,2,1,1,2,17.7f}; q[3] = {-1,1,2,0,1,2,17.7f};
}
static float clipArea(const ClipVertex *q, int pieces)
{
	float area = 0;
	for (int i=0;i<pieces;++i) for (int triangle=0;triangle<2;++triangle)
	{
		const ClipVertex &a=q[i*4], &b=q[i*4+triangle+1], &c=q[i*4+triangle+2];
		const float part = .5f*((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x));
		CHECK(part >= -0.00001f);
		area += part;
	}
	return area;
}

TEST(waterline_clipping_keeps_texture_coordinates_and_splits_a_pentagon_without_overlap)
{
	ClipVertex q[8]; waterQuad(q);
	const float plane[4] = {1,1,0,1};
	const int pieces = FireWaterEffects::clipQuad(q, plane, clipBlend);
	CHECK_EQ(pieces, 2);
	CHECK_NEAR(clipArea(q,pieces), 3.5f, 0.00001f);
	for (int i=0;i<pieces*4;++i)
	{
		CHECK(q[i].x+q[i].y+1 >= -0.00001f);
		CHECK_NEAR(q[i].u, (q[i].x+1)*.5f, .00001f);
		CHECK_NEAR(q[i].v, (q[i].y+1)*.5f, .00001f);
		CHECK_EQ(q[i].width, 2);
		CHECK_EQ(q[i].age, 17.7f);
	}
}

TEST(waterline_clipping_handles_full_submersion_and_exact_corner_contact)
{
	ClipVertex q[8]; waterQuad(q);
	const float below[4] = {0,1,0,-2};
	CHECK_EQ(FireWaterEffects::clipQuad(q,below,clipBlend),0);
	waterQuad(q); const float dry[4] = {0,1,0,1};
	CHECK_EQ(FireWaterEffects::clipQuad(q,dry,clipBlend),1);
	CHECK_NEAR(clipArea(q,1),4,.00001f);
	waterQuad(q); const float diagonal[4] = {1,1,0,0};
	CHECK_EQ(FireWaterEffects::clipQuad(q,diagonal,clipBlend),1);
	CHECK_NEAR(clipArea(q,1),2,.00001f);
	waterQuad(q); const float triangle[4] = {1,1,0,-1};
	CHECK_EQ(FireWaterEffects::clipQuad(q,triangle,clipBlend),1);
	CHECK_NEAR(clipArea(q,1),.5f,.00001f);
}

TEST(waterline_clipping_stays_bounded_for_rolled_billboards_and_mirrored_views)
{
	for (int angle=0;angle<32;++angle)
	{
		const float nx=std::cos(angle*.19634954f), ny=std::sin(angle*.19634954f);
		float previous=4.01f;
		for (int level=0;level<=40;++level)
		{
			ClipVertex q[8]; waterQuad(q);
			const float plane[4] = {nx,ny,0,2.0f-level*.1f};
			const int pieces=FireWaterEffects::clipQuad(q,plane,clipBlend);
			CHECK(pieces>=0 && pieces<=2);
			const float area=clipArea(q,pieces);
			CHECK(area<=previous+.00001f && area<=4.00001f);
			previous=area;
			for (int i=0;i<pieces*4;++i) CHECK(nx*q[i].x+ny*q[i].y+plane[3]>=-.00001f);
		}
	}
}

TEST(steam_requires_an_observed_dry_to_wet_crossing_and_does_not_replay_on_load)
{
	FireWaterEffects::Contact loaded;
	CHECK(!loaded.observe(true,10,20,100));
	CHECK(!loaded.observe(true,10,21,101));
	FireWaterEffects::Contact fire;
	CHECK(!fire.observe(true,20,19,100));
	CHECK(fire.observe(true,20,20.2f,101));
	CHECK(!fire.observe(true,20,20.3f,101)); // paused/repeated render pass
	CHECK(!fire.observe(true,20,19.9f,102));
	CHECK(!fire.observe(true,20,20.2f,103)); // small waves do not rearm the puff
	CHECK(!fire.observe(true,20,19.4f,104));
	CHECK(fire.observe(true,20,20.2f,105)); // genuinely exposed, then flooded again
	CHECK(!fire.observe(true,20,22,1)); // clock/reset discontinuity establishes state
}

TEST(flames_near_each_other_are_one_light_and_far_ones_are_two)
{
	Gather gather;
	gather.add(flamesAt(FULL_WEIGHT, 0.0f, 0.0f, 0.0f));
	gather.add(flamesAt(FULL_WEIGHT, 20.0f, 0.0f, 0.0f));
	CHECK_EQ(gather.count(), 1);
	gather.add(flamesAt(FULL_WEIGHT, 500.0f, 0.0f, 0.0f));
	CHECK_EQ(gather.count(), 2);

	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 2);
	CHECK_NEAR(lights[0].x, 10.0f, 0.01f);
	CHECK_NEAR(lights[0].z, LIFT, 0.01f);
	// two systems ten units either side of the middle widen the reach past a lone one's
	CHECK(lights[0].reach > lights[1].reach);
}

TEST(a_muzzle_flare_lights_nothing_and_a_fire_comes_on_from_nothing)
{
	CHECK_EQ(strength(MIN_WEIGHT), 0.0f);
	CHECK(strength(MIN_WEIGHT + 1.0f) < 0.01f);
	CHECK_EQ(strength(FULL_WEIGHT), 1.0f);
	CHECK_EQ(strength(FULL_WEIGHT * 10.0f), 1.0f);

	Gather gather;
	gather.add(flamesAt(MIN_WEIGHT - 1.0f, 0.0f, 0.0f, 0.0f));
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 0);
}

TEST(the_strongest_fires_take_the_capped_lights)
{
	Gather gather;
	for (int i = 0; i < MAX_LIGHTS * 2; ++i)
		gather.add(flamesAt(MIN_WEIGHT + 10.0f * (float)(i + 1), (float)i * 200.0f - 2000.0f, 0.0f, 0.0f));
	const float wide[3] = { 10000.0f, 10000.0f, 10000.0f };
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, wide, 0, lights), (int)MAX_LIGHTS);
	const float weakestKept = strength(MIN_WEIGHT + 10.0f * (float)(MAX_LIGHTS + 1));
	for (int i = 0; i < MAX_LIGHTS; ++i)
		CHECK(lights[i].strength >= weakestKept - 0.001f);
}

TEST(a_fire_out_of_reach_of_the_view_is_no_light)
{
	Gather gather;
	gather.add(flamesAt(FULL_WEIGHT, 5000.0f, 0.0f, 0.0f));
	Light lights[MAX_LIGHTS];
	CHECK_EQ(gather.pick(VIEW_CENTER, VIEW_EXTENT, 0, lights), 0);
}

TEST(the_flicker_stays_in_its_band_and_moves)
{
	float low = 2.0f, high = 0.0f;
	for (unsigned int frame = 0; frame < 2000; ++frame) {
		const float f = flicker(frame, 300.0f, 700.0f);
		low = f < low ? f : low;
		high = f > high ? f : high;
	}
	CHECK(low >= 0.78f - 0.001f);
	CHECK(high <= 1.0f + 0.001f);
	CHECK(high - low > 0.1f);
}
