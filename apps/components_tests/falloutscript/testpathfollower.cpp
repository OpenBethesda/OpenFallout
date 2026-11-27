#include <components/falloutscript/pathfollower.hpp>

#include <gtest/gtest.h>

#include <cmath>

namespace
{
    using namespace FalloutScript;
    using namespace FalloutScript::PathFollower;

    NavMeshSearch::WayPoint at(float x, float y)
    {
        NavMeshSearch::WayPoint point;
        point.mLocation.mPoint = { x, y, 0.f };
        return point;
    }

    TEST(FalloutScriptPathFollowerTest, the_spline_goes_through_the_points)
    {
        const std::vector<NavMeshSearch::WayPoint> path{ at(0, 0), at(100, 0), at(100, 100) };
        EXPECT_FLOAT_EQ(pointAt(path, 0.f)[0], 0.f);
        EXPECT_FLOAT_EQ(pointAt(path, 1.f)[0], 100.f);
        EXPECT_FLOAT_EQ(pointAt(path, 1.5f)[1], 50.f); // no tangents: halfway
        EXPECT_FLOAT_EQ(pointAt(path, 7.f)[1], 100.f); // beyond the end: the last point
        EXPECT_FLOAT_EQ(lengthOf(path), 200.f);
        // 0x6E7E70: 50 on from the start, within 1
        float t = 0.f;
        Point point{};
        ASSERT_TRUE(along(path, 0.f, 50.f, t, point));
        EXPECT_NEAR(point[0], 50.f, 1.f);
        // the last segment is searched only from a point on it (0x6E8029)
        EXPECT_FALSE(along(path, 0.f, 150.f, t, point));
        EXPECT_FLOAT_EQ(t, 2.f);
        ASSERT_TRUE(along(path, 1.f, 50.f, t, point));
        EXPECT_NEAR(point[1], 50.f, 1.f);
    }

    // Walks an actor that goes where the follower says, as a frame loop would
    Result walk(Follower& follower, Actor& actor, int frames, float dt = 0.05f)
    {
        Result result = Result::Moving;
        for (int i = 0; i < frames && result == Result::Moving; ++i)
        {
            Frame frame;
            result = follower.update(actor, dt, frame);
            actor.mHeading = frame.mHeading;
            for (int k = 0; k < 3; ++k)
                actor.mPosition[k] += frame.mMovement[k];
        }
        return result;
    }

    TEST(FalloutScriptPathFollowerTest, an_actor_walks_a_path_to_its_end)
    {
        Follower follower;
        Actor actor;
        actor.mSpeed = 100.f;
        ASSERT_EQ(follower.start({ at(0, 0), at(0, 300), at(300, 300) }, Start{}, actor), Result::Moving);
        EXPECT_EQ(walk(follower, actor, 400), Result::Arrived);
        // within the arrival radius of 25 (0x9E06B0)
        EXPECT_LT(std::hypot(actor.mPosition[0] - 300.f, actor.mPosition[1] - 300.f), 25.f);
        EXPECT_FALSE(follower.active());
    }

    TEST(FalloutScriptPathFollowerTest, the_turn_is_limited_and_slows_the_step)
    {
        Follower follower;
        Actor actor; // facing +y, the path goes to -y
        actor.mSpeed = 100.f;
        actor.mNoTurnMinTime = true;
        Frame frame;
        // the first point is where it stands: no turn on the spot (0x9E7719)
        ASSERT_EQ(follower.start({ at(0, 0), at(0, -300) }, Start{}, actor), Result::Moving);
        ASSERT_EQ(follower.update(actor, 0.1f, frame), Result::Moving);
        // 90 degrees a second times 3 for a tenth of a second (0x9E4800)
        EXPECT_NEAR(std::abs(frame.mHeading), 27.f * 0.0174533f, 1e-4f);
        // a turn of more than 2.7 radians walks at 0.05 of the gait
        EXPECT_NEAR(std::hypot(frame.mMovement[0], frame.mMovement[1]), 100.f * 0.1f * 0.05f, 1e-3f);
    }

    TEST(FalloutScriptPathFollowerTest, a_first_point_behind_is_turned_to_on_the_spot)
    {
        Follower follower;
        Actor actor;
        actor.mPosition = { 0, 100, 0 };
        actor.mSpeed = 100.f;
        Frame frame;
        ASSERT_EQ(follower.start({ at(0, 0), at(0, -300) }, Start{}, actor), Result::Moving);
        ASSERT_EQ(follower.update(actor, 0.1f, frame), Result::Moving);
        // 0x9E7D70: 90 degrees a second times fAITurnSpeedScale 1.5, and no step
        EXPECT_NEAR(std::abs(frame.mHeading), 13.5f * 0.0174533f, 1e-4f);
        EXPECT_FLOAT_EQ(frame.mMovement[1], 0.f);
    }

    TEST(FalloutScriptPathFollowerTest, an_actor_that_does_not_move_is_stuck_after_a_second_and_a_half)
    {
        Follower follower;
        Actor actor;
        actor.mSpeed = 100.f;
        follower.start({ at(0, 0), at(0, 300) }, Start{}, actor);
        Result result = Result::Moving;
        Frame frame;
        int frames = 0;
        while (result == Result::Moving && frames < 100)
        {
            result = follower.update(actor, 0.1f, frame); // it never moves
            ++frames;
        }
        EXPECT_EQ(result, Result::Stuck);
        EXPECT_TRUE(frame.mMarkStuck);
        EXPECT_EQ(frames, 16); // the first frame has no plan to fall short of; then 15 tenths add up to just over 1.5 s
        // blocked by an actor the time counts a fifth (0x9E5088)
        actor.mBlockedByActor = true;
        follower.start({ at(0, 0), at(0, 300) }, Start{}, actor);
        frames = 0;
        result = Result::Moving;
        while (result == Result::Moving && frames < 200)
        {
            result = follower.update(actor, 0.1f, frame);
            ++frames;
        }
        EXPECT_EQ(result, Result::Stuck);
        EXPECT_GT(frames, 70);
        // without a movement animation it never counts as stuck (0x495BE0)
        actor.mBlockedByActor = false;
        actor.mMoving = false;
        follower.start({ at(0, 0), at(0, 300) }, Start{}, actor);
        for (int i = 0; i < 100; ++i)
            ASSERT_EQ(follower.update(actor, 0.1f, frame), Result::Moving);
    }
}
