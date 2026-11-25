#include "types.hpp"

#include <components/detournavigator/agentbounds.hpp>
#include <components/esm/refid.hpp>
#include <components/falloutscript/actorvalues.hpp>
#include <components/lua/luastate.hpp>

#include "apps/openfallout/mwbase/environment.hpp"
#include "apps/openfallout/mwbase/mechanicsmanager.hpp"
#include "apps/openfallout/mwbase/world.hpp"
#include "apps/openfallout/mwmechanics/drawstate.hpp"
#include "apps/openfallout/mwmechanics/falloutactor.hpp"
#include "apps/openfallout/mwscript/falloutscripts.hpp"
#include "apps/openfallout/mwworld/class.hpp"
#include "apps/openfallout/mwworld/player.hpp"

#include "../localscripts.hpp"
#include "../luamanagerimp.hpp"

namespace OFLua
{
    namespace
    {
        using FalloutScript::FormId;

        OFScript::FalloutScripts* scripts()
        {
            return OFBase::Environment::get().getFalloutScripts();
        }

        std::string idText(FormId id)
        {
            return id == 0 ? std::string() : ESM::RefId::formIdRefId(ESM::FormId::fromUint32(id)).serializeText();
        }

        /// The form of the actor in the Fallout scripts; an error for an object that is not an actor
        FormId formOfActor(const Object& o)
        {
            const OFWorld::Ptr& ptr = o.ptr();
            if (!ptr.getClass().isActor())
                throw std::runtime_error("Actor expected");
            OFScript::FalloutScripts* host = scripts();
            return host != nullptr ? host->formOfReference(ptr) : 0;
        }

        /// The speed of a stance by the rules of the game (part B of docs/openfallout/fallout-rules.md)
        float stanceSpeed(const Object& o, bool running)
        {
            const OFWorld::Ptr& ptr = o.ptr();
            OFScript::FalloutScripts* host = scripts();
            if (host == nullptr)
                return 0.f;
            const bool swimming = OFBase::Environment::get().getWorld()->isSwimming(ptr);
            return static_cast<float>(host->moveSpeed(ptr, running, false, swimming));
        }
    }

    void addActorBindings(sol::table actor, const Context& context)
    {
        sol::state_view lua = context.sol();
        // The stances of Fallout: the weapon is in its holster or out (there is no stance of spells)
        actor["STANCE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, OFMechanics::DrawState>(lua,
                {
                    { "Nothing", OFMechanics::DrawState::Nothing },
                    { "Weapon", OFMechanics::DrawState::Weapon },
                }));

        actor["getStance"] = [](const Object& o) {
            const FormId form = formOfActor(o);
            return form != 0 && scripts()->equipment().weaponDrawn(form) ? OFMechanics::DrawState::Weapon
                                                                         : OFMechanics::DrawState::Nothing;
        };
        actor["stance"] = actor["getStance"]; // for compatibility; should be removed later
        actor["setStance"] = [](const SelfObject& self, int stance) {
            if (stance != static_cast<int>(OFMechanics::DrawState::Nothing)
                && stance != static_cast<int>(OFMechanics::DrawState::Weapon))
                throw std::runtime_error("Incorrect stance");
            const FormId form = formOfActor(self);
            if (form != 0)
                scripts()->equipment().setWeaponDrawn(form, stance == static_cast<int>(OFMechanics::DrawState::Weapon));
        };

        // Speeds by the rules of the game: what a full push of the controls moves the actor at walking and running,
        // and the speed it moved at in the last frame
        actor["canMove"] = [](const Object& o) {
            const OFWorld::Ptr& ptr = o.ptr();
            OFScript::FalloutScripts* host = scripts();
            return host != nullptr && !host->isDead(ptr) && !host->isParalyzed(ptr) && stanceSpeed(o, false) > 0.f;
        };
        actor["getRunSpeed"] = [](const Object& o) { return stanceSpeed(o, true); };
        actor["getWalkSpeed"] = [](const Object& o) { return stanceSpeed(o, false); };
        actor["getCurrentSpeed"] = [](const Object& o) {
            const OFMechanics::Actor* found = OFMechanics::findActor(o.ptr());
            return found != nullptr ? found->getCurrentSpeed() : 0.f;
        };
        // for compatibility; should be removed later
        actor["runSpeed"] = actor["getRunSpeed"];
        actor["walkSpeed"] = actor["getWalkSpeed"];
        actor["currentSpeed"] = actor["getCurrentSpeed"];

