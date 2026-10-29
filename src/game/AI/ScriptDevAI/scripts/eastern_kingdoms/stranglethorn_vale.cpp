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
SDName: Stranglethorn_Vale
SD%Complete: 100
SDComment: Quest support: 215, 349, 592, 3625
SDCategory: Stranglethorn Vale
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/TemporarySpawn.h"
#include "Entities/GossipDef.h"
#include "Server/WorldSession.h"
#include "Maps/SpawnGroup.h"
#include "Maps/SpawnManager.h"
#include "world_eastern_kingdoms.h"

/* ContentData
npc_private_thorsen
mob_yenniku
mob_colonel_kurzen
npc_galvan_the_ancient
npc_witch_doctor_unbagwa
EndContentData */

/*######
## mob_yenniku
######*/

enum
{
    SPELL_YENNIKUS_RELEASE      = 3607,

    QUEST_ID_SAVING_YENNIKU     = 592,

    FACTION_ID_HORDE_GENERIC    = 83,                       // Note: faction may not be correct!
};

struct mob_yennikuAI : public ScriptedAI
{
    mob_yennikuAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiResetTimer;

    void Reset() override { m_uiResetTimer = 0; }

    void SpellHit(Unit* pCaster, const SpellEntry* pSpell) override
    {
        if (pSpell->Id == SPELL_YENNIKUS_RELEASE && pCaster->GetTypeId() == TYPEID_PLAYER)
        {
            if (!m_uiResetTimer && ((Player*)pCaster)->GetQuestStatus(QUEST_ID_SAVING_YENNIKU) == QUEST_STATUS_INCOMPLETE)
            {
                m_uiResetTimer = 60000;
                EnterEvadeMode();
            }
        }
    }

    void EnterEvadeMode() override
    {
        if (m_uiResetTimer)
        {
            m_creature->RemoveAllAurasOnEvade();
            m_creature->CombatStop(true);
            m_creature->LoadCreatureAddon(true);

            m_creature->SetLootRecipient(nullptr);

            m_creature->HandleEmote(EMOTE_STATE_STUN);
            m_creature->SetFactionTemporary(FACTION_ID_HORDE_GENERIC, TEMPFACTION_RESTORE_REACH_HOME);
        }
        else
            ScriptedAI::EnterEvadeMode();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_uiResetTimer)
        {
            if (m_uiResetTimer <= uiDiff)
            {
                m_creature->HandleEmote(EMOTE_STATE_NONE);
                m_uiResetTimer = 0;
                EnterEvadeMode();
            }
            else
                m_uiResetTimer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_mob_yenniku(Creature* _Creature)
{
    return new mob_yennikuAI(_Creature);
}

enum
{
    SPELL_SMOKE_BOMB = 8817,
    SPELL_GARROTE = 8818,
    SPELL_STEALTH = 8822 // spell that get applied to npc after using smoke bomb
};

enum KurzenActions
{
    KURZEN_ACTION_VANISH,
    KURZEN_ACTION_GAROTTE,
    KURZEN_ACTION_MAX,
};

struct mob_colonel_kurzenAI : public CombatAI
{
    mob_colonel_kurzenAI(Creature* creature) : CombatAI(creature, KURZEN_ACTION_MAX)
    {
        AddCombatAction(KURZEN_ACTION_VANISH, urand(6000, 12000));
        AddCombatAction(KURZEN_ACTION_GAROTTE, true);
    }

    uint32 m_castFailures = 0;

    void ClearVanish()
    {
        m_creature->RemoveAurasDueToSpell(SPELL_STEALTH);
        m_creature->SetCombatOnlyStealth(false);
        m_castFailures = 0;
    }

    void Reset() override
    {
        CombatAI::Reset();
        ClearVanish();
    }

    void JustDied(Unit* killer) override
    {
        ClearVanish();
        CombatAI::JustDied(killer);
    }

    void FinishVanish()
    {
        ClearVanish();
        ResetCombatAction(KURZEN_ACTION_VANISH, urand(6000, 12000));
        DisableCombatAction(KURZEN_ACTION_GAROTTE);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case KURZEN_ACTION_VANISH:
            {
                m_creature->SetCombatOnlyStealth(true); // must be before smoke bomb
                if (DoCastSpellIfCan(nullptr, SPELL_SMOKE_BOMB) == CAST_OK)
                {
                    m_castFailures = 0;
                    ResetCombatAction(KURZEN_ACTION_GAROTTE, urand(2000, 6000));
                    DisableCombatAction(action);
                }
                else
                {
                    m_creature->SetCombatOnlyStealth(false);
                    if (++m_castFailures >= 3)
                        FinishVanish();
                    else
                        ResetCombatAction(action, 1000);
                }
                break;
            }
            case KURZEN_ACTION_GAROTTE:
            {
                if (!m_creature->HasAura(SPELL_STEALTH) ||
                    DoCastSpellIfCan(m_creature->GetVictim(), SPELL_GARROTE) == CAST_OK || ++m_castFailures >= 3)
                    FinishVanish();
                else
                    ResetCombatAction(action, 1000);
                break;
            }
        }
    }
};

/* Each acceptance owns its completion; the NPC only shares the presentation. */
struct npc_galvan_the_ancientAI : public ScriptedAI
{
    explicit npc_galvan_the_ancientAI(Creature* creature) : ScriptedAI(creature) {}

