#include "actor.hpp"

#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletCollision/CollisionShapes/btMultiSphereShape.h>

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
        , mCharacterShape(shape)
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
        // The body turns with the character (a lying body has a length, and the exe turns the offset of the
        // controller with it, SetPosition 0xC6E390); the navigator takes it for a cylinder of its half extents
        mRotationallyInvariant = false;
        mCollisionShapeType = DetourNavigator::CollisionShapeType::Cylinder;

        mCollisionObject = std::make_unique<btCollisionObject>();
        mCollisionObject->setCollisionFlags(btCollisionObject::CF_KINEMATIC_OBJECT);
        mCollisionObject->setActivationState(DISABLE_DEACTIVATION);
        mCollisionObject->setUserPointer(this);

        updateScaleUnsafe();

        const SceneUtil::PositionAttitudeTransform* baseNode = mPtr.getRefData().getBaseNode();
        if (baseNode)
            mRotation = baseNode->getAttitude();

        addCollisionMask(getCollisionMask());
        updateCollisionObjectPositionUnsafe();
    }

    void Actor::buildShape(float referenceScale)
    {
        namespace Body = FalloutScript::CharacterBody;
        // (a body without a model is not scaled, FalloutNV.exe 0xC72683)
        const double scale
            = mCharacterShape.mHasModel ? static_cast<double>(referenceScale) * mCharacterShape.mSize : 1.0;
        const Body::Body body = Body::body(mCharacterShape.mBound, scale > 0 ? scale : 1.0);
        mLying = body.mKind != Body::Kind::Upright;
        // shortcut: a body with legs is a list shape, so the exe reads [ctrl+0x5F0] for it as for the hull; the value
        // the builder stores there for it is not read, so it takes the body's capsule; read 0xC72834 to 0xC72F1F
        mBodyRadius = static_cast<float>(mLying ? body.mCapsules.front().mRadius : body.mHalfWidth);
        // The solver keeps the middle of the shape half its height over the feet
        const double middle = body.mHeight / 2;
        const auto at = [&](const Body::Point& point) {
            return btVector3(static_cast<btScalar>(point.mX), static_cast<btScalar>(point.mY),
                static_cast<btScalar>(point.mZ - middle));
        };
        const auto capsule = [&](const Body::Capsule& part) {
            const btVector3 ends[2] = { at(part.mA), at(part.mB) };
            const btScalar radii[2] = { static_cast<btScalar>(part.mRadius), static_cast<btScalar>(part.mRadius) };
            return std::make_unique<btMultiSphereShape>(ends, radii, 2);
        };

        std::unique_ptr<btCollisionShape> shape;
        std::vector<std::unique_ptr<btCollisionShape>> parts;
        if (body.mKind == Body::Kind::Upright)
        {
            auto hull = std::make_unique<btConvexHullShape>();
            for (const Body::Point& point : body.mHull)
                hull->addPoint(at(point), false);
            hull->recalcLocalAabb();
            hull->setMargin(0.001); // make sure bullet isn't using the huge default convex shape margin of 0.04
            shape = std::move(hull);
        }
        else if (body.mCapsules.size() == 1)
            shape = capsule(body.mCapsules.front());
        else
        {
            // The union of the capsules (a list shape in the exe); the sweeps of the solver take each part
            auto compound = std::make_unique<btCompoundShape>();
            for (const Body::Capsule& part : body.mCapsules)
            {
                parts.push_back(capsule(part));
                compound->addChildShape(btTransform::getIdentity(), parts.back().get());
            }
            shape = std::move(compound);
        }

        const float safeScale = referenceScale > 0.f ? referenceScale : 1.f;
        const float halfHeight = static_cast<float>(middle) / safeScale;
        mMeshTranslation = osg::Vec3f(0.f, 0.f, halfHeight);
        mOriginalHalfExtents = osg::Vec3f(static_cast<float>(body.mHalfWidth) / safeScale,
            static_cast<float>(body.mHalfLength) / safeScale, halfHeight);
        mCollisionObject->setCollisionShape(shape.get());
        // A sweep of a worker thread may still hold the shape it replaces (a SetScale): it is kept until the end
        if (mShape)
            mRetired.push_back(std::move(mShape));
        for (auto& part : mParts)
            mRetired.push_back(std::move(part));
        mShape = std::move(shape);
        mParts = std::move(parts);
        mBuiltScale = referenceScale;
        mStepHeight = static_cast<float>(body.mStepHeight);
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
        // (the bodies of the dead stop the living: CHARCONTROLLER and BIPED collide, NV 0xC828F0)
        int collisionMask = CollisionType_World | CollisionType_HeightMap | CollisionType_Ragdoll;
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
        // (the shape is made at the scale of the reference, buildShape)
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
        if (!mShape || scale != mBuiltScale)
            buildShape(scale);
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
