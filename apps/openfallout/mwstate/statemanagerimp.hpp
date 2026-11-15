#ifndef GAME_STATE_STATEMANAGER_H
#define GAME_STATE_STATEMANAGER_H

#include <filesystem>
#include <map>
#include <utility>

#include "../mwbase/statemanager.hpp"

#include <atomic>

#include "charactermanager.hpp"

namespace OFState
{
    class StateManager : public OFBase::StateManager
    {
    private:
        struct NewGameRequest
        {
            bool mBypass;
        };

        bool mQuitRequest;
        bool mAskLoadRecent;
        // (read by the scripts of Lua, which run on a thread of their own)
        std::atomic<bool> mPlayerDied{ false };
        std::optional<NewGameRequest> mNewGameRequest;
        struct LoadRequest
        {
            const Character* mCharacter;
            std::filesystem::path mPath;
            std::optional<ESM::MissingContentAnswer> mAnswer;
        };
        std::optional<LoadRequest> mLoadRequest;
        // The load that was asked for by the quick load key says its note when it is done (a load clears the notes)
        bool mQuickLoadNote = false;
        // A headless run has nobody to answer a question: a save that wants content files that are not loaded is loaded
        // unless the caller refused it
        bool mHeadless = false;
        // The answer to that question given for the whole run (--missing-content); a load can be given its own
        ESM::MissingContentAnswer mMissingContentAnswer = ESM::MissingContentAnswer::Ask;
        // The last load did not happen because the caller's answer refused it
        bool mLastLoadRefused = false;
        State mState;
        CharacterManager mCharacterManager;
        double mTimePlayed;
        std::filesystem::path mLastSavegame;

        void cleanup(bool force = false);

        void printSavegameFormatError(const std::string& exceptionText, const std::string& messageBoxText);

        // Whether the save that wants these content files is loaded; the caller's answer decides, or the player
        bool confirmLoading(const std::vector<std::string_view>& missingFiles, ESM::MissingContentAnswer answer);

        void writeScreenshot(std::vector<char>& imageData) const;

        std::map<int, int> buildContentFileIndexMap(const ESM::ESMReader& reader) const;

    public:
        StateManager(const std::filesystem::path& saves, const std::vector<std::string>& contentFiles);

        void setHeadless(bool headless) { mHeadless = headless; }
        void setMissingContentAnswer(ESM::MissingContentAnswer answer) { mMissingContentAnswer = answer; }
        /// The last load did not happen because the answer given in advance refused the save (no box was shown)
        bool lastLoadRefused() const { return mLastLoadRefused; }

        void requestQuit() override;

        bool hasQuitRequest() const override;

        void askLoadRecent() override;
        void loadRecent() override;
        bool hasRecentSave() override;
        void setPlayerDied(bool died) override { mPlayerDied = died; }
        bool playerDied() const override { return mPlayerDied; }

        void requestNewGame(bool bypass) override { mNewGameRequest = NewGameRequest{ .mBypass = bypass }; }

        void requestLoad(const Character* character, const std::filesystem::path& filepath,
            std::optional<ESM::MissingContentAnswer> answer = std::nullopt) override
        {
            mLoadRequest = LoadRequest{ character, filepath, answer };
        }

        State getState() const override;

        void newGame(bool bypass = false) override;
        ///< Start a new game.
        ///
        /// \param bypass Skip new game mechanics.

        void endGame() override;

        void resumeGame() override;

        void deleteGame(const OFState::Character* character, const OFState::Slot* slot) override;
        ///< Delete a saved game slot from this character. If all save slots are deleted, the character will be deleted
        ///< too.

        void saveGame(std::string_view description, const Slot* slot = nullptr) override;
        ///< Write a saved game to \a slot or create a new slot if \a slot == 0.
        ///
        /// \note Slot must belong to the current character.

        /// Saves a file, using supplied filename, overwritting if needed
        /** This is mostly used for quicksaving and autosaving, for they use the same name over and over again
            \param name Name of save, defaults to "Quicksave"**/
        void quickSave(std::string name = "Quicksave") override;

        /// Loads the last saved file
        /** Used for quickload **/
        void quickLoad() override;

        void loadGame(const std::filesystem::path& filepath) override;
        ///< Load a saved game directly from the given file path. This will search the CharacterManager
        /// for a Character containing this save file, and set this Character current if one was found.
        /// Otherwise, a new Character will be created.

        void loadGame(const Character* character, const std::filesystem::path& filepath,
            std::optional<ESM::MissingContentAnswer> answer = std::nullopt) override;
        ///< Load a saved game file belonging to the given character.

        Character* getCurrentCharacter() override;
        ///< @note May return null.

        CharacterIterator characterBegin() override;
        ///< Any call to SaveGame and getCurrentCharacter can invalidate the returned
        /// iterator.

        CharacterIterator characterEnd() override;

        void update(float duration);
    };
}

#endif
