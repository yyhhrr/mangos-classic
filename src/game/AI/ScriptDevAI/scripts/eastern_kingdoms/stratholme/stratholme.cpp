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
SDName: Instance_Stratholme
SD%Complete: 100
SDComment:
SDCategory: Stratholme
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "stratholme.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Globals/ObjectMgr.h"
#include "Entities/TemporarySpawn.h"
#include <cmath>
#include <iomanip>

instance_stratholme::instance_stratholme(Map* map) : ScriptedInstance(map),
    m_baronRunTimer(0),
    m_barthilasRunTimer(0),
    m_mindlessSummonTimer(0),
    m_slaughterSquareTimer(0),
    m_slaughterDoorTimer(0),
    m_blackGuardsTimer(0),
    m_auriusSummonTimer(0),
    m_isSlaughterDoorOpen(false),
    m_yellCounter(0),
    m_mindlessCount(0),
    m_postboxesUsed(0),
    m_jarienKilled(false),
    m_sothosKilled(false)
{
    Initialize();
}

void instance_stratholme::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

    for (auto& m_uiGateTrapTimer : m_gateTrapTimers)
    {
        for (uint8 j = 0; j < 3; ++j)
            m_uiGateTrapTimer[j] = 0;
    }
}

void instance_stratholme::OnPlayerEnter(Player* player)
{
    SyncRunAura(player);
    m_restorePending = true;
    m_ysidaAttempts = 0; m_ysidaTimer = 1;
    m_retryBlocked = false;
    for (auto& slot : m_spawns) slot.attempts = 0;
}

// This function will prevent to lock players/monsters out/in
// the Slaughterhouse when toggling the door:
// this could happen when wiping before the end of the whole event
// The door will be toggled only if the current state is
// different of the one requested by open (true = open, false  = closed)
void instance_stratholme::DoOpenSlaughterhouseDoor(bool open)
{
    m_isSlaughterDoorOpen = open;
    if (GameObject* door = GetSingleGameObjectFromStorage(GO_ZIGGURAT_DOOR_4, true))
        door->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
}

bool instance_stratholme::StartSlaughterSquare()
{
    bool ready = m_auiEncounter[1] == SPECIAL && m_auiEncounter[2] == SPECIAL && m_auiEncounter[3] == SPECIAL;
    if (ready) SyncObjects();
    return ready;
}

void instance_stratholme::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_AURIUS:
            // Flags and pose are restored only after creature initialization.
        // no break here
        case NPC_BARON:
        case NPC_YSIDA:
        case NPC_BARTHILAS:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;

        case NPC_CRYSTAL:
            m_crystalGUIDs.push_back(creature->GetObjectGuid());
            break;
        case NPC_ABOM_BILE:
        case NPC_ABOM_VENOM:
            m_abominationGUIDs.insert(creature->GetObjectGuid());
            break;
        case NPC_THUZADIN_ACOLYTE:
            m_acolyteGUIDs.push_back(creature->GetObjectGuid());
            break;
        case NPC_CRIMSON_INITIATE:
        case NPC_CRIMSON_GALLANT:
        case NPC_CRIMSON_GUARDSMAN:
        case NPC_CRIMSON_CONJURER:
        case NPC_CRIMSON_BATTLE_MAGE:
            // Store GUID of NPCs in the courtyard to spawn Timmy once they are all dead
            if (creature->IsWithinDist2d(defensePoints[TIMMY].x, defensePoints[TIMMY].y, 40.0f))
                m_crimsonDefendersLowGuids[TIMMY].push_back(creature->GetObjectGuid());
            // Iterate also over all the defense points where those NPCs are possibly spawned
            for (uint8 i = BARRICADE; i <= FIRST_BARRICADES; i++)
            {
                // Do not store - again - GUIDs for Timmy spawn point, they were done previously with a different range
                if (i == TIMMY)
                    continue;
                // Store the GUID of the nearby NPCs for each defense point
                if (creature->IsWithinDist2d(defensePoints[i].x, defensePoints[i].y, 8.0f))
                {
                    m_crimsonDefendersLowGuids[i].push_back(creature->GetObjectGuid());
                    break;
                }
            }
            break;
    }
    if (creature->HasStaticDBSpawnData())
    {
        m_staticGuids[creature->GetDbGuid()] = creature->GetObjectGuid();
        m_staticEntries[creature->GetDbGuid()] = creature->GetEntry();
        uint32 entry = creature->GetEntry();
        if (entry != NPC_CRIMSON_INITIATE && entry != NPC_CRIMSON_GALLANT && entry != NPC_CRIMSON_GUARDSMAN && entry != NPC_CRIMSON_CONJURER && entry != NPC_CRIMSON_BATTLE_MAGE)
            for (auto& members : m_defenseExpected) members.erase(creature->GetDbGuid());
    }
    if (creature->GetEntry() == 16046)
    {
        m_pairSpawner = creature->GetObjectGuid();
        for (auto& slot : m_spawns) if (slot.kind == 50 || slot.kind == 51) slot.attempts = 0;
        m_spawnTimer = 1;
    }
    m_restorePending = true;
}


void instance_stratholme::OnObjectCreate(GameObject* go)
{
    m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
    if (go->GetEntry() == GO_GAUNTLET_GATE1) go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED);
    if (go->GetEntry() == GO_ZIGGURAT_DOOR_1) m_zigguratStorage[0].m_doorGuid = go->GetObjectGuid();
    if (go->GetEntry() == GO_ZIGGURAT_DOOR_2) m_zigguratStorage[1].m_doorGuid = go->GetObjectGuid();
    if (go->GetEntry() == GO_ZIGGURAT_DOOR_3) m_zigguratStorage[2].m_doorGuid = go->GetObjectGuid();
    m_restorePending = true;
}

void instance_stratholme::OnObjectSpawn(GameObject* go)
{
    if (m_pairBoxCasting && go->GetEntry() == 153464 && go->GetSpellId() == SPELL_SUMMON_WINNER_BOX && go->GetSpawnerGuid() == m_pairBoxCastingOwner)
    {
        if (m_pairBoxCastingGeneration == m_pairGeneration) m_pairBox = true;
        m_pairRewards.erase(std::remove_if(m_pairRewards.begin(), m_pairRewards.end(), [&](StratholmeBoxRequest const& request) { return request.generation == m_pairBoxCastingGeneration; }), m_pairRewards.end());
        SaveState();
    }
    m_restorePending = true;
}

