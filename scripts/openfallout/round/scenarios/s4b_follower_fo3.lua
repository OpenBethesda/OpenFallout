-- Step 4b, a second follower (Fallout 3): Dogmeat, a creature, is recruited through his own dialogue at the scrapyard and follows the
-- player on his character controller, across the room of the yard and over the border of an exterior cell.
--   R  recruit   GREETING (Dogmeat mourns his master: FFEU255DogmeatMourn), then TakeDogmeat1A, TakeDogmeat2A, TakeDogmeat3A; the
--                last one's result sets Followers.DogmeatHired and DogmeatREF.IsFollowingDefault (FollowersDogmeatFollowPlayerDEFAULT)
--   F  follow    the player goes 700 units away; he closes the gap
--   C  cell      the player goes 1,500 units on, over the border of the exterior cell; he follows into the other cell
local FO3 = 'Fallout3.esm'
local DOGMEAT = { FO3, 0x6A775 } -- ACRE DogmeatREF, base CREA 0x6A772 FFEU255Dogmeat
local HOME = { x = 26222.8, y = 40066.8, z = 14239.8 } -- where he waits, in the scrapyard
local PICKS = { 0x9D24E, 0x9D251, 0x96E4D } -- TakeDogmeat1A, TakeDogmeat2A, TakeDogmeat3A

return { limit = 700, delay = 4, run = function(r)
    local h = require('scripts.roundhelpers')(r)
    h.file = FO3
    local function phase(name) r.log('PHASE ' .. name) end
    local function distance(a, b) return (a.position - b.position):length() end
    local function gridOf(object)
        local cell = object.cell
        return cell and cell.isExterior and string.format('%d,%d', cell.gridX or 0, cell.gridY or 0) or tostring(cell and cell.id)
    end

    phase('P start')
    r.god(true)
    r.teleport{ pos = { HOME.x, HOME.y - 160, HOME.z + 100 }, yaw = 0, onGround = true, label = 'dogmeat' }
    r.wait(12)
    r.settle(3, 30)
    r.closeDialogue('scrapyard')
    -- (a stand-in: DogmeatREF is placed initially disabled, flags 0xC00, and the start of the game that enables him is not
    -- played by a round that starts in the Wasteland)
    h.command('Enable', DOGMEAT)
    r.wait(3)
    local dog = h.object(DOGMEAT, 'dogmeat')
    r.log(string.format('dogmeat %s distance %.0f', r.describe(dog), distance(dog, r.player())))
    h.check('p-baseline-not-teammate', h.isTeammate(DOGMEAT), false)

    phase('R recruit')
    h.talk(DOGMEAT)
    for _, topic in ipairs(PICKS) do h.pick{ topic = topic, finish = true } end
    h.waitConversationEnd(60)
    r.settle(3, 20)
    r.log(string.format('R teammate %s', tostring(h.isTeammate(DOGMEAT))))

    phase('F follow')
    local start = r.player().position
    r.teleport{ pos = { start.x, start.y + 700, start.z + 100 }, yaw = 0, onGround = true, label = 'away from dogmeat' }
    r.wait(1)
    r.track({ { label = 'dogmeat', actor = dog } }, 1)
    local gapStart = distance(dog, r.player())
    h.waitSeconds(15, 'dogmeat follows')
    local gapEnd = distance(dog, r.player())
    r.log(string.format('F gap %.0f at the start, %.0f fifteen seconds later', gapStart, gapEnd))
    h.checkAtLeast('f-start-gap-at-least-600', gapStart, 600)
    h.checkRange('f-follows-within-350-after-15-s', gapEnd, 0, math.min(350, gapStart - 1))

    phase('C cell border')
    local before = gridOf(dog)
    -- on to the next cell to the north: past the next multiple of 4096 by 300 units
    local p = r.player().position
    local border = (math.floor(p.y / 4096) + 1) * 4096
    r.teleport{ pos = { p.x, border + 300, p.z + 100 }, yaw = 0, onGround = true, label = 'over the cell border' }
    r.wait(2)
    local gapC = distance(dog, r.player())
    local crossed = r.waitFor(function() return gridOf(dog) ~= before and distance(dog, r.player()) < 400 end, 40, 'dogmeat crosses')
    r.untrack()
    r.log(string.format('C dogmeat in cell %s (was %s), %.0f from the player (%.0f when the player crossed)', gridOf(dog), before,
        distance(dog, r.player()), gapC))
    h.check('c-dogmeat-follows-into-the-next-cell', crossed, true)
    r.log('fo3 follower round done')
end }
