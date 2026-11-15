#!/usr/bin/env python3
"""The game's Lua scripts run on Lua 5.1 (LuaJIT) inside the engine's sandbox, which adds `table.unpack` and nothing
else of the later versions. A call of a library function that only a later Lua has is nil there, and the script fails
the first time the line runs. The unit tests do not run the scripts, so this test reads them."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "files" / "data" / "scripts"

# library functions of Lua 5.2 and later that the sandbox does not give (components/lua/luastate.cpp)
MISSING = ("table.pack", "table.move", "math.tointeger", "math.type", "math.ult", "string.pack", "string.unpack",
           "string.packsize", "coroutine.isyieldable", "coroutine.close")
CALL = re.compile(r"(?<![\w.])(" + "|".join(re.escape(name) for name in MISSING) + r")\b")
COMMENT = re.compile(r"--.*$")


def missing_calls(text):
    """(line number, name) for each use of a function the sandbox does not have; comments are not read."""
    return [(number, match.group(1))
            for number, line in enumerate(text.splitlines(), start=1)
            for match in CALL.finditer(COMMENT.sub("", line))]


class SandboxNamesTest(unittest.TestCase):
    def test_a_call_is_found_and_a_comment_or_a_longer_name_is_not(self):
        self.assertEqual(missing_calls("local t = table.pack(1, 2)\n-- table.pack(1)\nlocal u = mytable.pack(1)\n"
                                       "local v = table.packed\nlocal w = table.unpack(t)\n"),
                         [(1, "table.pack")])

    def test_the_scripts_of_the_game_call_nothing_the_sandbox_lacks(self):
        self.assertTrue(SCRIPTS.is_dir())
        found = [f"{path.relative_to(ROOT).as_posix()}:{number}: {name}"
                 for path in sorted(SCRIPTS.rglob("*.lua"))
                 for number, name in missing_calls(path.read_text(encoding="utf-8"))]
        self.assertEqual(found, [])


if __name__ == "__main__":
    unittest.main()
