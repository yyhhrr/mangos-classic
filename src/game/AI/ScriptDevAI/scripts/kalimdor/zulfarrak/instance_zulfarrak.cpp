/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2 or any later version. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulfarrak.h"
#include "DBScripts/ScriptMgr.h"
#include "Combat/ThreatManager.h"
#include "Maps/MapPersistentStateMgr.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace
{
struct PyramidSpawn
{
    uint32 delay, entry;
    float x, y, z, o;
};
static PyramidSpawn const pyramidSpawns[ZF_PYRAMID_MEMBERS] = {
    {10000, 7787, 1869.45996f, 1210.38f, 9.14999962f, 2.46000004f},      // original slot 0
    {10000, 7787, 1873.44995f, 1204.43994f, 8.86999989f, 0.879999995f},  // original slot 1
    {10000, 7787, 1873.63f, 1204.65002f, 8.86999989f, 0.610000014f},     // original slot 2
    {10000, 7787, 1875.18005f, 1221.23999f, 9.21000004f, 0.839999974f},  // original slot 3
    {10000, 7787, 1877.0f, 1207.27002f, 8.86999989f, 3.79999995f},       // original slot 4
    {10000, 7787, 1879.02002f, 1223.06006f, 9.11999989f, 5.90999985f},   // original slot 5
    {10000, 7787, 1882.06995f, 1225.69995f, 9.31999969f, 5.69000006f},   // original slot 6
    {10000, 7787, 1883.5f, 1218.25f, 8.89999962f, 0.670000017f},         // original slot 7
    {10000, 7787, 1886.93005f, 1221.40002f, 8.93999958f, 1.60000002f},   // original slot 8
    {10000, 7787, 1886.96997f, 1225.85999f, 9.85000038f, 5.78999996f},   // original slot 9
    {10000, 7787, 1890.07996f, 1218.68005f, 8.86999989f, 1.59000003f},   // original slot 10
    {10000, 7787, 1892.28003f, 1225.48999f, 9.56999969f, 5.63000011f},   // original slot 11
    {10000, 7787, 1894.64001f, 1206.29004f, 8.86999989f, 2.22000003f},   // original slot 12
    {10000, 7787, 1894.71997f, 1221.91003f, 8.86999989f, 2.33999991f},   // original slot 13
    {10000, 7787, 1896.45996f, 1205.62f, 8.86999989f, 5.71999979f},      // original slot 14
    {10000, 7787, 1899.63f, 1202.52002f, 8.86999989f, 2.46000004f},      // original slot 15
    {10000, 7787, 1901.33997f, 1206.06006f, 8.86999989f, 5.71999979f},   // original slot 16
    {10000, 7787, 1902.82996f, 1223.41003f, 8.96000004f, 3.95000005f},   // original slot 17
    {10000, 7787, 1903.48999f, 1211.52002f, 8.88000011f, 0.610000014f},  // original slot 18
    {10000, 7788, 1868.75f, 1213.84998f, 9.47000027f, 3.07999992f},      // original slot 19
    {10000, 7788, 1872.72998f, 1203.04004f, 8.86999989f, 6.26000023f},   // original slot 20
    {10000, 7788, 1874.56995f, 1214.16003f, 8.86999989f, 3.11999989f},   // original slot 21
    {10000, 7788, 1877.40002f, 1216.41003f, 8.97000027f, 0.370000005f},  // original slot 22
    {10000, 7788, 1878.06995f, 1216.37f, 8.93000031f, 1.59000003f},      // original slot 23
    {10000, 7788, 1878.60999f, 1202.72998f, 8.86999989f, 1.59000003f},   // original slot 24
    {10000, 7788, 1881.31995f, 1205.39001f, 8.86999989f, 0.970000029f},  // original slot 25
    {10000, 7788, 1882.5f, 1203.32996f, 8.86999989f, 0.370000005f},      // original slot 26
    {10000, 7788, 1883.73999f, 1212.34998f, 8.86999989f, 1.59000003f},   // original slot 27
    {10000, 7788, 1883.76001f, 1222.30005f, 9.10999966f, 6.26000023f},   // original slot 28
    {10000, 7788, 1886.06006f, 1213.19995f, 8.86999989f, 3.11999989f},   // original slot 29
    {10000, 7788, 1887.18994f, 1205.08997f, 8.86999989f, 3.42000008f},   // original slot 30
    {10000, 7788, 1889.81995f, 1222.51001f, 9.02999973f, 0.970000029f},  // original slot 31
    {10000, 7788, 1889.93994f, 1212.20996f, 8.86999989f, 1.59000003f},   // original slot 32
    {10000, 7788, 1891.59998f, 1209.43005f, 8.86999989f, 1.59000003f},   // original slot 33
    {10000, 7788, 1893.06995f, 1215.26001f, 8.86999989f, 3.07999992f},   // original slot 34
    {10000, 7788, 1897.06006f, 1216.07996f, 8.86999989f, 3.11999989f},   // original slot 35
    {10000, 7788, 1898.22998f, 1217.96997f, 8.86999989f, 3.42000008f},   // original slot 36
    {10000, 7788, 1906.30005f, 1220.32996f, 9.02999973f, 1.59000003f},   // original slot 37
    {100000, 7787, 1869.45996f, 1210.38f, 9.14999962f, 2.46000004f},     // original slot 38
    {100000, 7787, 1873.44995f, 1204.43994f, 8.86999989f, 0.879999995f}, // original slot 39
    {100000, 7787, 1875.18005f, 1221.23999f, 9.21000004f, 0.839999974f}, // original slot 40
    {100000, 7787, 1879.02002f, 1223.06006f, 9.11999989f, 5.90999985f},  // original slot 41
    {100000, 7787, 1882.06995f, 1225.69995f, 9.31999969f, 5.69000006f},  // original slot 42
    {100000, 7787, 1883.5f, 1218.25f, 8.89999962f, 0.670000017f},        // original slot 43
    {100000, 7787, 1886.93005f, 1221.40002f, 8.93999958f, 1.60000002f},  // original slot 44
    {100000, 7787, 1886.96997f, 1225.85999f, 9.85000038f, 5.78999996f},  // original slot 45
    {100000, 7787, 1890.07996f, 1218.68005f, 8.86999989f, 1.59000003f},  // original slot 46
    {100000, 7787, 1892.28003f, 1225.48999f, 9.56999969f, 5.63000011f},  // original slot 47
    {100000, 7787, 1894.64001f, 1206.29004f, 8.86999989f, 2.22000003f},  // original slot 48
    {100000, 7787, 1894.71997f, 1221.91003f, 8.86999989f, 2.33999991f},  // original slot 49
    {100000, 7787, 1901.33997f, 1206.06006f, 8.86999989f, 5.71999979f},  // original slot 50
    {100000, 7787, 1902.82996f, 1223.41003f, 8.96000004f, 3.95000005f},  // original slot 51
    {100000, 7787, 1903.48999f, 1211.52002f, 8.88000011f, 0.610000014f}, // original slot 52
    {100000, 7788, 1868.75f, 1213.84998f, 9.47000027f, 3.07999992f},     // original slot 53
    {100000, 7788, 1872.72998f, 1203.04004f, 8.86999989f, 6.26000023f},  // original slot 54
    {100000, 7788, 1874.56995f, 1214.16003f, 8.86999989f, 3.11999989f},  // original slot 55
    {100000, 7788, 1877.40002f, 1216.41003f, 8.97000027f, 0.370000005f}, // original slot 56
    {100000, 7788, 1881.31995f, 1205.39001f, 8.86999989f, 0.970000029f}, // original slot 57
    {100000, 7788, 1882.5f, 1203.32996f, 8.86999989f, 0.370000005f},     // original slot 58
    {100000, 7788, 1883.73999f, 1212.34998f, 8.86999989f, 1.59000003f},  // original slot 59
    {100000, 7788, 1883.76001f, 1222.30005f, 9.10999966f, 6.26000023f},  // original slot 60
    {100000, 7788, 1886.06006f, 1213.19995f, 8.86999989f, 3.11999989f},  // original slot 61
    {100000, 7788, 1887.18994f, 1205.08997f, 8.86999989f, 3.42000008f},  // original slot 62
    {100000, 7788, 1889.81995f, 1222.51001f, 9.02999973f, 0.970000029f}, // original slot 63
    {100000, 7788, 1889.93994f, 1212.20996f, 8.86999989f, 1.59000003f},  // original slot 64
    {100000, 7788, 1893.06995f, 1215.26001f, 8.86999989f, 3.07999992f},  // original slot 65
    {100000, 7788, 1898.22998f, 1217.96997f, 8.86999989f, 3.42000008f},  // original slot 66
    {100000, 7788, 1906.30005f, 1220.32996f, 9.02999973f, 1.59000003f},  // original slot 67
    {100000, 7789, 1873.63f, 1204.65002f, 8.86999989f, 0.610000014f},    // original slot 68
    {100000, 7789, 1877.0f, 1207.27002f, 8.86999989f, 3.79999995f},      // original slot 69
    {100000, 7789, 1878.06995f, 1216.37f, 8.93000031f, 1.59000003f},     // original slot 70
    {100000, 7789, 1878.60999f, 1202.72998f, 8.86999989f, 1.59000003f},  // original slot 71
    {100000, 7789, 1891.59998f, 1209.43005f, 8.86999989f, 1.59000003f},  // original slot 72
    {100000, 7789, 1896.45996f, 1205.62f, 8.86999989f, 5.71999979f},     // original slot 73
    {100000, 7789, 1897.06006f, 1216.07996f, 8.86999989f, 3.11999989f},  // original slot 74
    {100000, 7789, 1899.63f, 1202.52002f, 8.86999989f, 2.46000004f},     // original slot 75
    {250000, 7275, 1883.97998f, 1201.66003f, 8.96000004f, 1.71000004f},  // original slot 76
    {250000, 7796, 1882.31995f, 1201.44995f, 8.96000004f, 6.05000019f},  // original slot 77
    {250000, 8876, 1873.31006f, 1197.55005f, 8.96000004f, 4.5999999f},   // original slot 78
    {250000, 8876, 1876.97998f, 1199.93994f, 8.96000004f, 2.19000006f},  // original slot 79
    {250000, 8876, 1888.44995f, 1200.17004f, 8.96000004f, 1.11000001f},  // original slot 80
    {250000, 8876, 1890.55005f, 1199.93994f, 8.96000004f, 4.46000004f},  // original slot 81
    {250000, 8877, 1874.73999f, 1199.03003f, 8.96000004f, 2.52999997f},  // original slot 82
    {250000, 8877, 1878.93994f, 1200.56995f, 8.96000004f, 2.5999999f},   // original slot 83
    {250000, 8877, 1895.16003f, 1198.05005f, 8.96000004f, 1.50999999f},  // original slot 84
};
constexpr uint32 crewEntries[ZF_CREW_COUNT] = {7604, 7605, 7606, 7607, 7608};
constexpr float gather[ZF_CREW_COUNT][4] = {{1884.29f, 1202.94f, 8.8781f, 4.61f},
                                            {1889.01f, 1201.98f, 8.8781f, 4.61f},
                                            {1876.14f, 1207.26f, 8.8781f, 4.61f},
                                            {1893.28f, 1206.29f, 8.877f, 4.61f},
                                            {1884.86f, 1208.98f, 8.8781f, 4.61f}};
int CrewIndex(uint32 entry)
{
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (crewEntries[i] == entry)
            return int(i);
    return -1;
}
uint32 EncounterType(uint32 entry)
{
    switch (entry)
    {
    case NPC_VELRATHA:
        return TYPE_VELRATHA;
    case NPC_GAHZRILLA:
        return TYPE_GAHZRILLA;
    case NPC_ANTUSUL:
        return TYPE_ANTUSUL;
    case NPC_THEKA:
        return TYPE_THEKA;
    case NPC_ZUMRAH:
        return TYPE_ZUMRAH;
    case NPC_NEKRUM:
        return TYPE_NEKRUM;
    case NPC_SEZZZIZ:
        return TYPE_SEZZZIZ;
    case NPC_CHIEF_SANDSCALP:
        return TYPE_CHIEF_SANDSCALP;
    }
    return MAX_ENCOUNTER;
}
} // namespace

