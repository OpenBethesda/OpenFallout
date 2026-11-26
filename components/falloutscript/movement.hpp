#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_MOVEMENT_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_MOVEMENT_H

#include <functional>
#include <optional>
#include <string_view>

#include "vanillacommands.hpp"

namespace FalloutScript::Movement
{
    /// The settings that the speed, the jump and the water of a character are worked out from. A setting that the
    /// files do not have keeps the default of the executables, which are the values here (spec movement.md and
    /// jump-fall-swim.md). Both master files override only the base speed (77) and the sneak share (0.57); New Vegas's
    /// overrides the armour and big-weapon penalties too.
    struct Settings
    {
        double mBaseSpeed = 85; // fMoveBaseSpeed: the walk of a humanoid at a SpeedMult of 100
        double mRunMult = 4; // fMoveRunMult: the run is the walk times this
        double mNoWeaponMult = 1.1; // fMoveNoWeaponMult: a humanoid with no weapon out is this much faster
        double mSneakMult = 0.6; // fMoveSneakMult
        double mOneCrippledLeg = 0.85; // fMoveOneCrippledLegSpeedMult
        double mTwoCrippledLegs = 0.75; // fMoveTwoCrippledLegsSpeedMult
        // New Vegas only (FalloutNV.exe 0x647D10)
        double mHeavyArmorPenalty = 0.2; // fMoveHeavyArmorPenalty
        double mMediumArmorPenalty = 0.1; // fMoveMediumArmorPenalty
        double mTwoHandBigPenalty = 0.15; // fMove2HBigPenalty: a minigun or a launcher out
        double mTwoHandRiflePenalty = 0.1; // fMove2HRPenalty: a rifle or an automatic weapon out
        // Fallout 3 only (Fallout3.exe 0x5845B0)
        double mEncumbranceEffect = 0.4; // fMoveEncumEffect: what the full weight takes, a weapon out
        double mEncumbranceEffectNoWeapon = 0.3; // fMoveEncumEffectNoWeapon: the same, no weapon out
        double mWeightMin = 0; // fMoveWeightMin
        double mWeightMax = 150; // fMoveWeightMax: the weight from which nothing more slows
        // The jump (FalloutNV.exe 0x930640, Fallout3.exe 0x7668B0)
        double mJumpHeight = 64; // fJumpHeightMin: the only height of a jump
        double mJumpSwimmingMult = 2; // fJumpSwimmingMult

        using Lookup = std::function<std::optional<double>(std::string_view)>;

        /// The settings that the lookup (a GMST by its editor id) has
        static Settings fromGameSettings(const Lookup& lookup);
    };

    /// The class of the body armour that a character wears: the flag byte of the armour record has 0x80 for heavy and
    /// 0x08 for medium (FalloutNV.exe 0x514410)
    enum class ArmorClass
    {
        LightOrNone,
        Medium,
        Heavy,
    };

    /// What a mover is, as the speed functions of the executables read it
    struct Mover
    {
        double mSpeedMult = 100; // actor value 21
        /// The legs at 0 condition or less (actor values 29 and 30)
        int mCrippledLegs = 0;
        /// IgnoreCrippledLimbs (actor value 72) is not 0
        bool mIgnoreCrippledLimbs = false;
        /// The weight carried is more than the carry weight
        bool mOverEncumbered = false;
        /// No weapon is out
        bool mHolstered = true;
        /// A character (the player and the NPC records), not a creature
        bool mHumanoid = true;
        bool mSneaking = false;
        ArmorClass mArmor = ArmorClass::LightOrNone;
        /// The animation type of the weapon that is out (the WEAP DNAM byte): -1 for none
        int mWeaponAnimationType = -1;
        /// The weight that the character carries (Fallout 3's slowdown)
        double mCarriedWeight = 0;
    };

    /// The walk of a humanoid in game units a second (New Vegas 0x647D10, Fallout 3 0x5845B0): the base speed by the
    /// SpeedMult, the legs, the holstered bonus, the armour and weapon penalties (New Vegas) or the weight
    /// (Fallout 3), and the sneak share. Never below 0.
    double walkSpeed(Game game, const Settings& settings, const Mover& mover);

    /// The run: the walk times fMoveRunMult (0x647F00, 0x584710), then the perk entry point 42 of New Vegas (Travel
    /// Light), whose result the caller gives as `perkMult`
    double runSpeed(Game game, const Settings& settings, const Mover& mover, double perkMult = 1.0);

    /// The speed in the water (0x885D90 and 0x885ED0, 0x584760 and 0x584790): the walk or the run with the sneak
    /// share off. The settings named fMoveSwim* are read by nothing in either game.
    double swimSpeed(Game game, const Settings& settings, const Mover& mover, bool running, double perkMult = 1.0);

    /// Whether the mover may run: never over the carry weight (the run flag 0x200 is withheld: 0x93E840, 0x788350)
    bool canRun(const Mover& mover);

    /// The speed of a creature (0x8D4EB0, 0x8D4F10): the speed of its own walk or run animation by its SpeedMult; none
    /// over the carry weight
    double creatureSpeed(double animationSpeed, const Mover& mover);

    /// How high a jump goes, in game units: fJumpHeightMin times the size of the character (the reference's scale times
    /// the height of the record), twice as high swimming. Nothing else counts: no skill, weight, sneak or legs.
    double jumpHeight(const Settings& settings, double size, bool swimming);

    /// The upward speed that makes a jump of that height under that gravity (units a second squared): the top of the
    /// jump is the height (the executable's launch, sqrt(2 g h): 0xCD43D9, 0x96BB41)
    double jumpSpeed(double height, double gravity);

    /// The lines of the share of the body that is under water (FalloutNV.exe 0x889917, 0x889946, 0x889975)
    constexpr double wadingLine = 0.01;
    constexpr double swimmingLine = 0.7;
    constexpr double headUnderLine = 0.875;

    /// How much of the body is under water (0x885560): the water above the feet over the height of the body times its
    /// size, from 0 to 1
    double submergedShare(double waterLevel, double feet, double bodyHeight);
}

#endif
