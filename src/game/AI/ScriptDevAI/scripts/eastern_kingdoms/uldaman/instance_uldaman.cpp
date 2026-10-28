/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License version 2 or any later version. */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "uldaman.h"
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnGroup.h"
#include <ctime>
#include <sstream>

instance_uldaman::instance_uldaman(Map* map) : ScriptedInstance(map)
{
    Initialize();
}

void instance_uldaman::Initialize()
{
    memset(m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_playerGuid.Clear();
    m_lWardens.clear();
    m_lKeepers.clear();
    m_deadKeepers.clear();
    m_activeKeeper.Clear();
    m_uiKeeperCooldown = 0;
    m_resettingKeepers = m_pendingKeeperReset = false;
    m_annoraUnlocked = false;
    m_restoreAnnoraKills = true;
    m_annoraScorpionDbGuids.clear();
    m_annoraDeadScorpionDbGuids.clear();
    if (auto groups = sObjectMgr.GetSpawnGroupContainer())
    {
        auto found = groups->spawnGroupMap.find(GROUP_ANNORA_SCORPIDS);
        if (found != groups->spawnGroupMap.end())
            for (auto const& member : found->second.DbGuids)
                m_annoraScorpionDbGuids.insert(member.DbGuid);
    }
    m_ironayaGuid.Clear();
    m_ironayaStage = m_ironayaSummonFailures = 0;
    m_ironayaTimer = 0;
    m_restoreIronaya = false;
    m_worldStateTimer = 1000;
    m_treasureState = TREASURE_UNAVAILABLE;
    m_treasureExpires = 0;
    m_treasureSnapshot.clear();
    instance->GetVariableManager().SetVariable(WORLD_STATE_CUSTOM_SPAWN_ANNORA, 0);
    BuildSaveData();
}

void instance_uldaman::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_TEMPLE_DOOR_UPPER:
        case GO_TEMPLE_DOOR_LOWER:
        case GO_ANCIENT_VAULT:
        case GO_KEYSTONE:
        case GO_IRONAYA_SEAL:
        case GO_ANCIENT_TREASURE:
            m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
            // LoadFromDB applies negative-spawn metadata after this callback.
            if (go->GetEntry() != GO_ANCIENT_TREASURE)
                SyncObject(go);
            break;
    }
}

void instance_uldaman::OnObjectSpawn(GameObject* go)
{
    SyncObject(go);
}

void instance_uldaman::OnObjectDespawn(GameObject* go)
{
    // Natural chest depletion/expiry calls this; an ordinary grid unload does not.
    if (go->GetEntry() == GO_ANCIENT_TREASURE && m_treasureState == TREASURE_AVAILABLE)
        CloseTreasure();
}

void instance_uldaman::SyncObject(GameObject* go)
{
    bool open = false;
    switch (go->GetEntry())
    {
        case GO_TEMPLE_DOOR_UPPER:
            open = GetData(TYPE_ALTAR_EVENT) == DONE;
            break;
        case GO_TEMPLE_DOOR_LOWER:
            open = GetData(TYPE_ALTAR_EVENT) == DONE && GetData(TYPE_ARCHAEDAS) != IN_PROGRESS && GetData(TYPE_ARCHAEDAS) != SPECIAL;
            break;
        case GO_ANCIENT_VAULT:
            open = GetData(TYPE_ARCHAEDAS) == DONE;
            break;
        case GO_IRONAYA_SEAL:
            open = GetData(TYPE_IRONAYA) == SPECIAL || GetData(TYPE_IRONAYA) == DONE;
            break;
        case GO_KEYSTONE:
            if (GetData(TYPE_IRONAYA) != NOT_STARTED)
                go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            else
                go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            open = GetData(TYPE_IRONAYA) != NOT_STARTED;
            break;
        case GO_ANCIENT_TREASURE:
        {
            if (go->IsSpawnedByDefault())
                return; // Wait for its negative-spawn data to be loaded.
            uint64 now = uint64(time(nullptr));
            if (m_treasureState == TREASURE_AVAILABLE && GetData(TYPE_ARCHAEDAS) == DONE && m_treasureExpires > now)
            {
                if (uint64(go->GetRespawnTime()) != m_treasureExpires)
                {
                    go->SetRespawnTime(time_t(m_treasureExpires - now));
                    go->Refresh();
                }
            }
            else if (go->IsSpawned())
            {
                go->SetForcedDespawn();
                go->SetLootState(GO_JUST_DEACTIVATED);
            }
            return;
        }
        default:
            return;
    }
    // Explicit states are idempotent, unlike DoUseDoorOrButton's toggle.
    go->SetLootState(GO_READY);
    go->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
}

