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
SDName: Boss_Garr
SD%Complete: 95
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "molten_core.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Maps/SpawnGroup.h"
#include "Spells/Scripts/SpellScript.h"
#include "Spells/SpellAuras.h"

enum
{
    EMOTE_MASSIVE_ERUPTION      = -1409025,

    // Garr spells
    SPELL_ANTIMAGICPULSE        = 19492,
    SPELL_MAGMASHACKLES         = 19496,
    SPELL_ENRAGE                = 19516,
    SPELL_ERUPTION_TRIGGER      = 20482,
    SPELL_SEPARATION_ANXIETY    = 23487,

    // Firesworn spells
    SPELL_ERUPTION              = 19497,
    SPELL_ENRAGE_TRIGGER        = 19515,
    SPELL_MASSIVE_ERUPTION      = 20483,
    SPELL_THRASH                = 8876,
    SPELL_IMMOLATE              = 15733,
    SPELL_SEPARATION_ENRAGE      = 23492,
};

namespace
{
bool IsGarrFiresworn(Unit const* garr, Unit const* firesworn)
{
    if (!garr || !garr->IsCreature() || garr->GetEntry() != NPC_GARR ||
        !firesworn || !firesworn->IsCreature() || firesworn->GetEntry() != NPC_FIRESWORN)
        return false;

    CreatureGroup const* group = static_cast<Creature const*>(garr)->GetCreatureGroup();
    return group && group == static_cast<Creature const*>(firesworn)->GetCreatureGroup();
}

bool IsGarrFightActive(Unit const* garr)
{
    if (!garr || !garr->IsAlive() || !garr->IsInCombat() || garr->GetCombatManager().IsEvadingHome())
        return false;

    InstanceData const* instance = garr->GetInstanceData();
    return instance && instance->GetData(TYPE_GARR) == IN_PROGRESS;
}

bool CanErupt(Unit const* garr, Unit const* firesworn)
{
    return IsGarrFiresworn(garr, firesworn) && IsGarrFightActive(garr) &&
           firesworn->IsAlive() && firesworn->IsInCombat() && !firesworn->GetCombatManager().IsInEvadeMode() &&
           !firesworn->HasAuraHolder([](SpellAuraHolder const* holder) { return holder->HasMechanic(MECHANIC_BANISH); });
}
}

enum GarrActions
{
    GARR_ANTI_MAGIC_PULSE,
    GARR_MAGMA_SHACKLES,
    GARR_MASSIVE_ERUPTION,
    GARR_ACTION_MAX,
};

struct boss_garrAI : public CombatAI
{
    boss_garrAI(Creature* creature) : CombatAI(creature, GARR_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddCombatAction(GARR_ANTI_MAGIC_PULSE, 10 * IN_MILLISECONDS, 15 * IN_MILLISECONDS);
        AddCombatAction(GARR_MAGMA_SHACKLES, 5 * IN_MILLISECONDS, 10 * IN_MILLISECONDS);
        AddCombatAction(GARR_MASSIVE_ERUPTION, uint32(6 * MINUTE * IN_MILLISECONDS));
        Reset();
    }

    ScriptedInstance* m_instance;

    void Aggro(Unit* /*who*/) override
    {
        if (m_instance)
            m_instance->SetData(TYPE_GARR, IN_PROGRESS);

        m_creature->CastSpell(static_cast<Unit*>(nullptr), SPELL_SEPARATION_ANXIETY,
            TRIGGERED_OLD_TRIGGERED | TRIGGERED_IGNORE_HIT_CALCULATION);
    }

    void JustDied(Unit* /*killer*/) override
    {
        ClearSeparationAnxiety();
        if (m_instance)
            m_instance->SetData(TYPE_GARR, DONE);
    }

    void EnterEvadeMode() override
    {
        ClearSeparationAnxiety();
        CombatAI::EnterEvadeMode();
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_GARR, FAIL);
    }

    void ClearSeparationAnxiety()
    {
        if (CreatureGroup const* group = m_creature->GetCreatureGroup())
            for (SpawnGroupDbGuids const& member : group->GetGroupEntry().DbGuids)
                if (Creature* firesworn = m_creature->GetMap()->GetCreature(member.DbGuid))
                    if (firesworn->GetEntry() == NPC_FIRESWORN)
                    {
                        firesworn->RemoveAurasByCasterSpell(SPELL_SEPARATION_ANXIETY, m_creature->GetObjectGuid());
                        firesworn->RemoveAurasByCasterSpell(SPELL_SEPARATION_ENRAGE, firesworn->GetObjectGuid());
                    }
    }

    void SpellHit(Unit* caster, const SpellEntry* spellInfo) override
    {
        if (spellInfo->Id == SPELL_ENRAGE_TRIGGER && IsGarrFightActive(m_creature) && IsGarrFiresworn(m_creature, caster) && !caster->IsAlive())
            DoCastSpellIfCan(nullptr, SPELL_ENRAGE, CAST_TRIGGERED);
    }

    void SpellHitTarget(Unit* target, const SpellEntry* spellInfo, SpellMissInfo missInfo) override
    {
        if (spellInfo->Id != SPELL_ERUPTION_TRIGGER || missInfo != SPELL_MISS_NONE || !CanErupt(m_creature, target) ||
            !target->GetSpellAuraHolder(SPELL_ERUPTION_TRIGGER, m_creature->GetObjectGuid()))
            return;

        target->CastSpell(target, SPELL_MASSIVE_ERUPTION, TRIGGERED_OLD_TRIGGERED);
        if (!target->IsAlive())
            DoScriptText(EMOTE_MASSIVE_ERUPTION, m_creature);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case GARR_ANTI_MAGIC_PULSE:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_ANTIMAGICPULSE) == CAST_OK)
                    ResetCombatAction(action, urand(15 * IN_MILLISECONDS, 20 * IN_MILLISECONDS));
                break;
            }
            case GARR_MAGMA_SHACKLES:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_MAGMASHACKLES) == CAST_OK)
                    ResetCombatAction(action, urand(10 * IN_MILLISECONDS, 15 * IN_MILLISECONDS));
                break;
            }
            case GARR_MASSIVE_ERUPTION:
            {
                DoCastSpellIfCan(m_creature, SPELL_ERUPTION_TRIGGER, CAST_TRIGGERED);
                ResetCombatAction(action, 20 * IN_MILLISECONDS);
                break;
            }
        }
    }
};

