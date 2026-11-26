#include "player.hpp"

#include <components/debug/debuglog.hpp>

#include <components/esm/defs.hpp>
#include <components/esm3/actoridconverter.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/player.hpp>
#include <components/esm4/loadachr.hpp>
#include <components/falloutscript/playerstate.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/falloutactor.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "cellstore.hpp"
#include "class.hpp"
#include "ptr.hpp"
#include "worldmodel.hpp"

namespace OFWorld
{
    namespace
    {
        ESM4::ActorCharacter makePlayerReference(const ESM4::Npc* player, ESM::RefNum reference)
        {
            ESM4::ActorCharacter result{};
            result.mId = reference;
            result.mFlags = 0;
            if (player != nullptr)
                result.mBaseObj = player->mId;
            result.mScale = 1.f;
            for (int i = 0; i < 3; ++i)
                result.mPos.pos[i] = result.mPos.rot[i] = 0.f;
            return result;
        }

        LiveCellRef<ESM4::Npc> makePlayer(const ESM4::Npc* player, ESM::RefNum reference)
        {
            LiveCellRef<ESM4::Npc> result(makePlayerReference(player, reference), player);
            // The player is not a character of a cell: it has its own class (OFClass::FalloutPlayer)
            result.mClass = &Class::get(ESM::REC_INTERNAL_PLAYER);
            return result;
        }

        ESM::Position positionOf(const std::array<float, 3>& position, const std::array<float, 3>& rotation)
        {
            ESM::Position result;
            for (int i = 0; i < 3; ++i)
            {
                result.pos[i] = position[i];
                result.rot[i] = rotation[i];
            }
            return result;
        }
    }

    Player::Player(const ESM4::Npc* player, ESM::RefNum reference)
        : mPlayer(makePlayer(player, reference))
        , mReference(reference)
        , mLastKnownExteriorPosition(0, 0, 0)
    {
    }

    void Player::set(const ESM4::Npc* player)
    {
        mPlayer.mBase = player;
    }

    void Player::setCell(OFWorld::CellStore* cellStore)
    {
        mCellStore = cellStore;
    }

    OFWorld::Ptr Player::getPlayer()
    {
        return OFWorld::Ptr(&mPlayer, mCellStore);
    }

    OFWorld::ConstPtr Player::getConstPlayer() const
    {
        return OFWorld::ConstPtr(&mPlayer, mCellStore);
    }

    void Player::setDrawState(OFMechanics::DrawState state)
    {
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (const FalloutScript::FormId player = scripts->playerReference(); player != 0)
                scripts->equipment().setWeaponDrawn(player, state == OFMechanics::DrawState::Weapon);
    }

    OFMechanics::DrawState Player::getDrawState() const
    {
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (scripts->isPlayerWeaponDrawn())
                return OFMechanics::DrawState::Weapon;
        return OFMechanics::DrawState::Nothing;
    }

    void Player::yaw(float yaw)
    {
        if (OFMechanics::Actor* actor = OFMechanics::findActor(getPlayer()))
            actor->getMovement().mRotation[2] += yaw;
    }

    void Player::pitch(float pitch)
    {
        if (OFMechanics::Actor* actor = OFMechanics::findActor(getPlayer()))
            actor->getMovement().mRotation[0] += pitch;
    }

    void Player::roll(float roll)
    {
        if (OFMechanics::Actor* actor = OFMechanics::findActor(getPlayer()))
            actor->getMovement().mRotation[1] += roll;
    }