void instance_uldaman::SyncObjects()
{
    for (auto const& object : m_goEntryGuidStore)
        if (GameObject* go = instance->GetGameObject(object.second))
            SyncObject(go);
}

void instance_uldaman::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_HALLSHAPER:
        case NPC_CUSTODIAN:
            m_lWardens.insert(creature->GetObjectGuid());
            break;
        case NPC_STONE_KEEPER:
            if (creature->HasStaticDBSpawnData())
                m_lKeepers.insert(creature->GetObjectGuid());
            break;
        case NPC_ARCHAEDAS:
            m_npcEntryGuidStore[NPC_ARCHAEDAS] = creature->GetObjectGuid();
            break;
        case NPC_IRONAYA:
            if (GetData(TYPE_IRONAYA) == IN_PROGRESS || GetData(TYPE_IRONAYA) == SPECIAL)
                m_ironayaGuid = creature->GetObjectGuid();
            break;
        case NPC_ANNORA_SCORPID:
            if (creature->IsAlive())
                m_annoraDeadScorpionDbGuids.erase(creature->GetDbGuid());
            break;
    }
}

void instance_uldaman::OnCreatureRespawn(Creature* creature)
{
    if (creature->GetEntry() == NPC_ANNORA_SCORPID)
        m_annoraDeadScorpionDbGuids.erase(creature->GetDbGuid());
}

void instance_uldaman::OnCreatureGroupDespawn(CreatureGroup* group, Creature* creature)
{
    // RemoveFromWorld also removes group members. Only an actual death can
    // request the check, which verifies every configured member independently.
    if (group && group->GetGroupId() == GROUP_ANNORA_SCORPIDS && creature && !creature->IsAlive())
        CheckAnnoraUnlock();
}

void instance_uldaman::CheckAnnoraUnlock()
{
    RestoreAnnoraKills();
    if (m_restoreAnnoraKills || m_annoraUnlocked || m_annoraScorpionDbGuids.empty())
        return;
    const time_t now = time(nullptr);
    for (uint32 guid : m_annoraScorpionDbGuids)
    {
        if (!m_annoraDeadScorpionDbGuids.count(guid))
            return;
        if (Creature* scorpid = instance->GetCreature(guid))
        {
            if (scorpid->IsAlive())
                return;
        }
        else if (instance->GetPersistentState()->GetCreatureRespawnTime(guid) <= now)
            return; // An unloaded live member is not a kill.
    }
    m_annoraUnlocked = true;
    instance->GetVariableManager().SetVariable(WORLD_STATE_CUSTOM_SPAWN_ANNORA, 1);
    SaveState();
}

void instance_uldaman::RestoreAnnoraKills()
{
    if (!m_restoreAnnoraKills)
        return;
    // Map constructs and loads its script before creating the persistent state.
    auto persistentState = instance->GetPersistentState();
    if (!persistentState)
        return;
    const time_t now = time(nullptr);
    // Merge once: death callbacks can precede the first update and respawn save.
    for (uint32 guid : m_annoraScorpionDbGuids)
    {
        Creature* scorpid = instance->GetCreature(guid);
        if (scorpid && scorpid->IsAlive())
            m_annoraDeadScorpionDbGuids.erase(guid);
        else if (persistentState->GetCreatureRespawnTime(guid) > now)
            m_annoraDeadScorpionDbGuids.insert(guid);
    }
    m_restoreAnnoraKills = false;
}

