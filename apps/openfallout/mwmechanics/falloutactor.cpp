#include "falloutactor.hpp"

#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/falloutscript/movement.hpp>
#include <components/misc/constants.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwscript/falloutscripts.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/refdata.hpp"

namespace OFMechanics
{
    namespace
    {
        /// The share of the requested movement a character steers with while it is in the air (rule C.J2: NV keeps
        /// 0.3 as a constant at 0x11B0140 and a zero base; the scalar it is multiplied by in the controller is not
        /// known and is taken as 1)
        constexpr float airSteering = 0.3f;

        /// The time sOverEncumbered stays on the screen (rule B.M5c: 2 s)
        constexpr float overEncumberedMessageSeconds = 2.f;

        /// The process tiers keep the data of the lower ones: copies what `from` has of the tiers `to` has
        void carryOver(const BaseProcess& from, BaseProcess& to)
        {
            to.mAlerted = from.mAlerted;
            const auto* fromHigh = dynamic_cast<const MiddleHighProcess*>(&from);
            auto* toHigh = dynamic_cast<MiddleHighProcess*>(&to);
            if (fromHigh != nullptr && toHigh != nullptr)
            {
                toHigh->mKnockState = fromHigh->mKnockState;
                toHigh->mCharacterController = fromHigh->mCharacterController;
            }
        }
    }

    std::unique_ptr<BaseProcess> makeProcess(ProcessLevel level, const BaseProcess* old)
    {
        std::unique_ptr<BaseProcess> process;
        switch (level)
        {
            case ProcessLevel::High:
                process = std::make_unique<HighProcess>();
                break;
            case ProcessLevel::MiddleHigh:
                process = std::make_unique<MiddleHighProcess>();
                break;
            case ProcessLevel::MiddleLow:
                process = std::make_unique<MiddleLowProcess>();
                break;
            case ProcessLevel::Low:
                process = std::make_unique<LowProcess>();
                break;
            case ProcessLevel::None:
                return nullptr;
        }
        if (old != nullptr)
            carryOver(*old, *process);
        return process;
    }

    Actor::Actor(const OFWorld::Ptr& ptr)
        : mPtr(ptr)
    {
    }

    Actor::~Actor() = default;

    ProcessLevel Actor::getProcessLevel() const
    {
        return mProcess == nullptr ? ProcessLevel::None : mProcess->getLevel();
    }

    void Actor::setProcessLevel(ProcessLevel level)
    {
        if (level == getProcessLevel())
            return;
        // Going down drops the data of the tiers above (the knock state of a Middle High process among them); the
        // knock state of an actor that is not near is normal
        mProcess = makeProcess(level, mProcess.get());
    }

    KnockState Actor::getKnockState() const
    {
        if (const auto* process = dynamic_cast<const MiddleHighProcess*>(mProcess.get()))
            return process->mKnockState;
        return KnockState::Normal;
    }

    void Actor::setKnockState(KnockState state)
    {
        if (auto* process = dynamic_cast<MiddleHighProcess*>(mProcess.get()))
            process->mKnockState = state;
    }

    void PlayerCharacter::teleported()
    {
        mFallStart.reset();
        mLanding.reset();
        mWasOnGround = true;
        mInJump = false;
    }

    void PlayerCharacter::takeControls()
    {
        OFBase::LuaManager* lua = OFBase::Environment::get().getLuaManager();
        OFBase::LuaManager::ActorControls* controls = lua == nullptr ? nullptr : lua->getActorControls(getPtr());
        if (controls == nullptr)
            return;
        Movement& movement = getMovement();
        // The turn of the mouse that the input (OFWorld::Player::yaw and pitch) gathered since the last frame: it goes
        // to the Lua scripts (the camera reads it), not what they set the frame before
        const float pitch = movement.mRotation[0];
        const float yaw = movement.mRotation[2];
        // The built-in script of the controls (playercontrols.lua) says what the player asks for
        if (controls->mChanged)
        {
            movement.mPosition[0] = controls->mSideMovement;
            movement.mPosition[1] = controls->mMovement;
            if (controls->mJump)
                movement.mPosition[2] = 1;
            movement.mRotation[0] = controls->mPitchChange;
            movement.mRotation[1] = 0;
            movement.mRotation[2] = controls->mYawChange;
            setRunning(controls->mRun);
            setSneaking(controls->mSneak);
            mUse = controls->mUse;
            controls->mChanged = false;
        }
        controls->mPitchChange = pitch;
        controls->mYawChange = yaw;
    }