instance_zulfarrak::instance_zulfarrak(Map *map) : ScriptedInstance(map)
{
    Initialize();
}
void instance_zulfarrak::Initialize()
{
    memset(m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_crewGuids.fill(ObjectGuid());
    m_memberGuids.fill(ObjectGuid());
    m_memberStates.fill(ZF_MEMBER_WAITING);
    m_dispatched.fill(false);
    m_crew.fill(ZulFarrakCrewState());
    m_crewRestorePending.fill(false);
    m_creatingMember = -1;
    m_creatingGahzrilla = false;
    m_rejectedGahzrilla.clear();
    m_restoringCrew = m_descended = m_spawnedThisUpdate = false;
    m_pyramidElapsed = 0;
    m_dispatchTimer = 20000;
    m_missingPlayersTimer = 5000;
    m_pollTimer = 1000;
    m_checkpointTimer = 5000;
    m_doorState = NOT_STARTED;
    m_doorTimer = m_doorRetries = 0;
    m_betrayalState = NOT_STARTED;
    m_betrayalTimer = 0;
    m_betrayalPlayer.Clear();
    m_gahzrillaGuid.Clear();
    m_gahzrillaTimer = m_gahzrillaMoveTimer = m_gahzrillaFailures = 0;
    m_barrelGuid.Clear();
    m_barrelCandidates.clear();
    m_graves.clear();
    m_cages.clear();
    BuildSaveData();
}

Creature *instance_zulfarrak::Crew(uint32 index) const
{
    return index < ZF_CREW_COUNT ? instance->GetCreature(m_crewGuids[index]) : nullptr;
}
GameObject *instance_zulfarrak::PyramidAnchor() const
{
    for (ObjectGuid guid : m_cages)
        if (GameObject *cage = instance->GetGameObject(guid))
            return cage;
    return nullptr;
}
bool instance_zulfarrak::IsValidPlayer(Player const *player, WorldObject const *origin, float distance,
                                       bool requireLineOfSight) const
{
    return player && origin && player->IsInWorld() && player->GetMap() == instance && player->IsAlive() &&
           !player->IsGameMaster() && !player->IsFeigningDeathSuccessfully() &&
           !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) &&
           !player->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNTARGETABLE | UNIT_FLAG_UNINTERACTIBLE |
                                                  UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_TAXI_FLIGHT) &&
           origin->IsWithinDistInMap(player, distance) && (!requireLineOfSight || origin->IsWithinLOSInMap(player));
}
Player *instance_zulfarrak::SelectPlayer(WorldObject const *origin, ObjectGuid preferred, float distance) const
{
    if (Player *player = instance->GetPlayer(preferred))
        if (IsValidPlayer(player, origin, distance))
            return player;
    Player *selected = nullptr;
    for (auto const &ref : instance->GetPlayers())
    {
        Player *player = ref.getSource();
        if (IsValidPlayer(player, origin, distance) &&
            (!selected || origin->GetDistance(player) < origin->GetDistance(selected)))
            selected = player;
    }
    return selected;
}
bool instance_zulfarrak::HasPyramidPlayers() const
{
    // Presence is independent of interaction LOS: pillars and stairs must not
    // turn a living nearby party into a failed event.
    GameObject *anchor = PyramidAnchor();
    for (auto const &ref : instance->GetPlayers())
    {
        Player *player = ref.getSource();
        if (anchor && IsValidPlayer(player, anchor, 220.0f, false))
            return true;
        for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
            if (Creature *crew = Crew(i))
                if (crew->IsAlive() && IsValidPlayer(player, crew, 180.0f, false))
                    return true;
    }
    return false;
}
void instance_zulfarrak::RunActorRelay(uint32 relay, WorldObject *source, WorldObject *target)
{
    if (source)
        instance->ScriptsStart(SCRIPT_TYPE_RELAY, relay, source, target, Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE);
}
int instance_zulfarrak::MemberIndex(ObjectGuid guid) const
{
    if (!guid)
        return -1;
    for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
        if (m_memberGuids[i] == guid)
            return int(i);
    return -1;
}
bool instance_zulfarrak::AllMembersKilled(uint32 count) const
{
    for (uint32 i = 0; i < count; ++i)
        if (m_memberStates[i] != ZF_MEMBER_KILLED)
            return false;
    return true;
}

