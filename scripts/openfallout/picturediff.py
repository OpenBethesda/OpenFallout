#!/usr/bin/env python3
"""Compares two pictures of one scene, drawn by two builds of the engine, and says whether a person needs to look.

    picturediff.py REFERENCE.png CANDIDATE.png [--limits picture_limits.json] [--scene NAME] [--difference out.png]

Two renderers never agree to the last bit: rounding, texture filtering and dithering move a level or two everywhere and a
few pixels a long way. What must be caught is a wrong texture, a missing object, lighting that is blown out, a colour shift
and missing shadows or water. One number over the whole picture is too weak (a small missing object vanishes in it) and
exact equality is too strict, so the comparison takes several measures (docs/openfallout/picture-comparison.md):

- `bad_share`: the share of pixels whose largest channel difference is above `pixel_tolerance` levels;
- `worst_tile_bad`: the same share in the worst window of `tile` x `tile` pixels (windows overlap by half), so that one small
  object that is wrong fills its window;
- `mean_shift`: the largest difference of the mean of one colour channel over the whole picture (a colour shift, a change of
  the lighting everywhere), in levels of 0 to 255;
- `worst_tile_shift`: the same in the worst window (lighting blown out in one part, a missing shadow);
- `ssim`: the structural similarity of the brightness, the mean over blocks of `block` x `block` pixels (1 is the same
  structure; a wrong or blurred texture lowers it although the brightness may stay);
- `worst_tile_ssim`: the mean structural similarity of the worst window;
- `max_difference`: the largest channel difference of any pixel (only shown, never a limit).

A measure beyond its limit flags the scene. The limits are in one place, `picture_limits.json`. The PNG reader and writer
are written here with `zlib`, so the comparison needs no imaging library (the hosted CI runner has none); a picture that is
not a PNG is read with Pillow where it is installed.
"""
import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
# The measures that have limits, and in which direction a value is worse: "max" (a value above the limit flags) or "min"
LIMITED = (("bad_share", "max"), ("worst_tile_bad", "max"), ("mean_shift", "max"), ("worst_tile_shift", "max"),
           ("ssim", "min"), ("worst_tile_ssim", "min"))
DIRECTION = dict(LIMITED)
# Colours of the difference picture
BAD = (255, 40, 40)
OUTLINE = (255, 220, 0)
# The constants of the structural similarity for 8-bit values (Wang et al. 2004: K1 = 0.01, K2 = 0.03)
C1 = (0.01 * 255) ** 2
C2 = (0.03 * 255) ** 2


class PictureError(Exception):
    """A picture that cannot be read or compared, with the reason."""


class Picture:
    """An RGB picture of 8 bits per channel: `rows` is a list of `height` byte strings of 3 * `width` bytes each."""

    def __init__(self, width, height, rows):
        self.width, self.height, self.rows = width, height, rows

    def pixel(self, x, y):
        row = self.rows[y]
        return row[3 * x], row[3 * x + 1], row[3 * x + 2]


# --------------------------------------------------------------------------------------------------------------------- PNG

def _chunks(data):
    position = len(PNG_SIGNATURE)
    while position + 8 <= len(data):
        length, kind = struct.unpack_from(">I4s", data, position)
        body = data[position + 8:position + 8 + length]
        if len(body) < length:
            raise PictureError("the file ends inside a chunk")
        yield kind, body
        position += 12 + length
        if kind == b"IEND":
            return
    raise PictureError("the file has no end chunk")


