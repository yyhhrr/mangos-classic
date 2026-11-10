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
SDName: Boss_Renataki
SD%Complete: 100
SDComment:
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Spells/SpellMgr.h"

#include <algorithm>

enum
{
    NPC_RENATAKI            = 15084,
    SPELL_THOUSAND_BLADES   = 24649,
    SPELL_VANISH            = 24699,
    SPELL_VANISH_TELEPORT   = 24700,
    SPELL_AMBUSH            = 24337,
    SPELL_ENRAGE            = 8269,

    SPELL_SPIRIT_PARTICLES      = 18951,
    SPELL_SPAWN_RED_LIGHTNING   = 24240,
};

constexpr uint32 VanishDelay = 2000;
constexpr float VanishTargetRange = 100.0f;
constexpr uint32 MaxAdditionalBladesTargets = 9;

enum RenatakiActions
{
    RENATAKI_ENRAGE,
    RENATAKI_ACTION_MAX,
    RENATAKI_VANISH_DELAY = RENATAKI_ACTION_MAX,
};

struct boss_renatakiAI : public CombatAI
{
    explicit boss_renatakiAI(Creature* creature) : CombatAI(creature, RENATAKI_ACTION_MAX)
    {
        AddTimerlessCombatAction(RENATAKI_ENRAGE, true);
        AddCustomAction(RENATAKI_VANISH_DELAY, true, [this]() { TeleportForAmbush(); }, TIMER_COMBAT_COMBAT);
    }

    void Reset() override
    {
        CancelVanish();
        CombatAI::Reset();
    }

    void EnterEvadeMode() override
    {
        CancelVanish();
        CombatAI::EnterEvadeMode();
    }

    void JustDied(Unit* killer) override
    {
        CancelVanish();
        CombatAI::JustDied(killer);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_vanished && (!m_creature->IsInCombat() || m_creature->getThreatManager().isThreatListEmpty() ||
            (m_vanishTarget && !GetVanishTarget())))
            CancelVanish();

        CombatAI::UpdateAI(diff);
    }

    void JustRespawned() override
    {
        CombatAI::JustRespawned();

        DoCastSpellIfCan(nullptr, SPELL_SPIRIT_PARTICLES);
        DoCastSpellIfCan(nullptr, SPELL_SPAWN_RED_LIGHTNING);
    }

    void ExecuteAction(uint32 action) override
    {
        if (action == RENATAKI_ENRAGE && m_creature->GetHealthPercent() < 30.0f)
            DoCastSpellIfCan(nullptr, SPELL_ENRAGE, CAST_AURA_NOT_PRESENT);
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType type, SpellEntry const* spellInfo) override
    {
        CombatAI::DamageTaken(dealer, damage, type, spellInfo);
        if (m_vanished && damage && spellInfo && (IsAreaOfEffectSpell(spellInfo) || HasPersistentAuraEffect(spellInfo)))
            m_creature->RemoveAurasDueToSpell(SPELL_VANISH);
    }

    void BeginVanish()
    {
        if (m_vanished || !m_creature->IsInCombat())
            return;

        m_vanished = true;
        SetCombatScriptStatus(true);
        SetMeleeEnabled(false);
        SetCombatMovement(false, true);
        ResetTimer(RENATAKI_VANISH_DELAY, VanishDelay);
    }

    void CancelVanish()
    {
        if (!m_vanished)
            return;

        m_vanished = false;
        ObjectGuid const targetGuid = m_vanishTarget;
        m_vanishTarget.Clear();
        DisableTimer(RENATAKI_VANISH_DELAY);

        if (targetGuid)
            m_creature->RemoveAurasByCasterSpell(SPELL_VANISH_TELEPORT, targetGuid);
        m_creature->RemoveAurasDueToSpell(SPELL_VANISH);

        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true, m_creature->IsAlive() && m_creature->IsInCombat());
    }

    void FinishVanish(ObjectGuid casterGuid, bool expired)
    {
        if (!m_vanished || !m_vanishTarget || m_vanishTarget != casterGuid)
            return;

        Unit* target = nullptr;
        if (expired && m_creature->IsAlive() && m_creature->IsInCombat() && m_creature->HasAura(SPELL_VANISH))
            target = GetVanishTarget();

        CancelVanish();
        if (target)
            m_creature->CastSpell(target, SPELL_AMBUSH, TRIGGERED_OLD_TRIGGERED | TRIGGERED_INSTANT_CAST);
    }

