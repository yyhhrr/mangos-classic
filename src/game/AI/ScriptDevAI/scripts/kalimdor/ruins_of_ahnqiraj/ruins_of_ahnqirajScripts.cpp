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
SDName: Ruins of Ahn'Qiraj
SD%Complete: 40
SDComment:
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Spells/SpellAuras.h"
#include "Spells/Scripts/SpellScript.h"

namespace
{
enum FleshHunterSpells
{
    SPELL_CONSUME = 25371,
    SPELL_CONSUME_HEAL = 25378,
    SPELL_SPIT_OUT = 25383,
};

struct FleshHunterAI : public CreatureEventAI
{
    explicit FleshHunterAI(Creature* creature) : CreatureEventAI(creature) {}

    void BeginConsume(Unit* target)
    {
        if (m_consumedTarget != target->GetObjectGuid())
            ClearConsume();

        m_consumedTarget = target->GetObjectGuid();
        SetConsumedThreat(target, STATE_SUPPRESSED);
        if (m_creature->GetVictim() == target)
            SetMeleeEnabled(false);
    }

    void FinishConsume(Unit* target, AuraRemoveMode mode)
    {
        if (m_consumedTarget != target->GetObjectGuid())
            return;

        m_consumedTarget.Clear();
        if (!m_creature->IsSuppressedTarget(target))
            SetConsumedThreat(target, STATE_NORMAL);
        SetMeleeEnabled(true);

        if (!m_creature->IsAlive() || !m_creature->IsInCombat())
            return;

        if (mode == AURA_REMOVE_BY_DEATH)
            m_creature->CastSpell(nullptr, SPELL_CONSUME_HEAL, TRIGGERED_OLD_TRIGGERED);
        else if (mode == AURA_REMOVE_BY_EXPIRE && target->IsAlive())
            m_creature->CastSpell(target, SPELL_SPIT_OUT, TRIGGERED_OLD_TRIGGERED);
    }

    void EnterEvadeMode() override
    {
        ClearConsume();
        CreatureEventAI::EnterEvadeMode();
    }

    void JustDied(Unit* killer) override
    {
        ClearConsume();
        CreatureEventAI::JustDied(killer);
    }

    void UpdateEventTimers(uint32 diff) override
    {
        if (m_consumedTarget && m_creature->GetVictim())
            SetMeleeEnabled(m_creature->GetVictim()->GetObjectGuid() != m_consumedTarget);
        CreatureEventAI::UpdateEventTimers(diff);
    }

private:
    void ClearConsume()
    {
        if (m_consumedTarget)
        {
            if (Unit* target = m_creature->GetMap()->GetUnit(m_consumedTarget))
                target->RemoveAurasByCasterSpell(SPELL_CONSUME, m_creature->GetObjectGuid());
            m_consumedTarget.Clear();
            SetMeleeEnabled(true);
        }
    }

    void SetConsumedThreat(Unit* target, HostileState state)
    {
        for (HostileReference* ref : m_creature->getThreatManager().getThreatList())
        {
            if (ref->getTarget() == target)
            {
                ref->SetHostileState(state);
                break;
            }
        }
    }

    ObjectGuid m_consumedTarget;
};

struct FleshHunterConsume : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_2)
            return;

        if (Unit* caster = aura->GetCaster())
            if (auto* ai = dynamic_cast<FleshHunterAI*>(caster->AI()))
            {
                if (apply)
                    ai->BeginConsume(aura->GetTarget());
                else
                    ai->FinishConsume(aura->GetTarget(), aura->GetRemoveMode());
            }
    }
};
}



/*######
## mob_anubisath_guardian
######*/
enum
{
    SPELL_METEOR                 = 24340,
    SPELL_PLAGUE                 = 22997,
    SPELL_SHADOW_STORM           = 26546,
    SPELL_THUNDER_CLAP           = 26554,
    SPELL_REFLECT_ARFR           = 13022,
    SPELL_REFLECT_FSSH           = 19595,
    SPELL_ENRAGE                 = 8599,
    SPELL_EXPLODE                = 25698,

    SPELL_SUMMON_ANUB_SWARMGUARD = 17430,
    SPELL_SUMMON_ANUB_WARRIOR    = 17431,

