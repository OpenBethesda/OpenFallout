#!/usr/bin/env python3
"""Tests of files/data/scripts/omw/falloutspecialscreen.lua, the screens on which the player sets SPECIAL by turning the
pages of a model, run with lupa (skipped where it is not installed). The engine's modules are stood in for: the scene
records what the screen says to it, and the rules of the screen (core.specialScreen) record which of them a button
reached.

The scene's camera stands at (0, 0, 1) and looks along -Z with +Y up, so a direction of the model that the screen's turn
takes to +Z faces the camera and one it takes to +Y is up in the picture."""

import unittest
from pathlib import Path

try:
    from lupa.luajit21 import LuaRuntime
except ImportError:  # pragma: no cover
    # lupa's wheels for Apple Silicon have no LuaJIT runtime; Lua 5.2 accepts what LuaJIT accepts here
    try:
        from lupa.lua52 import LuaRuntime
    except ImportError:
        LuaRuntime = None

SCRIPTS = Path(__file__).resolve().parents[2] / "files" / "data" / "scripts" / "omw"

BUTTONS = {
    "A": 0, "B": 1, "X": 2, "Y": 3, "Back": 4, "Guide": 5, "Start": 6, "LeftStick": 7, "RightStick": 8,
    "LeftShoulder": 9, "RightShoulder": 10, "DPadUp": 11, "DPadDown": 12, "DPadLeft": 13, "DPadRight": 14,
    "LeftTrigger": 100, "RightTrigger": 101,
}
KEYS = {"UpArrow": 1, "DownArrow": 2, "LeftArrow": 3, "RightArrow": 4, "A": 14}

BOOK_MENU = 1060

STUBS = r"""
local calls, rules = test_calls, test_rules

local function record(...) calls[#calls + 1] = { ... } end

local vector3 = {}
vector3.__index = vector3
vector3.__mul = function(a, b)
    if type(a) == 'number' then a, b = b, a end
    return setmetatable({ x = a.x * b, y = a.y * b, z = a.z * b }, vector3)
end
local function v3(x, y, z) return setmetatable({ x = x, y = y, z = z }, vector3) end

local menuScene = setmetatable({
    picked = function() return '', 0 end,
}, { __index = function(_, name) return function(...) record(name, ...) end end })

local screen = {}
for _, name in ipairs({ 'next', 'previous', 'increase', 'decrease', 'moveCursor', 'exit', 'press' }) do
    screen[name] = function(self, ...) rules[#rules + 1] = name return {} end
end
function screen:setPad(on) end
function screen:shapes() return {} end
function screen:buttons() return {} end
function screen:view() return { sequence = '', time = 0, left = 0 } end
function screen:opening() return '' end
function screen:update() end

package.preload['openfallout.core'] = function()
    return {
        menuScene = menuScene,
        specialScreen = { new = function() return screen end },
        chargen = { adjust = function() return true end, finish = function() end },
    }
end
package.preload['openfallout.input'] = function()
    return { KEY = test_keys, CONTROLLER_BUTTON = test_buttons }
end
package.preload['openfallout.ui'] = function()
    return { screenSize = function() return { x = 1280, y = 720 } end }
end
package.preload['openfallout.util'] = function() return { vector3 = v3 } end
package.preload['scripts.omw.falloutmenu'] = function()
    local shown = {}
    function shown:set() end
    function shown:onClick() end
    function shown:close() end
    function shown:playSound() end
    function shown:checkSize() end
    return { open = function() return shown end }
end
package.preload['scripts.omw.falloutpad'] = function() return { releasePick = function() end } end
package.preload['scripts.omw.falloutscene'] = function()
    return {
        available = function() return true end,
        open = function() return { update = function() return true end, close = function() end } end,
    }
end
"""


def apply_rows(rows, point):
    """The point turned by the nine numbers of a matrix, row by row, that turns a column vector (as the scene reads them)"""
    return tuple(sum(rows[3 * i + k] * point[k] for k in range(3)) for i in range(3))


def lua_list(table):
    return [table[i] for i in range(1, len(table) + 1)]


@unittest.skipUnless(LuaRuntime, "needs lupa")
class BookTest(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.calls = self.lua.table()
        self.rules = self.lua.table()
        self.lua.globals().print = lambda *words: None
        self.lua.globals().test_calls = self.calls
        self.lua.globals().test_rules = self.rules
        self.lua.globals().test_keys = self.lua.table_from(KEYS)
        self.lua.globals().test_buttons = self.lua.table_from(BUTTONS)
        self.lua.execute(STUBS)
        self.special = self.lua.eval("function(source) return load(source, 'falloutspecialscreen')() end")(
            (SCRIPTS / "falloutspecialscreen.lua").read_text(encoding="utf-8"))
        attributes = self.lua.table_from([self.lua.table_from({"value": 5}) for _ in range(7)])
        page = self.lua.table_from({"menu": BOOK_MENU, "limit": 40})
        self.assertTrue(self.special.start(page, self.lua.table_from({"attributes": attributes})))

    def scene_calls(self, name):
        return [lua_list(call)[1:] for call in lua_list(self.calls) if call[1] == name]

    def book_place(self):
        places = [args for args in self.scene_calls("setPlace") if args[1] == "book"]
        self.assertEqual(len(places), 1)
        place = places[0][2]
        position = place["position"]
        return lua_list(place["rows"]), (position.x, position.y, position.z), place["scale"]

    def test_the_book_turns_its_front_to_the_camera(self):
        # The model's front is its -Y (the side its own lamp is on, the way the models of the game face)
        rows, _, _ = self.book_place()
        self.assertGreater(apply_rows(rows, (0, -1, 0))[2], 0.99)

    def test_the_book_stands_the_right_way_up(self):
        rows, _, _ = self.book_place()
        self.assertGreater(apply_rows(rows, (0, 0, 1))[1], 0.99)

    def test_the_lamp_of_the_book_is_between_the_camera_and_the_book_and_above_it(self):
        rows, position, scale = self.book_place()
        lights = [args for args in self.scene_calls("addLight") if args[1] == "book"]
        self.assertEqual(len(lights), 1)
        offset = lights[0][3]
        turned = apply_rows(rows, (offset.x, offset.y, offset.z))
        lamp = tuple(position[i] + scale * turned[i] for i in range(3))
        self.assertGreater(lamp[2], position[2])
        self.assertLess(lamp[2], 1.0)
        self.assertGreater(lamp[1], position[1])

    def press(self, button):
        self.special.pad(BUTTONS[button])
        return lua_list(self.rules)

    def test_the_triggers_turn_the_pages_as_the_menu_file_ties_them(self):
        self.assertEqual(self.press("RightTrigger"), ["next"])
        self.assertEqual(self.press("LeftTrigger"), ["next", "previous"])

    def test_the_shoulders_turn_the_pages_too(self):
        self.assertEqual(self.press("RightShoulder"), ["next"])
        self.assertEqual(self.press("LeftShoulder"), ["next", "previous"])

    def test_the_dpad_sets_the_points_and_x_leaves(self):
        self.assertEqual(self.press("DPadRight"), ["increase"])
        self.assertEqual(self.press("DPadLeft"), ["increase", "decrease"])
        self.assertEqual(self.press("X"), ["increase", "decrease", "exit"])


if __name__ == "__main__":
    unittest.main()
