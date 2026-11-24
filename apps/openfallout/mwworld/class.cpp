#include "class.hpp"

#include <stdexcept>

#include <components/esm/defs.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/misc/resourcehelpers.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/esmstore.hpp"

#include "actiontake.hpp"
#include "containerstore.hpp"
#include "failedaction.hpp"
#include "inventorystore.hpp"
#include "nullaction.hpp"
#include "ptr.hpp"
#include "worldmodel.hpp"

#include "../mwgui/tooltips.hpp"

#include "../mwmechanics/npcstats.hpp"

namespace OFWorld
{
    std::map<unsigned, Class*>& Class::getClasses()
    {
        static std::map<unsigned, Class*> values;
        return values;
    }

    void Class::insertObjectRendering(
        const Ptr& ptr, const std::string& mesh, OFRender::RenderingInterface& renderingInterface) const
    {
    }

    void Class::insertObject(
        const Ptr& ptr, const std::string& mesh, const osg::Quat& rotation, OFPhysics::PhysicsSystem& physics) const
    {
    }

    void Class::insertObjectPhysics(
        const Ptr& ptr, const std::string& mesh, const osg::Quat& rotation, OFPhysics::PhysicsSystem& physics) const
    {
    }

    bool Class::canSell(const OFWorld::ConstPtr& item, int npcServices) const
    {
        return false;
    }

    bool Class::hasItemHealth(const ConstPtr& ptr) const
    {
        return false;
    }

    int Class::getItemHealth(const ConstPtr& ptr) const
    {
        if (ptr.getCellRef().getCharge() == -1)
            return getItemMaxHealth(ptr);
        else
            return ptr.getCellRef().getCharge();
    }

    float Class::getItemNormalizedHealth(const ConstPtr& ptr) const
    {
        if (getItemMaxHealth(ptr) == 0)
        {
            return 0.f;
        }
        else
        {
            return getItemHealth(ptr) / static_cast<float>(getItemMaxHealth(ptr));
        }
    }

    int Class::getItemMaxHealth(const ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not have item health");
    }

    std::unique_ptr<Action> Class::activate(const Ptr& ptr, const Ptr& actor) const
    {
        return std::make_unique<NullAction>();
    }

    std::unique_ptr<Action> Class::use(const Ptr& ptr, bool force) const
    {
        return std::make_unique<NullAction>();
    }

    ContainerStore& Class::getContainerStore(const Ptr& ptr) const
    {
        throw std::runtime_error("class does not have a container store");
    }

    bool Class::canLock(const ConstPtr& ptr) const
    {
        return false;
    }

    void Class::setRemainingUsageTime(const Ptr& ptr, float duration) const
    {
        throw std::runtime_error("class does not support time-based uses");
    }

    float Class::getRemainingUsageTime(const ConstPtr& ptr) const
    {
        return -1;
    }

    ESM::RefId Class::getScript(const ConstPtr& ptr) const
    {
        return ESM::RefId();
    }

    std::uint32_t Class::getFalloutScript(const ConstPtr& ptr) const
    {
        return 0;
    }

    int Class::getEnchantmentPoints(const OFWorld::ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not support enchanting");
    }

    int Class::getValue(const ConstPtr& ptr) const
    {
        throw std::logic_error("value not supported by this class");
    }

    float Class::getWeight(const ConstPtr& ptr) const
    {
        throw std::runtime_error("weight not supported by this class");
    }

    const Class& Class::get(unsigned int key)
    {
        const auto& classes = getClasses();
        auto iter = classes.find(key);

        if (iter == classes.end())
            throw std::logic_error("Class::get(): unknown class key: " + std::to_string(key));

        return *iter->second;
    }

    bool Class::isPersistent(const ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not support persistence");
    }

    void Class::registerClass(Class& instance)
    {
        getClasses().emplace(instance.getType(), &instance);
    }

    const ESM::RefId& Class::getUpSoundId(const ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not have an up sound");
    }

    const ESM::RefId& Class::getDownSoundId(const ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not have an down sound");
    }

    VFS::Path::NormalizedView Class::getInventoryIcon(const OFWorld::ConstPtr& ptr) const
    {
        throw std::runtime_error("class does not have any inventory icon");
    }

    OFGui::ToolTipInfo Class::getToolTipInfo(const ConstPtr& ptr, int count) const
    {
        throw std::runtime_error("class does not have a tool tip");
    }

    bool Class::showsInInventory(const ConstPtr& ptr) const
    {
        // NOTE: Don't show WerewolfRobe objects in the inventory, or allow them to be taken.
        // Vanilla likely uses a hack like this since there's no other way to prevent it from
        // being shown or taken.
        return (ptr.getCellRef().getRefId() != "werewolfrobe");
    }

    bool Class::hasToolTip(const ConstPtr& ptr) const
    {
        return true;
    }

    ESM::RefId Class::getEnchantment(const ConstPtr& ptr) const
    {
        return ESM::RefId();
    }

    void Class::adjustScale(const OFWorld::ConstPtr& ptr, osg::Vec3f& scale, bool rendering) const {}

    VFS::Path::NormalizedView Class::getModel(const OFWorld::ConstPtr& ptr) const
    {
        return {};
    }

    VFS::Path::Normalized Class::getCorrectedModel(const OFWorld::ConstPtr& ptr) const
    {
        const VFS::Path::NormalizedView model = getModel(ptr);
        if (!model.empty())
            return Misc::ResourceHelpers::correctMeshPath(model);
        return {};
    }

