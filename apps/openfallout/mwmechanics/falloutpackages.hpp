#ifndef OPENFALLOUT_MWMECHANICS_FALLOUTPACKAGES_H
#define OPENFALLOUT_MWMECHANICS_FALLOUTPACKAGES_H

#include <cstdint>
#include <string>
#include <vector>

#include <osg/Vec3f>

#include <components/esm/formid.hpp>
#include <components/esm4/levelled.hpp>
#include <components/esm4/packageschedule.hpp>

namespace OFMechanics
{
    // The parts of what a character of Fallout 3 or New Vegas does for its AI package that need nothing of the world:
    // where the clock of the game stands for the schedules of the packages, how far a character may be from a place
    // that a package names, where it goes about and for how long it stands still, and how it moves along its way.

    /// Where a package sends a character and what it does there
    struct FalloutGoal
    {
        ESM4::PackageBehaviour mBehaviour = ESM4::PackageBehaviour::None;
        osg::Vec3f mCenter;
        /// The distance from the center at which a character that goes there has arrived (Stay) or the radius of the
        /// circle in which it goes about (Roam)
        float mRadius = 0.f;
        /// What a Use package takes: the piece of furniture that the character goes to and uses when it is there
        ESM::FormId mTarget;
        /// The place of the package is in another space (a building and the world outside it): the character goes to
        /// the door that leads there (the center), and is at the place, whose reference this is, when it has gone
        /// through
        ESM::FormId mThrough;
        /// A Stay goal near a reference: the radius of the TRAVEL done test (FalloutNV.exe 0x6768D0), whether that
        /// takes no slack, and whether the target is furniture (the test of a seated or sleeping actor, 0x676D9B)
        bool mTravel = false;
        int mDoneRadius = 0;
        bool mPlain = false;
        bool mFurnitureTarget = false;
    };

    /// The smallest and largest distances from the center of the place, in game units, at which a character has
    /// arrived, and the radii in which it goes about, whatever radius the package names (many name none, and some
    /// name a cell)
    constexpr float falloutSmallestArrival = 48.f;
    constexpr float falloutLargestArrival = 200.f;
    constexpr float falloutSmallestRoam = 128.f;
    constexpr float falloutLargestRoam = 2048.f;
    constexpr float falloutDefaultRoam = 512.f;

    /// The radius of a goal from the radius that the location of the package (PLDT) names. A package that uses
    /// furniture names the radius it looks in, not the one it arrives at: the character goes to the entry of the
    /// marker.
    float falloutGoalRadius(std::int32_t locationRadius, ESM4::PackageBehaviour behaviour);

    /// What the done test of a TRAVEL package (FalloutNV.exe 0x6768D0) reads of the reference it goes to (0x676140)
    struct FalloutTravelTarget
    {
        enum class Kind
        {
            None,
            Marker, // the base is the static XMarker or XMarkerHeading (forms 0x3B and 0x34; 0x11CA244, 0x11CA248)
            Furniture, // form type 0x27 (0x568680)
            Actor, // vfunc 0x100
            Other,
        };
        Kind mKind = Kind::None;
        /// An actor asleep in a bed ([actor+0x1AC] 9, 0x920D00)
        bool mAsleep = false;
        /// The base is an activator, a container, a static or a static collection (form types 0x15, 0x1B, 0x20, 0x21):
        /// no margin on its size (the byte table 0x676E2C)
        bool mNoMargin = false;
        /// Half the largest size of the bounds of its base (0x571600)
        float mHalfSize = 0.f;
    };

    /// The radius of the done test (0x6768D0 steps H to K): the location's radius (PLDT, 0 for a cell, 0x67F1C0);
    /// when that is 0, 20 for a marker or furniture, 90 for an actor asleep, else half the target's size and 20
    /// (activators, containers and statics nothing), and `minimum` (iAIDistanceRadiusMinLocation, 0x11CDE98) when
    /// still 0. `plain` when the test takes no slack: a marker (with any radius up to 20, 0x676CD2), furniture, a
    /// sleeper (0x676BB8, 0x676C1A).
    int falloutTravelRadius(
        std::int32_t locationRadius, const FalloutTravelTarget& target, std::int32_t minimum, bool& plain);

    /// The distance test of the done test (actor vfunc 0x2CC, 0x8B2F90 with its z band): the place is within
    /// `radius` of the actor's feet, in 3D but with no height when the place is between 30 below the feet and 30 over
    /// the head (`height`, 128 when unknown, 0x8B3001); `slack` is added to the radius unless `plain`: the radius of
    /// the body of its character controller in game units (0xC6E280; the hull's at 0xC72F1F), doubled for a lying body
    /// (bit 1 of [ctrl+0x414], 0xC72706), at least 32 (0x8B30A3)
    bool falloutTravelDone(const osg::Vec3f& feet, float height, const osg::Vec3f& place, float radius, bool plain,
        float bodyRadius, bool lying);

