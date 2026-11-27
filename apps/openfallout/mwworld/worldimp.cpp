#include "worldimp.hpp"

#include <charconv>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <osg/ComputeBoundsVisitor>
#include <osg/Group>
#include <osg/Timer>

#include <MyGUI_TextIterator.h>

#include <LinearMath/btAabbUtil2.h>

#include <components/debug/debuglog.hpp>

#include <components/esm3/cellref.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/esm3/loadstat.hpp>
#include <components/esm4/loadcell.hpp>
#include <components/esm4/loaddoor.hpp>
#include <components/esm4/loadltex.hpp>
#include <components/esm4/loadnpc.hpp>
#include <components/esm4/loadstat.hpp>
#include <components/esm4/loadwrld.hpp>
#include <components/falloutscript/footsteps.hpp>
#include <components/falloutscript/movement.hpp>

#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/mathutil.hpp>
#include <components/misc/pathhelpers.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/rng.hpp>

#include <components/files/collections.hpp>

#include <components/esm/esmterrain.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/resource/bulletshape.hpp>
#include <components/resource/bulletshapemanager.hpp>
#include <components/esm4/loadtree.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <components/detournavigator/agentbounds.hpp>
#include <components/detournavigator/debug.hpp>
#include <components/detournavigator/navigator.hpp>
#include <components/detournavigator/settings.hpp>
#include <components/detournavigator/stats.hpp>
#include <components/detournavigator/updateguard.hpp>

#include <components/files/conversion.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/falloutactor.hpp"
#include "../mwmechanics/falloutpackages.hpp"
#include "../mwmechanics/levelledlist.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "../mwrender/animation.hpp"
#include "../mwrender/camera.hpp"
#include "../mwrender/postprocessor.hpp"
#include "../mwrender/renderingmanager.hpp"
#include "../mwrender/vismask.hpp"

#include "../mwclass/door.hpp"
#include "../mwclass/esm4base.hpp"

#include "../mwphysics/actor.hpp"
#include "../mwphysics/collisiontype.hpp"
#include "../mwphysics/object.hpp"
#include "../mwphysics/physicssystem.hpp"
#include "../mwrender/landmanager.hpp"

#include "../mwsound/constants.hpp"

#include "actionteleport.hpp"
#include "cellstore.hpp"
#include "containerstore.hpp"
#include "datetimemanager.hpp"
#include "manualref.hpp"
#include "player.hpp"
#include "weather.hpp"
#include "worldspacestart.hpp"

#include "contentloader.hpp"
#include "esmloader.hpp"

namespace OFWorld
{
    namespace
    {
        std::vector<std::pair<GlobalVariableName, ESM::Variant>> generateDefaultGlobals()
        {
            // The clock of the engine and the state of the character generation. The clock starts on the first day at
            // nine o'clock and the games count the days passed from 0 (these were the values of the placeholder records
            // of Morrowind that the engine made before; the date of the games is in their own globals, GameHour and the
            // others of the plugins)
            return {
                { Globals::sGameHour, ESM::Variant(9.f) },
                { Globals::sTimeScale, ESM::Variant(30.f) },
                { Globals::sDaysPassed, ESM::Variant(0) },
                { Globals::sDay, ESM::Variant(1) },
                { Globals::sMonth, ESM::Variant(0) },
                { Globals::sYear, ESM::Variant(1) },
                { Globals::sCharGenState, ESM::Variant(0) },
            };
        }
    }

    struct GameContentLoader : public ContentLoader
    {
        void addLoader(std::string&& extension, ContentLoader& loader)
        {
            mLoaders.emplace(std::move(extension), &loader);
        }

        void load(const std::filesystem::path& filepath, int& index, Loading::Listener* listener) override
        {
            const auto it
                = mLoaders.find(Misc::StringUtils::lowerCase(Files::pathToUnicodeString(filepath.extension())));
            if (it != mLoaders.end())
            {
                const auto filename = filepath.filename();
                Log(Debug::Info) << "Loading content file " << filename;
                if (listener != nullptr)
                    listener->setLabel(MyGUI::TextIterator::toTagsString(Files::pathToUnicodeString(filename)));
                it->second->load(filepath, index, listener);
            }
            else
            {
                std::string msg("Cannot load file: ");
                msg += Files::pathToUnicodeString(filepath);
                throw std::runtime_error(msg.c_str());
            }
        }

    private:
        std::map<std::string, ContentLoader*> mLoaders;
    };

    struct OFScriptsLoader : public ContentLoader
    {
        ESMStore& mStore;
        OFScriptsLoader(ESMStore& store)
            : mStore(store)
        {
        }
        void load(const std::filesystem::path& filepath, int& /*index*/, Loading::Listener* /*listener*/) override
        {
            mStore.addOMWScripts(filepath);
        }
    };

    void World::adjustSky()
    {
        if (mSky && (isCellExterior() || isCellQuasiExterior()))
        {
            mRendering->setSkyEnabled(true);
        }
        else
            mRendering->setSkyEnabled(false);
    }

    World::World(Resource::ResourceSystem* resourceSystem, int activationDistanceOverride, const std::string& startCell,
        const std::filesystem::path& userDataPath)
        : mResourceSystem(resourceSystem)
        , mWorldModel(mStore, mReaders)
        , mTimeManager(std::make_unique<DateTimeManager>())
        , mSky(true)
        , mGodMode(false)
        , mScriptsEnabled(true)
        , mDiscardMovements(true)
        , mUserDataPath(userDataPath)
        , mActivationDistanceOverride(activationDistanceOverride)
        , mStartCell(startCell)
        , mDistanceToFocusObject(-1.f)
        , mTeleportEnabled(true)
        , mLevitationEnabled(true)
        , mPlayerTraveling(false)
    {
    }

    void World::loadData(const Files::Collections& fileCollections, const std::vector<std::string>& contentFiles,
        const std::vector<std::string>& groundcoverFiles, ToUTF8::Utf8Encoder* encoder, Loading::Listener* listener)
    {
        mContentFiles = contentFiles;
        mESMVersions.resize(mContentFiles.size(), -1);

        loadContentFiles(fileCollections, contentFiles, encoder, listener);
        loadGroundcoverFiles(fileCollections, groundcoverFiles, encoder, listener);

        OFBase::Environment::get().getLuaManager()->contentFilesLoaded();

        fillGlobalVariables();

        mStore.setUp();
        mStore.validateRecords(mReaders);

        // The trees of Fallout are SpeedTree files, which are not read: each is drawn from its record. The scene manager
        // is asked for the model by its path in the archives, which is the one of the record under meshes/.
        for (const ESM4::Tree& tree : mStore.get<ESM4::Tree>())
        {
            if (!tree.mModel.empty() && !tree.mLeafTexture.empty())
                mResourceSystem->getSceneManager()->setSpeedTree(
                    Misc::ResourceHelpers::correctMeshPath(tree.mModel.getNormalized()),
                    Resource::SpeedTreeStandIn{ tree.mLeafTexture, tree.mBillboardWidth, tree.mBillboardHeight });
        }
    }

    void World::init(Debug::Level maxRecastLogLevel, osgViewer::Viewer* viewer, osg::ref_ptr<osg::Group> rootNode,
        const std::shared_ptr<SceneUtil::WorkQueue>& workQueue, SceneUtil::UnrefQueue& unrefQueue)
    {
        mPhysics = std::make_unique<OFPhysics::PhysicsSystem>(mResourceSystem, rootNode);

        if (Settings::navigator().mEnable)
        {
            auto navigatorSettings = DetourNavigator::makeSettingsFromSettingsManager(maxRecastLogLevel);
            navigatorSettings.mRecast.mSwimHeightScale = static_cast<float>(FalloutScript::Movement::swimmingLine);
            mNavigator = DetourNavigator::makeNavigator(navigatorSettings, mUserDataPath);
        }
        else
        {
            mNavigator = DetourNavigator::makeNavigatorStub();
        }

        mRendering = std::make_unique<OFRender::RenderingManager>(
            viewer, rootNode, mResourceSystem, workQueue, *mNavigator, mGroundcoverStore, unrefQueue);
        mRendering->preloadCommonAssets();

        mWeatherStore = std::make_unique<OFWorld::WeatherStore>();
        mWeatherManager = std::make_unique<OFWorld::WeatherManager>(*mRendering, mStore, *mWeatherStore);

        mWorldScene = std::make_unique<Scene>(*this, *mRendering.get(), mPhysics.get(), *mNavigator);
    }

    void World::fillGlobalVariables()
    {
        mGlobalVariables.fill(mStore);
        mTimeManager->setup(mGlobalVariables);
    }

    void World::startNewGame(bool bypass, bool useStartCell)
    {
        mLevitationEnabled = true;
        mTeleportEnabled = true;

        mGodMode = false;
        mScriptsEnabled = true;
        mSky = true;

        // Rebuild player
        setupPlayer();

        renderPlayer();
        mRendering->getCamera()->reset();

        // we don't want old weather to persist on a new game
        // Note that if reset later, the initial ChangeWeather that the chargen script calls will be lost.
        mWeatherManager.reset();
        mWeatherManager = std::make_unique<OFWorld::WeatherManager>(*mRendering.get(), mStore, *mWeatherStore);

        if (!bypass)
        {
            // set new game mark
            mGlobalVariables[Globals::sCharGenState].setInteger(1);
        }
        else
            mGlobalVariables[Globals::sCharGenState].setInteger(-1);

        OFBase::Environment::get().getLuaManager()->newGameStarted();

        if (bypass && useStartCell && !mStartCell.empty())
        {
            ESM::Position pos;
            ESM::RefId cellId = findExteriorPosition(mStartCell, pos);
            if (!cellId.empty())
            {
                changeToCell(cellId, pos, true);
                adjustPosition(getPlayerPtr(), false);
            }
            else
            {
                findInteriorPosition(mStartCell, pos);
                changeToInteriorCell(mStartCell, pos, true);
            }
        }
        else
        {
            if (!getPlayerPtr().isInCell())
            {
                ESM::Position pos;
                const int cellSize = Constants::CellSizeInUnits;
                pos.pos[0] = cellSize / 2;
                pos.pos[1] = cellSize / 2;
                pos.pos[2] = 0;
                pos.rot[0] = 0;
                pos.rot[1] = 0;
                pos.rot[2] = 0;

                ESM::ExteriorCellLocation exteriorCellPos = ESM::positionToExteriorCellLocation(pos.pos[0], pos.pos[1]);
                ESM::RefId cellId = ESM::RefId::esm3ExteriorCell(exteriorCellPos.mX, exteriorCellPos.mY);
                mWorldScene->changeToExteriorCell(cellId, pos, true);
            }
        }

        if (!bypass)
        {
            std::string_view video = Fallback::Map::getString("Movies_New_Game");
            if (!video.empty())
            {
                // Make sure that we do not continue to play a Title music after a new game video.
                OFBase::Environment::get().getSoundManager()->stopMusic();
                OFBase::Environment::get().getWindowManager()->playVideo(video, true);
            }
        }

        // enable collision
        if (!mPhysics->toggleCollisionMode())
            mPhysics->toggleCollisionMode();

        OFBase::Environment::get().getWindowManager()->updatePlayer();
        mTimeManager->setup(mGlobalVariables);

        // Initial seed.
        mPrng.seed(mRandomSeed);
    }

    void World::clear()
    {
        mWeatherManager->clear();
        mRendering->clear();
        mWorldScene->clear();
        mWorldModel.clear();

        mStore.clearDynamic();

        if (mPlayer)
        {
            mPlayer->clear();
            mPlayer->set(findPlayerRecord().first);
        }

        mDoorStates.clear();
        mFocusRay = OFPhysics::RayCastingResult();

        mTeleportEnabled = true;
        mLevitationEnabled = true;
        mPlayerTraveling = false;
        mIdsRebuilt = false;

        fillGlobalVariables();
    }

    size_t World::countSavedGameRecords() const
    {
        return mWorldModel.countSavedGameRecords() + mStore.countSavedGameRecords()
            + mGlobalVariables.countSavedGameRecords() + 1 // weather record
            + 1 // levitation/teleport enabled state
            + 1 // camera
            + 1; // random state.
    }

    size_t World::countSavedGameCells() const
    {
        return mWorldModel.countSavedGameRecords();
    }

