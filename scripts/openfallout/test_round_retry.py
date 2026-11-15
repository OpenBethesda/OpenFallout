#!/usr/bin/env python3
"""Tests of the menu script that asks again for a refused load (round/scripts/roundretry.lua) and of the round that uses it
(round/rounds/m1-refused-retry.json). The script runs with lupa (those tests are skipped where it is not installed) against a
stand-in of the engine's menu package: it asks for the newest save with the answer `refuse`, and when the engine refuses it,
asks for the same save once more with `accept`, never a third time."""
import json
import re
import unittest
from pathlib import Path

# The engine runs LuaJIT: its Lua 5.1 is the next best (a later Lua makes the variables of a loop constant)
try:
    from lupa.luajit21 import LuaRuntime
except ImportError:  # pragma: no cover
    try:
        from lupa.lua51 import LuaRuntime
    except ImportError:
        try:
            from lupa import LuaRuntime
        except ImportError:
            LuaRuntime = None

ROUND = Path(__file__).resolve().parent / "round"
SCRIPT = ROUND / "scripts" / "roundretry.lua"
ROUND_FILE = ROUND / "rounds" / "m1-refused-retry.json"

PRELUDE = """
clock = 0
printed = {}
loads = {}
state = 0
menu_open = true
saves = {
    ['Courier'] = {
        ['Old Save.omwsave'] = { description = 'Old', creationTime = 100 },
        ['New Save.omwsave'] = { description = 'New', creationTime = 300 },
    },
    ['Other'] = { ['Middle.omwsave'] = { description = 'Mid', creationTime = 200 } },
}
fail_load = false
print = function(...)
    local parts = {}
    for i = 1, select('#', ...) do parts[#parts + 1] = tostring((select(i, ...))) end
    printed[#printed + 1] = table.concat(parts, '\\t')
end
package.preload['openfallout.core'] = function()
    return { getRealTime = function() return clock end }
end
package.preload['openfallout.menu'] = function()
    return {
        STATE = { NoGame = 0, Running = 1, Ended = 2 },
        getState = function() return state end,
        mainMenuOpen = function() return menu_open end,
        getAllSaves = function() return saves end,
        loadGame = function(dir, slot, options)
            if fail_load then error('Save game slot not found: ' .. dir .. '/' .. slot) end
            loads[#loads + 1] = { dir = dir, slot = slot, answer = options and options.missingContent }
        end,
    }
end
"""


