#include "weapon.hpp"

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/constants.hpp>
#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/nullaction.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwmechanics/weapontype.hpp"

#include "../mwgui/tooltips.hpp"

#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"

#include "classmodel.hpp"
#include "nameorid.hpp"

namespace OFClass
{
    Weapon::Weapon()
        : OFWorld::RegisteredClass<Weapon>(ESM::Weapon::sRecordId)
    {
    }

    void Weapon::insertObjectRendering(
        const OFWorld::Ptr& ptr, const std::string& model, OFRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    VFS::Path::NormalizedView Weapon::getModel(const OFWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Weapon>(ptr);
    }

    std::string_view Weapon::getName(const OFWorld::ConstPtr& ptr) const
    {
        return getNameOrId<ESM::Weapon>(ptr);
    }

    std::unique_ptr<OFWorld::Action> Weapon::activate(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor) const
    {
        return defaultItemActivate(ptr, actor);
    }

    bool Weapon::hasItemHealth(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
        const ESM::RefId type = ref->mBase->mData.mType;

        return OFMechanics::getWeaponType(type)->mFlags & ESM::WeaponType::HasHealth;
    }

    int Weapon::getItemMaxHealth(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mData.mHealth;
    }

    ESM::RefId Weapon::getScript(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mScript;
    }

    int Weapon::getValue(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mData.mValue;
    }

    const ESM::RefId& Weapon::getUpSoundId(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
        const ESM::RefId type = ref->mBase->mData.mType;
        return OFMechanics::getWeaponType(type)->mSoundIdUp;
    }

    const ESM::RefId& Weapon::getDownSoundId(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
        const ESM::RefId type = ref->mBase->mData.mType;
        return OFMechanics::getWeaponType(type)->mSoundIdDown;
    }

    VFS::Path::NormalizedView Weapon::getInventoryIcon(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mIcon.getNormalized();
    }

    OFGui::ToolTipInfo Weapon::getToolTipInfo(const OFWorld::ConstPtr& ptr, int count) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
        const ESM::WeaponType* weaponType = OFMechanics::getWeaponType(ref->mBase->mData.mType);

        OFGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + OFGui::ToolTips::getCountString(count);
        info.icon = ref->mBase->mIcon.getOriginal();

        const OFWorld::ESMStore& store = *OFBase::Environment::get().getESMStore();

        std::string text;

        // weapon type & damage
        if (weaponType->mWeaponClass != ESM::WeaponType::Ammo || Settings::game().mShowProjectileDamage)
        {
            text += "\n#{sType} ";

            const ESM::Skill* skill
                = store.get<ESM::Skill>().find(OFMechanics::getWeaponType(ref->mBase->mData.mType)->mSkill);
            std::string_view oneOrTwoHanded;
            if (weaponType->mWeaponClass == ESM::WeaponType::Melee)
            {
                if (weaponType->mFlags & ESM::WeaponType::TwoHanded)
                    oneOrTwoHanded = "sTwoHanded";
                else
                    oneOrTwoHanded = "sOneHanded";
            }

            text += skill->mName;
            if (!oneOrTwoHanded.empty())
                text += ", " + store.get<ESM::GameSetting>().find(oneOrTwoHanded)->mValue.getString();

            // weapon damage
            if (weaponType->mWeaponClass == ESM::WeaponType::Thrown)
            {
                // Thrown weapons have 2x real damage applied
                // as they're both the weapon and the ammo
                text += "\n#{sAttack}: " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[0] * 2))
                    + " - " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[1] * 2));
            }
            else if (weaponType->mWeaponClass == ESM::WeaponType::Melee)
            {
                // Chop
                text += "\n#{sChop}: " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[0])) + " - "
                    + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[1]));
                // Slash
                text += "\n#{sSlash}: " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mSlash[0]))
                    + " - " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mSlash[1]));
                // Thrust
                text += "\n#{sThrust}: " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mThrust[0]))
                    + " - " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mThrust[1]));
            }
            else
            {
                // marksman
                text += "\n#{sAttack}: " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[0]))
                    + " - " + OFGui::ToolTips::toString(static_cast<int>(ref->mBase->mData.mChop[1]));
            }
        }

        if (hasItemHealth(ptr))
        {
            int remainingHealth = getItemHealth(ptr);
            text += "\n#{sCondition}: " + OFGui::ToolTips::toString(remainingHealth) + "/"
                + OFGui::ToolTips::toString(ref->mBase->mData.mHealth);
        }

        const bool verbose = Settings::game().mShowMeleeInfo;
        // add reach for melee weapon
        if (weaponType->mWeaponClass == ESM::WeaponType::Melee && verbose)
        {
            // display value in feet
            const float combatDistance
                = store.get<ESM::GameSetting>().find("fCombatDistance")->mValue.getFloat() * ref->mBase->mData.mReach;
            text += OFGui::ToolTips::getWeightString(combatDistance / Constants::UnitsPerFoot, "#{sRange}");
            text += " #{sFeet}";
        }

        // add attack speed for any weapon excepts arrows and bolts
        if (weaponType->mWeaponClass != ESM::WeaponType::Ammo && verbose)
        {
            text += OFGui::ToolTips::getPercentString(ref->mBase->mData.mSpeed, "#{sAttributeSpeed}");
        }

        text += OFGui::ToolTips::getWeightString(ref->mBase->mData.mWeight, "#{sWeight}");
        text += OFGui::ToolTips::getValueString(ref->mBase->mData.mValue, "#{sValue}");

        info.enchant = ref->mBase->mEnchant;

        if (!info.enchant.empty())
            info.remainingEnchantCharge = static_cast<int>(ptr.getCellRef().getEnchantmentCharge());

        if (OFBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += OFGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += OFGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        info.text = std::move(text);

        return info;
    }

    ESM::RefId Weapon::getEnchantment(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mEnchant;
    }

    const ESM::RefId& Weapon::applyEnchantment(
        const OFWorld::ConstPtr& ptr, const ESM::RefId& enchId, int enchCharge, const std::string& newName) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        ESM::Weapon newItem = *ref->mBase;
        newItem.mId = ESM::RefId();
        newItem.mName = newName;
        newItem.mData.mEnchant = static_cast<uint16_t>(enchCharge);
        newItem.mEnchant = enchId;
        newItem.mData.mFlags |= ESM::Weapon::Magical;
        const ESM::Weapon* record = OFBase::Environment::get().getESMStore()->insert(newItem);
        return record->mId;
    }

    OFWorld::Ptr Weapon::copyToCellImpl(const OFWorld::ConstPtr& ptr, OFWorld::CellStore& cell) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return OFWorld::Ptr(cell.insert(ref), &cell);
    }

    int Weapon::getEnchantmentPoints(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();

        return ref->mBase->mData.mEnchant;
    }

    bool Weapon::canSell(const OFWorld::ConstPtr& item, int npcServices) const
    {
        return (npcServices & ESM::NPC::Weapon)
            || ((npcServices & ESM::NPC::MagicItems) && !getEnchantment(item).empty());
    }

    float Weapon::getWeight(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Weapon>* ref = ptr.get<ESM::Weapon>();
        return ref->mBase->mData.mWeight;
    }
}
