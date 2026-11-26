#ifndef OPENFALLOUT_CONSTANTS_H
#define OPENFALLOUT_CONSTANTS_H

#include <string>

namespace Constants
{

    // The game uses 64 units per yard
    constexpr float UnitsPerMeter = 69.99125109f;
    constexpr float UnitsPerFoot = 21.33333333f;

    // Sound speed in meters per second
    constexpr float SoundSpeedInAir = 343.3f;
    constexpr float SoundSpeedUnderwater = 1484.0f;

    // Gravity in m/sec^2: the standard gravity, Havok's default. The executables of Fallout 3 and New Vegas set no
    // gravity of their own (no setting, no INI line: rule A.C2 of docs/openfallout/fallout-rules.md); times
    // UnitsPerMeter it is about 686 units/sec^2, under which the jump of fJumpHeightMin (64 units) lasts 0.86 s
    constexpr float GravityConst = 9.8f;

    // Size of one exterior cell in game units
    constexpr int CellSizeInUnits = 8192;
    constexpr int ESM4CellSizeInUnits = 4096;

    // Size of active cell grid in cells (it is a square with the (2 * CellGridRadius + 1) cells side)
    constexpr int CellGridRadius = 1;

    // ESM4 cells are twice smaller, so the active grid should have more cells.
    constexpr int ESM4CellGridRadius = CellGridRadius * 2;

    // A label to mark night/day visual switches
    const std::string NightDayLabel = "NightDaySwitch";

    // A label to mark visual switches for herbalism feature
    const std::string HerbalismLabel = "HerbalismSwitch";

    // Percentage height at which projectiles are spawned from an actor
    constexpr float TorsoHeight = 0.75f;

    // The step the character climbs without a jump: the executables have no setting for it (rule A.C2), and the
    // engine's 34 units is kept (a stair of the games is 16 to 24 units high)
    static constexpr float sStepSizeUp = 34.0f;
    // The steepest ground the character walks on: 47 degrees in both executables (NV [0x11b0148], rule A.C2)
    static constexpr float sMaxSlope = 47.0f;

    // Identifier for main scene camera
    const std::string SceneCamera = "SceneCam";

}

#endif
