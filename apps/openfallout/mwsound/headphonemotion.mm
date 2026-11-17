// The native bridge of head-tracked sound: headphone motion from Core Motion, behind HeadMotionSource
// (headtracking.hpp). It is the only Objective-C++ in the sound edge; keep it thin, the logic is in headtracking.cpp.
//
// Threads:
//   - start, stop and latest: the engine's main thread (the sound manager's update and its settings changes).
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
                if (mManager != nil)
                    return;
                CMHeadphoneMotionManager* manager = [[CMHeadphoneMotionManager alloc] init];
                if (!manager.deviceMotionAvailable)
                {
                    Log(Debug::Warning) << "Head tracking: this Mac cannot read headphone motion";
                    return;
                }
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
                    box->publish(HeadRotation{ q.w, q.x, q.y, q.z }, HeadClock::now());
                };
                [manager startConnectionStatusUpdates];
                [manager startDeviceMotionUpdatesToQueue:mQueue withHandler:handler];
                mManager = manager;
            }

            void stop() override
            {
                if (mManager == nil)
                    return;
                [mManager stopDeviceMotionUpdates];
                [mManager stopConnectionStatusUpdates];
                mManager.delegate = nil;
                [mQueue waitUntilAllOperationsAreFinished];
                mManager = nil;
                mDelegate = nil;
                mQueue = nil;
                mBox->disconnect();
                Log(Debug::Info) << "Head tracking: stopped reading headphone motion";
            }

            std::optional<HeadSample> latest() const override { return mBox->latest(); }

        private:
            std::shared_ptr<HeadSampleBox> mBox;
            CMHeadphoneMotionManager* mManager = nil;
            OFHeadphoneMotionDelegate* mDelegate = nil;
            NSOperationQueue* mQueue = nil;
        };
    }

    std::unique_ptr<HeadMotionSource> makeHeadphoneMotion()
    {
        return std::make_unique<HeadphoneMotion>();
    }
}
