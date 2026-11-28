#include "stepper.hpp"

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>
#include <BulletCollision/CollisionDispatch/btCollisionWorld.h>

#include <components/misc/constants.hpp>

#include "collisiontype.hpp"
#include "constants.hpp"
#include "movementsolver.hpp"

namespace OFPhysics
{
    static bool canStepDown(const ActorTracer& stepper)
    {
        if (!stepper.mHitObject)
            return false;
        static const float sMaxSlopeCos = std::cos(osg::DegreesToRadians(Constants::sMaxSlope));
        if (stepper.mPlaneNormal.z() <= sMaxSlopeCos)
            return false;

        return stepper.mHitObject->getBroadphaseHandle()->m_collisionFilterGroup != CollisionType_Actor;
    }

    Stepper::Stepper(const btCollisionWorld* colWorld, const btCollisionObject* colObj, float stepHeight)
        : mColWorld(colWorld)
        , mColObj(colObj)
        , mStepHeight(stepHeight)
    {
    }

    bool Stepper::step(
        osg::Vec3f& position, osg::Vec3f& velocity, float& remainingTime, const bool& onGround, bool firstIteration)
    {
        if (velocity.x() == 0.0 && velocity.y() == 0.0)
            return false;

        // Stairstepping algorithms work by moving up to avoid the step, moving forwards, then moving back down onto the
        // ground. This algorithm has a couple of minor problems, but they don't cause problems for sane geometry, and
        // just prevent stepping on insane geometry.

        mUpStepper.doTrace(mColObj, position, position + osg::Vec3f(0.0f, 0.0f, mStepHeight), mColWorld, onGround);

        float upDistance = 0;
        if (!mUpStepper.mHitObject)
            upDistance = mStepHeight;
        else if (mUpStepper.mFraction * mStepHeight > sCollisionMargin)
            upDistance = mUpStepper.mFraction * mStepHeight - sCollisionMargin;
        else
        {
            return false;
        }

        auto toMove = velocity * remainingTime;

        osg::Vec3f tracerPos = position + osg::Vec3f(0.0f, 0.0f, upDistance);

        osg::Vec3f tracerDest;
        auto normalMove = toMove;
        auto moveDistance = normalMove.normalize();
        // One try along the movement (OpenMW's two fixed-distance retries for Morrowind's stairs are gone: the exe's
        // proxy has none), then down onto the step: never below where the body started, as the exe has no step down
        tracerDest = tracerPos + toMove;
        mTracer.doTrace(mColObj, tracerPos, tracerDest, mColWorld);
        if (mTracer.mHitObject)
        {
            // map against what we hit, minus the safety margin
            moveDistance *= mTracer.mFraction;
            if (moveDistance <= sCollisionMargin) // didn't move enough to accomplish anything
                return false;

            moveDistance -= sCollisionMargin;
            tracerDest = tracerPos + normalMove * moveDistance;

            // safely eject from what we hit by the safety margin
            auto tempDest = tracerDest + mTracer.mPlaneNormal * sCollisionMargin * 2;

            ActorTracer tempTracer;
            tempTracer.doTrace(mColObj, tracerDest, tempDest, mColWorld);

            if (tempTracer.mFraction > 0.5f) // distance to any object is greater than sCollisionMargin (we checked
                                             // sCollisionMargin*2 distance)
            {
                auto effectiveFraction = tempTracer.mFraction * 2.0f - 1.0f;
                tracerDest += mTracer.mPlaneNormal * sCollisionMargin * effectiveFraction;
            }
        }

        const float downStepSize = upDistance;
        mDownStepper.doTrace(
            mColObj, tracerDest, tracerDest + osg::Vec3f(0.0f, 0.0f, -downStepSize), mColWorld, onGround);

        // can't step down onto air, non-walkable-slopes, or actors
        if (!canStepDown(mDownStepper))
            return false;

        // note: can't downstep onto actors so no need to pick safety margin
        float downDistance = 0;
        if (mDownStepper.mFraction * downStepSize > sCollisionMargin)
            downDistance = mDownStepper.mFraction * downStepSize - sCollisionMargin;

        if (downDistance - sCollisionMargin - sGroundOffset > upDistance && !onGround)
            return false;

        auto newpos = tracerDest + osg::Vec3f(0.0f, 0.0f, -downDistance);

        if ((position - newpos).length2() < sCollisionMargin * sCollisionMargin)
            return false;

        if (mTracer.mHitObject)
        {
            auto planeNormal = mTracer.mPlaneNormal;
            if (onGround && !isWalkableSlope(planeNormal) && planeNormal.z() != 0)
            {
                planeNormal.z() = 0;
                planeNormal.normalize();
            }
            velocity = reject(velocity, planeNormal);
        }
        velocity = reject(velocity, mDownStepper.mPlaneNormal);

        position = newpos;

        remainingTime *= (1.0f - mTracer.mFraction); // remaining time is proportional to remaining distance
        return true;
    }
}
