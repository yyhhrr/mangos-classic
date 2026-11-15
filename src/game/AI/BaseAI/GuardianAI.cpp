/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "GuardianAI.h"
#include "Entities/Pet.h"
#include "Grids/Cell.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include <algorithm>
#include <iterator>

namespace
{
    struct PlayerGuardianSummon
    {
        uint32 spellId;
        uint32 creatureEntry;
    };

    // These mobile guardians keep the owner follow movement when combat movement is cleared.
    constexpr PlayerGuardianSummon playerGuardianSummons[] =
    {
        {4073, 2678},   // Mechanical Dragonling
        {4078, 2676},   // Compact Harvest Reaper
        {5666, 3570},   // Guardian Treant
        {6084, 3619},   // Ghost Saber
        {8176, 10656},  // Guardian Felhunter
        {9515, 6867},   // Tracking Hound
        {12749, 8615},  // Mithril Dragonling
        {13049, 8776},  // Emerald Dragon Whelp
        {13166, 8836},  // Battle Chicken
        {17490, 11200}, // Summoned Skeleton
        {18307, 11637}, // Servant of Alexi Barov
        {18308, 11636}, // Servant of Weldon Barov
        {19363, 12257}, // Mechanical Yeti
        {19804, 12473}, // Arcanite Dragonling
        {22649, 14306}, // Eskhandar
        {26066, 15720}, // Timbermaw Ancestor
        {26067, 15721}, // Mechanical Greench
        {29305, 17003}, // Cinder Elemental
    };
}

GuardianAI::GuardianAI(Creature* creature) : CreatureEventAI(creature)
{
    Unit* owner = creature->GetOwner();
    MANGOS_ASSERT(owner);
    m_defaultMovement = FOLLOW_MOTION_TYPE;
    m_followAngle = M_PI_F / 2;
    m_followDist = 1.5f;

    switch (creature->GetUInt32Value(UNIT_CREATED_BY_SPELL))
    {
        case 11939: // Thule Ravenclaw - Imp
            m_followDist = 3.f;
            break;
    }
}

void GuardianAI::JustRespawned()
{
    Unit* owner = m_creature->GetOwner();

    if (!owner)
        return;

    CreatureEventAI::JustRespawned();

    if (GetDefaultMovement() != FOLLOW_MOTION_TYPE)
        return;

    if (owner->IsPlayer() && m_creature->IsPet() && static_cast<Pet*>(m_creature)->IsGuardian())
    {
        uint32 const summonSpell = m_creature->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        uint32 const creatureEntry = m_creature->GetEntry();
        if (std::any_of(std::begin(playerGuardianSummons), std::end(playerGuardianSummons),
            [summonSpell, creatureEntry](auto const& summon)
            {
                return summon.spellId == summonSpell && summon.creatureEntry == creatureEntry;
            }))
            m_creature->SetDefaultMovementType(FOLLOW_MOTION_TYPE);
    }

    RequestFollow(owner);
}

void GuardianAI::UpdateAI(const uint32 diff)
{
    switch (m_reactState)
    {
        case REACT_AGGRESSIVE:
        case REACT_DEFENSIVE:
        {
            Unit* owner = m_creature->GetOwner();
            if (!owner)
                break;

            if (!m_creature->IsInCombat() && owner->IsInCombat() && !(m_creature->IsPet() && ((Pet*)m_creature)->getPetType() == MINI_PET))
                AttackStart(owner->getAttackerForHelper());   // check for getAttackerForHelper() == nullpter in AttackStart()
            break;
        }
        default:
            break;
    }

    CreatureEventAI::UpdateAI(diff);
}

int GuardianAI::Permissible(const Creature* creature)
{
    if (creature->IsPet() && ((Pet*)creature)->getPetType() == GUARDIAN_PET)
        return PERMIT_BASE_SPECIAL;

    return PERMIT_BASE_NO;
}

void GuardianAI::JustDied(Unit* killer)
{
    CreatureEventAI::JustDied(killer);
    RelinquishFollow(m_unit->GetOwnerGuid());
}

void GuardianAI::OnUnsummon()
{
    CreatureEventAI::OnUnsummon();
    RelinquishFollow(m_unit->GetOwnerGuid());
}

void GuardianAI::EnterEvadeMode()
{
    UnitAI::EnterEvadeMode();

    // Handle Evade events
    IncreaseDepthIfNecessary();
    for (auto& i : m_CreatureEventAIList)
    {
        if (i.event.event_type == EVENT_T_EVADE)
            CheckAndReadyEventForExecution(i);
    }
    ProcessEvents();
}
