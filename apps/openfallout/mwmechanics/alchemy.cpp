#include "alchemy.hpp"

#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../mwbase/environment.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "magiceffects.hpp"

namespace
{
    constexpr size_t sNumEffects = 4;
}

bool OFMechanics::Alchemy::knownEffect(size_t potionEffectIndex, const OFWorld::Ptr& npc)
{
    float alchemySkill = npc.getClass().getSkill(npc, ESM::Skill::Alchemy);
    static const float fWortChanceValue
        = OFBase::Environment::get().getESMStore()->get<ESM::GameSetting>().find("fWortChanceValue")->mValue.getFloat();
    return (potionEffectIndex <= 1 && alchemySkill >= fWortChanceValue)
        || (potionEffectIndex <= 3 && alchemySkill >= fWortChanceValue * 2)
        || (potionEffectIndex <= 5 && alchemySkill >= fWortChanceValue * 3)
        || (potionEffectIndex <= 7 && alchemySkill >= fWortChanceValue * 4);
}

std::vector<std::string> OFMechanics::Alchemy::effectsDescription(
    const OFWorld::ConstPtr& ptr, const float alchemySkill)
{
    std::vector<std::string> effects;

    const auto& item = ptr.get<ESM::Ingredient>()->mBase;
    const auto& store = OFBase::Environment::get().getESMStore();
    const auto& mgef = store->get<ESM::MagicEffect>();
    const static auto fWortChanceValue = store->get<ESM::GameSetting>().find("fWortChanceValue")->mValue.getFloat();
    const auto& data = item->mData;

    for (size_t i = 0; i < sNumEffects; ++i)
    {
        const auto effectID = data.mEffectID[i];

        if (alchemySkill < fWortChanceValue * static_cast<int>(i + 1))
            break;

        if (!effectID.empty())
        {
            const ESM::Attribute* attribute = store->get<ESM::Attribute>().search(data.mAttributes[i]);
            const ESM::Skill* skill = store->get<ESM::Skill>().search(data.mSkills[i]);
            std::string effect = getMagicEffectString(*mgef.find(effectID), attribute, skill);

            effects.push_back(std::move(effect));
        }
    }
    return effects;
}