    bool Class::useAnim() const
    {
        return false;
    }

    void Class::getModelsToPreload(const ConstPtr& ptr, std::vector<VFS::Path::NormalizedView>& models) const
    {
        const VFS::Path::NormalizedView model = getModel(ptr);
        if (!model.empty())
            models.push_back(model);
    }

    const ESM::RefId& Class::applyEnchantment(
        const OFWorld::ConstPtr& ptr, const ESM::RefId& enchId, int enchCharge, const std::string& newName) const
    {
        throw std::runtime_error("class can't be enchanted");
    }

    void Class::adjustPosition(const OFWorld::Ptr& ptr, bool force) const {}

    std::unique_ptr<Action> Class::defaultItemActivate(const Ptr& ptr, const Ptr& actor) const
    {
        if (!OFBase::Environment::get().getWindowManager()->isAllowed(OFGui::GW_Inventory))
            return std::make_unique<NullAction>();

        std::unique_ptr<Action> werewolfAction = getWerewolfRefusalAction(actor);
        if (werewolfAction)
            return werewolfAction;

        std::unique_ptr<Action> action = std::make_unique<ActionTake>(ptr);
        action->setSound(getUpSoundId(ptr));

        return action;
    }

    OFWorld::Ptr Class::copyToCellImpl(const ConstPtr& ptr, CellStore& cell) const
    {
        throw std::runtime_error("unable to copy class to cell");
    }

    OFWorld::Ptr Class::copyToCell(const ConstPtr& ptr, CellStore& cell, int count) const
    {
        Ptr newPtr = copyToCellImpl(ptr, cell);
        newPtr.getCellRef().unsetRefNum(); // This RefNum is only valid within the original cell of the reference
        newPtr.getCellRef().setCount(count);
        newPtr.getRefData().setLuaScripts(nullptr);
        OFBase::Environment::get().getWorldModel()->registerPtr(newPtr);
        return newPtr;
    }

    OFWorld::Ptr Class::moveToCell(const Ptr& ptr, CellStore& cell) const
    {
        Ptr newPtr = copyToCellImpl(ptr, cell);
        ptr.getRefData().setLuaScripts(nullptr);
        OFBase::Environment::get().getWorldModel()->registerPtr(newPtr);
        return newPtr;
    }

    Ptr Class::moveToCell(const Ptr& ptr, CellStore& cell, const ESM::Position& pos) const
    {
        Ptr newPtr = moveToCell(ptr, cell);
        newPtr.getRefData().setPosition(pos);
        newPtr.getCellRef().setPosition(pos);
        return newPtr;
    }

    OFWorld::Ptr Class::copyToCell(const ConstPtr& ptr, CellStore& cell, const ESM::Position& pos, int count) const
    {
        Ptr newPtr = copyToCell(ptr, cell, count);
        newPtr.getRefData().setPosition(pos);
        newPtr.getCellRef().setPosition(pos);
        return newPtr;
    }

    bool Class::isPureWaterCreature(const ConstPtr& ptr) const
    {
        return canSwim(ptr) && !isBipedal(ptr) && !canFly(ptr) && !canWalk(ptr);
    }

    bool Class::isPureFlyingCreature(const ConstPtr& ptr) const
    {
        return canFly(ptr) && !isBipedal(ptr) && !canSwim(ptr) && !canWalk(ptr);
    }

    bool Class::isPureLandCreature(const Ptr& ptr) const
    {
        return canWalk(ptr) && !isBipedal(ptr) && !canFly(ptr) && !canSwim(ptr);
    }

    bool Class::isMobile(const OFWorld::Ptr& ptr) const
    {
        return canSwim(ptr) || canWalk(ptr) || canFly(ptr);
    }

    void Class::readAdditionalState(const OFWorld::Ptr& ptr, const ESM::ObjectState& state) const {}

    void Class::writeAdditionalState(const OFWorld::ConstPtr& ptr, ESM::ObjectState& state) const {}

    OFWorld::DoorState Class::getDoorState(const OFWorld::ConstPtr& ptr) const
    {
        throw std::runtime_error("this is not a door");
    }

    void Class::setDoorState(const OFWorld::Ptr& ptr, OFWorld::DoorState state) const
    {
        throw std::runtime_error("this is not a door");
    }

    ESM::RefId Class::getSound(const OFWorld::ConstPtr&) const
    {
        return ESM::RefId();
    }

    osg::Vec4f Class::getEnchantmentColor(const OFWorld::ConstPtr& item) const
    {
        osg::Vec4f result(1, 1, 1, 1);
        const ESM::RefId& enchantmentName = item.getClass().getEnchantment(item);
        if (enchantmentName.empty())
            return result;

        const ESM::Enchantment* enchantment
            = OFBase::Environment::get().getESMStore()->get<ESM::Enchantment>().search(enchantmentName);
        if (!enchantment || enchantment->mEffects.mList.empty())
            return result;

        const ESM::MagicEffect* magicEffect = OFBase::Environment::get().getESMStore()->get<ESM::MagicEffect>().search(
            enchantment->mEffects.mList.front().mData.mEffectID);
        if (!magicEffect)
            return result;

        result.x() = magicEffect->mData.mRed / 255.f;
        result.y() = magicEffect->mData.mGreen / 255.f;
        result.z() = magicEffect->mData.mBlue / 255.f;
        return result;
    }

}