void instance_stratholme::SetData(uint32 type, uint32 data)
{
    if (type >= MAX_ENCOUNTER) return;
    if (m_auiEncounter[type] == DONE && type != TYPE_BARONESS && type != TYPE_NERUB && type != TYPE_PALLID) return;
    // The tower boss and its five static acolytes have distinct completion states.
    switch (type)
    {
        case TYPE_BARON_RUN:
            if (data == IN_PROGRESS) return; // StartBaronRun validates the real participant.
            if (data != FAIL && data != DONE) return;
            m_auiEncounter[type] = data;
            m_baronRunTimer = 0; m_runPaused = false;
            for (auto const& ref : instance->GetPlayers()) if (Player* player = ref.getSource()) player->RemoveAurasDueToSpell(SPELL_BARON_ULTIMATUM);
            m_ysidaTimer = 1; m_ysidaAttempts = 0;
            break;
        case TYPE_BARONESS: case TYPE_NERUB: case TYPE_PALLID:
            if (m_auiEncounter[type] == SPECIAL) return;
            if (m_auiEncounter[type] == DONE && data != SPECIAL) return;
            if (data == SPECIAL && !StaticMembersDead(m_towerExpected[type - 1])) return;
            m_auiEncounter[type] = data;
            if (data == DONE) DoSortZiggurats();
            break;
        case TYPE_RAMSTEIN:
            if (data == SPECIAL)
            {
                if (m_slaughterPhase) return;
                if (m_auiEncounter[type] != SPECIAL && m_auiEncounter[type] != DONE) m_slaughterSquareTimer = 20000;
                m_auiEncounter[type] = SPECIAL;
                AdvanceSlaughter();
            }
            else if (data == IN_PROGRESS || data == FAIL)
            {
                m_auiEncounter[type] = data;
                if (data == FAIL) { m_slaughterSquareTimer = 0; DoOpenSlaughterhouseDoor(true); }
            }
            else return; // A qualified Ramstein death owns completion.
            break;
        case TYPE_BARON:
            m_runPaused = data == IN_PROGRESS && m_auiEncounter[TYPE_BARON_RUN] == IN_PROGRESS;
            if (data != IN_PROGRESS) m_auriusSummonTimer = 0;
            if (data == IN_PROGRESS)
            {
                DoUseOpenableObject(GO_ZIGGURAT_DOOR_5, false);

                // Close Slaughterhouse door if needed
                if (m_auiEncounter[type] == FAIL)
                    DoUseOpenableObject(GO_PORT_GAUNTLET, false);

                // If Aurius was given the medaillon wait 5s before summoning him
                if (GetData(TYPE_AURIUS) == DONE)
                {
                    m_auriusSummonTimer = 5 * IN_MILLISECONDS;
                    m_auriusAttempts = 0;
                }
            }
            else if (data == DONE)
            {
                // Players successfully engaged Baron within the time-limit of his ultimatum
                //     Note: UpdateAI() prevents TYPE_BARON_RUN to be marked as FAILED if the
                //     Baron is already engaged (in progress) when the ultimatum timer expires
                if (m_auiEncounter[TYPE_BARON_RUN] == IN_PROGRESS)
                {
                    SetData(TYPE_BARON_RUN, DONE);
                    Map::PlayerList const& players = instance->GetPlayers();

                    for (const auto& player : players)
                    {
                        if (Player* sourcePlayer = player.getSource())
                        {
                            if (sourcePlayer->HasAura(SPELL_BARON_ULTIMATUM))
                                sourcePlayer->RemoveAurasDueToSpell(SPELL_BARON_ULTIMATUM);

                            if (sourcePlayer->GetQuestStatus(QUEST_DEAD_MAN_PLEA) == QUEST_STATUS_INCOMPLETE)
                                sourcePlayer->KilledMonsterCredit(NPC_YSIDA);

                            // Argent Dawn reputation reward
                            sourcePlayer->CastSpell(sourcePlayer, SPELL_YSIDA_FREED, TRIGGERED_OLD_TRIGGERED);
                        }
                    }

                    // Open cage, finish rescue event
                    if (Creature* ysida = GetSingleCreatureFromStorage(NPC_YSIDA))
                    {
                        DoScriptText(SAY_EPILOGUE, ysida);
                        DoUseDoorOrButton(GO_YSIDA_CAGE);
                        ysida->GetMotionMaster()->MovePoint(0, stratholmeLocation[8].x, stratholmeLocation[8].y, stratholmeLocation[8].z);
                        ysida->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
                    }
                }

                // If Aurius was spawned to help fight the Baron
                if (GetData(TYPE_AURIUS) == DONE)
                {
                    // Baron killed and Aurius is alive: give him his NPC Flags back, so players can complete the quest and fake his death
                    if (Creature* aurius = GetSingleCreatureFromStorage(NPC_AURIUS))
                    {
                        if (aurius->IsAlive())
                        {
                            m_auriusPosition = Position{aurius->GetPositionX(), aurius->GetPositionY(), aurius->GetPositionZ(), aurius->GetOrientation()};
                            m_auriusPositionReady = true;
                            DoScriptText(SAY_AURIUS_DEATH, aurius);
                            aurius->StopMoving();
                            aurius->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
                            aurius->InterruptNonMeleeSpells(true);
                            aurius->SetHealth(1);
                            aurius->GetMotionMaster()->MovementExpired();
                            aurius->GetMotionMaster()->MoveIdle();
                            aurius->RemoveAllAurasOnDeath();
                            aurius->SetStandState(UNIT_STAND_STATE_DEAD);
                        }
                    }
                }
            }

            if (data != IN_PROGRESS)
            {
                // Open Slaughterhouse door again
                DoUseOpenableObject(GO_PORT_GAUNTLET, true);
                // Open Baron door
                DoUseOpenableObject(GO_ZIGGURAT_DOOR_5, true);
            }

            m_auiEncounter[type] = data;
            break;
        case TYPE_BARTHILAS_RUN:
            if (data == IN_PROGRESS)
            {
                Creature* barthilas = GetSingleCreatureFromStorage(NPC_BARTHILAS);
                if (barthilas && barthilas->IsAlive() && !barthilas->IsInCombat())
                {
                    DoScriptText(SAY_WARN_BARON, barthilas);
                    barthilas->SetWalk(false);
                    barthilas->GetMotionMaster()->MovePoint(0, stratholmeLocation[0].x, stratholmeLocation[0].y, stratholmeLocation[0].z);

                    m_barthilasRunTimer = 8 * IN_MILLISECONDS;
                }
            }
            m_auiEncounter[type] = data;
            break;
        case TYPE_BLACK_GUARDS:
            if (data != IN_PROGRESS && data != FAIL) return;
            m_auiEncounter[type] = data;
            break;
        case TYPE_POSTMASTER:
            return; // UsePostbox owns identity and successful summon confirmation.
        case TYPE_AURIUS:
            // Prevent further players to complete the quest in that instance or autocomplete the follow-up quest. the flag will be set back if event is succeed
            if (data == DONE)
            {
                if (Creature* aurius = GetSingleCreatureFromStorage(NPC_AURIUS))
                    aurius->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            }
            m_auiEncounter[type] = data;
            break;
        default:
            m_auiEncounter[type] = data;
            break;
    }

    SaveState();
    SyncObjects();
}


void instance_stratholme::Load(char const* data)
{
    if (!data) return;
    std::istringstream in(data);
    uint32 old[8] = {};
    for (auto& state : old) if (!(in >> state) || state > SPECIAL) return;
    instance_stratholme baseline(instance);
    for (uint32 i = 0; i < 8; ++i) baseline.m_auiEncounter[i] = old[i] == IN_PROGRESS ? uint32(NOT_STARTED) : old[i];
    baseline.m_legacy = true;
    if (old[0] == IN_PROGRESS) baseline.m_auiEncounter[0] = FAIL;
    for (uint32 i = 1; i <= 3; ++i) if (old[i] == DONE) baseline.m_auiEncounter[i] = SPECIAL;
    // Unknown old reward histories never authorize another Ramstein/guard drop.
    if (old[4] == DONE || old[5] == DONE) { baseline.m_slaughterPhase = 6; baseline.m_auiEncounter[8] = DONE; }
    *this = baseline;
    std::string version;
    if (!(in >> version) || version != "ST2") { BuildSaveData(); return; }
    instance_stratholme payload(instance);
    auto parse = [&]() -> bool
    {
        for (uint32 i = 0; i < 8; ++i) payload.m_auiEncounter[i] = old[i];
        uint32 paused, pair, legacy, auriusPosition; uint64 saved;
        if (!(in >> payload.m_auiEncounter[8] >> payload.m_auiEncounter[9] >> payload.m_baronRunTimer >> saved >> paused
              >> payload.m_yellCounter >> payload.m_slaughterPhase >> payload.m_blackGuardsTimer >> payload.m_slaughterDoorTimer
              >> payload.m_defenseTriggered >> pair >> legacy >> payload.m_runFailTimer >> auriusPosition
              >> payload.m_auriusPosition.x >> payload.m_auriusPosition.y >> payload.m_auriusPosition.z >> payload.m_auriusPosition.o
              >> payload.m_slaughterSquareTimer >> payload.m_mindlessSummonTimer)) return false;
        if (payload.m_auiEncounter[8] > SPECIAL || payload.m_auiEncounter[9] > SPECIAL || payload.m_baronRunTimer > 2700000 ||
            paused > 1 || pair > 1 || legacy > 1 || auriusPosition > 1 || payload.m_runFailTimer > 8000 || payload.m_yellCounter > 3 || payload.m_slaughterPhase > 6 || payload.m_slaughterPhase == 5 ||
            payload.m_blackGuardsTimer > 60000 || payload.m_slaughterDoorTimer > 10000 || payload.m_slaughterSquareTimer > 45000 || payload.m_mindlessSummonTimer > 500 || payload.m_defenseTriggered > 511) return false;
        for (auto& timers : payload.m_gateTrapTimers)
            for (uint32 i = 0; i < 3; ++i) if (!(in >> timers[i]) || timers[i] > (i == 0 ? 1800000u : i == 1 ? 20000u : 2000u)) return false;
        uint32 requests; if (!(in >> payload.m_pairGeneration >> requests) || !payload.m_pairGeneration || requests > 16) return false;
        std::set<uint32> generations;
        for (uint32 i = 0; i < requests; ++i)
        {
            StratholmeBoxRequest request; uint64 owner;
            if (!(in >> request.generation >> owner) || !request.generation || request.generation > payload.m_pairGeneration || !owner || !generations.insert(request.generation).second) return false;
            request.owner = ObjectGuid(owner); payload.m_pairRewards.push_back(request);
        }
        payload.m_pairBox = pair == 1;
        payload.m_legacy = legacy == 1; payload.m_auriusPositionReady = auriusPosition == 1;
        if (!std::isfinite(payload.m_auriusPosition.x) || !std::isfinite(payload.m_auriusPosition.y) || !std::isfinite(payload.m_auriusPosition.z) || !std::isfinite(payload.m_auriusPosition.o) ||
            std::abs(payload.m_auriusPosition.x) > 17066 || std::abs(payload.m_auriusPosition.y) > 17066 || std::abs(payload.m_auriusPosition.z) > 3000) return false;
        auto readSet = [&](std::set<uint32>& values, uint32 limit) -> bool
        {
            uint32 count, value; if (!(in >> count) || count > limit) return false;
            for (uint32 i = 0; i < count; ++i) { if (!(in >> value) || !value || !values.insert(value).second) return false; }
            return true;
        };
        if (!readSet(payload.m_staticDead, 256) || !readSet(payload.m_postboxGuids, 6)) return false;
        uint32 count; if (!(in >> count) || count > 100) return false;
        std::map<uint32, uint32> counts;
        for (uint32 i = 0; i < count; ++i)
        {
            StratholmeSpawn slot; uint32 created, killed;
            if (!(in >> slot.kind >> slot.entry >> created >> killed >> slot.expires >> slot.x >> slot.y >> slot.z >> slot.o >> slot.dx >> slot.dy >> slot.dz)) return false;
            if (created > 1 || killed > 1 || (killed && !created) || !std::isfinite(slot.x) || !std::isfinite(slot.y) || !std::isfinite(slot.z) || !std::isfinite(slot.o) ||
                !std::isfinite(slot.dx) || !std::isfinite(slot.dy) || !std::isfinite(slot.dz) || std::abs(slot.x) > 17066 || std::abs(slot.y) > 17066 || std::abs(slot.z) > 3000 ||
                std::abs(slot.dx) > 17066 || std::abs(slot.dy) > 17066 || std::abs(slot.dz) > 3000 || (slot.kind == 40 && slot.expires > uint64(time(nullptr)) + 180)) return false;
            bool valid = (slot.kind == 1 && slot.entry == NPC_RAMSTEIN && ++counts[1] <= 1) ||
                (slot.kind == 2 && slot.entry == NPC_MINDLESS_UNDEAD && ++counts[2] <= 30) ||
                (slot.kind == 3 && slot.entry == NPC_BLACK_GUARD && ++counts[3] <= 5) ||
                (slot.kind == 18 && slot.entry == NPC_TIMMY_THE_CRUEL && ++counts[18] <= 1) ||
                (slot.kind == 20 && slot.entry == NPC_CRIMSON_GALLANT && ++counts[20] <= 2) ||
                (slot.kind == 22 && (slot.entry == NPC_CRIMSON_MONK || slot.entry == NPC_CRIMSON_SORCERER) && ++counts[slot.kind * 100000 + slot.entry] <= 1) ||
                ((slot.kind == 19 || slot.kind == 21 || slot.kind == 24) && (slot.entry == NPC_SKELETAL_GUARDIAN || slot.entry == NPC_SKELETAL_BERSERKER) && ++counts[slot.kind] <= 5) ||
                (slot.kind >= 32 && slot.kind <= 34 && slot.entry == NPC_UNDEAD_POSTMAN && ++counts[slot.kind] <= 3) ||
                (slot.kind == 40 && slot.entry == 11143 && ++counts[40] <= 1) ||
                (slot.kind == 50 && slot.entry == NPC_JARIEN && ++counts[50] <= 1) || (slot.kind == 51 && slot.entry == NPC_SOTHOS && ++counts[51] <= 1);
            if (!valid) return false;
            slot.created = created == 1; slot.killed = killed == 1; payload.m_spawns.push_back(slot);
        }
        std::string end; if (!(in >> end) || end != "END") return false;
        in >> std::ws; if (!in.eof()) return false;
        for (uint32 kind : {19u,20u,21u,24u,32u,33u,34u})
            if (counts[kind] && counts[kind] != (kind == 20 ? 2u : kind >= 32 ? 3u : 5u)) return false;
        if (counts[22 * 100000 + NPC_CRIMSON_MONK] != counts[22 * 100000 + NPC_CRIMSON_SORCERER] || counts[50] != counts[51]) return false;
        if ((payload.m_slaughterPhase == 0 && (counts[1] || counts[2] || counts[3])) ||
            (payload.m_slaughterPhase == 1 && counts[1] != 1) ||
            ((payload.m_slaughterPhase == 2 || payload.m_slaughterPhase == 3 || payload.m_slaughterPhase == 4) &&
             (counts[1] != 1 || counts[2] != 30 || counts[3] != 5 || !payload.AllKilled(1, 1))) ||
            ((payload.m_slaughterPhase == 3 || payload.m_slaughterPhase == 4) && !payload.AllKilled(2, 30)) ||
            (payload.m_slaughterPhase == 6 && !payload.m_legacy && !payload.AllKilled(3, 5))) return false;
        uint64 now = uint64(time(nullptr));
        uint64 offline = now > saved ? (now - saved) * 1000 : 0;
        for (auto& timers : payload.m_gateTrapTimers) for (uint32& timer : timers) timer = offline >= timer ? 0 : timer - uint32(offline);
        if (payload.m_auiEncounter[0] == IN_PROGRESS)
        {
            uint64 elapsed = !paused && now > saved ? (now - saved) * 1000 : 0;
            payload.m_baronRunTimer = elapsed >= payload.m_baronRunTimer ? 0 : payload.m_baronRunTimer - uint32(elapsed);
            if (!payload.m_baronRunTimer) payload.m_auiEncounter[0] = FAIL;
        }
        else payload.m_baronRunTimer = 0;
        for (uint32 i : {1u, 2u, 3u, 5u, 6u}) if (payload.m_auiEncounter[i] == IN_PROGRESS) payload.m_auiEncounter[i] = NOT_STARTED;
        payload.m_slaughterResumePending = (payload.m_slaughterPhase > 0 && payload.m_slaughterPhase < 6) ||
            (!payload.m_slaughterPhase && payload.GetData(TYPE_RAMSTEIN) == SPECIAL);
        payload.m_runPaused = false;
        return true;
    };
    if (parse()) *this = payload;
    m_restorePending = true;
    BuildSaveData();
}

