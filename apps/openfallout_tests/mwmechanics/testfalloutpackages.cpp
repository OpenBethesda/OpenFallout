#include <apps/openfallout/mwmechanics/falloutpackages.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <osg/Math>

namespace
{
    using namespace testing;
    using namespace OFMechanics;

    constexpr float epsilon = 0.001f;

    TEST(FalloutPackagesTest, theTravelRadiusIsThePackagesOrOneFromItsTarget)
    {
        using Kind = FalloutTravelTarget::Kind;
        bool plain = true;
        FalloutTravelTarget target;
        // the location's own radius, with the slack
        EXPECT_EQ(falloutTravelRadius(300, target, 100, plain), 300);
        EXPECT_FALSE(plain);
        // none and no target: iAIDistanceRadiusMinLocation
        EXPECT_EQ(falloutTravelRadius(0, target, 100, plain), 100);
        // a marker: 20 and no slack, also with a radius of its own up to 20 (0x676CD2)
        target.mKind = Kind::Marker;
        EXPECT_EQ(falloutTravelRadius(0, target, 100, plain), 20);
        EXPECT_TRUE(plain);
        EXPECT_EQ(falloutTravelRadius(15, target, 100, plain), 15);
        EXPECT_TRUE(plain);
        EXPECT_EQ(falloutTravelRadius(21, target, 100, plain), 21);
        EXPECT_FALSE(plain);
        // an actor: half its size and 20, or 90 asleep
        target = { Kind::Actor, false, false, 40.7f };
        EXPECT_EQ(falloutTravelRadius(0, target, 100, plain), 60);
        target.mAsleep = true;
        EXPECT_EQ(falloutTravelRadius(0, target, 100, plain), 90);
        EXPECT_TRUE(plain);
        // a static: half its size only
        target = { Kind::Other, false, true, 64.f };
        EXPECT_EQ(falloutTravelRadius(0, target, 100, plain), 64);
    }

    TEST(FalloutPackagesTest, theTravelIsDoneWithinTheRadiusAndTheSlackIgnoringHeightNearTheBody)
    {
        const osg::Vec3f feet(0.f, 0.f, 0.f);
        // 0x8B2F90: within the band of the body the height does not count
        EXPECT_TRUE(falloutTravelDone(feet, 128.f, osg::Vec3f(100.f, 0.f, 150.f), 100.f, true, 20.f, false));
        EXPECT_FALSE(falloutTravelDone(feet, 128.f, osg::Vec3f(100.f, 0.f, 200.f), 100.f, true, 20.f, false));
        // the slack is the body's radius, at least 32, doubled for a lying body
        EXPECT_TRUE(falloutTravelDone(feet, 128.f, osg::Vec3f(132.f, 0.f, 0.f), 100.f, false, 20.f, false));
        EXPECT_FALSE(falloutTravelDone(feet, 128.f, osg::Vec3f(133.f, 0.f, 0.f), 100.f, false, 20.f, false));
        EXPECT_TRUE(falloutTravelDone(feet, 128.f, osg::Vec3f(160.f, 0.f, 0.f), 100.f, false, 30.f, true));
    }

    TEST(FalloutPackagesTest, aGoalThatRoamsGetsTheRadiusOfThePackageWithinLimits)
    {
        const auto roam = ESM4::PackageBehaviour::Roam;
        EXPECT_FLOAT_EQ(falloutGoalRadius(1000, roam), 1000.f);
        EXPECT_FLOAT_EQ(falloutGoalRadius(0, roam), falloutDefaultRoam);
        EXPECT_FLOAT_EQ(falloutGoalRadius(-5, roam), falloutDefaultRoam);
        EXPECT_FLOAT_EQ(falloutGoalRadius(10, roam), falloutSmallestRoam);
        EXPECT_FLOAT_EQ(falloutGoalRadius(1000000, roam), falloutLargestRoam);
    }

    TEST(FalloutPackagesTest, aGoalThatStaysGetsAnArrivalDistanceWithinLimits)
    {
        const auto stay = ESM4::PackageBehaviour::Stay;
        EXPECT_FLOAT_EQ(falloutGoalRadius(100, stay), 100.f);
        EXPECT_FLOAT_EQ(falloutGoalRadius(0, stay), falloutSmallestArrival);
        EXPECT_FLOAT_EQ(falloutGoalRadius(-1, stay), falloutSmallestArrival);
        EXPECT_FLOAT_EQ(falloutGoalRadius(5000, stay), falloutLargestArrival);
    }

