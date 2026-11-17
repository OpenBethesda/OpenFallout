#include "apps/openfallout/mwsound/headtracking.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <osg/Vec3f>

namespace OFSound
{
    namespace
    {
        using namespace std::chrono_literals;
        using Clock = std::chrono::steady_clock;

        constexpr double sDegree = 3.14159265358979323846 / 180.0;
        constexpr float sClose = 1e-5f;

        // The engine's world: x east, y north, z up. A camera looking north and level.
        const osg::Vec3f sNorth(0, 1, 0);
        const osg::Vec3f sEast(1, 0, 0);
        const osg::Vec3f sWest(-1, 0, 0);
        const osg::Vec3f sUp(0, 0, 1);

        // Turns of the head in its own frame: x to the right ear, y out of the face, z up
        HeadRotation yawLeft(double degrees)
        {
            return HeadRotation::fromAxisAngle(0, 0, 1, degrees * sDegree);
        }

        HeadRotation pitchUp(double degrees)
        {
            return HeadRotation::fromAxisAngle(1, 0, 0, degrees * sDegree);
        }

        HeadRotation rollRight(double degrees)
        {
            return HeadRotation::fromAxisAngle(0, 1, 0, degrees * sDegree);
        }

        void expectNear(const osg::Vec3f& actual, const osg::Vec3f& expected)
        {
            EXPECT_NEAR(actual.x(), expected.x(), sClose) << "x";
            EXPECT_NEAR(actual.y(), expected.y(), sClose) << "y";
            EXPECT_NEAR(actual.z(), expected.z(), sClose) << "z";
        }

        // Where a sound is heard, as OpenAL Soft places it: its direction in the listener's own axes
        // (right = at x up, the right-handed rule of OpenAL, which the engine's world also follows)
        struct Heard
        {
            float mRight;
            float mAhead;
            float mAbove;
        };

        Heard heard(const ListenerAxes& listener, const osg::Vec3f& towardsSound)
        {
            const osg::Vec3f right = listener.mForward ^ listener.mUp;
            return { towardsSound * right, towardsSound * listener.mForward, towardsSound * listener.mUp };
        }

        // A stand-in source that counts its starts and stops and hands out what a test publishes
        class CountingStandIn : public StandInHeadMotion
        {
        public:
            int mStarts = 0;
            int mStops = 0;
            int mUpdates = 0;

            void update(HeadClock::time_point now) override
            {
                ++mUpdates;
                StandInHeadMotion::update(now);
            }

            void start() override
            {
                ++mStarts;
                StandInHeadMotion::start();
            }

            void stop() override
            {
                ++mStops;
                StandInHeadMotion::stop();
            }
        };

        struct TrackerTest : ::testing::Test
        {
            CountingStandIn* mSource = nullptr;
            std::unique_ptr<HeadTracker> mTracker;
            Clock::time_point mNow = Clock::time_point{} + 100s;

            TrackerTest()
            {
                auto source = std::make_unique<CountingStandIn>();
                mSource = source.get();
                mTracker = std::make_unique<HeadTracker>(std::move(source));
            }

            void publish(const HeadRotation& attitude)
            {
                mNow += 40ms;
                mSource->box().publish(attitude, mNow);
            }

            ListenerAxes listen() { return mTracker->listener(sNorth, sUp, mNow); }

            void expectCamera(const ListenerAxes& axes)
            {
                // Exactly the camera's, not merely close
                EXPECT_EQ(axes.mForward, sNorth);
                EXPECT_EQ(axes.mUp, sUp);
            }
        };
    }

    // The orientation mathematics