def _unfilter(kind, line, previous, bpp):
    """The bytes of one row before the PNG filter `kind` was applied (in place where it can)."""
    size = len(line)
    if kind == 0:
        return line
    if kind == 1:
        for i in range(bpp, size):
            line[i] = (line[i] + line[i - bpp]) & 255
        return line
    if kind == 2:
        return bytearray((a + b) & 255 for a, b in zip(line, previous))
    if kind == 3:
        for i in range(size):
            left = line[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        return line
    if kind == 4:
        for i in range(size):
            if i >= bpp:
                a, c = line[i - bpp], previous[i - bpp]
            else:
                a = c = 0
            b = previous[i]
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        return line
    raise PictureError(f"unknown PNG filter {kind}")


def read_png(path):
    """The picture of a PNG file of 8 bits per channel, grey or colour, with or without alpha (the alpha is dropped)."""
    data = Path(path).read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise PictureError(f"{path} is not a PNG file")
    header, packed = None, bytearray()
    for kind, body in _chunks(data):
        if kind == b"IHDR":
            if len(body) < 13:
                raise PictureError(f"{path}: the header chunk is too short")
            header = struct.unpack(">IIBBBBB", body[:13])
        elif kind == b"IDAT":
            packed += body
    if header is None:
        raise PictureError(f"{path} has no header chunk")
    width, height, depth, colour, _, _, interlace = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour)
    if depth != 8 or channels is None or interlace != 0:
        raise PictureError(f"{path}: only 8-bit grey or colour PNG files without interlacing are read "
                           f"(this one has depth {depth}, colour type {colour}, interlace {interlace})")
    try:
        raw = zlib.decompress(bytes(packed))
    except zlib.error as error:
        raise PictureError(f"{path}: the image data is damaged ({error})") from error
    stride = width * channels
    if len(raw) < height * (stride + 1):
        raise PictureError(f"{path}: the image data is shorter than the picture")
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        line = _unfilter(raw[start], bytearray(raw[start + 1:start + 1 + stride]), previous, channels)
        previous = line
        if channels == 3:
            rows.append(bytes(line))
            continue
        out = bytearray(width * 3)
        if channels in (1, 2):
            grey = line[0::channels]
            out[0::3], out[1::3], out[2::3] = grey, grey, grey
        else:
            out[0::3], out[1::3], out[2::3] = line[0::4], line[1::4], line[2::4]
        rows.append(bytes(out))
    return Picture(width, height, rows)


