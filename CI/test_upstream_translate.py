#!/usr/bin/env python3
"""Tests of the translation of upstream OpenMW files into OpenFallout names.

Every rule and every exception of CI/upstream_translate.py has a small sample here, taken from the real rename of
4 October 2026 (the sample says what the real tree has). A rule without a sample could be broken without anybody
noticing until a merge of upstream code goes wrong. The tests use no history, no `upstream` ref and no network, so they
run on a shallow checkout.
"""

import os
import subprocess
import tempfile
import unittest

import upstream_translate as t


def content(path, text):
    return t.translate_content(path, text.encode()).decode()


class PathTest(unittest.TestCase):
    def test_source_folders_moved(self):
        self.assertEqual(t.translate_path("apps/openmw/mwlua/types/actor.cpp"),
                         "apps/openfallout/mwlua/types/actor.cpp")
        self.assertEqual(t.translate_path("apps/openmw/engine.hpp"), "apps/openfallout/engine.hpp")
        self.assertEqual(t.translate_path("apps/openmw_tests/mwworld/testptr.cpp"),
                         "apps/openfallout_tests/mwworld/testptr.cpp")

    def test_a_new_file_in_a_renamed_folder_follows(self):
        self.assertEqual(t.translate_path("apps/openmw/mwnewthing/thing.cpp"), "apps/openfallout/mwnewthing/thing.cpp")
        self.assertEqual(t.translate_path("files/lua_api/openmw/newmodule.lua"),
                         "files/lua_api/openfallout/newmodule.lua")
        self.assertEqual(t.translate_path("files/data/l10n/OMWNewContext/en.yaml"),
                         "files/data/l10n/OFNewContext/en.yaml")
        self.assertEqual(t.translate_path("docs/source/reference/lua-scripting/openmw_newmodule.rst"),
                         "docs/source/reference/lua-scripting/openfallout_newmodule.rst")

    def test_lua_modules_and_pages(self):
        self.assertEqual(t.translate_path("files/data/openmw_aux/time.lua"), "files/data/openfallout_aux/time.lua")
        self.assertEqual(t.translate_path("files/data-mw/openmw_aux/calendarconfig.lua"),
                         "files/data-mw/openfallout_aux/calendarconfig.lua")
        self.assertEqual(t.translate_path("docs/source/reference/lua-scripting/openmw_aux_ui.rst"),
                         "docs/source/reference/lua-scripting/openfallout_aux_ui.rst")
        self.assertEqual(t.translate_path("files/data/l10n/OMWEngine/de.yaml"), "files/data/l10n/OFEngine/de.yaml")
        self.assertEqual(t.translate_path("files/data-mw/l10n/OMWEngine/gmst.yaml"),
                         "files/data-mw/l10n/OFEngine/gmst.yaml")

    def test_configuration_and_packaging_files(self):
        for old, new in (
            ("files/openmw.cfg", "files/openfallout.cfg"),
            ("files/openmw.cfg.local", "files/openfallout.cfg.local"),
            ("files/openmw-cs.cfg", "files/openfallout-cs.cfg"),
            ("scripts/data/morrowind_tests/openmw.cfg", "scripts/data/morrowind_tests/openfallout.cfg"),
            ("files/openmw.appdata.xml", "files/openfallout.appdata.xml"),
            ("files/org.openmw.cs.desktop", "files/org.openfallout.cs.desktop"),
            ("files/org.openmw.launcher.desktop", "files/org.openfallout.launcher.desktop"),
            ("files/windows/openmw.rc", "files/windows/openfallout.rc"),
            ("files/windows/openmw.exe.manifest", "files/windows/openfallout.exe.manifest"),
            ("files/mac/openmw-Info.plist.in", "files/mac/openfallout-Info.plist.in"),
            ("files/mac/openmw-cs-Info.plist.in", "files/mac/openfallout-cs-Info.plist.in"),
            ("cmake/OpenMWMacros.cmake", "cmake/OpenFalloutMacros.cmake"),
        ):
            with self.subTest(path=old):
                self.assertEqual(t.translate_path(old), new)

    def test_paths_the_rename_left_alone(self):
        for path in (
            "apps/opencs/main.cpp",
            "apps/mwiniimporter/main.cpp",
            "files/data/mygui/openmw_hud.layout",
            "files/launcher/images/openmw.png",
            "files/mac/openmw.icns",
            "files/windows/openmw.ico",
            "files/windows/openmw-wizard.rc",
            "files/data/scripts/omw/ui.lua",
            "files/data/builtin.omwscripts",
            "files/data/shaders/adjustments.omwfx",
            "files/data/textures/omw_menu_icon_active.dds",
            "docs/source/manuals/openmw-cs/index.rst",
            "docs/source/reference/lua-scripting/interface_mwui.rst",
            "docs/openmw-stage1.md",
            "CHANGELOG.md",
        ):
            with self.subTest(path=path):
                self.assertEqual(t.translate_path(path), path)