    enum { QUEST_WEAPONRY = 3625, QUEST_EXPERT = 2765, SAY_WEAPON_READY = 4542 };
    std::map<ObjectGuid, uint32> m_players;
    uint32 m_otherScene = 0;
    uint32 m_timeout = 0;
    uint32 m_originalEmote = 0;
    bool m_forging = false;
    bool m_holdingActive = false;
    bool m_originalActive = false;

    void EndPresentation()
    {
        if (m_forging)
            m_creature->HandleEmote(m_originalEmote);
        m_forging = false;
    }

    void Finish()
    {
        EndPresentation();
        m_players.clear();
        m_timeout = 0;
        if (m_holdingActive)
            m_creature->SetActiveObjectState(m_originalActive);
        m_holdingActive = false;
    }

    void Reset() override { Finish(); }
    void JustDied(Unit* /*killer*/) override { Finish(); }
    void JustRespawned() override { Finish(); m_otherScene = 0; }

    void OtherSceneStarted()
    {
        // 2765 retains its original 54-second DB movement/completion script.
        // Pause our presentation before that script starts changing emotes.
        EndPresentation();
        m_otherScene = 55000;
        for (auto& player : m_players)
            player.second = 5000;
    }

    void Request(Player* player)
    {
        if (!m_creature->IsAlive() || !player || player->GetMap() != m_creature->GetMap() ||
            player->GetQuestStatus(QUEST_WEAPONRY) != QUEST_STATUS_INCOMPLETE)
            return;
        if (m_players.count(player->GetObjectGuid()))
            return; // Reopening gossip must not reset an existing timer.
        if (!m_holdingActive)
        {
            m_originalActive = m_creature->HasActiveObjectState();
            m_creature->SetActiveObjectState(true);
            m_holdingActive = true;
            m_timeout = 120000;
        }
        m_players.emplace(player->GetObjectGuid(), 5000);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_otherScene)
            m_otherScene = m_otherScene > diff ? m_otherScene - diff : 0;
        if (!m_players.empty())
        {
            if (!m_creature->IsAlive() || m_timeout <= diff)
                Finish();
            else
            {
                m_timeout -= diff;
                if (!m_otherScene && !m_creature->IsInCombat())
                {
                    if (!m_forging)
                    {
                        m_originalEmote = m_creature->GetUInt32Value(UNIT_NPC_EMOTESTATE);
                        m_creature->HandleEmote(EMOTE_STATE_WORK);
                        m_forging = true;
                    }
                    else
                    {
                        bool completed = false;
                        for (auto itr = m_players.begin(); itr != m_players.end();)
                        {
                            Player* player = m_creature->GetMap()->GetPlayer(itr->first);
                            if (!player || player->GetQuestStatus(QUEST_WEAPONRY) != QUEST_STATUS_INCOMPLETE)
                                itr = m_players.erase(itr);
                            else if (itr->second <= diff)
                            {
                                player->AreaExploredOrEventHappens(QUEST_WEAPONRY);
                                completed = true;
                                itr = m_players.erase(itr);
                            }
                            else
                            {
                                itr->second -= diff;
                                ++itr;
                            }
                        }
                        if (completed)
                            DoBroadcastText(SAY_WEAPON_READY, m_creature);
                        if (m_players.empty())
                            Finish();
                    }
                }
            }
        }
        if (m_creature->SelectHostileTarget() && m_creature->GetVictim())
            DoMeleeAttackIfReady();
    }
};

