#!/usr/bin/env python3
"""The picture comparison of two builds (check 1 of the done test of milestone two, docs/openfallout/picture-comparison.md).

It draws the fixed list of scenes (picture_scenes.json) on a reference build (the OpenGL one) and on a candidate build (the
Metal one) through the round runner, hidden, one engine at a time, compares each pair of pictures by machine (picturediff.py)
against the limits of picture_limits.json, and writes a report folder: a summary table, and for each flagged scene a page with
the two pictures and the difference picture side by side. The exit status is 0 when no scene is flagged, 1 when one is (or a
picture is missing), 2 when nothing could be compared.

    picturecompare.py scenes                                            the scene list
    picturecompare.py rounds --out DIR [--scene NAME ...]               writes the round of each scene (to run one by hand)
    picturecompare.py run --reference GL.json --candidate METAL.json    draws every scene on both builds and compares them
    picturecompare.py noise --profile GL.json                           the same build twice: the noise floor of each scene
    picturecompare.py compare REFERENCE_DIR CANDIDATE_DIR --out DIR     compares results that are there, starts nothing
    picturecompare.py limits RESULTS.json ... [--write --build TEXT]    the limits proposed from noise runs (and recorded)

A profile is a profile of the round runner (scripts/openfallout/round/README.md): it names the engine, its resources and the
games. `--runner` is the command that starts round.py (default: this Python and round/round.py), for a computer where every
heavy thing waits its turn in a queue of its own.
"""
import argparse
import datetime
import html
import json
import os
import shlex
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import picturediff  # noqa: E402

SCENES_FILE = HERE / "picture_scenes.json"
LIMITS_FILE = HERE / "picture_limits.json"
TEMPLATE = HERE / "picture_scene.lua"
ROUND_PY = HERE / "round" / "round.py"
# What the measures are called in the tables, in their order
COLUMNS = ("bad_share", "worst_tile_bad", "mean_shift", "worst_tile_shift", "ssim", "worst_tile_ssim", "max_difference")
# round.py's exit statuses that end the whole comparison: a guard stopped the engine (3), the round was refused (4)
STOPPING = {3: "a guard of the round runner stopped the engine", 4: "the round runner refused to start"}


# ------------------------------------------------------------------------------------------------------------------ scenes

def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def select(doc, names):
    """The scenes of the list with these names (all when there are none), in the order of the list."""
    scenes = doc["scenes"]
    if not names:
        return list(scenes)
    unknown = [name for name in names if name not in {scene["name"] for scene in scenes}]
    if unknown:
        raise SystemExit(f"no scene named {', '.join(unknown)} (`picturecompare.py scenes` lists them)")
    return [scene for scene in scenes if scene["name"] in names]


def merged_settings(*layers):
    result = {}
    for layer in layers:
        for section, values in (layer or {}).items():
            result.setdefault(section, {}).update(values)
    return result


def round_of(scene, doc):
    """The round file (a dict) that draws one scene: a new game on the scene's start cell, the common settings with the scene's
    over them, a fixed seed, lossless pictures, and the scenario `<name>.lua` next to it."""
    return {
        "name": scene["name"],
        "game": scene["game"],
        "start": scene["start"],
        "scenario": f"{scene['name']}.lua",
        "resolution": list(doc["resolution"]),
        "framerate": doc["framerate"],
        "limit": doc["limit"],
        "shot_format": "png",
        "settings": merged_settings(doc["settings"], scene.get("settings")),
        "extra_args": ["--random-seed", str(doc["random_seed"])],
        "variants": [{"name": "run"}],
    }


def lua_literal(value):
    """A Lua expression of a JSON value (None leaves the key out)."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    if isinstance(value, str):
        return "'" + value.replace("\\", "\\\\").replace("'", "\\'").replace("\n", "\\n") + "'"
    if isinstance(value, (list, tuple)):
        return "{ " + ", ".join(lua_literal(item) for item in value) + " }"
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{key} = {lua_literal(item)}" for key, item in value.items() if item is not None) + " }"
    raise TypeError(f"no Lua form for {value!r}")


def scenario_of(scene, doc):
    """The scenario of one scene: the scene's table, then picture_scene.lua."""
    hold = doc["hold"]
    table = {"name": scene["name"], "hour": scene.get("hour"), "weather": scene.get("weather"), "place": scene.get("place"),
             "camera": scene["camera"], "forward": hold["forward"], "settle": hold["settle"], "pauseAt": hold["pause_at"],
             "afterPause": hold["after_pause"], "waitPause": hold["wait_pause"], "limit": doc["limit"]}
    return f"local SCENE = {lua_literal(table)}\n" + TEMPLATE.read_text(encoding="utf-8")