class NamesTest(unittest.TestCase):
    def test_namespaces(self):
        self.assertEqual(content("apps/openmw/mwworld/ptr.hpp",
                                 "namespace MWWorld\n{\n}\nMWBase::Environment::get()\n"),
                         "namespace OFWorld\n{\n}\nOFBase::Environment::get()\n")
        for name in ("Mechanics", "Gui", "Render", "Lua", "Physics", "Dialogue", "Sound", "State", "Class", "Input"):
            with self.subTest(name=name):
                self.assertEqual(content("x.cpp", "MW%s::f();" % name), "OF%s::f();" % name)

    def test_namespaces_in_comments_and_test_fixtures(self):
        self.assertEqual(content("x.hpp", "// see MWWorld::Ptr\n"), "// see OFWorld::Ptr\n")
        self.assertEqual(content("x.cpp", "TEST(MWWorldWeatherTest, a)\nstruct MWScriptTest : Test {};\n"),
                         "TEST(OFWorldWeatherTest, a)\nstruct OFScriptTest : Test {};\n")

    def test_names_that_only_begin_like_a_namespace_stay(self):
        self.assertEqual(content("x.cpp", "MWWorldish MWScriptRef CSMWorld::Data\n"),
                         "MWWorldish MWScriptRef CSMWorld::Data\n")

    def test_mwscript_is_renamed_only_as_a_namespace(self):
        self.assertEqual(content("x.cpp", "namespace MWScript\n{\n}\nMWScript::Compiler c;\n"),
                         "namespace OFScript\n{\n}\nOFScript::Compiler c;\n")
        # The language, the enum value, a Lua type and a comment keep the name.
        self.assertEqual(content("x.cpp", "type = MusicType::MWScript; // MWScript's SkipAnim\n"),
                         "type = MusicType::MWScript; // MWScript's SkipAnim\n")
        self.assertEqual(content("files/lua_api/openmw/world.lua", "-- @type MWScript\n"), "-- @type MWScript\n")

    def test_omw_names(self):
        self.assertEqual(content("apps/openmw/engine.hpp", "namespace OMW\n{\n}\nOMW::Engine e;\n"),
                         "namespace OF\n{\n}\nOF::Engine e;\n")
        self.assertEqual(content("x.lua", "l10n = 'OMWEngine'\nsendEvent('OMWConsolePrint')\n"),
                         "l10n = 'OFEngine'\nsendEvent('OFConsolePrint')\n")
        self.assertEqual(content("cmake/OpenMWMacros.cmake", "set(OMW_ADD_EXE_OPTIONS WIN32)\n"),
                         "set(OF_ADD_EXE_OPTIONS WIN32)\n")

    def test_omw_names_that_stay(self):
        for text in (
            "OMWFX Language", "OMWScripts", "OMWAddon", "storage.playerSection('OMWInputBindings')",
            "gl_FragColor = omw_FragColor; OMW_HDR OMW_Generated_Static", "SettingsOMWCombat", "omw_GetDepth",
        ):
            with self.subTest(text=text):
                self.assertEqual(content("x.lua", text), text)

    def test_upper_case_macros_and_options(self):
        self.assertEqual(content("CMakeLists.txt", "option(OPENMW_USE_SYSTEM_GLM \"x\" OFF)\n"),
                         "option(OPENFALLOUT_USE_SYSTEM_GLM \"x\" OFF)\n")
        self.assertEqual(content("x.hpp", "#ifndef OPENMW_COMPONENTS_X_H\n"), "#ifndef OPENFALLOUT_COMPONENTS_X_H\n")
        self.assertEqual(content("CI/x.sh", "-DOPENMW_LTO_BUILD=ON\n"), "-DOPENFALLOUT_LTO_BUILD=ON\n")

    def test_openmw_in_cmake_variables_and_includes(self):
        self.assertEqual(content("CMakeLists.txt",
                                 "include(OpenMWMacros)\n${OpenMW_BINARY_DIR}/${OpenMW_SOURCE_DIR}\n"),
                         "include(OpenFalloutMacros)\n${OpenFallout_BINARY_DIR}/${OpenFallout_SOURCE_DIR}\n")

    def test_the_word_openmw_is_renamed_but_not_names_that_contain_it(self):
        self.assertEqual(content("CMakeLists.txt", "# OpenMW-CS and OpenMW.\n"), "# OpenFallout-CS and OpenFallout.\n")
        for text in ("TestingOpenMW::outputDir()", "OpenMWs", "OpenMW_Tests_Failed", "Role_OpenMWCfg",
                     "OpenMWResourcePlugin"):
            with self.subTest(text=text):
                self.assertEqual(content("CMakeLists.txt", text), text)

    def test_lower_case_names(self):
        self.assertEqual(content("CMakeLists.txt", "add_executable(openmw-tests x)\nopenmw_add_executable(openmw)\n"),
                         "add_executable(openfallout-tests x)\nopenfallout_add_executable(openfallout)\n")
        self.assertEqual(content("scripts/x.sh", "openmwluadocumentor -f doc\n"), "openmwluadocumentor -f doc\n")

    def test_lua_modules(self):
        self.assertEqual(content("files/data/scripts/omw/ui.lua",
                                 "local ui = require('openmw.ui')\nrequire('openmw_aux.util')\n"),
                         "local ui = require('openfallout.ui')\nrequire('openfallout_aux.util')\n")

    def test_apps_paths_in_text(self):
        self.assertEqual(content("x.hpp", "#include <apps/openmw/mwbase/environment.hpp>\n"),
                         "#include <apps/openfallout/mwbase/environment.hpp>\n")
        self.assertEqual(content("x.hpp", "// does not depend on apps/openmw.\n"),
                         "// does not depend on apps/openfallout.\n")
        self.assertEqual(content("x.cpp", '#include "apps/openmw_tests/x.hpp"\n'),
                         '#include "apps/openfallout_tests/x.hpp"\n')
        # A link to OpenMW's own repository stays, and so do paths written with backslashes in a CMake source group.
        link = "[a](https://gitlab.com/OpenMW/openmw/-/blob/master/apps/openmw/mwlua/types/actor.cpp)\n"
        self.assertEqual(content("apps/openmw/mwlua/README.md", link), link)
        group = 'source_group ("apps\\\\openmw\\\\${dir}" FILES ${files})\n'
        self.assertEqual(content("cmake/OpenMWMacros.cmake", group), group)


