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
SDName: Zul'Gurub
SD%Complete: 0
SDComment:
SDCategory: Zul'Gurub
EndScriptData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiersImpl.h"
#include "zulgurub.h"

/* ContentData
EndContentData */

enum
{
    // npc_soulflayer
    SPELL_SOUL_TAP               = 24619,
    SPELL_LIGHTNING_BREATH       = 20543,
    SPELL_THRASH                 = 8876,
    SPELL_KNOCKDOWN              = 20276,
    SPELL_FEAR                   = 22678,
    SPELL_FRENZY                 = 28371,
    SPELL_ENRAGE                 = 8269
};

/*######
## npc_soulflayer
######*/

struct npc_soulflayerAI : public ScriptedAI
{
    npc_soulflayerAI(Creature* pCreature) : ScriptedAI(pCreature), m_uiRandomBuffAbility(0) { Reset(); }

    uint32 m_uiSoulTapTimer;
    uint32 m_uiLightingBreathTimer;
    uint32 m_uiRandomCcAbility;
    uint32 m_uiRandomCcAbilityTimer;
    uint32 m_uiRandomBuffAbility;

    const uint32 spell_list_cc[2] = { SPELL_FEAR, SPELL_KNOCKDOWN };
    const uint32 spell_list_buff[3] = { SPELL_ENRAGE, SPELL_FRENZY, SPELL_THRASH };

    void JustRespawned() override
    {
        // Set random time based CC ability on spawn (Fear or Knockdown)
        m_uiRandomCcAbility = spell_list_cc[urand(0, 1)];

        // Set a random buff HP based ability (Frenzy or Enrage) or Thrash on spawn
        m_uiRandomBuffAbility = spell_list_buff[urand(0, 2)];

        // Cast Thrash on spawn, since a passive ability
        if (m_uiRandomBuffAbility == SPELL_THRASH)
            DoCastSpellIfCan(m_creature, m_uiRandomBuffAbility, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void Reset() override
    {
        m_uiRandomCcAbilityTimer = urand(2000, 5000);
        m_uiSoulTapTimer = urand(m_uiRandomCcAbilityTimer, 7000);
        m_uiLightingBreathTimer = urand(m_uiSoulTapTimer, 9000);

        if (m_uiRandomBuffAbility == SPELL_THRASH)
            DoCastSpellIfCan(m_creature, m_uiRandomBuffAbility, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // Check if we have a valid target, otherwise do nothing
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Check if we are casting / channelling, return to not interrupt process and pause CDs
        if (m_creature->IsNonMeleeSpellCasted(false))
            return;

        // Soul Tap
        if (m_uiSoulTapTimer < uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_SOUL_TAP, SELECT_TARGET_ANY_ENEMY))
                if (DoCastSpellIfCan(pTarget, SPELL_SOUL_TAP) == CAST_OK)
                    m_uiSoulTapTimer = urand(10000, 15000);
        }
        else
            m_uiSoulTapTimer -= uiDiff;

        // Lightning Breath
        if (m_uiLightingBreathTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_LIGHTNING_BREATH) == CAST_OK)
                m_uiLightingBreathTimer = urand(10000, 15000);
        }
        else
            m_uiLightingBreathTimer -= uiDiff;

        // Random time based CC ability
        // Either Fear or Knockdown
        if (m_uiRandomCcAbilityTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), m_uiRandomCcAbility) == CAST_OK)
                m_uiRandomCcAbilityTimer = urand(8000, 10000);
        }
        else
            m_uiRandomCcAbilityTimer -= uiDiff;

        // Random HP based ability
        // Either Frenzy or Enrage
        if (m_uiRandomBuffAbility != SPELL_THRASH)
            if (m_creature->GetHealthPercent() <= 30.0f)
                if (!m_creature->HasAura(m_uiRandomBuffAbility))
                    DoCastSpellIfCan(m_creature->GetVictim(), m_uiRandomBuffAbility);

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_soulflayer(Creature* pCreature)
{
    return new npc_soulflayerAI(pCreature);
}

struct HazzarahChainBurn : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* caster = spell->GetCaster();
        if (!caster || caster->GetEntry() != NPC_HAZZARAH)
            return SPELL_CAST_OK;
        if (caster->GetPower(POWER_MANA) == 0)
            return SPELL_FAILED_NO_POWER;
        return IsManaPlayer(spell->m_targets.getUnitTarget()) ? SPELL_CAST_OK : SPELL_FAILED_BAD_TARGETS;
    }

    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* caster = spell->GetCaster();
        return !caster || caster->GetEntry() != NPC_HAZZARAH || IsManaPlayer(target);
    }

private:
    static bool IsManaPlayer(Unit const* target)
    {
        return target && target->IsPlayer() && target->GetPowerType() == POWER_MANA;
    }
};

enum JindoShadeSpells
{
    SPELL_JINDO_DELUSIONS = 24306,
    SPELL_JINDO_SHADE_PASSIVE = 24307,
    SPELL_JINDO_SHADE_VISUAL = 24313,
};

