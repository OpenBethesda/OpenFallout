#include "ragdoll.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <unordered_map>

#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletDynamics/ConstraintSolver/btConeTwistConstraint.h>
#include <BulletDynamics/ConstraintSolver/btHingeConstraint.h>
#include <BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h>
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>

#include <osg/Transform>

#include <components/misc/convert.hpp>
#include <components/nif/node.hpp>
#include <components/nif/physics.hpp>
#include <components/nifbullet/havokshape.hpp>

#include "collisiontype.hpp"
#include "mtphysics.hpp"

namespace OFPhysics
{
    namespace
    {
        constexpr float havokScale = NifBullet::sHavokScale;

        // The groups of the world of the bodies
        constexpr int bodyGroup = 1;
        constexpr int staticGroup = 2;

        using FalloutScript::Ragdoll::bipedLayer;

        void visit(const Nif::NiAVObject& object, std::vector<RagdollData::Bone>& bones)
        {
            if (!object.mCollision.empty())
                if (const auto* collision = dynamic_cast<const Nif::bhkCollisionObject*>(object.mCollision.getPtr()))
                    if (!collision->mBody.empty())
                        if (const auto* body = dynamic_cast<const Nif::bhkRigidBody*>(collision->mBody.getPtr());
                            body != nullptr && !body->mShape.empty())
                            bones.push_back({ object.mName, body });
            if (const auto* node = dynamic_cast<const Nif::NiNode*>(&object))
                for (const auto& child : node->mChildren)
                    if (!child.empty())
                        visit(child.get(), bones);
        }

        btVector3 havokVector(const osg::Vec4f& value, float scale)
        {
            return btVector3(value.x(), value.y(), value.z()) * (havokScale * scale);
        }

        btVector3 direction(const osg::Vec4f& value)
        {
            btVector3 result(value.x(), value.y(), value.z());
            const btScalar length = result.length();
            return length > 0 ? result / length : btVector3(1, 0, 0);
        }

        /// A frame at `pivot` whose x axis is `x` and whose y axis is `y` made square to it
        btTransform frameOf(const btVector3& pivot, const btVector3& x, const btVector3& yWanted)
        {
            btVector3 z = x.cross(yWanted);
            if (z.length2() < 1e-12)
                z = x.cross(std::abs(x.x()) < 0.9 ? btVector3(1, 0, 0) : btVector3(0, 1, 0));
            z.normalize();
            const btVector3 y = z.cross(x);
            const btMatrix3x3 basis(x.x(), y.x(), z.x(), x.y(), y.y(), z.y(), x.z(), y.z(), z.z());
            return btTransform(basis, pivot);
        }

        /// The data of a constraint, unwrapped (a malleable constraint wraps one of the others)
        struct ConstraintData
        {
            Nif::HkConstraintType mType;
            const Nif::bhkRagdollConstraintCInfo* mRagdoll = nullptr;
            const Nif::bhkLimitedHingeConstraintCInfo* mLimitedHinge = nullptr;
            const Nif::bhkHingeConstraintCInfo* mHinge = nullptr;
            const Nif::bhkBallAndSocketConstraintCInfo* mBallAndSocket = nullptr;
        };

