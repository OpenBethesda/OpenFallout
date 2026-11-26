#ifndef GAME_MWCLASS_ESM4CREATURE_H
#define GAME_MWCLASS_ESM4CREATURE_H

#include <components/esm4/creaturemodel.hpp>
#include <components/esm4/loadcrea.hpp>

#include "../mwgui/tooltips.hpp"

#include "../mwrender/objects.hpp"
#include "../mwrender/renderinginterface.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/registeredclass.hpp"

#include "esm4base.hpp"

namespace ESM4
{
    struct AIPackage;
}

namespace OFClass
{
    class ESM4CreatureCustomData;

    /// The half extents of the character body (the capsule of the character controller) of a creature in game units:
    /// the radius of the executables (31, the default of bhkCharacterControllerCinfo) and half the height of the box of
    /// its record (kept within what the data has meant; a creature without a box is as tall as a person), both times
    /// the scale of the creature's record. The executables' capsule height is not known (spec A, unclear 4): the box
    /// of the record is the nearest thing to the skeleton's bound that the record has. A capsule that is lower than
    /// it is wide is a ball as wide as it is high (OFPhysics::Actor).
    osg::Vec3f creatureBodyHalfExtents(const ESM4::Creature& creature, float baseScale);

    /// A creature of Fallout 3 or New Vegas (CREA). What it looks like and is called comes from its record and, for
    /// the many that take them from a template, the records of its templates (`ESM4::creatureModel`). The skeleton is
    /// its model; the models that hang on it and its animations are found by the rendering.
    class ESM4Creature final : public OFWorld::RegisteredClass<ESM4Creature, ESM4Base<ESM4::Creature>>
    {
        friend OFWorld::RegisteredClass<ESM4Creature, ESM4Base<ESM4::Creature>>;
        ESM4Creature()
            : OFWorld::RegisteredClass<ESM4Creature, ESM4Base<ESM4::Creature>>(ESM4::Creature::sRecordId)
        {
        }

    public:
        void insertObjectRendering(const OFWorld::Ptr& ptr, const std::string& model,
            OFRender::RenderingInterface& renderingInterface) const override;

        void insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
            OFPhysics::PhysicsSystem& physics) const override;

        /// The player talks to a creature of Fallout that has dialogue (the robots)
        std::unique_ptr<OFWorld::Action> activate(const OFWorld::Ptr& ptr, const OFWorld::Ptr& actor) const override;

        bool hasToolTip(const OFWorld::ConstPtr& ptr) const override { return !getName(ptr).empty(); }
        OFGui::ToolTipInfo getToolTipInfo(const OFWorld::ConstPtr& ptr, int count) const override
        {
            return ESM4Impl::getToolTipInfo(getName(ptr), count);
        }

        VFS::Path::NormalizedView getModel(const OFWorld::ConstPtr& ptr) const override;
        std::string_view getName(const OFWorld::ConstPtr& ptr) const override;

        /// The skeleton, body models and animation files of the creature, found through its templates
        static const ESM4::CreatureModel& getCreatureModel(const OFWorld::Ptr& ptr);

        /// The AI packages of the creature, in the order of their priority, from its record or its template (null
        /// for a package that no file has a record for)
        static const std::vector<const ESM4::AIPackage*>& getPackages(const OFWorld::Ptr& ptr);

        /// The half extents of the body that a creature of Fallout is solid as (the capsule of its character
        /// controller: the radius twice, then the half height); none (all 0) for a creature of the games before Fallout
        static osg::Vec3f getBodyHalfExtents(const OFWorld::Ptr& ptr);

    private:
        static ESM4CreatureCustomData& getCustomData(const OFWorld::ConstPtr& ptr);
    };
}

#endif // GAME_MWCLASS_ESM4CREATURE_H