class KeptNamesTest(unittest.TestCase):
    def test_versions_of_real_openmw_releases(self):
        self.assertEqual(content("x.cpp", 'release = "OpenMW 0.48.0";\n'), 'release = "OpenMW 0.48.0";\n')
        self.assertEqual(content("files/lang/launcher_de.ts", "behavior of OpenMW 0.48 and earlier\n"),
                         "behavior of OpenMW 0.48 and earlier\n")

    def test_web_addresses_and_packages_of_openmw(self):
        for text in (
            "see https://openmw.org/faq/ and https://openmw.readthedocs.io/en/latest/",
            "https://gitlab.com/OpenMW/openmw-deps/-/raw/main/windows/x",
            "WWW: http://openmw.sourceforge.net/",
            "add-apt-repository ppa:openmw/openmw",
            "return \"@OPENFALLOUT_DOC_BASEURL@openmw-@OPENFALLOUT_VERSION_MAJOR@/\";",
        ):
            with self.subTest(text=text):
                self.assertEqual(content("x.sh", text + "\n"), text + "\n")

    def test_artwork_and_widget_layouts(self):
        for text in (
            'WindowBase("openmw_hud.layout")', '<List file="openmw_text.skin.xml"/>', ":/images/openmw-plugin.png",
            'mResDir / "openmw.png"', "files/windows/openmw.ico", "files/windows/openmw-wizard.rc", "openmw_layers.xml",
            "manuals/openmw-cs/index.html", "openmw-game-template",
            "docs/source/manuals/installation/install-openmw.rst",
        ):
            with self.subTest(text=text):
                self.assertEqual(content("x.cpp", text + "\n"), text + "\n")

    def test_editor_icon_aliases(self):
        self.assertEqual(content("files/opencs/resources.qrc", '<file alias="openmw-cs">openmw-cs.png</file>\n'),
                         '<file alias="openmw-cs">openmw-cs.png</file>\n')
        self.assertEqual(content("files/opencs/run-openmw.svg", 'id="run-openmw"\n'), 'id="run-openmw"\n')
        self.assertEqual(content("files/opencs/editor-icons.svg", 'inkscape:label="stop-openmw"\n'),
                         'inkscape:label="stop-openmw"\n')
        self.assertEqual(content("apps/opencs/main.cpp", 'QIcon(":openmw-cs")\nQIcon(":run-openmw")\n'),
                         'QIcon(":openmw-cs")\nQIcon(":run-openmw")\n')

    def test_oics_markers_and_other_small_exceptions(self):
        self.assertEqual(content("extern/oics/x.cpp", "/* OPENMW CODE STARTS HERE\n OPENMW CODE ENDS HERE */\n"),
                         "/* OPENMW CODE STARTS HERE\n OPENMW CODE ENDS HERE */\n")
        self.assertEqual(content("x.cpp", 'AddTopic "OpenMW Unit Test"\n'), 'AddTopic "OpenMW Unit Test"\n')
        self.assertEqual(content("files/settings-default.cfg", "# If false, OpenMW-VR will disable x\n"),
                         "# If false, OpenMW-VR will disable x\n")
        self.assertEqual(content("docs/Doxyfile.cmake",
                                 "DOCSET_BUNDLE_ID       = org.openmw\nQHP_NAMESPACE = org.openmw\n"),
                         "DOCSET_BUNDLE_ID       = org.openmw\nQHP_NAMESPACE = org.openmw\n")
        self.assertEqual(content("scripts/integration_tests.py", "command = [openmw_binary, '--replace=config']\n"),
                         "command = [openmw_binary, '--replace=config']\n")

    def test_the_bug_tracker_link_moved_but_issue_links_stay(self):
        self.assertEqual(content("components/crashcatcher/crashcatcher.cpp",
                                 'std::string m = "report this to https://gitlab.com/OpenMW/openmw/issues !";\n'),
                         'std::string m = "report this to https://github.com/Elemperor1/OpenFallout/issues !";\n')
        self.assertEqual(content("apps/wizard/conclusionpage.cpp",
                                 '"<a href=\\"https://gitlab.com/OpenMW/openmw/issues\\">x</a>"\n'),
                         '"<a href=\\"https://github.com/Elemperor1/OpenFallout/issues\\">x</a>"\n')
        issue = "// Ref https://gitlab.com/OpenMW/openmw/-/issues/6013\n"
        self.assertEqual(content("apps/openmw/mwrender/screenshotmanager.cpp", issue), issue)