        std::optional<ConstraintData> unwrap(const Nif::bhkSerializable& constraint)
        {
            ConstraintData data;
            if (const auto* ragdoll = dynamic_cast<const Nif::bhkRagdollConstraint*>(&constraint))
            {
                data.mType = Nif::HkConstraintType::Ragdoll;
                data.mRagdoll = &ragdoll->mConstraint;
            }
            else if (const auto* limited = dynamic_cast<const Nif::bhkLimitedHingeConstraint*>(&constraint))
            {
                data.mType = Nif::HkConstraintType::LimitedHinge;
                data.mLimitedHinge = &limited->mConstraint;
            }
            else if (const auto* hinge = dynamic_cast<const Nif::bhkHingeConstraint*>(&constraint))
            {
                data.mType = Nif::HkConstraintType::Hinge;
                data.mHinge = &hinge->mConstraint;
            }
            else if (const auto* ball = dynamic_cast<const Nif::bhkBallAndSocketConstraint*>(&constraint))
            {
                data.mType = Nif::HkConstraintType::BallAndSocket;
                data.mBallAndSocket = &ball->mConstraint;
            }
            else if (const auto* malleable = dynamic_cast<const Nif::bhkMalleableConstraint*>(&constraint))
            {
                const Nif::bhkMalleableConstraintCInfo& wrapped = malleable->mConstraint;
                data.mType = wrapped.mType;
                data.mRagdoll = &wrapped.mRagdollInfo;
                data.mLimitedHinge = &wrapped.mLimitedHingeInfo;
                data.mHinge = &wrapped.mHingeInfo;
                data.mBallAndSocket = &wrapped.mBallAndSocketInfo;
            }
            else
                return std::nullopt;
            return data;
        }

        osg::Matrix toOsg(const btTransform& transform)
        {
            return osg::Matrix::rotate(Misc::Convert::toOsg(transform.getRotation()))
                * osg::Matrix::translate(Misc::Convert::toOsg(transform.getOrigin()));
        }
    }

    std::shared_ptr<const RagdollData> readRagdollData(Nif::NIFFilePtr file)
    {
        if (file == nullptr)
            return nullptr;
        auto data = std::make_shared<RagdollData>();
        data->mFile = file;
        for (const Nif::Record* root : file->mRoots)
            if (const auto* object = dynamic_cast<const Nif::NiAVObject*>(root))
                visit(*object, data->mBones);
        if (data->mBones.empty())
            return nullptr;
        std::unordered_map<const Nif::bhkEntity*, std::size_t> indexOf;
        for (std::size_t i = 0; i < data->mBones.size(); ++i)
            indexOf.emplace(data->mBones[i].mBody, i);
        std::set<const Nif::bhkSerializable*> seen;
        for (const RagdollData::Bone& bone : data->mBones)
            for (const auto& listed : bone.mBody->mConstraints)
            {
                if (listed.empty() || !seen.insert(listed.getPtr()).second)
                    continue;
                const auto* constraint = dynamic_cast<const Nif::bhkConstraint*>(listed.getPtr());
                if (constraint == nullptr || constraint->mInfo.mEntityA.empty() || constraint->mInfo.mEntityB.empty())
                    continue;
                const auto a = indexOf.find(constraint->mInfo.mEntityA.getPtr());
                const auto b = indexOf.find(constraint->mInfo.mEntityB.getPtr());
                if (a == indexOf.end() || b == indexOf.end() || a->second == b->second)
                    continue;
                data->mJoints.push_back({ a->second, b->second, constraint });
            }
        return data;
    }