private:
    bool IsEligibleVanishTarget(Unit* target)
    {
        return target && target->IsPlayer() && target->IsAlive() && m_creature->IsWithinDistInMap(target, VanishTargetRange) &&
            m_creature->CanAttack(target) && m_creature->getThreatManager().HasThreat(target);
    }

    Unit* GetVanishTarget()
    {
        Unit* target = m_creature->GetMap()->GetUnit(m_vanishTarget);
        return IsEligibleVanishTarget(target) ? target : nullptr;
    }

    void TeleportForAmbush()
    {
        if (!m_vanished)
            return;

        std::vector<Unit*> targets;
        m_creature->SelectAttackingTargets(targets, ATTACKING_TARGET_ALL_SUITABLE, 0, nullptr, SELECT_FLAG_PLAYER);
        std::erase_if(targets, [this](Unit* target) { return !IsEligibleVanishTarget(target); });
        if (targets.empty())
        {
            CancelVanish();
            return;
        }

        Unit* target = targets[urand(0, targets.size() - 1)];
        m_vanishTarget = target->GetObjectGuid();
        if (target->CastSpell(m_creature, SPELL_VANISH_TELEPORT, TRIGGERED_OLD_TRIGGERED) != SPELL_CAST_OK ||
            !m_creature->GetSpellAuraHolder(SPELL_VANISH_TELEPORT, m_vanishTarget))
            CancelVanish();
    }

    bool m_vanished = false;
    ObjectGuid m_vanishTarget;
};

// 24649 - Thousand Blades
struct ThousandBladesRenataki : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (spell->m_IsTriggeredSpell) // only initial normal cast
            return;

        if (effIdx != EFFECT_INDEX_1)
            return;

        std::vector<Unit*> selectedTargets;
        Unit* caster = spell->GetCaster();
        caster->SelectAttackingTargets(selectedTargets, ATTACKING_TARGET_ALL_SUITABLE, 0, SPELL_THOUSAND_BLADES, SELECT_FLAG_PLAYER);
        std::shuffle(selectedTargets.begin(), selectedTargets.end(), *GetRandomGenerator());

        GuidSet selectedGuids;
        if (Unit* primary = spell->m_targets.getUnitTarget())
            selectedGuids.insert(primary->GetObjectGuid());

        uint32 additionalTargets = 0;
        for (Unit* target : selectedTargets)
        {
            if (!target || !target->IsPlayer() || !target->IsAlive() || !caster->CanAttack(target) ||
                !selectedGuids.insert(target->GetObjectGuid()).second)
                continue;

            caster->CastSpell(target, SPELL_THOUSAND_BLADES, TRIGGERED_OLD_TRIGGERED | TRIGGERED_INSTANT_CAST);
            if (++additionalTargets == MaxAdditionalBladesTargets)
                break;
        }
    }
};

struct RenatakiVanish : public AuraScript
{
    void OnAfterApply(Aura* aura, bool apply) const override
    {
        if (apply && aura->GetEffIndex() == EFFECT_INDEX_1)
            if (auto* ai = dynamic_cast<boss_renatakiAI*>(aura->GetTarget()->AI()))
                ai->BeginVanish();
    }

    void OnApply(Aura* aura, bool apply) const override
    {
        if (!apply && aura->GetEffIndex() == EFFECT_INDEX_1)
            if (auto* ai = dynamic_cast<boss_renatakiAI*>(aura->GetTarget()->AI()))
                ai->CancelVanish();
    }
};

struct RenatakiVanishTeleport : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (!apply && aura->GetEffIndex() == EFFECT_INDEX_1)
            if (auto* ai = dynamic_cast<boss_renatakiAI*>(aura->GetTarget()->AI()))
                ai->FinishVanish(aura->GetCasterGuid(), aura->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE);
    }
};

struct RenatakiDemoralizingShout : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_0)
            if (Unit* target = spell->GetUnitTarget(); target && target->GetEntry() == NPC_RENATAKI)
                target->RemoveAurasDueToSpell(SPELL_VANISH);
    }
};

void AddSC_boss_renataki()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_renataki";
    pNewScript->GetAI = &GetNewAIInstance<boss_renatakiAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ThousandBladesRenataki>("spell_thousand_blades_renataki");
    RegisterSpellScript<RenatakiVanish>("spell_renataki_vanish");
    RegisterSpellScript<RenatakiVanishTeleport>("spell_renataki_vanish_teleport");
    RegisterSpellScript<RenatakiDemoralizingShout>("spell_renataki_demoralizing_shout");
}
