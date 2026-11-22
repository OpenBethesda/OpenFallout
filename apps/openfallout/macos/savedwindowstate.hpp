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
    /// value goes to the registration domain of the user defaults: it lives as long as the process and is never
    /// written to the player's preferences. Call it first in main, before SDL or anything else makes the application
    /// object. Native bridge (savedwindowstate.mm).
    SavedWindowState ignoreSavedWindowState();

    /// Whether the default reads true in this process now
    bool savedWindowStateIgnored();

    /// Whether the registration domain holds it (where ignoreSavedWindowState puts it)
    bool savedWindowStateRegistered();
}

#endif
