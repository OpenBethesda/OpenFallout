#ifndef OPENFALLOUT_MWPHYSICS_PHYSICSSYSTEM_H
#define OPENFALLOUT_MWPHYSICS_PHYSICSSYSTEM_H

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include <osg/BoundingBox>
#include <osg/Quat>
#include <osg/Timer>
#include <osg/ref_ptr>

#include <components/vfs/pathutil.hpp>

#include "../mwworld/ptr.hpp"

#include "collisiontype.hpp"
#include "raycasting.hpp"

namespace osg
{
    class Group;
    class Object;
    class Stats;
}

namespace OFRender
{
    class DebugDrawer;
}

namespace Resource
{
    class BulletShapeManager;
    class ResourceSystem;
}

class btCollisionWorld;
class btDiscreteDynamicsWorld;
class btSequentialImpulseConstraintSolver;
class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btCollisionObject;
class btCollisionShape;
class btTriangleMesh;
class btVector3;

namespace ESMTerrain
{
    class LandObject;
}

namespace osg
{
    class MatrixTransform;
}

namespace OFPhysics
{
    class HeightField;
    class Ragdoll;
    struct RagdollData;
    class Object;
    class Actor;
    struct CharacterShape;
    class PhysicsTaskScheduler;
    class Projectile;
    enum ScriptedCollisionType : char;

    using ActorMap = std::unordered_map<const OFWorld::LiveCellRefBase*, std::shared_ptr<Actor>>;

    struct ContactPoint
    {
        OFWorld::Ptr mObject;
        osg::Vec3f mPoint;
        osg::Vec3f mNormal;
    };

    struct LOSRequest
    {
        LOSRequest(const std::weak_ptr<Actor>& a1, const std::weak_ptr<Actor>& a2);
        std::array<std::weak_ptr<Actor>, 2> mActors;
        std::array<const Actor*, 2> mRawActors;
        bool mResult;
        bool mStale;
        int mAge;
    };
    bool operator==(const LOSRequest& lhs, const LOSRequest& rhs) noexcept;

    struct ActorFrameData
    {
        ActorFrameData(Actor& actor, bool inert, bool waterCollision, float slowFall, float waterlevel, bool isPlayer);
        osg::Vec3f mPosition;
        osg::Vec3f mInertia;
        const btCollisionObject* mStandingOn;
        bool mIsOnGround;
        bool mIsOnSlope;
        bool mWalkingOnWater;
        const bool mInert;
        btCollisionObject* mCollisionObject;
        const float mSwimLevel;
        const float mSlowFall;
        osg::Vec2f mRotation;
        osg::Vec3f mMovement;
        osg::Vec3f mLastStuckPosition;
        const float mWaterlevel;
        const float mHalfExtentsZ;
        const float mStepHeight;
        float mOldHeight;
        unsigned int mStuckFrames;
        /// It ran into another actor in its last step (as the character proxy's characterInteraction of FalloutNV.exe,
        /// 0xC6F500, which the controller update 0xC73170 clears first)
        bool mTouchedActor = false;
        const bool mFlying;
        const bool mWasOnGround;
        const bool mWaterCollision;
        const bool mSkipCollisionDetection;
        const bool mIsPlayer;
    };

    struct ProjectileFrameData
    {
        explicit ProjectileFrameData(Projectile& projectile);
        osg::Vec3f mPosition;
        osg::Vec3f mMovement;
        const btCollisionObject* mCaster;
        const btCollisionObject* mCollisionObject;
        Projectile* mProjectile;
    };

    template <class Ptr, class FrameData>
    class SimulationImpl
    {
    public:
        explicit SimulationImpl(const std::weak_ptr<Ptr>& ptr, FrameData&& data)
            : mPtr(ptr)
            , mData(data)
        {
        }

        std::optional<std::pair<std::shared_ptr<Ptr>, std::reference_wrapper<FrameData>>> lock()
        {
            if (auto locked = mPtr.lock())
                return { { std::move(locked), std::ref(mData) } };
            return std::nullopt;
        }

