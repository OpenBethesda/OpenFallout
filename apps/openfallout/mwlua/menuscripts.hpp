#ifndef MWLUA_MENUSCRIPTS_H
#define MWLUA_MENUSCRIPTS_H

#include <SDL_events.h>

#include <string>
#include <string_view>
#include <vector>

#include <components/lua/luastate.hpp>
#include <components/lua/scriptscontainer.hpp>
#include <components/sdlutil/events.hpp>

#include "../mwbase/luamanager.hpp"

#include "inputprocessor.hpp"

namespace OFLua
{
    struct Context;

    sol::table initMenuPackage(const Context& context);

    class MenuScripts : public LuaUtil::ScriptsContainer
    {
    public:
        MenuScripts(LuaUtil::LuaState* lua)
            : LuaUtil::ScriptsContainer(lua, "Menu")
            , mInputProcessor(this)
        {
            registerEngineHandlers({ &mOnFrameHandlers, &mStateChanged, &mConsoleCommandHandlers,
                &mViewportResizedHandlers, &mUiModeChanged, &mLoadRefused });
        }

        void processInputEvent(const OFBase::LuaManager::InputEvent& event)
        {
            mInputProcessor.processInputEvent(event);
        }

        void onFrame(float dt) { callEngineHandlers(mOnFrameHandlers, dt); }

        void stateChanged() { callEngineHandlers(mStateChanged); }

        // onLoadRefused({ directory, slotName, missingContent }): the answer given in advance refused the save
        void loadRefused(
            std::string_view directory, std::string_view slotName, const std::vector<std::string_view>& missingContent)
        {
            sol::table info(mLua.unsafeState(), sol::create);
            info["directory"] = std::string(directory);
            info["slotName"] = std::string(slotName);
            sol::table files(mLua.unsafeState(), sol::create);
            for (const std::string_view file : missingContent)
                files.add(std::string(file));
            info["missingContent"] = files;
            callEngineHandlers(mLoadRefused, info);
        }

        bool consoleCommand(const std::string& consoleMode, const std::string& command)
        {
            callEngineHandlers(mConsoleCommandHandlers, consoleMode, command);
            return !mConsoleCommandHandlers.mList.empty();
        }

        void onViewportResized(int width, int height) { callEngineHandlers(mViewportResizedHandlers, width, height); }

        void uiModeChanged() { callEngineHandlers(mUiModeChanged); }

    private:
        friend class OFLua::InputProcessor<MenuScripts>;
        OFLua::InputProcessor<MenuScripts> mInputProcessor;
        EngineHandlerList mOnFrameHandlers{ "onFrame" };
        EngineHandlerList mStateChanged{ "onStateChanged" };
        EngineHandlerList mConsoleCommandHandlers{ "onConsoleCommand" };
        EngineHandlerList mViewportResizedHandlers{ "onViewportResized" };
        EngineHandlerList mUiModeChanged{ "_onUiModeChanged" };
        EngineHandlerList mLoadRefused{ "onLoadRefused" };
    };
}

#endif // MWLUA_GLOBALSCRIPTS_H
