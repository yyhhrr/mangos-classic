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
SDName: Eastern_Plaguelands
SD%Complete: 100
SDComment: Quest support: 5721, 7622.
SDCategory: Eastern Plaguelands
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_eris_havenfire
EndContentData */

#include <map>
#include <chrono>
#include "AI/BaseAI/GameObjectAI.h"
#include "Entities/TemporarySpawn.h"



/*######
## npc_eris_havenfire
######*/

enum
{
    SAY_PHASE_HEAL                      = -1000815,
    SAY_EVENT_END                       = -1000816,
    SAY_EVENT_FAIL_1                    = -1000817,
    SAY_EVENT_FAIL_2                    = -1000818,
    SAY_PEASANT_APPEAR_1                = -1000819,
    SAY_PEASANT_APPEAR_2                = -1000820,
    SAY_PEASANT_APPEAR_3                = -1000821,

    // SPELL_DEATHS_DOOR                 = 23127,           // damage spells cast on the peasants
    // SPELL_SEETHING_PLAGUE             = 23072,
    SPELL_ENTER_THE_LIGHT_DND           = 23107,
    SPELL_BLESSING_OF_NORDRASSIL        = 23108,

    NPC_INJURED_PEASANT                 = 14484,
    NPC_PLAGUED_PEASANT                 = 14485,
    NPC_SCOURGE_ARCHER                  = 14489,
    NPC_SCOURGE_FOOTSOLDIER             = 14486,
    NPC_THE_CLEANER                     = 14503,            // can be summoned if the priest has more players in the party for help. requires further research

    QUEST_BALANCE_OF_LIGHT_AND_SHADOW   = 7622,

    BASE_PEASANTS_PER_WAVE              = 11,
    MAX_KILLED_PEASANT                  = 15,
    MAX_SAVED_PEASANT                   = 50,
    MAX_ARCHERS                         = 8,
};

static const float aArcherSpawn[8][4] =
{
    {3327.42f, -3021.11f, 170.57f, 6.01f},
    {3335.4f,  -3054.3f,  173.63f, 0.49f},
    {3351.3f,  -3079.08f, 178.67f, 1.15f},
    {3358.93f, -3076.1f,  174.87f, 1.57f},
    {3371.58f, -3069.24f, 175.20f, 1.99f},
    {3369.46f, -3023.11f, 171.83f, 3.69f},
    {3383.25f, -3057.01f, 181.53f, 2.21f},
    {3380.03f, -3062.73f, 181.90f, 2.31f},
};

static const float aPeasantSpawnLoc[3] = {3360.12f, -3047.79f, 165.26f};
static const float aPeasantMoveLoc[3] = {3335.0f, -2994.04f, 161.14f};

static const int32 aPeasantSpawnYells[3] = {SAY_PEASANT_APPEAR_1, SAY_PEASANT_APPEAR_2, SAY_PEASANT_APPEAR_3};

struct npc_eris_havenfireAI : public ScriptedAI
{
    enum EventState { EVENT_IDLE, EVENT_RUNNING, EVENT_FAILED, EVENT_SUCCEEDED };
    enum ActorState { ACTOR_ACTIVE, ACTOR_SAVED };

    struct SummonState
    {
        uint32 entry;
        ActorState state;
        TimePoint lightUntil;
    };

    struct EventFrame
    {
        explicit EventFrame(npc_eris_havenfireAI& ai) : owner(ai) { ++owner.m_depth; }
        ~EventFrame() { --owner.m_depth; }
        npc_eris_havenfireAI& owner;
    };

    npc_eris_havenfireAI(Creature* creature) : ScriptedAI(creature), m_round(0),
        m_state(EVENT_IDLE), m_depth(0), m_cleaning(false), m_buildingWave(false),
        m_phaseEndPending(false)
    {
        Reset();
    }

    uint32 m_round;
    EventState m_state;
    uint32 m_depth;
    bool m_cleaning;
    bool m_buildingWave;
    bool m_phaseEndPending;
    uint32 m_uiEventTimer;
    uint32 m_uiSadEndTimer;
    uint32 m_uiRetireTimer;
    uint32 m_uiArcherCheckTargetTimer;
    uint8 m_uiPhase;
    uint8 m_uiCurrentWave;
    uint32 m_uiKillCounter;
    uint32 m_uiSaveCounter;
    uint32 m_uiTotalCounter;
    ObjectGuid m_playerGuid;
    std::map<ObjectGuid, SummonState> m_summons;

    bool IsPeasant(uint32 entry) const
    {
        return entry == NPC_INJURED_PEASANT || entry == NPC_PLAGUED_PEASANT;
    }

    bool IsRunning(uint32 round) const
    {
        return m_round == round && m_state == EVENT_RUNNING && !m_cleaning;
    }

