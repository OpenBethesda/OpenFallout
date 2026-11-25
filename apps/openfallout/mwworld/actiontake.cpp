#include "actiontake.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "class.hpp"
#include "containerstore.hpp"

namespace OFWorld
{
    ActionTake::ActionTake(const OFWorld::Ptr& object)
        : Action(true, object)
    {
    }

    void ActionTake::executeImp(const Ptr& actor)
    {
        int count = getTarget().getCellRef().getCount();
        if (getTarget().getClass().isGold(getTarget()))
            count *= getTarget().getClass().getValue(getTarget());

        OFBase::Environment::get().getMechanicsManager()->itemTaken(actor, getTarget(), OFWorld::Ptr(), count);
        OFWorld::Ptr newitem = *actor.getClass().getContainerStore(actor).add(getTarget(), count);
        OFBase::Environment::get().getWorld()->deleteObject(getTarget());
        setTarget(newitem);
    }
}
