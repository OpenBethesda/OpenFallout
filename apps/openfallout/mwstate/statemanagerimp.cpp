#include "statemanagerimp.hpp"

#include <filesystem>

#include <SDL_clipboard.h>

#include <components/debug/debuglog.hpp>

#include <components/esm3/actoridconverter.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/falloutscript/savestate.hpp>

#include <components/l10n/manager.hpp>

#include <components/loadinglistener/loadinglistener.hpp>

#include <components/files/conversion.hpp>
#include <components/misc/algorithm.hpp>
#include <components/settings/values.hpp>

#include <osg/Image>

#include <osgDB/Registry>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/datetimemanager.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/scene.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "quicksavemanager.hpp"

void OFState::StateManager::cleanup(bool force)
{
    if (mState != State_NoGame || force)
    {
        OFBase::Environment::get().getSoundManager()->clear();
        OFBase::Environment::get().getWindowManager()->clear();
        OFBase::Environment::get().getWorld()->clear();
        OFBase::Environment::get().getInputManager()->clear();
        OFBase::Environment::get().getMechanicsManager()->clear();
        // The objects it kept were in the cells of the world that is gone
        OFBase::Environment::get().getFalloutScripts()->reset();

        mCharacterManager.setCurrentCharacter(nullptr);
        mTimePlayed = 0;
        mLastSavegame.clear();
        mPlayerDied = false;

        mState = State_NoGame;
        OFBase::Environment::get().getLuaManager()->noGame();
    }
    else
    {
        // TODO: do we need this cleanup?
        OFBase::Environment::get().getLuaManager()->clear();
    }
}

std::map<int, int> OFState::StateManager::buildContentFileIndexMap(const ESM::ESMReader& reader) const
{
    const std::vector<std::string>& current = OFBase::Environment::get().getWorld()->getContentFiles();

    const std::vector<ESM::Header::MasterData>& prev = reader.getGameFiles();

    std::map<int, int> map;

    for (int iPrev = 0; iPrev < static_cast<int>(prev.size()); ++iPrev)
    {
        for (int iCurrent = 0; iCurrent < static_cast<int>(current.size()); ++iCurrent)
            if (Misc::StringUtils::ciEqual(prev[iPrev].name, current[iCurrent]))
            {
                map.insert(std::make_pair(iPrev, iCurrent));
                break;
            }
    }

    return map;
}

OFState::StateManager::StateManager(const std::filesystem::path& saves, const std::vector<std::string>& contentFiles)
    : mQuitRequest(false)
    , mAskLoadRecent(false)
    , mState(State_NoGame)
    , mCharacterManager(saves, contentFiles)
    , mTimePlayed(0)
{
}

void OFState::StateManager::requestQuit()
{
    mQuitRequest = true;
}

bool OFState::StateManager::hasQuitRequest() const
{
    return mQuitRequest;
}

void OFState::StateManager::askLoadRecent()
{
    if (OFBase::Environment::get().getWindowManager()->getMode() == OFGui::GM_MainMenu)
        return;

    if (!mAskLoadRecent)
    {
        if (mLastSavegame.empty()) // no saves
        {
            OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);
        }
        else
        {
            std::string saveName = Files::pathToUnicodeString(mLastSavegame.filename());
            // Assume the last saved game belongs to the current character's slot list.
            const Character* character = getCurrentCharacter();
            if (character)
            {
                for (const auto& slot : *character)
                {
                    if (slot.mPath == mLastSavegame)
                    {
                        saveName = slot.mProfile.mDescription;
                        break;
                    }
                }
            }

            std::vector<std::string> buttons;
            buttons.emplace_back("#{Interface:Yes}");
            buttons.emplace_back("#{Interface:No}");
            auto l10n = OFBase::Environment::get().getL10nManager()->getContext("OFEngine");
            std::string message = l10n->formatMessage("AskLoadLastSave", { "save" }, { L10n::toUnicode(saveName) });
            OFBase::Environment::get().getWindowManager()->interactiveMessageBox(message, buttons);
            mAskLoadRecent = true;
        }
    }
}

bool OFState::StateManager::hasRecentSave()
{
    return !mLastSavegame.empty() && getCurrentCharacter() != nullptr;
}

