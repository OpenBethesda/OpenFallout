#ifndef GAME_MWMECHANICS_WEAPONTYPE_H
#define GAME_MWMECHANICS_WEAPONTYPE_H

#include <string_view>
#include <vector>

#include <components/esm/refid.hpp>

namespace ESM
{
    struct WeaponType;
}

namespace OFMechanics
{

    const ESM::WeaponType* getWeaponType(ESM::RefId weaponTypeId);

    bool isWeaponType(ESM::RefId weaponTypeId);

    bool isWeaponOrToolType(ESM::RefId weaponTypeId);

    std::vector<std::string_view> getAllWeaponTypeShortGroups();
}

#endif
