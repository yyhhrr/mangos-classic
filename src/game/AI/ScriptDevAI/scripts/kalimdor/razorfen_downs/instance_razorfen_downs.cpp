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
SDName: instance_razorfen_downs
SD%Complete: 100
SDCategory: Razorfen Downs
EndScriptData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "razorfen_downs.h"
#include <ctime>
#include <sstream>
#include <vector>

instance_razorfen_downs::instance_razorfen_downs(Map* pMap) : ScriptedInstance(pMap)
{
    Initialize();
}

void instance_razorfen_downs::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_uiWaveCounter = 0;
    m_activeWaveIndex = 0;
    m_retryWaveMembers = 0;
    m_creatingWave = m_cleaningWave = m_pendingWaveReset = m_gongBossKilled = false;
    m_idolCompletedAt = 0;
    m_worldStateTimer = 1000;
    m_activeWaveGuids.clear();
    m_idolFireGuids.clear();
}

void instance_razorfen_downs::OnCreatureCreate(Creature* /*creature*/)
{
    // Wave ownership is committed by DoSpawnWaveIfCan, never inferred from entry.
}

void instance_razorfen_downs::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_GONG:
            m_goEntryGuidStore[GO_GONG] = go->GetObjectGuid();
            SyncGong(go);
            break;
        case GO_IDOL_BRAZIER:
            m_goEntryGuidStore[GO_IDOL_BRAZIER] = go->GetObjectGuid();
            break; // Negative spawn metadata is loaded after this callback.
        case GO_IDOL_FIRE_OVEN:
        case GO_IDOL_FIRE_CUP:
        case GO_IDOL_FIRE_MOUTH:
            m_idolFireGuids.insert(go->GetObjectGuid());
            break;
    }
}

void instance_razorfen_downs::OnObjectSpawn(GameObject* go)
{
    if (go->GetEntry() == GO_GONG)
        SyncGong(go);
    else
        ApplyIdolState(go);
}

void instance_razorfen_downs::SaveState()
{
    // The first field preserves compatibility with the old one-integer format.
    std::ostringstream stream;
    stream << m_auiEncounter[TYPE_TUTEN_KASH] << ' ' << m_auiEncounter[TYPE_IDOL_EVENT] << ' ' << m_idolCompletedAt;
    m_strInstData = stream.str();
    SaveToDB();
}

void instance_razorfen_downs::SetData(uint32 type, uint32 state)
{
    if (type >= MAX_ENCOUNTER)
        return;
    if (type == TYPE_IDOL_EVENT)
    {
        if (state == DONE)
            CompleteIdolEvent();
        return; // Starting another escort never restores extinguished fires.
    }
    if (m_auiEncounter[TYPE_TUTEN_KASH] == DONE)
        return;
    if (state == DONE)
    {
        if (!m_gongBossKilled)
            return;
        m_auiEncounter[type] = DONE;
        m_uiWaveCounter = MAX_WAVES;
        SyncGong();
        SaveState();
    }
    else if (state == FAIL)
        m_pendingWaveReset = !m_activeWaveGuids.empty();
    // Wave progression is owned by the successful summon commit, not SetData.
}

uint32 instance_razorfen_downs::GetData(uint32 type) const
{
    return type < MAX_ENCOUNTER ? m_auiEncounter[type] : 0;
}

void instance_razorfen_downs::Load(const char* input)
{
    if (!input)
        return;
    uint32 gong = NOT_STARTED, idol = NOT_STARTED;
    uint64 completed = 0;
    std::istringstream stream(input);
    if (!(stream >> gong))
        return;
    // Old saves contain only the gong encounter. Missing optional fields are safe.
    if (stream >> idol)
        if (!(stream >> completed))
            completed = 0;
    m_auiEncounter[TYPE_TUTEN_KASH] = gong == DONE ? DONE : NOT_STARTED;
    m_auiEncounter[TYPE_IDOL_EVENT] = idol == DONE ? DONE : NOT_STARTED;
    const uint64 now = uint64(time(nullptr));
    m_idolCompletedAt = idol == DONE && completed <= now ? completed : 0;
    m_uiWaveCounter = gong == DONE ? MAX_WAVES : 0;
    m_activeWaveIndex = m_retryWaveMembers = 0;
    m_creatingWave = m_cleaningWave = m_pendingWaveReset = m_gongBossKilled = false;
    m_activeWaveGuids.clear();
    SyncGong();
    UpdateIdolObjects();
    // Normalize old/partial saves for Save() without issuing another DB write.
    std::ostringstream restored;
    restored << m_auiEncounter[TYPE_TUTEN_KASH] << ' ' << m_auiEncounter[TYPE_IDOL_EVENT] << ' ' << m_idolCompletedAt;
    m_strInstData = restored.str();
}