    TEST(FalloutPackagesTest, roamPointsAreInTheCircleAndSpreadOverIt)
    {
        FalloutGoal goal;
        goal.mBehaviour = ESM4::PackageBehaviour::Roam;
        goal.mCenter = osg::Vec3f(1000.f, -500.f, 70.f);
        goal.mRadius = 400.f;

        ESM4::LevelledRandom random(7);
        constexpr int points = 2000;
        int north = 0;
        int east = 0;
        int inner = 0;
        float farthest = 0.f;
        for (int i = 0; i < points; ++i)
        {
            const osg::Vec3f point = falloutRoamPoint(goal, random);
            EXPECT_FLOAT_EQ(point.z(), 70.f);
            const float distance = (osg::Vec2f(point.x(), point.y()) - osg::Vec2f(1000.f, -500.f)).length();
            EXPECT_LE(distance, 400.f + epsilon);
            farthest = std::max(farthest, distance);
            north += point.y() > -500.f ? 1 : 0;
            east += point.x() > 1000.f ? 1 : 0;
            inner += distance < 200.f ? 1 : 0;
        }
        // Half of the points are on each side, and a quarter are in the inner half of the radius
        EXPECT_THAT(north, AllOf(Gt(points * 4 / 10), Lt(points * 6 / 10)));
        EXPECT_THAT(east, AllOf(Gt(points * 4 / 10), Lt(points * 6 / 10)));
        EXPECT_THAT(inner, AllOf(Gt(points * 2 / 10), Lt(points * 3 / 10)));
        EXPECT_GT(farthest, 380.f);
    }

    TEST(FalloutPackagesTest, theSameSeedGivesTheSamePointsAndPauses)
    {
        FalloutGoal goal;
        goal.mCenter = osg::Vec3f(0.f, 0.f, 0.f);
        goal.mRadius = 300.f;
        ESM4::LevelledRandom first(99);
        ESM4::LevelledRandom second(99);
        for (int i = 0; i < 10; ++i)
        {
            EXPECT_EQ(falloutRoamPoint(goal, first), falloutRoamPoint(goal, second));
            EXPECT_FLOAT_EQ(falloutRoamPause(first), falloutRoamPause(second));
        }
    }

    TEST(FalloutPackagesTest, aPauseLastsAFewSeconds)
    {
        ESM4::LevelledRandom random(3);
        for (int i = 0; i < 200; ++i)
        {
            const float pause = falloutRoamPause(random);
            EXPECT_GE(pause, 3.f);
            EXPECT_LT(pause, 12.f);
        }
    }

    TEST(FalloutPackagesTest, theClockBeginsOnAMonday)
    {
        EXPECT_FLOAT_EQ(falloutClock(13.5f, 0).mHour, 13.5f);
        EXPECT_EQ(falloutClock(0.f, 0).mDayOfWeek, 1);
        EXPECT_EQ(falloutClock(0.f, 5).mDayOfWeek, 6);
        EXPECT_EQ(falloutClock(0.f, 6).mDayOfWeek, 0);
        EXPECT_EQ(falloutClock(0.f, 7).mDayOfWeek, 1);
        EXPECT_EQ(falloutClock(0.f, -1).mDayOfWeek, 0);
    }

    TEST(FalloutPackagesTest, theClockStaysWithinTheDay)
    {
        EXPECT_FLOAT_EQ(falloutClock(-3.f, 0).mHour, 0.f);
        EXPECT_FLOAT_EQ(falloutClock(30.f, 0).mHour, 24.f);
    }

    TEST(FalloutPackagesTest, aTurnGoesTheShorterWayRound)
    {
        const float pi = osg::PIf;
        EXPECT_NEAR(falloutTurnToward(0.f, 1.f, 0.25f), 0.25f, epsilon);
        EXPECT_NEAR(falloutTurnToward(0.f, -1.f, 0.25f), -0.25f, epsilon);
        // Across the half turn: from 3 to -3 the short way is up through pi
        EXPECT_NEAR(falloutTurnToward(3.f, -3.f, 0.1f), 3.1f, epsilon);
        EXPECT_NEAR(falloutTurnToward(-3.f, 3.f, 0.1f), -3.1f, epsilon);
        // and a turn that reaches its target stops there
        EXPECT_NEAR(falloutTurnToward(0.5f, 0.6f, 1.f), 0.6f, epsilon);
        EXPECT_NEAR(falloutTurnToward(pi - 0.05f, -pi + 0.05f, 1.f), -pi + 0.05f, epsilon);
        EXPECT_NEAR(falloutTurnToward(0.f, 0.f, 0.5f), 0.f, epsilon);
    }

