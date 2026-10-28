/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information.
 * This program is free software licensed under GPL version 2. */
#ifndef SCHOLOMANCE_STATE_H
#define SCHOLOMANCE_STATE_H

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

// Only durable progress belongs here. Runtime GUIDs and failed spawn attempts
// belong to the current map object and must never be restored from a save.
struct ScholomanceProgress
{
    std::array<uint32_t, 10> encounters{};
    uint32_t gambit = 0;
    uint32_t gambitRemaining = 0;
    std::array<uint32_t, 17> entrance{};
    uint32_t entranceCreated = 0;
    uint32_t entranceDead = 0;
    uint32_t paladin = 0;
    uint32_t paladinElapsed = 0;
    uint32_t paladinCreated = 0;
    uint32_t paladinDead = 0;
    uint64_t chargerUntil = 0;
    std::array<float, 4> chargerPosition{};

    bool Valid() const
    {
        for (auto state : encounters) if (state > 3) return false;
        if (gambit != 0 && gambit != 1 && gambit != 3) return false;
        if (gambitRemaining > 12000 || ((gambit == 1) != (gambitRemaining != 0))) return false;
        if ((entranceCreated | entranceDead) > 0x1ffff || (entranceDead & ~entranceCreated)) return false;
        if (!entrance[16])
        {
            for (auto entry : entrance) if (entry) return false;
            if (entranceCreated || entranceDead || encounters[1] == 3) return false;
        }
        else
        {
            if (entrance[16] != 11551 || encounters[1] != 3) return false;
            for (unsigned group = 0; group < 4; ++group)
            {
                unsigned aberrations = 0, ghouls = 0, corpses = 0;
                for (unsigned slot = group * 4; slot < group * 4 + 4; ++slot)
                {
                    switch (entrance[slot])
                    {
                        case 10485: ++aberrations; break;
                        case 10495: ++ghouls; break;
                        case 10481: ++corpses; break;
                        default: return false;
                    }
                }
                if (aberrations != 2 || ghouls < 1 || corpses > 1) return false;
            }
        }
        if (paladin > 3 || paladinElapsed > 300000 || (paladinCreated | paladinDead) > 0x3fff || (paladinDead & ~paladinCreated)) return false;
        if (paladin == 0 && (paladinElapsed || paladinCreated || paladinDead)) return false;
        uint32_t const delays[14] = {3000, 3000, 20000, 60000, 60000, 80000, 120000, 120000, 140000, 200000, 200000, 200000, 230000, 300000};
        for (unsigned slot = 0; slot < 14; ++slot)
            if ((paladinCreated & (1u << slot)) && paladinElapsed < delays[slot]) return false;
        if ((paladin == 3) != ((paladinDead & (1u << 13)) != 0)) return false;
        if (chargerUntil > 4102444800ULL || (chargerUntil && paladin != 3)) return false;
        for (float value : chargerPosition) if (!std::isfinite(value) || std::abs(value) > 100000.0f) return false;
        return true;
    }

    std::string Encode() const
    {
        std::ostringstream out;
        for (auto state : encounters) out << state << ' ';
        out << "SC2 " << gambit << ' ' << gambitRemaining << ' ' << entranceCreated << ' ' << entranceDead;
        for (auto entry : entrance) out << ' ' << entry;
        out << ' ' << paladin << ' ' << paladinElapsed << ' ' << paladinCreated << ' ' << paladinDead << ' ' << chargerUntil;
        out << std::setprecision(9);
        for (float value : chargerPosition) out << ' ' << value;
        return out.str();
    }

    static bool Decode(char const* text, ScholomanceProgress& output)
    {
        if (!text) return false;
        ScholomanceProgress candidate;
        std::istringstream in(text);
        for (auto& state : candidate.encounters) if (!(in >> state) || state > 3) return false;
        in >> std::ws;
        if (in.eof())
        {
            // Old saves cannot tell whether the replacement mobs were looted.
            // Do not mint a second set of rewards when upgrading a completed room.
            if (candidate.encounters[1] == 3)
            {
                for (unsigned i = 0; i < 16; ++i)
                    candidate.entrance[i] = i % 4 < 2 ? 10485 : 10495;
                candidate.entrance[16] = 11551;
                candidate.entranceCreated = candidate.entranceDead = 0x1ffff;
            }
        }
        else
        {
            std::string version;
            if (!(in >> version) || version != "SC2") return false;
            if (!(in >> candidate.gambit >> candidate.gambitRemaining >> candidate.entranceCreated >> candidate.entranceDead)) return false;
            for (auto& entry : candidate.entrance) if (!(in >> entry)) return false;
            if (!(in >> candidate.paladin >> candidate.paladinElapsed >> candidate.paladinCreated >> candidate.paladinDead >> candidate.chargerUntil)) return false;
            for (auto& value : candidate.chargerPosition) if (!(in >> value)) return false;
            in >> std::ws;
            if (!in.eof()) return false;
        }
        if (!candidate.Valid()) return false;
        for (auto& state : candidate.encounters) if (state == 1) state = 0;
        output = candidate; // Commit only a complete, validated record.
        return true;
    }
};
#endif