class CppTest(unittest.TestCase):
    def test_strings_are_renamed_and_comments_and_identifiers_are_not(self):
        self.assertEqual(content("apps/openmw/engine.cpp", 'mWindow = SDL_CreateWindow("OpenMW", 1);\n'),
                         'mWindow = SDL_CreateWindow("OpenFallout", 1);\n')
        self.assertEqual(content("x.cpp", "// currently OpenMW will never do this\n"),
                         "// currently OpenMW will never do this\n")
        self.assertEqual(content("x.cpp", "namespace OpenMW { }\nTEST(OpenMWConversion, a)\n"),
                         "namespace OpenMW { }\nTEST(OpenMWConversion, a)\n")
        self.assertEqual(content("x.cpp", "void openmw_gl4es_init(SDL_Window* w);\n"),
                         "void openmw_gl4es_init(SDL_Window* w);\n")

    def test_lower_case_names_in_strings(self):
        self.assertEqual(content("x.cpp", 'lua["openmw_core"] = x; addPackage("openmw.storage", y);\n'),
                         'lua["openfallout_core"] = x; addPackage("openfallout.storage", y);\n')

    def test_file_names_and_identifiers_that_were_renamed_everywhere(self):
        self.assertEqual(content("x.hpp", "// found in openmw.cfg\nstd::string_view openmwCfgFile = x;\n"),
                         "// found in openfallout.cfg\nstd::string_view openfalloutCfgFile = x;\n")
        self.assertEqual(content("x.cpp", "// reads openmw-cs.cfg and openmw.log\n"),
                         "// reads openfallout-cs.cfg and openfallout.log\n")

    def test_raw_strings_rename_names_and_not_prose(self):
        self.assertEqual(content("apps/essimporter/main.cpp",
                                 'desc(R"(Syntax: openmw-essimporter <options>\nsecond line)");\n'),
                         'desc(R"(Syntax: openfallout-essimporter <options>\nsecond line)");\n')
        self.assertEqual(content("apps/niftest/niftest.cpp", 'R"(Ensure that OpenMW can use the file)"\n'),
                         'R"(Ensure that OpenMW can use the file)"\n')
        self.assertEqual(content("x.cpp", "R\"X(local s = require('openmw.interfaces'))X\"\n"),
                         "R\"X(local s = require('openfallout.interfaces'))X\"\n")
        self.assertEqual(content("x.cpp", "R\"(require('openmw').name = 'abc')\"\n"),
                         "R\"(require('openmw').name = 'abc')\"\n")

    def test_test_scaffolding_keeps_a_made_up_program_name(self):
        for path in ("apps/openmw_tests/options.cpp", "components/testing/util.hpp"):
            with self.subTest(path=path):
                self.assertEqual(content(path, 'a{ "openmw", "--load-savegame" }; "/home/user/openmw/save.omwsave";\n'),
                                 'a{ "openmw", "--load-savegame" }; "/home/user/openmw/save.omwsave";\n')
        # Anywhere else the same literal is the program name.
        self.assertEqual(content("apps/launcher/maindialog.cpp", 'QLatin1String("openmw")\n'),
                         'QLatin1String("openfallout")\n')
        self.assertEqual(content("apps/openmw_tests/options.cpp", 'f("openmw.cfg");\n'), 'f("openfallout.cfg");\n')

    def test_an_unclosed_quote_does_not_swallow_the_file(self):
        self.assertEqual(content("x.cpp", "char c = '\"';\nconst char* s = \"OpenMW\";\n"),
                         "char c = '\"';\nconst char* s = \"OpenFallout\";\n")