def write_rounds(doc, scenes, folder):
    """Writes `<name>.json` and `<name>.lua` of each scene into the folder; gives [(scene, round file)]."""
    folder.mkdir(parents=True, exist_ok=True)
    written = []
    for scene in scenes:
        (folder / f"{scene['name']}.lua").write_text(scenario_of(scene, doc), encoding="utf-8")
        path = folder / f"{scene['name']}.json"
        path.write_text(json.dumps(round_of(scene, doc), indent=1) + "\n", encoding="utf-8")
        written.append((scene, path))
    return written


# ----------------------------------------------------------------------------------------------------------------- results

class Shot:
    """One picture of a round's results with what the comparison needs to know of its run."""

    def __init__(self, report_path, report, entry):
        self.run = report_path.parent
        self.report = report
        self.verdict = report.get("verdict")
        recorded = Path(entry["file"]) if entry.get("file") else None
        # a folder that was moved still has its pictures under shots/
        if recorded is not None and not recorded.is_file():
            recorded = self.run / "shots" / recorded.name
        self.picture = recorded if recorded is not None and recorded.is_file() else None
        settings = self.run / "config" / "settings.cfg"
        self.settings = settings.read_text(encoding="utf-8", errors="replace") if settings.is_file() else None
        self.simulation = None
        for line in report.get("round_lines", []):
            if line.startswith("note\tstill\tsimulation="):
                self.simulation = float(line.split("=", 1)[1].split()[0])


def find_shots(folder):
    """{scene: Shot} of every round report under the folder (the scene is the round's name, and `<round>/<shot>` for a round that
    took more than one picture). Where a scene was run more than once, the newest run counts (the folders begin with the time)."""
    found = {}
    for path in sorted(Path(folder).rglob("report.json")):
        try:
            report = load_json(path)
        except (OSError, ValueError):
            continue
        shots = report.get("shots") or []
        for entry in shots:
            key = report.get("round", path.parent.parent.name)
            if len(shots) > 1:
                key = f"{key}/{entry.get('name')}"
            found[key] = Shot(path, report, entry)
        if not shots and report.get("round"):
            found[report["round"]] = Shot(path, report, {})
    return found


def compare_folders(reference_dir, candidate_dir, out, limits_doc, scenes_doc, noise=False, all_pages=False):
    """Compares the pictures of two folders of results and writes the report into `out`. Gives the rows of the scenes."""
    references, candidates = find_shots(reference_dir), find_shots(candidate_dir)
    order = [scene["name"] for scene in scenes_doc["scenes"]]
    keys = sorted(set(references) | set(candidates), key=lambda key: (order.index(key.split("/")[0])
                                                                      if key.split("/")[0] in order else len(order), key))
    by_name = {scene["name"]: scene for scene in scenes_doc["scenes"]}
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for key in keys:
        scene = by_name.get(key.split("/")[0], {})
        method, limits, from_runs = picturediff.limits_of(limits_doc, key.split("/")[0])
        reference, candidate = references.get(key), candidates.get(key)
        row = {"scene": key, "for": scene.get("for", ""), "verdict": "PASS", "reasons": [], "notes": [], "measures": {},
               "limits": limits, "limits_from_runs": from_runs, "simulation": {}, "page": None}
        for side, shot in (("reference", reference), ("candidate", candidate)):
            if shot is None or shot.picture is None:
                row["reasons"].append(f"no picture from the {side}" + (f" (its round: {shot.verdict})" if shot else ""))
            else:
                row["simulation"][side] = shot.simulation
                if shot.verdict != "PASS":
                    row["reasons"].append(f"the round of the {side} failed ({os.path.relpath(shot.run / 'report.md', out)}):"
                                          " the scene may not have been held still")
        if row["reasons"] and (reference is None or reference.picture is None or candidate is None or candidate.picture is None):
            row["verdict"] = "MISSING"
            rows.append(row)
            continue
        if reference.settings != candidate.settings:
            row["reasons"].append("the two runs had other settings (config/settings.cfg of each run): the profiles or the "
                                  "player's own settings differ")
        times = row["simulation"]
        if times.get("reference") is not None and times.get("candidate") is not None:
            row["notes"].append(f"paused at simulation time {times['reference']:.3f} and {times['candidate']:.3f} s")
        try:
            first, second = picturediff.load_picture(reference.picture), picturediff.load_picture(candidate.picture)
            if scene.get("ignore"):
                second = picturediff.ignore_regions(first, second, [region["box"] for region in scene["ignore"]])
                row["notes"].append("not compared: " + "; ".join(region["why"] for region in scene["ignore"]))
            result = picturediff.measure(first, second, method)
        except picturediff.PictureError as error:
            row["verdict"] = "MISSING"
            row["reasons"].append(str(error))
            rows.append(row)
            continue
        row["measures"] = result.measures
        verdict, reasons = picturediff.verdict(result.measures, limits)
        row["reasons"] = reasons + row["reasons"]
        row["verdict"] = "FLAG" if row["reasons"] else verdict
        if row["verdict"] != "PASS" or all_pages:
            row["page"] = write_page(out, row, reference.picture, candidate.picture,
                                     picturediff.difference_picture(second, result, method,
                                                                    picturediff.flagged_windows(result, limits)), method)
        rows.append(row)
    write_summary(out, rows, noise, limits_doc)
    return rows


