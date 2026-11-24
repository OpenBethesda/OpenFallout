#ifndef OPENFALLOUT_MWCLASS_FALLOUTPLAYER_H
#define OPENFALLOUT_MWCLASS_FALLOUTPLAYER_H

#include <components/esm/defs.hpp>
#include <components/esm4/loadnpc.hpp>

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/registeredclass.hpp"

namespace OFClass
{
    /// The player of Fallout 3, New Vegas and Tale of Two Wastelands: the reference PlayerRef (form 0x14 of the first
    /// Fallout plugin) of the NPC_ record 0x7, as the games have it. Its reference is a live reference of that record
    /// (its type is NPC_) that this class is given instead of the class of the characters (ESM4Npc), under the key of
    /// the engine's internal player record. It is the only actor the engine moves on character physics: its body is a
    /// capsule (rule A.C2), its movement the player's actor (OFMechanics::PlayerCharacter), and it is drawn by the
    /// renderer's own player body (FalloutPlayerBody), not as a character of a cell.
    class FalloutPlayer final : public OFWorld::RegisteredClass<FalloutPlayer>
    {
    public:
        FalloutPlayer()
            : OFWorld::RegisteredClass<FalloutPlayer>(ESM::REC_INTERNAL_PLAYER)
        {
        }

        OFWorld::Ptr copyToCellImpl(const OFWorld::ConstPtr& ptr, OFWorld::CellStore& cell) const override
        {
            const OFWorld::LiveCellRef<ESM4::Npc>* ref = ptr.get<ESM4::Npc>();
            return OFWorld::Ptr(cell.insert(ref), &cell);
        }

        /// The renderer draws the player itself (RenderingManager::renderPlayer)
        void insertObjectRendering(const OFWorld::Ptr& ptr, const std::string& model,
            OFRender::RenderingInterface& renderingInterface) const override
        {
        }

        void insertObject(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
            OFPhysics::PhysicsSystem& physics) const override
        {
            insertObjectPhysics(ptr, model, rotation, physics);
        }

        /// The body of character physics: a capsule of 31 units of radius and 132.4 of height (the human skeleton's
        /// bound), times the height of the player's race (rule A.C2)
        void insertObjectPhysics(const OFWorld::Ptr& ptr, const std::string& model, const osg::Quat& rotation,
            OFPhysics::PhysicsSystem& physics) const override;

        std::string_view getName(const OFWorld::ConstPtr& ptr) const override;

        bool isActor() const override { return true; }

        bool hasToolTip(const OFWorld::ConstPtr& ptr) const override { return false; }

        /// The player has a body without a model of a cell: its mesh is the renderer's
        VFS::Path::NormalizedView getModel(const OFWorld::ConstPtr& ptr) const override { return {}; }

        std::uint32_t getFalloutScript(const OFWorld::ConstPtr& ptr) const override { return 0; }
    };
}

#endif
