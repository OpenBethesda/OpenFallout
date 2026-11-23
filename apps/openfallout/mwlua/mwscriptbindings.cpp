#include "mwscriptbindings.hpp"

#include <components/lua/luastate.hpp>
#include <components/lua/util.hpp>
#include <components/misc/strings/lower.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/worldimp.hpp"

#include "context.hpp"
#include "object.hpp"

#include <stdexcept>

namespace sol
{
    template <>
    struct is_automagical<ESM::Global> : std::false_type
    {
    };
}

namespace OFLua
{

    float getGlobalVariableValue(const std::string_view globalId)
    {
        char varType = OFBase::Environment::get().getWorld()->getGlobalVariableType(globalId);
        if (varType == 'f')
        {
            return OFBase::Environment::get().getWorld()->getGlobalFloat(globalId);
        }
        else if (varType == 's' || varType == 'l')
        {
            return static_cast<float>(OFBase::Environment::get().getWorld()->getGlobalInt(globalId));
        }
        return 0;
    }

    void setGlobalVariableValue(const std::string_view globalId, float value)
    {
        const std::string name = Misc::StringUtils::lowerCase(globalId);
        char varType = OFBase::Environment::get().getWorld()->getGlobalVariableType(name);
        if (varType == 'f')
        {
            OFBase::Environment::get().getWorld()->setGlobalFloat(name, value);
        }
        else if (varType == 's' || varType == 'l')
        {
            OFBase::Environment::get().getWorld()->setGlobalInt(name, static_cast<int>(value));
        }
    }

    sol::table initMWScriptBindings(const Context& context)
    {
        sol::state_view lua = context.sol();
        sol::table api(lua, sol::create);

        using GlobalStore = OFWorld::Store<ESM::Global>;
        sol::usertype<GlobalStore> globalStoreT = lua.new_usertype<GlobalStore>("ESM3_GlobalStore");
        const GlobalStore* globalStore = &OFBase::Environment::get().getWorld()->getStore().get<ESM::Global>();
        globalStoreT[sol::meta_function::to_string] = [](const GlobalStore& store) {
            return "ESM3_GlobalStore{" + std::to_string(store.getSize()) + " globals}";
        };
        globalStoreT[sol::meta_function::length] = [](const GlobalStore& store) { return store.getSize(); };
        globalStoreT[sol::meta_function::index] = sol::overload(
            [](const GlobalStore& store, std::string_view globalId) -> sol::optional<float> {
                auto g = store.search(ESM::RefId::deserializeText(globalId));
                if (g == nullptr)
                    return sol::nullopt;
                return getGlobalVariableValue(globalId);
            },
            [](const GlobalStore& store, size_t index) -> sol::optional<float> {
                if (index < 1 || store.getSize() < index)
                    return sol::nullopt;
                auto g = store.at(LuaUtil::fromLuaIndex(index));
                if (g == nullptr)
                    return sol::nullopt;
                std::string globalId = g->mId.serializeText();
                return getGlobalVariableValue(globalId);
            });
        globalStoreT[sol::meta_function::new_index] = sol::overload(
            [](const GlobalStore& store, std::string_view globalId, float val) -> void {
                auto g = store.search(ESM::RefId::deserializeText(globalId));
                if (g == nullptr)
                    throw std::runtime_error("No variable \"" + std::string(globalId) + "\" in GlobalStore");
                setGlobalVariableValue(globalId, val);
            },
            [](const GlobalStore& store, size_t index, float val) {
                if (index < 1 || store.getSize() < index)
                    return;
                auto g = store.at(LuaUtil::fromLuaIndex(index));
                if (g == nullptr)
                    return;
                std::string globalId = g->mId.serializeText();
                setGlobalVariableValue(globalId, val);
            });
        globalStoreT[sol::meta_function::pairs] = [](const GlobalStore& store) {
            size_t index = 0;
            return sol::as_function(
                [index, &store](sol::this_state ts) mutable -> sol::optional<std::tuple<std::string, float>> {
                    if (index >= store.getSize())
                        return sol::nullopt;

                    const ESM::Global* global = store.at(index++);
                    if (!global)
                        return sol::nullopt;

                    std::string globalId = global->mId.serializeText();
                    float value = getGlobalVariableValue(globalId);

                    return std::make_tuple(globalId, value);
                });
        };
        globalStoreT[sol::meta_function::ipairs] = lua["ipairsForArray"].template get<sol::function>();
        api["getGlobalVariables"] = [globalStore](sol::optional<GObject> player) {
            if (player.has_value() && player->ptr() != OFBase::Environment::get().getWorld()->getPlayerPtr())
                throw std::runtime_error("First argument must either be a player or be missing");

            return globalStore;
        };
        return LuaUtil::makeReadOnly(api);
    }
}
