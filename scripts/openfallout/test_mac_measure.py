"""Tests of mac_measure.py: the command lines it builds, what it reads from a run, and the comparison of two runs."""
import argparse
import os
from pathlib import Path
import signal
import sys
import tempfile
import unittest
import unittest.mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mac_measure  # noqa: E402


class SceneCommandTests(unittest.TestCase):
    def test_new_vegas_interior_starts_the_cell_with_the_probe_in_a_private_folder(self):
        command = mac_measure.engine_command(
            engine="/build/openfallout", resources="/build/resources", scene=mac_measure.SCENES["nv-interior"],
            game_data=["/games/nv/Data"], run_dir=Path("/runs/r1"), probe_dir=Path("/repo/perfprobe"))
        self.assertEqual(command, [
            "/build/openfallout", "--resources", "/build/resources",
            "--config", "/runs/r1/config", "--replace", "config", "--user-data", "/runs/r1/userdata",
            "--data-local", "/runs/r1/userdata/data",
            "--data", "/games/nv/Data", "--data", "/repo/perfprobe", "--data", "/runs/r1/data",
            "--content", "FalloutNV.esm", "--content", "perfprobe.omwscripts",
            "--skip-menu", "--no-grab", "--start", "GSDocMitchellHouse",
        ])


class DisplayTests(unittest.TestCase):
    def test_a_window_run_keeps_the_small_window_without_vsync_or_limit(self):
        self.assertEqual(mac_measure.settings_text("window"),
                         "[Video]\nwindow mode = 2\nresolution x = 1280\nresolution y = 720\n"
                         "vsync mode = 0\nframerate limit = 0\n")

    def test_a_full_screen_run_asks_for_full_screen_and_names_no_size_of_its_own(self):
        self.assertEqual(mac_measure.settings_text("fullscreen"),
                         "[Video]\nwindow mode = 0\nresolution x = 1280\nresolution y = 720\n"
                         "vsync mode = 0\nframerate limit = 0\n")

    def test_reads_the_pixels_of_the_main_display(self):
        report = """{"SPDisplaysDataType": [{"spdisplays_ndrvs": [
            {"_name": "Second", "_spdisplays_pixels": "2560 x 1440"},
            {"_name": "Built in", "_spdisplays_pixels": "3456 x 2234", "spdisplays_main": "spdisplays_yes"}]}]}"""
        self.assertEqual(mac_measure.main_display_pixels(report), (3456, 2234))

    def test_no_main_display_in_the_report_gives_nothing(self):
        self.assertIsNone(mac_measure.main_display_pixels('{"SPDisplaysDataType": []}'))
        self.assertIsNone(mac_measure.main_display_pixels("not a report"))

    def test_a_full_screen_run_takes_no_size(self):
        with self.assertRaises(SystemExit), unittest.mock.patch("sys.stderr"):
            mac_measure.main(["run", "nv-interior", "--engine", "/build/openfallout", "--display", "fullscreen",
                              "--size", "1280x720"])


class EngineEnvironmentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.run_dir = Path(temporary.name) / "run"
        self.base = {"PATH": "/usr/bin:/opt/debuggers", "HOME": "/home/x", "SDL_MAC_BACKGROUND_APP": "1"}

    def test_a_window_run_never_comes_to_the_front(self):
        env = mac_measure.engine_environment("window", self.run_dir, base=self.base)
        self.assertEqual(env["SDL_MAC_BACKGROUND_APP"], "1")

    def test_a_full_screen_run_is_an_ordinary_application(self):
        env = mac_measure.engine_environment("fullscreen", self.run_dir, base=self.base)
        self.assertNotIn("SDL_MAC_BACKGROUND_APP", env)

    def test_the_engine_finds_no_debugger_and_keeps_its_crash_log_with_the_run(self):
        for display in ("window", "fullscreen"):
            env = mac_measure.engine_environment(display, self.run_dir, base=self.base)
            self.assertEqual(env["PATH"], str(self.run_dir / "no-programs"))
            self.assertEqual(os.listdir(env["PATH"]), [])
            self.assertEqual(env["TMPDIR"], str(self.run_dir / "tmp"))
            self.assertTrue(Path(env["TMPDIR"]).is_dir())
            self.assertEqual(env["HOME"], "/home/x")

    def test_the_hud_is_asked_for_by_its_variable(self):
        self.assertEqual(mac_measure.engine_environment("window", self.run_dir, hud=True, base=self.base)["MTL_HUD_ENABLED"], "1")
        self.assertNotIn("MTL_HUD_ENABLED", mac_measure.engine_environment("window", self.run_dir, base=self.base))


class PictureSizeTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)

    def test_reads_the_size_of_a_png(self):
        picture = self.folder / "a.png"
        picture.write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\rIHDR" + (3456).to_bytes(4, "big")
                            + (2234).to_bytes(4, "big") + b"\x08\x02\x00\x00\x00")
        self.assertEqual(mac_measure.picture_size(picture), (3456, 2234))

    def test_reads_the_size_of_a_jpeg_past_its_other_segments(self):
        picture = self.folder / "a.jpg"
        picture.write_bytes(b"\xff\xd8" + b"\xff\xe0\x00\x04ab" + b"\xff\xc0\x00\x11\x08"
                            + (2234).to_bytes(2, "big") + (3456).to_bytes(2, "big") + b"\x03" + bytes(9))
        self.assertEqual(mac_measure.picture_size(picture), (3456, 2234))

    def test_a_file_that_is_no_picture_or_is_missing_gives_nothing(self):
        other = self.folder / "a.txt"
        other.write_bytes(b"hello")
        self.assertIsNone(mac_measure.picture_size(other))
        self.assertIsNone(mac_measure.picture_size(self.folder / "missing.png"))

    def test_the_first_picture_of_a_run_is_measured(self):
        shots = self.folder / "userdata" / "screenshots"
        shots.mkdir(parents=True)
        (shots / "screenshot001.png").write_bytes(b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\rIHDR"
                                                  + (640).to_bytes(4, "big") + (480).to_bytes(4, "big") + bytes(5))
        self.assertEqual(mac_measure.run_picture_size(self.folder), (640, 480))
        self.assertIsNone(mac_measure.run_picture_size(self.folder / "elsewhere"))


class CaptureCommandTests(unittest.TestCase):
    def test_time_profile_attaches_to_the_running_engine_for_the_measured_phases(self):
        self.assertEqual(mac_measure.xctrace_command("time", pid=4242, trace=Path("/runs/r1/time.trace"), seconds=70), [
            "xcrun", "xctrace", "record", "--template", "Time Profiler", "--attach", "4242",
            "--time-limit", "70s", "--output", "/runs/r1/time.trace", "--no-prompt",
        ])

    def test_each_capture_kind_names_its_instruments_template(self):
        templates = {kind: mac_measure.xctrace_command(kind, 1, Path("t.trace"), 5)[4]
                     for kind in ("time", "alloc", "game", "metal")}
        self.assertEqual(templates, {"time": "Time Profiler", "alloc": "Allocations",
                                     "game": "Game Performance", "metal": "Metal System Trace"})


PROBE = "L@0x1[scripts/perfprobe.lua]:\tPERF\t"
LOG = f"""[23:59:58.000 I] OpenFallout version 0.52.0
[23:59:58.100 I] Loading content file FalloutNV.esm
[23:59:59.500 I] Starting a new game
[23:59:59.600 I] Loading cell GSDocMitchellHouse
[00:00:01.750 I] {PROBE}begin\tphases=3
[00:00:01.750 I] {PROBE}start\tsettle\tcell=FormId:0x1 world=nil grid=- pos=1,2,3
[00:00:21.750 I] {PROBE}phase\tsettle\tsecs=20.0\tframes=900 mean=22.2 p50=20.0 p95=40.0 p99=80.0 max=300.0 over33=90 over100=3\tfps=45.0\tcell=x
[00:00:51.750 I] {PROBE}phase\tstand\tsecs=30.0\tframes=3000 mean=10.0 p50=9.5 p95=12.5 p99=20.0 max=40.0 over33=2 over100=0\tfps=100.0\tcell=x
[00:01:31.750 I] {PROBE}phase\tturn\tsecs=40.0\tframes=3200 mean=12.5 p50=11.0 p95=18.0 p99=30.0 max=90.0 over33=8 over100=0\tfps=80.0\tcell=x
[00:01:31.760 I] {PROBE}done\thitches=3\tcell=x
"""


