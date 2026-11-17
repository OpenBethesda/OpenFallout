#!/usr/bin/env python3
"""Tests of picturecompare.py, the picture comparison of two builds: the scene list, the round and the scenario written for
each scene, the comparison of two folders of results and its report, the limits set from the noise floor, and (macOS only)
a whole run of the round runner against a stand-in engine that draws a picture for each scene."""
import contextlib
import io
import json
import re
import shutil
import stat
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mac_measure  # noqa: E402
import picturecompare  # noqa: E402
import picturediff  # noqa: E402
from test_picturediff import changed, textured  # noqa: E402

try:
    from test_round_scenario import LuaRuntime, make_runtime
except ImportError:  # pragma: no cover
    LuaRuntime = None

SCENES = json.loads((HERE / "picture_scenes.json").read_text(encoding="utf-8"))
LIMITS = json.loads((HERE / "picture_limits.json").read_text(encoding="utf-8"))


def quietly(function, *args, **kwargs):
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return function(*args, **kwargs)


class SceneList(unittest.TestCase):
    def test_each_game_has_an_interior_an_exterior_and_a_busy_exterior(self):
        for game in ("nv", "fo3"):
            kinds = {scene["kind"] for scene in SCENES["scenes"] if scene["game"] == game}
            self.assertLessEqual({"interior", "exterior", "busy"}, kinds, game)

    def test_shadows_water_and_post_processing_have_scenes_that_turn_them_on(self):
        def on(scene, section, key):
            return (scene.get("settings", {}).get(section, {}).get(key)) == "true"
        by_kind = {}
        for scene in SCENES["scenes"]:
            by_kind.setdefault(scene["kind"], []).append(scene)
        self.assertTrue(by_kind["shadows"] and all(on(s, "Shadows", "enable shadows") for s in by_kind["shadows"]))
        self.assertTrue(by_kind["water"] and all(on(s, "Water", "shader") for s in by_kind["water"]))
        post = by_kind["post-processing"]
        self.assertTrue(post and all(on(s, "Post Processing", "enabled") and s["settings"]["Post Processing"]["chain"] for s in post))
        # the other scenes have them off, so that each effect is seen by the scenes made for it
        self.assertEqual(SCENES["settings"]["Shadows"]["enable shadows"], "false")
        self.assertEqual(SCENES["settings"]["Water"]["shader"], "false")
        self.assertEqual(SCENES["settings"]["Post Processing"]["enabled"], "false")

    def test_the_main_scenes_are_the_places_of_the_measurement_scenes(self):
        # the pictures and the frame rate are taken at the same places (docs/openfallout/measuring.md)
        starts = {scene["name"]: scene["start"] for scene in SCENES["scenes"]}
        for name, measured in mac_measure.SCENES.items():
            self.assertEqual(starts.get(name), measured["start"], name)

    def test_a_place_is_an_editor_id_or_a_grid_cell_and_nothing_else(self):
        names = [scene["name"] for scene in SCENES["scenes"]]
        self.assertEqual(len(names), len(set(names)))
        for scene in SCENES["scenes"]:
            self.assertIn(scene["game"], ("nv", "fo3"))
            self.assertRegex(scene["start"], r"^[A-Za-z0-9_]+(:-?\d+,-?\d+)?$", scene["name"])
            self.assertTrue(scene["for"], scene["name"])
            # no yaw: the camera looks the way the start put the player
            for key in ("yaw", "pitch"):
                if key in scene["camera"] or key == "pitch":
                    self.assertIsInstance(scene["camera"][key], (int, float), scene["name"])
            for value in scene["camera"].get("pos", []) + scene.get("place", {}).get("pos", []):
                self.assertIsInstance(value, (int, float), scene["name"])
            if "weather" in scene:
                self.assertRegex(scene["weather"]["master"], r"^[A-Za-z0-9]+\.esm$")
                self.assertRegex(scene["weather"]["form"], r"^0x[0-9A-Fa-f]{6}$")
                self.assertRegex(scene["weather"]["editor_id"], r"^[A-Za-z0-9_]+$")
            # every setting is text, as settings.cfg holds it
            for section in scene.get("settings", {}).values():
                self.assertTrue(all(isinstance(value, str) for value in section.values()), scene["name"])
        self.assertNotRegex(json.dumps(SCENES), r"[A-Za-z]:\\\\|/Users/|/home/|\.(dds|nif|kf|wav|mp3|ogg)\b")

    def test_the_documentation_names_every_scene(self):
        page = (HERE.parents[1] / "docs" / "openfallout" / "picture-comparison.md").read_text(encoding="utf-8")
        for scene in SCENES["scenes"]:
            self.assertIn(f"| `{scene['name']}` |", page, scene["name"])

    def test_every_scene_runs_with_lossless_pictures(self):
        self.assertEqual(SCENES["settings"]["General"]["screenshot format"], "png")


