/* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 * Licensed under the GNU General Public License, version 2 or later.
 */

#ifndef MANGOS_ENGINEERING_TARGET_DUMMY_H
#define MANGOS_ENGINEERING_TARGET_DUMMY_H

#include <cstdint>

namespace EngineeringTargetDummy
{
    struct Definition
    {
        std::uint32_t summonSpell;
        std::uint32_t creatureEntry;
        std::uint32_t passiveSpell;
        std::uint32_t spawnSpell;
    };

    constexpr std::uint32_t Lifetime = 15000;
    constexpr Definition Definitions[] =
    {
        {4071,  2673,  4044, 4507},
        {4072,  2674,  4048, 4092},
        {19805, 12426, 19809, 4092},
    };

    // Match both fields so unrelated guardians and customized summon entries
    // keep their existing summon and loot rules.
    inline Definition const* Find(std::uint32_t spell, std::uint32_t entry)
    {
        for (Definition const& definition : Definitions)
            if (definition.summonSpell == spell && definition.creatureEntry == entry)
                return &definition;
        return nullptr;
    }
}

#endif