    Player* GetEventOwner() const
    {
        Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid);
        return player && player->IsInWorld() && player->IsAlive() &&
            player->GetMap() == m_creature->GetMap() &&
            m_creature->IsWithinDistInMap(player, 180.0f) &&
            player->GetQuestStatus(QUEST_BALANCE_OF_LIGHT_AND_SHADOW) == QUEST_STATUS_INCOMPLETE ? player : nullptr;
    }

    bool IsOwned(Creature* summoned, uint32 entry) const
    {
        return summoned && summoned->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
            summoned->GetEntry() == entry && summoned->GetMap() == m_creature->GetMap() &&
            summoned->GetMap()->GetCreature(summoned->GetObjectGuid()) == summoned;
    }

    void RetireActors(std::map<ObjectGuid, SummonState> const& actors)
    {
        std::vector<std::pair<Creature*, uint32>> retire;
        for (auto const& actor : actors)
            if (Creature* summoned = m_creature->GetMap()->GetCreature(actor.first))
                if (IsOwned(summoned, actor.second.entry))
                    retire.emplace_back(summoned, actor.second.entry);
        // Resolve the entire detached snapshot first; reentrant lifecycle callbacks must
        // neither strand its remaining actors nor admit new actors into this removal list.
        for (auto const& actor : retire)
            if (IsOwned(actor.first, actor.second))
                actor.first->ForcedDespawn();
    }

    void Reset() override
    {
        // Combat evade is not a new event. Only the real lifecycle hooks discard a round.
        if (m_state != EVENT_IDLE)
            return;

        m_uiEventTimer = m_uiSadEndTimer = m_uiRetireTimer = m_uiArcherCheckTargetTimer = 0;
        m_uiPhase = m_uiCurrentWave = 0;
        m_uiKillCounter = m_uiSaveCounter = m_uiTotalCounter = 0;
        m_playerGuid.Clear();
        m_buildingWave = m_phaseEndPending = false;
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    void ClearScene(bool failOwner = false)
    {
        if (m_cleaning)
        {
            ++m_round;
            return;
        }

        m_cleaning = true;
        Map* map = m_creature->GetMap();
        Player* owner = failOwner && m_state == EVENT_RUNNING ? map->GetPlayer(m_playerGuid) : nullptr;
        std::map<ObjectGuid, SummonState> old;
        old.swap(m_summons);
        ++m_round;
        m_state = EVENT_IDLE;
        Reset();

        // Detach everything before callbacks caused by quest failure or removal can run.
        if (owner && owner->GetQuestStatus(QUEST_BALANCE_OF_LIGHT_AND_SHADOW) == QUEST_STATUS_INCOMPLETE)
            owner->FailQuest(QUEST_BALANCE_OF_LIGHT_AND_SHADOW);
        RetireActors(old);
        m_cleaning = false;
    }

    void JustDied(Unit* killer) override
    {
        EventFrame frame(*this);
        ClearScene(true);
        ScriptedAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        EventFrame frame(*this);
        ClearScene(true);
        ScriptedAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        EventFrame frame(*this);
        ClearScene(true);
        ScriptedAI::JustRespawned();
    }

    void FailBalanceEvent()
    {
        EventFrame frame(*this);
        if (m_state != EVENT_RUNNING)
            return;
        uint32 round = m_round;
        Map* map = m_creature->GetMap();
        Player* owner = map->GetPlayer(m_playerGuid);
        m_state = EVENT_FAILED;
        m_uiEventTimer = m_uiArcherCheckTargetTimer = 0;
        m_uiSadEndTimer = 4000;
        m_uiRetireTimer = 0;
        m_buildingWave = m_phaseEndPending = false;
        std::map<ObjectGuid, SummonState> old;
        old.swap(m_summons);
        if (owner && owner->GetQuestStatus(QUEST_BALANCE_OF_LIGHT_AND_SHADOW) == QUEST_STATUS_INCOMPLETE)
            owner->FailQuest(QUEST_BALANCE_OF_LIGHT_AND_SHADOW);
        RetireActors(old);
        if (m_round == round)
            DoScriptText(SAY_EVENT_FAIL_1, m_creature);
    }

    Creature* SummonOwned(uint32 entry, float x, float y, float z, float orientation)
    {
        uint32 round = m_round;
        Map* map = m_creature->GetMap();
        // A generous live-actor ceiling bounds an abandoned/stalled challenge without
        // imposing a shorter deadline on legitimate healing play.
        if (m_summons.size() >= 160)
        {
            FailBalanceEvent();
            return nullptr;
        }
        for (uint8 attempt = 0; attempt < 3 && IsRunning(round); ++attempt)
        {
            Creature* summoned = m_creature->SummonCreature(entry, x, y, z, orientation, TEMPSPAWN_DEAD_DESPAWN, 0);
            if (!IsRunning(round))
            {
                if (IsOwned(summoned, entry))
                    summoned->ForcedDespawn();
                return nullptr;
            }
            if (!summoned)
                continue;
            if (!IsOwned(summoned, entry) || summoned->GetMap() != map || !summoned->IsAlive())
            {
                if (IsOwned(summoned, entry))
                    summoned->ForcedDespawn();
                FailBalanceEvent();
                return nullptr;
            }

            // The factory return, not an arbitrary JustSummoned callback, admits an actor.
            ObjectGuid guid = summoned->GetObjectGuid();
            if (m_summons.find(guid) != m_summons.end())
            {
                FailBalanceEvent();
                return nullptr;
            }
            m_summons.emplace(guid, SummonState{entry, ACTOR_ACTIVE, TimePoint()});
            if (IsPeasant(entry))
            {
                ++m_uiTotalCounter;
                float moveX, moveY, moveZ;
                summoned->GetRandomPoint(aPeasantMoveLoc[0], aPeasantMoveLoc[1], aPeasantMoveLoc[2], 10.0f, moveX, moveY, moveZ);
                summoned->GetMotionMaster()->MovePoint(1, moveX, moveY, moveZ);
            }
            else if (entry == NPC_SCOURGE_FOOTSOLDIER)
            {
                if (!summoned->AI())
                {
                    FailBalanceEvent();
                    return nullptr;
                }
                if (Player* owner = GetEventOwner())
                    summoned->AI()->AttackStart(owner);
            }
            return IsRunning(round) ? summoned : nullptr;
        }
        if (IsRunning(round))
            FailBalanceEvent();
        return nullptr;
    }

    void DoSummonWave(uint32 summonId = 0)
    {
        EventFrame frame(*this);
        uint32 round = m_round;
        if (!IsRunning(round))
            return;
        float x, y, z;
        if (!summonId)
        {
            if (m_buildingWave || m_uiCurrentWave >= 5)
                return;
            m_buildingWave = true;
            uint8 count = BASE_PEASANTS_PER_WAVE + m_uiCurrentWave;
            for (uint8 i = 0; i < count; ++i)
            {
                uint32 entry = roll_chance_i(70) ? NPC_INJURED_PEASANT : NPC_PLAGUED_PEASANT;
                m_creature->GetRandomPoint(aPeasantSpawnLoc[0], aPeasantSpawnLoc[1], aPeasantSpawnLoc[2], 10.0f, x, y, z);
                Creature* summoned = SummonOwned(entry, x, y, z, 0);
                if (!summoned || !IsRunning(round))
                    return;
                if (!i)
                    DoScriptText(aPeasantSpawnYells[urand(0, 2)], summoned);
                if (!IsRunning(round))
                    return;
            }
            ++m_uiCurrentWave;
            m_buildingWave = false;
        }
        else if (summonId == NPC_SCOURGE_FOOTSOLDIER)
        {
            uint8 count = urand(2, 3);
            for (uint8 i = 0; i < count && IsRunning(round); ++i)
            {
                m_creature->GetRandomPoint(aPeasantSpawnLoc[0], aPeasantSpawnLoc[1], aPeasantSpawnLoc[2], 15.0f, x, y, z);
                if (!SummonOwned(summonId, x, y, z, 0))
                    return;
            }
        }
        else if (summonId == NPC_SCOURGE_ARCHER)
            for (auto const& position : aArcherSpawn)
                if (!SummonOwned(summonId, position[0], position[1], position[2], position[3]))
                    return;
    }

    void SummonedMovementInform(Creature* summoned, uint32 motionType, uint32 pointId) override
    {
        EventFrame frame(*this);
        if (!summoned || m_state != EVENT_RUNNING || motionType != POINT_MOTION_TYPE || pointId != 1 || !GetEventOwner())
            return;
        auto actor = m_summons.find(summoned->GetObjectGuid());
        if (actor == m_summons.end() || !IsOwned(summoned, actor->second.entry) ||
            !IsPeasant(actor->second.entry) || actor->second.state != ACTOR_ACTIVE || !summoned->IsAlive())
            return;

        uint32 round = m_round;
        actor->second.state = ACTOR_SAVED;
        actor->second.lightUntil = m_creature->GetMap()->GetCurrentClockTime() + std::chrono::milliseconds(10000);
        ++m_uiSaveCounter;
        summoned->SetImmuneToNPC(true);
        summoned->SetImmuneToPlayer(true);
        summoned->CombatStop(true);
        if (!IsRunning(round))
            return;
        summoned->GetMotionMaster()->Clear();
        if (!IsRunning(round))
            return;
        summoned->RemoveAllAuras();
        if (!IsRunning(round))
            return;
        summoned->CastSpell(summoned, SPELL_ENTER_THE_LIGHT_DND, TRIGGERED_NONE);
        if (!IsRunning(round))
            return;
        if (m_uiSaveCounter >= MAX_SAVED_PEASANT)
            DoBalanceEventEnd();
        else if (m_uiSaveCounter + m_uiKillCounter == m_uiTotalCounter)
            m_phaseEndPending = true;
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        EventFrame frame(*this);
        if (!summoned)
            return;
        auto actor = m_summons.find(summoned->GetObjectGuid());
        if (actor == m_summons.end() || !IsOwned(summoned, actor->second.entry))
            return;
        bool casualty = IsPeasant(actor->second.entry) && actor->second.state == ACTOR_ACTIVE;
        m_summons.erase(actor);
        if (m_state != EVENT_RUNNING || !casualty)
            return;
        if (!GetEventOwner())
        {
            FailBalanceEvent();
            return;
        }
        ++m_uiKillCounter;
        if (m_uiKillCounter >= MAX_KILLED_PEASANT)
            FailBalanceEvent();
        else if (m_uiSaveCounter + m_uiKillCounter == m_uiTotalCounter)
            m_phaseEndPending = true;
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        EventFrame frame(*this);
        if (!summoned)
            return;
        auto actor = m_summons.find(summoned->GetObjectGuid());
        if (actor == m_summons.end() || !IsOwned(summoned, actor->second.entry))
            return;
        bool unexpected = actor->second.state == ACTOR_ACTIVE;
        m_summons.erase(actor);
        if (unexpected && m_state == EVENT_RUNNING)
            FailBalanceEvent();
    }

    void DoHandlePhaseEnd()
    {
        if (m_state != EVENT_RUNNING || m_buildingWave || !m_phaseEndPending ||
            m_uiSaveCounter + m_uiKillCounter != m_uiTotalCounter)
            return;
        m_phaseEndPending = false;
        uint32 round = m_round;
        Player* owner = GetEventOwner();
        if (!owner)
        {
            FailBalanceEvent();
            return;
        }
        owner->CastSpell(owner, SPELL_BLESSING_OF_NORDRASSIL, TRIGGERED_OLD_TRIGGERED);
        if (!IsRunning(round))
            return;
        DoScriptText(SAY_PHASE_HEAL, m_creature);
        if (IsRunning(round) && m_uiCurrentWave < 5)
            DoSummonWave();
    }

    void DoStartBalanceEvent(Player* player)
    {
        if (m_depth || m_cleaning || m_state != EVENT_IDLE || !player || !player->IsInWorld() ||
            !player->IsAlive() || !m_creature->IsAlive() || !m_creature->IsInWorld() ||
            player->GetMap() != m_creature->GetMap() ||
            player->GetQuestStatus(QUEST_BALANCE_OF_LIGHT_AND_SHADOW) != QUEST_STATUS_INCOMPLETE)
            return;
        EventFrame frame(*this);
        ClearScene();
        m_state = EVENT_RUNNING;
        m_playerGuid = player->GetObjectGuid();
        m_uiEventTimer = 5000;
        m_uiArcherCheckTargetTimer = 2000;
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    void DoBalanceEventEnd()
    {
        EventFrame frame(*this);
        if (m_state != EVENT_RUNNING || m_uiSaveCounter < MAX_SAVED_PEASANT)
            return;
        Player* owner = GetEventOwner();
        if (!owner)
        {
            FailBalanceEvent();
            return;
        }
        uint32 round = m_round;
        m_state = EVENT_SUCCEEDED;
        m_uiEventTimer = m_uiArcherCheckTargetTimer = 0;
        m_buildingWave = m_phaseEndPending = false;
        std::map<ObjectGuid, SummonState> retire;
        for (auto actor = m_summons.begin(); actor != m_summons.end();)
            if (actor->second.state != ACTOR_SAVED)
            {
                retire.emplace(actor->first, actor->second);
                actor = m_summons.erase(actor);
            }
            else
                ++actor;
        RetireActors(retire);
        if (m_round != round)
            return;
        owner->AreaExploredOrEventHappens(QUEST_BALANCE_OF_LIGHT_AND_SHADOW);
        if (m_round != round)
            return;
        DoScriptText(SAY_EVENT_END, m_creature);
        if (m_round == round)
            EnterEvadeMode();
    }

    Creature* FindArcherTarget(Creature* archer)
    {
        for (uint32 entry : {uint32(NPC_INJURED_PEASANT), uint32(NPC_PLAGUED_PEASANT)})
        {
            Creature* target = nullptr;
            float closest = 30.0f;
            for (auto const& actor : m_summons)
            {
                if (actor.second.entry != entry || actor.second.state != ACTOR_ACTIVE)
                    continue;
                Creature* peasant = m_creature->GetMap()->GetCreature(actor.first);
                if (!IsOwned(peasant, entry) || !peasant->IsAlive())
                    continue;
                float distance = archer->GetDistance(peasant);
                if (distance <= closest)
                {
                    closest = distance;
                    target = peasant;
                }
            }
            if (target)
                return target;
        }
        return nullptr;
    }

    void DoAttackArchersTarget()
    {
        uint32 round = m_round;
        GuidList archers;
        for (auto const& actor : m_summons)
            if (actor.second.entry == NPC_SCOURGE_ARCHER)
                archers.push_back(actor.first);
        for (ObjectGuid guid : archers)
        {
            Creature* archer = m_creature->GetMap()->GetCreature(guid);
            if (!IsOwned(archer, NPC_SCOURGE_ARCHER) || !archer->IsAlive())
                continue;
            Unit* victim = archer->GetVictim();
            auto actor = victim ? m_summons.find(victim->GetObjectGuid()) : m_summons.end();
            if (actor != m_summons.end() && IsPeasant(actor->second.entry) && actor->second.state == ACTOR_ACTIVE &&
                victim->IsAlive() && archer->IsWithinDistInMap(victim, 30.0f))
                continue;
            archer->CombatStop(true);
            if (!IsRunning(round))
                return;
            if (Creature* target = FindArcherTarget(archer))
            {
                if (!archer->AI())
                {
                    FailBalanceEvent();
                    return;
                }
                archer->AI()->AttackStart(target);
            }
            if (!IsRunning(round))
                return;
        }
    }

    void RetireLights()
    {
        Map* map = m_creature->GetMap();
        std::map<ObjectGuid, SummonState> retire;
        for (auto actor = m_summons.begin(); actor != m_summons.end();)
            if (actor->second.state == ACTOR_SAVED && actor->second.lightUntil <= map->GetCurrentClockTime())
            {
                retire.emplace(actor->first, actor->second);
                actor = m_summons.erase(actor);
            }
            else
                ++actor;
        RetireActors(retire);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_depth || m_cleaning)
            return;
        EventFrame frame(*this);
        uint32 round = m_round;
        RetireLights();
        if (m_round != round)
            return;
        if (m_state == EVENT_SUCCEEDED)
        {
            if (m_summons.empty())
                ClearScene();
            return;
        }
        if (m_state == EVENT_FAILED)
        {
            if (m_uiSadEndTimer)
            {
                if (m_uiSadEndTimer > diff)
                    m_uiSadEndTimer -= diff;
                else
                {
                    m_uiSadEndTimer = 0;
                    m_uiRetireTimer = 5000;
                    DoScriptText(SAY_EVENT_FAIL_2, m_creature);
                }
            }
            else if (m_uiRetireTimer)
            {
                if (m_uiRetireTimer > diff)
                    m_uiRetireTimer -= diff;
                else
                {
                    ClearScene();
                    if (m_round == round + 1)
                        m_creature->ForcedDespawn();
                }
            }
            return;
        }
        if (m_state != EVENT_RUNNING)
            return;
        if (!GetEventOwner())
        {
            FailBalanceEvent();
            return;
        }
        DoHandlePhaseEnd();
        if (!IsRunning(round))
            return;
        if (m_uiEventTimer)
        {
            if (m_uiEventTimer > diff)
                m_uiEventTimer -= diff;
            else
            {
                uint8 phase = m_uiPhase;
                DoSummonWave(phase == 0 ? NPC_SCOURGE_ARCHER : phase == 1 ? 0 : NPC_SCOURGE_FOOTSOLDIER);
                if (!IsRunning(round))
                    return;
                m_uiEventTimer = phase == 0 ? 5000 : phase == 1 ? urand(60000, 80000) : urand(5000, 30000);
                if (m_uiPhase < 2)
                    ++m_uiPhase;
            }
        }
        if (m_uiArcherCheckTargetTimer)
        {
            if (m_uiArcherCheckTargetTimer > diff)
                m_uiArcherCheckTargetTimer -= diff;
            else
            {
                DoAttackArchersTarget();
                if (IsRunning(round))
                    m_uiArcherCheckTargetTimer = 2000;
            }
        }
    }
};