    Ragdoll::Ragdoll(std::shared_ptr<const RagdollData> data,
        const std::vector<osg::ref_ptr<osg::MatrixTransform>>& bones, btDiscreteDynamicsWorld& world,
        PhysicsTaskScheduler& scheduler, const FalloutScript::Ragdoll::WaterSettings& water)
        : mData(std::move(data))
        , mWorld(world)
        , mScheduler(scheduler)
        , mSettings(water)
    {
        if (mData == nullptr || bones.size() != mData->mBones.size())
            return;
        // The size of the character: the scale of its first bone in the world
        std::vector<std::size_t> partOfBone(mData->mBones.size(), mData->mBones.size());
        for (std::size_t i = 0; i < mData->mBones.size(); ++i)
        {
            const osg::ref_ptr<osg::MatrixTransform>& bone = bones[i];
            if (bone == nullptr || bone->getParentalNodePaths().empty())
                continue;
            const Nif::bhkRigidBody& record = *mData->mBones[i].mBody;
            const Nif::bhkRigidBodyCInfo& info = record.mInfo;
            const osg::Matrix world = osg::computeLocalToWorld(bone->getParentalNodePaths().front());
            osg::Vec3d translation, scale;
            osg::Quat rotation, scaleOrientation;
            world.decompose(translation, rotation, scale, scaleOrientation);
            if (mParts.empty())
                mScale = static_cast<float>(scale.x());

            // A bhkRigidBodyT has a transform of its own in the frame of its bone (a bhkRigidBody has none)
            osg::Matrixf bodyTransform;
            if (record.mRecordType == Nif::RC_bhkRigidBodyT)
                bodyTransform = osg::Matrixf::rotate(info.mRotation)
                    * osg::Matrixf::translate(
                        osg::Vec3f(info.mTranslation.x(), info.mTranslation.y(), info.mTranslation.z()) * havokScale);
            std::vector<NifBullet::HavokPiece> pieces;
            if (!NifBullet::convertHavokShape(record.mShape.get(), bodyTransform, pieces).empty() || pieces.empty())
                continue;

            Part part;
            part.mBone = bone;
            part.mPart = record.mHavokFilter.mFlags & 0x1f;
            part.mLayer = record.mHavokFilter.mLayer;
            part.mCenter = btTransform(btQuaternion::getIdentity(), havokVector(info.mCenter, mScale));
            auto compound = std::make_unique<btCompoundShape>();
            const btTransform toCenter = part.mCenter.inverse();
            for (NifBullet::HavokPiece& piece : pieces)
            {
                piece.mShape->setLocalScaling(btVector3(mScale, mScale, mScale));
                osg::Vec3f pieceTranslation, pieceScale;
                osg::Quat pieceRotation, pieceScaleOrientation;
                piece.mTransform.decompose(pieceTranslation, pieceRotation, pieceScale, pieceScaleOrientation);
                const btTransform local(
                    Misc::Convert::toBullet(pieceRotation), Misc::Convert::toBullet(pieceTranslation) * mScale);
                compound->addChildShape(toCenter * local, piece.mShape.get());
                part.mPieces.push_back(std::move(piece.mShape));
            }
            std::vector<std::uint32_t> materials;
            if (const auto material = NifBullet::havokMaterialOf(record.mShape.get()))
                materials.push_back(*material);
            part.mBuoyancy = FalloutScript::Ragdoll::buoyancy(water, materials, record.mHavokFilter.mLayer, part.mPart);

            // The mass, the inertia (Havok units, kg m²), the damping, the friction and the bounce of the file
            const btScalar mass = info.mMass > 0 ? info.mMass : 1.f;
            const float unit = havokScale * mScale;
            btVector3 inertia(info.mInertiaTensor.mValues[0][0], info.mInertiaTensor.mValues[1][1],
                info.mInertiaTensor.mValues[2][2]);
            inertia *= unit * unit;
            if (inertia.x() <= 0 || inertia.y() <= 0 || inertia.z() <= 0)
                compound->calculateLocalInertia(mass, inertia);
            btRigidBody::btRigidBodyConstructionInfo construction(mass, nullptr, compound.get(), inertia);
            construction.m_linearDamping = std::clamp(info.mLinearDamping, 0.f, 1.f);
            construction.m_angularDamping = std::clamp(info.mAngularDamping, 0.f, 1.f);
            construction.m_friction = info.mFriction;
            construction.m_restitution = info.mRestitution;
            const btTransform boneFrame(
                Misc::Convert::toBullet(rotation), Misc::Convert::toBullet(osg::Vec3f(translation)));
            construction.m_startWorldTransform = boneFrame * part.mCenter;
            part.mBody = std::make_unique<btRigidBody>(construction);
            part.mMaxLinearVelocity = info.mMaxLinearVelocity * havokScale;
            part.mMaxAngularVelocity = info.mMaxAngularVelocity;
            part.mShape = std::move(compound);
            // A body of the filter with no collision (flag 0x40, bit 14 of the word) touches nothing (0xC84757)
            const bool collides = (record.mHavokFilter.mFlags & 0x40) == 0;
            mWorld.addRigidBody(part.mBody.get(), bodyGroup, collides ? (bodyGroup | staticGroup) : 0);
            part.mBody->setGravity(mWorld.getGravity() * info.mGravityFactor);

            // The copy that the living, the projectiles and the lines of sight meet
            part.mCopy = std::make_unique<btCollisionObject>();
            part.mCopy->setCollisionShape(part.mShape.get());
            part.mCopy->setWorldTransform(part.mBody->getWorldTransform());
            if (collides)
                mScheduler.addCollisionObject(
                    part.mCopy.get(), CollisionType_Ragdoll, CollisionType_AnyPhysical | CollisionType_Ragdoll);
            partOfBone[i] = mParts.size();
            mParts.push_back(std::move(part));
        }

        // Two parts of the body collide as the table of the parts says, when both are of the layer BIPED (0xC84794)
        for (std::size_t a = 0; a < mParts.size(); ++a)
            for (std::size_t b = a + 1; b < mParts.size(); ++b)
            {
                if (mParts[a].mLayer != bipedLayer || mParts[b].mLayer != bipedLayer
                    || FalloutScript::Ragdoll::partsCollide(mParts[a].mPart, mParts[b].mPart))
                    continue;
                mParts[a].mBody->setIgnoreCollisionCheck(mParts[b].mBody.get(), true);
                mParts[b].mBody->setIgnoreCollisionCheck(mParts[a].mBody.get(), true);
            }

        // The constraints of the file, in the frames of the bodies (their pivots and axes are in those of the bones)
        for (const RagdollData::Joint& joint : mData->mJoints)
        {
            const std::size_t a = partOfBone[joint.mA];
            const std::size_t b = partOfBone[joint.mB];
            if (a >= mParts.size() || b >= mParts.size())
                continue;
            const std::optional<ConstraintData> data = unwrap(*joint.mConstraint);
            if (!data)
                continue;
            btRigidBody& bodyA = *mParts[a].mBody;
            btRigidBody& bodyB = *mParts[b].mBody;
            const btTransform toA = mParts[a].mCenter.inverse();
            const btTransform toB = mParts[b].mCenter.inverse();
            std::unique_ptr<btTypedConstraint> made;
            switch (data->mType)
            {
                case Nif::HkConstraintType::Ragdoll:
                {
                    const Nif::bhkRagdollConstraintCInfo& info = *data->mRagdoll;
                    btTransform frameA = toA
                        * frameOf(havokVector(info.mDataA.mPivot, mScale), direction(info.mDataA.mTwist),
                            direction(info.mDataA.mPlane));
                    const btTransform frameB = toB
                        * frameOf(havokVector(info.mDataB.mPivot, mScale), direction(info.mDataB.mTwist),
                            direction(info.mDataB.mPlane));
                    // Bullet's twist is the same either way: the middle of the range of the file is its zero
                    const float middle = (info.mTwistMinAngle + info.mTwistMaxAngle) / 2;
                    frameA.setRotation(frameA.getRotation() * btQuaternion(btVector3(1, 0, 0), middle));
                    auto cone = std::make_unique<btConeTwistConstraint>(bodyA, bodyB, frameA, frameB);
                    cone->setLimit(info.mConeMaxAngle, info.mConeMaxAngle,
                        std::max(0.f, (info.mTwistMaxAngle - info.mTwistMinAngle) / 2));
                    made = std::move(cone);
                    break;
                }
                case Nif::HkConstraintType::LimitedHinge:
                case Nif::HkConstraintType::Hinge:
                {
                    const bool limited = data->mType == Nif::HkConstraintType::LimitedHinge;
                    const auto& dataA = limited ? data->mLimitedHinge->mDataA.mPivot : data->mHinge->mDataA.mPivot;
                    const auto& dataB = limited ? data->mLimitedHinge->mDataB.mPivot : data->mHinge->mDataB.mPivot;
                    const auto& axisA = limited ? data->mLimitedHinge->mDataA.mAxis : data->mHinge->mDataA.mAxis;
                    const auto& axisB = limited ? data->mLimitedHinge->mDataB.mAxis : data->mHinge->mDataB.mAxis;
                    const auto& perpA
                        = limited ? data->mLimitedHinge->mDataA.mPerpAxis1 : data->mHinge->mDataA.mPerpAxis1;
                    const auto& perpB
                        = limited ? data->mLimitedHinge->mDataB.mPerpAxis1 : data->mHinge->mDataB.mPerpAxis1;
                    // Bullet's hinge turns about the z axis of its frames, from their x axis
                    const auto hingeFrame = [this](const osg::Vec4f& pivot, const osg::Vec4f& axis,
                                                const osg::Vec4f& perp) {
                        const btTransform frame = frameOf(havokVector(pivot, mScale), direction(axis), direction(perp));
                        const btMatrix3x3& m = frame.getBasis();
                        const btVector3 x = m.getColumn(0), y = m.getColumn(1), z = m.getColumn(2);
                        // (x: the axis, y: the zero of the angle) to (z: the axis, x: the zero)
                        const btMatrix3x3 basis(y.x(), z.x(), x.x(), y.y(), z.y(), x.y(), y.z(), z.z(), x.z());
                        return btTransform(basis, frame.getOrigin());
                    };
                    auto hinge = std::make_unique<btHingeConstraint>(
                        bodyA, bodyB, toA * hingeFrame(dataA, axisA, perpA), toB * hingeFrame(dataB, axisB, perpB));
                    if (limited)
                        hinge->setLimit(data->mLimitedHinge->mMinAngle, data->mLimitedHinge->mMaxAngle);
                    made = std::move(hinge);
                    break;
                }
                case Nif::HkConstraintType::BallAndSocket:
                    made = std::make_unique<btPoint2PointConstraint>(bodyA, bodyB,
                        toA * havokVector(data->mBallAndSocket->mPivotA, mScale),
                        toB * havokVector(data->mBallAndSocket->mPivotB, mScale));
                    break;
                default:
                    break;
            }
            if (made == nullptr)
                continue;
            // Havok does not turn off the collision of the bodies it ties: the table of the parts does that
            mWorld.addConstraint(made.get(), false);
            mConstraints.push_back(std::move(made));
        }
    }

