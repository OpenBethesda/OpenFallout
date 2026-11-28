#include "fightai.hpp"

#include <algorithm>
#include <cmath>

namespace FalloutScript::FightAI
{
    bool attacksOnSight(int aggression, Standing standing)
    {
        switch (aggression)
        {
            case Aggression::aggressive:
                return standing == Standing::Enemy;
            case Aggression::veryAggressive:
                return standing == Standing::Enemy || standing == Standing::Neutral;
            case Aggression::frenzied:
                return true;
            default:
                return false;
        }
    }

    double fleeHealthShare(int confidence)
    {
        switch (confidence)
        {
            case Confidence::cowardly:
                return 1.01;
            case Confidence::cautious:
                return 0.5;
            case Confidence::average:
                return 0.25;
            case Confidence::brave:
                return 0.1;
            default:
                return 0.0;
        }
    }

    bool runsAway(int confidence, double healthShare)
    {
        return healthShare < fleeHealthShare(confidence);
    }

    double engageDistance(bool melee, double reach)
    {
        if (melee)
            return std::max(reach * 0.7, 50.0);
        // A shot is made from the middle of its range, but a weapon of short range from as far as it reaches, and none
        // from beyond what a character can aim at
        return std::clamp(reach * 0.5, std::min(reach, 300.0), 1500.0);
    }

    Move decide(double distance, double engage, bool visible, bool flee)
    {
        if (flee)
            return distance < fleeDistance ? Move::Retreat : Move::Hold;
        if (!visible || distance > engage)
            return Move::Approach;
        return Move::Hold;
    }

    bool withinFiringArc(double offHeading, double targetingFov, double aimArc, bool moving, double stationaryShare)
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double degree = pi / 180.0;
        // (the exe turns the difference into -π to π and takes its size, 0x4B15E0 and 0x408840)
        double off = std::fmod(offHeading, 2.0 * pi);
        if (off > pi)
            off -= 2.0 * pi;
        else if (off < -pi)
            off += 2.0 * pi;
        off = std::abs(off);
        if (targetingFov > 0 && off > targetingFov * degree * 0.5)
            return false;
        if (aimArc > 0)
        {
            const double arc = aimArc * degree * (moving ? 1.0 : stationaryShare);
            if (off > arc * 0.5)
                return false;
        }
        return true;
    }

    bool keepsFiring(bool inSight, double sinceSight, double sinceDetected, double losBuffer, double detectionBuffer)
    {
        return inSight || (sinceSight < losBuffer && sinceDetected < detectionBuffer);
    }

    FormId bestWeapon(const std::vector<WeaponCandidate>& candidates)
    {
        FormId best = 0;
        double bestScore = 0;
        for (const WeaponCandidate& candidate : candidates)
        {
            if (candidate.mWeapon == 0)
                continue;
            if (!candidate.mMelee && candidate.mRounds < std::max(candidate.mAmmoUse, 1))
                continue;
            const double score = candidate.mDamage * (candidate.mMelee ? 1.0 : 1.25);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate.mWeapon;
            }
        }
        return best;
    }

    double hitChance(double skill)
    {
        return std::clamp(skill / 100.0, 0.3, 0.95);
    }

    bool lineOfFireBlocked(FloorPoint shooter, FloorPoint target, const std::vector<FloorPoint>& others)
    {
        const double aimX = target.mX - shooter.mX;
        const double aimY = target.mY - shooter.mY;
        const double aimLength = std::hypot(aimX, aimY);
        if (aimLength <= 0)
            return false;
        for (const FloorPoint& other : others)
        {
            const double x = other.mX - shooter.mX;
            const double y = other.mY - shooter.mY;
            const double length = std::hypot(x, y);
            // One that is as near as the target, or on the shooter, is not in the way
            if (length <= 0 || length >= aimLength)
                continue;
            const double cosine = std::clamp((x * aimX + y * aimY) / (length * aimLength), -1.0, 1.0);
            if (std::acos(cosine) < lineOfFireAngle)
                return true;
        }
        return false;
    }
}
