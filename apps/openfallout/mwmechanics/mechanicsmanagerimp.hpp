#ifndef GAME_MWMECHANICS_MECHANICSMANAGERIMP_H
#define GAME_MWMECHANICS_MECHANICSMANAGERIMP_H

#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/ptr.hpp"

#include "falloutactors.hpp"
#include "processlists.hpp"

namespace OFWorld
{
    class CellStore;
}

namespace OFMechanics
{
    /// The actors of the world (see OFBase::MechanicsManager): the process lists hold the player and the characters and
    /// creatures of the scene with their tiers, the player moves by PlayerCharacter::update, and the characters and
    /// creatures of Fallout follow their packages in FalloutActors.
    class MechanicsManager : public OFBase::MechanicsManager
    {
    public:
        MechanicsManager();

        void add(const OFWorld::Ptr& ptr) override;
        void remove(const OFWorld::Ptr& ptr, bool keepActive) override;
        void updateCell(const OFWorld::Ptr& old, const OFWorld::Ptr& ptr) override;
        void drop(const OFWorld::CellStore* cellStore) override;
        void update(float duration, bool paused) override;
        void playerLoaded() override;

        Actor* getActor(const OFWorld::Ptr& ptr) override;
        PlayerCharacter* getPlayerCharacter() override;
        int countHighActors() const override;

        bool isRunning(const OFWorld::Ptr& ptr) override;
        bool isSneaking(const OFWorld::Ptr& ptr) override;

        bool toggleAI() override;
        bool isAIActive() override;

        void startFalloutFollowing(const OFWorld::Ptr& actor, const OFWorld::Ptr& target, float distance) override;
        void stopFalloutFollowing(const OFWorld::Ptr& actor) override;
        void playerUsesDoor(ESM::RefId cell, const ESM::Position& destination) override;
        void evaluateFalloutPackages(const OFWorld::Ptr& actor) override;
        void knockFallout(const OFWorld::Ptr& actor, const osg::Vec3f& velocity, float seconds) override;
        ESM::FormId falloutPackageOf(const OFWorld::Ptr& actor) const override;
        void setFalloutPackageDuration(const OFWorld::Ptr& actor, double hundredths) override;

        void itemTaken(const OFWorld::Ptr& ptr, const OFWorld::Ptr& item, const OFWorld::Ptr& container, int count,
            bool alarm) override;

        void getObjectsInRange(const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects) override;
        void getFalloutActorsInRange(
            const osg::Vec3f& position, float radius, std::vector<OFWorld::Ptr>& objects) override;
        bool isAnyActorInRange(const osg::Vec3f& position, float radius) override;

        size_t countSavedGameRecords() const override;
        void write(ESM::ESMWriter& writer, Loading::Listener& listener) const override;
        void readRecord(ESM::ESMReader& reader, uint32_t type) override;
        void clear() override;

        void processChangedSettings(const std::set<std::pair<std::string, std::string>>& settings) override;

        void reportStats(unsigned int frameNumber, osg::Stats& stats) const override;

    private:
        bool mUpdatePlayer = true;
        bool mAI = true;
        ProcessLists mProcessLists;
        FalloutActors mFalloutActors;
    };
}

#endif
