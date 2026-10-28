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
SDName: Boss_Gyth
SD%Complete: 100
SDComment: Timers may need adjustments
SDCategory: Blackrock Spire
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_spire.h"

enum
{
    SAY_NEFARIUS_BUFF_GYTH  = -1229017,
    EMOTE_KNOCKED_OFF       = -1229019,

    SPELL_CHROMATIC_CHAOS   = 16337,                // casted by Nefarius at 50%
    SPELL_REND_MOUNTS       = 16167,
    SPELL_SUMMON_REND       = 16328,
    SPELL_CORROSIVE_ACID    = 16359,
    SPELL_FREEZE            = 16350,
    SPELL_FLAME_BREATH      = 16390,
    SPELL_KNOCK_AWAY        = 10101,

    STAGE_RETRY_DELAY      = 1000,
    MAX_STAGE_ATTEMPTS     = 5,
};

struct boss_gythAI : public ScriptedAI
{
    boss_gythAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = (instance_blackrock_spire*) pCreature->GetInstanceData();
        Reset();
    }

    instance_blackrock_spire* m_pInstance;

    uint32 uiCorrosiveAcidTimer;
    uint32 uiFreezeTimer;
    uint32 uiFlamebreathTimer;
    uint32 uiKnockAwayTimer;

    bool m_bSummonedRend;
    bool m_bHasChromaticChaos;
    bool m_bRendCastPending;
    bool m_bEncounterFailed;
    uint8 m_uiMountAttempts;
    uint8 m_uiChaosAttempts;
    uint8 m_uiRendAttempts;
    uint32 m_uiMountRetryTimer;
    uint32 m_uiChaosRetryTimer;
    uint32 m_uiRendRetryTimer;

    void Reset() override
    {
        uiCorrosiveAcidTimer = 8000;
        uiFreezeTimer        = 11000;
        uiFlamebreathTimer   = 4000;
        uiKnockAwayTimer     = 23000;
        m_bSummonedRend      = false;
        m_bHasChromaticChaos = false;
        m_bRendCastPending   = false;
        m_bEncounterFailed  = false;
        m_uiMountAttempts   = 0;
        m_uiChaosAttempts   = 0;
        m_uiRendAttempts    = 0;
        m_uiMountRetryTimer = 0;
        m_uiChaosRetryTimer = 0;
        m_uiRendRetryTimer  = 0;
        SetDeathPrevention(true);
    }

    void FailEncounter()
    {
        m_bEncounterFailed = true;
        m_bRendCastPending = false;
        // Never leave a failed summon with an immortal Gyth.
        SetDeathPrevention(false);
        if (m_pInstance && m_pInstance->IsStadiumGyth(m_creature))
            m_pInstance->FailStadium();
    }

    bool RestoreMount()
    {
        if (m_creature->HasAura(SPELL_REND_MOUNTS))
        {
            m_uiMountAttempts = 0;
            return true;
        }

        ++m_uiMountAttempts;
        DoCastSpellIfCan(m_creature, SPELL_REND_MOUNTS, CAST_AURA_NOT_PRESENT);
        if (m_creature->HasAura(SPELL_REND_MOUNTS))
        {
            m_uiMountAttempts = 0;
            return true;
        }

        m_uiMountRetryTimer = STAGE_RETRY_DELAY;
        if (m_uiMountAttempts >= MAX_STAGE_ATTEMPTS)
            FailEncounter();
        return false;
    }

    void JustSummoned(Creature* pSummoned) override
    {
        // The instant spell's CAST_OK is not proof that its wild summon was
        // allocated. Only this cast window and this stadium's owned Rend count.
        if (!m_bRendCastPending || m_bSummonedRend || m_bEncounterFailed || !pSummoned ||
            !pSummoned->IsAlive() || pSummoned->GetEntry() != NPC_REND_BLACKHAND ||
            (pSummoned->GetSpawnerGuid() != m_creature->GetObjectGuid() &&
             pSummoned->GetOwnerGuid() != m_creature->GetObjectGuid()) ||
            !m_pInstance || !m_pInstance->ConfirmStadiumRend(m_creature, pSummoned))
            return;

        m_bSummonedRend = true;
        SetDeathPrevention(false);
        m_creature->RemoveAurasDueToSpell(SPELL_REND_MOUNTS);
        DoScriptText(EMOTE_KNOCKED_OFF, pSummoned);
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_bEncounterFailed || !m_creature->IsAlive())
            return;

        // Creation/Reset can run inside the instance's actor allocation. Wait
        // until the next world update, when the current slot has been committed.
        if (!m_pInstance || !m_pInstance->IsStadiumGyth(m_creature))
        {
            FailEncounter();
            return;
        }

        // The persisted actor ledger may already contain a living or killed
        // combat Rend. Restoring Gyth must not create a second reward actor.
        if (!m_bSummonedRend && m_pInstance->WasStadiumRendReleased(m_creature))
        {
            m_bSummonedRend = true;
            SetDeathPrevention(false);
            m_creature->RemoveAurasDueToSpell(SPELL_REND_MOUNTS);
        }

        if (!m_bSummonedRend && !m_creature->HasAura(SPELL_REND_MOUNTS))
        {
            if (m_uiMountRetryTimer > uiDiff)
                m_uiMountRetryTimer -= uiDiff;
            else if (!RestoreMount())
                return;
        }

        // Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Chromatic Chaos at 50%
        if (!m_bHasChromaticChaos && m_creature->GetHealthPercent() < 50.0f)
        {
            if (m_uiChaosRetryTimer > uiDiff)
                m_uiChaosRetryTimer -= uiDiff;
            else
            {
                ++m_uiChaosAttempts;
                if (Creature* pNefarius = m_pInstance ? m_pInstance->GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS) : nullptr)
                {
                    if (pNefarius->IsAlive())
                    {
                        pNefarius->CastSpell(m_creature, SPELL_CHROMATIC_CHAOS, TRIGGERED_OLD_TRIGGERED);
                        if (m_creature->GetSpellAuraHolder(SPELL_CHROMATIC_CHAOS, pNefarius->GetObjectGuid()))
                        {
                            m_bHasChromaticChaos = true;
                            DoScriptText(SAY_NEFARIUS_BUFF_GYTH, pNefarius);
                        }
                    }
                }
                m_uiChaosRetryTimer = STAGE_RETRY_DELAY;
                if (!m_bHasChromaticChaos && m_uiChaosAttempts >= MAX_STAGE_ATTEMPTS)
                {
                    FailEncounter();
                    return;
                }
            }
        }

        // CorrosiveAcid_Timer
        if (uiCorrosiveAcidTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_CORROSIVE_ACID) == CAST_OK)
                uiCorrosiveAcidTimer = 7000;
        }
        else
            uiCorrosiveAcidTimer -= uiDiff;

        // Freeze_Timer
        if (uiFreezeTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_FREEZE) == CAST_OK)
                uiFreezeTimer = 16000;
        }
        else
            uiFreezeTimer -= uiDiff;

        // Flamebreath_Timer
        if (uiFlamebreathTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_FLAME_BREATH) == CAST_OK)
                uiFlamebreathTimer = 10500;
        }
        else
            uiFlamebreathTimer -= uiDiff;

        if (uiKnockAwayTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_KNOCK_AWAY) == CAST_OK)
                uiKnockAwayTimer = 23000;
        }
        else
            uiKnockAwayTimer -= uiDiff;

        // Summon Rend
        if (!m_bSummonedRend && m_creature->GetHealthPercent() < 11.0f)
        {
            if (!m_pInstance || !m_pInstance->IsStadiumGyth(m_creature))
            {
                FailEncounter();
                return;
            }

            if (m_uiRendRetryTimer > uiDiff)
                m_uiRendRetryTimer -= uiDiff;
            else
            {
                ++m_uiRendAttempts;
                m_bRendCastPending = true;
                DoCastSpellIfCan(m_creature, SPELL_SUMMON_REND);
                m_bRendCastPending = false;
                m_uiRendRetryTimer = STAGE_RETRY_DELAY;
                if (!m_bSummonedRend && m_uiRendAttempts >= MAX_STAGE_ATTEMPTS)
                {
                    FailEncounter();
                    return;
                }
            }
        }

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_gyth(Creature* pCreature)
{
    return new boss_gythAI(pCreature);
}

void AddSC_boss_gyth()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_gyth";
    pNewScript->GetAI = &GetAI_boss_gyth;
    pNewScript->RegisterSelf();
}