    Ragdoll::~Ragdoll()
    {
        for (const auto& constraint : mConstraints)
            mWorld.removeConstraint(constraint.get());
        for (Part& part : mParts)
        {
            mWorld.removeRigidBody(part.mBody.get());
            if (part.mCopy->getBroadphaseHandle() != nullptr)
                mScheduler.removeCollisionObject(part.mCopy.get());
        }
    }

    void Ragdoll::applyWater(
        const std::function<std::optional<float>(const btVector3&)>& waterAt, float dt, const btVector3& gravity)
    {
        for (Part& part : mParts)
        {
            btRigidBody& body = *part.mBody;
            btVector3 min, max;
            body.getAabb(min, max);
            const std::optional<float> water = waterAt((min + max) * 0.5);
            // A body all above the water is not in it (0x6301E3)
            if (!water || *water < min.z())
                continue;
            const float fraction = FalloutScript::Ragdoll::submergedFraction(min.z(), max.z(), *water);
            if (fraction <= 0.f)
                continue;
            const btScalar mass = body.getMass();
            const btVector3 velocity = body.getLinearVelocity();
            const btVector3 horizontal(velocity.x(), velocity.y(), 0);
            const float buoyancy = FalloutScript::Ragdoll::submergedBuoyancy(fraction, part.mBuoyancy);
            // The fall is damped unless the body floats near the surface and hardly moves across (0x6303C3)
            const bool dampFall = FalloutScript::Ragdoll::dampsFall(
                fraction, part.mBuoyancy, horizontal.length(), mSettings.mMaxSpeed * havokScale);
            // The water lifts the body by its buoyancy (at its centre of mass: a part of a ragdoll is tied by its
            // constraints, 0xC9C380) and takes its weight away (0x6307BF)
            body.applyCentralForce(gravity * (-buoyancy * mass));
            body.applyCentralForce(gravity * -mass);
            btVector3 damped = velocity;
            if (dampFall)
                damped.setZ(velocity.z() * std::exp(-mSettings.mLinearDampingV * dt));
            if (horizontal.length() > mSettings.mMaxSpeed * havokScale)
            {
                const float keep = std::exp(-mSettings.mLinearDampingH * dt);
                damped.setX(velocity.x() * keep);
                damped.setY(velocity.y() * keep);
            }
            body.setLinearVelocity(damped);
            body.setAngularVelocity(body.getAngularVelocity() * std::exp(-mSettings.mAngularDamping * dt));
            body.activate();
        }
    }

