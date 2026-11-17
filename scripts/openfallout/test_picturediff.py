#!/usr/bin/env python3
"""Tests of picturediff.py, the comparison of two pictures of one scene: the PNG reader and writer, each measure on
synthetic pictures (what a second renderer may legitimately change must pass, what must be caught must be flagged), the
verdict against limits, the limits proposed from the noise floor, and the difference picture. Standard library only."""
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import picturediff  # noqa: E402

METHOD = {"pixel_tolerance": 24, "tile": 32, "block": 8}
ALLOWANCE = {"bad_share": 0.01, "worst_tile_bad": 0.10, "mean_shift": 2.0, "worst_tile_shift": 12.0, "ssim": 0.97,
             "worst_tile_ssim": 0.75}


def textured(width=128, height=96, seed=1):
    """A picture with structure: a gradient, a checker of two greys and some colour, like a textured wall."""
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            checker = 40 if ((x // 4) + (y // 4)) % 2 else 0
            noise = (x * 31 + y * 17 + seed * 7) % 13
            row += bytes(((x * 2 + checker + noise) % 256, (y * 2 + checker) % 256, (100 + checker + noise * 3) % 256))
        rows.append(bytes(row))
    return picturediff.Picture(width, height, rows)


def changed(picture, function):
    """A copy with function(x, y, (r, g, b)) -> (r, g, b) applied to every pixel."""
    rows = []
    for y, row in enumerate(picture.rows):
        out = bytearray(row)
        for x in range(picture.width):
            r, g, b = function(x, y, (row[3 * x], row[3 * x + 1], row[3 * x + 2]))
            out[3 * x:3 * x + 3] = bytes((max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))))
        rows.append(bytes(out))
    return picturediff.Picture(picture.width, picture.height, rows)


def judge(reference, candidate, limits=ALLOWANCE):
    result = picturediff.measure(reference, candidate, METHOD)
    return result, picturediff.verdict(result.measures, limits)


class Png(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def test_a_written_picture_reads_back_the_same(self):
        picture = textured(37, 21)
        path = self.tmp / "a.png"
        picturediff.write_png(path, picture)
        back = picturediff.read_png(path)
        self.assertEqual((back.width, back.height), (37, 21))
        self.assertEqual(back.rows, picture.rows)

    def png(self, width, height, colour_type, raw_rows, filters):
        """A PNG written by hand with the given filter type on each row, as a libpng writer may choose."""
        channels = {0: 1, 2: 3, 4: 2, 6: 4}[colour_type]
        bpp = channels
        data = bytearray()
        previous = bytes(width * bpp)
        for row, kind in zip(raw_rows, filters):
            out = bytearray()
            for i, value in enumerate(row):
                left = row[i - bpp] if i >= bpp else 0
                up = previous[i]
                upleft = previous[i - bpp] if i >= bpp else 0
                if kind == 0:
                    predicted = 0
                elif kind == 1:
                    predicted = left
                elif kind == 2:
                    predicted = up
                elif kind == 3:
                    predicted = (left + up) // 2
                else:
                    p = left + up - upleft
                    pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                    predicted = left if pa <= pb and pa <= pc else up if pb <= pc else upleft
                out.append((value - predicted) & 255)
            data += bytes((kind,)) + out
            previous = row

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
        header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
        # the image data split over two chunks, as writers do for big pictures
        packed = zlib.compress(bytes(data))
        path = self.tmp / f"hand-{colour_type}.png"
        path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", packed[:5]) + chunk(b"IDAT", packed[5:])
                         + chunk(b"IEND", b""))
        return path

    def test_every_filter_type_and_colour_type_is_read(self):
        width, height = 6, 5
        rgba = [bytes(v for x in range(width) for v in (x * 40 + y, y * 50, (x * y * 13) % 256, 200)) for y in range(height)]
        path = self.png(width, height, 6, rgba, [0, 1, 2, 3, 4])
        picture = picturediff.read_png(path)
        expected = [bytes(v for x in range(width) for v in (x * 40 + y, y * 50, (x * y * 13) % 256)) for y in range(height)]
        self.assertEqual(picture.rows, expected)
        grey = [bytes((x * 30 + y * 7) % 256 for x in range(width)) for y in range(height)]
        picture = picturediff.read_png(self.png(width, height, 0, grey, [4, 3, 2, 1, 0]))
        self.assertEqual(picture.rows[2], bytes(v for x in range(width) for v in ((x * 30 + 14) % 256,) * 3))

    def test_what_is_not_a_plain_8_bit_png_is_refused_with_a_reason(self):
        path = self.tmp / "x.png"
        path.write_bytes(b"not a picture")
        with self.assertRaises(picturediff.PictureError):
            picturediff.read_png(path)
        with self.assertRaises(picturediff.PictureError):
            picturediff.load_picture(self.tmp / "missing.png")


