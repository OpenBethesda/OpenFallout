#ifndef OPENFALLOUT_MWMECHANICS_FALLOUTACTOR_H
#define OPENFALLOUT_MWMECHANICS_FALLOUTACTOR_H

#include <memory>
#include <optional>

#include <components/falloutscript/processlevels.hpp>

#include "../mwworld/ptr.hpp"

#include "attacktype.hpp"
#include "movement.hpp"

namespace OFMechanics
{
    using FalloutScript::KnockState;
    using FalloutScript::LifeState;
    using FalloutScript::ProcessLevel;

    /// The part of an actor's life that the world keeps by its process tier (rule A.T1 of
    /// docs/openfallout/fallout-rules.md). Each tier is the one below with more: Low keeps what an actor far away needs,
    /// Middle High adds the knock state and the character controller, High is updated in every frame. The level is the
    /// one byte the executables keep at process+0x28.
    class BaseProcess
    {
    public:
        virtual ~BaseProcess() = default;

        ProcessLevel getLevel() const { return mLevel; }

        /// Low: the actor was alerted (LowProcess+0x30)
        bool mAlerted = false;

    protected:
        explicit BaseProcess(ProcessLevel level)
            : mLevel(level)
        {
        }

    private:
        ProcessLevel mLevel;
    };

    class LowProcess : public BaseProcess
    {
    public:
        LowProcess()
            : BaseProcess(ProcessLevel::Low)
        {
        }

    protected:
        explicit LowProcess(ProcessLevel level)
            : BaseProcess(level)
        {
        }
    };

    class MiddleLowProcess : public LowProcess
    {
    public:
        MiddleLowProcess()
            : LowProcess(ProcessLevel::MiddleLow)
        {
        }

    protected:
        explicit MiddleLowProcess(ProcessLevel level)
            : LowProcess(level)
        {
        }
    };

    class MiddleHighProcess : public MiddleLowProcess
    {
    public:
        MiddleHighProcess()
            : MiddleLowProcess(ProcessLevel::MiddleHigh)
        {
        }

        /// MiddleHighProcess+0x13C
        KnockState mKnockState = KnockState::Normal;
        /// The actor moves on character physics (the character controller, MiddleHighProcess+0x138)
        bool mCharacterController = false;

    protected:
        explicit MiddleHighProcess(ProcessLevel level)
            : MiddleLowProcess(level)
        {
        }
    };

    class HighProcess final : public MiddleHighProcess
    {
    public:
        HighProcess()
            : MiddleHighProcess(ProcessLevel::High)
        {
        }
    };

    /// A process of that level that keeps what the old one had of the tiers the two share (rule A.T3: going down drops
    /// the data of the higher tier, the Low data stays). Null for ProcessLevel::None.
    std::unique_ptr<BaseProcess> makeProcess(ProcessLevel level, const BaseProcess* old);

    /// A character or creature in the world, the player among them: the executables' Actor (rule A.1). What its kind
    /// does differently is in Character, Creature and PlayerCharacter. Its stats are Fallout's (FalloutScripts keeps
    /// them by the actor's reference); the actor holds what the world needs of it from frame to frame.
    class Actor
    {
    public:
        explicit Actor(const OFWorld::Ptr& ptr);
        virtual ~Actor();

        Actor(const Actor&) = delete;
        Actor& operator=(const Actor&) = delete;

        const OFWorld::Ptr& getPtr() const { return mPtr; }
        /// The actor moved to another cell, which makes it another reference
        void updatePtr(const OFWorld::Ptr& ptr) { mPtr = ptr; }

        /// vt+0x218 and vt+0x21C of the executables
        virtual bool isCharacter() const { return false; }
        virtual bool isCreature() const { return false; }
        virtual bool isPlayer() const { return false; }

        ProcessLevel getProcessLevel() const;
        BaseProcess* getProcess() { return mProcess.get(); }
        const BaseProcess* getProcess() const { return mProcess.get(); }
        /// MobileObject::SetProcessLevel (FalloutNV.exe 0x9319c0): replaces the process, keeping what the tiers share
        void setProcessLevel(ProcessLevel level);

        /// What the controls (the player) or the AI ask of the actor in this frame
        Movement& getMovement() { return mMovement; }
        const Movement& getMovement() const { return mMovement; }

        bool isSneaking() const { return mSneaking; }
        void setSneaking(bool sneaking) { mSneaking = sneaking; }
        bool isRunning() const { return mRunning; }
        void setRunning(bool running) { mRunning = running; }

        /// The speed the actor moved at in the last frame, in units a second (0 standing)
        float getCurrentSpeed() const { return mCurrentSpeed; }
        void setCurrentSpeed(float speed) { mCurrentSpeed = speed; }

        /// The knock state (Normal when the process has none: only Middle High and High keep it)
        KnockState getKnockState() const;
        void setKnockState(KnockState state);

    private:
        OFWorld::Ptr mPtr;
        Movement mMovement;
        std::unique_ptr<BaseProcess> mProcess;
        bool mSneaking = false;
        bool mRunning = false;
        float mCurrentSpeed = 0.f;
    };

    /// A character of an NPC_ record: it walks and runs by the formula of the speed (rule B.M1)
    class Character : public Actor
    {
    public:
        using Actor::Actor;

        bool isCharacter() const override { return true; }
    };

    /// A creature of a CREA record: it walks and runs at the speed of its animation (rule B.M12)
    class Creature final : public Actor
    {
    public:
        using Actor::Actor;

        bool isCreature() const override { return true; }
    };

    /// The player: PlayerRef (form 0x14 of the first Fallout plugin), whose base is NPC_ 0x7. It moves on character
    /// physics by the controls and the rules of parts B and C; see PlayerCharacter::update.
    class PlayerCharacter final : public Character
    {
    public:
        using Character::Character;

        bool isPlayer() const override { return true; }

        /// One frame of the player: the controls become a velocity by the speed rules (walk, run, sneak, swim), a jump
        /// is started, a fall is measured from where it began, and the velocity goes to the physics
        void update(float duration, bool paused);

        /// The height the player left the ground at, none while it stands (rule C.F1)
        std::optional<float> getFallStart() const { return mFallStart; }
        bool isSwimming() const { return mSwimming; }
        bool isInJump() const { return mInJump; }
        /// The speed of the stance the player is in (walk, run, sneak or swim, by the rules of part B), in units a
        /// second: what a full push of the controls moves it at on the ground
        float getStanceSpeed() const { return mStanceSpeed; }
        /// What the use control asks for (the fire of the weapon)
        AttackType getUse() const { return mUse; }
        /// The player was placed somewhere: a fall in progress does not count
        void teleported();

    private:
        /// What the built-in script of the controls asked for in this frame
        void takeControls();
        void updateSpeed(bool running, bool sneaking);
        void updateFall(bool onGround, bool weightless);

        std::optional<float> mFallStart;
        bool mWasOnGround = true;
        bool mSwimming = false;
        bool mInJump = false;
        bool mOverEncumbered = false;
        bool mOverEncumberedKnown = false;
        AttackType mUse = AttackType::NoAttack;
        float mStanceSpeed = 0.f;
    };

    /// The actor the world keeps for the reference: the player, or a character or creature of a loaded cell. Null for
    /// anything else.
    Actor* findActor(const OFWorld::Ptr& ptr);
}

#endif
