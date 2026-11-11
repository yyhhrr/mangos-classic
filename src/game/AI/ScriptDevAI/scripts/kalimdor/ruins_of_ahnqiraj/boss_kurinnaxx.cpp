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
SDName: Boss_Kurinnaxx
SD%Complete: 90
SDComment: Summon Player recovers an unreachable victim
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "MotionGenerators/MovementGenerator.h"

enum
{
    SAY_ENRAGE              = -1000002,

    SPELL_THRASH            = 3391,
    SPELL_WIDE_SLASH        = 25814,
    SPELL_MORTAL_WOUND      = 25646,
    SPELL_SANDTRAP          = 26524,        // summons gameobject 180647
    SPELL_ENRAGE            = 26527,
    SPELL_SUMMON_PLAYER     = 26446,

    GO_SAND_TRAP            = 180647,
};

enum KurinnaxxAction
{
    KURINNAXX_ENRAGE,
    KURINNAXX_MORTAL_WOUND,
    KURINNAXX_SAND_TRAP,
    KURINNAXX_THRASH,
    KURINNAXX_WIDE_SLASH,
    KURINNAXX_SUMMON_PLAYER,
    KURINNAXX_ACTION_MAX,
    KURINNAXX_TRAP_TRIGGER,
};

static constexpr auto MORTAL_WOUND_INTERVAL = 8s;
static constexpr auto SAND_TRAP_INTERVAL = 8s;
static constexpr auto THRASH_INTERVAL = 16s;
static constexpr auto WIDE_SLASH_INTERVAL = 11s;

struct boss_kurinnaxxAI : public CombatAI
{
    boss_kurinnaxxAI(Creature* creature) : CombatAI(creature, KURINNAXX_ACTION_MAX)
    {
        AddTimerlessCombatAction(KURINNAXX_ENRAGE, true);
        AddCombatAction(KURINNAXX_MORTAL_WOUND, MORTAL_WOUND_INTERVAL);
        AddCombatAction(KURINNAXX_SAND_TRAP, SAND_TRAP_INTERVAL);
        AddCombatAction(KURINNAXX_THRASH, THRASH_INTERVAL);
        AddCombatAction(KURINNAXX_WIDE_SLASH, WIDE_SLASH_INTERVAL);
        AddCombatAction(KURINNAXX_SUMMON_PLAYER, 1s);
        AddCustomAction(KURINNAXX_TRAP_TRIGGER, true, [this]() { TriggerTrap(); }, TIMER_COMBAT_COMBAT);
    }

    ObjectGuid m_sandtrapGuid;
    ObjectGuid m_blockedTargetGuid;
    uint8 m_blockedChaseChecks = 0;

    void Reset() override
    {
        CombatAI::Reset();
        RemovePendingTrap();
        m_blockedTargetGuid.Clear();
        m_blockedChaseChecks = 0;
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        RemovePendingTrap();
    }

    void JustSummoned(GameObject* trap) override
    {
        if (trap->GetEntry() != GO_SAND_TRAP)
            return;

        RemovePendingTrap();
        m_sandtrapGuid = trap->GetObjectGuid();
        ResetTimer(KURINNAXX_TRAP_TRIGGER, 4s);
    }

    void RemovePendingTrap()
    {
        DisableTimer(KURINNAXX_TRAP_TRIGGER);
        if (!m_sandtrapGuid)
            return;

        if (GameObject* trap = m_creature->GetMap()->GetGameObject(m_sandtrapGuid))
            trap->ForcedDespawn();
        m_sandtrapGuid.Clear();
    }

    void TriggerTrap()
    {
        GameObject* trap = m_creature->GetMap()->GetGameObject(m_sandtrapGuid);
        m_sandtrapGuid.Clear();
        if (trap)
            trap->Use(m_creature);
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

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case KURINNAXX_ENRAGE:
            {
                if (m_creature->GetHealthPercent() <= 30.0f)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_ENRAGE) == CAST_OK)
                    {
                        DoScriptText(SAY_ENRAGE, m_creature);
                        SetActionReadyStatus(action, false);
                    }
                }
                break;
            }
            case KURINNAXX_MORTAL_WOUND:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MORTAL_WOUND) == CAST_OK)
                    ResetCombatAction(action, MORTAL_WOUND_INTERVAL);
                break;
            }
            case KURINNAXX_SAND_TRAP:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_SANDTRAP) == CAST_OK)
                    ResetCombatAction(action, SAND_TRAP_INTERVAL);
                break;
            }
            case KURINNAXX_THRASH:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_THRASH) == CAST_OK)
                    ResetCombatAction(action, THRASH_INTERVAL);
                break;
            }
            case KURINNAXX_WIDE_SLASH:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_WIDE_SLASH) == CAST_OK)
                    ResetCombatAction(action, WIDE_SLASH_INTERVAL);
                break;
            }
            case KURINNAXX_SUMMON_PLAYER:
            {
                ResetCombatAction(action, 1s);
                Unit* victim = m_creature->GetVictim();
                if (!IsChaseBlocked(victim))
                {
                    m_blockedTargetGuid.Clear();
                    m_blockedChaseChecks = 0;
                    break;
                }

                if (m_blockedTargetGuid != victim->GetObjectGuid())
                {
                    m_blockedTargetGuid = victim->GetObjectGuid();
                    m_blockedChaseChecks = 0;
                }

                // Six observations one second apart span five seconds.
                if (++m_blockedChaseChecks >= 6)
                {
                    DoCastSpellIfCan(victim, SPELL_SUMMON_PLAYER);
                    m_blockedChaseChecks = 0;
                }
                break;
            }
        }
    }
};

void AddSC_boss_kurinnaxx()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_kurinnaxx";
    pNewScript->GetAI = &GetNewAIInstance<boss_kurinnaxxAI>;
    pNewScript->RegisterSelf();
}
