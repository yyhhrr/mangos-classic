/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
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

/* ScriptData
SDName: Boss_Overlord_Wyrmthalak
SD%Complete: 100
SDComment:
SDCategory: Blackrock Spire
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include <array>

enum
{
    SPELL_LIST_WYRMTHALAK      = 956897,

    NPC_SPIRESTONE_WARLORD     = 9216,
    NPC_SMOLDERTHORN_BERSERKER = 9268,
    NPC_BLOODAXE_VETERAN       = 9583
};

const uint32 uSummons[3] = {NPC_BLOODAXE_VETERAN, NPC_SMOLDERTHORN_BERSERKER, NPC_SPIRESTONE_WARLORD};

const float afLocations[4][4] =
{
    { -51.6805f, -439.831f, 78.2874f, 4.657f},              // spawn points
    { -54.4554f, -439.679f, 78.2884f, 4.657f},
    { -39.355381f, -513.456482f, 88.472046f, 4.679872f},    // destination
    { -49.875881f, -511.896942f, 88.195160f, 4.613114f}
};

struct boss_overlordwyrmthalakAI : public ScriptedAI
{
    boss_overlordwyrmthalakAI(Creature* pCreature) : ScriptedAI(pCreature) {Reset();}

    struct GuardRequest
    {
        uint32 entry = 0;
        ObjectGuid guid;
        uint8 attempts = 0;
        bool created = false;
    };

    std::array<GuardRequest, 2> m_guards{};
    uint32 m_guardRetryTimer = 0;
    bool m_guardsRequested = false;

    void Reset() override
    {
        // Detach before despawn callbacks can alter this round's requests.
        auto guards = m_guards;
        m_guards = {};
        m_guardRetryTimer = 0;
        m_guardsRequested = false;
        for (auto const& guard : guards)
            if (Creature* summoned = m_creature->GetMap()->GetAnyTypeCreature(guard.guid))
                if (summoned->IsAlive() && summoned->GetEntry() == guard.entry &&
                    (summoned->GetSpawnerGuid() == m_creature->GetObjectGuid() || summoned->GetOwnerGuid() == m_creature->GetObjectGuid()))
                    summoned->ForcedDespawn();
        ScriptedAI::Reset();
        m_creature->SetSpellList(SPELL_LIST_WYRMTHALAK);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() != NPC_SPIRESTONE_WARLORD && pSummoned->GetEntry() != NPC_SMOLDERTHORN_BERSERKER && pSummoned->GetEntry() != NPC_BLOODAXE_VETERAN)
            return;

        if (pSummoned->GetSpawnerGuid() != m_creature->GetObjectGuid() && pSummoned->GetOwnerGuid() != m_creature->GetObjectGuid())
            return;

        if (m_creature->GetVictim())
        {
            Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0);
            pSummoned->AI()->AttackStart(pTarget ? pTarget : m_creature->GetVictim());
        }
    }

    void SummonedCreatureJustDied(Creature* summoned) override { ForgetGuard(summoned); }
    void SummonedCreatureDespawn(Creature* summoned) override { ForgetGuard(summoned); }

    void ForgetGuard(Creature* summoned)
    {
        for (auto& guard : m_guards)
            if (guard.guid == summoned->GetObjectGuid()) guard.guid.Clear();
        // A created guard remains fulfilled, including after death or expiry.
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // The inherited implementation advances the native list scheduler and
        // its 1.2s retry timer, selects the target and performs normal melee.
        ScriptedAI::UpdateAI(uiDiff);
        if (!m_creature->IsInCombat() || !m_creature->GetVictim() || !CanExecuteCombatAction())
            return;

        if (!m_guardsRequested && m_creature->GetHealthPercent() < 51.0f)
        {
            m_guardsRequested = true;
            for (auto& guard : m_guards) guard.entry = uSummons[urand(0, 2)];
        }
        if (!m_guardsRequested) return;
        if (m_guardRetryTimer > uiDiff) { m_guardRetryTimer -= uiDiff; return; }
        m_guardRetryTimer = 0;

        for (uint32 i = 0; i < m_guards.size(); ++i)
        {
            auto& guard = m_guards[i];
            if (guard.created || guard.attempts >= 5) continue;
            ++guard.attempts;
            if (Creature* summoned = m_creature->SummonCreature(guard.entry, afLocations[i][0], afLocations[i][1], afLocations[i][2], afLocations[i][3], TEMPSPAWN_TIMED_DESPAWN, 300000))
            {
                guard.guid = summoned->GetObjectGuid();
                guard.created = true;
                summoned->SetWalk(false);
                summoned->GetMotionMaster()->MovePoint(0, afLocations[i + 2][0], afLocations[i + 2][1], afLocations[i + 2][2]);
            }
            else
                m_guardRetryTimer = 1000;
        }
    }
};

UnitAI* GetAI_boss_overlordwyrmthalak(Creature* pCreature)
{
    return new boss_overlordwyrmthalakAI(pCreature);
}

void AddSC_boss_overlordwyrmthalak()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_overlord_wyrmthalak";
    pNewScript->GetAI = &GetAI_boss_overlordwyrmthalak;
    pNewScript->RegisterSelf();
}
