#ifndef GAME_MWWORLD_ACTIONTELEPORT_H
#define GAME_MWWORLD_ACTIONTELEPORT_H

#include <components/esm/defs.hpp>
#include <components/esm/refid.hpp>

#include "action.hpp"

namespace OFWorld
{
    /// Moves an actor to a place in a cell. The followers of the player come after it by their own packages (the
    /// follow package of the Fallout AI goes through the door the player took), not with this action.
    class ActionTeleport : public Action
    {
        ESM::RefId mCellId;
        ESM::Position mPosition;

        void executeImp(const Ptr& actor) override;

    public:
        ActionTeleport(ESM::RefId cellId, const ESM::Position& position);
    };
}

#endif