void OFState::StateManager::loadRecent()
{
    Character* character = getCurrentCharacter();
    if (character == nullptr || mLastSavegame.empty())
    {
        OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);
        return;
    }
    // (requestLoad: a load in the middle of a frame can crash)
    requestLoad(character, mLastSavegame);
}

OFState::StateManager::State OFState::StateManager::getState() const
{
    return mState;
}

void OFState::StateManager::newGame(bool bypass)
{
    cleanup();

    try
    {
        Log(Debug::Info) << "Starting a new game";
        auto falloutScripts = OFBase::Environment::get().getFalloutScripts();
        falloutScripts->reset();
        // A game that has the quest of character creation of Fallout starts as the games do: the quest, not the
        // character generation of Morrowind, makes the character and takes the player to where the game begins
        const bool falloutStart = !bypass && falloutScripts->newGameQuest() != 0;
        OFBase::Environment::get().getWorld()->startNewGame(bypass || falloutStart, !falloutStart);
        if (falloutStart)
            falloutScripts->startNewGame();

        mState = State_Running;
        OFBase::Environment::get().getLuaManager()->gameLoaded();

        OFBase::Environment::get().getWindowManager()->fadeScreenOut(0);
        OFBase::Environment::get().getWindowManager()->fadeScreenIn(1);
    }
    catch (std::exception& e)
    {
        std::stringstream error;
        error << "Failed to start new game: " << e.what();

        Log(Debug::Error) << error.str();
        cleanup(true);

        OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);

        std::vector<std::string> buttons;
        buttons.emplace_back("#{Interface:OK}");
        OFBase::Environment::get().getWindowManager()->interactiveMessageBox(error.str(), buttons);
    }
}

void OFState::StateManager::endGame()
{
    mState = State_Ended;
    OFBase::Environment::get().getLuaManager()->gameEnded();
}

void OFState::StateManager::resumeGame()
{
    mState = State_Running;
    OFBase::Environment::get().getLuaManager()->gameLoaded();
}

