/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/Totem.h"
#include "zulgurub.h"

void InitializeJindoShade(Creature* jindo, Creature* shade);

enum
{
    SPELL_HEX                  = 24053,
    SPELL_DELUSIONS            = 24306,
    SPELL_SUMMON_SHADE         = 24308,
    SPELL_BRAIN_WASH           = 24261,
    NPC_SHADE_OF_JINDO          = 14986,
    NPC_POWERFUL_HEALING_WARD   = 14987,
    NPC_BRAIN_WASH_TOTEM        = 15112,
    HEALING_WARD_FIRST_TICK     = 2000,
    HEALING_WARD_TICK_INTERVAL  = 3000,
};

struct boss_jindoAI : public CreatureEventAI
{
    explicit boss_jindoAI(Creature* creature) : CreatureEventAI(creature) {}

    void JustSummoned(Creature* summon) override
    {
        CreatureEventAI::JustSummoned(summon);

        switch (summon->GetEntry())
        {
            case NPC_SHADE_OF_JINDO:
                if (summon->IsTemporarySummon() && summon->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
                    m_summons.insert(summon->GetObjectGuid()).second)
                    InitializeJindoShade(m_creature, summon);
                break;
            case NPC_POWERFUL_HEALING_WARD:
            case NPC_BRAIN_WASH_TOTEM:
                if (summon->IsTotem() && summon->GetOwnerGuid() == m_creature->GetObjectGuid())
                    m_summons.insert(summon->GetObjectGuid());
                break;
        }
    }

    void SummonedCreatureDespawn(Creature* summon) override
    {
        m_summons.erase(summon->GetObjectGuid());
        CreatureEventAI::SummonedCreatureDespawn(summon);
    }

    void SpellHitTarget(Unit* target, SpellEntry const* spell, SpellMissInfo missInfo) override
    {
        CreatureEventAI::SpellHitTarget(target, spell);

        if (missInfo != SPELL_MISS_NONE || !m_creature->IsInCombat() || !target->IsPlayer())
            return;

        if (spell->Id == SPELL_DELUSIONS && target->GetSpellAuraHolder(SPELL_DELUSIONS, m_creature->GetObjectGuid()))
        {
            m_creature->CastSpell(nullptr, SPELL_SUMMON_SHADE, TRIGGERED_OLD_TRIGGERED);
            return;
        }

        if (spell->Id != SPELL_HEX || !target->GetSpellAuraHolder(SPELL_HEX, m_creature->GetObjectGuid()))
            return;

        ThreatManager& threat = m_creature->getThreatManager();
        float const previousThreat = threat.getThreat(target);
        if (previousThreat > 0.0f && m_hexThreat.emplace(target->GetObjectGuid(), previousThreat).second)
            threat.modifyThreatPercent(target, -100);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_creature->IsInCombat())
            RestoreHexThreat();
        else
            m_hexThreat.clear();

        CreatureEventAI::UpdateAI(diff);
    }

    void EnterEvadeMode() override
    {
        CleanEncounter();
        CreatureEventAI::EnterEvadeMode();
    }

    void JustDied(Unit* killer) override
    {
        CleanEncounter();
        CreatureEventAI::JustDied(killer);
    }

    void StartBrainWash(Creature* totem)
    {
        if (!m_creature->IsInCombat() || !totem->IsAlive() || totem->HasCharm() ||
            m_summons.find(totem->GetObjectGuid()) == m_summons.end())
            return;

        std::vector<Unit*> targets;
        m_creature->SelectAttackingTargets(targets, ATTACKING_TARGET_ALL_SUITABLE, 0, SPELL_BRAIN_WASH, SELECT_FLAG_PLAYER);
        Unit* tank = m_creature->GetVictim();
        targets.erase(std::remove_if(targets.begin(), targets.end(), [tank, totem](Unit* target)
        {
            return target == tank || !target->IsAlive() || target->HasCharmer() || target->HasAura(SPELL_HEX) ||
                !totem->CanAttack(target);
        }), targets.end());

        while (!targets.empty())
        {
            uint32 const index = urand(0, uint32(targets.size() - 1));
            Unit* target = targets[index];
            if (totem->CastSpell(target, SPELL_BRAIN_WASH, TRIGGERED_NONE) == SPELL_CAST_OK)
                return;

            targets[index] = targets.back();
            targets.pop_back();
        }
    }

