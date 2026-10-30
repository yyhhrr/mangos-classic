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
SDName: Arathi Highlands
SD%Complete: 100
SDComment: Quest support: 660, 665, 667
SDCategory: Arathi Highlands
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"

/* ContentData
npc_professor_phizzlethorpe
npc_kinelory
npc_shakes_o_breen
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Entities/TemporarySpawn.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"

/*######
## npc_professor_phizzlethorpe
######*/

enum
{
    SAY_PROGRESS_1          = -1000264,
    SAY_PROGRESS_2          = -1000265,
    SAY_PROGRESS_3          = -1000266,
    EMOTE_PROGRESS_4        = -1000267,
    SAY_AGGRO               = -1000268,
    SAY_PROGRESS_5          = -1000269,
    SAY_PROGRESS_6          = -1000270,
    SAY_PROGRESS_7          = -1000271,
    EMOTE_PROGRESS_8        = -1000272,
    SAY_PROGRESS_9          = -1000273,

    FACTION_BOOTY_BAY       = 120,
    QUEST_SUNKEN_TREASURE   = 665,
    ENTRY_VENGEFUL_SURGE    = 2776
};

struct npc_professor_phizzlethorpeAI : public npc_escortAI
{
    explicit npc_professor_phizzlethorpeAI(Creature* creature) : npc_escortAI(creature) {}

    GuidSet m_summons;
    bool m_ambushStarted = false;
    bool m_completed = false;
    bool m_aborted = false;

    void Reset() override { }