bool QuestAccept_npc_galvan_the_ancient(Player* player, Creature* creature, Quest const* quest)
{
    if (auto* ai = dynamic_cast<npc_galvan_the_ancientAI*>(creature->AI()))
    {
        if (quest->GetQuestId() == npc_galvan_the_ancientAI::QUEST_WEAPONRY)
            ai->Request(player);
        else if (quest->GetQuestId() == npc_galvan_the_ancientAI::QUEST_EXPERT)
            ai->OtherSceneStarted();
    }
    return false;
}

bool GossipHello_npc_galvan_the_ancient(Player* player, Creature* creature)
{
    // An interrupted/offline acceptance can be resumed without abandoning it.
    if (auto* ai = dynamic_cast<npc_galvan_the_ancientAI*>(creature->AI()))
        ai->Request(player);
    player->PrepareQuestMenu(creature->GetObjectGuid());
    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetObjectGuid());
    return true;
}

/* Keep the original spawn groups and paths, but count real deaths, not removal. */
struct npc_witch_doctor_unbagwaAI : public ScriptedAI
{
    explicit npc_witch_doctor_unbagwaAI(Creature* creature) : ScriptedAI(creature) {}

    struct Attacker { bool dead; bool originalActive; };
    std::map<ObjectGuid, Attacker> m_attackers;
    uint32 m_wave = 0;
    uint32 m_delay = 0;
    uint32 m_timeout = 0;
    uint32 m_spawnTimeout = 0;
    uint32 m_castAttempts = 0;
    uint32 m_originalFaction = 0;
    uint32 m_originalFactionFlags = 0;
    bool m_running = false;
    bool m_channeling = false;
    bool m_rosterComplete = false;
    bool m_originalActive = false;
    bool m_originalQuestgiver = false;

    CreatureGroup* Group(uint32 wave) const
    {
        SpawnGroup* group = m_creature->GetMap()->GetSpawnManager().GetSpawnGroup(STV_FEVER_GROUP_01 + wave - 1);
        return group ? group->GetCreatureGroup() : nullptr;
    }

    void ReleaseAttackers()
    {
        for (auto const& attacker : m_attackers)
            if (Creature* actor = m_creature->GetMap()->GetCreature(attacker.first))
                actor->SetActiveObjectState(attacker.second.originalActive);
        m_attackers.clear();
    }

    void Finish(bool success)
    {
        if (!m_running)
            return;
        m_running = false; // Cleanup can synchronously invoke map hooks.
        ReleaseAttackers();
        for (uint32 wave = 1; wave <= 3; ++wave)
        {
            m_creature->GetMap()->GetVariableManager().SetVariable(STV_FEVER_GROUP_01 + wave - 1, 0);
            if (!success)
                if (CreatureGroup* group = Group(wave))
                    group->Despawn();
        }
        m_creature->InterruptNonMeleeSpells(false, 12380);
        m_creature->RemoveAurasDueToSpell(12380);
        m_creature->SetFactionTemporary(m_originalFaction, m_originalFactionFlags);
        if (m_originalQuestgiver)
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        else
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetActiveObjectState(m_originalActive);
        m_wave = m_delay = m_timeout = m_spawnTimeout = 0;
        m_rosterComplete = m_channeling = false;
    }