    TEST(FalloutPackagesTest, aFollowerKeepsTheDistanceOfThePackageWithinLimits)
    {
        EXPECT_FLOAT_EQ(falloutFollowDistance(300), 300.f);
        EXPECT_FLOAT_EQ(falloutFollowDistance(0), falloutDefaultFollow);
        EXPECT_FLOAT_EQ(falloutFollowDistance(-4), falloutDefaultFollow);
        EXPECT_FLOAT_EQ(falloutFollowDistance(10), falloutSmallestFollow);
        EXPECT_FLOAT_EQ(falloutFollowDistance(100000), falloutLargestFollow);
    }

    TEST(FalloutPackagesTest, aCharacterTalksFromTheDistanceOfThePackageWithinLimits)
    {
        EXPECT_FLOAT_EQ(falloutTalkDistance(300), 300.f);
        EXPECT_FLOAT_EQ(falloutTalkDistance(0), falloutDefaultTalk);
        EXPECT_FLOAT_EQ(falloutTalkDistance(-4), falloutDefaultTalk);
        EXPECT_FLOAT_EQ(falloutTalkDistance(10), falloutSmallestTalk);
        EXPECT_FLOAT_EQ(falloutTalkDistance(100000), falloutLargestTalk);
    }

    TEST(FalloutPackagesTest, aScriptCanNameAnyDistanceToFollowAt)
    {
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(300.5f), 300.5f);
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(0.f), falloutDefaultFollow);
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(-4.f), falloutDefaultFollow);
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(std::numeric_limits<float>::quiet_NaN()), falloutDefaultFollow);
        // A fraction is a distance that is too short, not none
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(0.5f), falloutSmallestFollow);
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(1e20f), falloutLargestFollow);
        EXPECT_FLOAT_EQ(falloutScriptFollowDistance(std::numeric_limits<float>::infinity()), falloutLargestFollow);
    }

    TEST(FalloutPackagesTest, aFollowerStartsToWalkBeyondTheSlackAndStopsWithinTheDistance)
    {
        const float distance = 200.f;
        // Standing still, it waits until the one it follows is out of the distance and the slack
        EXPECT_FALSE(falloutFollowMoves(distance, distance, false));
        EXPECT_FALSE(falloutFollowMoves(distance + falloutFollowSlack - 1.f, distance, false));
        EXPECT_TRUE(falloutFollowMoves(distance + falloutFollowSlack + 1.f, distance, false));
        // Walking, it goes on until it is within the distance
        EXPECT_TRUE(falloutFollowMoves(distance + 1.f, distance, true));
        EXPECT_FALSE(falloutFollowMoves(distance, distance, true));
        EXPECT_FALSE(falloutFollowMoves(0.f, distance, true));
    }

    TEST(FalloutPackagesTest, aFollowerRunsOnlyWhenFarBehind)
    {
        EXPECT_FALSE(falloutFollowRuns(300.f, 200.f));
        EXPECT_FALSE(falloutFollowRuns(200.f + falloutFollowRunBeyond, 200.f));
        EXPECT_TRUE(falloutFollowRuns(200.f + falloutFollowRunBeyond + 1.f, 200.f));
    }

    TEST(FalloutPackagesTest, aGoalThatUsesFurnitureArrivesAtTheEntryWhateverRadiusThePackageLooksIn)
    {
        EXPECT_FLOAT_EQ(falloutGoalRadius(350, ESM4::PackageBehaviour::Use), falloutSmallestArrival);
        EXPECT_FLOAT_EQ(falloutGoalRadius(0, ESM4::PackageBehaviour::Use), falloutSmallestArrival);
        EXPECT_FLOAT_EQ(falloutGoalRadius(350, ESM4::PackageBehaviour::Escort), falloutLargestArrival);
    }

    TEST(FalloutPackagesTest, aCharacterTakesTheNearestPieceOfFurnitureThatHasRoomInReach)
    {
        const auto at = [](float x, int free) {
            FalloutCandidate candidate;
            candidate.mOrigin = candidate.mPlace = osg::Vec3f(x, 0.f, 0.f);
            candidate.mFree = free;
            return candidate;
        };
        const std::vector<FalloutCandidate> candidates = {
            at(100.f, 0), // near the character, but taken
            at(200.f, 1), at(900.f, 2), // out of reach of the center
            at(150.f, -1), // more are on their way to it than it has places
        };
        const osg::Vec3f center(0.f, 0.f, 0.f);
        const osg::Vec3f from(120.f, 0.f, 0.f);
        EXPECT_EQ(falloutChooseCandidate(candidates, center, 350.f, from), 1);
        // A radius of 0 looks everywhere
        EXPECT_EQ(falloutChooseCandidate(candidates, center, 0.f, from), 1);
        EXPECT_EQ(falloutChooseCandidate(candidates, center, 0.f, osg::Vec3f(800.f, 0.f, 0.f)), 2);
        EXPECT_EQ(falloutChooseCandidate(candidates, center, 50.f, from), -1);
        EXPECT_EQ(falloutChooseCandidate({}, center, 350.f, from), -1);
        // The first of two that are as near
        const std::vector<FalloutCandidate> tied = { at(10.f, 1), at(-10.f, 1) };
        EXPECT_EQ(falloutChooseCandidate(tied, center, 0.f, center), 0);
    }

    TEST(FalloutPackagesTest, anEscortWaitsForTheOneItEscortsOnceItIsFarBehind)
    {
        EXPECT_FLOAT_EQ(falloutEscortWait(0), 2.f * falloutDefaultFollow);
        EXPECT_FLOAT_EQ(falloutEscortWait(150), falloutSmallestEscortWait);
        EXPECT_FLOAT_EQ(falloutEscortWait(500), 1000.f);
        const float wait = 500.f;
        EXPECT_TRUE(falloutEscortMoves(wait, wait, false));
        EXPECT_FALSE(falloutEscortMoves(wait + 1.f, wait, false));
        EXPECT_TRUE(falloutEscortMoves(wait + falloutFollowSlack, wait, true));
        EXPECT_FALSE(falloutEscortMoves(wait + falloutFollowSlack + 1.f, wait, true));
    }

    TEST(FalloutPackagesTest, thePlayerReferenceIsForm14OfTheFirstPlugin)
    {
        // (the scripts that the engine adds come first in the list of content files)
        const std::vector<std::string> files{ "builtin.omwscripts", "FalloutNV.ESM", "DeadMoney.esm" };
        const int first = falloutFirstPlugin(files);
        EXPECT_EQ(first, 1);
        EXPECT_TRUE(falloutIsPlayerReference(ESM::FormId::fromUint32(0x01000014), first));
        EXPECT_FALSE(falloutIsPlayerReference(ESM::FormId::fromUint32(0x02000014), first));
        EXPECT_FALSE(falloutIsPlayerReference(ESM::FormId::fromUint32(0x01000015), first));
        EXPECT_FALSE(falloutIsPlayerReference(ESM::FormId{}, first));
        EXPECT_FALSE(falloutIsPlayerReference(ESM::FormId::fromUint32(0x01000014), -1));
    }

    TEST(FalloutPackagesTest, theFirstPluginIsTheFirstFileOfAGameFormat)
    {
        EXPECT_EQ(falloutFirstPlugin({}), -1);
        EXPECT_EQ(falloutFirstPlugin({ "builtin.omwscripts", "mod.omwaddon" }), -1);
        EXPECT_EQ(falloutFirstPlugin({ "a.omwscripts", "Fallout3.esm" }), 1);
        EXPECT_EQ(falloutFirstPlugin({ "noextension", "x.ESP", "y.esm" }), 1);
        EXPECT_EQ(falloutFirstPlugin({ "x.esl" }), 0);
    }

    TEST(FalloutPackagesTest, theControlsOfAScriptMoveTheActorWhereItFacesAtMostAtItsSpeed)
    {
        // heading 0 faces +y: forward goes along y, the side to the right along +x
        const osg::Vec3f ahead = falloutControlVelocity(0.f, 1.f, 0.f, 100.f);
        EXPECT_NEAR(ahead.x(), 0.f, epsilon);
        EXPECT_NEAR(ahead.y(), 100.f, epsilon);
        const osg::Vec3f right = falloutControlVelocity(0.f, 0.f, 1.f, 100.f);
        EXPECT_NEAR(right.x(), 100.f, epsilon);
        EXPECT_NEAR(right.y(), 0.f, epsilon);
        // heading a quarter turn faces +x
        const osg::Vec3f turned = falloutControlVelocity(static_cast<float>(osg::PI_2), 1.f, 0.f, 100.f);
        EXPECT_NEAR(turned.x(), 100.f, epsilon);
        EXPECT_NEAR(turned.y(), 0.f, epsilon);
        // forward and side together are no faster than one alone; half a push is half the speed; beyond 1 is 1
        EXPECT_NEAR(falloutControlVelocity(0.f, 1.f, 1.f, 100.f).length(), 100.f, epsilon);
        EXPECT_NEAR(falloutControlVelocity(0.f, 0.5f, 0.f, 100.f).length(), 50.f, epsilon);
        EXPECT_NEAR(falloutControlVelocity(0.f, 3.f, 0.f, 100.f).length(), 100.f, epsilon);
        // no push, no speed: it stands
        EXPECT_EQ(falloutControlVelocity(0.f, 0.f, 0.f, 100.f), osg::Vec3f());
        EXPECT_EQ(falloutControlVelocity(0.f, 1.f, 0.f, 0.f), osg::Vec3f());
    }
}