struct npc_shade_of_jindoAI : public CreatureEventAI
{
    explicit npc_shade_of_jindoAI(Creature* creature) : CreatureEventAI(creature)
    {
        creature->SetAOEImmune(true);
        creature->SetChainImmune(true);
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        // Periodic effects already applied by eligible players keep ticking.
        if (dealer && dealer != m_creature && damageType != DOT)
        {
            Player const* player = dealer->GetBeneficiaryPlayer();
            if (!player || !player->HasAura(SPELL_JINDO_DELUSIONS))
            {
                damage = 0;
                return;
            }
        }
        CreatureEventAI::DamageTaken(dealer, damage, damageType, spellInfo);
    }
};

void InitializeJindoShade(Creature* jindo, Creature* shade)
{
    shade->CastSpell(nullptr, SPELL_JINDO_SHADE_PASSIVE, TRIGGERED_OLD_TRIGGERED);
    shade->CastSpell(nullptr, SPELL_JINDO_SHADE_VISUAL, TRIGGERED_OLD_TRIGGERED);

    // A Shade can attack any player in the encounter, including players without Delusions.
    if (Unit* target = jindo->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
        if (shade->AI() && shade->CanAttack(target))
            shade->AI()->AttackStart(target);
}

// 24335 - Wyvern Sting
struct WyvernStingAura : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (!apply && aura->GetRemoveMode() == AURA_REMOVE_BY_EXPIRE)
            aura->GetCaster()->CastSpell(aura->GetTarget(), 24336, TRIGGERED_OLD_TRIGGERED); // Wyvern Sting Dot
    }
};

enum MarliSpawnGrowth
{
    SPELL_MARLI_SPAWN_GROWTH = 24086,
    SPELL_MARLI_SPAWN_FULL_GROWN = 24088,
    EMOTE_MARLI_SPAWN_FULL_GROWN = 10445,
    MARLI_SPAWN_GROWTH_INTERVAL = 4000,
    MARLI_SPAWN_GROWTH_CASTS = 5,
};

// Keep the spider's poison and standard combat behavior in EventAI.
struct npc_spawn_of_marliAI : public CreatureEventAI
{
    explicit npc_spawn_of_marliAI(Creature* creature) : CreatureEventAI(creature) {}

    void Reset() override
    {
        CreatureEventAI::Reset();
        m_growthStep = 0;
        m_growthTimer = MARLI_SPAWN_GROWTH_INTERVAL;
    }

    void UpdateEventTimers(uint32 diff) override
    {
        CreatureEventAI::UpdateEventTimers(diff);

        Unit* victim = m_creature->GetVictim();
        if (m_growthStep > MARLI_SPAWN_GROWTH_CASTS || !m_creature->IsAlive() ||
            !m_creature->IsInCombat() || !victim || !victim->IsAlive())
            return;

        if (m_growthTimer > diff)
        {
            m_growthTimer -= diff;
            return;
        }

        bool fullGrown = m_growthStep == MARLI_SPAWN_GROWTH_CASTS;
        uint32 spell = fullGrown ? SPELL_MARLI_SPAWN_FULL_GROWN : SPELL_MARLI_SPAWN_GROWTH;
        if (DoCastSpellIfCan(m_creature, spell, CAST_TRIGGERED) != CAST_OK)
        {
            m_growthTimer = 0;
            return;
        }

        ++m_growthStep;
        m_growthTimer = MARLI_SPAWN_GROWTH_INTERVAL;
        if (fullGrown)
            DoBroadcastText(EMOTE_MARLI_SPAWN_FULL_GROWN, m_creature, nullptr, CHAT_TYPE_TEXT_EMOTE);
    }

private:
    uint32 m_growthTimer = MARLI_SPAWN_GROWTH_INTERVAL;
    uint32 m_growthStep = 0;
};

namespace
{
constexpr uint32 GurubashiDrainLife = 24435;
constexpr uint32 GurubashiKnockback = 11130;
constexpr uint32 GurubashiIntimidatingRoar = 16508;
constexpr uint32 WillOfHakkar = 24178;
constexpr float VoodooTrapRange = 5.0f;
constexpr uint32 VoodooTrapTimeout = 21 * IN_MILLISECONDS;
constexpr uint32 VoodooTrapSearchInterval = 500;

struct npc_gurubashi_blood_drinkerAI : public CreatureEventAI
{
    explicit npc_gurubashi_blood_drinkerAI(Creature* creature) : CreatureEventAI(creature) {}

