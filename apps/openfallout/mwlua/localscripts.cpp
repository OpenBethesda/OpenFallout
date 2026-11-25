#include "localscripts.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwmechanics/attacktype.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "context.hpp"
#include "luamanagerimp.hpp"

namespace sol
{
    template <>
    struct is_automagical<OFBase::LuaManager::ActorControls> : std::false_type
    {
    };
    template <>
    struct is_automagical<OFLua::SelfObject> : std::false_type
    {
    };
}

namespace OFLua
{
    void SelfObject::cacheStat(LuaManager& manager, SelfObject::CachedStat key, sol::main_object value)
    {
        if (mStatsCache.empty())
        {
            manager.addAction(
                [obj = Object(*this)] {
                    LocalScripts* scripts = obj.ptr().getRefData().getLuaScripts();
                    if (scripts)
                        scripts->applyStatsCache();
                },
                "StatUpdateAction");
        }
        mStatsCache[std::move(key)] = std::move(value);
    }

    void LocalScripts::initializeSelfPackage(const Context& context)
    {
        auto lua = context.sol();
        using ActorControls = OFBase::LuaManager::ActorControls;
        sol::usertype<ActorControls> controls = lua.new_usertype<ActorControls>("ActorControls");

#define CONTROL(TYPE, FIELD)                                                                                           \
    sol::property([](const ActorControls& c) { return c.FIELD; },                                                      \
        [](ActorControls& c, const TYPE& v) {                                                                          \
            c.FIELD = v;                                                                                               \
            c.mChanged = true;                                                                                         \
        })
        controls["movement"] = CONTROL(float, mMovement);
        controls["sideMovement"] = CONTROL(float, mSideMovement);
        controls["pitchChange"] = CONTROL(float, mPitchChange);
        controls["yawChange"] = CONTROL(float, mYawChange);
        controls["run"] = CONTROL(bool, mRun);
        controls["sneak"] = CONTROL(bool, mSneak);
        controls["jump"] = CONTROL(bool, mJump);
        controls["use"] = CONTROL(OFMechanics::AttackType, mUse);
#undef CONTROL

        sol::usertype<SelfObject> selfAPI
            = lua.new_usertype<SelfObject>("SelfObject", sol::base_classes, sol::bases<LObject, Object>());
        selfAPI[sol::meta_function::to_string]
            = [](SelfObject& self) { return "openfallout.self[" + self.toString() + "]"; };
        selfAPI["object"] = sol::readonly_property([](SelfObject& self) -> LObject { return LObject(self); });
        selfAPI["controls"] = sol::readonly_property([](SelfObject& self) { return &self.mControls; });
        selfAPI["isActive"] = [](SelfObject& self) -> bool { return self.mIsActive; };
        selfAPI["enableAI"] = [](SelfObject& self, bool v) { self.mControls.mDisableAI = !v; };
        selfAPI["saveState"]
            = sol::readonly_property([](const SelfObject& self) { return self.ptr().getRefData().hasChanged(); });
        selfAPI["ATTACK_TYPE"]
            = LuaUtil::makeStrictReadOnly(LuaUtil::tableFromPairs<std::string_view, OFMechanics::AttackType>(lua,
                { { "NoAttack", OFMechanics::AttackType::NoAttack }, { "Any", OFMechanics::AttackType::Any },
                    { "Chop", OFMechanics::AttackType::Chop }, { "Slash", OFMechanics::AttackType::Slash },
                    { "Thrust", OFMechanics::AttackType::Thrust } }));
    }

    LocalScripts::LocalScripts(LuaUtil::LuaState* lua, const LObject& obj, LuaUtil::ScriptTracker* tracker)
        : LuaUtil::ScriptsContainer(lua, "L" + obj.id().toString(), tracker, false)
        , mData(obj)
    {
        lua->protectedCall(
            [&](LuaUtil::LuaView& view) { addPackage("openfallout.self", sol::make_object(view.sol(), &mData)); });
        registerEngineHandlers({ &mOnActiveHandlers, &mOnInactiveHandlers, &mOnConsumeHandlers, &mOnActivatedHandlers,
            &mOnTeleportedHandlers, &mOnAnimationTextKeyHandlers, &mOnPlayAnimationHandlers, &mOnAnimationEndedHandlers,
            &mOnSkillUse, &mOnSkillLevelUp });
    }

    void LocalScripts::setActive(bool active, bool callHandlers)
    {
        mData.mIsActive = active;
        if (callHandlers)
        {
            if (active)
                callEngineHandlers(mOnActiveHandlers);
            else
                callEngineHandlers(mOnInactiveHandlers);
        }
    }

    void LocalScripts::applyStatsCache()
    {
        const auto& ptr = mData.ptr();
        for (auto& [stat, value] : mData.mStatsCache)
            stat(ptr, value);
        mData.mStatsCache.clear();
    }
}
