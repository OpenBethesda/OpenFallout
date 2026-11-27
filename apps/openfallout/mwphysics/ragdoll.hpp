#ifndef OPENFALLOUT_MWPHYSICS_RAGDOLL_H
#define OPENFALLOUT_MWPHYSICS_RAGDOLL_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <osg/MatrixTransform>
#include <osg/ref_ptr>

#include <LinearMath/btTransform.h>

#include <components/falloutscript/ragdollrules.hpp>
#include <components/nif/niffile.hpp>

class btCollisionObject;
class btCollisionShape;
class btRigidBody;
class btTypedConstraint;
class btDiscreteDynamicsWorld;

namespace Nif
{
    struct bhkRigidBody;
    struct bhkSerializable;
}

namespace OFPhysics
{
    class PhysicsTaskScheduler;

    /// The bodies and the constraints of a skeleton file that Havok moves when the character is down (fallout-rules.md
    /// A.K3): each bone that has a rigid body of its own, parents first, and the constraints that tie two of them
    struct RagdollData
    {
        struct Bone
        {
            std::string mName;
            const Nif::bhkRigidBody* mBody = nullptr;
        };
        struct Joint
        {
            std::size_t mA = 0;
            std::size_t mB = 0;
            const Nif::bhkSerializable* mConstraint = nullptr;
        };
        std::vector<Bone> mBones;
        std::vector<Joint> mJoints;
        Nif::NIFFilePtr mFile; // keeps the records alive
    };

    /// Reads them from a skeleton file; null when the file has no rigid body
    std::shared_ptr<const RagdollData> readRagdollData(Nif::NIFFilePtr file);

    /// A character's or a creature's body moved by Havok: the rigid bodies of its skeleton file, tied by their
    /// constraints, in a world of their own, with copies of what is near them, and copies of them in the world of the
    /// living, where they stop characters, projectiles and the lines of sight as bodies of the layer BIPED do
    class Ragdoll
    {
    public:
        /// `bones` are the nodes of the bones of `data`, in its order (null for one the skeleton has not)
        Ragdoll(std::shared_ptr<const RagdollData> data, const std::vector<osg::ref_ptr<osg::MatrixTransform>>& bones,
            btDiscreteDynamicsWorld& world, PhysicsTaskScheduler& scheduler,
            const FalloutScript::Ragdoll::WaterSettings& water);
        ~Ragdoll();
        Ragdoll(const Ragdoll&) = delete;
        Ragdoll& operator=(const Ragdoll&) = delete;

        /// Whether it has bodies to move
        bool valid() const { return !mParts.empty(); }

        /// Before a step: the push of the water on the bodies that are in it (NV 0x63017A)
        void applyWater(
            const std::function<std::optional<float>(const btVector3&)>& waterAt, float dt, const btVector3& gravity);

        /// After a step: the bones are where the bodies are, and their copies in the world of the living too
        void sync();

        /// The box around the bodies, for what is near them
        void getAabb(btVector3& min, btVector3& max) const;

        /// Whether the bodies have come to rest
        bool isAsleep() const;

        /// Where the first body is (the pelvis of a person)
        btVector3 rootPosition() const;

    private:
        struct Part
        {
            std::unique_ptr<btCollisionShape> mShape;
            std::vector<std::unique_ptr<btCollisionShape>> mPieces;
            std::unique_ptr<btRigidBody> mBody;
            std::unique_ptr<btCollisionObject> mCopy; // in the world of the living
            osg::ref_ptr<osg::MatrixTransform> mBone;
            btTransform mCenter; // from the frame of the bone to that of the body (its centre of mass)
            float mBuoyancy = 0.f;
            float mMaxLinearVelocity = 0.f;
            float mMaxAngularVelocity = 0.f;
            unsigned mPart = 0;
            unsigned mLayer = 0;
        };

        std::shared_ptr<const RagdollData> mData;
        btDiscreteDynamicsWorld& mWorld;
        PhysicsTaskScheduler& mScheduler;
        std::vector<Part> mParts;
        std::vector<std::unique_ptr<btTypedConstraint>> mConstraints;
        FalloutScript::Ragdoll::WaterSettings mSettings;
        float mScale = 1.f;
    };
}

#endif
