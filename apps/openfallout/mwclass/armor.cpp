#include "armor.hpp"

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadskil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwlua/localscripts.hpp"

#include "../mwworld/nullaction.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwmechanics/weapontype.hpp"
#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"

#include "../mwgui/tooltips.hpp"

#include "classmodel.hpp"
#include "nameorid.hpp"

namespace OFClass
{
    Armor::Armor()
        : OFWorld::RegisteredClass<Armor>(ESM::Armor::sRecordId)
    {
    }

    void Armor::insertObjectRendering(
        const OFWorld::Ptr& ptr, const std::string& model, OFRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
        }
    }

    VFS::Path::NormalizedView Armor::getModel(const OFWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Armor>(ptr);
    }

    std::string_view Armor::getName(const OFWorld::ConstPtr& ptr) const
    {
        return getNameOrId<ESM::Armor>(ptr);
    }

    std::unique_ptr<OFWorld::Action> Armor::activate(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor) const
    {
        return defaultItemActivate(ptr, actor);
    }

    bool Armor::hasItemHealth(const OFWorld::ConstPtr& ptr) const
    {
        return true;
    }

    int Armor::getItemMaxHealth(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mData.mHealth;
    }

    ESM::RefId Armor::getScript(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mScript;
    }

    int Armor::getValue(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mData.mValue;
    }

    // The weight classes of Morrowind armor came from Morrowind settings and skills, which the engine no longer has: a
    // Morrowind armor sounds as the heavy class and shows no class (the ESM3 records go in step 6)
    const ESM::RefId& Armor::getUpSoundId(const OFWorld::ConstPtr& ptr) const
    {
        static const ESM::RefId heavyUp = ESM::RefId::stringRefId("Item Armor Heavy Up");
        return heavyUp;
    }

    const ESM::RefId& Armor::getDownSoundId(const OFWorld::ConstPtr& ptr) const
    {
        static const ESM::RefId heavyDown = ESM::RefId::stringRefId("Item Armor Heavy Down");
        return heavyDown;
    }

    VFS::Path::NormalizedView Armor::getInventoryIcon(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mIcon.getNormalized();
    }

    OFGui::ToolTipInfo Armor::getToolTipInfo(const OFWorld::ConstPtr& ptr, int count) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        OFGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + OFGui::ToolTips::getCountString(count);
        info.icon = ref->mBase->mIcon.getOriginal();

        std::string text;

        const std::string typeText;
        text += "\n#{sArmorRating}: " + OFGui::ToolTips::toString(ref->mBase->mData.mArmor);

        int remainingHealth = getItemHealth(ptr);
        text += "\n#{sCondition}: " + OFGui::ToolTips::toString(remainingHealth) + "/"
            + OFGui::ToolTips::toString(ref->mBase->mData.mHealth);

        if (!typeText.empty())
        {
            text += "\n#{sWeight}: " + OFGui::ToolTips::toString(ref->mBase->mData.mWeight) + " (";
            text += typeText;
            text += ')';
        }

        text += OFGui::ToolTips::getValueString(ref->mBase->mData.mValue, "#{sValue}");

        if (OFBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += OFGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += OFGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        info.enchant = ref->mBase->mEnchant;
        if (!info.enchant.empty())
            info.remainingEnchantCharge = static_cast<int>(ptr.getCellRef().getEnchantmentCharge());

        info.text = std::move(text);

        return info;
    }

    ESM::RefId Armor::getEnchantment(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mEnchant;
    }

    const ESM::RefId& Armor::applyEnchantment(
        const OFWorld::ConstPtr& ptr, const ESM::RefId& enchId, int enchCharge, const std::string& newName) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        ESM::Armor newItem = *ref->mBase;
        newItem.mId = ESM::RefId();
        newItem.mName = newName;
        newItem.mData.mEnchant = enchCharge;
        newItem.mEnchant = enchId;
        const ESM::Armor* record = OFBase::Environment::get().getESMStore()->insert(newItem);
        return record->mId;
    }

    OFWorld::Ptr Armor::copyToCellImpl(const OFWorld::ConstPtr& ptr, OFWorld::CellStore& cell) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return OFWorld::Ptr(cell.insert(ref), &cell);
    }

    int Armor::getEnchantmentPoints(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();

        return ref->mBase->mData.mEnchant;
    }

    bool Armor::canSell(const OFWorld::ConstPtr& item, int npcServices) const
    {
        return (npcServices & ESM::NPC::Armor)
            || ((npcServices & ESM::NPC::MagicItems) && !getEnchantment(item).empty());
    }

    float Armor::getWeight(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Armor>* ref = ptr.get<ESM::Armor>();
        return ref->mBase->mData.mWeight;
    }
}
