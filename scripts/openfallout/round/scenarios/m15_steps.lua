-- M15 round 15A: footsteps and the sounds that animations name (New Vegas). Nobody can hear a hidden round: what is checked is what
-- the engine logs ("Footstep of ...", "Sound records played: ..."), with the master volume at 0 so that nothing is heard either.
-- The player walks, runs and sneaks on open ground east of the doctor's house of Goodsprings, in the third person (the body is
-- drawn) and in the first person (the body is not drawn but goes on moving), and goes through the doctor's house (wood floors).
local NV = 'FalloutNV.esm'
local ARENA = { x = -69660, y = 1900, z = 8352 }  -- open flat ground, 8340 to 8370 high
-- the door of the doctor's house seen from outside (0x103e61 is the one inside, the way out) and the place in front of it
local HOUSE_DOOR = { NV, 0x103e69 }
local HOUSE_FRONT = { x = -73163, y = 1312, z = 8690 }
-- cells of WastelandNV (the save can be anywhere: a cell that lives in the Mojave names its worldspace)
local MOJAVE_CELLS = { 0x0daeb9, 0x0daeb8, 0x0daebb }

return { limit = 300, delay = 4, run = function(r)
    local player = r.player()
    local function phase(name) r.log('PHASE ' .. name) end
    local function gaits(label)
        r.log('GAITS ' .. label)
        r.move{ forward = 1, seconds = 4 }
        r.wait(5)
        r.move{ forward = 1, run = true, seconds = 4 }
        r.wait(5)
        r.move{ forward = 1, sneak = true, seconds = 4 }
        r.wait(5)
        r.move{ forward = -1, seconds = 3 }
        r.wait(4)
    end
    local mojave
    for _, id in ipairs(MOJAVE_CELLS) do
        local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(NV, id)) end)
        r.log('anchor', string.format('cell 0x%x', id), ok and (cell and 'found' or 'nil') or ('error ' .. tostring(cell)))
        if ok and cell then mojave = cell break end
    end
    if mojave == nil then error('no cell of the Mojave was found') end
    r.god(true)
    phase('S setup')
    r.teleport{ pos = { ARENA.x, ARENA.y, ARENA.z }, worldspace = mojave, yaw = 0, onGround = true, label = 'arena' }
    r.wait(10)
    r.settle(3, 30)
    r.eye('arena')
    phase('A third person, open ground')
    r.camera('ThirdPerson')
    r.wait(2)
    r.mode('third')
    gaits('third person')
    r.teleport{ pos = { ARENA.x, ARENA.y, ARENA.z }, worldspace = mojave, yaw = 0, onGround = true, label = 'arena again' }
    r.wait(4)
    phase('B first person, open ground')
    r.camera('FirstPerson')
    r.wait(2)
    r.mode('first')
    gaits('first person')
    -- The eye of the first person while the player sneaks: the games put the camera on the Camera1st node of the skeleton of
    -- the first person, which its sneak animations lower by 40 units (Bip01 NonAccum at -40 in _1stperson/sneakmtidle.kf)
    local standing = r.eye('first person, standing')
    r.crouch(true)
    r.wait(3)
    local low = r.eye('first person, sneaking')
    r.crouch(false)
    r.wait(3)
    local again = r.eye('first person, standing again')
    if standing and low and again and standing.eyeZ and low.eyeZ and again.eyeZ then
        local drop = (standing.eyeZ - standing.z) - (low.eyeZ - low.z)
        local back = (again.eyeZ - again.z) - (standing.eyeZ - standing.z)
        if drop >= 30 and drop <= 50 and math.abs(back) <= 2 then
            r.log('sneak eye', string.format('drop=%.1f back=%.1f', drop, back), 'ok')
        else
            r.error('sneak eye', string.format('drop=%.1f back=%.1f', drop, back), 'the eye does not go down about 40 units and back')
        end
    else
        r.error('sneak eye', 'no eye position')
    end
    phase('C the doctor\'s house')
    r.teleport{ pos = { HOUSE_FRONT.x, HOUSE_FRONT.y, HOUSE_FRONT.z }, worldspace = mojave, yaw = 0, onGround = true, label = 'house front' }
    r.wait(4)
    r.activate(HOUSE_DOOR, 'door in')
    -- A door that did not take the player in leaves the house phase outdoors: the round fails rather than pass without
    -- having looked at the floors of a house
    if not r.waitFor(function() return player.cell ~= nil and not player.cell.isExterior end, 40, 'interior') then
        error('C the door of the doctor\'s house did not take the player indoors')
    end
    r.wait(10)
    r.settle(3, 30)
    r.camera('ThirdPerson')
    r.wait(2)
    gaits('house third person')
    phase('D done')
    r.eye('end')
end }
