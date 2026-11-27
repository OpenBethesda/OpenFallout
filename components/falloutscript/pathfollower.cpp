#include "pathfollower.hpp"

#include <algorithm>
#include <cmath>

namespace FalloutScript::PathFollower
{
    namespace
    {
        constexpr float pi = 3.1415927f; // 0x102B3C8
        constexpr float twoPi = 6.2831855f; // 0x101FF50
        constexpr float degree = 0.0174533f; // 0x1023128

        Point minus(const Point& a, const Point& b)
        {
            return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
        }
        float length2Across(const Point& v)
        {
            return v[0] * v[0] + v[1] * v[1];
        }
        float length2(const Point& v)
        {
            return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        }
        // NV 0x4E44F0
        float wrap(float a)
        {
            while (a > pi)
                a -= twoPi;
            while (a < -pi)
                a += twoPi;
            return a;
        }
        // NV 0x4B13C0: the heading of a way on the map, clockwise from +y
        float headingOf(const Point& v)
        {
            return std::atan2(v[0], v[1]);
        }
        // NV 0x9E1640 with 0x4B4500: a vector of the actor's frame (x right, y forward) turned by a heading
        Point rotate(const Point& v, float a)
        {
            const float c = std::cos(a);
            const float s = std::sin(a);
            return { v[0] * c + v[1] * s, -v[0] * s + v[1] * c, v[2] };
        }

        // The point of segment `i` at `u` from 0 to 1 (NV 0x6E9AB0)
        Point onSegment(const Path& path, int i, float u)
        {
            return u >= 1.f ? path[i + 1].mLocation.mPoint : pointAt(path, static_cast<float>(i) + u);
        }

        // NV 0x6E9AF0 with two chords, as its callers have it
        float arc(const Path& path, int i, float from, float to)
        {
            constexpr int chords = 2;
            Point previous = onSegment(path, i, from);
            float sum = 0.f;
            for (int k = 0; k < chords; ++k)
            {
                const float s = std::clamp(from + (to - from) * static_cast<float>(k + 1) / chords, 0.f, 1.f);
                const Point point = onSegment(path, i, s);
                sum += std::sqrt(length2(minus(point, previous)));
                previous = point;
            }
            return sum;
        }

        // NV 0x6E9830 forward with the tolerance of 1 its callers give: the point of the segment at the straight
        // distance from the point at `u`, by halving
        bool searchSegment(const Path& path, int i, float u, float distance, float& outU, Point& outPoint)
        {
            constexpr float tolerance = 1.f;
            if (!(tolerance < distance))
            {
                outU = u;
                outPoint = onSegment(path, i, u);
                return true;
            }
            const float far2 = (distance + tolerance) * (distance + tolerance);
            const float near2 = (distance - tolerance) * (distance - tolerance);
            u = std::clamp(u, 0.f, 1.f);
            const Point from = onSegment(path, i, u);
            float low = u;
            float high = 1.f;
            float current = 1.f;
            while (true)
            {
                const Point point = onSegment(path, i, current);
                const float d2 = length2(minus(from, point));
                if (near2 <= d2 && d2 <= far2)
                {
                    outU = current;
                    outPoint = point;
                    return true;
                }
                if (d2 < near2)
                {
                    if (!(current < 0.99f)) // 0x6E99A8: 0.99
                        break;
                    low = current;
                }
                else
                {
                    if (!(current > 0.01f)) // 0x6E99F6: 0.01
                        break;
                    high = current;
                }
                current = (low + high) * 0.5f;
            }
            outU = 1.f;
            outPoint = path[i + 1].mLocation.mPoint;
            return false;
        }
    }

    Point pointAt(const Path& path, float t)
    {
        if (path.empty())
            return {};
        const int last = static_cast<int>(path.size()) - 1;
        const int i = static_cast<int>(t);
        if (i >= last)
            return path.back().mLocation.mPoint;
        const float f = t - static_cast<float>(i);
        // NV 0x6E7C40
        const float f2 = f * f;
        const float f3 = f2 * f;
        const float h01 = -2.f * f3 + 3.f * f2;
        const float h00 = 1.f - h01;
        const float h11 = f3 - f2;
        const float h10 = h11 - f2 + f;
        const NavMeshSearch::WayPoint& a = path[i];
        const NavMeshSearch::WayPoint& b = path[i + 1];
        Point p{};
        for (int k = 0; k < 3; ++k)
        {
            // the height has no tangent (NV 0x6E7DB9)
            const float ta = k < 2 ? a.mTangent[k] : 0.f;
            const float tb = k < 2 ? b.mTangent[k] : 0.f;
            p[k] = h00 * a.mLocation.mPoint[k] + h01 * b.mLocation.mPoint[k] + h10 * ta + h11 * tb;
        }
        return p;
    }

