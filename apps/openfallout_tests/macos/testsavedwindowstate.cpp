#include "apps/openfallout/macos/savedwindowstate.hpp"

#include <gtest/gtest.h>

namespace OFMac
{
    namespace
    {
        // A game has no windows to reopen: after the call, AppKit is told to ignore saved window state, and the
        // value sits in the argument and registration domains, which live only as long as the process and are never
        // written to the player's preferences
        TEST(OFMacSavedWindowStateTest, after_the_call_saved_window_state_is_ignored_for_this_process_only)
        {
            const SavedWindowState state = ignoreSavedWindowState();
            EXPECT_TRUE(state.mIgnored);
            EXPECT_TRUE(savedWindowStateIgnored());
            EXPECT_TRUE(savedWindowStateRegistered());
        }

        // A "no" stored in the preferences of the program must not bring the box back
        TEST(OFMacSavedWindowStateTest, a_stored_no_does_not_win)
        {
            storeSavedWindowStateChoiceForTest(false);
            const SavedWindowState state = ignoreSavedWindowState();
            const bool ignored = savedWindowStateIgnored();
            forgetSavedWindowStateChoiceForTest();
            EXPECT_TRUE(state.mIgnored);
            EXPECT_TRUE(ignored);
        }

        TEST(OFMacSavedWindowStateTest, a_second_call_changes_nothing)
        {
            ignoreSavedWindowState();
            const SavedWindowState again = ignoreSavedWindowState();
            EXPECT_TRUE(again.mIgnored);
            EXPECT_TRUE(savedWindowStateRegistered());
        }
    }
}
