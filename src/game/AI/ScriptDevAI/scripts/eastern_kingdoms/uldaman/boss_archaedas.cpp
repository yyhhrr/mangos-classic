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
SDName: Boss_Archaedas
SD%Complete: 100
SDComment:
SDCategory: Uldaman
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "uldaman.h"

enum
{
    SPELL_ARCHAEDAS_AWAKEN_VISUAL   = 10347,
    SPELL_GROUND_TREMOR             = 6524,

    SPELL_AWAKEN_EARTHEN_GUARDIAN   = 10252,                // awaken all 7076 npcs
    SPELL_AWAKEN_VAULT_WARDER       = 10258,                // awaken 2 npcs 10120
    SPELL_AWAKEN_EARTHEN_DWARF      = 10259,                // awaken random npc 7309 or 7077

    SAY_AGGRO                       = -1070001,
    SAY_AWAKE_GUARDIANS             = -1070002,
    SAY_AWAKE_WARDERS               = -1070003,
    SAY_UNIT_SLAIN                  = -1070004,
    EMOTE_BREAKS_FREE               = -1070005,
};

struct boss_archaedasAI : public ScriptedAI
{
    boss_archaedasAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = dynamic_cast<instance_uldaman*>(pCreature->GetInstanceData());
        Reset();
    }

    instance_uldaman* m_pInstance;

    uint32 m_uiAwakeningTimer;
    uint32 m_uiAwakeDwarfTimer;
    uint32 m_uiTremorTimer;
    uint8 m_uiSubevent;
    bool m_bDwarvesAwaken;

    uint8 m_uiHpPhaseCheck;

    void Reset() override
    {
        m_uiAwakeningTimer  = 1000;
        m_uiSubevent        = 0;
        m_uiAwakeDwarfTimer = 10000;
        m_uiTremorTimer     = urand(7000, 14000);
        m_bDwarvesAwaken    = false;
        m_uiHpPhaseCheck    = 1;

        DoCastSpellIfCan(m_creature, SPELL_FREEZE_ANIM);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
    }

    void Aggro(Unit* /*pWho*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_ARCHAEDAS, IN_PROGRESS);
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        DoScriptText(SAY_UNIT_SLAIN, m_creature);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        // open door to vault (handled by instance script)
        if (m_pInstance)
            m_pInstance->SetData(TYPE_ARCHAEDAS, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_ARCHAEDAS, FAIL);
    }

    void AbortAwakening()
    {
        m_pInstance->SetData(TYPE_ARCHAEDAS, FAIL);
        EnterEvadeMode();
        // The intro can fail before combat, when returning home is unnecessary.
        // Restore its frozen state immediately and allow the altar to retry.
        Reset();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // so many things are based in this script on instance data
        // so if we don't have access to it better do nothing
        if (!m_pInstance || !m_creature->IsAlive())
            return;

        // OOC Intro part triggered by Altar activation
        if (m_pInstance->GetData(TYPE_ARCHAEDAS) == SPECIAL)
        {
            if (m_uiAwakeningTimer <= uiDiff)
            {
                switch (m_uiSubevent)
                {
                    case 0:
                        if (DoCastSpellIfCan(m_creature, SPELL_ARCHAEDAS_AWAKEN_VISUAL) != CAST_OK)
                        {
                            m_uiAwakeningTimer = 500;
                            return;
                        }
                        m_uiAwakeningTimer = 2000;
                        break;
                    case 1:
                        DoScriptText(EMOTE_BREAKS_FREE, m_creature);
                        m_uiAwakeningTimer = 3000;
                        break;
                    case 2:
                    {
                        // The altar starter may have died or left during the intro.
                        Player* player = m_pInstance->SelectEventTarget(m_creature);
                        if (!player)
                        {
                            AbortAwakening();
                            return;
                        }

                        m_creature->RemoveAurasDueToSpell(SPELL_FREEZE_ANIM);
                        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
                        AttackStart(player);
                        if (!m_creature->GetVictim())
                        {
                            AbortAwakening();
                            return;
                        }

                        DoScriptText(SAY_AGGRO, m_creature);
                        m_pInstance->SetData(TYPE_ARCHAEDAS, IN_PROGRESS);
                        break;
                    }
                    default:
                        // An incomplete attack must not leave SPECIAL spinning forever.
                        AbortAwakening();
                        return;
                }

                ++m_uiSubevent;
            }
            else
                m_uiAwakeningTimer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Phase switch
        if (m_uiHpPhaseCheck <= 2 && m_creature->GetHealthPercent() < 100.0f - 33.4f * (float)m_uiHpPhaseCheck)
        {
            if (DoCastSpellIfCan(m_creature, m_uiHpPhaseCheck == 1 ? SPELL_AWAKEN_EARTHEN_GUARDIAN : SPELL_AWAKEN_VAULT_WARDER) == CAST_OK)
            {
                DoScriptText(m_uiHpPhaseCheck == 1 ? SAY_AWAKE_GUARDIANS : SAY_AWAKE_WARDERS, m_creature);
                ++m_uiHpPhaseCheck;
            }
        }

        // Awake random Dwarf
        if (!m_bDwarvesAwaken && m_creature->GetHealthPercent() >= 33.0f)
        {
            if (m_uiAwakeDwarfTimer < uiDiff)
            {
                if (Creature* pEarthen = m_pInstance->GetClosestDwarfNotInCombat(m_creature))
                {
                    if (DoCastSpellIfCan(pEarthen, SPELL_AWAKEN_EARTHEN_DWARF) == CAST_OK)
                        m_uiAwakeDwarfTimer = urand(9000, 12000);
                }
                else
                    m_bDwarvesAwaken = true;
            }
            else
                m_uiAwakeDwarfTimer -= uiDiff;
        }

        if (m_uiTremorTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_GROUND_TREMOR) == CAST_OK)
                m_uiTremorTimer = urand(8000, 17000);
        }
        else
            m_uiTremorTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_archaedas(Creature* pCreature)
{
    return new boss_archaedasAI(pCreature);
}

namespace
{
// Both awakening spells and aura applications must belong to the active boss.
// In particular an aura's removal during an encounter reset must never wake it.
Unit* GetArchaedasAwakeningVictim(Unit* caster, Unit* target, uint32 spellId)
{
    if (!caster || !target || !caster->IsCreature() || !target->IsCreature() ||
        !caster->IsInWorld() || !target->IsInWorld() || !caster->IsAlive() || !target->IsAlive() ||
        caster->GetMap() != target->GetMap() || !target->HasAura(SPELL_STONED) || !target->AI())
        return nullptr;

    switch (spellId)
    {
        case SPELL_AWAKEN_EARTHEN_GUARDIAN:
            if (target->GetEntry() != NPC_GUARDIAN)
                return nullptr;
            break;
        case SPELL_AWAKEN_EARTHEN_DWARF:
            if (target->GetEntry() != NPC_CUSTODIAN && target->GetEntry() != NPC_HALLSHAPER)
                return nullptr;
            break;
        case SPELL_AWAKEN_VAULT_WARDER:
            if (target->GetEntry() != NPC_VAULT_WARDER)
                return nullptr;
            break;
        default:
            return nullptr;
    }

    instance_uldaman* instance = dynamic_cast<instance_uldaman*>(target->GetInstanceData());
    if (!instance || caster->GetInstanceData() != instance || instance->GetData(TYPE_ARCHAEDAS) != IN_PROGRESS)
        return nullptr;

    Creature* archaedas = instance->GetSingleCreatureFromStorage(NPC_ARCHAEDAS);
    if (!archaedas || archaedas != caster || !archaedas->IsAlive())
        return nullptr;

    Unit* victim = archaedas->GetVictim();
    if (victim && victim->IsInWorld() && victim->IsAlive() && !victim->IsFeigningDeath() &&
        victim->GetMap() == archaedas->GetMap() && victim->GetInstanceData() == instance &&
        (!victim->IsPlayer() || !static_cast<Player*>(victim)->IsGameMaster()) &&
        archaedas->CanAttackInCombat(victim, false, false))
        return victim;

    return instance->SelectEventTarget(archaedas);
}

void AwakenArchaedasMinion(Unit* caster, Unit* target, uint32 spellId)
{
    if (Unit* victim = GetArchaedasAwakeningVictim(caster, target, spellId))
    {
        target->RemoveAurasDueToSpell(SPELL_STONED);
        target->AI()->AttackStart(victim);
        // A failed attack leaves this minion available for the next activation.
        if (!target->GetVictim())
            target->CastSpell(target, SPELL_STONED, TRIGGERED_OLD_TRIGGERED);
    }
}
}

// 10258 - Awaken Vault Warder (the database keeps its two-target limit)
struct AwakenVaultWarder : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_0)
            AwakenArchaedasMinion(spell->GetCaster(), spell->GetUnitTarget(), SPELL_AWAKEN_VAULT_WARDER);
    }
};

// 10252 - Awaken Earthen Guardians
// 10259 - Awaken Earthen Dwarf
struct AwakenEarthenArchaedas : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (!apply || aura->GetEffIndex() != EFFECT_INDEX_0)
            return;

        AwakenArchaedasMinion(aura->GetCaster(), aura->GetTarget(), aura->GetId());
    }
};

void AddSC_boss_archaedas()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_archaedas";
    pNewScript->GetAI = &GetAI_boss_archaedas;
    pNewScript->RegisterSelf();

    RegisterSpellScript<AwakenVaultWarder>("spell_awaken_vault_warder");
    RegisterSpellScript<AwakenEarthenArchaedas>("spell_awaken_earthen_archaedas");
}