    bool CanOfferQuest(uint32 questId) const override
    {
        return questId != QUEST_SUNKEN_TREASURE || (m_creature->IsAlive() && !m_creature->IsInCombat() &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) &&
            !HasEscortState(STATE_ESCORT_ESCORTING) && !m_completed && !m_aborted);
    }

    bool OwnedSurge(Creature* actor) const
    {
        return actor && actor->IsTemporarySummon() && actor->GetEntry() == ENTRY_VENGEFUL_SURGE &&
            actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    void CleanupSummons()
    {
        GuidSet actors;
        actors.swap(m_summons);
        for (ObjectGuid guid : actors)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (OwnedSurge(actor) && actor->IsAlive())
                    actor->ForcedDespawn();
    }

    void JustStartedEscort() override
    {
        CleanupSummons();
        m_ambushStarted = m_completed = m_aborted = false;
    }

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        CleanupSummons();
        End();
        m_ambushStarted = m_completed = m_aborted = false;
    }

    void FailQuestForPlayerAndGroup() override
    {
        m_aborted = true;
        CleanupSummons();
        npc_escortAI::FailQuestForPlayerAndGroup();
    }

    void JustDied(Unit* killer) override
    {
        npc_escortAI::JustDied(killer);
        m_aborted = true;
        CleanupSummons();
    }

    bool TryStart(Player* player, Quest const* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_SUNKEN_TREASURE ||
            player->GetMap() != m_creature->GetMap() || !CanOfferQuest(QUEST_SUNKEN_TREASURE) ||
            player->GetQuestStatus(QUEST_SUNKEN_TREASURE) != QUEST_STATUS_INCOMPLETE)
            return false;
        Start(false, player, quest, true);
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_questForEscort != quest || GetPlayerForEscort() != player)
            return false;
        m_creature->SetFactionTemporary(FACTION_BOOTY_BAY, TEMPFACTION_RESTORE_RESPAWN);
        DoScriptText(SAY_PROGRESS_1, m_creature, player);
        return true;
    }

    bool SummonSurge(float x, float y, float z, float orientation)
    {
        Creature* actor = m_creature->SummonCreature(ENTRY_VENGEFUL_SURGE, x, y, z, orientation,
            TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN, 600000);
        if (OwnedSurge(actor) && actor->IsAlive() && m_summons.count(actor->GetObjectGuid()))
            return true;
        if (OwnedSurge(actor) && actor->IsAlive())
            actor->ForcedDespawn();
        FailQuestForPlayerAndGroup();
        End();
        m_creature->SetDeathState(JUST_DIED);
        m_creature->Respawn();
        return false;
    }

    void WaypointReached(uint32 uiPointId) override
    {
        Player* pPlayer = GetPlayerForEscort();

        if (!pPlayer || !HasEscortState(STATE_ESCORT_ESCORTING) || m_aborted || m_completed)
            return;

        switch (uiPointId)
        {
            case 5: DoScriptText(SAY_PROGRESS_2, m_creature, pPlayer); break;
            case 6: DoScriptText(SAY_PROGRESS_3, m_creature, pPlayer); break;
            case 9: DoScriptText(EMOTE_PROGRESS_4, m_creature); break;
            case 10:
                if (m_ambushStarted)
                    break;
                m_ambushStarted = true;
                if (SummonSurge(-2056.41f, -2144.01f, 20.59f, 5.70f))
                    SummonSurge(-2050.17f, -2140.02f, 19.54f, 5.17f);
                break;
            case 11: DoScriptText(SAY_PROGRESS_5, m_creature, pPlayer); break;
            case 12:
                DoScriptText(SAY_PROGRESS_6, m_creature, pPlayer);
                SetRun();
                break;
            case 20: DoScriptText(SAY_PROGRESS_7, m_creature, pPlayer); break;
            case 21:
                m_completed = true;
                DoScriptText(EMOTE_PROGRESS_8, m_creature);
                DoScriptText(SAY_PROGRESS_9, m_creature, pPlayer);
                pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_SUNKEN_TREASURE, m_creature);
                QuestEnd();
                CleanupSummons();
                break;
        }
    }

    void Aggro(Unit* /*pWho*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (!OwnedSurge(pSummoned))
            return;
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || !m_ambushStarted || m_aborted || m_completed)
        {
            if (pSummoned->IsAlive())
                pSummoned->ForcedDespawn();
            return;
        }
        m_summons.insert(pSummoned->GetObjectGuid());
        pSummoned->AI()->AttackStart(m_creature);
    }

    void SummonedCreatureJustDied(Creature* actor) override { m_summons.erase(actor->GetObjectGuid()); }
    void SummonedCreatureDespawn(Creature* actor) override { m_summons.erase(actor->GetObjectGuid()); }
};

bool QuestAccept_npc_professor_phizzlethorpe(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_SUNKEN_TREASURE)
    {
        if (npc_professor_phizzlethorpeAI* pEscortAI = dynamic_cast<npc_professor_phizzlethorpeAI*>(pCreature->AI()))
            pEscortAI->TryStart(pPlayer, pQuest);
    }
    return true;
}

UnitAI* GetAI_npc_professor_phizzlethorpe(Creature* pCreature)
{
    return new npc_professor_phizzlethorpeAI(pCreature);
}

/*######
## npc_kinelory
######*/

enum
{
    SAY_START               = -1000948,
    SAY_REACH_BOTTOM        = -1000949,
    SAY_AGGRO_KINELORY      = -1000950,
    SAY_AGGRO_JORELL        = -1000951,
    SAY_WATCH_BACK          = -1000952,
    EMOTE_BELONGINGS        = -1000953,
    SAY_DATA_FOUND          = -1000954,
    SAY_ESCAPE              = -1000955,
    SAY_FINISH              = -1000956,
    EMOTE_HAND_PACK         = -1000957,

    SPELL_REJUVENATION      = 3627,
    SPELL_BEAR_FORM         = 4948,

    NPC_JORELL              = 2733,
    NPC_QUAE                = 2712,

    QUEST_HINTS_NEW_PLAGUE  = 660,
    CONDITION_KINELORY_HOT  = 271301
};

class KineloryHealTargetCheck
{
public:
    explicit KineloryHealTargetCheck(Unit const* caster) : m_caster(caster) {}
    WorldObject const& GetFocusObject() const { return *m_caster; }
    bool operator()(Unit* unit)
    {
        if (!unit->IsAlive() || !unit->IsInCombat() || unit->GetHealthPercent() > 80.0f ||
            !m_caster->CanAssist(unit) || !m_caster->IsWithinDistInMap(unit, 40.0f) ||
            !unit->IsUnitConditionSatisfied(CONDITION_KINELORY_HOT, m_caster))
            return false;
        float missing = 100.0f - unit->GetHealthPercent();
        if (missing <= m_missing)
            return false;
        m_missing = missing;
        return true;
    }
private:
    Unit const* m_caster;
    float m_missing = 0.0f;
};

struct npc_kineloryAI : public npc_escortAI
{
    npc_kineloryAI(Creature* pCreature) : npc_escortAI(pCreature) { Reset(); }

    uint32 m_uiBearFormTimer;
    uint32 m_uiHealTimer;
    uint32 m_healAttempts = 0;
    bool m_completed = false;
    bool m_aborted = false;
    bool m_originalImmune = true;
    std::set<uint32> m_seenPoints;

    void Reset() override
    {
        m_uiBearFormTimer = urand(5000, 7000);
        m_uiHealTimer     = urand(2000, 5000);
        m_healAttempts = 0;
    }

    bool CanOfferQuest(uint32 questId) const override
    {
        return questId != QUEST_HINTS_NEW_PLAGUE || (m_creature->IsAlive() && !m_creature->IsInCombat() &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) &&
            !HasEscortState(STATE_ESCORT_ESCORTING) && !m_completed && !m_aborted);
    }

    void JustStartedEscort() override
    {
        m_completed = m_aborted = false;
        m_seenPoints.clear();
        Reset();
    }

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        End();
        m_completed = m_aborted = false;
        m_seenPoints.clear();
        Reset();
    }

    void FailQuestForPlayerAndGroup() override
    {
        m_aborted = true;
        m_creature->SetImmuneToNPC(m_originalImmune);
        npc_escortAI::FailQuestForPlayerAndGroup();
    }

    bool TryStart(Player* player, Quest const* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_HINTS_NEW_PLAGUE ||
            player->GetMap() != m_creature->GetMap() || !CanOfferQuest(QUEST_HINTS_NEW_PLAGUE) ||
            player->GetQuestStatus(QUEST_HINTS_NEW_PLAGUE) != QUEST_STATUS_INCOMPLETE)
            return false;
        bool originalImmune = m_creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        Start(false, player, quest, true);
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_questForEscort != quest || GetPlayerForEscort() != player)
            return false;
        m_originalImmune = originalImmune;
        m_creature->SetImmuneToNPC(false);
        DoScriptText(SAY_START, m_creature);
        return true;
    }

    void WaypointReached(uint32 uiPointId) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_aborted || m_completed || !m_seenPoints.insert(uiPointId).second)
            return;
        switch (uiPointId)
        {
            case 10:
                DoScriptText(SAY_REACH_BOTTOM, m_creature);
                break;
            case 17:
                DoScriptText(SAY_WATCH_BACK, m_creature);
                DoScriptText(EMOTE_BELONGINGS, m_creature);
                break;
            case 18:
                DoScriptText(SAY_DATA_FOUND, m_creature);
                break;
            case 19:
                DoScriptText(SAY_ESCAPE, m_creature);
                if (Player* pPlayer = GetPlayerForEscort())
                    m_creature->SetFacingToObject(pPlayer);
                SetRun();
                break;
            case 34:
                DoScriptText(SAY_FINISH, m_creature);
                m_creature->SetImmuneToNPC(true);
                if (Creature* pQuae = GetClosestCreatureWithEntry(m_creature, NPC_QUAE, 10.0f))
                {
                    DoScriptText(EMOTE_HAND_PACK, m_creature, pQuae);
                    m_creature->SetFacingToObject(pQuae);
                }
                break;
            case 35:
                m_completed = true;
                if (Player* pPlayer = GetPlayerForEscort())
                    pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_HINTS_NEW_PLAGUE, m_creature);
                QuestEnd();
                break;
        }
    }

    void Aggro(Unit* pWho) override
    {
        if (pWho->GetEntry() == NPC_JORELL)
            DoScriptText(SAY_AGGRO_JORELL, pWho, m_creature);
        else if (roll_chance_i(10))
            DoScriptText(SAY_AGGRO_KINELORY, m_creature);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 value) override
    {
        if (eventType == AI_EVENT_START_ESCORT && sender == m_creature && invoker &&
            invoker->GetTypeId() == TYPEID_PLAYER && value == QUEST_HINTS_NEW_PLAGUE)
            TryStart(static_cast<Player*>(invoker), GetQuestTemplateStore(value));
    }

    void UpdateEscortAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_uiBearFormTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_BEAR_FORM) == CAST_OK)
                m_uiBearFormTimer = urand(25000, 30000);
            else
                m_uiBearFormTimer = 1000;
        }
        else
            m_uiBearFormTimer -= uiDiff;

        if (m_uiHealTimer < uiDiff)
        {
            Unit* target = nullptr;
            KineloryHealTargetCheck check(m_creature);
            MaNGOS::UnitLastSearcher<KineloryHealTargetCheck> searcher(target, check);
            Cell::VisitAllObjects(m_creature, searcher, 40.0f);
            if (!target)
            {
                m_uiHealTimer = 1000;
                m_healAttempts = 0;
            }
            else if (DoCastSpellIfCan(target, SPELL_REJUVENATION) == CAST_OK)
            {
                m_uiHealTimer = urand(15000, 25000);
                m_healAttempts = 0;
            }
            else if (++m_healAttempts >= 3)
            {
                m_healAttempts = 0;
                m_uiHealTimer = 5000;
            }
            else
                m_uiHealTimer = 1000;
        }
        else
            m_uiHealTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_kinelory(Creature* pCreature)
{
    return new npc_kineloryAI(pCreature);
}