UnitAI* GetAI_npc_eris_havenfire(Creature* pCreature)
{
    return new npc_eris_havenfireAI(pCreature);
}

bool QuestAccept_npc_eris_havenfire(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_BALANCE_OF_LIGHT_AND_SHADOW)
    {
        if (npc_eris_havenfireAI* pErisAI = dynamic_cast<npc_eris_havenfireAI*>(pCreature->AI()))
            pErisAI->DoStartBalanceEvent(pPlayer);
    }

    return true;
}

// 23209 - Terrordale Haunting Spirit #2 (Terrordale Haunting Spirit #2)
struct TerrordaleHauntingSpirit2 : public SpellScript
{
    void OnInit(Spell* spell) const override
    {
        spell->SetEffectChance(55, EFFECT_INDEX_1);
    }
};

// 23253 - Terrordale Haunting Spirit #3 (Terrordale Haunting Spirit #3)
struct TerrordaleHauntingSpirit3 : public SpellScript
{
    void OnInit(Spell* spell) const override
    {
        spell->SetEffectChance(35, EFFECT_INDEX_1);
    }
};

// DARROWSHIRE_BATTLE_BEGIN
// Insert before AddSC_eastern_plaguelands; call RegisterDarrowshireBattle() there.
// Additional includes: AI/BaseAI/GameObjectAI.h, Entities/TemporarySpawn.h.
// VM classic battle coordinates/waves are adapted to current CMaNGOS interfaces.
// GameObjectAI has no creature death/movement callbacks: inspect only registered
// per-controller GUIDs; keep principal corpses long enough for the next heartbeat.
namespace
{
enum DarrowshireBattleData
{
    DS_QUEST = 5721, DS_BUNDLE_SPELL = 18987, DS_CONTROLLER = 177526,
    DS_JOSEPH = 10936, DS_CAPTAIN = 10937, DS_CORRUPTED = 10938, DS_MARDUK = 10939,
    DS_DAVIL = 10944, DS_CROKFORD = 10945, DS_HORGUS = 10946, DS_BETRAYER = 10947,
    DS_DEFENDER = 10948, DS_DISCIPLE = 10949, DS_MILITIA = 10950,
    DS_CORPSE = 10951, DS_SKELETON = 10952, DS_SERVANT = 10953, DS_BLOODLETTER = 10954,
    DS_RUNNING_LIMIT = 15 * MINUTE * IN_MILLISECONDS,
    DS_ENDING_LIMIT = 10 * MINUTE * IN_MILLISECONDS,
    DS_ACTOR_CAP = 200
};
struct DarrowshirePosition { float x, y, z, o; };
static DarrowshirePosition const dsPositions[] =
{
    {1500.04f, -3662.67f, 82.832f, 3.70805f},
    {1506.17f, -3686.72f, 82.8769f, 5.75945f},
    {1512.81f, -3724.64f, 87.12099f, 1.64164f},
    {1537.6f, -3677.1f, 88.7f, 3.14884f},
    {1484.68f, -3668.74f, 80.6953f, 0.236567f},
    {1493.53f, -3695.01f, 80.1347f, 0.264055f},
    {1505.28f, -3718.83f, 83.343f, 1.36911f},
    {1446.8f, -3694.27f, 76.5966f, 0.401503f}
};
bool DarrowshireLocation(WorldObject const* object)
{
    return object && object->GetMapId() == 0 &&
        object->IsWithinDist2d(1493.53f, -3695.01f, 100.0f);
}
bool DarrowshireEligible(Player* player)
{
    return player && player->IsInWorld() && player->IsAlive() && DarrowshireLocation(player) &&
        player->GetQuestStatus(DS_QUEST) == QUEST_STATUS_INCOMPLETE;
}
}

