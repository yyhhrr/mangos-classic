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

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"

enum
{
    QUEST_SUMMONING = 1713,
    QUEST_SPIRIT_TOTEM = 8412,
    NPC_CYCLONIAN = 6239,
    SPELL_SUMMON_CYCLONIAN = 8606,
    SIGNAL_CAST = 617602,
    SIGNAL_HOME = 617601,
    SIGNAL_TOTEM_FINISHED = 8412,
    RETRY_ACTION = GOSSIP_ACTION_INFO_DEF + 1,
    CONFIRM_TIMEOUT = 11000, // Native 8606 cast time is 10000 ms.
    RETRY_DELAY = 1000,
    MAX_ATTEMPTS = 3
};

// EventAI remains the only combat scheduler. This wrapper owns the shared
// quest scene and real Cyclonian GUID; no generic spell effect is duplicated.
struct npc_bathrah_windwatcherAI : public CreatureEventAI
{
    enum Scene { IDLE, SUMMONING, SPIRIT_TOTEM };

    explicit npc_bathrah_windwatcherAI(Creature* creature) : CreatureEventAI(creature),
        m_scene(IDLE), m_confirmTimer(0), m_retryTimer(0), m_sceneTimer(0), m_attempts(0),
        m_atHome(false), m_castPointSeen(false), m_retryPaused(false), m_acceptSummon(false),
        m_totemEnding(false), m_pendingTotem(false), m_activationHeld(false), m_originalActive(false)
    {
        m_creature->GetRespawnCoord(m_homeX, m_homeY, m_homeZ, &m_homeO);
    }

    Scene m_scene;
    ObjectGuid m_cyclonianGuid, m_pendingWarrior, m_totemPlayer;
    GuidSet m_retryTickets;
    uint32 m_confirmTimer, m_retryTimer, m_sceneTimer, m_attempts;
    bool m_atHome, m_castPointSeen, m_retryPaused, m_acceptSummon, m_totemEnding;
    bool m_pendingTotem, m_activationHeld, m_originalActive;
    float m_homeX, m_homeY, m_homeZ, m_homeO;

    bool LivingCyclonian() const
    {
        if (m_cyclonianGuid)
            if (Creature* actor = m_creature->GetMap()->GetCreature(m_cyclonianGuid))
                return actor->IsAlive();
        return false;
    }

    bool NearHome() const { return m_creature->IsWithinDist3d(m_homeX, m_homeY, m_homeZ, 3.0f); }

    void HoldActive()
    {
        if (!m_activationHeld)
        {
            m_originalActive = m_creature->HasActiveObjectState();
            m_activationHeld = true;
        }
        m_creature->SetActiveObjectState(true);
    }

    void ReconcileActor()
    {
        if (m_cyclonianGuid && !LivingCyclonian())
            m_cyclonianGuid.Clear();
        if (m_activationHeld && m_scene == IDLE && !m_cyclonianGuid && !m_pendingWarrior && !m_pendingTotem)
        {
            m_creature->SetActiveObjectState(m_originalActive);
            m_activationHeld = false;
        }
    }

    bool CanRetry(Player* player) const
    {
        return player && player->IsAlive() && m_creature->IsAlive() && !m_creature->IsInCombat() &&
            player->GetQuestStatus(QUEST_SUMMONING) == QUEST_STATUS_INCOMPLETE &&
            m_creature->IsWithinDistInMap(player, INTERACTION_DISTANCE) &&
            m_scene == IDLE && m_atHome && NearHome() && !LivingCyclonian() &&
            !m_pendingWarrior && !m_pendingTotem;
    }

