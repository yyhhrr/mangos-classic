/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"

#include <algorithm>

namespace
{
enum
{
    SPELL_CHAIN_LIGHTNING = 24680,
    SPELL_LIGHTNING_CLOUD = 24683,
};

struct boss_wushoolayAI : public CreatureEventAI
{
    explicit boss_wushoolayAI(Creature* creature) : CreatureEventAI(creature) {}

    void Reset() override
    {
        m_creature->RemoveDynObject(SPELL_LIGHTNING_CLOUD);
        CreatureEventAI::Reset();
    }

    void EnterEvadeMode() override
    {
        m_creature->RemoveDynObject(SPELL_LIGHTNING_CLOUD);
        CreatureEventAI::EnterEvadeMode();
    }

    Unit* GetTargetByType(uint32 target, Unit* actionInvoker, Unit* eventSender, Unit* eventTarget,
        bool& isError, uint32 forSpellId, uint32 selectFlags) const override
    {
        if (forSpellId != SPELL_CHAIN_LIGHTNING || target != TARGET_T_HOSTILE_RANDOM_PLAYER)
            return CreatureEventAI::GetTargetByType(target, actionInvoker, eventSender, eventTarget, isError, forSpellId, selectFlags);

        // Use the current facing before the cast turns Wushoolay toward its first target.
        std::vector<Unit*> players;
        m_creature->SelectAttackingTargets(players, ATTACKING_TARGET_ALL_SUITABLE, 0, forSpellId, selectFlags | SELECT_FLAG_PLAYER);
        std::erase_if(players, [this](Unit* player)
        {
            return !m_creature->HasInArc(player) || !m_creature->CanAttack(player);
        });

        isError = players.empty();
        return isError ? nullptr : players[urand(0, players.size() - 1)];
    }
};
}

void AddSC_boss_wushoolay()
{
    Script* script = new Script;
    script->Name = "boss_wushoolay";
    script->GetAI = &GetNewAIInstance<boss_wushoolayAI>;
    script->RegisterSelf();
}
