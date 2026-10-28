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
SDName: Boss_General_Angerforge
SD%Complete: 100
SDComment:
SDCategory: Blackrock Depths
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"

enum
{
    EMOTE_ALARM                 = -1230035,

    SPELL_FLURRY                = 15088,
    SPELL_ENRAGE                = 15097,
    SPELL_SUNDER_ARMOR          = 15572,

    NPC_ANVILRAGE_MEDIC         = 8894,
    NPC_ANVILRAGE_RESERVIST     = 8901,

    NPC_ELITE_AMOUNT            = 2,
    NPC_NORMAL_AMOUNT           = 8,
};

static const float aAlarmPoint[4] = {717.343f, 22.116f, -45.4321f, 3.1415f};

struct boss_general_angerforgeAI : public ScriptedAI
{
    boss_general_angerforgeAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiSunderArmorTimer;
    uint32 m_uiAlarmTimer;
    uint32 m_uiAlarmRetryTimer;
    uint8 m_uiAlarmAttempts;
    bool m_bAlarmPending;
    bool m_abAlarmSummoned[NPC_NORMAL_AMOUNT + NPC_ELITE_AMOUNT];
    GuidSet m_sAlarmGuids;

    void Reset() override
    {
        m_uiSunderArmorTimer = urand(5 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
        m_uiAlarmTimer = 0;
        m_uiAlarmRetryTimer = 0;
        m_uiAlarmAttempts = 0;
        m_bAlarmPending = false;
        m_sAlarmGuids.clear();
        for (bool& summoned : m_abAlarmSummoned)
            summoned = false;
    }

    void EnterEvadeMode() override
    {
        GuidSet summons;
        summons.swap(m_sAlarmGuids);
        for (ObjectGuid const& guid : summons)
            if (Creature* summon = m_creature->GetMap()->GetCreature(guid))
                if (summon->IsAlive())
                    summon->ForcedDespawn();
        ScriptedAI::EnterEvadeMode();
    }

    void JustDied(Unit* /*killer*/) override
    {
        // Preserve the original surviving-add behavior after a boss kill.
        m_bAlarmPending = false;
    }

    void Aggro(Unit* /*pWho*/) override
    {
        DoCastSpellIfCan(m_creature, SPELL_FLURRY, CAST_AURA_NOT_PRESENT | CAST_TRIGGERED);
        DoCastSpellIfCan(m_creature, SPELL_ENRAGE, CAST_AURA_NOT_PRESENT | CAST_TRIGGERED);
    }

    Creature* SummonAdd(uint32 uiEntry)
    {
        float fX, fY, fZ;
        m_creature->GetRandomPoint(aAlarmPoint[0], aAlarmPoint[1], aAlarmPoint[2], 1.0f, fX, fY, fZ);
        return m_creature->SummonCreature(uiEntry, fX, fY, fZ, aAlarmPoint[3], TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 30 * IN_MILLISECONDS);
    }

    void TrySummonAlarm()
    {
        bool complete = true;
        ++m_uiAlarmAttempts;
        for (uint8 i = 0; i < NPC_NORMAL_AMOUNT + NPC_ELITE_AMOUNT; ++i)
        {
            if (m_abAlarmSummoned[i])
                continue;
            if (Creature* summon = SummonAdd(i < NPC_NORMAL_AMOUNT ? NPC_ANVILRAGE_RESERVIST : NPC_ANVILRAGE_MEDIC))
            {
                m_abAlarmSummoned[i] = true;
                m_sAlarmGuids.insert(summon->GetObjectGuid());
            }
            else
                complete = false;
        }
        m_bAlarmPending = !complete && m_uiAlarmAttempts < 5;
        m_uiAlarmRetryTimer = 1000;
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (m_creature->GetVictim())
            pSummoned->AI()->AttackStart(m_creature->GetVictim());
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Sunder_Armor-Timer
        if (m_uiSunderArmorTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_SUNDER_ARMOR) == CAST_OK)
                m_uiSunderArmorTimer = urand(5 * IN_MILLISECONDS, 15 * IN_MILLISECONDS);
        }
        else
            m_uiSunderArmorTimer -= uiDiff;

        // Retry only slots that failed to spawn. Successful units, including
        // ones killed meanwhile, never cause replacements in this wave.
        if (m_bAlarmPending)
        {
            if (m_uiAlarmRetryTimer <= uiDiff)
                TrySummonAlarm();
            else
                m_uiAlarmRetryTimer -= uiDiff;
        }

        // Alarm-Timer: keep the original threshold and three-minute cadence,
        // measured from the first attempt rather than the last retry.
        if (m_creature->GetHealthPercent() < 30.0f)
        {
            if (m_uiAlarmTimer < uiDiff)
            {
                DoScriptText(EMOTE_ALARM, m_creature);

                for (bool& summoned : m_abAlarmSummoned)
                    summoned = false;
                m_uiAlarmAttempts = 0;
                TrySummonAlarm();

                m_uiAlarmTimer = 3 * MINUTE * IN_MILLISECONDS;
            }
            else
                m_uiAlarmTimer -= uiDiff;
        }

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_general_angerforge(Creature* pCreature)
{
    return new boss_general_angerforgeAI(pCreature);
}

void AddSC_boss_general_angerforge()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_general_angerforge";
    pNewScript->GetAI = &GetAI_boss_general_angerforge;
    pNewScript->RegisterSelf();
}
