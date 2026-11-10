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
SDName: Boss_Arlokk
SD%Complete: 80
SDComment: Phase timings need combat log verification
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/ScriptDevAI/base/CombatAI.h"

/* ContentData
boss_arlokk
npc_zulian_prowler
go_gong_of_bethekk
EndContentData */

enum
{
    SAY_AGGRO                   = -1309011,
    SAY_FEAST_PANTHER           = -1309012,
    SAY_DEATH                   = -1309013,

    SPELL_GOUGE                 = 12540,
    SPELL_MARK_ARLOKK           = 24210,
    SPELL_MARK_ARLOKK_THREAT    = 24211,
    SPELL_WHIRLWIND             = 24236,
    SPELL_PANTHER_TRANSFORM     = 24190,
    SPELL_SUMMON_ZULIAN_PROWLERS = 24247,
    SPELL_VANISH_TELEPORT       = 24228,
    SPELL_VANISH                = 24223,
    SPELL_SUPER_INVIS           = 24235,
    SPELL_TRANSFORM_VISUAL      = 24085,
    SPELL_SNEAK                 = 22766,

    SPELL_LIST_PHASE_1 = 1451501,
    SPELL_LIST_PHASE_2 = 1451502,
};

enum ArlokkActions
{
    ARLOKK_PHASE_TRANSITION,
    ARLOKK_GOUGE,
    ARLOKK_ACTION_MAX,
    ARLOKK_EMERGE,
    ARLOKK_MARK,
};

enum class ArlokkPhase
{
    Troll,
    Vanished,
    Panther,
};

struct boss_arlokkAI : public CombatAI
{
    boss_arlokkAI(Creature* creature) : CombatAI(creature, ARLOKK_ACTION_MAX), m_instance(static_cast<instance_zulgurub*>(creature->GetInstanceData()))
    {
        AddCombatAction(ARLOKK_PHASE_TRANSITION, 35s, 40s);
        AddTimerlessCombatAction(ARLOKK_GOUGE, true);
        AddCustomAction(ARLOKK_EMERGE, true, [&]() { EndVanish(); }, TIMER_COMBAT_COMBAT);
        AddCustomAction(ARLOKK_MARK, 5s, [&]() { CastMark(); }, TIMER_COMBAT_COMBAT);
    }

    instance_zulgurub* m_instance;
    ArlokkPhase m_phase = ArlokkPhase::Troll;
    ObjectGuid m_markedPlayer;
    uint32 m_gougeTimer = 14000;

    void Reset() override
    {
        CombatAI::Reset();
        ClearMark();
        RestoreVisibility();
        m_phase = ArlokkPhase::Troll;
        m_gougeTimer = 14000;
        m_creature->RemoveAurasDueToSpell(SPELL_PANTHER_TRANSFORM);
        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
        SetCombatMovement(true);
        SetMeleeEnabled(true);
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
        if (m_instance)
            for (bool left : { true, false })
                if (Creature* trigger = m_instance->SelectRandomPantherTrigger(left))
                    trigger->CastSpell(trigger, SPELL_SUMMON_ZULIAN_PROWLERS, TRIGGERED_NONE);
    }

    void JustReachedHome() override
    {
        ClearMark();
        if (m_instance)
            m_instance->SetData(TYPE_ARLOKK, FAIL);
        m_creature->ForcedDespawn();
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoScriptText(SAY_DEATH, m_creature);
        ClearMark();
        RestoreVisibility();
        if (m_creature->GetVisibility() != VISIBILITY_ON)
            m_creature->SetVisibility(VISIBILITY_ON);
        if (m_instance)
            m_instance->SetData(TYPE_ARLOKK, DONE);
    }

    void ClearMark()
    {
        if (m_markedPlayer)
            if (Unit* target = m_creature->GetMap()->GetUnit(m_markedPlayer))
                target->RemoveAurasByCasterSpell(SPELL_MARK_ARLOKK, m_creature->GetObjectGuid());
        m_markedPlayer.Clear();
    }

