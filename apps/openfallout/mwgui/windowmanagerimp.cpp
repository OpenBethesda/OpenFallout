#include "windowmanagerimp.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

#include <osgViewer/Viewer>

#include <MyGUI_ClipboardManager.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_LayerManager.h>
#include <MyGUI_PointerManager.h>
#include <MyGUI_UString.h>

// For BT_NO_PROFILE
#include <LinearMath/btQuickprof.h>

#include <SDL_clipboard.h>
#include <SDL_keyboard.h>

#include <components/debug/debuglog.hpp>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>

#include <components/fontloader/fontloader.hpp>

#include <components/resource/imagemanager.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/workqueue.hpp>

#include <components/sdlutil/desktopconventions.hpp>

#include <components/translation/translation.hpp>

#include <components/myguiplatform/additivelayer.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/myguiplatform/myguirendermanager.hpp>
#include <components/myguiplatform/scalinglayer.hpp>

#include <components/vfs/manager.hpp>

#include <components/widgets/imagebutton.hpp>
#include <components/widgets/tags.hpp>
#include <components/widgets/widgets.hpp>

#include <components/misc/frameratelimiter.hpp>

#include <components/l10n/manager.hpp>

#include <components/lua_ui/util.hpp>
#include <components/lua_ui/widget.hpp>

#include <components/settings/values.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/statemanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwrender/vismask.hpp"

#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/globals.hpp"
#include "../mwworld/player.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwrender/postprocessor.hpp"

#include "../mwscript/falloutscripts.hpp"

#include "backgroundimage.hpp"
#include "confirmationdialog.hpp"
#include "console.hpp"
#include "controllerbuttonsoverlay.hpp"
#include "controllers.hpp"
#include "cursor.hpp"
#include "debugwindow.hpp"
#include "exposedwindow.hpp"
#include "falloutairmeter.hpp"
#include "hud.hpp"
#include "keyboardnavigation.hpp"
#include "loadingscreen.hpp"
#include "mainmenu.hpp"
#include "menuscenes.hpp"
#include "postprocessorhud.hpp"
#include "resourceskin.hpp"
#include "screenfader.hpp"
#include "settingswindow.hpp"
#include "videowidget.hpp"
#include "widgets.hpp"

namespace OFGui
{
    WindowManager::WindowManager(SDL_Window* window, osgViewer::Viewer* viewer, osg::Group* guiRoot,
        Resource::ResourceSystem* resourceSystem, const std::shared_ptr<SceneUtil::WorkQueue>& workQueue,
        const std::filesystem::path& logpath, Translation::Storage& translationDataStorage, ToUTF8::FromType encoding,
        bool exportFonts, const std::string& versionDescription, Files::ConfigurationManager& cfgMgr)
        : mOldUpdateMask(0)
        , mOldCullMask(0)
        , mStore(nullptr)
        , mResourceSystem(resourceSystem)
        , mWorkQueue(workQueue)
        , mViewer(viewer)
        , mCurrentModals()
        , mHud(nullptr)
        , mConsole(nullptr)
        , mSettingsWindow(nullptr)
        , mConfirmationDialog(nullptr)
        , mLoadingScreen(nullptr)
        , mVideoBackground(nullptr)
        , mVideoWidget(nullptr)
        , mWerewolfFader(nullptr)
        , mBlindnessFader(nullptr)
        , mHitFader(nullptr)
        , mScreenFader(nullptr)
        , mDebugWindow(nullptr)
        , mPostProcessorHud(nullptr)
        , mControllerButtonsOverlay(nullptr)
        , mTranslationDataStorage(translationDataStorage)
        , mInputBlocker(nullptr)
        , mHudEnabled(true)
        , mCursorVisible(true)
        , mCursorActive(true)
        , mCursorInteractionEnabled(false)
        , mPlayerBounty(-1)
        , mGuiModes()
        , mVersionDescription(versionDescription)
        , mWindowVisible(true)
        , mCfgMgr(cfgMgr)
        , mSdlWindow(window)
        , mWindowSize{ Settings::video().mResolutionX, Settings::video().mResolutionY }
    {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        int dw, dh;
        SDL_GL_GetDrawableSize(window, &dw, &dh);

        mScalingFactor = SDLUtil::interfaceScale(Settings::gui().mScalingFactor, w, dw, SDLUtil::thisDesktopPlatform());
        constexpr VFS::Path::NormalizedView resourcePath("mygui");
        mGuiPlatform = std::make_unique<MyGUIPlatform::Platform>(viewer, guiRoot, resourceSystem->getImageManager(),
            resourceSystem->getVFS(), mScalingFactor, resourcePath, logpath / "MyGUI.log");

        mGui = std::make_unique<MyGUI::Gui>();
        mGui->initialise({});

        createTextures();

        MyGUI::LanguageManager::getInstance().eventRequestTag = MyGUI::newDelegate(this, &WindowManager::onRetrieveTag);

        // Load fonts
        mFontLoader
            = std::make_unique<Gui::FontLoader>(encoding, resourceSystem->getVFS(), mScalingFactor, exportFonts);

        // Register own widgets with MyGUI
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWSkill>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWAttribute>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWSpell>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWEffectList>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWSpellEffect>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Widgets::MWDynamicStat>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Window>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<VideoWidget>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<BackgroundImage>("Widget");
        MyGUI::FactoryManager::getInstance().registerFactory<MyGUIPlatform::AdditiveLayer>("Layer");
        MyGUI::FactoryManager::getInstance().registerFactory<MyGUIPlatform::ScalingLayer>("Layer");
        PostProcessorHud::registerMyGUIComponents();
        Gui::registerAllWidgets();
        LuaUi::registerAllWidgets();

        MyGUI::FactoryManager::getInstance().registerFactory<OFGui::Controllers::ControllerFollowMouse>("Controller");

        MyGUI::FactoryManager::getInstance().registerFactory<ResourceImageSetPointerFix>(
            "Resource", "ResourceImageSetPointer");
        MyGUI::FactoryManager::getInstance().registerFactory<AutoSizedResourceSkin>(
            "Resource", "AutoSizedResourceSkin");
        MyGUI::ResourceManager::getInstance().load("core.xml");

        const bool keyboardNav = Settings::gui().mKeyboardNavigation;
        mKeyboardNavigation = std::make_unique<KeyboardNavigation>();
        mKeyboardNavigation->setEnabled(keyboardNav);
        Gui::ImageButton::setDefaultNeedKeyFocus(keyboardNav);

        auto loadingScreen = std::make_unique<LoadingScreen>(mResourceSystem, mViewer);
        mLoadingScreen = loadingScreen.get();
        mWindows.push_back(std::move(loadingScreen));

        // set up the hardware cursor manager
        mCursorManager = std::make_unique<SDLUtil::SDLCursorManager>();

        MyGUI::PointerManager::getInstance().eventChangeMousePointer
            += MyGUI::newDelegate(this, &WindowManager::onCursorChange);

        MyGUI::InputManager::getInstance().eventChangeKeyFocus
            += MyGUI::newDelegate(this, &WindowManager::onKeyFocusChanged);

        // Create all cursors in advance
        createCursors();
        onCursorChange(MyGUI::PointerManager::getInstance().getDefaultPointer());
        mCursorManager->setEnabled(true);

        // hide mygui's pointer
        MyGUI::PointerManager::getInstance().setVisible(false);

        mVideoBackground = MyGUI::Gui::getInstance().createWidgetReal<MyGUI::ImageBox>(
            "ImageBox", 0, 0, 1, 1, MyGUI::Align::Default, "Video");
        mVideoBackground->setImageTexture("black");
        mVideoBackground->setVisible(false);
        mVideoBackground->setNeedMouseFocus(true);
        mVideoBackground->setNeedKeyFocus(true);

        mVideoWidget = mVideoBackground->createWidgetReal<VideoWidget>("ImageBox", 0, 0, 1, 1, MyGUI::Align::Default);
        mVideoWidget->setNeedMouseFocus(true);
        mVideoWidget->setNeedKeyFocus(true);
        mVideoWidget->setVFS(resourceSystem->getVFS());

        // Removes default MyGUI system clipboard implementation, which supports windows only
        MyGUI::ClipboardManager::getInstance().eventClipboardChanged.clear();
        MyGUI::ClipboardManager::getInstance().eventClipboardRequested.clear();

        MyGUI::ClipboardManager::getInstance().eventClipboardChanged
            += MyGUI::newDelegate(this, &WindowManager::onClipboardChanged);
        MyGUI::ClipboardManager::getInstance().eventClipboardRequested
            += MyGUI::newDelegate(this, &WindowManager::onClipboardRequested);

        mVideoWrapper = std::make_unique<SDLUtil::VideoWrapper>(window, viewer);
        mVideoWrapper->setGammaContrast(Settings::video().mGamma, Settings::video().mContrast);

        mGuiPlatform->getRenderManagerPtr()->enableShaders(mResourceSystem->getSceneManager()->getShaderManager());
    }

