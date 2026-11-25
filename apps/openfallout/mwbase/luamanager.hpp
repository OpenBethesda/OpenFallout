#ifndef GAME_MWBASE_LUAMANAGER_H
#define GAME_MWBASE_LUAMANAGER_H

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <SDL_events.h>
#include <osg/Quat>
#include <osg/Vec3f>

#include <components/esm3/refnum.hpp>
#include <components/sdlutil/events.hpp>

#include "../mwmechanics/attacktype.hpp"

namespace OFWorld
{
    class CellStore;
    class Ptr;
}

namespace Loading
{
    class Listener;
}

namespace ESM
{
    class ESMReader;
    class ESMWriter;
    class RefId;
    struct LuaScripts;
    struct DialInfo;
    struct Dialogue;
}

namespace LuaUtil
{
    namespace InputAction
    {
        class Registry;
    }
}

namespace osg
{
    class Vec3f;
}

namespace OFBase
{
    // \brief LuaManager is the central interface through which the engine invokes lua scripts.
    //
    // The native side invokes functions on this interface, which queues events to be handled by the
    // scripts in the lua thread. Synchronous calls are not possible.
    //
    // The main implementation is in apps/openfallout/mwlua/luamanagerimp.cpp.
    // Lua logic in general lives under apps/openfallout/mwlua and this interface is
    // the main way for the rest of the engine to interact with the logic there.
    class LuaManager
    {
    public:
        virtual ~LuaManager() = default;

        virtual void contentFilesLoaded() = 0;
        virtual void newGameStarted() = 0;
        virtual void gameLoaded() = 0;
        virtual void gameEnded() = 0;
        virtual void noGame() = 0;
        // A load did not happen because the answer given in advance refused a save whose content files are not all
        // loaded (menu scripts: onLoadRefused)
        virtual void loadRefused(
            std::string_view directory, std::string_view slotName, const std::vector<std::string_view>& missingContent)
            = 0;
        virtual void objectAddedToScene(const OFWorld::Ptr& ptr) = 0;
        virtual void objectRemovedFromScene(const OFWorld::Ptr& ptr) = 0;
        virtual void objectTeleported(const OFWorld::Ptr& ptr) = 0;
        virtual void itemConsumed(const OFWorld::Ptr& consumable, const OFWorld::Ptr& actor) = 0;
        virtual void objectDropped(const OFWorld::Ptr& object, const OFWorld::Ptr& actor, const osg::Vec3f& position,
            const osg::Quat& rotation)
            = 0;
        virtual void objectPlaced(const OFWorld::Ptr& object, const OFWorld::Ptr& actor, const osg::Vec3f& position,
            const osg::Quat& rotation)
            = 0;
        virtual void objectActivated(const OFWorld::Ptr& object, const OFWorld::Ptr& actor) = 0;
        /// The object does what it does when it is activated, with no script of Fallout in the way (the script of an
        /// object with an OnActivate block calls this with Activate)
        virtual void defaultActivation(const OFWorld::Ptr& object, const OFWorld::Ptr& actor) = 0;
        virtual void useItem(const OFWorld::Ptr& object, const OFWorld::Ptr& actor, bool force) = 0;
        virtual void exteriorCreated(OFWorld::CellStore& cell) = 0;
        virtual void onDialogueResponse(
            const OFWorld::Ptr& actor, const ESM::DialInfo& info, const ESM::Dialogue& record)
            = 0;
        virtual void questUpdated(const ESM::RefId& questId, int stage) = 0;
        // `arg` is either forwarded from OFGui::pushGuiMode or empty
        virtual void uiModeChanged(const OFWorld::Ptr& arg) = 0;
        virtual void viewportResized(int width, int height) = 0;
        virtual void savePermanentStorage(const std::filesystem::path& userConfigPath) = 0;

        struct InputEvent
        {
            struct WheelChange
            {
                int x;
                int y;
            };

            enum
            {
                KeyPressed,
                KeyReleased,
                ControllerPressed,
                ControllerReleased,
                Action,
                TouchPressed,
                TouchReleased,
                TouchMoved,
                MouseButtonPressed,
                MouseButtonReleased,
                MouseWheel,
            } mType;
            std::variant<SDL_Keysym, int, SDLUtil::TouchEvent, WheelChange> mValue;
            // The console or a dialog of the engine's own windows had the input when it came, before the engine acted
            // on it (the key that closes either is theirs too). Only the keys and the buttons of the pad say it
            bool mTaken = false;
        };
        virtual void inputEvent(const InputEvent& event) = 0;

        struct ActorControls
        {
            bool mDisableAI = false;
            bool mChanged = false;

            bool mJump = false;
            bool mRun = false;
            bool mSneak = false;
            float mMovement = 0;
            float mSideMovement = 0;
            float mPitchChange = 0;
            float mYawChange = 0;
            OFMechanics::AttackType mUse = OFMechanics::AttackType::NoAttack;
        };

        virtual ActorControls* getActorControls(const OFWorld::Ptr&) const = 0;

        virtual void clear() = 0;
        virtual void setupPlayer(const OFWorld::Ptr&) = 0;

        // Saving
        size_t countSavedGameRecords() const { return 1; }
        virtual void write(ESM::ESMWriter& writer, Loading::Listener& progress) = 0;
        virtual void saveLocalScripts(const OFWorld::Ptr& ptr, ESM::LuaScripts& data) = 0;

        // Must be called before save, otherwise the world can be saved in an inconsistent state.
        virtual void applyDelayedActions() = 0;

        // Loading from a save
        virtual void readRecord(ESM::ESMReader& reader, uint32_t type) = 0;
        virtual void loadLocalScripts(const OFWorld::Ptr& ptr, const ESM::LuaScripts& data) = 0;
        /// The scripts of a saved game on an object that may be in the scene already (the scripts that the
        /// configuration started on it are replaced by the saved ones)
        virtual void restoreLocalScripts(const OFWorld::Ptr& ptr, const ESM::LuaScripts& data) = 0;

        // Should be called before loading. The map is used to fix refnums if the order of content files was changed.
        virtual void setContentFileMapping(const std::map<int, int>&) = 0;

        // Drops script cache and reloads all scripts. Calls `onSave` and `onLoad` for every script.
        virtual void reloadAllScripts() = 0;

        virtual void handleConsoleCommand(
            const std::string& consoleMode, const std::string& command, const OFWorld::Ptr& selectedPtr)
            = 0;

        virtual std::string formatResourceUsageStats() const = 0;
    };

}

#endif // GAME_MWBASE_LUAMANAGER_H