class LogTests(unittest.TestCase):
    def test_reads_frame_times_cell_load_and_start_up_across_midnight(self):
        result = mac_measure.read_log(LOG)
        self.assertEqual(result["version"], "OpenFallout version 0.52.0")
        self.assertAlmostEqual(result["cell_load_s"], 2.25)
        self.assertAlmostEqual(result["startup_s"], 3.75)
        self.assertTrue(result["done"])
        self.assertEqual(result["phases"]["stand"], {"secs": 30.0, "frames": 3000, "mean": 10.0, "p50": 9.5, "p95": 12.5,
                                                     "p99": 20.0, "max": 40.0, "over33": 2, "over100": 0, "fps": 100.0})
        self.assertEqual(result["phases"]["turn"]["p95"], 18.0)

    def test_a_run_that_never_reached_the_scene_has_no_cell_load(self):
        result = mac_measure.read_log("[10:00:00.000 I] Starting a new game\n")
        self.assertIsNone(result["cell_load_s"])
        self.assertFalse(result["done"])
        self.assertEqual(result["phases"], {})


FOOTPRINT = """======================================================================
openfallout [4242]: 64-bit    Footprint: 1803 MB (16384 bytes per page)
======================================================================

Auxiliary data:
    phys_footprint: 1803 MB
    phys_footprint_peak: 2.1 GB
"""


class FootprintTests(unittest.TestCase):
    def test_reads_the_footprint_and_its_peak_in_mib(self):
        self.assertEqual(mac_measure.read_footprint(FOOTPRINT), {"footprint_mib": 1803.0, "peak_mib": 2150.4})

    def test_gives_nothing_for_a_process_that_is_gone(self):
        self.assertIsNone(mac_measure.read_footprint("footprint: no process matching 4242\n"))


def result(scene, stand_p50, cell_load, peak, **outcome):
    return {"scene": scene, "cell_load_s": cell_load, "memory": {"peak_mib": peak},
            "done": True, "stopped": None, "exit_status": 0, **outcome,
            "phases": {"stand": {"p50": stand_p50, "p95": stand_p50 * 2}, "turn": {"p50": 10.0, "p95": 20.0}}}


class CompareTests(unittest.TestCase):
    def test_compares_the_medians_of_each_scene_and_metric(self):
        before = [result("nv-busy", 10.0, 2.0, 2000.0), result("nv-busy", 12.0, 3.0, 2100.0),
                  result("nv-busy", 50.0, 2.5, 2050.0)]
        after = [result("nv-busy", 9.0, 2.5, 2050.0)]
        rows = {(row[0], row[1]): row[2:] for row in mac_measure.compare(before, after)}
        self.assertEqual(rows[("nv-busy", "stand p50 ms")], (12.0, 9.0, -25.0))
        self.assertEqual(rows[("nv-busy", "stand p95 ms")], (24.0, 18.0, -25.0))
        self.assertEqual(rows[("nv-busy", "cell load s")], (2.5, 2.5, 0.0))
        self.assertEqual(rows[("nv-busy", "peak footprint MiB")], (2050.0, 2050.0, 0.0))

    def test_runs_that_did_not_finish_cleanly_are_left_out_of_the_medians(self):
        good = result("nv-busy", 10.0, 2.0, 2000.0)
        failed = [result("nv-busy", 90.0, 9.0, 9000.0, stopped="time limit 300 s"),
                  result("nv-busy", 90.0, 9.0, 9000.0, exit_status=-10),
                  result("nv-busy", 90.0, 9.0, 9000.0, done=False)]
        rows = {(row[0], row[1]): row[2:] for row in mac_measure.compare([good] + failed, failed)}
        self.assertEqual(rows[("nv-busy", "stand p50 ms")], (10.0, None, None))
        self.assertEqual(rows[("nv-busy", "cell load s")], (2.0, None, None))

    def test_names_the_runs_it_left_out_and_why(self):
        failed = [result("nv-busy", 90.0, 9.0, 9000.0, stopped="time limit 300 s", run="nv-busy-2"),
                  result("fo3-busy", 90.0, 9.0, 9000.0, exit_status=-10),
                  result("fo3-interior", 90.0, 9.0, 9000.0, done=False)]
        self.assertEqual(mac_measure.left_out([result("nv-busy", 10.0, 2.0, 2000.0)] + failed),
                         ["nv-busy-2: stopped (time limit 300 s)", "fo3-busy: exit status -10",
                          "fo3-interior: the probe did not finish"])

    def test_a_run_without_a_measured_phase_is_left_out_of_every_median(self):
        good = result("nv-busy", 10.0, 2.0, 2000.0)
        no_turn = result("nv-busy", 90.0, 9.0, 9000.0, run="nv-busy-2")
        del no_turn["phases"]["turn"]
        no_stand = result("nv-busy", 90.0, 9.0, 9000.0, run="nv-busy-3")
        del no_stand["phases"]["stand"]
        rows = {(row[0], row[1]): row[2:] for row in mac_measure.compare([good, no_turn, no_stand], [])}
        self.assertEqual(rows[("nv-busy", "stand p50 ms")], (10.0, None, None))
        self.assertEqual(rows[("nv-busy", "cell load s")], (2.0, None, None))
        self.assertEqual(mac_measure.left_out([good, no_turn, no_stand]),
                         ["nv-busy-2: no turn phase", "nv-busy-3: no stand phase"])

    def test_a_scene_measured_on_one_side_only_shows_a_gap(self):
        rows = mac_measure.compare([result("fo3-busy", 10.0, 2.0, 2000.0)], [])
        self.assertIn(("fo3-busy", "stand p50 ms", 10.0, None, None), rows)


