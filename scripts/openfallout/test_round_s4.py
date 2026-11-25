#!/usr/bin/env python3
"""Tests of the rounds of step 4 (round/rounds/s4-*.json, round/scripts/s4common.lua, s4rounds.lua): the committed round files, the
`expect_gap` and `${VARIABLE}` features of round.py that they use, and the rules of s4common.lua (the numbers the rounds judge by)."""
import importlib.util
import json
import sys
import unittest
from pathlib import Path

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    # lupa's wheels for Apple Silicon have no LuaJIT runtime: Lua 5.2 accepts what LuaJIT accepts here (the newest Lua refuses an
    # assignment to the variable of a for loop, which roundplayer.lua does); the tests are skipped where lupa is not installed
    try:
        from lupa.lua52 import LuaRuntime
    except ImportError:
        LuaRuntime = None

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("round_runner_s4", HERE / "round" / "round.py")
round_runner = importlib.util.module_from_spec(spec)
sys.modules["round_runner_s4"] = round_runner
spec.loader.exec_module(round_runner)

ROUNDS = HERE / "round" / "rounds"
SCRIPTS = HERE / "round" / "scripts"
SCENARIOS = HERE / "round" / "scenarios"
GAMES = ("nv", "fo3")
KINDS = ("movement", "jump", "fall", "death", "levelup")


class Args:
    scenario = game = save = start = limit = name = None


class RoundFiles(unittest.TestCase):
    def test_every_kind_has_a_round_for_each_game_that_names_its_scenario_and_a_save_by_variable(self):
        for kind in KINDS:
            for game in GAMES:
                path = ROUNDS / f"s4-{kind}-{game}.json"
                raw = path.read_text(encoding="utf-8")
                self.assertNotIn("/Users/", raw, path.name)
                config = round_runner.load_round(path, Args())
                self.assertEqual(config["game"], game)
                self.assertEqual(config["scenario"].name, f"s4_{kind}_{game}.lua")
                self.assertTrue(config["scenario"].is_file(), path.name)
                self.assertRegex(config["save"], r"^\$\{OPENFALLOUT_ROUND_SAVE_(NV|FO3)\}$")
                self.assertTrue(config["save"].upper().endswith(f"_{game.upper()}}}"), path.name)
                self.assertEqual(config["variants"][0]["name"], "plain")
                scenario = (SCENARIOS / f"s4_{kind}_{game}.lua").read_text(encoding="utf-8")
                self.assertIn(f"{kind}('{game}')", scenario)

    def test_the_old_save_round_loads_the_probe_and_a_save_named_by_variable(self):
        config = round_runner.load_round(ROUNDS / "s4-oldsave-nv.json", Args())
        self.assertEqual(config["save"], "${OPENFALLOUT_ROUND_OLDSAVE_NV}")
        self.assertEqual(config["data"], [round_runner.absolute(HERE / "autoplay")])
        self.assertEqual(config["extra_args"][-1], "autoplaysaves.omwscripts")
        self.assertIsNone(config["scenario"])
        self.assertEqual(config["expect_exit"], 0)
        self.assertNotIn("/Users/", (ROUNDS / "s4-oldsave-nv.json").read_text())
        text = ("[10:00:01.000 I] Global[scripts/autoplayprobe.lua]:\tAUTOPLAY\tdigest\tloaded\tVCG01=60/r\n"
                "[10:00:01.000 I] Global[scripts/autoplayprobe.lua]:\tAUTOPLAY\tplayer\tloaded\tcell=x level=1\n"
                "[10:00:01.000 I] Global[scripts/autoplayprobe.lua]:\tAUTOPLAY\tplayer\tno digest in this save\n"
                "[10:00:01.000 I] Global[scripts/autoplayprobe.lua]:\tAUTOPLAY\tdigest\tcheck ok\n"
                "[10:00:05.000 I] Global[scripts/autoplayprobe.lua]:\tAUTOPLAY\tsave\tautoplay-again\tok\tdir=P\tfiles=a.omwsave\n"
                "[10:02:05.000 I] Global[scripts/autoworld.lua]:\tAUTOPLAY\tintro over\tVMQ01\treal=120.0\n")
        expect = round_runner.outcome_expectations(config, config["variants"][0])
        self.assertEqual(round_runner.expectations(text, 0, False, expect), [])
        for fault in ("AUTOPLAY\tdigest\tcheck MISMATCH", "AUTOPLAY\tplayer\tMISMATCH\tlevel 1 -> 2", "AUTOPLAY\tplayer\terror\tload\tx",
                      "Ignoring unknown record"):
            failed = round_runner.expectations(text + fault + "\n", 0, False, expect)
            self.assertTrue(any("forbidden" in line for line in failed), fault)

    def test_the_death_round_judges_five_seconds_from_the_death_to_the_load_by_the_games_own_lines(self):
        config = round_runner.load_round(ROUNDS / "s4-death-nv.json", Args())
        gaps = config["expect_gap"]
        self.assertEqual(len(gaps), 1)
        self.assertEqual((gaps[0]["min"], gaps[0]["max"]), (4.4, 5.6))
        expect = round_runner.outcome_expectations(config, config["variants"][0])
        # the death script's lines: the first frame it sees the player dead, and the end of its timer
        start = "[10:00:10.000 I] L@0x1[scripts/omw/falloutdeath.lua]:\tthe player is dead (fPlayerDeathReloadTime 5)\n"
        over = "[10:00:{:02d}.{:03d} I] L@0x1[scripts/omw/falloutdeath.lua]:\tthe death timer is over after 5.00 s: the newest save loads\n"
        load = "[10:00:16.400 I] Loading saved game 's4-death' for character 'Courier'\n"
        self.assertEqual(round_runner.expectations(start + over.format(15, 20) + load, 0, False, expect), [])
        self.assertIn("the gap from", round_runner.expectations(start + over.format(16, 200) + load, 0, False, expect)[0])
        # the timer is over but no save is loaded after it: that fails too
        self.assertTrue(round_runner.expectations(start + over.format(15, 20), 0, False, expect))
        # a box that waited for a button (what the game did before the rule): no end of the timer at all fails too
        self.assertIn("no log line matches", round_runner.expectations(start, 0, False, expect)[0])