    void Ragdoll::sync()
    {
        for (Part& part : mParts)
        {
            btRigidBody& body = *part.mBody;
            // The fastest the file lets the body move
            if (part.mMaxLinearVelocity > 0 && body.getLinearVelocity().length() > part.mMaxLinearVelocity)
                body.setLinearVelocity(body.getLinearVelocity().normalized() * part.mMaxLinearVelocity);
            if (part.mMaxAngularVelocity > 0 && body.getAngularVelocity().length() > part.mMaxAngularVelocity)
                body.setAngularVelocity(body.getAngularVelocity().normalized() * part.mMaxAngularVelocity);

            const btTransform& transform = body.getWorldTransform();
            if (part.mCopy->getBroadphaseHandle() != nullptr)
                mScheduler.moveCollisionObject(part.mCopy.get(), transform);

            // The bone is where the body is, in the frame of its parent (which has the size of the character)
            osg::Group* parent = part.mBone->getNumParents() > 0 ? part.mBone->getParent(0) : nullptr;
            if (parent == nullptr || parent->getParentalNodePaths().empty())
                continue;
            const osg::Matrix parentWorld = osg::computeLocalToWorld(parent->getParentalNodePaths().front());
            const osg::Matrix boneWorld
                = osg::Matrix::scale(mScale, mScale, mScale) * toOsg(transform * part.mCenter.inverse());
            part.mBone->setMatrix(boneWorld * osg::Matrix::inverse(parentWorld));
        }
    }

