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

#include "headtracking.hpp"

#include <atomic>
#include <memory>

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
        // A sample older than this was not measured on the clock the bridge compares it with (see the motion handler)
        constexpr NSTimeInterval sLongestBelievableAge = 5.0;

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
                    // The pose is as old as its measurement, not as its delivery: a handler that ran late must not
                    // make an old pose look fresh. Core Motion stamps a sample in seconds since the system started,
                    // the clock of NSProcessInfo's systemUptime. An age outside a plausible range means the two
                    // clocks do not agree after all; the time of arrival is used then, as the best that is known.
                    const NSTimeInterval age = NSProcessInfo.processInfo.systemUptime - motion.timestamp;
                    HeadClock::time_point measured = HeadClock::now();
                    if (age > 0 && age < sLongestBelievableAge)
                        measured -= std::chrono::duration_cast<HeadClock::duration>(std::chrono::duration<double>(age));
                    box->publish(HeadRotation{ q.w, q.x, q.y, q.z }, measured);
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
}
