#ifndef GAME_SOUND_HEADTRACKING_H
#define GAME_SOUND_HEADTRACKING_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

#include <osg/Vec3f>

namespace OFSound
{
    // Head-tracked sound: headphones that report motion turn the sound listener, so that a sound stays fixed in the
    // world when the player turns the head. See docs/openfallout/head-tracking.md.
    //
    // The head frame used here is the engine's camera frame: x towards the right ear, y out of the face, z up out of
    // the crown. Apple's figure of the axes of motion-capable headphones and the reports of others' use agree with
    // it; the owner's listening check confirms it.

    /// A rotation, as a unit quaternion (w, x, y, z)
    struct HeadRotation
    {
        double mW = 1;
        double mX = 0;
        double mY = 0;
        double mZ = 0;

        /// The rotation by `radians` about the axis (x, y, z), counter-clockwise when the axis points at the viewer
        static HeadRotation fromAxisAngle(double x, double y, double z, double radians);
    };

    /// `a * b` turns by `b` first and then by `a`; in a frame that turns with the head, it is `a` and then `b`
    HeadRotation operator*(const HeadRotation& a, const HeadRotation& b);

    /// How far `attitude` is turned from `reference`, in the head frame of the reference
    HeadRotation relativeTo(const HeadRotation& reference, const HeadRotation& attitude);

    /// The direction the listener looks along and its up, in the engine's world (x east, y north, z up)
    struct ListenerAxes
    {
        osg::Vec3f mForward;
        osg::Vec3f mUp;
    };

    /// The listener of a camera looking along `forward` with `up`, when the head is turned by `head` from straight
    /// ahead: the head turns relative to the camera, so a sound stays where it is in the world
    ListenerAxes turnListener(const osg::Vec3f& forward, const osg::Vec3f& up, const HeadRotation& head);

    using HeadClock = std::chrono::steady_clock;

    /// One reading of the headphones
    struct HeadSample
    {
        /// The head's attitude in the source's own reference frame
        HeadRotation mAttitude;
        /// When it arrived
        HeadClock::time_point mTime;
        /// Samples since the box was made, for the rate in the log
        std::uint64_t mCount = 0;
        /// Goes up each time headphones connect; a new connection sets straight ahead again
        std::uint32_t mConnection = 0;
    };

    /// The hand-over from the thread that receives motion to the thread that turns the listener: the latest sample
    /// under a lock. Every member may be called from any thread.
    class HeadSampleBox
    {
    public:
        /// Headphones that report motion connected (a second call while connected does nothing)
        void connect();
        /// They went; no sample is handed out until the next connection
        void disconnect();
        /// A new attitude; one without a connect before it counts as a connection
        void publish(const HeadRotation& attitude, HeadClock::time_point time);
        /// The latest sample of the current connection
        std::optional<HeadSample> latest() const;

    private:
        mutable std::mutex mMutex;
        std::optional<HeadSample> mSample;
        bool mConnected = false;
        std::uint32_t mConnection = 0;
        std::uint64_t mCount = 0;
    };

    /// Where the head's motion comes from. start, stop and latest are called on the thread that turns the listener
    /// (the engine's main thread, in the sound manager's update).
    class HeadMotionSource
    {
    public:
        virtual ~HeadMotionSource() = default;
        /// Begin reading motion (also when no headphones are connected yet)
        virtual void start() = 0;
        /// Stop reading; after it no new sample arrives
        virtual void stop() = 0;
        /// The latest sample while headphones that report motion are connected and delivering
        virtual std::optional<HeadSample> latest() const = 0;
    };

    /// A source that hands out what is published to its box, for tests
    class StandInHeadMotion : public HeadMotionSource
    {
    public:
        void start() override { mStarted = true; }
        void stop() override { mStarted = false; }
        std::optional<HeadSample> latest() const override { return mBox.latest(); }
        HeadSampleBox& box() { return mBox; }
        bool isStarted() const { return mStarted; }

    private:
        HeadSampleBox mBox;
        bool mStarted = false;
    };

    /// A sample older than this means the headphones stopped delivering: the listener is the camera's again
    constexpr std::chrono::milliseconds sHeadSampleStale{ 500 };

    /// Turns the listener by the head's motion while the setting is on and a source delivers. Straight ahead is the
    /// pose of the first sample after tracking starts, after headphones connect, and after recentre().
    class HeadTracker
    {
    public:
        explicit HeadTracker(std::unique_ptr<HeadMotionSource> source);
        ~HeadTracker();

        HeadTracker(const HeadTracker&) = delete;
        HeadTracker& operator=(const HeadTracker&) = delete;

        /// The setting: on starts the source and sets straight ahead again, off stops it
        void setEnabled(bool enabled);
        bool isEnabled() const { return mEnabled; }

        /// The next sample becomes straight ahead
        void recentre();

        /// The head's turn from straight ahead; none when the setting is off, no headphones deliver, or the latest
        /// sample is older than sHeadSampleStale
        std::optional<HeadRotation> rotation(HeadClock::time_point now);

        /// The listener for the camera's axes: exactly the camera's when rotation() gives none
        ListenerAxes listener(const osg::Vec3f& forward, const osg::Vec3f& up, HeadClock::time_point now);

    private:
        void setDelivering(bool delivering);
        void logRate(const HeadSample& sample);

        std::unique_ptr<HeadMotionSource> mSource;
        bool mEnabled = false;
        bool mDelivering = false;
        /// Straight ahead, and the sample it was taken from
        std::optional<HeadSample> mReference;
        /// The last sample counted for the rate in the log
        std::optional<HeadSample> mRateStart;
    };

    /// The headphones' motion through Core Motion (the native bridge, headphonemotion.mm)
    std::unique_ptr<HeadMotionSource> makeHeadphoneMotion();
}

#endif