    Unit* GetTargetByType(uint32 target, Unit* invoker, Unit* sender, Unit* eventTarget, bool& isError,
        uint32 spellId, uint32 selectFlags) const override
    {
        if (target != TARGET_T_EVENT_SPECIFIC || spellId != GurubashiDrainLife)
            return CreatureEventAI::GetTargetByType(target, invoker, sender, eventTarget, isError, spellId, selectFlags);

        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        float const range = CalculateSpellRange(spellInfo);
        MaNGOS::AnySpellAssistableUnitInObjectRangeCheck check(m_creature, spellInfo, range);
        UnitList nearby;
        MaNGOS::UnitListSearcher<MaNGOS::AnySpellAssistableUnitInObjectRangeCheck> searcher(nearby, check);
        Cell::VisitAllObjects(m_creature, searcher, range);

        Unit* closest = nullptr;
        float closestDistance = range;
        for (Unit* candidate : nearby)
        {
            float const distance = m_creature->GetDistance(candidate);
            if (candidate != m_creature && distance < closestDistance && m_creature->IsWithinLOSInMap(candidate, true))
            {
                closest = candidate;
                closestDistance = distance;
            }
        }
        isError = !closest;
        return closest;
    }
};

struct npc_gurubashi_berserkerAI : public CreatureEventAI
{
    explicit npc_gurubashi_berserkerAI(Creature* creature) : CreatureEventAI(creature) {}

    bool ProcessAction(CreatureEventAI_Action const& action, uint32 random, uint32 eventId, Unit* invoker,
        Unit* sender, Unit* eventTarget) override
    {
        bool const success = CreatureEventAI::ProcessAction(action, random, eventId, invoker, sender, eventTarget);
        if (success && action.type == ACTION_T_CAST &&
            (action.cast.spellId == GurubashiKnockback || action.cast.spellId == GurubashiIntimidatingRoar) &&
            !m_creature->getThreatManager().isThreatListEmpty())
            DoResetThreat();
        return success;
    }
};

struct npc_jinxed_voodoo_pileAI : public ScriptedAI
{
    explicit npc_jinxed_voodoo_pileAI(Creature* creature) : ScriptedAI(creature) {}

    void JustRespawned() override
    {
        ScriptedAI::JustRespawned();
        SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
        SetMeleeEnabled(false);
        SetAIImmobilizedState(true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_creature->HasCharm())
        {
            m_charmedPlayer = true;
            return;
        }
        if (m_charmedPlayer || m_timeout <= diff)
        {
            m_creature->ForcedDespawn();
            return;
        }
        m_timeout -= diff;
        if (m_castStarted)
            return;
        if (m_searchTimer > diff)
        {
            m_searchTimer -= diff;
            return;
        }
        m_searchTimer = VoodooTrapSearchInterval;

        PlayerList nearby;
        GetPlayerListWithEntryInWorld(nearby, m_creature, VoodooTrapRange);
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(WillOfHakkar);
        Player* closest = nullptr;
        float closestDistance = VoodooTrapRange;
        for (Player* player : nearby)
        {
            float const distance = m_creature->GetDistance(player);
            if (distance < closestDistance && !player->HasCharmer() && !player->IsGameMaster() &&
                m_creature->CanAttackSpell(player, spellInfo))
            {
                closest = player;
                closestDistance = distance;
            }
        }
        if (closest && DoCastSpellIfCan(closest, WillOfHakkar) == CAST_OK)
            m_castStarted = true;
    }

private:
    uint32 m_timeout = VoodooTrapTimeout;
    uint32 m_searchTimer = VoodooTrapSearchInterval;
    bool m_castStarted = false;
    bool m_charmedPlayer = false;
};
}

bool GossipHello_go_edge_of_madness_tablet(Player* player, GameObject* tablet)
{
    auto* instance = dynamic_cast<instance_zulgurub*>(tablet->GetInstanceData());
    if (!instance)
        return false;

    uint32 const text = instance->GetEdgeOfMadnessTabletText(tablet->GetEntry());
    if (!text)
        return false;

    player->GetPlayerMenu()->ClearMenus();
    player->GetPlayerMenu()->SendGossipMenu(text, tablet->GetObjectGuid());
    return true;
}

void AddSC_zulgurub()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_soulflayer";
    pNewScript->GetAI = &GetAI_npc_soulflayer;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_spawn_of_marli";
    pNewScript->GetAI = &GetNewAIInstance<npc_spawn_of_marliAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_shade_of_jindo";
    pNewScript->GetAI = &GetNewAIInstance<npc_shade_of_jindoAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_gurubashi_blood_drinker";
    pNewScript->GetAI = &GetNewAIInstance<npc_gurubashi_blood_drinkerAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_gurubashi_berserker";
    pNewScript->GetAI = &GetNewAIInstance<npc_gurubashi_berserkerAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_jinxed_voodoo_pile";
    pNewScript->GetAI = &GetNewAIInstance<npc_jinxed_voodoo_pileAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_edge_of_madness_tablet";
    pNewScript->pGossipHelloGO = &GossipHello_go_edge_of_madness_tablet;
    pNewScript->RegisterSelf();

    RegisterSpellScript<HazzarahChainBurn>("spell_hazzarah_chain_burn");
    RegisterSpellScript<WyvernStingAura>("spell_wyvern_sting_aura");
}