# ------------------------------------------------------------------------------------------------------------------ report

PAGE_STYLE = """
body { font-family: -apple-system, system-ui, sans-serif; margin: 16px; background: #fff; color: #111; }
table { border-collapse: collapse; } td, th { border: 1px solid #ccc; padding: 3px 8px; text-align: right; }
td:first-child, th:first-child { text-align: left; }
.FLAG, .MISSING { color: #b00000; font-weight: bold; } .PASS { color: #106010; font-weight: bold; }
.row { display: flex; flex-wrap: wrap; gap: 8px; } .row figure { margin: 0; flex: 1 1 30%; min-width: 280px; }
.row img { width: 100%; image-rendering: pixelated; } figcaption { font-size: 90%; }
@media (prefers-color-scheme: dark) { body { background: #111; color: #eee; } td, th { border-color: #444; } }
"""


def number(value):
    if value is None:
        return "-"
    return f"{value:.0f}" if isinstance(value, int) else f"{value:.4f}"


def write_page(out, row, reference, candidate, difference, method):
    """The page of one scene: the reasons, the measures with their limits and the three pictures side by side. Gives its name."""
    name = row["scene"].replace("/", "-")
    pictures = out / "pictures"
    pictures.mkdir(exist_ok=True)
    shutil.copyfile(reference, pictures / f"{name}-reference{Path(reference).suffix}")
    shutil.copyfile(candidate, pictures / f"{name}-candidate{Path(candidate).suffix}")
    picturediff.write_png(pictures / f"{name}-difference.png", difference)
    measures = "".join(
        f"<tr><td>{column}</td><td>{number(row['measures'].get(column))}</td><td>{number(row['limits'].get(column))}</td></tr>"
        for column in COLUMNS)
    figures = "".join(
        f'<figure><a href="pictures/{name}-{side}{suffix}"><img src="pictures/{name}-{side}{suffix}" alt="{side}"></a>'
        f"<figcaption>{caption}</figcaption></figure>"
        for side, suffix, caption in (("reference", Path(reference).suffix, "reference (OpenGL build)"),
                                      ("candidate", Path(candidate).suffix, "candidate"),
                                      ("difference", ".png", f"difference: red = more than {method['pixel_tolerance']} levels "
                                                             "apart, blue = apart by less, yellow = a window beyond a limit")))
    text = (f"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
            f"<title>{html.escape(row['scene'])}</title><style>{PAGE_STYLE}</style></head><body>"
            f"<p><a href=\"index.html\">all scenes</a></p><h1>{html.escape(row['scene'])}: "
            f"<span class=\"{row['verdict']}\">{row['verdict']}</span></h1><p>{html.escape(row['for'])}</p>"
            + "".join(f"<p>{html.escape(reason)}</p>" for reason in row["reasons"])
            + "".join(f"<p><i>{html.escape(note)}</i></p>" for note in row["notes"])
            + f"<table><tr><th>measure</th><th>value</th><th>limit"
            + ("" if row["limits_from_runs"] else " (allowance: not yet set from runs)")
            + f"</th></tr>{measures}</table><div class=\"row\">{figures}</div></body></html>")
    (out / f"{name}.html").write_text(text, encoding="utf-8")
    return f"{name}.html"


