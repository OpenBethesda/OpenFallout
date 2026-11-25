#include "mechanicsmanagerimp.hpp"

#include <osg/Stats>

#include <components/esm/defs.hpp>
#include <components/esm3/esmreader.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwscript/falloutlaw.hpp"
#include "../mwscript/falloutscripts.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "actorutil.hpp"

namespace OFMechanics
{
    MechanicsManager::MechanicsManager() = default;

    void MechanicsManager::add(const OFWorld::Ptr& ptr)
    {
        if (ptr == getPlayer())
        {
            mUpdatePlayer = true;
            return;
        }
        if (FalloutActors::handles(ptr))
        {
            mProcessLists.add(ptr);
            mFalloutActors.add(ptr);
        }
    }

    void MechanicsManager::remove(const OFWorld::Ptr& ptr, bool keepActive)
    {
        (void)keepActive;
        mProcessLists.remove(ptr);
        mFalloutActors.remove(ptr);
    }

    void MechanicsManager::updateCell(const OFWorld::Ptr& old, const OFWorld::Ptr& ptr)
    {
        mProcessLists.updatePtr(old, ptr);
        if (FalloutActors::handles(ptr))
            mFalloutActors.updatePtr(old, ptr);
    }

    void MechanicsManager::drop(const OFWorld::CellStore* cellStore)
    {
        mProcessLists.drop(cellStore);
        mFalloutActors.drop(cellStore);
    }

    void MechanicsManager::update(float duration, bool paused)
    {
        if (mUpdatePlayer)
        {
            // The player was made anew (a new game, a load): its actor is made for that reference
            mUpdatePlayer = false;
            mProcessLists.setPlayer(getPlayer());
        }
        mProcessLists.updateLevels();
        if (PlayerCharacter* player = mProcessLists.getPlayer())
            player->update(duration, paused);
        if (!paused && mAI)
            mFalloutActors.update(duration, mProcessLists);
        if (mAI)
            mFalloutActors.faceSpeaker(duration);
    }

    void MechanicsManager::playerLoaded()
    {
        mUpdatePlayer = true;
    }

    Actor* MechanicsManager::getActor(const OFWorld::Ptr& ptr)
    {
        if (mUpdatePlayer && ptr == getPlayer())
        {
            mUpdatePlayer = false;
            mProcessLists.setPlayer(ptr);
        }
        return mProcessLists.find(ptr);
    }

    PlayerCharacter* MechanicsManager::getPlayerCharacter()
    {
        if (mUpdatePlayer)
        {
            mUpdatePlayer = false;
            mProcessLists.setPlayer(getPlayer());
        }
        return mProcessLists.getPlayer();
    }

    int MechanicsManager::countHighActors() const
    {
        // The player is not on the list of the High actors: the executables update it on their own (0x96d520)
        std::size_t count = mProcessLists.count(ProcessLevel::High);
        if (count > 0 && mProcessLists.getPlayer() != nullptr)
            --count;
        return static_cast<int>(count);
    }

    bool MechanicsManager::isRunning(const OFWorld::Ptr& ptr)
    {
        // The player runs when it asks to (or a script forces it) and is not over the carry weight (rule B.M5c)
        OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
        const Actor* actor = getActor(ptr);
        const bool asked
            = (actor != nullptr && actor->isRunning()) || (scripts != nullptr && scripts->actorForcesRun(ptr));
        if (!asked)
            return false;
        return scripts == nullptr || !scripts->overEncumbered(ptr);
    }

    bool MechanicsManager::isSneaking(const OFWorld::Ptr& ptr)
    {
        // The stance is what a character sneaks by; a swimming one does not (rule B.M4: the sneak flag counts with the
        // swim flag clear)
        OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
        const Actor* actor = getActor(ptr);
        const bool asked
            = (actor != nullptr && actor->isSneaking()) || (scripts != nullptr && scripts->actorForcesSneak(ptr));
        if (!asked)
            return false;
        OFBase::World* world = OFBase::Environment::get().getWorld();
        return !world->isSwimming(ptr) && !world->isFlying(ptr);
    }

    bool MechanicsManager::toggleAI()
    {
        mAI = !mAI;
        return mAI;
    }

    bool MechanicsManager::isAIActive()
    {
        return mAI;
    }

    void MechanicsManager::startFalloutFollowing(const OFWorld::Ptr& actor, const OFWorld::Ptr& target, float distance)
    {
        mFalloutActors.follow(actor, target, distance);
    }

    void MechanicsManager::stopFalloutFollowing(const OFWorld::Ptr& actor)
    {
        mFalloutActors.stopFollowing(actor);
    }

    void MechanicsManager::evaluateFalloutPackages(const OFWorld::Ptr& actor)
    {
        mFalloutActors.evaluate(actor);
    }

    void MechanicsManager::knockFallout(const OFWorld::Ptr& actor, const osg::Vec3f& velocity, float seconds)
    {
        mFalloutActors.knock(actor, velocity, seconds);
    }

    ESM::FormId MechanicsManager::falloutPackageOf(const OFWorld::Ptr& actor) const
    {
        return mFalloutActors.currentPackage(actor);
    }

    void MechanicsManager::setFalloutPackageDuration(const OFWorld::Ptr& actor, double hundredths)
    {
        mFalloutActors.setPackageDuration(actor, hundredths);
    }

    void MechanicsManager::itemTaken(
        const OFWorld::Ptr& ptr, const OFWorld::Ptr& item, const OFWorld::Ptr& container, int count, bool alarm)
    {
        // The games of Fallout judge what is taken by their own rules (the crime of the Fallout scripts); a take that
        // raises no alarm (a pocket that was picked) is not one of theirs
        if (ptr != getPlayer() || !alarm)
            return;
        OFScript::noteTaking(ptr, item, container, count);
    }

    void MechanicsManager::getObjectsInRange(
        const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects)
    {
        mProcessLists.getActorsInRange(position, radius, objects);
    }

    void MechanicsManager::getFalloutActorsInRange(
        const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects)
    {
        mFalloutActors.getObjectsInRange(position, radius, objects);
    }

    bool MechanicsManager::isAnyActorInRange(const osg::Vec3f& position, float radius)
    {
        return mProcessLists.isAnyActorInRange(position, radius);
    }

    size_t MechanicsManager::countSavedGameRecords() const
    {
        // What the actors keep is in the record of the Fallout scripts (FOSV)
        return 0;
    }

    void MechanicsManager::write(ESM::ESMWriter& writer, Loading::Listener& listener) const
    {
        (void)writer;
        (void)listener;
    }

    void MechanicsManager::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        // The deaths of Morrowind characters (DCOU) and the Morrowind items the player had stolen (STLN), which every
        // save of the engine wrote until the Morrowind mechanics were removed: Fallout counts its deaths and judges its
        // thefts in the record of its scripts
        (void)type;
        reader.skipRecord();
    }

    void MechanicsManager::clear()
    {
        mProcessLists.clear();
        mFalloutActors.clear();
        mUpdatePlayer = true;
    }

    void MechanicsManager::processChangedSettings(const std::set<std::pair<std::string, std::string>>& settings)
    {
        // The tiers are about cells, not a distance (rule A.T2): no setting of the engine changes them
        (void)settings;
    }

    void MechanicsManager::reportStats(unsigned int frameNumber, osg::Stats& stats) const
    {
        stats.setAttribute(frameNumber, "Mechanics Actors", static_cast<double>(mProcessLists.size() + 1));
        stats.setAttribute(frameNumber, "Mechanics Objects", 0.0);
    }
}