    private:
        std::weak_ptr<Ptr> mPtr;
        FrameData mData;
    };

    using ActorSimulation = SimulationImpl<Actor, ActorFrameData>;
    using ProjectileSimulation = SimulationImpl<Projectile, ProjectileFrameData>;
    using Simulation = std::variant<ActorSimulation, ProjectileSimulation>;

    class PhysicsSystem : public RayCastingInterface
    {
    public:
        PhysicsSystem(Resource::ResourceSystem* resourceSystem, osg::ref_ptr<osg::Group> parentNode);
        virtual ~PhysicsSystem();

        Resource::BulletShapeManager* getShapeManager();

        void enableWater(float height);
        void setWaterHeight(float height);
        void disableWater();

        /// The water of a cell of a worldspace of Fallout: a square of the size of the cell at the height of the
        /// surface. There is no one plane for the water of such a world.
        void addWaterTile(int gridX, int gridY, float height, float size);
        void removeWaterTile(int gridX, int gridY);

        void addObject(const OFWorld::Ptr& ptr, VFS::Path::NormalizedView mesh, osg::Quat rotation,
            int collisionType = CollisionType_World);
        /// The character body of the player, a character or a creature (the capsule of the character controller, moved
        /// by the movement solver while the actor is active)
        void addCharacter(const OFWorld::Ptr& ptr, const CharacterShape& shape);

        /// The ragdoll of a body that is down (fallout-rules.md A.K3): the rigid bodies and constraints of the
        /// skeleton file `model`, started from the pose of the bones that `findBone` gives by name. False when the file
        /// has none.
        bool startRagdoll(const OFWorld::Ptr& ptr, VFS::Path::NormalizedView model,
            const std::function<osg::ref_ptr<osg::MatrixTransform>(std::string_view)>& findBone);
        void stopRagdoll(const OFWorld::Ptr& ptr);
        bool hasRagdoll(const OFWorld::ConstPtr& ptr) const;

        int addProjectile(
            const OFWorld::Ptr& caster, const osg::Vec3f& position, VFS::Path::NormalizedView mesh, bool computeRadius);
        void setCaster(int projectileId, const OFWorld::Ptr& caster);
        void removeProjectile(const int projectileId);

        void updatePtr(const OFWorld::Ptr& old, const OFWorld::Ptr& updated);

        Actor* getActor(const OFWorld::Ptr& ptr);
        const Actor* getActor(const OFWorld::ConstPtr& ptr) const;

        const Object* getObject(const OFWorld::ConstPtr& ptr) const;

        Projectile* getProjectile(int projectileId) const;

        // Object or Actor
        void remove(const OFWorld::Ptr& ptr);

        void updateScale(const OFWorld::Ptr& ptr);
        void updateRotation(const OFWorld::Ptr& ptr, osg::Quat rotate);
        void updatePosition(const OFWorld::Ptr& ptr);

        void addHeightField(const float* heights, int x, int y, int size, int verts, float minH, float maxH,
            std::shared_ptr<const ESMTerrain::LandObject> holdObject);

        void removeHeightField(int x, int y);

        const HeightField* getHeightField(int x, int y) const;

        bool toggleCollisionMode();

        /// Determine new position based on all queued movements, then clear the list.
        void stepSimulation(
            float dt, bool skipSimulation, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats);

        /// Apply new positions to actors
        void moveActors();
        void debugDraw();

        std::vector<OFWorld::Ptr> getCollisions(const OFWorld::ConstPtr& ptr, int collisionGroup,
            int collisionMask) const; ///< get handles this object collides with
        std::vector<ContactPoint> getCollisionsPoints(
            const OFWorld::ConstPtr& ptr, int collisionGroup, int collisionMask) const;
        osg::Vec3f traceDown(const OFWorld::Ptr& ptr, const osg::Vec3f& position, float maxHeight);