class FileKindsTest(unittest.TestCase):
    def test_frozen_files_do_not_change(self):
        for path in ("CHANGELOG.md", "AUTHORS.md", "docs/openmw-stage1.md", "manual/opencs/main.tex",
                     "docs/requirements.txt"):
            with self.subTest(path=path):
                text = "Bug #1: Crash in MWPhysics::X for OpenMW\nopenmw.cfg OPENMW_X\n"
                self.assertEqual(content(path, text), text)

    def test_prose_files_rename_only_names(self):
        text = "OpenMW reads ``openmw.cfg``.\nMWGui is a namespace.\nrequire('openmw.ui') and OPENMW_X\n"
        self.assertEqual(content("docs/source/reference/lua-scripting/overview.rst", text),
                         "OpenMW reads ``openfallout.cfg``.\nMWGui is a namespace.\n"
                         "require('openfallout.ui') and OPENFALLOUT_X\n")
        self.assertEqual(content("docs/source/reference/lua-scripting/overview.rst",
                                 "`core.sendGlobalEvent <openmw_core.html##(sendGlobalEvent)>`_\n"),
                         "`core.sendGlobalEvent <openfallout_core.html##(sendGlobalEvent)>`_\n")
        self.assertEqual(content("docs/source/manuals/x.rst", "in ``~/.config/openmw/saves``\n"),
                         "in ``~/.config/openfallout/saves``\n")

    def test_the_documentation_build_files_say_openfallout(self):
        self.assertEqual(content("docs/source/conf.py", "project = u'OpenMW'\nset\\(OPENMW_VERSION_MAJOR (\\d+)\\)\n"),
                         "project = u'OpenFallout'\nset\\(OPENFALLOUT_VERSION_MAJOR (\\d+)\\)\n")
        self.assertEqual(content("docs/source/_ext/omw-lexers.py",
                                 "class OMWConfigLexer: filenames = ['openmw.cfg']\n"),
                         "class OMWConfigLexer: filenames = ['openfallout.cfg']\n")

    def test_the_lua_documentation_keeps_openmw_cfg_but_renames_modules(self):
        self.assertEqual(content("files/lua_api/openmw/core.lua",
                                 "-- from openmw.cfg; see @{openmw_util#util.bitOr}\n"),
                         "-- from openmw.cfg; see @{openfallout_util#util.bitOr}\n")
        self.assertEqual(content("files/lua_api/CMakeLists.txt", "    openmw/ambient.lua\n"),
                         "    openfallout/ambient.lua\n")

    def test_shaders_use_the_new_l10n_context(self):
        self.assertEqual(content("files/data/shaders/debug.omwfx",
                                 'header = "#{OMWShaders:DebugHeaderDepth}"; // OpenMW\n'),
                         'header = "#{OFShaders:DebugHeaderDepth}"; // OpenMW\n')

    def test_ci_files_rename_what_the_build_produces(self):
        self.assertEqual(content(".github/workflows/push.yml",
                                 "    - name: Run OpenMW tests\n      run: ./openmw-tests\n"),
                         "    - name: Run OpenFallout tests\n      run: ./openfallout-tests\n")
        self.assertEqual(content(".github/workflows/push.yml", "    - name: Add OpenMW PPA Dependencies\n"),
                         "    - name: Add OpenMW PPA Dependencies\n")
        self.assertEqual(content("CI/check_package.osx.sh", "F=('Applications' 'OpenMW-CS.app' 'OpenMW.app')\n"),
                         "F=('Applications' 'OpenFallout-CS.app' 'OpenFallout.app')\n")
        # Archives, dependency bundles and the upstream project path are not ours.
        keep = ("curl https://gitlab.com/OpenMW/openmw-deps/-/raw/main/a.zip -o ~/openmw-android-deps.zip\n"
                "project=\"${CI_PROJECT_PATH:-OpenMW/openmw}\"\necho \"Setting up OpenMW build...\"\n"
                "[openmw-deps]=\"libx\"\nadd-apt-repository -y ppa:openmw/openmw-daily\nopenmw-**\n")
        self.assertEqual(content("CI/before_install.sh", keep), keep)
        self.assertEqual(content(".gitlab-ci.yml", "BUILD_TARGETS: openmw openmw-tests\nOPENMW_CXX_FLAGS: ''\n"),
                         "BUILD_TARGETS: openfallout openfallout-tests\nOPENFALLOUT_CXX_FLAGS: ''\n")
        self.assertEqual(content("docker/Dockerfile.ubuntu",
                                 "apt-get install --no-install-recommends openmw openmw-launcher\n"),
                         "apt-get install --no-install-recommends openmw openmw-launcher\n")

    def test_prose_of_other_files_says_openfallout(self):
        self.assertEqual(content("apps/launcher/ui/mainwindow.ui", "<string>Welcome to OpenMW</string>\n"),
                         "<string>Welcome to OpenFallout</string>\n")
        self.assertEqual(content("files/lang/wizard_de.ts", "<source>The OpenMW Wizard</source>\n"),
                         "<source>The OpenFallout Wizard</source>\n")
        self.assertEqual(content("files/lang/launcher_sv.ts", "istället för OpenMWs, alternativt\n"),
                         "istället för OpenMWs, alternativt\n")


