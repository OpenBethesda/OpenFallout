#include "actorutil.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/ptr.hpp"

namespace OFMechanics
{
    OFWorld::Ptr getPlayer()
    {
        return OFBase::Environment::get().getWorld()->getPlayerPtr();
    }
}
