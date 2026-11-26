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
}