void OFState::StateManager::saveGame(std::string_view description, const Slot* slot)
{
    OFBase::Environment::get().getLuaManager()->applyDelayedActions();

    OFState::Character* character = getCurrentCharacter();

    try
    {
        // (a save game does not keep the followers that are on their way through a door: they come out first; a door
        // whose cell is not found fails the save, as any other error of it does)
        OFBase::Environment::get().getMechanicsManager()->finishFollowerArrivals();

        const auto start = std::chrono::steady_clock::now();

        if (!character)
        {
            OFWorld::ConstPtr player = OFMechanics::getPlayer();
            const std::string name(player.getClass().getName(player));

            character = mCharacterManager.createCharacter(name);
            mCharacterManager.setCurrentCharacter(character);
        }

        ESM::SavedGame profile;

        OFBase::World& world = *OFBase::Environment::get().getWorld();

        OFWorld::Ptr player = world.getPlayerPtr();

        profile.mContentFiles = world.getContentFiles();

        // The header is rebuilt from Fallout's values: the name the player gave the character (else the full name
        // of NPC_ 0x7), the level and the health of the Fallout scripts. Fallout has no class: the class fields stay
        // empty.
        profile.mPlayerName = std::string(player.getClass().getName(player));
        profile.mPlayerLevel = 1;

        profile.mPlayerCellName = world.getCellName();
        profile.mInGameTime = world.getTimeManager()->getEpochTimeStamp();
        profile.mTimePlayed = mTimePlayed;
        profile.mDescription = description;
        profile.mCurrentDay = world.getTimeManager()->getTimeStamp().getDay();
        profile.mCurrentHealth = 0;
        profile.mMaximumHealth = 0;
        if (OFScript::FalloutScripts* fallout = OFBase::Environment::get().getFalloutScripts())
        {
            const OFScript::FalloutScripts::SavedPlayer saved = fallout->savedPlayer();
            if (saved.mKnown)
            {
                profile.mPlayerLevel = saved.mLevel;
                profile.mCurrentHealth = static_cast<float>(saved.mHealth);
                profile.mMaximumHealth = static_cast<float>(saved.mMaxHealth);
            }
        }

        Log(Debug::Info) << "Making a screenshot for saved game '" << description << "'";
        writeScreenshot(profile.mScreenshot);

        if (!slot)
            slot = character->createSlot(profile);
        else
            slot = character->updateSlot(slot, profile);

        Log(Debug::Info) << "Writing saved game '" << description << "' for character '" << profile.mPlayerName << "'";

        // Write to a memory stream first. If there is an exception during the save process, we don't want to trash the
        // existing save file we are overwriting.
        std::stringstream stream;

        ESM::ESMWriter writer;

        for (const std::string& contentFile : OFBase::Environment::get().getWorld()->getContentFiles())
            writer.addMaster(contentFile, 0); // not using the size information anyway -> use value of 0

        writer.setFormatVersion(ESM::CurrentSaveGameFormatVersion);

        // all unused
        writer.setVersion(0);
        writer.setType(0);
        writer.setAuthor("");
        writer.setDescription("");

        size_t recordCount = 1 // saved game header
            + OFBase::Environment::get().getLuaManager()->countSavedGameRecords()
            + OFBase::Environment::get().getWorld()->countSavedGameRecords()
            + OFBase::Environment::get().getFalloutScripts()->countSavedGameRecords()
            + OFBase::Environment::get().getMechanicsManager()->countSavedGameRecords()
            + OFBase::Environment::get().getInputManager()->countSavedGameRecords();
        writer.setRecordCount(static_cast<int>(recordCount));

        writer.save(stream);

        Loading::Listener& listener = *OFBase::Environment::get().getWindowManager()->getLoadingScreen();
        // Using only Cells for progress information, since they typically have the largest records by far
        listener.setProgressRange(OFBase::Environment::get().getWorld()->countSavedGameCells());
        listener.setLabel("#{OFEngine:SavingInProgress}", true);

        Loading::ScopedLoad load(&listener);

        writer.startRecord(ESM::REC_SAVE);
        slot->mProfile.save(writer);
        writer.endRecord(ESM::REC_SAVE);

        // LuaManager::write should be called before World::write because world also saves
        // local scripts that depend on LuaManager.
        OFBase::Environment::get().getLuaManager()->write(writer, listener);
        OFBase::Environment::get().getWorld()->write(writer, listener);
        OFBase::Environment::get().getFalloutScripts()->write(writer, listener);
        OFBase::Environment::get().getMechanicsManager()->write(writer, listener);
        OFBase::Environment::get().getInputManager()->write(writer, listener);

        // Ensure we have written the number of records that was estimated
        if (static_cast<size_t>(writer.getRecordCount()) != recordCount + 1) // 1 extra for TES3 record
            Log(Debug::Warning) << "Warning: number of written savegame records does not match. Estimated: "
                                << recordCount + 1 << ", written: " << writer.getRecordCount();

        writer.close();

        if (stream.fail())
            throw std::runtime_error(
                "Write operation failed (memory stream): " + std::generic_category().message(errno));

        // All good, write to file
        std::ofstream filestream(slot->mPath, std::ios::binary);
        filestream << stream.rdbuf();

        if (filestream.fail())
            throw std::runtime_error("Write operation failed (file stream): " + std::generic_category().message(errno));

        Settings::saves().mCharacter.set(Files::pathToUnicodeString(slot->mPath.parent_path().filename()));
        mLastSavegame = slot->mPath;

        const auto finish = std::chrono::steady_clock::now();

        Log(Debug::Info) << '\'' << description << "' is saved in "
                         << std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(finish - start).count()
                         << "ms";
    }
    catch (const std::exception& e)
    {
        std::stringstream error;
        error << "Failed to save game: " << e.what();

        Log(Debug::Error) << error.str();

        std::vector<std::string> buttons;
        buttons.emplace_back("#{Interface:OK}");
        OFBase::Environment::get().getWindowManager()->interactiveMessageBox(error.str(), buttons);

        // If no file was written, clean up the slot
        if (character && slot && !std::filesystem::exists(slot->mPath))
        {
            character->deleteSlot(slot);
            character->cleanup();
        }
    }
}