@unittest.skipIf(LuaRuntime is None, "lupa is not installed")
class RetryScript(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute(PRELUDE)
        self.script = self.lua.execute(SCRIPT.read_text(encoding="utf-8"))
        self.g = self.lua.globals()

    def frame(self, seconds=0.5):
        self.g.clock = self.g.clock + seconds
        self.script.engineHandlers.onFrame(0)

    def frames(self, count, seconds=0.5):
        for _ in range(count):
            self.frame(seconds)

    def loads(self):
        return [(entry.dir, entry.slot, entry.answer) for entry in self.g.loads.values()]

    def printed(self):
        return list(self.g.printed.values())

    def refuse(self, directory="Courier", slot="New Save.omwsave", files=("play.omwscripts",)):
        info = self.lua.table_from({"directory": directory, "slotName": slot,
                                    "missingContent": self.lua.table_from(list(files))})
        self.script.engineHandlers.onLoadRefused(info)

    def test_it_asks_for_the_newest_save_with_refuse_once_the_main_menu_has_been_up_a_moment(self):
        self.frame()
        self.assertEqual(self.loads(), [])           # not on the first frame of the menu
        self.frames(10)
        self.assertEqual(self.loads(), [("Courier", "New Save.omwsave", "refuse")])
        self.frames(20)
        self.assertEqual(len(self.loads()), 1)       # once

    def test_it_waits_while_a_game_runs_or_the_menu_is_not_up(self):
        self.g.state = 1
        self.frames(20)
        self.g.state = 0
        self.g.menu_open = False
        self.frames(20)
        self.assertEqual(self.loads(), [])
        self.g.menu_open = True
        self.frames(10)
        self.assertEqual(len(self.loads()), 1)

    def test_a_refusal_is_answered_by_asking_for_the_same_save_with_accept_once(self):
        self.frames(10)
        self.refuse()
        self.assertEqual(self.loads(), [("Courier", "New Save.omwsave", "refuse"), ("Courier", "New Save.omwsave", "accept")])
        lines = [line for line in self.printed() if line.startswith("ROUND\tretry\trefused")]
        self.assertEqual(len(lines), 1)
        self.assertIn("same=true", lines[0])
        self.assertIn("missing=1", lines[0])
        # a second refusal is not answered again: that would be a loop
        self.refuse()
        self.assertEqual(len(self.loads()), 2)
        self.assertTrue(any(line.startswith("ROUND\terror\tretry") for line in self.printed()))

    def test_a_refusal_it_did_not_ask_for_is_left_alone(self):
        self.refuse()
        self.assertEqual(self.loads(), [])

    def test_no_save_is_an_error_of_the_round(self):
        self.g.saves = self.lua.table()
        self.frames(10)
        self.assertEqual(self.loads(), [])
        self.assertTrue(any(line.startswith("ROUND\terror\tretry") for line in self.printed()))

    def test_a_load_that_cannot_be_asked_for_is_an_error_that_names_nothing(self):
        self.g.fail_load = True
        self.frames(10)
        errors = [line for line in self.printed() if line.startswith("ROUND\terror")]
        self.assertEqual(len(errors), 1)
        self.assertNotIn("Courier", errors[0])

    def test_its_lines_hold_counts_and_booleans_and_no_name_of_the_game(self):
        self.frames(10)
        self.refuse()
        self.refuse()
        text = "\n".join(self.printed())
        self.assertTrue(text)
        for name in ("Courier", "Save", "New", "Old", "Other", "play"):
            self.assertNotIn(name, text)

    def test_every_line_is_a_round_line(self):
        self.frames(10)
        self.refuse()
        self.assertTrue(all(line.startswith("ROUND\t") for line in self.printed()))


class RetryRound(unittest.TestCase):
    """The round file names what the runner and the engine need, with no path of one computer."""

    def setUp(self):
        self.config = json.loads(ROUND_FILE.read_text(encoding="utf-8"))

    def test_it_starts_at_the_main_menu_with_the_newest_new_vegas_save(self):
        self.assertEqual((self.config["game"], self.config["save"], self.config["load_save"]), ("nv", "latest", False))
        self.assertTrue((ROUND_FILE.parent / self.config["scenario"]).exists())

    def test_the_menu_script_is_added_from_the_folder_of_the_runner(self):
        args = self.config["extra_args"]
        content = args[args.index("--content") + 1]
        # the runner gives the engine its own folder with --data, so the file is found by name
        scripts = (ROUND / content).read_text(encoding="utf-8")
        self.assertIn("MENU: scripts/roundretry.lua", scripts)
        self.assertTrue(SCRIPT.exists())
        for arg in args:
            self.assertFalse(Path(arg).is_absolute(), arg)

    def test_it_ends_well_under_two_minutes_when_the_retry_is_lost(self):
        # the runner stops an engine `limit` + 30 seconds after the start
        self.assertLessEqual(self.config["limit"] + 30, 90)

    def test_it_asks_for_one_refusal_then_the_load_and_never_a_box(self):
        counts = self.config["expect_count"]
        self.assertEqual(sorted(counts.values()), [1, 1, 1, 1])
        log = "\n".join([
            "[1 I] Load: 1 content file(s) of the save are not loaded, not loading it (missing-content refuse)",
            "[2 I] [menu] ROUND\tretry\trefused\tmissing=1\tsame=true\tasking=accept",
            "[3 I] Load: 1 content file(s) of the save are not loaded, loading it anyway (missing-content accept)",
            "[4 I] Loading saved game 'x' for character 'y'",
        ])
        for pattern, count in counts.items():
            self.assertEqual(sum(1 for line in log.splitlines() if re.search(pattern, line)), count, pattern)
        for pattern in self.config["expect_log"]:
            self.assertTrue(re.search(pattern, log, re.MULTILINE), pattern)
        for pattern in self.config["forbid_log"]:
            self.assertFalse(re.search(pattern, log, re.MULTILINE), pattern)
        # the order matters: a load before the refusal is not a retry
        swapped = "\n".join(reversed(log.splitlines()))
        self.assertFalse(all(re.search(pattern, swapped, re.MULTILINE) for pattern in self.config["expect_log"]))
        for box in ("[5 I] Load: 1 content file(s) of the save are not loaded, asking the player",
                    "[5 I] Headless: 1 content file(s) of the save are not loaded, loading it anyway"):
            self.assertTrue(any(re.search(pattern, box) for pattern in self.config["forbid_log"]), box)


if __name__ == "__main__":
    unittest.main()