uint32 instance_stratholme::GetData(uint32 type) const
{
    return type < MAX_ENCOUNTER ? m_auiEncounter[type] : 0;
}

static bool sortByHeight(Creature* first, Creature* second)
{
    return first && second && first->GetPositionZ() > second->GetPositionZ();
}

void instance_stratholme::DoSortZiggurats()
{
    if (m_acolyteGUIDs.empty())
        return;

    CreatureList acolytesList;                         // Valid pointers, only used locally
    for (auto acolyteGUID : m_acolyteGUIDs)
    {
        if (Creature* acolyte = instance->GetCreature(acolyteGUID))
            acolytesList.push_back(acolyte);
    }
    m_acolyteGUIDs.clear();

    if (acolytesList.empty())
        return;

    if (!GetSingleCreatureFromStorage(NPC_THUZADIN_ACOLYTE, true))
    {
        // Sort the acolytes by height, and the one with the biggest height is the announcer (a bit outside the map)
        acolytesList.sort(sortByHeight);
        m_npcEntryGuidStore[NPC_THUZADIN_ACOLYTE] = (*acolytesList.begin())->GetObjectGuid();
        acolytesList.erase(acolytesList.begin());
    }

    // Sort Acolytes
    for (auto itr = acolytesList.begin(); itr != acolytesList.end();)
    {
        bool isAlreadyIterated = false;
        for (auto& i : m_zigguratStorage)
        {
            if (GameObject* pZigguratDoor = instance->GetGameObject(i.m_doorGuid))
            {
                if ((*itr)->IsAlive() && (*itr)->IsWithinDistInMap(pZigguratDoor, 35.0f, false))
                {
                    i.m_zigguratAcolyteGuid.push_back((*itr)->GetObjectGuid());
                    itr = acolytesList.erase(itr);
                    isAlreadyIterated = true;
                    break;
                }
            }
        }

        if (itr != acolytesList.end() && !isAlreadyIterated)
            ++itr;
    }

    // In case some mobs have not been able to be sorted, store their GUIDs again
    for (CreatureList::const_iterator itr = acolytesList.begin(); itr != acolytesList.end(); ++itr)
        m_acolyteGUIDs.push_back((*itr)->GetObjectGuid());

    // Sort Crystal
    for (auto itr = m_crystalGUIDs.begin(); itr != m_crystalGUIDs.end();)
    {
        Creature* crystal = instance->GetCreature(*itr);
        if (!crystal)
        {
            itr = m_crystalGUIDs.erase(itr);
            continue;
        }

        bool isAlreadyIterated = false;
        for (auto& i : m_zigguratStorage)
        {
            if (GameObject* pZigguratDoor = instance->GetGameObject(i.m_doorGuid))
            {
                if (crystal->IsWithinDistInMap(pZigguratDoor, 50.0f, false))
                {
                    i.m_crystalGuid = crystal->GetObjectGuid();
                    itr = m_crystalGUIDs.erase(itr);
                    isAlreadyIterated = true;
                    break;
                }
            }
        }

        if (itr != m_crystalGUIDs.end() && !isAlreadyIterated)
            ++itr;
    }
}

void instance_stratholme::OnCreatureEnterCombat(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_BARONESS_ANASTARI: SetData(TYPE_BARONESS, IN_PROGRESS); break;
        case NPC_NERUBENKAN: SetData(TYPE_NERUB, IN_PROGRESS); break;
        case NPC_MALEKI_THE_PALLID: SetData(TYPE_PALLID, IN_PROGRESS); break;
        case NPC_BARON: SetData(TYPE_BARON, IN_PROGRESS); break;
        case NPC_ABOM_BILE: case NPC_ABOM_VENOM:
            if (creature->HasStaticDBSpawnData() && m_abomExpected.count(creature->GetDbGuid())) SetData(TYPE_RAMSTEIN, SPECIAL);
            break;
        default:
            for (auto const& slot : m_spawns) if (slot.guid == creature->GetObjectGuid() && !slot.killed)
            {
                if (slot.kind == 1) SetData(TYPE_RAMSTEIN, IN_PROGRESS);
                if (slot.kind == 2 || slot.kind == 3) SetData(TYPE_BLACK_GUARDS, IN_PROGRESS);
                if (slot.kind == 50 || slot.kind == 51)
                {
                    if (creature->IsTemporarySummon()) static_cast<TemporarySpawn*>(creature)->SetSummonProperties(TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 7200000);
                    for (auto& current : m_spawns) if (current.guid == creature->GetObjectGuid()) current.expires = uint64(time(nullptr)) + 7200;
                }
                break;
            }
    }
}

void instance_stratholme::OnCreatureEvade(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_BARONESS_ANASTARI: SetData(TYPE_BARONESS, FAIL); break;
        case NPC_NERUBENKAN: SetData(TYPE_NERUB, FAIL); break;
        case NPC_MALEKI_THE_PALLID: SetData(TYPE_PALLID, FAIL); break;
        case NPC_BARON: SetData(TYPE_BARON, FAIL); break;
        case NPC_ABOM_BILE: case NPC_ABOM_VENOM:
            if (!m_slaughterPhase) SetData(TYPE_RAMSTEIN, FAIL);
            break;
        default:
            for (auto const& slot : m_spawns) if (slot.guid == creature->GetObjectGuid() && !slot.killed)
            {
                if (slot.kind == 1) SetData(TYPE_RAMSTEIN, FAIL);
                if (slot.kind == 2 || slot.kind == 3) SetData(TYPE_BLACK_GUARDS, FAIL);
                break;
            }
    }
}