    void RestoreQuestgiver() { m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER); }

    void BeginSummoning(Player* player)
    {
        HoldActive();
        m_scene = SUMMONING;
        m_atHome = false;
        m_castPointSeen = m_retryPaused = m_acceptSummon = false;
        m_confirmTimer = m_retryTimer = m_attempts = 0;
        m_sceneTimer = 180000;
        m_retryTickets.clear();
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->GetMap()->ScriptsStart(SCRIPT_TYPE_QUEST_START, QUEST_SUMMONING,
            m_creature, player, Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE);
    }

    void StartQueuedScene()
    {
        if (m_scene != IDLE || !m_atHome || !NearHome() || !m_creature->IsAlive() || m_creature->IsInCombat())
            return;
        if (m_pendingTotem)
        {
            Player* player = m_creature->GetMap()->GetPlayer(m_totemPlayer);
            m_pendingTotem = false;
            m_totemPlayer.Clear();
            // The reward was already committed. The shared visual scene can
            // run without that player remaining online, using Bath'rah as target.
            HoldActive();
            m_scene = SPIRIT_TOTEM;
            m_totemEnding = false;
            m_sceneTimer = 60000;
            m_retryTickets.clear();
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->GetMap()->ScriptsStart(SCRIPT_TYPE_QUEST_END, QUEST_SPIRIT_TOTEM,
                m_creature, player ? static_cast<WorldObject*>(player) : static_cast<WorldObject*>(m_creature),
                Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE);
            return;
        }
        if (m_pendingWarrior)
        {
            Player* player = m_creature->GetMap()->GetPlayer(m_pendingWarrior);
            m_pendingWarrior.Clear();
            if (player && player->GetQuestStatus(QUEST_SUMMONING) == QUEST_STATUS_INCOMPLETE && !LivingCyclonian())
                BeginSummoning(player);
        }
        ReconcileActor();
    }

    void RequestSummoning(Player* player)
    {
        ReconcileActor();
        if (!player || player->GetQuestStatus(QUEST_SUMMONING) != QUEST_STATUS_INCOMPLETE || LivingCyclonian())
            return;
        if (m_scene == SUMMONING)
            return; // Other eligible warriors share this admitted actor.
        HoldActive();
        if (!m_pendingWarrior)
            m_pendingWarrior = player->GetObjectGuid();
        StartQueuedScene();
    }

    void RequestTotem(Player* player)
    {
        if (m_scene == SPIRIT_TOTEM)
            return; // One shared presentation; every reward remains committed.
        HoldActive();
        m_pendingTotem = true;
        if (player)
            m_totemPlayer = player->GetObjectGuid();
        StartQueuedScene();
    }

    void StopAttempt()
    {
        m_creature->InterruptNonMeleeSpells(false, SPELL_SUMMON_CYCLONIAN);
        m_confirmTimer = m_retryTimer = 0;
        m_acceptSummon = false;
    }

    void ReleaseRetryPause()
    {
        if (m_retryPaused)
        {
            m_retryPaused = false;
            m_creature->GetMotionMaster()->UnpauseWaypoints();
        }
    }

    void FailedAttempt()
    {
        StopAttempt(); // A timed-out old cast cannot overlap the next attempt.
        if (m_attempts < MAX_ATTEMPTS)
        {
            m_retryPaused = true;
            m_creature->GetMotionMaster()->PauseWaypoints(0);
            m_retryTimer = RETRY_DELAY;
        }
        else
            ReleaseRetryPause(); // Return normally; the menu enables a fresh request.
    }

    void TrySummon()
    {
        if (m_scene != SUMMONING || LivingCyclonian() || m_confirmTimer)
            return;
        m_retryTimer = 0;
        ++m_attempts;
        m_acceptSummon = true;
        m_confirmTimer = CONFIRM_TIMEOUT; // Set before Cast: callbacks may be synchronous.
        if (DoCastSpellIfCan(m_creature, SPELL_SUMMON_CYCLONIAN) != CAST_OK && !LivingCyclonian())
            FailedAttempt();
    }

    void FinishScene()
    {
        StopAttempt();
        ReleaseRetryPause();
        m_scene = IDLE;
        m_sceneTimer = 0;
        m_totemEnding = false;
        RestoreQuestgiver();
        ReconcileActor();
    }

    void RecoverHome()
    {
        // Only an abnormal, bounded scene timeout uses the known spawn origin.
        // Never despawn a pulled-away Cyclonian or clear his ownership here.
        FinishScene();
        m_atHome = false;
        m_creature->NearTeleportTo(m_homeX, m_homeY, m_homeZ, m_homeO);
        m_creature->GetMotionMaster()->UnpauseWaypoints();
        m_creature->GetMotionMaster()->Initialize();
    }

    void ReceiveAIEvent(AIEventType type, Unit* sender, Unit* invoker, uint32 value) override
    {
        CreatureEventAI::ReceiveAIEvent(type, sender, invoker, value);
        if (sender != m_creature)
            return;
        if (type == AI_EVENT_CUSTOM_A && value == SIGNAL_CAST && m_scene == SUMMONING && !m_castPointSeen)
        {
            m_castPointSeen = true;
            TrySummon(); // Exactly the original point8 + 2000ms DB callback.
        }
        else if (type == AI_EVENT_CUSTOM_B && value == SIGNAL_HOME)
        {
            m_atHome = true;
            if (m_scene == SUMMONING && !m_confirmTimer && !m_retryTimer)
                FinishScene();
            if (m_scene == IDLE)
                RestoreQuestgiver();
        }
        else if (type == AI_EVENT_CUSTOM_C && value == SIGNAL_TOTEM_FINISHED && m_scene == SPIRIT_TOTEM)
            m_totemEnding = true; // Finish next update, after all same-tick DB rows.
    }

    void JustSummoned(Creature* actor) override
    {
        CreatureEventAI::JustSummoned(actor);
        if (actor->GetEntry() != NPC_CYCLONIAN || actor->GetMap() != m_creature->GetMap() ||
            actor->GetSpawnerGuid() != m_creature->GetObjectGuid() ||
            m_scene != SUMMONING || !m_acceptSummon || LivingCyclonian())
            return;
        m_cyclonianGuid = actor->GetObjectGuid();
        m_confirmTimer = m_retryTimer = 0;
        m_acceptSummon = false;
        ReleaseRetryPause();
    }

    void SummonedCreatureJustDied(Creature* actor) override
    {
        CreatureEventAI::SummonedCreatureJustDied(actor);
        if (actor->GetObjectGuid() == m_cyclonianGuid)
            m_cyclonianGuid.Clear();
        ReconcileActor();
    }

    void SummonedCreatureDespawn(Creature* actor) override
    {
        CreatureEventAI::SummonedCreatureDespawn(actor);
        if (actor->GetObjectGuid() == m_cyclonianGuid)
            m_cyclonianGuid.Clear();
        ReconcileActor();
    }

    void Reset() override
    {
        CreatureEventAI::Reset();
        // Evade/reset must not forget a living actor or admit a second cast.
        ReconcileActor();
    }

    void JustRespawned() override
    {
        CreatureEventAI::JustRespawned();
        StopAttempt();
        m_scene = IDLE;
        m_sceneTimer = 0;
        m_atHome = m_totemEnding = m_pendingTotem = false;
        m_pendingWarrior.Clear();
        m_totemPlayer.Clear();
        m_retryTickets.clear();
        m_retryPaused = false;
        ReconcileActor();
        if (LivingCyclonian())
            HoldActive();
    }

    void UpdateAI(uint32 diff) override
    {
        CreatureEventAI::UpdateAI(diff);
        ReconcileActor();
        if (!m_creature->IsAlive())
            return;
        if (m_scene != IDLE)
        {
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            if (m_sceneTimer <= diff)
            {
                RecoverHome();
                return;
            }
            m_sceneTimer -= diff;
        }
        if (m_confirmTimer)
        {
            if (m_confirmTimer <= diff)
                FailedAttempt();
            else
                m_confirmTimer -= diff;
        }
        else if (m_retryTimer)
        {
            if (m_retryTimer <= diff)
                TrySummon();
            else
                m_retryTimer -= diff;
        }
        if (m_scene == SPIRIT_TOTEM && m_totemEnding && NearHome())
            FinishScene();
        if (m_scene == SUMMONING && m_atHome && !m_confirmTimer && !m_retryTimer)
            FinishScene();
        StartQueuedScene();
    }
};

