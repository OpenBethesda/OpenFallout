#include "processlists.hpp"

#include <components/esm4/loadcrea.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwscript/falloutscripts.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/livecellref.hpp"
#include "../mwworld/refdata.hpp"

namespace OFMechanics
{
    ProcessLists::ProcessLists() = default;

    ProcessLists::~ProcessLists() = default;

    void ProcessLists::setPlayer(const OFWorld::Ptr& player)
    {
        if (player.isEmpty())
        {
            mPlayer.reset();
            return;
        }
        if (mPlayer != nullptr && mPlayer->getPtr() == player)
            return;
        mPlayer = std::make_unique<PlayerCharacter>(player);
        // The player's process is kept whatever the cell (Actor::UpdateProcessLevel, NV 0x8860d0): it is High
        mPlayer->setProcessLevel(ProcessLevel::High);
    }

    void ProcessLists::add(const OFWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || (mPlayer != nullptr && mPlayer->getPtr() == ptr))
            return;
        std::unique_ptr<Actor>& actor = mActors[ptr.getBase()];
        if (ptr.getType() == ESM4::Creature::sRecordId)
            actor = std::make_unique<Creature>(ptr);
        else
            actor = std::make_unique<Character>(ptr);
        actor->setProcessLevel(judge(*actor));
    }

    void ProcessLists::remove(const OFWorld::Ptr& ptr)
    {
        mActors.erase(ptr.getBase());
    }

    void ProcessLists::updatePtr(const OFWorld::Ptr& old, const OFWorld::Ptr& ptr)
    {
        if (mPlayer != nullptr && mPlayer->getPtr() == old)
        {
            mPlayer->updatePtr(ptr);
            return;
        }
        const auto found = mActors.find(old.getBase());
        if (found == mActors.end())
            return;
        std::unique_ptr<Actor> actor = std::move(found->second);
        mActors.erase(found);
        actor->updatePtr(ptr);
        mActors[ptr.getBase()] = std::move(actor);
    }

    void ProcessLists::drop(const OFWorld::CellStore* cell)
    {
        for (auto it = mActors.begin(); it != mActors.end();)
        {
            if (it->second->getPtr().getCell() == cell)
                it = mActors.erase(it);
            else
                ++it;
        }
    }

    void ProcessLists::clear()
    {
        mActors.clear();
    }

    Actor* ProcessLists::find(const OFWorld::Ptr& ptr)
    {
        if (ptr.isEmpty())
            return nullptr;
        if (mPlayer != nullptr && mPlayer->getPtr() == ptr)
            return mPlayer.get();
        const auto found = mActors.find(ptr.getBase());
        return found == mActors.end() ? nullptr : found->second.get();
    }

    ProcessLevel ProcessLists::levelOf(const OFWorld::Ptr& ptr) const
    {
        if (ptr.isEmpty())
            return ProcessLevel::None;
        if (mPlayer != nullptr && mPlayer->getPtr() == ptr)
            return mPlayer->getProcessLevel();
        const auto found = mActors.find(ptr.getBase());
        return found == mActors.end() ? ProcessLevel::None : found->second->getProcessLevel();
    }

    ProcessLevel ProcessLists::judge(const Actor& actor)
    {
        const OFWorld::Ptr& ptr = actor.getPtr();
        FalloutScript::ProcessInputs inputs;
        // The state of the cell (cell+0x26): a cell of the scene is attached, one that is loaded but not in the scene
        // is loaded; an actor with no cell is in none (FalloutNV.exe 0x4511e0 says no for a null cell: Low)
        if (ptr.isInCell())
        {
            OFWorld::CellStore* cell = ptr.getCell();
            if (cell->getState() == OFWorld::CellStore::State_Loaded)
                inputs.mCell = OFBase::Environment::get().getWorld()->isCellActive(*cell)
                    ? FalloutScript::CellLoad::Attached
                    : FalloutScript::CellLoad::Loaded;
        }
        inputs.mHas3D = ptr.getRefData().getBaseNode() != nullptr;
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            inputs.mTeammate = scripts->isPlayerTeammate(ptr);
        // The bulk-load flag (A.unclear 1) is up only while the executable attaches cells in bulk; the cells of the
        // scene are attached here at once, and the actors of each are judged when they are added (mBulkLoading stays
        // false). The post-load counter that keeps the levels after a load (0x94df60) is for the same asynchronous
        // attach and has no counterpart.
        return FalloutScript::desiredProcessLevel(inputs);
    }

    void ProcessLists::updateLevels()
    {
        for (auto& [key, actor] : mActors)
        {
            // A dead actor keeps its process (Actor::UpdateProcessLevel keeps it when desired != current, A.T3)
            const ProcessLevel desired = judge(*actor);
            if (desired == actor->getProcessLevel())
                continue;
            if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                if (scripts->isDead(actor->getPtr()))
                    continue;
            actor->setProcessLevel(desired);
        }
    }

    std::size_t ProcessLists::count(ProcessLevel level) const
    {
        std::size_t result = (mPlayer != nullptr && mPlayer->getProcessLevel() == level) ? 1 : 0;
        for (const auto& [key, actor] : mActors)
            if (actor->getProcessLevel() == level)
                ++result;
        return result;
    }

    void ProcessLists::getActorsInRange(
        const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& actors) const
    {
        const float radius2 = radius * radius;
        const auto consider = [&](const Actor& actor) {
            const OFWorld::Ptr& ptr = actor.getPtr();
            if ((ptr.getRefData().getPosition().asVec3() - position).length2() <= radius2)
                actors.push_back(ptr);
        };
        if (mPlayer != nullptr)
            consider(*mPlayer);
        for (const auto& [key, actor] : mActors)
            consider(*actor);
    }

    bool ProcessLists::isAnyActorInRange(const osg::Vec3f& position, float radius) const
    {
        std::vector<OFWorld::Ptr> actors;
        getActorsInRange(position, radius, actors);
        return !actors.empty();
    }
}
