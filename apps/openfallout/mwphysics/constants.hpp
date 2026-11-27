#ifndef OPENFALLOUT_MWPHYSICS_CONSTANTS_H
#define OPENFALLOUT_MWPHYSICS_CONSTANTS_H

namespace OFPhysics
{
    // FalloutNV.exe's character proxy (hkpCharacterProxyCinfo, defaults stored at 0xC6CDE0; the controller's setup
    // 0xC6DA62 changes none of these). One Havok unit is 1 / 0.1428767 units.
    static constexpr double sHavokUnit = 1.0 / 0.1428767293691635;
    // How far below the body a surface still holds it up: the keep contact tolerance, 0.1 Havok units (+0x28, stored at
    // 0xC6CE06 from 0x101E2BC). There is no step down: when nothing holds the body up the on-ground state goes to the
    // in-air state at once (0xCD4855).
    static constexpr float sKeepContactTolerance = static_cast<float>(0.1 * sHavokUnit);
    // How many times the proxy casts the body in one step: +0x6C, 10 (0xC6CE22)
    static constexpr int sMaxIterations = 10;

    // The gap a body keeps from what it touches: the keep distance of FalloutNV.exe's character proxy, 0.05 Havok
    // units (hkpCharacterProxyCinfo +0x4C, stored at 0xC6CE32 from 0x10181BC and not changed by the controller's own
    // setup 0xC6DA62), 0.35 units at 1 / 0.1428767 units each
    static constexpr float sCollisionMargin = static_cast<float>(0.05 * sHavokUnit);
    // The body stands that gap over the ground
    static constexpr float sGroundOffset = sCollisionMargin;
    // Allow for a small amount of penetration to prevent numerical precision issues from causing the "unstuck"ing code
    // to run unnecessarily Currently set to 0 because having the "unstuck"ing code run whenever possible prevents some
    // glitchy snagging issues
    static constexpr float sAllowedPenetration = 0.0f;
}

#endif