    EMOTE_FRENZY                 = -1000002,

    NPC_ANUB_WARRIOR             = 15537,
    NPC_ANUB_SWARM               = 15538
};

struct mob_anubisath_guardianAI : public ScriptedAI
{
    mob_anubisath_guardianAI(Creature* pCreature) : ScriptedAI(pCreature) {Reset();}

    uint32 m_uiSpell1;
    uint32 m_uiSpell2;
    uint32 m_uiSpell3;
    uint32 m_uiSpell4;
    uint32 m_uiSpell5;

    uint32 m_uiSpell1Timer;
    uint32 m_uiSpell2Timer;
    uint32 m_uiSpell5Timer;

    bool m_bIsEnraged;

    void Reset() override
    {
        m_creature->RemoveGuardians();
        m_uiSpell1 = urand(0, 1) ? SPELL_METEOR : SPELL_PLAGUE;
        m_uiSpell2 = urand(0, 1) ? SPELL_SHADOW_STORM : SPELL_THUNDER_CLAP;
        m_uiSpell3 = urand(0, 1) ? SPELL_REFLECT_ARFR : SPELL_REFLECT_FSSH;
        m_uiSpell4 = urand(0, 1) ? SPELL_ENRAGE : SPELL_EXPLODE;
        m_uiSpell5 = urand(0, 1) ? SPELL_SUMMON_ANUB_SWARMGUARD : SPELL_SUMMON_ANUB_WARRIOR;

        m_uiSpell1Timer = 10000;
        m_uiSpell2Timer = 20000;
        m_uiSpell5Timer = 10000;
        m_bIsEnraged    = false;
    }

    void Aggro(Unit* /*pWho*/) override
    {
        // spell reflection
        DoCastSpellIfCan(m_creature, m_uiSpell3);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        pSummoned->AI()->AttackStart(m_creature->GetVictim());
    }

    void JustDied(Unit* killer) override
    {
        ScriptedAI::JustDied(killer);
        m_creature->RemoveGuardians();
    }

    void DamageTaken(Unit* /*dealer*/, uint32& damage, DamageEffectType /*damagetype*/, SpellEntry const* /*spellInfo*/) override
    {
        uint32 const health = m_creature->GetHealth();
        if (m_bIsEnraged || damage >= health || health - damage > m_creature->GetMaxHealth() / 10)
            return;

        if (DoCastSpellIfCan(m_creature, m_uiSpell4, CAST_TRIGGERED) != CAST_OK)
            return;

        m_bIsEnraged = true;
        if (m_uiSpell4 == SPELL_ENRAGE)
            DoScriptText(EMOTE_FRENZY, m_creature);
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Meteor or Plague
        if (m_uiSpell1Timer < uiDiff)
        {
            DoCastSpellIfCan(m_creature->GetVictim(), m_uiSpell1);
            m_uiSpell1Timer = 15000;
        }
        else
            m_uiSpell1Timer -= uiDiff;

        // Shadow Storm or Thunder Clap
        if (m_uiSpell2Timer < uiDiff)
        {
            DoCastSpellIfCan(m_creature->GetVictim(), m_uiSpell2);
            m_uiSpell2Timer = 15000;
        }
        else
            m_uiSpell2Timer -= uiDiff;

        // summon Anubisath Swarmguard or Anubisath Warrior
        if (m_uiSpell5Timer < uiDiff)
        {
            // change for summon spell
            if (m_creature->CountGuardiansWithEntry(NPC_ANUB_WARRIOR) + m_creature->CountGuardiansWithEntry(NPC_ANUB_SWARM) < 4)
                DoCastSpellIfCan(m_creature->GetVictim(), m_uiSpell5);

            m_uiSpell5Timer = 15000;
        }
        else
            m_uiSpell5Timer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_mob_anubisath_guardian(Creature* pCreature)
{
    return new mob_anubisath_guardianAI(pCreature);
}

void AddSC_ruins_of_ahnqiraj()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "mob_anubisath_guardian";
    pNewScript->GetAI = &GetAI_mob_anubisath_guardian;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_aq20_flesh_hunter";
    pNewScript->GetAI = &GetNewAIInstance<FleshHunterAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<FleshHunterConsume>("spell_aq20_flesh_hunter_consume");
}