class ExpectGap(unittest.TestCase):
    GAP = {"from": "start", "to": "end", "min": 4.0, "max": 6.0}

    @staticmethod
    def log(*pairs):
        return "".join(f"[{stamp} I] {text}\n" for stamp, text in pairs)

    def test_inside_outside_and_missing(self):
        inside = self.log(("10:00:00.000", "start"), ("10:00:05.250", "end"))
        self.assertEqual(round_runner.gap_failures(inside, self.GAP), [])
        too_long = self.log(("10:00:00.000", "start"), ("10:00:06.500", "end"))
        self.assertIn("is 6.50 s, expected 4 to 6 s", round_runner.gap_failures(too_long, self.GAP)[0])
        too_short = self.log(("10:00:00.000", "start"), ("10:00:03.900", "end"))
        self.assertEqual(len(round_runner.gap_failures(too_short, self.GAP)), 1)
        self.assertIn("no log line matches `start`", round_runner.gap_failures(self.log(("10:00:00.000", "end")), self.GAP)[0])
        self.assertIn("no log line matches `end` after the start", round_runner.gap_failures(self.log(("10:00:00.000", "start")), self.GAP)[0])

    def test_an_end_before_the_start_does_not_count_and_midnight_passes(self):
        early = self.log(("10:00:00.000", "end"), ("10:00:01.000", "start"), ("10:00:06.000", "end"))
        self.assertEqual(round_runner.gap_failures(early, self.GAP), [])
        midnight = self.log(("23:59:58.000", "start"), ("00:00:03.000", "end"))
        self.assertEqual(round_runner.gap_failures(midnight, self.GAP), [])

    def test_it_is_one_of_the_expectations_of_a_round(self):
        text = self.log(("10:00:00.000", "start"), ("10:00:09.000", "end"))
        failed = round_runner.expectations(text, 0, False, {"expect_gap": [self.GAP]})
        self.assertEqual(len(failed), 1)
        self.assertEqual(round_runner.outcome_expectations({"expect_gap": [self.GAP]}, {}), {"expect_gap": [self.GAP]})


class SaveVariables(unittest.TestCase):
    def test_a_variable_is_replaced_and_an_unset_one_names_itself(self):
        self.assertEqual(round_runner.expand_save_variables("${A}/x.omwsave", {"A": "/saves"}), "/saves/x.omwsave")
        self.assertEqual(round_runner.expand_save_variables("plain.omwsave", {}), "plain.omwsave")
        with self.assertRaises(SystemExit) as stop:
            round_runner.expand_save_variables("${OPENFALLOUT_ROUND_SAVE_NV}", {})
        self.assertIn("OPENFALLOUT_ROUND_SAVE_NV", str(stop.exception))

    def test_the_save_of_a_round_is_the_file_the_variable_names(self):
        tmp = Path(__import__("tempfile").mkdtemp())
        self.addCleanup(lambda: __import__("shutil").rmtree(tmp, ignore_errors=True))
        save = tmp / "fixture.omwsave"
        save.write_bytes(b"x")
        with unittest.mock.patch.dict("os.environ", {"OPENFALLOUT_ROUND_SAVE_NV": str(save)}):
            self.assertEqual(round_runner.resolve_save({"save": "${OPENFALLOUT_ROUND_SAVE_NV}", "game": "nv"}, {}), save)


import unittest.mock  # noqa: E402  (used above)