class LeftBehindTests(unittest.TestCase):
    """What is still in the engine's session when the engine is gone: its crash reporter, which shows a box."""

    def clear(self, answers):
        """clear_process_group(77) where each killpg call gets the next of `answers` (an exception is raised)."""
        calls, answers = [], iter(answers)

        def killpg(group, sig):
            calls.append((group, sig))
            answer = next(answers)
            if answer is not None:
                raise answer

        with unittest.mock.patch("mac_measure.os.killpg", killpg), unittest.mock.patch("mac_measure.time.sleep"):
            return mac_measure.clear_process_group(77, wait=0.05), calls

    def test_an_empty_session_is_left_alone(self):
        found, calls = self.clear([ProcessLookupError()])
        self.assertFalse(found)
        self.assertEqual(calls, [(77, signal.SIGTERM)])

    def test_what_is_left_is_told_to_end_and_waited_for(self):
        found, calls = self.clear([None, None, ProcessLookupError()])
        self.assertTrue(found)
        self.assertEqual(calls, [(77, signal.SIGTERM), (77, 0), (77, 0)])

    def test_what_does_not_end_is_killed(self):
        calls = []

        def killpg(group, sig):
            calls.append(sig)
            if sig == 0 and signal.SIGKILL in calls:
                raise ProcessLookupError()

        with unittest.mock.patch("mac_measure.os.killpg", killpg), unittest.mock.patch("mac_measure.time.sleep"):
            self.assertTrue(mac_measure.clear_process_group(77, wait=0.01))
        self.assertEqual(calls[0], signal.SIGTERM)
        self.assertIn(signal.SIGKILL, calls)
        self.assertEqual(calls[-1], 0, "it waits for the session to be empty after the kill too")


class FakeProcess:
    """A stand-in for the engine or xctrace: running until it is stopped or `exits()` says so."""

    def __init__(self, pid, exit_after=None, status=0):
        self.pid, self.returncode, self.status = pid, None, status
        self.polls, self.exit_after = 0, exit_after
        self.terminated = False

    def poll(self):
        self.polls += 1
        if self.returncode is None and self.exit_after is not None and self.polls > self.exit_after:
            self.returncode = self.status
        return self.returncode

    def wait(self, timeout=None):
        if self.returncode is None:
            self.returncode = self.status if self.exit_after is not None else -15
        return self.returncode

    def terminate(self):
        self.terminated = True
        self.returncode = -15

    kill = terminate


