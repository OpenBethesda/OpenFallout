#include "types.hpp"

#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/lua/util.hpp>

#include "../birthsignbindings.hpp"
#include "../luamanagerimp.hpp"

#include "apps/openfallout/mwbase/inputmanager.hpp"
#include "apps/openfallout/mwbase/mechanicsmanager.hpp"
#include "apps/openfallout/mwbase/world.hpp"
#include "apps/openfallout/mwmechanics/npcstats.hpp"
#include "apps/openfallout/mwworld/class.hpp"
#include "apps/openfallout/mwworld/esmstore.hpp"
#include "apps/openfallout/mwworld/globals.hpp"
#include "apps/openfallout/mwworld/player.hpp"

namespace
{
    ESM::RefId toBirthSignId(const sol::object& recordOrId)
    {
        if (recordOrId.is<ESM::BirthSign>())
            return recordOrId.as<const ESM::BirthSign*>()->mId;
        std::string_view textId = LuaUtil::cast<std::string_view>(recordOrId);
        ESM::RefId id = ESM::RefId::deserializeText(textId);
        if (!OFBase::Environment::get().getESMStore()->get<ESM::BirthSign>().search(id))
            throw std::runtime_error("Failed to find birth sign: " + std::string(textId));
        return id;
    }

    ESM::RefId parseFactionId(std::string_view faction)
    {
        ESM::RefId id = ESM::RefId::deserializeText(faction);
        if (!OFBase::Environment::get().getESMStore()->get<ESM::Faction>().search(id))
            return ESM::RefId();
        return id;
    }
}

namespace OFLua
{
    static void verifyPlayer(const Object& player)
    {
        if (player.ptr() != OFBase::Environment::get().getWorld()->getPlayerPtr())
            throw std::runtime_error("The argument must be a player!");
    }

    static void verifyNpc(const OFWorld::Class& cls)
    {
        if (!cls.isNpc())
            throw std::runtime_error("The argument must be a NPC!");
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

        player["getCrimeLevel"] = [](const Object& o) -> int {
            const OFWorld::Class& cls = o.ptr().getClass();
            return cls.getNpcStats(o.ptr()).getBounty();
        };
        player["setCrimeLevel"] = [](const Object& o, int amount) {
            verifyPlayer(o);
            if (!o.isGObject())
                throw std::runtime_error("Only global scripts can change crime level");
            const OFWorld::Class& cls = o.ptr().getClass();
            cls.getNpcStats(o.ptr()).setBounty(amount);
            if (amount == 0)
                OFBase::Environment::get().getWorld()->getPlayer().recordCrimeId();
        };
        player["isCharGenFinished"] = [](const Object&) -> bool {
            return OFBase::Environment::get().getWorld()->getGlobalFloat(OFWorld::Globals::sCharGenState) == -1;
        };

        player["OFFENSE_TYPE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, int>(context.sol(),
                { { "Theft", OFBase::MechanicsManager::OffenseType::OT_Theft },
                    { "Assault", OFBase::MechanicsManager::OffenseType::OT_Assault },
                    { "Murder", OFBase::MechanicsManager::OffenseType::OT_Murder },
                    { "Trespassing", OFBase::MechanicsManager::OffenseType::OT_Trespassing },
                    { "SleepingInOwnedBed", OFBase::MechanicsManager::OffenseType::OT_SleepingInOwnedBed },
                    { "Pickpocket", OFBase::MechanicsManager::OffenseType::OT_Pickpocket } }));
        player["_runStandardCommitCrime"] = [](const Object& o, const sol::optional<Object> victim, int type,
                                                std::string_view faction, int arg = 0, bool victimAware = false) {
            verifyPlayer(o);
            if (victim.has_value() && !victim->ptrOrEmpty().isEmpty())
                verifyNpc(victim->ptrOrEmpty().getClass());
            if (!o.isGObject())
                throw std::runtime_error("Only global scripts can commit crime");
            if (type < 0 || type > OFBase::MechanicsManager::OffenseType::OT_Pickpocket)
                throw std::runtime_error("Invalid offense type");

            ESM::RefId factionId = parseFactionId(faction);
            // If the faction is provided but not found, error out
            if (faction != "" && factionId == ESM::RefId())
                throw std::runtime_error("Faction does not exist");

            OFWorld::Ptr victimObj = nullptr;
            if (victim.has_value())
                victimObj = victim->ptrOrEmpty();
            return OFBase::Environment::get().getMechanicsManager()->commitCrime(o.ptr(), victimObj,
                static_cast<OFBase::MechanicsManager::OffenseType>(type), factionId, arg, victimAware);
        };

        player["birthSigns"] = initBirthSignRecordBindings(context);
        player["getBirthSign"] = [](const Object& object) -> std::string {
            verifyPlayer(object);
            return OFBase::Environment::get().getWorld()->getPlayer().getBirthSign().serializeText();
        };
        player["setBirthSign"] = [](const Object& object, const sol::object& recordOrId) {
            verifyPlayer(object);
            if (!object.isGObject())
                throw std::runtime_error("Only global scripts can change birth signs");
            OFBase::Environment::get().getWorld()->getPlayer().setBirthSign(toBirthSignId(recordOrId));
        };
    }
}