@unittest.skipUnless(LuaRuntime, "needs lupa")
class Rules(unittest.TestCase):
    """The numbers of the rules spec as s4common.lua gives them."""

    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.common = self.lua.execute((SCRIPTS / "s4common.lua").read_text(encoding="utf-8"))

    def test_speeds_of_a_fresh_character(self):
        speed = self.common.speed
        table = self.lua.table_from
        for game in ("nv", "fo3"):
            self.assertAlmostEqual(speed(game, table({"weight": 0})), 84.7, places=3)
            self.assertAlmostEqual(speed(game, table({"drawn": True})), 77.0, places=3)
            self.assertAlmostEqual(speed(game, table({"run": True})), 338.8, places=3)
            self.assertAlmostEqual(speed(game, table({"run": True, "drawn": True})), 308.0, places=3)
            self.assertAlmostEqual(speed(game, table({"sneak": True})), 48.279, places=3)
            self.assertAlmostEqual(speed(game, table({"sneak": True, "drawn": True})), 43.89, places=3)
        # only Fallout 3 loses speed with weight (up to 150 lb)
        self.assertAlmostEqual(speed("nv", table({"weight": 150})), 84.7, places=3)
        self.assertAlmostEqual(speed("fo3", table({"weight": 75})), 71.995, places=3)
        self.assertAlmostEqual(speed("fo3", table({"weight": 75, "drawn": True})), 61.6, places=3)
        self.assertAlmostEqual(speed("fo3", table({"weight": 300})), 59.29, places=3)
        self.assertAlmostEqual(speed("fo3", table({"weight": 150, "run": True, "drawn": True})), 184.8, places=3)

    def test_fall_damage(self):
        self.assertEqual(self.common.fallDamage(350), 0)
        self.assertEqual(self.common.fallDamage(600), 0)
        self.assertAlmostEqual(self.common.fallDamage(699.9), 49.8, delta=0.1)
        self.assertAlmostEqual(self.common.fallDamage(1399.8), 1541, delta=2)

    def test_levelling(self):
        common = self.common
        self.assertEqual([common.xpForLevel(n) for n in (1, 2, 3, 4, 5, 10)], [0, 200, 550, 1050, 1700, 7200])
        self.assertEqual((common.skillPoints("nv", 5, 2), common.skillPoints("fo3", 5, 2)), (13, 15))
        self.assertEqual((common.skillPoints("nv", 5, 3), common.skillPoints("fo3", 5, 3)), (12, 15))
        self.assertEqual((common.skillPoints("nv", 7, 2), common.skillPoints("fo3", 7, 2)), (14, 17))
        self.assertEqual((common.perkOffered("nv", 2), common.perkOffered("nv", 3), common.perkOffered("fo3", 3)), (True, False, True))
        self.assertEqual((common.maxHealth("nv", 5, 1), common.maxHealth("nv", 5, 2), common.maxHealth("fo3", 5, 2)), (200, 205, 210))
        self.assertEqual(common.maxHealth("nv", 7, 1), 240)

    def test_a_skill_is_two_plus_twice_its_attribute_plus_half_the_luck_rounded_up(self):
        table = self.lua.table_from
        attributes = table([table({"number": n, "value": 5}) for n in range(5, 12)])
        skill = table({"number": 37, "value": 15, "tagged": False})
        self.assertEqual(self.common.skillValue(skill, attributes), 15)
        self.assertEqual(self.common.skillValue(table({"number": 37, "tagged": True}), attributes), 30)
        # the second SPECIAL of the spec: S7 P6 E5 C4 I7 A6 L5 gives Medicine 19, Barter 13, Lockpick 17
        special = {5: 7, 6: 6, 7: 5, 8: 4, 9: 7, 10: 6, 11: 5}
        attributes = table([table({"number": n, "value": v}) for n, v in special.items()])
        for number, want in ((37, 19), (32, 13), (36, 17), (38, 19), (45, 15), (41, 17)):
            self.assertEqual(self.common.skillValue(table({"number": number}), attributes), want, number)

    def test_tagged_skills_are_read_from_the_first_look_and_the_formula_adds_their_fifteen(self):
        table = self.lua.table_from
        attributes = table([table({"number": n, "value": 5}) for n in range(5, 12)])
        # master's rows: no tag flag; Barter (32) 14 untagged (Luck rounded down), Guns (38) 29 = tagged
        stats = table({"attributes": attributes, "skills": table([table({"number": 32, "value": 14}), table({"number": 38, "value": 29})])})
        tags = self.common.taggedSkills(stats)
        self.assertTrue(tags[38])
        self.assertIsNone(tags[32])
        count, words = self.common.skillMismatches(stats, tags)
        self.assertEqual(count, 2)     # both one under the rule (Luck 5 gives 3, not 2)
        stats = table({"attributes": attributes, "skills": table([table({"number": 32, "value": 15}), table({"number": 38, "value": 30})])})
        self.assertEqual(self.common.skillMismatches(stats, self.common.taggedSkills(stats))[0], 0)

    def test_what_level_up_points_and_books_added_is_not_taken_for_a_wrong_formula(self):
        table = self.lua.table_from
        attributes = table([table({"number": n, "value": 5}) for n in range(5, 12)])
        # Medicine (37) 20 with 5 added for good by spent points: the formula's 15 plus the modifier
        stats = table({"attributes": attributes, "skills": table([table({"number": 37, "value": 20, "modifier": 5})])})
        tags = self.common.taggedSkills(stats)
        self.assertIsNone(tags[37])
        self.assertEqual(self.common.skillMismatches(stats, tags)[0], 0)
        stats = table({"attributes": attributes, "skills": table([table({"number": 37, "value": 20, "modifier": 0})])})
        self.assertEqual(self.common.skillMismatches(stats, tags)[0], 1)

    def test_a_check_logs_the_line_and_fails_with_an_error_outside_the_tolerance(self):
        lines = []
        r = self.lua.table_from({"log": lambda text: lines.append(("note", text)), "error": lambda text: lines.append(("error", text))})
        table = self.lua.table_from
        self.assertTrue(self.common.check(r, "walk", 84.7, 84.7, table({"abs": 0.05})))
        self.assertTrue(self.common.check(r, "ground", 80.0, 84.7, table({"rel": 0.06})))
        self.assertFalse(self.common.check(r, "ground", 150.0, 84.7, table({"rel": 0.05}), "slow"))
        self.assertFalse(self.common.check(r, "none", None, 1.0, table({"abs": 0.5})))
        self.assertFalse(self.common.flag(r, "dead", False, True, "alive"))
        self.assertEqual([kind for kind, _ in lines], ["note", "note", "error", "error", "error"])
        self.assertEqual(lines[0][1], "s4 measure walk=84.700 expect=84.700 tol=0.05 ok")
        self.assertTrue(lines[2][1].endswith("FAIL slow"))
        self.assertIn("tol=5%", lines[2][1])
        self.assertIn("s4 measure dead=false expect=true tol=exact FAIL alive", lines[4][1])
        # the 3 % or 1 point of the fall damage: either is enough
        lines.clear()
        self.assertTrue(self.common.check(r, "fall", 50.5, 49.8, table({"rel": 0.03, "abs": 1.0})))
        self.assertIn("tol=3%|1", lines[0][1])