    void CastMark()
    {
        Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER | SELECT_FLAG_IN_LOS);
        uint32 flags = m_phase == ArlokkPhase::Vanished ? CAST_TRIGGERED : 0;
        bool const cast = target && DoCastSpellIfCan(target, SPELL_MARK_ARLOKK, flags) == CAST_OK;
        ResetTimer(ARLOKK_MARK, cast ? 30s : 1s);
    }

    bool HasMarked(Unit const* target) const
    {
        return m_markedPlayer == target->GetObjectGuid();
    }

    void RestoreVisibility()
    {
        m_creature->RemoveAurasDueToSpell(SPELL_VANISH);
        m_creature->RemoveAurasDueToSpell(SPELL_SUPER_INVIS);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        SetCombatScriptStatus(false);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* /*invoker*/, uint32 /*miscValue*/) override
    {
        if (eventType == AI_EVENT_CUSTOM_A && m_phase == ArlokkPhase::Troll && m_creature->IsInCombat())
            BeginVanish();
    }

    void BeginVanish()
    {
        m_phase = ArlokkPhase::Vanished;
        DisableCombatAction(ARLOKK_PHASE_TRANSITION);
        SetCombatScriptStatus(true);
        SetCombatMovement(false, true);
        SetMeleeEnabled(false);
        m_creature->AttackStop();
        DoResetThreat();
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->CastSpell(nullptr, SPELL_VANISH_TELEPORT, TRIGGERED_OLD_TRIGGERED);
        m_creature->CastSpell(nullptr, SPELL_SUPER_INVIS, TRIGGERED_OLD_TRIGGERED);
        m_creature->CastSpell(nullptr, SPELL_PANTHER_TRANSFORM, TRIGGERED_OLD_TRIGGERED);
        ResetTimer(ARLOKK_EMERGE, 50s);
    }

    void EndVanish()
    {
        Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER);
        if (target && !target->GetTransport())
        {
            Position landing;
            target->GetFirstCollisionPosition(landing, 4.f, target->GetOrientation() + M_PI_F);
            if (target->GetDistance2d(landing.x, landing.y, DIST_CALC_NONE) >= 1.f &&
                std::abs(landing.z - target->GetPositionZ()) <= 3.f &&
                target->IsWithinLOS(landing.x, landing.y, landing.z + m_creature->GetCollisionHeight()))
                m_creature->NearTeleportTo(landing.x, landing.y, landing.z, landing.GetAngle(target->GetPositionX(), target->GetPositionY()));
        }

        m_phase = ArlokkPhase::Panther;
        RestoreVisibility();
        m_creature->SetSpellList(SPELL_LIST_PHASE_2);
        if (target)
            m_creature->CastSpell(nullptr, SPELL_WHIRLWIND, TRIGGERED_OLD_TRIGGERED);
        SetCombatMovement(true);
        SetMeleeEnabled(true);
        if (target)
            AttackStart(target);
        ResetCombatAction(ARLOKK_PHASE_TRANSITION, 20s);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_phase == ArlokkPhase::Vanished && !m_creature->IsInCombat())
        {
            if (GetAIOrder() != ORDER_EVADE)
                EnterEvadeMode();
            return;
        }

        // Gouge counts only the time spent in visible panther form.
        if (m_phase == ArlokkPhase::Panther && m_creature->IsInCombat())
            m_gougeTimer -= std::min(m_gougeTimer, diff);
        CombatAI::UpdateAI(diff);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case ARLOKK_PHASE_TRANSITION:
                if (m_phase == ArlokkPhase::Panther)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_TRANSFORM_VISUAL) == CAST_OK)
                    {
                        m_creature->RemoveAurasDueToSpell(SPELL_PANTHER_TRANSFORM);
                        m_phase = ArlokkPhase::Troll;
                        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
                        ResetCombatAction(action, urand(35000, 40000));
                    }
                }
                else if (m_phase == ArlokkPhase::Troll)
                    DoCastSpellIfCan(nullptr, SPELL_VANISH);
                break;
            case ARLOKK_GOUGE:
                if (m_phase == ArlokkPhase::Panther && !m_gougeTimer &&
                    DoCastSpellIfCan(m_creature->GetVictim(), SPELL_GOUGE) == CAST_OK)
                    m_gougeTimer = urand(17000, 27000);
                break;
        }
    }

    void SpellHitTarget(Unit* target, SpellEntry const* spellInfo, SpellMissInfo missInfo) override
    {
        if (missInfo != SPELL_MISS_NONE)
            return;

        if (spellInfo->Id == SPELL_GOUGE && m_phase == ArlokkPhase::Panther)
            m_creature->getThreatManager().modifyThreatPercent(target, -80);
        else if (spellInfo->Id == SPELL_MARK_ARLOKK && target->HasAura(SPELL_MARK_ARLOKK))
        {
            if (!HasMarked(target))
            {
                ClearMark();
                m_markedPlayer = target->GetObjectGuid();
            }
            DoScriptText(SAY_FEAST_PANTHER, m_creature, target);
        }
    }
};