        /// @param ignore Optional, a list of Ptr to ignore in the list of results. targets are actors to filter for,
        /// ignoring all other actors.
        RayCastingResult castRay(const osg::Vec3f& from, const osg::Vec3f& to,
            const std::vector<OFWorld::ConstPtr>& ignore = {}, const std::vector<OFWorld::Ptr>& targets = {},
            int mask = CollisionType_Default, int group = 0xff) const override;
        using RayCastingInterface::castRay;

        RayCastingResult castSphere(const osg::Vec3f& from, const osg::Vec3f& to, float radius,
            int mask = CollisionType_Default, int group = 0xff) const override;

        /// Return true if actor1 can see actor2.
        bool getLineOfSight(const OFWorld::ConstPtr& actor1, const OFWorld::ConstPtr& actor2) const override;

        bool isOnGround(const OFWorld::Ptr& actor);

        bool canMoveToWaterSurface(const OFWorld::ConstPtr& actor, const float waterlevel);

        /// Get physical half extents (scaled) of the given actor.
        osg::Vec3f getHalfExtents(const OFWorld::ConstPtr& actor) const;

        /// Get physical half extents (not scaled) of the given actor.
        osg::Vec3f getOriginalHalfExtents(const OFWorld::ConstPtr& actor) const;

        /// @see OFPhysics::Actor::getRenderingHalfExtents
        osg::Vec3f getRenderingHalfExtents(const OFWorld::ConstPtr& actor) const;

        /// Get the position of the collision shape for the actor. Use together with getHalfExtents() to get the
        /// collision bounds in world space.
        /// @note The collision shape's origin is in its center, so the position returned can be described as center of
        /// the actor collision box in world space.
        osg::Vec3f getCollisionObjectPosition(const OFWorld::ConstPtr& actor) const;

        /// Get bounding box in world space of the given object.
        osg::BoundingBox getBoundingBox(const OFWorld::ConstPtr& object) const;

        /// Queues velocity movement for a Ptr. If a Ptr is already queued, its velocity will
        /// be overwritten. Valid until the next call to stepSimulation
        void queueObjectMovement(const OFWorld::Ptr& ptr, const osg::Vec3f& velocity);

        /// Clear the queued movements list without applying.
        void clearQueuedMovement();

        /// Return true if \a actor has been standing on \a object in this frame
        /// This will trigger whenever the object is directly below the actor.
        /// It doesn't matter if the actor is stationary or moving.
        bool isActorStandingOn(const OFWorld::Ptr& actor, const OFWorld::ConstPtr& object) const;

        /// Get the handle of all actors standing on \a object in this frame.
        void getActorsStandingOn(const OFWorld::ConstPtr& object, std::vector<OFWorld::Ptr>& out) const;

        /// Return true if an object of the given type has collided with this object
        bool isObjectCollidingWith(const OFWorld::ConstPtr& object, ScriptedCollisionType type) const;

        /// Get the handle of all actors colliding with \a object in this frame.
        void getActorsCollidingWith(const OFWorld::ConstPtr& object, std::vector<OFWorld::Ptr>& out) const;

        bool toggleDebugRendering();

        /// Mark the given object as a 'non-solid' object. A non-solid object means that
        /// \a isOnSolidGround will return false for actors standing on that object.
        void markAsNonSolid(const OFWorld::ConstPtr& ptr);

        bool isOnSolidGround(const OFWorld::Ptr& actor) const;

        void updateAnimatedCollisionShape(const OFWorld::Ptr& object);

        template <class Function>
        void forEachAnimatedObject(Function&& function) const
        {
            std::for_each(mAnimatedObjects.begin(), mAnimatedObjects.end(), function);
        }

        bool isAreaOccupiedByOtherActor(
            const OFWorld::LiveCellRefBase* actor, const osg::Vec3f& position, float radius) const;

        void reportStats(unsigned int frameNumber, osg::Stats& stats) const;
        void reportCollision(const btVector3& position, const btVector3& normal);

        float mPhysicsDt;

    private:
        void updateWater();

