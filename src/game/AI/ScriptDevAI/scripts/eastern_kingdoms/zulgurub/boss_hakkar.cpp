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
SDName: Boss_Hakkar
SD%Complete: 100
SDComment:
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Combat/HostileRefManager.h"
#include "Spells/SpellAuras.h"

#include <algorithm>
#include <atomic>

enum
{
    SAY_AGGRO                   = -1309020,
    EMOTE_BERSERK               = -1000004,

    SPELL_BLOOD_SIPHON          = 24324,                    // triggers 24322 or 24323 on caster
    SPELL_CORRUPTED_BLOOD       = 24328,
    SPELL_CAUSE_INSANITY        = 24327,
    SPELL_ENRAGE                = 24318,
    SPELL_DOUBLE_ATTACK         = 19818,

    // The Aspects of all High Priests
    SPELL_ASPECT_OF_JEKLIK      = 24687,
    SPELL_ASPECT_OF_VENOXIS     = 24688,
    SPELL_ASPECT_OF_MARLI       = 24686,
    SPELL_ASPECT_OF_THEKAL      = 24689,
    SPELL_ASPECT_OF_ARLOKK      = 24690
};

enum HakkarActions
{
    HAKKAR_ENRAGE_LOW,
    HAKKAR_ENRAGE_TIMEOUT,
    HAKKAR_CAUSE_INSANITY,
    HAKKAR_ACTION_MAX,
};

struct boss_hakkarAI : public CombatAI
{
    boss_hakkarAI(Creature* creature) : CombatAI(creature, HAKKAR_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddTimerlessCombatAction(HAKKAR_ENRAGE_LOW, true);
        AddCombatAction(HAKKAR_ENRAGE_TIMEOUT, 10u * MINUTE * IN_MILLISECONDS);
        AddCombatAction(HAKKAR_CAUSE_INSANITY, 20000u);
    }

    ScriptedInstance* m_instance;

    void Reset() override
    {
        CombatAI::Reset();

        ClearCauseInsanity();
        DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);

        SynchronizeHakkarPowerStacks();
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* /*invoker*/, uint32 miscValue) override
    {
        if (eventType != AI_EVENT_CUSTOM_B || !m_creature->IsAlive() || m_creature->IsInCombat())
            return;
        if (miscValue == ZG_RESET_HAKKAR_POWER)
            SynchronizeHakkarPowerStacks();
        else if (miscValue == ZG_RESET_HAKKAR_EVENT)
        {
            for (uint32 spell : {uint32(SPELL_ASPECT_OF_JEKLIK), uint32(SPELL_ASPECT_OF_VENOXIS),
                uint32(SPELL_ASPECT_OF_MARLI), uint32(SPELL_ASPECT_OF_THEKAL), uint32(SPELL_ASPECT_OF_ARLOKK),
                uint32(SPELL_ENRAGE)})
                m_creature->RemoveAurasDueToSpell(spell);
            m_creature->RemoveAllCooldowns();
            JustRespawned();
        }
    }

    void UpdateAI(uint32 diff) override
    {
        RestoreInsanityThreat();
        CombatAI::UpdateAI(diff);
    }

    void JustDied(Unit* killer) override
    {
        ClearCauseInsanity();
        CombatAI::JustDied(killer);
    }

    void SynchronizeHakkarPowerStacks()
    {
        if (!m_instance)
            return;

        uint32 stacks = 0;
        for (uint8 i = 0; i < MAX_PRIESTS; i++)
            if (m_instance->GetData(i) != DONE)
                ++stacks;

        SpellAuraHolder* power = m_creature->GetSpellAuraHolder(SPELL_HAKKAR_POWER, m_creature->GetObjectGuid());
        if ((power ? power->GetStackAmount() : 0) == stacks)
            return;

        uint32 const previousHealth = m_creature->GetHealth();
        bool const wasFullOutOfCombat = !m_creature->IsInCombat() && previousHealth == m_creature->GetMaxHealth();
        if (!stacks)
            m_creature->RemoveSpellAuraHolder(power);
        else
        {
            if (!power)
            {
                m_creature->CastSpell(m_creature, SPELL_HAKKAR_POWER, TRIGGERED_OLD_TRIGGERED);
                power = m_creature->GetSpellAuraHolder(SPELL_HAKKAR_POWER, m_creature->GetObjectGuid());
            }
            if (power && power->GetStackAmount() != stacks)
                power->SetStackAmount(stacks, m_creature);
        }

        // Changing stacks briefly removes the old max-health bonus, which can clamp current health.
        uint32 const maxHealth = m_creature->GetMaxHealth();
        uint32 const health = wasFullOutOfCombat ? maxHealth : std::min(previousHealth, maxHealth);
        if (m_creature->GetHealth() != health)
            m_creature->SetHealth(health);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case HAKKAR_ENRAGE_LOW:
                if (m_creature->GetHealthPercent() >= 5.f)
                    return;
                [[fallthrough]];
            case HAKKAR_ENRAGE_TIMEOUT:
                if (DoCastSpellIfCan(m_creature, SPELL_ENRAGE) == CAST_OK && m_creature->HasAura(SPELL_ENRAGE))
                {
                    DisableCombatAction(HAKKAR_ENRAGE_LOW);
                    DisableCombatAction(HAKKAR_ENRAGE_TIMEOUT);
                    DoScriptText(EMOTE_BERSERK, m_creature);
                }
                break;
            case HAKKAR_CAUSE_INSANITY:
                CastCauseInsanity();
                break;
        }
    }