void instance_uldaman::BuildSaveData()
{
    // Keep the original two fields first. U1 adds versioned optional state.
    std::ostringstream stream;
    stream << m_auiEncounter[0] << ' ' << m_auiEncounter[1] << " U1 "
           << uint32(m_annoraUnlocked) << ' ' << m_auiEncounter[2] << ' '
           << m_treasureState << ' ' << m_treasureExpires << ' '
           << (m_treasureSnapshot.empty() ? "-" : m_treasureSnapshot);
    m_strInstData = stream.str();
}

void instance_uldaman::SaveState()
{
    BuildSaveData();
    SaveToDB();
}

void instance_uldaman::SetData(uint32 type, uint32 state)
{
    if (type == TYPE_ALTAR_EVENT)
    {
        if (GetData(type) == DONE)
            return;
        if (state == IN_PROGRESS)
        {
            if (GetData(type) == IN_PROGRESS)
                return;
            DoResetKeeperEvent();
            m_auiEncounter[0] = IN_PROGRESS;
            m_uiKeeperCooldown = 5000;
        }
        else if (state == NOT_STARTED || state == FAIL)
        {
            m_auiEncounter[0] = NOT_STARTED;
            DoResetKeeperEvent();
        }
        else if (state == DONE && m_lKeepers.size() == MAX_STONE_KEEPERS && m_deadKeepers.size() == MAX_STONE_KEEPERS)
        {
            m_auiEncounter[0] = DONE;
            m_uiKeeperCooldown = 0;
            m_activeKeeper.Clear();
            SyncObjects();
            SaveState();
        }
        return;
    }
    if (type == TYPE_ARCHAEDAS)
    {
        if (GetData(type) == DONE || GetData(type) == state)
            return;
        m_auiEncounter[1] = state;
        if (state == DONE)
        {
            m_treasureState = TREASURE_AVAILABLE;
            m_treasureExpires = uint64(time(nullptr)) + HOUR;
            m_treasureSnapshot.clear();
            SaveState(); // Commit availability before exposing the chest.
        }
        SyncObjects();
    }
}

void instance_uldaman::Load(const char* input)
{
    // Load also runs before Map creates its persistent state, even for old saves.
    m_annoraDeadScorpionDbGuids.clear();
    m_restoreAnnoraKills = true;
    if (!input)
        return;
    uint32 altar = NOT_STARTED, boss = NOT_STARTED;
    std::istringstream stream(input);
    if (!(stream >> altar >> boss))
        return;
    m_auiEncounter[0] = altar == DONE ? DONE : NOT_STARTED;
    m_auiEncounter[1] = boss == DONE ? DONE : NOT_STARTED;
    m_auiEncounter[2] = NOT_STARTED;
    m_annoraUnlocked = false;
    m_treasureState = boss == DONE ? TREASURE_CLOSED : TREASURE_UNAVAILABLE;
    m_treasureExpires = 0;
    m_treasureSnapshot.clear();
    std::string version, snapshot;
    uint32 annora = 0, ironaya = 0, treasure = 0;
    uint64 expires = 0;
    if (stream >> version && version == "U1" && stream >> annora >> ironaya >> treasure >> expires >> snapshot)
    {
        m_annoraUnlocked = annora == 1;
        m_auiEncounter[2] = ironaya == DONE || ironaya == SPECIAL ? ironaya : uint32(NOT_STARTED);
        uint64 now = uint64(time(nullptr));
        if (boss == DONE && treasure == TREASURE_AVAILABLE && expires > now && expires <= now + HOUR && snapshot.size() <= 32771 && (snapshot == "-" || snapshot.compare(0, 3, "IL1") == 0))
        {
            m_treasureState = TREASURE_AVAILABLE;
            m_treasureExpires = expires;
            if (snapshot != "-")
                m_treasureSnapshot = snapshot;
        }
    }
    // Old DONE saves cannot establish that a reward was never claimed.
    m_deadKeepers.clear();
    m_activeKeeper.Clear();
    m_uiKeeperCooldown = 0;
    m_pendingKeeperReset = m_resettingKeepers = false;
    m_ironayaStage = m_auiEncounter[2] == SPECIAL ? 3 : 0;
    m_ironayaTimer = 0;
    m_restoreIronaya = m_auiEncounter[2] == SPECIAL;
    // Already dead members are not instantiated by SpawnGroup after a reload.
    // Record their persisted death cooldown once; ordinary unload/despawn during
    // this run cannot manufacture another real-kill notification.
    instance->GetVariableManager().SetVariable(WORLD_STATE_CUSTOM_SPAWN_ANNORA, m_annoraUnlocked ? 1 : 0);
    BuildSaveData();
    SyncObjects();
}

