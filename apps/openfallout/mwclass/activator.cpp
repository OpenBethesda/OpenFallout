#include "activator.hpp"

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadsndg.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/misc/rng.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/failedaction.hpp"
#include "../mwworld/nullaction.hpp"
#include "../mwworld/ptr.hpp"

#include "../mwphysics/physicssystem.hpp"

#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/vismask.hpp"

#include "../mwgui/tooltips.hpp"


#include "classmodel.hpp"

namespace OFClass
{
    Activator::Activator()
        : OFWorld::RegisteredClass<Activator>(ESM::Activator::sRecordId)
    {
    }

    void Activator::insertObjectRendering(
        const OFWorld::Ptr& ptr, const std::string& model, OFRender::RenderingInterface& renderingInterface) const
    {
        if (!model.empty())
        {
            renderingInterface.getObjects().insertModel(ptr, model);
            ptr.getRefData().getBaseNode()->setNodeMask(OFRender::Mask_Static);
        }
    }

    void Activator::insertObject(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        OFPhysics::PhysicsSystem& physics) const
    {
        insertObjectPhysics(ptr, model, rotation, physics);
    }

    void Activator::insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        OFPhysics::PhysicsSystem& physics) const
    {
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, OFPhysics::CollisionType_World);
    }

    VFS::Path::NormalizedView Activator::getModel(const OFWorld::ConstPtr& ptr) const
    {
        return getClassModel<ESM::Activator>(ptr);
    }

    bool Activator::isActivator() const
    {
        return true;
    }

    bool Activator::useAnim() const
    {
        return true;
    }

    std::string_view Activator::getName(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mName;
    }

    ESM::RefId Activator::getScript(const OFWorld::ConstPtr& ptr) const
    {
        const OFWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return ref->mBase->mScript;
    }

    bool Activator::hasToolTip(const OFWorld::ConstPtr& ptr) const
    {
        return !getName(ptr).empty();
    }

    OFGui::ToolTipInfo Activator::getToolTipInfo(const OFWorld::ConstPtr& ptr, int count) const
    {
        const OFWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        OFGui::ToolTipInfo info;
        std::string_view name = getName(ptr);
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + OFGui::ToolTips::getCountString(count);

        if (OFBase::Environment::get().getWindowManager()->getFullHelp())
        {
            info.extra += OFGui::ToolTips::getCellRefString(ptr.getCellRef());
            info.extra += OFGui::ToolTips::getMiscString(ref->mBase->mScript.getRefIdString(), "Script");
        }

        return info;
    }

    std::unique_ptr<OFWorld::Action> Activator::activate(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor) const
    {
        return std::make_unique<OFWorld::NullAction>();
    }

    OFWorld::Ptr Activator::copyToCellImpl(const OFWorld::ConstPtr& ptr, OFWorld::CellStore& cell) const
    {
        const OFWorld::LiveCellRef<ESM::Activator>* ref = ptr.get<ESM::Activator>();

        return OFWorld::Ptr(cell.insert(ref), &cell);
    }

    int Activator::getSndGenTypeFromName(std::string_view name)
    {
        if (name == "left")
            return ESM::SoundGenerator::LeftFoot;
        if (name == "right")
            return ESM::SoundGenerator::RightFoot;
        if (name == "swimleft")
            return ESM::SoundGenerator::SwimLeft;
        if (name == "swimright")
            return ESM::SoundGenerator::SwimRight;
        if (name == "moan")
            return ESM::SoundGenerator::Moan;
        if (name == "roar")
            return ESM::SoundGenerator::Roar;
        if (name == "scream")
            return ESM::SoundGenerator::Scream;
        if (name == "land")
            return ESM::SoundGenerator::Land;

        throw std::runtime_error("Unexpected soundgen type: " + std::string(name));
    }
}
