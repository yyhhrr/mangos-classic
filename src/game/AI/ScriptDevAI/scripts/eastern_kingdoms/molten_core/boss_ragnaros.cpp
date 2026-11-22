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
SDName: Boss_Ragnaros
SD%Complete: 99
SDComment: Submerge emote is not played
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "molten_core.h"
#include "AI/ScriptDevAI/base/CombatAI.h"

enum
{
    SAY_ARRIVAL5_RAG            = -1409012,
    SAY_REINFORCEMENTS_1        = -1409013,
    SAY_REINFORCEMENTS_2        = -1409014,
    SAY_HAMMER                  = -1409015,
    SAY_WRATH                   = -1409016,
    SAY_KILL                    = 7626,
    SAY_MAGMABURST              = -1409018,

    SPELL_WRATH_OF_RAGNAROS     = 20566,
    SPELL_ELEMENTAL_FIRE        = 20563,                    // Aura, proc spell 20564 when doing melee damage
    SPELL_MAGMA_BLAST           = 20565,                    // Ranged attack if nobody is in melee range
    SPELL_MELT_WEAPON           = 21387,
    SPELL_RAGNA_SUBMERGE        = 21107,                    // Stealth aura
    SPELL_RAGNA_SUBMERGE_VISUAL = 20567,                    // Visual for submerging into lava
    SPELL_RAGNA_EMERGE          = 20568,                    // Emerge from lava
    SPELL_SUBMERGE_EFFECT       = 21859,                    // Make Ragnaros immune and passive while submerged
    SPELL_ELEMENTAL_FIRE_KILL   = 19773,
    SPELL_MIGHT_OF_RAGNAROS     = 21154,
    SPELL_INTENSE_HEAT          = 21155,
    SPELL_LAVA_BURST            = 21908,                    // Randomly trigger one of spells 21886, 21900 - 21907 which summons Go 178088
    SPELL_LAVA_BURST_DAMAGE     = 21158,
    SPELL_SUMMON_SONS_FLAME     = 21108,                    // Trigger the eight spells summoning the Son of Flame adds

    // add spells
    SPELL_DOUBLE_ATTACK         = 19818,
    SPELL_LAVA_SHIELD           = 21857,

    GO_RAGNAROS_LAVA_BURST      = 178088,
    NPC_SON_OF_FLAME            = 12143,
    NPC_FLAME_OF_RAGNAROS       = 13148,

    PHASE_EMERGING              = 0,
    PHASE_EMERGED               = 1,
    PHASE_SUBMERGING            = 2,
    PHASE_SUBMERGED             = 3
};

enum RagnarosActions
{
    RAGNAROS_WRATH_OF_RAGNAROS,
    RAGNAROS_MIGHT_OF_RAGNAROS,
    RAGNAROS_MAGMA_BLAST,
    RAGNAROS_ACTION_MAX,
    RAGNAROS_ENTER_COMBAT,
    RAGNAROS_PHASE_TRANSITION,
    RAGNAROS_LAVA_BURST,
    RAGNAROS_ERUPT,
};

struct boss_ragnarosAI : public CombatAI
{
    boss_ragnarosAI(Creature* creature) : CombatAI(creature, RAGNAROS_ACTION_MAX), m_instance(static_cast<instance_molten_core*>(creature->GetInstanceData()))
    {
        AddCustomAction(RAGNAROS_ENTER_COMBAT, true, [&]() { HandleEnterCombat(); });
        AddCustomAction(RAGNAROS_PHASE_TRANSITION, true, [&]() { HandlePhaseTransition(); });
        AddCustomAction(RAGNAROS_LAVA_BURST, true, [&]() { ScheduleLavaBurst(); });
        AddCustomAction(RAGNAROS_ERUPT, true, [&]() { Erupt(); });
        AddCombatAction(RAGNAROS_WRATH_OF_RAGNAROS, 30000u);
        AddCombatAction(RAGNAROS_MIGHT_OF_RAGNAROS, 11000u);
        AddCombatAction(RAGNAROS_MAGMA_BLAST, 2000u);
        AddOnKillText(SAY_KILL);
        SetCombatMovement(false);
    }