struct go_darrowshire_battle : public GameObjectAI
{
    enum Phase
    {
        FIRST_DEFENDER, DAVIL_ENTERS, HORGUS_ENTERS, HORGUS_FIGHT,
        DAVIL_LEAVES, CAPTAIN_ENTERS, MARDUK_ENTERS, CORRUPTION,
        CORRUPTED_FIGHT, VICTORY_ACTORS, ENDING, CLOSED
    };
    explicit go_darrowshire_battle(GameObject* go) : GameObjectAI(go),
        m_phase(FIRST_DEFENDER), m_started(false), m_updating(false),
        m_phaseTimer(0), m_totalTimer(0), m_ownerTimer(0), m_heartbeat(0),
        m_retryCount(0), m_faction(0), m_mardukExit(0)
    {
        for (uint32& timer : m_waveTimer) timer = 0;
    }

    Phase m_phase;
    bool m_started, m_updating;
    ObjectGuid m_owner, m_davil, m_horgus, m_captain, m_marduk, m_corrupted;
    std::map<ObjectGuid, uint32> m_actors;
    uint32 m_phaseTimer, m_totalTimer, m_ownerTimer, m_heartbeat;
    uint32 m_retryCount, m_faction, m_mardukExit, m_waveTimer[7];

    bool OccupiesBattlefield() const { return m_started && m_phase != CLOSED; }
    static bool IsOccupied(WorldObject* source, GameObject* exclude = nullptr)
    {
        GameObjectList list;
        GetGameObjectListWithEntryInGrid(list, source, DS_CONTROLLER, 200.0f);
        for (GameObject* go : list)
            if (go != exclude)
                if (go_darrowshire_battle* ai = dynamic_cast<go_darrowshire_battle*>(go->AI()))
                    if (ai->OccupiesBattlefield()) return true;
        return false;
    }
    Creature* Own(ObjectGuid guid) const
    {
        if (!guid || m_actors.find(guid) == m_actors.end()) return nullptr;
        Creature* actor = m_go->GetMap()->GetCreature(guid);
        return actor && actor->IsTemporarySummon() &&
            actor->GetSpawnerGuid() == m_go->GetObjectGuid() &&
            actor->GetEntry() == m_actors.find(guid)->second ? actor : nullptr;
    }
    void Retire(ObjectGuid guid)
    {
        // UnSummon retires a temporary actor without manufacturing a death event.
        Creature* actor = Own(guid);
        m_actors.erase(guid);
        if (actor) static_cast<TemporarySpawn*>(actor)->UnSummon();
    }
    void ClearActors(bool keepEnding = false)
    {
        GuidVector retire;
        for (auto const& pair : m_actors)
            if (!keepEnding || (pair.second != DS_JOSEPH && pair.second != DS_CROKFORD))
                retire.push_back(pair.first);
        for (ObjectGuid guid : retire) Retire(guid);
    }
    void Close(bool deleteObject = true)
    {
        if (m_phase == CLOSED) return;
        m_phase = CLOSED; // Commit terminal state before UnSummon/GO callbacks.
        m_phaseTimer = m_totalTimer = m_mardukExit = 0;
        for (uint32& timer : m_waveTimer) timer = 0;
        ClearActors();
        if (deleteObject) m_go->Delete(); // Also deletes only this GO's linked trap.
    }
    void JustDespawned() override { Close(false); }