class RunTests(unittest.TestCase):
    """run_once with stand-ins for the engine, xctrace, the clock and the window-system heartbeat: no game starts."""

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="mac measure ")
        self.addCleanup(temporary.cleanup)
        self.run_dir = Path(temporary.name) / "nv-interior-1"
        self.log = self.run_dir / "config" / "openfallout.log"
        self.killed = []
        self.cleared = []
        self.fds = []
        self.heartbeat = unittest.mock.Mock()
        self.heartbeat.problem.return_value = None
        sys.path.insert(0, str(Path(__file__).resolve().parent / "round"))
        import roundguard
        system = unittest.mock.Mock(heartbeat=True)
        real_openpty = __import__("pty").openpty

        def openpty():
            fds = real_openpty()
            self.fds.extend(fds)
            return fds

        for target, value in (("roundguard.default_system", lambda is_mac: system),
                              ("roundguard.Heartbeat", lambda *a: self.heartbeat),
                              ("pty.openpty", openpty),
                              ("mac_measure.sample_footprint", lambda pid: None),
                              ("mac_measure.machine", lambda: {}),
                              ("mac_measure.os.killpg", lambda pid, sig: self.killed.append((pid, sig))),
                              ("mac_measure.clear_process_group", lambda group: self.cleared.append(group))):
            patcher = unittest.mock.patch(target, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def args(self, capture=None):
        return argparse.Namespace(engine="/build/openfallout", resources="/build/resources", nice=0, hud=False,
                                  capture=capture, limit=300, label="x", commit="c", display="window", panel=(3456, 2234))

    def run_with(self, processes, ticks):
        """run_once where Popen gives `processes` in turn and each half-second tick calls the next of `ticks`."""
        steps = iter(ticks)
        with unittest.mock.patch("mac_measure.subprocess.Popen", side_effect=processes), \
                unittest.mock.patch("mac_measure.time.sleep", side_effect=lambda seconds: next(steps)()):
            return mac_measure.run_once(self.args(capture="time" if len(processes) > 1 else None), "nv-interior",
                                        self.run_dir, ["/games/nv/Data"])

    def write_log(self, text):
        return lambda: self.log.write_text(text)

    def assert_cleaned_up(self):
        self.assertEqual(self.cleared, [4242], "what the engine left in its session is ended")
        self.heartbeat.stop.assert_called_once()
        for fd in self.fds:
            with self.assertRaises(OSError, msg="the pty is closed"):
                os.fstat(fd)

    def interrupt(self):
        raise KeyboardInterrupt

    def test_an_interrupt_stops_the_engine_the_capture_and_the_heartbeat(self):
        engine, xctrace = FakeProcess(4242), FakeProcess(4343)
        with self.assertRaises(KeyboardInterrupt):
            self.run_with([engine, xctrace], [self.write_log(f"{PROBE}start\tstand\n"), self.interrupt])
        self.assertEqual(self.killed[0], (4242, signal.SIGTERM))
        self.assertIsNotNone(engine.returncode, "the engine was reaped")
        self.assertTrue(xctrace.terminated)
        self.assert_cleaned_up()

    def test_an_error_while_watching_also_stops_the_engine(self):
        engine = FakeProcess(4242)

        def broken():
            raise RuntimeError("footprint failed")

        with self.assertRaises(RuntimeError):
            self.run_with([engine], [broken])
        self.assertEqual(self.killed, [(4242, signal.SIGTERM)])
        self.assert_cleaned_up()

    def test_a_capture_that_failed_is_not_recorded_and_its_log_is_named(self):
        engine, xctrace = FakeProcess(4242, exit_after=2), FakeProcess(4343, exit_after=0, status=1)
        result = self.run_with([engine, xctrace], [self.write_log(LOG + f"{PROBE}start\tstand\n"), lambda: None])
        self.assertIsNone(result["capture"])
        self.assertIn("xctrace exited with status 1", result["capture_error"])
        self.assertIn(str(self.run_dir / "xctrace.log"), result["capture_error"])
        self.assertEqual(self.killed, [], "an engine that quit by itself is not signalled")
        self.assert_cleaned_up()

    def test_an_engine_that_ended_by_itself_still_has_its_session_cleared(self):
        # a crashed engine is killed by its own crash reporter, which then shows a box and waits for a click
        self.run_with([FakeProcess(4242, exit_after=1, status=-9)], [lambda: None, lambda: None])
        self.assertEqual(self.killed, [])
        self.assert_cleaned_up()

    def test_a_capture_that_succeeded_is_recorded(self):
        engine, xctrace = FakeProcess(4242, exit_after=2), FakeProcess(4343, exit_after=0, status=0)
        result = self.run_with([engine, xctrace], [self.write_log(LOG + f"{PROBE}start\tstand\n"), lambda: None])
        self.assertEqual(result["capture"], "time.trace")
        self.assertIsNone(result["capture_error"])


    def test_a_run_records_how_it_was_shown(self):
        result = self.run_with([FakeProcess(4242, exit_after=1)], [lambda: None])
        self.assertEqual(result["display"], {"mode": "window", "panel": [3456, 2234], "picture": None})


if __name__ == "__main__":
    unittest.main()
