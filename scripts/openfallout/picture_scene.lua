-- The scenario of one scene of the picture comparison (docs/openfallout/picture-comparison.md). picturecompare.py writes it into
-- the round of the scene with the scene's table in front of it (`local SCENE = { ... }`). It holds the scene still and takes one
-- picture, the same way on every build, so that two pictures differ by what the builds draw and not by when they were taken:
--
-- 1. the characters' AI goes off at once (they stop where they stand), the engine's head-up display and crosshair are hidden
--    (the Fallout display of health, compass and action points is a script of its own and stays), the game's
--    clock stops at the scene's hour and the weather is forced and held;
-- 2. the place loads (`settle` real seconds) and the camera is put in its static mode at the scene's view (a scene without a
--    yaw looks the way the start put the player);
-- 3. the world is paused when the simulation time reaches `pauseAt` (water, clouds, particles and animations follow the simulation
--    time, so two runs paused at the same simulation time show them at the same phase, within one frame); a scene that is not ready
--    by then is an error, and the round fails;
-- 4. after `afterPause` real seconds (the drawing of the paused world settles) the picture is taken.
return { limit = SCENE.limit, delay = 0, run = function(r)
    r.ai(false)
    r.hud(false)
    r.gameTimeScale(0)
    if SCENE.hour then r.hour(SCENE.hour) end
    if SCENE.place then
        r.teleport{ pos = SCENE.place.pos, onGround = SCENE.place.onGround ~= false, label = SCENE.name }
    end
    if SCENE.weather then r.weather(r.core.getFormId(SCENE.weather.master, tonumber(SCENE.weather.form))) end
    r.wait(SCENE.settle)
    r.view{ pos = SCENE.camera.pos, yaw = SCENE.camera.yaw and math.rad(SCENE.camera.yaw), pitch = math.rad(SCENE.camera.pitch or 0),
            forward = SCENE.forward, label = SCENE.name }
    r.wait(1)
    r.eye(SCENE.name)
    local now = r.simulationTime()
    if now >= SCENE.pauseAt then
        r.error('late', string.format('ready at simulation time %.1f, after the time the scene is paused at (%.1f)', now,
            SCENE.pauseAt))
    else
        r.waitFor(function() return r.simulationTime() >= SCENE.pauseAt end, SCENE.pauseAt - now + 30, 'pause time')
    end
    r.pause(true)
    r.log('still', string.format('simulation=%.3f', r.simulationTime()))
    r.wait(SCENE.afterPause)
    r.shot(SCENE.name)
end }