void instance_stratholme::OnCreatureDeath(Creature* creature)
{
    if (creature->HasStaticDBSpawnData())
    {
        uint32 guid = creature->GetDbGuid();
        bool expected = m_abomExpected.count(guid) != 0;
        for (auto const& members : m_towerExpected) expected = expected || members.count(guid) != 0;
        for (auto const& members : m_defenseExpected) expected = expected || members.count(guid) != 0;
        if (expected) m_staticDead.insert(guid);
    }
    for (auto& slot : m_spawns) if (slot.guid == creature->GetObjectGuid() && !slot.killed) { slot.killed = true; slot.created = true; break; }
    UpdatePairReward();
    AdvanceSlaughter();
    ThazudinAcolyteJustDied(creature);
    CheckDefenses(creature);
    SaveState();
    switch (creature->GetEntry())
    {
        case NPC_BARONESS_ANASTARI: SetData(TYPE_BARONESS, DONE); break;
        case NPC_MALEKI_THE_PALLID: SetData(TYPE_PALLID, DONE);   break;
        case NPC_NERUBENKAN:        SetData(TYPE_NERUB, DONE);    break;
        case NPC_BARON:             SetData(TYPE_BARON, DONE);    break;

        case NPC_AURIUS:
            DoScriptText(SAY_AURIUS_DEATH, creature);
            break;

        case NPC_THUZADIN_ACOLYTE:
        case NPC_ABOM_BILE: case NPC_ABOM_VENOM:
        case NPC_RAMSTEIN: case NPC_MINDLESS_UNDEAD: case NPC_BLACK_GUARD:
            break;

        // Scarlet Bastion defense and Timmy spawn support
        case NPC_CRIMSON_INITIATE:
        case NPC_CRIMSON_GALLANT:
        case NPC_CRIMSON_GUARDSMAN:
        case NPC_CRIMSON_CONJURER:
        case NPC_CRIMSON_BATTLE_MAGE:
            CheckDefenses();
            break;
        case NPC_BALNAZZAR:
            if (!(m_defenseTriggered & 256))
            {
                m_defenseTriggered |= 256;
                std::vector<uint32> entries = {NPC_SKELETAL_GUARDIAN,NPC_SKELETAL_GUARDIAN,NPC_SKELETAL_BERSERKER,NPC_SKELETAL_BERSERKER,urand(0,1) ? uint32(NPC_SKELETAL_GUARDIAN) : uint32(NPC_SKELETAL_BERSERKER)};
                std::shuffle(entries.begin(), entries.end(), *GetRandomGenerator());
                for (uint32 entry : entries) QueueSpawn(24, entry, scourgeInvaders[5], scourgeInvaders[6]);
                SaveState();
            }
            break;
        case NPC_JARIEN: case NPC_SOTHOS: break;
    }
}


void instance_stratholme::OnCreatureRespawn(Creature* creature)
{
    if (creature->GetEntry() == 16046)
    {
        if (m_pairTriggerSeen && PairEnded()) BeginNewPair();
        m_pairTriggerSeen = true; m_pairSpawner = creature->GetObjectGuid();
    }
    switch (creature->GetEntry())
    {
        case NPC_BARTHILAS:
            if (GetData(TYPE_BARTHILAS_RUN) != NOT_STARTED)
                creature->NearTeleportTo(stratholmeLocation[1].x, stratholmeLocation[1].y, stratholmeLocation[1].z, stratholmeLocation[1].o);
            break;

    }
    if (creature->GetEntry() == 11143 && m_postmasterCasting && creature->GetSpawnerGuid() == m_postmasterOwner && creature->GetUInt32Value(UNIT_CREATED_BY_SPELL) == SPELL_SUMMON_POSTMASTER)
        for (auto& slot : m_spawns) if (slot.kind == 40 && !slot.guid && !slot.killed)
        {
            slot.guid = creature->GetObjectGuid(); slot.created = true; slot.expires = uint64(time(nullptr)) + 180;
            m_auiEncounter[9] = DONE; SaveState(); break;
        }
    if (creature->GetEntry() == NPC_JARIEN || creature->GetEntry() == NPC_SOTHOS)
        if (Creature* trigger = instance->GetCreature(creature->GetSpawnerGuid()))
            if (trigger->GetEntry() == 16046)
            {
                uint32 kind = creature->GetEntry() == NPC_JARIEN ? 50 : 51;
                if (m_internalSpawnKind == kind) return;
                if (PairEnded()) BeginNewPair();
                bool found = false;
                for (auto& slot : m_spawns) if (slot.kind == kind)
                {
                    found = true;
                    if (!slot.created && !slot.killed) { slot.guid = creature->GetObjectGuid(); slot.created = true; }
                    else if (slot.guid != creature->GetObjectGuid()) m_excessActors.insert(creature->GetObjectGuid());
                    break;
                }
                if (!found)
                {
                    Position jarien{3421.199951f,-3056.520020f,136.498001f,0.343068f}, sothos{3425.850098f,-3054.75f,136.498001f,3.338580f};
                    QueueSpawn(50, NPC_JARIEN, jarien, jarien); QueueSpawn(51, NPC_SOTHOS, sothos, sothos);
                    for (auto& slot : m_spawns) { if (slot.kind == 50 || slot.kind == 51) slot.expires = uint64(time(nullptr)) + 7200; if (slot.kind == kind) { slot.guid = creature->GetObjectGuid(); slot.created = true; } }
                }
                m_pairSpawner = trigger->GetObjectGuid(); SaveState();
            }
    m_restorePending = true;
}


void instance_stratholme::ThazudinAcolyteJustDied(Creature*)
{
    for (uint32 i = 0; i < 3; ++i)
        if (m_auiEncounter[i + 1] == DONE && StaticMembersDead(m_towerExpected[i]))
        {
            int32 const texts[] = {SAY_ANNOUNCE_ZIGGURAT_1, SAY_ANNOUNCE_ZIGGURAT_2, SAY_ANNOUNCE_ZIGGURAT_3};
            DoOrSimulateScriptTextForThisInstance(texts[i], NPC_THUZADIN_ACOLYTE);
            if (Creature* crystal = instance->GetCreature(m_zigguratStorage[i].m_crystalGuid)) crystal->CastSpell(nullptr, SPELL_SUICIDE, TRIGGERED_OLD_TRIGGERED);
            SetData(i + 1, SPECIAL);
        }
}

void instance_stratholme::DoSpawnScarletGuards(uint8 step, Player* summoner)
{
    if (!summoner)
        return;

    uint32 npcEntry[2];
    uint8  index;

    switch (step)
    {
        case HALL_OF_LIGHTS:
            npcEntry[0] = NPC_CRIMSON_GALLANT;
            npcEntry[1] = NPC_CRIMSON_GALLANT;
            index = 0;
            break;
        case INNER_BASTION_2:
            npcEntry[0] = NPC_CRIMSON_MONK;
            npcEntry[1] = NPC_CRIMSON_SORCERER;
            index = 3;
            break;
        default:
            return;    // avoid indexing the following tables with a wrong index. Should never happen.
    }

    // Spawn the two guards and move them to where they will guard (each side of a door)
    for (uint8 i = 0; i < 2; i++)
    {
        if (Creature* temp = summoner->SummonCreature(npcEntry[i], scarletGuards[index].x, scarletGuards[index].y, scarletGuards[index].z, scarletGuards[index].o, TEMPSPAWN_DEAD_DESPAWN, 0))
        {
            temp->SetWalk(false);
            temp->GetMotionMaster()->MovePoint(0, scarletGuards[index + i + 1].x, scarletGuards[index + i + 1].y, scarletGuards[index + i + 1].z);
        }
    }
}

void instance_stratholme::DoSpawnScourgeInvaders(uint8 step, Player* summoner)
{
    if (!summoner)
        return;

    // Define the group of 5 Scourge invaders
    std::vector<uint32> mobList;                  // Vector holding the 5 creatures entries for each Scourge invaders group
    mobList.push_back(NPC_SKELETAL_GUARDIAN);     // 4 static NPC entries
    mobList.push_back(NPC_SKELETAL_GUARDIAN);
    mobList.push_back(NPC_SKELETAL_BERSERKER);
    mobList.push_back(NPC_SKELETAL_BERSERKER);

    uint32 mobEntry;                              // will hold the last random creature entry
    uint8  index;

    // Pick the fifth NPC in the group and randomize the five possible spawns
    switch (urand(0, 1))
    {
        case 0: mobEntry = NPC_SKELETAL_GUARDIAN;     break;
        case 1: mobEntry = NPC_SKELETAL_BERSERKER;    break;
    }

    mobList.push_back(mobEntry);
    std::shuffle(mobList.begin(), mobList.end(), *GetRandomGenerator());

    // Define the correct index for the spawn/move coords table
    switch (step)
    {
        case ENTRANCE:        index = 1; break;
        case INNER_BASTION_1: index = 3; break;
        case CRIMSON_THRONE:  index = 5; break;
        default: return;    // avoid indexing the following table with a wrong index. Should never happen.
    }

    // Summon the five invaders and make them run into the room
    for (uint8 i = 0; i < 5; i++)
    {
        float targetPosX, targetPosY, targetPosZ;

        if (Creature* temp = summoner->SummonCreature(mobList[i], scourgeInvaders[index].x, scourgeInvaders[index].y, scourgeInvaders[index].z, scourgeInvaders[index].o, TEMPSPAWN_DEAD_DESPAWN, 0))
        {
            temp->SetWalk(false);
            temp->GetRandomPoint(scourgeInvaders[index + 1].x, scourgeInvaders[index + 1].y, scourgeInvaders[index + 1].z, 6.0f, targetPosX, targetPosY, targetPosZ);
            temp->GetMotionMaster()->MovePoint(0, targetPosX, targetPosY, targetPosZ);
        }
    }
}