private:
    void CastCauseInsanity()
    {
        Unit* target = m_creature->GetVictim();
        ThreatManager& threat = m_creature->getThreatManager();
        if (!target || !target->IsPlayer() || target->HasCharmer() || !m_insanityTarget.IsEmpty() ||
            threat.getThreatList().size() <= 1)
            return;

        float const previousThreat = threat.getThreat(target);
        if (DoCastSpellIfCan(target, SPELL_CAUSE_INSANITY) != CAST_OK)
            return;

        ResetCombatAction(HAKKAR_CAUSE_INSANITY, urand(20000, 25000));
        if (target->HasCharmer(m_creature->GetObjectGuid()) &&
            target->GetSpellAuraHolder(SPELL_CAUSE_INSANITY, m_creature->GetObjectGuid()))
        {
            if (m_creature->GetVictim() == target)
                m_creature->AttackStop(true);
            target->getHostileRefManager().deleteReference(m_creature);
            m_insanityTarget = target->GetObjectGuid();
            m_insanityThreat = previousThreat;
        }
    }

    void RestoreInsanityThreat()
    {
        if (m_insanityTarget.IsEmpty())
            return;

        if (!m_creature->IsInCombat())
        {
            ClearCauseInsanity();
            return;
        }

        if (Player* target = m_creature->GetMap()->GetPlayer(m_insanityTarget))
        {
            if (target->IsAlive())
            {
                if (target->GetSpellAuraHolder(SPELL_CAUSE_INSANITY, m_creature->GetObjectGuid()))
                    return;

                if (m_insanityThreat > 0.f && m_creature->CanAttack(target))
                    m_creature->getThreatManager().addThreatDirectly(target, m_insanityThreat, false);
            }
        }

        m_insanityTarget.Clear();
        m_insanityThreat = 0.f;
    }

    void ClearCauseInsanity()
    {
        if (m_insanityTarget.IsEmpty())
            return;

        ObjectGuid const targetGuid = m_insanityTarget;
        m_insanityTarget.Clear();
        m_insanityThreat = 0.f;
        if (Player* target = m_creature->GetMap()->GetPlayer(targetGuid))
            if (SpellAuraHolder* aura = target->GetSpellAuraHolder(SPELL_CAUSE_INSANITY, m_creature->GetObjectGuid()))
                target->RemoveSpellAuraHolder(aura);
    }

    ObjectGuid m_insanityTarget;
    float m_insanityThreat = 0.f;
};

struct HakkarPowerDown : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return;

        Unit* caster = spell->GetCaster();
        if (caster && caster == spell->GetUnitTarget() && caster->GetEntry() == NPC_HAKKAR && caster->IsAlive())
            if (auto* ai = dynamic_cast<boss_hakkarAI*>(caster->AI()))
                ai->SynchronizeHakkarPowerStacks();
    }
};

enum HakkarBloodSpells
{
    NPC_HAKKAR_POISON_CLOUD       = 14989,
    SPELL_HAKKAR_POISON_CLOUD     = 24320,
    SPELL_HAKKAR_POISONOUS_BLOOD  = 24321,
    SPELL_HAKKAR_SIPHON_HEAL      = 24322,
    SPELL_HAKKAR_SIPHON_DAMAGE    = 24323,
};

