"""Tests of the rule that keeps Apple code out of the core.

Each test builds a small temporary tree; the real tree is never read.
"""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import check_core_edges


class CoreEdgesTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def write(self, path, text):
        full = self.root / path
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_text(text)

    def violations(self):
        return check_core_edges.check_tree(self.root)

    def run_main(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            status = check_core_edges.main(["--root", str(self.root), *args])
        return status, out.getvalue().splitlines()

    def test_clean_core_file_passes(self):
        self.write("components/esm4/reader.cpp",
                   '#include <vector>\n#include "reader.hpp"\nint f() { return 1; }\n')
        self.assertEqual(self.violations(), [])

    def test_apple_headers_fail(self):
        for header in ("CoreFoundation/CoreFoundation.h", "mach-o/dyld.h", "pthread/qos.h",
                       "TargetConditionals.h", "libproc.h", "sys/sysctl.h", "mach_time.h",
                       "AppKit/AppKit.h", "Metal/Metal.h"):
            with self.subTest(header=header):
                self.write("components/esm/a.cpp", f"#include <{header}>\n")
                self.assertEqual(len(self.violations()), 1)

    def test_import_and_quoted_include_fail(self):
        self.write("components/esm/a.hpp", '#  import "Foundation/Foundation.h"\n')
        self.assertEqual(len(self.violations()), 1)

    def test_plain_posix_and_library_headers_pass(self):
        self.write("components/esm/a.cpp",
                   "#include <pthread.h>\n#include <sys/stat.h>\n#include <AL/al.h>\n"
                   "#include <QtCore/QString>\n#include <osg/Node>\n")
        self.assertEqual(self.violations(), [])

    def test_apple_macros_fail(self):
        for macro in ("__APPLE__", "__MACH__", "TARGET_OS_MAC", "__OBJC__", "Q_OS_MACOS",
                      "Q_OS_DARWIN", "MAC_OS_X_VERSION_MIN_REQUIRED", "__apple_build_version__"):
            with self.subTest(macro=macro):
                self.write("apps/openfallout/mwworld/a.cpp", f"#ifdef {macro}\nint x;\n#endif\n")
                found = self.violations()
                self.assertEqual(len(found), 1)
                self.assertEqual(found[0][1], 1)

    def test_a_longer_name_is_not_a_macro_hit(self):
        self.write("components/esm/a.cpp", "int MY__APPLE__X = 1; int Q_OS_MACHINE = 2;\n")
        self.assertEqual(self.violations(), [])

    def test_runtime_names_that_reach_appkit_fail(self):
        for word in ("libobjc.A.dylib", "objc_msgSend", "objc_getClass", "sel_registerName",
                     "NSApplication"):
            with self.subTest(word=word):
                self.write("components/esm/a.cpp", f'const char* n = "{word}";\n')
                self.assertEqual(len(self.violations()), 1)

    def test_objective_c_files_fail_even_when_empty(self):
        for name in ("a.mm", "b.m"):
            with self.subTest(name=name):
                self.write(f"components/{name}/{name}", "")
                found = [v for v in self.violations() if v[0] == f"components/{name}/{name}"]
                self.assertEqual(len(found), 1)
                self.assertIn("Objective-C", found[0][2])

    def test_edge_files_may_use_apple_code(self):
        self.write("components/sdlutil/a.mm", "#import <AppKit/AppKit.h>\n")
        self.write("components/crashcatcher/c.cpp", "#include <libproc.h>\n#ifdef __APPLE__\n#endif\n")
        self.write("apps/openfallout/main.cpp", 'const char* n = "NSApplication";\n')
        self.assertEqual(self.violations(), [])

    def test_a_file_beside_an_edge_is_still_core(self):
        self.write("components/misc/other.cpp", "#ifdef __APPLE__\n#endif\n")
        self.assertEqual(len(self.violations()), 1)

    def test_extern_is_skipped(self):
        self.write("extern/foo/a.cpp", "#ifdef __APPLE__\n#endif\n")
        self.assertEqual(self.violations(), [])

    def test_only_apps_and_components_are_read(self):
        self.write("scripts/a.cpp", "#ifdef __APPLE__\n#endif\n")
        self.write("components/esm/notes.txt", "__APPLE__\n")
        self.assertEqual(self.violations(), [])

    def test_template_source_files_are_read(self):
        self.write("components/misc/a.cpp.in", "#if defined(__APPLE__)\n#endif\n")
        self.assertEqual(len(self.violations()), 1)

    def test_a_name_in_a_comment_does_not_fail(self):
        self.write("components/esm/a.cpp",
                   "// #ifdef __APPLE__\n"
                   "/* #include <mach-o/dyld.h>\n   __MACH__ */\n"
                   "int x; // NSApplication\n")
        self.assertEqual(self.violations(), [])

    def test_comment_marks_inside_a_string_do_not_hide_code(self):
        self.write("components/esm/a.cpp", 'const char* u = "http://x"; int a = __APPLE__;\n')
        self.assertEqual(len(self.violations()), 1)

    def test_a_name_inside_if_0_does_not_fail(self):
        self.write("components/esm/a.cpp",
                   "#if 0\n#ifdef __APPLE__\n#include <mach-o/dyld.h>\n#endif\n#endif\n"
                   "#ifdef __APPLE__\n#endif\n")
        found = self.violations()
        self.assertEqual([v[1] for v in found], [6])

    def test_if_0_with_else_reads_the_else_branch(self):
        self.write("components/esm/a.cpp", "#if 0\nint a;\n#else\n#ifdef __APPLE__\n#endif\n#endif\n")
        self.assertEqual([v[1] for v in self.violations()], [4])

    def test_line_numbers_count_comment_lines(self):
        self.write("components/esm/a.cpp", "/* one\n two */\nint x = __APPLE__;\n")
        self.assertEqual([v[1] for v in self.violations()], [3])

    def test_one_line_per_violating_line(self):
        self.write("components/esm/a.cpp", "#if defined(__APPLE__) || defined(__MACH__)\n#endif\n")
        self.assertEqual(len(self.violations()), 1)

    def test_main_prints_lines_and_a_count_and_fails(self):
        self.write("components/esm/a.cpp", "\n#ifdef __APPLE__\n#endif\n")
        status, lines = self.run_main()
        self.assertEqual(status, 1)
        self.assertEqual(lines[0].split(": ")[0], "components/esm/a.cpp:2")
        self.assertIn("__APPLE__", lines[0])
        self.assertEqual(lines[-1], "1 violation")

    def test_main_passes_on_a_clean_tree(self):
        self.write("components/esm/a.cpp", "int x;\n")
        status, lines = self.run_main()
        self.assertEqual(status, 0)
        self.assertEqual(lines, ["0 violations"])

    def test_list_edges_prints_the_table(self):
        status, lines = self.run_main("--list-edges")
        self.assertEqual(status, 0)
        self.assertTrue(any(line.startswith("components/sdlutil/") for line in lines))
        self.assertEqual(len(lines), len(check_core_edges.EDGES))

    def test_every_edge_has_a_reason(self):
        for prefix, reason in check_core_edges.EDGES:
            self.assertTrue(reason.strip(), prefix)


if __name__ == "__main__":
    unittest.main()
