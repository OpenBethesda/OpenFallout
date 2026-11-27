#include <components/falloutscript/ragdollrules.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace FalloutScript::Ragdoll;

    TEST(FalloutScriptRagdollRulesTest, the_parts_of_one_body_collide_as_the_table_of_the_executable_says)
    {
        // 0x1268078: part 1 meets 6, 7, 12, 13, 17 and 22, and the table is the same both ways
        EXPECT_TRUE(partsCollide(1, 6));
        EXPECT_TRUE(partsCollide(6, 1));
        EXPECT_TRUE(partsCollide(1, 22));
        EXPECT_FALSE(partsCollide(1, 2));
        EXPECT_FALSE(partsCollide(2, 1));
        // a part with nothing in its row meets nothing of its own body
        EXPECT_FALSE(partsCollide(19, 22));
        EXPECT_FALSE(partsCollide(40, 1));
        for (unsigned a = 0; a < 32; ++a)
            for (unsigned b = 0; b < 32; ++b)
                EXPECT_EQ(partsCollide(a, b), partsCollide(b, a)) << a << " " << b;
    }

    TEST(FalloutScriptRagdollRulesTest, a_body_of_skin_floats_and_its_hands_and_feet_sink_a_little)
    {
        const WaterSettings settings;
        constexpr std::uint32_t skin = 7;
        // part 1 is of the body (fBuoyancyMultBody 1): 0.75
        EXPECT_FLOAT_EQ(buoyancy(settings, { skin }, bipedLayer, 1), 0.75f);
        // part 6 is an extremity (fBuoyancyMultExtremity -0.3)
        EXPECT_FLOAT_EQ(buoyancy(settings, { skin }, bipedLayer, 6), 0.75f * -0.3f);
        // parts 17 to 21 do not float at all, those past the table keep the material's
        EXPECT_FLOAT_EQ(buoyancy(settings, { skin }, bipedLayer, 18), 0.f);
        EXPECT_FLOAT_EQ(buoyancy(settings, { skin }, bipedLayer, 30), 0.75f);
        // a body that is not of a character keeps its material's
        EXPECT_FLOAT_EQ(buoyancy(settings, { skin }, 4, 6), 0.75f);
        // metal sinks, times fBuoyancyMultBody whatever its part
        EXPECT_FLOAT_EQ(buoyancy(settings, { 5 }, bipedLayer, 6), -1.f);
        // the materials of the pieces count by their mean
        EXPECT_FLOAT_EQ(buoyancy(settings, { 9, 5 }, 4, 0), 0.f);
    }

    TEST(FalloutScriptRagdollRulesTest, a_floating_body_is_held_at_the_surface)
    {
        // the water halfway up a body from 0 to 100: between 25 and 85, at 50, is 25/60 of the way
        EXPECT_FLOAT_EQ(submergedFraction(0.f, 100.f, 50.f), 25.f / 60.f);
        EXPECT_FLOAT_EQ(submergedFraction(0.f, 100.f, 25.f), 0.f);
        EXPECT_FLOAT_EQ(submergedBuoyancy(0.8f, 0.75f), 0.3f);
        EXPECT_FLOAT_EQ(submergedBuoyancy(0.6f, 0.75f), 0.f);
        EXPECT_FLOAT_EQ(submergedBuoyancy(0.2f, 0.75f), -0.3f);
        // deep under, or a body that sinks, keeps its own
        EXPECT_FLOAT_EQ(submergedBuoyancy(1.5f, 0.75f), 0.75f);
        EXPECT_FLOAT_EQ(submergedBuoyancy(0.6f, -1.f), -1.f);
        // the fall of a floating body near the surface that hardly moves across is not slowed
        EXPECT_FALSE(dampsFall(0.6f, 0.75f, 1.f, 2.f));
        EXPECT_TRUE(dampsFall(0.6f, 0.75f, 3.f, 2.f));
        EXPECT_TRUE(dampsFall(0.6f, -1.f, 1.f, 2.f));
    }
}
