/*
 * This file is part of the CMaNGOS Project. See AUTHORS for copyright information.
 * This program is free software licensed under GNU GPL version 2 or later.
 */

#include "AI/ScriptDevAI/include/sc_common.h"
#include <array>

namespace
{
enum KalendrisStage { KALENDRIS_NORMAL, KALENDRIS_SHADOW, KALENDRIS_FINAL };
enum KalendrisTarget { KALENDRIS_VICTIM, KALENDRIS_RANDOM_PLAYER, KALENDRIS_RANDOM_NOT_TOP_PLAYER };
enum KalendrisMode { KALENDRIS_ANY_MODE, KALENDRIS_RANGED_MODE, KALENDRIS_MELEE_MODE };

struct KalendrisAction
{
    uint32 eventId, spell, initialMin, initialMax, repeatMin, repeatMax, castFlags;
    KalendrisTarget target;
    KalendrisMode mode;
};

// Preserve the five original combat timers, including both independent Mind
// Blast timers. EventAI decrements both even when only one mode may execute.
KalendrisAction const kalendrisActions[] =
{
    {1148704, 22919, 14000, 32400, 13300, 27200, CAST_PLAYER_ONLY, KALENDRIS_RANDOM_PLAYER, KALENDRIS_ANY_MODE},
    {1148705, 17146,  9900,  9900,  7100, 13200, CAST_PLAYER_ONLY | CAST_AURA_NOT_PRESENT, KALENDRIS_RANDOM_PLAYER, KALENDRIS_ANY_MODE},
    {1148706,  7645,  6000, 18000, 12000, 15000, CAST_PLAYER_ONLY | CAST_AURA_NOT_PRESENT, KALENDRIS_RANDOM_NOT_TOP_PLAYER, KALENDRIS_ANY_MODE},
    {1148707, 17287,     0,  1000,  1500,  3000, CAST_MAIN_SPELL, KALENDRIS_VICTIM, KALENDRIS_RANGED_MODE},
    {1148708, 17287,     0,  1000,  5000, 10000, CAST_MAIN_SPELL, KALENDRIS_VICTIM, KALENDRIS_MELEE_MODE},
};
uint32 const KALENDRIS_ACTIONS = sizeof(kalendrisActions) / sizeof(kalendrisActions[0]);
uint32 const SPELL_KALENDRIS_SHADOWFORM = 22917;
uint32 const KALENDRIS_EVENT_UPDATE = 500;
}

struct boss_kalendrisAI : public ScriptedAI
{
    explicit boss_kalendrisAI(Creature* creature) : ScriptedAI(creature)
    {
        Reset();
    }

    std::array<uint32, KALENDRIS_ACTIONS> m_timers;
    uint32 m_stage, m_updateTimer, m_eventDiff;

    void Reset() override
    {
        ScriptedAI::Reset();
        // AIM_Initialize clears main spells through the nonvirtual UnitAI
        // SpellListChanged, then dispatches JustRespawned -> virtual Reset.
        // Register here so startup, respawn, evade and Aggro all retain the
        // original main spell used by native proximity/cast-result handling.
        AddMainSpell(17287);
        m_creature->RemoveAurasDueToSpell(SPELL_KALENDRIS_SHADOWFORM);
        m_stage = KALENDRIS_NORMAL;
        m_updateTimer = KALENDRIS_EVENT_UPDATE;
        m_eventDiff = 0;
        for (uint32 i = 0; i < KALENDRIS_ACTIONS; ++i)
            m_timers[i] = urand(kalendrisActions[i].initialMin, kalendrisActions[i].initialMax);
    }

    void Aggro(Unit* /*who*/) override
    {
        Reset();
        // Original1148701: TYPE_PROXIMITY, 25 yards. UnitAI::UpdateAI retains
        // the native range/melee transitions and movement rather than copying it.
        SetRangedMode(true, 25.f, TYPE_PROXIMITY);
    }

    Unit* SelectActionTarget(KalendrisAction const& action)
    {
        if (action.target == KALENDRIS_VICTIM)
            return m_creature->GetVictim();
        uint32 flags = SELECT_FLAG_PLAYER;
        if (action.castFlags & CAST_AURA_NOT_PRESENT)
            flags |= SELECT_FLAG_NOT_AURA;
        return m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM,
            action.target == KALENDRIS_RANDOM_NOT_TOP_PLAYER ? 1 : 0, action.spell, flags);
    }

    void UpdateEventTimers(uint32 diff) override
    {
        // Keep the native EventAI batch boundary and accumulated elapsed time.
        // Ordinary combat actions still run through UnitAI::UpdateAI, including
        // its original proximity-mode movement, spell-result handling and melee.
        m_eventDiff += diff;
        if (m_updateTimer >= diff)
        {
            m_updateTimer -= diff;
            return;
        }
        uint32 elapsed = m_eventDiff;
        m_eventDiff = 0;
        m_updateTimer = KALENDRIS_EVENT_UPDATE;
        for (uint32& timer : m_timers)
            timer = timer > elapsed ? timer - elapsed : 0;

        if (!m_creature->IsAlive() || !m_creature->IsInCombat() || !m_creature->GetVictim())
            return;

        if (CanExecuteCombatAction() && m_stage != KALENDRIS_FINAL && m_creature->GetMaxHealth())
        {
            uint32 percent = uint32(uint64(m_creature->GetHealth()) * 100 / m_creature->GetMaxHealth());
            // Final HP wins when damage crosses both thresholds in one batch.
            // Once reached, healing cannot put Shadowform back onto the boss.
            if (percent <= 30)
            {
                m_creature->RemoveAurasDueToSpell(SPELL_KALENDRIS_SHADOWFORM);
                m_stage = KALENDRIS_FINAL;
            }
            else if (m_stage == KALENDRIS_NORMAL && percent <= 60 &&
                DoCastSpellIfCan(m_creature, SPELL_KALENDRIS_SHADOWFORM, CAST_TRIGGERED | CAST_FORCE_CAST) == CAST_OK)
                m_stage = KALENDRIS_SHADOW;
        }

        // EventAI queues eligible mode events before it executes their casts.
        // A ranged Mind Blast failing OOM must not also fire its melee row in
        // the same batch when the native cast-result handler switches modes.
        bool rangedAtStart = GetCurrentRangedMode();
        for (uint32 i = 0; i < KALENDRIS_ACTIONS; ++i)
        {
            KalendrisAction const& action = kalendrisActions[i];
            if (m_timers[i] || !CanExecuteCombatAction() ||
                (action.mode == KALENDRIS_RANGED_MODE && !rangedAtStart) ||
                (action.mode == KALENDRIS_MELEE_MODE && rangedAtStart))
                continue;
            Unit* target = SelectActionTarget(action);
            if (target && DoCastSpellIfCan(target, action.spell, action.castFlags) == CAST_OK)
                m_timers[i] = urand(action.repeatMin, action.repeatMax);
        }
    }
};

void AddSC_boss_kalendris()
{
    Script* script = new Script;
    script->Name = "boss_kalendris";
    script->GetAI = &GetNewAIInstance<boss_kalendrisAI>;
    script->RegisterSelf();
}