void OFState::StateManager::quickSave(std::string name)
{
    if (!(mState == State_Running
            && OFBase::Environment::get().getWorld()->getGlobalInt(OFWorld::Globals::sCharGenState) == -1 // char gen
            && OFBase::Environment::get().getWindowManager()->isSavingAllowed()))
    {
        // You can not save your game right now (the games say it on the line of the HUD, in their words)
        if (OFBase::Environment::get().getFalloutScripts() != nullptr)
            OFBase::Environment::get().getFalloutScripts()->quickNote(true, true);
        else
            OFBase::Environment::get().getWindowManager()->messageBox("#{OFEngine:SaveGameDenied}");
        return;
    }
    if (OFBase::Environment::get().getFalloutScripts() != nullptr)
        OFBase::Environment::get().getFalloutScripts()->quickNote(true, false);

    Character* currentCharacter = getCurrentCharacter(); // Get current character
    QuickSaveManager saveFinder(name, Settings::saves().mMaxQuicksaves);

    if (currentCharacter)
    {
        for (auto& save : *currentCharacter)
        {
            // Visiting slots allows the quicksave finder to find the oldest quicksave
            saveFinder.visitSave(&save);
        }
    }

    // Once all the saves have been visited, the save finder can tell us which
    // one to replace (or create)
    saveGame(name, saveFinder.getNextQuickSaveSlot());
}

void OFState::StateManager::loadGame(const std::filesystem::path& filepath)
{
    for (const auto& character : mCharacterManager)
    {
        for (const auto& slot : character)
        {
            if (std::filesystem::equivalent(slot.mPath, filepath))
            {
                loadGame(&character, slot.mPath);
                return;
            }
        }
    }

    OFState::Character* character = getCurrentCharacter();
    loadGame(character, filepath);
}

struct SaveFormatVersionError : public std::exception
{
    using std::exception::exception;

    SaveFormatVersionError(ESM::FormatVersion savegameFormat, const std::string& message)
        : mSavegameFormat(savegameFormat)
        , mErrorMessage(message)
    {
    }

    const char* what() const noexcept override { return mErrorMessage.c_str(); }
    ESM::FormatVersion getFormatVersion() const { return mSavegameFormat; }

protected:
    ESM::FormatVersion mSavegameFormat = ESM::DefaultFormatVersion;
    std::string mErrorMessage;
};

struct SaveVersionTooOldError : SaveFormatVersionError
{
    SaveVersionTooOldError(ESM::FormatVersion savegameFormat)
        : SaveFormatVersionError(savegameFormat, "format version " + std::to_string(savegameFormat) + " is too old")
    {
    }
};

struct SaveVersionTooNewError : SaveFormatVersionError
{
    SaveVersionTooNewError(ESM::FormatVersion savegameFormat)
        : SaveFormatVersionError(savegameFormat, "format version " + std::to_string(savegameFormat) + " is too new")
    {
    }
};

