#include "types.hpp"

#include <components/lua/util.hpp>

#include "../luamanagerimp.hpp"

#include "apps/openfallout/mwbase/inputmanager.hpp"
#include "apps/openfallout/mwbase/mechanicsmanager.hpp"
#include "apps/openfallout/mwbase/world.hpp"
#include "apps/openfallout/mwworld/class.hpp"
#include "apps/openfallout/mwworld/esmstore.hpp"
#include "apps/openfallout/mwworld/globals.hpp"
#include "apps/openfallout/mwworld/player.hpp"

namespace OFLua
{
    static void verifyPlayer(const Object& player)
    {
        if (player.ptr() != OFBase::Environment::get().getWorld()->getPlayerPtr())
            throw std::runtime_error("The argument must be a player!");
    }

    void addPlayerBindings(sol::table player, const Context& context)
    {
        sol::state_view lua = context.sol();

        player["CONTROL_SWITCH"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, std::string_view>(lua,
                {
                    { "Controls", "playercontrols" },
                    { "Fighting", "playerfighting" },
                    { "Jumping", "playerjumping" },
                    { "Looking", "playerlooking" },
                    { "Magic", "playermagic" },
                    { "ViewMode", "playerviewswitch" },
                    { "VanityMode", "vanitymode" },
                }));

        OFBase::InputManager* input = OFBase::Environment::get().getInputManager();
        player["getControlSwitch"] = [input](const Object& object, std::string_view key) {
            verifyPlayer(object);
            return input->getControlSwitch(key);
        };
        player["setControlSwitch"] = [input](const Object& object, std::string_view key, bool v) {
            verifyPlayer(object);
            if (object.isLObject() && !object.isSelfObject())
                throw std::runtime_error("Only player and global scripts can toggle control switches.");
            input->toggleControlSwitch(key, v);
        };
        player["isTeleportingEnabled"] = [](const Object& object) -> bool {
            verifyPlayer(object);
            return OFBase::Environment::get().getWorld()->isTeleportingEnabled();
        };
        player["setTeleportingEnabled"] = [](const Object& object, bool state) {
            verifyPlayer(object);
            if (object.isLObject() && !object.isSelfObject())
                throw std::runtime_error("Only player and global scripts can toggle teleportation.");
            OFBase::Environment::get().getWorld()->enableTeleporting(state);
        };
        player["sendMenuEvent"] = [context](const Object& object, std::string eventName, const sol::object& eventData) {
            verifyPlayer(object);
            context.mLuaEvents->addMenuEvent({ std::move(eventName), LuaUtil::serialize(eventData) });
        };

        player["isCharGenFinished"] = [](const Object&) -> bool {
            return OFBase::Environment::get().getWorld()->getGlobalFloat(OFWorld::Globals::sCharGenState) == -1;
        };
    }
}
