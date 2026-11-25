-- The scenarios of the rounds of step 4 (the player as a Fallout actor that follows the rules of the original games): movement, jump,
-- fall damage, death and level-up, each for New Vegas and Fallout 3 (`scenarios/s4_<name>_<game>.lua` gives the game). Every number
-- is judged against the rule (s4common.lua, the rules spec in docs and the exes) and a wrong one fails the round.
--
-- Where each tolerance comes from (the method of measuring, not what makes a build pass):
--   the speed number (Actor.getCurrentSpeed)   0.05 units a second: the value is one product of float32 settings, so only rounding is left
--   the distance covered on the ground         5 %: the window starts 1 s after the first step (the ramp is over), lasts 3 to 4 s, and
--                                              is the sum of the steps in x and y, so a slope or a bump does not count; a frame is a
--                                              step of 3 (walk) to 11 (run) units at 30 frames a second, 1 % of the 300 to 1,300 units
--                                              of the window; the spec keeps 5 % until it is known that the speed number is the ground
--                                              speed (movement.md, unclear item 1), and the round logs both numbers and their ratio
--   the jump                                   +-8 units of 64: the apex of a continuous parabola against the steps of the physics (jump-fall-swim.md J1)
--   the height lost in a fall                  +-6 units: the shape of the character and the frame on which the landing is seen (F1)
--   the damage of a fall                       3 % or 1 point, whichever is larger, of the rule at the height measured (F2)
--   death to reload                            5.0 s +-0.6 s: the dying state lasts up to 2 s before the dead one, and the timer
--                                              starts at the first frame of either (D2)
--   the numbers of levelling                   none: integers and sums of them (stats-levelling.md)
local common = require('scripts.s4common')

local M = {}

-- Gives the player a weapon of the game and puts it in the hand (the first one of the list that the Pip-Boy shows with this record)
local function weaponInHand(r, game)
    local g = common.games[game]
    common.addItem(r, game, g.pistol, 1)
    r.wait(0.5)
    local reply = r.equip({ g.file, g.pistol })
    if reply == nil or reply.ok ~= true then
        r.error('s4 setup the pistol could not be put in the hand: the drawn rows of the round cannot be measured')
        return false
    end
    return true
end

-- Opens the round the same way each time: the controls are the player's, the camera is behind, the player is on the arena, still
local function arrive(r, game)
    r.camera('ThirdPerson')
    r.command('EnablePlayerControls', 'player', {})
    common.goArena(r, game, 'arena')
    r.wait(10)
    r.settle(3, 30)
    r.eye('arena')
end

