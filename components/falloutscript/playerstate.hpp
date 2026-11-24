#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PLAYERSTATE_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_PLAYERSTATE_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <components/esm/luascripts.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/refnum.hpp>

#include "savestate.hpp"

namespace ESM
{
    struct Player;
}

namespace FalloutScript
{
    /// The version of the "player" section of the save of the Fallout scripts (FOSV)
    constexpr std::uint32_t playerSectionVersion = 1;

    /// What a save keeps of the player that is not a stat of the games (those are in the other sections of FOSV): where
    /// it is, how it stands, and the data of its Lua scripts. It takes the place of the PLAY record of Morrowind, which
    /// kept them in the state of a Morrowind character.
    struct PlayerState
    {
        /// The cell, as the engine names cells (ESM::RefId::serializeText); empty for none
        std::string mCell;
        std::array<float, 3> mPosition{};
        std::array<float, 3> mRotation{};
        /// Where the player last was in an exterior (for an interior with no place in the world)
        std::array<float, 3> mLastExteriorPosition{};
        /// The reference the Lua scripts of the player belong to (LuaManager finds them by it)
        ESM::RefNum mRefNum;
        /// The stance the player asked for (the run toggle and the sneak)
        bool mRunning = false;
        bool mSneaking = false;
        /// The Lua scripts of the player, as their ids in the save's list of scripts
        ESM::LuaScripts mLuaScripts;
    };

    void writePlayerState(const PlayerState& state, SaveWriter& out);
    void readPlayerState(PlayerState& state, SaveReader& in, std::uint32_t version);

    /// What converting the PLAY record of an old save gives, with the names of what it had and the new save keeps
    /// elsewhere or rebuilds
    struct ConvertedPlayer
    {
        PlayerState mState;
        /// The cell the record named was an exterior one (its grid position is in the cell id)
        bool mExterior = false;
        /// What was in the record and is not taken: kept by the Fallout scripts already, or Morrowind's only
        std::vector<std::string> mDropped;
    };

    /// Reads what the player needs of the PLAY record of a save made before step 4 (the player was a Morrowind
    /// character): the cell, the place, the last exterior place, the reference and the Lua scripts, the run and sneak
    /// flags. The rest of the record (the Morrowind stats, spells, inventory, AI, birthsign, mark, crime ids, werewolf
    /// state) has no counterpart: the Fallout stats are in FOSV.
    ConvertedPlayer convertOldPlayer(const ESM::Player& player);
}

#endif