    void WindowManager::initUI()
    {
        // Get size info from the Gui object
        int w = MyGUI::RenderManager::getInstance().getViewSize().width;
        int h = MyGUI::RenderManager::getInstance().getViewSize().height;

        mTextColours.loadColours();

        auto menu = std::make_unique<MainMenu>(w, h, mResourceSystem->getVFS(), mVersionDescription);
        mGuiModeStates[GM_MainMenu] = GuiModeState(menu.get());
        mWindows.push_back(std::move(menu));

        mMenuScenes = std::make_unique<MenuScenes>(
            mViewer->getSceneData()->asGroup(), mResourceSystem, mGuiPlatform->getRenderManagerPtr());
        // The windows of Morrowind that sat in Interface mode (the map, the inventory, the spells and the stats) are
        // gone: the screens of Fallout in this mode are all drawn by Lua (I.UI.registerWindow)
        mGuiModeStates[GM_Inventory] = GuiModeState();
        mGuiModeStates[GM_None] = GuiModeState();

        auto console = std::make_unique<Console>(w, h, mCfgMgr);
        mConsole = console.get();
        mWindows.push_back(std::move(console));
        trackWindow(mConsole, makeConsoleWindowSettingValues());

        mMessageBoxManager = std::make_unique<MessageBoxManager>(
            mStore->get<ESM::GameSetting>().find("fMessageTimePerChar")->mValue.getFloat());

        // A conversation of Fallout has no C++ window: falloutdialogue.lua draws DialogMenu (I.UI.registerWindow
        // takes a window that only Lua has)
        mGuiModeStates[GM_DialogMenu] = GuiModeState();

        auto hud = std::make_unique<HUD>();
        mHud = hud.get();
        mWindows.push_back(std::move(hud));

        mToolTips = std::make_unique<ToolTips>();

        auto settingsWindow = std::make_unique<SettingsWindow>(mCfgMgr);
        mSettingsWindow = settingsWindow.get();
        mWindows.push_back(std::move(settingsWindow));
        trackWindow(mSettingsWindow, makeSettingsWindowSettingValues());

        auto confirmationDialog = std::make_unique<ConfirmationDialog>();
        mConfirmationDialog = confirmationDialog.get();
        mWindows.push_back(std::move(confirmationDialog));

        std::string werewolfFaderTex = "textures\\werewolfoverlay.dds";
        if (mResourceSystem->getVFS()->exists(werewolfFaderTex))
        {
            auto werewolfFader = std::make_unique<ScreenFader>(werewolfFaderTex);
            mWerewolfFader = werewolfFader.get();
            mWindows.push_back(std::move(werewolfFader));
        }
        auto blindnessFader = std::make_unique<ScreenFader>("black");
        mBlindnessFader = blindnessFader.get();
        mWindows.push_back(std::move(blindnessFader));

        // fall back to player_hit_01.dds if bm_player_hit_01.dds is not available
        std::string hitFaderTexture = "textures\\bm_player_hit_01.dds";
        const std::string hitFaderLayout = "openmw_screen_fader_hit.layout";
        MyGUI::FloatCoord hitFaderCoord(0, 0, 1, 1);
        if (!mResourceSystem->getVFS()->exists(hitFaderTexture))
        {
            hitFaderTexture = "textures\\player_hit_01.dds";
            hitFaderCoord = MyGUI::FloatCoord(0.2f, 0.25f, 0.6f, 0.5f);
        }
        auto hitFader = std::make_unique<ScreenFader>(hitFaderTexture, hitFaderLayout, hitFaderCoord);
        mHitFader = hitFader.get();
        mWindows.push_back(std::move(hitFader));

        auto screenFader = std::make_unique<ScreenFader>("black");
        mScreenFader = screenFader.get();
        mWindows.push_back(std::move(screenFader));

        auto debugWindow = std::make_unique<DebugWindow>();
        mDebugWindow = debugWindow.get();
        mWindows.push_back(std::move(debugWindow));
        trackWindow(mDebugWindow, makeDebugWindowSettingValues());

        auto postProcessorHud = std::make_unique<PostProcessorHud>();
        mPostProcessorHud = postProcessorHud.get();
        mWindows.push_back(std::move(postProcessorHud));
        trackWindow(mPostProcessorHud, makePostprocessorWindowSettingValues());

        auto controllerButtonsOverlay = std::make_unique<ControllerButtonsOverlay>();
        mControllerButtonsOverlay = controllerButtonsOverlay.get();
        mWindows.push_back(std::move(controllerButtonsOverlay));

        mControllerTooltipEnabled = Settings::gui().mControllerTooltips;

        mInputBlocker = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
            {}, 0, 0, w, h, MyGUI::Align::Stretch, "InputBlocker");

        mHud->setVisible(true);

        // Set up visibility
        updateVisible();

