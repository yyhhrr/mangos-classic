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
    SPELL_AVATAR = 24646,
    SPELL_AVATAR_STUN = 24647,
};

struct boss_grilekAI : public CreatureEventAI
{
    explicit boss_grilekAI(Creature* creature) : CreatureEventAI(creature) {}

    void Reset() override
    {
        m_avatarTarget.Clear();
        CreatureEventAI::Reset();
    }

    bool ProcessAction(CreatureEventAI_Action const& action, uint32 rnd, uint32 eventId,
        Unit* actionInvoker, Unit* eventSender, Unit* eventTarget) override
    {
        bool const startsAvatar = action.type == ACTION_T_CAST && action.cast.spellId == SPELL_AVATAR &&
            !m_creature->HasAura(SPELL_AVATAR);
        bool const success = CreatureEventAI::ProcessAction(action, rnd, eventId, actionInvoker, eventSender, eventTarget);
        if (success && startsAvatar && m_creature->HasAura(SPELL_AVATAR))
            PrepareAvatarPursuit();
        return success;
    }

    void UpdateAI(uint32 diff) override
    {
        StartAvatarPursuit();
        CreatureEventAI::UpdateAI(diff);
    }

private:
    void PrepareAvatarPursuit()
    {
        std::vector<Unit*> players;
        m_creature->SelectAttackingTargets(players, ATTACKING_TARGET_ALL_SUITABLE, 0, nullptr, SELECT_FLAG_PLAYER);
        players.erase(std::remove_if(players.begin(), players.end(), [this](Unit* player)
        {
            return !player->IsAlive() || !m_creature->CanAttack(player);
        }), players.end());
        if (players.size() > 1)
            players.erase(std::remove(players.begin(), players.end(), m_creature->GetVictim()), players.end());

        m_avatarTarget = players.empty() ? ObjectGuid() : players[urand(0, uint32(players.size() - 1))]->GetObjectGuid();
        m_creature->CastSpell(m_creature, SPELL_AVATAR_STUN, TRIGGERED_OLD_TRIGGERED);
        m_creature->getThreatManager().modifyAllThreatPercent(-100, true);
    }

    void StartAvatarPursuit()
    {
        if (m_avatarTarget.IsEmpty())
            return;
        if (!m_creature->IsInCombat() || !m_creature->HasAura(SPELL_AVATAR))
        {
            m_avatarTarget.Clear();
            return;
        }
        if (m_creature->HasAura(SPELL_AVATAR_STUN))
            return;

        Player* target = m_creature->GetMap()->GetPlayer(m_avatarTarget);
        m_avatarTarget.Clear();
        ThreatManager& threat = m_creature->getThreatManager();
        if (!target || !target->IsAlive() || !m_creature->CanAttack(target) || !threat.HasThreat(target))
            return;

        // Break a zero-threat tie for the opening chase. Normal threat selection resumes immediately.
        if (threat.getThreat(target) <= 0.0f)
            threat.addThreatDirectly(target, 1.0f, true);
        threat.setCurrentVictimByTarget(target);
        AttackStart(target);
    }

    ObjectGuid m_avatarTarget;
};
}

void AddSC_boss_grilek()
{
    Script* script = new Script;
    script->Name = "boss_grilek";
    script->GetAI = &GetNewAIInstance<boss_grilekAI>;
    script->RegisterSelf();
}
