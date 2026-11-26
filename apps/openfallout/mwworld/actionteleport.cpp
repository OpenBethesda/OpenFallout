#include "actionteleport.hpp"

#include <components/esm3/loadcell.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/worldmodel.hpp"

#include "player.hpp"

namespace OFWorld
{
    ActionTeleport::ActionTeleport(ESM::RefId cellId, const ESM::Position& position)
        : Action(true)
        , mCellId(cellId)
        , mPosition(position)
    {
    }

    void ActionTeleport::executeImp(const Ptr& actor)
    {
        OFBase::World* world = OFBase::Environment::get().getWorld();
        OFWorld::WorldModel* worldModel = OFBase::Environment::get().getWorldModel();

        Ptr teleported;
        if (actor == world->getPlayerPtr())
        {
            // A fall in progress ends where the player arrives (PlayerCharacter::teleported)
            world->getPlayer().setTeleported(true);
            // IsPlayerMovingIntoNewSpace is on while a door or a fast travel moves the player
            OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
            if (scripts != nullptr)
                scripts->playerMoveBegins();
            // (the followers are told before the cell they are in is left)
            if (mCellId != world->getPlayerPtr().getCell()->getCell()->getId())
                OFBase::Environment::get().getMechanicsManager()->playerUsesDoor(mCellId, mPosition);
            world->changeToCell(mCellId, mPosition, true);
            if (scripts != nullptr)
                scripts->playerMoveEnds();
            teleported = world->getPlayerPtr();
        }
        else
            teleported = world->moveObject(actor, &worldModel->getCell(mCellId), mPosition.asVec3(), true, true);

        OFBase::Environment::get().getLuaManager()->objectTeleported(teleported);
    }
}