def write_summary(out, rows, noise, limits_doc):
    flagged = [row for row in rows if row["verdict"] != "PASS"]
    title = "Noise floor (one build against itself)" if noise else "Picture comparison"
    head = f"{len(rows)} scene(s), {len(flagged)} flagged or missing"
    lines = [f"# {title}", "", head, "", "| scene | verdict | " + " | ".join(COLUMNS) + " | limits | reasons |",
             "|---" * (len(COLUMNS) + 4) + "|"]
    for row in rows:
        lines.append(f"| {row['scene']} | {row['verdict']} | "
                     + " | ".join(number(row["measures"].get(column)) for column in COLUMNS)
                     + f" | {'from runs' if row['limits_from_runs'] else 'allowance'} | {'; '.join(row['reasons'])} |")
    (out / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    cells = []
    for row in rows:
        name = f'<a href="{row["page"]}">{html.escape(row["scene"])}</a>' if row["page"] else html.escape(row["scene"])
        cells.append(f"<tr><td>{name}</td><td class=\"{row['verdict']}\">{row['verdict']}</td>"
                     + "".join(f"<td>{number(row['measures'].get(column))}</td>" for column in COLUMNS)
                     + f"<td>{'from runs' if row['limits_from_runs'] else 'allowance'}</td>"
                     + f"<td style=\"text-align:left\">{html.escape('; '.join(row['reasons']))}</td></tr>")
    (out / "index.html").write_text(
        f"<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\">"
        f"<title>{title}</title><style>{PAGE_STYLE}</style></head><body><h1>{title}</h1><p>{head}</p>"
        f"<table><tr><th>scene</th><th>verdict</th>" + "".join(f"<th>{column}</th>" for column in COLUMNS)
        + "<th>limits</th><th>reasons</th></tr>" + "".join(cells) + "</table></body></html>", encoding="utf-8")
    (out / "results.json").write_text(json.dumps({"noise": noise, "method": limits_doc["method"], "scenes": rows}, indent=1),
                                      encoding="utf-8")


def report_status(rows, out):
    flagged = [row for row in rows if row["verdict"] != "PASS"]
    for row in rows:
        print(f"{row['scene']:16} {row['verdict']:8} " + "; ".join(row["reasons"]))
    print(f"report: {out / 'index.html'}")
    if not rows:
        print("nothing to compare: no round results in the folders", file=sys.stderr)
        return 2
    return 1 if flagged else 0


# --------------------------------------------------------------------------------------------------------------------- runs

def run_rounds(args, reference_profile, candidate_profile, noise):
    """Runs the round of every scene on the reference build, then on the candidate, scene after scene (one engine at a time: each
    round takes the round runner's lock), then compares."""
    scenes_doc, limits_doc = load_json(args.scenes), load_json(args.limits)
    scenes = select(scenes_doc, args.scene)
    out_root = Path(args.out or Path(load_json(reference_profile).get("out") or ".") / "pictures").expanduser()
    folder = out_root / (time.strftime("%Y%m%d-%H%M%S-") + ("noise" if noise else "pictures"))
    folder.mkdir(parents=True)
    rounds = write_rounds(scenes_doc, scenes, folder / "rounds")
    runner = shlex.split(args.runner) if args.runner else [sys.executable, str(ROUND_PY)]
    print(f"{len(scenes)} scene(s) on {'one build twice' if noise else 'two builds'}, results in {folder}", flush=True)
    stopped = None
    with (folder / "runs.log").open("w", encoding="utf-8") as log:
        for scene, round_file in rounds:
            for side, profile in (("reference", reference_profile), ("candidate", candidate_profile)):
                command = runner + ["--profile", str(profile), "run", str(round_file), "--out", str(folder / side)]
                started = time.monotonic()
                log.write(f"$ {shlex.join(command)}\n")
                log.flush()
                code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False).returncode
                print(f"  {scene['name']} {side}: round exit status {code} in {time.monotonic() - started:.0f} s", flush=True)
                if code in STOPPING:
                    stopped = f"{STOPPING[code]} at {scene['name']} ({side}); see {folder / 'runs.log'}"
                    break
            if stopped:
                break
    rows = compare_folders(folder / "reference", folder / "candidate", folder / "report", limits_doc, scenes_doc, noise,
                           args.all_pages)
    status = report_status(rows, folder / "report")
    if stopped:
        print(f"STOPPED: {stopped}; the scenes after it were not run", file=sys.stderr)
        return 2
    return status


# ------------------------------------------------------------------------------------------------------------------- limits

