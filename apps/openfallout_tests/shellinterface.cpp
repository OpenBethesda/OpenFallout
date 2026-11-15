// The interface that another program (the shell) drives the engine with: the command-line options and the functions of
// the Lua menu package listed on docs/openfallout/shell-interface.md. These tests fail when one of them disappears or
// is renamed, so that the page's promise ("not changed without notice") is held by a machine. A change here needs an
// entry on that page and in the release notes.

#include "apps/openfallout/mwlua/context.hpp"
#include "apps/openfallout/mwlua/menuscripts.hpp"
#include "apps/openfallout/options.hpp"

#include <components/esm3/missingcontent.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/lua/luastate.hpp>

#include <gtest/gtest.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    namespace bpo = boost::program_options;

    // The command-line options of the page (keep the two lists the same)
    constexpr std::string_view keptOptions[] = {
        "config",
        "replace",
        "user-data",
        "resources",
        "data",
        "data-local",
        "content",
        "load-savegame",
        "skip-menu",
        "new-game",
        "headless",
        "no-grab",
        "no-sound",
        "missing-content",
    };

    // The functions and fields of the Lua menu package (openfallout.menu) on the page
    constexpr std::string_view keptMenuFunctions[] = {
        "getState",
        "newGame",
        "loadGame",
        "saveGame",
        "deleteGame",
        "getSaves",
        "getAllSaves",
        "getCurrentSaveDir",
        "quit",
    };

    template <class T>
    bpo::variables_map parse(const T& arguments)
    {
        bpo::options_description description = OpenMW::makeOptionsDescription();
        bpo::variables_map variables;
        Files::parseArgs(static_cast<int>(arguments.size()), arguments.data(), variables, description);
        return variables;
    }

    TEST(OpenFalloutShellInterfaceTest, everyKeptOptionExists)
    {
        const bpo::options_description description = OpenMW::makeOptionsDescription();
        for (const std::string_view name : keptOptions)
            EXPECT_NE(description.find_nothrow(std::string(name), false), nullptr) << "--" << name;
    }

    TEST(OpenFalloutShellInterfaceTest, theMissingContentAnswerIsAskWhenNotGiven)
    {
        const std::array arguments{ "openfallout" };
        EXPECT_EQ(OpenMW::readMissingContentAnswer(parse(arguments)), ESM::MissingContentAnswer::Ask);
    }

    TEST(OpenFalloutShellInterfaceTest, theMissingContentAnswerIsRead)
    {
        const std::array accept{ "openfallout", "--missing-content=accept" };
        EXPECT_EQ(OpenMW::readMissingContentAnswer(parse(accept)), ESM::MissingContentAnswer::Accept);
        const std::array refuse{ "openfallout", "--missing-content", "refuse" };
        EXPECT_EQ(OpenMW::readMissingContentAnswer(parse(refuse)), ESM::MissingContentAnswer::Refuse);
        const std::array ask{ "openfallout", "--missing-content=ask" };
        EXPECT_EQ(OpenMW::readMissingContentAnswer(parse(ask)), ESM::MissingContentAnswer::Ask);
    }

    // A word that is not an answer stops the engine (exit status 1) instead of being taken for one
    TEST(OpenFalloutShellInterfaceTest, aWrongMissingContentAnswerIsAnError)
    {
        const std::array arguments{ "openfallout", "--missing-content=yes" };
        const bpo::variables_map variables = parse(arguments);
        EXPECT_THROW(OpenMW::readMissingContentAnswer(variables), std::runtime_error);
    }

    class OpenFalloutShellMenuTest : public testing::Test
    {
    protected:
        LuaUtil::LuaState mState{ nullptr, nullptr };
        OFLua::Context mContext{ OFLua::Context::Menu, nullptr, &mState };
        sol::table mMenu = OFLua::initMenuPackage(mContext);
    };

    TEST_F(OpenFalloutShellMenuTest, everyKeptFunctionExists)
    {
        for (const std::string_view name : keptMenuFunctions)
            EXPECT_EQ(mMenu[std::string(name)].get_type(), sol::type::function) << "menu." << name;
    }

    TEST_F(OpenFalloutShellMenuTest, theStatesAreKept)
    {
        const sol::table states = mMenu["STATE"];
        for (const std::string_view name : { "NoGame", "Running", "Ended" })
            EXPECT_EQ(states[std::string(name)].get_type(), sol::type::number) << "menu.STATE." << name;
    }

    // The answer of one load is checked before anything is looked up, so a wrong word is the caller's error, not a load
    TEST_F(OpenFalloutShellMenuTest, loadGameRefusesAWrongMissingContentAnswer)
    {
        sol::state_view lua = mState.unsafeState();
        lua["menu"] = mMenu;
        const sol::protected_function_result result = lua.safe_script(
            "return menu.loadGame('dir', 'slot', { missingContent = 'maybe' })", sol::script_pass_on_error);
        ASSERT_FALSE(result.valid());
        const sol::error error = result;
        EXPECT_NE(std::string(error.what()).find("maybe"), std::string::npos) << error.what();
    }
}
