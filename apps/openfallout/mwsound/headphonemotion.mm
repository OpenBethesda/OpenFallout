// The native bridge of head-tracked sound: headphone motion from Core Motion, behind HeadMotionSource
// (headtracking.hpp). It is the only Objective-C++ in the sound edge; keep it thin, the logic is in headtracking.cpp.
//
// Threads:
//   - start, stop, update and latest: the engine's main thread (the sound manager's update and its settings changes).
//     When Core Motion says device motion is not available, update tries again every few seconds while the setting
//     is on (Apple does not say whether that answer depends on headphones being connected).
//   - the motion handler: mQueue, a serial operation queue of this bridge. It only publishes to the box.
//   - the delegate's connect and disconnect: a thread Core Motion chooses. They only write to the box.
// The box (HeadSampleBox) holds the latest sample under a lock, and the handler and the delegate keep it alive, so a
// call that arrives while the bridge stops is harmless.
//
// The attitude is Core Motion's own (CMDeviceMotion.attitude). No personalised spatial audio profile is used.
//
// findMotionUsageDescription touches no Core Motion: it only reads an Info.plist, so that the engine never asks for
// motion when macOS would end it for asking (see the function).

#include "headtracking.hpp"

#include <atomic>
#include <memory>

#include <dlfcn.h>
#include <libproc.h>
#include <unistd.h>

#import <CoreMotion/CoreMotion.h>
#import <Foundation/Foundation.h>

#include <components/debug/debuglog.hpp>

#if !__has_feature(objc_arc)
#error "headphonemotion.mm is built with ARC (-fobjc-arc)"
#endif

@interface OFHeadphoneMotionDelegate : NSObject <CMHeadphoneMotionManagerDelegate>
- (instancetype)initWithBox:(std::shared_ptr<OFSound::HeadSampleBox>)box;
@end

@implementation OFHeadphoneMotionDelegate
{
    std::shared_ptr<OFSound::HeadSampleBox> mBox;
}

- (instancetype)initWithBox:(std::shared_ptr<OFSound::HeadSampleBox>)box
{
    self = [super init];
    if (self != nil)
        mBox = std::move(box);
    return self;
}

- (void)headphoneMotionManagerDidConnect:(CMHeadphoneMotionManager*)manager
{
    Log(Debug::Info) << "Head tracking: headphones that report motion connected";
    mBox->connect();
}

- (void)headphoneMotionManagerDidDisconnect:(CMHeadphoneMotionManager*)manager
{
    Log(Debug::Info) << "Head tracking: headphones disconnected";
    mBox->disconnect();
}
@end

namespace OFSound
{
    namespace
    {
        const char* permissionName(CMAuthorizationStatus status)
        {
            switch (status)
            {
                case CMAuthorizationStatusNotDetermined:
                    return "not asked yet";
                case CMAuthorizationStatusRestricted:
                    return "restricted";
                case CMAuthorizationStatusDenied:
                    return "denied";
                case CMAuthorizationStatusAuthorized:
                    return "given";
            }
            return "unknown";
        }

        // How often to ask Core Motion again while device motion is not available
        constexpr std::chrono::seconds sRetryInterval{ 3 };

        class HeadphoneMotion final : public HeadMotionSource
        {
        public:
            HeadphoneMotion()
                : mBox(std::make_shared<HeadSampleBox>())
            {
            }

            ~HeadphoneMotion() override { stop(); }

            void start() override
            {
                if (mManager != nil || mWaiting)
                    return;
                if (!tryStart())
                {
                    mWaiting = true;
                    mNextTry = HeadClock::now() + sRetryInterval;
                    Log(Debug::Warning) << "Head tracking: Core Motion says headphone motion is not available now; "
                                           "trying again every "
                                        << sRetryInterval.count() << " seconds while the setting is on";
                }
            }

            void update(HeadClock::time_point now) override
            {
                if (!mWaiting || now < mNextTry)
                    return;
                if (tryStart())
                {
                    mWaiting = false;
                    Log(Debug::Info) << "Head tracking: headphone motion is available now";
                    return;
                }
                mNextTry = now + sRetryInterval;
            }

            void stop() override
            {
                mWaiting = false;
                if (mManager == nil)
                    return;
                [mManager stopDeviceMotionUpdates];
                [mManager stopConnectionStatusUpdates];
                mManager.delegate = nil;
                [mQueue waitUntilAllOperationsAreFinished];
                mManager = nil;
                mDelegate = nil;
                mQueue = nil;
                // No sample or connection message can arrive any more; a later start begins as the first did
                mBox->reset();
                Log(Debug::Info) << "Head tracking: stopped reading headphone motion";
            }

            std::optional<HeadSample> latest() const override { return mBox->latest(); }

