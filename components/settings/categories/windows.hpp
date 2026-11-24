#ifndef OPENFALLOUT_COMPONENTS_SETTINGS_CATEGORIES_WINDOWS_H
#define OPENFALLOUT_COMPONENTS_SETTINGS_CATEGORIES_WINDOWS_H

#include <components/settings/settingvalue.hpp>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <cstdint>
#include <string>
#include <string_view>

namespace Settings
{
    struct WindowsCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<float> mConsoleX{ mIndex, "Windows", "console x" };
        SettingValue<float> mConsoleY{ mIndex, "Windows", "console y" };
        SettingValue<float> mConsoleW{ mIndex, "Windows", "console w" };
        SettingValue<float> mConsoleH{ mIndex, "Windows", "console h" };
        SettingValue<float> mConsoleMaximizedX{ mIndex, "Windows", "console maximized x" };
        SettingValue<float> mConsoleMaximizedY{ mIndex, "Windows", "console maximized y" };
        SettingValue<float> mConsoleMaximizedW{ mIndex, "Windows", "console maximized w" };
        SettingValue<float> mConsoleMaximizedH{ mIndex, "Windows", "console maximized h" };
        SettingValue<bool> mConsoleMaximized{ mIndex, "Windows", "console maximized" };
        SettingValue<float> mSettingsX{ mIndex, "Windows", "settings x" };
        SettingValue<float> mSettingsY{ mIndex, "Windows", "settings y" };
        SettingValue<float> mSettingsW{ mIndex, "Windows", "settings w" };
        SettingValue<float> mSettingsH{ mIndex, "Windows", "settings h" };
        SettingValue<float> mSettingsMaximizedX{ mIndex, "Windows", "settings maximized x" };
        SettingValue<float> mSettingsMaximizedY{ mIndex, "Windows", "settings maximized y" };
        SettingValue<float> mSettingsMaximizedW{ mIndex, "Windows", "settings maximized w" };
        SettingValue<float> mSettingsMaximizedH{ mIndex, "Windows", "settings maximized h" };
        SettingValue<bool> mSettingsMaximized{ mIndex, "Windows", "settings maximized" };
        SettingValue<float> mPostprocessorH{ mIndex, "Windows", "postprocessor h" };
        SettingValue<float> mPostprocessorW{ mIndex, "Windows", "postprocessor w" };
        SettingValue<float> mPostprocessorX{ mIndex, "Windows", "postprocessor x" };
        SettingValue<float> mPostprocessorY{ mIndex, "Windows", "postprocessor y" };
        SettingValue<float> mPostprocessorMaximizedX{ mIndex, "Windows", "postprocessor maximized x" };
        SettingValue<float> mPostprocessorMaximizedY{ mIndex, "Windows", "postprocessor maximized y" };
        SettingValue<float> mPostprocessorMaximizedW{ mIndex, "Windows", "postprocessor maximized w" };
        SettingValue<float> mPostprocessorMaximizedH{ mIndex, "Windows", "postprocessor maximized h" };
        SettingValue<bool> mPostprocessorMaximized{ mIndex, "Windows", "postprocessor maximized" };
        SettingValue<float> mDebugX{ mIndex, "Windows", "debug x" };
        SettingValue<float> mDebugY{ mIndex, "Windows", "debug y" };
        SettingValue<float> mDebugW{ mIndex, "Windows", "debug w" };
        SettingValue<float> mDebugH{ mIndex, "Windows", "debug h" };
        SettingValue<float> mDebugMaximizedX{ mIndex, "Windows", "debug maximized x" };
        SettingValue<float> mDebugMaximizedY{ mIndex, "Windows", "debug maximized y" };
        SettingValue<float> mDebugMaximizedW{ mIndex, "Windows", "debug maximized w" };
        SettingValue<float> mDebugMaximizedH{ mIndex, "Windows", "debug maximized h" };
        SettingValue<bool> mDebugMaximized{ mIndex, "Windows", "debug maximized" };
    };
}

#endif