private:
    void RestoreHexThreat()
    {
        for (auto iter = m_hexThreat.begin(); iter != m_hexThreat.end();)
        {
            Player* target = m_creature->GetMap()->GetPlayer(iter->first);
            if (target && target->IsAlive())
            {
                if (target->GetSpellAuraHolder(SPELL_HEX, m_creature->GetObjectGuid()))
                {
                    ++iter;
                    continue;
                }

                // Add back the pre-Hex threat without discarding threat earned during the effect.
                m_creature->getThreatManager().addThreatDirectly(target, iter->second, true);
            }

            iter = m_hexThreat.erase(iter);
        }
    }

    void CleanEncounter()
    {
        m_hexThreat.clear();

        // Totem unsummon immediately notifies the owner AI.
        std::set<ObjectGuid> summons;
        summons.swap(m_summons);
        for (ObjectGuid const& guid : summons)
        {
            if (Creature* summon = m_creature->GetMap()->GetCreature(guid))
            {
                if (summon->IsTotem())
                    static_cast<Totem*>(summon)->UnSummon();
                else
                    summon->ForcedDespawn();
            }
        }
    }

    std::map<ObjectGuid, float> m_hexThreat;
    std::set<ObjectGuid> m_summons;
};

struct JindoHealingWardAura : public AuraScript
{
    void OnAfterApply(Aura* aura, bool apply) const override
    {
        if (apply)
            aura->ForcePeriodicity(HEALING_WARD_FIRST_TICK);
    }

    void OnPeriodicTickEnd(Aura* aura) const override
    {
        if (aura->GetAuraTicks() == 1)
            aura->ForcePeriodicity(HEALING_WARD_TICK_INTERVAL);
    }

    void OnPeriodicTrigger(Aura* aura, PeriodicTriggerData& data) const override
    {
        Unit* ward = aura->GetTarget();
        Unit* owner = ward->GetOwner();
        if (ward->GetEntry() == NPC_POWERFUL_HEALING_WARD && ward->IsAlive() &&
            owner && owner->GetEntry() == NPC_JINDO && owner->IsAlive() && owner->IsInCombat())
            data.target = owner;
        else
            data.spellInfo = nullptr;
    }
};

struct JindoHealingWardHeal : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* ward = spell->GetCaster();
        return ward && ward->GetEntry() == NPC_POWERFUL_HEALING_WARD &&
            target->GetEntry() == NPC_JINDO && ward->GetOwnerGuid() == target->GetObjectGuid();
    }
};

struct JindoBrainWashTotem : public SpellScript
{
    void OnSummon(Spell* spell, Creature* summon) const override
    {
        Unit* caster = spell->GetCaster();
        if (caster && caster->GetEntry() == NPC_JINDO && summon->GetEntry() == NPC_BRAIN_WASH_TOTEM)
            if (auto* ai = dynamic_cast<boss_jindoAI*>(caster->AI()))
                ai->StartBrainWash(summon);
    }
};

void AddSC_boss_jindo()
{
    Script* script = new Script;
    script->Name = "boss_jindo";
    script->GetAI = &GetNewAIInstance<boss_jindoAI>;
    script->RegisterSelf();

    RegisterSpellScript<JindoHealingWardAura>("spell_jindo_healing_ward");
    RegisterSpellScript<JindoHealingWardHeal>("spell_jindo_healing_ward_heal");
    RegisterSpellScript<JindoBrainWashTotem>("spell_jindo_brain_wash_totem");
}