    void PlayerCharacter::update(float duration, bool paused)
    {
        const OFBase::Environment& env = OFBase::Environment::get();
        OFBase::World& world = *env.getWorld();
        const OFWorld::Ptr& ptr = getPtr();
        Movement& movement = getMovement();

        takeControls();
        if (paused || duration <= 0.f)
            return;

        OFScript::FalloutScripts* scripts = env.getFalloutScripts();
        const bool dead = scripts != nullptr && scripts->isDead(ptr);
        if (dead)
        {
            // A dead player moves no more and turns no more; the body falls where it is
            movement = Movement();
            mFallStart.reset();
            setCurrentSpeed(0.f);
            world.queueMovement(ptr, osg::Vec3f());
            return;
        }

        // The turn the mouse and the controls asked for
        const osg::Vec3f rotation(movement.mRotation[0], movement.mRotation[1], movement.mRotation[2]);
        movement.mRotation[0] = movement.mRotation[1] = movement.mRotation[2] = 0.f;
        if (rotation != osg::Vec3f())
            world.rotateObject(ptr, rotation, OFBase::RotationFlag_adjust);

        const bool onGround = world.isOnGround(ptr);
        mSwimming = world.isSwimming(ptr);
        const bool collides = world.isActorCollisionEnabled(ptr);
        const bool flying = world.isFlying(ptr);

        // The stance (rule B.M4: no sneaking while swimming); a script can force either
        const bool sneaking = (isSneaking() || (scripts != nullptr && scripts->actorForcesSneak(ptr))) && !mSwimming;
        const bool running = isRunning() || (scripts != nullptr && scripts->actorForcesRun(ptr));

        // The speed by the rules of the game
        updateSpeed(running, sneaking);

        osg::Vec3f velocity(movement.mPosition[0], movement.mPosition[1], 0.f);
        const float asked = std::min(velocity.length(), 1.f);
        if (asked > 0.f)
            velocity.normalize();
        velocity *= asked * mStanceSpeed;

        // A landing of this frame counts before a new jump starts: a jump on the frame of the landing does not take
        // the fall away (C.F1). A jump out of the water is over when the player is back in it and no longer rises.
        const float z = ptr.getRefData().getPosition().pos[2];
        if (mSwimming && z <= mLastZ)
            mInJump = false;
        mInJump = mInJump && !onGround;
        updateFall(onGround, flying || !collides);

        // A jump: the height of the rule, as a speed that takes the player there under gravity (C.J1); the speed the
        // player had is kept. A swimmer whose head is out of the water jumps twice as high (C.W2); the movement solver
        // lets that jump take it out of the water.
        const bool waterJump = mSwimming && !world.isSubmerged(ptr);
        if (movement.mPosition[2] > 0.f && (onGround || waterJump) && (!mSwimming || waterJump) && !flying && collides
            && !mInJump && !(scripts != nullptr && scripts->isDead(ptr)))
        {
            const double height = scripts != nullptr ? scripts->jumpHeight(ptr, mSwimming) : (mSwimming ? 128.0 : 64.0);
            const double gravity = Constants::GravityConst * Constants::UnitsPerMeter;
            velocity.z() = static_cast<float>(FalloutScript::Movement::jumpSpeed(height, gravity));
            mInJump = true;
            // (water ends a fall: a jump from it has no take-off height)
            if (!mSwimming)
                mFallStart = ptr.getRefData().getPosition().pos[2];
            if (scripts != nullptr)
                scripts->jumped(ptr);
            world.getPlayer().setJumping(true);
        }
        else if (!onGround && !mSwimming && !flying)
        {
            // In the air: only a share of what is asked steers (C.J2)
            velocity.x() *= airSteering;
            velocity.y() *= airSteering;
        }
        setCurrentSpeed(osg::Vec2f(velocity.x(), velocity.y()).length());
        // The physics takes the jump from the movement (PhysicsSystem's handleJump); a jump that did not start is
        // forgotten
        if (velocity.z() <= 0.f)
            movement.mPosition[2] = 0.f;

        world.queueMovement(ptr, velocity);
        movement.mPosition[0] = movement.mPosition[1] = 0.f;
    }

