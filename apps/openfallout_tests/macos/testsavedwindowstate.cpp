#include "apps/openfallout/macos/savedwindowstate.hpp"

#include <gtest/gtest.h>

namespace OFMac
{
    namespace
    {
        // A game has no windows to reopen: after the call, AppKit is told to ignore saved window state, and the
        // value sits in the registration domain, which lives only as long as the process and is never written to the
        // player's preferences
        TEST(OFMacSavedWindowStateTest, after_the_call_saved_window_state_is_ignored_for_this_process_only)
        {
            const SavedWindowState state = ignoreSavedWindowState();
            EXPECT_TRUE(state.mIgnored);
            EXPECT_TRUE(savedWindowStateIgnored());
            EXPECT_TRUE(savedWindowStateRegistered());
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
