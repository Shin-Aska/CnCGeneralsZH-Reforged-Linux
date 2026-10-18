#include "test_harness.h"
#include "Platform/HazardCloudPolicy.h"

#include <initializer_list>
#include <limits>

using namespace HazardCloudPolicy;

TEST(hazard_cloud_recognizes_only_known_particle_templates)
{
    const char *const names[][3] = {
        {"PoisonFieldSmall", "PoisonFieldMedium", "PoisonFieldLarge"},
        {"RadiationFieldSmall", "RadiationFieldMedium", "RadiationFieldLarge"},
        {"AnthraxFieldSmall", "AnthraxFieldMedium", "AnthraxFieldLarge"},
        {"GC_Chem_AnthraxGammaFieldSmall", "GC_Chem_AnthraxFieldGammaMedium",
            "GC_Chem_AnthraxFieldGammaLarge"}
    };
    for (int palette = 0; palette < 4; ++palette)
        for (const char *name : names[palette]) CHECK_EQ(paletteForTemplate(name), palette);
    CHECK_EQ(paletteForTemplate("NukeRadiationInitial"), 1);
    CHECK_EQ(paletteForTemplate("poisonfieldLARGE"), 0);
    for (const char *name : {static_cast<const char *>(nullptr), "", "PoisonFieldLargeExtra", "MyRadiationFieldLarge",
            "GC_Chem_AnthraxFieldGammaSmall", "ToxinTankPuddle", "NukeMushroomStem",
            "ToxinSpray", "PoisonFieldAnthraxBomb"})
        CHECK_EQ(paletteForTemplate(name), -1);
}

TEST(hazard_cloud_matches_actual_hazard_object_names)
{
    CHECK_EQ(paletteForOwner("PoisonFieldMedium"), 0);
    CHECK_EQ(paletteForOwner("PoisonFieldUpgradedLarge"), 2);
    CHECK_EQ(paletteForOwner("PoisonFieldAnthraxBomb"), 2);
    CHECK_EQ(paletteForOwner("PoisonFieldAnthraxGammaBomb"), 3);
    CHECK_EQ(paletteForOwner("NukeRadiationFieldWeapon"), 1);
    CHECK_EQ(paletteForOwner("Nuke_RadiationFieldSmall"), 1);
    for (const char *name : {"RadiationFieldSmall", "RadiationFieldMedium", "RadiationFieldLarge"})
        CHECK_EQ(paletteForOwner(name), 1);
    for (const char *name : {"Chem_PoisonFieldGammaSmall", "Chem_PoisonFieldGammaMedium",
            "Chem_PoisonFieldGammaLarge", "GC_Chem_PoisonFieldGammaSmall",
            "GC_Chem_PoisonFieldGammaMedium", "GC_Chem_PoisonFieldGammaLarge"})
        CHECK_EQ(paletteForOwner(name), 3);
    for (const char *name : {static_cast<const char *>(nullptr), "", "NukeRadiationInitial", "AnthraxFieldLarge",
            "GC_Chem_AnthraxGammaFieldSmall", "GLAVehicleToxinTruck", "Chem_GLAScudStorm",
            "LargePoisonFieldWeapon", "PoisonFieldMediumCustom"})
        CHECK_EQ(paletteForOwner(name), -1);
}

TEST(hazard_cloud_nuclear_fades_in_across_its_full_footprint)
{
    for (float age : {0.0f, 0.1f, 1.0f, 3.0f, 6.0f, 12.0f}) {
        const Buildup nuclear = visualBuildup(1, true, age, 30.0f);
        CHECK_EQ(nuclear.formation, 1.0f);
        CHECK(nuclear.density >= 0.0f && nuclear.density <= 1.0f);
        CHECK(nuclear.release >= 0.0f && nuclear.release <= 1.0f);
    }
    CHECK_NEAR(visualBuildup(1, true, 3.0f, 30.0f).density, 0.5f, 0.00001f);
    CHECK_NEAR(visualBuildup(1, true, 3.0f, 30.0f).release, 0.675f, 0.00001f);
    CHECK_EQ(visualBuildup(1, true, 6.0f, 30.0f).density, 1.0f);
    CHECK_EQ(visualBuildup(1, true, 6.0f, 30.0f).release, 0.0f);
    CHECK_EQ(visualBuildup(1, false, 3.0f, 30.0f).density, 1.0f);
}

