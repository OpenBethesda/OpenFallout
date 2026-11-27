#ifndef OPENFALLOUT_MWPHYSICS_ACTOR_H
#define OPENFALLOUT_MWPHYSICS_ACTOR_H

#include <memory>
#include <mutex>
#include <vector>

#include "ptrholder.hpp"

#include <components/detournavigator/collisionshapetype.hpp>
#include <components/falloutscript/characterbody.hpp>

#include <osg/Quat>
#include <osg/Vec3f>

class btCollisionShape;
class btCollisionWorld;

namespace Resource
{
    struct BulletShape;
}

namespace OFPhysics
{
    class PhysicsTaskScheduler;

    /// The body of a character on character physics, as the executables make it (rule A.C2 of
    /// docs/openfallout/fallout-rules.md, FalloutScript::CharacterBody::body): from the BSBound of its skeleton and
    /// its size (the height of its race, or the base scale of a creature; the scale of the reference is the physics'
    /// own, OFPhysics::Actor::updateScaleUnsafe)
    struct CharacterShape
    {
        FalloutScript::CharacterBody::Bound mBound = FalloutScript::CharacterBody::noModelBound();
        float mSize = 1.f;
        /// False for a character without a skeleton the engine can read: the size of no 3D, not scaled
        bool mHasModel = true;
    };

    class Actor final : public PtrHolder
    {
    public:
        Actor(const OFWorld::Ptr& ptr, const CharacterShape& shape, PhysicsTaskScheduler* scheduler);
        ~Actor() override;

        Actor(const Actor&) = delete;

        Actor& operator=(const Actor&) = delete;

        /**
         * Sets the collisionMode for this actor. If disabled, the actor can fly and clip geometry.
         */
        void enableCollisionMode(bool collision);

        bool getCollisionMode() const { return mInternalCollisionMode; }

        /**
         * Enables or disables the *external* collision body. If disabled, other actors will not collide with this
         * actor.
         */
        void enableCollisionBody(bool collision);

        void updateScale();
        void setRotation(osg::Quat quat);

        /**
         * Return true if the collision shape looks the same no matter how its Z rotated.
         */
        bool isRotationallyInvariant() const;

        /**
         * Used by the physics simulation to store the simulation result. Used in conjunction with mWorldPosition
         * to account for e.g. scripted movements
         */
        void setSimulationPosition(const osg::Vec3f& position);

        void updateCollisionObjectPosition();

        /**
         * Returns the half extents of the collision body (scaled according to collision scale)
         */
        osg::Vec3f getHalfExtents() const;

        /**
         * Returns the half extents of the collision body (not scaled)
         */
        osg::Vec3f getOriginalHalfExtents() const;

        /// How high an edge the body walks up, in units (CharacterBody::Body::mStepHeight)
        float getStepHeight() const { return mStepHeight; }

        /**
         * Returns the position of the collision body
         * @note The collision shape's origin is in its center, so the position returned can be described as center of
         * the actor collision box in world space.
         */
        osg::Vec3f getCollisionObjectPosition() const;

        /**
         * Store the current position into mPreviousPosition, then move to this position.
         * Returns true if the new position is different.
         */
        bool setPosition(const osg::Vec3f& position);

        // force set actor position to be as in Ptr::RefData
        void updatePosition();

        // register a position offset that will be applied during simulation.
        void adjustPosition(const osg::Vec3f& offset);

        // apply position offset. Can't be called during simulation
        osg::Vec3f applyOffsetChange();

        /**
         * Returns the half extents of the collision body (scaled according to rendering scale)
         * @note The reason we need this extra method is because of an inconsistency in MW - NPC race scales aren't
         * applied to the collision shape, most likely to make environment collision testing easier. However in some
         * cases (swimming level) we want the actual scale.
         */
        osg::Vec3f getRenderingHalfExtents() const;

        /**
         * Sets the current amount of inertial force (incl. gravity) affecting this physic actor
         */
        void setInertialForce(const osg::Vec3f& force);

        /**
         * Gets the current amount of inertial force (incl. gravity) affecting this physic actor
         */
        const osg::Vec3f& getInertialForce() const { return mForce; }

        void setOnGround(bool grounded);

        bool getOnGround() const { return mOnGround; }

        void setOnSlope(bool slope);

        bool getOnSlope() const { return mOnSlope; }

        /// Sets whether this actor should be able to collide with the water surface
        void setCanWaterWalk(bool waterWalk);

        /// Sets whether this actor has been walking on the water surface in the last frame
        void setWalkingOnWater(bool walkingOnWater);
        bool isWalkingOnWater() const;

        OFWorld::Ptr getStandingOnPtr() const;
        void setStandingOnPtr(const OFWorld::Ptr& ptr);

        unsigned int getStuckFrames() const { return mStuckFrames; }
        /// It ran into another actor in its last step of the solver
        bool touchedActor() const { return mTouchedActor; }
        void setTouchedActor(bool touched) { mTouchedActor = touched; }
        void setStuckFrames(unsigned int frames) { mStuckFrames = frames; }

        const osg::Vec3f& getLastStuckPosition() const { return mLastStuckPosition; }
        void setLastStuckPosition(osg::Vec3f position) { mLastStuckPosition = position; }

        bool canMoveToWaterSurface(float waterlevel, const btCollisionWorld* world) const;

        bool isActive() const { return mActive; }

        void setActive(bool value) { mActive = value; }

        DetourNavigator::CollisionShapeType getCollisionShapeType() const { return mCollisionShapeType; }

    private:
        OFWorld::Ptr mStandingOnPtr;
        /// Removes then re-adds the collision object to the dynamics world
        void updateCollisionMask();
        void addCollisionMask(int collisionMask);
        int getCollisionMask() const;

        /// Returns the mesh translation, scaled and rotated as necessary
        osg::Vec3f getScaledMeshTranslation() const;

        bool mCanWaterWalk;
        bool mWalkingOnWater;

        bool mRotationallyInvariant;

        DetourNavigator::CollisionShapeType mCollisionShapeType;

        /// Builds the body at the scale of the reference times the size (the constants of the exe's shape do not
        /// scale, so the shape is made again when the scale changes)
        void buildShape(float referenceScale);

        CharacterShape mCharacterShape;
        float mBuiltScale = 0.f;
        float mStepHeight = 0.f;
        std::unique_ptr<btCollisionShape> mShape;
        /// The parts of a body that is the union of several capsules (a btCompoundShape keeps no ownership)
        std::vector<std::unique_ptr<btCollisionShape>> mParts;
        std::vector<std::unique_ptr<btCollisionShape>> mRetired;

        osg::Vec3f mMeshTranslation;
        osg::Vec3f mOriginalHalfExtents;
        osg::Vec3f mHalfExtents;
        osg::Vec3f mRenderingHalfExtents;
        osg::Quat mRotation;

        osg::Vec3f mScale;
        osg::Vec3f mPositionOffset;
        bool mSkipSimulation = true;
        mutable std::mutex mPositionMutex;

        unsigned int mStuckFrames;
        bool mTouchedActor = false;
        osg::Vec3f mLastStuckPosition;

        osg::Vec3f mForce;
        bool mOnGround;
        bool mOnSlope;
        bool mInternalCollisionMode;
        bool mExternalCollisionMode;
        bool mActive;

        PhysicsTaskScheduler* mTaskScheduler;

        inline void updateScaleUnsafe();

        inline void updateCollisionObjectPositionUnsafe();
    };

}

#endif
