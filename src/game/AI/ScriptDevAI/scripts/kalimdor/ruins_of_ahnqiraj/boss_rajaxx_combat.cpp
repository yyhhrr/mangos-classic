/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "MotionGenerators/MovementGenerator.h"
#include "ruins_of_ahnqiraj.h"

namespace
{
constexpr uint32 SPELL_SUMMON_PLAYER = 20477;
constexpr uint32 CHASE_CHECK_INTERVAL = IN_MILLISECONDS;
constexpr uint8 BLOCKED_CHASE_CHECKS = 6;

struct boss_rajaxxAI : public CreatureEventAI
{
    explicit boss_rajaxxAI(Creature* creature) : CreatureEventAI(creature),
        m_instance(dynamic_cast<instance_ruins_of_ahnqiraj*>(creature->GetInstanceData()))
    {
    }

    void Reset() override
    {
        CreatureEventAI::Reset();
        ResetChaseCheck();
    }

    void UpdateEventTimers(uint32 diff) override
    {
        CreatureEventAI::UpdateEventTimers(diff);

        if (!m_instance)
            return;

        if (!m_instance->IsRajaxxReleased() || !m_creature->IsAlive() || !m_creature->IsInCombat())
        {
            ResetChaseCheck();
            return;
        }

        if (m_chaseCheckTimer > diff)
        {
            m_chaseCheckTimer -= diff;
            return;
        }

        m_chaseCheckTimer = 0;
        if (!CanExecuteCombatAction())
            return;

        m_chaseCheckTimer = CHASE_CHECK_INTERVAL;
        Unit* victim = m_creature->GetVictim();
        if (!IsChaseBlocked(victim))
        {
            ClearBlockedTarget();
            return;
        }

        if (m_blockedTargetGuid != victim->GetObjectGuid())
        {
            m_blockedTargetGuid = victim->GetObjectGuid();
            m_blockedChaseChecks = 0;
        }

        // Six observations one second apart span five seconds.
        if (++m_blockedChaseChecks >= BLOCKED_CHASE_CHECKS)
        {
            DoCastSpellIfCan(victim, SPELL_SUMMON_PLAYER);
            m_blockedChaseChecks = 0;
        }
    }

private:
    void ClearBlockedTarget()
    {
        m_blockedTargetGuid.Clear();
        m_blockedChaseChecks = 0;
    }

    void ResetChaseCheck()
    {
        ClearBlockedTarget();
        m_chaseCheckTimer = CHASE_CHECK_INTERVAL;
    }

    bool IsChaseBlocked(Unit* victim) const
    {
        if (!victim || !victim->IsPlayer() || !victim->IsAlive() || !IsCombatMovement() ||
            m_creature->hasUnitState(UNIT_STAT_NOT_MOVE) || m_creature->GetCombatManager().GetEvadeState() != EVADE_NONE ||
            static_cast<Player*>(victim)->IsBeingTeleported())
            return false;

        MotionMaster const* motion = m_creature->GetMotionMaster();
        if (motion->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            return false;

        // A failed initial path can leave the chase reporting reachable until a spline finishes.
        return !motion->GetCurrent()->IsReachable() ||
            (m_creature->IsStopped() && !m_creature->_IsWithinCombatDist(victim, 0.0f, true));
    }

    instance_ruins_of_ahnqiraj* const m_instance;
    ObjectGuid m_blockedTargetGuid;
    uint32 m_chaseCheckTimer = CHASE_CHECK_INTERVAL;
    uint8 m_blockedChaseChecks = 0;
};
}

void AddSC_boss_rajaxx_combat()
{
    Script* script = new Script;
    script->Name = "boss_rajaxx";
    script->GetAI = &GetNewAIInstance<boss_rajaxxAI>;
    script->RegisterSelf();
}
