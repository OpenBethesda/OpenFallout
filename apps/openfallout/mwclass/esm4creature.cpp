#include "esm4creature.hpp"

#include <algorithm>
#include <cmath>

#include <components/debug/debuglog.hpp>
#include <components/esm4/loadpack.hpp>
#include <components/esm4/packageschedule.hpp>

#include "../mwphysics/actor.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwworld/customdata.hpp"
#include "../mwworld/esmstore.hpp"

#include "esm4sources.hpp"
#include "../mwbase/environment.hpp"
#include "../mwscript/falloutscripts.hpp"

namespace OFClass
{
    osg::Vec3f creatureBodyHalfExtents(const ESM4::Creature& creature, float baseScale)
    {
        // The radius of the executables, a person's half height for a creature that does not say, and the limits of
        // what the data has meant
        constexpr float sRadius = 31.f;
        constexpr float sUnknownHalfHeight = 66.22f;
        constexpr float sSmallestHeight = 8.f;
        constexpr float sLargestHeight = 250.f;
        constexpr float sSmallestScale = 0.1f;
        constexpr float sLargestScale = 10.f;

        const float scale
            = std::isfinite(baseScale) && baseScale > 0.f ? std::clamp(baseScale, sSmallestScale, sLargestScale) : 1.f;
        float halfHeight = sUnknownHalfHeight;
        if (creature.mHasBounds)
        {
            const float height = static_cast<float>(std::abs(creature.mBounds.mZ2 - creature.mBounds.mZ1));
            if (height > 0.f)
                halfHeight = std::clamp(height / 2.f, sSmallestHeight, sLargestHeight);
        }
        return osg::Vec3f(sRadius, sRadius, halfHeight) * scale;
    }

    class ESM4CreatureCustomData : public OFWorld::TypedCustomData<ESM4CreatureCustomData>
    {
    public:
        ESM4::CreatureModel mModel;
        /// The record that has the name of the creature, if any has one
        const ESM4::Creature* mBaseData = nullptr;
        std::vector<const ESM4::AIPackage*> mPackages;

        ESM4CreatureCustomData& asESM4CreatureCustomData() override { return *this; }
        const ESM4CreatureCustomData& asESM4CreatureCustomData() const override { return *this; }
    };

    ESM4CreatureCustomData& ESM4Creature::getCustomData(const OFWorld::ConstPtr& ptr)
    {
        // Note: the argument is ConstPtr because this function is used in `getModel` and `getName`, which are virtual
        // and work with ConstPtr, and they need the results of the templates and levelled lists, which are cached
        OFWorld::RefData& refData = const_cast<OFWorld::RefData&>(ptr.getRefData());

        if (auto* data = refData.getCustomData())
            return data->asESM4CreatureCustomData();

        auto data = std::make_unique<ESM4CreatureCustomData>();

        const OFWorld::ESMStore* store = OFBase::Environment::get().getESMStore();
        const ESM4::Creature& base = *ptr.get<ESM4::Creature>()->mBase;
        const StoreCreatureSource source(*store);
        const ESM4::SpawnRoll roll = ESM4Impl::actorRoll(ptr);
        const std::vector<const ESM4::Creature*> chain = ESM4::creatureTemplateChain(
            source, base, roll.mLevel, ESM4Impl::actorSeed(ptr, base.mId), roll.mLevelMod);

        data->mModel = ESM4::creatureModel(chain);
        // Fallout names a marker as the model of a creature that is not in the world
        if (ESM4Impl::isMarkerModel(data->mModel.mSkeleton))
            data->mModel = {};
        if (data->mModel.mOwner == nullptr)
            Log(Debug::Verbose) << "No model is found for ESM4 creature base record: \"" << base.mEditorId << "\" ("
                                << ESM::RefId(base.mId) << ")";

        data->mBaseData = ESM4::creatureTemplateOwner(chain, ESM4::Creature::Template_UseBaseData);
        for (const ESM::FormId package : ESM4::creaturePackages(chain))
            data->mPackages.push_back(store->get<ESM4::AIPackage>().search(package));

        ESM4CreatureCustomData& res = *data;
        refData.setCustomData(std::move(data));
        return res;
    }