    void Begin()
    {
        if (m_running || !m_creature->IsAlive())
            return;
        m_originalActive = m_creature->HasActiveObjectState();
        m_originalQuestgiver = m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_originalFaction = m_creature->GetFaction();
        m_originalFactionFlags = m_creature->GetTemporaryFactionFlags();
        m_running = true;
        m_channeling = m_rosterComplete = false;
        m_wave = m_castAttempts = 0;
        m_delay = 5000;
        m_timeout = 600000;
        m_creature->SetActiveObjectState(true);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetFactionTemporary(495, TEMPFACTION_RESTORE_RESPAWN);
        // Drop leftover live actors before admitting a new round. Dead loot
        // corpses have already left their groups and are deliberately retained.
        for (uint32 wave = 1; wave <= 3; ++wave)
        {
            m_creature->GetMap()->GetVariableManager().SetVariable(STV_FEVER_GROUP_01 + wave - 1, 0);
            if (CreatureGroup* group = Group(wave))
                group->Despawn();
        }
    }

    void CollectAttackers()
    {
        CreatureGroup* group = Group(m_wave);
        if (!group)
            return;
        for (auto const& slot : group->GetGroupEntry().DbGuids)
            if (Creature* actor = m_creature->GetMap()->GetCreature(slot.DbGuid))
                if (actor->GetCreatureGroup() == group && actor->IsAlive() && !m_attackers.count(actor->GetObjectGuid()))
                {
                    m_attackers.emplace(actor->GetObjectGuid(), Attacker{false, actor->HasActiveObjectState()});
                    actor->SetActiveObjectState(true);
                }
        m_rosterComplete = m_attackers.size() == group->GetGroupEntry().DbGuids.size();
    }

    void SpawnWave()
    {
        ReleaseAttackers();
        ++m_wave;
        CreatureGroup* group = Group(m_wave);
        uint32 const counts[] = {3, 5, 6};
        if (!group || group->GetGroupEntry().DbGuids.size() != counts[m_wave - 1])
        {
            Finish(false);
            return;
        }
        m_rosterComplete = false;
        m_spawnTimeout = 5000;
        m_delay = 180000; // Bounded recovery even if attackers remain in combat.
        m_creature->GetMap()->GetVariableManager().SetVariable(group->GetGroupId(), 1);
        group->Spawn(true, true);
        m_creature->GetMap()->GetVariableManager().SetVariable(group->GetGroupId(), 0);
        CollectAttackers();
    }

    void ReceiveAIEvent(AIEventType type, Unit* sender, Unit* /*invoker*/, uint32 /*misc*/) override
    {
        if (!m_running || !m_wave || type != AI_EVENT_CUSTOM_A || !sender || sender->IsAlive())
            return;
        auto itr = m_attackers.find(sender->GetObjectGuid());
        if (itr != m_attackers.end())
            itr->second.dead = true; // Only the map's actual death hook sends this.
    }

    void JustDied(Unit* /*killer*/) override { Finish(false); }
    void JustRespawned() override { Finish(false); }
    void Reset() override {} // Ordinary inter-wave combat evades do not abort the ritual.

