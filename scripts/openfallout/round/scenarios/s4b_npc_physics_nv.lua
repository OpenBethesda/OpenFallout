-- Step 4b, NPC physics (New Vegas): a character of the High tier moves on the character controller, the same body as the player's.
-- The character is GS Settler AAM (ACHR 0x104F03, persistent, SpeedMult 100, no template), put where each measure needs him.
--   W  wall    put 100 units from a wall, facing it, his own AI off and driven by his controls (roundactor.lua: self:enableAI(false),
--              self.controls.movement 1): he walks straight into the wall and the solver stops his centre at the capsule radius,
--              31 x scale, from the face
--   F  fall    let go 650 units above the ground: he falls under gravity (1.38 s for 650 units at 9.8 x 69.99 u/s^2) and takes
--              the fall damage of rule F2: 0.025 x (650 - 600)^1.65 = 15.9 (Normal difficulty, an NPC victim)
--   S  speed   walks after the player (a commanded follow, 450 units, a walk, not a run): rule M12, 84.7 u/s holstered x SpeedMult/100
--   T  stairs  the concrete steps of round 15G (22861, -16120): he climbs them after the player and stands on the treads
-- His own AI is off for W (a local script drives him), the world's AI off for F (his package would walk him away), on for S
-- and T. The wall is the nearest one around the Goodsprings ground the player stands on (sixteen rays at the height of a chest).
local NV = 'FalloutNV.esm'
local SETTLER = { NV, 0x104F03 }
local ARENA = { -69660, 1900, 8352 }
local STAIRS = { x = 22861, y = -16120, z = 10027 }
local RADIUS = 31 -- the capsule radius of the exe (spec A), x scale 1
local WALK = 84.7 -- rule M12 at SpeedMult 100, holstered
local GRAVITY = 9.8 * 69.99
local DROP = 650
local FALL_DAMAGE = 0.025 * (DROP - 600) ^ 1.65
local HEALTH, SPEEDMULT = 16, 21

