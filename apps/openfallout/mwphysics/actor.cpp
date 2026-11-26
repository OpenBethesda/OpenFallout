#include "actor.hpp"

#include <BulletCollision/CollisionShapes/btCapsuleShape.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/convert.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwworld/class.hpp"

#include "collisiontype.hpp"
#include "mtphysics.hpp"
#include "trace.h"

#include <algorithm>
#include <cmath>

namespace OFPhysics
{

    Actor::Actor(const OFWorld::Ptr& ptr, const CharacterShape& shape, PhysicsTaskScheduler* scheduler)
        : PtrHolder(ptr, ptr.getRefData().getPosition().asVec3())
        , mStandingOnPtr(nullptr)
        , mCanWaterWalk(false)
        , mWalkingOnWater(false)
        , mMeshTranslation(0.f, 0.f, shape.mHalfHeight)
        , mOriginalHalfExtents(shape.mRadius, shape.mRadius, shape.mHalfHeight)
        , mStuckFrames(0)
        , mLastStuckPosition{ 0, 0, 0 }
        , mForce(0.f, 0.f, 0.f)
        , mOnGround(true)
        , mOnSlope(false)
        , mInternalCollisionMode(true)
        , mExternalCollisionMode(true)
        , mActive(false)
        , mTaskScheduler(scheduler)
    {
        // The capsule: its cylinder is the height less the two half spheres (a radius larger than the half height
        // makes a sphere). It turns with nothing, and the navigator takes it for the cylinder of the same size.
        const float cylinder = std::max(0.f, 2.f * (shape.mHalfHeight - shape.mRadius));
        const float radius = std::min(shape.mRadius, shape.mHalfHeight);
        mShape = std::make_unique<btCapsuleShapeZ>(radius, cylinder);
        mRotationallyInvariant = true;
        mCollisionShapeType = DetourNavigator::CollisionShapeType::Cylinder;

        mConvexShape = static_cast<btConvexShape*>(mShape.get());
        mConvexShape->setMargin(0.001); // make sure bullet isn't using the huge default convex shape margin of 0.04

        mCollisionObject = std::make_unique<btCollisionObject>();
        mCollisionObject->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
        mCollisionObject->setActivationState(DISABLE_DEACTIVATION);
        mCollisionObject->setCollisionShape(mShape.get());
        mCollisionObject->setUserPointer(this);

        updateScaleUnsafe();

        if (!mRotationallyInvariant)
        {
            const SceneUtil::PositionAttitudeTransform* baseNode = mPtr.getRefData().getBaseNode();
            if (baseNode)
                mRotation = baseNode->getAttitude();
        }

        addCollisionMask(getCollisionMask());
        updateCollisionObjectPositionUnsafe();
    }

    Actor::~Actor()
    {
        mTaskScheduler->removeCollisionObject(mCollisionObject.get());
    }

    void Actor::enableCollisionMode(bool collision)
    {
        mInternalCollisionMode = collision;
    }

    void Actor::enableCollisionBody(bool collision)
    {
        if (mExternalCollisionMode != collision)
        {
            mExternalCollisionMode = collision;
            updateCollisionMask();
        }
    }

    void Actor::addCollisionMask(int collisionMask)
    {
        mTaskScheduler->addCollisionObject(mCollisionObject.get(), CollisionType_Actor, collisionMask);
    }

    void Actor::updateCollisionMask()
    {
        mTaskScheduler->setCollisionFilterMask(mCollisionObject.get(), getCollisionMask());
    }

    int Actor::getCollisionMask() const
    {
        int collisionMask = CollisionType_World | CollisionType_HeightMap;
        if (mExternalCollisionMode)
            collisionMask |= CollisionType_Actor | CollisionType_Projectile | CollisionType_Door;
        if (mCanWaterWalk)
            collisionMask |= CollisionType_Water;
        return collisionMask;
    }

    void Actor::updatePosition()
    {
        std::scoped_lock lock(mPositionMutex);
        const auto worldPosition = mPtr.getRefData().getPosition().asVec3();
        mPreviousPosition = worldPosition;
        mPosition = worldPosition;
        mSimulationPosition = worldPosition;
        mPositionOffset = osg::Vec3f();
        mStandingOnPtr = nullptr;
        mSkipSimulation = true;
        // A placed actor starts its count of stuck frames again. The unstuck step gives up after 10 frames stuck at one
        // place (MovementSolver::unstuck), and without this a teleport back to that place left the actor stuck for
        // good.
        mStuckFrames = 0;
        mLastStuckPosition = osg::Vec3f();
    }

    void Actor::setSimulationPosition(const osg::Vec3f& position)
    {
        if (!std::exchange(mSkipSimulation, false))
            mSimulationPosition = position;
    }

    osg::Vec3f Actor::getScaledMeshTranslation() const
    {
        return mRotation * osg::componentMultiply(mMeshTranslation, mScale);
    }

