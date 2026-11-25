#include "playerstate.hpp"

#include <components/esm3/player.hpp>

#include <algorithm>
#include <map>

namespace FalloutScript
{
    namespace
    {
        /// Flag_Run and Flag_Sneak of the movement flags of a Morrowind character (CreatureStats::Flag)
        constexpr std::uint32_t oldRunFlag = 4;
        constexpr std::uint32_t oldSneakFlag = 8;

        void writeVector(SaveWriter& out, const std::array<float, 3>& value)
        {
            for (const float component : value)
                out.f32(component);
        }

        void readVector(SaveReader& in, std::array<float, 3>& value)
        {
            for (float& component : value)
                component = in.f32();
        }
    }

    void writePlayerState(const PlayerState& state, SaveWriter& out)
    {
        out.string(state.mCell);
        writeVector(out, state.mPosition);
        writeVector(out, state.mRotation);
        writeVector(out, state.mLastExteriorPosition);
        out.u32(state.mRefNum.mIndex);
        out.i32(state.mRefNum.mContentFile);
        out.boolean(state.mRunning);
        out.boolean(state.mSneaking);
        out.count(state.mLuaScripts.mScripts.size());
        for (const ESM::LuaScript& script : state.mLuaScripts.mScripts)
        {
            out.i32(script.mScriptId);
            out.string(script.mData);
            out.count(script.mTimers.size());
            for (const ESM::LuaTimer& timer : script.mTimers)
            {
                out.u8(static_cast<std::uint8_t>(timer.mType));
                out.f64(timer.mTime);
                out.string(timer.mCallbackName);
                out.string(timer.mCallbackArgument);
            }
        }
    }

    void readPlayerState(PlayerState& state, SaveReader& in, std::uint32_t version)
    {
        (void)version;
        state = PlayerState();
        state.mCell = in.string();
        readVector(in, state.mPosition);
        readVector(in, state.mRotation);
        readVector(in, state.mLastExteriorPosition);
        state.mRefNum.mIndex = in.u32();
        state.mRefNum.mContentFile = in.i32();
        state.mRunning = in.boolean();
        state.mSneaking = in.boolean();
        // (a script takes its id, its data and the count of its timers: 9 bytes at least)
        const std::size_t scripts = in.count(9);
        for (std::size_t i = 0; i < scripts; ++i)
        {
            ESM::LuaScript script;
            script.mScriptId = in.i32();
            script.mData = in.string();
            // (a timer takes its type, its time and two strings: 17 bytes at least)
            const std::size_t timers = in.count(17);
            for (std::size_t j = 0; j < timers; ++j)
            {
                ESM::LuaTimer timer;
                timer.mType = static_cast<ESM::LuaTimer::Type>(in.u8());
                timer.mTime = in.f64();
                timer.mCallbackName = in.string();
                timer.mCallbackArgument = in.string();
                script.mTimers.push_back(std::move(timer));
            }
            state.mLuaScripts.mScripts.push_back(std::move(script));
        }
    }

    ConvertedPlayer convertOldPlayer(const ESM::Player& player)
    {
        ConvertedPlayer result;
        PlayerState& state = result.mState;
        const ESM::NpcState& object = player.mObject;
        state.mCell = player.mCellId.serializeText();
        result.mExterior = player.mCellId.is<ESM::ESM3ExteriorCellRefId>();
        for (int i = 0; i < 3; ++i)
        {
            state.mPosition[i] = object.mPosition.pos[i];
            state.mRotation[i] = object.mPosition.rot[i];
            state.mLastExteriorPosition[i] = player.mLastKnownExteriorPosition[i];
        }
        state.mRefNum = object.mRef.mRefNum;
        state.mLuaScripts = object.mLuaScripts;
        state.mRunning = (object.mCreatureStats.mMovementFlags & oldRunFlag) != 0;
        state.mSneaking = (object.mCreatureStats.mMovementFlags & oldSneakFlag) != 0;

        // What the record had that the save keeps elsewhere or that Fallout does not have
        result.mDropped = {
            "the Morrowind stats (attributes, skills, health, magicka, fatigue, level): the Fallout actor values and "
            "progress are in FOSV",
            "the draw state and the forced run and sneak: the weapon drawn is in FOSV (equipment), the forced stance "
            "in "
            "FOSV (actoroverrides)",
            "the Morrowind inventory: the Fallout items are in FOSV (actors)",
            "the spells, active spells, magic effects, AI packages and the record of the player's NPC_",
        };
        if (player.mHasMark)
            result.mDropped.emplace_back("the Mark of the Morrowind spell");
        if (!player.mBirthsign.empty())
            result.mDropped.emplace_back("the birthsign");
        // A save of format 39 or older always has the stats kept for the werewolf, as zeros when the player never
        // was one
        const auto kept = [](const std::map<ESM::RefId, float>& values) {
            return std::any_of(values.begin(), values.end(), [](const auto& value) { return value.second != 0.f; });
        };
        if (object.mNpcStats.mIsWerewolf || kept(player.mSaveSkills) || kept(player.mSaveAttributes))
            result.mDropped.emplace_back("the werewolf state");
        if (!player.mPreviousItems.empty())
            result.mDropped.emplace_back("the items that bound items replaced");
        return result;
    }
}