class RstTest(unittest.TestCase):
    def test_title_underlines_follow_the_longer_title(self):
        self.assertEqual(content("docs/source/reference/lua-scripting/openmw_core.rst",
                                 "Package openmw.core\n===================\n\ntext\n"),
                         "Package openfallout.core\n========================\n\ntext\n")
        # Upstream sometimes draws the underline longer than the title.
        self.assertEqual(content("docs/source/reference/lua-scripting/openmw_menu.rst",
                                 "Package openmw.menu\n======================\n"),
                         "Package openfallout.menu\n========================\n")

    def test_underlines_of_titles_that_did_not_change_stay(self):
        text = "Some title\n==========\n\nSecond\n----------------\n"
        self.assertEqual(content("docs/source/x.rst", text), text)

    def test_markdown_titles_too(self):
        self.assertEqual(content("README.md", "OpenMW\n======\n\nOpenMW is an engine.\n"),
                         "OpenFallout\n===========\n\nOpenFallout is an engine.\n")


class GeneralTest(unittest.TestCase):
    def test_binary_data_comes_back_unchanged(self):
        data = b"\x89PNG\r\n\x1a\n\0\0\0OpenMW openmw OPENMW MWWorld"
        self.assertEqual(t.translate_content("files/launcher/images/openmw.png", data), data)
        # The NUL byte only counts in the first 8000 bytes, like git's own test.
        late = b"OpenMW " * 2000 + b"\0"
        self.assertEqual(t.translate_content("x.txt", late), late.replace(b"OpenMW", b"OpenFallout"))

    def test_text_that_never_mentions_openmw_is_returned_as_it_is(self):
        text = b"int main() { return 0; }\n"
        self.assertIs(t.translate_content("x.cpp", text), text)

    def test_empty_file(self):
        self.assertEqual(t.translate_content("x.cpp", b""), b"")

    def test_translating_twice_is_the_same_as_once(self):
        samples = {
            "apps/openmw/engine.cpp": 'namespace OMW { MWWorld::Ptr p; }\nauto w = SDL_CreateWindow("OpenMW", 1);\n'
                                      "// OpenMW and openmw.cfg and apps/openmw/x\n"
                                      "OPENMW_X MWScript::A TestingOpenMW\n",
            "docs/source/reference/lua-scripting/openmw_core.rst": "Package openmw.core\n===================\n"
                                                                   "``openmw.cfg`` require('openmw.core')\n",
            "CI/x.sh": "echo OpenMW; ./openmw-tests; ppa:openmw/openmw; OpenMW-CS.app\n",
            "components/crashcatcher/crashcatcher.cpp": '"https://gitlab.com/OpenMW/openmw/issues"\n',
            "files/lua_api/openmw/core.lua": "-- openmw.cfg @{openmw_core#x} OMWEngine\n",
            "CMakeLists.txt": "${OpenMW_BINARY_DIR} include(OpenMWMacros) openmw-tests\n",
            "apps/openmw_tests/options.cpp": 'a{ "openmw", "x" }; R"(openmw-x)"; OpenMW\n',
            "CHANGELOG.md": "MWGui OpenMW\n",
        }
        for path, text in samples.items():
            with self.subTest(path=path):
                once = t.translate_content(path, text.encode())
                self.assertEqual(t.translate_content(path, once), once)

    def test_translation_is_fast_enough_for_a_whole_tree(self):
        line = '#include <apps/openmw/mwbase/world.hpp>\nnamespace MWWorld { void f() { log("OpenMW"); } }\n'
        text = (line * 2000).encode()
        for _ in range(20):
            t.translate_content("apps/openmw/mwworld/x.cpp", text)