-- ---------------------------------------------------------------------------------------------------------------------------------------
-- Movement
-- ---------------------------------------------------------------------------------------------------------------------------------------
function M.movement(game)
    return { limit = 1200, delay = 4, run = function(r)
        r.log('s4 round movement game=' .. game)
        r.god(false)
        arrive(r, game)
        local haveWeapon = weaponInHand(r, game)
        r.drawn(false)

        -- one walk of 5 s in a straight line north from the start of the arena, the player put back there first
        local function leg(name, o)
            common.goArena(r, game, name)
            r.wait(3)
            r.drawn(o.drawn == true)
            r.wait(1)
            local stats = r.s4stats(name)
            -- (`rule`: what the rule says of the gait when it is not the one asked for: over the carry weight the run key gives the walk)
            local gait = o.rule or o
            local want = common.speed(game, { drawn = gait.drawn, run = gait.run, sneak = gait.sneak, weight = stats and stats.weight or 0 })
            local res = r.s4move{ label = name, forward = 1, run = o.run, sneak = o.sneak, seconds = o.seconds or 5, skip = 1.0 }
            if res == nil then
                r.error('s4 measure ' .. name .. ' no measurement')
                return nil
            end
            common.check(r, name .. '_api', res.api, want, { abs = 0.05 },
                'the speed number of the game is not the rule\'s')
            common.check(r, name .. '_ground', res.path, want, { rel = 0.05 },
                'the player does not cover the ground at the speed of the rule')
            if res.api and res.api > 0 then
                common.observe(r, name .. '_ground_over_api', res.path / res.api, 'the spec: 1 (unverified: movement.md unclear item 1)')
            end
            return res, want, stats
        end

        leg('walk_holstered', {})
        if haveWeapon then leg('walk_drawn', { drawn = true }) end
        leg('run_holstered', { run = true })
        if haveWeapon then leg('run_drawn', { run = true, drawn = true }) end
        leg('sneak_holstered', { sneak = true })
        if haveWeapon then leg('sneak_drawn', { sneak = true, drawn = true }) end

        -- weight: Fallout 3 loses speed from the first pound (up to 150 lb); New Vegas does not (the same rows, the same numbers
        -- as with nothing carried: the speed function reads the weight only in Fallout 3)
        local stats = r.s4stats('before the weights')
        local heavy = common.games[game].heavy
        common.addItem(r, game, heavy, 1)
        r.wait(1)
        leg('weight_mid_walk_holstered', {})
        stats = r.s4stats('weight mid')
        local more = math.max(1, math.ceil((150 - (stats and stats.weight or 0)) / 50))
        if stats and stats.weight + more * 50 >= stats.maxWeight then more = math.max(1, more - 1) end
        common.addItem(r, game, heavy, more)
        r.wait(1)
        leg('weight_150_walk_holstered', {})
        leg('weight_150_run_holstered', { run = true })

        -- over the carry weight: no running (the run flag is never set), the message of the game once, the walking speed of the weight
        stats = r.s4stats('before the overload')
        r.s4message('sOverEncumbered')      -- the messages so far are not the ones that count
        local over = math.floor(((stats and stats.maxWeight or 200) - (stats and stats.weight or 0)) / 50) + 1
        common.addItem(r, game, heavy, math.max(1, over))
        r.wait(2)
        stats = r.s4stats('overloaded')
        common.flag(r, 'overloaded', stats and stats.weight > stats.maxWeight, true,
            'the round could not put the player over the carry weight (the weights it read are in the line above)')
        local said = r.s4message('sOverEncumbered')
        common.flag(r, 'overencumbered_message', said, true, 'the HUD did not say sOverEncumbered when the weight crossed the carry weight')
        local res, want = leg('overencumbered_run_key', { run = true, rule = { run = false } })
        if res and want then
            local walking = common.speed(game, { weight = stats and stats.weight or 0 })
            common.flag(r, 'overencumbered_ran', res.api ~= nil and res.api > walking * 1.5, false,
                'the player ran over the carry weight')
        end
        r.eye('end')
    end }
end

-- ---------------------------------------------------------------------------------------------------------------------------------------
-- Jump
-- ---------------------------------------------------------------------------------------------------------------------------------------
function M.jump(game)
    return { limit = 600, delay = 4, run = function(r)
        r.log('s4 round jump game=' .. game)
        r.god(false)
        arrive(r, game)
        r.s4stats('baseline')
        local plain = r.s4jump{ label = 'plain', seconds = 3.5 }
        if plain == nil then
            r.error('s4 measure jump no measurement')
            return
        end
        common.flag(r, 'jump_left_the_ground', plain.airborne, true, 'the jump key did not lift the player')
        common.flag(r, 'jump_landed', plain.landed, true, 'the player did not come down within 3.5 s')
        common.check(r, 'jump_apex', plain.apex, 64.0, { abs = 8 }, 'the rule is fJumpHeightMin 64 units times the size, nothing else (J1)')
        r.wait(2)
        common.goArena(r, game, 'again')
        r.wait(3)
        -- three more presses of the key while the player is in the air: the games have no double jump (fJumpDoubleMult is read by nothing)
        local extra = r.s4jump{ label = 'with extra presses', seconds = 4, again = { 0.25, 0.5, 0.75 } }
        if extra == nil then
            r.error('s4 measure jump_extra no measurement')
            return
        end
        common.check(r, 'jump_apex_extra_presses', extra.apex, 64.0, { abs = 8 }, 'a press in the air lifted the player again (a double jump), or the height is wrong')
        common.check(r, 'jump_no_double', extra.apex - plain.apex, 0.0, { abs = 8 }, 'the extra presses changed the height')
        r.eye('end')
    end }