void instance_stratholme::DoMoveBackDefenders(uint8 step, Creature* creature)
{
    uint8 index;
    uint8 treshold = 0;
    uint8 foundGuards = 0;

    switch (step)
    {
        case BARRICADE:
            index = FIRST_BARRICADES;
            break;
        case STAIRS:
            index = BARRICADE;
            treshold = 3;
            break;
        default:
            return;     // avoid indexing the following table with a wrong index. Should never happen.
    }

    // Check that there are still defenders to move to the stairs/last barricade
    if (m_crimsonDefendersLowGuids[index].empty())
        return;
    if (creature)
        DoScriptText(ScarletEventYells[step], creature);

    for (auto guardGUID : m_crimsonDefendersLowGuids[index])
    {
        Creature* guard = instance->GetCreature(guardGUID);
        if (guard && guard->IsAlive() && !guard->IsInCombat())
        {
            guard->GetMotionMaster()->MoveIdle();
            guard->SetWalk(false);
            guard->GetMotionMaster()->MovePoint(0, scarletLastStand[treshold + foundGuards].x, scarletLastStand[treshold + foundGuards].y, scarletLastStand[treshold + foundGuards].z);
            foundGuards++;
        }

        if (foundGuards == 3)
            return;
    }
}

void instance_stratholme::DoScarletBastionDefense(uint8 step, Creature* creature)
{
    if (!creature)
        return;

    switch (step)
    {
        case BARRICADE:
        case STAIRS:
            DoMoveBackDefenders(step, creature);
            return;
        case TIMMY:
            creature->SummonCreature(NPC_TIMMY_THE_CRUEL, scourgeInvaders[0].x, scourgeInvaders[0].y, scourgeInvaders[0].z, scourgeInvaders[0].o, TEMPSPAWN_DEAD_DESPAWN, 0);
            return;
        // Scarlet guards spawned
        case HALL_OF_LIGHTS:
        case INNER_BASTION_2:
            DoScriptText(ScarletEventYells[step], creature);
            if (Player* player = GetPlayerInMap())
                DoSpawnScarletGuards(step, player);
            return;
        // Scourge invading
        case ENTRANCE:
        case INNER_BASTION_1:
            DoScriptText(ScarletEventYells[step], creature);
        case CRIMSON_THRONE:
            if (Player* player = GetPlayerInMap())
                DoSpawnScourgeInvaders(step, player);
        default:
            return;
    }
}

void instance_stratholme::DoGateTrap(uint8 gate)
{
    // Check if timer was not already set by another player/pet a few milliseconds before
    if (m_gateTrapTimers[gate][0])
        return;

    debug_log("SD2: Instance Stratholme - Rat Trap activated %i.", gate);
    // close the gates
    DoUseDoorOrButton(gates[2 * gate]);
    DoUseDoorOrButton(gates[2 * gate + 1]);

    // set timer to reset the trap
    m_gateTrapTimers[gate][0] = 30 * MINUTE * IN_MILLISECONDS;
    // set timer to reopen gates
    m_gateTrapTimers[gate][1] = 20 * IN_MILLISECONDS;
    // set timer to spawn the plagued critters
    m_gateTrapTimers[gate][2] = 2 * IN_MILLISECONDS;
    SaveState();
}

void instance_stratholme::DoSpawnPlaguedCritters(uint8 gate, Player* player)
{
    if (!player)
        return;

    uint32 entry = plaguedCritters[urand(0, 2)];
    for (uint8 i = 0; i < 30; ++i)
    {
        float fX, fY, fZ;
        player->GetRandomPoint(gateTrap[gate].x, gateTrap[gate].y, gateTrap[gate].z, 8.0f, fX, fY, fZ);
        if (Creature* critter = player->SummonCreature(entry, fX, fY, fZ, 0, TEMPSPAWN_DEAD_DESPAWN, 0))
            critter->Attack(player, true);
    }
}

void instance_stratholme::Update(uint32 diff)
{
    bool runFailedNow = false;
    if (m_restorePending) { m_restorePending = false; RestoreProgress(); }
    for (ObjectGuid guid : m_excessActors) if (Creature* actor = instance->GetCreature(guid)) if (actor->IsAlive()) actor->ForcedDespawn();
    m_excessActors.clear();
    if (m_slaughterResumePending && GetSlaughterParticipant())
    {
        m_slaughterResumePending = false;
        m_isSlaughterDoorOpen = m_slaughterPhase >= 4;
        for (auto& slot : m_spawns) slot.attempts = 0;
        SaveState(); SyncObjects();
    }
    for (uint32 gate = 0; gate < 2; ++gate)
    {
        if (m_gateTrapTimers[gate][0])
            m_gateTrapTimers[gate][0] = m_gateTrapTimers[gate][0] <= diff ? 0 : m_gateTrapTimers[gate][0] - diff;
        else
            for (auto const& ref : instance->GetPlayers())
                if (Player* player = ref.getSource())
                    if (player->IsAlive() && !player->IsGameMaster() && player->GetMap() == instance)
                    {
                        Pet* pet = player->GetPet();
                        if (player->IsWithinDist2d(gateTrap[gate].x, gateTrap[gate].y, 5.5f) || (pet && pet->IsAlive() && pet->IsWithinDist2d(gateTrap[gate].x, gateTrap[gate].y, 5.5f)))
                        { m_gateTrapTarget[gate] = player->GetObjectGuid(); DoGateTrap(uint8(gate)); break; }
                    }
        if (m_gateTrapTimers[gate][1])
        {
            if (m_gateTrapTimers[gate][1] <= diff)
            {
                if (GameObject* go = GetSingleGameObjectFromStorage(gates[2 * gate], true)) go->SetGoState(GO_STATE_ACTIVE);
                if (GameObject* go = GetSingleGameObjectFromStorage(gates[2 * gate + 1], true)) go->SetGoState(GO_STATE_ACTIVE);
                m_gateTrapTimers[gate][1] = 0;
            }
            else m_gateTrapTimers[gate][1] -= diff;
        }
        if (m_gateTrapTimers[gate][2])
        {
            if (m_gateTrapTimers[gate][2] <= diff)
            {
                Player* target = nullptr;
                for (auto const& ref : instance->GetPlayers()) if (Player* player = ref.getSource())
                    if (player->IsAlive() && !player->IsGameMaster() && player->GetMap() == instance && player->IsWithinDist2d(gateTrap[gate].x, gateTrap[gate].y, 15))
                    { if (!target || player->GetObjectGuid() == m_gateTrapTarget[gate]) target = player; }
                if (target) DoSpawnPlaguedCritters(uint8(gate), target);
                m_gateTrapTimers[gate][2] = 0;
            }
            else m_gateTrapTimers[gate][2] -= diff;
        }
    }
    if (m_barthilasRunTimer)
    {
        if (m_barthilasRunTimer <= diff)
        {
            if (Creature* actor = GetSingleCreatureFromStorage(NPC_BARTHILAS, true))
                if (actor->IsAlive() && !actor->IsInCombat()) actor->NearTeleportTo(stratholmeLocation[1].x, stratholmeLocation[1].y, stratholmeLocation[1].z, stratholmeLocation[1].o);
            m_barthilasRunTimer = 0; SetData(TYPE_BARTHILAS_RUN, DONE);
        }
        else m_barthilasRunTimer -= diff;
    }
    if (m_auriusSummonTimer)
    {
        Creature* baron = GetSingleCreatureFromStorage(NPC_BARON, true);
        if (GetData(TYPE_BARON) != IN_PROGRESS || !baron || !baron->IsAlive() || !baron->IsInCombat()) m_auriusSummonTimer = 0;
        else if (m_auriusSummonTimer <= diff)
        {
            bool joined = false;
            if (Creature* actor = GetSingleCreatureFromStorage(NPC_AURIUS, true))
                if (actor->IsAlive() && !actor->IsInCombat())
                {
                    float x, y, z; baron->GetRandomPoint(baron->GetPositionX(), baron->GetPositionY(), baron->GetPositionZ(), 4, x, y, z);
                    actor->SetStandState(UNIT_STAND_STATE_STAND); actor->NearTeleportTo(x, y, z, actor->GetOrientation()); actor->SetRespawnCoord(x, y, z, actor->GetOrientation());
                    DoScriptText(SAY_AURIUS_AGGRO, actor); actor->SetFactionTemporary(FACTION_VICTIM, TEMPFACTION_RESTORE_RESPAWN);
                    actor->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP); actor->AI()->AttackStart(baron);
                    joined = actor->IsInCombat();
                }
            m_auriusSummonTimer = joined || ++m_auriusAttempts >= 5 ? 0 : 1000;
        }
        else m_auriusSummonTimer -= diff;
    }
    if (GetData(TYPE_BARON_RUN) == IN_PROGRESS && !m_runPaused && m_baronRunTimer)
    {
        uint32 const threshold[] = {600000, 300000, 290000};
        int32 const texts[] = {SAY_ANNOUNCE_RUN_10_MIN, SAY_ANNOUNCE_RUN_5_MIN, SAY_YSIDA_RUN_5_MIN};
        if (m_yellCounter < 3 && m_baronRunTimer <= threshold[m_yellCounter])
        { DoOrSimulateScriptTextForThisInstance(texts[m_yellCounter], m_yellCounter == 2 ? NPC_YSIDA : NPC_BARON); ++m_yellCounter; SaveState(); }
        if (m_baronRunTimer <= diff)
        {
            SetData(TYPE_BARON_RUN, FAIL);
            m_runFailTimer = 8000;
            runFailedNow = true; SaveState();
            DoOrSimulateScriptTextForThisInstance(SAY_ANNOUNCE_RUN_FAIL, NPC_BARON);
            if (Creature* ysida = GetSingleCreatureFromStorage(NPC_YSIDA, true)) ysida->GetMotionMaster()->MovePoint(0, stratholmeLocation[8].x, stratholmeLocation[8].y, stratholmeLocation[8].z);
        }
        else m_baronRunTimer -= diff;
    }
    if (m_runFailTimer && !runFailedNow)
    {
        if (m_runFailTimer <= diff)
        {
            m_runFailTimer = 0;
            if (Creature* ysida = GetSingleCreatureFromStorage(NPC_YSIDA, true))
                if (Creature* baron = GetSingleCreatureFromStorage(NPC_BARON, true)) baron->CastSpell(ysida, SPELL_BARON_SOUL_DRAIN, TRIGGERED_NONE);
            DoOrSimulateScriptTextForThisInstance(SAY_YSIDA_RUN_FAIL, NPC_YSIDA);
        }
        else m_runFailTimer -= diff;
    }
    if (m_ysidaTimer <= diff) { m_ysidaTimer = 1000; EnsureYsida(); } else m_ysidaTimer -= diff;
    if (!m_slaughterResumePending && !m_retryBlocked)
    {
        if (m_slaughterPhase == 2 && m_mindlessSummonTimer <= diff)
        {
            m_mindlessSummonTimer = 400;
            for (auto& slot : m_spawns) if (slot.kind == 2 && !slot.killed && !instance->GetCreature(slot.guid))
            {
                if (!SpawnSlot(slot) && slot.attempts >= 5) { m_retryBlocked = true; m_auiEncounter[8] = FAIL; SyncObjects(); SaveState(); }
                break;
            }
        }
        else if (m_slaughterPhase == 2) m_mindlessSummonTimer -= diff;
        if (m_slaughterPhase == 3 && m_blackGuardsTimer)
        {
            if (m_blackGuardsTimer <= diff)
            {
                m_blackGuardsTimer = 0; m_slaughterPhase = 4; DoOpenSlaughterhouseDoor(true);
                for (auto const& slot : m_spawns) if (slot.kind == 3) if (Creature* actor = instance->GetCreature(slot.guid)) if (actor->IsAlive() && !actor->IsInCombat()) actor->GetMotionMaster()->MovePoint(0, slot.dx, slot.dy, slot.dz);
                SaveState();
            }
            else m_blackGuardsTimer -= diff;
        }
        if (m_slaughterDoorTimer)
        {
            if (m_slaughterDoorTimer <= diff) { m_slaughterDoorTimer = 0; DoOpenSlaughterhouseDoor(false); } else m_slaughterDoorTimer -= diff;
        }
    }
    ProcessSpawns(diff);
    AdvanceSlaughter();
    if (m_slaughterSquareTimer && !m_slaughterPhase && !m_slaughterResumePending && !m_retryBlocked)
    {
        if (m_slaughterSquareTimer <= diff)
        {
            for (auto it = m_abominationGUIDs.begin(); it != m_abominationGUIDs.end();)
            {
                Creature* actor = instance->GetCreature(*it);
                if (!actor) { it = m_abominationGUIDs.erase(it); continue; }
                ++it;
                if (!actor->IsAlive() || actor->IsInCombat() || actor->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE) continue;
                float x, y, z; actor->GetRandomPoint(4037.15f, -3469.15f, 121.804f, 10, x, y, z); actor->GetMotionMaster()->MovePoint(0, x, y, z); break;
            }
            m_slaughterSquareTimer = urand(30, 45) * 1000;
        }
        else m_slaughterSquareTimer -= diff;
    }
    if (m_saveTimer <= diff) { m_saveTimer = 30000; SaveState(); } else m_saveTimer -= diff;
}


