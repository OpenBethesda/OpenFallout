#ifndef OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_RAGDOLLRULES_H
#define OPENFALLOUT_COMPONENTS_FALLOUTSCRIPT_RAGDOLLRULES_H

#include <cstdint>
#include <map>
#include <vector>

/// The rules of the executables for the bodies of a ragdoll (fallout-rules.md A.K3)
namespace FalloutScript::Ragdoll
{
    /// The layers of Havok that a body of a character has (NV table 0x11B0810)
    constexpr unsigned bipedLayer = 8;
    constexpr unsigned deadBipedLayer = 29;

    /// Two parts of one body collide when the executable's table of parts says so (NV 0xC84740 for two bodies of the
    /// layer BIPED with the same group, the table 0x1268078 that 0xC828F0 fills). Parts are bits 8 to 12 of the filter.
    bool partsCollide(unsigned partA, unsigned partB);

    /// The game settings that the water of Havok uses (NV 0x62B1E0, 0x62B310, 0x63017A), with the executable's defaults
    struct WaterSettings
    {
        /// fBuoyancy* of the Havok materials 0 to 9 (stone, cloth, dirt, glass, grass, metal, organic, skin, water,
        /// wood), then 24 (bottle) and 25 (soda can); a material that is not there floats by 0
        std::map<std::uint32_t, float> mMaterialBuoyancy
            = { { 0, -0.75f }, { 1, 0.75f }, { 2, 0.25f }, { 3, -0.2f }, { 4, 0.25f }, { 5, -1.f }, { 6, 0.75f },
                  { 7, 0.75f }, { 8, 0.f }, { 9, 1.f }, { 24, 0.75f }, { 25, 0.75f } };
        float mMultBody = 1.f; // fBuoyancyMultBody
        float mMultExtremity = -0.3f; // fBuoyancyMultExtremity
        float mMaxSpeed = 2.f; // fSubmergedMaxSpeed, Havok units a second
        float mLinearDampingH = 2.f; // fSubmergedLinearDampingH
        float mLinearDampingV = 4.f; // fSubmergedLinearDampingV
        float mAngularDamping = 0.75f; // fSubmergedAngularDamping
    };

    /// How much a body floats (NV 0x62B310): the mean of the buoyancy of the materials of its pieces (0x62B1E0), times
    /// fBuoyancyMultBody or fBuoyancyMultExtremity by its part when it is of the layer BIPED or DEADBIP and floats
    /// (0x62B3D3 to 0x62B446: the table at 0x62B464, of part - 1; parts 17 to 21 give 0, those after 22 keep it), and
    /// times fBuoyancyMultBody when it sinks
    float buoyancy(
        const WaterSettings& settings, const std::vector<std::uint32_t>& materials, unsigned layer, unsigned part);

    /// How far the water is between a quarter and 85% of the height of the body (NV 0x630275 to 0x6302EA)
    float submergedFraction(float bottom, float top, float water);

    /// The buoyancy that is used at that depth (NV 0x63032C to 0x6303BD): a body that floats and is less than 1.2 of
    /// the way under is held at the surface, 0.3 from 0.75 up, 0 from 0.5 up and -0.3 below (the two thresholds of
    /// 0x11CC5E4 and 0x11CC5E0 are never written, so 0)
    float submergedBuoyancy(float fraction, float buoyancy);

    /// Whether the fall of a body in the water is slowed (NV 0x6303C3): not when it floats less than 1.2 of the way
    /// under and moves across slower than fSubmergedMaxSpeed
    bool dampsFall(float fraction, float buoyancy, float acrossSpeed, float maxSpeed);
}

#endif
