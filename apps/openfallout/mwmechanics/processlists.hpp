#ifndef OPENFALLOUT_MWMECHANICS_PROCESSLISTS_H
#define OPENFALLOUT_MWMECHANICS_PROCESSLISTS_H

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <osg/Vec3f>

#include "falloutactor.hpp"

namespace OFWorld
{
    class CellStore;
    struct LiveCellRefBase;
}

namespace OFMechanics
{
    /// The actors of the world by their process tier, as the executables' ProcessLists keep them (NV 0x96d450 adds,
    /// 0x96d470 removes): the player, and each character and creature of a cell in the scene. The tier of an actor is
    /// judged again in each frame by the rule of the executables (FalloutScript::desiredProcessLevel): it is about the
    /// cell and the 3D, not the distance.
    class ProcessLists
    {
    public:
        ProcessLists();
        ~ProcessLists();

        /// The player is made anew (a new game, a load): its actor is for that reference
        void setPlayer(const OFWorld::Ptr& player);
        PlayerCharacter* getPlayer() { return mPlayer.get(); }
        const PlayerCharacter* getPlayer() const { return mPlayer.get(); }

        /// A character or creature of Fallout that came into the scene (the player is setPlayer's)
        void add(const OFWorld::Ptr& ptr);
        void remove(const OFWorld::Ptr& ptr);
        /// An actor that moved to another cell is another reference
        void updatePtr(const OFWorld::Ptr& old, const OFWorld::Ptr& ptr);
        void drop(const OFWorld::CellStore* cell);
        /// Forgets the characters and creatures (not the player)
        void clear();

        Actor* find(const OFWorld::Ptr& ptr);
        /// The tier of the actor (None for one that is not in the lists)
        ProcessLevel levelOf(const OFWorld::Ptr& ptr) const;

        /// Judges the tier of each actor again (A.T2, A.T3)
        void updateLevels();

        /// One frame of the bodies of the characters and creatures (Actor::updateBody): `held` says which ones
        /// something else moves in this frame
        void updateBodies(float duration, bool paused, const std::function<bool(const OFWorld::Ptr&)>& held);

        /// How many actors have the tier (the player counts: it is High)
        std::size_t count(ProcessLevel level) const;
        std::size_t size() const { return mActors.size(); }

        /// The actors (the player included) within `radius` units of `position`
        void getActorsInRange(const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& actors) const;
        bool isAnyActorInRange(const osg::Vec3f& position, float radius) const;

    private:
        /// The tier the actor should have now
        static ProcessLevel judge(const Actor& actor);

        std::unique_ptr<PlayerCharacter> mPlayer;
        std::map<const OFWorld::LiveCellRefBase*, std::unique_ptr<Actor>> mActors;
    };
}

#endif