bool instance_stratholme::CheckConditionCriteriaMeet(Player const* player, uint32 instanceConditionId, WorldObject const* conditionSource, uint32 conditionSourceType) const
{
    if (instanceConditionId == INSTANCE_CONDITION_ID_NORMAL_MODE)   // Aurius event during fight against Baron Rivendare
        return GetData(TYPE_AURIUS) == DONE;
    else
        script_error_log("instance_stratholme::CheckConditionCriteriaMeet called with unsupported Id %u. Called with param plr %s, src %s, condition source type %u",
                     instanceConditionId, player ? player->GetGuidStr().c_str() : "nullptr", conditionSource ? conditionSource->GetGuidStr().c_str() : "nullptr", conditionSourceType);
    return false;
}

void instance_stratholme::BuildSaveData()
{
    std::ostringstream out;
    out << std::setprecision(9);
    for (uint32 i = 0; i < 8; ++i) out << m_auiEncounter[i] << ' ';
    out << "ST2 " << m_auiEncounter[8] << ' ' << m_auiEncounter[9] << ' ' << m_baronRunTimer << ' ' << uint64(time(nullptr)) << ' '
        << m_runPaused << ' ' << m_yellCounter << ' ' << m_slaughterPhase << ' ' << m_blackGuardsTimer << ' ' << m_slaughterDoorTimer << ' '
        << m_defenseTriggered << ' ' << m_pairBox << ' ' << m_legacy << ' ' << m_runFailTimer << ' ' << m_auriusPositionReady
        << ' ' << m_auriusPosition.x << ' ' << m_auriusPosition.y << ' ' << m_auriusPosition.z << ' ' << m_auriusPosition.o
        << ' ' << m_slaughterSquareTimer << ' ' << m_mindlessSummonTimer;
    for (auto const& timers : m_gateTrapTimers) for (uint32 timer : timers) out << ' ' << timer;
    out << ' ' << m_pairGeneration << ' ' << m_pairRewards.size();
    for (auto const& request : m_pairRewards) out << ' ' << request.generation << ' ' << request.owner.GetRawValue();
    auto writeSet = [&](std::set<uint32> const& values) { out << ' ' << values.size(); for (uint32 value : values) out << ' ' << value; };
    writeSet(m_staticDead); writeSet(m_postboxGuids);
    out << ' ' << m_spawns.size();
    for (auto const& slot : m_spawns)
        out << ' ' << slot.kind << ' ' << slot.entry << ' ' << slot.created << ' ' << slot.killed << ' ' << slot.expires
            << ' ' << slot.x << ' ' << slot.y << ' ' << slot.z << ' ' << slot.o << ' ' << slot.dx << ' ' << slot.dy << ' ' << slot.dz;
    out << " END";
    m_strInstData = out.str();
}

void instance_stratholme::SaveState() { BuildSaveData(); SaveToDB(); }

void instance_stratholme::BuildStaticMembers()
{
    if (m_staticReady) return;
    auto worker = [&](CreatureDataPair const& pair) -> bool
    {
        CreatureData const& data = pair.second;
        if (data.mapid != instance->GetId()) return false;
        if (data.id == NPC_ABOM_BILE || data.id == NPC_ABOM_VENOM) m_abomExpected.insert(pair.first);
        if (data.id == NPC_THUZADIN_ACOLYTE)
        {
            float const doors[3][2] = {{3853.47f,-3722.94f},{3851.70f,-3522.96f},{4038.13f,-3649.91f}};
            for (uint32 i = 0; i < 3; ++i)
                if (std::hypot(data.posX - doors[i][0], data.posY - doors[i][1]) < 35.0f) m_towerExpected[i].insert(pair.first);
        }
        auto eligible = [](uint32 entry) { return entry == NPC_CRIMSON_INITIATE || entry == NPC_CRIMSON_GALLANT || entry == NPC_CRIMSON_GUARDSMAN || entry == NPC_CRIMSON_CONJURER || entry == NPC_CRIMSON_BATTLE_MAGE; };
        bool defender = eligible(data.id);
        if (!data.id)
        {
            auto actual = m_staticEntries.find(pair.first);
            if (actual != m_staticEntries.end()) defender = eligible(actual->second);
            else if (auto entries = sObjectMgr.GetAllRandomCreatureEntries(pair.first))
                for (uint32 entry : *entries) defender = defender || eligible(entry);
        }
        if (defender)
        {
            if (std::hypot(data.posX - defensePoints[TIMMY].x, data.posY - defensePoints[TIMMY].y) <= 40.0f) m_defenseExpected[TIMMY].insert(pair.first);
            for (uint32 i = 0; i < MAX_DEFENSE_POINTS; ++i)
                if (i != TIMMY && std::hypot(data.posX - defensePoints[i].x, data.posY - defensePoints[i].y) <= 8.0f) { m_defenseExpected[i].insert(pair.first); break; }
        }
        return false;
    };
    sObjectMgr.DoCreatureData(worker);
    m_staticReady = true;
}

bool instance_stratholme::StaticMembersDead(std::set<uint32> const& members) const
{
    if (members.empty()) return false;
    for (uint32 guid : members) if (!m_staticDead.count(guid)) return false;
    return true;
}

bool instance_stratholme::StartBaronRun(Player* player)
{
    if (!player || !player->IsAlive() || player->IsGameMaster() || player->GetMap() != instance || GetData(TYPE_BARON_RUN) != NOT_STARTED) return false;
    m_auiEncounter[0] = IN_PROGRESS; m_baronRunTimer = 45 * MINUTE * IN_MILLISECONDS; m_yellCounter = 0; m_runPaused = false;
    SaveState();
    DoOrSimulateScriptTextForThisInstance(SAY_ANNOUNCE_RUN_START, NPC_BARON);
    for (auto const& ref : instance->GetPlayers()) if (Player* target = ref.getSource()) SyncRunAura(target);
    m_ysidaAttempts = 0; m_ysidaTimer = 1;
    return true;
}