void instance_zulfarrak::OnCreatureCreate(Creature *creature)
{
    if (m_creatingMember >= 0 && uint32(m_creatingMember) < ZF_PYRAMID_MEMBERS &&
        creature->GetEntry() == pyramidSpawns[m_creatingMember].entry)
        m_memberGuids[m_creatingMember] = creature->GetObjectGuid();
    int index = CrewIndex(creature->GetEntry());
    if (index >= 0 && creature->HasStaticDBSpawnData())
    {
        m_crewGuids[index] = creature->GetObjectGuid();
        m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
        if (m_crew[index].life == ZF_CREW_ALIVE &&
            instance->GetPersistentState()->GetCreatureRespawnTime(creature->GetDbGuid()) > time(nullptr))
        {
            m_crew[index].life = ZF_CREW_DEAD;
            SaveState();
        }
        if (m_crew[index].positioned && GetData(TYPE_PYRAMID_EVENT) != NOT_STARTED)
        {
            m_crewRestorePending[index] = true;
            m_restoringCrew = true;
        }
        return;
    }
    if (creature->GetEntry() == NPC_ANTUSUL || creature->GetEntry() == NPC_CHIEF_SANDSCALP)
        m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
    if (creature->GetEntry() == NPC_GAHZRILLA)
    {
        if (GetData(TYPE_GAHZRILLA) == DONE ||
            (m_gahzrillaGuid && m_gahzrillaGuid != creature->GetObjectGuid() && instance->GetCreature(m_gahzrillaGuid)))
        {
            // Create fires before AIM_Initialize; defer removal until the actor
            // has finished initialization and entered the map.
            m_rejectedGahzrilla.insert(creature->GetObjectGuid());
        }
        else if (m_creatingGahzrilla || !m_gahzrillaGuid)
            m_gahzrillaGuid = creature->GetObjectGuid();
    }
}
void instance_zulfarrak::OnCreatureRespawn(Creature *creature)
{
    int index = CrewIndex(creature->GetEntry());
    if (index >= 0 && m_crew[index].life != ZF_CREW_ALIVE && creature->IsAlive())
        creature->ForcedDespawn();
    if (creature->GetEntry() == NPC_GAHZRILLA && creature->IsAlive() &&
        (GetData(TYPE_GAHZRILLA) == DONE || m_rejectedGahzrilla.count(creature->GetObjectGuid())))
        creature->ForcedDespawn();
}
void instance_zulfarrak::OnObjectCreate(GameObject *go)
{
    uint32 entry = go->GetEntry();
    if (entry == GO_SHALLOW_GRAVE)
        m_graves.insert(go->GetObjectGuid());
    if (entry >= GO_FIRST_TROLL_CAGE && entry <= GO_LAST_TROLL_CAGE)
        m_cages.insert(go->GetObjectGuid());
    if (entry == GO_END_DOOR || entry == GO_GAHZRILLA_GONG)
        m_goEntryGuidStore[entry] = go->GetObjectGuid();
    if (entry == GO_WEEGLI_BARREL)
        m_barrelCandidates.insert(go->GetObjectGuid());
    if (entry != GO_WEEGLI_BARREL)
        SyncObject(go);
}
void instance_zulfarrak::OnObjectSpawn(GameObject *go)
{
    if (go->GetEntry() == GO_WEEGLI_BARREL)
        RegisterBarrel(go);
    else
        SyncObject(go);
}
void instance_zulfarrak::SyncObject(GameObject *go)
{
    if (go->GetEntry() == GO_END_DOOR)
    {
        go->SetLootState(GO_READY);
        go->SetGoState(m_doorState == DONE ? GO_STATE_ACTIVE_ALTERNATIVE : GO_STATE_READY);
    }
    else if (go->GetEntry() >= GO_FIRST_TROLL_CAGE && go->GetEntry() <= GO_LAST_TROLL_CAGE &&
             GetData(TYPE_PYRAMID_EVENT) != NOT_STARTED)
        go->SetGoState(GO_STATE_ACTIVE);
}
void instance_zulfarrak::RegisterBarrel(GameObject *go)
{
    if (go->GetEntry() != GO_WEEGLI_BARREL || m_doorState != IN_PROGRESS)
        return;
    Creature *weegli = Crew(3);
    if (!weegli || !weegli->IsAlive() ||
        (go->GetSpawnerGuid() != weegli->GetObjectGuid() && go->GetOwnerGuid() != weegli->GetObjectGuid()))
        return;
    if (!go->IsWithinDistInMap(weegli, 20.0f))
        return;
    m_barrelGuid = go->GetObjectGuid();
    m_doorTimer = 6000;
    m_barrelCandidates.erase(go->GetObjectGuid());
    SaveState();
}

