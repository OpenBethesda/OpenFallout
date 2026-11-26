#include <components/falloutscript/movement.hpp>

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    using namespace FalloutScript::Movement;
    using FalloutScript::Game;

    Settings settingsOf(const std::map<std::string, double, std::less<>>& plugin)
    {
        return Settings::fromGameSettings([&](std::string_view name) -> std::optional<double> {
            const auto found = plugin.find(name);
            if (found == plugin.end())
                return std::nullopt;
            return found->second;
        });
    }

    // What FalloutNV.esm says (spec movement.md, M1 and M6)
    Settings newVegas()
    {
        return settingsOf({ { "fMoveBaseSpeed", 77 }, { "fMoveSneakMult", 0.57 }, { "fMoveRunMult", 4 },
            { "fMoveOneCrippledLegSpeedMult", 0.85 }, { "fMoveTwoCrippledLegsSpeedMult", 0.75 },
            { "fMoveHeavyArmorPenalty", 0.15 }, { "fMoveMediumArmorPenalty", 0.075 }, { "fMove2HBigPenalty", 0.10 },
            { "fJumpSwimmingMult", 2 } });
    }

    // What Fallout3.esm says
    Settings fallout3()
    {
        return settingsOf({ { "fMoveBaseSpeed", 77 }, { "fMoveSneakMult", 0.57 }, { "fMoveRunMult", 4 },
            { "fMoveOneCrippledLegSpeedMult", 0.85 }, { "fMoveTwoCrippledLegsSpeedMult", 0.75 },
            { "fJumpSwimmingMult", 2 } });
    }

    TEST(FalloutScriptMovementTest, the_settings_keep_the_executables_defaults_for_what_the_files_do_not_say)
    {
        const Settings defaults = settingsOf({});
        EXPECT_DOUBLE_EQ(defaults.mBaseSpeed, 85);
        EXPECT_DOUBLE_EQ(defaults.mSneakMult, 0.6);
        EXPECT_DOUBLE_EQ(defaults.mNoWeaponMult, 1.1);
        EXPECT_DOUBLE_EQ(defaults.mTwoHandRiflePenalty, 0.1);
        EXPECT_DOUBLE_EQ(defaults.mWeightMax, 150);
        EXPECT_DOUBLE_EQ(defaults.mJumpHeight, 64);

        const Settings nv = newVegas();
        EXPECT_DOUBLE_EQ(nv.mBaseSpeed, 77);
        EXPECT_DOUBLE_EQ(nv.mHeavyArmorPenalty, 0.15);
        EXPECT_DOUBLE_EQ(nv.mNoWeaponMult, 1.1);
    }

    TEST(FalloutScriptMovementTest, a_fresh_character_walks_and_runs_at_the_speeds_of_the_spec)
    {
        for (const Game game : { Game::NewVegas, Game::Fallout3 })
        {
            const Settings settings = game == Game::NewVegas ? newVegas() : fallout3();
            Mover mover;
            EXPECT_NEAR(walkSpeed(game, settings, mover), 84.7, 0.05);
            EXPECT_NEAR(runSpeed(game, settings, mover), 338.8, 0.05);
            mover.mHolstered = false;
            EXPECT_NEAR(walkSpeed(game, settings, mover), 77.0, 0.05);
            EXPECT_NEAR(runSpeed(game, settings, mover), 308.0, 0.05);
        }
    }

    TEST(FalloutScriptMovementTest, the_speed_mult_scales_the_speed)
    {
        Mover mover;
        mover.mSpeedMult = 200;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 169.4, 0.05);
        mover.mSpeedMult = 150;
        mover.mHolstered = false;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 115.5, 0.05);
    }

    TEST(FalloutScriptMovementTest, sneaking_takes_the_sneak_share_of_walk_and_run)
    {
        Mover mover;
        mover.mSneaking = true;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 48.279, 0.05);
        EXPECT_NEAR(runSpeed(Game::NewVegas, newVegas(), mover), 193.12, 0.05);
        mover.mHolstered = false;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 43.89, 0.05);
    }

    TEST(FalloutScriptMovementTest, crippled_legs_slow_unless_the_limbs_are_ignored_and_the_load_is_light)
    {
        Mover mover;
        mover.mCrippledLegs = 1;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 71.995, 0.05);
        EXPECT_NEAR(runSpeed(Game::NewVegas, newVegas(), mover), 287.98, 0.05);
        mover.mCrippledLegs = 2;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 63.525, 0.05);
        EXPECT_NEAR(runSpeed(Game::NewVegas, newVegas(), mover), 254.10, 0.05);
        mover.mIgnoreCrippledLimbs = true;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 84.7, 0.05);
        mover.mOverEncumbered = true;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 63.525, 0.05);
    }

    TEST(FalloutScriptMovementTest, new_vegas_slows_heavy_armour_and_two_handed_guns)
    {
        const Settings settings = newVegas();
        Mover mover;
        mover.mArmor = ArmorClass::Heavy;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, settings, mover), 71.995, 0.05);
        EXPECT_NEAR(runSpeed(Game::NewVegas, settings, mover), 287.98, 0.05);
        mover.mArmor = ArmorClass::Medium;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, settings, mover), 78.348, 0.05);
        mover.mArmor = ArmorClass::LightOrNone;
        mover.mHolstered = false;
        for (const int type : { 5, 6, 8, 9 })
        {
            mover.mWeaponAnimationType = type;
            EXPECT_NEAR(walkSpeed(Game::NewVegas, settings, mover), 69.3, 0.05) << type;
        }
        // Two-handed melee and the one-handed types take nothing, and a holstered big gun takes nothing
        for (const int type : { 0, 1, 2, 3, 4, 7 })
        {
            mover.mWeaponAnimationType = type;
            EXPECT_NEAR(walkSpeed(Game::NewVegas, settings, mover), 77.0, 0.05) << type;
        }
        mover.mHolstered = true;
        mover.mWeaponAnimationType = 8;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, settings, mover), 84.7, 0.05);
        // Fallout 3 has neither penalty
        mover.mArmor = ArmorClass::Heavy;
        mover.mHolstered = false;
        EXPECT_NEAR(walkSpeed(Game::Fallout3, fallout3(), mover), 77.0, 0.05);
    }

    TEST(FalloutScriptMovementTest, fallout_3_slows_with_the_weight_up_to_150_and_new_vegas_does_not)
    {
        Mover mover;
        mover.mCarriedWeight = 75;
        EXPECT_NEAR(walkSpeed(Game::Fallout3, fallout3(), mover), 71.995, 0.05);
        EXPECT_NEAR(runSpeed(Game::Fallout3, fallout3(), mover), 287.98, 0.05);
        mover.mHolstered = false;
        EXPECT_NEAR(walkSpeed(Game::Fallout3, fallout3(), mover), 61.6, 0.05);
        mover.mCarriedWeight = 400;
        EXPECT_NEAR(walkSpeed(Game::Fallout3, fallout3(), mover), 46.2, 0.05);
        mover.mHolstered = true;
        EXPECT_NEAR(walkSpeed(Game::Fallout3, fallout3(), mover), 59.29, 0.05);
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 84.7, 0.05);
    }

    TEST(FalloutScriptMovementTest, over_the_carry_weight_there_is_no_running_but_no_extra_slowdown)
    {
        Mover mover;
        EXPECT_TRUE(canRun(mover));
        mover.mOverEncumbered = true;
        EXPECT_FALSE(canRun(mover));
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 84.7, 0.05);
    }

    TEST(FalloutScriptMovementTest, travel_light_multiplies_only_the_run_of_new_vegas)
    {
        Mover mover;
        EXPECT_NEAR(runSpeed(Game::NewVegas, newVegas(), mover, 1.1), 372.68, 0.05);
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), mover), 84.7, 0.05);
        EXPECT_NEAR(runSpeed(Game::Fallout3, fallout3(), mover, 1.1), 338.8, 0.05);
    }

    TEST(FalloutScriptMovementTest, swimming_is_the_walk_or_run_with_no_sneak)
    {
        Mover mover;
        mover.mSneaking = true;
        EXPECT_NEAR(swimSpeed(Game::NewVegas, newVegas(), mover, false), 84.7, 0.05);
        EXPECT_NEAR(swimSpeed(Game::NewVegas, newVegas(), mover, true), 338.8, 0.05);
    }

    TEST(FalloutScriptMovementTest, holstered_creatures_get_no_bonus_and_go_at_their_animation_speed)
    {
        Mover creature;
        creature.mHumanoid = false;
        EXPECT_NEAR(walkSpeed(Game::NewVegas, newVegas(), creature), 77.0, 0.05);
        creature.mSpeedMult = 150;
        EXPECT_DOUBLE_EQ(creatureSpeed(120, creature), 180);
        creature.mOverEncumbered = true;
        EXPECT_DOUBLE_EQ(creatureSpeed(120, creature), 0);
    }

    TEST(FalloutScriptMovementTest, a_jump_goes_64_units_by_the_size_and_twice_that_swimming)
    {
        const Settings settings = newVegas();
        EXPECT_DOUBLE_EQ(jumpHeight(settings, 1.0, false), 64);
        EXPECT_DOUBLE_EQ(jumpHeight(settings, 1.0, true), 128);
        EXPECT_DOUBLE_EQ(jumpHeight(settings, 1.1, false), 64 * 1.1);
        // A size of 0 (the player's record has no height) is a size of 1
        EXPECT_DOUBLE_EQ(jumpHeight(settings, 0.0, false), 64);
    }

    TEST(FalloutScriptMovementTest, the_launch_speed_makes_the_top_of_the_jump_its_height)
    {
        const double gravity = 686.0;
        const double speed = jumpSpeed(64, gravity);
        // v^2 / 2g is the height
        EXPECT_NEAR(speed * speed / (2 * gravity), 64, 1e-9);
        EXPECT_EQ(jumpSpeed(0, gravity), 0);
    }

    TEST(FalloutScriptMovementTest, the_share_under_water_is_capped_and_crosses_the_three_lines)
    {
        EXPECT_EQ(submergedShare(0, 10, 132), 0);
        EXPECT_DOUBLE_EQ(submergedShare(100, 0, 100), 1);
        EXPECT_DOUBLE_EQ(submergedShare(500, 0, 100), 1);
        EXPECT_LT(submergedShare(69, 0, 100), swimmingLine);
        EXPECT_GE(submergedShare(70, 0, 100), swimmingLine);
        EXPECT_GE(submergedShare(87.5, 0, 100), headUnderLine);
        EXPECT_GE(submergedShare(1, 0, 100), wadingLine);
        EXPECT_EQ(submergedShare(10, 0, 0), 0);
    }
}
