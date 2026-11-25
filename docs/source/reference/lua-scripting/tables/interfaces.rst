.. list-table::
  :widths: 30 40 60
  :header-rows: 1

  * - Interface
    - Context
    - Description
  * - :doc:`Activation </reference/lua-scripting/interface_activation>`
    - |bdg-ctx-global|
    - Allows to extend or override built-in activation mechanics.
  * - :doc:`Camera </reference/lua-scripting/interface_camera>`
    - |bdg-ctx-player|
    - Allows to alter behavior of the built-in camera script without overriding the script completely.
  * - :doc:`Controls </reference/lua-scripting/interface_controls>`
    - |bdg-ctx-player|
    - Allows to alter behavior of the built-in script that handles player controls.
  * - :doc:`GamepadControls </reference/lua-scripting/interface_gamepadcontrols>`
    - |bdg-ctx-player|
    - Allows to alter behavior of the built-in script that handles player gamepad controls.
  * - :doc:`ItemUsage </reference/lua-scripting/interface_item_usage>`
    - |bdg-ctx-global|
    - Allows to extend or override built-in item usage mechanics.
  * - :doc:`MWUI </reference/lua-scripting/interface_mwui>`
    - |bdg-ctx-menu| |bdg-ctx-player|
    - Morrowind-style UI templates.
  * - :doc:`Settings </reference/lua-scripting/interface_settings>`
    - |bdg-ctx-global| |bdg-ctx-menu| |bdg-ctx-player| 
    - Save, display and track changes of setting values.
  * - :doc:`UI </reference/lua-scripting/interface_ui>`
    - |bdg-ctx-player|
    - High-level UI modes interface. Allows to override parts of the interface.
