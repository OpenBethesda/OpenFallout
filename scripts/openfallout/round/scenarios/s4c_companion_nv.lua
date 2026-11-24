-- Step 4c, the companion part of the Hardcore round (New Vegas): the player recruits Cass through the game's own dialogue, the
-- companion is killed, and the outcome is checked. With Hardcore OFF an essential teammate is put down and gets up again after
-- fEssentialDeathTime (10 s); with Hardcore ON the same blow kills her for good and OnDeath runs (the rules: spec G, companions.md,
-- FalloutNV.exe Actor::IsEssential 0x87F3D0). One engine start, three stages:
--   P  prepare   start with Hardcore off, put in the three stand-ins that a fresh character needs, go to Cass, read her baseline
--   R  recruit   talk with her and pick the ten topics of the recruit path (spec G, section 1b), no SetPlayerTeammate shortcut
--   K  casual    kill her with Hardcore off (KillActor, then blows of a pistol): she must be put down, not dead, and get up
--   H  hardcore  Hardcore on (the Gameplay switch's own call), kill her: she must die, OnDeath must have run, she must stay dead
-- Each check logs `ROUND check <label> ok` or `ROUND error check <label> FAIL actual= want=`; a failed check does not stop the
-- scenario, so one run shows every rule that master does not follow yet. The helpers are in scripts/roundhelpers.lua.
--
-- The stand-ins (a fresh character does not have them; Alice McLafferty's job in play):
--   VMS05 "You Can Depend on Me" at stage 30: the first topic (Topic003) needs objective 30 of VMS05 displayed, and stage 30's own
--       script shows it. The stages before it are the courier's errand; their result scripts add nothing the recruit path reads.
--   750 caps (800 given): Topic009 ("It's just a name, this is money") needs 750 caps; Topic013 then takes them.
--   Barter 50 (60 forced): Topic009 has two responses, the success one needs Barter >= 50; below it the pick falls to the speech
--       branches that lead to "help Jackson first", not to the recruit.
local NV = 'FalloutNV.esm'
local CASS = { NV, 0x135F19 } -- ACHR RoseofSharonCassidyREF
local CASS_BASE = 0x133FDD -- NPC_ RoseofSharonCassidy
local CAPS = { NV, 0xF } -- Caps001
local BARTER = 32 -- actor value 0x20
local BARRACKS = 0x141AA5 -- cell MojaveOutpostBarracks01 (the name is "Mojave Outpost Barracks", but the world does not find it by that name)
local SHOTGUN, KNIFE = { NV, 0x176E55 }, { NV, 0x176E56 } -- what Cass carries (spec G, section 3)
local PISTOL = { NV, 0x434F } -- a pistol for the blows of the damage path (as m16_damage.lua)
-- The ten topics of the recruit path, in order (DIAL ids; spec G, section 1b). The last one's follow-up (INFO 0x15D054) is the recruit.
local PICKS = { 0x15C754, 0x15C753, 0x15C750, 0x15C74F, 0x15C740, 0x15C74E, 0x15C74A, 0x15C736, 0x15D047, 0x15D046 }
-- fEssentialDeathTime of the exe (no plugin sets it): how long a put-down teammate lies
local DOWN_SECONDS = 10

return { limit = 1400, delay = 4, run = function(r)
    local h = require('scripts.roundhelpers')(r)
    local core = r.core
    local function phase(name) r.log('PHASE ' .. name) end
    local function distance(a, b) return (a.position - b.position):length() end

    -------------------------------------------------------------------------------------------------------------- P: prepare
    phase('P start')
    r.god(true)
    r.eye('start')
    -- Hardcore off for the first stages (a save of the intro has it off; the call says so in the log)
    h.setHardcore(false)

    phase('P2 stand-ins')
    h.setQuestStage('VMS05', 30)
    h.addItem(CAPS, 800)
    h.command('ForceActorValue', h.player(), BARTER, 60)
    r.wait(1)
    h.check('p-vms05-stage-30', h.questStage('VMS05'), 30)
    h.checkAtLeast('p-caps-at-least-750', h.itemCount(CAPS), 750)
    h.checkAtLeast('p-barter-at-least-50', h.command('GetActorValue', h.player(), BARTER), 50)

    phase('P3 go to Cass')
    r.teleport{ cellId = core.getFormId(NV, BARRACKS), pos = { 2103, 1861, 9328 }, yaw = 0, onGround = true, label = 'cass' }
    r.wait(3)
    r.settle(3, 30)
    r.eye('barracks')
    local cass = h.object(CASS, 'cass')
    r.actors('near cass', 800, 10)
    local startDistance = distance(cass, r.player())
    r.log(string.format('cass %s distance %.0f', r.describe(cass), startDistance))
    h.check('p-cass-in-the-same-cell', cass.cell ~= nil and cass.cell.id == r.player().cell.id, true)
    h.checkRange('p-cass-within-300-units', startDistance, 0, 300)

    phase('P4 baseline')
    local base = h.state(CASS, 'baseline')
    h.check('p-baseline-teammate-0', base.teammate, false)
    h.check('p-baseline-essential-0', base.essential, false)
    h.check('p-baseline-dead-0', base.dead, false)
    h.check('p-baseline-knockedout-0', base.knockedOut, false)
    local stageBefore = h.questStage('VCassCompanion')
    r.inventory('cass before', cass, SHOTGUN)

    -------------------------------------------------------------------------------------------------------------- R: recruit
    phase('R1 talk')
    local talk = h.talk(CASS)
    h.check('r-speaker-is-cass', h.formNumber(talk.speakerId) == h.formNumber(h.form(CASS)), true)

    phase('R2 the ten picks')
    -- (finish = true: the response is over at once, the time it takes to read is not what is checked; its end script runs as it
    -- does when the player has read it)
    for _, topic in ipairs(PICKS) do h.pick{ topic = topic, finish = true } end
    -- the follow-up (INFO 0x15D054) is the recruit and ends the conversation by its goodbye flag
    h.waitConversationEnd(60)
    r.settle(3, 20)

    phase('R3 check the hire')
    local hired = h.state(CASS, 'after recruit')
    h.check('r-teammate-1', hired.teammate, true)
    h.checkAtLeast('r-teammate-count-at-least-1', h.teammateCount(), 1)
    -- (Topic013 takes the 750 caps of the 800)
    h.check('r-caps-50-after-the-purchase', h.itemCount(CAPS), 50)
    h.check('r-essential-1-without-hardcore', hired.essential, true)
    h.check('r-vms05-objective-35-displayed', h.objectiveDisplayed('VMS05', 35), true)
    h.check('r-not-dead', hired.dead, false)
    r.inventory('cass hired', cass, SHOTGUN)

    phase('R4 follows')
    -- The player goes to the far end of the room (700 units and more from Cass); a teammate with a follow package closes the gap
    r.teleport{ cellId = core.getFormId(NV, BARRACKS), pos = { 2230, 1250, 9330 }, yaw = 0, onGround = true, label = 'away from cass' }
    r.wait(1)
    local gapAfterWalk = distance(cass, r.player())
    r.actors('away from cass', 2000, 3)
    -- (her place and what she plays every three seconds, to see whether she moves at all)
    r.track({ { label = 'cass follow', actor = cass } }, 3)
    local packageNow = pcall(h.command, 'GetCurrentPackage', CASS)
    h.waitSeconds(15, 'cass follows')
    r.untrack()
    local gapAfterFollow = distance(cass, r.player())
    r.log(string.format('follow: gap %.0f at the start, %.0f fifteen seconds later', gapAfterWalk, gapAfterFollow))
    -- She follows when the player is more than 600 units away at the start and fifteen seconds later she is within 350 and nearer.
    -- A start with no gap proves nothing and is a failure of this check, not a pass.
    h.checkAtLeast('r-start-gap-at-least-600', gapAfterWalk, 600)
    h.checkRange('r-follows-within-350-after-15-s', gapAfterFollow, 0, math.min(350, gapAfterWalk - 1))

    ---------------------------------------------------------------------------------------------------- K: casual, Hardcore off
    phase('K1 kill with Hardcore off (KillActor)')
    h.check('k-hardcore-is-off', r.hardcore(nil, 'before kill').on, false)
    local killTime = core.getSimulationTime()
    h.kill(CASS)
    r.wait(1)
    local down = h.state(CASS, 'casual +1 s')
    h.check('k-casual-not-dead', down.dead, false)
    h.check('k-casual-knocked-out', down.knockedOut, true)
    h.check('k-casual-still-essential', down.essential, true)
    h.check('k-casual-still-teammate', down.teammate, true)
    -- OnDeath did not run: its first act is to put VCassCompanion at stage 110 (the failed one)
    h.check('k-casual-quest-stage-unchanged', h.questStage('VCassCompanion'), stageBefore)
    -- she gets up after about fEssentialDeathTime (10 s of the game's time, counted from the blow)
    local woke = r.waitFor(function() return not h.isKnockedOut(CASS) end, 40, 'cass gets up')
    local downSeconds = core.getSimulationTime() - killTime
    r.log(string.format('K1 she lay %.1f s of the game (fEssentialDeathTime %d), woke=%s', downSeconds, DOWN_SECONDS, tostring(woke)))
    h.check('k-casual-gets-up', woke, true)
    h.checkRange('k-casual-lay-7-to-14-s', downSeconds, DOWN_SECONDS - 3, DOWN_SECONDS + 4)
    r.wait(1)
    local up = h.state(CASS, 'casual woke')
    h.check('k-casual-woke-not-dead', up.dead, false)
    h.check('k-casual-woke-not-knocked-out', up.knockedOut, false)
    h.check('k-casual-woke-still-teammate', up.teammate, true)
    -- (what health she has when she gets up is not settled by the exe reading, spec G UNCLEAR 1: only that she has some)
    h.checkAtLeast('k-casual-woke-health-above-0', up.hp or -1, 0.01)
    r.log(string.format('K1 health when she got up: %s (%s of her health)', tostring(up.hp), tostring(up.hpPercent)))
    h.check('k-casual-quest-stage-still-unchanged', h.questStage('VCassCompanion'), stageBefore)

    phase('K2 knock-out by blows (Hardcore off)')
    local blowTime = core.getSimulationTime()
    local row, blows = h.strike(CASS, PISTOL, { soft = true, limit = 60 })
    r.wait(1)
    local hit = h.state(CASS, 'casual blows +1 s')
    h.check('k2-casual-blows-did-not-kill', hit.dead, false)
    h.check('k2-casual-blows-knocked-her-out', hit.knockedOut, true)
    local woke2 = r.waitFor(function() return not h.isKnockedOut(CASS) end, 40, 'cass gets up again')
    r.log(string.format('K2 after %s blows she lay %.1f s of the game, woke=%s', tostring(blows), core.getSimulationTime() - blowTime, tostring(woke2)))
    h.check('k2-casual-gets-up-again', woke2, true)
    r.wait(1)
    local up2 = h.state(CASS, 'casual blows woke')
    h.check('k2-casual-woke-not-dead', up2.dead, false)
    h.check('k2-casual-quest-stage-still-unchanged', h.questStage('VCassCompanion'), stageBefore)

    ---------------------------------------------------------------------------------------------------- H: Hardcore on
    phase('H1 Hardcore on')
    -- the call of the Gameplay screen's Hardcore Mode switch (and of the box at the end of Doc Mitchell's talk): SetHardcore 1
    h.setHardcore(true)
    h.check('h-hardcore-is-on', r.hardcore(nil, 'mode').on, true)
    local mortal = h.state(CASS, 'hardcore before the blow')
    h.check('h-teammate-no-longer-essential', mortal.essential, false)
    h.check('h-still-teammate', mortal.teammate, true)
    h.check('h-alive-before-the-blow', mortal.dead, false)

    phase('H2 kill with Hardcore on (KillActor)')
    h.kill(CASS)
    r.wait(1)
    local dead = h.state(CASS, 'hardcore +1 s')
    h.check('h-dead', dead.dead, true)
    h.check('h-not-knocked-out', dead.knockedOut, false)
    h.check('h-no-longer-essential', dead.essential, false)
    -- OnDeath of her script ran: VCassCompanion at stage 110 (the failed stage)
    local stageAfter, quest = h.questStage('VCassCompanion')
    h.check('h-quest-stage-110-after-OnDeath', stageAfter, 110)
    h.check('h-quest-failed', quest.failed, true)
    -- her body keeps what she carried
    local shotgun = r.inventory('corpse shotgun', cass, SHOTGUN)
    local knife = r.inventory('corpse knife', cass, KNIFE)
    h.checkAtLeast('h-corpse-has-the-caravan-shotgun', shotgun and shotgun.carried or -1, 1)
    h.checkAtLeast('h-corpse-has-the-combat-knife', knife and knife.carried or -1, 1)

    phase('H3 she stays dead')
    h.waitSeconds(20, 'stays dead')
    local stays = h.state(CASS, 'hardcore +21 s')
    h.check('h-stays-dead', stays.dead, true)
    h.check('h-stays-not-knocked-out', stays.knockedOut, false)

    phase('H4 the same by blows')
    -- A second death with Hardcore still on, by the damage path (the blows of a pistol), after the game's own Resurrect
    h.command('ResurrectActor', CASS, 1)
    r.wait(2)
    local back = h.state(CASS, 'resurrected')
    h.check('h4-resurrected-alive', back.dead, false)
    h.check('h4-resurrected-not-essential', back.essential, false)
    h.strike(CASS, PISTOL, { soft = true, limit = 80 })
    r.wait(1)
    local again = h.state(CASS, 'hardcore blows +1 s')
    h.check('h4-blows-killed-her', again.dead, true)
    h.check('h4-blows-did-not-knock-out', again.knockedOut, false)
    r.actors('end', 800, 10)
    r.log('companion round done; Cass base record ' .. string.format('0x%x', CASS_BASE))
end }