void instance_razorfen_downs::SyncGong(GameObject* go)
{
    if (!go)
        go = GetSingleGameObjectFromStorage(GO_GONG, true);
    if (!go)
        return;
    if (m_auiEncounter[TYPE_TUTEN_KASH] == DONE || m_creatingWave || m_pendingWaveReset || !m_activeWaveGuids.empty())
        go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    else
    {
        go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        go->Respawn(); // Existing workaround for the goober's action despawn.
    }
}

void instance_razorfen_downs::OnCreatureDeath(Creature* creature)
{
    if (m_creatingWave || m_cleaningWave || m_activeWaveGuids.erase(creature->GetObjectGuid()) == 0)
        return;
    // Only the real kill callback can complete the final wave.
    if (creature->GetEntry() == NPC_TUTEN_KASH && m_activeWaveIndex == MAX_WAVES - 1)
    {
        m_gongBossKilled = true;
        m_pendingWaveReset = false;
        SetData(TYPE_TUTEN_KASH, DONE);
    }
    else if (m_activeWaveGuids.empty())
    {
        m_pendingWaveReset = false;
        SyncGong();
    }
}

void instance_razorfen_downs::OnCreatureEvade(Creature* creature)
{
    if (!m_creatingWave && !m_cleaningWave && m_activeWaveGuids.count(creature->GetObjectGuid()))
        m_pendingWaveReset = true;
    // Defer cleanup until the AI's evade callback has returned.
}

void instance_razorfen_downs::OnCreatureDespawn(Creature* creature)
{
    if (!m_creatingWave && !m_cleaningWave && m_activeWaveGuids.count(creature->GetObjectGuid()))
        m_pendingWaveReset = true;
    // Killed actors were removed in OnCreatureDeath; unexpected despawn is a failure.
}

void instance_razorfen_downs::AbortWave()
{
    if (m_cleaningWave || m_activeWaveGuids.empty() || m_auiEncounter[TYPE_TUTEN_KASH] == DONE)
    {
        m_pendingWaveReset = false;
        return;
    }
    m_cleaningWave = true;
    GuidSet actors;
    actors.swap(m_activeWaveGuids); // Despawn callbacks cannot mutate this iteration.
    m_retryWaveMembers = uint8(actors.size()); // Do not recreate already killed, rewarded actors.
    m_uiWaveCounter = m_activeWaveIndex;
    m_auiEncounter[TYPE_TUTEN_KASH] = NOT_STARTED;
    m_pendingWaveReset = false;
    for (ObjectGuid guid : actors)
        if (Creature* creature = instance->GetCreature(guid))
            if (creature->IsAlive())
                creature->ForcedDespawn();
    m_cleaningWave = false;
    SyncGong();
}

void instance_razorfen_downs::DoSpawnWaveIfCan(GameObject* go)
{
    if (!go || go->GetEntry() != GO_GONG || go->GetMap() != instance ||
        m_auiEncounter[TYPE_TUTEN_KASH] == DONE || m_creatingWave || m_cleaningWave ||
        m_pendingWaveReset || !m_activeWaveGuids.empty() || m_uiWaveCounter >= MAX_WAVES)
        return;
    m_creatingWave = true;
    const auto& wave = aWaveSummonInformation[m_uiWaveCounter];
    const uint8 count = m_retryWaveMembers ? m_retryWaveMembers : wave.m_uiNPCperWave;
    GuidVector created;
    for (uint8 i = 0; i < count; ++i)
    {
        const uint8 position = i % MAX_COUNT_POS;
        float x, y, z;
        go->GetRandomPoint(aSpawnLocations[position].x, aSpawnLocations[position].y,
            aSpawnLocations[position].z, 5.0f, x, y, z);
        Creature* creature = go->SummonCreature(wave.m_uiNpcEntry, x, y, z,
            aSpawnLocations[position].o, TEMPSPAWN_DEAD_DESPAWN, 0);
        if (!creature)
            break;
        created.push_back(creature->GetObjectGuid());
        if (!creature->IsAlive())
            break;
    }
    bool ready = created.size() == count;
    for (ObjectGuid guid : created)
    {
        Creature* creature = instance->GetCreature(guid);
        if (!creature || !creature->IsAlive())
            ready = false;
    }
    if (!ready)
    {
        // No committed wave, no progression, no repeatable reward from partial setup.
        for (ObjectGuid guid : created)
            if (Creature* creature = instance->GetCreature(guid))
            {
                creature->SetNoRewards();
                creature->ForcedDespawn();
            }
        m_creatingWave = false;
        SyncGong(go);
        return;
    }
    m_activeWaveGuids.insert(created.begin(), created.end());
    m_activeWaveIndex = m_uiWaveCounter++;
    m_retryWaveMembers = 0;
    m_auiEncounter[TYPE_TUTEN_KASH] = IN_PROGRESS;
    m_creatingWave = false;
    SyncGong(go);
    go->SendGameObjectCustomAnim(go->GetObjectGuid());
    for (ObjectGuid guid : created)
        if (Creature* creature = instance->GetCreature(guid))
        {
            float x, y, z;
            go->GetContactPoint(creature, x, y, z);
            creature->SetWalk(false);
            creature->GetMotionMaster()->MovePoint(0, x, y, z);
        }
}

