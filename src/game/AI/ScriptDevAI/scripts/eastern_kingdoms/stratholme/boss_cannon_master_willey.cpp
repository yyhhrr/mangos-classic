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
SDName: boss_cannon_master_willey
SD%Complete: 100
SDComment:
SDCategory: Stratholme
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "Entities/Pet.h"

enum
{
    SPELL_KNOCK_AWAY        = 10101,
    SPELL_PUMMEL            = 15615,
    SPELL_SHOOT             = 16496,
    SPELL_SUMMON_RIFLEMAN   = 17279,        // spell needs script target
    NPC_CRIMSON_RIFLEMAN    = 11054,
    MAX_RIFLEMEN_PER_WAVE   = 3,
    MAX_SUMMON_ATTEMPTS     = 5,
};

struct boss_cannon_master_willeyAI : public ScriptedAI
{
    boss_cannon_master_willeyAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiKnockAwayTimer;
    uint32 m_uiPummelTimer;
    uint32 m_uiShootTimer;
    uint32 m_uiSummonRiflemanTimer;
    uint32 m_uiSummonRetryTimer;
    uint8 m_uiWaveCreated;
    uint8 m_uiSummonAttempts;
    bool m_bSummonPending;
    bool m_bCountingWave;
    bool m_bEvading = false;
    uint32 m_uiSummonGeneration = 0;
    GuidSet m_sRiflemanGuids;

    void Reset() override
    {
        ++m_uiSummonGeneration;
        m_uiShootTimer          = 1000;
        m_uiPummelTimer         = 7000;
        m_uiKnockAwayTimer      = 11000;
        m_uiSummonRiflemanTimer = 15000;
        m_uiSummonRetryTimer    = 0;
        m_uiWaveCreated         = 0;
        m_uiSummonAttempts      = 0;
        m_bSummonPending        = false;
        m_bCountingWave         = false;
        m_sRiflemanGuids.clear();
    }

    void EnterEvadeMode() override
    {
        m_bEvading = true;
        m_bSummonPending = false;
        m_bCountingWave = false;
        GuidSet summons;
        summons.swap(m_sRiflemanGuids);
        for (ObjectGuid const& guid : summons)
            //17279 creates Pet(GUARDIAN_PET), whose GUID is not HIGHGUID_UNIT.
            if (Creature* summon = m_creature->GetMap()->GetAnyTypeCreature(guid))
                if (summon->IsAlive() && summon->IsPet() && summon->GetOwnerGuid() == m_creature->GetObjectGuid() &&
                    summon->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_SUMMON_RIFLEMAN)
                    static_cast<Pet*>(summon)->Unsummon(PET_SAVE_AS_DELETED, m_creature);
        ScriptedAI::EnterEvadeMode();
    }

    void Aggro(Unit* /*who*/) override
    {
        m_bEvading = false;
    }

    void JustDied(Unit* /*killer*/) override
    {
        // Keep the original surviving guardian lifetime after a boss kill.
        m_bSummonPending = false;
        m_bCountingWave = false;
        ++m_uiSummonGeneration;
    }

    void TrySummonRiflemen()
    {
        if (!CanExecuteCombatAction())
            return;

        // CastCustomSpell takes the EFFECTIVE base point value. For17279 the
        // native effect converts that exact value into the guardian count.
        // Reusing its original GO target and duration avoids a manual spawn
        // clone, while a partial success never repeats a full three-unit wave.
        int32 count = MAX_RIFLEMEN_PER_WAVE - m_uiWaveCreated;
        uint32 generation = m_uiSummonGeneration;
        m_bCountingWave = true;
        SpellCastResult result = m_creature->CastCustomSpell(m_creature, SPELL_SUMMON_RIFLEMAN, &count, nullptr, nullptr, TRIGGERED_NORMAL_COMBAT_CAST);
        if (generation != m_uiSummonGeneration)
            return;
        m_bCountingWave = false;
        if (result == SPELL_CAST_OK)
            ++m_uiSummonAttempts;

        //17279 is instant: successful creations report JustSummoned before
        // CastCustomSpell returns. A dead successful slot stays committed.
        if (m_uiWaveCreated == MAX_RIFLEMEN_PER_WAVE || m_uiSummonAttempts >= MAX_SUMMON_ATTEMPTS)
        {
            m_bSummonPending = false;
            m_uiSummonRiflemanTimer = 30000;
        }
        m_uiSummonRetryTimer = 1000;
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (pSummoned->GetEntry() == NPC_CRIMSON_RIFLEMAN && pSummoned->IsPet() &&
            pSummoned->GetOwnerGuid() == m_creature->GetObjectGuid() && pSummoned->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_SUMMON_RIFLEMAN)
        {
            // A reentrant evade can occur during the native multi-summon
            // callback loop. Reject only its later living owned creations.
            if (m_bEvading)
            {
                if (pSummoned->IsAlive())
                    static_cast<Pet*>(pSummoned)->Unsummon(PET_SAVE_AS_DELETED, m_creature);
                return;
            }
            bool inserted = m_sRiflemanGuids.insert(pSummoned->GetObjectGuid()).second;
            if (inserted && m_bCountingWave && m_uiWaveCreated < MAX_RIFLEMEN_PER_WAVE)
                ++m_uiWaveCreated;
        }
        if (m_creature->GetVictim())
            pSummoned->AI()->AttackStart(m_creature->GetVictim());
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Pummel
        if (m_uiPummelTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_PUMMEL) == CAST_OK)
                m_uiPummelTimer = 12000;
        }
        else
            m_uiPummelTimer -= uiDiff;

        // KnockAway
        if (m_uiKnockAwayTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_KNOCK_AWAY) == CAST_OK)
                m_uiKnockAwayTimer = 14000;
        }
        else
            m_uiKnockAwayTimer -= uiDiff;

        // Shoot
        if (m_uiShootTimer < uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_SHOOT, SELECT_FLAG_NOT_IN_MELEE_RANGE))
            {
                if (DoCastSpellIfCan(pTarget, SPELL_SHOOT) == CAST_OK)
                    m_uiShootTimer = urand(3000, 4000);
            }
        }
        else
            m_uiShootTimer -= uiDiff;

        // SummonRifleman
        if (m_bSummonPending)
        {
            if (m_uiSummonRetryTimer <= uiDiff)
                TrySummonRiflemen();
            else
                m_uiSummonRetryTimer -= uiDiff;
        }
        else if (m_uiSummonRiflemanTimer < uiDiff)
        {
            m_uiWaveCreated = 0;
            m_uiSummonAttempts = 0;
            m_bSummonPending = true;
            TrySummonRiflemen();
        }
        else
            m_uiSummonRiflemanTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_cannon_master_willey(Creature* pCreature)
{
    return new boss_cannon_master_willeyAI(pCreature);
}

void AddSC_boss_cannon_master_willey()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_cannon_master_willey";
    pNewScript->GetAI = &GetAI_boss_cannon_master_willey;
    pNewScript->RegisterSelf();
}