void instance_zulfarrak::CaptureCrew()
{
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (m_crew[i].life == ZF_CREW_ALIVE)
            if (Creature *crew = Crew(i))
                if (crew->IsAlive() && crew->IsInWorld())
                {
                    auto &state = m_crew[i];
                    state.x = crew->GetPositionX();
                    state.y = crew->GetPositionY();
                    state.z = crew->GetPositionZ();
                    state.o = crew->GetOrientation();
                    state.faction = crew->GetFaction();
                    state.waypoint = crew->GetMotionMaster()->getLastReachedWaypoint();
                    state.paused = crew->hasUnitState(UNIT_STAT_WAYPOINT_PAUSED);
                    state.positioned = true;
                }
}
void instance_zulfarrak::RestoreCrew()
{
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (Creature *crew = Crew(i))
        {
            auto &state = m_crew[i];
            if (state.life != ZF_CREW_ALIVE)
            {
                if (crew->IsAlive())
                    crew->ForcedDespawn();
                m_crewRestorePending[i] = false;
                continue;
            }
            if (!crew->IsAlive() || !m_crewRestorePending[i] || !state.positioned)
                continue;
            crew->CombatStop(true);
            crew->getThreatManager().clearReferences();
            crew->NearTeleportTo(state.x, state.y, state.z, state.o);
            crew->SetFactionTemporary(state.faction, TEMPFACTION_RESTORE_RESPAWN);
            crew->GetMotionMaster()->MoveWaypoint(0);
            // SetNextWaypoint sets lastReached to next - 1. Resume AFTER the saved
            // node, otherwise a paused actor permanently loses its gossip condition.
            uint32 lastNode = i == 3 ? 16 : 9;
            uint32 nextNode = state.waypoint < lastNode ? state.waypoint + 1 : lastNode;
            crew->GetMotionMaster()->SetNextWaypoint(std::max(uint32(1), nextNode));
            if (state.paused && state.waypoint < lastNode)
                crew->GetMotionMaster()->PauseWaypoints(0);
            else
            {
                crew->GetMotionMaster()->UnpauseWaypoints();
                // The engine does not expose the remaining waypoint wait timer.
                // Restore Bly's existing interaction window without replaying scripts.
                if (i == 0 && state.waypoint == 8 && m_betrayalState != DONE)
                    crew->GetMotionMaster()->PauseWaypoints(60000);
            }
            if (i == 0 || i == 3)
            {
                bool ready = GetData(TYPE_PYRAMID_EVENT) == DONE &&
                             ((i == 0 && state.waypoint == 8 && m_betrayalState == NOT_STARTED) ||
                              (i == 3 && state.waypoint == 10 && m_doorState == NOT_STARTED));
                if (ready)
                    crew->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                else
                    crew->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            }
            if (i == 3 && m_doorState == IN_PROGRESS)
                crew->AI()->SetReactState(REACT_PASSIVE);
            m_crewRestorePending[i] = false; // Keep the checkpoint for later unloads.
        }
    bool pending = false;
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (m_crewRestorePending[i])
            pending = true;
    if (!pending)
        m_restoringCrew = false;
}
void instance_zulfarrak::SetLegacyCrewPositions()
{
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
    {
        auto &state = m_crew[i];
        state.positioned = true;
        state.paused = i == 3;
        state.waypoint = i == 3 ? 10 : 8;
        state.faction = 495;
        state.x = gather[i][0];
        state.y = gather[i][1];
        state.z = gather[i][2];
        state.o = gather[i][3];
        m_crewRestorePending[i] = true;
    }
    m_restoringCrew = true;
}
void instance_zulfarrak::NotifyCrewEscaped(Creature *creature)
{
    if (!creature || creature->GetMap() != instance)
        return;
    int index = CrewIndex(creature->GetEntry());
    if (index < 0 || m_crewGuids[index] != creature->GetObjectGuid() || !creature->IsAlive())
        return;
    if (m_crew[index].life != ZF_CREW_ALIVE)
        return;
    m_crew[index].life = ZF_CREW_ESCAPED;
    SaveState();
}