        actor["isOnGround"]
            = [](const LObject& o) { return OFBase::Environment::get().getWorld()->isOnGround(o.ptr()); };
        actor["isSwimming"]
            = [](const LObject& o) { return OFBase::Environment::get().getWorld()->isSwimming(o.ptr()); };

        actor["inventory"] = sol::overload([](const LObject& o) { return Inventory<LObject>{ o }; },
            [](const GObject& o) { return Inventory<GObject>{ o }; });

        // What the actor holds and wears in the Fallout scripts: the weapon in the hands and its ammunition (record ids,
        // nil for none) and the armor (a list of record ids, in the order it was put on)
        actor["getEquipment"] = [](sol::this_state thisState, const Object& o) {
            sol::table equipment(thisState, sol::create);
            const FormId form = formOfActor(o);
            if (form == 0)
                return equipment;
            FalloutScript::Equipment& worn = scripts()->equipment();
            if (const FormId weapon = worn.weapon(form))
                equipment["weapon"] = idText(weapon);
            if (const FormId ammo = worn.ammo(form))
                equipment["ammo"] = idText(ammo);
            sol::table armor(thisState, sol::create);
            int index = 1;
            for (const FormId piece : worn.armor(form))
                armor[index++] = idText(piece);
            equipment["armor"] = armor;
            return equipment;
        };
        actor["hasEquipped"] = [](const Object& o, std::string_view recordId) {
            const FormId form = formOfActor(o);
            if (form == 0)
                return false;
            FalloutScript::Equipment& worn = scripts()->equipment();
            if (idText(worn.weapon(form)) == recordId || idText(worn.ammo(form)) == recordId)
                return true;
            for (const FormId piece : worn.armor(form))
                if (idText(piece) == recordId)
                    return true;
            return false;
        };

        actor["getPathfindingAgentBounds"] = [](sol::this_state thisState, const LObject& o) {
            const DetourNavigator::AgentBounds agentBounds
                = OFBase::Environment::get().getWorld()->getPathfindingAgentBounds(o.ptr());
            sol::table result(thisState, sol::create);
            result["shapeType"] = agentBounds.mShapeType;
            result["halfExtents"] = agentBounds.mHalfExtents;
            return result;
        };
        // Whether the actor is processed in full: in the High or Middle High process (rule A.T2 of the tiers)
        actor["isInActorsProcessingRange"] = [](const Object& o) {
            if (!o.ptr().getClass().isActor())
                throw std::runtime_error("Actor expected");
            const OFMechanics::Actor* found = OFMechanics::findActor(o.ptr());
            if (found == nullptr)
                return false;
            const FalloutScript::ProcessLevel level = found->getProcessLevel();
            return level == FalloutScript::ProcessLevel::High || level == FalloutScript::ProcessLevel::MiddleHigh;
        };

        // Dead below one point of health (rule D.D1), as the Fallout scripts tell
        actor["isDead"] = [](const Object& o) {
            OFScript::FalloutScripts* host = scripts();
            return host != nullptr && host->isDead(o.ptr());
        };
        // The Paralysis actor value is above 0: the actor can neither move nor act
        actor["isParalyzed"] = [](const Object& o) {
            OFScript::FalloutScripts* host = scripts();
            return host != nullptr && host->isParalyzed(o.ptr());
        };
        // Knocked down or out (the knock state of the actor's process)
        actor["getKnockedDown"] = [](const Object& o) {
            const OFMechanics::Actor* found = OFMechanics::findActor(o.ptr());
            return found != nullptr && found->getKnockState() != FalloutScript::KnockState::Normal;
        };

        // What the actor carries (the weight of its items by their cards) and what it can carry (CarryWeight)
        actor["getEncumbrance"] = [](const Object& o) -> float {
            const FormId form = formOfActor(o);
            return form != 0 ? static_cast<float>(scripts()->carriedWeightOf(form)) : 0.f;
        };
        actor["getCapacity"] = [](const Object& o) -> float {
            const FormId form = formOfActor(o);
            return form != 0
                ? static_cast<float>(scripts()->actorValues().current(form, FalloutScript::ActorValue::carryWeight))
                : 0.f;
        };
    }
}
