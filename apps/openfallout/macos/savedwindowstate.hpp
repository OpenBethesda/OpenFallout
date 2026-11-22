#ifndef OPENFALLOUT_MACOS_SAVEDWINDOWSTATE_H
#define OPENFALLOUT_MACOS_SAVEDWINDOWSTATE_H

namespace OFMac
{
    /// What ignoreSavedWindowState found and left
    struct SavedWindowState
    {
        /// The default reads true after the call
        bool mIgnored = false;
        /// The application object already existed when the call was made: AppKit may have read the default before
        bool mApplicationExisted = false;
    };

    /// Tells AppKit that this process restores no saved window state (the default ApplePersistenceIgnoreState), so
    /// that macOS never offers to reopen the windows of a run that ended early. A game has no windows to reopen. The
    /// value goes to two domains of the user defaults that live as long as the process and are never written to the
    /// player's preferences: the argument domain, which is searched first, so that a "no" stored in the preferences
    /// does not win (the arguments already there are kept), and the registration domain, where SDL puts it later.
    /// Call it first in main, before SDL or anything else makes the application object. Native bridge
    /// (savedwindowstate.mm).
    SavedWindowState ignoreSavedWindowState();

    /// Whether the default reads true in this process now
    bool savedWindowStateIgnored();

    /// Whether both domains hold it (where ignoreSavedWindowState puts it)
    bool savedWindowStateRegistered();

    /// For tests: stores the opposite choice in the preferences of this program, as a player's `defaults write` would,
    /// and takes it out again
    void storeSavedWindowStateChoiceForTest(bool ignore);
    void forgetSavedWindowStateChoiceForTest();
}

#endif
