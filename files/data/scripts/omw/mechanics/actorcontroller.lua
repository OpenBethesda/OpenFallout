local self = require('openfallout.self')
local core = require('openfallout.core')
local anim = require('openfallout.animation')

return {
    eventHandlers = {
        AddVfx = function(data)
            anim.addVfx(self, data.model, data.options)
        end,
        PlaySound3d = function(data)
            if data.sound then
                core.sound.playSound3d(data.sound, self, data.options)
            else
                core.sound.playSoundFile3d(data.file, self, data.options)
            end
        end,
    },
}
