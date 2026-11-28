---
-- Provides full access to the object the script is attached to.
-- All fields and function of `GameObject` are also available for `openfallout.self`.
-- @context local
-- @module Self
-- @extends openfallout.core#GameObject
-- @usage local self = require('openfallout.self')
-- local types = require('openfallout.types')
-- if self.type == types.Player then  -- All fields and functions of `GameObject` are available.
--     self:sendEvent("something", self.position)
-- end



---
-- Returns true if the script isActive (the object it is attached to is in an active cell).
-- If it is not active, then `openfallout.nearby` can not be used.
-- @function [parent=#Self] isActive
-- @param Self
-- @return #boolean

---
-- The object the script is attached to (readonly)
-- @field [parent=#Self] openfallout.core#GameObject object


---
-- Movement controls (only for actors). They move the player always, and a character or a creature only while its AI is off
-- (@{#Self.enableAI}): then `movement`, `sideMovement`, `run` and `yawChange` turn it and move it on its character body at the
-- speed it walks or runs at; `pitchChange`, `sneak`, `jump` and `use` do nothing for it yet.
-- @field [parent=#Self] #ActorControls controls

---
-- @type ATTACK_TYPE
-- @field #number NoAttack
-- @field #number Any
-- @field #number Chop
-- @field #number Slash
-- @field #number Thrust

---
-- Allows to view and/or modify controls of an actor. All fields are mutable.
-- @type ActorControls
-- @field [parent=#ActorControls] #number movement +1 - move forward, -1 - move backward
-- @field [parent=#ActorControls] #number sideMovement +1 - move right, -1 - move left
-- @field [parent=#ActorControls] #number yawChange Turn right (radians); if negative - turn left
-- @field [parent=#ActorControls] #number pitchChange Look down (radians); if negative - look up
-- @field [parent=#ActorControls] #boolean run true - run, false - walk
-- @field [parent=#ActorControls] #boolean sneak If true - sneak
-- @field [parent=#ActorControls] #boolean jump If true - initiate a jump
-- @field [parent=#ActorControls] #ATTACK_TYPE use Activates the readied weapon/spell according to a provided value. For weapons, keeping this value modified will charge the attack until set to @{#ATTACK_TYPE.NoAttack}. If an @{#ATTACK_TYPE} not appropriate for a currently equipped weapon provided - an appropriate @{#ATTACK_TYPE} will be used instead.

---
-- Enables or disables standard AI (enabled by default). A character or a creature whose AI is off follows no package, fights
-- no one and turns to no speaker: its @{#Self.controls} move it.
-- @function [parent=#Self] enableAI
-- @param Self
-- @param #boolean v

return nil