    void Player::activate()
    {
        if (OFBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        OFWorld::Ptr player = getPlayer();
        // A dead, paralysed or knocked down player does not activate (the executables' activation checks the life
        // state and the knock state of the actor)
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (scripts->isDead(player) || scripts->isParalyzed(player))
                return;
        if (const OFMechanics::Actor* actor = OFMechanics::findActor(player))
            if (actor->getKnockState() != FalloutScript::KnockState::Normal)
                return;

        OFWorld::Ptr toActivate = OFBase::Environment::get().getWorld()->getFocusObject();

        if (toActivate.isEmpty())
            return;

        if (!toActivate.getClass().hasToolTip(toActivate))
            return;

        OFBase::Environment::get().getLuaManager()->objectActivated(toActivate, player);
    }

    bool Player::wasTeleported() const
    {
        return mTeleported;
    }

    void Player::setTeleported(bool teleported)
    {
        mTeleported = teleported;
        if (teleported)
            if (OFMechanics::Actor* actor = OFMechanics::findActor(getPlayer()); actor != nullptr && actor->isPlayer())
                static_cast<OFMechanics::PlayerCharacter*>(actor)->teleported();
    }

    void Player::setJumping(bool jumping)
    {
        mJumping = jumping;
    }

    bool Player::getJumping() const
    {
        return mJumping;
    }

    bool Player::isInCombat() const
    {
        OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
        if (scripts == nullptr)
            return false;
        const FalloutScript::FormId player = scripts->playerReference();
        return player != 0 && (scripts->combat().inCombat(player) || scripts->combat().isFought(player));
    }

    void Player::clear()
    {
        mPlayer = makePlayer(mPlayer.mBase, mReference);
        mCellStore = nullptr;
        mTeleported = false;
        mJumping = false;
        mLastKnownExteriorPosition = osg::Vec3f(0, 0, 0);
    }

    FalloutScript::PlayerState Player::saveState() const
    {
        FalloutScript::PlayerState state;
        if (mCellStore != nullptr)
            state.mCell = mCellStore->getCell()->getId().serializeText();
        const ESM::Position& position = mPlayer.mData.getPosition();
        for (int i = 0; i < 3; ++i)
        {
            state.mPosition[i] = position.pos[i];
            state.mRotation[i] = position.rot[i];
        }
        state.mLastExteriorPosition
            = { mLastKnownExteriorPosition.x(), mLastKnownExteriorPosition.y(), mLastKnownExteriorPosition.z() };
        state.mRefNum = mPlayer.mRef.getRefNum();
        OFWorld::Ptr self(const_cast<LiveCellRef<ESM4::Npc>*>(&mPlayer), mCellStore);
        if (const OFMechanics::Actor* actor = OFMechanics::findActor(self))
        {
            state.mRunning = actor->isRunning();
            state.mSneaking = actor->isSneaking();
        }
        OFBase::Environment::get().getLuaManager()->saveLocalScripts(self, state.mLuaScripts);
        return state;
    }

    void Player::loadState(const FalloutScript::PlayerState& state)
    {
        OFWorld::WorldModel& worldModel = *OFBase::Environment::get().getWorldModel();

        // The reference number is the one the Lua scripts of the save know the player by. For a save of this version
        // it is PlayerRef, and the Lua data of the save is moved to the plugin's index of this load order
        // (OFLua::Serializer::adjustRefNum), so it is this run's PlayerRef. An older save has the number the engine
        // gave the Morrowind character (no content file), which is kept as it is.
        worldModel.deregisterLiveCellRef(mPlayer);
        mPlayer.mRef.setRefNum(state.mRefNum.isSet() && !state.mRefNum.hasContentFile() ? state.mRefNum : mReference);
        const ESM::Position position = positionOf(state.mPosition, state.mRotation);
        mPlayer.mRef.setPosition(position);
        mPlayer.mData.setPosition(position);
        mPlayer.mData.enable();
        worldModel.registerPtr(getPlayer());

        mCellStore = nullptr;
        if (!state.mCell.empty())
        {
            const ESM::RefId cell = ESM::RefId::deserializeText(state.mCell);
            mCellStore = worldModel.findCell(cell);
            if (mCellStore == nullptr)
                Log(Debug::Warning) << "Player cell " << cell << " no longer exists";
        }

        mLastKnownExteriorPosition = osg::Vec3f(
            state.mLastExteriorPosition[0], state.mLastExteriorPosition[1], state.mLastExteriorPosition[2]);
        mTeleported = false;
        mJumping = false;

        OFBase::Environment::get().getLuaManager()->loadLocalScripts(getPlayer(), state.mLuaScripts);

        if (OFMechanics::Actor* actor = OFMechanics::findActor(getPlayer()))
        {
            actor->setRunning(state.mRunning);
            actor->setSneaking(state.mSneaking);
        }
    }

    bool Player::readRecord(ESM::ESMReader& reader, std::uint32_t type)
    {
        if (type != ESM::REC_PLAY)
            return false;

        ESM::Player player;
        player.load(reader);

        const FalloutScript::ConvertedPlayer converted = FalloutScript::convertOldPlayer(player);
        loadState(converted.mState);
        if (reader.mActorIdConverter)
            reader.mActorIdConverter->mMappings.emplace(
                player.mObject.mCreatureStats.mActorId, mPlayer.mRef.getRefNum());

        Log(Debug::Info) << "The save was made when the player was a Morrowind character: its place, its Lua scripts "
                            "and its stance were taken from it (PLAY)";
        for (const std::string& dropped : converted.mDropped)
            Log(Debug::Info) << "  not taken: " << dropped;
        return true;
    }

    void Player::update()
    {
        OFWorld::Ptr player = getPlayer();
        if (player.getCell() != nullptr && player.getCell()->isExterior())
            setLastKnownExteriorPosition(player.getRefData().getPosition().asVec3());
    }
}
