#include "WaterDx11HullFixture.h"

namespace {
using namespace WaterDx11HullTest;
// Match both fixed regions, not just a single possibly clipped/background pixel.
void EqualRegions(const char *label,const std::vector<Pixel> &a,const std::vector<Pixel> &b) {
    const float central=Region(a,b),distant=Region(a,b,true);
    std::printf("  hull equality %s central=%.9g distant=%.9g\n",label,double(central),double(distant));
    CHECK_NEAR(central,0,.0001f);CHECK_NEAR(distant,0,.0001f);
}
}

TEST(task8_hull_analytic_surface_pin)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;
    Result first,repeat;if(!Render(f,NULL,first)||!Render(f,NULL,repeat))return;
    EqualRegions("pin-native-repeat",first.production,repeat.production);
    EqualRegions("pin-native-disabled",first.production,first.disabled);
    EqualRegions("pin-disabled-enabled-no-hull",first.disabled,first.enabled); // mesh-enabled shader with no hull is neutral
    const auto fft=f.water->Get_Test_Fft_State();CHECK_EQ(fft.updates,0u);CHECK(!fft.ready);
    std::printf("  hull PIN actual-prepared-PS finite/nonempty repeatable; shared-disabled parity and no-hull-neutral; FFT-updates=%llu\n",
        static_cast<unsigned long long>(fft.updates));
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

TEST(task8_mesh_contact_triangle_readback)
{
    Fixture f;CHECK(f.Initialise());if(!f.water)return;
    Pose crossing,above,shifted;CHECK(crossing.Set(0));CHECK(above.Set(32));CHECK(shifted.Set(0,16));
    double low=0,high=0;CHECK_EQ(crossing.Intersections(low,high),4u);CHECK_NEAR(low,-16,1e-12);CHECK_NEAR(high,16,1e-12);
    CHECK_EQ(above.Intersections(low,high),0u);CHECK_EQ(shifted.Intersections(low,high),4u);
    // Only posed mesh changes: mean/contact contours, motion, time, camera, lighting,
    // scene/atlas bytes and disabled FFT fields are held fixed. Metadata/radius rebuilt.
    CHECK_MEM(crossing.mean,above.mean,sizeof(crossing.mean));CHECK_MEM(crossing.contact,above.contact,sizeof(crossing.contact));
    Result wet,dry,moved,absent;
    if(!Render(f,&crossing,wet)||!Render(f,&above,dry)||!Render(f,&shifted,moved)||!Render(f,NULL,absent))return;
    EqualRegions("dry-native-enabled",dry.production,dry.enabled);
    EqualRegions("wet-native-enabled",wet.production,wet.enabled);
    EqualRegions("moved-native-enabled",moved.production,moved.enabled);
    EqualRegions("absent-native-enabled",absent.production,absent.enabled);
    EqualRegions("disabled-wet-dry",wet.disabled,dry.disabled);EqualRegions("disabled-wet-moved",wet.disabled,moved.disabled);
    EqualRegions("absent-disabled-enabled",absent.disabled,absent.enabled);
    CHECK_NEAR(Region(wet.enabled,dry.enabled,true),0,.0001f); // outside mesh support
    CHECK_NEAR(Region(moved.enabled,moved.disabled),0,.0001f); // wrong-region witness
    const float control=Region(wet.enabled,dry.enabled),production=Region(wet.production,dry.production);
    const float disabled=Region(wet.disabled,dry.disabled);
    std::printf("  hull fixedROI x[28,36)y[16,32) distant-x[56,60) independent-plane=0 crossing=4 above=0 shifted-x=16\n"
        "  hull contrasts shared-mesh=%.9g shared-disabled=%.9g actual-prepared-production=%.9g fixed-threshold=%.9g\n",
        double(control),double(disabled),double(production),double(Contrast));
    // Positive visibility and disabled/contact-shift controls MUST pass before a
    // production failure qualifies as RED. Never retune this threshold from a RED.
    CHECK(control>Contrast);CHECK(disabled<.0001f);CHECK(production>Contrast);
    CHECK_EQ(f.water->Get_Test_Fft_State().updates,0u);
    CHECK_EQ(DebugWarnings(f.device.Get_Device()),0u);
}