void instance_uldaman::SetData64(uint32 type, uint64 guid)
{
    if (type == DATA_EVENT_STARTER)
        m_playerGuid = ObjectGuid(guid);
}

uint32 instance_uldaman::GetData(uint32 type) const
{
    if (type == TYPE_ALTAR_EVENT)
        return m_auiEncounter[0];
    if (type == TYPE_ARCHAEDAS)
        return m_auiEncounter[1];
    if (type == TYPE_IRONAYA)
        return m_auiEncounter[2];
    return 0;
}

uint64 instance_uldaman::GetData64(uint32 type) const
{
    return type == DATA_EVENT_STARTER ? m_playerGuid.GetRawValue() : 0;
}

std::vector<InstanceEventReset> instance_uldaman::GetResettableEvents() const
{
    return {{TYPE_ARCHAEDAS, "Archaedas / Ancient Treasure", {NPC_ARCHAEDAS}, GO_ANCIENT_TREASURE}};
}

bool instance_uldaman::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId != TYPE_ARCHAEDAS || GetData(TYPE_ARCHAEDAS) == IN_PROGRESS || GetData(TYPE_ARCHAEDAS) == SPECIAL)
        return false;
    instance_uldaman pending(*this);
    pending.m_auiEncounter[1] = NOT_STARTED;
    pending.m_treasureState = TREASURE_UNAVAILABLE;
    pending.m_treasureExpires = 0;
    pending.m_treasureSnapshot.clear();
    pending.BuildSaveData();
    data = pending.m_strInstData;
    return true;
}

void instance_uldaman::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId != TYPE_ARCHAEDAS)
        return;
    m_auiEncounter[1] = NOT_STARTED;
    m_treasureState = TREASURE_UNAVAILABLE;
    m_treasureExpires = 0;
    m_treasureSnapshot.clear();
    m_playerGuid.Clear();
    m_strInstData = data;
    ResetEventRewardGameObject(GO_ANCIENT_TREASURE);
    if (Creature* boss = GetSingleCreatureFromStorage(NPC_ARCHAEDAS, true))
        if (boss->IsAlive() && boss->AI())
            boss->AI()->JustRespawned();
    // Keep the completed keeper altar, Ironaya and Annora progress unchanged.
    SyncObjects();
}

bool instance_uldaman::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    if (bossEntry != NPC_ARCHAEDAS)
        return false;
    reward.gameObjectEntry = GO_ANCIENT_TREASURE;
    reward.completed = GetData(TYPE_ARCHAEDAS) == DONE;
    return true;
}

bool instance_uldaman::GetGameObjectLootSnapshot(uint32 entry, std::string& snapshot) const
{
    if (entry != GO_ANCIENT_TREASURE)
        return false;
    snapshot = m_treasureState == TREASURE_AVAILABLE && GetData(TYPE_ARCHAEDAS) == DONE && m_treasureExpires > uint64(time(nullptr))
        ? m_treasureSnapshot : "closed";
    return true;
}

void instance_uldaman::OnGameObjectLootSnapshot(uint32 entry, std::string const& snapshot, bool exhausted)
{
    if (entry != GO_ANCIENT_TREASURE || m_treasureState != TREASURE_AVAILABLE)
        return;
    if (exhausted || m_treasureExpires <= uint64(time(nullptr)) || snapshot.size() > 32771)
    {
        CloseTreasure();
        return;
    }
    if (m_treasureSnapshot != snapshot)
    {
        m_treasureSnapshot = snapshot;
        SaveState();
    }
}

