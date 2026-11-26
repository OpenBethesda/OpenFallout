-- Step 4b, a follower (New Vegas): Cass, recruited through the game's own dialogue, gets up from her stool and follows the player,
-- across the room and out through the barracks door into the Mojave. The stand-ins and the ten picks are those of the companion round
-- of step 4c (s4/companion-round 665522486d, phases P and R; spec G section 1b): VMS05 at stage 30, 800 caps, Barter 60.
--   P  prepare   the stand-ins, go to Cass in the Mojave Outpost Barracks (cell 0x141AA5), she sits (GetSitting)
--   R  recruit   talk with her, the ten picks; she is a teammate
--   U  get up    her follow package has no furniture: she leaves the stool (the exe: a package without furniture takes over)
--   F  follow    the player goes 600+ units away in the room; she closes the gap
--   D  door      the player goes out through the barracks door (REFR 0x1438B2); she comes out after him
local NV = 'FalloutNV.esm'
local CASS = { NV, 0x135F19 } -- ACHR RoseofSharonCassidyREF
local CAPS = { NV, 0xF }
local BARTER = 32
local BARRACKS = 0x141AA5
local DOOR = { NV, 0x1438B2 } -- the barracks door, to the Mojave Outpost (door 0x143859)
local PICKS = { 0x15C754, 0x15C753, 0x15C750, 0x15C74F, 0x15C740, 0x15C74E, 0x15C74A, 0x15C736, 0x15D047, 0x15D046 }

return { limit = 900, delay = 4, run = function(r)
    local h = require('scripts.roundhelpers')(r)
    local core = r.core
    local function phase(name) r.log('PHASE ' .. name) end
    local function distance(a, b) return (a.position - b.position):length() end
    local function sitting() return h.command('GetSitting', CASS) end

    -------------------------------------------------------------------------------------------------------------- P: prepare
    phase('P start')
    r.god(true)
    h.setHardcore(false)
    h.setQuestStage('VMS05', 30)
    h.addItem(CAPS, 800)
    h.command('ForceActorValue', h.player(), BARTER, 60)
    r.wait(1)
    r.teleport{ cellId = core.getFormId(NV, BARRACKS), pos = { 2103, 1861, 9328 }, yaw = 0, onGround = true, label = 'cass' }
    r.wait(3)
    r.settle(3, 30)
    local cass = h.object(CASS, 'cass')
    r.log(string.format('cass %s distance %.0f sitting %s package %s', r.describe(cass), distance(cass, r.player()),
        tostring(sitting()), tostring(select(2, pcall(h.command, 'GetCurrentPackage', CASS)))))
    h.check('p-cass-in-the-same-cell', cass.cell ~= nil and cass.cell.id == r.player().cell.id, true)
    h.check('p-baseline-teammate-0', h.isTeammate(CASS), false)
    -- (GetSitting 3: sitting; the stool of her sandbox package)
    local satBefore = sitting()

    -------------------------------------------------------------------------------------------------------------- R: recruit
    phase('R recruit')
    h.talk(CASS)
    for _, topic in ipairs(PICKS) do h.pick{ topic = topic, finish = true } end
    h.waitConversationEnd(60)
    r.settle(3, 20)
    h.check('r-teammate-1', h.isTeammate(CASS), true)

    -------------------------------------------------------------------------------------------------------------- U: get up
    phase('U get up')
    r.track({ { label = 'cass', actor = cass } }, 1)
    local up = r.waitFor(function() return sitting() == 0 end, 20, 'cass gets up')
    r.log(string.format('U sat %s before the recruit, %s now, package %s', tostring(satBefore), tostring(sitting()),
        tostring(select(2, pcall(h.command, 'GetCurrentPackage', CASS)))))
    h.check('u-cass-gets-up', up, true)

    -------------------------------------------------------------------------------------------------------------- F: follow
    phase('F follow')
    r.teleport{ cellId = core.getFormId(NV, BARRACKS), pos = { 2230, 1250, 9330 }, yaw = 0, onGround = true, label = 'away from cass' }
    r.wait(1)
    local gapStart = distance(cass, r.player())
    h.waitSeconds(15, 'cass follows')
    local gapEnd = distance(cass, r.player())
    r.log(string.format('F gap %.0f at the start, %.0f fifteen seconds later', gapStart, gapEnd))
    h.checkAtLeast('f-start-gap-at-least-600', gapStart, 600)
    h.checkRange('f-follows-within-350-after-15-s', gapEnd, 0, math.min(350, gapStart - 1))

    -------------------------------------------------------------------------------------------------------------- D: door
    phase('D door')
    local inside = r.player().cell.id
    r.activate(DOOR, 'barracks door')
    r.waitFor(function() return r.player().cell ~= nil and r.player().cell.isExterior end, 40, 'outside')
    r.wait(2)
    r.settle(3, 30)
    local out = r.waitFor(function() return cass.cell ~= nil and cass.cell.isExterior end, 30, 'cass comes out')
    r.wait(3)
    local gapOut = distance(cass, r.player())
    r.log(string.format('D cass %s, %.0f from the player (outside=%s, left %s)', r.describe(cass), gapOut, tostring(out),
        tostring(inside)))
    h.check('d-cass-comes-out', out, true)
    h.checkRange('d-cass-near-the-player-outside', gapOut, 0, 500)
    r.untrack()
    r.log('follower round done')
end }