    instance_molten_core* m_instance;

    uint32 m_phase = PHASE_EMERGED;
    int32 m_rangeCheckState = -1;

    bool m_bHasAggroYelled = false;
    bool m_introFinished = false;
    bool m_bHasSubmergedOnce = false;

    GuidVector m_spawns;
    GuidSet m_currentSons;
    ObjectGuid m_lavaBurst;

    void Reset() override
    {
        CombatAI::Reset();
        RestoreEmergedState();
        m_rangeCheckState       = -1;

        m_bHasSubmergedOnce = false;

        m_creature->SetImmobilizedState(true);
        m_creature->SetIgnoreRangedTargets(true);

        m_creature->HandleEmote(EMOTE_STATE_STAND);

        DoCastSpellIfCan(nullptr, SPELL_MELT_WEAPON, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(nullptr, SPELL_ELEMENTAL_FIRE, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);

        ClearSummons();
    }

    void RestoreEmergedState()
    {
        m_phase = PHASE_EMERGED;
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->RemoveAurasDueToSpell(SPELL_RAGNA_SUBMERGE);
        m_creature->RemoveAurasDueToSpell(SPELL_SUBMERGE_EFFECT);
        m_creature->RemoveAurasDueToSpell(SPELL_RAGNA_SUBMERGE_VISUAL);
        if (m_introFinished)
        {
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            m_creature->SetImmuneToPlayer(false);
        }
        SetMeleeEnabled(true);
    }

    void ClearSummons()
    {
        DisableTimer(RAGNAROS_PHASE_TRANSITION);
        DisableTimer(RAGNAROS_LAVA_BURST);
        DisableTimer(RAGNAROS_ERUPT);
        m_currentSons.clear();
        if (GameObject* lava = m_creature->GetMap()->GetGameObject(m_lavaBurst))
            lava->ForcedDespawn();
        m_lavaBurst.Clear();
        DespawnGuids(m_spawns);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClearSummons();
        if (m_instance)
            m_instance->SetData(TYPE_RAGNAROS, DONE);
    }

    void Aggro(Unit* who) override
    {
        if (!m_introFinished || (who->GetTypeId() == TYPEID_UNIT && who->GetEntry() == NPC_MAJORDOMO))
            return;

        if (m_instance)
            m_instance->SetData(TYPE_RAGNAROS, IN_PROGRESS);
        ResetTimer(RAGNAROS_PHASE_TRANSITION, 3 * MINUTE * IN_MILLISECONDS);
        ResetTimer(RAGNAROS_LAVA_BURST, 20 * IN_MILLISECONDS);
    }

    void EnterEvadeMode() override
    {
        if (m_instance && m_introFinished)
            m_instance->SetData(TYPE_RAGNAROS, FAIL);

        // Reset flag if Ragnaros had been submerged
        if (m_phase != PHASE_EMERGED)
            DoCastSpellIfCan(m_creature, SPELL_RAGNA_EMERGE);
        ScriptedAI::EnterEvadeMode();
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        if (m_currentSons.erase(summoned->GetObjectGuid()) && m_currentSons.empty() && m_phase == PHASE_SUBMERGED)
            ReduceTimer(RAGNAROS_PHASE_TRANSITION, 1000);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        SummonedCreatureJustDied(summoned);
        m_spawns.erase(std::remove(m_spawns.begin(), m_spawns.end(), summoned->GetObjectGuid()), m_spawns.end());
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_SON_OF_FLAME)
        {
            if (m_phase == PHASE_SUBMERGING)
                m_currentSons.insert(summoned->GetObjectGuid());
            summoned->AI()->DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
            summoned->AI()->DoCastSpellIfCan(nullptr, SPELL_LAVA_SHIELD, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
            summoned->SetInCombatWithZone();
            summoned->AI()->AttackClosestEnemy();
        }
        else if (summoned->GetEntry() == NPC_FLAME_OF_RAGNAROS)
            summoned->CastSpell(nullptr, SPELL_INTENSE_HEAT, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, m_creature->GetObjectGuid());

        m_spawns.push_back(summoned->GetObjectGuid());
    }

    void JustSummoned(GameObject* summoned) override
    {
        if (summoned->GetEntry() != GO_RAGNAROS_LAVA_BURST)
            return;
        m_lavaBurst = summoned->GetObjectGuid();
        // Prevent the contact trap from firing independently of the scheduled eruption.
        summoned->SetGoState(GO_STATE_ACTIVE);
        ResetTimer(RAGNAROS_ERUPT, 500);
    }

    void ScheduleLavaBurst()
    {
        if (!m_creature->IsInCombat() || !m_introFinished)
            return;
        m_creature->CastSpell(m_creature, SPELL_LAVA_BURST, TRIGGERED_OLD_TRIGGERED);
        ResetTimer(RAGNAROS_LAVA_BURST, urand(5000, 25000));
    }

    void Erupt()
    {
        if (!m_creature->IsInCombat())
            return;
        if (GameObject* lava = m_creature->GetMap()->GetGameObject(m_lavaBurst))
        {
            lava->SendGameObjectCustomAnim(m_lavaBurst);
            lava->CastSpell(nullptr, nullptr, SPELL_LAVA_BURST_DAMAGE, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, m_lavaBurst);
        }
    }

    void SpellHitTarget(Unit* target, const SpellEntry* spellInfo, SpellMissInfo miss) override
    {
        if (miss != SPELL_MISS_NONE)
            return;
        if (spellInfo->Id == SPELL_WRATH_OF_RAGNAROS)
            m_creature->getThreatManager().modifyThreatPercent(target, -100);
        // As Majordomo is now killed, the last timer (until attacking) must be handled with ragnaros script
        else if (!m_introFinished && spellInfo->Id == SPELL_ELEMENTAL_FIRE_KILL &&
                 target->GetTypeId() == TYPEID_UNIT && target->GetEntry() == NPC_MAJORDOMO && !target->IsAlive())
            ResetTimer(RAGNAROS_ENTER_COMBAT, 10000);
    }

    void ReceiveAIEvent(AIEventType event, Unit* /*sender*/, Unit* /*invoker*/, uint32 /*misc*/) override
    {
        if (event == AI_EVENT_CUSTOM_A)
        {
            m_introFinished = true;
            m_bHasAggroYelled = true;
            RestoreEmergedState();
        }
    }

    void HandleEnterCombat()
    {
        // Introduction
        if (!m_bHasAggroYelled)
        {
            ResetTimer(RAGNAROS_ENTER_COMBAT, 3000);
            m_bHasAggroYelled = true;
            DoScriptText(SAY_ARRIVAL5_RAG, m_creature);
        }
        else
        {
            // If we don't remove this passive flag, he will be unattackable after evading, this way he will enter combat
            m_introFinished = true;
            RestoreEmergedState();
            if (m_instance)
            {
                m_instance->CompleteRagnarosIntro();
                m_creature->SetInCombatWithZone();
                AttackClosestEnemy();
            }
        }

    }

    void HandlePhaseTransition()
    {
        if (!m_creature->IsInCombat() || !m_introFinished)
            return;
        switch (m_phase)
        {
            case PHASE_EMERGED:
            {
                // Submerge and attack again after 90 secs
                DoCastSpellIfCan(m_creature, SPELL_RAGNA_SUBMERGE_VISUAL, CAST_TRIGGERED);
                DoCastSpellIfCan(nullptr, SPELL_SUBMERGE_EFFECT, CAST_TRIGGERED);
                m_creature->SetStandState(UNIT_STAND_STATE_CUSTOM);
                m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);

                // Say dependend if first time or not
                DoScriptText(!m_bHasSubmergedOnce ? SAY_REINFORCEMENTS_1 : SAY_REINFORCEMENTS_2, m_creature);
                m_bHasSubmergedOnce = true;

                m_phase = PHASE_SUBMERGING;
                m_currentSons.clear();
                DoCastSpellIfCan(nullptr, SPELL_SUMMON_SONS_FLAME, CAST_TRIGGERED);
                ResetTimer(RAGNAROS_PHASE_TRANSITION, 3000);
                DisableCombatAction(RAGNAROS_WRATH_OF_RAGNAROS);
                DisableCombatAction(RAGNAROS_MIGHT_OF_RAGNAROS);
                DisableCombatAction(RAGNAROS_MAGMA_BLAST);
                SetMeleeEnabled(false);
                break;
            }
            case PHASE_SUBMERGING:
            {
                DoCastSpellIfCan(nullptr, SPELL_RAGNA_SUBMERGE, CAST_TRIGGERED);
                // Account for the three-second transition already spent below the surface.
                ResetTimer(RAGNAROS_PHASE_TRANSITION, m_currentSons.empty() ? 500 : 87 * IN_MILLISECONDS);
                m_phase = PHASE_SUBMERGED;
                break;
            }
            case PHASE_SUBMERGED:
            {
                // Become emerged again
                ResetTimer(RAGNAROS_PHASE_TRANSITION, 500);
                m_phase = PHASE_EMERGING;
                break;
            }
            case PHASE_EMERGING:
            {
                RestoreEmergedState();
                DoCastSpellIfCan(m_creature, SPELL_RAGNA_EMERGE, CAST_TRIGGERED);
                ResetTimer(RAGNAROS_PHASE_TRANSITION, 3 * MINUTE * IN_MILLISECONDS);
                ResetCombatAction(RAGNAROS_WRATH_OF_RAGNAROS, 20000);
                ResetCombatAction(RAGNAROS_MIGHT_OF_RAGNAROS, urand(11000, 30000));
                ResetCombatAction(RAGNAROS_MAGMA_BLAST, 3000);
                m_rangeCheckState = -1;
                break;
            }
        }
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case RAGNAROS_WRATH_OF_RAGNAROS:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_WRATH_OF_RAGNAROS) == CAST_OK)
                {
                    DoScriptText(SAY_WRATH, m_creature);
                    ResetCombatAction(action, 25000);
                }
                break;
            }
            case RAGNAROS_MIGHT_OF_RAGNAROS:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_MIGHT_OF_RAGNAROS,
                                                                   SELECT_FLAG_PLAYER | SELECT_FLAG_POWER_MANA))
                {
                    if (DoCastSpellIfCan(target, SPELL_MIGHT_OF_RAGNAROS) == CAST_OK)
                    {
                        DoScriptText(SAY_HAMMER, m_creature);
                        ResetCombatAction(action, urand(11000, 30000));
                    }
                }
                break;
            }
            case RAGNAROS_MAGMA_BLAST:
            {
                uint32 timer = 500;
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 0, nullptr,
                    SELECT_FLAG_PLAYER | SELECT_FLAG_IN_MELEE_RANGE | SELECT_FLAG_IN_LOS))
                {
                    m_rangeCheckState = -1;
                    Unit* victim = m_creature->GetVictim();
                    if (!victim || !victim->IsPlayer() || !m_creature->CanReachWithMeleeAttack(victim) || !m_creature->IsWithinLOSInMap(victim))
                        AttackStart(target);
                }
                else
                {
                    ++m_rangeCheckState;
                    if (m_rangeCheckState > 1)
                    {
                        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_MAGMA_BLAST, SELECT_FLAG_PLAYER))
                        {
                            if (DoCastSpellIfCan(target, SPELL_MAGMA_BLAST) == CAST_OK)
                            {
                                timer = 2500;
                            }
                        }
                    }
                }
                ResetCombatAction(action, timer);
                break;
            }
        }
    }
};

UnitAI* GetAI_boss_ragnaros(Creature* creature)
{
    return new boss_ragnarosAI(creature);
}

void AddSC_boss_ragnaros()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_ragnaros";
    pNewScript->GetAI = &GetAI_boss_ragnaros;
    pNewScript->RegisterSelf();
}