    void UpdateAI(uint32 diff) override
    {
        if (m_running)
        {
            if (!m_creature->IsAlive() || m_timeout <= diff)
                Finish(false);
            else
            {
                m_timeout -= diff;
                if (!m_wave)
                {
                    if (m_delay > diff)
                        m_delay -= diff;
                    else if (m_channeling)
                        SpawnWave();
                    else if (DoCastSpellIfCan(m_creature, 12380) == CAST_OK)
                    {
                        m_channeling = true;
                        m_delay = 1000;
                    }
                    else if (++m_castAttempts >= 3)
                        Finish(false);
                    else
                        m_delay = 1000;
                }
                else
                {
                    if (!m_rosterComplete)
                        CollectAttackers();
                    bool allDead = m_rosterComplete;
                    bool lost = false;
                    for (auto const& attacker : m_attackers)
                    {
                        if (attacker.second.dead)
                            continue;
                        allDead = false;
                        Creature* actor = m_creature->GetMap()->GetCreature(attacker.first);
                        if (!actor || !actor->IsAlive())
                            lost = true;
                    }
                    if (lost || m_delay <= diff || (!m_rosterComplete && m_spawnTimeout <= diff))
                        Finish(false);
                    else if (allDead)
                    {
                        if (m_wave == 3)
                            Finish(true);
                        else
                            SpawnWave();
                    }
                    else
                    {
                        m_delay -= diff;
                        if (!m_rosterComplete)
                            m_spawnTimeout -= diff;
                    }
                }
            }
        }
        if (m_creature->SelectHostileTarget() && m_creature->GetVictim())
            DoMeleeAttackIfReady();
    }
};

bool QuestRewarded_npc_witch_doctor_unbagwa(Player* /*player*/, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() != 349)
        return false;
    if (auto* ai = dynamic_cast<npc_witch_doctor_unbagwaAI*>(creature->AI()))
        ai->Begin();
    return true;
}

/*######
## npc_private_thorsen
######*/

enum ThorsenRescueConstants
{
    QUEST_JUNGLE_SECRETS       = 215,
    NPC_THORSEN_KURZEN_AGENT   = 775,
    SAY_THORSEN_REJOIN         = 23,
    SAY_THORSEN_RESCUED        = 100,
    SAY_THORSEN_RETURN         = 105,
    THORSEN_RESCUE_FACTION     = 42,
    THORSEN_AGENT_COUNT        = 2,
    THORSEN_RESCUE_TIMEOUT     = 10 * MINUTE * IN_MILLISECONDS,
    THORSEN_QUEST_WINDOW       = 3 * MINUTE * IN_MILLISECONDS,
    THORSEN_AGENT_LIFETIME     = 5 * MINUTE * IN_MILLISECONDS,
};

struct npc_private_thorsenAI : public CreatureEventAI
{
    explicit npc_private_thorsenAI(Creature* creature) : CreatureEventAI(creature) { Reset(); }

    ObjectGuid m_agentGuids[THORSEN_AGENT_COUNT];
    bool m_agentDead[THORSEN_AGENT_COUNT] = { false, false };
    uint32 m_summoningSlot = THORSEN_AGENT_COUNT;
    bool m_rescueActive = false;
    bool m_summonsComplete = false;
    bool m_rescueSucceeded = false;
    uint32 m_rescueTimer = 0;
    uint32 m_questTimer = 0;
    uint32 m_returnTextTimer = 0;
    bool m_lastCanOfferQuest = false;

    // Evade resets combat AI too: do not discard this patrol's rescue roster.
    void Reset() override { CreatureEventAI::Reset(); }

    bool CanOfferQuest(uint32 questId) const override
    {
        if (questId != QUEST_JUNGLE_SECRETS)
            return true;

        return m_rescueActive && m_rescueSucceeded && m_questTimer &&
            m_creature->IsAlive() && !m_creature->IsInCombat();
    }

    void RefreshQuestAvailability()
    {
        bool canOffer = CanOfferQuest(QUEST_JUNGLE_SECRETS);
        if (canOffer == m_lastCanOfferQuest)
            return;

        m_lastCanOfferQuest = canOffer;
        // The NPC keeps its questgiver flag for quest 198. Update the marker
        // explicitly because the availability of only quest 215 has changed.
        for (ObjectGuid guid : m_creature->GetClientGuidsIAmAt())
        {
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
            {
                uint32 status = DIALOG_STATUS_NONE;
                if (player->CanInteract(m_creature))
                    status = player->GetSession()->getDialogStatus(player, m_creature, DIALOG_STATUS_NONE);
                player->GetPlayerMenu()->SendQuestGiverStatus(uint8(status), m_creature->GetObjectGuid());
            }
        }
    }