        private:
            /// Starts reading when Core Motion says device motion is available; false (and nothing kept) otherwise
            bool tryStart()
            {
                CMHeadphoneMotionManager* manager = [[CMHeadphoneMotionManager alloc] init];
                if (!manager.deviceMotionAvailable)
                    return false;
                const CMAuthorizationStatus permission = [CMHeadphoneMotionManager authorizationStatus];
                Log(Debug::Info) << "Head tracking: reading headphone motion through Core Motion (permission "
                                 << permissionName(permission) << ")";
                if (permission == CMAuthorizationStatusDenied || permission == CMAuthorizationStatusRestricted)
                    Log(Debug::Warning) << "Head tracking: macOS does not let the game read headphone motion; "
                                           "allow it in System Settings, Privacy & Security, Motion & Fitness";

                mDelegate = [[OFHeadphoneMotionDelegate alloc] initWithBox:mBox];
                manager.delegate = mDelegate;
                mQueue = [[NSOperationQueue alloc] init];
                mQueue.name = @"OpenFallout head tracking";
                mQueue.maxConcurrentOperationCount = 1;
                mQueue.qualityOfService = NSQualityOfServiceUserInteractive;

                std::shared_ptr<HeadSampleBox> box = mBox;
                std::shared_ptr<std::atomic<bool>> errorLogged = std::make_shared<std::atomic<bool>>(false);
                CMHeadphoneDeviceMotionHandler handler = ^(CMDeviceMotion* motion, NSError* error) {
                    if (error != nil)
                    {
                        if (!errorLogged->exchange(true))
                            Log(Debug::Warning)
                                << "Head tracking: Core Motion reports " << error.localizedDescription.UTF8String;
                        return;
                    }
                    if (motion == nil)
                        return;
                    const CMQuaternion q = motion.attitude.quaternion;
                    // The pose is as old as its measurement, not as its delivery: the box works the age out from
                    // Core Motion's own time stamp (seconds on a clock whose zero the box learns by itself).
                    box->publishMeasured(HeadRotation{ q.w, q.x, q.y, q.z }, motion.timestamp, HeadClock::now());
                };
                [manager startConnectionStatusUpdates];
                [manager startDeviceMotionUpdatesToQueue:mQueue withHandler:handler];
                mManager = manager;
                return true;
            }

            std::shared_ptr<HeadSampleBox> mBox;
            CMHeadphoneMotionManager* mManager = nil;
            OFHeadphoneMotionDelegate* mDelegate = nil;
            NSOperationQueue* mQueue = nil;
            /// Started while device motion was not available: update() asks again at mNextTry
            bool mWaiting = false;
            HeadClock::time_point mNextTry;
        };
    }

    std::unique_ptr<HeadMotionSource> makeHeadphoneMotion()
    {
        return std::make_unique<HeadphoneMotion>();
    }

    MotionUsageDescription findMotionUsageDescription()
    {
        @autoreleasepool
        {
            // macOS (its privacy service, TCC) charges a request for motion to the responsible process: the app that
            // started this one from a terminal or another program, or this program itself when it was opened from
            // Finder or with `open`. It reads NSMotionUsageDescription from that app's Info.plist and ends the
            // program that asked when the key is missing, whatever this program's own bundle says. There is no
            // public call that names the responsible process; this one is part of the system library and is looked
            // up by name, so that a system without it falls back to the program's own bundle.
            NSBundle* bundle = [NSBundle mainBundle];
            using Responsible = pid_t (*)(pid_t);
            const auto responsible
                = reinterpret_cast<Responsible>(dlsym(RTLD_DEFAULT, "responsibility_get_pid_responsible_for_pid"));
            if (responsible != nullptr)
            {
                const pid_t pid = responsible(getpid());
                if (pid > 0 && pid != getpid())
                {
                    char path[PROC_PIDPATHINFO_MAXSIZE] = {};
                    if (proc_pidpath(pid, path, sizeof(path)) <= 0)
                        return { false, "the program that started the game" };
                    NSString* executable = [NSString stringWithUTF8String:path];
                    const NSRange contents = [executable rangeOfString:@"/Contents/MacOS/" options:NSBackwardsSearch];
                    // A program outside an app bundle has no Info.plist that macOS would read
                    if (contents.location == NSNotFound)
                        return { false, executable.lastPathComponent.UTF8String };
                    bundle = [NSBundle bundleWithPath:[executable substringToIndex:contents.location]];
                    if (bundle == nil)
                        return { false, executable.lastPathComponent.UTF8String };
                }
            }
            if (bundle == nil)
                return { false, "the program" };
            const id text = [bundle objectForInfoDictionaryKey:@"NSMotionUsageDescription"];
            const bool present = [text isKindOfClass:[NSString class]] && [(NSString*)text length] > 0;
            return { present, bundle.bundlePath.lastPathComponent.UTF8String };
        }
    }
}