    float lengthOf(const Path& path)
    {
        float sum = 0.f;
        for (std::size_t i = 1; i < path.size(); ++i)
            sum += std::sqrt(length2(minus(path[i].mLocation.mPoint, path[i - 1].mLocation.mPoint)));
        return sum;
    }

    bool along(const Path& path, float t, float distance, float& outT, Point& outPoint)
    {
        const int n = static_cast<int>(path.size());
        if (n == 0)
            return false;
        const float last = static_cast<float>(n - 1);
        const auto end = [&] {
            outT = last;
            outPoint = path.back().mLocation.mPoint;
            return false;
        };
        if (t >= last)
            return end();
        int segment = static_cast<int>(t);
        float u = t - static_cast<float>(segment);
        while (true)
        {
            float found = 0.f;
            if (searchSegment(path, segment, u, distance, found, outPoint))
            {
                outT = found + static_cast<float>(segment);
                return true;
            }
            distance -= arc(path, segment, u, 1.f);
            ++segment;
            u = 0.f;
            // 0x6E8029: the last segment is searched only when it is the first
            if (!(segment < n - 2))
                return end();
        }
    }

    Result Follower::start(Path path, const Start& request, const Actor& actor)
    {
        mPath = std::move(path);
        mRequest = request;
        mState = 1; // 0x9E047C
        if (mAvoidance != 2 && mAvoidance != 4) // 0x9E04A2
        {
            mAvoidance = 1;
            mWaitTime = 0.f;
        }
        mPlanned = {};
        mPlannedLength = 0.f;
        mTurning = false;
        mStuck = false; // 0x9E04DB
        mStuckTime = 0.f;
        mLast = actor.mPosition; // 0x9E0572
        mTurnTime = -1.f; // 0x9E05A9
        if (mPath.size() < 2) // 0x9E05B8
        {
            mWaypoint = actor.mPosition;
            mPrevious = mWaypoint;
            mState = 5;
            return Result::Arrived;
        }
        // 0x9E063D to 0x9E06DF: a way that does not get there ends 40 away
        if (request.mPartial)
            mArrival2 = 1600.f;
        else
            mArrival2 = std::max(0.1f, request.mGoalRadius * request.mGoalRadius);
        // 0x9E27E0: the waypoint is passed at half the request's radius
        mPass = request.mRadius * 0.5f;
        // 0x9E77D0 with 0 and 0x9E3C00(0): the first point
        mT = 0.f;
        mSegment = 0;
        mWaypoint = pointAt(mPath, 0.f);
        mPrevious = mWaypoint;
        // 0x9E0757: a way shorter than the request's radius is walked without turning (+0xC1: its own heading)
        // shortcut: the owner's goal point (0x8B3D10; set from a package with a target, 0x8F74C0) is not ported, so it
        // never stops this
        mKeepHeading = !actor.mMounted && request.mRadius > lengthOf(mPath);
        if (reached(actor) == Result::Moving) // 0x9E07D5
        {
            // 0x9E07E5 to 0x9E08C8: it turns on the spot to the first point, unless that is almost straight up or down
            const Point to = minus(mWaypoint, actor.mPosition);
            const float heading = std::abs(to[2]) < 0.95f * std::sqrt(length2(to)) ? headingOf(to) : actor.mHeading;
            startTurn(actor, heading);
        }
        mState = 2; // 0x9E08EA
        return Result::Moving;
    }

    bool Follower::startTurn(const Actor& actor, float heading)
    {
        if (mKeepHeading)
            return false; // 0x9E7623
        // 0x4B15E0: the shorter way round
        const float d = wrap(heading - wrap(actor.mHeading));
        if (degree >= std::abs(d)) // 0x9E7719: 0x10923DC
            return false;
        mTurning = true;
        mTurnTo = wrap(heading);
        if (!actor.mNoTurnMinTime)
            mTurnTime = 0.3f; // fActorTurnAnimMinTime (0x11CD294)
        return true;
    }

    float Follower::turnStep(const Actor& actor, float dt)
    {
        const float h = wrap(actor.mHeading);
        const float d = wrap(mTurnTo - h);
        float heading = mTurnTo;
        // 0x9E7E8D to 0x9E7F0C: fAICombatTurnSpeedScale 2.5 when mounted, fAITurnSpeedScale 1.5, fCreature(Combat)
        // TurnSpeedScale 1.25
        const float scale = actor.mCreature ? 1.25f : (actor.mMounted ? 2.5f : 1.5f);
        const float step = actor.mTurnSpeed * degree * dt * scale;
        if (std::abs(d) > degree && !(step > std::abs(d)))
            heading = d > 0.f ? h + step : h - step;
        else if (mTurnTime <= 0.f)
            mTurning = false;
        mTurnTime -= dt; // 0x9E7FE0
        return heading;
    }

