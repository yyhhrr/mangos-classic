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
SDName: Boss_Golemagg
SD%Complete: 100
SDComment:
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "molten_core.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Maps/SpawnGroup.h"
#include "Spells/Scripts/SpellScript.h"

enum
{
    SPELL_MAGMA_SPLASH      = 13879,
    SPELL_PYROBLAST         = 20228,
    SPELL_EARTHQUAKE        = 19798,
    SPELL_ATTRACT_RAGER     = 20544,
    SPELL_GOLEMAGG_TRUST    = 20556,
    SPELL_DOUBLE_ATTACK     = 18943,

    // Core Rager
    EMOTE_LOW_HP            = -1409002,
    SPELL_QUIET_SUICIDE     = 3617,
    SPELL_THRASH            = 12787,
    SPELL_FULL_HEAL         = 17683,
    SPELL_MANGLE            = 19820
};

namespace
{
CreatureGroup const* GetGolemaggGroup(Creature const* creature)
{
    CreatureGroup const* group = creature->GetCreatureGroup();
    return group && group->GetGroupId() == GROUP_GOLEMAGG ? group : nullptr;
}

bool IsGolemaggRager(Unit const* caster, Unit const* target)
{
    if (!caster || !caster->IsCreature() || caster->GetEntry() != NPC_GOLEMAGG ||
        !target || !target->IsCreature() || target->GetEntry() != NPC_CORE_RAGER || !target->IsAlive())
        return false;

    CreatureGroup const* group = GetGolemaggGroup(static_cast<Creature const*>(caster));
    return group && group == static_cast<Creature const*>(target)->GetCreatureGroup();
}

bool HealthBelowPctAfterDamage(Creature const* creature, uint32 damage, uint32 pct)
{
    int64 const remainingHealth = int64(creature->GetHealth()) - damage;
    return remainingHealth * 100 < int64(creature->GetMaxHealth()) * pct;
}

bool IsGolemaggComplete(InstanceData const* instance)
{
    uint32 const state = instance->GetData(TYPE_GOLEMAGG);
    return state == DONE || state == SPECIAL;
}

void KillCoreRager(Creature* rager)
{
    rager->RemoveAurasDueToSpell(SPELL_ATTRACT_RAGER);
    rager->CastSpell(rager, SPELL_QUIET_SUICIDE, TRIGGERED_OLD_TRIGGERED);
    if (rager->IsAlive())
        rager->Suicide();
}
}

enum GolemaggActions
{
    GOLEMAGG_PYROBLAST,
    GOLEMAGG_EARTHQUAKE,
    GOLEMAGG_ACTION_MAX,
};

struct boss_golemaggAI : public CombatAI
{
    boss_golemaggAI(Creature* creature) : CombatAI(creature, GOLEMAGG_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddCombatAction(GOLEMAGG_PYROBLAST, uint32(7 * IN_MILLISECONDS));
        AddCombatAction(GOLEMAGG_EARTHQUAKE, true);
    }

    ScriptedInstance* m_instance;
    bool m_lowHealthPhase = false;

    void Reset() override
    {
        CombatAI::Reset();
        m_lowHealthPhase = false;

        DoCastSpellIfCan(nullptr, SPELL_MAGMA_SPLASH, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(nullptr, SPELL_GOLEMAGG_TRUST, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void Aggro(Unit* /*who*/) override
    {
        if (m_instance)
            m_instance->SetData(TYPE_GOLEMAGG, IN_PROGRESS);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_instance)
            m_instance->SetData(TYPE_GOLEMAGG, DONE);

        if (CreatureGroup const* group = GetGolemaggGroup(m_creature))
            for (SpawnGroupDbGuids const& member : group->GetGroupEntry().DbGuids)
                if (Creature* rager = m_creature->GetMap()->GetCreature(member.DbGuid))
                    if (IsGolemaggRager(m_creature, rager))
                        KillCoreRager(rager);
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_GOLEMAGG, FAIL);
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        CombatAI::DamageTaken(dealer, damage, damageType, spellInfo);
        if (m_lowHealthPhase || !damage || !HealthBelowPctAfterDamage(m_creature, damage, 10))
            return;

        m_lowHealthPhase = true;
        DoCastSpellIfCan(m_creature, SPELL_ATTRACT_RAGER, CAST_TRIGGERED);
        ResetCombatAction(GOLEMAGG_EARTHQUAKE, 0u);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case GOLEMAGG_PYROBLAST:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_PYROBLAST, SELECT_FLAG_PLAYER))
                    if (DoCastSpellIfCan(target, SPELL_PYROBLAST) == CAST_OK)
                        ResetCombatAction(action, 7 * IN_MILLISECONDS);
                break;
            }
            case GOLEMAGG_EARTHQUAKE:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_EARTHQUAKE) == CAST_OK)
                    ResetCombatAction(action, 3 * IN_MILLISECONDS);
                break;
            }
        }
    }
};

enum CoreRagerActions
{
    CORE_RAGER_MANGLE,
    CORE_RAGER_ACTION_MAX,
};

struct mob_core_ragerAI : public CombatAI
{
    mob_core_ragerAI(Creature* creature) : CombatAI(creature, CORE_RAGER_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddCombatAction(CORE_RAGER_MANGLE, uint32(7 * IN_MILLISECONDS)); // These times are probably wrong
    }

    ScriptedInstance* m_instance;

    void Reset() override
    {
        CombatAI::Reset();
        if (m_creature->IsAlive() && m_instance && IsGolemaggComplete(m_instance) && GetGolemaggGroup(m_creature))
        {
            // A rager may load from another grid after its master has died.
            KillCoreRager(m_creature);
            return;
        }
        DoCastSpellIfCan(nullptr, SPELL_THRASH, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        CombatAI::DamageTaken(dealer, damage, damageType, spellInfo);
        if (!damage || !m_instance || IsGolemaggComplete(m_instance) || !GetGolemaggGroup(m_creature) ||
            !HealthBelowPctAfterDamage(m_creature, damage, 50))
            return;

        if (Creature* boss = m_instance->GetSingleCreatureFromStorage(NPC_GOLEMAGG, true))
            if (!boss->IsAlive() || !IsGolemaggRager(boss, m_creature))
                return;

        damage = 0;
        DoCastSpellIfCan(nullptr, SPELL_FULL_HEAL, CAST_TRIGGERED);
        m_creature->SetHealth(m_creature->GetMaxHealth());
        DoScriptText(EMOTE_LOW_HP, m_creature);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case CORE_RAGER_MANGLE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MANGLE) == CAST_OK)
                    ResetCombatAction(action, 10 * IN_MILLISECONDS);
                break;
            }
        }
    }
};

struct GolemaggAttractRager : SpellScript
{
    void OnInit(Spell* spell) const override
    {
        // AzerothCore skips the threat effect between Golemagg and his friendly ragers.
        spell->SetEffectSkipMask(1u << EFFECT_INDEX_0);
    }

    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effect*/) const override
    {
        return IsGolemaggRager(spell->GetCaster(), target);
    }
};

void AddSC_boss_golemagg()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_golemagg";
    pNewScript->GetAI = &GetNewAIInstance<boss_golemaggAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_core_rager";
    pNewScript->GetAI = &GetNewAIInstance<mob_core_ragerAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<GolemaggAttractRager>("spell_golemagg_attract_rager");
}