struct mob_fireswornAI : public ScriptedAI
{
    mob_fireswornAI(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override
    {
        DoCastSpellIfCan(nullptr, SPELL_THRASH, CAST_AURA_NOT_PRESENT | CAST_TRIGGERED);
        DoCastSpellIfCan(nullptr, SPELL_IMMOLATE, CAST_AURA_NOT_PRESENT | CAST_TRIGGERED);
    }

    void JustDied(Unit* killer) override
    {
        if (m_creature != killer)
            DoCastSpellIfCan(nullptr, SPELL_ERUPTION, CAST_TRIGGERED);

        if (Creature* garr = GetActiveGarr())
            DoCastSpellIfCan(garr, SPELL_ENRAGE_TRIGGER, CAST_TRIGGERED);
    }

    void Aggro(Unit* /*enemy*/) override
    {
        if (Creature* garr = GetActiveGarr())
        {
            if (m_creature->GetSpellAuraHolder(SPELL_SEPARATION_ANXIETY, garr->GetObjectGuid()))
                return;

            // An add can evade and rejoin while Garr is still fighting. Only reapply to that add.
            SpellCastArgs args;
            args.SetScriptValue(m_creature->GetObjectGuid().GetRawValue());
            garr->CastSpell(args, SPELL_SEPARATION_ANXIETY, TRIGGERED_OLD_TRIGGERED | TRIGGERED_IGNORE_HIT_CALCULATION);
        }
    }

    Creature* GetActiveGarr() const
    {
        auto* instance = static_cast<ScriptedInstance*>(m_creature->GetInstanceData());
        Creature* garr = instance ? instance->GetSingleCreatureFromStorage(NPC_GARR, true) : nullptr;
        return IsGarrFiresworn(garr, m_creature) && IsGarrFightActive(garr) ? garr : nullptr;
    }
};

struct GarrSeparationAnxiety : public SpellScript, public AuraScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*eff*/) const override
    {
        return IsGarrFiresworn(spell->GetCaster(), target) && IsGarrFightActive(spell->GetCaster()) &&
               (!spell->GetScriptValue() || spell->GetScriptValue() == target->GetObjectGuid().GetRawValue());
    }

    void OnRadiusCalculate(Spell* spell, SpellEffectIndex /*eff*/, bool targetB, float& radius) const override
    {
        // Allow a rejoining add to regain the tracker even when already beyond the normal radius.
        if (targetB && spell->GetScriptValue())
            if (Unit* target = spell->GetCaster()->GetMap()->GetUnit(ObjectGuid(spell->GetScriptValue())))
                radius = std::max(radius, std::sqrt(spell->GetCaster()->GetDistance(target, true, DIST_CALC_NONE)) + 1.0f);
    }

    void OnPeriodicDummy(Aura* aura) const override
    {
        Unit* garr = aura->GetCaster();
        Unit* target = aura->GetTarget();
        if (!IsGarrFiresworn(garr, target) || !IsGarrFightActive(garr))
        {
            target->RemoveAurasByCasterSpell(SPELL_SEPARATION_ANXIETY, aura->GetCasterGuid());
            return;
        }

        float radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(aura->GetSpellProto()->EffectRadiusIndex[aura->GetEffIndex()]));
        if (target->IsAlive() && target->IsInCombat() && !garr->IsWithinDistInMap(target, radius))
            target->CastSpell(target, SPELL_SEPARATION_ENRAGE, TRIGGERED_OLD_TRIGGERED | TRIGGERED_IGNORE_HIT_CALCULATION);
    }
};

struct GarrEruptionTrigger : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*eff*/) const override
    {
        return CanErupt(spell->GetCaster(), target);
    }
};

void AddSC_boss_garr()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_garr";
    pNewScript->GetAI = &GetNewAIInstance<boss_garrAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_firesworn";
    pNewScript->GetAI = &GetNewAIInstance<mob_fireswornAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<GarrSeparationAnxiety>("spell_garr_separation_anxiety");
    RegisterSpellScript<GarrEruptionTrigger>("spell_garr_eruption_trigger");
}
