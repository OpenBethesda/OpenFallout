#include "esm4base.hpp"

#include <MyGUI_TextIterator.h>
#include <MyGUI_UString.h>

#include <components/debug/debuglog.hpp>
#include <components/esm4/loadacti.hpp>
#include <components/esm4/loadcont.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadfurn.hpp>
#include <components/esm4/loadmstt.hpp>
#include <components/esm4/loadtact.hpp>
#include <components/esm4/loadterm.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/nif/extra.hpp>
#include <components/nif/niffile.hpp>
#include <components/resource/niffilemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include <map>
#include <mutex>

#include "../mwgui/tooltips.hpp"

#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwrender/vismask.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwphysics/actor.hpp"
#include "../mwphysics/physicssystem.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "../mwworld/actiontalk.hpp"
#include "../mwworld/ptr.hpp"

namespace OFClass
{
    namespace
    {
        /// The kinds of objects whose models hold animations of their own that scripts and the player play: a door
        /// that opens, a switch that is thrown, a machine that runs
        bool holdsAnimations(unsigned int type)
        {
            return type == ESM4::Door::sRecordId || type == ESM4::Activator::sRecordId
                || type == ESM4::TalkingActivator::sRecordId || type == ESM4::Terminal::sRecordId
                || type == ESM4::Furniture::sRecordId || type == ESM4::Container::sRecordId
                || type == ESM4::MovableStatic::sRecordId;
        }
    }

    std::optional<FalloutScript::CharacterBody::Bound> ESM4Impl::skeletonBound(const OFWorld::ConstPtr& ptr)
    {
        const VFS::Path::NormalizedView path = ptr.getClass().getModel(ptr);
        const std::string model(path.value());
        if (model.empty())
            return std::nullopt;
        static std::mutex mutex;
        static std::map<std::string, std::optional<FalloutScript::CharacterBody::Bound>> bounds;
        {
            const std::lock_guard lock(mutex);
            if (const auto found = bounds.find(model); found != bounds.end())
                return found->second;
        }
        std::optional<FalloutScript::CharacterBody::Bound> bound;
        if (Resource::ResourceSystem* resources = OFBase::Environment::get().getResourceSystem())
        {
            try
            {
                const Nif::NIFFilePtr file
                    = resources->getNifFileManager()->get(Misc::ResourceHelpers::correctMeshPath(path));
                for (const auto& block : file->mRecords)
                    if (const auto* found = dynamic_cast<const Nif::BSBound*>(block.get()))
                    {
                        bound = FalloutScript::CharacterBody::Bound{ { found->mCenter.x(), found->mCenter.y(),
                                                                         found->mCenter.z() },
                            { found->mExtents.x(), found->mExtents.y(), found->mExtents.z() } };
                        break;
                    }
            }
            catch (const std::exception& e)
            {
                Log(Debug::Warning) << "The skeleton " << model << " can not be read for its bound: " << e.what();
            }
        }
        const std::lock_guard lock(mutex);
        bounds.emplace(model, bound);
        return bound;
    }

    void ESM4Impl::insertCharacterBody(const OFWorld::Ptr& ptr, OFPhysics::PhysicsSystem& physics)
    {
        OFPhysics::CharacterShape shape;
        if (const auto bound = skeletonBound(ptr))
        {
            shape.mBound = *bound;
            // The size of the race or of the creature's record (the scale of the reference is the physics' own)
            const double scale = ptr.getCellRef().getScale();
            if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
                scripts && scale > 0)
                shape.mSize = static_cast<float>(scripts->sizeOf(ptr) / scale);
        }
        else
            shape.mHasModel = false;
        physics.addCharacter(ptr, shape);
    }

    void ESM4Impl::insertObjectRendering(
        const OFWorld::Ptr& ptr, const std::string& model, OFRender::RenderingInterface& renderingInterface)
    {
        if (!model.empty())
        {
            const bool embedded = holdsAnimations(ptr.getType());
            renderingInterface.getObjects().insertModel(ptr, model, true, embedded);
            ptr.getRefData().getBaseNode()->setNodeMask(OFRender::Mask_Static);
            if (embedded)
                if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                    scripts->objectInserted(ptr);
        }
    }

    void ESM4Impl::insertObjectPhysics(
        const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation, OFPhysics::PhysicsSystem& physics)
    {
        // A door that rests open is loaded without its collision
        if (ptr.getType() == ESM4::Door::sRecordId)
            if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                if (!scripts->hasCollision(ptr))
                    return;
        physics.addObject(ptr, VFS::Path::toNormalized(model), rotation, OFPhysics::CollisionType_World);
    }

    std::uint32_t ESM4Impl::actorSeed(const OFWorld::ConstPtr& ptr, ESM::FormId baseId)
    {
        return actorSeed(ptr.getCellRef().getRefNum(), baseId);
    }

    std::uint32_t ESM4Impl::actorSeed(ESM::FormId reference, ESM::FormId baseId)
    {
        return (reference.mIndex * 2654435761u + static_cast<std::uint32_t>(reference.mContentFile))
            ^ (baseId.mIndex * 40503u);
    }

    std::unique_ptr<OFWorld::Action> ESM4Impl::talkAction(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor)
    {
        if (actor != OFMechanics::getPlayer())
            return nullptr;
        const OFScript::FalloutScripts& scripts = *OFBase::Environment::get().getFalloutScripts();
        if (!scripts.hasDialogue() || scripts.isDead(ptr))
            return nullptr;
        return std::make_unique<OFWorld::ActionTalk>(ptr);
    }

    OFGui::ToolTipInfo ESM4Impl::getToolTipInfo(std::string_view name, int count)
    {
        OFGui::ToolTipInfo info;
        info.caption = MyGUI::TextIterator::toTagsString(MyGUI::UString(name)) + OFGui::ToolTips::getCountString(count);
        return info;
    }
}
