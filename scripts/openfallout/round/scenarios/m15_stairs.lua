-- M15 round 15G: footsteps on stairs (New Vegas). Nobody can hear a hidden round: what is checked is what the engine logs
-- ("Footstep of ... -> FSTxxxStairsL/R"), with the master volume at 0. The player walks over stair objects of the world
-- (concrete step-down stairs, retaining-wall stairs) in four directions, one of which climbs them; the engine's rule for stairs (a flat
-- tread with ground a step higher or lower a stride ahead or behind) should name the Stairs slots there.
local NV = 'FalloutNV.esm'
local SITES = {
    { name = 'concrete step-down stairs', x = 22861, y = -16120, z = 10027 },
    { name = 'retaining wall stairs', x = -8192, y = -31696, z = 6771 },
}
local MOJAVE_CELLS = { 0x0daeb9, 0x0daeb8, 0x0daebb }

return { limit = 300, delay = 4, run = function(r)
    local function phase(name) r.log('PHASE ' .. name) end
    local mojave
    for _, id in ipairs(MOJAVE_CELLS) do
        local ok, cell = pcall(function() return r.world.getCellById(r.core.getFormId(NV, id)) end)
        if ok and cell then mojave = cell break end
    end
    if mojave == nil then error('no cell of the Mojave was found') end
    r.god(true)
    -- The characters stand still: since step 4a the characters of the loaded cells run their packages, and a feral ghoul at
    -- the retaining wall came for the player and stood in the way of the walks. This round is about the footsteps on stairs.
    r.ai(false)
    for _, site in ipairs(SITES) do
        phase(site.name)
        r.teleport{ pos = { site.x, site.y, site.z }, worldspace = mojave, yaw = 0, onGround = true, label = site.name }
        r.wait(10)
        r.settle(3, 30)
        -- (a character of the loaded cells may come and talk to the player: its package runs since step 4a; the talk is closed)
        r.closeDialogue(site.name)
        r.eye(site.name)
        r.camera('ThirdPerson')
        r.wait(2)
        r.mode('third')
        for _, yaw in ipairs({ 0, math.pi / 2, math.pi, 3 * math.pi / 2 }) do
            r.teleport{ pos = { site.x, site.y, site.z }, worldspace = mojave, yaw = yaw, onGround = true, label = string.format('%s yaw %.2f', site.name, yaw) }
            r.wait(3)
            -- (the message boxes of the DLCs pause the world: a walk starts when none has been open for 3 s)
            r.settle(3, 30)
            r.closeDialogue(string.format('%s yaw %.2f', site.name, yaw))
            r.move{ forward = 1, seconds = 3 }
            r.wait(4)
            r.eye(string.format('after the walk, yaw %.2f', yaw))
        end
    end
    r.eye('end')
end }