if __name__ == "__main__":
    unittest.main()


# ---- the measuring handlers of the player script, against a stand-in for the player's physics ------------------------------------

PLAYER_WORLD = """
clock, sim = 0, 0
printed = {}
print = function(...)
    local parts = {}
    for i = 1, select('#', ...) do parts[#parts + 1] = tostring((select(i, ...))) end
    printed[#printed + 1] = table.concat(parts, '\\t')
end
local vector_methods = { length = function(a) return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end }
vec = function(x, y, z) return setmetatable({ x = x, y = y, z = z }, { __add = function(a, b) return vec(a.x + b.x, a.y + b.y, a.z + b.z) end,
    __sub = function(a, b) return vec(a.x - b.x, a.y - b.y, a.z - b.z) end, __mul = function(a, k) return vec(a.x * k, a.y * k, a.z * k) end,
    __index = vector_methods }) end
-- the stand-in for the player: walks at `walk` or `run` units a second north, jumps with `jump_speed` against `gravity`, hurts by `fall_damage`
state = { x = 0, y = 0, z = 100, vz = 0, ground = true, ground_z = 100, walk = 84.7, run = 338.8, jump_speed = 283, gravity = 627,
          hp = 200, max_hp = 200, fall_lowest = 100, mod = {}, xp = 0, level = 1, skill_points = 0, perk_points = 0, ui_mode = nil, spent = 0 }
self_module = { position = vec(0, 0, 100), cell = { isExterior = true, gridX = 0, gridY = 0, worldSpaceId = 'w' }, object = {},
                controls = { movement = 0, sideMovement = 0, run = false, sneak = false, jump = false } }
package.preload['openfallout.self'] = function() return self_module end
package.preload['openfallout.core'] = function()
    return { getRealTime = function() return clock end, getSimulationTime = function() return sim end, getRealFrameDuration = function() return 0.03 end,
             sendGlobalEvent = function() end,
             pipboy = {
                 stats = function() return { level = state.level, experience = state.xp, levelStart = 0, nextLevel = 200, health = state.hp,
                     maxHealth = state.max_hp, actionPoints = 80, maxActionPoints = 80, weight = 30, maxWeight = 200,
                     skillPoints = state.skill_points, perkPoints = state.perk_points, newVegas = true } end,
                 attributes = function()
                     local rows = {}
                     for number = 5, 11 do rows[#rows + 1] = { number = number, value = 5 + (state.mod[number] or 0), base = 5 } end
                     return rows
                 end,
                 skills = function()
                     -- Barter (Charisma, 8) and Medicine (Intelligence, 9, tagged): 2 + 2 x attribute + ceil(Luck / 2) + 15 if tagged
                     local luck = math.ceil((5 + (state.mod[11] or 0)) / 2)
                     local barter = 2 + 2 * (5 + (state.mod[8] or 0)) + luck
                     local medicine = 2 + 2 * (5 + (state.mod[9] or 0)) + luck + 15
                     return { { number = 32, value = barter, base = barter, tagged = false }, { number = 37, value = medicine, base = medicine, tagged = true } }
                 end,
                 offeredPerks = function() return state.perk_points > 0 and { { id = 'perk1', available = true } } or {} end,
                 spendSkillPoints = function(number, points) state.spent = state.spent + points; state.skill_points = 0; return points end,
                 choosePerk = function(id) state.perk_points = 0; return true end,
                 playerDead = function() return state.hp < 1 end, hasRecentSave = function() return true end,
                 text = function(name) return name == 'sOverEncumbered' and 'You are overencumbered' or '' end,
             },
             hud = { takeMessages = function() local m = messages or {}; messages = {}; return m end } }
end
package.preload['openfallout.camera'] = function() return {} end
package.preload['openfallout.debug'] = function() return {} end
package.preload['openfallout.nearby'] = function() return {} end
package.preload['openfallout.input'] = function() return {} end
package.preload['openfallout.util'] = function() return { vector3 = vec, transform = { rotateZ = function(a) return { yaw = a } end } } end
package.preload['openfallout.animation'] = function() return { BONE_GROUP = {} } end
package.preload['openfallout.types'] = function()
    return { Actor = {
        getCurrentSpeed = function() return state.api_speed or 0 end,
        isOnGround = function() return state.ground end,
        getStance = function() return 0 end,
    } }
end
package.preload['openfallout.interfaces'] = function()
    return { UI = { getMode = function() return state.ui_mode end, removeMode = function(m) if state.ui_mode == m then state.ui_mode = nil end end },
             Controls = { overrideMovementControls = function() end },
             FalloutHud = { state = function() return state.hud end } }
end
function load_player() return load(player_source)() end

-- one frame of `dt` seconds of the stand-in
function step(dt, module)
    clock, sim = clock + dt, sim + dt
    local c = self_module.controls
    local speed = 0
    if c.movement ~= 0 then speed = c.run and state.run or state.walk end
    state.api_speed = speed
    state.y = state.y + speed * dt * c.movement
    if c.jump and state.ground then state.vz, state.ground = state.jump_speed, false end
    if not state.ground then
        state.vz = state.vz - state.gravity * dt
        state.z = state.z + state.vz * dt
        state.fall_lowest = math.min(state.fall_lowest, state.z)
        if state.z <= state.ground_z and state.vz < 0 then
            state.z, state.vz, state.ground = state.ground_z, 0, true
            local lost = state.fall_from and (state.fall_from - state.z) or 0
            if lost > 600 then state.hp = state.hp - 0.025 * (lost - 600) ^ 1.65 end
            state.fall_from = nil
        end
    end
    self_module.position = vec(state.x, state.y, state.z)
    module.engineHandlers.onFrame()
    module.engineHandlers.onUpdate()
end
-- the player is put in the air at height z (r.teleport)
function put_in_air(z) state.z, state.ground, state.vz, state.fall_from = z, false, 0, z; self_module.position = vec(state.x, state.y, z) end
"""