void OFState::StateManager::loadGame(
    const Character* character, const std::filesystem::path& filepath, std::optional<ESM::MissingContentAnswer> answer)
{
    mLastLoadRefused = false;
    try
    {
        cleanup();
        // What the save holds of Fallout is read over this; a save without it (made by a game of Morrowind) leaves the
        // quests as they begin
        OFBase::Environment::get().getFalloutScripts()->reset();

        Log(Debug::Info) << "Reading save file " << filepath.filename();

        ESM::ESMReader reader;
        reader.open(filepath);

        ESM::FormatVersion version = reader.getFormatVersion();
        if (version > ESM::CurrentSaveGameFormatVersion)
            throw SaveVersionTooNewError(version);
        else if (version < ESM::MinSupportedSaveGameFormatVersion)
            throw SaveVersionTooOldError(version);

        std::map<int, int> contentFileMap = buildContentFileIndexMap(reader);
        reader.setContentFileMapping(&contentFileMap);
        OFBase::Environment::get().getLuaManager()->setContentFileMapping(contentFileMap);

        ESM::ActorIdConverter actorIdConverter;
        if (version <= ESM::MaxActorIdSaveGameFormatVersion)
            reader.mActorIdConverter = &actorIdConverter;

        Loading::Listener& listener = *OFBase::Environment::get().getWindowManager()->getLoadingScreen();

        listener.setProgressRange(100);
        listener.setLabel("#{OFEngine:LoadingInProgress}");

        Loading::ScopedLoad load(&listener);

        bool firstPersonCam = false;

        size_t total = reader.getFileSize();
        int currentPercent = 0;
        while (reader.hasMoreRecs())
        {
            ESM::NAME n = reader.getRecName();
            reader.getRecHeader();

            switch (n.toInt())
            {
                case ESM::REC_SAVE:
                {
                    ESM::SavedGame profile;
                    profile.load(reader);
                    const auto& selectedContentFiles = OFBase::Environment::get().getWorld()->getContentFiles();
                    auto missingFiles = profile.getMissingContentFiles(selectedContentFiles);
                    if (!missingFiles.empty() && !confirmLoading(missingFiles, answer.value_or(mMissingContentAnswer)))
                    {
                        cleanup(true);
                        OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);
                        // A load that the caller's answer refused is told to the menu scripts, which asked for it
                        if (mLastLoadRefused)
                            OFBase::Environment::get().getLuaManager()->loadRefused(
                                filepath.parent_path().filename().string(), filepath.filename().string(), missingFiles);
                        return;
                    }
                    mTimePlayed = profile.mTimePlayed;
                    Log(Debug::Info) << "Loading saved game '" << profile.mDescription << "' for character '"
                                     << profile.mPlayerName << "'";
                }
                break;

                case ESM::REC_ALCH:
                case ESM::REC_MISC:
                case ESM::REC_ACTI:
                case ESM::REC_ARMO:
                case ESM::REC_BOOK:
                case ESM::REC_CLOT:
                case ESM::REC_ENCH:
                case ESM::REC_SPEL:
                case ESM::REC_WEAP:
                case ESM::REC_GLOB:
                case ESM::REC_PLAY:
                case ESM::REC_CSTA:
                case ESM::REC_WTHR:
                case ESM::REC_DYNA:
                case ESM::REC_ACTC:
                case ESM::REC_ENAB:
                case ESM::REC_LEVC:
                case ESM::REC_LEVI:
                case ESM::REC_LIGH:
                case ESM::REC_CREA:
                case ESM::REC_CONT:
                case ESM::REC_RAND:
                case ESM::REC_STAT:
                case ESM::REC_DOOR:
                case ESM::REC_PROB:
                case ESM::REC_INGR:
                    OFBase::Environment::get().getWorld()->readRecord(reader, n.toInt());
                    break;

                case ESM::REC_CAM_:
                    reader.getHNT(firstPersonCam, "FIRS");
                    break;

                case ESM::REC_PROJ:
                case ESM::REC_MPRJ:
                    // The arrows and the magic bolts of Morrowind in flight: they went with its combat and magic.
                case ESM::REC_NPC_:
                case ESM::REC_CLAS:
                    // The placeholder NPC_ record "Player" of a save made when the player was a Morrowind character,
                    // and a class the player made: the player is NPC_ 0x7 of the first Fallout plugin, and Fallout
                    // has no classes. No other NPC_ is written: the Morrowind characters are gone.
                case ESM::REC_GSCR:
                    // The running Morrowind global scripts of a save made before they were removed: there is nothing
                    // to restore them into, and nothing else in the file refers to them.
                case ESM::REC_JOUR:
                case ESM::REC_QUES:
                case ESM::REC_DIAS:
                    // The Morrowind journal (its entries and quests) and the Morrowind dialogue state (known topics,
                    // changed faction reactions) of a save made before they were removed: Fallout keeps its quests
                    // and conversations in the record of its scripts (FOSV), and nothing reads these any more.
                case ESM::REC_KEYS:
                    // The quick keys of Morrowind, which every save of the engine wrote until their menu was removed:
                    // the hot keys of Fallout are in the record of its scripts.
                case ESM::REC_GMAP:
                    // The explored part of the world map of Morrowind (an image), which every save of the engine wrote
                    // until its map window was removed: Fallout keeps its map markers in the record of its scripts
                    // (FOSV).
                case ESM::REC_MARK:
                    // The notes that the player put on the map of Morrowind, which went with that map.
                case ESM::REC_ASPL:
                    // The spell that the player had selected in the spell window of Morrowind, which went with that
                    // window (the selected spell is in the stats of the player).
                    reader.skipRecord();
                    break;

                case ESM::REC_FOSV:

                    OFBase::Environment::get().getFalloutScripts()->readRecord(reader, n.toInt());
                    break;

                case ESM::REC_DCOU:
                case ESM::REC_STLN:

                    OFBase::Environment::get().getMechanicsManager()->readRecord(reader, n.toInt());
                    break;

                case ESM::REC_INPU:
                    OFBase::Environment::get().getInputManager()->readRecord(reader, n.toInt());
                    break;

                case ESM::REC_LUAM:
                    OFBase::Environment::get().getLuaManager()->readRecord(reader, n.toInt());
                    break;

                default:

                    // ignore invalid records
                    Log(Debug::Warning) << "Warning: Ignoring unknown record: " << n.toStringView();
                    reader.skipRecord();
            }
            int progressPercent = static_cast<int>(float(reader.getFileOffset()) / total * 100);
            if (progressPercent > currentPercent)
            {
                listener.increaseProgress(progressPercent - currentPercent);
                currentPercent = progressPercent;
            }
        }
        mCharacterManager.setCurrentCharacter(character);

        mState = State_Running;

        if (character)
            Settings::saves().mCharacter.set(Files::pathToUnicodeString(character->getPath().filename()));
        mLastSavegame = filepath;

        OFBase::Environment::get().getWorld()->saveLoaded(reader);
        actorIdConverter.apply();
        OFBase::Environment::get().getWorld()->setupPlayer();
        OFBase::Environment::get().getWorld()->renderPlayer();
        OFBase::Environment::get().getWindowManager()->updatePlayer();
        OFBase::Environment::get().getMechanicsManager()->playerLoaded();
        OFBase::Environment::get().getWorld()->toggleVanityMode(false);

        if (firstPersonCam != OFBase::Environment::get().getWorld()->isFirstPerson())
            OFBase::Environment::get().getWorld()->togglePOV();

        OFWorld::ConstPtr ptr = OFMechanics::getPlayer();

        if (ptr.isInCell())
        {
            const ESM::RefId cellId = ptr.getCell()->getCell()->getId();

            // Use detectWorldSpaceChange=false, otherwise some of the data we just loaded would be cleared again
            OFBase::Environment::get().getWorld()->changeToCell(cellId, ptr.getRefData().getPosition(), false, false);
        }
        else
        {
            // Cell no longer exists (i.e. changed game files), choose a default cell
            Log(Debug::Warning) << "Player character's cell no longer exists, changing to the default cell";
            ESM::ExteriorCellLocation cellIndex(0, 0, ESM::Cell::sDefaultWorldspaceId);
            OFWorld::CellStore& cell = OFBase::Environment::get().getWorldModel()->getExterior(cellIndex);
            const osg::Vec2f posFromIndex = ESM::indexToPosition(cellIndex, false);
            ESM::Position pos;
            pos.pos[0] = posFromIndex.x();
            pos.pos[1] = posFromIndex.y();
            pos.pos[2] = 0; // should be adjusted automatically (adjustPlayerPos=true)
            pos.rot[0] = 0;
            pos.rot[1] = 0;
            pos.rot[2] = 0;
            OFBase::Environment::get().getWorld()->changeToCell(cell.getCell()->getId(), pos, true, false);
        }

        // Since we passed "changeEvent=false" to changeCell, we shouldn't have triggered the cell change flag.
        // But make sure the flag is cleared anyway in case it was set from an earlier game.
        OFBase::Environment::get().getWorldScene()->markCellAsUnchanged();

        // (the summoned creatures of Morrowind that an older save lists in actorIdConverter.mGraveyard are gone with
        // its magic)
        OFBase::Environment::get().getLuaManager()->gameLoaded();
    }
    catch (const SaveVersionTooNewError& e)
    {
        std::string error = "#{OFEngine:LoadingRequiresNewVersionError}";
        printSavegameFormatError(e.what(), error);
    }
    catch (const FalloutScript::SaveTooNewError& e)
    {
        std::string error = "#{OFEngine:LoadingRequiresNewVersionError}";
        printSavegameFormatError(e.what(), error);
    }
    catch (const SaveVersionTooOldError& e)
    {
        const char* release;
        // Report the last version still capable of reading this save
        if (e.getFormatVersion() < ESM::OpenMW0_49MinSaveGameFormatVersion)
            release = "OpenMW 0.48.0";
        else
        {
            // Insert additional else if statements above to cover future releases
            static_assert(ESM::MinSupportedSaveGameFormatVersion <= ESM::OpenMW0_49MinSaveGameFormatVersion);
            release = "OpenMW 0.52.0";
        }
        auto l10n = OFBase::Environment::get().getL10nManager()->getContext("OFEngine");
        std::string error = l10n->formatMessage("LoadingRequiresOldVersionError", { "version" }, { release });
        printSavegameFormatError(e.what(), error);
    }
    catch (const std::exception& e)
    {
        std::string error = "#{OFEngine:LoadingFailed}: " + std::string(e.what());
        printSavegameFormatError(e.what(), error);
    }
}

