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

        SettingValue<float> mStatsX{ mIndex, "Windows", "stats x" };
        SettingValue<float> mStatsY{ mIndex, "Windows", "stats y" };
        SettingValue<float> mStatsW{ mIndex, "Windows", "stats w" };
        SettingValue<float> mStatsH{ mIndex, "Windows", "stats h" };
        SettingValue<float> mStatsMaximizedX{ mIndex, "Windows", "stats maximized x" };
        SettingValue<float> mStatsMaximizedY{ mIndex, "Windows", "stats maximized y" };
        SettingValue<float> mStatsMaximizedW{ mIndex, "Windows", "stats maximized w" };
        SettingValue<float> mStatsMaximizedH{ mIndex, "Windows", "stats maximized h" };
        SettingValue<bool> mStatsPin{ mIndex, "Windows", "stats pin" };
        SettingValue<bool> mStatsHidden{ mIndex, "Windows", "stats hidden" };
        SettingValue<bool> mStatsMaximized{ mIndex, "Windows", "stats maximized" };
        SettingValue<float> mSpellsX{ mIndex, "Windows", "spells x" };
        SettingValue<float> mSpellsY{ mIndex, "Windows", "spells y" };
        SettingValue<float> mSpellsW{ mIndex, "Windows", "spells w" };
        SettingValue<float> mSpellsH{ mIndex, "Windows", "spells h" };
        SettingValue<float> mSpellsMaximizedX{ mIndex, "Windows", "spells maximized x" };
        SettingValue<float> mSpellsMaximizedY{ mIndex, "Windows", "spells maximized y" };
        SettingValue<float> mSpellsMaximizedW{ mIndex, "Windows", "spells maximized w" };
        SettingValue<float> mSpellsMaximizedH{ mIndex, "Windows", "spells maximized h" };
        SettingValue<bool> mSpellsPin{ mIndex, "Windows", "spells pin" };
        SettingValue<bool> mSpellsHidden{ mIndex, "Windows", "spells hidden" };
        SettingValue<bool> mSpellsMaximized{ mIndex, "Windows", "spells maximized" };
        SettingValue<float> mMapX{ mIndex, "Windows", "map x" };
        SettingValue<float> mMapY{ mIndex, "Windows", "map y" };
        SettingValue<float> mMapW{ mIndex, "Windows", "map w" };
        SettingValue<float> mMapH{ mIndex, "Windows", "map h" };
        SettingValue<float> mMapMaximizedX{ mIndex, "Windows", "map maximized x" };
        SettingValue<float> mMapMaximizedY{ mIndex, "Windows", "map maximized y" };
        SettingValue<float> mMapMaximizedW{ mIndex, "Windows", "map maximized w" };
        SettingValue<float> mMapMaximizedH{ mIndex, "Windows", "map maximized h" };
        SettingValue<bool> mMapPin{ mIndex, "Windows", "map pin" };
        SettingValue<bool> mMapHidden{ mIndex, "Windows", "map hidden" };
        SettingValue<bool> mMapMaximized{ mIndex, "Windows", "map maximized" };
        SettingValue<float> mInventoryX{ mIndex, "Windows", "inventory x" };
        SettingValue<float> mInventoryY{ mIndex, "Windows", "inventory y" };
        SettingValue<float> mInventoryW{ mIndex, "Windows", "inventory w" };
        SettingValue<float> mInventoryH{ mIndex, "Windows", "inventory h" };
        SettingValue<float> mInventoryMaximizedX{ mIndex, "Windows", "inventory maximized x" };
        SettingValue<float> mInventoryMaximizedY{ mIndex, "Windows", "inventory maximized y" };
        SettingValue<float> mInventoryMaximizedW{ mIndex, "Windows", "inventory maximized w" };
        SettingValue<float> mInventoryMaximizedH{ mIndex, "Windows", "inventory maximized h" };
        SettingValue<bool> mInventoryPin{ mIndex, "Windows", "inventory pin" };
        SettingValue<bool> mInventoryHidden{ mIndex, "Windows", "inventory hidden" };
        SettingValue<bool> mInventoryMaximized{ mIndex, "Windows", "inventory maximized" };
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