end

-- ---------------------------------------------------------------------------------------------------------------------------------------
-- Fall damage
-- ---------------------------------------------------------------------------------------------------------------------------------------
-- The height lost and the health taken at two drops: 5 m (350 units, below the 600 line: nothing) and 10 m (700 units: 49.8)
function M.fall(game)
    return { limit = 600, delay = 4, run = function(r)
        r.log('s4 round fall game=' .. game)
        r.god(false)
        arrive(r, game)
        local g = common.games[game]
        local function fill()
            local stats = r.s4stats('fill')
            r.command('ForceActorValue', 'player', { 16, stats.maxHealth })
            r.wait(1)
            stats = r.s4stats('filled')
            common.check(r, 'health_known', stats.health, stats.maxHealth, { abs = 0.5 }, 'the health could not be set for the drop')
            return stats
        end
        for _, drop in ipairs({ 350, 700 }) do
            common.goArena(r, game, 'before the drop')
            r.wait(4)
            fill()
            local ground = r.ground('drop ' .. drop, {})
            local z = ground and ground.heights and ground.heights[1]
            if z == nil then
                r.error('s4 measure fall_' .. drop .. ' the ground under the arena was not found')
            else
                local start = z + drop
                local where = { pos = { g.arena.x, g.arena.y, start }, yaw = 0, onGround = false, label = 'drop ' .. drop }
                if g.mojave then
                    for _, id in ipairs(g.mojave) do
                        local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(g.file, id)) end)
                        if ok and cell then where.worldspace = cell break end
                    end
                else
                    where.cellId = r.core.getFormId(g.file, g.cell)
                end
                local res = r.s4drop{ label = 'drop ' .. drop, z = start, teleport = where, hold = 2.0, timeout = 25 }
                if res == nil or not res.landed then
                    r.error('s4 measure fall_' .. drop .. ' the player did not land')
                else
                    local damage = (res.hp0 or 0) - (res.hp1 or 0)
                    local rule = common.fallDamage(res.lost)
                    common.check(r, 'fall_lost_' .. drop, res.lost, drop, { abs = 6 },
                        'the height lost is the start height minus the height at which the player stands (F1)')
                    -- the rule at the height that was lost (the damage moves 0.8 points for each unit of it)
                    common.check(r, 'fall_damage_' .. drop, damage, rule, (drop <= 600) and { abs = 0.01 } or { rel = 0.03, abs = 1.0 },
                        'the damage is not 0.025 * (lost - 600) ^ 1.65 (F2)')
                    local nominal = common.fallDamage(drop)
                    common.observe(r, 'fall_damage_' .. drop .. '_nominal', nominal, 'the rule at the nominal height (the spec: 0 and 49.8)')
                    if drop > 600 then
                        common.check(r, 'fall_damage_' .. drop .. '_vs_nominal', damage, nominal, { abs = 5 },
                            'the damage is not the spec\'s at the nominal height')
                    end
                end
            end
            r.wait(2)
        end
        r.eye('end')
    end }
end

