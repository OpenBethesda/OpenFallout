#ifndef OPENFALLOUT_APPS_OPENFALLOUT_MWGUI_SETTINGS_H
#define OPENFALLOUT_APPS_OPENFALLOUT_MWGUI_SETTINGS_H

#include "components/settings/settingvalue.hpp"

namespace OFGui
{
    struct WindowRectSettingValues
    {
        Settings::SettingValue<float>& mX;
        Settings::SettingValue<float>& mY;
        Settings::SettingValue<float>& mW;
        Settings::SettingValue<float>& mH;
    };

    struct WindowSettingValues
    {
        WindowRectSettingValues mRegular;
        WindowRectSettingValues mMaximized;
        Settings::SettingValue<bool>& mIsMaximized;
    };

    WindowSettingValues makeConsoleWindowSettingValues();
    WindowSettingValues makeDebugWindowSettingValues();
    WindowSettingValues makeInventoryWindowSettingValues();
    WindowSettingValues makeMapWindowSettingValues();
    WindowSettingValues makePostprocessorWindowSettingValues();
    WindowSettingValues makeSettingsWindowSettingValues();
    WindowSettingValues makeSpellsWindowSettingValues();
    WindowSettingValues makeStatsWindowSettingValues();
}

#endif
