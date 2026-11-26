---
-- Defines functions that allow control of character animations.
-- The engine animates Fallout characters by the rules of the games; these functions act on top of that.
-- @context local
-- @module animation
-- @usage local anim = require('openfallout.animation')

--- Possible @{#Priority} values
-- @field [parent=#animation] #Priority PRIORITY

--- `animation.PRIORITY`
-- @type Priority
-- @field #number Default "0"
-- @field #number WeaponLowerBody "1"
-- @field #number SneakIdleLowerBody "2"
-- @field #number SwimIdle "3"
-- @field #number Jump "4"
-- @field #number Movement "5"
-- @field #number Hit "6"
-- @field #number Weapon "7"
-- @field #number Block "8"
-- @field #number Knockdown "9"
-- @field #number Torch "10"
-- @field #number Storm "11"
-- @field #number Death "12" 
-- @field #number Scripted "13" Special priority used by scripted animations. When any animation with this priority is present, all animations without this priority are paused.

--- Possible @{#BlendMask} values
-- @field [parent=#animation] #BlendMask BLEND_MASK

--- `animation.BLEND_MASK`
-- @type BlendMask
-- @field #number LowerBody "1" All bones from 'Bip01 pelvis' and below
-- @field #number Torso "2" All bones from 'Bip01 Spine1' and up, excluding arms
-- @field #number LeftArm "4" All bones from 'Bip01 L Clavicle' and out
-- @field #number RightArm "8" All bones from 'Bip01 R Clavicle' and out
-- @field #number UpperBody "14" All bones from 'Bip01 Spine1' and up, including arms
-- @field #number All "15" All bones

--- Possible @{#BoneGroup} values
-- @field [parent=#animation] #BoneGroup BONE_GROUP

--- `animation.BONE_GROUP`
-- @type BoneGroup
-- @field #number LowerBody "1" All bones from 'Bip01 pelvis' and below
-- @field #number Torso "2" All bones from 'Bip01 Spine1' and up, excluding arms
-- @field #number LeftArm "3" All bones from 'Bip01 L Clavicle' and out
-- @field #number RightArm "4" All bones from 'Bip01 R Clavicle' and out

---
-- Check if the object has an animation object or not
-- @function [parent=#animation] hasAnimation
-- @param openfallout.core#GameObject actor
-- @return #boolean

---
-- Get the absolute position within the animation track of the given text key
-- @function [parent=#animation] getTextKeyTime
-- @param openfallout.core#GameObject actor
-- @param #string text key
-- @return #number

---
-- Check if the given animation group is currently playing
-- @function [parent=#animation] isPlaying
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #boolean

---
-- Get the current absolute time of the given animation group if it is playing, or -1 if it is not playing.
-- @function [parent=#animation] getCurrentTime
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #number

---
-- Check whether the animation is a looping animation or not. This is determined by a combination 
-- of groupName, some of which are hardcoded to be looping, and the presence of loop start/stop keys.
-- The groupNames that are hardcoded as looping are the following, as well as per-weapon-type suffixed variants of each.
-- "walkforward", "walkback", "walkleft", "walkright", "swimwalkforward", "swimwalkback", "swimwalkleft", "swimwalkright", 
-- "runforward", "runback", "runleft", "runright", "swimrunforward", "swimrunback", "swimrunleft", "swimrunright", 
-- "sneakforward", "sneakback", "sneakleft", "sneakright", "turnleft", "turnright", "swimturnleft", "swimturnright",
-- "spellturnleft", "spellturnright", "torch", "idle", "idle2", "idle3", "idle4", "idle5", "idle6", "idle7", "idle8", 
-- "idle9", "idlesneak", "idlestorm", "idleswim", "jump", "inventoryhandtohand", "inventoryweapononehand", 
-- "inventoryweapontwohand", "inventoryweapontwowide"
-- @function [parent=#animation] isLoopingAnimation
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #boolean

---
-- Cancels and removes the animation group from the list of active animations.
-- Can only be used on self.
-- @function [parent=#animation] cancel
-- @param openfallout.core#GameObject actor
-- @param #string groupName

---
-- Enables or disables looping for the given animation group. Looping is enabled by default.
-- Can only be used on self.
-- @function [parent=#animation] setLoopingEnabled
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @param #boolean enabled

---
-- Returns the completion of the animation, or nil if the animation group is not active.
-- @function [parent=#animation] getCompletion
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Returns the remaining number of loops, not counting the current loop, or nil if the animation group is not active.
-- @function [parent=#animation] getLoopCount
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Get the current playback speed of an animation group, or nil if the animation group is not active.
-- @function [parent=#animation] getSpeed
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #number, #nil

---
-- Modifies the playback speed of an animation group.
-- Note that this is not sticky and only affects the speed until the currently playing sequence ends.
-- Can only be used on self.
-- @function [parent=#animation] setSpeed
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @param #number speed The new animation speed, where speed=1 is normal speed.

---
-- Play an animation directly. The engine animates Fallout characters by the rules of the games (the Fallout
-- animation of each actor and the PlayGroup commands of the Fallout scripts), and may at any time alter or cancel an
-- animation played this way.
-- Can only be used on self.
-- @function [parent=#animation] playBlended
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @param #table options A table of play options. Can contain:
--
--   * `loops` - a number >= 0, the number of times the animation should loop after the first play (default: 0).
--   * `priority` - Either a single #Priority value that will be assigned to all bone groups. Or a table mapping bone groups to its priority (default: PRIORITY.Default).
--   * `blendMask` - A mask of which bone groups to include in the animation (Default: BLEND_MASK.All).
--   * `autoDisable` - If true, the animation will be immediately  removed upon finishing, which means information will not be possible to query once completed. (Default: true)
--   * `speed` - a floating point number >= 0, the speed at which the animation should play (default: 1)
--   * `startKey` - the animation key at which the animation should start (default: "start")
--   * `stopKey` - the animation key at which the animation should end (default: "stop")
--   * `startPoint` - a floating point number 0 <= value <= 1, starting completion of the animation (default: 0)
--   * `forceLoop` - a boolean, to set if the animation should loop even if it's not a looping animation (default: false)

---
-- Check if the actor's animation has the given animation group or not.
-- @function [parent=#animation] hasGroup
-- @param openfallout.core#GameObject actor
-- @param #string groupName
-- @return #boolean 

---
-- Check if the actor's skeleton has the given bone or not
-- @function [parent=#animation] hasBone
-- @param openfallout.core#GameObject actor
-- @param #string boneName
-- @return #boolean 

---
-- Get the current active animation for a bone group
-- @function [parent=#animation] getActiveGroup
-- @param openfallout.core#GameObject actor
-- @param #number boneGroup Bone group enum, see @{openfallout.animation#BONE_GROUP}
-- @return #string 

---
-- Adds a spell-casting glow to the actor.
-- Can only be used on self.
-- @function [parent=#animation] addGlow
-- @param openfallout.core#GameObject actor
-- @param #table options A table of parameters. Must contain the following required parameters:
--
--   * `color` - the @{openfallout.util#Color} of the glow.
--   * `duration` - a finite duration in seconds. A negative value makes the glow permanent.
--
-- @usage
-- local util = require('openfallout.util')
-- animation.addGlow(self, { color = util.color.rgb(1, 0, 0), duration = 1.5 })

---
-- Plays a VFX on the actor.
-- Can only be used on self. Can also be evoked by sending an AddVfx event to the target actor.
-- @function [parent=#animation] addVfx
-- @param openfallout.core#GameObject actor
-- @param #string model path (normally taken from a record such as @{openfallout.types#StaticRecord.model} or similar)
-- @param #table options optional table of parameters. Can contain:
--
--   * `loop` - boolean, if true the effect will loop until removed (default: false).
--   * `boneName` - name of the bone to attach the vfx to. (default: "")
--   * `particleTextureOverride` - name of the particle texture to use. (default: "")
--   * `vfxId` - a string ID that can be used to remove the effect later, using #removeVfx, and to avoid duplicate effects. The default value of "" can have duplicates. (Default: "").
--   * `useAmbientLight` - boolean, vfx get a white ambient light attached in Morrowind. If false don't attach this. (default: true)
--   * `autoTransform` - boolean, if true, the engine will auto-calculate the transform. (default: true)
--   * `transform` - relative transform (openfallout.util#Transform) applied to the vfx. If autoTransform is true this will be applied on top of it.
--
-- @usage anim.addVfx(self, 'meshes/effects/myeffect.nif', {boneName = 'Bip01 L Hand', loop = true, vfxId = 'myuniquename'})
-- -- later:
-- anim.removeVfx(self, 'myuniquename')
--
-- @usage -- Add vfx to another actor using an event
-- target:sendEvent('AddVfx', {
--   model = 'meshes/effects/myeffect.nif',
--   options = {
--     vfxId = 'myuniquename',
--     loop = false,
--     transform = util.transform.move(100, 0, 0)
--   }
-- })
--

---
-- Removes a specific VFX.
-- Can only be used on self.
-- @function [parent=#animation] removeVfx
-- @param openfallout.core#GameObject actor
-- @param #string vfxId a string ID that uniquely identifies the VFX to remove

---
-- Removes all vfx from the actor.
-- Can only be used on self.
-- @function [parent=#animation] removeAllVfx
-- @param openfallout.core#GameObject actor

return nil