bool QuestAccept_npc_kinelory(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_HINTS_NEW_PLAGUE)
        pCreature->AI()->SendAIEvent(AI_EVENT_START_ESCORT, pPlayer, pCreature, pQuest->GetQuestId());

    return true;
}

/* The former DB script was unique by NPC and could complete only its first
 * player. Own one cancellable timeline and register each admitted participant. */
enum
{
    QUEST_DEATH_FROM_BELOW = 667,
    NPC_DAGGERSPINE_MARAUDER = 2775,
    SAY_PROTECT_START = 6372,
    SAY_PROTECT_CONTINUE = 863,
    DEATH_FROM_BELOW_JOIN_WINDOW = 15000,
    DEATH_FROM_BELOW_DURATION = 195000,
};

struct ArathiNagaSpawn
{
    uint32 time;
    uint32 path;
    float x, y, z;
};

// Kept exactly aligned with the nine original quest667 DB spawn records.
static ArathiNagaSpawn const arathiNagaSpawns[] =
{
    {15000,  1, -2158.06f, -1925.64f, 4.07425f},
    {18000,  3, -2145.76f, -1926.12f, 3.02982f},
    {18000,  4, -2166.72f, -1918.18f, 3.82191f},
    {55000,  2, -2158.06f, -1925.64f, 4.07425f},
    {58000,  3, -2145.76f, -1926.12f, 3.02982f},
    {58000,  4, -2166.72f, -1918.18f, 3.82191f},
    {110000, 5, -2158.06f, -1925.64f, 4.07425f},
    {130000, 3, -2145.76f, -1926.12f, 3.02982f},
    {130000, 4, -2166.72f, -1918.18f, 3.82191f},
};