class Measures(unittest.TestCase):
    def test_the_same_picture_is_a_pass_with_perfect_numbers(self):
        picture = textured()
        result, (verdict, reasons) = judge(picture, picture)
        self.assertEqual(verdict, "PASS", reasons)
        self.assertEqual(result.measures["bad_share"], 0)
        self.assertEqual(result.measures["max_difference"], 0)
        self.assertAlmostEqual(result.measures["ssim"], 1.0)
        self.assertAlmostEqual(result.measures["worst_tile_ssim"], 1.0)

    def test_last_bit_rounding_everywhere_passes(self):
        # what two drivers' rounding may change: a level or two in each channel, in a pattern
        reference = textured()
        candidate = changed(reference, lambda x, y, c: tuple(v + ((x + y + i) % 3 - 1) for i, v in enumerate(c)))
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "PASS", reasons)
        self.assertLess(result.measures["mean_shift"], 1.0)

    def test_scattered_filtering_differences_pass(self):
        # texture filtering of another renderer: a few single pixels far off, spread over the picture
        reference = textured()
        candidate = changed(reference, lambda x, y, c: tuple(v + 60 for v in c) if (x * 7 + y * 13) % 211 == 0 else c)
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "PASS", reasons)
        self.assertGreater(result.measures["bad_share"], 0)

    def test_a_small_missing_object_is_flagged_although_the_whole_picture_mean_hardly_moves(self):
        # a 12 x 12 object (a sign, a lamp) is drawn black in the reference and is not there in the candidate
        reference = changed(textured(), lambda x, y, c: (10, 10, 10) if 50 <= x < 62 and 40 <= y < 52 else c)
        candidate = textured()
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertLess(result.measures["mean_shift"], ALLOWANCE["mean_shift"])
        self.assertEqual(verdict, "FLAG")
        self.assertTrue(any(reason.startswith("worst_tile_bad") for reason in reasons), reasons)
        # the window that holds the object is the one that is named
        x, y, w, h = result.worst["worst_tile_bad"]
        self.assertTrue(x <= 50 and y <= 40 and x + w >= 62 and y + h >= 52, result.worst["worst_tile_bad"])

    def test_a_wrong_texture_on_one_wall_is_flagged(self):
        # a region with another pattern of the same brightness: the mean does not see it, the structure does
        reference = textured()
        candidate = changed(reference, lambda x, y, c: ((c[0] + 128) % 256, c[2], c[1]) if x >= 64 and y >= 48 else c)
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "FLAG")
        self.assertLess(result.measures["worst_tile_ssim"], ALLOWANCE["worst_tile_ssim"])

    def test_a_colour_shift_of_the_whole_picture_is_flagged(self):
        reference = textured()
        candidate = changed(reference, lambda x, y, c: (c[0] + 6, c[1], c[2] - 6))
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "FLAG")
        self.assertTrue(any(reason.startswith("mean_shift") for reason in reasons), reasons)
        self.assertEqual(result.measures["bad_share"], 0)

    def test_blown_out_lighting_is_flagged(self):
        reference = textured()
        candidate = changed(reference, lambda x, y, c: tuple(int(v * 1.6) + 30 for v in c) if y < 60 else c)
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "FLAG")
        self.assertGreater(result.measures["worst_tile_shift"], ALLOWANCE["worst_tile_shift"])

    def test_missing_shadows_are_flagged(self):
        # the reference has a soft dark band (a shadow) across the ground; the candidate has none
        reference = changed(textured(), lambda x, y, c: tuple(v // 2 for v in c) if 20 <= x - y // 2 < 50 else c)
        candidate = textured()
        result, (verdict, reasons) = judge(reference, candidate)
        self.assertEqual(verdict, "FLAG")
        self.assertGreater(result.measures["worst_tile_shift"], ALLOWANCE["worst_tile_shift"])

    def test_pictures_of_another_size_cannot_be_compared(self):
        with self.assertRaises(picturediff.PictureError):
            picturediff.measure(textured(64, 64), textured(64, 48), METHOD)

    def test_an_ignored_region_does_not_count(self):
        reference = textured()
        candidate = changed(reference, lambda x, y, c: (255, 255, 255) if y < 16 else c)
        self.assertEqual(judge(reference, candidate)[1][0], "FLAG")
        masked = picturediff.ignore_regions(reference, candidate, [[0.0, 0.0, 1.0, 0.2]])
        result, (verdict, reasons) = judge(reference, masked)
        self.assertEqual(verdict, "PASS", reasons)
        self.assertEqual(result.measures["max_difference"], 0)

    def test_a_picture_whose_size_is_no_multiple_of_the_tile_is_measured_to_its_edge(self):
        reference = textured(70, 45)
        candidate = changed(reference, lambda x, y, c: (0, 0, 0) if x >= 62 and y >= 37 else c)
        result, (verdict, _) = judge(reference, candidate)
        self.assertEqual(verdict, "FLAG")
        x, y, w, h = result.worst["worst_tile_bad"]
        self.assertEqual((x + w, y + h), (70, 45))


class Verdict(unittest.TestCase):
    def test_each_limit_is_checked_in_its_own_direction(self):
        measures = {"bad_share": 0.0, "worst_tile_bad": 0.0, "mean_shift": 0.0, "worst_tile_shift": 0.0, "ssim": 1.0,
                    "worst_tile_ssim": 1.0, "max_difference": 200}
        self.assertEqual(picturediff.verdict(measures, ALLOWANCE), ("PASS", []))
        verdict, reasons = picturediff.verdict(dict(measures, ssim=0.5, mean_shift=3.0), ALLOWANCE)
        self.assertEqual(verdict, "FLAG")
        self.assertEqual([reason.split()[0] for reason in reasons], ["mean_shift", "ssim"])

    def test_a_limit_that_is_not_given_is_not_checked(self):
        self.assertEqual(picturediff.verdict({"bad_share": 1.0}, {}), ("PASS", []))

    def test_the_limits_of_a_scene_must_name_every_measure_and_no_other(self):
        file = {"method": {}, "allowance": dict(ALLOWANCE), "scenes": {"whole": dict(ALLOWANCE)}}
        self.assertEqual(picturediff.limits_of(file, "whole"), ({}, ALLOWANCE, True))
        self.assertEqual(picturediff.limits_of(file, "other"), ({}, ALLOWANCE, False))
        misspelled = dict(ALLOWANCE)
        misspelled["ssim_"] = misspelled.pop("ssim")
        for scenes in ({"bad": misspelled}, {"bad": {"ssim": 0.9}}):
            with self.assertRaises(picturediff.PictureError) as raised:
                picturediff.limits_of(dict(file, scenes=scenes), "bad")
            self.assertIn("ssim", str(raised.exception))
        with self.assertRaises(picturediff.PictureError):
            picturediff.limits_of(dict(file, allowance=misspelled), "other")


class ProposedLimits(unittest.TestCase):
    MARGIN = {"factor": 1.5, "pad": {"bad_share": 0.002, "worst_tile_bad": 0.02, "mean_shift": 0.5, "worst_tile_shift": 3.0,
                                     "ssim": 0.005, "worst_tile_ssim": 0.03}}

    def test_a_quiet_scene_keeps_the_allowance(self):
        floor = [{"bad_share": 0.0, "worst_tile_bad": 0.0, "mean_shift": 0.0, "worst_tile_shift": 0.0, "ssim": 1.0, "worst_tile_ssim": 1.0}]
        self.assertEqual(picturediff.propose_limits(floor, ALLOWANCE, self.MARGIN), ALLOWANCE)

    def test_a_noisy_scene_gets_its_floor_with_the_margin_above_it(self):
        floor = [{"bad_share": 0.004, "worst_tile_bad": 0.30, "mean_shift": 0.2, "worst_tile_shift": 20.0, "ssim": 0.99,
                  "worst_tile_ssim": 0.6},
                 {"bad_share": 0.002, "worst_tile_bad": 0.10, "mean_shift": 0.1, "worst_tile_shift": 10.0, "ssim": 0.995,
                  "worst_tile_ssim": 0.7}]
        limits = picturediff.propose_limits(floor, ALLOWANCE, self.MARGIN)
        self.assertEqual(limits["bad_share"], 0.01)                     # 0.004 * 1.5 + 0.002 = 0.008, under the allowance
        self.assertAlmostEqual(limits["worst_tile_bad"], 0.47)          # the worse of the runs, 0.30 * 1.5 + 0.02
        self.assertAlmostEqual(limits["worst_tile_shift"], 33.0)
        self.assertEqual(limits["ssim"], 0.97)                          # the deficit 0.01 * 1.5 + 0.005 = 0.02 < 0.03
        self.assertAlmostEqual(limits["worst_tile_ssim"], 1 - (0.4 * 1.5 + 0.03))

    def test_a_proposed_lower_limit_never_goes_below_zero(self):
        floor = [{"worst_tile_ssim": -0.9}]
        self.assertEqual(picturediff.propose_limits(floor, {"worst_tile_ssim": 0.75}, self.MARGIN)["worst_tile_ssim"], -1.0)


class DifferencePicture(unittest.TestCase):
    def test_bad_pixels_are_red_and_the_rest_a_dim_copy(self):
        reference = textured(64, 64)
        candidate = changed(reference, lambda x, y, c: (255, 255, 255) if x == 10 and y == 20 else c)
        result = picturediff.measure(reference, candidate, METHOD)
        picture = picturediff.difference_picture(candidate, result, METHOD, outline=[])
        self.assertEqual((picture.width, picture.height), (64, 64))
        self.assertEqual(picture.pixel(10, 20), (255, 40, 40))
        r, g, b = picture.pixel(0, 0)
        self.assertEqual(r, g)
        self.assertLess(r, 128)

    def test_the_windows_given_are_outlined(self):
        reference = textured(64, 64)
        result = picturediff.measure(reference, reference, METHOD)
        picture = picturediff.difference_picture(reference, result, METHOD, outline=[(16, 16, 32, 32)])
        self.assertEqual(picture.pixel(16, 30), picturediff.OUTLINE)
        self.assertEqual(picture.pixel(47, 47), picturediff.OUTLINE)
        self.assertNotEqual(picture.pixel(30, 30), picturediff.OUTLINE)


if __name__ == "__main__":
    unittest.main()