void instance_uldaman::CloseTreasure()
{
    if (m_treasureState != TREASURE_AVAILABLE)
        return;
    m_treasureState = TREASURE_CLOSED;
    m_treasureSnapshot.clear();
    ClearBossLootItems(GO_ANCIENT_TREASURE);
    SaveState();
}

void instance_uldaman::StartEvent(uint32 eventId, Player* player)
{
    if (!player || player->GetMap() != instance || !player->IsAlive())
        return;
    if (eventId == EVENT_ID_ALTAR_KEEPER && GetData(TYPE_ALTAR_EVENT) == NOT_STARTED)
    {
        m_playerGuid = player->GetObjectGuid();
        SetData(TYPE_ALTAR_EVENT, IN_PROGRESS);
    }
    else if (eventId == EVENT_ID_ALTAR_ARCHAEDAS && GetData(TYPE_ALTAR_EVENT) == DONE &&
             (GetData(TYPE_ARCHAEDAS) == NOT_STARTED || GetData(TYPE_ARCHAEDAS) == FAIL))
    {
        m_playerGuid = player->GetObjectGuid();
        SetData(TYPE_ARCHAEDAS, SPECIAL);
    }
}

Player* instance_uldaman::SelectEventTarget(Creature* searcher, float maxDistance) const
{
    if (!searcher || searcher->GetMap() != instance)
        return nullptr;
    auto eligible = [&](Player* player)
    {
        return player && player->IsInWorld() && player->GetMap() == instance && player->IsAlive() && !player->IsGameMaster() &&
            !player->IsFeigningDeathSuccessfully() && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) &&
            !player->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_UNTARGETABLE | UNIT_FLAG_TAXI_FLIGHT |
                UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_SPAWNING | UNIT_FLAG_IMMUNE_TO_NPC) &&
            searcher->IsWithinDistInMap(player, maxDistance) && searcher->IsWithinLOSInMap(player);
    };
    if (Player* starter = instance->GetPlayer(m_playerGuid))
        if (eligible(starter))
            return starter;
    Player* closest = nullptr;
    for (auto const& reference : instance->GetPlayers())
    {
        Player* player = reference.getSource();
        if (eligible(player) && (!closest || searcher->GetDistance(player) < searcher->GetDistance(closest)))
            closest = player;
    }
    return closest;
}

Creature* instance_uldaman::GetClosestDwarfNotInCombat(Creature* searcher)
{
    Creature* closest = nullptr;
    for (ObjectGuid guid : m_lWardens)
        if (Creature* dwarf = instance->GetCreature(guid))
            if (dwarf->IsAlive() && dwarf->HasAura(SPELL_STONED) && !dwarf->IsInCombat() && !dwarf->GetVictim() &&
                (!closest || searcher->GetDistance(dwarf) < searcher->GetDistance(closest)))
                closest = dwarf;
    return closest;
}

void instance_uldaman::DoResetKeeperEvent()
{
    if (m_resettingKeepers)
        return;
    m_resettingKeepers = true;
    m_pendingKeeperReset = false;
    m_uiKeeperCooldown = 0;
    m_activeKeeper.Clear();
    m_deadKeepers.clear();
    for (ObjectGuid guid : m_lKeepers)
        if (Creature* keeper = instance->GetCreature(guid))
        {
            if (keeper->IsAlive() && (keeper->IsInCombat() || !keeper->HasAura(SPELL_STONED)))
                keeper->ForcedDespawn();
            if (!keeper->IsAlive())
                keeper->Respawn();
        }
    m_resettingKeepers = false;
}

void instance_uldaman::OnCreatureEvade(Creature* creature)
{
    if (!m_resettingKeepers && GetData(TYPE_ALTAR_EVENT) == IN_PROGRESS && creature->GetObjectGuid() == m_activeKeeper)
        m_pendingKeeperReset = true;
}