    const ESM4::CreatureModel& ESM4Creature::getCreatureModel(const OFWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mModel;
    }

    const std::vector<const ESM4::AIPackage*>& ESM4Creature::getPackages(const OFWorld::Ptr& ptr)
    {
        return getCustomData(ptr).mPackages;
    }

    namespace
    {
        // A creature of Oblivion keeps the generic behaviour of the records of its kind: its model is drawn as it is
        // and collides as a mesh. What the class adds is for the records of Fallout 3 and New Vegas.
        bool isFalloutCreature(const OFWorld::ConstPtr& ptr)
        {
            return ptr.get<ESM4::Creature>()->mBase->mIsFONV;
        }
    }

    VFS::Path::NormalizedView ESM4Creature::getModel(const OFWorld::ConstPtr& ptr) const
    {
        if (!isFalloutCreature(ptr))
            return ESM4Base<ESM4::Creature>::getModel(ptr);
        const ESM4CreatureCustomData& data = getCustomData(ptr);
        if (data.mModel.mOwner == nullptr)
            return {};
        return data.mModel.mOwner->mModel.getNormalized();
    }

    std::unique_ptr<OFWorld::Action> ESM4Creature::activate(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor) const
    {
        // Only a creature that is flagged Allow PC Dialogue (by the record that its template leaves the base data to)
        // can be talked to
        if (isFalloutCreature(ptr))
        {
            const ESM4::Creature* baseData = getCustomData(ptr).mBaseData;
            if (baseData != nullptr && (baseData->mBaseConfig.fo3.flags & ESM4::Creature::FO3_AllowPCDialog) != 0)
                if (std::unique_ptr<OFWorld::Action> talk = ESM4Impl::talkAction(ptr, actor))
                    return talk;
        }
        return Class::activate(ptr, actor);
    }

    std::string_view ESM4Creature::getName(const OFWorld::ConstPtr& ptr) const
    {
        // The name that a script gave the base record (SetActorFullName) is the one shown, for every reference of it
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (const std::string_view given = scripts->nameGivenToBase(ptr.get<ESM4::Creature>()->mBase->mId); !given.empty())
                return given;
        if (!isFalloutCreature(ptr))
            return ptr.get<ESM4::Creature>()->mBase->mFullName;
        const ESM4::Creature* baseData = getCustomData(ptr).mBaseData;
        if (baseData == nullptr)
            return {};
        // The name of the record that gives the traits (a template) that a script changed comes after the own one
        if (baseData != ptr.get<ESM4::Creature>()->mBase)
            if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                if (const std::string_view given = scripts->nameGivenToBase(baseData->mId); !given.empty())
                    return given;
        return baseData->mFullName;
    }

    void ESM4Creature::insertObjectRendering(
        const OFWorld::Ptr& ptr, const std::string& model, OFRender::RenderingInterface& renderingInterface) const
    {
        if (!isFalloutCreature(ptr))
        {
            ESM4Impl::insertObjectRendering(ptr, model, renderingInterface);
            return;
        }
        if (!model.empty())
            renderingInterface.getObjects().insertESM4Creature(ptr);
    }

    void ESM4Creature::insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
        OFPhysics::PhysicsSystem& physics) const
    {
        if (!isFalloutCreature(ptr))
        {
            ESM4Impl::insertObjectPhysics(ptr, model, rotation, physics);
            return;
        }
        // The character controller, as the player's (the process tier says whether it moves the creature)
        const osg::Vec3f half = getBodyHalfExtents(ptr);
        OFPhysics::CharacterShape shape;
        shape.mRadius = half.x();
        shape.mHalfHeight = half.z();
        physics.addCharacter(ptr, shape);
    }

    osg::Vec3f ESM4Creature::getBodyHalfExtents(const OFWorld::Ptr& ptr)
    {
        if (!isFalloutCreature(ptr))
            return osg::Vec3f();
        // The box is the one of the record that has the model
        const ESM4::Creature* boxOwner = getCustomData(ptr).mModel.mOwner;
        if (boxOwner == nullptr)
            boxOwner = ptr.get<ESM4::Creature>()->mBase;
        return creatureBodyHalfExtents(*boxOwner, ptr.get<ESM4::Creature>()->mBase->mBaseScale);
    }
}