    void Ragdoll::moveBy(const btTransform& moved)
    {
        for (Part& part : mParts)
        {
            btRigidBody& body = *part.mBody;
            const btTransform transform = moved * body.getWorldTransform();
            body.setWorldTransform(transform);
            body.setInterpolationWorldTransform(transform);
            body.setLinearVelocity(moved.getBasis() * body.getLinearVelocity());
            body.setAngularVelocity(moved.getBasis() * body.getAngularVelocity());
            body.activate(true);
            if (part.mCopy->getBroadphaseHandle() != nullptr)
                mScheduler.moveCollisionObject(part.mCopy.get(), transform);
        }
    }

    void Ragdoll::getAabb(btVector3& min, btVector3& max) const
    {
        min = btVector3(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT);
        max = -min;
        for (const Part& part : mParts)
        {
            btVector3 partMin, partMax;
            part.mBody->getAabb(partMin, partMax);
            min.setMin(partMin);
            max.setMax(partMax);
        }
    }

    bool Ragdoll::isAsleep() const
    {
        return std::none_of(mParts.begin(), mParts.end(), [](const Part& part) { return part.mBody->isActive(); });
    }

    btVector3 Ragdoll::rootPosition() const
    {
        return mParts.empty() ? btVector3(0, 0, 0) : mParts.front().mBody->getWorldTransform().getOrigin();
    }
}