void instance_zulfarrak::BuildSaveData()
{
    std::ostringstream stream;
    for (uint32 state : m_auiEncounter)
        stream << state << ' ';
    stream << "ZF1 " << m_pyramidElapsed << ' ' << m_dispatchTimer << ' ' << uint32(m_descended) << ' ' << m_doorState
           << ' ' << m_doorRetries << ' ' << m_doorTimer << ' ' << m_betrayalState << ' ' << m_betrayalTimer << ' '
           << m_betrayalPlayer.GetRawValue() << ' ';
    for (uint8 state : m_memberStates)
        stream << char('0' + state);
    stream << std::setprecision(9);
    for (auto const &crew : m_crew)
        stream << ' ' << crew.life << ' ' << crew.waypoint << ' ' << crew.faction << ' ' << uint32(crew.paused) << ' '
               << uint32(crew.positioned) << ' ' << crew.x << ' ' << crew.y << ' ' << crew.z << ' ' << crew.o;
    m_strInstData = stream.str();
}
void instance_zulfarrak::SaveState()
{
    std::string previous = m_strInstData;
    BuildSaveData();
    if (previous != m_strInstData)
        SaveToDB();
}
void instance_zulfarrak::Load(const char *input)
{
    if (!input)
        return;
    std::istringstream stream(input);
    std::array<uint32, MAX_ENCOUNTER> states;
    for (uint32 &state : states)
        if (!(stream >> state))
            return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i] == DONE ? DONE : NOT_STARTED;
    std::string version, slots;
    uint32 descended = 0;
    uint64 target = 0;
    m_crew.fill(ZulFarrakCrewState());
    m_crewRestorePending.fill(false);
    bool modern = bool(stream >> version) && version == "ZF1" &&
                  bool(stream >> m_pyramidElapsed >> m_dispatchTimer >> descended >> m_doorState >> m_doorRetries >>
                       m_doorTimer >> m_betrayalState >> m_betrayalTimer >> target >> slots);
    std::array<ZulFarrakCrewState, ZF_CREW_COUNT> crewStates;
    if (modern)
    {
        modern = slots.size() == ZF_PYRAMID_MEMBERS && m_pyramidElapsed <= 86400000 && m_doorRetries <= 4 &&
                 (m_doorState == NOT_STARTED || m_doorState == IN_PROGRESS || m_doorState == DONE) &&
                 (m_betrayalState == NOT_STARTED || m_betrayalState == IN_PROGRESS || m_betrayalState == DONE) &&
                 m_dispatchTimer <= 20000 && m_doorTimer <= 60000 && m_betrayalTimer <= 6000;
        for (char c : slots)
            if (c < '0' || c > '2')
                modern = false;
        for (auto &crew : crewStates)
        {
            uint32 paused = 0, positioned = 0;
            if (!(stream >> crew.life >> crew.waypoint >> crew.faction >> paused >> positioned >> crew.x >> crew.y >>
                  crew.z >> crew.o))
                modern = false;
            crew.paused = paused != 0;
            crew.positioned = positioned != 0;
            if (crew.life > ZF_CREW_ESCAPED || crew.waypoint > 16 || !std::isfinite(crew.x) || !std::isfinite(crew.y) ||
                !std::isfinite(crew.z) || !std::isfinite(crew.o) || std::fabs(crew.x) > 20000 ||
                std::fabs(crew.y) > 20000 || std::fabs(crew.z) > 20000)
                modern = false;
            if (crew.faction != 35 && crew.faction != 495 && crew.faction != 14)
                modern = false;
        }
    }
    m_memberGuids.fill(ObjectGuid());
    m_dispatched.fill(false);
    m_barrelGuid.Clear();
    m_barrelCandidates.clear();
    if (modern)
    {
        for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
            m_memberStates[i] = uint8(slots[i] - '0');
        m_crew = crewStates;
        m_restoringCrew = true;
        m_crewRestorePending.fill(true);
        m_descended = descended != 0;
        m_betrayalPlayer = ObjectGuid(target);
        m_auiEncounter[TYPE_PYRAMID_EVENT] =
            states[TYPE_PYRAMID_EVENT] == DONE
                ? DONE
                : (states[TYPE_PYRAMID_EVENT] == IN_PROGRESS || states[TYPE_PYRAMID_EVENT] == FAIL ? FAIL
                                                                                                   : NOT_STARTED);
        if (m_doorState == IN_PROGRESS)
        {
            m_doorTimer = 15000;
            if (m_crew[3].waypoint >= 13)
            {
                m_crew[3].waypoint = 13;
                m_crew[3].paused = false;
            }
        }
    }
    else
    {
        m_memberStates.fill(ZF_MEMBER_WAITING);
        m_pyramidElapsed = 0;
        m_dispatchTimer = 20000;
        m_descended = false;
        m_doorState = NOT_STARTED;
        m_doorRetries = m_doorTimer = 0;
        m_betrayalState = NOT_STARTED;
        m_betrayalTimer = 0;
        m_betrayalPlayer.Clear();
        if (states[TYPE_PYRAMID_EVENT] == DONE)
        {
            m_memberStates.fill(ZF_MEMBER_KILLED);
            m_pyramidElapsed = 250000;
            m_descended = true;
            m_doorState = DONE; // One-time compatibility with the original nine-field save contract.
            SetLegacyCrewPositions();
        }
        else if (states[TYPE_NEKRUM] == DONE || states[TYPE_SEZZZIZ] == DONE)
        {
            // Legacy temporary trash had no saved identities; never reissue its rewards.
            m_memberStates.fill(ZF_MEMBER_KILLED);
            m_pyramidElapsed = 250000;
            m_descended = true;
            for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
                if ((pyramidSpawns[i].entry == NPC_NEKRUM && states[TYPE_NEKRUM] != DONE) ||
                    (pyramidSpawns[i].entry == NPC_SEZZZIZ && states[TYPE_SEZZZIZ] != DONE))
                    m_memberStates[i] = ZF_MEMBER_REQUESTED;
            m_auiEncounter[TYPE_PYRAMID_EVENT] = FAIL;
            SetLegacyCrewPositions();
        }
    }
    m_gahzrillaGuid.Clear();
    m_gahzrillaTimer = m_gahzrillaMoveTimer = m_gahzrillaFailures = 0;
    m_creatingMember = -1;
    m_creatingGahzrilla = false;
    m_pollTimer = 1000;
    m_checkpointTimer = 5000;
    m_missingPlayersTimer = 5000;
    BuildSaveData();
    if (GameObject *door = GetSingleGameObjectFromStorage(GO_END_DOOR, true))
        SyncObject(door);
}