    bool Start(Player* player)
    {
        if (m_started || m_phase == CLOSED) return false;
        if (!DarrowshireEligible(player) || !DarrowshireLocation(m_go) ||
            m_go->GetSpellId() != DS_BUNDLE_SPELL ||
            m_go->GetSpawnerGuid() != player->GetObjectGuid() ||
            player->GetMap() != m_go->GetMap() || IsOccupied(m_go, m_go))
        {
            Close();
            return false;
        }
        m_started = true;
        m_owner = player->GetObjectGuid();
        m_faction = player->GetTeam() == HORDE ? 85 : 57;
        m_totalTimer = DS_RUNNING_LIMIT;
        m_phaseTimer = 6000;
        m_waveTimer[0] = 15000;
        m_waveTimer[1] = 17000;
        m_go->SetRespawnTime(15 * MINUTE);
        m_go->SetActiveObjectState(true);
        return true;
    }
    Creature* Spawn(uint32 entry, uint32 position, uint32 lifetime = 120000, bool scatter = true)
    {
        if (m_phase == CLOSED || m_actors.size() >= DS_ACTOR_CAP) return nullptr;
        DarrowshirePosition const& pos = dsPositions[position];
        float x = pos.x, y = pos.y, z = pos.z;
        float radius = scatter ? 5.0f : 0.0f;
        if (entry == DS_HORGUS || entry == DS_MARDUK || entry == DS_CORRUPTED)
        {
            Creature* principal = Own(entry == DS_HORGUS ? m_davil : m_captain);
            if (!principal || !principal->IsAlive()) return nullptr;
            x = principal->GetPositionX(); y = principal->GetPositionY(); z = principal->GetPositionZ();
            radius = entry == DS_HORGUS ? 6.0f : entry == DS_MARDUK ? 10.0f : 0.0f;
        }
        else if (entry == DS_CROKFORD)
        {
            x = 1465.43f; y = -3678.48f; z = 78.0816f; radius = 0.0f;
        }
        if (radius) m_go->GetRandomPoint(x, y, z, radius, x, y, z);
        Creature* actor = m_go->SummonCreature(entry, x, y, z, pos.o,
            TEMPSPAWN_TIMED_OR_DEAD_DESPAWN, lifetime, true);
        if (!actor) return nullptr;
        // SummonCreature runs the actual EAI initialization before it returns.
        m_actors[actor->GetObjectGuid()] = entry;
        if (m_phase == CLOSED)
        {
            Retire(actor->GetObjectGuid());
            return nullptr;
        }
        if (!actor->AI())
        {
            Retire(actor->GetObjectGuid());
            return nullptr;
        }
        actor->SetCorpseDelay(30);
        if (entry == DS_DEFENDER || entry == DS_DISCIPLE || entry == DS_MILITIA ||
            entry == DS_DAVIL || entry == DS_CAPTAIN)
            actor->setFaction(m_faction);
        if (entry == DS_MARDUK)
            actor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE |
                UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PLAYER);
        else if (entry == DS_DAVIL || entry == DS_CAPTAIN || entry == DS_BLOODLETTER)
        {
            actor->SetWalk(false);
            Move(actor, entry == DS_BLOODLETTER ? 0 : 2);
        }
        else if (entry != DS_HORGUS && entry != DS_CORRUPTED &&
            entry != DS_JOSEPH && entry != DS_CROKFORD)
            actor->GetMotionMaster()->MoveRandomAroundPoint(actor->GetPositionX(),
                actor->GetPositionY(), actor->GetPositionZ(), 5.0f);
        return m_phase == CLOSED ? nullptr : actor;
    }
    void Move(Creature* actor, uint32 point)
    {
        static uint32 const positions[] = {5, 7, 4, 6};
        DarrowshirePosition const& pos = dsPositions[positions[point % 4]];
        actor->SetRespawnCoord(pos.x, pos.y, pos.z, pos.o);
        actor->GetMotionMaster()->MovePoint(point, pos.x, pos.y, pos.z);
    }
    void Say(uint32 text, Creature* actor)
    {
        if (actor && m_phase != CLOSED) DoBroadcastText(text, actor);
    }
    void SayDefender(uint32 text)
    {
        for (auto const& pair : m_actors)
            if (pair.second == DS_DEFENDER)
                if (Creature* actor = Own(pair.first))
                    if (actor->IsAlive()) { Say(text, actor); return; }
    }
    void Retry()
    {
        if (m_phase == CLOSED) return;
        if (++m_retryCount >= 3) Close();
        else m_phaseTimer = 5000;
    }
    void Next(Phase phase, uint32 timer)
    {
        if (m_phase == CLOSED) return;
        m_phase = phase; m_phaseTimer = timer; m_retryCount = 0;
    }
    bool PrincipalAlive(ObjectGuid guid, uint32 failText)
    {
        Creature* actor = Own(guid);
        if (actor && actor->IsAlive()) return true;
        if (actor) SayDefender(failText);
        Close();
        return false;
    }
    void CheckPrincipals()
    {
        if (m_phase == HORGUS_ENTERS || m_phase == HORGUS_FIGHT)
        {
            if (!PrincipalAlive(m_davil, 7366)) return;
            if (m_phase == HORGUS_FIGHT)
            {
                Creature* horgus = Own(m_horgus);
                if (!horgus) { Close(); return; } // Absence never counts as death.
                if (!horgus->IsAlive())
                {
                    Next(DAVIL_LEAVES, 8000);
                    m_waveTimer[2] = 0;
                    m_waveTimer[3] = 6000;
                    SayDefender(7368);
                }
            }
        }
        else if (m_phase == MARDUK_ENTERS || m_phase == CORRUPTION)
            PrincipalAlive(m_captain, 7369);
        else if (m_phase == CORRUPTED_FIGHT)
        {
            Creature* corrupted = Own(m_corrupted);
            if (!corrupted) { Close(); return; }
            if (!corrupted->IsAlive())
            {
                Next(VICTORY_ACTORS, 1);
                for (uint32& timer : m_waveTimer) timer = 0;
                SayDefender(7407);
            }
        }
    }
    static bool Friendly(uint32 entry)
    {
        return entry == DS_DEFENDER || entry == DS_DISCIPLE || entry == DS_MILITIA ||
            entry == DS_DAVIL || entry == DS_CAPTAIN;
    }
    void MaintainActors()
    {
        GuidVector remove;
        // Snapshot GUIDs before calling movement/AI; no iterator crosses a callback.
        GuidVector snapshot;
        for (auto const& pair : m_actors) snapshot.push_back(pair.first);
        for (ObjectGuid guid : snapshot)
        {
            if (m_phase == CLOSED) return;
            Creature* actor = Own(guid);
            if (!actor) { remove.push_back(guid); continue; }
            if (!actor->IsAlive() || actor->IsInCombat() || actor->GetEntry() == DS_MARDUK ||
                actor->GetEntry() == DS_JOSEPH || actor->GetEntry() == DS_CROKFORD) continue;
            Creature* target = nullptr;
            float distance = 60.0f;
            for (ObjectGuid candidate : snapshot)
            {
                Creature* enemy = Own(candidate);
                if (!enemy || !enemy->IsAlive() || enemy == actor ||
                    enemy->GetEntry() == DS_MARDUK || enemy->GetEntry() == DS_JOSEPH ||
                    enemy->GetEntry() == DS_CROKFORD ||
                    Friendly(enemy->GetEntry()) == Friendly(actor->GetEntry()) ||
                    !actor->CanAttack(enemy)) continue;
                float next = actor->GetDistance(enemy);
                if (next < distance) { distance = next; target = enemy; }
            }
            if (target && actor->AI()) actor->AI()->AttackStart(target);
        }
        for (ObjectGuid guid : remove) m_actors.erase(guid);
    }
    void Wave(uint32 kind)
    {
        if (kind == 6)
        {
            GuidVector snapshot;
            for (auto const& pair : m_actors) snapshot.push_back(pair.first);
            for (ObjectGuid guid : snapshot)
                if (Creature* actor = Own(guid))
                    if (actor->IsAlive() && !actor->IsInCombat() &&
                        (actor->GetEntry() == DS_BLOODLETTER || actor->GetEntry() == DS_DAVIL ||
                         actor->GetEntry() == DS_CAPTAIN) &&
                        actor->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                    {
                        Move(actor, urand(0, 3));
                        if (m_phase == CLOSED) return;
                    }
            return;
        }
        uint32 entry = kind == 1 ? DS_DEFENDER : kind == 2 ? DS_SERVANT :
            kind == 3 ? DS_DISCIPLE : kind == 4 ? DS_BLOODLETTER :
            m_phase >= CORRUPTED_FIGHT ? DS_BETRAYER : DS_MILITIA;
        for (uint32 lane = 0; lane != 3; ++lane)
        {
            uint32 amount = (kind == 0 || kind == 2) ? urand(1, 2) : 1;
            for (uint32 n = 0; n != amount; ++n)
            {
                if (m_phase == CLOSED) return;
                uint32 spawnEntry = kind == 0 ? (urand(0, 1) ? DS_CORPSE : DS_SKELETON) : entry;
                uint32 position = kind == 1 || kind == 3 || kind == 5 ? lane + 4 : kind == 4 ? 3 : lane;
                Creature* actor = Spawn(spawnEntry, position);
                if (kind == 5 && lane == 0 && actor && spawnEntry == DS_MILITIA)
                    Say(urand(7347, 7354), actor);
            }
        }
    }
    void PhaseAction()
    {
        switch (m_phase)
        {
            case FIRST_DEFENDER:
                if (Creature* actor = Spawn(DS_DEFENDER, 7, 120000, false))
                {
                    Next(DAVIL_ENTERS, urand(120000, 180000));
                    Move(actor, 2);
                    Say(7358, actor);
                }
                else Retry();
                break;
            case DAVIL_ENTERS:
                if (Creature* actor = Spawn(DS_DAVIL, 7, DS_RUNNING_LIMIT, false))
                {
                    m_davil = actor->GetObjectGuid();
                    Next(HORGUS_ENTERS, 60000);
                    m_waveTimer[2] = 4000; m_waveTimer[6] = 10000;
                    Say(7343, actor);
                }
                else Retry();
                break;
            case HORGUS_ENTERS:
                if (Creature* actor = Spawn(DS_HORGUS, 4, DS_RUNNING_LIMIT))
                {
                    m_horgus = actor->GetObjectGuid();
                    Next(HORGUS_FIGHT, 0);
                    if (Creature* davil = Own(m_davil))
                    {
                        actor->AI()->AttackStart(davil);
                        if (m_phase != CLOSED) Say(7346, davil);
                    }
                    Say(7344, actor);
                }
                else Retry();
                break;
            case DAVIL_LEAVES:
                if (Creature* davil = Own(m_davil)) Say(7227, davil);
                if (m_phase == CLOSED) return;
                Retire(m_davil);
                Next(CAPTAIN_ENTERS, 10000);
                break;
            case CAPTAIN_ENTERS:
                if (Creature* actor = Spawn(DS_CAPTAIN, 7, DS_RUNNING_LIMIT, false))
                {
                    m_captain = actor->GetObjectGuid();
                    Next(MARDUK_ENTERS, urand(300000, 350000));
                    m_waveTimer[4] = 4000; m_waveTimer[5] = 6000;
                    Say(7355, actor);
                }
                else Retry();
                break;
            case MARDUK_ENTERS:
                if (Creature* actor = Spawn(DS_MARDUK, 4, DS_RUNNING_LIMIT))
                {
                    m_marduk = actor->GetObjectGuid();
                    Next(CORRUPTION, 5000);
                    m_mardukExit = 12000;
                    Say(7471, actor);
                }
                else Retry();
                break;
            case CORRUPTION:
                if (!Own(m_marduk)) { Close(); return; }
                if (Creature* actor = Spawn(DS_CORRUPTED, 4, DS_RUNNING_LIMIT))
                {
                    m_corrupted = actor->GetObjectGuid();
                    Next(CORRUPTED_FIGHT, 0); // Commit before the captain's death.
                    if (Creature* captain = Own(m_captain)) captain->Suicide();
                    if (m_phase == CLOSED) return;
                    GuidVector militia;
                    for (auto const& pair : m_actors) if (pair.second == DS_MILITIA) militia.push_back(pair.first);
                    for (ObjectGuid guid : militia) Retire(guid);
                    // Subsequent militia wave becomes this round's betrayers.
                    m_waveTimer[5] = 1;
                    Say(7357, actor);
                }
                else Retry();
                break;
            case VICTORY_ACTORS:
            {
                Creature* joseph = Spawn(DS_JOSEPH, 7, DS_ENDING_LIMIT, false);
                if (!joseph) { Retry(); break; }
                ObjectGuid josephGuid = joseph->GetObjectGuid();
                joseph->SetUInt32Value(UNIT_NPC_FLAGS, 0);
                Creature* crokford = Spawn(DS_CROKFORD, 4, 120000, false);
                if (!crokford)
                {
                    Retire(josephGuid);
                    Retry();
                    break;
                }
                Next(ENDING, DS_ENDING_LIMIT);
                m_totalTimer = 0;
                m_mardukExit = 0;
                ClearActors(true);
                if (m_phase == CLOSED) return;
                m_go->SetRespawnTime(10 * MINUTE);
                if (Creature* ready = Own(josephGuid)) ready->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                // Existing Joseph gossip3861 performs credit; no direct CompleteQuest.
                break;
            }
            case ENDING: Close(); break;
            default: break;
        }
    }
    void UpdateBattle(uint32 diff)
    {
        if (!m_started || m_phase == CLOSED) return;
        if (m_phase != ENDING)
        {
            if (m_totalTimer <= diff) { Close(); return; }
            m_totalTimer -= diff;
            Player* owner = m_go->GetMap()->GetPlayer(m_owner);
            bool ownerHere = owner && owner->IsInWorld() && owner->IsAlive() &&
                owner->GetQuestStatus(DS_QUEST) == QUEST_STATUS_INCOMPLETE &&
                owner->IsWithinDist2d(1493.53f, -3695.01f, 180.0f);
            if (ownerHere) m_ownerTimer = 0;
            else
            {
                if (diff >= 30000 || m_ownerTimer >= 30000 - diff) { Close(); return; }
                m_ownerTimer += diff;
            }
            Phase const oldPhase = m_phase;
            CheckPrincipals();
            if (m_phase == CLOSED || m_phase != oldPhase) return;
        }
        if (m_mardukExit)
        {
            if (m_mardukExit <= diff) { m_mardukExit = 0; Retire(m_marduk); }
            else m_mardukExit -= diff;
        }
        if (m_heartbeat <= diff)
        {
            m_heartbeat = 1000;
            if (m_phase != ENDING) MaintainActors();
        }
        else m_heartbeat -= diff;
        if (m_phase == CLOSED) return;
        static uint32 const repeats[] = {25000, 45000, 35000, 45000, 35000, 45000, 5000};
        for (uint32 i = 0; i != 7; ++i)
        {
            if (!m_waveTimer[i]) continue;
            if (m_waveTimer[i] <= diff)
            {
                m_waveTimer[i] = repeats[i];
                Wave(i);
                if (m_phase == CLOSED) return;
            }
            else m_waveTimer[i] -= diff;
        }
        if (m_phaseTimer)
        {
            if (m_phaseTimer <= diff) { m_phaseTimer = 0; PhaseAction(); }
            else m_phaseTimer -= diff;
        }
    }
    void UpdateAI(uint32 diff) override
    {
        if (m_updating) return;
        m_updating = true;
        UpdateBattle(diff);
        m_updating = false;
    }
};

