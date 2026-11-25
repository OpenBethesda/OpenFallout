#include <components/falloutscript/actorvalues.hpp>
#include <components/falloutscript/processlevels.hpp>

#include <gtest/gtest.h>

namespace
{
    using namespace FalloutScript;

    ProcessInputs inputs(CellLoad cell, bool has3D, bool teammate = false, bool bulk = false, bool keeps = false)
    {
        ProcessInputs result;
        result.mCell = cell;
        result.mHas3D = has3D;
        result.mTeammate = teammate;
        result.mBulkLoading = bulk;
        result.mCellKeepsActors = keeps;
        return result;
    }

    TEST(FalloutScriptProcessLevelsTest, an_actor_with_its_3d_is_high_in_an_attached_cell_and_middle_high_in_a_loaded_one)
    {
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Attached, true)), ProcessLevel::High);
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Loaded, true)), ProcessLevel::MiddleHigh);
    }

    TEST(FalloutScriptProcessLevelsTest, a_follower_or_a_bulk_load_counts_as_interesting_without_3d)
    {
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Attached, false, true)), ProcessLevel::High);
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Attached, false, false, true)), ProcessLevel::High);
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Loaded, false, true)), ProcessLevel::MiddleHigh);
    }

    TEST(FalloutScriptProcessLevelsTest, an_actor_without_3d_in_a_loaded_or_attached_cell_is_middle_low)
    {
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Attached, false)), ProcessLevel::MiddleLow);
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::Loaded, false)), ProcessLevel::MiddleLow);
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::None, false, false, false, true)), ProcessLevel::MiddleLow);
    }

    TEST(FalloutScriptProcessLevelsTest, an_actor_with_no_loaded_cell_is_low_whatever_else_it_has)
    {
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::None, false)), ProcessLevel::Low);
        // The cell-state helper of the executable says no for a null cell, so a follower there is low too
        EXPECT_EQ(desiredProcessLevel(inputs(CellLoad::None, true, true, true)), ProcessLevel::Low);
    }

    TEST(FalloutScriptProcessLevelsTest, the_levels_have_the_names_and_numbers_of_the_executables)
    {
        EXPECT_EQ(static_cast<int>(ProcessLevel::High), 0);
        EXPECT_EQ(static_cast<int>(ProcessLevel::Low), 3);
        EXPECT_EQ(static_cast<int>(ProcessLevel::None), -1);
        EXPECT_EQ(processLevelName(ProcessLevel::MiddleHigh), "Middle High");
        EXPECT_EQ(processLevelName(ProcessLevel::None), "None");
    }

    TEST(FalloutScriptProcessLevelsTest, dying_and_dead_are_dead_and_the_essential_down_state_only_when_not_strict)
    {
        EXPECT_TRUE(isDeadState(LifeState::Dying, true));
        EXPECT_TRUE(isDeadState(LifeState::Dead, true));
        EXPECT_FALSE(isDeadState(LifeState::EssentialDown, true));
        EXPECT_TRUE(isDeadState(LifeState::EssentialDown, false));
        EXPECT_FALSE(isDeadState(LifeState::Unconscious, false));
        EXPECT_FALSE(isDeadState(LifeState::Alive, false));
    }

    TEST(FalloutScriptProcessLevelsTest, an_actor_dies_under_one_point_of_health)
    {
        EXPECT_TRUE(outOfHealth(0.99));
        EXPECT_TRUE(outOfHealth(0.4));
        EXPECT_TRUE(outOfHealth(-3));
        EXPECT_FALSE(outOfHealth(1.0));
        EXPECT_FALSE(outOfHealth(150));
    }
}