uint32 instance_zulfarrak::GetData(uint32 type) const
{
    if (type < MAX_ENCOUNTER)
        return m_auiEncounter[type];
    if (type == TYPE_END_DOOR)
        return m_doorState;
    if (type == TYPE_BLY_BETRAYAL)
        return m_betrayalState;
    return 0;
}
void instance_zulfarrak::SetData(uint32 type, uint32 state)
{
    if (type >= MAX_ENCOUNTER || m_auiEncounter[type] == DONE || m_auiEncounter[type] == state)
        return;
    if (type == TYPE_PYRAMID_EVENT)
    {
        if (state == DONE && !AllMembersKilled(ZF_PYRAMID_MEMBERS))
            return;
        m_auiEncounter[type] = state;
    }
    else
        m_auiEncounter[type] = state;
    if (state == DONE)
    {
        CaptureCrew();
        SaveState();
    }
}
void instance_zulfarrak::OnCreatureEnterCombat(Creature *creature)
{
    uint32 type = EncounterType(creature->GetEntry());
    if (type == TYPE_GAHZRILLA && m_gahzrillaGuid != creature->GetObjectGuid())
        return;
    if ((type == TYPE_NEKRUM || type == TYPE_SEZZZIZ) && MemberIndex(creature->GetObjectGuid()) < 0)
        return;
    if (type < MAX_ENCOUNTER)
        SetData(type, IN_PROGRESS);
}
void instance_zulfarrak::OnCreatureEvade(Creature *creature)
{
    uint32 type = EncounterType(creature->GetEntry());
    if (type == TYPE_GAHZRILLA && m_gahzrillaGuid != creature->GetObjectGuid())
        return;
    if ((type == TYPE_NEKRUM || type == TYPE_SEZZZIZ) && MemberIndex(creature->GetObjectGuid()) < 0)
        return;
    if (type < MAX_ENCOUNTER)
        SetData(type, FAIL);
}
void instance_zulfarrak::OnCreatureDeath(Creature *creature)
{
    int crew = CrewIndex(creature->GetEntry());
    if (crew >= 0 && m_crewGuids[crew] == creature->GetObjectGuid())
    {
        m_crew[crew].life = ZF_CREW_DEAD;
        if (crew == 3 && m_doorState == IN_PROGRESS)
        {
            m_doorState = NOT_STARTED;
            m_doorTimer = 0;
        }
        SaveState();
    }
    int member = MemberIndex(creature->GetObjectGuid());
    if (member >= 0 && m_memberStates[member] == ZF_MEMBER_REQUESTED)
    {
        m_memberStates[member] = ZF_MEMBER_KILLED;
        uint32 type = EncounterType(creature->GetEntry());
        if (type == TYPE_NEKRUM || type == TYPE_SEZZZIZ)
            m_auiEncounter[type] = DONE;
        if (AllMembersKilled(ZF_PYRAMID_MEMBERS))
            m_auiEncounter[TYPE_PYRAMID_EVENT] = DONE;
        CaptureCrew();
        SaveState();
        return;
    }
    uint32 type = EncounterType(creature->GetEntry());
    if (type == TYPE_NEKRUM || type == TYPE_SEZZZIZ)
        return;
    if (type == TYPE_GAHZRILLA && m_gahzrillaGuid != creature->GetObjectGuid())
        return;
    if (type < MAX_ENCOUNTER)
        SetData(type, DONE);
}

bool instance_zulfarrak::StartPyramidEvent(Player *player)
{
    GameObject *anchor = PyramidAnchor();
    if (!IsValidPlayer(player, anchor, 30.0f) || GetData(TYPE_PYRAMID_EVENT) == DONE ||
        GetData(TYPE_PYRAMID_EVENT) == IN_PROGRESS)
        return false;
    if (GetData(TYPE_PYRAMID_EVENT) == FAIL)
    {
        ResumePyramid();
        return true;
    }
    m_auiEncounter[TYPE_PYRAMID_EVENT] = IN_PROGRESS;
    m_pyramidElapsed = 0;
    m_dispatchTimer = 20000;
    m_descended = false;
    CaptureCrew();
    SaveState();
    RunActorRelay(ZF_RELAY_RELEASE, anchor, anchor);
    return true;
}
void instance_zulfarrak::SpawnRequestedMembers()
{
    if (m_spawnedThisUpdate)
        return;
    GameObject *anchor = PyramidAnchor();
    if (!anchor)
        return;
    m_spawnedThisUpdate = true;
    for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
    {
        if (m_memberStates[i] != ZF_MEMBER_REQUESTED)
            continue;
        if (Creature *existing = instance->GetCreature(m_memberGuids[i]))
            if (existing->IsAlive())
                continue;
        auto const &spawn = pyramidSpawns[i];
        m_creatingMember = int(i);
        Creature *creature = anchor->SummonCreature(spawn.entry, spawn.x, spawn.y, spawn.z, spawn.o,
                                                    TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 9000000);
        m_creatingMember = -1;
        if (creature)
        {
            m_memberGuids[i] = creature->GetObjectGuid();
            m_dispatched[i] = false;
        }
    }
}
void instance_zulfarrak::DispatchMember()
{
    std::vector<uint32> candidates;
    for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
        if (m_memberStates[i] == ZF_MEMBER_REQUESTED && !m_dispatched[i] && pyramidSpawns[i].entry != NPC_NEKRUM &&
            pyramidSpawns[i].entry != NPC_SEZZZIZ)
            if (Creature *troll = instance->GetCreature(m_memberGuids[i]))
                if (troll->IsAlive() && !troll->IsInCombat())
                    candidates.push_back(i);
    if (candidates.empty())
        return;
    WorldObject *target = nullptr;
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (Creature *crew = Crew(i))
            if (crew->IsAlive() && m_crew[i].life == ZF_CREW_ALIVE)
            {
                target = crew;
                break;
            }
    if (!target)
        if (GameObject *anchor = PyramidAnchor())
            target = SelectPlayer(anchor, ObjectGuid(), 220.0f);
    if (!target)
        return; // No candidate is consumed until a destination is available.
    uint32 slot = candidates[urand(0, uint32(candidates.size() - 1))];
    Creature *troll = instance->GetCreature(m_memberGuids[slot]);
    if (!troll)
        return;
    float x, y, z;
    target->GetRandomPoint(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 4.0f, x, y, z);
    troll->SetWalk(false);
    troll->GetMotionMaster()->MovePoint(0, x, y, z);
    m_dispatched[slot] = true;
}
void instance_zulfarrak::PausePyramid()
{
    CaptureCrew();
    m_auiEncounter[TYPE_PYRAMID_EVENT] = FAIL;
    SaveState();
    for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
        if (m_memberStates[i] == ZF_MEMBER_REQUESTED)
            if (Creature *creature = instance->GetCreature(m_memberGuids[i]))
                if (creature->IsAlive())
                    creature->ForcedDespawn();
    for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
        if (Creature *crew = Crew(i))
            if (crew->IsAlive())
            {
                crew->CombatStop(true);
                crew->getThreatManager().clearReferences();
                crew->GetMotionMaster()->PauseWaypoints(0);
            }
}
void instance_zulfarrak::ResumePyramid()
{
    m_auiEncounter[TYPE_PYRAMID_EVENT] = IN_PROGRESS;
    m_missingPlayersTimer = 5000;
    m_restoringCrew = true;
    m_crewRestorePending.fill(true);
    RestoreCrew();
    SaveState();
    if (m_pyramidElapsed < 5000)
        if (GameObject *anchor = PyramidAnchor())
            RunActorRelay(ZF_RELAY_RELEASE, anchor, anchor);
}
void instance_zulfarrak::UpdatePyramid(uint32 diff)
{
    if (GetData(TYPE_PYRAMID_EVENT) == FAIL)
    {
        if (HasPyramidPlayers())
            ResumePyramid();
        return;
    }
    if (GetData(TYPE_PYRAMID_EVENT) == DONE)
    {
        if (!m_descended && AllMembersKilled(ZF_FIRST_TWO_WAVES))
            if (GameObject *anchor = PyramidAnchor())
            {
                m_descended = true;
                SaveState();
                RunActorRelay(ZF_RELAY_DESCEND, anchor, anchor);
            }
        return;
    }
    if (GetData(TYPE_PYRAMID_EVENT) != IN_PROGRESS)
        return;
    if (!HasPyramidPlayers())
    {
        if (m_missingPlayersTimer <= diff)
        {
            PausePyramid();
            return;
        }
        m_missingPlayersTimer -= diff;
    }
    else
        m_missingPlayersTimer = 5000;
    m_pyramidElapsed = std::min(uint32(86400000), m_pyramidElapsed + diff);
    bool changed = false;
    for (uint32 i = 0; i < ZF_PYRAMID_MEMBERS; ++i)
        if (m_memberStates[i] == ZF_MEMBER_WAITING && pyramidSpawns[i].delay <= m_pyramidElapsed)
        {
            m_memberStates[i] = ZF_MEMBER_REQUESTED;
            changed = true;
        }
    if (changed)
    {
        SaveState();             // Persist spawn intent before exposing any member/reward.
        SpawnRequestedMembers(); // Keep the original exact10/100/250-second boundaries.
    }
    if (m_dispatchTimer <= diff)
    {
        DispatchMember();
        m_dispatchTimer = urand(0, 2) ? urand(3000, 10000) : 1000;
    }
    else
        m_dispatchTimer -= diff;
    if (!m_descended && m_pyramidElapsed >= 250000 && AllMembersKilled(ZF_FIRST_TWO_WAVES))
        if (GameObject *anchor = PyramidAnchor())
        {
            m_descended = true;
            SaveState();
            RunActorRelay(ZF_RELAY_DESCEND, anchor, anchor);
        }
}

