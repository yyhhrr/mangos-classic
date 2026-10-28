/*
 * This file is part of the CMaNGOS Project. See AUTHORS for copyright information.
 * This program is free software licensed under GNU GPL version 2 or later.
 */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"
#include <array>

namespace
{
enum AlzzinPhase
{
    ALZZIN_NORMAL = 1,
    ALZZIN_WOLF = 2,
    ALZZIN_TREE = 3,
    ALZZIN_FINAL = 4,
};

enum AlzzinAction
{
    ALZZIN_WITHER,
    ALZZIN_REGENERATION,
    ALZZIN_WOLF_FORM,
    ALZZIN_TREE_FORM,
    ALZZIN_VICIOUS_BITE,
    ALZZIN_MANGLE,
    ALZZIN_LEAVE_WOLF,
    ALZZIN_DISARM,
    ALZZIN_ENERVATE,
    ALZZIN_KNOCK_AWAY,
    ALZZIN_LEAVE_TREE,
    ALZZIN_ACTION_COUNT,
};

enum AlzzinTarget { ALZZIN_SELF, ALZZIN_VICTIM, ALZZIN_RANDOM_PLAYER, ALZZIN_RANDOM_MANA_PLAYER };
struct AlzzinActionData
{
    uint32 eventId, spell, initialMin, initialMax, repeatMin, repeatMax, phases, castFlags;
    AlzzinTarget target;
};

// The original classic EventAI timers are active only in their matching phase.
// A queued second form must wait for the current form to finish, rather than
// casting both triggered transforms in a single EventAI update batch.
AlzzinActionData const alzzinActions[ALZZIN_ACTION_COUNT] =
{
    {1149202, 22662,     0, 10000,  5000, 20000, (1 << ALZZIN_NORMAL) | (1 << ALZZIN_FINAL), CAST_AURA_NOT_PRESENT, ALZZIN_RANDOM_PLAYER},
    {1149203,  7948,  8000, 15000,  9000, 14000, (1 << ALZZIN_NORMAL) | (1 << ALZZIN_FINAL), CAST_AURA_NOT_PRESENT, ALZZIN_SELF},
    {1149204, 22660, 12000, 15000, 40000, 50000, (1 << ALZZIN_NORMAL), CAST_TRIGGERED | CAST_FORCE_CAST, ALZZIN_SELF},
    {1149205, 22688, 12000, 15000, 40000, 50000, (1 << ALZZIN_NORMAL), CAST_TRIGGERED | CAST_FORCE_CAST, ALZZIN_SELF},
    {1149206, 19319,     0,  5000,  8000, 12000, (1 << ALZZIN_WOLF), 0, ALZZIN_VICTIM},
    {1149207, 22689,     0, 10000,  7000, 13000, (1 << ALZZIN_WOLF), 0, ALZZIN_VICTIM},
    {1149208, 22660, 25000, 35000, 40000, 50000, (1 << ALZZIN_WOLF), 0, ALZZIN_SELF},
    {1149209, 22691,     0,  5000,  9000, 14000, (1 << ALZZIN_TREE), 0, ALZZIN_VICTIM},
    {1149210, 22661,     0, 10000, 11000, 33000, (1 << ALZZIN_TREE), 0, ALZZIN_RANDOM_MANA_PLAYER},
    {1149211, 10101,  8000, 15000, 12000, 16000, (1 << ALZZIN_TREE), 0, ALZZIN_VICTIM},
    {1149212, 22688, 25000, 35000, 40000, 50000, (1 << ALZZIN_TREE), 0, ALZZIN_SELF},
};

uint32 const SPELL_ALZZIN_THORNS = 22128;
uint32 const ALZZIN_ACTION_UPDATE = 500;
}

struct boss_alzzinAI : public ScriptedAI
{
    explicit boss_alzzinAI(Creature* creature) : ScriptedAI(creature),
        m_instance(static_cast<instance_dire_maul*>(creature->GetInstanceData()))
    {
        Reset();
    }

    instance_dire_maul* m_instance;
    std::array<uint32, ALZZIN_ACTION_COUNT> m_timers;
    uint32 m_phase, m_updateTimer, m_eventDiff;
    bool m_thornsPending;

    void Reset() override
    {
        for (uint32 i = 0; i < ALZZIN_ACTION_COUNT; ++i)
            m_timers[i] = urand(alzzinActions[i].initialMin, alzzinActions[i].initialMax);
        // This is a read-only query: creation/Reset can run inside the instance's
        // actor allocation and must not begin a batch or invalidate its ledger.
        m_phase = m_instance && m_instance->East().IsAlzzinWaveStarted() ? ALZZIN_FINAL : ALZZIN_NORMAL;
        m_updateTimer = ALZZIN_ACTION_UPDATE;
        m_eventDiff = 0;
        m_thornsPending = false;
    }

    void Aggro(Unit* /*who*/) override
    {
        // EventAI originally initializes every combat timer on EnterCombat,
        // including when combat is entered without a new creature allocation.
        Reset();
        // Original1149201 interrupts previous channeling and applies Thorns.
        // A failed cast may retry without trapping the whole encounter inphase0.
        m_thornsPending = DoCastSpellIfCan(m_creature, SPELL_ALZZIN_THORNS, CAST_INTERRUPT_PREVIOUS) != CAST_OK &&
            !m_creature->HasAura(SPELL_ALZZIN_THORNS);
    }

