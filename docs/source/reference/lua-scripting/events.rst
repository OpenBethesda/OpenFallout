Events
======

.. include:: version.rst

Actor events
------------

**DialogueResponse**

This event is sent to the player's local script when an actor triggers a greeting, topic response, service refusal, or plays a voice line.
It returns a lua table with the following fields:
- ``actor``: The actor that responded.
- ``type``: The type of ``DialogueRecord`` that triggered the event, can be ``"greeting"``, ``"journal"``, ``"persuasion"``, ``"topic"``, or ``"voice"``.
- ``recordId``: The ID of the ``DialogueRecord``.
- ``infoId``: The ID of the ``DialogueRecordInfo`` in question.

.. code-block:: Lua

    eventHandlers = {
        DialogueResponse = function(e)
            local topic = core.dialogue[e.type].records[e.recordId];
            for _, info in pairs(topic.infos) do
                if info.id == e.infoId then
                    print(e.actor, 'said', info.text)
                    return
                end
            end
        end
    }

**Died**

This event is sent to an actor's local script when that actor dies.

.. code-block:: Lua

    eventHandlers = {
        Died = function()
            print('Alas, ye hardly knew me!')
        end
    }

**UseItem**

Any script can send global event ``UseItem`` with arguments ``object``, ``actor``, and optional boolean ``force``.
The actor will use (e.g. equip or consume) the object. The object should be in the actor's inventory.

Example:

.. code-block:: Lua

    core.sendGlobalEvent('UseItem', {object = potion, actor = player, force = true})

**AddVfx**

Calls the corresponding method in openfallout.animation

.. code-block:: Lua

    local eventParams = {
        model = 'vfx_default',
        options = {
            textureOverride = effect.particle,
        },
    }
    actor:sendEvent('AddVfx', eventParams)

**PlaySound3d**

Calls the corresponding function in openw.core on the target. Will use core.sound.playSoundFile3d instead of core.sound.playSound3d if you put `file` instead of `sound` in the event data.

.. code-block:: Lua
    actor:sendEvent('PlaySound3d', {sound = 'Open Lock'})

UI events
---------

**ShowMessage**

If sent to a player, shows a message as if a call to ui.showMessage was made.

.. code-block:: Lua

    player:sendEvent('ShowMessage', {message = 'Lorem ipsum', showInDialogue = false})

**UiModeChanged**

Every time UI mode is changed built-in scripts send to player the event ``UiModeChanged`` with arguments ``oldMode, ``newMode`` (same as ``I.UI.getMode()``)
and ``arg`` (for example in the mode ``DialogMenu`` the argument is the actor the player is talking to).

.. code-block:: Lua

    eventHandlers = {
        UiModeChanged = function(data)
            print('UiModeChanged from', data.oldMode , 'to', data.newMode, '('..tostring(data.arg)..')')
        end
    }

**AddUiMode**

Equivalent to ``I.UI.addMode``, but can be sent from another object or global script.

.. code-block:: Lua

    player:sendEvent('AddUiMode', {mode = 'DialogMenu', target = actor})

**SetUiMode**

Equivalent to ``I.UI.setMode``, but can be sent from another object or global script.

.. code-block:: Lua

    player:sendEvent('SetUiMode', {mode = 'DialogMenu', target = actor})

World events
------------

Global events that just call the corresponding function in `openfallout.world`.

.. code-block:: Lua

    -- world.pause(tag)
    core.sendGlobalEvent('Pause', tag)

    -- world.unpause(tag)
    core.sendGlobalEvent('Unpause', tag)

    -- world.setGameTimeScale(scale)
    core.sendGlobalEvent('SetGameTimeScale', scale)

    -- world.setSimulationTimeScale(scale)
    core.sendGlobalEvent('SetSimulationTimeScale', scale)


**SpawnVfx, PlaySound3d**

Calls the corresponding function in openw.core. Note that PlaySound3d will call core.sound.playSoundFile3d instead of core.sound.playSound3d if you put `file` instead of `sound` in the event data.

.. code-block:: Lua
    core.sendGlobalEvent('SpawnVfx', {position = hitPos, model = 'vfx_destructarea', options = {scale = 10}})
    core.sendGlobalEvent('PlaySound3d', {sound = 'Open Lock', position = container.position})

**ConsumeItem**

Reduces stack size of an item by a given amount, removing the item completely if stack size is reduced to 0 or less.

.. code-block:: Lua

    core.sendGlobalEvent('ConsumeItem', {item = foobar, amount = 1})

**Lock**

Lock a container or door

.. code-block:: Lua

    core.sendGlobalEvent('Lock', {target = selected, magnitude = 50})

**Unlock**

Unlock a container or door

.. code-block:: Lua

    core.sendGlobalEvent('Unlock', {target = selected})

**SetSaveState**

Sets whether or not an object will be written into the save. For script-spawned objects, setting this to false effectively means they are deleted after the game loads. For objects from content files, setting this to false restores them to their original state upon loading.
Use this as a means to easily reset **individual** objects. To reset all objects in a given cell instead, use `cell:getAll` and call `object:setSaveState(false)` on each object.

.. code-block:: Lua

   core.sendGlobalEvent('SetSaveState', { nearby.actors[#nearby.actors] = false })
