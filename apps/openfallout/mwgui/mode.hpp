#ifndef MWGUI_MODE_H
#define MWGUI_MODE_H

namespace OFGui
{
    enum GuiMode
    {
        GM_None,
        GM_Inventory, // Inventory mode (the Lua name is Interface: the Fallout screens that Lua draws are in it)
        GM_MainMenu, // Main menu mode

        // A conversation of Fallout (DialogMenu of the games, drawn by falloutdialogue.lua). No C++ window: the
        // window manager keeps a conversation that offers no goodbye open (FalloutScripts::conversationHeld)
        GM_DialogMenu,

        GM_Loading,
        GM_LoadingWallpaper,
    };
}

#endif