    void World::write(ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        writer.startRecord(ESM::REC_RAND);
        writer.writeHNOString("RAND", Misc::Rng::serialize(mPrng));
        writer.endRecord(ESM::REC_RAND);

        mStore.write(writer, progress); // dynamic Store must be written (and read) before Cells, so that
                                        // references to custom made records will be recognized
        // (the player's place and Lua scripts are in the "player" section of the record of the Fallout scripts, which
        // comes after the cells)
        mWorldModel.write(writer, progress);
        mGlobalVariables.write(writer, progress);
        mWeatherManager->write(writer, progress);

        writer.startRecord(ESM::REC_ENAB);
        writer.writeHNT("TELE", mTeleportEnabled);
        writer.writeHNT("LEVT", mLevitationEnabled);
        writer.endRecord(ESM::REC_ENAB);

        writer.startRecord(ESM::REC_CAM_);
        writer.writeHNT("FIRS", isFirstPerson());
        writer.endRecord(ESM::REC_CAM_);
    }

    void World::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        switch (type)
        {
            case ESM::REC_ACTC:
                reader.skipRecord();
                return;
            case ESM::REC_ENAB:
                reader.getHNT(mTeleportEnabled, "TELE");
                reader.getHNT(mLevitationEnabled, "LEVT");
                return;
            case ESM::REC_RAND:
            {
                auto data = reader.getHNOString("RAND");
                Misc::Rng::deserialize(data, mPrng);
            }
            break;
            case ESM::REC_PLAY:
                // A save made when the player was a Morrowind character: converted once (Player::readRecord)
                if (!mIdsRebuilt)
                {
                    mStore.rebuildIdsIndex();
                    mIdsRebuilt = true;
                }
                mPlayer->readRecord(reader, type);
                break;
            case ESM::REC_CSTA:
                // We need to rebuild the ESMStore index in order to be able to lookup dynamic records while loading the
                // WorldModel and, afterwards, the player.
                if (!mIdsRebuilt)
                {
                    mStore.rebuildIdsIndex();
                    mIdsRebuilt = true;
                }
                mWorldModel.readRecord(reader, type);
                break;
            default:
                if (!mStore.readRecord(reader, type) && !mGlobalVariables.readRecord(reader, type)
                    && !mWeatherManager->readRecord(reader, type))
                {
                    throw std::runtime_error("unknown record in saved game");
                }
                break;
        }
    }

    void World::ensureNeededRecords()
    {
        for (const auto& [name, value] : generateDefaultGlobals())
        {
            if (mStore.get<ESM::Global>().search(ESM::RefId::stringRefId(name.getValue())) == nullptr)
            {
                ESM::Global record;
                record.mId = ESM::RefId::stringRefId(name.getValue());
                record.mValue = value;
                record.mRecordFlags = 0;
                mStore.insertStatic(record);
            }
        }
    }

    World::~World()
    {
        if (Settings::navigator().mWaitForAllJobsOnExit && mNavigator != nullptr)
        {
            Log(Debug::Verbose) << "Waiting for all navmesh jobs to be done...";
            mNavigator->wait(DetourNavigator::WaitConditionType::allJobsDone, nullptr);
        }
    }

    void World::setRandomSeed(uint32_t seed)
    {
        mRandomSeed = seed;
    }

    OFWorld::Player& World::getPlayer()
    {
        return *mPlayer;
    }

    const std::vector<int>& World::getESMVersions() const
    {
        return mESMVersions;
    }

    void World::setGlobalInt(GlobalVariableName name, int value)
    {
        mTimeManager->updateGlobalInt(name, value);
        mGlobalVariables[name].setInteger(value);
    }

    void World::setGlobalFloat(GlobalVariableName name, float value)
    {
        mTimeManager->updateGlobalFloat(name, value);
        mGlobalVariables[name].setFloat(value);
    }

    int World::getGlobalInt(GlobalVariableName name) const
    {
        return mGlobalVariables[name].getInteger();
    }

    float World::getGlobalFloat(GlobalVariableName name) const
    {
        return mGlobalVariables[name].getFloat();
    }

    char World::getGlobalVariableType(GlobalVariableName name) const
    {
        return mGlobalVariables.getType(name);
    }

    std::string_view World::getCellName(const OFWorld::CellStore* cell) const
    {
        if (!cell)
            cell = mWorldScene->getCurrentCell();
        return getCellName(*cell->getCell());
    }

    std::string_view World::getCellName(const OFWorld::Cell& cell) const
    {
        if (!cell.isExterior() || !cell.getDisplayName().empty())
            return cell.getDisplayName();

        if (!cell.getRegion().empty())
        {
            std::string_view regionName
                = ESM::visit(ESM::VisitOverload{
                                 [&](const ESM::Cell& cellIn) -> std::string_view {
                                     if (const ESM::Region* region = mStore.get<ESM::Region>().search(cell.getRegion()))
                                         return !region->mName.empty() ? region->mName : region->mId.getRefIdString();
                                     return {};
                                 },
                                 [&](const ESM4::Cell& cellIn) -> std::string_view { return {}; },
                             },
                    cell);
            if (!regionName.empty())
                return regionName;
        }

        if (!cell.getWorldSpace().empty() && ESM::isEsm4Ext(cell.getWorldSpace()))
        {
            if (const ESM4::World* worldspace = mStore.get<ESM4::World>().search(cell.getWorldSpace()))
                if (!worldspace->mFullName.empty())
                    return worldspace->mFullName;
        }

        // An exterior cell with no name of its own (NV 0x551740)
        if (mDefaultCellName.empty())
            if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
                mDefaultCellName = scripts->gameSettingText("sDefaultCellName");
        return mDefaultCellName;
    }

    Ptr World::searchPtr(const ESM::RefId& name, bool activeOnly, bool searchInContainers)
    {
        Ptr ret;
        // the player is always in an active cell.
        if (name == mPlayer->getPlayer().getCellRef().getRefId())
        {
            return mPlayer->getPlayer();
        }

        for (CellStore* cellstore : mWorldScene->getActiveCells())
        {
            // TODO: caching still doesn't work efficiently here (only works for the one CellStore that the reference is
            // in)
            Ptr ptr = cellstore->getPtr(name);

            if (!ptr.isEmpty())
                return ptr;
        }

        if (!activeOnly)
        {
            ret = mWorldModel.getPtrByRefId(name);
            if (!ret.isEmpty())
                return ret;
        }

        if (searchInContainers)
        {
            for (CellStore* cellstore : mWorldScene->getActiveCells())
            {
                Ptr ptr = cellstore->searchInContainer(name);
                if (!ptr.isEmpty())
                    return ptr;
            }
        }

        Ptr ptr = mPlayer->getPlayer().getClass().getContainerStore(mPlayer->getPlayer()).search(name);

        return ptr;
    }

    Ptr World::getPtr(const ESM::RefId& name, bool activeOnly)
    {
        Ptr ret = searchPtr(name, activeOnly);
        if (!ret.isEmpty())
            return ret;
        std::string error = "Failed to find an instance of object " + name.toDebugString();
        if (activeOnly)
            error += " in active cells";
        throw std::runtime_error(error);
    }

    struct FindContainerVisitor
    {
        ConstPtr mContainedPtr;
        Ptr mResult;

        FindContainerVisitor(const ConstPtr& containedPtr)
            : mContainedPtr(containedPtr)
        {
        }

        bool operator()(const Ptr& ptr)
        {
            if (mContainedPtr.getContainerStore() == &ptr.getClass().getContainerStore(ptr))
            {
                mResult = ptr;
                return false;
            }

            return true;
        }
    };

    Ptr World::findContainer(const ConstPtr& ptr)
    {
        if (ptr.isInCell())
            return Ptr();

        Ptr player = getPlayerPtr();
        if (ptr.getContainerStore() == &player.getClass().getContainerStore(player))
            return player;

        for (CellStore* cellstore : mWorldScene->getActiveCells())
        {
            FindContainerVisitor visitor(ptr);
            cellstore->forEachType<ESM::Container>(visitor);
            if (visitor.mResult.isEmpty())
                cellstore->forEachType<ESM::Creature>(visitor);
            if (visitor.mResult.isEmpty())
                cellstore->forEachType<ESM::NPC>(visitor);

            if (!visitor.mResult.isEmpty())
                return visitor.mResult;
        }

        return Ptr();
    }

    void World::enable(const Ptr& reference)
    {
        if (!reference.isInCell())
            return;

        if (!reference.getRefData().isEnabled())
        {
            reference.getRefData().enable();

            if (mWorldScene->getActiveCells().find(reference.getCell()) != mWorldScene->getActiveCells().end()
                && reference.getCellRef().getCount())
                mWorldScene->addObjectToScene(reference);

            if (reference.getCellRef().getRefNum().hasContentFile())
            {
                int type = mStore.find(reference.getCellRef().getRefId());
                if (mRendering->pagingEnableObject(type, reference, true))
                    mWorldScene->reloadTerrain();
            }
        }
    }

    void World::disable(const Ptr& reference)
    {
        if (!reference.getRefData().isEnabled())
            return;

        // disable is a no-op for items in containers
        if (!reference.isInCell())
            return;

        if (reference == getPlayerPtr())
            throw std::runtime_error("can not disable player object");

        reference.getRefData().disable();

        if (reference.getCellRef().getRefNum().hasContentFile())
        {
            int type = mStore.find(reference.getCellRef().getRefId());
            if (mRendering->pagingEnableObject(type, reference, false))
                mWorldScene->reloadTerrain();
        }

        if (mWorldScene->getActiveCells().find(reference.getCell()) != mWorldScene->getActiveCells().end()
            && reference.getCellRef().getCount())
        {
            mWorldScene->removeObjectFromScene(reference);
            mWorldScene->addPostponedPhysicsObjects();
        }
    }

    void World::setObjectCollision(const Ptr& ptr, bool enabled)
    {
        mWorldScene->setObjectCollision(ptr, enabled);
    }

    void World::advanceTime(double hours, bool incremental)
    {
        mWeatherManager->advanceTime(hours, incremental);
        mTimeManager->advanceTime(hours, mGlobalVariables);

        if (!incremental)
        {
            mRendering->notifyWorldSpaceChanged();
            mDiscardMovements = true;
        }
    }

    TimeStamp World::getTimeStamp() const
    {
        return mTimeManager->getTimeStamp();
    }

    bool World::toggleSky()
    {
        mSky = !mSky;
        mRendering->setSkyEnabled(mSky);
        return mSky;
    }

    int World::getMasserPhase() const
    {
        return mRendering->skyGetMasserPhase();
    }

    int World::getSecundaPhase() const
    {
        return mRendering->skyGetSecundaPhase();
    }

    std::vector<OFWorld::Moon> World::getCurrentMoons() const
    {
        return mWeatherManager->getCurrentMoons(getTimeStamp());
    }

    void World::setMoonColour(bool red)
    {
        mRendering->skySetMoonColour(red);
    }

    void World::changeToInteriorCell(
        const std::string_view cellName, const ESM::Position& position, bool adjustPlayerPos, bool changeEvent)
    {
        mPhysics->clearQueuedMovement();
        mDiscardMovements = true;

        if (changeEvent && mCurrentWorldSpace != cellName)
        {
            // changed worldspace
            mRendering->notifyWorldSpaceChanged();

            mCurrentWorldSpace = cellName;
        }

        mWorldScene->changeToInteriorCell(cellName, position, adjustPlayerPos, changeEvent);
    }

    void World::changeToCell(
        const ESM::RefId& cellId, const ESM::Position& position, bool adjustPlayerPos, bool changeEvent)
    {
        const OFWorld::Cell* destinationCell = getWorldModel().getCell(cellId).getCell();
        bool exteriorCell = destinationCell->isExterior();

        mPhysics->clearQueuedMovement();
        mDiscardMovements = true;

        if (changeEvent && mCurrentWorldSpace != destinationCell->getNameId())
        {
            // changed worldspace
            mRendering->notifyWorldSpaceChanged();
            mCurrentWorldSpace = destinationCell->getNameId();
        }
        if (exteriorCell)
            mWorldScene->changeToExteriorCell(cellId, position, adjustPlayerPos, changeEvent);
        else
            mWorldScene->changeToInteriorCell(destinationCell->getNameId(), position, adjustPlayerPos, changeEvent);
    }

    float World::getMaxActivationDistance() const
    {
        if (mActivationDistanceOverride >= 0)
            return static_cast<float>(mActivationDistanceOverride);

        // The length of the pick ray of activation, from the camera (rule D.A1): iActivatePickLength, 150 in both
        // executables, which a plugin can change
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            return static_cast<float>(scripts->gameSettingValue("iActivatePickLength"));
        return 0.f;
    }

    OFWorld::Ptr World::getFocusObject()
    {
        if (OFBase::Environment::get().getStateManager()->getState() == OFBase::StateManager::State_NoGame)
            return {};

        // The console picks far; otherwise the pick ray of activation (the games have no other distance for what the
        // crosshair names, and no telekinesis)
        if (OFBase::Environment::get().getWindowManager()->isConsoleMode())
            return getFocusObject(getMaxActivationDistance() * 50, false);
        return getFocusObject(getMaxActivationDistance(), true);
    }

    float World::getDistanceToFocusObject()
    {
        return mDistanceToFocusObject;
    }

    osg::Matrixf World::getActorHeadTransform(const OFWorld::ConstPtr& actor) const
    {
        const OFRender::Animation* anim = mRendering->getAnimation(actor);
        if (anim)
        {
            const osg::Node* node = anim->getNode("Head");
            if (!node)
                node = anim->getNode("Bip01 Head");
            if (node)
            {
                osg::NodePathList nodepaths = node->getParentalNodePaths();
                if (!nodepaths.empty())
                    return osg::computeLocalToWorld(nodepaths[0]);
            }
        }
        return osg::Matrixf::translate(actor.getRefData().getPosition().asVec3());
    }

    void World::deleteObject(const Ptr& ptr)
    {
        if (!ptr.mRef->isDeleted() && ptr.getContainerStore() == nullptr)
        {
            if (ptr == getPlayerPtr())
                throw std::runtime_error("can not delete player object");

            ptr.getCellRef().setCount(0);

            if (ptr.isInCell()
                && mWorldScene->getActiveCells().find(ptr.getCell()) != mWorldScene->getActiveCells().end()
                && ptr.getRefData().isEnabled())
            {
                mWorldScene->removeObjectFromScene(ptr);
            }
        }
    }

    void World::undeleteObject(const Ptr& ptr)
    {
        if (!ptr.getCellRef().hasContentFile())
            return;
        if (ptr.mRef->isDeleted())
        {
            ptr.getCellRef().setCount(1);
            if (mWorldScene->getActiveCells().find(ptr.getCell()) != mWorldScene->getActiveCells().end()
                && ptr.getRefData().isEnabled())
            {
                mWorldScene->addObjectToScene(ptr);
            }
        }
    }

    OFWorld::Ptr World::moveObject(
        const Ptr& ptr, CellStore* newCell, const osg::Vec3f& position, bool movePhysics, bool keepActive)
    {
        ESM::Position pos = ptr.getRefData().getPosition();
        std::memcpy(pos.pos, &position, sizeof(osg::Vec3f));
        ptr.getRefData().setPosition(pos);

        CellStore* currCell = ptr.isInCell()
            ? ptr.getCell()
            : nullptr; // currCell == nullptr should only happen for player, during initial startup
        bool isPlayer = ptr == mPlayer->getPlayer();
        bool haveToMove = isPlayer || (currCell && mWorldScene->isCellActive(*currCell));
        OFWorld::Ptr newPtr = ptr;

        if (!isPlayer && !currCell)
            throw std::runtime_error("Can not move actor " + ptr.getCellRef().getRefId().toDebugString()
                + " to another cell: current cell is nullptr");

        if (!newCell)
            throw std::runtime_error("Can not move actor " + ptr.getCellRef().getRefId().toDebugString()
                + " to another cell: new cell is nullptr");

        if (currCell != newCell)
        {
            if (isPlayer)
            {
                if (!newCell->isExterior())
                {
                    changeToInteriorCell(newCell->getCell()->getNameId(), pos, false);
                }
                else
                {
                    if (mWorldScene->isCellActive(*newCell))
                        mWorldScene->changePlayerCell(*newCell, pos, false);
                    else
                        mWorldScene->changeToExteriorCell(newCell->getCell()->getId(), pos, false);
                }
                newPtr = getPlayerPtr();
            }
            else
            {
                bool currCellActive = mWorldScene->isCellActive(*currCell);
                bool newCellActive = mWorldScene->isCellActive(*newCell);
                if (!currCellActive && newCellActive)
                {
                    newPtr = currCell->moveTo(ptr, newCell);
                    if (newPtr.getRefData().isEnabled())
                        mWorldScene->addObjectToScene(newPtr);
                }
                else if (!newCellActive && currCellActive)
                {
                    mWorldScene->removeObjectFromScene(ptr, keepActive);
                    haveToMove = false;

                    newPtr = currCell->moveTo(ptr, newCell);
                    newPtr.getRefData().setBaseNode(nullptr);
                }
                else if (!currCellActive && !newCellActive)
                    newPtr = currCell->moveTo(ptr, newCell);
                else // both cells active
                {
                    newPtr = currCell->moveTo(ptr, newCell);

                    mRendering->updatePtr(ptr, newPtr);
                    OFBase::Environment::get().getSoundManager()->updatePtr(ptr, newPtr);
                    mPhysics->updatePtr(ptr, newPtr);

                    OFBase::MechanicsManager* mechMgr = OFBase::Environment::get().getMechanicsManager();
                    mechMgr->updateCell(ptr, newPtr);
                }
            }

            OFBase::Environment::get().getWindowManager()->updateConsoleObjectPtr(ptr, newPtr);
        }
        if (haveToMove && newPtr.getRefData().getBaseNode())
        {
            mRendering->moveObject(newPtr, position);
            if (movePhysics)
            {
                mPhysics->updatePosition(newPtr);
                if (const OFPhysics::Object* object = mPhysics->getObject(newPtr))
                    updateNavigatorObject(*object);
            }
        }

        if (isPlayer)
            mWorldScene->playerMoved(position);
        else
        {
            mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
            mWorldScene->removeFromPagedRefs(newPtr);
        }

        return newPtr;
    }

    OFWorld::Ptr World::moveObject(const Ptr& ptr, const osg::Vec3f& position, bool movePhysics, bool moveToActive)
    {
        CellStore* cell = ptr.getCell();
        ESM::RefId worldspaceId
            = cell->isExterior() ? cell->getCell()->getWorldSpace() : ESM::Cell::sDefaultWorldspaceId;
        const ESM::ExteriorCellLocation index
            = ESM::positionToExteriorCellLocation(position.x(), position.y(), worldspaceId);

        CellStore* newCell = cell->isExterior() ? &mWorldModel.getExterior(index) : nullptr;
        bool isCellActive = getPlayerPtr().isInCell() && getPlayerPtr().getCell()->isExterior()
            && (newCell && mWorldScene->isCellActive(*newCell));

        if (cell->isExterior() || (moveToActive && isCellActive && ptr.getClass().isActor()))
            cell = newCell;

        return moveObject(ptr, cell, position, movePhysics);
    }

    OFWorld::Ptr World::moveObjectBy(const Ptr& ptr, const osg::Vec3f& vec, bool moveToActive)
    {
        auto* actor = mPhysics->getActor(ptr);
        osg::Vec3f newpos = ptr.getRefData().getPosition().asVec3() + vec;
        if (actor)
            actor->adjustPosition(vec);
        if (ptr.getClass().isActor())
            return moveObject(ptr, newpos, false, moveToActive && ptr != getPlayerPtr());
        return moveObject(ptr, newpos);
    }

    void World::scaleObject(const Ptr& ptr, float scale, bool force)
    {
        if (!force && scale == ptr.getCellRef().getScale())
            return;
        if (mPhysics->getActor(ptr))
            mNavigator->removeAgent(getPathfindingAgentBounds(ptr));

        ptr.getCellRef().setScale(scale);
        mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
        mWorldScene->removeFromPagedRefs(ptr);

        if (ptr.getRefData().getBaseNode() != nullptr)
            mWorldScene->updateObjectScale(ptr);

        if (mPhysics->getActor(ptr))
        {
            const DetourNavigator::AgentBounds agentBounds = getPathfindingAgentBounds(ptr);
            if (!mNavigator->addAgent(agentBounds))
                Log(Debug::Warning) << "Scaled agent bounds are not supported by navigator: " << agentBounds;
        }
        else if (const auto object = mPhysics->getObject(ptr))
            updateNavigatorObject(*object);
    }

    void World::rotateObject(const Ptr& ptr, const osg::Vec3f& rot, OFBase::RotationFlags flags)
    {
        ESM::Position pos = ptr.getRefData().getPosition();
        float* objRot = pos.rot;
        if (flags & OFBase::RotationFlag_adjust)
        {
            objRot[0] += rot.x();
            objRot[1] += rot.y();
            objRot[2] += rot.z();
        }
        else
        {
            objRot[0] = rot.x();
            objRot[1] = rot.y();
            objRot[2] = rot.z();
        }

        if (ptr.getClass().isActor())
        {
            /* HACK? Actors shouldn't really be rotating around X (or Y), but
             * currently it's done so for rotating the camera, which needs
             * clamping.
             */
            objRot[0] = std::clamp(objRot[0], -osg::PI_2f, osg::PI_2f);
            objRot[1] = static_cast<float>(Misc::normalizeAngle(objRot[1]));
            objRot[2] = static_cast<float>(Misc::normalizeAngle(objRot[2]));
        }

        ptr.getRefData().setPosition(pos);

        mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
        mWorldScene->removeFromPagedRefs(ptr);

        if (ptr.getRefData().getBaseNode() != nullptr)
        {
            const auto order
                = flags & OFBase::RotationFlag_inverseOrder ? RotationOrder::inverse : RotationOrder::direct;
            mWorldScene->updateObjectRotation(ptr, order);

            if (const auto object = mPhysics->getObject(ptr))
                updateNavigatorObject(*object);
        }
    }

    void World::adjustPosition(const Ptr& ptr, bool force)
    {
        if (ptr.isEmpty())
        {
            Log(Debug::Warning) << "Unable to adjust position for empty object";
            return;
        }

        osg::Vec3f pos(ptr.getRefData().getPosition().asVec3());

        if (!ptr.getRefData().getBaseNode())
        {
            // will be adjusted when Ptr's cell becomes active
            return;
        }

        if (!ptr.isInCell())
        {
            Log(Debug::Warning) << "Unable to adjust position for object '" << ptr.getCellRef().getRefId()
                                << "' - it has no cell";
            return;
        }

        const float terrainHeight = ptr.getCell()->isExterior()
            ? getTerrainHeightAt(pos, ptr.getCell()->getCell()->getWorldSpace())
            : -std::numeric_limits<float>::max();
        pos.z() = std::max(pos.z(), terrainHeight)
            + 20; // place slightly above terrain. will snap down to ground with code below

        // An actor in water is not put on the ground under it: a living one swims, and the ragdoll of a dead one moves
        // its body in the water (fallout-rules.md A.K3, NV 0x63017A). No class of Fallout is asked for the persistence
        // of Morrowind (asking for it threw "class does not support persistence", and a new game of Fallout 3 failed).
        bool swims = ptr.getClass().isActor() && isSwimming(ptr);
        if (force || !ptr.getClass().isActor() || (!isFlying(ptr) && !swims && isActorCollisionEnabled(ptr)))
        {
            float height = static_cast<float>(ESM::getCellSize(ptr.getCell()->getCell()->getWorldSpace()));
            osg::Vec3f traced = mPhysics->traceDown(ptr, pos, height);
            pos.z() = std::min(pos.z(), traced.z());
        }

        moveObject(ptr, ptr.getCell(), pos);
    }

    void World::fixPosition()
    {
        const OFWorld::Ptr actor = getPlayerPtr();
        const float distance = 128.f;
        ESM::Position esmPos = actor.getRefData().getPosition();
        osg::Quat orientation(esmPos.rot[2], osg::Vec3f(0, 0, -1));
        osg::Vec3f pos(esmPos.asVec3());

        int direction = 0;
        int fallbackDirections[4] = { direction, (direction + 3) % 4, (direction + 2) % 4, (direction + 1) % 4 };

        osg::Vec3f targetPos = pos;
        for (int i = 0; i < 4; ++i)
        {
            direction = fallbackDirections[i];
            if (direction == 0)
                targetPos = pos + (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (direction == 1)
                targetPos = pos - (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (direction == 2)
                targetPos = pos - (orientation * osg::Vec3f(1, 0, 0)) * distance;
            else if (direction == 3)
                targetPos = pos + (orientation * osg::Vec3f(1, 0, 0)) * distance;

            // destination is free
            if (!mPhysics->castRay(pos, targetPos, OFPhysics::CollisionType_World | OFPhysics::CollisionType_Door).mHit)
                break;
        }
        targetPos.z() += distance / 2.f; // move up a bit to get out from geometry, will snap down later
        float height = static_cast<float>(ESM::getCellSize(actor.getCell()->getCell()->getWorldSpace()));
        osg::Vec3f traced = mPhysics->traceDown(actor, targetPos, height);
        if (traced != pos)
        {
            esmPos.pos[0] = traced.x();
            esmPos.pos[1] = traced.y();
            esmPos.pos[2] = traced.z();
            ESM::RefId cell = actor.getCell()->getCell()->getId();
            OFWorld::ActionTeleport(cell, esmPos).execute(actor);
        }
    }

    void World::rotateWorldObject(const Ptr& ptr, const osg::Quat& rotate)
    {
        if (ptr.getRefData().getBaseNode() != nullptr)
        {
            mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
            mWorldScene->removeFromPagedRefs(ptr);

            mRendering->rotateObject(ptr, rotate);
            mPhysics->updateRotation(ptr, rotate);

            if (const auto object = mPhysics->getObject(ptr))
                updateNavigatorObject(*object);
        }
    }

    OFWorld::Ptr World::placeObject(const OFWorld::ConstPtr& ptr, OFWorld::CellStore* cell, const ESM::Position& pos)
    {
        return copyObjectToCell(ptr, cell, pos, ptr.getCellRef().getCount(), false);
    }

    OFWorld::Ptr World::safePlaceObject(const ConstPtr& ptr, const ConstPtr& referenceObject,
        OFWorld::CellStore* referenceCell, int direction, float distance)
    {
        ESM::Position ipos = referenceObject.getRefData().getPosition();
        osg::Vec3f pos(ipos.asVec3());
        osg::Quat orientation(ipos.rot[2], osg::Vec3f(0, 0, -1));

        int fallbackDirections[4] = { direction, (direction + 3) % 4, (direction + 2) % 4, (direction + 1) % 4 };

        osg::Vec3f spawnPoint = pos;

        for (int i = 0; i < 4; ++i)
        {
            direction = fallbackDirections[i];
            if (direction == 0)
                spawnPoint = pos + (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (direction == 1)
                spawnPoint = pos - (orientation * osg::Vec3f(0, 1, 0)) * distance;
            else if (direction == 2)
                spawnPoint = pos - (orientation * osg::Vec3f(1, 0, 0)) * distance;
            else if (direction == 3)
                spawnPoint = pos + (orientation * osg::Vec3f(1, 0, 0)) * distance;

            if (!ptr.getClass().isActor())
                break;

            // check if spawn point is safe, fall back to another direction if not
            spawnPoint.z() += 30; // move up a little to account for slopes, will snap down later

            if (!mPhysics
                     ->castRay(spawnPoint, osg::Vec3f(pos.x(), pos.y(), pos.z() + 20),
                         OFPhysics::CollisionType_World | OFPhysics::CollisionType_Door)
                     .mHit)
            {
                // safe
                break;
            }
        }
        ipos.pos[0] = spawnPoint.x();
        ipos.pos[1] = spawnPoint.y();
        ipos.pos[2] = spawnPoint.z();

        if (referenceObject.getClass().isActor())
        {
            ipos.rot[0] = 0;
            ipos.rot[1] = 0;
        }

        OFWorld::Ptr placed = copyObjectToCell(ptr, referenceCell, ipos, ptr.getCellRef().getCount(), false);
        adjustPosition(placed, true); // snap to ground
        return placed;
    }

    void World::queueMovement(const Ptr& ptr, const osg::Vec3f& velocity)
    {
        mPhysics->queueObjectMovement(ptr, velocity);
        if (ptr == OFMechanics::getPlayer())
            OFBase::Environment::get().getSoundManager()->setListenerVel(velocity);
    }

    void World::updateAnimatedCollisionShape(const Ptr& ptr)
    {
        mPhysics->updateAnimatedCollisionShape(ptr);
    }

    void World::doPhysics(float duration, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats)
    {
        processDoors(duration);
        mPhysics->stepSimulation(duration, mDiscardMovements, frameStart, frameNumber, stats);
        mDiscardMovements = false;
        mPhysics->moveActors();
    }

    void World::updateNavigator()
    {
        auto navigatorUpdateGuard = mNavigator->makeUpdateGuard();

        mPhysics->forEachAnimatedObject([&](const auto& pair) {
            const auto [object, changed] = pair;
            if (changed)
                updateNavigatorObject(*object, navigatorUpdateGuard.get());
        });

        for (const auto& door : mDoorStates)
            if (const auto object = mPhysics->getObject(door.first))
                updateNavigatorObject(*object, navigatorUpdateGuard.get());

        mNavigator->update(getPlayerPtr().getRefData().getPosition().asVec3(), navigatorUpdateGuard.get());
    }

    void World::updateNavigatorObject(
        const OFPhysics::Object& object, const DetourNavigator::UpdateGuard* navigatorUpdateGuard)
    {
        // The body of a person is not a part of the world that the navigator builds a mesh of (and it is a cylinder,
        // which the navigator has no shape for)
        if (object.getShapeInstance()->mVisualCollisionType != Resource::VisualCollisionType::None
            || object.isActorBody())
            return;
        const OFWorld::Ptr ptr = object.getPtr();
        const DetourNavigator::ObjectShapes shapes(object.getShapeInstance(),
            DetourNavigator::ObjectTransform{ ptr.getRefData().getPosition(), ptr.getCellRef().getScale() });
        mNavigator->updateObject(
            DetourNavigator::ObjectId(&object), shapes, object.getTransform(), navigatorUpdateGuard);
    }

    const OFPhysics::RayCastingInterface* World::getRayCasting() const
    {
        return mPhysics.get();
    }

    bool World::rotateDoor(const Ptr door, OFWorld::DoorState state, float duration)
    {
        const ESM::Position& objPos = door.getRefData().getPosition();
        auto oldRot = objPos.asRotationVec3();
        auto newRot = oldRot;

        float minRot = door.getCellRef().getPosition().rot[2];
        float maxRot = minRot + osg::DegreesToRadians(90.f);

        float diff = duration * osg::DegreesToRadians(90.f) * (state == OFWorld::DoorState::Opening ? 1 : -1);
        float targetRot = std::clamp(oldRot.z() + diff, minRot, maxRot);
        newRot.z() = targetRot;
        rotateObject(door, newRot, OFBase::RotationFlag_none);

        bool reached = (targetRot == maxRot && state != OFWorld::DoorState::Idle) || targetRot == minRot;

        /// \todo should use convexSweepTest here
        bool collisionWithActor = false;
        for (auto& [ptr, point, normal] :
            mPhysics->getCollisionsPoints(door, OFPhysics::CollisionType_Door, OFPhysics::CollisionType_Actor))
        {

            if (ptr.getClass().isActor())
            {
                auto localPoint = objPos.asVec3() - point;
                osg::Vec3f direction = osg::Quat(diff, osg::Vec3f(0, 0, 1)) * localPoint - localPoint;
                direction.normalize();
                mPhysics->reportCollision(Misc::Convert::toBullet(point), Misc::Convert::toBullet(normal));
                if (direction * normal < 0) // door is turning away from actor
                    continue;

                collisionWithActor = true;

                // we need to undo the rotation
                reached = false;
            }
        }

        // Cancel door closing sound if collision with actor is detected
        if (collisionWithActor)
        {
            const ESM::Door* ref = door.get<ESM::Door>()->mBase;

            if (state == OFWorld::DoorState::Opening)
            {
                const ESM::RefId& openSound = ref->mOpenSound;
                if (!openSound.empty()
                    && OFBase::Environment::get().getSoundManager()->getSoundPlaying(door, openSound))
                    OFBase::Environment::get().getSoundManager()->stopSound3D(door, openSound);
            }
            else if (state == OFWorld::DoorState::Closing)
            {
                const ESM::RefId& closeSound = ref->mCloseSound;
                if (!closeSound.empty()
                    && OFBase::Environment::get().getSoundManager()->getSoundPlaying(door, closeSound))
                    OFBase::Environment::get().getSoundManager()->stopSound3D(door, closeSound);
            }

            rotateObject(door, oldRot, OFBase::RotationFlag_none);
        }

        return reached;
    }

    void World::processDoors(float duration)
    {
        auto it = mDoorStates.begin();
        while (it != mDoorStates.end())
        {
            if (!mWorldScene->isCellActive(*it->first.getCell()) || !it->first.getRefData().getBaseNode())
            {
                // The door is no longer in an active cell, or it was disabled.
                // Erase from mDoorStates, since we no longer need to move it.
                // Once we load the door's cell again (or re-enable the door), Door::insertObject will reinsert to
                // mDoorStates.
                mDoorStates.erase(it++);
            }
            else
            {
                bool reached = rotateDoor(it->first, it->second, duration);

                if (reached)
                {
                    // Mark as non-moving
                    it->first.getClass().setDoorState(it->first, OFWorld::DoorState::Idle);
                    mDoorStates.erase(it++);
                }
                else
                    ++it;
            }
        }
    }

    void World::setActorCollisionMode(const OFWorld::Ptr& ptr, bool internal, bool external)
    {
        OFPhysics::Actor* physicActor = mPhysics->getActor(ptr);
        if (physicActor && physicActor->getCollisionMode() != internal)
        {
            physicActor->enableCollisionMode(internal);
            physicActor->enableCollisionBody(external);
        }
    }

    bool World::isActorCollisionEnabled(const OFWorld::Ptr& ptr)
    {
        OFPhysics::Actor* physicActor = mPhysics->getActor(ptr);
        return physicActor && physicActor->getCollisionMode();
    }

    bool World::toggleCollisionMode()
    {
        if (mPhysics->toggleCollisionMode())
        {
            adjustPosition(getPlayerPtr(), true);
            return true;
        }

        return false;
    }

    bool World::toggleRenderMode(OFRender::RenderMode mode)
    {
        switch (mode)
        {
            case OFRender::Render_CollisionDebug:
                return mPhysics->toggleDebugRendering();
            default:
                return mRendering->toggleRenderMode(mode);
        }
    }

    void World::update(float duration, bool paused)
    {
        // Reset "traveling" flag - there was a frame to detect traveling.
        mPlayerTraveling = false;

        updateWeather(duration, paused);

        updateNavigator();

        mPlayer->update();

        mPhysics->debugDraw();

        mWorldScene->update(duration);

        mRendering->update(duration, paused);

        updateSoundListener();

        if (mWorldScene->hasCellLoaded())
        {
            mNavigator->wait(DetourNavigator::WaitConditionType::requiredTilesPresent,
                OFBase::Environment::get().getWindowManager()->getLoadingScreen());
            mWorldScene->resetCellLoaded();
        }
    }

    void World::updatePhysics(
        float duration, bool paused, osg::Timer_t frameStart, unsigned int frameNumber, osg::Stats& stats)
    {
        if (!paused)
        {
            doPhysics(duration, frameStart, frameNumber, stats);
        }
        else
        {
            // zero the async stats if we are paused
            stats.setAttribute(frameNumber, "physicsworker_time_begin", 0);
            stats.setAttribute(frameNumber, "physicsworker_time_taken", 0);
            stats.setAttribute(frameNumber, "physicsworker_time_end", 0);
        }
    }

    void World::updateSoundListener()
    {
        const OFRender::Camera* camera = mRendering->getCamera();
        const auto& player = getPlayerPtr();
        const ESM::Position& refpos = player.getRefData().getPosition();
        osg::Vec3f listenerPos, up, forward;
        osg::Quat listenerOrient;

        if (isFirstPerson() || Settings::sound().mCameraListener)
            listenerPos = camera->getPosition();
        else
            listenerPos = refpos.asVec3() + osg::Vec3f(0, 0, 1.85f * mPhysics->getHalfExtents(player).z());

        if (isFirstPerson() || Settings::sound().mCameraListener)
            listenerOrient = camera->getOrient();
        else
            listenerOrient = osg::Quat(refpos.rot[1], osg::Vec3f(0, -1, 0))
                * osg::Quat(refpos.rot[0], osg::Vec3f(-1, 0, 0)) * osg::Quat(refpos.rot[2], osg::Vec3f(0, 0, -1));

        forward = listenerOrient * osg::Vec3f(0, 1, 0);
        up = listenerOrient * osg::Vec3f(0, 0, 1);

        bool underwater = isUnderwater(player.getCell(), camera->getPosition());

        OFBase::Environment::get().getSoundManager()->setListenerPosDir(listenerPos, forward, up, underwater);
    }

    void World::updateFocusObject()
    {
        try
        {
            // inform the GUI about focused object
            OFWorld::Ptr object = getFocusObject();

            // retrieve the object's top point's screen position so we know where to place the floating label
            if (!object.isEmpty())
            {
                osg::BoundingBox bb = mPhysics->getBoundingBox(object);
                if (!bb.valid() && object.getRefData().getBaseNode())
                {
                    osg::ComputeBoundsVisitor computeBoundsVisitor;
                    computeBoundsVisitor.setTraversalMask(~(OFRender::Mask_ParticleSystem | OFRender::Mask_Effect));
                    object.getRefData().getBaseNode()->accept(computeBoundsVisitor);
                    bb = computeBoundsVisitor.getBoundingBox();
                }
                const osg::Vec2f pos = mRendering->getScreenCoords(bb);
                OFBase::Environment::get().getWindowManager()->setFocusObjectScreenCoords(pos.x(), pos.y());
            }

            OFBase::Environment::get().getWindowManager()->setFocusObject(object);
        }
        catch (std::exception& e)
        {
            Log(Debug::Error) << "Error updating focus object: " << e.what();
        }
    }

    OFWorld::Ptr World::getFocusObject(float maxDistance, bool ignorePlayer)
    {
        const float camDist = mRendering->getCamera()->getCameraDistance();
        maxDistance += camDist;
        OFWorld::Ptr focusObject;
        OFRender::RenderingManager::RayResult rayToObject;

        const bool ignoreTerrain = !Settings::game().mTerrainObstructsFocus;

        if (OFBase::Environment::get().getWindowManager()->isGuiMode())
        {
            float x, y;
            OFBase::Environment::get().getWindowManager()->getMousePosition(x, y);
            rayToObject = mRendering->castCameraToViewportRay(x, y, maxDistance, ignorePlayer, false, ignoreTerrain);
        }
        else
            rayToObject
                = mRendering->castCameraToViewportRay(0.5f, 0.5f, maxDistance, ignorePlayer, false, ignoreTerrain);

        focusObject = rayToObject.mHitObject;
        if (focusObject.isEmpty() && rayToObject.mHitRefnum.isSet())
            focusObject = OFBase::Environment::get().getWorldModel()->getPtr(rayToObject.mHitRefnum);
        if (rayToObject.mHit)
            mDistanceToFocusObject = (rayToObject.mRatio * maxDistance) - camDist;
        else
            mDistanceToFocusObject = -1;
        mFocusRay = { rayToObject.mHit, rayToObject.mHitPointWorld, rayToObject.mHitNormalWorld, focusObject };
        return focusObject;
    }

    bool World::castRenderingRay(OFPhysics::RayCastingResult& res, const osg::Vec3f& from, const osg::Vec3f& to,
        bool ignorePlayer, bool ignoreActors, bool ignoreTerrain, std::span<const OFWorld::Ptr> ignoreList)
    {
        OFRender::RenderingManager::RayResult rayRes
            = mRendering->castRay(from, to, ignorePlayer, ignoreActors, ignoreTerrain, ignoreList);
        res.mHit = rayRes.mHit;
        res.mHitPos = rayRes.mHitPointWorld;
        res.mHitNormal = rayRes.mHitNormalWorld;
        res.mHitObject = rayRes.mHitObject;
        if (res.mHitObject.isEmpty() && rayRes.mHitRefnum.isSet())
            res.mHitObject = OFBase::Environment::get().getWorldModel()->getPtr(rayRes.mHitRefnum);
        return res.mHit;
    }

    bool World::isCellExterior() const
    {
        const CellStore* currentCell = mWorldScene->getCurrentCell();
        if (currentCell)
        {
            return currentCell->getCell()->isExterior();
        }
        return false;
    }

    bool World::isCellQuasiExterior() const
    {
        const CellStore* currentCell = mWorldScene->getCurrentCell();
        if (currentCell)
        {
            return currentCell->getCell()->isQuasiExterior();
        }
        return false;
    }

    const std::set<CellStore*, std::less<>>& World::getActiveCells() const
    {
        return mWorldScene->getActiveCells();
    }

    bool World::isCellActive(const CellStore& cell) const
    {
        const auto& active = mWorldScene->getActiveCells();
        return active.find(const_cast<CellStore*>(&cell)) != active.end();
    }

    ESM::RefId World::getCurrentWorldspace() const
    {
        const CellStore* cellStore = mWorldScene->getCurrentCell();
        if (cellStore)
            return cellStore->getCell()->getWorldSpace();
        return ESM::Cell::sDefaultWorldspaceId;
    }

    const OFWorld::WeatherStore& World::getAllWeather() const
    {
        return *mWeatherStore;
    }

    int World::getCurrentWeatherScriptId() const
    {
        return mWeatherManager->getWeather().mScriptId;
    }

    const OFWorld::Weather& World::getCurrentWeather() const
    {
        return mWeatherManager->getWeather();
    }

    int World::getNextWeatherScriptId() const
    {
        auto next = mWeatherManager->getNextWeather();
        if (next == nullptr)
            return -1;

        return next->mScriptId;
    }

    const OFWorld::Weather* World::getNextWeather() const
    {
        return mWeatherManager->getNextWeather();
    }

    float World::getWeatherTransition() const
    {
        return mWeatherManager->getTransitionFactor();
    }

    unsigned int World::getNightDayMode() const
    {
        return mWeatherManager->getNightDayMode();
    }

    void World::changeWeather(ESM::RefId region, ESM::RefId id)
    {
        mWeatherManager->changeWeather(region, id);
    }

    void World::forceWeather(ESM::RefId id, bool hold)
    {
        mWeatherManager->overrideWeather(id, hold);
    }

    void World::releaseWeatherOverride()
    {
        mWeatherManager->releaseWeatherOverride();
    }

    void World::modRegion(ESM::RefId regionid, const std::map<ESM::RefId, uint8_t>& chances)
    {
        mWeatherManager->modRegion(regionid, chances);
    }

    const std::map<ESM::RefId, uint8_t>& World::getRegionWeatherChances(ESM::RefId regionid) const
    {
        return mWeatherManager->getRegionChances(regionid);
    }

    struct GetDoorMarkerVisitor
    {
        std::vector<World::DoorMarker>& mOut;

        bool operator()(const OFWorld::Ptr& ptr)
        {
            OFWorld::LiveCellRef<ESM::Door>& ref = *static_cast<OFWorld::LiveCellRef<ESM::Door>*>(ptr.getBase());

            if (!ref.mData.isEnabled() || ref.isDeleted())
                return true;

            if (ref.mRef.getTeleport())
            {
                World::DoorMarker newMarker;
                newMarker.name = OFClass::Door::getDestination(ref);
                if (newMarker.name.empty())
                    return true;
                newMarker.dest = ref.mRef.getDestCell();

                ESM::Position pos = ref.mData.getPosition();

                newMarker.x = pos.pos[0];
                newMarker.y = pos.pos[1];
                mOut.push_back(std::move(newMarker));
            }
            return true;
        }
    };

    void World::getDoorMarkers(CellStore& cell, std::vector<World::DoorMarker>& out)
    {
        GetDoorMarkerVisitor visitor{ out };
        cell.forEachType<ESM::Door>(visitor);
    }

    void World::setWaterHeight(const float height)
    {
        mPhysics->setWaterHeight(height);
        mRendering->setWaterHeight(height);
    }

    bool World::toggleWater()
    {
        return mRendering->toggleRenderMode(OFRender::Render_Water);
    }

    bool World::toggleWorld()
    {
        return mRendering->toggleRenderMode(OFRender::Render_Scene);
    }

    bool World::toggleBorders()
    {
        return mRendering->toggleBorders();
    }

    OFWorld::Ptr World::placeObject(const OFWorld::Ptr& object, float cursorX, float cursorY, int amount, bool copy)
    {
        const float maxDist = 200.f;

        OFRender::RenderingManager::RayResult result
            = mRendering->castCameraToViewportRay(cursorX, cursorY, maxDist, true, true, false);

        CellStore* cell = getPlayerPtr().getCell();

        ESM::Position pos = getPlayerPtr().getRefData().getPosition();
        if (result.mHit)
        {
            pos.pos[0] = result.mHitPointWorld.x();
            pos.pos[1] = result.mHitPointWorld.y();
            pos.pos[2] = result.mHitPointWorld.z();
        }
        // We want only the Z part of the player's rotation
        pos.rot[0] = 0;
        pos.rot[1] = 0;

        // copy the object and set its count
        Ptr dropped
            = copy ? copyObjectToCell(object, cell, pos, amount, true) : moveObjectToCell(object, cell, pos, true);

        OFBase::Environment::get().getLuaManager()->objectPlaced(
            dropped, getPlayerPtr(), pos.asVec3(), Misc::Convert::makeOsgQuat(pos.rot));

        return dropped;
    }

    bool World::canPlaceObject(float cursorX, float cursorY)
    {
        const float maxDist = 200.f;
        OFRender::RenderingManager::RayResult result
            = mRendering->castCameraToViewportRay(cursorX, cursorY, maxDist, true, true, false);

        if (result.mHit)
        {
            // check if the wanted position is on a flat surface, and not e.g. against a vertical wall
            if (std::acos((result.mHitNormalWorld / result.mHitNormalWorld.length()) * osg::Vec3f(0, 0, 1))
                >= osg::DegreesToRadians(30.f))
                return false;

            return true;
        }
        else
            return false;
    }

    Ptr World::copyObjectToCell(const ConstPtr& object, CellStore* cell, ESM::Position pos, int count, bool adjustPos)
    {
        if (!cell)
            throw std::runtime_error("copyObjectToCell(): cannot copy object to null cell");
        if (cell->isExterior())
        {
            const ESM::ExteriorCellLocation index
                = ESM::positionToExteriorCellLocation(pos.pos[0], pos.pos[1], cell->getCell()->getWorldSpace());
            cell = &mWorldModel.getExterior(index);
        }

        OFWorld::Ptr dropped = object.getClass().copyToCell(object, *cell, pos, count);

        initObjectInCell(dropped, *cell, adjustPos);

        return dropped;
    }

    Ptr World::moveObjectToCell(const Ptr& object, CellStore* cell, ESM::Position pos, bool adjustPos)
    {
        if (!cell)
            throw std::runtime_error("moveObjectToCell(): cannot move object to null cell");
        if (cell->isExterior())
        {
            const ESM::ExteriorCellLocation index
                = ESM::positionToExteriorCellLocation(pos.pos[0], pos.pos[1], cell->getCell()->getWorldSpace());
            cell = &mWorldModel.getExterior(index);
        }

        OFWorld::Ptr dropped = object.getClass().moveToCell(object, *cell, pos);

        initObjectInCell(dropped, *cell, adjustPos);

        return dropped;
    }

    void World::initObjectInCell(const Ptr& object, CellStore& cell, bool adjustPos)
    {
        if (mWorldScene->isCellActive(cell))
        {
            if (object.getRefData().isEnabled())
            {
                mWorldScene->addObjectToScene(object);
            }
        }

        if (!object.getClass().isActor() && adjustPos && object.getRefData().getBaseNode())
        {
            // Adjust position so the location we wanted ends up in the middle of the object bounding box
            osg::ComputeBoundsVisitor computeBounds;
            computeBounds.setTraversalMask(~OFRender::Mask_ParticleSystem);
            object.getRefData().getBaseNode()->accept(computeBounds);
            osg::BoundingBox bounds = computeBounds.getBoundingBox();
            if (bounds.valid())
            {
                ESM::Position pos = object.getRefData().getPosition();
                bounds.set(bounds._min - pos.asVec3(), bounds._max - pos.asVec3());

                osg::Vec3f adjust(
                    (bounds.xMin() + bounds.xMax()) / 2, (bounds.yMin() + bounds.yMax()) / 2, bounds.zMin());
                pos.pos[0] -= adjust.x();
                pos.pos[1] -= adjust.y();
                pos.pos[2] -= adjust.z();
                moveObject(object, pos.asVec3());
            }
        }
    }

    OFWorld::Ptr World::dropObjectOnGround(const Ptr& actor, const Ptr& object, int amount, bool copy)
    {
        OFWorld::CellStore* cell = actor.getCell();

        ESM::Position pos = actor.getRefData().getPosition();
        // We want only the Z part of the actor's rotation
        pos.rot[0] = 0;
        pos.rot[1] = 0;

        osg::Vec3f orig = pos.asVec3();
        orig.z() += 20;
        osg::Vec3f dir(0, 0, -1);

        float len = 1000000.0;

        OFRender::RenderingManager::RayResult result = mRendering->castRay(orig, orig + dir * len, true, true, false);
        if (result.mHit)
            pos.pos[2] = result.mHitPointWorld.z();

        // copy the object and set its count
        Ptr dropped
            = copy ? copyObjectToCell(object, cell, pos, amount, true) : moveObjectToCell(object, cell, pos, true);

        OFBase::Environment::get().getLuaManager()->objectDropped(
            dropped, actor, pos.asVec3(), Misc::Convert::makeOsgQuat(pos.rot));

        return dropped;
    }

    void World::processChangedSettings(const Settings::CategorySettingVector& settings)
    {
        mRendering->processChangedSettings(settings);
    }

    bool World::isFlying(const OFWorld::Ptr& ptr) const
    {
        if (!ptr.getClass().isActor())
            return false;

        // An actor flies only without collision (the console's ToggleCollision): Fallout has no levitation, and the
        // creatures that hover are moved by their packages, not as fliers
        if (OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts())
            if (scripts->isDead(ptr))
                return false;

        const OFPhysics::Actor* actor = mPhysics->getActor(ptr);
        if (!actor)
            return true;

        return false;
    }

    // The lines of the share of the body under water of the executables (FalloutNV.exe 0x889917, 0x889946,
    // 0x889975): the head is under at 0.875, the actor swims from 0.7, and wades from 0.01

    bool World::isSubmerged(const OFWorld::ConstPtr& object) const
    {
        return isUnderwater(object, static_cast<float>(FalloutScript::Movement::headUnderLine));
    }

    bool World::isSwimming(const OFWorld::ConstPtr& object) const
    {
        return isUnderwater(object, static_cast<float>(FalloutScript::Movement::swimmingLine));
    }

    bool World::isWading(const OFWorld::ConstPtr& object) const
    {
        return isUnderwater(object, static_cast<float>(FalloutScript::Movement::wadingLine));
    }

    bool World::isUnderwater(const OFWorld::ConstPtr& object, const float heightRatio) const
    {
        osg::Vec3f pos(object.getRefData().getPosition().asVec3());

        pos.z() += heightRatio * 2 * mPhysics->getRenderingHalfExtents(object).z();

        const CellStore* currCell = object.isInCell()
            ? object.getCell()
            : nullptr; // currCell == nullptr should only happen for player, during initial startup

        return isUnderwater(currCell, pos);
    }

    bool World::isUnderwater(const OFWorld::CellStore* cell, const osg::Vec3f& pos) const
    {
        if (!cell)
            return false;

        if (!(cell->getCell()->hasWater()))
        {
            return false;
        }
        return pos.z() < cell->getWaterLevel();
    }

    int World::getFloorMaterial(const OFWorld::ConstPtr& actor, bool& stairs) const
    {
        stairs = false;
        if (actor.isEmpty() || !actor.isInCell())
            return -1;
        // An actor with its feet in water has the material of water. "In water" is the port's choice, the water over a
        // twentieth of the actor's height (about the ankle, 0.05; isWading is the knee, 0.25): the executable's own
        // threshold was not read
        constexpr int waterMaterial = 8;
        if (isUnderwater(actor, 0.05f))
            return waterMaterial;

        const osg::Vec3f pos = actor.getRefData().getPosition().asVec3();
        constexpr int groundMask
            = OFPhysics::CollisionType_World | OFPhysics::CollisionType_HeightMap | OFPhysics::CollisionType_Door;
        const OFPhysics::RayCastingResult hit
            = mPhysics->castRay(pos + osg::Vec3f(0.f, 0.f, 24.f), pos - osg::Vec3f(0.f, 0.f, 48.f), { actor }, {}, groundMask);
        if (!hit.mHit)
            return -1;

        // Stairs: the port's own rule (the executable asks a bit of the actor's movement that its physics sets): a flat
        // tread of an object with ground a stride ahead or behind it that is a step higher or lower
        if (!hit.mHitObject.isEmpty())
        {
            namespace Steps = FalloutScript::Footsteps;
            static const Steps::StairSettings stairSettings;
            const float facing = actor.getRefData().getPosition().rot[2];
            const osg::Vec3f forward(std::sin(facing), std::cos(facing), 0.f);
            const auto heightAt = [&](const osg::Vec3f& offset) -> std::optional<float> {
                const osg::Vec3f at = hit.mHitPos + offset;
                const OFPhysics::RayCastingResult other = mPhysics->castRay(
                    at + osg::Vec3f(0.f, 0.f, 40.f), at - osg::Vec3f(0.f, 0.f, 60.f), { actor }, {}, groundMask);
                if (!other.mHit)
                    return std::nullopt;
                return other.mHitPos.z();
            };
            // (what the plane of the tread rises along the probe is not a step: a ramp goes up evenly)
            const float slopeAhead = -(hit.mHitNormal.x() * forward.x() + hit.mHitNormal.y() * forward.y())
                / std::max(hit.mHitNormal.z(), 1e-3f) * stairSettings.mReach;
            stairs = Steps::isStairs(stairSettings, hit.mHitNormal.z(), hit.mHitPos.z(),
                heightAt(forward * stairSettings.mReach), heightAt(forward * -stairSettings.mReach), slopeAhead);
        }

        int material = -1;
        const CellStore* cell = actor.getCell();
        if (!hit.mHitObject.isEmpty())
        {
            const VFS::Path::Normalized model = hit.mHitObject.getClass().getCorrectedModel(hit.mHitObject);
            if (!model.empty())
            {
                try
                {
                    if (const std::shared_ptr<const Resource::BulletShape> shape
                        = mPhysics->getShapeManager()->getShape(model))
                        material = shape->mHavokMaterial;
                }
                catch (const std::exception& e)
                {
                    // The files of a model can be missing (a plugin may have more than the archives hold): the ground
                    // is then of no known material, and that is said once for the model
                    static std::set<std::string> sSaid;
                    if (sSaid.emplace(std::string(model.value())).second)
                        Log(Debug::Warning) << "Material of the floor of " << model << " is not known: " << e.what();
                }
            }
        }
        else if (cell != nullptr && cell->getCell()->isExterior())
        {
            // The land: the texture that shows most where the foot comes down. Each quarter of a cell has a base
            // texture and layers over it that show where their opacity is.
            constexpr float cellSize = 4096.f;
            constexpr float quarter = cellSize / 2.f;
            const float x = hit.mHitPos.x();
            const float y = hit.mHitPos.y();
            const int cellX = static_cast<int>(std::floor(x / cellSize));
            const int cellY = static_cast<int>(std::floor(y / cellSize));
            const std::shared_ptr<const ESMTerrain::LandObject> land
                = mRendering->getLandManager()->getLand(ESM::ExteriorCellLocation(cellX, cellY, cell->getCell()->getWorldSpace()));
            const ESM::LandData* data = land != nullptr ? land->getData(0) : nullptr;
            if (data != nullptr && data->isEsm4())
            {
                const float localX = x - static_cast<float>(cellX) * cellSize;
                const float localY = y - static_cast<float>(cellY) * cellSize;
                const bool right = localX >= quarter;
                const bool top = localY >= quarter;
                const ESM4::Land::Texture& texture = data->getEsm4Texture((right ? 1 : 0) + (top ? 2 : 0));
                ESM::FormId ltex = ESM::FormId::fromUint32(texture.base.formId);
                // The point of the 17 by 17 grid of the quarter that is nearest
                const int gridX = std::clamp(static_cast<int>(std::lround((localX - (right ? quarter : 0.f)) / quarter * 16.f)), 0, 16);
                const int gridY = std::clamp(static_cast<int>(std::lround((localY - (top ? quarter : 0.f)) / quarter * 16.f)), 0, 16);
                const int position = gridY * 17 + gridX;
                float strongest = 0.5f;
                for (const ESM4::Land::TxtLayer& layer : texture.layers)
                    for (const ESM4::Land::VTXT& point : layer.data)
                        if (point.position == position && point.opacity > strongest)
                        {
                            strongest = point.opacity;
                            ltex = ESM::FormId::fromUint32(layer.texture.formId);
                        }
                if (!ltex.isZeroOrUnset())
                    if (const ESM4::LandTexture* record = mStore.get<ESM4::LandTexture>().search(ESM::RefId::formIdRefId(ltex)))
                        material = record->mHavokMaterial;
            }
        }
        if (material < 0)
        {
            // Said for the first few: what the ground was where its material was not known
            static int sSaid = 0;
            if (sSaid < 30)
            {
                ++sSaid;
                Log(Debug::Info) << "Floor material of " << actor.getCellRef().getRefId() << " not known: "
                                 << (!hit.mHitObject.isEmpty()
                                            ? "object " + std::string(hit.mHitObject.getClass().getCorrectedModel(hit.mHitObject).value())
                                            : std::string(cell != nullptr && cell->getCell()->isExterior() ? "land" : "no object"));
            }
        }
        return material;
    }

    bool World::isOnGround(const OFWorld::Ptr& ptr) const
    {
        return mPhysics->isOnGround(ptr);
    }

    void World::togglePOV(bool force)
    {
        mRendering->getCamera()->toggleViewMode(force);
    }

    bool World::isFirstPerson() const
    {
        return mRendering->getCamera()->getMode() == OFRender::Camera::Mode::FirstPerson;
    }

    bool World::isPreviewModeEnabled() const
    {
        return mRendering->getCamera()->getMode() == OFRender::Camera::Mode::Preview;
    }

    bool World::toggleVanityMode(bool enable)
    {
        return mRendering->getCamera()->toggleVanityMode(enable);
    }

    void World::disableDeferredPreviewRotation()
    {
        mRendering->getCamera()->disableDeferredPreviewRotation();
    }

    void World::applyDeferredPreviewRotationToPlayer(float dt)
    {
        mRendering->getCamera()->applyDeferredPreviewRotationToPlayer(dt);
    }

    OFRender::Camera* World::getCamera()
    {
        return mRendering->getCamera();
    }

    bool World::vanityRotateCamera(const float* rot)
    {
        auto* camera = mRendering->getCamera();
        if (!camera->isVanityOrPreviewModeEnabled())
            return false;

        camera->setPitch(camera->getPitch() + rot[0]);
        camera->setYaw(camera->getYaw() + rot[2]);
        return true;
    }

    void World::saveLoaded(const ESM::ESMReader& reader)
    {
        mStore.rebuildIdsIndex();
        mStore.validateDynamic();
        mTimeManager->setup(mGlobalVariables);
    }

    std::pair<const ESM4::Npc*, ESM::RefNum> World::findPlayerRecord() const
    {
        // The player of the games: the reference PlayerRef (form 0x14) of the NPC_ record 0x7, both of the first
        // Fallout plugin (Fallout3.esm, FalloutNV.esm or the master of Tale of Two Wastelands)
        const int plugin = std::max(0, OFMechanics::falloutFirstPlugin(mContentFiles));
        const ESM::FormId record{ 0x7, plugin };
        const ESM4::Npc* player = mStore.get<ESM4::Npc>().search(record);
        if (player == nullptr)
            throw std::runtime_error("The content has no player: the NPC_ record 0x7 of the first Fallout plugin ("
                + (static_cast<std::size_t>(plugin) < mContentFiles.size() ? mContentFiles[plugin]
                                                                           : std::string("none"))
                + ") is missing");
        return { player, ESM::RefNum{ 0x14, plugin } };
    }

    void World::setupPlayer()
    {
        const auto [player, reference] = findPlayerRecord();
        if (!mPlayer)
            mPlayer = std::make_unique<OFWorld::Player>(player, reference);
        else
        {
            // Remove the old actor of the player
            OFBase::Environment::get().getMechanicsManager()->remove(getPlayerPtr(), true);
            mNavigator->removeAgent(getPathfindingAgentBounds(getPlayerConstPtr()));
            mPhysics->remove(getPlayerPtr());
            mRendering->removePlayer(getPlayerPtr());
            OFBase::Environment::get().getLuaManager()->objectRemovedFromScene(getPlayerPtr());

            mPlayer->set(player);
        }

        Ptr ptr = mPlayer->getPlayer();
        mRendering->setupPlayer(ptr);
        OFBase::Environment::get().getLuaManager()->setupPlayer(ptr);
    }

    void World::renderPlayer()
    {
        OFBase::Environment::get().getMechanicsManager()->remove(getPlayerPtr(), true);

        OFWorld::Ptr player = getPlayerPtr();

        mRendering->renderPlayer(player);

        scaleObject(player, player.getCellRef().getScale(), true); // apply race height
        rotateObject(player, osg::Vec3f(), OFBase::RotationFlag_inverseOrder | OFBase::RotationFlag_adjust);

        OFBase::Environment::get().getMechanicsManager()->add(getPlayerPtr());

        mPhysics->remove(getPlayerPtr());
        getPlayerPtr().getClass().insertObjectPhysics(getPlayerPtr(), {}, osg::Quat(), *mPhysics);

        const DetourNavigator::AgentBounds agentBounds = getPathfindingAgentBounds(getPlayerConstPtr());
        if (!mNavigator->addAgent(agentBounds))
            Log(Debug::Warning) << "Player agent bounds are not supported by navigator: " << agentBounds;
    }

    int World::canRest() const
    {
        int result = 0;

        CellStore* currentCell = mWorldScene->getCurrentCell();

        Ptr player = mPlayer->getPlayer();

        const OFPhysics::Actor* actor = mPhysics->getActor(player);
        if (!actor)
            throw std::runtime_error("can't find player");

        const osg::Vec3f playerPos(player.getRefData().getPosition().asVec3());
        if (isUnderwater(currentCell, playerPos) || isWalkingOnWater(player))
            result |= Rest_PlayerIsUnderwater;

        // In the air: off solid ground, or a fall has begun (the player's actor marks where it left the ground)
        bool falling = false;
        if (const OFMechanics::Actor* character = OFMechanics::findActor(player);
            character != nullptr && character->isPlayer())
            falling = static_cast<const OFMechanics::PlayerCharacter*>(character)->getFallStart().has_value();
        if ((actor->getCollisionMode() && (!mPhysics->isOnSolidGround(player) || falling)) || isFlying(player))
            result |= Rest_PlayerIsInAir;

        if (mPlayer->enemiesNearby())
            result |= Rest_EnemiesAreNearby;

        if (!currentCell->getCell()->noSleep())
            result |= Rest_CanSleep;

        return result;
    }

    OFRender::Animation* World::getAnimation(const OFWorld::Ptr& ptr)
    {
        auto* animation = mRendering->getAnimation(ptr);
        if (!animation)
        {
            mWorldScene->removeFromPagedRefs(ptr);
            animation = mRendering->getAnimation(ptr);
            if (animation)
                mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
        }
        return animation;
    }

    const OFRender::Animation* World::getAnimation(const OFWorld::ConstPtr& ptr) const
    {
        return mRendering->getAnimation(ptr);
    }

    void World::screenshot(osg::Image* image, int w, int h)
    {
        mRendering->screenshot(image, w, h);
    }

    void World::activateDoor(const OFWorld::Ptr& door)
    {
        auto state = door.getClass().getDoorState(door);
        switch (state)
        {
            case OFWorld::DoorState::Idle:
                if (door.getRefData().getPosition().rot[2] == door.getCellRef().getPosition().rot[2])
                    state = OFWorld::DoorState::Opening; // if closed, then open
                else
                    state = OFWorld::DoorState::Closing; // if open, then close
                break;
            case OFWorld::DoorState::Closing:
                state = OFWorld::DoorState::Opening; // if closing, then open
                break;
            case OFWorld::DoorState::Opening:
            default:
                state = OFWorld::DoorState::Closing; // if opening, then close
                break;
        }
        door.getClass().setDoorState(door, state);
        mDoorStates[door] = state;
    }

    void World::activateDoor(const Ptr& door, OFWorld::DoorState state)
    {
        door.getClass().setDoorState(door, state);
        mDoorStates[door] = state;
        if (state == OFWorld::DoorState::Idle)
        {
            mDoorStates.erase(door);
            rotateDoor(door, state, 1);
        }
    }

    bool World::getPlayerStandingOn(const OFWorld::ConstPtr& object)
    {
        OFWorld::Ptr player = getPlayerPtr();
        return mPhysics->isActorStandingOn(player, object);
    }

    bool World::getActorStandingOn(const OFWorld::ConstPtr& object)
    {
        std::vector<OFWorld::Ptr> actors;
        mPhysics->getActorsStandingOn(object, actors);
        return !actors.empty();
    }

    void World::getActorsStandingOn(const OFWorld::ConstPtr& object, std::vector<OFWorld::Ptr>& actors)
    {
        mPhysics->getActorsStandingOn(object, actors);
    }

    bool World::getPlayerCollidingWith(const OFWorld::ConstPtr& object)
    {
        return mPhysics->isObjectCollidingWith(object, OFPhysics::ScriptedCollisionType_Player);
    }

    bool World::getActorCollidingWith(const OFWorld::ConstPtr& object)
    {
        return mPhysics->isObjectCollidingWith(object, OFPhysics::ScriptedCollisionType_Actor);
    }

    float World::getWindSpeed() const
    {
        if (isCellExterior() || isCellQuasiExterior())
            return mWeatherManager->getWindSpeed();
        else
            return 0.f;
    }

    bool World::isInStorm() const
    {
        if (isCellExterior() || isCellQuasiExterior())
            return mWeatherManager->isInStorm();
        else
            return false;
    }

    osg::Vec3f World::getStormDirection() const
    {
        if (isCellExterior() || isCellQuasiExterior())
            return mWeatherManager->getStormDirection();
        else
            return osg::Vec3f(0, 1, 0);
    }

    bool World::getLOS(const OFWorld::ConstPtr& actor, const OFWorld::ConstPtr& targetActor)
    {
        if (!targetActor.getRefData().isEnabled() || !actor.getRefData().isEnabled())
            return false; // cannot get LOS unless both NPC's are enabled
        if (!targetActor.getRefData().getBaseNode() || !actor.getRefData().getBaseNode())
            return false; // not in active cell

        return mPhysics->getLineOfSight(actor, targetActor);
    }

    float World::getDistToNearestRayHit(const osg::Vec3f& from, const osg::Vec3f& dir, float maxDist, bool includeWater)
    {
        osg::Vec3f to(dir);
        to.normalize();
        to = from + (to * maxDist);

        // (the bodies of the dead are in the way of rays as of what walks)
        int collisionTypes = OFPhysics::CollisionType_World | OFPhysics::CollisionType_HeightMap
            | OFPhysics::CollisionType_Door | OFPhysics::CollisionType_Ragdoll;
        if (includeWater)
        {
            collisionTypes |= OFPhysics::CollisionType_Water;
        }
        OFPhysics::RayCastingResult result
            = mPhysics->castRay(from, to, { OFWorld::Ptr() }, std::vector<OFWorld::Ptr>(), collisionTypes);

        if (!result.mHit)
            return maxDist;
        else
            return (result.mHitPos - from).length();
    }

    void World::enableActorCollision(const OFWorld::Ptr& actor, bool enable)
    {
        OFPhysics::Actor* physicActor = mPhysics->getActor(actor);
        if (physicActor)
            physicActor->enableCollisionBody(enable);
    }

    static std::optional<ESM::Position> searchMarkerPosition(const CellStore& cellStore, std::string_view editorId)
    {
        for (const OFWorld::LiveCellRef<ESM4::Static>& stat4 : cellStore.getReadOnlyEsm4Statics().mList)
        {
            if (Misc::StringUtils::lowerCase(stat4.mBase->mEditorId) == editorId)
                return stat4.mRef.getPosition();
        }
        return std::nullopt;
    }

    static std::optional<ESM::Position> searchDoorDestInCell(const CellStore& cellStore)
    {
        ESM::RefId cellId = cellStore.getCell()->getId();
        std::vector<const OFWorld::CellRef*> sortedDoors;
        for (const OFWorld::LiveCellRef<ESM::Door>& door : cellStore.getReadOnlyDoors().mList)
        {
            if (!door.mRef.getTeleport())
                continue;
            sortedDoors.push_back(&door.mRef);
        }
        for (const OFWorld::LiveCellRef<ESM4::Door>& door : cellStore.getReadOnlyEsm4Doors().mList)
        {
            if (!door.mRef.getTeleport())
                continue;
            sortedDoors.push_back(&door.mRef);
        }

        // Sort teleporting doors alphabetically, first by ID, then by destination cell to make search consistent
        std::sort(sortedDoors.begin(), sortedDoors.end(), [](const OFWorld::CellRef* lhs, const OFWorld::CellRef* rhs) {
            if (lhs->getRefId() != rhs->getRefId())
                return lhs->getRefId() < rhs->getRefId();

            return lhs->getDestCell() < rhs->getDestCell();
        });

        WorldModel* worldModel = OFBase::Environment::get().getWorldModel();
        for (const OFWorld::CellRef* door : sortedDoors)
        {
            const OFWorld::CellStore* source = worldModel->findCell(door->getDestCell());
            if (source == nullptr)
                continue;

            // Find door leading to our current teleport door
            // and use its destination to position inside cell.
            // \note Using _any_ door pointed to the cell,
            // not the one pointed to current door.
            for (const OFWorld::LiveCellRef<ESM::Door>& destDoor : source->getReadOnlyDoors().mList)
            {
                if (cellId == destDoor.mRef.getDestCell())
                {
                    ESM::Position doorDest = destDoor.mRef.getDoorDest();
                    doorDest.rot[0] = doorDest.rot[1] = doorDest.rot[2] = 0;
                    return doorDest;
                }
            }
            for (const OFWorld::LiveCellRef<ESM4::Door>& destDoor : source->getReadOnlyEsm4Doors().mList)
            {
                if (cellId == destDoor.mRef.getDestCell())
                    return destDoor.mRef.getDoorDest();
            }
        }

        return std::nullopt;
    }

    ESM::RefId World::findInteriorPosition(std::string_view name, ESM::Position& pos)
    {
        pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;
        pos.pos[0] = pos.pos[1] = pos.pos[2] = 0;

        const OFWorld::CellStore* cellStore = mWorldModel.findInterior(name);
        if (!cellStore)
            return ESM::RefId();
        ESM::RefId cellId = cellStore->getCell()->getId();

        if (std::optional<ESM::Position> destPos = searchMarkerPosition(*cellStore, "cocmarkerheading"))
        {
            pos = *destPos;
            return cellId;
        }
        if (std::optional<ESM::Position> destPos = searchDoorDestInCell(*cellStore))
        {
            pos = *destPos;
            return cellId;
        }
        if (std::optional<ESM::Position> destPos = searchMarkerPosition(*cellStore, "xmarkerheading"))
        {
            pos = *destPos;
            return cellId;
        }

        // Fall back to the first static location.
        const OFWorld::CellRefList<ESM4::Static>::List& statics4 = cellStore->getReadOnlyEsm4Statics().mList;
        if (!statics4.empty())
        {
            pos = statics4.begin()->mRef.getPosition();
            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;
            return cellId;
        }
        const OFWorld::CellRefList<ESM::Static>::List& statics = cellStore->getReadOnlyStatics().mList;
        if (!statics.empty())
        {
            pos = statics.begin()->mRef.getPosition();
            pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;
            return cellId;
        }

        return ESM::RefId();
    }

    ESM::RefId World::findExteriorPosition(std::string_view nameId, ESM::Position& pos)
    {
        pos.rot[0] = pos.rot[1] = pos.rot[2] = 0;

        const OFWorld::CellStore* cellStore = mWorldModel.findCell(nameId);

        if (cellStore != nullptr && !cellStore->isExterior())
            return ESM::RefId();

        // A cell of a worldspace of a Fallout game: "WastelandNV:-2,-3"
        if (!cellStore)
        {
            if (const std::optional<WorldspaceStart> start = parseWorldspaceStart(nameId))
            {
                const ESM4::World* worldspace = nullptr;
                for (const ESM4::World& world : mStore.get<ESM4::World>())
                {
                    if (Misc::StringUtils::ciEqual(world.mEditorId, start->mWorldspace))
                    {
                        worldspace = &world;
                        break;
                    }
                }
                if (worldspace == nullptr)
                    throw std::runtime_error("Can't find the worldspace " + std::string(start->mWorldspace));
                cellStore = &mWorldModel.getExterior(
                    ESM::ExteriorCellLocation(start->mX, start->mY, ESM::RefId(worldspace->mId)));
            }
        }

        if (!cellStore)
        {
            size_t comma = nameId.find(',');
            if (comma != std::string::npos)
            {
                int x, y;
                std::from_chars_result xResult = std::from_chars(nameId.data(), nameId.data() + comma, x);
                std::from_chars_result yResult
                    = std::from_chars(nameId.data() + comma + 1, nameId.data() + nameId.size(), y);
                if (xResult.ec == std::errc::result_out_of_range || yResult.ec == std::errc::result_out_of_range)
                    throw std::runtime_error("Cell coordinates out of range.");
                else if (xResult.ec == std::errc{} && yResult.ec == std::errc{})
                    cellStore
                        = &mWorldModel.getExterior(ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
                // ignore std::errc::invalid_argument, as this means that name probably refers to a interior cell
                // instead of comma separated coordinates
            }
        }

        if (!cellStore)
            return ESM::RefId();
        const OFWorld::Cell* ext = cellStore->getCell();

        if (std::optional<ESM::Position> destPos = searchMarkerPosition(*cellStore, "cocmarkerheading"))
        {
            pos = *destPos;
            return ext->getId();
        }
        if (std::optional<ESM::Position> destPos = searchMarkerPosition(*cellStore, "xmarkerheading"))
        {
            pos = *destPos;
            return ext->getId();
        }

        int x = ext->getGridX();
        int y = ext->getGridY();
        const osg::Vec2f posFromIndex = indexToPosition(ESM::ExteriorCellLocation(x, y, ext->getWorldSpace()), true);
        pos.pos[0] = posFromIndex.x();
        pos.pos[1] = posFromIndex.y();

        // Note: Z pos will be adjusted by adjustPosition later
        pos.pos[2] = 0;

        return ext->getId();
    }

    void World::enableTeleporting(bool enable)
    {
        mTeleportEnabled = enable;
    }

    bool World::isTeleportingEnabled() const
    {
        return mTeleportEnabled;
    }

    void World::enableLevitation(bool enable)
    {
        mLevitationEnabled = enable;
    }

    bool World::isLevitationEnabled() const
    {
        return mLevitationEnabled;
    }

    void World::reattachPlayerCamera()
    {
        mRendering->rebuildPtr(getPlayerPtr());
    }

    bool World::getGodModeState() const
    {
        return mGodMode;
    }

    bool World::toggleGodMode()
    {
        mGodMode = !mGodMode;

        return mGodMode;
    }

    bool World::toggleScripts()
    {
        mScriptsEnabled = !mScriptsEnabled;
        return mScriptsEnabled;
    }

    bool World::getScriptsEnabled() const
    {
        return mScriptsEnabled;
    }

    void World::loadContentFiles(const Files::Collections& fileCollections, const std::vector<std::string>& content,
        ToUTF8::Utf8Encoder* encoder, Loading::Listener* listener)
    {
        GameContentLoader gameContentLoader;
        EsmLoader esmLoader(mStore, mReaders, encoder, mESMVersions);

        gameContentLoader.addLoader(".esm", esmLoader);
        gameContentLoader.addLoader(".esp", esmLoader);
        gameContentLoader.addLoader(".omwgame", esmLoader);
        gameContentLoader.addLoader(".omwaddon", esmLoader);
        gameContentLoader.addLoader(".project", esmLoader);

        OFScriptsLoader omwScriptsLoader(mStore);
        gameContentLoader.addLoader(".omwscripts", omwScriptsLoader);

        int idx = 0;
        for (const std::string& file : content)
        {
            const Files::MultiDirCollection& col = fileCollections.getCollection(Misc::getFileExtension(file));
            if (col.doesExist(file))
            {
                gameContentLoader.load(col.getPath(file), idx, listener);
            }
            else
            {
                std::string message = "Failed loading " + file + ": the content file does not exist";
                throw std::runtime_error(message);
            }
            idx++;
        }

        // The clock globals of the engine: no Fallout plugin has them (they came from the placeholder records of
        // Morrowind before step 4a), and a Morrowind master may lack some
        ensureNeededRecords();
    }

    void World::loadGroundcoverFiles(const Files::Collections& fileCollections,
        const std::vector<std::string>& groundcoverFiles, ToUTF8::Utf8Encoder* encoder, Loading::Listener* listener)
    {
        if (!Settings::groundcover().mEnabled)
            return;

        Log(Debug::Info) << "Loading groundcover:";

        mGroundcoverStore.init(mStore.get<ESM::Static>(), fileCollections, groundcoverFiles, encoder, listener);
    }

    const std::vector<std::string>& World::getContentFiles() const
    {
        return mContentFiles;
    }

    bool World::useTorches() const
    {
        // If we are in exterior, check the weather manager.
        // In interiors there are no precipitations and sun, so check the ambient
        // Looks like pseudo-exteriors considered as interiors in this case
        OFWorld::CellStore* cell = mPlayer->getPlayer().getCell();
        if (cell->isExterior())
        {
            float hour = getTimeStamp().getHour();
            return mWeatherManager->useTorches(hour);
        }
        else
        {
            const OFWorld::Cell& cellVariant = *cell->getCell();
            uint32_t ambient = cellVariant.getMood().mAmbiantColor;
            int ambientTotal = (ambient & 0xff) + ((ambient >> 8) & 0xff) + ((ambient >> 16) & 0xff);
            return !cell->getCell()->noSleep() && ambientTotal <= 201;
        }
    }

    const osg::Vec4f& World::getSunLightPosition() const
    {
        return mRendering->getSunLightPosition();
    }

    float World::getSunVisibility() const
    {
        return mWeatherManager->getSunVisibility();
    }

    float World::getSunPercentage() const
    {
        return mWeatherManager->getSunPercentage(getTimeStamp().getHour());
    }

    float World::getPhysicsFrameRateDt() const
    {
        return mPhysics->mPhysicsDt;
    }

    bool World::findInteriorPositionInWorldSpace(const OFWorld::CellStore* cell, osg::Vec3f& result)
    {
        if (cell->isExterior())
            return false;

        // Search for a 'nearest' exterior, counting each cell between the starting
        // cell and the exterior as a distance of 1.  Will fail for isolated interiors.
        std::set<ESM::RefId> checkedCells;
        std::set<ESM::RefId> currentCells;
        std::set<ESM::RefId> nextCells;
        nextCells.insert(cell->getCell()->getId());

        while (!nextCells.empty())
        {
            currentCells = nextCells;
            nextCells.clear();
            for (const auto& currentCell : currentCells)
            {
                OFWorld::CellStore* next = mWorldModel.findCell(currentCell);
                if (next == nullptr)
                    continue;

                // Check if any door in the cell leads to an exterior directly
                for (const OFWorld::LiveCellRef<ESM::Door>& ref : next->getReadOnlyDoors().mList)
                {
                    if (!ref.mRef.getTeleport())
                        continue;

                    if (ref.mRef.getDestCell().is<ESM::ESM3ExteriorCellRefId>())
                    {
                        ESM::Position pos = ref.mRef.getDoorDest();
                        result = pos.asVec3();
                        return true;
                    }
                    else
                    {
                        ESM::RefId dest = ref.mRef.getDestCell();
                        if (!checkedCells.count(dest) && !currentCells.count(dest))
                            nextCells.insert(dest);
                    }
                }

                checkedCells.insert(currentCell);
            }
        }

        // No luck :(
        return false;
    }

    void World::updateWeather(float duration, bool paused)
    {
        bool isExterior = isCellExterior() || isCellQuasiExterior();
        if (mPlayer->wasTeleported())
        {
            mPlayer->setTeleported(false);

            const ESM::RefId& playerRegion = getPlayerPtr().getCell()->getCell()->getRegion();
            mWeatherManager->playerTeleported(playerRegion, isExterior);
        }

        const TimeStamp time = getTimeStamp();
        mWeatherManager->update(duration, paused, time, isExterior);
    }

    OFWorld::Ptr World::getPlayerPtr()
    {
        return mPlayer->getPlayer();
    }

    OFWorld::ConstPtr World::getPlayerConstPtr() const
    {
        return mPlayer->getConstPlayer();
    }

    void World::setPlayerTraveling(bool traveling)
    {
        mPlayerTraveling = traveling;
    }

    bool World::isPlayerTraveling() const
    {
        return mPlayerTraveling;
    }

    float World::getTerrainHeightAt(const osg::Vec3f& worldPos, ESM::RefId worldspace) const
    {
        return mRendering->getTerrainHeightAt(worldPos, worldspace);
    }

    osg::Vec3f World::getHalfExtents(const ConstPtr& object, bool rendering) const
    {
        if (!object.getClass().isActor())
            return mRendering->getHalfExtents(object);

        // Handle actors separately because of bodyparts
        if (rendering)
            return mPhysics->getRenderingHalfExtents(object);
        else
            return mPhysics->getHalfExtents(object);
    }

    std::filesystem::path World::exportSceneGraph(const Ptr& ptr)
    {
        auto file = mUserDataPath / "openfallout.osgt";
        if (!ptr.isEmpty())
        {
            mRendering->pagingBlacklistObject(mStore.find(ptr.getCellRef().getRefId()), ptr);
            mWorldScene->removeFromPagedRefs(ptr);
        }
        mRendering->exportSceneGraph(ptr, file, "Ascii");
        return file;
    }

    void World::spawnEffect(VFS::Path::NormalizedView model, const std::string& textureOverride,
        const osg::Vec3f& worldPos, float scale, bool isMagicVFX, bool useAmbientLight, std::string_view effectId,
        bool loop)
    {
        mRendering->spawnEffect(model, textureOverride, worldPos, scale, isMagicVFX, useAmbientLight, effectId, loop);
    }

    void World::removeEffect(std::string_view effectId)
    {
        mRendering->removeEffect(effectId);
    }

    bool World::isWalkingOnWater(const ConstPtr& actor) const
    {
        const OFPhysics::Actor* physicActor = mPhysics->getActor(actor);
        if (physicActor && physicActor->isWalkingOnWater())
            return true;
        return false;
    }

    DetourNavigator::Navigator* World::getNavigator() const
    {
        return mNavigator.get();
    }

    void World::updateActorPath(const OFWorld::ConstPtr& actor, const std::deque<osg::Vec3f>& path,
        const DetourNavigator::AgentBounds& agentBounds, const osg::Vec3f& start, const osg::Vec3f& end) const
    {
        mRendering->updateActorPath(actor, path, agentBounds, start, end);
    }

    void World::removeActorPath(const OFWorld::ConstPtr& actor) const
    {
        mRendering->removeActorPath(actor);
    }

    void World::setNavMeshNumberToRender(const std::size_t value)
    {
        mRendering->setNavMeshNumber(value);
    }

    DetourNavigator::AgentBounds World::getPathfindingAgentBounds(const OFWorld::ConstPtr& actor) const
    {
        const OFPhysics::Actor* physicsActor = mPhysics->getActor(actor);
        // The games have one navigation mesh for all: the characters and creatures go by the mesh of the player's
        // size (an interior has that one only), whatever the size of their own character bodies
        if (physicsActor != nullptr && actor != getPlayerConstPtr())
            physicsActor = mPhysics->getActor(getPlayerConstPtr());
        if (physicsActor == nullptr || !actor.isInCell() || actor.getCell()->isExterior())
            return DetourNavigator::AgentBounds{ Settings::game().mActorCollisionShapeType,
                Settings::game().mDefaultActorPathfindHalfExtents };
        else
            return DetourNavigator::AgentBounds{ physicsActor->getCollisionShapeType(),
                physicsActor->getHalfExtents() };
    }

    bool World::isAreaOccupiedByOtherActor(const OFWorld::ConstPtr& actor, const osg::Vec3f& position) const
    {
        const osg::Vec3f halfExtents = getPathfindingAgentBounds(actor).mHalfExtents;
        const float maxHalfExtent = std::max(halfExtents.x(), std::max(halfExtents.y(), halfExtents.z()));
        return mPhysics->isAreaOccupiedByOtherActor(actor.mRef, position, 2 * maxHalfExtent);
    }

    void World::reportStats(unsigned int frameNumber, osg::Stats& stats) const
    {
        DetourNavigator::reportStats(mNavigator->getStats(), frameNumber, stats);
        mPhysics->reportStats(frameNumber, stats);
        mWorldScene->reportStats(frameNumber, stats);
    }

    std::vector<OFWorld::Ptr> World::getAll(const ESM::RefId& id)
    {
        return mWorldModel.getAll(id);
    }

    Misc::Rng::Generator& World::getPrng()
    {
        return mPrng;
    }

    OFRender::PostProcessor* World::getPostProcessor()
    {
        return mRendering->getPostProcessor();
    }

    void World::setActorActive(const OFWorld::Ptr& ptr, bool value)
    {
        if (OFPhysics::Actor* const actor = mPhysics->getActor(ptr))
            actor->setActive(value);
    }

    bool World::startRagdoll(const OFWorld::Ptr& ptr, const OFRender::Animation* bones)
    {
        const OFRender::Animation* animation = bones != nullptr ? bones : mRendering->getAnimation(ptr);
        if (animation == nullptr)
            return false;
        // the player's class has no model, its skeleton is the MODL of its record
        return mPhysics->startRagdoll(ptr, OFClass::ESM4Impl::skeletonModel(ptr),
            [animation](std::string_view name) { return animation->getBone(name); });
    }

    void World::stopRagdoll(const OFWorld::Ptr& ptr)
    {
        mPhysics->stopRagdoll(ptr);
    }

    bool World::hasRagdoll(const OFWorld::ConstPtr& ptr) const
    {
        return mPhysics->hasRagdoll(ptr);
    }
}