    Result Follower::reached(const Actor& actor) const
    {
        // NV 0x9E824A: the end of the path, near enough on the map
        const Point d = minus(mPath.back().mLocation.mPoint, actor.mPosition);
        const float dist2 = length2Across(d);
        if (!(dist2 <= mArrival2))
            return Result::Moving;
        // 0x9E83E4 to 0x9E84E0: not before it has passed the waypoint, unless it waits or is stuck
        if (mRequest.mHoldOff && mArrival2 > 25.f && dist2 > 25.f)
        {
            const Point along = minus(mWaypoint, mPrevious);
            const Point to = minus(mWaypoint, actor.mPosition);
            if (along[0] * to[0] + along[1] * to[1] >= 0.f && mWaitTime == 0.f && mStuckTime == 0.f)
                return Result::Moving;
        }
        if (!(std::abs(d[2]) > mRequest.mGoalHeight)) // 0x9E851E: request +0x70
            return Result::Arrived;
        if (mSegment + 1 >= static_cast<int>(mPath.size()))
            return Result::TooHigh; // "failed pathing due to goal Z difference" (0x10924B0)
        return Result::Moving;
    }

    void Follower::advance(const Actor& actor)
    {
        const int n = static_cast<int>(mPath.size());
        if (n == 0)
            return;
        if (mSegment + 1 >= n)
        {
            mWaypoint = mPath.back().mLocation.mPoint;
            return;
        }
        // 0x9E3D9B: only forward while it goes forward (the last frame's plan)
        if (mPlanned[1] < 0.f) // 0x9E3E20
            return;
        const Point world = rotate(mPlanned, actor.mHeading);
        const auto measure = [&](float& a, float& b, float& c) {
            Point d = minus(mWaypoint, actor.mPosition);
            d[2] = 0.f;
            a = world[0] * d[0] + world[1] * d[1];
            const Point next = minus(pointAt(mPath, mT + 0.1f), mWaypoint);
            b = next[0] * d[0] + next[1] * d[1];
            c = length2Across(d);
        };
        // 0x9E3FAA: four times the plan of a frame, at least +0x14
        const float threshold = std::max(4.f * length2(mPlanned), mPass * mPass);
        float a, b, c;
        measure(a, b, c);
        if (!(a < 0.f || b < 0.f || c < threshold))
            return;
        mPrevious = mWaypoint;
        const float last = static_cast<float>(n - 1);
        if (mT >= last)
        {
            mT = last;
            mSegment = static_cast<int>(mT);
            mWaypoint = mPath.back().mLocation.mPoint;
            return;
        }
        float before = a;
        mT += 0.1f; // 0x101FFA0
        mWaypoint = pointAt(mPath, mT);
        measure(a, b, c);
        // 0x9E4245: on while it is behind or near, and the dot does not shrink when it is behind
        while (true)
        {
            if (mT >= last)
            {
                mT = last;
                break;
            }
            if (a < 0.f || b < 0.f) // 0x9E42D1 to 0x9E4313
            {
                if (!(before <= a))
                    break;
            }
            else if (!(c < threshold))
                break;
            before = a;
            mT += 0.1f;
            mWaypoint = pointAt(mPath, mT);
            measure(a, b, c);
        }
        mSegment = static_cast<int>(mT);
    }

    float Follower::turn(const Actor& actor, float dt, float& slow) const
    {
        // NV 0x9E4450: its own heading on a short way, else toward the path a tenth of a segment on
        const float want = mKeepHeading ? actor.mHeading : headingOf(minus(pointAt(mPath, mT + 0.1f), actor.mPosition));
        const float h = wrap(actor.mHeading);
        const float c = wrap(wrap(want) - h);
        if (std::abs(c) < degree)
            return want;
        // 0x9E48F6 to 0x9E4A6B: a turner of more than 45 degrees a second turns as fast as the turn it is coming to
        // asks, within a gait of the waypoint
        float f = 1.f;
        if (actor.mTurnSpeed > 45.f)
        {
            const float l = std::sqrt(length2(minus(mWaypoint, actor.mPosition)));
            if (l < actor.mSpeed)
            {
                float t = 0.f;
                Point ahead{};
                along(mPath, mT, actor.mSpeed - l, t, ahead);
                const float to = std::abs(wrap(headingOf(minus(ahead, actor.mPosition)) - actor.mHeading));
                f = std::clamp(to / (pi / 2.f), 0.025f, 1.f); // 0x10313D4
            }
        }
        const float maxStep = actor.mTurnSpeed * degree * dt * 3.f * f; // 0x1021928
        if (maxStep < std::abs(c)) // 0x9E4AB4
        {
            const float a = std::abs(c);
            if (a > 2.7f) // 0x1092498
                slow = 0.05f;
            else if (a > 2.f) // 0x1011590
                slow = 0.1f;
            else if (a > 1.6f) // 0x106EFA0
                slow = 0.2f;
            else if (length2Across(minus(mPath.back().mLocation.mPoint, actor.mPosition))
                < actor.mRadius * actor.mRadius)
                slow = 0.5f;
        }
        return h + std::clamp(c, -maxStep, maxStep);
    }