    void CloseRescue()
    {
        ObjectGuid agentGuids[THORSEN_AGENT_COUNT];
        bool agentDead[THORSEN_AGENT_COUNT];
        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            agentGuids[i] = m_agentGuids[i];
            agentDead[i] = m_agentDead[i];
            m_agentGuids[i].Clear();
            m_agentDead[i] = false;
        }

        // Clear state before despawning: cleanup can synchronously call back.
        m_rescueActive = false;
        m_summonsComplete = false;
        m_rescueSucceeded = false;
        m_summoningSlot = THORSEN_AGENT_COUNT;
        m_rescueTimer = 0;
        m_questTimer = 0;
        m_returnTextTimer = 0;

        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            if (!agentGuids[i] || agentDead[i])
                continue;
            if (Creature* agent = m_creature->GetMap()->GetCreature(agentGuids[i]))
                if (agent->IsAlive())
                    agent->ForcedDespawn();
        }

        // Preserve dead agents and their loot; only unfinished attackers leave.
        m_creature->ClearTemporaryFaction();
        RefreshQuestAvailability();
    }

    void BeginRescue()
    {
        // Like VMaNGOS, success belongs to the two agents of this encounter,
        // rather than to an unconditional waypoint or a nearby creature count.
        CloseRescue();
        if (!m_creature->IsAlive())
            return;

        m_rescueActive = true;
        m_rescueTimer = THORSEN_RESCUE_TIMEOUT;
        m_creature->SetFactionTemporary(THORSEN_RESCUE_FACTION, TEMPFACTION_NONE);
        float const spawnPositions[THORSEN_AGENT_COUNT][4] =
        {
            { -11511.1f, -445.023f, 41.4481f, 1.03381f },
            { -11508.2f, -442.857f, 41.6781f, 2.53693f },
        };

        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            m_summoningSlot = i;
            // Keep the existing spawn locations, five minute OOC lifetime and
            // paths 1/2, including the agents' dialogue and attack scripts.
            TempSpawnSettings settings(m_creature, NPC_THORSEN_KURZEN_AGENT,
                spawnPositions[i][0], spawnPositions[i][1], spawnPositions[i][2], spawnPositions[i][3],
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, THORSEN_AGENT_LIFETIME, false, false, i + 1);
            Creature* agent = WorldObject::SummonCreature(settings, m_creature->GetMap());
            m_summoningSlot = THORSEN_AGENT_COUNT;

            bool registered = agent && m_rescueActive &&
                agent->GetEntry() == NPC_THORSEN_KURZEN_AGENT && agent->IsTemporarySummon() &&
                agent->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
                m_agentGuids[i] == agent->GetObjectGuid() &&
                (!i || m_agentGuids[0] != m_agentGuids[i]) &&
                (agent->IsAlive() || m_agentDead[i]);
            if (!registered)
            {
                CloseRescue();
                // A rejected returned summon may not have entered the roster.
                if (agent && agent->GetSpawnerGuid() == m_creature->GetObjectGuid() && agent->IsAlive())
                    agent->ForcedDespawn();
                return;
            }
        }

        // A death during a summon callback must not complete a partial roster.
        m_summonsComplete = true;
        DoBroadcastText(SAY_THORSEN_REJOIN, m_creature);
    }

    void JustRespawned() override
    {
        CloseRescue();
        CreatureEventAI::JustRespawned();
    }

    void JustDied(Unit* killer) override
    {
        CloseRescue();
        CreatureEventAI::JustDied(killer);
    }

    void EnterCombat(Unit* enemy) override
    {
        CreatureEventAI::EnterCombat(enemy);
        RefreshQuestAvailability();
    }

    void MovementInform(uint32 movementType, uint32 pointId) override
    {
        if (movementType != WAYPOINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case 23:
                BeginRescue();
                break;
            case 25:
                CloseRescue();
                if (m_creature->IsAlive())
                    m_returnTextTimer = 1000;
                break;
            case 1:
            case 47:
                CloseRescue();
                break;
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        // Register before invoking EventAI: its summon actions can call back.
        if (m_rescueActive && !m_summonsComplete && m_summoningSlot < THORSEN_AGENT_COUNT &&
            summoned->GetEntry() == NPC_THORSEN_KURZEN_AGENT && summoned->IsTemporarySummon() &&
            summoned->GetSpawnerGuid() == m_creature->GetObjectGuid() && !m_agentGuids[m_summoningSlot])
        {
            uint32 otherSlot = 1 - m_summoningSlot;
            if (m_agentGuids[otherSlot] != summoned->GetObjectGuid())
                m_agentGuids[m_summoningSlot] = summoned->GetObjectGuid();
        }
        CreatureEventAI::JustSummoned(summoned);
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        if (m_rescueActive && summoned->GetEntry() == NPC_THORSEN_KURZEN_AGENT && !summoned->IsAlive())
            for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
                if (m_agentGuids[i] == summoned->GetObjectGuid())
                    m_agentDead[i] = true;
        CreatureEventAI::SummonedCreatureJustDied(summoned);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        if (m_rescueActive)
            for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
                if (m_agentGuids[i] == summoned->GetObjectGuid() && !m_agentDead[i])
                {
                    CloseRescue();
                    break;
                }
        CreatureEventAI::SummonedCreatureDespawn(summoned);
    }

    void UpdateAI(uint32 diff) override
    {
        // SelectHostileTarget in the base may finish combat and trigger evade.
        CreatureEventAI::UpdateAI(diff);

        if (!m_creature->IsAlive())
        {
            if (m_rescueActive || m_returnTextTimer)
                CloseRescue();
            return;
        }

        if (m_returnTextTimer)
        {
            if (m_returnTextTimer <= diff)
            {
                m_returnTextTimer = 0;
                DoBroadcastText(SAY_THORSEN_RETURN, m_creature);
            }
            else
                m_returnTextTimer -= diff;
        }

        if (m_rescueActive)
        {
            if (m_rescueSucceeded)
            {
                if (m_questTimer <= diff)
                    CloseRescue();
                else
                    m_questTimer -= diff;
            }
            else if (m_rescueTimer <= diff)
                CloseRescue();
            else
            {
                m_rescueTimer -= diff;
                if (m_summonsComplete && m_agentDead[0] && m_agentDead[1] && !m_creature->IsInCombat())
                {
                    m_rescueSucceeded = true;
                    m_questTimer = THORSEN_QUEST_WINDOW;
                    m_creature->ClearTemporaryFaction();
                    DoBroadcastText(SAY_THORSEN_RESCUED, m_creature);
                }
            }
        }

        // Re-entering combat temporarily hides 215; the same window continues
        // to run, and the task is offered again only after combat has ended.
        RefreshQuestAvailability();
    }
};

/*######
##
######*/

void AddSC_stranglethorn_vale()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_galvan_the_ancient";
    pNewScript->GetAI = &GetNewAIInstance<npc_galvan_the_ancientAI>;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_galvan_the_ancient;
    pNewScript->pGossipHello = &GossipHello_npc_galvan_the_ancient;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_witch_doctor_unbagwa";
    pNewScript->GetAI = &GetNewAIInstance<npc_witch_doctor_unbagwaAI>;
    pNewScript->pQuestRewardedNPC = &QuestRewarded_npc_witch_doctor_unbagwa;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_private_thorsen";
    pNewScript->GetAI = &GetNewAIInstance<npc_private_thorsenAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_yenniku";
    pNewScript->GetAI = &GetAI_mob_yenniku;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_colonel_kurzen";
    pNewScript->GetAI = &GetNewAIInstance<mob_colonel_kurzenAI>;
    pNewScript->RegisterSelf();
}