-- ---------------------------------------------------------------------------------------------------------------------------------------
-- Death
-- ---------------------------------------------------------------------------------------------------------------------------------------
-- The player dies at a health below 1 (D1) and, five seconds on, the newest save is loaded without a box (D2, `fPlayerDeathReloadTime`
-- 5). A save is made first (`s4-death`) and a mark says so; when the game loads it the scenario starts again from its first line in the
-- loaded world, finds the mark and says what the loaded player is. The five seconds are judged by round.py (`expect_gap` of the round
-- file: from the line `s4 death t0` to the engine's own `Loading saved game 's4-death'`).
function M.death(game)
    return { limit = 400, delay = 4, run = function(r)
        if r.marked('phase') == 'reloaded' then
            r.log('s4 death the game was loaded again by the death')
            local stats = r.s4stats('reloaded')
            local state = r.s4dead('reloaded')
            common.flag(r, 'death_reloaded_alive', state ~= nil and state.dead == false, true, 'the loaded player is dead')
            if stats then
                common.check(r, 'death_reloaded_health', stats.health, stats.maxHealth, { abs = 1.0 }, 'the loaded player has not the health of the save')
            end
            return
        end
        r.log('s4 round death game=' .. game)
        r.god(false)
        arrive(r, game)
        local stats = r.s4stats('before')
        r.command('ForceActorValue', 'player', { 16, stats.maxHealth })
        r.wait(1)
        r.boxes(false)               -- a box that comes is looked at, not answered
        r.mark('phase', 'reloaded')
        r.saveGame('s4-death')
        r.wait(4)
        r.mark('phase', 'dying')
        local state = r.s4dead('saved')
        common.flag(r, 'death_has_save', state ~= nil and state.hasSave, true, 'the game does not know of the save it just wrote')

        -- D1: a health of 0.5 is dead (the exe kills below 1.0, not at 0)
        r.log('s4 death t0 kind=half_a_point')
        r.command('ForceActorValue', 'player', { 16, 0.5 })
        r.wait(1.5)
        state = r.s4dead('after 0.5')
        local deadAtHalf = state ~= nil and state.dead
        common.flag(r, 'death_below_one_point', deadAtHalf, true, 'the exe kills at a health below 1.0 (D1); a health of 0.5 is alive here')
        if not deadAtHalf then
            -- go on with a health of nothing so that the rest of the sequence is still measured
            r.log('s4 death t0 kind=damage')
            r.command('DamageActorValue', 'player', { 16, 100000 })
        end

        -- D2: no box, and the save loaded 5 s after the death
        local boxSeen, deadline = false, 14
        for i = 1, deadline * 4 do
            r.wait(0.25)
            state = r.s4dead('waiting')
            if state ~= nil and state.box and not boxSeen then
                boxSeen = true
                r.error(string.format('s4 measure death_box=%d expect=0 tol=exact FAIL a box of %d buttons came with fPlayerDeathReloadTime 5; the exe shows it only at 0 or less (D2)',
                    state.buttons, state.buttons))
            end
        end
        r.error(string.format('s4 measure death_reload=none expect=load tol=5.0s+-0.6 FAIL the newest save was not loaded within %d s of the death (box seen: %s)',
            deadline, tostring(boxSeen)))
    end }
end

-- ---------------------------------------------------------------------------------------------------------------------------------------
-- Level-up
-- ---------------------------------------------------------------------------------------------------------------------------------------
function M.levelup(game)
    return { limit = 600, delay = 4, run = function(r)
        r.log('s4 round levelup game=' .. game)
        r.god(true)
        arrive(r, game)
        local base = r.s4stats('baseline')
        if base == nil then return end
        local level = base.level
        local special = function(stats, number, field) return common.attribute(stats, number, field) end

        -- the numbers of the screen of the character as it is: the experience that begins this level and the next, each skill
        common.check(r, 'xp_level_start', base.levelStart, common.xpForLevel(level), { abs = 0 }, 'the experience at which this level begins (1.12)')
        common.check(r, 'xp_to_next_level', base.nextLevel, common.xpForLevel(level + 1), { abs = 0 }, 'the experience at which the next level begins (1.12)')
        local wrong, which = common.skillMismatches(base)
        common.check(r, 'skills_from_special_at_the_start', wrong, 0, { abs = 0 },
            'a skill is not 2 + 2 x its attribute + ceil(Luck / 2) + 15 if tagged (1.9); the first ones: skill:got/wanted ' .. which)

        -- the skills follow the SPECIAL of now: Luck and Intelligence two points up (a temporary change of the game's own command), and back
        r.command('ModActorValue', 'player', { common.LUCK, 2 })
        r.command('ModActorValue', 'player', { common.INTELLIGENCE, 2 })
        r.wait(1)
        local changed = r.s4stats('luck and intelligence +2')
        if changed then
            common.check(r, 'special_changed_luck', special(changed, common.LUCK) or -1, (special(base, common.LUCK) or 0) + 2, { abs = 0 },
                'the command did not change Luck')
            wrong, which = common.skillMismatches(changed)
            common.check(r, 'skills_follow_current_special', wrong, 0, { abs = 0 },
                'the skills must follow the SPECIAL of now (1.9): skill:got/wanted ' .. which)
        end
        r.command('ModActorValue', 'player', { common.LUCK, -2 })
        r.command('ModActorValue', 'player', { common.INTELLIGENCE, -2 })
        r.wait(1)

        -- one level: the experience to the next level, then the screen
        local before = r.s4stats('before the level')
        local need = before.nextLevel - before.experience
        r.command('RewardXP', nil, { need })
        local screen = r.s4levelup('first level', 30)
        local newLevel = level + 1
        if screen == nil then
            r.error('s4 measure levelup_screen=none expect=open tol=exact FAIL no level-up screen opened after the experience of the next level')
            return
        end
        local intelligence = special(before, common.INTELLIGENCE, 'base') or 5
        local endurance = special(before, common.ENDURANCE, 'base') or 5
        common.check(r, 'levelup_skill_points', screen.skillPoints, common.skillPoints(game, intelligence, newLevel), { abs = 0 },
            'the points of the level are not the rule\'s for this Intelligence (1.13)')
        common.flag(r, 'levelup_spent_all', screen.spent == screen.skillPoints, true, 'the points could not be spent on the skills')
        common.check(r, 'levelup_perk_points', screen.perkPoints, common.perkOffered(game, newLevel) and 1 or 0, { abs = 0 },
            'the perk rate is not the rule\'s (1.14)')
        r.wait(2)
        local after = r.s4stats('after the level')
        common.check(r, 'levelup_level', after.level, newLevel, { abs = 0 }, 'the level did not rise by one')
        common.check(r, 'levelup_max_health', after.maxHealth, common.maxHealth(game, endurance, newLevel), { abs = 0.5 },
            'max health = 100 + 20 x Endurance + (level - 1) x 5 (New Vegas) or 10 (Fallout 3) (1.1)')
        common.observe(r, 'levelup_max_health_on_the_screen', screen.maxHealth, 'before the screen closed (the spec leaves open whether it rises first)')
        common.observe(r, 'levelup_health_before_after', string.format('%.2f -> %.2f', before.health, after.health), '(unclear item 4: does the current health rise)')

        -- two levels at once: two screens, the points of each level
        local second = r.s4stats('before two levels')
        local target = common.xpForLevel(newLevel + 2)
        r.command('RewardXP', nil, { target - second.experience })
        local one = r.s4levelup('level ' .. (newLevel + 1), 30)
        r.wait(1)
        local two = r.s4levelup('level ' .. (newLevel + 2), 30)
        if one == nil or two == nil then
            r.error(string.format('s4 measure levelup_two_screens=%s expect=2 tol=exact FAIL the screens of two levels at once did not both open',
                (one and 1 or 0) + (two and 1 or 0)))
        else
            common.check(r, 'levelup2_skill_points', one.skillPoints, common.skillPoints(game, intelligence, newLevel + 1), { abs = 0 }, 'first of two levels (1.13)')
            common.check(r, 'levelup3_skill_points', two.skillPoints, common.skillPoints(game, intelligence, newLevel + 2), { abs = 0 }, 'second of two levels (1.13)')
            common.check(r, 'levelup2_perk_points', one.perkPoints, common.perkOffered(game, newLevel + 1) and 1 or 0, { abs = 0 }, '1.14')
            common.check(r, 'levelup3_perk_points', two.perkPoints, common.perkOffered(game, newLevel + 2) and 1 or 0, { abs = 0 }, '1.14')
        end
        r.wait(2)
        local last = r.s4stats('end')
        common.check(r, 'levelup_level_after_two', last.level, newLevel + 2, { abs = 0 }, 'the level did not rise by two')
        r.eye('end')
    end }
end

return M