bool instance_zulfarrak::CanStartBlyBetrayal(Player const *player) const
{
    Creature *bly = Crew(0);
    return GetData(TYPE_PYRAMID_EVENT) == DONE && m_betrayalState == NOT_STARTED && m_crew[0].life == ZF_CREW_ALIVE &&
           bly && bly->IsAlive() && !bly->IsInCombat() && bly->GetMotionMaster()->getLastReachedWaypoint() == 8 &&
           IsValidPlayer(player, bly, 5.0f);
}
bool instance_zulfarrak::StartBlyBetrayal(Player *player)
{
    if (!CanStartBlyBetrayal(player))
        return false;
    m_betrayalState = IN_PROGRESS;
    m_betrayalTimer = 6000;
    m_betrayalPlayer = player->GetObjectGuid();
    CaptureCrew();
    SaveState();
    RunActorRelay(ZF_RELAY_BLY_START, Crew(0), player);
    return true;
}
void instance_zulfarrak::UpdateBetrayal(uint32 diff)
{
    if (m_betrayalState != IN_PROGRESS)
        return;
    if (m_betrayalTimer > diff)
    {
        m_betrayalTimer -= diff;
        return;
    }
    Creature *bly = Crew(0);
    if (!bly)
    {
        m_betrayalTimer = 1000;
        return;
    }
    m_betrayalTimer = 0;
    m_betrayalState = DONE;
    if (bly->IsAlive())
    {
        Player *target = SelectPlayer(bly, m_betrayalPlayer, 50.0f);
        for (uint32 i = 0; i < ZF_CREW_COUNT; ++i)
            if (i != 3 && m_crew[i].life == ZF_CREW_ALIVE)
                if (Creature *crew = Crew(i))
                    if (crew->IsAlive())
                    {
                        crew->SetFactionTemporary(14, TEMPFACTION_RESTORE_RESPAWN);
                        if (target)
                            crew->AI()->AttackStart(target);
                    }
        StartDoorDemolition(target, true);
    }
    CaptureCrew();
    SaveState();
}
bool instance_zulfarrak::CanStartDoorDemolition(Player const *player) const
{
    Creature *weegli = Crew(3);
    return GetData(TYPE_PYRAMID_EVENT) == DONE && m_doorState == NOT_STARTED && m_crew[3].life == ZF_CREW_ALIVE &&
           weegli && weegli->IsAlive() && !weegli->IsInCombat() &&
           weegli->GetMotionMaster()->getLastReachedWaypoint() == 10 && IsValidPlayer(player, weegli, 5.0f);
}
bool instance_zulfarrak::StartDoorDemolition(Player *player, bool fromBly)
{
    Creature *weegli = Crew(3);
    if (fromBly)
    {
        if (GetData(TYPE_PYRAMID_EVENT) != DONE || m_doorState != NOT_STARTED || !weegli || !weegli->IsAlive() ||
            m_crew[3].life != ZF_CREW_ALIVE || weegli->GetMotionMaster()->getLastReachedWaypoint() != 10)
            return false;
    }
    else if (!CanStartDoorDemolition(player))
        return false;
    m_doorState = IN_PROGRESS;
    m_doorTimer = 60000;
    m_doorRetries = 0;
    m_barrelGuid.Clear();
    m_barrelCandidates.clear();
    weegli->CombatStop(true);
    weegli->getThreatManager().clearReferences();
    weegli->AI()->SetReactState(REACT_PASSIVE);
    CaptureCrew();
    SaveState();
    RunActorRelay(fromBly ? ZF_RELAY_DOOR_FROM_BLY : ZF_RELAY_DOOR_START, weegli, player);
    return true;
}
bool instance_zulfarrak::ConfirmDoorDestroyed(GameObject *door, WorldObject *caster)
{
    if (m_doorState != IN_PROGRESS || !door || door->GetMap() != instance || door->GetEntry() != GO_END_DOOR ||
        door->GetGoState() != GO_STATE_ACTIVE_ALTERNATIVE || !caster || caster->GetEntry() != GO_WEEGLI_BARREL ||
        caster->GetMap() != instance)
        return false;
    // Owner assignment may follow OnObjectSpawn, and the spell may finish before
    // the next instance poll. Validate the actual caster instead of losing success.
    if (caster->GetObjectGuid() != m_barrelGuid)
        if (GameObject *barrel = dynamic_cast<GameObject *>(caster))
            RegisterBarrel(barrel);
    if (caster->GetObjectGuid() != m_barrelGuid)
        return false;
    m_doorState = DONE;
    m_doorTimer = 0;
    CaptureCrew();
    SaveState();
    return true;
}
void instance_zulfarrak::UpdateDoor(uint32 diff)
{
    if (m_doorState != IN_PROGRESS)
        return;
    Creature *weegli = Crew(3);
    if (m_crew[3].life != ZF_CREW_ALIVE || (weegli && !weegli->IsAlive()))
    {
        m_doorState = NOT_STARTED;
        m_doorTimer = 0;
        SaveState();
        return;
    }
    if (!weegli)
        return; // An unloaded living actor is restored from its checkpoint.
    if (m_doorTimer > diff)
    {
        m_doorTimer -= diff;
        return;
    }
    if (++m_doorRetries <= 3)
    {
        if (GameObject *barrel = instance->GetGameObject(m_barrelGuid))
            barrel->ForcedDespawn();
        m_barrelGuid.Clear();
        m_doorTimer = 15000;
        weegli->GetMotionMaster()->SetNextWaypoint(13);
        weegli->GetMotionMaster()->UnpauseWaypoints();
    }
    else
    {
        m_doorState = NOT_STARTED;
        m_doorTimer = 0;
        weegli->AI()->SetReactState(REACT_AGGRESSIVE);
        weegli->GetMotionMaster()->SetNextWaypoint(10);
        weegli->GetMotionMaster()->UnpauseWaypoints();
    }
    CaptureCrew();
    SaveState();
}
bool instance_zulfarrak::CheckConditionCriteriaMeet(Player const *player, uint32 id, WorldObject const *, uint32) const
{
    if (id == ZF_CONDITION_BLY)
        return CanStartBlyBetrayal(player);
    if (id == ZF_CONDITION_WEEGLI)
        return CanStartDoorDemolition(player);
    return false;
}