TEST(hazard_cloud_toxin_formation_and_release_preserve_palette_identity)
{
    for (int palette : {0, 2, 3}) {
        const Buildup beginning = visualBuildup(palette, false, 0.0f, 30.0f);
        const Buildup middle = visualBuildup(palette, false, 1.5f, 30.0f);
        const Buildup formed = visualBuildup(palette, false, 3.0f, 30.0f);
        CHECK_EQ(beginning.formation, 0.0f);
        CHECK_EQ(beginning.density, 0.0f);
        CHECK_EQ(beginning.release, 1.0f);
        CHECK_NEAR(middle.formation, 0.5f, 0.00001f);
        CHECK_EQ(middle.density, 1.0f);
        CHECK_EQ(formed.formation, 1.0f);
        CHECK_EQ(formed.density, 1.0f);
        CHECK_EQ(formed.release, 0.0f);
        CHECK_NEAR(visualBuildup(palette, true, 1.5f, 30.0f).formation,
            middle.formation, 0.00001f); // Large-nuclear flag must not slow toxin.
    }
}

TEST(hazard_cloud_short_lifetimes_have_a_visible_lingering_phase)
{
    for (float lifetime : {0.04f, 0.5f, 2.5f, 10.0f, 30.0f, 180.0f})
        for (int palette = 0; palette < 4; ++palette) {
            const Buildup formed = visualBuildup(palette, true, lifetime * 0.25f, lifetime);
            CHECK_EQ(formed.formation, 1.0f);
            CHECK_EQ(formed.density, 1.0f);
            CHECK_EQ(formed.release, 0.0f);
            CHECK_EQ(visualBuildup(palette, true, lifetime, lifetime).density, 0.0f);
            CHECK_EQ(visualBuildup(palette, true, lifetime + 1.0f, lifetime).density, 0.0f);
        }
    CHECK(visualBuildup(1, false, 0.3f, 2.5f).density > 0.4f);
}

TEST(hazard_cloud_invalid_buildup_is_hidden_and_finite)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (int palette : {-1, 0, 1, 4})
        for (float value : {-1.0f, nan, inf}) {
            const Buildup badAge = visualBuildup(palette, true, value, 30.0f);
            const Buildup badLife = visualBuildup(palette, true, 1.0f, value);
            CHECK_EQ(badAge.density, 0.0f);
            CHECK_EQ(badLife.density, 0.0f);
            CHECK(std::isfinite(badAge.formation));
            CHECK(std::isfinite(badLife.formation));
        }
    CHECK_EQ(visualBuildup(1, true, 0.0f, 0.0f).density, 0.0f);
    CHECK_EQ(visualBuildup(-1, true, 1.0f, 30.0f).density, 0.0f);
}

TEST(hazard_cloud_fringe_covers_original_rotated_breathing_quads)
{
    CHECK_EQ(visualFringe(0.0f), 0.0f);
    CHECK_NEAR(visualFringe(42.82f), std::sqrt(2.0f) * 1.04f * 42.82f, 0.000001f);
    CHECK(visualFringe(50.0f) > 50.0f * std::sqrt(2.0f));
    CHECK_EQ(visualFringe(-1.0f), 0.0f);
    CHECK_EQ(visualFringe(std::numeric_limits<float>::infinity()), 0.0f);
    CHECK_EQ(visualFringe(std::numeric_limits<float>::max()), 0.0f);
}

TEST(hazard_cloud_admission_rejects_invalid_values_without_changing_footprints)
{
    CHECK(validField(-100.0f, 200.0f, 30.0f, 300.0f, 18.0f, 1.0f, 1.0f, 89.0f, 2));
    CHECK(validField(0.0f, 0.0f, 0.0f, 7.5f, 18.0f, 0.0f, 0.0f, 0.0f, 3));
    const float good[] = {0.0f, 0.0f, 30.0f, 80.0f, 18.0f, 1.0f, 1.0f, 63.0f};
    for (int slot = 0; slot < 8; ++slot)
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity()}) {
            float values[8];
            for (int i = 0; i < 8; ++i) values[i] = i == slot ? invalid : good[i];
            CHECK(!validField(values[0], values[1], values[2], values[3], values[4],
                values[5], values[6], values[7], 0));
        }
    CHECK(!validField(0, 0, 0, 0, 18, 1, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 0, 1, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, -0.1f, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1.1f, 1, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1.1f, 0, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, -1, 0));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, 0, -1));
    CHECK(!validField(0, 0, 0, 80, 18, 1, 1, 0, 4));
    CHECK(!validField(0, 0, 0, 1e30f, 18, 1, 1, 0, 0));
}
