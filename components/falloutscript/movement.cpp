#include "movement.hpp"

#include <algorithm>
#include <cmath>

namespace FalloutScript::Movement
{
    namespace
    {
        void take(const Settings::Lookup& lookup, std::string_view name, double& setting)
        {
            if (const std::optional<double> value = lookup(name))
                setting = *value;
        }

        double legMultiplier(const Settings& settings, const Mover& mover)
        {
            // IgnoreCrippledLimbs does not help a mover over the carry weight (0x647E4C)
            if (mover.mIgnoreCrippledLimbs && !mover.mOverEncumbered)
                return 1;
            if (mover.mCrippledLegs >= 2)
                return settings.mTwoCrippledLegs;
            if (mover.mCrippledLegs == 1)
                return settings.mOneCrippledLeg;
            return 1;
        }

        /// The armour and weapon penalties of New Vegas, summed (0x647D1B to 0x647DCE). The two-handed weapons are the
        /// animation types 2, 5, 6, 8 and 9: 8 and 9 (a handle, a launcher) take the big penalty, 5 and 6 (a rifle, an
        /// automatic) the rifle one, and 2 (melee) none.
        double newVegasPenalty(const Settings& settings, const Mover& mover)
        {
            double penalty = 0;
            if (mover.mArmor == ArmorClass::Heavy)
                penalty += settings.mHeavyArmorPenalty;
            else if (mover.mArmor == ArmorClass::Medium)
                penalty += settings.mMediumArmorPenalty;
            if (!mover.mHolstered)
            {
                switch (mover.mWeaponAnimationType)
                {
                    case 8:
                    case 9:
                        penalty += settings.mTwoHandBigPenalty;
                        break;
                    case 5:
                    case 6:
                        penalty += settings.mTwoHandRiflePenalty;
                        break;
                    default:
                        break;
                }
            }
            return penalty;
        }

        /// The weight term of Fallout 3 (0x5845B7 to 0x5846C6)
        double fallout3Slowdown(const Settings& settings, const Mover& mover)
        {
            const double weight = std::min(std::max(mover.mCarriedWeight, 0.0), settings.mWeightMax);
            const double span = settings.mWeightMax - settings.mWeightMin;
            if (span <= 0)
                return 0;
            const double share = (weight + settings.mWeightMin) / span;
            const bool noWeapon = mover.mHolstered && mover.mHumanoid;
            return share * (noWeapon ? settings.mEncumbranceEffectNoWeapon : settings.mEncumbranceEffect);
        }

        double core(Game game, const Settings& settings, const Mover& mover, bool sneaking)
        {
            double speed = settings.mBaseSpeed * (mover.mSpeedMult * 0.01) * legMultiplier(settings, mover);
            if (mover.mHolstered && mover.mHumanoid)
                speed *= settings.mNoWeaponMult;
            if (game == Game::NewVegas)
                speed *= 1 - newVegasPenalty(settings, mover);
            else
                speed *= 1 - fallout3Slowdown(settings, mover);
            if (sneaking)
                speed *= settings.mSneakMult;
            return std::max(speed, 0.0);
        }
    }

    Settings Settings::fromGameSettings(const Lookup& lookup)
    {
        Settings settings;
        if (!lookup)
            return settings;
        take(lookup, "fMoveBaseSpeed", settings.mBaseSpeed);
        take(lookup, "fMoveRunMult", settings.mRunMult);
        take(lookup, "fMoveNoWeaponMult", settings.mNoWeaponMult);
        take(lookup, "fMoveSneakMult", settings.mSneakMult);
        take(lookup, "fMoveOneCrippledLegSpeedMult", settings.mOneCrippledLeg);
        take(lookup, "fMoveTwoCrippledLegsSpeedMult", settings.mTwoCrippledLegs);
        take(lookup, "fMoveHeavyArmorPenalty", settings.mHeavyArmorPenalty);
        take(lookup, "fMoveMediumArmorPenalty", settings.mMediumArmorPenalty);
        take(lookup, "fMove2HBigPenalty", settings.mTwoHandBigPenalty);
        take(lookup, "fMove2HRPenalty", settings.mTwoHandRiflePenalty);
        take(lookup, "fMoveEncumEffect", settings.mEncumbranceEffect);
        take(lookup, "fMoveEncumEffectNoWeapon", settings.mEncumbranceEffectNoWeapon);
        take(lookup, "fMoveWeightMin", settings.mWeightMin);
        take(lookup, "fMoveWeightMax", settings.mWeightMax);
        take(lookup, "fJumpHeightMin", settings.mJumpHeight);
        take(lookup, "fJumpSwimmingMult", settings.mJumpSwimmingMult);
        return settings;
    }

    double walkSpeed(Game game, const Settings& settings, const Mover& mover)
    {
        return core(game, settings, mover, mover.mSneaking);
    }

    double runSpeed(Game game, const Settings& settings, const Mover& mover, double perkMult)
    {
        double speed = walkSpeed(game, settings, mover) * settings.mRunMult;
        // Travel Light is New Vegas's
        if (game == Game::NewVegas)
            speed *= perkMult;
        return std::max(speed, 0.0);
    }

    double swimSpeed(Game game, const Settings& settings, const Mover& mover, bool running, double perkMult)
    {
        double speed = core(game, settings, mover, false);
        if (running)
        {
            speed *= settings.mRunMult;
            if (game == Game::NewVegas)
                speed *= perkMult;
        }
        return std::max(speed, 0.0);
    }

    bool canRun(const Mover& mover)
    {
        return !mover.mOverEncumbered;
    }

    double creatureSpeed(double animationSpeed, const Mover& mover)
    {
        if (mover.mOverEncumbered)
            return 0;
        return std::max(animationSpeed * mover.mSpeedMult * 0.01, 0.0);
    }

    double jumpHeight(const Settings& settings, double size, bool swimming)
    {
        const double height = settings.mJumpHeight * (size > 0 ? size : 1.0);
        return std::max(swimming ? height * settings.mJumpSwimmingMult : height, 0.0);
    }

    double jumpSpeed(double height, double gravity)
    {
        if (height <= 0 || gravity <= 0)
            return 0;
        return std::sqrt(2 * gravity * height);
    }

    double submergedShare(double waterLevel, double feet, double bodyHeight)
    {
        if (bodyHeight <= 0 || waterLevel <= feet)
            return 0;
        return std::min((waterLevel - feet) / bodyHeight, 1.0);
    }
}
