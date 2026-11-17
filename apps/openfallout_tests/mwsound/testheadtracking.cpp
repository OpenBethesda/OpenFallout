#include "apps/openfallout/mwsound/headtracking.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
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
        // It turns again when samples come back
        publish(yawLeft(90));
        expectNear(listen().mForward, sWest);
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
        box.publish(yawLeft(10), Clock::time_point{} + 2s);
        EXPECT_EQ(box.latest()->mConnection, first);
        box.disconnect();
        box.connect();
        box.publish(yawLeft(10), Clock::time_point{} + 3s);
        EXPECT_EQ(box.latest()->mConnection, first + 1);
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