        for (auto& window : mWindows)
        {
            std::string_view id = window->getWindowIdForLua();
            if (!id.empty())
                mLuaIdToWindow.emplace(id, window.get());
        }
    }

    WindowManager::~WindowManager()
    {
        try
        {
            LuaUi::clearGameInterface();
            LuaUi::clearMenuInterface();

            mAirMeter.reset();

            MyGUI::LanguageManager::getInstance().eventRequestTag.clear();
            MyGUI::PointerManager::getInstance().eventChangeMousePointer.clear();
            MyGUI::InputManager::getInstance().eventChangeKeyFocus.clear();
            MyGUI::ClipboardManager::getInstance().eventClipboardChanged.clear();
            MyGUI::ClipboardManager::getInstance().eventClipboardRequested.clear();

            mWindows.clear();
            mMessageBoxManager.reset();
            mToolTips.reset();
            mMenuScenes.reset();

            mKeyboardNavigation.reset();

            mFontLoader.reset();

            mGui->shutdown();

            mGuiPlatform->shutdown();
        }
        catch (const MyGUI::Exception& e)
        {
            Log(Debug::Error) << "Error in the destructor: " << e.what();
        }
    }

    void WindowManager::setStore(const OFWorld::ESMStore& store)
    {
        mStore = &store;
        mLoadingScreen->setStore(store);
    }

    void WindowManager::enableScene(bool enable)
    {
        unsigned int disablemask = OFRender::Mask_GUI | OFRender::Mask_PreCompile;
        if (!enable && getCullMask() != disablemask)
        {
            mOldUpdateMask = mViewer->getUpdateVisitor()->getTraversalMask();
            mOldCullMask = getCullMask();
            mViewer->getUpdateVisitor()->setTraversalMask(disablemask);
            setCullMask(disablemask);
        }
        else if (enable && getCullMask() == disablemask)
        {
            mViewer->getUpdateVisitor()->setTraversalMask(mOldUpdateMask);
            setCullMask(mOldCullMask);
        }
    }

    void WindowManager::updateConsoleObjectPtr(const OFWorld::Ptr& currentPtr, const OFWorld::Ptr& newPtr)
    {
        mConsole->updateSelectedObjectPtr(currentPtr, newPtr);
    }

    void WindowManager::updateVisible()
    {
        bool loading = (getMode() == GM_Loading || getMode() == GM_LoadingWallpaper);

        bool mainmenucover = containsMode(GM_MainMenu)
            && OFBase::Environment::get().getStateManager()->getState() == OFBase::StateManager::State_NoGame;

        enableScene(!loading && !mainmenucover);

        if (!mHud)
            return; // UI not created yet

        mHud->setVisible(mHudEnabled && !loading);
        mToolTips->setVisible(mHudEnabled && !loading);

        bool gameMode = !isGuiMode();

        OFBase::Environment::get().getInputManager()->changeInputMode(!gameMode);

        mInputBlocker->setVisible(gameMode && !mCursorInteractionEnabled);

        if (loading)
            setCursorVisible(mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox());
        else
            setCursorVisible(!gameMode);

        updateMouseEmulationCursor();

        if (gameMode)
            setKeyFocusWidget(nullptr);

        if (mGuiModes.empty())
        {
            if (Settings::gui().mControllerMenus && mControllerButtonsOverlay)
                mControllerButtonsOverlay->setVisible(false);
            return;
        }

        if (getMode() != GM_Inventory)
            mHud->setDrowningBarVisible(false);

        updateControllerButtonsOverlay();
    }

    void WindowManager::setDrowningTimeLeft(float time, float maxTime)
    {
        mHud->setDrowningTimeLeft(time, maxTime);
    }

    void WindowManager::exitCurrentGuiMode()
    {
        if (mGuiModes.empty())
            return;

        // A conversation of Fallout that offers no goodbye is not left: the pause menu opens over it instead (the
        // doctor's test of Goodsprings, which a player who walked out of it half way could not go on with)
        if (getMode() == GM_DialogMenu)
        {
            if (const OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
                scripts != nullptr && scripts->conversationHeld())
            {
                if (!MyGUI::InputManager::getInstance().isModalAny())
                    pushGuiMode(GM_MainMenu);
                return;
            }
        }

        GuiModeState& state = mGuiModeStates[mGuiModes.back()];
        for (const auto& window : state.mWindows)
        {
            if (!window->exit())
            {
                // unable to exit window, but give access to main menu
                if (!MyGUI::InputManager::getInstance().isModalAny() && getMode() != GM_MainMenu)
                    pushGuiMode(GM_MainMenu);
                return;
            }
        }

        popGuiMode();
    }

    void WindowManager::interactiveMessageBox(
        std::string_view message, const std::vector<std::string>& buttons, bool block, int defaultFocus)
    {
        mMessageBoxManager->createInteractiveMessageBox(message, buttons, block, defaultFocus);
        updateVisible();

        if (block)
        {
            Misc::FrameRateLimiter frameRateLimiter
                = Misc::makeFrameRateLimiter(OFBase::Environment::get().getFrameRateLimit());
            while (mMessageBoxManager->readPressedButton(false) == -1
                && !OFBase::Environment::get().getStateManager()->hasQuitRequest())
            {
                const float dt
                    = std::chrono::duration_cast<std::chrono::duration<float>>(frameRateLimiter.getLastFrameDuration())
                          .count();

                mKeyboardNavigation->onFrame();
                mMessageBoxManager->onFrame(dt);
                OFBase::Environment::get().getInputManager()->update(dt, true, false);

                if (!mWindowVisible)
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                else
                {
                    mViewer->eventTraversal();
                    mViewer->updateTraversal();
                    mViewer->renderingTraversals();
                }
                // at the time this function is called we are in the middle of a frame,
                // so out of order calls are necessary to get a correct frameNumber for the next frame.
                // refer to the advance() and frame() order in Engine::go()
                mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());

                frameRateLimiter.limit();
            }

            mMessageBoxManager->resetInteractiveMessageBox();
        }
    }

    void WindowManager::messageBox(std::string_view message, enum OFGui::ShowInDialogueMode showInDialogueMode)
    {
        // During a conversation a message goes where the games put it: the message queue of the head-up display
        // (ShowInDialogueMode)
        OFScript::FalloutScripts* scripts = OFBase::Environment::get().getFalloutScripts();
        if (getMode() == GM_DialogMenu && scripts != nullptr && showInDialogueMode != OFGui::ShowInDialogueMode_Never)
        {
            MyGUI::UString text = MyGUI::LanguageManager::getInstance().replaceTags(MyGUI::UString(message));
            scripts->hudMessages().push(text.asUTF8());
        }
        else if (showInDialogueMode != OFGui::ShowInDialogueMode_Only)
        {
            mMessageBoxManager->createMessageBox(message);
        }
    }

    void WindowManager::scheduleMessageBox(std::string message, enum OFGui::ShowInDialogueMode showInDialogueMode)
    {
        mScheduledMessageBoxes.lock()->emplace_back(std::move(message), showInDialogueMode);
    }

    void WindowManager::staticMessageBox(std::string_view message)
    {
        mMessageBoxManager->createMessageBox(message, true);
    }

    void WindowManager::removeStaticMessageBox()
    {
        mMessageBoxManager->removeStaticMessageBox();
    }

    int WindowManager::readPressedButton()
    {
        return mMessageBoxManager->readPressedButton();
    }

    bool WindowManager::pressInteractiveMessageBoxButton(std::size_t button)
    {
        return mMessageBoxManager->pressInteractiveMessageBoxButton(button);
    }

    std::string_view WindowManager::getGameSettingString(std::string_view id, std::string_view defaultValue)
    {
        const ESM::GameSetting* setting = mStore->get<ESM::GameSetting>().search(id);

        if (setting && setting->mValue.getType() == ESM::VT_String)
            return setting->mValue.getString();

        return defaultValue;
    }

    WindowBase* WindowManager::getActiveControllerWindow()
    {
        if (!mCurrentModals.empty())
            return mCurrentModals.back();

        if (mWindows.empty())
            return nullptr;

        if (isSettingsWindowVisible())
            return mSettingsWindow;

        if (!mGuiModes.empty())
        {
            GuiMode mode = mGuiModes.back();
            GuiModeState& state = mGuiModeStates[mode];
            if (state.mWindows.empty())
                return nullptr;

            size_t activeIndex = std::clamp<size_t>(mActiveControllerWindows[mode], 0, state.mWindows.size() - 1);

            // If the active window is no longer visible, find the next visible window.
            if (!state.mWindows[activeIndex]->isVisible())
                cycleActiveControllerWindow(true);

            return state.mWindows[activeIndex];
        }

        return nullptr;
    }

    void WindowManager::cycleActiveControllerWindow(bool next)
    {
        if (!Settings::gui().mControllerMenus || mGuiModes.empty())
            return;

        GuiMode mode = mGuiModes.back();
        size_t winCount = mGuiModeStates[mode].mWindows.size();

        size_t activeIndex = 0;
        if (winCount > 1)
        {
            // Find next/previous visible window
            activeIndex = mActiveControllerWindows[mode];
            int delta = next ? 1 : -1;

            for (size_t i = 0; i < winCount; ++i)
            {
                activeIndex = wrap(activeIndex, winCount, delta);
                if (mGuiModeStates[mode].mWindows[activeIndex]->isVisible())
                    break;
            }
        }

        if (mActiveControllerWindows[mode] != activeIndex)
            setActiveControllerWindow(mode, activeIndex);
    }

    void WindowManager::reapplyActiveControllerWindow()
    {
        if (!Settings::gui().mControllerMenus || mGuiModes.empty())
            return;

        const GuiMode mode = mGuiModes.back();
        size_t winCount = mGuiModeStates[mode].mWindows.size();

        for (size_t i = 0; i < winCount; i++)
        {
            // Set active window last so inactive windows don't stomp on changes it makes, e.g. to tooltips.
            if (i != mActiveControllerWindows[mode])
                mGuiModeStates[mode].mWindows[i]->setActiveControllerWindow(false);
        }
        if (winCount > 0)
            mGuiModeStates[mode].mWindows[mActiveControllerWindows[mode]]->setActiveControllerWindow(true);
    }

    void WindowManager::setActiveControllerWindow(GuiMode mode, size_t activeIndex)
    {
        if (!Settings::gui().mControllerMenus)
            return;

        size_t winCount = mGuiModeStates[mode].mWindows.size();
        if (winCount == 0)
            return;

        activeIndex = std::clamp<size_t>(activeIndex, 0, winCount - 1);
        mActiveControllerWindows[mode] = activeIndex;

        reapplyActiveControllerWindow();

        OFBase::Environment::get().getInputManager()->setGamepadGuiCursorEnabled(
            mGuiModeStates[mode].mWindows[activeIndex]->isGamepadCursorAllowed());

        WindowBase* activeWindow = mGuiModeStates[mode].mWindows[activeIndex];
        if (activeWindow->isVisible())
            MyGUI::LayerManager::getInstance().upLayerItem(activeWindow->mMainWidget);

        updateControllerButtonsOverlay();
        setCursorActive(false);

        if (winCount > 1)
            playSound(ESM::RefId::stringRefId("Menu Size"));
    }

    void WindowManager::update(float frameDuration)
    {
        handleScheduledMessageBoxes();

        bool gameRunning
            = OFBase::Environment::get().getStateManager()->getState() != OFBase::StateManager::State_NoGame;

        if (!mGuiModes.empty())
        {
            GuiModeState& state = mGuiModeStates[mGuiModes.back()];
            for (WindowBase* window : state.mWindows)
                window->onFrame(frameDuration);
        }

        // Make sure message boxes are always in front
        // This is an awful workaround for a series of awfully interwoven issues that couldn't be worked around
        // in a better way because of an impressive number of even more awfully interwoven issues.
        if (mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox()
            && mCurrentModals.back() != mMessageBoxManager->getInteractiveMessageBox())
        {
            std::vector<WindowModal*>::iterator found = std::find(
                mCurrentModals.begin(), mCurrentModals.end(), mMessageBoxManager->getInteractiveMessageBox());
            if (found != mCurrentModals.end())
            {
                WindowModal* msgbox = *found;
                std::swap(*found, mCurrentModals.back());
                MyGUI::InputManager::getInstance().addWidgetModal(msgbox->mMainWidget);
                mKeyboardNavigation->setModalWindow(msgbox->mMainWidget);
                mKeyboardNavigation->setDefaultFocus(msgbox->mMainWidget, msgbox->getDefaultKeyFocus());
            }
        }

        if (!mCurrentModals.empty())
            mCurrentModals.back()->onFrame(frameDuration);

        mKeyboardNavigation->onFrame();

        if (mMessageBoxManager)
            mMessageBoxManager->onFrame(frameDuration);

        mToolTips->onFrame(frameDuration);

        mDebugWindow->onFrame(frameDuration);

        if (isConsoleMode())
            mConsole->onFrame(frameDuration);

        if (isSettingsWindowVisible())
            mSettingsWindow->onFrame(frameDuration);

        if (mControllerButtonsOverlay && mControllerButtonsOverlay->isVisible())
            mControllerButtonsOverlay->onFrame(frameDuration);

        updateMouseEmulationCursor();

        if (!gameRunning)
            return;

        // We should display message about crime only once per frame, even if there are several crimes.
        // Otherwise we will get message spam when stealing several items via Take All button.
        const OFWorld::Ptr player = OFMechanics::getPlayer();
        const OFWorld::Class& playerCls = player.getClass();
        int currentBounty = playerCls.getNpcStats(player).getBounty();
        if (currentBounty != mPlayerBounty)
        {
            if (mPlayerBounty >= 0 && currentBounty > mPlayerBounty)
                messageBox("#{sCrimeMessage}");

            mPlayerBounty = currentBounty;
        }

        OFBase::LuaManager::ActorControls* playerControls
            = OFBase::Environment::get().getLuaManager()->getActorControls(player);
        bool triedToMove = playerControls
            && (playerControls->mMovement != 0 || playerControls->mSideMovement != 0 || playerControls->mJump);
        if (mMessageBoxManager && triedToMove && playerCls.getEncumbrance(player) > playerCls.getCapacity(player))
        {
            const auto& msgboxs = mMessageBoxManager->getActiveMessageBoxes();
            auto it
                = std::find_if(msgboxs.begin(), msgboxs.end(), [](const std::unique_ptr<OFGui::MessageBox>& msgbox) {
                      return (msgbox->getMessage() == "#{sNotifyMessage59}");
                  });

            // if an overencumbered messagebox is already present, reset its expiry timer,
            // otherwise create a new one.
            if (it != msgboxs.end())
                (*it)->mCurrentTime = 0;
            else
                messageBox("#{sNotifyMessage59}");
        }

        mHud->onFrame(frameDuration);

        mPostProcessorHud->onFrame(frameDuration);
    }

    void WindowManager::setDrowningBarVisibility(bool visible)
    {
        mHud->setDrowningBarVisible(visible);
    }

    void WindowManager::setFocusObject(const OFWorld::Ptr& focus)
    {
        mToolTips->setFocusObject(focus);

        const int showOwned = Settings::game().mShowOwned;
        if (mHud && (showOwned == 2 || showOwned == 3))
        {
            bool owned = mToolTips->checkOwned();
            mHud->setCrosshairOwned(owned);
        }
    }

    void WindowManager::setFocusObjectScreenCoords(float x, float y)
    {
        mToolTips->setFocusObjectScreenCoords(x, y);
    }

    bool WindowManager::toggleFullHelp()
    {
        return mToolTips->toggleFullHelp();
    }

    bool WindowManager::getFullHelp() const
    {
        return mToolTips->getFullHelp();
    }

    void WindowManager::setCursorVisible(bool visible)
    {
        mCursorVisible = visible;
    }

    void WindowManager::setCursorActive(bool active)
    {
        mCursorActive = active;
    }

    void WindowManager::setCursorInteractionEnabled(bool enabled)
    {
        mCursorInteractionEnabled = enabled;
        if (mInputBlocker)
            mInputBlocker->setVisible(!isGuiMode() && !mCursorInteractionEnabled);
    }

    void WindowManager::onRetrieveTag(const MyGUI::UString& tag, MyGUI::UString& result)
    {
        std::string_view tagView = tag;

        constexpr std::string_view myGuiPrefix = "setting=";

        constexpr std::string_view tokenToFind = "sCell=";

        if (tagView.starts_with(myGuiPrefix))
        {
            tagView = tagView.substr(myGuiPrefix.length());
            const size_t commaPos = tagView.find(',');
            if (commaPos == std::string_view::npos)
                throw std::runtime_error("Invalid setting tag (expected comma): " + std::string(tagView));

            std::string_view settingSection = tagView.substr(0, commaPos);
            std::string_view settingTag = tagView.substr(commaPos + 1, tagView.length());

            result = Settings::get<MyGUI::Colour>(settingSection, settingTag).get().print();
        }
        else if (tagView.starts_with(tokenToFind))
        {
            std::string_view cellName = mTranslationDataStorage.translateCellName(tagView.substr(tokenToFind.length()));
            result.assign(cellName.data(), cellName.size());
            result = MyGUI::TextIterator::toTagsString(result);
        }
        else if (Gui::replaceTag(tagView, result))
        {
            return;
        }
        else
        {
            std::vector<std::string> split;
            Misc::StringUtils::split(tagView, split, ":");

            L10n::Manager& l10nManager = *OFBase::Environment::get().getL10nManager();

            // If a key has a "Context:KeyName" format, use YAML to translate data
            if (split.size() == 2)
            {
                result = l10nManager.getContext(split[0])->formatMessage(split[1], {}, {});
                return;
            }

            // If not, treat is as GMST name from legacy localization
            if (!mStore)
            {
                Log(Debug::Error) << "Error: WindowManager::onRetrieveTag: no Store set up yet, can not replace '"
                                  << tagView << "'";
                result.assign(tagView.data(), tagView.size());
                return;
            }
            const ESM::GameSetting* setting = mStore->get<ESM::GameSetting>().search(tagView);

            if (setting && setting->mValue.getType() == ESM::VT_String)
                result = setting->mValue.getString();
            else
                result.assign(tagView.data(), tagView.size());
        }
    }

    void WindowManager::processChangedSettings(const Settings::CategorySettingVector& changed)
    {
        bool changeRes = false;
        bool resolutionChosen = false;
        for (const auto& setting : changed)
        {
            if (setting.first == "GUI" && setting.second == "menu transparency")
                setMenuTransparency(Settings::gui().mMenuTransparency);
            else if (setting.first == "Video"
                && (setting.second == "resolution x" || setting.second == "resolution y"
                    || setting.second == "window mode" || setting.second == "window border"))
            {
                changeRes = true;
                if (setting.second == "resolution x" || setting.second == "resolution y")
                    resolutionChosen = true;
            }

            else if (setting.first == "Video" && setting.second == "vsync mode")
                mVideoWrapper->setSyncToVBlank(Settings::video().mVsyncMode);
            else if (setting.first == "Video" && (setting.second == "gamma" || setting.second == "contrast"))
                mVideoWrapper->setGammaContrast(Settings::video().mGamma, Settings::video().mContrast);
        }

        if (changeRes)
        {
            // A resolution picked in the options is the size the window is to have
            if (resolutionChosen)
                mWindowSize = { Settings::video().mResolutionX, Settings::video().mResolutionY };
            setVideoMode();
        }
    }

    void WindowManager::setVideoMode()
    {
        const Settings::WindowMode mode = Settings::video().mWindowMode;
        const SDLUtil::WindowSize setting{ Settings::video().mResolutionX, Settings::video().mResolutionY };
        const SDLUtil::WindowSize size
            = SDLUtil::sizeForWindowMode(setting, mWindowSize, mode, SDLUtil::thisDesktopPlatform());
        mVideoWrapper->setVideoMode(size.width, size.height, mode, Settings::video().mWindowBorder);
    }

    void WindowManager::saveUserSettings()
    {
        const std::filesystem::path file = mCfgMgr.getUserConfigPath() / "settings.cfg";
        const SDLUtil::WindowSize setting{ Settings::video().mResolutionX, Settings::video().mResolutionY };
        const SDLUtil::WindowSize size = SDLUtil::resolutionToSave(
            setting, mWindowSize, SDL_GetWindowFlags(mSdlWindow), SDLUtil::thisDesktopPlatform());
        if (size == setting)
        {
            Settings::Manager::saveUser(file);
            return;
        }

        // The file gets the size of the window on the desktop; the setting goes on following the drawable. Setting
        // it there and back is no change of the resolution for anyone to act on.
        const Settings::CategorySettingVector resolution = { { "Video", "resolution x" }, { "Video", "resolution y" } };
        const bool pending = !Settings::Manager::getPendingChanges(resolution).empty();
        Settings::video().mResolutionX.set(size.width);
        Settings::video().mResolutionY.set(size.height);
        Settings::Manager::saveUser(file);
        Settings::video().mResolutionX.set(setting.width);
        Settings::video().mResolutionY.set(setting.height);
        if (!pending)
            Settings::Manager::resetPendingChanges(resolution);
    }

    void WindowManager::windowResized(int x, int y)
    {
        // The flags are those of the window now, at or after the resize this event is about: the size of a Space the
        // window has entered is never taken for its own
        mWindowSize = SDLUtil::windowSizeAfterResize(
            mWindowSize, { x, y }, SDL_GetWindowFlags(mSdlWindow), SDLUtil::thisDesktopPlatform());
        Settings::video().mResolutionX.set(x);
        Settings::video().mResolutionY.set(y);

        // We only want to process changes to window-size related settings.
        Settings::CategorySettingVector filter = { { "Video", "resolution x" }, { "Video", "resolution y" } };

        // If the HUD has not been initialised, the World singleton will not be available.
        if (mHud)
        {
            OFBase::Environment::get().getWorld()->processChangedSettings(Settings::Manager::getPendingChanges(filter));
        }

        Settings::Manager::resetPendingChanges(filter);

        // On the Mac the interface is sized in points: on a display of another scaling (the window moved) it keeps its
        // size. Elsewhere it keeps the scale it started with.
        int pointsW, pointsH;
        SDL_GetWindowSize(mSdlWindow, &pointsW, &pointsH);
        if (const std::optional<float> scalingFactor = SDLUtil::interfaceScaleAfterResize(
                Settings::gui().mScalingFactor, pointsW, x, mScalingFactor, SDLUtil::thisDesktopPlatform()))
        {
            Log(Debug::Info) << "Interface scale " << mScalingFactor << " -> " << *scalingFactor << " (" << x
                             << " pixels across " << pointsW << " points)";
            mScalingFactor = *scalingFactor;
            mGuiPlatform->getRenderManagerPtr()->setScalingFactor(mScalingFactor);
        }

        mGuiPlatform->getRenderManagerPtr()->setViewSize(x, y);

        // scaled size
        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        x = viewSize.width;
        y = viewSize.height;

        sizeVideo(x, y);

        if (!mHud)
            return; // UI not initialized yet

        for (const auto& [window, settings] : mTrackedWindows)
        {
            const WindowRectSettingValues& rect = settings.mIsMaximized ? settings.mMaximized : settings.mRegular;
            window->setPosition(MyGUI::IntPoint(static_cast<int>(rect.mX * x), static_cast<int>(rect.mY * y)));
            window->setSize(MyGUI::IntSize(static_cast<int>(rect.mW * x), static_cast<int>(rect.mH * y)));

            WindowBase::clampWindowCoordinates(window);
        }

        for (const auto& window : mWindows)
            window->onResChange(x, y);

        LuaUi::updateAllElementCoords();

        // Re-apply any controller-specific window changes.
        reapplyActiveControllerWindow();

        OFBase::Environment::get().getLuaManager()->viewportResized(x, y);

        // TODO: check if any windows are now off-screen and move them back if so
    }

    bool WindowManager::isWindowVisible() const
    {
        return mWindowVisible;
    }

    void WindowManager::windowVisibilityChange(bool visible)
    {
        mWindowVisible = visible;
    }

    void WindowManager::windowClosed()
    {
        OFBase::Environment::get().getStateManager()->requestQuit();
    }

    void WindowManager::windowModeRequested(Settings::WindowMode mode)
    {
        // Control-Command-F out of (or into) a full-screen mode of the game: the setting changes as from the options
        Log(Debug::Info) << "Window mode " << static_cast<int>(Settings::video().mWindowMode.get()) << " -> "
                         << static_cast<int>(mode) << " (Control-Command-F), window size " << mWindowSize.width << "x"
                         << mWindowSize.height;
        Settings::video().mWindowMode.set(mode);
        setVideoMode();
        Settings::Manager::resetPendingChanges({ { "Video", "window mode" } });
    }

    void WindowManager::onCursorChange(std::string_view name)
    {
        mMyGuiCursor = name;
        mCursorManager->cursorChanged(mLuaCursorOverride.empty() ? name : mLuaCursorOverride);
    }

    void WindowManager::pushGuiMode(GuiMode mode)
    {
        pushGuiMode(mode, OFWorld::Ptr());
    }

    void WindowManager::pushGuiMode(GuiMode mode, const OFWorld::Ptr& arg)
    {
        if (mGuiModes.empty() || mGuiModes.back() != mode)
        {
            // If this mode already exists somewhere in the stack, just bring it to the front.
            if (std::find(mGuiModes.begin(), mGuiModes.end(), mode) != mGuiModes.end())
            {
                mGuiModes.erase(std::find(mGuiModes.begin(), mGuiModes.end(), mode));
            }

            if (!mGuiModes.empty())
            {
                mKeyboardNavigation->saveFocus(mGuiModes.back());
                mGuiModeStates[mGuiModes.back()].update(false);
            }
            mGuiModes.push_back(mode);

            mGuiModeStates[mode].update(true);
        }
        try
        {
            for (WindowBase* window : mGuiModeStates[mode].mWindows)
                window->setPtr(arg);
        }
        catch (...)
        {
            popGuiMode();
            throw;
        }

        mKeyboardNavigation->restoreFocus(mode);

        updateVisible();
        OFBase::Environment::get().getLuaManager()->uiModeChanged(arg);

        if (Settings::gui().mControllerMenus)
        {
            // Activate first visible window. This needs to be called after updateVisible.
            if (mActiveControllerWindows[mode] != 0)
                mActiveControllerWindows[mode] = mActiveControllerWindows[mode] - 1;
            cycleActiveControllerWindow(true);
        }
    }

    void WindowManager::setCullMask(uint32_t mask)
    {
        mViewer->getCamera()->setCullMask(mask);

        // We could check whether stereo is enabled here, but these methods are
        // trivial and have no effect in mono or multiview so just call them regardless.
        mViewer->getCamera()->setCullMaskLeft(mask);
        mViewer->getCamera()->setCullMaskRight(mask);
    }

    uint32_t WindowManager::getCullMask()
    {
        return mViewer->getCamera()->getCullMask();
    }

    void WindowManager::popGuiMode(bool forceExit)
    {
        if (!mGuiModes.empty())
        {
            const GuiMode mode = mGuiModes.back();
            if (forceExit)
            {
                GuiModeState& state = mGuiModeStates[mode];
                for (const auto& window : state.mWindows)
                    window->exit();
            }
            mKeyboardNavigation->saveFocus(mode);
            if (containsMode(mode))
            {
                mGuiModes.pop_back();
                mGuiModeStates[mode].update(false);
                OFBase::Environment::get().getLuaManager()->uiModeChanged(OFWorld::Ptr());
            }
        }

        if (!mGuiModes.empty())
        {
            const GuiMode mode = mGuiModes.back();
            mGuiModeStates[mode].update(true);
            mKeyboardNavigation->restoreFocus(mode);
        }

        updateVisible();

        // To make sure that console window get focus again
        if (mConsole && mConsole->isVisible())
            mConsole->onOpen();

        if (Settings::gui().mControllerMenus)
        {
            if (mGuiModes.empty())
            {
                setControllerTooltipVisible(false);
                // When all windows are hidden, reset tooltip visibility to user's preference.
                mControllerTooltipEnabled = Settings::gui().mControllerTooltips;
            }
            else
                reapplyActiveControllerWindow();
        }
    }

    void WindowManager::removeGuiMode(GuiMode mode)
    {
        if (!mGuiModes.empty() && mGuiModes.back() == mode)
        {
            popGuiMode();
            return;
        }

        std::vector<GuiMode>::iterator it = mGuiModes.begin();
        while (it != mGuiModes.end())
        {
            if (*it == mode)
                it = mGuiModes.erase(it);
            else
                ++it;
        }

        updateVisible();
        OFBase::Environment::get().getLuaManager()->uiModeChanged(OFWorld::Ptr());
    }

    void WindowManager::getMousePosition(int& x, int& y)
    {
        const MyGUI::IntPoint& pos = MyGUI::InputManager::getInstance().getMousePosition();
        x = pos.left;
        y = pos.top;
    }

    void WindowManager::getMousePosition(float& x, float& y)
    {
        const MyGUI::IntPoint& pos = MyGUI::InputManager::getInstance().getMousePosition();
        x = static_cast<float>(pos.left);
        y = static_cast<float>(pos.top);
        const MyGUI::IntSize& viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        x /= viewSize.width;
        y /= viewSize.height;
    }

    bool WindowManager::getWorldMouseOver()
    {
        return mHud->getWorldMouseOver();
    }

    float WindowManager::getScalingFactor() const
    {
        return mScalingFactor;
    }

    OFGui::ConfirmationDialog* WindowManager::getConfirmationDialog()
    {
        return mConfirmationDialog;
    }
    OFGui::HUD* WindowManager::getHud()
    {
        return mHud;
    }
    OFGui::PostProcessorHud* WindowManager::getPostProcessorHud()
    {
        return mPostProcessorHud;
    }

    bool WindowManager::isGuiMode() const
    {
        return !mGuiModes.empty() || isConsoleMode() || isPostProcessorHudVisible() || isInteractiveMessageBoxActive();
    }

    bool WindowManager::isConsoleMode() const
    {
        return mConsole && mConsole->isVisible();
    }

    bool WindowManager::isPostProcessorHudVisible() const
    {
        return mPostProcessorHud && mPostProcessorHud->isVisible();
    }

    bool WindowManager::isSettingsWindowVisible() const
    {
        return mSettingsWindow && mSettingsWindow->isVisible();
    }

    bool WindowManager::isInteractiveMessageBoxActive() const
    {
        return mMessageBoxManager && mMessageBoxManager->isInteractiveMessageBox();
    }

    std::uint64_t WindowManager::interactiveMessageBoxId() const
    {
        return mMessageBoxManager ? mMessageBoxManager->getInteractiveMessageBoxId() : 0;
    }

    OFGui::GuiMode WindowManager::getMode() const
    {
        if (mGuiModes.empty())
            return GM_None;
        return mGuiModes.back();
    }

    void WindowManager::disallowMouse()
    {
        mInputBlocker->setVisible(true);
    }

    void WindowManager::allowMouse()
    {
        mInputBlocker->setVisible(!isGuiMode() && !mCursorInteractionEnabled);
    }

    void WindowManager::notifyInputActionBound()
    {
        mSettingsWindow->updateControlsBox();
        allowMouse();
    }

    bool WindowManager::containsMode(GuiMode mode) const
    {
        if (mGuiModes.empty())
            return false;

        return std::find(mGuiModes.begin(), mGuiModes.end(), mode) != mGuiModes.end();
    }

    void WindowManager::showCrosshair(bool show)
    {
        if (mHud)
            mHud->setCrosshairVisible(show && Settings::hud().mCrosshair);
    }

    bool WindowManager::setHudVisibility(bool show)
    {
        mHudEnabled = show;
        updateVisible();
        mMessageBoxManager->setVisible(mHudEnabled);
        return mHudEnabled;
    }

    const Translation::Storage& WindowManager::getTranslationDataStorage() const
    {
        return mTranslationDataStorage;
    }

    void WindowManager::changePointer(const std::string& name)
    {
        MyGUI::PointerManager::getInstance().setPointer(name);
        onCursorChange(name);
    }

    void WindowManager::createLuaCursor(const std::string& name, const std::string& path, int width, int height,
        int hotspotX, int hotspotY, double rotation)
    {
        osg::ref_ptr<osg::Image> image = mResourceSystem->getImageManager()->getImage(VFS::Path::Normalized(path));
        if (!image.valid())
        {
            Log(Debug::Warning) << "Failed to load Lua cursor texture: " << path;
            return;
        }
        mCursorManager->createCursor(name, rotation, image, hotspotX, hotspotY, width, height);
    }

    void WindowManager::removeLuaCursor(const std::string& name)
    {
        if (mLuaCursorOverride == name)
            mLuaCursorOverride.clear();
        mCursorManager->removeCursor(name);
        mCursorManager->cursorChanged(getCurrentCursorName());
    }

    void WindowManager::setLuaCursorOverride(const std::string& name)
    {
        mLuaCursorOverride = name;
        mCursorManager->cursorChanged(getCurrentCursorName());
    }

    std::string WindowManager::getCurrentCursorName() const
    {
        return mLuaCursorOverride.empty() ? mMyGuiCursor : mLuaCursorOverride;
    }

    void WindowManager::updatePlayer()
    {
        const OFWorld::Ptr player = OFMechanics::getPlayer();
        if (player.getClass().getNpcStats(player).isWerewolf())
            setWerewolfOverlay(true);
    }

    // Remove this wrapper once onKeyFocusChanged call is rendered unnecessary
    void WindowManager::setKeyFocusWidget(MyGUI::Widget* widget)
    {
        MyGUI::InputManager::getInstance().setKeyFocusWidget(widget);
        onKeyFocusChanged(widget);
    }

    void WindowManager::onKeyFocusChanged(MyGUI::Widget* widget)
    {
        bool capturesInput = false;
        if (widget)
        {
            LuaUi::WidgetExtension* luaWidget = dynamic_cast<LuaUi::WidgetExtension*>(widget);
            if (luaWidget)
                capturesInput = luaWidget->isTextInput();
            else
                capturesInput = widget->castType<MyGUI::EditBox>(false);
        }

        // The SDL_IsTextInputActive() check helps to avoid duplicate calls in SDL2.
        // This may no longer be required when switching to SDL3 where the function
        // has also been renamed to SDL_TextInputActive() and returns bool instead
        // of SDL_bool.

        const bool inputActive = SDL_IsTextInputActive() == SDL_TRUE;
        if (capturesInput == inputActive)
            return;

        if (capturesInput)
            SDL_StartTextInput();
        else
            SDL_StopTextInput();
    }

    void WindowManager::setEnemy(const OFWorld::Ptr& enemy)
    {
        mHud->setEnemy(enemy);
    }

    std::size_t WindowManager::getMessagesCount() const
    {
        std::size_t count = 0;
        if (mMessageBoxManager)
            count = mMessageBoxManager->getMessagesCount();

        return count;
    }

    Loading::Listener* WindowManager::getLoadingScreen()
    {
        return mLoadingScreen;
    }

    bool WindowManager::getCursorVisible()
    {
        return mCursorVisible && mCursorActive;
    }

    void WindowManager::updateMouseEmulationCursor()
    {
        if (!mHud)
            return;

        bool mouseEmulation = OFBase::Environment::get().getInputManager()->joystickLastUsed()
            && OFBase::Environment::get().getInputManager()->isGamepadGuiCursorEnabled();

        // The circle is where the stick left the pointer: while the pad holds a pick (the D-pad moved it, and A acts on
        // it) the pointer is not what the screen is waiting for, and the circle left at its old place only misleads
        bool showCursor = mouseEmulation && Settings::hud().mMouseEmulationCursor && getCursorVisible()
            && !OFBase::Environment::get().getInputManager()->padPick();
        mHud->setMouseEmulationCursorVisible(showCursor);

        if (showCursor)
        {
            MyGUI::IntPoint pos = MyGUI::InputManager::getInstance().getMousePosition();
            mHud->setMouseEmulationCursorPosition(pos.left, pos.top);
        }
    }

    void WindowManager::trackWindow(Layout* layout, const WindowSettingValues& settings)
    {
        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        const WindowRectSettingValues& rect = settings.mIsMaximized ? settings.mMaximized : settings.mRegular;

        MyGUI::Window* window = layout->mMainWidget->castType<MyGUI::Window>();
        window->setPosition(
            MyGUI::IntPoint(static_cast<int>(rect.mX * viewSize.width), static_cast<int>(rect.mY * viewSize.height)));
        window->setSize(
            MyGUI::IntSize(static_cast<int>(rect.mW * viewSize.width), static_cast<int>(rect.mH * viewSize.height)));

        window->eventWindowChangeCoord += MyGUI::newDelegate(this, &WindowManager::onWindowChangeCoord);
        WindowBase::clampWindowCoordinates(window);

        mTrackedWindows.emplace(window, settings);
    }

    void WindowManager::toggleMaximized(Layout* layout)
    {
        MyGUI::Window* window = layout->mMainWidget->castType<MyGUI::Window>();
        const auto it = mTrackedWindows.find(window);
        if (it == mTrackedWindows.end())
            return;

        const WindowSettingValues& settings = it->second;
        const WindowRectSettingValues& rect = settings.mIsMaximized ? settings.mRegular : settings.mMaximized;

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        const int x = static_cast<int>(rect.mX * viewSize.width);
        const int y = static_cast<int>(rect.mY * viewSize.height);
        const int w = static_cast<int>(rect.mW * viewSize.width);
        const int h = static_cast<int>(rect.mH * viewSize.height);
        window->setCoord(x, y, w, h);

        settings.mIsMaximized.set(!settings.mIsMaximized.get());
    }

    void WindowManager::onWindowChangeCoord(MyGUI::Window* window)
    {
        const auto it = mTrackedWindows.find(window);
        if (it == mTrackedWindows.end())
            return;

        WindowBase::clampWindowCoordinates(window);

        const WindowSettingValues& settings = it->second;

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();
        settings.mRegular.mX.set(window->getPosition().left / static_cast<float>(viewSize.width));
        settings.mRegular.mY.set(window->getPosition().top / static_cast<float>(viewSize.height));
        settings.mRegular.mW.set(window->getSize().width / static_cast<float>(viewSize.width));
        settings.mRegular.mH.set(window->getSize().height / static_cast<float>(viewSize.height));

        settings.mIsMaximized.set(false);
    }

    void WindowManager::clear()
    {
        mPlayerBounty = -1;

        for (const auto& window : mWindows)
        {
            window->clear();
            window->setDisabledByLua(false);
        }

        mMessageBoxManager->clear();



        // The air meter is not part of the display that the scripts of the game that is left take down with them
        if (mAirMeter)
            mAirMeter->hide();

        while (!mGuiModes.empty())
            popGuiMode();

        updateVisible();
    }

    bool WindowManager::isSavingAllowed() const
    {
        return !MyGUI::InputManager::getInstance().isModalAny()
            && !isConsoleMode()
            // TODO: remove this, once we have properly serialized the state of open windows
            && (!isGuiMode() || (mGuiModes.size() == 1 && getMode() == GM_MainMenu));
    }

    void WindowManager::playVideo(std::string_view name, bool allowSkipping, bool overrideSounds)
    {
        mVideoWidget->playVideo("video\\" + std::string{ name });

        mVideoWidget->eventKeyButtonPressed.clear();
        mVideoBackground->eventKeyButtonPressed.clear();
        if (allowSkipping)
        {
            mVideoWidget->eventKeyButtonPressed += MyGUI::newDelegate(this, &WindowManager::onVideoKeyPressed);
            mVideoBackground->eventKeyButtonPressed += MyGUI::newDelegate(this, &WindowManager::onVideoKeyPressed);
        }

        enableScene(false);

        MyGUI::IntSize screenSize = MyGUI::RenderManager::getInstance().getViewSize();
        sizeVideo(screenSize.width, screenSize.height);

        MyGUI::Widget* oldKeyFocus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
        setKeyFocusWidget(mVideoWidget);

        mVideoBackground->setVisible(true);

        if (mInputBlocker)
            mInputBlocker->setVisible(false);

        bool cursorWasVisible = mCursorVisible;
        setCursorVisible(false);

        if (overrideSounds && mVideoWidget->hasAudioStream())
            OFBase::Environment::get().getSoundManager()->pauseSounds(
                OFSound::VideoPlayback, ~OFSound::Type::Movie & OFSound::Type::Mask);

        Misc::FrameRateLimiter frameRateLimiter
            = Misc::makeFrameRateLimiter(OFBase::Environment::get().getFrameRateLimit());
        while (mVideoWidget->update() && !OFBase::Environment::get().getStateManager()->hasQuitRequest())
        {
            const float dt
                = std::chrono::duration_cast<std::chrono::duration<float>>(frameRateLimiter.getLastFrameDuration())
                      .count();

            OFBase::Environment::get().getInputManager()->update(dt, true, false);

            if (!mWindowVisible)
            {
                mVideoWidget->pause();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            else
            {
                if (mVideoWidget->isPaused())
                    mVideoWidget->resume();

                mVideoWidget->commitFrame();

                mViewer->eventTraversal();
                mViewer->updateTraversal();
                mViewer->renderingTraversals();
            }
            // at the time this function is called we are in the middle of a frame,
            // so out of order calls are necessary to get a correct frameNumber for the next frame.
            // refer to the advance() and frame() order in Engine::go()
            mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());

            frameRateLimiter.limit();
        }
        mVideoWidget->stop();

        OFBase::Environment::get().getSoundManager()->resumeSounds(OFSound::VideoPlayback);

        setKeyFocusWidget(oldKeyFocus);

        setCursorVisible(cursorWasVisible);

        // Restore normal rendering
        updateVisible();

        mVideoBackground->setVisible(false);
    }

    void WindowManager::sizeVideo(int screenWidth, int screenHeight)
    {
        // Use black bars to correct aspect ratio
        mVideoBackground->setSize(screenWidth, screenHeight);
        mVideoWidget->autoResize(Settings::gui().mStretchMenuBackground);
    }

    void WindowManager::exitCurrentModal()
    {
        if (!mCurrentModals.empty())
        {
            WindowModal* window = mCurrentModals.back();
            if (!window->exit())
                return;
            window->setVisible(false);
            updateControllerButtonsOverlay();
        }
    }

    void WindowManager::addCurrentModal(WindowModal* input)
    {
        if (mCurrentModals.empty())
            mKeyboardNavigation->saveFocus(getMode());

        mCurrentModals.push_back(input);
        mKeyboardNavigation->restoreFocus(-1);

        mKeyboardNavigation->setModalWindow(input->mMainWidget);
        mKeyboardNavigation->setDefaultFocus(input->mMainWidget, input->getDefaultKeyFocus());

        updateControllerButtonsOverlay();
    }

    void WindowManager::removeCurrentModal(WindowModal* input)
    {
        if (!mCurrentModals.empty())
        {
            if (input == mCurrentModals.back())
            {
                mCurrentModals.pop_back();
                mKeyboardNavigation->saveFocus(-1);
            }
            else
            {
                auto found = std::find(mCurrentModals.begin(), mCurrentModals.end(), input);
                if (found != mCurrentModals.end())
                    mCurrentModals.erase(found);
                else
                    Log(Debug::Warning) << "Warning: can't find modal window " << input;
            }
        }
        if (mCurrentModals.empty())
        {
            mKeyboardNavigation->setModalWindow(nullptr);
            mKeyboardNavigation->restoreFocus(getMode());
        }
        else
            mKeyboardNavigation->setModalWindow(mCurrentModals.back()->mMainWidget);
    }

    void WindowManager::onVideoKeyPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char value)
    {
        if (key == MyGUI::KeyCode::Escape)
            mVideoWidget->stop();
    }

    void WindowManager::fadeScreenIn(const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeOut(time, delay);
    }

    void WindowManager::fadeScreenOut(const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeIn(time, delay);
    }

    void WindowManager::fadeScreenTo(const int percent, const float time, bool clearQueue, float delay)
    {
        if (clearQueue)
            mScreenFader->clearQueue();
        mScreenFader->fadeTo(percent, time, delay);
    }

    void WindowManager::setBlindness(const int percent)
    {
        mBlindnessFader->notifyAlphaChanged(percent / 100.f);
    }

    void WindowManager::setAirMeter(
        float x, float y, float unit, float alpha, int ticks, float red, float green, float blue)
    {
        // Made the first time that the player is under water
        if (!mAirMeter)
        {
            if (!(alpha > 0.f))
                return;
            mAirMeter = std::make_unique<FalloutAirMeter>();
        }
        FalloutAirMeter::State state;
        state.mX = x;
        state.mY = y;
        state.mUnit = unit;
        state.mAlpha = alpha;
        state.mTicks = ticks;
        state.mColor = { red, green, blue };
        mAirMeter->draw(state);
    }

    void WindowManager::activateHitOverlay(bool interrupt)
    {
        if (!Settings::gui().mHitFader)
            return;

        if (!interrupt && !mHitFader->isEmpty())
            return;

        mHitFader->clearQueue();
        mHitFader->fadeTo(100, 0.0f);
        mHitFader->fadeTo(0, 0.5f);
    }

    void WindowManager::setWerewolfOverlay(bool set)
    {
        if (!Settings::gui().mWerewolfOverlay)
            return;

        if (mWerewolfFader)
            mWerewolfFader->notifyAlphaChanged(set ? 1.0f : 0.0f);
    }

    void WindowManager::onClipboardChanged(std::string_view type, std::string_view data)
    {
        if (type == "Text")
            SDL_SetClipboardText(MyGUI::TextIterator::getOnlyText(MyGUI::UString(data)).asUTF8().c_str());
    }

    void WindowManager::onClipboardRequested(std::string_view type, std::string& data)
    {
        if (type != "Text")
            return;
        char* text = nullptr;
        text = SDL_GetClipboardText();
        if (text)
            data = MyGUI::TextIterator::toTagsString(text);

        SDL_free(text);
    }

    void WindowManager::toggleConsole()
    {
        bool visible = mConsole->isVisible();

        if (!visible && !mGuiModes.empty())
            mKeyboardNavigation->saveFocus(mGuiModes.back());

        mConsole->setVisible(!visible);

        if (visible && !mGuiModes.empty())
            mKeyboardNavigation->restoreFocus(mGuiModes.back());

        updateVisible();
    }

    void WindowManager::toggleDebugWindow()
    {
        mDebugWindow->setVisible(!mDebugWindow->isVisible());
    }

    void WindowManager::togglePostProcessorHud()
    {
        if (!OFBase::Environment::get().getWorld()->getPostProcessor()->isEnabled())
        {
            messageBox("#{OFEngine:PostProcessingIsNotEnabled}");
            return;
        }

        bool visible = mPostProcessorHud->isVisible();

        if (!visible && !mGuiModes.empty())
            mKeyboardNavigation->saveFocus(mGuiModes.back());

        mPostProcessorHud->setVisible(!visible);

        if (visible && !mGuiModes.empty())
            mKeyboardNavigation->restoreFocus(mGuiModes.back());

        updateVisible();
    }

    void WindowManager::toggleSettingsWindow()
    {
        bool visible = mSettingsWindow->isVisible();

        if (!visible && !mGuiModes.empty())
            mKeyboardNavigation->saveFocus(mGuiModes.back());

        mSettingsWindow->setVisible(!visible);

        if (visible && !mGuiModes.empty())
            mKeyboardNavigation->restoreFocus(mGuiModes.back());

        updateVisible();
    }

    namespace
    {
        std::int64_t steadyMilliseconds()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
    }

    void WindowManager::applyChangedSettings()
    {
        const Settings::CategorySettingVector changed = Settings::Manager::getPendingChanges();
        OFBase::Environment::get().getWorld()->processChangedSettings(changed);
        OFBase::Environment::get().getSoundManager()->processChangedSettings(changed);
        processChangedSettings(changed);
        OFBase::Environment::get().getInputManager()->processChangedSettings(changed);
        OFBase::Environment::get().getMechanicsManager()->processChangedSettings(changed);
        Settings::Manager::resetPendingChanges();
    }

    void WindowManager::saveSettings()
    {
        saveUserSettings();
        OFBase::Environment::get().getLuaManager()->savePermanentStorage(mCfgMgr.getUserConfigPath());
    }

    void WindowManager::setMainMenuDrawnByLua(bool drawn, bool escape)
    {
        if (drawn)
            mMainMenuClaimedAt = steadyMilliseconds();
        mMainMenuEscape = drawn && escape;
        mMainMenuDrawn = drawn;
    }

    bool WindowManager::isMainMenuDrawnByLua() const
    {
        return mMainMenuDrawn.load() && steadyMilliseconds() - mMainMenuClaimedAt.load() < 1000;
    }

    bool WindowManager::isMainMenuEscapeLua() const
    {
        return isMainMenuDrawnByLua() && mMainMenuEscape.load();
    }

    void WindowManager::playSound(const ESM::RefId& soundId, float volume, float pitch)
    {
        if (soundId.empty())
            return;

        OFBase::Environment::get().getSoundManager()->playSound(
            soundId, volume, pitch, OFSound::Type::Sfx, OFSound::PlayMode::NoEnvNoScaling);
    }

    void WindowManager::setConsoleSelectedObject(const OFWorld::Ptr& object)
    {
        mConsole->setSelectedObject(object);
    }

    OFWorld::Ptr WindowManager::getConsoleSelectedObject() const
    {
        return mConsole->getSelectedObject();
    }

    void WindowManager::printToConsole(const std::string& msg, std::string_view color)
    {
        mConsole->print(msg, color);
    }

    void WindowManager::setConsoleMode(std::string_view mode)
    {
        mConsole->setConsoleMode(mode);
    }

    const std::string& WindowManager::getConsoleMode()
    {
        return mConsole->getConsoleMode();
    }

    void WindowManager::createCursors()
    {
        MyGUI::ResourceManager::EnumeratorPtr enumerator = MyGUI::ResourceManager::getInstance().getEnumerator();
        while (enumerator.next())
        {
            MyGUI::IResource* resource = enumerator.current().second;
            ResourceImageSetPointerFix* imgSetPointer = resource->castType<ResourceImageSetPointerFix>(false);
            if (!imgSetPointer)
                continue;

            const VFS::Path::Normalized path(imgSetPointer->getImageSet()->getIndexInfo(0, 0).texture);

            osg::ref_ptr<osg::Image> image = mResourceSystem->getImageManager()->getImage(path);

            if (image.valid())
            {
                // scale the cursor to the interface scaling factor
                const float guiUiScale = Settings::gui().mScalingFactor;

                // everything looks good, send it to the cursor manager
                const Uint8 hotspotX = static_cast<Uint8>(guiUiScale * imgSetPointer->getHotSpot().left);
                const Uint8 hotspotY = static_cast<Uint8>(guiUiScale * imgSetPointer->getHotSpot().top);

                MyGUI::IntSize pointerSize = imgSetPointer->getSize();
                const Uint8 width = static_cast<Uint8>(guiUiScale * pointerSize.width);
                const Uint8 height = static_cast<Uint8>(guiUiScale * pointerSize.height);

                int rotation = imgSetPointer->getRotation();

                mCursorManager->createCursor(
                    imgSetPointer->getResourceName(), rotation, image, hotspotX, hotspotY, width, height);
            }
        }
    }

    void WindowManager::createTextures()
    {
        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("white");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8);
            unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                {
                    *(data++) = 255;
                    *(data++) = 255;
                    *(data++) = 255;
                }
            tex->unlock();
        }

        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("black");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8);
            unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                {
                    *(data++) = 0;
                    *(data++) = 0;
                    *(data++) = 0;
                }
            tex->unlock();
        }

        {
            MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().createTexture("transparent");
            tex->createManual(8, 8, MyGUI::TextureUsage::Write, MyGUI::PixelFormat::R8G8B8A8);
            setMenuTransparency(Settings::gui().mMenuTransparency);
        }
    }

    void WindowManager::setMenuTransparency(float value)
    {
        MyGUI::ITexture* tex = MyGUI::RenderManager::getInstance().getTexture("transparent");
        unsigned char* data = reinterpret_cast<unsigned char*>(tex->lock(MyGUI::TextureUsage::Write));
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y)
            {
                *(data++) = 255;
                *(data++) = 255;
                *(data++) = 255;
                *(data++) = static_cast<unsigned char>(value * 255);
            }
        tex->unlock();
    }

    const OFGui::TextColours& WindowManager::getTextColours()
    {
        return mTextColours;
    }

    bool WindowManager::injectKeyPress(MyGUI::KeyCode key, unsigned int text, bool repeat)
    {
        // The pause menu that a script draws has the keys: the keyboard navigation of the windows takes the first arrow
        // key after a click to put the key focus back on a widget it remembers, and the script never gets that key (an
        // option of the settings did not change with the first Left or Right after a click). The console and a modal
        // window of the engine still have their keys
        if (getMode() == OFGui::GM_MainMenu && isMainMenuDrawnByLua() && !isConsoleMode()
            && !MyGUI::InputManager::getInstance().isModalAny())
            return false;
        if (!mKeyboardNavigation->injectKeyPress(key, text, repeat))
        {
            MyGUI::Widget* focus = MyGUI::InputManager::getInstance().getKeyFocusWidget();
            bool widgetActive = MyGUI::InputManager::getInstance().injectKeyPress(key, text);
            if (!widgetActive || !focus)
                return false;
            // FIXME: MyGUI doesn't allow widgets to state if a given key was actually used, so make a guess
            if (focus->getTypeName().find("Button") != std::string::npos)
            {
                switch (key.getValue())
                {
                    case MyGUI::KeyCode::ArrowDown:
                    case MyGUI::KeyCode::ArrowUp:
                    case MyGUI::KeyCode::ArrowLeft:
                    case MyGUI::KeyCode::ArrowRight:
                    case MyGUI::KeyCode::Return:
                    case MyGUI::KeyCode::NumpadEnter:
                    case MyGUI::KeyCode::Space:
                        return true;
                    default:
                        return false;
                }
            }
            return false;
        }
        else
            return true;
    }

    bool WindowManager::injectKeyRelease(MyGUI::KeyCode key)
    {
        return MyGUI::InputManager::getInstance().injectKeyRelease(key);
    }

    void WindowManager::GuiModeState::update(bool visible)
    {
        for (const auto& window : mWindows)
            window->setVisible(visible);
    }

    const std::string& WindowManager::getVersionDescription() const
    {
        return mVersionDescription;
    }

    void WindowManager::handleScheduledMessageBoxes()
    {
        const auto scheduledMessageBoxes = mScheduledMessageBoxes.lock();
        for (const ScheduledMessageBox& v : *scheduledMessageBoxes)
            messageBox(v.mMessage, v.mShowInDialogueMode);
        scheduledMessageBoxes->clear();
    }

    void WindowManager::setDisabledByLua(std::string_view windowId, bool disabled)
    {
        // A window that went (the windows of Morrowind) or never was is said once in the log, by this greppable line
        const auto it = mLuaIdToWindow.find(windowId);
        if (it == mLuaIdToWindow.end())
        {
            Log(Debug::Warning) << "Warning: unknown GUI window '" << windowId << "'";
            return;
        }
        it->second->setDisabledByLua(disabled);
        updateVisible();
    }

    bool WindowManager::isWindowVisible(std::string_view windowId) const
    {
        auto it = mLuaIdToWindow.find(windowId);
        if (it == mLuaIdToWindow.end())
        {
            Log(Debug::Warning) << "Warning: unknown GUI window '" << windowId << "'";
            return false;
        }
        return it->second->isVisible();
    }

    std::vector<std::string_view> WindowManager::getAllWindowIds() const
    {
        std::vector<std::string_view> res;
        for (const auto& [id, _] : mLuaIdToWindow)
            res.push_back(id);
        return res;
    }

    std::vector<std::string_view> WindowManager::getAllowedWindowIds(GuiMode mode) const
    {
        std::vector<std::string_view> res;
        {
            auto it = mGuiModeStates.find(mode);
            if (it != mGuiModeStates.end())
            {
                for (const auto* w : it->second.mWindows)
                    if (!w->getWindowIdForLua().empty())
                        res.push_back(w->getWindowIdForLua());
            }
        }
        return res;
    }

    int WindowManager::getControllerMenuHeight()
    {
        int height = MyGUI::RenderManager::getInstance().getViewSize().height;
        if (mControllerButtonsOverlay != nullptr && mControllerButtonsOverlay->isVisible())
            height -= mControllerButtonsOverlay->getHeight();
        return height;
    }

    void WindowManager::setControllerTooltipVisible(bool visible)
    {
        if (!Settings::gui().mControllerMenus)
            return;

        mControllerTooltipVisible = visible;
    }

    void WindowManager::setControllerTooltipEnabled(bool enabled)
    {
        if (!Settings::gui().mControllerMenus)
            return;

        mControllerTooltipEnabled = enabled;
        // When user toggles the setting, also update visibility
        mControllerTooltipVisible = enabled;
    }

    void WindowManager::restoreControllerTooltips()
    {
        // Restore tooltip visibility if user has them enabled but they were hidden by mouse movement
        if (mControllerTooltipEnabled && !mControllerTooltipVisible)
            setControllerTooltipVisible(true);
    }

    void WindowManager::updateControllerButtonsOverlay()
    {
        if (!Settings::gui().mControllerMenus || !mControllerButtonsOverlay)
            return;

        if (mWindows.empty())
            return;

        WindowBase* topWin = getActiveControllerWindow();
        if (!topWin || !topWin->isVisible())
        {
            mControllerButtonsOverlay->setVisible(false);
            return;
        }

        // setButtons will handle setting visibility based on if any buttons are defined.
        mControllerButtonsOverlay->setButtons(topWin->getControllerButtons());
    }

}