    void Actor::updateCollisionObjectPosition()
    {
        std::scoped_lock lock(mPositionMutex);
        updateCollisionObjectPositionUnsafe();
    }

    void Actor::updateCollisionObjectPositionUnsafe()
    {
        mShape->setLocalScaling(Misc::Convert::toBullet(mScale));
        osg::Vec3f newPosition = getScaledMeshTranslation() + mPosition;

        auto& trans = mCollisionObject->getWorldTransform();
        trans.setOrigin(Misc::Convert::toBullet(newPosition));
        trans.setRotation(Misc::Convert::toBullet(mRotation));
        mCollisionObject->setWorldTransform(trans);
    }

    osg::Vec3f Actor::getCollisionObjectPosition() const
    {
        std::scoped_lock lock(mPositionMutex);
        return getScaledMeshTranslation() + mPosition;
    }

    bool Actor::setPosition(const osg::Vec3f& position)
    {
        std::scoped_lock lock(mPositionMutex);
        const bool worldPositionChanged = mPositionOffset.length2() != 0;
        applyOffsetChange();
        if (worldPositionChanged || mSkipSimulation)
            return true;
        mPreviousPosition = mPosition;
        mPosition = position;
        return mPreviousPosition != mPosition;
    }

    void Actor::adjustPosition(const osg::Vec3f& offset)
    {
        std::scoped_lock lock(mPositionMutex);
        mPositionOffset += offset;
    }

    osg::Vec3f Actor::applyOffsetChange()
    {
        if (mPositionOffset.length2() != 0)
        {
            mPosition += mPositionOffset;
            mPreviousPosition += mPositionOffset;
            mSimulationPosition += mPositionOffset;
            mPositionOffset = osg::Vec3f();
        }
        return mPosition;
    }

    void Actor::setRotation(osg::Quat quat)
    {
        std::scoped_lock lock(mPositionMutex);
        mRotation = quat;
    }

    bool Actor::isRotationallyInvariant() const
    {
        return mRotationallyInvariant;
    }

    void Actor::updateScale()
    {
        std::scoped_lock lock(mPositionMutex);
        updateScaleUnsafe();
    }

    void Actor::updateScaleUnsafe()
    {
        float scale = mPtr.getCellRef().getScale();
        osg::Vec3f scaleVec(scale, scale, scale);

        mPtr.getClass().adjustScale(mPtr, scaleVec, false);
        mScale = scaleVec;
        mHalfExtents = osg::componentMultiply(mOriginalHalfExtents, scaleVec);

        scaleVec = osg::Vec3f(scale, scale, scale);
        mPtr.getClass().adjustScale(mPtr, scaleVec, true);
        mRenderingHalfExtents = osg::componentMultiply(mOriginalHalfExtents, scaleVec);
    }

    osg::Vec3f Actor::getHalfExtents() const
    {
        return mHalfExtents;
    }

    osg::Vec3f Actor::getOriginalHalfExtents() const
    {
        return mOriginalHalfExtents;
    }

    osg::Vec3f Actor::getRenderingHalfExtents() const
    {
        return mRenderingHalfExtents;
    }

    void Actor::setInertialForce(const osg::Vec3f& force)
    {
        mForce = force;
    }

    void Actor::setOnGround(bool grounded)
    {
        mOnGround = grounded;
    }

    void Actor::setOnSlope(bool slope)
    {
        mOnSlope = slope;
    }

    bool Actor::isWalkingOnWater() const
    {
        return mWalkingOnWater;
    }

    void Actor::setWalkingOnWater(bool walkingOnWater)
    {
        mWalkingOnWater = walkingOnWater;
    }

    void Actor::setCanWaterWalk(bool waterWalk)
    {
        if (waterWalk != mCanWaterWalk)
        {
            mCanWaterWalk = waterWalk;
            updateCollisionMask();
        }
    }

    OFWorld::Ptr Actor::getStandingOnPtr() const
    {
        std::scoped_lock lock(mPositionMutex);
        return mStandingOnPtr;
    }

    void Actor::setStandingOnPtr(const OFWorld::Ptr& ptr)
    {
        std::scoped_lock lock(mPositionMutex);
        mStandingOnPtr = ptr;
    }

    bool Actor::canMoveToWaterSurface(float waterlevel, const btCollisionWorld* world) const
    {
        const float halfZ = getHalfExtents().z();
        const osg::Vec3f actorPosition = getPosition();
        const osg::Vec3f startingPosition(actorPosition.x(), actorPosition.y(), actorPosition.z() + halfZ);
        const osg::Vec3f destinationPosition(actorPosition.x(), actorPosition.y(), waterlevel + halfZ);
        OFPhysics::ActorTracer tracer;
        tracer.doTrace(getCollisionObject(), startingPosition, destinationPosition, world);
        return (tracer.mFraction >= 1.0f);
    }

}
