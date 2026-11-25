#include "weapontype.hpp"

#include "../mwbase/environment.hpp"
#include "../mwworld/esmstore.hpp"

#include <components/esm3/loadweap.hpp>

#include <set>

namespace OFMechanics
{
    const ESM::WeaponType* getWeaponType(ESM::RefId weaponTypeId)
    {
        const auto& weaponTypes = OFBase::Environment::get().getESMStore()->get<ESM::WeaponType>();
        if (const ESM::WeaponType* result = weaponTypes.search(weaponTypeId))
            return result;
        return weaponTypes.find(ESM::WeaponType::ShortBladeOneHand);
    }

    bool isWeaponType(ESM::RefId weaponTypeId)
    {
        return !weaponTypeId.empty() && weaponTypeId != ESM::WeaponType::PickProbe
            && weaponTypeId != ESM::WeaponType::HandToHand && weaponTypeId != ESM::WeaponType::Spell;
    }

    bool isWeaponOrToolType(ESM::RefId weaponTypeId)
    {
        return isWeaponType(weaponTypeId) || weaponTypeId == ESM::WeaponType::PickProbe;
    }

    std::vector<std::string_view> getAllWeaponTypeShortGroups()
    {
        // Go via a set to eliminate duplicates.
        std::set<std::string_view> shortGroupSet;
        for (const ESM::WeaponType& type : OFBase::Environment::get().getESMStore()->get<ESM::WeaponType>())
        {
            std::string_view shortGroup = type.mShortGroup;
            if (!shortGroup.empty())
                shortGroupSet.insert(shortGroup);
        }

        return std::vector<std::string_view>(shortGroupSet.begin(), shortGroupSet.end());
    }
}
