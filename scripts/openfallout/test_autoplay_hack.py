#!/usr/bin/env python3
"""Tests of the autoplay's way of hacking a terminal (autoplay/scripts/autoplayhack.lua). The module runs with lupa (the tests are
skipped where it is not installed) against boards played here as the game plays them: a guess is told its likeness, and four
wrong guesses lock the terminal. Whatever the words, the autoplay never locks a terminal: it leaves a board with one attempt
left when more than one word could still be the password, and a new board gives it its attempts back."""
import random
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

SCRIPT = Path(__file__).resolve().parent / "autoplay" / "scripts" / "autoplayhack.lua"
ATTEMPTS = 4


def likeness(a, b):
    return sum(1 for x, y in zip(a, b) if x == y)


@unittest.skipUnless(LuaRuntime, "needs lupa")
class AutoplayHackTest(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.hack = self.lua.execute(SCRIPT.read_text())

    def table(self, items):
        return self.lua.table_from(items)

    def guesses(self, played):
        return self.table([self.lua.table_from({"word": word, "likeness": same}) for word, same in played])

    def play_board(self, words, password):
        """One board: 'solved', 'left' or 'locked', and the number of guesses."""
        played = []
        attempts = ATTEMPTS
        while True:
            move = self.hack.choose(self.table(words), self.guesses(played), attempts)
            if move["close"]:
                return "closed", len(played)
            if move["leave"]:
                return "left", len(played)
            word = words[move["guess"] - 1]
            if word == password:
                return "solved", len(played) + 1
            played.append((word, likeness(word, password)))
            attempts -= 1
            if attempts == 0:
                return "locked", len(played)

    def board(self, rng, length, count):
        # words that share letters in places, as the game's dictionary words of one length do
        letters = "AEIRSTNLO"
        words = set()
        while len(words) < count:
            words.add("".join(rng.choice(letters) for _ in range(length)))
        words = sorted(words)
        return words, rng.choice(words)

    def test_the_likeness_counts_the_same_letters_in_the_same_places(self):
        self.assertEqual(self.hack.likeness("CRATER", "CRATER"), 6)
        self.assertEqual(self.hack.likeness("CRATER", "GRATES"), 4)
        self.assertEqual(self.hack.likeness("ABC", "XYZ"), 0)

    def test_only_the_words_that_agree_with_every_likeness_are_left(self):
        words = ["TRIES", "TRAIN", "SPINE", "TRITE"]
        left = self.hack.candidates(self.table(words), self.guesses([("TRIES", likeness("TRIES", "TRITE"))]))
        self.assertEqual([words[i - 1] for i in left.values()], ["TRITE"])

    def test_it_never_locks_a_terminal_and_a_few_boards_open_it(self):
        rng = random.Random(1234)
        for _ in range(300):
            length = rng.choice([4, 6, 8, 10, 12])
            count = rng.randint(5, 20)
            opened = False
            for _ in range(5):  # a board left is followed by a new one, as the terminal lays out when it is opened again
                words, password = self.board(rng, length, count)
                result, _ = self.play_board(words, password)
                self.assertNotEqual(result, "locked", (words, password))
                self.assertNotEqual(result, "closed", (words, password))
                if result == "solved":
                    opened = True
                    break
            self.assertTrue(opened)

    def test_the_last_attempt_is_only_used_on_the_one_word_left(self):
        words = ["AAAA", "AAAB", "AABB", "ABBB", "BBBB", "BBBA", "BBAA", "BAAA"]
        move = self.hack.choose(self.table(words), self.guesses([]), 1)
        self.assertTrue(move["leave"])
        played = [("AAAA", likeness("AAAA", "BBBB")), ("AABB", likeness("AABB", "BBBB"))]
        move = self.hack.choose(self.table(words), self.guesses(played), 1)
        self.assertEqual(words[move["guess"] - 1], "BBBB")


if __name__ == "__main__":
    unittest.main()