@unittest.skipUnless(LuaRuntime, "needs lupa")
class MeasuringHandlers(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.globals().player_source = (SCRIPTS / "roundplayer.lua").read_text(encoding="utf-8")
        self.lua.execute(PLAYER_WORLD)
        self.module = self.lua.eval("load_player")()
        self.step = self.lua.eval("step")

    def ask(self, what, **args):
        self.module.eventHandlers.RoundAsk(self.lua.table_from({"id": 1, "what": what, "args": self.lua.table_from(args)}))

    def run_frames(self, seconds, dt=1 / 30):
        for _ in range(int(round(seconds / dt))):
            self.step(dt, self.module)

    def lines(self, kind):
        printed = self.lua.eval("printed")
        return [printed[i] for i in range(1, len(printed) + 1) if printed[i].startswith(f"ROUND\t{kind}\t")]

    def test_a_walk_is_measured_over_the_window_after_the_first_second(self):
        self.ask("s4move", label="walk", forward=1, seconds=5, skip=1.0)
        self.run_frames(5.5)
        (line,) = self.lines("s4move")
        self.assertIn("walk", line)
        self.assertRegex(line, r"path=84\.70 net=84\.70 api=84\.70 apiMin=84\.70 apiMax=84\.70 window=4\.00 frames=\d+")
        # the controls are let go
        self.assertEqual(self.lua.eval("self_module.controls.movement"), 0)

    def test_a_run_and_the_speed_number_that_is_not_the_ground_speed_are_both_logged(self):
        self.lua.execute("state.run = 270")
        self.ask("s4move", label="run", forward=1, run=True, seconds=4, skip=1.0)
        # the API says 338.8 (the rule) while the player really covers 270 a second: the two numbers differ in the line
        self.lua.execute("state.api_override = true")
        self.run_frames(4.5)
        (line,) = self.lines("s4move")
        self.assertRegex(line, r"path=2[67]\d\.\d+ net=2[67]\d\.\d+ api=270\.00")

    def test_a_jump_gives_its_apex_and_the_landing(self):
        self.ask("s4jump", label="plain", seconds=3.5)
        self.run_frames(3.5)
        (line,) = self.lines("s4jump")
        # v^2 / 2g = 283^2 / 1254 = 63.9 units, 59 in steps of a thirtieth of a second
        self.assertRegex(line, r"apex=5[89]\.\d+ airborne=true takeoff=0\.0\d air=\d\.\d+ landed=true presses=1")

    def test_extra_presses_in_the_air_are_counted_and_do_nothing_when_the_physics_does_not_allow_them(self):
        self.ask("s4jump", label="extra", seconds=3.5, again=self.lua.table_from([0.25, 0.5]))
        self.run_frames(3.5)
        (line,) = self.lines("s4jump")
        self.assertIn("presses=3", line)
        self.assertRegex(line, r"apex=5[89]\.\d+")
        self.assertRegex(line, r"apexAtPress=\d+\.\d+,\d+\.\d+$")

    def test_a_drop_gives_the_height_lost_and_the_health_taken(self):
        self.lua.execute("state.ground_z = 100")
        self.ask("s4drop", label="drop", z=800, hold=1.0)
        self.lua.execute("put_in_air(800)")
        self.run_frames(8)
        (line,) = self.lines("s4drop")
        self.assertRegex(line, r"startZ=79\d\.\d landZ=100\.0 lost=69\d\.\d hp0=200\.00 hp1=150\.\d\d minHp=150\.\d\d air=\d\.\d\d")

    def test_a_drop_that_never_lands_says_so_after_its_timeout(self):
        self.ask("s4drop", label="stuck", z=5000, timeout=2)
        self.run_frames(3)
        self.assertIn("ROUND\ts4drop\tstuck\tno landing\tstarted=false\tairborne=false", self.lines("s4drop"))

    def test_stats_are_numbers_with_special_and_skills_by_number(self):
        self.ask("s4stats", label="base")
        (line,) = self.lines("s4stats")
        self.assertIn("level=1 xp=0 levelStart=0 nextLevel=200 hp=200.00/200.00 ap=80.0/80.0 weight=30.0/200.0 skillPoints=0 perkPoints=0", line)
        self.assertIn("special=5:5/5,6:5/5,7:5/5,8:5/5,9:5/5,10:5/5,11:5/5", line)
        self.assertIn("skills=32:15/15,37:30/30t", line)

    def test_the_level_up_screen_is_answered_only_when_one_is_open(self):
        self.ask("s4levelup", label="none")
        self.assertEqual(self.lines("s4levelup"), [])
        self.lua.execute("state.ui_mode, state.skill_points, state.perk_points = 'Interface', 13, 1")
        self.ask("s4levelup", label="screen")
        (line,) = self.lines("s4levelup")
        self.assertIn("skillPoints=13 spent=13 perkPoints=1 offered=1 perkChosen=true", line)
        self.assertIsNone(self.lua.eval("state.ui_mode"))

    def test_the_message_of_a_setting_is_found_by_its_text_and_the_text_is_not_logged(self):
        self.lua.execute("messages = { { text = 'You are overencumbered' }, { text = 'other' } }")
        self.ask("s4message", setting="sOverEncumbered")
        (line,) = self.lines("s4message")
        self.assertEqual(line, "ROUND\ts4message\tsOverEncumbered\tseen=true\tmessages=2\tshown=0\ttextKnown=true")
        self.assertNotIn("overencumbered", line)

    def test_a_message_the_display_took_is_seen_on_its_lines(self):
        # the display takes the engine's messages each frame: the round finds the text on the lines the display shows
        self.lua.execute("messages = {}; state.hud = { lines = { { text = 'You are overencumbered' } } }")
        self.run_frames(0.1)
        self.lua.execute("state.hud = nil")
        self.ask("s4message", setting="sOverEncumbered")
        self.ask("s4message", setting="sOverEncumbered")
        first, second = self.lines("s4message")
        self.assertEqual(first, "ROUND\ts4message\tsOverEncumbered\tseen=true\tmessages=0\tshown=1\ttextKnown=true")
        self.assertIn("seen=false", second)

    def test_dead_is_the_pipboys_answer(self):
        self.lua.execute("state.hp = 0.5")
        self.ask("s4dead", label="x")
        self.assertIn("dead=true box=none save=true", self.lines("s4dead")[0])


# ---- the scenarios of the rounds, played whole against a stand-in for the engine -----------------------------------------------------
# (nothing of the game: a player that walks, jumps and falls by the rules of a stand-in physics, and the Lua modules the two scripts call)

WHOLE_PRELUDE = PLAYER_WORLD + """
commands = {}
saved_games = {}
menu_events = {}
god = false
local function cell_of() return { isExterior = true, gridX = 0, gridY = 0, worldSpaceId = 'w', id = 'cell' } end
world_player = { position = self_module.position, cell = cell_of(), id = '0x14',
    sendEvent = function(self, name, data) player_module.eventHandlers.RoundAsk(data) end,
    teleport = function(self, cell, pos, options)
        state.x, state.y = pos.x, pos.y
        if options and options.onGround then state.z, state.ground, state.vz = state.ground_z, true, 0 else put_in_air(pos.z) end
    end }
setmetatable(world_player, { __index = function(t, k) if k == 'position' then return self_module.position end end })
rawset(world_player, 'position', nil)
local core = package.preload['openfallout.core']()
core.sendGlobalEvent = function(name, data) world_module.eventHandlers.RoundAnswer(data) end
core.quit = function() quit_called = true end
core.isWorldPaused = function() return false end
core.getFormId = function(file, id) return string.format('FormId:%s:0x%x', file, id) end
core.worldCommands = { run = function() return 1 end, result = function() return 0 end }
core.messageBox = { current = function() return nil end }
core.combat = { setDrawn = function(on) state.drawn = on end, weapon = function() return { weapon = 'x', rounds = 1, range = 100 } end }
core.pipboy.items = function(holder)
    return { { id = 'row1', category = 'weapons', form = 'FormId:FalloutNV.esm:0xe3778', equipped = state.equipped == true, hasAmmo = true, clip = 1,
               loaded = 1, reserve = 0, count = 1 } }
end
core.pipboy.use = function(id) state.equipped = true; return 'equipped' end
package.preload['openfallout.core'] = function() return core end
package.preload['openfallout.world'] = function()
    return { players = { world_player }, globals = { GameHour = 8 }, quests = { find = function() return nil end },
             getExteriorCell = function() return cell_of() end, getCellById = function() return cell_of() end, getCellByName = function() return cell_of() end }
end
package.preload['openfallout.types'] = function()
    return { Actor = { getCurrentSpeed = function() return state.api_speed or 0 end, isOnGround = function() return state.ground end,
                       getStance = function() return 0 end, setStance = function() end, STANCE = { Weapon = 1, Nothing = 0 } },
             Player = { sendMenuEvent = function(player, event, data) menu_events[#menu_events + 1] = { event = event, data = data } end } }
end
package.preload['openfallout.debug'] = function()
    return { isGodMode = function() return god end, toggleGodMode = function() god = not god end, takeScreenshot = function() end,
             scriptCommand = function(name, reference, arguments)
                 commands[#commands + 1] = { name = name, reference = reference, arguments = arguments }
                 if name == 'ForceActorValue' and arguments[1] == 16 then state.hp = arguments[2] end
                 if name == 'DamageActorValue' and arguments[1] == 16 then state.hp = 0 end
                 if name == 'ModActorValue' then state.mod[arguments[1]] = (state.mod[arguments[1]] or 0) + arguments[2] end
                 if name == 'RewardXP' then state.ui_mode, state.skill_points, state.perk_points = 'Interface', 13, 1 end
             end }
end
package.preload['openfallout.camera'] = function()
    return { MODE = { FirstPerson = 1, ThirdPerson = 2, Static = 0 }, setMode = function() end, getMode = function() return 2 end,
             getPosition = function() return vec(0, 0, 164) end, getYaw = function() return 0 end, getPitch = function() return 0 end,
             setYaw = function() end, setPitch = function() end }
end
package.preload['openfallout.nearby'] = function()
    return { COLLISION_TYPE = { HeightMap = 4 }, actors = {}, castRay = function(from, to) return { hit = true, hitPos = vec(from.x, from.y, state.ground_z) } end }
end
package.preload['scripts.roundscenario'] = function() return load(scenario_source)() end
player_module = load(player_source)()
world_module = load(world_source)()

function frame(dt)
    step(dt, player_module)
    world_module.engineHandlers.onUpdate()
end
"""


@unittest.skipUnless(LuaRuntime, "needs lupa")
class WholeScenarios(unittest.TestCase):
    def play(self, kind, game, seconds, setup=""):
        lua = LuaRuntime(unpack_returned_tuples=True)
        g = lua.globals()
        g.player_source = (SCRIPTS / "roundplayer.lua").read_text(encoding="utf-8")
        g.world_source = (SCRIPTS / "roundworld.lua").read_text(encoding="utf-8")
        g.scenario_source = (SCENARIOS / f"s4_{kind}_{game}.lua").read_text(encoding="utf-8")
        g.s4rounds_source = (SCRIPTS / "s4rounds.lua").read_text(encoding="utf-8")
        g.s4common_source = (SCRIPTS / "s4common.lua").read_text(encoding="utf-8")
        # the scenario's own require of the other files of the round (the files of the round folder are on the data path of the engine)
        lua.execute("""
            package.preload['scripts.s4rounds'] = function() return load(s4rounds_source, 's4rounds')() end
            package.preload['scripts.s4common'] = function() return load(s4common_source, 's4common')() end
        """)
        lua.execute(f"quit_called = false\n{WHOLE_PRELUDE}\n{setup}")
        for _ in range(int(seconds * 30)):
            lua.eval("frame")(1 / 30)
            if lua.eval("quit_called"):
                break
        printed = lua.eval("printed")
        return lua, [printed[i] for i in range(1, len(printed) + 1)]

    def test_the_movement_round_measures_every_leg_and_logs_the_lines(self):
        lua, lines = self.play("movement", "nv", 400, "state.walk, state.run = 84.7, 338.8")
        text = "\n".join(lines)
        self.assertNotIn("attempt to", text)
        self.assertIn("ROUND\tnote\ts4 measure walk_holstered_api=84.700 expect=84.700 tol=0.05 ok", text)
        self.assertIn("ROUND\tnote\ts4 measure walk_holstered_ground=84.700 expect=84.700 tol=5% ok", text)
        self.assertIn("ROUND\tnote\ts4 measure run_holstered_ground=338.800 expect=338.800 tol=5% ok", text)
        # a stand-in that does not move at the rule's speed fails: the line is a ROUND error
        self.assertTrue(any(line.startswith("ROUND\terror\ts4 measure walk_drawn_api=84.700 expect=77.000") for line in lines))
        self.assertRegex(text, r"ROUND\tdone\tend\t")
        # the weights were added with the game's own command, and the heavy item is the one of the record
        self.assertIn("overloaded", text)

    def test_the_jump_round_checks_the_apex_and_the_extra_presses(self):
        lua, lines = self.play("jump", "nv", 120, "state.jump_speed = 285")
        text = "\n".join(lines)
        self.assertNotIn("attempt to", text)
        self.assertRegex(text, r"s4 measure jump_apex=6\d\.\d+ expect=64\.000 tol=8 ok")
        self.assertRegex(text, r"s4 measure jump_no_double=-?\d\.\d+ expect=0\.000 tol=8 ok")

    def test_the_fall_round_measures_two_drops_and_the_damage_of_the_rule(self):
        lua, lines = self.play("fall", "fo3", 200)
        text = "\n".join(lines)
        self.assertNotIn("attempt to", text)
        self.assertRegex(text, r"s4 measure fall_lost_350=3[45]\d\.\d+ expect=350\.000 tol=6 ok")
        self.assertRegex(text, r"s4 measure fall_damage_350=0\.000 expect=0\.000 tol=0\.01 ok")
        self.assertRegex(text, r"s4 measure fall_lost_700=69\d\.\d+ expect=700\.000 tol=6 ok")
        self.assertRegex(text, r"s4 measure fall_damage_700=49\.\d+ expect=49\.\d+ tol=3%\|1 ok")

    def test_the_level_up_round_reads_the_screen_and_the_formula(self):
        lua, lines = self.play("levelup", "nv", 200)
        text = "\n".join(lines)
        self.assertNotIn("attempt to", text)
        self.assertRegex(text, r"s4 measure xp_to_next_level=200\.000 expect=200\.000 tol=0 ok")
        self.assertRegex(text, r"s4 measure skills_follow_current_special=0\.000 expect=0\.000 tol=0 ok")
        self.assertRegex(text, r"s4 measure levelup_skill_points=13\.000 expect=13\.000 tol=0 ok")
        self.assertRegex(text, r"s4 measure levelup_perk_points=1\.000 expect=1\.000 tol=0 ok")

    def test_the_death_round_saves_marks_and_waits_for_the_load_that_a_stand_in_cannot_make(self):
        lua, lines = self.play("death", "nv", 60)
        text = "\n".join(lines)
        self.assertNotIn("attempt to", text)
        self.assertIn("s4 death t0 kind=half_a_point", text)
        self.assertRegex(text, r"s4 measure death_below_one_point=true expect=true")
        self.assertEqual(lua.eval("menu_events[1].event"), "S4Save")
        self.assertEqual(lua.eval("menu_events[1].data.description"), "s4-death")
        self.assertIn("s4 measure death_reload=none", text)

    def test_the_second_run_of_the_death_round_finds_the_mark_and_checks_the_loaded_player(self):
        lua, lines = self.play("death", "nv", 30, "world_module.engineHandlers.onLoad({ marks = { phase = 'reloaded' } })")
        text = "\n".join(lines)
        self.assertIn("s4 death the game was loaded again by the death", text)
        self.assertIn("s4 measure death_reloaded_alive=true expect=true tol=exact ok", text)
        self.assertIn("s4 measure death_reloaded_health=200.000 expect=200.000 tol=1 ok", text)
        self.assertNotIn("s4 death t0", text)