struct npc_shakes_o_breenAI : public ScriptedAI
{
    explicit npc_shakes_o_breenAI(Creature* creature) : ScriptedAI(creature) {}

    GuidSet m_participants;
    GuidSet m_joinCandidates;
    GuidSet m_actors;
    uint32 m_elapsed = 0;
    uint32 m_playerCheck = 0;
    uint32 m_nextSpawn = 0;
    uint32 m_retryAt = 0;
    uint32 m_spawnAttempts = 0;
    bool m_running = false;
    bool m_acceptSummon = false;
    bool m_joinWindowClosed = false;
    bool m_secondText = false;
    bool m_originalActive = false;
    bool m_originalQuestgiver = false;

    bool Eligible(Player* player) const
    {
        return player && player->GetMap() == m_creature->GetMap() && player->IsAlive() &&
            player->GetQuestStatus(QUEST_DEATH_FROM_BELOW) == QUEST_STATUS_INCOMPLETE &&
            m_creature->IsWithinDistInMap(player, 60.0f);
    }

    bool CanOfferQuest(uint32 questId) const override
    {
        return questId != QUEST_DEATH_FROM_BELOW || (m_creature->IsAlive() && !m_creature->IsInCombat() &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) &&
            (!m_running || !m_joinWindowClosed));
    }

    bool OwnedNaga(Creature* actor) const
    {
        return actor && actor->IsTemporarySummon() && actor->GetEntry() == NPC_DAGGERSPINE_MARAUDER &&
            actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    void CleanupActors()
    {
        GuidSet actors;
        actors.swap(m_actors);
        for (ObjectGuid guid : actors)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (OwnedNaga(actor) && actor->IsAlive())
                    actor->ForcedDespawn();
    }

    void Finish(bool success)
    {
        if (!m_running)
        {
            if (!success)
                CleanupActors();
            return;
        }
        m_running = false; // Player credit and despawns may call script hooks.
        m_acceptSummon = false;
        m_joinWindowClosed = false;
        GuidSet participants;
        participants.swap(m_participants);
        m_joinCandidates.clear();
        for (ObjectGuid guid : participants)
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                if (player->GetQuestStatus(QUEST_DEATH_FROM_BELOW) == QUEST_STATUS_INCOMPLETE)
                {
                    if (success && Eligible(player))
                        player->AreaExploredOrEventHappens(QUEST_DEATH_FROM_BELOW);
                    else
                        player->FailQuest(QUEST_DEATH_FROM_BELOW);
                }
        if (!success)
            CleanupActors();
        // A successful protection timer does not erase an ongoing final fight.
        // Its actors retain their native 120s OOC timer and stay tracked until
        // death/despawn or cleanup before the next admitted round.
        if (m_originalQuestgiver && m_creature->IsAlive())
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        else
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetActiveObjectState(m_originalActive);
        m_elapsed = m_playerCheck = m_retryAt = m_spawnAttempts = m_nextSpawn = 0;
    }

    bool Request(Player* player)
    {
        if (m_running && player && m_participants.count(player->GetObjectGuid()))
            return Eligible(player);
        if (!Eligible(player))
            return false;
        if (m_running && m_joinWindowClosed)
        {
            // Normal quest offering is already closed; handle a stale accept
            // without admitting a late participant or disturbing this round.
            player->FailQuest(QUEST_DEATH_FROM_BELOW);
            return false;
        }
        if (!CanOfferQuest(QUEST_DEATH_FROM_BELOW))
            return false;
        if (!m_running)
        {
            CleanupActors();
            m_originalActive = m_creature->HasActiveObjectState();
            m_originalQuestgiver = m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            m_running = true;
            m_acceptSummon = m_secondText = m_joinWindowClosed = false;
            m_elapsed = m_playerCheck = m_nextSpawn = m_retryAt = m_spawnAttempts = 0;
            m_creature->SetActiveObjectState(true);
            DoBroadcastText(SAY_PROTECT_START, m_creature);
        }
        m_participants.insert(player->GetObjectGuid());
        RememberGroupCandidates(player);
        return true;
    }

    void RememberGroupCandidates(Player* player)
    {
        if (Group* group = player->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->getSource())
                {
                    m_joinCandidates.insert(member->GetObjectGuid());
                    if (Eligible(member))
                        m_participants.insert(member->GetObjectGuid());
                }
    }

    void CheckParticipants()
    {
        if (!m_joinWindowClosed)
        {
            // Party-shared quest acceptance has a player giver and does not
            // invoke the NPC hook. Admit those nearby acceptors in this same
            // window, even if their original quest-sharing leader logs out.
            GuidSet admitted = m_participants;
            for (ObjectGuid guid : admitted)
                if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                    if (Eligible(player))
                        RememberGroupCandidates(player);
            for (ObjectGuid guid : m_joinCandidates)
                if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                    if (Eligible(player))
                        m_participants.insert(guid);
        }
        for (auto itr = m_participants.begin(); itr != m_participants.end();)
        {
            Player* player = m_creature->GetMap()->GetPlayer(*itr);
            if (Eligible(player))
                ++itr;
            else
            {
                if (player && player->GetQuestStatus(QUEST_DEATH_FROM_BELOW) == QUEST_STATUS_INCOMPLETE)
                    player->FailQuest(QUEST_DEATH_FROM_BELOW);
                itr = m_participants.erase(itr);
            }
        }
        if (m_participants.empty())
            Finish(false);
    }

    void JustSummoned(Creature* actor) override
    {
        if (!OwnedNaga(actor))
            return;
        if (m_running && m_acceptSummon && actor->IsAlive())
            m_actors.insert(actor->GetObjectGuid());
        else if (actor->IsAlive())
            actor->ForcedDespawn();
    }

    void SummonedCreatureJustDied(Creature* actor) override { m_actors.erase(actor->GetObjectGuid()); }
    void SummonedCreatureDespawn(Creature* actor) override { m_actors.erase(actor->GetObjectGuid()); }
    void Reset() override {} // Normal combat evades must not erase the round.
    void JustDied(Unit* /*killer*/) override { Finish(false); }
    void JustRespawned() override { Finish(false); }

    bool SpawnNext()
    {
        if (!m_running || m_nextSpawn >= 9)
            return false;
        ArathiNagaSpawn const& spawn = arathiNagaSpawns[m_nextSpawn];
        m_acceptSummon = true;
        ++m_spawnAttempts;
        Creature* actor = m_creature->SummonCreature(NPC_DAGGERSPINE_MARAUDER, spawn.x, spawn.y, spawn.z, 0.0f,
            TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 120000, true, true, spawn.path);
        m_acceptSummon = false;
        if (OwnedNaga(actor) && actor->IsAlive() && m_actors.count(actor->GetObjectGuid()))
        {
            ++m_nextSpawn;
            m_spawnAttempts = m_retryAt = 0;
            return true;
        }
        if (OwnedNaga(actor) && actor->IsAlive())
            actor->ForcedDespawn();
        if (m_spawnAttempts >= 3)
            Finish(false);
        else
            m_retryAt = m_elapsed + 1000;
        return false;
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_running)
        {
            if (!m_creature->IsAlive())
                Finish(false);
            else
            {
                // Saturation also bounds unusually large map update deltas.
                m_elapsed += std::min(diff, uint32(DEATH_FROM_BELOW_DURATION) - m_elapsed);
                if (!m_joinWindowClosed && m_elapsed >= DEATH_FROM_BELOW_JOIN_WINDOW)
                {
                    // Capture party-shared accepts received since the last
                    // scan before closing the window in this map update.
                    CheckParticipants();
                    if (m_running)
                    {
                        m_joinWindowClosed = true;
                        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                    }
                }
                if (m_running && (m_playerCheck <= diff || m_elapsed == DEATH_FROM_BELOW_DURATION))
                {
                    m_playerCheck = 1000;
                    CheckParticipants();
                }
                else if (m_running)
                    m_playerCheck -= diff;
                if (m_running && !m_secondText && m_elapsed >= 65000)
                {
                    m_secondText = true;
                    DoBroadcastText(SAY_PROTECT_CONTINUE, m_creature);
                }
                while (m_running && m_nextSpawn < 9 && m_elapsed >= arathiNagaSpawns[m_nextSpawn].time && m_elapsed >= m_retryAt)
                    if (!SpawnNext())
                        break;
                if (m_running && m_elapsed == DEATH_FROM_BELOW_DURATION)
                    Finish(m_nextSpawn == 9);
            }
        }
        if (m_creature->SelectHostileTarget() && m_creature->GetVictim())
            DoMeleeAttackIfReady();
    }
};

bool QuestAccept_npc_shakes_o_breen(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_DEATH_FROM_BELOW)
        if (auto* ai = dynamic_cast<npc_shakes_o_breenAI*>(creature->AI()))
            ai->Request(player);
    return true;
}

void AddSC_arathi_highlands()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_professor_phizzlethorpe";
    pNewScript->GetAI = &GetAI_npc_professor_phizzlethorpe;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_professor_phizzlethorpe;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_kinelory";
    pNewScript->GetAI = &GetAI_npc_kinelory;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_kinelory;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_shakes_o_breen";
    pNewScript->GetAI = &GetNewAIInstance<npc_shakes_o_breenAI>;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_shakes_o_breen;
    pNewScript->RegisterSelf();
}
