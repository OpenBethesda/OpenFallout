#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PATHFOLLOWER_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PATHFOLLOWER_H

#include "navmeshsmoother.hpp"

#include <cstdint>
#include <optional>
#include <vector>

/// How an actor walks a path, as DetailedActorPathHandler of the executables (FalloutNV.exe vtable 0x10923EC, start
/// 0x9E0470, update 0x9E0A00) does it each frame: along a spline through the smoother's points, a tenth of a segment at
/// a time, turning at a limited rate, and marking where it got stuck
namespace FalloutScript::PathFollower
{
    using NavMeshSearch::Point;
    using Path = std::vector<NavMeshSearch::WayPoint>;

    /// The point of the path at `t` (a point's number and how far on to the next): a cubic Hermite curve between the
    /// points with their tangents on the map, the height without (NV 0x6E7A10, 0x6E7C40, 0x6E7D60)
    Point pointAt(const Path& path, float t);

    /// The sum of the straight lines between the points (NV 0x6E78D0)
    float lengthOf(const Path& path);

    /// The point `distance` on from `t` along the path (NV 0x6E7E70 forward): the straight distance from the point at
    /// `t`, found within 1 unit on its segment, and on a later segment the rest after the length of the curve on
    /// before. False with the end of the path when it is not found.
    bool along(const Path& path, float t, float distance, float& outT, Point& outPoint);

    /// What the follower reads of the actor this frame
    struct Actor
    {
        Point mPosition{};
        float mHeading = 0.f; // radians, clockwise from +y
        /// vfunc 0x354: degrees a second (fCharacterDefaultTurningSpeed 90 for characters, NV 0x8D3290;
        /// fCreatureDefaultTurningSpeed 45 for a creature without its own, 0x8D47F0)
        float mTurnSpeed = 90.f;
        /// How far it goes in a second walking, and running (NV 0x884DC0, 0x884EB0 by the word, 0x8A0B10)
        float mSpeed = 0.f;
        float mRunSpeed = 0.f;
        /// NV 0x8BE1B0 to 0x885020: half the larger of its widths on the map by its scale, or of 32
        float mRadius = 16.f;
        /// It may act (NV 0x8843A0: alive, not paralysed, not in a scene that holds it)
        bool mCanAct = true;
        /// vfunc 0x21C
        bool mCreature = false;
        /// Byte +0x104 (NV 0x493BB0): it rides (set when it is put on a mount, 0x8A01B8 in 0x89FCF0, and from a mount
        /// form, 0x8ABDC4): the fast turn scales, and no short-way rule (0x9E0757)
        bool mMounted = false;
        /// Byte +0x1B0 (NV 0x87EEB0): a creature loaded without its ragdoll and look controllers (0x87E960 from
        /// 0x87E130 when 0x5F0C80 holds); when it is not set a turn on the spot lasts fActorTurnAnimMinTime at least
        bool mNoTurnMinTime = false;
        /// What its character controller touches is an actor (NV 0x9E4E75 to 0x9E4EF6)
        bool mBlockedByActor = false;
        /// Its strongest animation is its movement one and plays a group from Forward to DodgeRight, 3 to 14 (NV
        /// 0x495BE0)
        bool mMoving = true;
        /// The movement word its owner asks for (ActorMover +0x34, owner vfunc 0x1C): 0x100 walk, 0x200 run, and the
        /// way in the low bits (1 forward); a package asks for 0x101, or 0x201 to run (NV 0x8DA950)
        std::uint32_t mRequested = 0x101;
        /// The owner's goal point (+0x73 with +0x4 to +0xC, NV 0x8B3D10): it faces it while it walks. Set by a script's
        /// Look with its integer (0x5C988B, 0x8EF4E3), by combat (0x9817E4, 0x981892) and by the UseWeapon and Guard
        /// packages
        std::optional<Point> mGoalPoint;
        /// Its life state is Unconscious (3) or Restrained (5): no movement (NV 0x9E2AAF to 0x9E2ACD)
        bool mHeld = false;
        /// Its plain set of animations has the groups Backward, Left and Right (NV 0x494710 with 4, 5 and 6)
        bool mHasBackward = false;
        bool mHasLeft = false;
        bool mHasRight = false;
        /// The time of the text key "End" of its Forward group (NV 0x897B50(3), 0x5F3780(1))
        float mForwardEnd = 0.f;
        /// What its movement animation moved it this frame, in its own frame (x right, y forward; NV 0x8B7100 with
        /// 0x494390); none when it has no 3D, and then it goes at its gait (0x9E3520)
        std::optional<Point> mRootMotion;
    };

