#ifndef ENGINE_H
#define ENGINE_H

#include <filesystem>
#include <memory>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/missingcontent.hpp>
#include <components/files/collections.hpp>
#include <components/files/gameini.hpp>
#include <components/settings/settings.hpp>
#include <components/translation/translation.hpp>
#include <components/vfs/registerarchives.hpp>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

#include "mwbase/environment.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace SceneUtil
{
    class WorkQueue;
    class AsyncScreenCaptureOperation;
    class UnrefQueue;
}

namespace VFS
{
    class Manager;
}

namespace Compiler
{
    class Context;
}

namespace OFLua
{
    class LuaManager;
    class Worker;
}

namespace Stereo
{
    class Manager;
}

namespace Files
{
    struct ConfigurationManager;
}

namespace osgViewer
{
    class ScreenCaptureHandler;
}

namespace SceneUtil
{
    class SelectDepthFormatOperation;

    namespace Color
    {
        class SelectColorFormatOperation;
    }
}

namespace OFState
{
    class StateManager;
}

namespace OFGui
{
    class WindowManager;
}

namespace OFInput
{
    class InputManager;
}

namespace OFSound
{
    class SoundManager;
}

namespace OFWorld
{
    class World;
}

namespace OFScript
{
    class FalloutScripts;
}

namespace OFMechanics
{
    class MechanicsManager;
}

namespace L10n
{
    class Manager;
}

struct SDL_Window;

namespace OF
{
    /// \brief Main engine class, that brings together all the components of OpenMW
    class Engine
    {
        SDL_Window* mWindow;
        std::unique_ptr<VFS::Manager> mVFS;
        std::unique_ptr<Resource::ResourceSystem> mResourceSystem;
        std::shared_ptr<SceneUtil::WorkQueue> mWorkQueue;
        std::unique_ptr<SceneUtil::UnrefQueue> mUnrefQueue;
        std::unique_ptr<OFWorld::World> mWorld;
        std::unique_ptr<OFSound::SoundManager> mSoundManager;
        std::unique_ptr<OFScript::FalloutScripts> mFalloutScripts;
        std::unique_ptr<OFGui::WindowManager> mWindowManager;
        std::unique_ptr<OFMechanics::MechanicsManager> mMechanicsManager;
        std::unique_ptr<OFInput::InputManager> mInputManager;
        std::unique_ptr<OFState::StateManager> mStateManager;
        std::unique_ptr<OFLua::LuaManager> mLuaManager;
        std::unique_ptr<OFLua::Worker> mLuaWorker;
        std::unique_ptr<L10n::Manager> mL10nManager;
        OFBase::Environment mEnvironment;
        ToUTF8::FromType mEncoding;
        std::unique_ptr<ToUTF8::Utf8Encoder> mEncoder;
        Files::PathContainer mDataDirs;
        std::vector<std::string> mArchives;
        VFS::ArchiveOptions mArchiveOptions;
        std::filesystem::path mResDir;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> mScreenCaptureHandler;
        osg::ref_ptr<SceneUtil::AsyncScreenCaptureOperation> mScreenCaptureOperation;
        osg::ref_ptr<SceneUtil::SelectDepthFormatOperation> mSelectDepthFormatOperation;
        osg::ref_ptr<SceneUtil::Color::SelectColorFormatOperation> mSelectColorFormatOperation;
        std::string mCellName;
        std::vector<std::string> mContentFiles;
        Files::GameIni mGameIni;
        std::vector<std::string> mGroundcoverFiles;

        std::unique_ptr<Stereo::Manager> mStereoManager;

        bool mSkipMenu;
        bool mUseSound;
        std::string mFocusName;
        int mActivationDistanceOverride;
        std::filesystem::path mSaveGameFile;
        // Grab mouse?
        bool mGrab;
        bool mHeadless;
        // The answer, given in advance, to the box that asks the player whether to load a save whose content files are
        // not all loaded (--missing-content), and the exit status of the process
        ESM::MissingContentAnswer mMissingContentAnswer = ESM::MissingContentAnswer::Ask;
        int mExitStatus = 0;

        bool mExportFonts;
        unsigned int mRandomSeed;
        Debug::Level mMaxRecastLogLevel = Debug::Error;

        Files::Collections mFileCollections;
        Translation::Storage mTranslationDataStorage;
        bool mNewGame;

        Files::ConfigurationManager& mCfgMgr;
        int mGlMaxTextureImageUnits;

        // not implemented
        Engine(const Engine&);
        Engine& operator=(const Engine&);

        bool frame(unsigned frameNumber, float dt);

        /// Prepare engine for game play
        void prepareEngine();

        void createWindow();
        void setWindowIcon();

    public:
        Engine(Files::ConfigurationManager& configurationManager);
        virtual ~Engine();

        /// Set data dirs
        void setDataDirs(const Files::PathContainer& dataDirs);
        /// The settings of the ini files of the game, which the script rules read some of
        void setGameIni(Files::GameIni ini) { mGameIni = std::move(ini); }

        /// Add BSA archive. The archives go in the order of increasing priority.
        void addArchive(const std::string& archive);

        /// How the archives and the loose files of the data directories go together
        void setArchiveOptions(VFS::ArchiveOptions options);

        /// Set resource dir
        void setResourceDir(const std::filesystem::path& parResDir);

        /// Set start cell name
        void setCell(const std::string& cellName);

        /**
         * @brief addContentFile - Adds content file (ie. esm/esp, or omwgame/omwaddon) to the content files container.
         * @param file - filename (extension is required)
         */
        void addContentFile(const std::string& file);
        void addGroundcoverFile(const std::string& file);

        /// Disable or enable all sounds
        void setSoundUsage(bool soundUsage);

        /// Skip main menu and go directly into the game
        ///
        /// \param newGame Start a new game instead off dumping the player into the game
        /// (ignored if !skipMenu).
        void setSkipMenu(bool skipMenu, bool newGame);

        void setGrabMouse(bool grab) { mGrab = grab; }
        // The window is created hidden and stays so; the engine runs as if it were shown (see main.cpp)
        void setHeadless(bool headless) { mHeadless = headless; }
        void setMissingContentAnswer(ESM::MissingContentAnswer answer) { mMissingContentAnswer = answer; }

        /// What the process ends with: 0, or refusedLoadExitStatus when the save given with --load-savegame was not
        /// loaded because the caller refused it (--missing-content=refuse)
        int getExitStatus() const { return mExitStatus; }
        static constexpr int refusedLoadExitStatus = 4;

        /// Initialise and enter main loop.
        void go();

        /// Font encoding
        void setEncoding(const ToUTF8::FromType& encoding);

        /// Override the game setting specified activation distance.
        void setActivationDistanceOverride(int distance);

        void enableFontExport(bool exportFonts);

        /// Set the save game file to load after initialising the engine.
        void setSaveGameFile(const std::filesystem::path& savegame);

        void setRandomSeed(unsigned int seed);

        void setRecastMaxLogLevel(Debug::Level value) { mMaxRecastLogLevel = value; }
    };
}

#endif /* ENGINE_H */