    /// A piece of furniture (or anything else) that a Find or Use Item At package may take
    struct FalloutCandidate
    {
        /// Where it is, which the radius of the package is measured from
        osg::Vec3f mOrigin;
        /// Where the character stands to use it
        osg::Vec3f mPlace;
        /// How many more characters can use it: its places that no one uses less those that others are on their way to
        int mFree = 0;
    };

    /// The radius, in game units, that a package that names none looks for furniture in
    constexpr float falloutDefaultSearch = 512.f;

    /// The one that a character at `from` takes: the nearest place to `from` of the candidates that can serve one more
    /// character and are within `radius` of `center` across the ground (a radius of 0 or less takes any), -1 when there
    /// is none. A tie goes to the first in the list.
    int falloutChooseCandidate(const std::vector<FalloutCandidate>& candidates, const osg::Vec3f& center, float radius,
        const osg::Vec3f& from);

    /// The object type (PTDT of type 2) that is furniture
    constexpr int falloutFurnitureObjectType = 11;

    /// How far behind the one it escorts a character that escorts goes on, whatever distance the package names (many
    /// name none): twice the distance a follower keeps, at least this
    constexpr float falloutSmallestEscortWait = 400.f;
    float falloutEscortWait(std::int32_t packageDistance);

    /// Whether the escort goes on to the place: one that stands still goes on when the one it escorts is within `wait`,
    /// and one that goes on stops when it is farther than `wait` and the slack
    bool falloutEscortMoves(float away, float wait, bool moving);

    /// A place in the circle of the goal, at the height of its center, that every place of the circle is as likely to
    /// be chosen
    osg::Vec3f falloutRoamPoint(const FalloutGoal& goal, ESM4::LevelledRandom& random);

    /// How long, in seconds, a character stands still where it has gone to before it goes on
    float falloutRoamPause(ESM4::LevelledRandom& random);

    /// The distances, in game units, at which a character that follows another one keeps from it, whatever distance
    /// the package names (the target of a package, PTDT, has one, and many name none), how far beyond that distance the
    /// follower is when it starts to walk after the one it follows, and when it runs
    constexpr float falloutDefaultFollow = 256.f;
    constexpr float falloutSmallestFollow = 128.f;
    constexpr float falloutLargestFollow = 1024.f;
    constexpr float falloutFollowSlack = 64.f;
    constexpr float falloutFollowRunBeyond = 500.f;

    float falloutFollowDistance(std::int32_t packageDistance);

    /// The same for the distance that a script names, a number of any size or none at all: not a number, 0 or less is
    /// the default, and the rest is kept within the limits
    float falloutScriptFollowDistance(float distance);

    /// Whether a follower that is `away` units from the one it follows (across the ground) walks: one that stands
    /// still starts when it is farther than `distance` and the slack, and one that walks stops when it is within
    /// `distance`
    bool falloutFollowMoves(float away, float distance, bool walking);

    /// Whether the follower runs after the one it follows
    bool falloutFollowRuns(float away, float distance);

    /// The distance, in game units, at which a character with a Dialogue package talks to its target, whatever distance
    /// the package names (the games take 120 for one that names none)
    constexpr float falloutDefaultTalk = 120.f;
    constexpr float falloutSmallestTalk = 80.f;
    constexpr float falloutLargestTalk = 1024.f;

    float falloutTalkDistance(std::int32_t packageDistance);

    /// The position in the content files of the first plugin (a file with the extension esm, esp or esl, whatever the
    /// case; the scripts the engine adds come first), where the games keep the player's reference: -1 when there is
    /// none
    int falloutFirstPlugin(const std::vector<std::string>& contentFiles);

    /// The reference the games give the player (PlayerRef, form 0x14 of the first plugin), which a package names as the
    /// target to follow
    constexpr bool falloutIsPlayerReference(const ESM::FormId& id, int firstPlugin)
    {
        return id.mIndex == 0x14 && firstPlugin >= 0 && id.mContentFile == firstPlugin;
    }

    /// The clock for the schedules from the time of day (0 to 24) and the number of days that have passed since the
    /// game began. The files of the games do not say what day the game begins on; it is a Monday here.
    ESM4::PackageClock falloutClock(float hour, int daysPassed);

    /// An angle (radians, as the games turn an object around the vertical axis) that is at most `change` closer to the
    /// target than the angle is, by the shorter way round
    float falloutTurnToward(float angle, float target, float change);

    /// The velocity (units a second, on the ground) that the controls of a Lua script ask of an actor whose AI is off:
    /// forward and side from -1 to 1 (forward is where the heading, from the y axis toward the x axis, points; the side
    /// is to its right), at most `speed` together
    osg::Vec3f falloutControlVelocity(float heading, float forward, float side, float speed);
}

#endif