    void EnterEvadeMode() override
    {
        if (m_instance)
            m_instance->East().AlzzinEvaded(m_creature);
        ScriptedAI::EnterEvadeMode();
    }

    void JustReachedHome() override
    {
        ScriptedAI::JustReachedHome();
        // Original1149220 resumes path0, including its existing Dark Channeling
        // and random-movement DB scripts. Resume after arriving home.
        m_creature->StopMoving();
        m_creature->GetMotionMaster()->Clear(false, true);
        m_creature->GetMotionMaster()->MoveWaypoint(0);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_instance)
            m_instance->East().AlzzinDied(m_creature);
        // Successful death does not remove the original remaining living adds.
    }

    void EnterFinalPhase(bool beginWave)
    {
        m_creature->RemoveAurasDueToSpell(22660);
        m_creature->RemoveAurasDueToSpell(22688);
        m_phase = ALZZIN_FINAL;
        if (beginWave)
        {
            DoCallForHelp(100.f);
            if (m_instance)
                m_instance->East().BeginAlzzinWave(m_creature);
        }
    }

    Unit* ActionTarget(AlzzinActionData const& action)
    {
        switch (action.target)
        {
            case ALZZIN_SELF: return m_creature;
            case ALZZIN_VICTIM: return m_creature->GetVictim();
            case ALZZIN_RANDOM_PLAYER:
                return m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, action.spell,
                    SELECT_FLAG_PLAYER | SELECT_FLAG_NOT_AURA);
            case ALZZIN_RANDOM_MANA_PLAYER:
                return m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, action.spell,
                    SELECT_FLAG_PLAYER | SELECT_FLAG_POWER_MANA);
        }
        return nullptr;
    }

    bool TryAction(uint32 index)
    {
        AlzzinActionData const& action = alzzinActions[index];
        if (index == ALZZIN_LEAVE_WOLF || index == ALZZIN_LEAVE_TREE)
        {
            m_creature->RemoveAurasDueToSpell(action.spell);
            m_phase = ALZZIN_NORMAL;
            return true;
        }

        Unit* target = ActionTarget(action);
        if (!target || DoCastSpellIfCan(target, action.spell, action.castFlags) != CAST_OK)
            return false;
        if (index == ALZZIN_WOLF_FORM || index == ALZZIN_TREE_FORM)
        {
            // Triggered instant casts already applied their aura on return.
            // Confirm that aura before changing the logical form or its timer.
            if (!m_creature->HasAura(action.spell))
                return false;
            m_creature->RemoveAurasDueToSpell(index == ALZZIN_WOLF_FORM ? 22688 : 22660);
            m_phase = index == ALZZIN_WOLF_FORM ? ALZZIN_WOLF : ALZZIN_TREE;
        }
        return true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_creature->IsAlive() || !m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        m_eventDiff += diff;
        if (m_updateTimer > diff)
            m_updateTimer -= diff;
        else
        {
            uint32 elapsed = m_eventDiff;
            m_eventDiff = 0;
            m_updateTimer = ALZZIN_ACTION_UPDATE;

            if (m_phase != ALZZIN_FINAL)
            {
                if (m_instance && m_instance->East().IsAlzzinWaveStarted())
                    EnterFinalPhase(false);
                // Preserve EVENT_T_HP's original integer percent semantics.
                else if (m_creature->GetMaxHealth() &&
                    uint32(uint64(m_creature->GetHealth()) * 100 / m_creature->GetMaxHealth()) <= 40)
                    EnterFinalPhase(true);
            }

            uint32 phaseAtStart = m_phase;
            for (uint32 i = 0; i < ALZZIN_ACTION_COUNT; ++i)
                if (alzzinActions[i].phases & (1 << phaseAtStart))
                    m_timers[i] = m_timers[i] > elapsed ? m_timers[i] - elapsed : 0;

            if (m_thornsPending && CanExecuteCombatAction())
                if (DoCastSpellIfCan(m_creature, SPELL_ALZZIN_THORNS, CAST_INTERRUPT_PREVIOUS) == CAST_OK ||
                    m_creature->HasAura(SPELL_ALZZIN_THORNS))
                    m_thornsPending = false;

            for (uint32 i = 0; i < ALZZIN_ACTION_COUNT; ++i)
            {
                AlzzinActionData const& action = alzzinActions[i];
                if (m_timers[i] || !(action.phases & (1 << phaseAtStart)) ||
                    !(action.phases & (1 << m_phase)) || !CanExecuteCombatAction())
                    continue;
                if (TryAction(i))
                    m_timers[i] = urand(action.repeatMin, action.repeatMax);
            }
        }
        DoMeleeAttackIfReady();
    }
};

void AddSC_boss_alzzin()
{
    Script* script = new Script;
    script->Name = "boss_alzzin";
    script->GetAI = &GetNewAIInstance<boss_alzzinAI>;
    script->RegisterSelf();
}