void OFState::StateManager::printSavegameFormatError(
    const std::string& exceptionText, const std::string& messageBoxText)
{
    Log(Debug::Error) << "Failed to load saved game: " << exceptionText;

    cleanup(true);

    OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);

    std::vector<std::string> buttons;
    buttons.emplace_back("#{Interface:OK}");

    OFBase::Environment::get().getWindowManager()->interactiveMessageBox(messageBoxText, buttons);
}

void OFState::StateManager::quickLoad()
{
    Character* currentCharacter = getCurrentCharacter();
    if (currentCharacter == nullptr || currentCharacter->begin() == currentCharacter->end())
    {
        // (the games tell it on the HUD line while a game runs)
        if (mState == State_Running && OFBase::Environment::get().getFalloutScripts() != nullptr)
            OFBase::Environment::get().getFalloutScripts()->quickNote(false, true);
        return;
    }
    // The note is said when the load is done: a load clears the notes of the HUD (see update)
    mQuickLoadNote = true;
    // use requestLoad, otherwise we can crash by loading during the wrong part of the frame
    requestLoad(currentCharacter, currentCharacter->begin()->mPath);
}

void OFState::StateManager::deleteGame(const OFState::Character* character, const OFState::Slot* slot)
{
    const std::filesystem::path savePath = slot->mPath;
    mCharacterManager.deleteSlot(slot, character);
    if (mLastSavegame == savePath)
    {
        if (character != nullptr)
            mLastSavegame = character->begin()->mPath;
        else
            mLastSavegame.clear();
    }
}