    /// What the path request says to the follower (NV PathingRequest)
    struct Start
    {
        float mRadius = 35.f; // +0x68 (0x69EF80)
        float mGoalRadius = 25.f; // +0x74 (0x9B0BD0)
        float mGoalHeight = 180.f; // +0x70 (0x51B860)
        bool mHoldOff = false; // +0x9C (0x9E8670)
        std::optional<float> mGoalHeading; // +0x8C when +0x99 (0x507E10, 0x505C00)
        /// The search did not get to the goal (path byte +0x40, 0x6C9FA0)
        bool mPartial = false;
    };

    /// What the follower does this frame
    struct Frame
    {
        float mHeading = 0.f;
        /// The movement of this frame on the map: the plan H+0x1C that the mover takes (slot 19, 0x9E32F0), turned by
        /// the heading (0x92F260) and given to the character controller as a velocity (0xC73170)
        Point mMovement{};
        /// Where it got stuck, for the mark on the mesh (NV 0x9E4CF0 to 0x691510)
        bool mMarkStuck = false;
        Point mStuckAt{};
        /// The movement word it publishes (H+0x60, published by 0x9DDC00 for the animation, 0x895110): 0x100 walk,
        /// 0x200 run; 1 forward, 2 backward, 4 left, 8 right, 0x10 and 0x20 a turn on the spot to the left and right
        std::uint32_t mMove = 0;
    };

    enum class Result
    {
        Moving = 0,
        Stuck = 1, // the path is to be made again (NV 0x9DC5E0: owner +0x6C = 7)
        TooHigh = 2, // the end is too far above or below (NV 0x9E8000)
        Door = 3,
        Arrived = 5,
    };

    class Follower
    {
    public:
        /// NV 0x9E0470 with a start fraction of 0. Arrived for a path of less than two points; the caller runs the
        /// first update in the same frame (0x9DC0F1)
        Result start(Path path, const Start& request, const Actor& actor);
        bool active() const { return mState != 5 && mState != 6; }
        /// No path to walk: the next walk asks for one
        void stop()
        {
            mState = 5;
            mPath.clear();
        }
        const Path& path() const { return mPath; }

        /// NV 0x9E0A00
        Result update(const Actor& actor, float dt, Frame& out);

    private:
        void advance(const Actor& actor); // NV 0x9E3D50
        float turn(const Actor& actor, float dt, float& slow) const; // NV 0x9E4800
        bool stuck(const Actor& actor, float dt, Frame& out); // NV 0x9E4CF0
        Result reached(const Actor& actor) const; // NV 0x9E8000
        bool startTurn(const Actor& actor, float heading); // NV 0x9E7610 with both flags clear
        float turnStep(const Actor& actor, float dt); // NV 0x9E7D70
        void chooseMove(const Actor& actor, float dt); // NV 0x9E2AA0

        Path mPath;
        Start mRequest;
        Point mPlanned{}; // +0x1C, in the frame of the actor: x right, y forward
        float mPlannedLength = 0.f; // +0x9C
        Point mWaypoint{}; // +0x34
        Point mPrevious{}; // +0x4C
        Point mLast{}; // +0x40
        float mT = 0.f; // +0x70
        int mSegment = 0; // +0x64
        float mPass = 10.f; // +0x14
        float mArrival2 = 625.f; // +0xAC
        float mStuckTime = 0.f; // +0x88
        bool mStuck = false; // +0xDE
        int mState = 5; // +0xC8
        int mAvoidance = 0; // +0xD0
        float mWaitTime = 0.f; // +0x78
        bool mKeepHeading = false; // +0xC0 with +0xC1
        bool mTurning = false; // +0xDC
        float mTurnTo = 0.f; // +0x68
        float mTurnTime = -1.f; // +0x94
        std::uint32_t mMove = 0; // +0x60
        int mRunStart = 0; // +0xCC
        float mRunStartTime = 0.f; // +0x90
        float mDecisionTime = 1.f; // +0x8C
        bool mTurnInPlace = false; // +0xE1
        float mTurnInPlaceTime = 0.f; // +0xB8
    };
}

#endif