class MeasureTest(unittest.TestCase):
    """--measure on a small repository made here, so that no history of ours is needed."""

    def git(self, *args):
        subprocess.run(("git", "-C", self.directory.name) + args, check=True, capture_output=True)

    def write(self, path, data):
        full = os.path.join(self.directory.name, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as stream:
            stream.write(data)

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.git("init", "-q")
        self.git("config", "user.email", "t@example.org")
        self.git("config", "user.name", "t")
        self.git("config", "commit.gpgsign", "false")
        self.write("apps/openmw/a.cpp", b"namespace MWWorld {}\nauto s = \"OpenMW\";\n")
        self.write("apps/openmw/b.cpp", b"namespace MWGui {}\n")
        self.write("apps/openmw/c.cpp", b"namespace MWGui {}\n")
        self.write("README.md", b"nothing\n")
        self.write("pic.png", b"\x89PNG\0\0OpenMW")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "base")
        self.git("tag", "base")
        self.git("mv", "apps/openmw", "apps/openfallout")
        self.write("apps/openfallout/a.cpp", b"namespace OFWorld {}\nauto s = \"OpenFallout\";\n")
        self.write("apps/openfallout/b.cpp", b"namespace OFGui {}\n")
        self.write("apps/openfallout/c.cpp", b"namespace OFGui { /* hand-made edit */ }\n")
        self.write("new.cpp", b"int x;\n")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "renamed")
        self.git("tag", "renamed")

    def test_counts_and_list(self):
        import contextlib
        import io
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            differing = t.measure("base", "renamed", True, repo=self.directory.name)
        self.assertEqual(differing, [("apps/openmw/c.cpp", "apps/openfallout/c.cpp")])
        text = out.getvalue()
        self.assertIn("files in base: 5", text)
        self.assertIn("translated path exists in renamed: 5", text)
        self.assertIn("byte-identical after translation: 4", text)
        self.assertIn("different: 1", text)
        self.assertIn("apps/openmw/c.cpp -> apps/openfallout/c.cpp", text)
        self.assertIn("translated: namespace OFGui {}", text)
        self.assertIn("renamed:    namespace OFGui { /* hand-made edit */ }", text)

    def test_command_line(self):
        result = subprocess.run(
            ["python3", os.path.join(os.path.dirname(os.path.abspath(__file__)), "upstream_translate.py"),
             "--measure", "base", "renamed"],
            cwd=self.directory.name, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("different: 1", result.stdout)
        self.assertNotIn("c.cpp", result.stdout)  # without --list there are no file names

    def test_usage(self):
        result = subprocess.run(
            ["python3", os.path.join(os.path.dirname(os.path.abspath(__file__)), "upstream_translate.py")],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage", result.stderr)


if __name__ == "__main__":
    unittest.main()