def write_png(path, picture):
    """Writes an RGB PNG (no filter: what it is used for is looked at, not stored)."""
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(row) for row in picture.rows)
    header = struct.pack(">IIBBBBB", picture.width, picture.height, 8, 2, 0, 0, 0)
    Path(path).write_bytes(PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def load_picture(path):
    """A PNG with the reader above; any other picture with Pillow where it is installed."""
    path = Path(path)
    if not path.is_file():
        raise PictureError(f"{path} does not exist")
    with path.open("rb") as stream:
        if stream.read(8) == PNG_SIGNATURE:
            return read_png(path)
    try:
        from PIL import Image
    except ImportError as error:
        raise PictureError(f"{path} is not a PNG file, and Pillow, which would read it, is not installed") from error
    try:
        with Image.open(path) as image:
            image = image.convert("RGB")
            data = image.tobytes()
            width, height = image.size
    except (OSError, ValueError) as error:
        raise PictureError(f"{path} cannot be read ({error})") from error
    return Picture(width, height, [data[y * width * 3:(y + 1) * width * 3] for y in range(height)])


# -------------------------------------------------------------------------------------------------------------- measures

class Result:
    """What `measure` found: `measures` (name -> number), `worst` (measure -> the window (x, y, w, h) that gave the worst value),
    `windows` (every window: (x, y, w, h), bad share, shift, ssim) and `difference` (the largest channel difference of each
    pixel, row by row)."""

    def __init__(self, measures, worst, windows, difference, width, height):
        self.measures, self.worst, self.windows = measures, worst, windows
        self.difference, self.width, self.height = difference, width, height


def _same_size(reference, candidate):
    if (reference.width, reference.height) != (candidate.width, candidate.height):
        raise PictureError(f"the pictures differ in size ({reference.width}x{reference.height} and "
                           f"{candidate.width}x{candidate.height})")


def ignore_regions(reference, candidate, boxes):
    """A copy of the candidate with the reference's pixels in each box ([x0, y0, x1, y1] as fractions of the picture), so that
    what cannot be held still there (and only that) is not compared."""
    _same_size(reference, candidate)
    rows = [bytearray(row) for row in candidate.rows]
    for x0, y0, x1, y1 in boxes:
        left, right = int(x0 * candidate.width), int(round(x1 * candidate.width))
        top, bottom = int(y0 * candidate.height), int(round(y1 * candidate.height))
        for y in range(max(0, top), min(candidate.height, bottom)):
            rows[y][3 * left:3 * right] = reference.rows[y][3 * left:3 * right]
    return Picture(candidate.width, candidate.height, [bytes(row) for row in rows])


def _starts(count, size, step):
    """The first blocks of the windows along one side: every `step`, and one that ends at the edge."""
    if count <= size:
        return [0]
    starts = list(range(0, count - size + 1, step))
    if starts[-1] != count - size:
        starts.append(count - size)
    return starts


def measure(reference, candidate, method):
    """The measures of two pictures of the same size. `method`: `pixel_tolerance` (levels), `tile` (the side of a window, pixels),
    `block` (the side of a block of the structural similarity, pixels)."""
    _same_size(reference, candidate)
    width, height = reference.width, reference.height
    tolerance, block = int(method["pixel_tolerance"]), int(method["block"])
    across, down = (width + block - 1) // block, (height + block - 1) // block
    count = across * down
    # per block: pixels, bad pixels, the sums of the channel differences, the brightness sums of the similarity
    pixels, bad = [0] * count, [0] * count
    shift = [[0, 0, 0] for _ in range(count)]
    sums = [[0, 0, 0, 0, 0] for _ in range(count)]
    difference = bytearray(width * height)
    largest = 0
    for y in range(height):
        first, second = reference.rows[y], candidate.rows[y]
        base, line = (y // block) * across, y * width
        for column in range(across):
            x0, x1 = column * block, min(width, (column + 1) * block)
            k = base + column
            if first[3 * x0:3 * x1] == second[3 * x0:3 * x1]:
                # the same pixels: no difference, and the brightness of one side is the other's
                s1 = s11 = 0
                for x in range(x0, x1):
                    i = 3 * x
                    lum = (54 * first[i] + 183 * first[i + 1] + 19 * first[i + 2]) >> 8
                    s1 += lum
                    s11 += lum * lum
                entry = sums[k]
                entry[0] += s1
                entry[1] += s1
                entry[2] += s11
                entry[3] += s11
                entry[4] += s11
                pixels[k] += x1 - x0
                continue
            nbad = dr = dg = db = s1 = s2 = s11 = s22 = s12 = 0
            for x in range(x0, x1):
                i = 3 * x
                r1, g1, b1 = first[i], first[i + 1], first[i + 2]
                r2, g2, b2 = second[i], second[i + 1], second[i + 2]
                er, eg, eb = r1 - r2, g1 - g2, b1 - b2
                dr += er
                dg += eg
                db += eb
                d = max(abs(er), abs(eg), abs(eb))
                difference[line + x] = d
                if d > tolerance:
                    nbad += 1
                if d > largest:
                    largest = d
                l1 = (54 * r1 + 183 * g1 + 19 * b1) >> 8
                l2 = (54 * r2 + 183 * g2 + 19 * b2) >> 8
                s1 += l1
                s2 += l2
                s11 += l1 * l1
                s22 += l2 * l2
                s12 += l1 * l2
            pixels[k] += x1 - x0
            bad[k] += nbad
            entry = shift[k]
            entry[0] += dr
            entry[1] += dg
            entry[2] += db
            entry = sums[k]
            entry[0] += s1
            entry[1] += s2
            entry[2] += s11
            entry[3] += s22
            entry[4] += s12
    similarity = []
    for k in range(count):
        n = pixels[k]
        s1, s2, s11, s22, s12 = sums[k]
        mu1, mu2 = s1 / n, s2 / n
        var1, var2, cov = s11 / n - mu1 * mu1, s22 / n - mu2 * mu2, s12 / n - mu1 * mu2
        similarity.append(((2 * mu1 * mu2 + C1) * (2 * cov + C2)) / ((mu1 * mu1 + mu2 * mu2 + C1) * (var1 + var2 + C2)))
    total = width * height
    measures = {
        "bad_share": sum(bad) / total,
        "mean_shift": max(abs(sum(entry[c] for entry in shift)) for c in range(3)) / total,
        "ssim": sum(similarity[k] * pixels[k] for k in range(count)) / total,
        "max_difference": largest,
    }
    # the windows: `tile` pixels on a side, half a window apart
    size = max(1, int(method["tile"]) // block)
    step = max(1, size // 2)
    windows = []
    worst = {"worst_tile_bad": (-1.0, None), "worst_tile_shift": (-1.0, None), "worst_tile_ssim": (2.0, None)}
    for top in _starts(down, size, step):
        for left in _starts(across, size, step):
            n = nbad = 0
            moved = [0, 0, 0]
            similar = 0.0
            for row in range(top, min(down, top + size)):
                for column in range(left, min(across, left + size)):
                    k = row * across + column
                    n += pixels[k]
                    nbad += bad[k]
                    moved[0] += shift[k][0]
                    moved[1] += shift[k][1]
                    moved[2] += shift[k][2]
                    similar += similarity[k] * pixels[k]
            x, y = left * block, top * block
            rect = (x, y, min(width, (left + size) * block) - x, min(height, (top + size) * block) - y)
            values = (nbad / n, max(abs(value) for value in moved) / n, similar / n)
            windows.append((rect,) + values)
            if values[0] > worst["worst_tile_bad"][0]:
                worst["worst_tile_bad"] = (values[0], rect)
            if values[1] > worst["worst_tile_shift"][0]:
                worst["worst_tile_shift"] = (values[1], rect)
            if values[2] < worst["worst_tile_ssim"][0]:
                worst["worst_tile_ssim"] = (values[2], rect)
    for name, (value, _) in worst.items():
        measures[name] = value
    return Result(measures, {name: rect for name, (_, rect) in worst.items()}, windows, difference, width, height)


def verdict(measures, limits):
    """("PASS", []) when every measure that has a limit is within it, else ("FLAG", [one reason for each measure beyond it])."""
    reasons = []
    for name, direction in LIMITED:
        limit = limits.get(name)
        if limit is None or name not in measures:
            continue
        value = measures[name]
        if (direction == "max" and value > limit) or (direction == "min" and value < limit):
            word = "above" if direction == "max" else "below"
            reasons.append(f"{name} {value:.4g} is {word} its limit {limit:.4g}")
    return ("FLAG" if reasons else "PASS"), reasons


def flagged_windows(result, limits):
    """The windows whose own values are beyond the limits of the window measures (for the outlines of the difference picture)."""
    found = []
    for rect, nbad, moved, similar in result.windows:
        if (nbad > limits.get("worst_tile_bad", float("inf")) or moved > limits.get("worst_tile_shift", float("inf"))
                or similar < limits.get("worst_tile_ssim", float("-inf"))):
            found.append(rect)
    return found


def propose_limits(floors, allowance, margin):
    """The limits of one scene from its noise floor: `floors` are the measures of comparisons of one build with itself. A
    measure's limit sits `margin` above the worst value of the floor (value x factor + pad; for a "min" measure the same is
    done to its distance from 1), and never inside the `allowance`, which is what two renderers may legitimately differ by."""
    factor, pads = float(margin.get("factor", 1.5)), margin.get("pad", {})
    limits = {}
    for name, floor_limit in allowance.items():
        values = [floor[name] for floor in floors if name in floor]
        if not values:
            limits[name] = floor_limit
            continue
        pad = float(pads.get(name, 0.0))
        if DIRECTION.get(name, "max") == "max":
            limits[name] = max(floor_limit, round(max(values) * factor + pad, 6))
        else:
            deficit = max(1.0 - value for value in values)
            limits[name] = max(-1.0, min(floor_limit, round(1.0 - (deficit * factor + pad), 6)))
    return limits


# ----------------------------------------------------------------------------------------------------- difference picture

def difference_picture(candidate, result, method, outline=()):
    """A picture of where the two differ: a dim grey copy of the candidate, pixels within the tolerance that differ at all tinted
    blue (stronger the more they differ), pixels beyond it red, and each window of `outline` ((x, y, w, h)) drawn in yellow."""
    tolerance = int(method["pixel_tolerance"])
    width, height, difference = result.width, result.height, result.difference
    rows = []
    for y in range(height):
        source, line = candidate.rows[y], y * width
        out = bytearray(width * 3)
        for x in range(width):
            i = 3 * x
            d = difference[line + x]
            if d > tolerance:
                out[i], out[i + 1], out[i + 2] = BAD
                continue
            grey = ((54 * source[i] + 183 * source[i + 1] + 19 * source[i + 2]) >> 8) * 35 // 100
            out[i] = out[i + 1] = grey
            out[i + 2] = min(255, grey + 40 + 6 * d) if d else grey
        rows.append(out)
    for x0, y0, w, h in outline:
        x1, y1 = min(width, x0 + w) - 1, min(height, y0 + h) - 1
        for y in (y0, y1):
            for x in range(x0, x1 + 1):
                rows[y][3 * x:3 * x + 3] = bytes(OUTLINE)
        for y in range(y0, y1 + 1):
            for x in (x0, x1):
                rows[y][3 * x:3 * x + 3] = bytes(OUTLINE)
    return Picture(width, height, [bytes(row) for row in rows])


# ---------------------------------------------------------------------------------------------------------------- command

def limits_of(limits_file, scene=None):
    """(method, the limits of the scene, whether they were set from runs) from the contents of picture_limits.json: a scene's own
    limits once they have been set from runs, the allowance before that."""
    own = (limits_file.get("scenes") or {}).get(scene) if scene else None
    limits = dict(own or limits_file["allowance"])
    # A scene's own limits take the place of the whole allowance, and a limit that is not given is not checked: a name that
    # is misspelled or left out would switch its measure off without a word.
    unknown = sorted(set(limits) - set(DIRECTION))
    missing = [name for name, _ in LIMITED if name not in limits]
    empty = [name for name, _ in LIMITED if name in limits
             and (isinstance(limits[name], bool) or not isinstance(limits[name], (int, float))
                  or not math.isfinite(limits[name]))]
    if unknown or missing or empty:
        where = f"the limits of the scene {scene}" if own else "the allowance"
        raise PictureError(f"{where} in the limits file: "
                           + "; ".join(part for part in (f"unknown measure(s) {', '.join(unknown)}" if unknown else "",
                                                         f"no limit for {', '.join(missing)}" if missing else "",
                                                         f"no finite number for {', '.join(empty)}" if empty else "") if part))
    return limits_file["method"], limits, own is not None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference")
    parser.add_argument("candidate")
    parser.add_argument("--limits", default=str(Path(__file__).resolve().parent / "picture_limits.json"))
    parser.add_argument("--scene", help="the scene whose limits apply (the allowance otherwise)")
    parser.add_argument("--difference", help="write the difference picture here (PNG)")
    args = parser.parse_args(argv)
    try:
        method, limits, measured = limits_of(json.loads(Path(args.limits).read_text(encoding="utf-8")), args.scene)
        result = measure(load_picture(args.reference), candidate := load_picture(args.candidate), method)
    except PictureError as error:
        print(f"cannot compare: {error}", file=sys.stderr)
        return 2
    outcome, reasons = verdict(result.measures, limits)
    for name, value in result.measures.items():
        limit = limits.get(name)
        print(f"{name:18} {value:10.4f}" + (f"   limit {limit}" if limit is not None else ""))
    print(f"{outcome}" + ("" if measured else " (limits not yet set from runs: the allowance)") +
          "".join(f"\n  {reason}" for reason in reasons))
    if args.difference:
        write_png(args.difference, difference_picture(candidate, result, method, flagged_windows(result, limits)))
    return 0 if outcome == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