OFState::Character* OFState::StateManager::getCurrentCharacter()
{
    return mCharacterManager.getCurrentCharacter();
}

OFState::StateManager::CharacterIterator OFState::StateManager::characterBegin()
{
    return mCharacterManager.begin();
}

OFState::StateManager::CharacterIterator OFState::StateManager::characterEnd()
{
    return mCharacterManager.end();
}

void OFState::StateManager::update(float duration)
{
    mTimePlayed += duration;

    // Note: It would be nicer to trigger this from InputManager, i.e. the very beginning of the frame update.
    if (mAskLoadRecent)
    {
        int iButton = OFBase::Environment::get().getWindowManager()->readPressedButton();
        OFState::Character* curCharacter = getCurrentCharacter();
        if (iButton == 0 && curCharacter)
        {
            mAskLoadRecent = false;
            // Load last saved game for current character
            // loadGame resets the game state along with mLastSavegame so we want to preserve it
            const std::filesystem::path filePath = std::move(mLastSavegame);
            loadGame(curCharacter, filePath);
        }
        else if (iButton == 1)
        {
            mAskLoadRecent = false;
            OFBase::Environment::get().getWindowManager()->pushGuiMode(OFGui::GM_MainMenu);
        }
    }

    if (mNewGameRequest.has_value())
    {
        OFBase::Environment::get().getWindowManager()->removeGuiMode(OFGui::GM_MainMenu);
        newGame(mNewGameRequest->mBypass);
        mNewGameRequest = std::nullopt;
    }

    if (mLoadRequest)
    {
        OFBase::Environment::get().getWindowManager()->removeGuiMode(OFGui::GM_MainMenu);
        // The request is taken before the load runs: a refused load tells the menu scripts at once, and a handler
        // that asks for the same save again with another answer must find the slot free for its own request.
        const LoadRequest request = std::move(*mLoadRequest);
        mLoadRequest = std::nullopt;
        const Character* character = request.mCharacter;
        // The character may have been deleted after the request was made
        const bool validCharacter = std::ranges::find_if(mCharacterManager, [=](const Character& c) {
            return &c == character;
        }) != mCharacterManager.end();
        if (!validCharacter)
            character = getCurrentCharacter();
        loadGame(character, request.mPath, request.mAnswer);
        // (the load that the quick load key asked for says its note now, after the load cleared the notes)
        if (mQuickLoadNote && mState == State_Running && OFBase::Environment::get().getFalloutScripts() != nullptr)
            OFBase::Environment::get().getFalloutScripts()->quickNote(false, false);
        mQuickLoadNote = false;
    }
}