def command_limits(args):
    """The limits of each scene proposed from the results of noise runs (`noise`), printed; recorded with --write."""
    limits_doc = load_json(args.limits)
    floors = {}
    for path in args.results:
        results = load_json(path)
        if not results.get("noise"):
            print(f"{path} is a comparison of two builds, not of one build with itself: it is no noise floor", file=sys.stderr)
            return 2
        for row in results["scenes"]:
            if row.get("verdict") != "MISSING" and row.get("measures"):
                floors.setdefault(row["scene"], []).append(row["measures"])
    if not floors:
        print("no scene with pictures in these results", file=sys.stderr)
        return 2
    allowance, margin = limits_doc["allowance"], limits_doc["margin"]
    print("| scene | runs | " + " | ".join(allowance) + " |")
    print("|---" * (len(allowance) + 2) + "|")
    proposed = {}
    for scene, measures in floors.items():
        proposed[scene] = picturediff.propose_limits(measures, allowance, margin)
        print(f"| {scene} | {len(measures)} | " + " | ".join(f"{proposed[scene][name]:.4g}" for name in allowance) + " |")
    if args.write:
        for scene, measures in floors.items():
            limits_doc.setdefault("scenes", {})[scene] = proposed[scene]
            worst = {name: (min if picturediff.DIRECTION.get(name) == "min" else max)(m[name] for m in measures if name in m)
                     for name in allowance if any(name in m for m in measures)}
            limits_doc.setdefault("noise_floor", {})[scene] = {
                "measured": datetime.date.today().isoformat(), "runs": len(measures), "build": args.build or "",
                "worst": worst}
        Path(args.limits).write_text(json.dumps(limits_doc, indent=2) + "\n", encoding="utf-8")
        print(f"written to {args.limits}")
    return 0


# --------------------------------------------------------------------------------------------------------------------- main

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    def common(sp):
        sp.add_argument("--scenes", default=str(SCENES_FILE), help="the scene list (default %(default)s)")
        sp.add_argument("--limits", default=str(LIMITS_FILE), help="the limits (default %(default)s)")

    sp = sub.add_parser("scenes", help="list the scenes")
    common(sp)
    sp = sub.add_parser("rounds", help="write the round file and scenario of each scene")
    common(sp)
    sp.add_argument("--out", required=True)
    sp.add_argument("--scene", action="append")
    for name, text in (("run", "draw every scene on both builds and compare"), ("noise", "draw every scene twice on one build")):
        sp = sub.add_parser(name, help=text)
        common(sp)
        if name == "run":
            sp.add_argument("--reference", required=True, help="the round profile of the reference (OpenGL) build")
            sp.add_argument("--candidate", required=True, help="the round profile of the candidate (Metal) build")
        else:
            sp.add_argument("--profile", required=True, help="the round profile of the build")
        sp.add_argument("--scene", action="append", help="only this scene (repeat for more)")
        sp.add_argument("--out", help="the folder of the results (default: `pictures` in the reference profile's `out`)")
        sp.add_argument("--runner", help="the command that starts round.py (default: this Python and round/round.py)")
        sp.add_argument("--all-pages", action="store_true", help="a page with the pictures for every scene, not only the flagged")
    sp = sub.add_parser("compare", help="compare two folders of results, starting nothing")
    common(sp)
    sp.add_argument("reference")
    sp.add_argument("candidate")
    sp.add_argument("--out", required=True, help="the folder of the report")
    sp.add_argument("--noise", action="store_true", help="the two folders are of one build (a noise floor)")
    sp.add_argument("--all-pages", action="store_true")
    sp = sub.add_parser("limits", help="propose the limits from noise runs, and record them with --write")
    common(sp)
    sp.add_argument("results", nargs="+", help="results.json of the report of noise runs")
    sp.add_argument("--write", action="store_true", help="record the proposed limits and the noise floor in the limits file")
    sp.add_argument("--build", help="what was measured (commit and renderer), recorded with the noise floor")
    args = parser.parse_args(argv)

    if args.command == "scenes":
        for scene in load_json(args.scenes)["scenes"]:
            print(f"{scene['name']:14} {scene['kind']:16} --start {scene['start']:20} {scene['for']}")
        return 0
    if args.command == "rounds":
        doc = load_json(args.scenes)
        for _, path in write_rounds(doc, select(doc, args.scene), Path(args.out)):
            print(path)
        return 0
    if args.command == "run":
        return run_rounds(args, Path(args.reference).expanduser(), Path(args.candidate).expanduser(), noise=False)
    if args.command == "noise":
        profile = Path(args.profile).expanduser()
        return run_rounds(args, profile, profile, noise=True)
    if args.command == "compare":
        out = Path(args.out)
        rows = compare_folders(Path(args.reference), Path(args.candidate), out, load_json(args.limits), load_json(args.scenes),
                               args.noise, args.all_pages)
        return report_status(rows, out)
    return command_limits(args)


if __name__ == "__main__":
    sys.exit(main())