    bool Follower::stuck(const Actor& actor, float dt, Frame& out)
    {
        if (mAvoidance == 3 || mAvoidance == 4)
            return true; // 0x9E4D28
        const bool notMoved
            = length2(minus(actor.mPosition, mLast)) < 0.125f * mPlannedLength * mPlannedLength; // 0x10924A8
        mLast = actor.mPosition;
        if (notMoved)
        {
            mStuckTime += actor.mBlockedByActor ? dt * 0.2f : dt; // 0x9E5088: 0x1018A90
            if (mState == 3)
            {
                mState = 4;
                return false;
            }
        }
        else if (!mStuck)
        {
            mStuckTime = 0.f;
            return true;
        }
        if (mStuckTime > 1.5f && actor.mMoving) // 0x9E5117: 0x1016FF0; 0x9E5176
            mStuck = true;
        if (!mStuck)
            return true;
        if (mAvoidance == 0 || mAvoidance == 1 || mAvoidance == 5) // 0x9E51A2
        {
            out.mMarkStuck = true;
            out.mStuckAt = mWaypoint;
        }
        return false;
    }

    Result Follower::update(const Actor& actor, float dt, Frame& out)
    {
        out = Frame{};
        out.mHeading = actor.mHeading;
        const auto cleanup = [&] { // 0x9E8790
            mPlanned = {};
            mPlannedLength = 0.f;
            out.mMovement = {};
        };
        if (mState == 5 || mState == 6)
            return Result::Arrived;
        if (!actor.mCanAct)
        {
            if (reached(actor) == Result::Arrived)
            {
                cleanup();
                mState = 5;
                return Result::Arrived;
            }
            return Result::Moving;
        }
        if (dt == 0.f)
        {
            cleanup();
            return Result::Moving;
        }
        if (!mTurning) // 0x9E0BBD
        {
            if (const Result r = reached(actor); r != Result::Moving)
            {
                // 0x9E7550: at the end of the last segment it first turns to the goal's heading
                const bool turns = r == Result::Arrived && mRequest.mGoalHeading
                    && mSegment + 1 >= static_cast<int>(mPath.size()) && startTurn(actor, *mRequest.mGoalHeading);
                if (!turns)
                {
                    cleanup();
                    mState = 5;
                    return r;
                }
            }
        }
        advance(actor); // 0x9E0C2E
        if (mTurning) // 0x9E0CAE
        {
            out.mHeading = turnStep(actor, dt);
            cleanup();
            return Result::Moving;
        }
        if (mState == 4)
        {
            cleanup();
            return Result::Moving;
        }
        if (!stuck(actor, dt, out))
        {
            if (mState == 4)
            {
                cleanup();
                return Result::Moving;
            }
            mAvoidance = 5;
            return Result::Stuck;
        }
        // 0x9E3520: forward at its gait
        mPlanned = { 0.f, actor.mSpeed * dt, 0.f };
        float slow = 0.f;
        const float heading = turn(actor, dt, slow);
        out.mHeading = heading;
        if (slow > 0.f)
            mPlanned = { mPlanned[0] * slow, mPlanned[1] * slow, mPlanned[2] * slow };
        // 0x9E37D7: toward the waypoint
        Point to = minus(mWaypoint, actor.mPosition);
        to[2] = 0.f;
        mPlanned = rotate(mPlanned, headingOf(to) - heading);
        mPlannedLength = std::sqrt(length2Across(mPlanned));
        out.mMovement = rotate(mPlanned, heading);
        // 0x9E14D8: on the last segment it goes no farther than the end
        if (mSegment + 1 >= static_cast<int>(mPath.size()))
        {
            const float d = std::sqrt(length2Across(minus(actor.mPosition, mPath.back().mLocation.mPoint)));
            if (mPlannedLength > d)
            {
                const float z = mPlanned[2];
                const float l = std::sqrt(length2(mPlanned));
                if (l > 0.f)
                    mPlanned = { mPlanned[0] / l * d, mPlanned[1] / l * d, z };
                mPlannedLength = std::sqrt(length2Across(mPlanned));
                // (the game also copies this unturned into +0xA0 at 0x9E1602, which nothing reads)
                out.mMovement = rotate(mPlanned, heading);
            }
        }
        return Result::Moving;
    }
}