bool OFState::StateManager::confirmLoading(
    const std::vector<std::string_view>& missingFiles, ESM::MissingContentAnswer answer)
{
    std::ostringstream stream;
    for (auto& contentFile : missingFiles)
    {
        Log(Debug::Warning) << "Warning: Saved game dependency " << contentFile << " is missing.";
        stream << contentFile << "\n";
    }

    auto fullList = stream.str();
    if (!fullList.empty())
        fullList.pop_back();

    // The lines below are read by callers that run the engine unattended (docs/openfallout/shell-interface.md)
    switch (ESM::decideMissingContent(answer, mHeadless, true))
    {
        case ESM::MissingContentAction::Load:
            if (answer == ESM::MissingContentAnswer::Ask)
            {
                // The question is a message box that waits for a click: in a headless run nobody clicks and the load
                // would wait for ever
                Log(Debug::Info) << "Headless: " << missingFiles.size()
                                 << " content file(s) of the save are not loaded, loading it anyway";
            }
            else
                Log(Debug::Info) << "Load: " << missingFiles.size()
                                 << " content file(s) of the save are not loaded, loading it anyway (missing-content "
                                 << ESM::missingContentAnswerName(answer) << ")";
            return true;
        case ESM::MissingContentAction::Refuse:
            Log(Debug::Info) << "Load: " << missingFiles.size()
                             << " content file(s) of the save are not loaded, not loading it (missing-content "
                             << ESM::missingContentAnswerName(answer) << ")";
            mLastLoadRefused = true;
            return false;
        case ESM::MissingContentAction::AskPlayer:
            Log(Debug::Info) << "Load: " << missingFiles.size()
                             << " content file(s) of the save are not loaded, asking the player";
            break;
    }

    constexpr size_t missingPluginsDisplayLimit = 12;

    std::vector<std::string> buttons;
    buttons.emplace_back("#{Interface:Yes}");
    buttons.emplace_back("#{Interface:Copy}");
    buttons.emplace_back("#{Interface:No}");
    std::string message = "#{OFEngine:MissingContentFilesConfirmation}";

    auto l10n = OFBase::Environment::get().getL10nManager()->getContext("OFEngine");
    message += l10n->formatMessage("MissingContentFilesList", { "files" }, { static_cast<int>(missingFiles.size()) });
    auto cappedSize = std::min(missingFiles.size(), missingPluginsDisplayLimit);
    if (cappedSize == missingFiles.size())
    {
        message += fullList;
    }
    else
    {
        for (size_t i = 0; i < cappedSize - 1; ++i)
        {
            message += missingFiles[i];
            message += "\n";
        }

        message += "...";
    }

    message
        += l10n->formatMessage("MissingContentFilesListCopy", { "files" }, { static_cast<int>(missingFiles.size()) });

    int selectedButton = -1;
    while (true)
    {
        auto windowManager = OFBase::Environment::get().getWindowManager();
        windowManager->interactiveMessageBox(message, buttons, true, selectedButton);
        selectedButton = windowManager->readPressedButton();
        if (selectedButton == 0)
            break;

        if (selectedButton == 1)
        {
            SDL_SetClipboardText(fullList.c_str());
            continue;
        }

        return false;
    }

    return true;
}

void OFState::StateManager::writeScreenshot(std::vector<char>& imageData) const
{
    int screenshotW = 259 * 2, screenshotH = 133 * 2; // *2 to get some nice antialiasing

    osg::ref_ptr<osg::Image> screenshot(new osg::Image);

    OFBase::Environment::get().getWorld()->screenshot(screenshot.get(), screenshotW, screenshotH);

    osgDB::ReaderWriter* readerwriter = osgDB::Registry::instance()->getReaderWriterForExtension("jpg");
    if (!readerwriter)
    {
        Log(Debug::Error) << "Error: Unable to write screenshot, can't find a jpg ReaderWriter";
        return;
    }

    std::ostringstream ostream;
    osgDB::ReaderWriter::WriteResult result = readerwriter->writeImage(*screenshot, ostream);
    if (!result.success())
    {
        Log(Debug::Error) << "Error: Unable to write screenshot: " << result.message() << " code " << result.status();
        return;
    }

    std::string data = ostream.str();
    imageData = std::vector<char>(data.begin(), data.end());
}