void instance_stratholme::SyncRunAura(Player* player)
{
    if (!player || player->GetMap() != instance || !player->IsAlive() || player->IsGameMaster()) return;
    if (GetData(TYPE_BARON_RUN) == IN_PROGRESS && m_baronRunTimer)
    {
        if (!player->HasAura(SPELL_BARON_ULTIMATUM)) player->CastSpell(player, SPELL_BARON_ULTIMATUM, TRIGGERED_OLD_TRIGGERED);
        if (SpellAuraHolder* aura = player->GetSpellAuraHolder(SPELL_BARON_ULTIMATUM)) aura->SetAuraDuration(m_baronRunTimer);
    }
    else player->RemoveAurasDueToSpell(SPELL_BARON_ULTIMATUM);
}

bool instance_stratholme::IsSlaughterParticipant(Player* player) const
{
    // The two approach ports are at y=-3504/-3469; the fighting square is north
    // of both, at floor 115..122. Do not close the approach on somebody outside.
    return player && player->GetMap() == instance && player->IsAlive() && !player->IsGameMaster() &&
        player->GetPositionX() >= 3965 && player->GetPositionX() <= 4110 && player->GetPositionY() >= -3462 && player->GetPositionY() <= -3370 &&
        player->GetPositionZ() >= 110 && player->GetPositionZ() <= 130;
}

Player* instance_stratholme::GetSlaughterParticipant() const
{
    for (auto const& ref : instance->GetPlayers()) if (IsSlaughterParticipant(ref.getSource())) return ref.getSource();
    return nullptr;
}

void instance_stratholme::QueueSpawn(uint32 kind, uint32 entry, Position const& pos, Position const& dest)
{
    StratholmeSpawn slot; slot.kind = kind; slot.entry = entry;
    slot.x = pos.x; slot.y = pos.y; slot.z = pos.z; slot.o = pos.o;
    slot.dx = dest.x; slot.dy = dest.y; slot.dz = dest.z;
    m_spawns.push_back(slot);
}

bool instance_stratholme::AllKilled(uint32 kind, uint32 count) const
{
    uint32 found = 0;
    for (auto const& slot : m_spawns) if (slot.kind == kind) { ++found; if (!slot.created || !slot.killed) return false; }
    return found == count;
}

void instance_stratholme::AdvanceSlaughter()
{
    if (!m_staticReady) return;
    bool changed = false;
    if (!m_slaughterPhase && m_abomExpected.size() == 13 && StaticMembersDead(m_abomExpected) &&
        m_auiEncounter[1] == SPECIAL && m_auiEncounter[2] == SPECIAL && m_auiEncounter[3] == SPECIAL)
    {
        m_slaughterPhase = 1; m_slaughterSquareTimer = 0;
        QueueSpawn(1, NPC_RAMSTEIN, stratholmeLocation[2], stratholmeLocation[5]);
        changed = true;
    }
    if (m_slaughterPhase == 1 && AllKilled(1, 1))
    {
        m_slaughterPhase = 2; m_auiEncounter[TYPE_RAMSTEIN] = DONE;
        m_mindlessCount = 0; m_mindlessSummonTimer = 500;
        DoOpenSlaughterhouseDoor(false);
        for (uint32 i = 0; i < 30; ++i)
        {
            Position dest = stratholmeLocation[5];
            if (Player* player = GetSlaughterParticipant()) player->GetRandomPoint(dest.x, dest.y, dest.z, 20.0f, dest.x, dest.y, dest.z);
            QueueSpawn(2, NPC_MINDLESS_UNDEAD, stratholmeLocation[4], dest);
        }
        for (uint32 i = 0; i < 5; ++i) QueueSpawn(3, NPC_BLACK_GUARD, stratholmeLocation[6], stratholmeLocation[5]);
        changed = true;
    }
    if (m_slaughterPhase == 2 && AllKilled(2, 30)) { m_slaughterPhase = 3; m_blackGuardsTimer = 60000; changed = true; }
    if (m_slaughterPhase == 4 && AllKilled(3, 5))
    {
        m_slaughterPhase = 6; m_auiEncounter[TYPE_BLACK_GUARDS] = DONE; m_slaughterResumePending = false;
        DoOpenSlaughterhouseDoor(true);
        DoOrSimulateScriptTextForThisInstance(SAY_UNDEAD_DEFEAT, NPC_BARON);
        changed = true;
    }
    if (changed) { SaveState(); SyncObjects(); }
}

bool instance_stratholme::SpawnSlot(StratholmeSpawn& slot)
{
    uint64 const now = uint64(time(nullptr));
    if (slot.killed) return true;
    if (slot.expires && slot.expires <= now) return true;
    if (Creature* actor = instance->GetCreature(slot.guid)) return actor->IsAlive();
    if (slot.attempts >= 5) return false;
    WorldObject* owner = slot.kind <= 3 ? static_cast<WorldObject*>(GetSlaughterParticipant()) :
        slot.kind == 50 || slot.kind == 51 ? static_cast<WorldObject*>(instance->GetCreature(m_pairSpawner)) : static_cast<WorldObject*>(GetPlayerInMap());
    if (!owner) return false;
    ++slot.attempts;
    Creature* actor;
    if (slot.kind == 40)
    {
        TempSpawnSettings settings(owner, slot.entry, slot.x, slot.y, slot.z, slot.o, TEMPSPAWN_TIMED_OR_DEAD_DESPAWN,
            slot.expires ? uint32(slot.expires - now) * 1000 : 180000, false, false, 0, 0, 0, false, false, SPELL_SUMMON_POSTMASTER, -1, 60);
        actor = WorldObject::SummonCreature(settings, instance);
        if (actor && !slot.expires) { slot.expires = now + 180; m_auiEncounter[9] = DONE; }
    }
    else
    {
        m_internalSpawnKind = slot.kind;
        bool pair = slot.kind == 50 || slot.kind == 51;
        actor = owner->SummonCreature(slot.entry, slot.x, slot.y, slot.z, slot.o, pair ? TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN : TEMPSPAWN_DEAD_DESPAWN,
            pair ? uint32(slot.expires > now ? slot.expires - now : 7200) * 1000 : 0);
        m_internalSpawnKind = 0;
    }
    if (!actor) return false;
    slot.guid = actor->GetObjectGuid(); slot.created = true; slot.attempts = 0;
    if (slot.kind == 1) { DoOpenSlaughterhouseDoor(true); m_slaughterDoorTimer = 10000; }
    if (slot.kind == 1 || slot.kind == 2 || (slot.kind == 3 && m_slaughterPhase >= 4) || (slot.kind >= 16 && slot.kind <= 24 && slot.kind != 18))
    { actor->SetWalk(false); actor->GetMotionMaster()->MovePoint(0, slot.dx, slot.dy, slot.dz); }
    SaveState();
    return true;
}

void instance_stratholme::ProcessSpawns(uint32 diff)
{
    if (m_spawnTimer > diff) { m_spawnTimer -= diff; return; }
    m_spawnTimer = 1000;
    bool blocked = false;
    // Each kind is a fixed persisted request. Failed slots never reroll or duplicate siblings.
    for (auto& slot : m_spawns)
    {
        if (slot.kind == 50 || slot.kind == 51)
            if (Creature* actor = instance->GetCreature(slot.guid)) if (actor->IsAlive() && actor->IsInCombat()) slot.expires = uint64(time(nullptr)) + 7200;
        if (slot.kind == 2 || slot.kind == 40 || slot.killed) continue;
        if (slot.kind <= 3 && (m_slaughterResumePending || m_retryBlocked)) continue;
        if (!SpawnSlot(slot) && slot.kind <= 3 && slot.attempts >= 5) blocked = true;
    }
    if (blocked)
    {
        m_retryBlocked = true;
        m_auiEncounter[m_slaughterPhase == 1 ? TYPE_RAMSTEIN : TYPE_BLACK_GUARDS] = FAIL;
        SaveState(); SyncObjects();
    }
    for (auto& slot : m_spawns) if (slot.kind == 40 && !slot.killed && !instance->GetCreature(slot.guid))
        SpawnSlot(slot);
    UpdatePairReward();
}

bool instance_stratholme::PairEnded() const
{
    uint32 found = 0;
    for (auto const& slot : m_spawns) if (slot.kind == 50 || slot.kind == 51)
    {
        ++found;
        if (!slot.killed)
        {
            if (!slot.expires || slot.expires > uint64(time(nullptr))) return false;
            if (Creature* actor = instance->GetCreature(slot.guid)) if (actor->IsAlive()) return false;
        }
    }
    return found == 2;
}

void instance_stratholme::BeginNewPair()
{
    // Current earned corpses and earlier unconfirmed box requests survive a new
    // legitimate SpellHit request on the same static trigger.
    m_spawns.erase(std::remove_if(m_spawns.begin(), m_spawns.end(), [](StratholmeSpawn const& slot) { return slot.kind == 50 || slot.kind == 51; }), m_spawns.end());
    ++m_pairGeneration; m_pairBox = false;
    SaveState();
}

void instance_stratholme::UpdatePairReward()
{
    if (!m_pairBox && AllKilled(50, 1) && AllKilled(51, 1))
    {
        bool queued = false;
        for (auto const& request : m_pairRewards) if (request.generation == m_pairGeneration) queued = true;
        if (!queued) { StratholmeBoxRequest request; request.generation = m_pairGeneration; request.owner = m_pairSpawner; m_pairRewards.push_back(request); SaveState(); }
    }
    std::vector<uint32> generations;
    for (auto const& request : m_pairRewards) if (request.attempts < 5) generations.push_back(request.generation);
    for (uint32 generation : generations)
        for (auto& request : m_pairRewards) if (request.generation == generation)
        {
            Creature* owner = instance->GetCreature(request.owner);
            if (!owner) break;
            ++request.attempts;
            m_pairBoxCastingOwner = request.owner; m_pairBoxCastingGeneration = request.generation; m_pairBoxCasting = true;
            owner->CastSpell(nullptr, SPELL_SUMMON_WINNER_BOX, TRIGGERED_OLD_TRIGGERED);
            m_pairBoxCasting = false;
            break; // OnObjectSpawn may have removed this request synchronously.
        }
}

