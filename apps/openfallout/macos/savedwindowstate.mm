// The native bridge of start-up: AppKit's saved window state (savedwindowstate.hpp).
//
// AppKit reopens the windows of the last run when an application object finishes launching, and when that run ended
// early it asks first, in a box on the screen. The default ApplePersistenceIgnoreState turns both off. SDL registers it
// too, but only after the application object it makes has finished launching (or from its delegate, later still), so
// the engine registers it itself before SDL starts.

#include "savedwindowstate.hpp"

#import <AppKit/AppKit.h>

#if !__has_feature(objc_arc)
#error "savedwindowstate.mm is built with ARC (-fobjc-arc)"
#endif

namespace OFMac
{
    namespace
    {
        NSString* const sKey = @"ApplePersistenceIgnoreState";
    }

    SavedWindowState ignoreSavedWindowState()
    {
        @autoreleasepool
        {
            // NSApp is only read here: nothing makes the application object
            const bool existed = NSApp != nil;
            [[NSUserDefaults standardUserDefaults] registerDefaults:@{ sKey : @YES }];
            return { savedWindowStateIgnored(), existed };
        }
    }

    bool savedWindowStateIgnored()
    {
        @autoreleasepool
        {
            return [[NSUserDefaults standardUserDefaults] boolForKey:sKey];
        }
    }

    bool savedWindowStateRegistered()
    {
        @autoreleasepool
        {
            NSDictionary* registered =
                [[NSUserDefaults standardUserDefaults] volatileDomainForName:NSRegistrationDomain];
            const id value = registered[sKey];
            return [value isKindOfClass:[NSNumber class]] && [(NSNumber*)value boolValue];
        }
    }
}