return { limit = 600, delay = 4, run = function(r)
    local h = require('scripts.roundhelpers')(r)
    local core, util = r.core, r.util
    local function phase(name) r.log('PHASE ' .. name) end
    local player = r.player()
    local settler
    local function flat(v) return util.vector3(v.x, v.y, 0) end
    -- puts the settler at a place of the player's worldspace (the exterior cell of that place)
    local function put(pos, yaw, onGround)
        local cell = player.cell.isExterior and r.world.getExteriorCell(math.floor(pos.x / 4096), math.floor(pos.y / 4096), player.cell)
            or player.cell
        settler:teleport(cell, pos, { rotation = util.transform.rotateZ(yaw or 0), onGround = onGround })
    end
    local function groundBelow(x, y, z, label)
        local ray = r.ray({ x, y, z + 300 }, { x, y, z - 3000 }, label)
        return ray and ray.hit and ray.z or nil
    end

    phase('S setup')
    r.god(true)
    local mojave
    for _, id in ipairs({ 0x0DAEB9, 0x0DAEB8, 0x0DAEBB }) do
        local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(NV, id)) end)
        if ok and cell then mojave = cell break end
    end
    if mojave == nil then error('no cell of the Mojave was found') end
    r.teleport{ pos = ARENA, worldspace = mojave, yaw = 0, onGround = true, label = 'arena' }
    r.wait(10)
    r.settle(3, 30)
    r.closeDialogue('outside')
    settler = h.object(SETTLER, 'settler')
    r.log('settler ' .. r.describe(settler))
    local speedMult = h.command('GetActorValue', SETTLER, SPEEDMULT)
    r.log(string.format('settler SpeedMult %.1f, health %.1f, scale %.2f', speedMult, h.command('GetActorValue', SETTLER, HEALTH),
        settler.scale or 1))

    ---------------------------------------------------------------------------------------------------------------- W: wall
    phase('W wall')
    r.ai(false)
    -- the nearest wall around the player (the doctor's house is behind the door's way out): eight rays at the height of a chest
    local eye = player.position
    local best
    for i = 0, 15 do
        local a = i * math.pi / 8
        local dir = util.vector3(math.sin(a), math.cos(a), 0)
        local from = eye + util.vector3(0, 0, 60)
        local to = from + dir * 3000
        local ray = r.ray({ from.x, from.y, from.z }, { to.x, to.y, to.z }, string.format('wall %d', i), { 'World' })
        if ray and ray.hit then
            local d = (flat(util.vector3(ray.x, ray.y, 0)) - flat(from)):length()
            if d > 80 and (best == nil or d < best.d) then best = { d = d, dir = dir, hit = util.vector3(ray.x, ray.y, ray.z) } end
        end
    end
    h.check('w-a-wall-is-found', best ~= nil, true)
    if best then
        -- along the wall: the way on which rays at the same height still meet the wall at about the same distance, every
        -- 100 units, the farthest of the two
        local along, reach = util.vector3(best.dir.y, -best.dir.x, 0), 0
        for _, sign in ipairs({ 1, -1 }) do
            local t = util.vector3(best.dir.y, -best.dir.x, 0) * sign
            local goes = 0
            for step = 1, 4 do
                local from = eye + util.vector3(0, 0, 60) + t * (100 * step)
                local to = from + best.dir * 3000
                local ray = r.ray({ from.x, from.y, from.z }, { to.x, to.y, to.z }, string.format('wall along %d', 100 * step), { 'World' })
                if not (ray and ray.hit and math.abs((flat(util.vector3(ray.x, ray.y, 0)) - flat(from)):length() - best.d) < 40) then
                    break
                end
                goes = 100 * step
            end
            if goes > reach then along, reach = t, goes end
        end
        r.log(string.format('W the wall goes on %d units along', reach))
        h.checkAtLeast('w-the-wall-goes-on-150', reach, 150)
        local function wallGap() return (flat(best.hit) - flat(settler.position)):dot(best.dir) end
        -- the player out of the way, 80 out from the wall and along it; the settler 100 units from the wall, facing it
        local stand = best.hit - best.dir * 80 + along * math.max(reach, 150)
        r.teleport{ pos = { stand.x, stand.y, eye.z }, worldspace = mojave, yaw = 0, onGround = true, label = 'along the wall' }
        r.wait(1)
        local spot = best.hit - best.dir * 100
        put(util.vector3(spot.x, spot.y, eye.z), math.atan2(best.dir.x, best.dir.y), true)
        r.wait(1)
        -- (the game's own time: a slow Mac draws fewer frames in a second)
        local function waitGame(seconds)
            local from = core.getSimulationTime()
            local deadline = core.getRealTime() + 30
            while core.getSimulationTime() - from < seconds and core.getRealTime() < deadline do r.wait(0.05) end
        end
        -- his own AI off and his controls on (a local script, roundactor.lua: self:enableAI(false), self.controls), the
        -- world's AI on: he walks straight ahead into the wall for 2.5 s of the game (84.7 u/s x 2.5 s = 212 units, more
        -- than the 69 to the face), and the solver stops his capsule at its radius, 31 x scale
        settler:addScript('scripts/roundactor.lua')
        r.wait(0.3)
        settler:sendEvent('RoundActorAI', { on = false })
        r.wait(0.3)
        r.ai(true)
        r.wait(0.5)
        local start = wallGap()
        settler:sendEvent('RoundActorDrive', { forward = 1, seconds = 2.5 })
        waitGame(1.2)
        local midway = wallGap()
        waitGame(1.5)
        local stopped = wallGap()
        settler:sendEvent('RoundActorAI', { on = true })
        r.wait(0.5)
        r.ai(false)
        r.log(string.format('W wall at %.0f units; settler centre %.1f from the wall when put (at 100), %.1f after 1.2 s of walking into it, %.1f after 2.7 s',
            best.d, start, midway, stopped))
        h.checkRange('w-put-100-from-the-wall', start, 90, 110)
        h.checkAtLeast('w-he-walked-to-the-wall', start - stopped, 40)
        h.checkRange('w-capsule-stops-at-its-radius', stopped, RADIUS - 4, RADIUS + 12)
        h.checkAtLeast('w-stays-out-of-the-wall', math.min(midway, stopped), RADIUS - 4)
    end

    ---------------------------------------------------------------------------------------------------------------- F: fall
    phase('F fall')
    r.teleport{ pos = ARENA, worldspace = mojave, yaw = 0, onGround = true, label = 'arena' }
    r.wait(3)
    r.settle(3, 30)
    local fx, fy = ARENA[1], ARENA[2] + 400
    local ground = groundBelow(fx, fy, ARENA[3], 'fall ground') or ARENA[3]
    h.command('ResetHealth', SETTLER)
    r.wait(0.5)
    local before = h.command('GetActorValue', SETTLER, HEALTH)
    put(util.vector3(fx, fy, ground + DROP), 0, false)
    local start = core.getSimulationTime()
    r.wait(0.2)
    local highest = settler.position.z
    local landed = r.waitFor(function() return settler.position.z < ground + 5 end, 15, 'settler lands')
    local fallTime = core.getSimulationTime() - start
    r.wait(2)
    local after = h.command('GetActorValue', SETTLER, HEALTH)
    r.log(string.format('F ground %.1f, let go at %.1f (seen %.1f after 0.2 s), landed=%s after %.2f s, z now %.1f, health %.1f -> %.1f',
        ground, ground + DROP, highest, tostring(landed), fallTime, settler.position.z, before, after))
    h.check('f-he-falls-and-lands', landed, true)
    h.checkRange('f-fall-time-by-gravity', fallTime, math.sqrt(2 * DROP / GRAVITY) - 0.25, math.sqrt(2 * DROP / GRAVITY) + 0.35)
    h.checkRange('f-fall-damage-by-rule-F2', before - after, FALL_DAMAGE - 1.5, FALL_DAMAGE + 1.5)
    h.command('ResetHealth', SETTLER)

    ---------------------------------------------------------------------------------------------------------------- S: speed
    phase('S speed')
    r.ai(true)
    put(util.vector3(ARENA[1], ARENA[2] - 450, ARENA[3]), 0, true)
    r.wait(1.5)
    settler:startFollowing(player, 64)
    r.wait(1.5)
    local p0, t0 = settler.position, core.getSimulationTime()
    r.wait(2)
    local p1, t1 = settler.position, core.getSimulationTime()
    local speed = (flat(p1) - flat(p0)):length() / math.max(t1 - t0, 0.001)
    local want = WALK * speedMult / 100
    r.log(string.format('S walked %.1f units in %.2f s: %.1f u/s (rule %.1f)', (flat(p1) - flat(p0)):length(), t1 - t0, speed, want))
    h.checkRange('s-walk-speed-by-rule-M12', speed, want * 0.95, want * 1.05)
    settler:stopFollowing()

    ---------------------------------------------------------------------------------------------------------------- T: stairs
    phase('T stairs')
    -- the player at the top of the steps (east), the settler at their foot (west); the player stands at the foot first, which
    -- gives its height
    r.teleport{ pos = { STAIRS.x - 200, STAIRS.y, STAIRS.z }, worldspace = mojave, yaw = math.pi / 2, onGround = true,
        label = 'stairs foot' }
    r.wait(8)
    r.settle(3, 30)
    r.closeDialogue('stairs')
    local foot = player.position.z
    r.teleport{ pos = { STAIRS.x + 250, STAIRS.y, foot + 200 }, worldspace = mojave, yaw = math.pi * 1.5, onGround = true,
        label = 'stairs top' }
    r.wait(2)
    r.ai(false)
    put(util.vector3(STAIRS.x - 200, STAIRS.y, foot), math.pi / 2, true)
    r.wait(2)
    local z0 = settler.position.z
    r.ai(true)
    settler:startFollowing(player, 64)
    r.track({ { label = 'settler stairs', actor = settler } }, 0.5)
    r.wait(10)
    r.untrack()
    settler:stopFollowing()
    local pos = settler.position
    local under = groundBelow(pos.x, pos.y, pos.z + 50, 'under the settler')
    local gap = (flat(pos) - flat(player.position)):length()
    r.log(string.format('T foot z %.1f, settler z %.1f -> %.1f, ground under him %s, %.0f units from the player (z %.1f)', foot, z0,
        pos.z, under and string.format('%.1f', under) or 'none', gap, player.position.z))
    h.checkRange('t-he-reached-the-top', gap, 0, 200)
    h.checkAtLeast('t-he-climbed', pos.z - z0, 20)
    h.checkRange('t-he-stands-on-the-tread', pos.z - (under or -1e6), -3, 12)
    r.god(true)
    r.log('npc physics round done')
end }