        void prepareSimulation(bool willSimulate, std::vector<Simulation>& simulations);
        /// One step of the ragdolls: what is near them is copied into their world, the water pushes them, they move,
        /// and the bones follow
        void stepRagdolls(float dt);
        std::optional<float> waterHeightAt(const btVector3& position) const;

        std::unique_ptr<btBroadphaseInterface> mBroadphase;
        std::unique_ptr<btDefaultCollisionConfiguration> mCollisionConfiguration;
        std::unique_ptr<btCollisionDispatcher> mDispatcher;
        std::unique_ptr<btCollisionWorld> mCollisionWorld;
        std::unique_ptr<PhysicsTaskScheduler> mTaskScheduler;

        std::unique_ptr<Resource::BulletShapeManager> mShapeManager;
        Resource::ResourceSystem* mResourceSystem;

        using ObjectMap = std::unordered_map<const OFWorld::LiveCellRefBase*, std::shared_ptr<Object>>;
        ObjectMap mObjects;

        std::map<Object*, bool> mAnimatedObjects; // stores pointers to elements in mObjects

        ActorMap mActors;

        using ProjectileMap = std::map<int, std::shared_ptr<Projectile>>;
        ProjectileMap mProjectiles;

        using HeightFieldMap = std::map<std::pair<int, int>, std::unique_ptr<HeightField>>;
        HeightFieldMap mHeightFields;

        bool mDebugDrawEnabled;

        float mTimeAccum;

        unsigned int mProjectileId;

        float mWaterHeight;
        bool mWaterEnabled;

        std::unique_ptr<btCollisionObject> mWaterCollisionObject;
        std::unique_ptr<btCollisionShape> mWaterCollisionShape;

        struct WaterTile
        {
            // The order is the one in which they are destroyed: the object, then the shape, then the mesh of the shape
            std::unique_ptr<btTriangleMesh> mMesh;
            std::unique_ptr<btCollisionShape> mShape;
            std::unique_ptr<btCollisionObject> mObject;
            float mHeight = 0.f;
            float mSize = 0.f;
            int mX = 0;
            int mY = 0;
        };
        std::map<std::pair<int, int>, WaterTile> mWaterTiles;

        // The world of the ragdolls (destroyed after them: they are declared after it)
        std::unique_ptr<btDefaultCollisionConfiguration> mRagdollConfiguration;
        std::unique_ptr<btCollisionDispatcher> mRagdollDispatcher;
        std::unique_ptr<btBroadphaseInterface> mRagdollBroadphase;
        std::unique_ptr<btSequentialImpulseConstraintSolver> mRagdollSolver;
        std::unique_ptr<btDiscreteDynamicsWorld> mRagdollWorld;
        std::map<std::string, std::shared_ptr<const RagdollData>, std::less<>> mRagdollData;
        struct RagdollEntry
        {
            std::unique_ptr<Ragdoll> mRagdoll;
            std::string mLabel;
            float mAge = 0.f;
            bool mRested = false;
            /// Where the reference was, and its heading, when the bodies last went with it
            osg::Vec3f mAt;
            float mHeading = 0.f;
        };
        std::unordered_map<const OFWorld::LiveCellRefBase*, RagdollEntry> mRagdolls;
        /// The copies of what is near the ragdolls, by the object of the world of the living they copy
        struct RagdollMirror
        {
            std::unique_ptr<btCollisionObject> mObject;
            const btCollisionShape* mShape = nullptr;
        };
        std::unordered_map<const btCollisionObject*, RagdollMirror> mRagdollMirrors;

        std::unique_ptr<OFRender::DebugDrawer> mDebugDrawer;

        osg::ref_ptr<osg::Group> mParentNode;

        std::size_t mSimulationsCounter = 0;
        std::array<std::vector<Simulation>, 2> mSimulations;
        std::vector<std::pair<OFWorld::Ptr, osg::Vec3f>> mActorsPositions;

        PhysicsSystem(const PhysicsSystem&);
        PhysicsSystem& operator=(const PhysicsSystem&);
    };
}

#endif