void instance_uldaman::OnCreatureDeath(Creature* creature)
{
    if (creature->GetEntry() == NPC_ANNORA_SCORPID && m_annoraScorpionDbGuids.count(creature->GetDbGuid()))
    {
        m_annoraDeadScorpionDbGuids.insert(creature->GetDbGuid());
        CheckAnnoraUnlock();
    }
    if (creature->GetEntry() == NPC_IRONAYA && creature->GetObjectGuid() == m_ironayaGuid && GetData(TYPE_IRONAYA) != DONE)
    {
        m_auiEncounter[2] = DONE;
        m_restoreIronaya = false;
        m_ironayaTimer = 0;
        SyncObjects();
        SaveState();
    }
    if (m_resettingKeepers || GetData(TYPE_ALTAR_EVENT) != IN_PROGRESS || creature->GetObjectGuid() != m_activeKeeper ||
        !m_lKeepers.count(creature->GetObjectGuid()) || !m_deadKeepers.insert(creature->GetObjectGuid()).second)
        return;
    m_activeKeeper.Clear();
    if (m_lKeepers.size() == MAX_STONE_KEEPERS && m_deadKeepers.size() == MAX_STONE_KEEPERS)
        SetData(TYPE_ALTAR_EVENT, DONE);
    else
        m_uiKeeperCooldown = 5000;
}

bool instance_uldaman::StartIronayaEvent(GameObject* keystone)
{
    if (!keystone || keystone->GetMap() != instance || keystone->GetEntry() != GO_KEYSTONE || GetData(TYPE_IRONAYA) != NOT_STARTED)
        return false;
    m_goEntryGuidStore[GO_KEYSTONE] = keystone->GetObjectGuid();
    m_auiEncounter[2] = IN_PROGRESS;
    m_ironayaStage = m_ironayaSummonFailures = 0;
    m_ironayaTimer = 1000;
    SyncObject(keystone);
    SaveState();
    return true;
}

Creature* instance_uldaman::SummonIronaya(bool startMoving)
{
    if (Creature* existing = instance->GetCreature(m_ironayaGuid))
        return existing;
    GameObject* keystone = GetSingleGameObjectFromStorage(GO_KEYSTONE, true);
    if (!keystone)
        return nullptr;
    Creature* ironaya = keystone->SummonCreature(NPC_IRONAYA, -235.673f, 309.639f, -47.5976f, 4.74317f,
        TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 9000000);
    if (ironaya)
    {
        m_ironayaGuid = ironaya->GetObjectGuid();
        if (startMoving)
            ironaya->GetMotionMaster()->MoveWaypoint(0);
    }
    return ironaya;
}

void instance_uldaman::UpdateIronaya(uint32 diff)
{
    if (!m_ironayaTimer)
        return;
    if (m_ironayaTimer > diff)
    {
        m_ironayaTimer -= diff;
        return;
    }
    m_ironayaTimer = 0;
    if (m_ironayaStage == 0)
    {
        if (!SummonIronaya(false))
        {
            if (++m_ironayaSummonFailures < 3)
                m_ironayaTimer = 1000;
            else
            {
                m_auiEncounter[2] = NOT_STARTED;
                SyncObjects();
                SaveState();
            }
            return;
        }
        m_ironayaStage = 1;
        m_ironayaTimer = 26000;
    }
    else if (m_ironayaStage == 1)
    {
        m_auiEncounter[2] = SPECIAL; // Seal is permanently open; the boss is alive.
        m_ironayaStage = 2;
        m_ironayaTimer = 1000;
        SyncObjects();
        SaveState();
    }
    else if (m_ironayaStage == 2)
    {
        if (Creature* ironaya = SummonIronaya(false))
        {
            DoBroadcastText(3261, ironaya);
            if (!ironaya->IsInCombat())
                ironaya->GetMotionMaster()->MoveWaypoint(0);
        }
        else
            m_restoreIronaya = true;
        m_ironayaStage = 3;
    }
}