struct DarrowshireBundle : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* caster = spell->GetCaster();
        if (!caster || !caster->IsPlayer() || !DarrowshireEligible(static_cast<Player*>(caster)))
            return SPELL_FAILED_BAD_TARGETS;
        if (go_darrowshire_battle::IsOccupied(caster)) return SPELL_FAILED_NOT_READY;
        return SPELL_CAST_OK;
    }
    void OnSummon(Spell* spell, GameObject* summoned) const override
    {
        if (!summoned || summoned->GetEntry() != DS_CONTROLLER) return;
        Unit* caster = spell->GetCaster();
        go_darrowshire_battle* ai = dynamic_cast<go_darrowshire_battle*>(summoned->AI());
        if (caster && caster->IsPlayer() && ai)
            ai->Start(static_cast<Player*>(caster));
        else
            summoned->Delete();
    }
};

void RegisterDarrowshireBattle()
{
    Script* script = new Script;
    script->Name = "go_darrowshire_battle";
    script->GetGameObjectAI = &GetNewAIInstance<go_darrowshire_battle>;
    script->RegisterSelf();
    RegisterSpellScript<DarrowshireBundle>("spell_darrowshire_bundle");
}
// DARROWSHIRE_BATTLE_END

void AddSC_eastern_plaguelands()
{
RegisterDarrowshireBattle();

    Script* pNewScript = new Script;
    pNewScript->Name = "npc_eris_havenfire";
    pNewScript->GetAI = &GetAI_npc_eris_havenfire;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_eris_havenfire;
    pNewScript->RegisterSelf();

    RegisterSpellScript<TerrordaleHauntingSpirit2>("spell_terrordale_haunting_spirit2");
    RegisterSpellScript<TerrordaleHauntingSpirit3>("spell_terrordale_haunting_spirit3");
}
