#ifndef GAME_MWWORLD_PLAYER_H
#define GAME_MWWORLD_PLAYER_H

#include <cstdint>

#include <osg/Vec3f>

#include <components/esm4/loadnpc.hpp>

#include "../mwworld/livecellref.hpp"

#include "../mwmechanics/drawstate.hpp"

namespace ESM
{
    class ESMReader;
}

namespace FalloutScript
{
    struct PlayerState;
}

namespace OFWorld
{
    class CellStore;
    class ConstPtr;

    /// The player of Fallout 3, New Vegas and Tale of Two Wastelands: the reference PlayerRef (form 0x14 of the first
    /// Fallout plugin) of the NPC_ record 0x7 of that plugin, as the games have it. Its class is the player's own
    /// (OFClass::FalloutPlayer), its stats are the Fallout scripts' (actor values, perks, equipment), and its movement
    /// is the player's actor (OFMechanics::PlayerCharacter). What a save keeps of it here is in the "player" section of
    /// the record of the Fallout scripts (FOSV).
    class Player
    {
        LiveCellRef<ESM4::Npc> mPlayer;
        OFWorld::CellStore* mCellStore = nullptr;

        osg::Vec3f mLastKnownExteriorPosition;

        bool mTeleported = false;
        bool mJumping = false;

    public:
        /// \param player The NPC_ record 0x7 of the first Fallout plugin
        /// \param reference PlayerRef: form 0x14 of that plugin
        Player(const ESM4::Npc* player, ESM::RefNum reference);

        /// Interiors can not always be placed in the world: the map shows the player where it last was outside
        void setLastKnownExteriorPosition(const osg::Vec3f& position) { mLastKnownExteriorPosition = position; }
        osg::Vec3f getLastKnownExteriorPosition() const { return mLastKnownExteriorPosition; }

        void set(const ESM4::Npc* player);

        void setCell(OFWorld::CellStore* cellStore);

        OFWorld::Ptr getPlayer();
        OFWorld::ConstPtr getConstPlayer() const;

        /// The weapon out of its holster (the equipment of the Fallout scripts): Weapon or Nothing; Fallout has no
        /// stance of spells
        void setDrawState(OFMechanics::DrawState state);
        OFMechanics::DrawState getDrawState() const;

        /// Activate the object under the crosshair, if any
        void activate();

        void yaw(float yaw);
        void pitch(float pitch);
        void roll(float roll);

        bool wasTeleported() const;
        void setTeleported(bool teleported);

        void setJumping(bool jumping);
        bool getJumping() const;

        /// The player fights or is fought (the combat state of the Fallout scripts: StartCombat and the actors' own)
        bool isInCombat() const;

        /// The games refuse to rest or wait while an enemy fights the player
        bool enemiesNearby() const { return isInCombat(); }

        void clear();

        /// What the "player" section of a save keeps
        FalloutScript::PlayerState saveState() const;

        /// Restores what saveState gave (called while the save is read, before the world goes to the player's cell)
        void loadState(const FalloutScript::PlayerState& state);

        /// Reads the PLAY record of a save made when the player was a Morrowind character: it is converted once
        /// (FalloutScript::convertOldPlayer) and what it had that Fallout does not is dropped
        bool readRecord(ESM::ESMReader& reader, std::uint32_t type);

        void update();
    };
}
#endif
