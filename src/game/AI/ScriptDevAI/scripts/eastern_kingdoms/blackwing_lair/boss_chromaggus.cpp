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
SDName: Boss_Chromaggus
SD%Complete: 100
SDComment:
SDCategory: Blackwing Lair
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackwing_lair.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Spells/Scripts/SpellScript.h"

enum
{
    EMOTE_GENERIC_FRENZY_KILL   = -1000001,
    EMOTE_GENERIC_FRENZY        = -1000002,
    EMOTE_SHIMMER               = -1469003,

    SPELL_BROOD_AFFLICTION      = 23173,
    SPELL_CHROMATIC_MUTATION    = 23174,
    SPELL_MUTATION_OFFENSE      = 23175,
    SPELL_MUTATION_VITALITY     = 23177,

    SPELL_ELEMENTAL_SHIELD_BWL  = 22276,
    SPELL_FRENZY                = 23128,
    SPELL_ENRAGE                = 23537
};

constexpr uint32 BROOD_AFFLICTIONS[] = {23153, 23154, 23155, 23170, 23169}; // Blue, Black, Red, Bronze, Green
constexpr uint32 BROOD_AFFLICTION_CASTS = 15;

enum ChromaggusActions
{
    CHROMAGGUS_ENRAGE,
    CHROMAGGUS_ELEMENTAL_SHIELD,
    CHROMAGGUS_BREATH_LEFT,
    CHROMAGGUS_BREATH_RIGHT,
    CHROMAGGUS_BROOD_AFFLICTION,
    CHROMAGGUS_FRENZY,
    CHROMAGGUS_ACTION_MAX,
};

struct boss_chromaggusAI : public CombatAI
{
    boss_chromaggusAI(Creature* creature) : CombatAI(creature, CHROMAGGUS_ACTION_MAX), m_instance(dynamic_cast<instance_blackwing_lair*>(creature->GetInstanceData()))
    {
        AddTimerlessCombatAction(CHROMAGGUS_ENRAGE, true);
        AddCombatAction(CHROMAGGUS_ELEMENTAL_SHIELD, 0u);
        AddCombatAction(CHROMAGGUS_BREATH_LEFT, 30000u);
        AddCombatAction(CHROMAGGUS_BREATH_RIGHT, 60000u);
        AddCombatAction(CHROMAGGUS_BROOD_AFFLICTION, uint32(7 * IN_MILLISECONDS));
        AddCombatAction(CHROMAGGUS_FRENZY, 15000u);
    }

    instance_blackwing_lair* m_instance;

    void Aggro(Unit* /*who*/) override
    {
        if (m_instance)
        {
            m_instance->SelectChromaggusBreaths();
            m_instance->SetData(TYPE_CHROMAGGUS, IN_PROGRESS);
        }
    }

    void EnterEvadeMode() override
    {
        m_creature->BreakCharmOutgoing();
        CombatAI::EnterEvadeMode();
    }

    void JustDied(Unit* /*killer*/) override
    {
        m_creature->BreakCharmOutgoing();
        if (m_instance)
            m_instance->SetData(TYPE_CHROMAGGUS, DONE);
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_CHROMAGGUS, FAIL);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case CHROMAGGUS_ENRAGE:
            {
                if (m_creature->GetHealthPercent() < 20.0f && DoCastSpellIfCan(nullptr, SPELL_ENRAGE) == CAST_OK)
                {
                    DoScriptText(EMOTE_GENERIC_FRENZY, m_creature);
                    SetActionReadyStatus(action, false);
                }
                break;
            }
            case CHROMAGGUS_ELEMENTAL_SHIELD:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_ELEMENTAL_SHIELD_BWL) == CAST_OK)
                {
                    DoScriptText(EMOTE_SHIMMER, m_creature);
                    ResetCombatAction(action, 20 * IN_MILLISECONDS);
                }
                break;
            }
            case CHROMAGGUS_BREATH_LEFT:
            case CHROMAGGUS_BREATH_RIGHT:
            {
                if (!m_instance)
                    break;

                uint32 const slot = action == CHROMAGGUS_BREATH_LEFT ? TYPE_CHROMA_LBREATH : TYPE_CHROMA_RBREATH;
                uint32 const breath = m_instance->GetData(slot);
                if (breath && DoCastSpellIfCan(nullptr, breath) == CAST_OK)
                    ResetCombatAction(action, 60000);
                break;
            }
            case CHROMAGGUS_BROOD_AFFLICTION:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_BROOD_AFFLICTION) == CAST_OK)
                    ResetCombatAction(action, 7 * IN_MILLISECONDS);
                break;
            }
            case CHROMAGGUS_FRENZY:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_FRENZY) == CAST_OK)
                {
                    DoScriptText(EMOTE_GENERIC_FRENZY_KILL, m_creature);
                    ResetCombatAction(action, 15 * IN_MILLISECONDS);
                }
                break;
            }
        }
    }
};

