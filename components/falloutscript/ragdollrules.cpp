#include "ragdollrules.hpp"

#include <array>

namespace FalloutScript::Ragdoll
{
    namespace
    {
        /// NV 0x1268078 as 0xC828F0 fills it: for each part, a bit for each part of the same body that it collides with
        constexpr std::array<std::uint32_t, 32> partTable
            = { 0x0, 0x4230c0, 0x14030c0, 0x14030c0, 0x14230c0, 0x403000, 0x42791e, 0x41ff1e, 0x145f0c0, 0x145e080,
                  0x144e080, 0x4000c0, 0x4241fe, 0x45c7fe, 0x14437c0, 0x1442780, 0x1442380, 0x401052, 0x41e700, 0x0,
                  0x400000, 0x400000, 0x37fffe, 0x0, 0x1c71c, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };
    }

    bool partsCollide(unsigned partA, unsigned partB)
    {
        return partA < partTable.size() && partB < 32 && (partTable[partA] & (1u << partB)) != 0;
    }

    float buoyancy(
        const WaterSettings& settings, const std::vector<std::uint32_t>& materials, unsigned layer, unsigned part)
    {
        float result = 0.f;
        for (const std::uint32_t material : materials)
        {
            const auto found = settings.mMaterialBuoyancy.find(material);
            result += found != settings.mMaterialBuoyancy.end() ? found->second : 0.f;
        }
        if (!materials.empty())
            result /= static_cast<float>(materials.size());
        float mult = 1.f;
        if (layer == bipedLayer || layer == deadBipedLayer)
        {
            if (result > 0.f)
            {
                // 0x62B464: the case of part - 1 (0 body, 1 extremity, 2 none)
                constexpr std::array<std::uint8_t, 22> cases
                    = { 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 2, 2, 2, 2, 2, 1 };
                const int index = static_cast<int>(part) - 1;
                if (index >= 0 && index < static_cast<int>(cases.size()))
                    mult = cases[index] == 0 ? settings.mMultBody : (cases[index] == 1 ? settings.mMultExtremity : 0.f);
            }
            else
                mult = settings.mMultBody;
        }
        return result * mult;
    }

    float submergedFraction(float bottom, float top, float water)
    {
        const float height = top - bottom;
        const float low = bottom + 0.25f * height;
        const float high = bottom + 0.85f * height;
        if (high <= low)
            return water > low ? 1.f : 0.f;
        return (water - low) / (high - low);
    }

    float submergedBuoyancy(float fraction, float buoyancy)
    {
        if (fraction >= 1.2f || buoyancy <= 0.f)
            return buoyancy;
        if (fraction >= 0.75f)
            return 0.3f;
        if (fraction >= 0.5f)
            return 0.f;
        if (fraction >= 0.f)
            return -0.3f;
        return buoyancy;
    }

    bool dampsFall(float fraction, float buoyancy, float acrossSpeed, float maxSpeed)
    {
        return !(fraction < 1.2f && buoyancy > 0.f && acrossSpeed < maxSpeed);
    }
}