    void PlayerCharacter::updateSpeed(bool running, bool sneaking)
    {
        const OFBase::Environment& env = OFBase::Environment::get();
        OFScript::FalloutScripts* scripts = env.getFalloutScripts();
        if (scripts == nullptr)
        {
            mStanceSpeed = 0.f;
            return;
        }
        const OFWorld::Ptr& ptr = getPtr();
        mStanceSpeed = static_cast<float>(scripts->moveSpeed(ptr, running, sneaking, mSwimming));

        // The over-encumbered message shows when the player gets over the carry weight, once (B.M5c; nothing shows
        // when it gets under again)
        const bool over = scripts->overEncumbered(ptr);
        if (mOverEncumberedKnown && over && !mOverEncumbered)
        {
            const std::string text = scripts->gameSettingText("sOverEncumbered");
            if (!text.empty())
                scripts->hudMessages().push(text, {}, overEncumberedMessageSeconds);
        }
        mOverEncumbered = over;
        mOverEncumberedKnown = true;
    }

    void PlayerCharacter::updateFall(bool onGround, bool weightless)
    {
        const float z = getPtr().getRefData().getPosition().pos[2];
        // Water and flight end a fall without a landing (C.F4 has no exe basis for damage in water)
        if (mSwimming || weightless)
        {
            mFallStart.reset();
            mLanding.reset();
            mWasOnGround = onGround;
            mLastZ = z;
            return;
        }
        // The height the player left the ground at: the take-off of a jump (set there) or the edge it walked off
        // (rule C.F1)
        if (!onGround && mWasOnGround && !mFallStart)
            mFallStart = z;
        if (onGround && !mWasOnGround && mFallStart)
        {
            // The ground is touched: the height lost is judged where the player comes to stand (C.F1), a frame later
            mLanding = Landing{ *mFallStart, z, 0 };
            mFallStart.reset();
            mInJump = false;
        }
        else if (mLanding)
        {
            mLanding->mLowest = std::min(mLanding->mLowest, z);
            ++mLanding->mFrames;
            // Settled (the height no longer changes), off the ground again (a jump), or long enough
            constexpr float settled = 0.5f;
            constexpr int mostFrames = 8;
            if (!onGround || std::abs(z - mLastZ) < settled || mLanding->mFrames >= mostFrames)
            {
                const float lost = mLanding->mStart - mLanding->mLowest;
                mLanding.reset();
                if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                    scripts->landed(getPtr(), lost);
            }
        }
        // (a jump sets its take-off height after this, on the frame it starts)
        if (onGround && !mInJump)
            mFallStart.reset();
        mWasOnGround = onGround;
        mLastZ = z;
    }

    Actor* findActor(const OFWorld::Ptr& ptr)
    {
        if (ptr.isEmpty())
            return nullptr;
        OFBase::MechanicsManager* mechanics = OFBase::Environment::get().getMechanicsManager();
        return mechanics == nullptr ? nullptr : mechanics->getActor(ptr);
    }
}
