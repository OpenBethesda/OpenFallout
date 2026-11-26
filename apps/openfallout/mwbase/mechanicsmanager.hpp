#ifndef GAME_MWBASE_MECHANICSMANAGER_H
#define GAME_MWBASE_MECHANICSMANAGER_H

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <components/esm/formid.hpp>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class Stats;
    class Vec3f;
}

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace OFMechanics
{
    class Actor;
    class PlayerCharacter;
}

namespace OFWorld
{
    class Ptr;
    class CellStore;
}

namespace Loading
{
    class Listener;
}

namespace OFBase
{
    /// \brief The actors of the world: the player and the characters and creatures of Fallout (implemented in
    /// OFMechanics). Each has its process tier; the player moves by the rules of its game, the others by their
    /// packages.
    class MechanicsManager
    {
        MechanicsManager(const MechanicsManager&);
        ///< not implemented

        MechanicsManager& operator=(const MechanicsManager&);
        ///< not implemented

    public:
        MechanicsManager() {}

        virtual ~MechanicsManager() {}

        /// Register an object for an update (an actor, the player among them, of a cell that comes into the scene)
        virtual void add(const OFWorld::Ptr& ptr) = 0;

        /// Deregister an object for an update
        virtual void remove(const OFWorld::Ptr& ptr, bool keepActive) = 0;

        /// Moves an object to a new cell
        virtual void updateCell(const OFWorld::Ptr& old, const OFWorld::Ptr& ptr) = 0;

        /// Deregister all objects in the given cell
        virtual void drop(const OFWorld::CellStore* cellStore) = 0;

        /// One frame of the actors
        virtual void update(float duration, bool paused) = 0;

        /// The player was made anew or loaded: its actor is made for it again
        virtual void playerLoaded() = 0;

        /// The actor the world keeps for the reference (the player, or a Fallout character or creature of a cell in the
        /// scene); null for anything else
        virtual OFMechanics::Actor* getActor(const OFWorld::Ptr& ptr) = 0;
        virtual OFMechanics::PlayerCharacter* getPlayerCharacter() = 0;
        /// How many characters and creatures have a High process (GetActorsInHigh; the player is not one of them)
        virtual int countHighActors() const = 0;

        /// Whether the actor runs and sneaks now (the stance it asked for, or a script forced on it)
        virtual bool isRunning(const OFWorld::Ptr& ptr) = 0;
        virtual bool isSneaking(const OFWorld::Ptr& ptr) = 0;

        /// The actors follow their packages (TAI): false stops them all
        virtual bool toggleAI() = 0;
        virtual bool isAIActive() = 0;

        /// Makes the actor follow the target (a character or the player), keeping within `distance` units of it
        virtual void startFalloutFollowing(const OFWorld::Ptr& actor, const OFWorld::Ptr& target, float distance) = 0;
        virtual void stopFalloutFollowing(const OFWorld::Ptr& actor) = 0;
        /// The packages of the character are looked at again (EvaluatePackage, AddScriptPackage and the others)
        virtual void evaluateFalloutPackages(const OFWorld::Ptr& actor) = 0;
        /// A blow throws the character back at `velocity`, which falls to nothing over `seconds`
        virtual void knockFallout(const OFWorld::Ptr& actor, const osg::Vec3f& velocity, float seconds) = 0;
        /// The package the character follows now (unset for none)
        virtual ESM::FormId falloutPackageOf(const OFWorld::Ptr& actor) const = 0;
        virtual void setFalloutPackageDuration(const OFWorld::Ptr& actor, double hundredths) = 0;

        /// The player took an item (from the ground, or from the container): the crime rules of the Fallout scripts
        /// judge it when `alarm` is set (a pocket that was picked raises none)
        virtual void itemTaken(const OFWorld::Ptr& ptr, const OFWorld::Ptr& item, const OFWorld::Ptr& container,
            int count, bool alarm = true)
            = 0;

        /// The actors and objects that move by themselves within `radius` units of `position`
        virtual void getObjectsInRange(const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects)
            = 0;
        /// The characters and creatures of Fallout within `radius` units of `position`
        virtual void getFalloutActorsInRange(
            const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects)
            = 0;
        /// Whether any actor (the player included) is within `radius` units of `position`
        virtual bool isAnyActorInRange(const osg::Vec3f& position, float radius) = 0;

        virtual size_t countSavedGameRecords() const = 0;
        virtual void write(ESM::ESMWriter& writer, Loading::Listener& listener) const = 0;
        virtual void readRecord(ESM::ESMReader& reader, uint32_t type) = 0;
        virtual void clear() = 0;

        virtual void processChangedSettings(const std::set<std::pair<std::string, std::string>>& settings) = 0;

        virtual void reportStats(unsigned int frameNumber, osg::Stats& stats) const = 0;
    };
}

#endif