bool instance_zulfarrak::StartGahzrillaEvent(Player *player)
{
    GameObject *gong = GetSingleGameObjectFromStorage(GO_GAHZRILLA_GONG, true);
    if (!IsValidPlayer(player, gong, 30.0f) || GetData(TYPE_GAHZRILLA) == DONE || m_gahzrillaTimer)
        return false;
    if (Creature *existing = instance->GetCreature(m_gahzrillaGuid))
        if (existing->IsAlive())
            return false;
    m_gahzrillaGuid.Clear();
    m_gahzrillaFailures = 0;
    m_gahzrillaTimer = 2000;
    return true;
}
void instance_zulfarrak::UpdateGahzrilla(uint32 diff)
{
    if (m_gahzrillaMoveTimer)
    {
        if (m_gahzrillaMoveTimer <= diff)
        {
            m_gahzrillaMoveTimer = 0;
            if (Creature *boss = instance->GetCreature(m_gahzrillaGuid))
                if (boss->IsAlive())
                    boss->GetMotionMaster()->MoveWaypoint(0);
        }
        else
            m_gahzrillaMoveTimer -= diff;
    }
    if (!m_gahzrillaTimer)
        return;
    if (m_gahzrillaTimer > diff)
    {
        m_gahzrillaTimer -= diff;
        return;
    }
    m_gahzrillaTimer = 0;
    if (GetData(TYPE_GAHZRILLA) == DONE)
        return;
    GameObject *gong = GetSingleGameObjectFromStorage(GO_GAHZRILLA_GONG, true);
    Creature *boss = nullptr;
    if (gong)
    {
        m_creatingGahzrilla = true;
        boss = gong->SummonCreature(NPC_GAHZRILLA, 1667.540649f, 1190.572388f, -2.804439f, 0.890118f,
                                    TEMPSPAWN_DEAD_DESPAWN, 0);
        m_creatingGahzrilla = false;
    }
    if (!boss)
    {
        if (++m_gahzrillaFailures < 3)
            m_gahzrillaTimer = 1000;
        return;
    }
    m_gahzrillaGuid = boss->GetObjectGuid();
    m_auiEncounter[TYPE_GAHZRILLA] = IN_PROGRESS;
    m_gahzrillaMoveTimer = 1000;
    SaveState();
}

void instance_zulfarrak::Update(uint32 diff)
{
    m_spawnedThisUpdate = false;
    // Pending timers must see restored positions/factions, not fresh cage actors.
    if (m_restoringCrew)
        RestoreCrew();
    UpdateGahzrilla(diff);
    UpdatePyramid(diff);
    UpdateBetrayal(diff);
    UpdateDoor(diff);
    if (m_pollTimer <= diff)
    {
        m_pollTimer = 1000;
        RestoreCrew();
        for (auto it = m_rejectedGahzrilla.begin(); it != m_rejectedGahzrilla.end();)
        {
            if (Creature *creature = instance->GetCreature(*it))
            {
                if (creature->IsAlive())
                    creature->ForcedDespawn();
                ++it;
            }
            else
                it = m_rejectedGahzrilla.erase(it);
        }
        auto candidates = m_barrelCandidates;
        for (ObjectGuid guid : candidates)
            if (GameObject *go = instance->GetGameObject(guid))
                RegisterBarrel(go);
        if (GetData(TYPE_PYRAMID_EVENT) == IN_PROGRESS)
            SpawnRequestedMembers();
    }
    else
        m_pollTimer -= diff;
    if (m_checkpointTimer <= diff)
    {
        m_checkpointTimer = 5000;
        if (GetData(TYPE_PYRAMID_EVENT) == IN_PROGRESS || GetData(TYPE_PYRAMID_EVENT) == DONE)
        {
            CaptureCrew();
            SaveState();
        }
    }
    else
        m_checkpointTimer -= diff;
}

InstanceData *GetInstanceData_instance_zulfarrak(Map *map)
{
    return new instance_zulfarrak(map);
}
void AddSC_instance_zulfarrak()
{
    Script *script = new Script;
    script->Name = "instance_zulfarrak";
    script->GetInstanceData = &GetInstanceData_instance_zulfarrak;
    script->RegisterSelf();
}
