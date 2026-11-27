#include <components/falloutscript/characterbody.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace FalloutScript::CharacterBody;

    // The BSBound of meshes\characters\_male\skeleton.nif, of the dog and of the deathclaw (FalloutNV.esm's archives)
    const Bound human{ { 0, 0, 66.22 }, { 23.0977, 17.6446, 66.22 } };
    const Bound dog{ { 0, 0, 35.3895 }, { 29.563, 62.7549, 35.5083 } };
    const Bound deathclaw{ { 0, 0, 76.1823 }, { 50, 95.1381, 76.1823 } };

    TEST(FalloutScriptCharacterBodyTest, a_person_is_an_upright_hull_of_18_points_not_a_capsule_of_radius_31)
    {
        const Body person = body(human, 1);
        EXPECT_EQ(person.mKind, Kind::Upright);
        ASSERT_EQ(person.mHull.size(), 18u);
        const double r0 = (23.0977 + 17.6446) / 2;
        EXPECT_NEAR(person.mHalfWidth, r0, 1e-9);
        EXPECT_NEAR(person.mHeight, 132.44, 1e-9);
        EXPECT_DOUBLE_EQ(person.mHull.front().mZ, 0);
        EXPECT_DOUBLE_EQ(person.mHull.back().mZ, 132.44);
        // the low ring is at the foot (31) plus 0.1 Havok units; the high one half a radius below the top
        EXPECT_NEAR(person.mHull[1].mZ, 31 + 0.6999041, 1e-6);
        EXPECT_NEAR(person.mHull[1].mX, r0, 1e-9);
        EXPECT_NEAR(person.mHull[3].mY, r0, 1e-9);
        EXPECT_NEAR(person.mHull[9].mZ, 132.44 - r0 / 2, 1e-9);
        // a person twice as large: the foot is 62, and three quarters of the radius never reaches it
        EXPECT_NEAR(body(human, 2).mHull[1].mZ, 62 + 0.6999041, 1e-6);
    }

    TEST(FalloutScriptCharacterBodyTest, a_dog_lies_in_one_capsule_along_its_length)
    {
        const Body lying = body(dog, 1);
        EXPECT_EQ(lying.mKind, Kind::Lying);
        ASSERT_EQ(lying.mCapsules.size(), 1u);
        const Capsule& capsule = lying.mCapsules[0];
        EXPECT_NEAR(capsule.mRadius, 29.563, 1e-9);
        EXPECT_NEAR(capsule.mA.mY, 62.7549 - 29.563, 1e-9);
        EXPECT_NEAR(capsule.mB.mY, -(62.7549 - 29.563), 1e-9);
        EXPECT_NEAR(capsule.mA.mZ, 29.563, 1e-9);
        EXPECT_NEAR(lying.mHeight, 2 * 29.563, 1e-9);
        EXPECT_NEAR(lying.mHalfLength, 62.7549, 1e-9);
    }

    TEST(FalloutScriptCharacterBodyTest, a_deathclaw_stands_on_two_legs_under_its_body)
    {
        const Body lying = body(deathclaw, 1);
        EXPECT_EQ(lying.mKind, Kind::LyingWithLegs);
        ASSERT_EQ(lying.mCapsules.size(), 3u);
        const double d = 76.1823 - 50;
        EXPECT_NEAR(lying.mCapsules[0].mA.mZ, 2 * d + 50, 1e-9);
        EXPECT_NEAR(lying.mCapsules[0].mRadius, 50, 1e-9);
        // the legs: 8 units in from each end, from d - 1 to d + 1, of radius d - 1, on the floor
        EXPECT_NEAR(lying.mCapsules[1].mA.mY, 95.1381 - 50 - 8, 1e-9);
        EXPECT_NEAR(lying.mCapsules[2].mA.mY, -(95.1381 - 50) + 8, 1e-9);
        EXPECT_NEAR(lying.mCapsules[1].mRadius, d - 1, 1e-9);
        EXPECT_NEAR(lying.mCapsules[1].mB.mZ - lying.mCapsules[1].mRadius, 0, 1e-9);
        EXPECT_NEAR(lying.mHeight, 2 * 76.1823, 1e-9);
    }

    TEST(FalloutScriptCharacterBodyTest, a_body_longer_across_lies_along_x_with_the_legs_still_off_y)
    {
        const Body lying = body(Bound{ {}, { 60, 20, 30 } }, 1);
        EXPECT_EQ(lying.mKind, Kind::LyingWithLegs);
        EXPECT_NEAR(lying.mCapsules[0].mA.mX, 40, 1e-9);
        EXPECT_NEAR(lying.mCapsules[0].mB.mX, -40, 1e-9);
        EXPECT_NEAR(lying.mCapsules[1].mA.mY, -8, 1e-9);
        EXPECT_NEAR(lying.mCapsules[2].mA.mY, 8, 1e-9);
        EXPECT_NEAR(lying.mCapsules[1].mRadius, 9, 1e-9);
    }

    TEST(FalloutScriptCharacterBodyTest, a_blow_reaches_across_both_radii_and_half_the_reach)
    {
        EXPECT_NEAR(meleeRadius(human, 1), 17.6446, 1e-9);
        EXPECT_NEAR(meleeRadius(dog, 1.5), 62.7549 * 1.5, 1e-9);
        EXPECT_DOUBLE_EQ(meleeDistance(17.5, 20, 64), 69.5);
        EXPECT_DOUBLE_EQ(meleeDistance(-50, 0, 10), 0);
    }

    TEST(FalloutScriptCharacterBodyTest, a_body_walks_up_an_edge_below_the_lowest_upright_part_of_its_side)
    {
        // a person: the low ring at the foot (31) and 0.1 Havok units, not 34
        EXPECT_NEAR(body(human, 1).mStepHeight, 31 + 0.6999041, 1e-6);
        // the dog: the middle of its capsule; the deathclaw: the middle of the lower balls of its legs
        EXPECT_NEAR(body(dog, 1).mStepHeight, 29.563, 1e-9);
        EXPECT_NEAR(body(deathclaw, 1).mStepHeight, 76.1823 - 50 - 1, 1e-9);
    }

    TEST(FalloutScriptCharacterBodyTest, a_radscorpion_wider_than_tall_stands_its_width_high)
    {
        // meshes\creatures\radscorpion\skeleton.nif: half extents 70.561 across, 125.838 long, 30.206 high. The lying
        // branch takes the smaller of across and high as the radius and the larger for the height (0xC72749 to
        // 0xC7276C), so the body's capsule stands over its legs up to twice the width
        const Body scorpion = body(Bound{ { 0, 0, 30.206 }, { 70.561, 125.838, 30.206 } }, 1);
        EXPECT_EQ(scorpion.mKind, Kind::LyingWithLegs);
        EXPECT_NEAR(scorpion.mHeight, 2 * 70.561, 1e-9);
        EXPECT_NEAR(scorpion.mCapsules[0].mRadius, 30.206, 1e-9);
        EXPECT_NEAR(scorpion.mCapsules[0].mA.mZ, 2 * (70.561 - 30.206) + 30.206, 1e-9);
    }
}