void instance_uldaman::OnCreatureDespawn(Creature* creature)
{
    if (creature->GetObjectGuid() == m_ironayaGuid && GetData(TYPE_IRONAYA) == SPECIAL)
    {
        m_ironayaGuid.Clear();
        m_restoreIronaya = true;
    }
}

void instance_uldaman::Update(uint32 diff)
{
    RestoreAnnoraKills();
    if (m_pendingKeeperReset)
        SetData(TYPE_ALTAR_EVENT, NOT_STARTED);
    UpdateIronaya(diff);
    if (m_worldStateTimer <= diff)
    {
        m_worldStateTimer = 1000;
        CheckAnnoraUnlock();
        if (m_treasureState == TREASURE_AVAILABLE && m_treasureExpires <= uint64(time(nullptr)))
            CloseTreasure();
        SyncObjects();
        // TemporarySpawn::UnSummon and ordinary grid unloading do not emit
        // InstanceData::OnCreatureDespawn. An open seal must remain recoverable
        // when its living boss disappears through either engine path.
        if (GetData(TYPE_IRONAYA) == SPECIAL && !instance->GetCreature(m_ironayaGuid))
            m_restoreIronaya = true;
        if (m_restoreIronaya && GetData(TYPE_IRONAYA) == SPECIAL && SummonIronaya(true))
            m_restoreIronaya = false;
    }
    else
        m_worldStateTimer -= diff;
    if (GetData(TYPE_ALTAR_EVENT) != IN_PROGRESS || !m_uiKeeperCooldown)
        return;
    if (m_uiKeeperCooldown > diff)
    {
        m_uiKeeperCooldown -= diff;
        return;
    }
    m_uiKeeperCooldown = 1000; // Missing/unloaded actors are retried, never completed.
    if (m_lKeepers.size() != MAX_STONE_KEEPERS)
        return;
    for (ObjectGuid guid : m_lKeepers)
    {
        Creature* keeper = instance->GetCreature(guid);
        if (!keeper || !keeper->IsAlive() || m_deadKeepers.count(guid) || keeper->IsInCombat())
            continue;
        Player* target = SelectEventTarget(keeper);
        if (!target)
        {
            SetData(TYPE_ALTAR_EVENT, NOT_STARTED);
            return;
        }
        m_activeKeeper = guid;
        keeper->RemoveAurasDueToSpell(SPELL_STONED);
        keeper->AI()->AttackStart(target);
        if (!keeper->GetVictim())
        {
            m_pendingKeeperReset = true;
            return;
        }
        m_uiKeeperCooldown = 0;
        return;
    }
}

InstanceData* GetInstanceData_instance_uldaman(Map* map)
{
    return new instance_uldaman(map);
}

bool ProcessEventId_event_spell_altar_boss_aggro(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (isStart && source && source->GetTypeId() == TYPEID_PLAYER)
        if (auto data = dynamic_cast<instance_uldaman*>(static_cast<Player*>(source)->GetInstanceData()))
        {
            data->StartEvent(eventId, static_cast<Player*>(source));
            return eventId == EVENT_ID_ALTAR_KEEPER || eventId == EVENT_ID_ALTAR_ARCHAEDAS;
        }
    return false;
}

bool GOUse_go_uldaman_keystone(Player* /*player*/, GameObject* go)
{
    if (go)
        if (auto data = dynamic_cast<instance_uldaman*>(go->GetInstanceData()))
        {
            data->StartIronayaEvent(go);
            return true; // Suppress the old database summon sequence.
        }
    return false;
}

void AddSC_instance_uldaman()
{
    Script* script = new Script;
    script->Name = "instance_uldaman";
    script->GetInstanceData = &GetInstanceData_instance_uldaman;
    script->RegisterSelf();
    script = new Script;
    script->Name = "event_spell_altar_boss_aggro";
    script->pProcessEventId = &ProcessEventId_event_spell_altar_boss_aggro;
    script->RegisterSelf();
    script = new Script;
    script->Name = "go_uldaman_keystone";
    script->pGOUse = &GOUse_go_uldaman_keystone;
    script->RegisterSelf();
}