UnitAI* GetAI_npc_bathrah_windwatcher(Creature* creature) { return new npc_bathrah_windwatcherAI(creature); }

bool QuestAccept_npc_bathrah_windwatcher(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_SUMMONING)
        if (auto* ai = dynamic_cast<npc_bathrah_windwatcherAI*>(creature->AI()))
            ai->RequestSummoning(player);
    return true;
}

bool QuestRewarded_npc_bathrah_windwatcher(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() != QUEST_SPIRIT_TOTEM)
        return false;
    if (auto* ai = dynamic_cast<npc_bathrah_windwatcherAI*>(creature->AI()))
        ai->RequestTotem(player);
    return true; // Only this hook starts 8412; the already granted reward is unchanged.
}

bool GossipHello_npc_bathrah_windwatcher(Player* player, Creature* creature)
{
    auto* ai = dynamic_cast<npc_bathrah_windwatcherAI*>(creature->AI());
    if (ai)
    {
        ai->m_retryTickets.erase(player->GetObjectGuid());
        if (ai->m_scene == npc_bathrah_windwatcherAI::IDLE && creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER))
            player->PrepareQuestMenu(creature->GetObjectGuid());
        if (ai->CanRetry(player))
        {
            ai->m_retryTickets.insert(player->GetObjectGuid());
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "Please summon Cyclonian again.", GOSSIP_SENDER_MAIN, RETRY_ACTION);
        }
    }
    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_bathrah_windwatcher(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    player->CLOSE_GOSSIP_MENU();
    auto* ai = dynamic_cast<npc_bathrah_windwatcherAI*>(creature->AI());
    if (ai && sender == GOSSIP_SENDER_MAIN && action == RETRY_ACTION &&
        ai->m_retryTickets.erase(player->GetObjectGuid()) && ai->CanRetry(player))
        ai->RequestSummoning(player);
    return true;
}

void AddSC_alterac_mountains()
{
    Script* script = new Script;
    script->Name = "npc_bathrah_windwatcher";
    script->GetAI = &GetAI_npc_bathrah_windwatcher;
    script->pQuestAcceptNPC = &QuestAccept_npc_bathrah_windwatcher;
    script->pQuestRewardedNPC = &QuestRewarded_npc_bathrah_windwatcher;
    script->pGossipHello = &GossipHello_npc_bathrah_windwatcher;
    script->pGossipSelect = &GossipSelect_npc_bathrah_windwatcher;
    script->RegisterSelf();
}