struct npc_hakkar_poison_cloudAI : public ScriptedAI
{
    explicit npc_hakkar_poison_cloudAI(Creature* creature) : ScriptedAI(creature)
    {
        SetReactState(REACT_PASSIVE);
    }

    void UpdateAI(uint32 const diff) override
    {
        if (m_pulseTimer > diff)
        {
            m_pulseTimer -= diff;
            return;
        }

        m_pulseTimer = IN_MILLISECONDS;
        m_creature->CastSpell(m_creature, SPELL_HAKKAR_POISON_CLOUD, TRIGGERED_OLD_TRIGGERED);
    }

    bool HasPoisoned(Unit const& player) const
    {
        return m_poisoned.count(player.GetObjectGuid()) != 0;
    }

    void RecordPoisoned(Unit const& player)
    {
        m_poisoned.insert(player.GetObjectGuid());
    }

private:
    uint32 m_pulseTimer = 0;
    GuidSet m_poisoned;
};

struct PoisonousCloud : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        auto* cloud = GetCloudAI(spell);
        return !cloud || (target->IsPlayer() && target->IsAlive() &&
            !cloud->HasPoisoned(*target) && !target->HasAura(SPELL_HAKKAR_POISONOUS_BLOOD));
    }

    void OnAfterHit(Spell* spell) const override
    {
        Unit* target = spell->GetUnitTarget();
        if (target && target->IsPlayer() && target->HasAura(SPELL_HAKKAR_POISONOUS_BLOOD))
            if (auto* cloud = GetCloudAI(spell))
                cloud->RecordPoisoned(*target);
    }

private:
    static npc_hakkar_poison_cloudAI* GetCloudAI(Spell const* spell)
    {
        Unit* caster = spell->GetCaster();
        return caster && caster->GetEntry() == NPC_HAKKAR_POISON_CLOUD
            ? dynamic_cast<npc_hakkar_poison_cloudAI*>(caster->AI()) : nullptr;
    }
};

struct BloodSiphon : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return true;

        Unit* hakkar = spell->GetCaster();
        return hakkar && target->IsPlayer() && target->IsAlive() &&
            hakkar->getThreatManager().HasThreat(target, true);
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return;

        if (Unit* target = spell->GetUnitTarget())
            if (OnCheckTarget(spell, target, effIdx))
                target->CastSpell(spell->GetCaster(), target->HasAura(SPELL_HAKKAR_POISONOUS_BLOOD)
                    ? SPELL_HAKKAR_SIPHON_DAMAGE : SPELL_HAKKAR_SIPHON_HEAL, TRIGGERED_OLD_TRIGGERED);
    }
};

struct BloodSiphonPoisoned : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_0 || aura->GetTarget()->GetEntry() != NPC_HAKKAR)
            return;

        Unit* player = aura->GetCaster();
        if (!player || !player->IsPlayer())
            return;

        Aura* poison = player->GetAura(SPELL_HAKKAR_POISONOUS_BLOOD, EFFECT_INDEX_0);
        if (!poison)
            return;

        if (apply)
        {
            // Pair this damage aura with the exact poison instance that supplied it.
            static std::atomic<uint64> nextPoisonUse{1};
            uint64 token = nextPoisonUse.fetch_add(1, std::memory_order_relaxed);
            poison->SetScriptValue(token);
            aura->SetScriptValue(token);
        }
        else if (aura->GetScriptValue() && aura->GetScriptValue() == poison->GetScriptValue())
            player->RemoveSpellAuraHolder(poison->GetHolder());
    }
};

struct CorruptedBlood : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* caster = spell->GetCaster();
        return !caster || caster->GetEntry() != NPC_HAKKAR || target->IsPlayer();
    }
};

void AddSC_boss_hakkar()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_hakkar";
    pNewScript->GetAI = &GetNewAIInstance<boss_hakkarAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_hakkar_poison_cloud";
    pNewScript->GetAI = &GetNewAIInstance<npc_hakkar_poison_cloudAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<HakkarPowerDown>("spell_hakkar_power_down");
    RegisterSpellScript<PoisonousCloud>("spell_hakkar_poisonous_cloud");
    RegisterSpellScript<BloodSiphon>("spell_hakkar_blood_siphon");
    RegisterSpellScript<BloodSiphonPoisoned>("spell_hakkar_blood_siphon_poisoned");
    RegisterSpellScript<CorruptedBlood>("spell_hakkar_corrupted_blood");
}