    TEST(OFSoundHeadTrackingTest, straight_ahead_leaves_the_camera_as_it_is)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, HeadRotation{});
        expectNear(axes.mForward, sNorth);
        expectNear(axes.mUp, sUp);
    }

    TEST(OFSoundHeadTrackingTest, head_turned_left_turns_the_sound_field_right)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, yawLeft(90));
        expectNear(axes.mForward, sWest);
        expectNear(axes.mUp, sUp);
        // A sound straight ahead of the camera is now heard in the right ear
        const Heard ahead = heard(axes, sNorth);
        EXPECT_NEAR(ahead.mRight, 1, sClose);
        EXPECT_NEAR(ahead.mAhead, 0, sClose);
    }

    TEST(OFSoundHeadTrackingTest, a_small_turn_moves_the_sound_by_the_same_angle)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, yawLeft(30));
        const Heard ahead = heard(axes, sNorth);
        EXPECT_NEAR(std::atan2(ahead.mRight, ahead.mAhead) / sDegree, 30, 1e-3);
    }

    TEST(OFSoundHeadTrackingTest, head_turned_right_turns_the_sound_field_left)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, yawLeft(-90));
        expectNear(axes.mForward, sEast);
        EXPECT_NEAR(heard(axes, sNorth).mRight, -1, sClose);
    }

    TEST(OFSoundHeadTrackingTest, looking_up_puts_a_sound_ahead_below_the_listener)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, pitchUp(30));
        const float c = std::cos(30 * sDegree);
        const float s = std::sin(30 * sDegree);
        expectNear(axes.mForward, osg::Vec3f(0, c, s));
        expectNear(axes.mUp, osg::Vec3f(0, -s, c));
        EXPECT_LT(heard(axes, sNorth).mAbove, 0);
    }

    TEST(OFSoundHeadTrackingTest, tilting_the_head_right_lifts_a_sound_on_the_right)
    {
        const ListenerAxes axes = turnListener(sNorth, sUp, rollRight(30));
        const float c = std::cos(30 * sDegree);
        const float s = std::sin(30 * sDegree);
        expectNear(axes.mForward, sNorth);
        expectNear(axes.mUp, osg::Vec3f(s, 0, c));
        // A sound due east (the camera's right) is now above the right ear
        const Heard right = heard(axes, sEast);
        EXPECT_GT(right.mRight, 0);
        EXPECT_GT(right.mAbove, 0);
    }

    TEST(OFSoundHeadTrackingTest, turn_then_look_up_combines_in_the_head_frame)
    {
        // The head turns left by 90 degrees and then looks up by 30 in its own frame
        const ListenerAxes axes = turnListener(sNorth, sUp, yawLeft(90) * pitchUp(30));
        const float c = std::cos(30 * sDegree);
        const float s = std::sin(30 * sDegree);
        expectNear(axes.mForward, osg::Vec3f(-c, 0, s));
        expectNear(axes.mUp, osg::Vec3f(s, 0, c));
    }

    TEST(OFSoundHeadTrackingTest, the_head_turns_relative_to_the_camera_not_the_world)
    {
        // Camera looking east: the head turned left looks north
        ListenerAxes axes = turnListener(sEast, sUp, yawLeft(90));
        expectNear(axes.mForward, sNorth);
        expectNear(axes.mUp, sUp);

        // Camera looking north and down by 45 degrees: the head turned left looks west, level with the camera's tilt
        const float h = std::sqrt(0.5f);
        axes = turnListener(osg::Vec3f(0, h, -h), osg::Vec3f(0, h, h), yawLeft(90));
        expectNear(axes.mForward, sWest);
        expectNear(axes.mUp, osg::Vec3f(0, h, h));
    }

    TEST(OFSoundHeadTrackingTest, the_axes_stay_unit_and_square)
    {
        const ListenerAxes axes
            = turnListener(osg::Vec3f(0.3f, 0.9f, 0.1f), osg::Vec3f(0, -0.1f, 1), yawLeft(37) * rollRight(-12));
        EXPECT_NEAR(axes.mForward.length(), 1, sClose);
        EXPECT_NEAR(axes.mUp.length(), 1, sClose);
        EXPECT_NEAR(axes.mForward * axes.mUp, 0, sClose);
    }

    TEST(OFSoundHeadTrackingTest, the_change_from_a_reference_is_in_the_head_frame)
    {
        // Straight ahead was taken with the head turned 20 degrees left and tilted; turning 90 further left
        // reads as 90 left
        const HeadRotation reference = yawLeft(20) * rollRight(10);
        const HeadRotation now = reference * yawLeft(90);
        const ListenerAxes axes = turnListener(sNorth, sUp, relativeTo(reference, now));
        expectNear(axes.mForward, sWest);
        expectNear(axes.mUp, sUp);
    }

    // The tracker: the setting, the device and straight ahead

    TEST_F(TrackerTest, the_listener_turns_from_a_stand_in_source_through_the_interface)
    {
        mTracker->setEnabled(true);
        EXPECT_EQ(mSource->mStarts, 1);
        // The first sample after tracking starts is straight ahead, whatever the source reports
        publish(yawLeft(-40));
        expectCamera(listen());
        publish(yawLeft(50));
        const ListenerAxes axes = listen();
        expectNear(axes.mForward, sWest);
        expectNear(axes.mUp, sUp);
    }

    TEST_F(TrackerTest, the_setting_off_leaves_the_listener_exactly_as_the_camera)
    {
        publish(HeadRotation{});
        publish(yawLeft(90));
        expectCamera(listen());
        EXPECT_EQ(mSource->mStarts, 0);
        EXPECT_FALSE(mTracker->rotation(mNow).has_value());
    }

    TEST_F(TrackerTest, no_device_leaves_the_listener_exactly_as_the_camera)
    {
        mTracker->setEnabled(true);
        expectCamera(listen());
    }

    TEST_F(TrackerTest, a_device_gone_mid_game_gives_the_camera_back)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        expectNear(listen().mForward, sWest);
        mSource->box().disconnect();
        expectCamera(listen());
    }

    TEST_F(TrackerTest, motion_that_stops_arriving_gives_the_camera_back)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        mNow += sHeadSampleStale + 1ms;
        expectCamera(listen());
        // It turns again when samples come back, from the pose they come back with
        publish(yawLeft(90));
        expectCamera(listen());
        publish(yawLeft(180));
        expectNear(listen().mForward, sWest);
    }

    TEST_F(TrackerTest, a_gap_on_one_connection_sets_straight_ahead_again)
    {
        // The headphones leave the ears and come back without a disconnect; the tracker does not look during the gap
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        expectNear(listen().mForward, sWest);
        mNow += sHeadSampleStale + 1ms;
        publish(yawLeft(-30));
        expectCamera(listen());
        publish(yawLeft(60));
        expectNear(listen().mForward, sWest);
    }

    TEST_F(TrackerTest, a_short_pause_between_samples_keeps_straight_ahead)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        mNow += sHeadSampleStale - 50ms;
        publish(yawLeft(90));
        expectNear(listen().mForward, sWest);
    }

    TEST_F(TrackerTest, a_late_sample_after_a_disconnect_is_not_a_connection)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(45));
        mSource->box().disconnect();
        // Core Motion's queue delivers one more sample after the disconnect message
        publish(yawLeft(90));
        expectCamera(listen());
        EXPECT_FALSE(mTracker->rotation(mNow).has_value());
        // The real reconnection: straight ahead is its first sample
        mSource->box().connect();
        publish(yawLeft(30));
        expectCamera(listen());
        publish(yawLeft(120));
        expectNear(listen().mForward, sWest);
    }

    TEST_F(TrackerTest, bad_numbers_never_reach_the_listener)
    {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        constexpr double inf = std::numeric_limits<double>::infinity();
        const HeadRotation bad[] = {
            { nan, 0, 0, 0 },
            { 1, 0, nan, 0 },
            { inf, 0, 0, 0 },
            { 1, 0, 0, -inf },
            { 0, 0, 0, 0 },
            { 2, 0, 0, 0 },
            { 0.5, 0, 0, 0.5 },
        };
        mTracker->setEnabled(true);
        for (const HeadRotation& q : bad)
        {
            publish(q);
            expectCamera(listen());
            EXPECT_FALSE(mTracker->rotation(mNow).has_value());
            EXPECT_FALSE(mTracker->turnedListener(sNorth, sUp, mNow).has_value());
        }
        // A good sample afterwards works: it is straight ahead, and the next turns the listener
        publish(yawLeft(10));
        expectCamera(listen());
        publish(yawLeft(100));
        expectNear(listen().mForward, sWest);
        // A bad sample in the middle is as if none came: the listener keeps the last good turn
        for (const HeadRotation& q : bad)
        {
            publish(q);
            expectNear(listen().mForward, sWest);
        }
    }

    // Between the sound manager's updates only the listener's direction is set, and only when the head turns it

    TEST_F(TrackerTest, between_updates_nothing_is_set_with_the_setting_off)
    {
        publish(HeadRotation{});
        publish(yawLeft(90));
        EXPECT_FALSE(mTracker->turnedListener(sNorth, sUp, mNow).has_value());
        EXPECT_EQ(mSource->mUpdates, 0);
    }

    TEST_F(TrackerTest, between_updates_nothing_is_set_without_a_device)
    {
        mTracker->setEnabled(true);
        EXPECT_FALSE(mTracker->turnedListener(sNorth, sUp, mNow).has_value());
        publish(HeadRotation{});
        mNow += sHeadSampleStale + 1ms;
        EXPECT_FALSE(mTracker->turnedListener(sNorth, sUp, mNow).has_value());
    }

    TEST_F(TrackerTest, between_updates_the_direction_follows_the_head)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        std::optional<ListenerAxes> axes = mTracker->turnedListener(sNorth, sUp, mNow);
        ASSERT_TRUE(axes.has_value());
        expectCamera(*axes);
        publish(yawLeft(90));
        axes = mTracker->turnedListener(sNorth, sUp, mNow);
        ASSERT_TRUE(axes.has_value());
        expectNear(axes->mForward, sWest);
        expectNear(axes->mUp, sUp);
    }

    TEST_F(TrackerTest, the_source_is_given_a_turn_on_the_main_thread_only_while_on)
    {
        listen();
        EXPECT_EQ(mSource->mUpdates, 0);
        mTracker->setEnabled(true);
        listen();
        mTracker->turnedListener(sNorth, sUp, mNow);
        EXPECT_EQ(mSource->mUpdates, 2);
        mTracker->setEnabled(false);
        listen();
        EXPECT_EQ(mSource->mUpdates, 2);
    }

    TEST_F(TrackerTest, headphones_connected_again_set_straight_ahead_again)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        mSource->box().disconnect();
        mSource->box().connect();
        publish(yawLeft(90));
        expectCamera(listen());
        publish(yawLeft(180));
        expectNear(listen().mForward, sWest);
    }

    TEST_F(TrackerTest, recentre_makes_the_current_pose_straight_ahead)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        mTracker->recentre();
        expectCamera(listen());
        publish(yawLeft(90));
        expectNear(listen().mForward, sNorth);
        publish(HeadRotation{});
        expectNear(listen().mForward, sEast);
    }

    TEST_F(TrackerTest, switching_off_stops_the_source_and_on_again_recentres)
    {
        mTracker->setEnabled(true);
        publish(HeadRotation{});
        expectCamera(listen());
        publish(yawLeft(90));
        mTracker->setEnabled(false);
        EXPECT_EQ(mSource->mStops, 1);
        expectCamera(listen());
        mTracker->setEnabled(true);
        EXPECT_EQ(mSource->mStarts, 2);
        publish(yawLeft(90));
        expectCamera(listen());
        // Switching on twice does not start the source twice
        mTracker->setEnabled(true);
        EXPECT_EQ(mSource->mStarts, 2);
    }

    TEST(OFSoundHeadTrackingTest, the_tracker_stops_its_source_when_it_goes)
    {
        struct Watched : StandInHeadMotion
        {
            int& mStops;
            explicit Watched(int& stops)
                : mStops(stops)
            {
            }
            void stop() override { ++mStops; }
        };
        int stops = 0;
        {
            HeadTracker tracker(std::make_unique<Watched>(stops));
            tracker.setEnabled(true);
        }
        EXPECT_EQ(stops, 1);
    }

    // The hand-over from the thread that receives motion

    TEST(OFSoundHeadSampleBoxTest, nothing_before_a_sample_and_after_a_disconnect)
    {
        HeadSampleBox box;
        EXPECT_FALSE(box.latest().has_value());
        box.connect();
        EXPECT_FALSE(box.latest().has_value());
        box.publish(yawLeft(10), Clock::time_point{} + 1s);
        ASSERT_TRUE(box.latest().has_value());
        EXPECT_EQ(box.latest()->mCount, 1u);
        box.disconnect();
        EXPECT_FALSE(box.latest().has_value());
    }

    TEST(OFSoundHeadSampleBoxTest, a_sample_without_a_connect_counts_as_one_connection)
    {
        HeadSampleBox box;
        box.publish(yawLeft(10), Clock::time_point{} + 1s);
        const std::uint32_t first = box.latest()->mConnection;
        box.connect();
        box.publish(yawLeft(10), Clock::time_point{} + 1s + 40ms);
        EXPECT_EQ(box.latest()->mConnection, first);
        box.disconnect();
        box.connect();
        box.publish(yawLeft(10), Clock::time_point{} + 1s + 80ms);
        EXPECT_EQ(box.latest()->mConnection, first + 1);
    }

    TEST(OFSoundHeadSampleBoxTest, after_a_disconnect_only_a_connect_starts_a_connection)
    {
        HeadSampleBox box;
        box.connect();
        box.publish(yawLeft(10), Clock::time_point{} + 1s);
        const std::uint32_t first = box.latest()->mConnection;
        box.disconnect();
        box.publish(yawLeft(20), Clock::time_point{} + 2s);
        EXPECT_FALSE(box.latest().has_value());
        box.connect();
        box.publish(yawLeft(30), Clock::time_point{} + 3s);
        ASSERT_TRUE(box.latest().has_value());
        EXPECT_EQ(box.latest()->mConnection, first + 1);
        EXPECT_NEAR(box.latest()->mAttitude.mZ, yawLeft(30).mZ, 1e-12);
    }

    TEST(OFSoundHeadSampleBoxTest, reset_forgets_the_disconnect)
    {
        // A source that stops and starts again may not be told that headphones already worn are connected
        HeadSampleBox box;
        box.connect();
        box.disconnect();
        box.reset();
        box.publish(yawLeft(10), Clock::time_point{} + 1s);
        EXPECT_TRUE(box.latest().has_value());
    }

    TEST(OFSoundHeadSampleBoxTest, a_gap_in_the_samples_counts_as_a_new_connection)
    {
        HeadSampleBox box;
        box.publish(yawLeft(10), Clock::time_point{} + 1s);
        const std::uint32_t first = box.latest()->mConnection;
        box.publish(yawLeft(10), Clock::time_point{} + 1s + sHeadSampleStale);
        EXPECT_EQ(box.latest()->mConnection, first);
        box.publish(yawLeft(10), Clock::time_point{} + 1s + 2 * sHeadSampleStale + 1ms);
        EXPECT_EQ(box.latest()->mConnection, first + 1);
    }

    // The source stamps a sample on its own clock, whose zero the engine does not know. 7000 s here: any number works.
    TEST(OFSoundHeadSampleBoxTest, a_sample_is_as_old_as_its_measurement_whatever_the_sources_clock)
    {
        for (const double sourceZero : { 7000.0, -3.5e6, 0.0 })
        {
            HeadSampleBox box;
            const Clock::time_point start = Clock::time_point{} + 50s;
            // delivered 4 ms after each measurement: these are fresh, at the time they arrived (less nothing)
            box.publishMeasured(yawLeft(10), sourceZero + 0.000, start + 4ms);
            box.publishMeasured(yawLeft(11), sourceZero + 0.040, start + 44ms);
            ASSERT_TRUE(box.latest().has_value());
            EXPECT_EQ(box.latest()->mTime, start + 44ms) << sourceZero;
            // measured 40 ms after the last one, but its handler ran 800 ms late: it is 800 ms old, not fresh
            box.publishMeasured(yawLeft(12), sourceZero + 0.080, start + 884ms);
            const auto age = (start + 884ms) - box.latest()->mTime;
            EXPECT_NEAR(std::chrono::duration<double>(age).count(), 0.8, 0.002) << sourceZero;
            EXPECT_GT(age, sHeadSampleStale);
        }
    }

    TEST(OFSoundHeadSampleBoxTest, a_sample_delayed_for_many_seconds_is_old_too)
    {
        HeadSampleBox box;
        const Clock::time_point start = Clock::time_point{} + 50s;
        box.publishMeasured(yawLeft(10), 100.000, start);
        // the process stood still for a minute; the sample waiting in the queue is a minute old
        box.publishMeasured(yawLeft(11), 100.040, start + 60s);
        EXPECT_GT((start + 60s) - box.latest()->mTime, 59s);
        // samples measured now are fresh again
        box.publishMeasured(yawLeft(12), 160.050, start + 60s + 50ms);
        EXPECT_EQ(box.latest()->mTime, start + 60s + 50ms);
    }

    TEST(OFSoundHeadSampleBoxTest, a_new_connection_learns_the_sources_clock_again)
    {
        HeadSampleBox box;
        const Clock::time_point start = Clock::time_point{} + 50s;
        box.connect();
        box.publishMeasured(yawLeft(10), 100.0, start);
        box.disconnect();
        box.connect();
        // the source's clock stood still while the system slept: the same delivery now looks 30 s "older"
        box.publishMeasured(yawLeft(11), 100.5, start + 30s + 500ms);
        ASSERT_TRUE(box.latest().has_value());
        EXPECT_EQ(box.latest()->mTime, start + 30s + 500ms);
    }

    TEST(OFSoundHeadSampleBoxTest, clocks_that_run_slightly_apart_do_not_make_samples_old)
    {
        HeadSampleBox box;
        const Clock::time_point start = Clock::time_point{} + 50s;
        // an hour of samples every 100 ms from a source whose clock runs 200 parts in a million slow
        Clock::time_point arrival = start;
        for (int i = 0; i < 36000; ++i)
        {
            arrival = start + std::chrono::milliseconds(100 * i);
            box.publishMeasured(yawLeft(10), 0.1 * i * (1.0 - 200e-6), arrival);
        }
        EXPECT_LT(arrival - box.latest()->mTime, 5ms);
    }

    TEST(OFSoundHeadSampleBoxTest, a_time_that_is_not_a_number_counts_as_the_time_of_arrival)
    {
        HeadSampleBox box;
        const Clock::time_point start = Clock::time_point{} + 50s;
        box.publishMeasured(yawLeft(10), std::numeric_limits<double>::quiet_NaN(), start);
        ASSERT_TRUE(box.latest().has_value());
        EXPECT_EQ(box.latest()->mTime, start);
    }

    TEST(OFSoundHeadSampleBoxTest, an_unusable_attitude_is_dropped)
    {
        EXPECT_TRUE(isUsableAttitude(yawLeft(33)));
        EXPECT_TRUE(isUsableAttitude(HeadRotation{ 1.001, 0, 0, 0 }));
        EXPECT_FALSE(isUsableAttitude(HeadRotation{ std::numeric_limits<double>::quiet_NaN(), 0, 0, 0 }));
        EXPECT_FALSE(isUsableAttitude(HeadRotation{ 1, std::numeric_limits<double>::infinity(), 0, 0 }));
        EXPECT_FALSE(isUsableAttitude(HeadRotation{ 1.2, 0, 0, 0 }));
        EXPECT_FALSE(isUsableAttitude(HeadRotation{ 0, 0, 0, 0 }));

        HeadSampleBox box;
        box.publish(HeadRotation{ 0, 0, 0, 0 }, Clock::time_point{} + 1s);
        EXPECT_FALSE(box.latest().has_value());
        box.publish(yawLeft(10), Clock::time_point{} + 2s);
        ASSERT_TRUE(box.latest().has_value());
        EXPECT_EQ(box.latest()->mCount, 1u);
    }

    TEST(OFSoundHeadSampleBoxTest, a_reader_never_sees_a_half_written_sample)
    {
        HeadSampleBox box;
        std::atomic<bool> done{ false };
        std::thread writer([&] {
            for (int i = 0; i < 20000; ++i)
                box.publish(yawLeft(i % 360), Clock::time_point{} + std::chrono::milliseconds(i));
            done = true;
        });
        int reads = 0;
        while (!done || reads == 0)
        {
            if (const std::optional<HeadSample> sample = box.latest())
            {
                const HeadRotation& q = sample->mAttitude;
                const double angle = static_cast<double>(sample->mTime.time_since_epoch() / 1ms % 360);
                const HeadRotation expected = yawLeft(angle);
                EXPECT_NEAR(q.mW, expected.mW, 1e-12);
                EXPECT_NEAR(q.mZ, expected.mZ, 1e-12);
                ++reads;
            }
        }
        writer.join();
        EXPECT_EQ(box.latest()->mCount, 20000u);
    }
}
