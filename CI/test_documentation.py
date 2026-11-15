"""Tests for the narrow documentation entrypoint link checker."""

from pathlib import Path
import tempfile
import unittest

import check_documentation


class DocumentationLinkTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "docs" / "guide").mkdir(parents=True)
        (self.root / "docs" / "source").mkdir(parents=True)
        (self.root / "docs" / "guide" / "topic.md").touch()
        (self.root / "docs" / "source" / "reference.rst").touch()

    def test_markdown_checks_relative_files_and_ignores_external_and_anchor_links(self):
        text = "[topic](topic.md#intro) [external](https://example.com) [self](#top)"
        self.assertEqual(
            check_documentation.check_markdown_links(
                text, Path("docs/guide/index.md"), self.root),
            [],
        )

    def test_markdown_reports_missing_relative_target(self):
        errors = check_documentation.check_markdown_links(
            "[missing](missing.md)", Path("docs/guide/index.md"), self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("missing link target missing.md", errors[0])

    def test_repository_rst_links_require_canonical_existing_target(self):
        text = (
            "`topic <https://github.com/OpenBethesda/OpenFallout/blob/master/"
            "docs/guide/topic.md>`_\n"
            "`missing <https://github.com/OpenBethesda/OpenFallout/blob/master/"
            "docs/guide/missing.md>`_\n"
            "`wrong branch <https://github.com/OpenBethesda/OpenFallout/blob/main/"
            "docs/guide/topic.md>`_"
        )
        errors = check_documentation.check_repo_blob_links(
            text, Path("docs/source/openfallout.rst"), self.root)
        self.assertEqual(len(errors), 2)
        self.assertIn("missing repository link target", errors[0])
        self.assertIn("non-canonical repository link", errors[1])

    def test_repository_rst_links_to_the_former_owner_are_not_canonical(self):
        text = "`topic <https://github.com/Elemperor1/OpenFallout/blob/master/docs/guide/topic.md>`_"
        errors = check_documentation.check_repo_blob_links(
            text, Path("docs/source/openfallout.rst"), self.root)
        self.assertEqual(len(errors), 1)
        self.assertIn("non-canonical repository link", errors[0])

    def test_sphinx_doc_roles_and_toctree_resolve_rst_files(self):
        text = """See :doc:`Reference <reference>`.

.. dropdown:: Contents

   .. toctree::
      :maxdepth: 2

      reference
"""
        self.assertEqual(
            check_documentation.check_sphinx_targets(
                text, Path("docs/source/index.rst"), self.root),
            [],
        )

    def test_sphinx_reports_missing_doc_role_and_toctree_entries(self):
        text = """See :doc:`missing`.

.. dropdown:: Contents

   .. toctree::

      absent
"""
        errors = check_documentation.check_sphinx_targets(
            text, Path("docs/source/index.rst"), self.root)
        self.assertEqual(len(errors), 2)
        self.assertTrue(all("missing Sphinx document" in error for error in errors))


SHELL_PAGE = """# The interface

## Command-line options

| option | what it does |
|---|---|
| `--load-savegame <file>` | load it |
| `--missing-content <answer>` | answer |

## Lua: the menu package

| function | arguments | result |
|---|---|---|
| `loadGame(directory, slotName, options)` | the save | nothing |
| `STATE` | (a table) | states |

## Lua: engine handlers of menu scripts

| handler | when |
|---|---|
| `onLoadRefused(info)` | refused |

## Outcomes: log lines and exit status

| line | means |
|---|---|
| `Load: refused` | not a name |
"""

SHELL_TEST = """
    constexpr std::string_view keptOptions[] = {
        "load-savegame",
        "missing-content",
    };
    constexpr std::string_view keptMenuFunctions[] = {
        "loadGame",
    };
    const sol::table states = mMenu["STATE"];
"""

SHELL_HANDLERS = """
        EngineHandlerList mLoadRefused{ "onLoadRefused" };
"""


class ShellInterfaceTest(unittest.TestCase):
    def check(self, page=SHELL_PAGE, test=SHELL_TEST, handlers=SHELL_HANDLERS):
        return check_documentation.check_shell_interface(page, test, handlers)

    def test_a_page_whose_names_the_test_checks_passes(self):
        self.assertEqual(self.check(), [])

    def test_the_names_of_the_page_are_read_by_section(self):
        names = check_documentation.shell_interface_names(SHELL_PAGE)
        self.assertEqual(names["options"], ["load-savegame", "missing-content"])
        self.assertEqual(names["functions"], ["loadGame", "STATE"])
        self.assertEqual(names["handlers"], ["onLoadRefused"])

    def test_an_option_of_the_page_that_the_test_does_not_check_fails(self):
        page = SHELL_PAGE.replace("| `--missing-content <answer>` | answer |",
                                  "| `--missing-content <answer>` | answer |\n| `--headless` | hidden |")
        errors = self.check(page=page)
        self.assertEqual(len(errors), 1)
        self.assertIn("--headless", errors[0])

    def test_a_function_that_the_test_checks_but_the_page_does_not_list_fails(self):
        test = SHELL_TEST.replace('"loadGame",', '"loadGame",\n        "quit",')
        errors = self.check(test=test)
        self.assertEqual(len(errors), 1)
        self.assertIn("quit", errors[0])

    def test_a_renamed_handler_fails(self):
        errors = self.check(handlers=SHELL_HANDLERS.replace("onLoadRefused", "onLoadDenied"))
        self.assertEqual(len(errors), 1)
        self.assertIn("onLoadRefused", errors[0])

    def test_the_real_page_matches_the_real_test(self):
        root = check_documentation.ROOT
        self.assertEqual(check_documentation.check_shell_interface(
            (root / check_documentation.SHELL_INTERFACE_PAGE).read_text(encoding="utf-8"),
            (root / check_documentation.SHELL_INTERFACE_TEST).read_text(encoding="utf-8"),
            (root / check_documentation.SHELL_INTERFACE_HANDLERS).read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