struct ChromaggusBreathSelection : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effectIndex*/) const override
    {
        if (auto* caster = dynamic_cast<Creature*>(spell->GetCaster()))
            if (auto* instance = dynamic_cast<instance_blackwing_lair*>(caster->GetInstanceData()))
                instance->SelectChromaggusBreaths();
    }
};

struct ChromaggusBroodAffliction : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effectIndex*/) const override
    {
        auto* caster = dynamic_cast<Creature*>(spell->GetCaster());
        if (!caster || spell->GetScriptValue())
            return;
        spell->SetScriptValue(1); // The dummy hit starts a single wave for the entire cast.

        GuidVector guids;
        caster->FillGuidsListFromThreatList(guids);
        std::vector<Unit*> targets;
        for (ObjectGuid guid : guids)
            if (Unit* target = caster->GetMap()->GetUnit(guid); target && target->IsPlayer() && target->IsAlive() && !target->HasAura(SPELL_CHROMATIC_MUTATION))
                targets.push_back(target);

        if (targets.empty())
            return;

        std::shuffle(targets.begin(), targets.end(), *GetRandomGenerator());
        uint32 const affliction = BROOD_AFFLICTIONS[urand(0, std::size(BROOD_AFFLICTIONS) - 1)];
        for (uint32 i = 0; i < BROOD_AFFLICTION_CASTS; ++i)
        {
            Unit* target = targets[i % targets.size()];
            if (!target->IsAlive() || target->HasAura(SPELL_CHROMATIC_MUTATION))
                continue;

            caster->CastSpell(target, affliction, TRIGGERED_OLD_TRIGGERED);
            if (std::all_of(std::begin(BROOD_AFFLICTIONS), std::end(BROOD_AFFLICTIONS),
                [target](uint32 id) { return target->HasAura(id); }))
            {
                caster->CastSpell(target, SPELL_CHROMATIC_MUTATION, TRIGGERED_OLD_TRIGGERED);
                if (!target->HasCharmer(caster->GetObjectGuid()))
                    target->RemoveAurasByCasterSpell(SPELL_CHROMATIC_MUTATION, caster->GetObjectGuid());
            }
        }
    }
};

struct ChromaggusMutation : public AuraScript
{
    void OnAfterApply(Aura* aura, bool apply) const override
    {
        if (!apply || aura->GetEffIndex() != EFFECT_INDEX_1)
            return;

        Unit* target = aura->GetTarget();
        if (!target->HasCharmer(aura->GetCasterGuid()))
            return;

        for (uint32 affliction : BROOD_AFFLICTIONS)
            target->RemoveAurasDueToSpell(affliction);
        target->CastSpell(target, SPELL_MUTATION_OFFENSE, TRIGGERED_OLD_TRIGGERED);
        target->CastSpell(target, SPELL_MUTATION_VITALITY, TRIGGERED_OLD_TRIGGERED);
    }

    void OnApply(Aura* aura, bool apply) const override
    {
        if (apply || aura->GetEffIndex() != EFFECT_INDEX_1)
            return;

        Unit* target = aura->GetTarget();
        target->RemoveAurasByCasterSpell(SPELL_MUTATION_OFFENSE, target->GetObjectGuid());
        target->RemoveAurasByCasterSpell(SPELL_MUTATION_VITALITY, target->GetObjectGuid());
    }
};

struct ChromaggusTimeLapse : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_0)
            return;

        Unit* caster = aura->GetCaster();
        if (!caster || caster->GetEntry() != NPC_CHROMAGGUS)
            return;

        Unit* target = aura->GetTarget();
        ThreatManager& threat = caster->getThreatManager();
        for (HostileReference* ref = target->getHostileRefManager().getFirst(); ref; ref = ref->next())
        {
            if (ref->getSource() != &threat)
                continue;

            if (apply)
                ref->SetHostileState(STATE_SUPPRESSED);
            else if (ref->GetHostileState() == STATE_SUPPRESSED && !caster->IsSuppressedTarget(target))
            {
                ref->SetHostileState(STATE_NORMAL);
                // Re-evaluate the original threat order after the temporary handoff.
                threat.setCurrentVictim(nullptr);
            }
            break;
        }
    }
};

void AddSC_boss_chromaggus()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_chromaggus";
    pNewScript->GetAI = &GetNewAIInstance<boss_chromaggusAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ChromaggusBreathSelection>("spell_chromaggus_breath_selection");
    RegisterSpellScript<ChromaggusBroodAffliction>("spell_chromaggus_brood_affliction");
    RegisterSpellScript<ChromaggusMutation>("spell_chromaggus_mutation");
    RegisterSpellScript<ChromaggusTimeLapse>("spell_chromaggus_time_lapse");
}
