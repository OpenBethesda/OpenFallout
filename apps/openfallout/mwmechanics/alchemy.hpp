#ifndef GAME_MWMECHANICS_ALCHEMY_H
#define GAME_MWMECHANICS_ALCHEMY_H

#include <string>
#include <vector>

#include "../mwworld/ptr.hpp"

namespace OFMechanics
{
    /// \brief What an actor's alchemy skill lets it know about an ingredient or a potion of Morrowind. The making of
    /// potions went with its window (Fallout makes things at a workbench, falloutcraft.lua)
    class Alchemy
    {
    public:
        static bool knownEffect(size_t potionEffectIndex, const OFWorld::Ptr& npc);
        ///< Does npc have sufficient alchemy skill to know about this potion effect?

        static std::vector<std::string> effectsDescription(const OFWorld::ConstPtr& ptr, const float alchemySKill);
    };
}

#endif
