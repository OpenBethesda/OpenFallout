#include "types.hpp"

#include "usertypeutil.hpp"

#include <components/esm3/loadalch.hpp>
#include <components/lua/luastate.hpp>

namespace OFLua
{
    void addPotionBindings(sol::table potion, const Context& context)
    {
        // (the records of Morrowind's potions can be read; their effects and the making of new ones went with its
        // magic)
        addRecordFunctionBinding<ESM::Potion>(potion, context);
    }
}