void instance_razorfen_downs::CompleteIdolEvent()
{
    m_auiEncounter[TYPE_IDOL_EVENT] = DONE;
    m_idolCompletedAt = uint64(time(nullptr));
    UpdateIdolObjects();
    SaveState();
}

void instance_razorfen_downs::ApplyIdolState(GameObject* go)
{
    if (!go || m_auiEncounter[TYPE_IDOL_EVENT] != DONE)
        return;
    switch (go->GetEntry())
    {
        case GO_IDOL_FIRE_OVEN:
        case GO_IDOL_FIRE_CUP:
        case GO_IDOL_FIRE_MOUTH:
            if (go->IsSpawned())
                go->SetLootState(GO_JUST_DEACTIVATED);
            break;
        case GO_IDOL_BRAZIER:
        {
            const uint64 now = uint64(time(nullptr));
            const uint64 expires = m_idolCompletedAt + HOUR;
            if (m_idolCompletedAt && expires > now)
            {
                const bool wasSpawned = go->IsSpawned();
                if (go->GetRespawnTime() != time_t(expires))
                    go->SetRespawnTime(time_t(expires - now)); // Seconds, never milliseconds.
                if (!wasSpawned)
                    go->Refresh(); // May re-enter OnObjectSpawn; the timer is already installed.
            }
            else if (go->IsSpawned())
                go->SetLootState(GO_JUST_DEACTIVATED);
            break;
        }
    }
}

void instance_razorfen_downs::UpdateIdolObjects()
{
    if (GameObject* brazier = GetSingleGameObjectFromStorage(GO_IDOL_BRAZIER, true))
        ApplyIdolState(brazier);
    for (ObjectGuid guid : m_idolFireGuids)
        if (GameObject* fire = instance->GetGameObject(guid))
            ApplyIdolState(fire);
}

void instance_razorfen_downs::Update(uint32 diff)
{
    if (m_pendingWaveReset)
        AbortWave();
    if (m_worldStateTimer > diff)
        m_worldStateTimer -= diff;
    else
    {
        m_worldStateTimer = 1000;
        if (m_auiEncounter[TYPE_IDOL_EVENT] == DONE)
            UpdateIdolObjects();
    }
}

InstanceData* GetInstanceData_instance_razorfen_downs(Map* map)
{
    return new instance_razorfen_downs(map);
}

bool ProcessEventId_event_go_tutenkash_gong(uint32 /*eventId*/, Object* source, Object* target, bool isStart)
{
    if (!source || !target || source->GetTypeId() != TYPEID_PLAYER || target->GetTypeId() != TYPEID_GAMEOBJECT)
        return false;
    if (!isStart)
        return true;
    if (auto data = dynamic_cast<instance_razorfen_downs*>(static_cast<Player*>(source)->GetInstanceData()))
        data->DoSpawnWaveIfCan(static_cast<GameObject*>(target));
    return true;
}

void AddSC_instance_razorfen_downs()
{
    Script* script = new Script;
    script->Name = "instance_razorfen_downs";
    script->GetInstanceData = &GetInstanceData_instance_razorfen_downs;
    script->RegisterSelf();
    script = new Script;
    script->Name = "event_go_tutenkash_gong";
    script->pProcessEventId = &ProcessEventId_event_go_tutenkash_gong;
    script->RegisterSelf();
}
