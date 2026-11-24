#include "settings.hpp"

#include "components/settings/values.hpp"

namespace OFGui
{
    WindowSettingValues makeConsoleWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mConsoleX,
                .mY = Settings::windows().mConsoleY,
                .mW = Settings::windows().mConsoleW,
                .mH = Settings::windows().mConsoleH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mConsoleMaximizedX,
                .mY = Settings::windows().mConsoleMaximizedY,
                .mW = Settings::windows().mConsoleMaximizedW,
                .mH = Settings::windows().mConsoleMaximizedH,
            },
            .mIsMaximized = Settings::windows().mConsoleMaximized,
        };
    }

    WindowSettingValues makeDebugWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mDebugX,
                .mY = Settings::windows().mDebugY,
                .mW = Settings::windows().mDebugW,
                .mH = Settings::windows().mDebugH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mDebugMaximizedX,
                .mY = Settings::windows().mDebugMaximizedY,
                .mW = Settings::windows().mDebugMaximizedW,
                .mH = Settings::windows().mDebugMaximizedH,
            },
            .mIsMaximized = Settings::windows().mDebugMaximized,
        };
    }

    WindowSettingValues makeInventoryWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mInventoryX,
                .mY = Settings::windows().mInventoryY,
                .mW = Settings::windows().mInventoryW,
                .mH = Settings::windows().mInventoryH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mInventoryMaximizedX,
                .mY = Settings::windows().mInventoryMaximizedY,
                .mW = Settings::windows().mInventoryMaximizedW,
                .mH = Settings::windows().mInventoryMaximizedH,
            },
            .mIsMaximized = Settings::windows().mInventoryMaximized,
        };
    }

    WindowSettingValues makeMapWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mMapX,
                .mY = Settings::windows().mMapY,
                .mW = Settings::windows().mMapW,
                .mH = Settings::windows().mMapH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mMapMaximizedX,
                .mY = Settings::windows().mMapMaximizedY,
                .mW = Settings::windows().mMapMaximizedW,
                .mH = Settings::windows().mMapMaximizedH,
            },
            .mIsMaximized = Settings::windows().mMapMaximized,
        };
    }

    WindowSettingValues makePostprocessorWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mPostprocessorX,
                .mY = Settings::windows().mPostprocessorY,
                .mW = Settings::windows().mPostprocessorW,
                .mH = Settings::windows().mPostprocessorH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mPostprocessorMaximizedX,
                .mY = Settings::windows().mPostprocessorMaximizedY,
                .mW = Settings::windows().mPostprocessorMaximizedW,
                .mH = Settings::windows().mPostprocessorMaximizedH,
            },
            .mIsMaximized = Settings::windows().mPostprocessorMaximized,
        };
    }

    WindowSettingValues makeSettingsWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mSettingsX,
                .mY = Settings::windows().mSettingsY,
                .mW = Settings::windows().mSettingsW,
                .mH = Settings::windows().mSettingsH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mSettingsMaximizedX,
                .mY = Settings::windows().mSettingsMaximizedY,
                .mW = Settings::windows().mSettingsMaximizedW,
                .mH = Settings::windows().mSettingsMaximizedH,
            },
            .mIsMaximized = Settings::windows().mSettingsMaximized,
        };
    }

    WindowSettingValues makeSpellsWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mSpellsX,
                .mY = Settings::windows().mSpellsY,
                .mW = Settings::windows().mSpellsW,
                .mH = Settings::windows().mSpellsH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mSpellsMaximizedX,
                .mY = Settings::windows().mSpellsMaximizedY,
                .mW = Settings::windows().mSpellsMaximizedW,
                .mH = Settings::windows().mSpellsMaximizedH,
            },
            .mIsMaximized = Settings::windows().mSpellsMaximized,
        };
    }

    WindowSettingValues makeStatsWindowSettingValues()
    {
        return WindowSettingValues{
            .mRegular = WindowRectSettingValues {
                .mX = Settings::windows().mStatsX,
                .mY = Settings::windows().mStatsY,
                .mW = Settings::windows().mStatsW,
                .mH = Settings::windows().mStatsH,
            },
            .mMaximized = WindowRectSettingValues {
                .mX = Settings::windows().mStatsMaximizedX,
                .mY = Settings::windows().mStatsMaximizedY,
                .mW = Settings::windows().mStatsMaximizedW,
                .mH = Settings::windows().mStatsMaximizedH,
            },
            .mIsMaximized = Settings::windows().mStatsMaximized,
        };
    }

}