class Rounds(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def test_a_scene_is_one_round_on_its_start_cell_with_its_settings_over_the_common_ones(self):
        scene = next(s for s in SCENES["scenes"] if s["name"] == "nv-water")
        config = picturecompare.round_of(scene, SCENES)
        self.assertEqual((config["name"], config["game"], config["start"]), ("nv-water", "nv", "WastelandNV:13,12"))
        self.assertEqual(config["shot_format"], "png")
        self.assertEqual(config["resolution"], [1280, 720])
        self.assertEqual(config["framerate"], 30)
        self.assertEqual(config["extra_args"], ["--random-seed", "1"])
        self.assertEqual(config["settings"]["Water"], {"shader": "true", "refraction": "true", "reflection detail": "2"})
        self.assertEqual(config["settings"]["Shadows"]["enable shadows"], "false")
        self.assertEqual(config["variants"], [{"name": "run"}])
        self.assertNotIn("save", config)

    def test_rounds_are_written_with_their_scenarios(self):
        written = picturecompare.write_rounds(SCENES, SCENES["scenes"][:2], self.tmp)
        self.assertEqual([path.name for _, path in written], ["nv-interior.json", "nv-exterior.json"])
        config = json.loads(written[0][1].read_text())
        scenario = self.tmp / config["scenario"]
        self.assertTrue(scenario.read_text().startswith("local SCENE = {"))

    def test_a_scene_may_put_its_camera_further_ahead_of_the_eye(self):
        scene = {"name": "s", "camera": {"yaw": 90, "pitch": 8}}
        self.assertIn("forward = 48,", picturecompare.scenario_of(scene, SCENES))
        # the player's own arm, whose idle pose is not held still, is then behind the camera
        scene["camera"]["forward"] = 150
        self.assertIn("forward = 150,", picturecompare.scenario_of(scene, SCENES))

    def test_a_scene_table_is_written_as_lua(self):
        text = picturecompare.lua_literal({"a": [1, 2.5, -3], "b": "x'y", "c": True, "d": {"e": None}})
        self.assertEqual(text, "{ a = { 1, 2.5, -3 }, b = 'x\\'y', c = true, d = {  } }")


@unittest.skipUnless(LuaRuntime, "needs lupa")
class Scenario(unittest.TestCase):
    """The scenario of a scene, run by the scenario player of the rounds against the stand-ins of the engine's Lua modules."""
    WORLD = (HERE / "round" / "scripts" / "roundworld.lua").read_text(encoding="utf-8")

    def play(self, name, simulation_start=20.0, steps=200, frozen_after=None, held=False):
        scene = next(s for s in SCENES["scenes"] if s["name"] == name)
        source = picturecompare.scenario_of(scene, SCENES)
        self.lua = make_runtime(self.WORLD)
        self.lua.execute(f"simulation = {simulation_start}")
        module = self.lua.eval("function(source) return load_world(source) end")(source)
        for _ in range(steps):
            self.lua.execute("clock = clock + 1; if not paused_tags.round then simulation = simulation + 1 end")
            if frozen_after is not None:
                self.lua.execute(f"if simulation > {frozen_after} then simulation = {frozen_after} end")
                if held and self.lua.eval(f"simulation >= {frozen_after}"):
                    self.lua.execute("paused = true")
            module.engineHandlers.onUpdate(0)
            sent = self.lua.eval("sent_to_player")
            for index in range(1, len(sent) + 1):
                module.eventHandlers.RoundAnswer(self.lua.table_from({"id": sent[index].data.id, "result": self.lua.table_from({})}))
            self.lua.execute("sent_to_player = {}")
            if self.lua.eval("quit_called"):
                break
        printed = self.lua.eval("printed")
        return [printed[i] for i in range(1, len(printed) + 1)], self.lua.eval("asked_log")

    def test_the_scene_is_held_still_before_its_one_picture(self):
        lines, asked = self.play("nv-water")
        asked = [asked[i] for i in range(1, len(asked) + 1)]
        # the player script is asked: AI off, god mode, interface hidden, the fixed view, the eye, then the picture
        self.assertEqual([a for a in asked if a != "quiet"], ["ai", "god", "hud", "view", "eye", "shot"])
        steps = [line.split("\t")[1] for line in lines if line.startswith("ROUND\t")]
        order = [steps.index(step) for step in ("gameTimeScale", "hour", "teleport", "weather", "pause", "note", "done")]
        self.assertEqual(order, sorted(order), steps)
        self.assertIn("ROUND\tgameTimeScale\tok\t0.00", lines)
        self.assertIn("ROUND\thour\tok\t13.00", lines)
        self.assertIn("ROUND\tweather\tok\tFormId:FalloutNV.esm:1047688", lines)
        self.assertEqual(self.lua.eval("game_time_scale"), 0)
        self.assertEqual(self.lua.eval("teleports[1].pos.x"), 55296)
        # paused when the simulation time reached the scene's time, and the line says at what time
        still = next(line for line in lines if line.startswith("ROUND\tnote\tstill"))
        self.assertEqual(still, "ROUND\tnote\tstill\tsimulation=60.000")
        self.assertTrue(self.lua.eval("paused_tags.round"))
        self.assertFalse(any(line.startswith("ROUND\terror") for line in lines), lines)

    def test_a_scene_that_is_ready_after_its_pause_time_is_an_error(self):
        lines, _ = self.play("nv-interior", simulation_start=100.0)
        self.assertTrue(any(line.startswith("ROUND\terror\tlate") for line in lines), lines)

    def test_a_scene_whose_simulation_time_stands_still_is_an_error_that_names_the_cause(self):
        # a menu or a box holds the world: the pause time is never reached
        lines, _ = self.play("nv-busy", simulation_start=20.0, steps=300, frozen_after=30.0, held=True)
        self.assertTrue(any(line.startswith("ROUND\terror\theld") and "stopped at 30.0" in line for line in lines), lines)
        # nothing holds it, but the engine is too slow to get there in time
        lines, _ = self.play("nv-busy", simulation_start=20.0, steps=300, frozen_after=30.0)
        self.assertTrue(any(line.startswith("ROUND\terror\tslow") and "only 30.0 of 60.0 in 150 real seconds" in line
                            for line in lines), lines)

    def test_every_scenario_is_valid_lua(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        load = lua.eval("function(source, name) local f, err = load(source, name) return err end")
        for scene in SCENES["scenes"]:
            self.assertIsNone(load(picturecompare.scenario_of(scene, SCENES), scene["name"]), scene["name"])


def make_side(folder, scene, picture, verdict="PASS", settings="[Video]\nantialiasing = 0\n", simulation="60.000",
              stamp="20261116-100000"):
    """A folder of the results of one round as round.py writes it: report.json, the shot and the run's settings.cfg."""
    run = folder / f"{stamp}-{scene}" / "run"
    (run / "shots").mkdir(parents=True)
    (run / "config").mkdir()
    (run / "config" / "settings.cfg").write_text(settings)
    shot = run / "shots" / f"01-{scene}.png"
    if picture is not None:
        picturediff.write_png(shot, picture)
    lines = [f"note\tstill\tsimulation={simulation}", f"shot\t{scene}\tn=1", "done\tend\treal=80.0 steps=40"]
    report = {"round": scene, "variant": "run", "verdict": verdict, "round_lines": lines,
              "shots": [{"n": 1, "name": scene, "file": str(shot) if picture is not None else None}]}
    (run / "report.json").write_text(json.dumps(report))
    (run / "report.md").write_text(f"# Round {scene} / run: {verdict}\n")
    return run


class CompareFolders(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.reference, self.candidate, self.out = self.tmp / "reference", self.tmp / "candidate", self.tmp / "report"
        self.limits = self.tmp / "limits.json"
        # the limits before any were set from runs: every scene is judged by the allowance
        self.limits.write_text(json.dumps(dict(LIMITS, scenes={}, noise_floor={})))

    def compare(self, *extra):
        return quietly(picturecompare.main, ["compare", str(self.reference), str(self.candidate), "--out", str(self.out),
                                             "--limits", str(self.limits), *extra])

    def results(self):
        return {row["scene"]: row for row in json.loads((self.out / "results.json").read_text())["scenes"]}

    def test_the_same_pictures_pass_and_the_status_says_nothing_was_flagged(self):
        for scene in ("nv-interior", "fo3-busy"):
            make_side(self.reference, scene, textured(64, 48))
            make_side(self.candidate, scene, textured(64, 48), simulation="60.033")
        self.assertEqual(self.compare(), 0)
        results = self.results()
        self.assertEqual({row["verdict"] for row in results.values()}, {"PASS"})
        self.assertEqual(results["nv-interior"]["simulation"], {"reference": 60.0, "candidate": 60.033})
        summary = (self.out / "summary.md").read_text()
        self.assertIn("| nv-interior | PASS |", summary)
        # scenes in the order of the list, a passed scene has no page of its own
        self.assertLess(summary.index("nv-interior"), summary.index("fo3-busy"))
        self.assertFalse((self.out / "nv-interior.html").exists())
        self.assertIn("nv-interior", (self.out / "index.html").read_text())

    def test_a_changed_picture_is_flagged_with_a_page_of_the_three_pictures(self):
        reference = textured(64, 48)
        make_side(self.reference, "nv-water", reference)
        make_side(self.candidate, "nv-water", changed(reference, lambda x, y, c: (0, 0, 0) if x < 20 and y < 20 else c))
        self.assertEqual(self.compare(), 1)
        row = self.results()["nv-water"]
        self.assertEqual(row["verdict"], "FLAG")
        self.assertTrue(any(reason.startswith("worst_tile_bad") for reason in row["reasons"]))
        self.assertFalse(row["limits_from_runs"])
        page = (self.out / "nv-water.html").read_text()
        for picture in re.findall(r'src="([^"]+)"', page):
            self.assertFalse(Path(picture).is_absolute(), picture)
            self.assertTrue((self.out / picture).is_file(), picture)
        self.assertEqual(len(re.findall(r"<img ", page)), 3)
        self.assertIn('href="nv-water.html"', (self.out / "index.html").read_text())
        self.assertEqual(picturediff.read_png(self.out / "pictures" / "nv-water-reference.png").rows, reference.rows)

    def test_every_scene_can_have_its_page(self):
        make_side(self.reference, "nv-water", textured(64, 48))
        make_side(self.candidate, "nv-water", textured(64, 48))
        self.assertEqual(self.compare("--all-pages"), 0)
        self.assertTrue((self.out / "nv-water.html").exists())

    def test_a_missing_picture_is_reported_and_counts_as_flagged(self):
        make_side(self.reference, "nv-busy", textured(64, 48))
        make_side(self.candidate, "nv-busy", None, verdict="FAIL")
        make_side(self.reference, "fo3-post", textured(64, 48))
        self.assertEqual(self.compare(), 1)
        results = self.results()
        self.assertEqual(results["nv-busy"]["verdict"], "MISSING")
        self.assertEqual(results["fo3-post"]["verdict"], "MISSING")
        self.assertIn("candidate", " ".join(results["fo3-post"]["reasons"]))

    def test_a_failed_round_or_other_settings_make_a_scene_not_comparable(self):
        make_side(self.reference, "nv-busy", textured(64, 48))
        make_side(self.candidate, "nv-busy", textured(64, 48), verdict="FAIL")
        make_side(self.reference, "fo3-busy", textured(64, 48))
        make_side(self.candidate, "fo3-busy", textured(64, 48), settings="[Video]\nantialiasing = 4\n")
        self.assertEqual(self.compare(), 1)
        results = self.results()
        self.assertEqual(results["nv-busy"]["verdict"], "FLAG")
        self.assertIn("round of the candidate failed", " ".join(results["nv-busy"]["reasons"]))
        self.assertEqual(results["fo3-busy"]["verdict"], "FLAG")
        self.assertIn("settings", " ".join(results["fo3-busy"]["reasons"]))

    def test_the_newest_run_of_a_scene_counts(self):
        make_side(self.reference, "nv-busy", textured(64, 48))
        make_side(self.candidate, "nv-busy", None, stamp="20261116-090000")
        make_side(self.candidate, "nv-busy", textured(64, 48), stamp="20261116-110000")
        self.assertEqual(self.compare(), 0)

    def test_a_scene_whose_limits_were_set_from_runs_is_judged_by_them(self):
        reference = textured(64, 48)
        candidate = changed(reference, lambda x, y, c: (c[0] + 4, c[1], c[2]))
        make_side(self.reference, "nv-busy", reference)
        make_side(self.candidate, "nv-busy", candidate)
        self.assertEqual(self.compare(), 1)                                   # the allowance: a mean shift of 4 is too much
        own = dict(LIMITS, scenes={"nv-busy": dict(LIMITS["allowance"], mean_shift=5.0, worst_tile_shift=12.0)})
        self.limits.write_text(json.dumps(own))
        self.assertEqual(self.compare(), 0)
        self.assertTrue(self.results()["nv-busy"]["limits_from_runs"])

    def test_nothing_to_compare_is_an_error(self):
        self.reference.mkdir()
        self.candidate.mkdir()
        self.assertEqual(self.compare(), 2)


class Limits(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.limits = self.tmp / "limits.json"
        self.limits.write_text(json.dumps(LIMITS))

    def noise(self, name, measures, noise=True):
        path = self.tmp / name
        path.write_text(json.dumps({"noise": noise, "scenes": [
            {"scene": scene, "verdict": "PASS", "measures": values} for scene, values in measures.items()]}))
        return path

    def test_the_recorded_limits_are_complete(self):
        self.assertEqual(set(LIMITS["allowance"]), {name for name, _ in picturediff.LIMITED})
        self.assertEqual(set(LIMITS["method"]), {"pixel_tolerance", "tile", "block"})
        names = {scene["name"] for scene in SCENES["scenes"]}
        self.assertLessEqual(set(LIMITS["scenes"]), names)
        for limits in LIMITS["scenes"].values():
            self.assertEqual(set(limits), set(LIMITS["allowance"]))

    def test_limits_are_proposed_from_noise_runs_and_written_with_their_floor(self):
        quiet = {name: (1.0 if name.endswith("ssim") else 0.0) for name in LIMITS["allowance"]}
        first = self.noise("a.json", {"nv-busy": dict(quiet, worst_tile_bad=0.2), "nv-interior": quiet})
        second = self.noise("b.json", {"nv-busy": dict(quiet, worst_tile_bad=0.3)})
        code = quietly(picturecompare.main, ["limits", str(first), str(second), "--limits", str(self.limits), "--write",
                                             "--build", "abc1234 OpenGL"])
        self.assertEqual(code, 0)
        written = json.loads(self.limits.read_text())
        self.assertAlmostEqual(written["scenes"]["nv-busy"]["worst_tile_bad"], 0.3 * 1.5 + 0.02)
        self.assertEqual(written["scenes"]["nv-interior"], LIMITS["allowance"])
        self.assertEqual(written["noise_floor"]["nv-busy"]["runs"], 2)
        self.assertEqual(written["noise_floor"]["nv-busy"]["build"], "abc1234 OpenGL")
        self.assertEqual(written["noise_floor"]["nv-busy"]["worst"]["worst_tile_bad"], 0.3)
        self.assertRegex(written["noise_floor"]["nv-busy"]["measured"], r"^\d{4}-\d{2}-\d{2}$")

    def test_without_write_the_file_is_left_alone(self):
        first = self.noise("a.json", {"nv-busy": {"worst_tile_bad": 0.5}})
        self.assertEqual(quietly(picturecompare.main, ["limits", str(first), "--limits", str(self.limits)]), 0)
        self.assertEqual(json.loads(self.limits.read_text()), LIMITS)

    def test_a_comparison_of_two_builds_is_no_noise_floor(self):
        other = self.noise("c.json", {"nv-busy": {"worst_tile_bad": 0.5}}, noise=False)
        self.assertEqual(quietly(picturecompare.main, ["limits", str(other), "--limits", str(self.limits), "--write"]), 2)
        self.assertEqual(json.loads(self.limits.read_text()), LIMITS)


# The stand-in engine: it reads its arguments as the engine would, logs the lines of the scenario of a scene and writes one
# picture for it. The picture of `nv-busy` has a dark square in the candidate build.
ENGINE = """
import os, pathlib, sys
sys.path.insert(0, @HERE@)
import picturediff
from test_picturediff import textured, changed
args = sys.argv[1:]
config = pathlib.Path(args[args.index("--config") + 1])
userdata = pathlib.Path(args[args.index("--user-data") + 1])
start = args[args.index("--start") + 1]
assert args[args.index("--random-seed") + 1] == "1"
assert "screenshot format = png" in (config / "settings.cfg").read_text()
scene = {"GSDocMitchellHouse": "nv-interior", "WastelandNV:-18,0": "nv-busy"}[start]
with open(@STARTS@, "a") as log:
    log.write(scene + " @BUILD@\\n")
picture = textured(64, 48)
if "@BUILD@" == "candidate" and scene == "nv-busy":
    picture = changed(picture, lambda x, y, c: (0, 0, 0) if 10 <= x < 30 and 10 <= y < 30 else c)
shots = userdata / "screenshots"
shots.mkdir(parents=True, exist_ok=True)
picturediff.write_png(shots / "screenshot000.png", picture)
lines = ["ROUND\\tnote\\tstill\\tsimulation=60.000", "ROUND\\tshot\\t%s\\tn=1" % scene,
         "%s has been saved" % (shots / "screenshot000.png"), "ROUND\\tdone\\tend\\treal=1.0 steps=3"]
(config / "openfallout.log").write_text("".join("[10:00:00.000 I] %s\\n" % line for line in lines))
"""

# round.py as the tool starts it, on a computer whose guards do not watch a window system and give no rest between runs (a test)
SHIM = """
import importlib.util, sys
spec = importlib.util.spec_from_file_location("round_runner", @ROUND@)
runner = importlib.util.module_from_spec(spec)
sys.modules["round_runner"] = runner
spec.loader.exec_module(runner)
runner.make_system = lambda: runner.roundguard.System()
sys.exit(runner.main(sys.argv[1:]))
"""


@unittest.skipUnless(sys.platform == "darwin", "round.py run works on macOS only")
class WholeRun(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="picture-run-"))
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.starts = self.tmp / "starts.txt"
        for build in ("reference", "candidate"):
            engine = self.tmp / build / "openfallout"
            engine.parent.mkdir()
            body = ENGINE.replace("@HERE@", repr(str(HERE))).replace("@STARTS@", repr(str(self.starts))).replace("@BUILD@", build)
            engine.write_text("#!" + sys.executable + "\n" + textwrap.dedent(body))
            engine.chmod(engine.stat().st_mode | stat.S_IXUSR)
            profile = {"engine": str(engine), "resources": str(self.tmp), "out": str(self.tmp / "out"),
                       "games": {"nv": {"data": [str(self.tmp)], "content": []}}}
            (self.tmp / f"{build}.json").write_text(json.dumps(profile))
        (self.tmp / "shim.py").write_text(SHIM.replace("@ROUND@", repr(str(HERE / "round" / "round.py"))))
        self.runner = f"{sys.executable} {self.tmp / 'shim.py'}"

    def test_every_scene_is_drawn_on_both_builds_one_at_a_time_and_compared(self):
        code = quietly(picturecompare.main, ["run", "--reference", str(self.tmp / "reference.json"), "--candidate",
                                             str(self.tmp / "candidate.json"), "--scene", "nv-interior", "--scene", "nv-busy",
                                             "--out", str(self.tmp / "results"), "--runner", self.runner])
        self.assertEqual(code, 1)
        self.assertEqual(self.starts.read_text().split("\n")[:-1],
                         ["nv-interior reference", "nv-interior candidate", "nv-busy reference", "nv-busy candidate"])
        (folder,) = (self.tmp / "results").iterdir()
        results = {row["scene"]: row for row in json.loads((folder / "report" / "results.json").read_text())["scenes"]}
        self.assertEqual(results["nv-interior"]["verdict"], "PASS")
        self.assertEqual(results["nv-busy"]["verdict"], "FLAG")
        self.assertTrue((folder / "report" / "nv-busy.html").exists())
        self.assertTrue((folder / "rounds" / "nv-busy.lua").exists())
        self.assertFalse(json.loads((folder / "report" / "results.json").read_text())["noise"])

    def test_a_build_against_itself_gives_the_noise_floor(self):
        code = quietly(picturecompare.main, ["noise", "--profile", str(self.tmp / "candidate.json"), "--scene", "nv-busy",
                                             "--out", str(self.tmp / "results"), "--runner", self.runner])
        self.assertEqual(code, 0)
        (folder,) = (self.tmp / "results").iterdir()
        report = json.loads((folder / "report" / "results.json").read_text())
        self.assertTrue(report["noise"])
        self.assertEqual(report["scenes"][0]["measures"]["max_difference"], 0)
        self.assertEqual(self.starts.read_text().split("\n")[:-1], ["nv-busy candidate", "nv-busy candidate"])


if __name__ == "__main__":
    unittest.main()