struct npc_zulian_prowlerAI : public ScriptedAI
{
    npc_zulian_prowlerAI(Creature* creature) : ScriptedAI(creature) {}

    void JustRespawned() override
    {
        ScriptedAI::JustRespawned();
        DoCastSpellIfCan(nullptr, SPELL_SNEAK);
        if (!m_creature->IsTemporarySummon() || m_creature->GetSpawnerGuid().GetEntry() != NPC_PANTHER_TRIGGER)
            return;

        auto* instance = static_cast<instance_zulgurub*>(m_creature->GetInstanceData());
        if (!instance || !instance->RegisterArlokkProwler(*m_creature))
        {
            m_creature->ForcedDespawn();
            return;
        }

        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MovePoint(1, aArlokkWallShieldPos[0], aArlokkWallShieldPos[1], aArlokkWallShieldPos[2]);
    }

    void SpellHit(Unit* caster, SpellEntry const* spellInfo) override
    {
        if (spellInfo->Id == SPELL_MARK_ARLOKK_THREAT && caster && !m_creature->GetVictim() &&
            m_creature->getThreatManager().getThreat(caster) > 0.f)
            AttackStart(caster);
    }
};

bool GOUse_go_gong_of_bethekk(Player* /*player*/, GameObject* go)
{
    if (auto* instance = static_cast<instance_zulgurub*>(go->GetInstanceData()))
        instance->StartArlokkEncounter(*go);
    return true;
}

struct ArlokkVanish : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_1)
            return;

        if (Unit* caster = spell->GetCaster())
            if (caster->GetEntry() == NPC_ARLOKK && caster->AI())
                caster->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, caster, caster);
    }
};

struct ArlokkMarkThreat : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        if (!target->IsCreature() || !target->IsAlive())
            return false;

        Creature const& prowler = *static_cast<Creature*>(target);
        auto* instance = static_cast<instance_zulgurub*>(prowler.GetInstanceData());
        if (!instance || instance->GetData(TYPE_ARLOKK) != IN_PROGRESS || !instance->IsArlokkProwler(prowler))
            return false;

        Creature* arlokk = instance->GetSingleCreatureFromStorage(NPC_ARLOKK, true);
        auto* ai = arlokk ? dynamic_cast<boss_arlokkAI*>(arlokk->AI()) : nullptr;
        return ai && spell->GetCaster() && ai->HasMarked(spell->GetCaster());
    }
};

void AddSC_boss_arlokk()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_arlokk";
    pNewScript->GetAI = &GetNewAIInstance<boss_arlokkAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_zulian_prowler";
    pNewScript->GetAI = &GetNewAIInstance<npc_zulian_prowlerAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_gong_of_bethekk";
    pNewScript->pGOUse = &GOUse_go_gong_of_bethekk;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ArlokkVanish>("spell_arlokk_vanish");
    RegisterSpellScript<ArlokkMarkThreat>("spell_arlokk_mark_threat");
}