void instance_stratholme::SyncObjects()
{
    auto door = [&](uint32 entry, bool open) { if (GameObject* go = GetSingleGameObjectFromStorage(entry, true)) go->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY); };
    for (uint32 i = 0; i < 3; ++i) door(i == 0 ? GO_ZIGGURAT_DOOR_1 : i == 1 ? GO_ZIGGURAT_DOOR_2 : GO_ZIGGURAT_DOOR_3, m_auiEncounter[i + 1] == DONE || m_auiEncounter[i + 1] == SPECIAL);
    bool ready = m_auiEncounter[1] == SPECIAL && m_auiEncounter[2] == SPECIAL && m_auiEncounter[3] == SPECIAL;
    bool fighting = (m_slaughterPhase > 0 && m_slaughterPhase < 6 && !m_slaughterResumePending && !m_retryBlocked && GetData(TYPE_RAMSTEIN) != FAIL && GetData(TYPE_BLACK_GUARDS) != FAIL) ||
        (!m_slaughterPhase && GetData(TYPE_RAMSTEIN) == SPECIAL && !m_slaughterResumePending && !m_retryBlocked) || GetData(TYPE_BARON) == IN_PROGRESS;
    door(GO_PORT_GAUNTLET, ready && !fighting);
    door(GO_PORT_SLAUGHTER, ready);
    door(GO_PORT_SLAUGHTER_GATE, m_slaughterPhase >= 2);
    door(GO_ZIGGURAT_DOOR_4, m_isSlaughterDoorOpen || m_slaughterPhase == 6);
    door(GO_ZIGGURAT_DOOR_5, m_slaughterPhase == 6 && GetData(TYPE_BARON) != IN_PROGRESS);
    door(GO_YSIDA_CAGE, GetData(TYPE_BARON_RUN) == DONE || GetData(TYPE_BARON_RUN) == FAIL);
    for (uint32 i = 0; i < 2; ++i) { door(gates[2 * i], !m_gateTrapTimers[i][1]); door(gates[2 * i + 1], !m_gateTrapTimers[i][1]); }
}

void instance_stratholme::EnsureYsida()
{
    uint32 state = GetData(TYPE_BARON_RUN);
    if (state == NOT_STARTED) return;
    Creature* ysida = GetSingleCreatureFromStorage(NPC_YSIDA, true);
    if (ysida && !ysida->IsAlive()) return; // A real Soul Drain corpse is already the single failed-event actor.
    if (!ysida || !ysida->IsAlive())
    {
        if (m_ysidaAttempts++ >= 5) return;
        WorldObject* owner = GetPlayerInMap();
        if (!owner) return;
        Position const& pos = stratholmeLocation[state == IN_PROGRESS ? 7 : 8];
        ysida = owner->SummonCreature(NPC_YSIDA, pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_DEAD_DESPAWN, 0);
        if (!ysida) return;
    }
    if (state == IN_PROGRESS) ysida->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
    else if (state == DONE) ysida->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
    else if (!m_runFailTimer && !ysida->HasAura(29266)) ysida->CastSpell(ysida, 29266, TRIGGERED_OLD_TRIGGERED);
}

void instance_stratholme::SyncAurius()
{
    Creature* aurius = GetSingleCreatureFromStorage(NPC_AURIUS, true);
    if (!aurius || !aurius->IsAlive()) return; // Never resurrect his real death/cooldown.
    if (GetData(TYPE_AURIUS) == DONE && GetData(TYPE_BARON) == DONE)
    {
        Position const& pos = m_auriusPositionReady ? m_auriusPosition : stratholmeLocation[8];
        aurius->NearTeleportTo(pos.x, pos.y, pos.z, pos.o);
        aurius->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
        aurius->SetHealth(1); aurius->SetStandState(UNIT_STAND_STATE_DEAD);
    }
    else if (GetData(TYPE_AURIUS) == DONE) aurius->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    else aurius->SetStandState(UNIT_STAND_STATE_KNEEL);
}

void instance_stratholme::RestoreProgress()
{
    BuildStaticMembers();
    auto absorb = [&](std::set<uint32> const& members)
    {
        for (uint32 guid : members) if (instance->GetPersistentState()->GetCreatureRespawnTime(guid) > time(nullptr)) m_staticDead.insert(guid);
    };
    absorb(m_abomExpected);
    for (auto const& members : m_towerExpected) absorb(members);
    for (auto const& members : m_defenseExpected) absorb(members);
    DoSortZiggurats();
    ThazudinAcolyteJustDied(nullptr);
    CheckDefenses();
    uint32 const oldPhase = m_slaughterPhase;
    AdvanceSlaughter();
    if (!oldPhase && m_slaughterPhase && m_slaughterPhase < 6) m_slaughterResumePending = true;
    SyncAurius(); SyncObjects();
    SaveState();
}

void instance_stratholme::CheckDefenses(Creature* speaker)
{
    if (!m_staticReady) return;
    for (uint32 step = 0; step < FIRST_BARRICADES; ++step)
        if (!(m_defenseTriggered & (1u << step)) && StaticMembersDead(m_defenseExpected[step]))
        {
            m_defenseTriggered |= 1u << step;
            if (m_legacy) { SaveState(); continue; } // Unknown old Timmy/wave rewards stay settled.
            if (step == BARRICADE || step == STAIRS) DoMoveBackDefenders(step, speaker);
            else if (step != TIMMY && speaker) DoScriptText(ScarletEventYells[step], speaker);
            if (step == TIMMY) QueueSpawn(16 + step, NPC_TIMMY_THE_CRUEL, scourgeInvaders[0], scourgeInvaders[0]);
            if (step == HALL_OF_LIGHTS || step == INNER_BASTION_2)
            {
                uint32 start = step == HALL_OF_LIGHTS ? 0 : 3;
                for (uint32 i = 0; i < 2; ++i) QueueSpawn(16 + step, step == HALL_OF_LIGHTS ? uint32(NPC_CRIMSON_GALLANT) : i ? uint32(NPC_CRIMSON_SORCERER) : uint32(NPC_CRIMSON_MONK), scarletGuards[start], scarletGuards[start + i + 1]);
            }
            if (step == ENTRANCE || step == INNER_BASTION_1 || step == CRIMSON_THRONE)
            {
                uint32 start = step == ENTRANCE ? 1 : step == INNER_BASTION_1 ? 3 : 5;
                std::vector<uint32> entries = {NPC_SKELETAL_GUARDIAN, NPC_SKELETAL_GUARDIAN, NPC_SKELETAL_BERSERKER, NPC_SKELETAL_BERSERKER, urand(0, 1) ? uint32(NPC_SKELETAL_GUARDIAN) : uint32(NPC_SKELETAL_BERSERKER)};
                std::shuffle(entries.begin(), entries.end(), *GetRandomGenerator());
                for (uint32 entry : entries)
                {
                    Position dest = scourgeInvaders[start + 1];
                    if (Player* player = GetPlayerInMap()) player->GetRandomPoint(dest.x, dest.y, dest.z, 6, dest.x, dest.y, dest.z);
                    QueueSpawn(16 + step, entry, scourgeInvaders[start], dest);
                }
            }
            SaveState();
        }
}

bool instance_stratholme::UsePostbox(Player* player, GameObject* go)
{
    if (m_legacy) return false; // Old records do not identify already awarded mailbox spawns.
    if (!player || !go || !player->IsAlive() || player->IsGameMaster() || player->GetMap() != instance || go->GetMap() != instance || !go->GetDbGuid() ||
        instance->GetGameObject(go->GetObjectGuid()) != go || m_postboxGuids.count(go->GetDbGuid())) return false;
    uint32 const entries[] = {176346,176349,176350,176351,176352,176353};
    if (std::find(std::begin(entries), std::end(entries), go->GetEntry()) == std::end(entries)) return false;
    uint32 index = uint32(m_postboxGuids.size());
    if (index >= 6) return false;
    m_postboxGuids.insert(go->GetDbGuid()); m_postboxesUsed = uint8(m_postboxGuids.size());
    if (index >= 3) { SaveState(); return true; }
    Position pos{player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->GetOrientation()};
    for (uint32 i = 0; i < 3; ++i)
    {
        Position spawn = pos; player->GetRandomPoint(pos.x, pos.y, pos.z, 3, spawn.x, spawn.y, spawn.z);
        QueueSpawn(32 + index, NPC_UNDEAD_POSTMAN, spawn, spawn);
    }
    if (index == 2)
    {
        QueueSpawn(40, 11143, pos, pos); m_auiEncounter[9] = SPECIAL;
        SaveState();
        m_postmasterOwner = player->GetObjectGuid(); m_postmasterCasting = true;
        player->CastSpell(player, SPELL_SUMMON_POSTMASTER, TRIGGERED_OLD_TRIGGERED);
        m_postmasterCasting = false;
    }
    SaveState();
    return true;
}

InstanceData* GetInstanceData_instance_stratholme(Map* map)
{
    return new instance_stratholme(map);
}

void AddSC_instance_stratholme()
{
    Script* newScript = new Script;
    newScript->Name = "instance_stratholme";
    newScript->GetInstanceData = &GetInstanceData_instance_stratholme;
    newScript->RegisterSelf();
}
