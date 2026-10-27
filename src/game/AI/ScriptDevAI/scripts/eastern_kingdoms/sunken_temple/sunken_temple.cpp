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
SDName: instance_sunken_temple
SD%Complete: 90
SDComment: Hakkar Summon Event needs more sources to improve
SDCategory: Sunken Temple
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "Entities/TemporarySpawn.h"
#include "sunken_temple.h"

namespace
{
    uint32 const HAKKAR_RETRY_DELAY = 1000;
    uint8 const MAX_SUMMON_RETRIES = 5;
    uint32 const DRAGON_LIFETIME = DAY * IN_MILLISECONDS;
    uint32 const dragonEntries[2] = { NPC_DREAMSCYTH, NPC_WEAVER };
    SummonLocations const dragonLocations[2] =
    {
        { -422.56207275390625f, 69.0182876586914f, -57.812232971191406f, 2.7241601943969727f },
        { -428.4565734863281f, 117.45812225341797f, -58.15437316894531f, 3.6598100662231445f }
    };
}

instance_sunken_temple::instance_sunken_temple(Map* map) : ScriptedInstance(map)
{
    Initialize();
}

void instance_sunken_temple::Initialize()
{
    memset(m_auiEncounter, 0, sizeof(m_auiEncounter));
    memset(m_dragonState, 0, sizeof(m_dragonState));
    memset(m_dragonExpires, 0, sizeof(m_dragonExpires));
    m_uiStatueCounter = 0;
    m_uiProtectorsRemaining = 0;
    m_uiFlameCounter = 0;
    m_uiAvatarSummonTimer = m_uiSupressorTimer = m_suppressionTimer = 0;
    m_startRetryTimer = m_transformRetryTimer = 0;
    m_restoreTimer = 1000;
    m_saveTimer = 0;
    m_startRetries = m_waveRetries = m_suppressorRetries = 0;
    m_transformRetries = m_restoreRetries = 0;
    m_bIsFirstHakkarWave = m_bCanSummonBloodkeeper = false;
    m_firstWaveInitialized = m_firstBloodkeeperSummoned = false;
    m_avatarSummoned = false;
    m_idolExpires = 0;
}

void instance_sunken_temple::OnPlayerEnter(Player* /*player*/)
{
    m_restoreRetries = 0;
    m_restoreTimer = 1000;
}

void instance_sunken_temple::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_JAMMALAN_BARRIER:
            go->SetGoState(GetData(TYPE_PROTECTORS) == DONE ? GO_STATE_ACTIVE : GO_STATE_READY);
            break;
        case GO_HAKKAR_DOOR_1:
        case GO_HAKKAR_DOOR_2:
            // These two templates have startOpen=1; ACTIVE is open.
            go->SetGoState(GetData(TYPE_AVATAR) == IN_PROGRESS ? GO_STATE_READY : GO_STATE_ACTIVE);
            break;
        case GO_IDOL_OF_HAKKAR:
            // LoadFromDB has not finished setting the negative spawn's lifetime here.
            m_restoreTimer = 1000;
            break;
        case GO_ATALAI_LIGHT_BIG:
            m_luiBigLightGUIDs.insert(go->GetObjectGuid());
            m_restoreTimer = 1000;
            return;
        case GO_EVIL_CIRCLE:
            m_evilCircleGuidList.insert(go->GetObjectGuid());
            if (GetData(TYPE_AVATAR) == IN_PROGRESS && !m_avatarSummoned)
                m_restoreTimer = 1000;
            return;
        case GO_ETERNAL_FLAME_1:
        case GO_ETERNAL_FLAME_2:
        case GO_ETERNAL_FLAME_3:
        case GO_ETERNAL_FLAME_4:
            m_luiFlameGUIDs.insert(go->GetObjectGuid());
            if (GetData(TYPE_AVATAR) == IN_PROGRESS && m_dousedFlames.count(go->GetObjectGuid()) == 0 && !m_avatarSummoned)
                go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            else
                go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            return;
        default:
            return;
    }
    m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
}

void instance_sunken_temple::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_ZOLO:
        case NPC_GASHER:
        case NPC_LORO:
        case NPC_HUKKU:
        case NPC_ZULLOR:
        case NPC_MIJAN:
            if (m_protectorGuids.insert(creature->GetObjectGuid()).second)
                ++m_uiProtectorsRemaining;
            break;
        case NPC_JAMMALAN:
        case NPC_OGOM:
            if (GetData(TYPE_PROTECTORS) == DONE)
                creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
        case NPC_ATALARION:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
        case NPC_SHADE_OF_ERANIKUS:
            if (GetData(TYPE_JAMMALAN) == DONE)
                creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
        case NPC_MALFURION:
            m_malfurionGuid = creature->GetObjectGuid();
            break;
    }
}

bool instance_sunken_temple::IsCurrentHakkarMember(Creature* creature) const
{
    if (!creature || GetData(TYPE_AVATAR) != IN_PROGRESS)
        return false;
    if (creature->GetObjectGuid() == m_hakkarGuid)
        return true;
    return m_hakkarSummons.count(creature->GetObjectGuid()) != 0 && creature->GetSpawnerGuid() == m_hakkarGuid;
}

void instance_sunken_temple::OnCreatureEvade(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_BLOODKEEPER:
        case NPC_HAKKARI_MINION:
        case NPC_SUPPRESSOR:
        case NPC_AVATAR_OF_HAKKAR:
            if (IsCurrentHakkarMember(creature))
                EndAvatarEvent(FAIL);
            break;
        case NPC_JAMMALAN:
        case NPC_OGOM:
            if (GetData(TYPE_PROTECTORS) == DONE)
                creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            break;
        case NPC_SHADE_OF_ERANIKUS:
            if (GetData(TYPE_JAMMALAN) == DONE)
                creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            break;
        case NPC_DREAMSCYTH:
        case NPC_WEAVER:
            for (uint32 i = 0; i < 2; ++i)
                if (creature->GetObjectGuid() == m_dragonGuid[i] && m_dragonState[i] == 0)
                {
                    // The native summon timer restarts its 24-hour OOC lifetime in combat.
                    // A restored summon initially has only its saved remaining lifetime.
                    if (creature->IsTemporarySummon())
                        static_cast<TemporarySpawn*>(creature)->SetSummonProperties(TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, DRAGON_LIFETIME);
                    m_dragonExpires[i] = time(nullptr) + DAY;
                    SaveProgress();
                }
            break;
    }
}

void instance_sunken_temple::OnCreatureDeath(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_ATALARION:
            if (creature->GetObjectGuid() == m_npcEntryGuidStore[NPC_ATALARION])
                SetData(TYPE_ATALARION, DONE);
            break;
        case NPC_JAMMALAN:
            SetData(TYPE_JAMMALAN, DONE);
            break;
        case NPC_AVATAR_OF_HAKKAR:
            if (creature->GetObjectGuid() == m_hakkarGuid && m_avatarSummoned && IsCurrentHakkarMember(creature))
                EndAvatarEvent(DONE);
            break;
        case NPC_SUPPRESSOR:
            if (!m_avatarSummoned && IsCurrentHakkarMember(creature))
                m_bCanSummonBloodkeeper = true;
            break;
        case NPC_DREAMSCYTH:
        case NPC_WEAVER:
            for (uint32 i = 0; i < 2; ++i)
                if (creature->GetObjectGuid() == m_dragonGuid[i] && m_dragonState[i] == 0)
                {
                    m_dragonState[i] = 1;
                    SaveProgress();
                }
            break;
        case NPC_ZOLO:
        case NPC_GASHER:
        case NPC_LORO:
        case NPC_HUKKU:
        case NPC_ZULLOR:
        case NPC_MIJAN:
            if (m_protectorGuids.count(creature->GetObjectGuid()) != 0 && m_deadProtectorGuids.insert(creature->GetObjectGuid()).second)
                SetData(TYPE_PROTECTORS, DONE);
            break;
    }
}

void instance_sunken_temple::OnCreatureDespawn(Creature* creature)
{
    for (uint32 i = 0; i < 2; ++i)
        if (creature->GetObjectGuid() == m_dragonGuid[i] && m_dragonState[i] == 0)
        {
            m_dragonGuid[i].Clear();
            m_restoreRetries = 0;
            m_restoreTimer = 1000;
        }
}

void instance_sunken_temple::UpdateDoors()
{
    if (GameObject* barrier = GetSingleGameObjectFromStorage(GO_JAMMALAN_BARRIER, true))
        barrier->SetGoState(GetData(TYPE_PROTECTORS) == DONE ? GO_STATE_ACTIVE : GO_STATE_READY);
    for (uint32 entry : { GO_HAKKAR_DOOR_1, GO_HAKKAR_DOOR_2 })
        if (GameObject* door = GetSingleGameObjectFromStorage(entry, true))
            door->SetGoState(GetData(TYPE_AVATAR) == IN_PROGRESS ? GO_STATE_READY : GO_STATE_ACTIVE);
}

void instance_sunken_temple::SetData(uint32 type, uint32 data)
{
    if (type >= MAX_ENCOUNTER || m_auiEncounter[type] == DONE)
        return;
    switch (type)
    {
        case TYPE_ATALARION:
            if (data != SPECIAL && data != DONE)
                return;
            m_auiEncounter[type] = data;
            m_restoreRetries = 0;
            m_restoreTimer = 1;
            SaveProgress();
            break;
        case TYPE_PROTECTORS:
            if (data != DONE || !m_uiProtectorsRemaining || --m_uiProtectorsRemaining)
                return;
            m_auiEncounter[type] = DONE;
            UpdateDoors();
            DoOrSimulateScriptTextForThisInstance(SAY_JAMMALAN_INTRO, NPC_JAMMALAN);
            for (uint32 entry : { NPC_JAMMALAN, NPC_OGOM })
                if (Creature* creature = GetSingleCreatureFromStorage(entry, true))
                    creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            SaveProgress();
            break;
        case TYPE_JAMMALAN:
            if (data != DONE)
                return;
            m_auiEncounter[type] = DONE;
            if (Creature* eranikus = GetSingleCreatureFromStorage(NPC_SHADE_OF_ERANIKUS, true))
                eranikus->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            m_restoreRetries = 0;
            m_restoreTimer = 1;
            SaveProgress();
            break;
        case TYPE_MALFURION:
            if (data == IN_PROGRESS && HasMalfurion())
                m_auiEncounter[type] = IN_PROGRESS;
            break;
        case TYPE_AVATAR:
            // SPECIAL used to ambiguously mean both a suppression hit and a flame.
            // Neither operation is accepted through this unqualified interface now.
            if (data == IN_PROGRESS && m_auiEncounter[type] != IN_PROGRESS && !m_startRetryTimer)
            {
                m_startRetries = 0;
                if (!StartAvatarEvent())
                    m_startRetryTimer = HAKKAR_RETRY_DELAY;
            }
            else if (data == FAIL && m_auiEncounter[type] == IN_PROGRESS)
                EndAvatarEvent(FAIL);
            break;
    }
}

void instance_sunken_temple::SaveProgress()
{
    std::ostringstream out;
    for (uint32 state : m_auiEncounter)
        out << state << ' ';
    out << "ST2 " << uint64(m_idolExpires) << ' ' << m_dragonState[0] << ' ' << m_dragonState[1]
        << ' ' << uint64(m_dragonExpires[0]) << ' ' << uint64(m_dragonExpires[1]);
    m_strInstData = out.str();
    SaveToDB();
    m_saveTimer = 0;
}

bool instance_sunken_temple::SpawnAtalarion()
{
    if (GetData(TYPE_ATALARION) != SPECIAL)
        return true;
    if (Creature* atalarion = GetSingleCreatureFromStorage(NPC_ATALARION, true))
        if (atalarion->IsAlive())
            return true;
    Player* player = GetPlayerInMap();
    if (!player)
        return false;
    SummonLocations const& pos = aSunkenTempleLocation[0];
    if (!player->SummonCreature(NPC_ATALARION, pos.m_fX, pos.m_fY, pos.m_fZ, pos.m_fO, TEMPSPAWN_DEAD_DESPAWN, 0))
        return false;
    if (!m_idolExpires)
    {
        m_idolExpires = time(nullptr) + 30 * MINUTE;
        SaveProgress();
    }
    return true;
}

void instance_sunken_temple::RestoreIdol()
{
    time_t const now = time(nullptr);
    if (!m_idolExpires || m_idolExpires <= now)
        return;
    uint32 const remaining = uint32(m_idolExpires - now);
    if (GameObject* idol = GetSingleGameObjectFromStorage(GO_IDOL_OF_HAKKAR, true))
    {
        bool const refresh = !idol->IsSpawned();
        idol->SetRespawnTime(remaining);
        if (refresh)
            idol->Refresh();
    }
    for (ObjectGuid guid : m_luiBigLightGUIDs)
        DoRespawnGameObject(guid, remaining);
}

bool instance_sunken_temple::SpawnDragons()
{
    if (GetData(TYPE_JAMMALAN) != DONE)
        return true;
    bool complete = true;
    time_t const now = time(nullptr);
    for (uint32 i = 0; i < 2; ++i)
    {
        if (m_dragonState[i] != 0)
            continue;
        if (Creature* dragon = instance->GetCreature(m_dragonGuid[i]))
            if (dragon->IsAlive())
                continue;
        if (m_dragonExpires[i] && m_dragonExpires[i] <= now)
        {
            m_dragonState[i] = 3;
            SaveProgress();
            continue;
        }
        Player* player = GetPlayerInMap();
        if (!player)
        {
            complete = false;
            continue;
        }
        SummonLocations const& pos = dragonLocations[i];
        uint32 const lifetime = m_dragonExpires[i] ? uint32(m_dragonExpires[i] - now) * IN_MILLISECONDS : DRAGON_LIFETIME;
        if (Creature* dragon = player->SummonCreature(dragonEntries[i], pos.m_fX, pos.m_fY, pos.m_fZ, pos.m_fO, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, lifetime))
        {
            m_dragonGuid[i] = dragon->GetObjectGuid();
            if (!m_dragonExpires[i])
            {
                m_dragonExpires[i] = now + DAY;
                SaveProgress();
            }
        }
        else
            complete = false;
    }
    return complete;
}

void instance_sunken_temple::RestoreProgress()
{
    // Map's persistent state and objects are fully initialized only after Load/Create.
    UpdateDoors();
    bool const atalarionReady = SpawnAtalarion();
    bool const dragonsReady = SpawnDragons();
    RestoreIdol();
    if (GetData(TYPE_AVATAR) == IN_PROGRESS && !m_avatarSummoned)
        for (ObjectGuid guid : m_evilCircleGuidList)
            DoRespawnGameObject(guid, 30 * MINUTE);
    if ((!atalarionReady || !dragonsReady) && ++m_restoreRetries < MAX_SUMMON_RETRIES)
        m_restoreTimer = HAKKAR_RETRY_DELAY;
}

bool instance_sunken_temple::StartAvatarEvent()
{
    if (GetData(TYPE_AVATAR) == DONE || GetData(TYPE_AVATAR) == IN_PROGRESS)
        return false;
    Player* player = GetPlayerInMap();
    if (!player)
        return false;
    SummonLocations const& pos = aSunkenTempleLocation[1];
    Creature* shade = player->SummonCreature(NPC_SHADE_OF_HAKKAR, pos.m_fX, pos.m_fY, pos.m_fZ, pos.m_fO, TEMPSPAWN_MANUAL_DESPAWN, 0);
    if (!shade)
        return false;
    ClearHakkarSummons();
    m_hakkarGuid = shade->GetObjectGuid();
    m_npcEntryGuidStore[NPC_SHADE_OF_HAKKAR] = m_hakkarGuid;
    shade->SetRespawnDelay(DAY);
    m_auiEncounter[TYPE_AVATAR] = IN_PROGRESS;
    m_uiFlameCounter = 0;
    m_dousedFlames.clear();
    m_suppressingGuids.clear();
    m_firstWavePending.clear();
    m_waveRequests.clear();
    m_uiAvatarSummonTimer = 3000;
    m_uiSupressorTimer = m_suppressionTimer = 0;
    m_startRetryTimer = m_transformRetryTimer = 0;
    m_waveRetries = m_suppressorRetries = m_transformRetries = 0;
    m_bIsFirstHakkarWave = true;
    m_bCanSummonBloodkeeper = false;
    m_firstWaveInitialized = m_firstBloodkeeperSummoned = false;
    m_avatarSummoned = false;
    UpdateFlames();
    UpdateDoors();
    for (ObjectGuid guid : m_evilCircleGuidList)
        DoRespawnGameObject(guid, 30 * MINUTE);
    return true;
}

void instance_sunken_temple::ClearHakkarSummons()
{
    GuidSet summons;
    summons.swap(m_hakkarSummons);
    ObjectGuid const shadeGuid = m_hakkarGuid;
    m_hakkarGuid.Clear();
    // Detach all tracking before despawn callbacks; never remove a real loot corpse.
    for (ObjectGuid guid : summons)
        if (Creature* creature = instance->GetCreature(guid))
            if (creature->IsAlive())
                creature->ForcedDespawn();
    if (Creature* shade = instance->GetCreature(shadeGuid))
        if (shade->IsAlive())
            shade->ForcedDespawn();
}

void instance_sunken_temple::HideCircles()
{
    for (ObjectGuid guid : m_evilCircleGuidList)
        if (GameObject* circle = instance->GetGameObject(guid))
        {
            circle->SetForcedDespawn();
            circle->SetLootState(GO_JUST_DEACTIVATED);
        }
}

void instance_sunken_temple::EndAvatarEvent(uint32 state)
{
    if (GetData(TYPE_AVATAR) != IN_PROGRESS || (state != FAIL && state != DONE))
        return;
    m_auiEncounter[TYPE_AVATAR] = state;
    m_uiAvatarSummonTimer = m_uiSupressorTimer = m_suppressionTimer = 0;
    m_startRetryTimer = m_transformRetryTimer = 0;
    m_bCanSummonBloodkeeper = false;
    m_suppressingGuids.clear();
    m_firstWavePending.clear();
    m_waveRequests.clear();
    ClearHakkarSummons();
    if (state == FAIL)
    {
        m_uiFlameCounter = 0;
        m_dousedFlames.clear();
    }
    UpdateFlames();
    HideCircles();
    UpdateDoors();
    if (state == DONE)
        SaveProgress();
}

void instance_sunken_temple::UpdateFlames()
{
    bool const ritual = GetData(TYPE_AVATAR) == IN_PROGRESS && !m_avatarSummoned;
    for (ObjectGuid guid : m_luiFlameGUIDs)
        DoToggleGameObjectFlags(guid, GO_FLAG_NO_INTERACT, !ritual || m_dousedFlames.count(guid) != 0);
}

bool instance_sunken_temple::DouseFlame(GameObject* flame)
{
    if (!flame || GetData(TYPE_AVATAR) != IN_PROGRESS || m_avatarSummoned || m_uiFlameCounter >= MAX_FLAMES ||
        m_luiFlameGUIDs.count(flame->GetObjectGuid()) == 0 || m_dousedFlames.count(flame->GetObjectGuid()) != 0)
        return false;
    Creature* shade = instance->GetCreature(m_hakkarGuid);
    if (!shade || !shade->IsAlive() || shade->GetEntry() != NPC_SHADE_OF_HAKKAR)
        return false;
    m_dousedFlames.insert(flame->GetObjectGuid());
    ++m_uiFlameCounter;
    flame->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    int32 const texts[MAX_FLAMES] = { SAY_AVATAR_BRAZIER_1, SAY_AVATAR_BRAZIER_2, SAY_AVATAR_BRAZIER_3, SAY_AVATAR_BRAZIER_4 };
    DoScriptText(texts[m_uiFlameCounter - 1], shade);
    if (m_uiFlameCounter == MAX_FLAMES)
    {
        shade->CastSpell(shade, SPELL_SUMMON_AVATAR, TRIGGERED_OLD_TRIGGERED);
        if (!m_avatarSummoned)
            m_transformRetryTimer = HAKKAR_RETRY_DELAY;
    }
    else
    {
        m_suppressorRetries = 0;
        m_uiSupressorTimer = urand(15000, 45000);
    }
    return true;
}

void instance_sunken_temple::SuppressionChanged(Creature* shade, ObjectGuid casterGuid, bool apply, bool replacement)
{
    if (!shade || shade->GetObjectGuid() != m_hakkarGuid || shade->GetEntry() != NPC_SHADE_OF_HAKKAR ||
        GetData(TYPE_AVATAR) != IN_PROGRESS || m_avatarSummoned)
        return;
    if (apply)
    {
        Creature* suppressor = instance->GetCreature(casterGuid);
        if (!suppressor || suppressor->GetEntry() != NPC_SUPPRESSOR || !IsCurrentHakkarMember(suppressor))
            return;
        if (!m_suppressionTimer)
            m_suppressionTimer = 20 * IN_MILLISECONDS;
        m_suppressingGuids.insert(casterGuid);
    }
    else
    {
        m_suppressingGuids.erase(casterGuid);
        if (m_suppressingGuids.empty() && !replacement)
            m_suppressionTimer = 0;
    }
}

bool instance_sunken_temple::SummonAvatar(Creature* shade)
{
    if (!shade || shade->GetObjectGuid() != m_hakkarGuid || GetData(TYPE_AVATAR) != IN_PROGRESS ||
        m_avatarSummoned || shade->GetEntry() != NPC_SHADE_OF_HAKKAR || m_uiFlameCounter != MAX_FLAMES)
        return false;
    if (!shade->UpdateEntry(NPC_AVATAR_OF_HAKKAR))
        return false;
    m_avatarSummoned = true;
    m_uiAvatarSummonTimer = m_uiSupressorTimer = m_suppressionTimer = 0;
    m_transformRetryTimer = 0;
    m_suppressingGuids.clear();
    shade->RemoveAurasDueToSpell(SPELL_SUPPRESSION);
    shade->AIM_Initialize();
    UpdateFlames();
    HideCircles();
    DoScriptText(SAY_AVATAR_SPAWN, shade);
    return true;
}

bool instance_sunken_temple::HasMalfurion() const
{
    Creature* malfurion = instance->GetCreature(m_malfurionGuid);
    return malfurion && malfurion->IsAlive();
}

GameObject* instance_sunken_temple::GetRandomCircle() const
{
    std::vector<GameObject*> circles;
    for (ObjectGuid guid : m_evilCircleGuidList)
        if (GameObject* circle = instance->GetGameObject(guid))
            circles.push_back(circle);
    return circles.empty() ? nullptr : circles[urand(0, uint32(circles.size() - 1))];
}

Creature* instance_sunken_temple::SummonHakkarMob(uint32 entry, GameObject* circle)
{
    Creature* shade = instance->GetCreature(m_hakkarGuid);
    if (!shade || !circle || GetData(TYPE_AVATAR) != IN_PROGRESS || m_avatarSummoned)
        return nullptr;
    Creature* summoned = shade->SummonCreature(entry, circle->GetPositionX(), circle->GetPositionY(), circle->GetPositionZ(), 0, TEMPSPAWN_DEAD_DESPAWN, 0);
    if (summoned)
        m_hakkarSummons.insert(summoned->GetObjectGuid());
    return summoned;
}

bool instance_sunken_temple::RetryHakkarSummon(uint32& timer, uint8& retries)
{
    if (++retries >= MAX_SUMMON_RETRIES)
    {
        EndAvatarEvent(FAIL);
        return false;
    }
    timer = HAKKAR_RETRY_DELAY;
    return true;
}

bool instance_sunken_temple::ProcessStatueEvent(uint32 eventId)
{
    if (GetData(TYPE_ATALARION) != NOT_STARTED || m_uiStatueCounter >= MAX_STATUES || eventId != m_aAtalaiStatueEvents[m_uiStatueCounter])
        return false;
    if (++m_uiStatueCounter == MAX_STATUES)
        SetData(TYPE_ATALARION, SPECIAL);
    return true;
}

void instance_sunken_temple::Load(const char* data)
{
    if (!data)
        return;
    uint32 states[MAX_ENCOUNTER] = {};
    std::istringstream in(data);
    for (uint32& state : states)
        if (!(in >> state))
            return;
    std::string version;
    uint64 idol = 0, dragon0 = 0, dragon1 = 0;
    uint32 dragonState0 = 0, dragonState1 = 0;
    bool const extended = (in >> version) && version == "ST2" &&
        (in >> idol >> dragonState0 >> dragonState1 >> dragon0 >> dragon1);
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i] == DONE ? DONE : (extended && i == TYPE_ATALARION && states[i] == SPECIAL ? SPECIAL : NOT_STARTED);
    if (extended)
    {
        m_idolExpires = time_t(idol);
        m_dragonState[0] = dragonState0 <= 3 ? dragonState0 : 2;
        m_dragonState[1] = dragonState1 <= 3 ? dragonState1 : 2;
        m_dragonExpires[0] = time_t(dragon0);
        m_dragonExpires[1] = time_t(dragon1);
    }
    else if (m_auiEncounter[TYPE_JAMMALAN] == DONE)
    {
        // A five-field save cannot distinguish killed dragons from lost live summons.
        // Do not mint another pair of loot-bearing bosses for such a legacy save.
        m_dragonState[0] = m_dragonState[1] = 2;
    }
    if (m_auiEncounter[TYPE_ATALARION] != NOT_STARTED)
        m_uiStatueCounter = MAX_STATUES;
    m_strInstData = data;
    m_restoreTimer = 1000;
    m_saveTimer = 1000;
    // Do not touch MapPersistentState here: Map constructs it after loading scripts.
}

uint32 instance_sunken_temple::GetData(uint32 type) const
{
    return type < MAX_ENCOUNTER ? m_auiEncounter[type] : 0;
}

void instance_sunken_temple::Update(uint32 diff)
{
    if (m_restoreTimer)
    {
        if (m_restoreTimer <= diff)
        {
            m_restoreTimer = 0;
            RestoreProgress();
        }
        else
            m_restoreTimer -= diff;
    }
    for (uint32 i = 0; i < 2; ++i)
        if (m_dragonState[i] == 0)
            if (Creature* dragon = instance->GetCreature(m_dragonGuid[i]))
                if (dragon->IsAlive() && dragon->IsInCombat())
                {
                    m_dragonExpires[i] = time(nullptr) + DAY;
                    if (!m_saveTimer)
                        m_saveTimer = 60000;
                }
    if (m_saveTimer)
    {
        if (m_saveTimer <= diff)
            SaveProgress();
        else
            m_saveTimer -= diff;
    }
    if (m_startRetryTimer)
    {
        if (m_startRetryTimer <= diff)
        {
            m_startRetryTimer = 0;
            if (!StartAvatarEvent() && ++m_startRetries < MAX_SUMMON_RETRIES)
                m_startRetryTimer = HAKKAR_RETRY_DELAY;
        }
        else
            m_startRetryTimer -= diff;
    }
    if (GetData(TYPE_AVATAR) != IN_PROGRESS)
        return;
    Creature* shade = instance->GetCreature(m_hakkarGuid);
    if (!shade || !shade->IsAlive())
    {
        EndAvatarEvent(FAIL);
        return;
    }
    if (m_avatarSummoned)
        return;
    if (m_transformRetryTimer)
    {
        if (m_transformRetryTimer <= diff)
        {
            m_transformRetryTimer = 0;
            shade->CastSpell(shade, SPELL_SUMMON_AVATAR, TRIGGERED_OLD_TRIGGERED);
            if (!m_avatarSummoned)
                RetryHakkarSummon(m_transformRetryTimer, m_transformRetries);
        }
        else
            m_transformRetryTimer -= diff;
        if (m_avatarSummoned || GetData(TYPE_AVATAR) != IN_PROGRESS)
            return;
    }
    if (m_suppressionTimer)
    {
        if (m_suppressionTimer <= diff)
        {
            m_suppressionTimer = 0;
            if (shade->HasAura(SPELL_SUPPRESSION))
            {
                DoScriptText(SAY_AVATAR_DESPAWN, shade);
                EndAvatarEvent(FAIL);
                return;
            }
        }
        else
            m_suppressionTimer -= diff;
    }
    if (m_uiAvatarSummonTimer)
    {
        if (m_uiAvatarSummonTimer <= diff)
        {
            m_uiAvatarSummonTimer = 0;
            if (m_bIsFirstHakkarWave)
            {
                if (!m_firstWaveInitialized)
                {
                    if (m_evilCircleGuidList.empty())
                    {
                        RetryHakkarSummon(m_uiAvatarSummonTimer, m_waveRetries);
                        return;
                    }
                    m_firstWavePending = m_evilCircleGuidList;
                    m_firstWaveInitialized = true;
                }
                for (auto it = m_firstWavePending.begin(); it != m_firstWavePending.end();)
                {
                    GameObject* circle = instance->GetGameObject(*it);
                    if (SummonHakkarMob(NPC_HAKKARI_MINION, circle))
                        it = m_firstWavePending.erase(it);
                    else
                        ++it;
                }
                if (!m_firstBloodkeeperSummoned)
                    m_firstBloodkeeperSummoned = SummonHakkarMob(NPC_BLOODKEEPER, GetRandomCircle()) != nullptr;
                if (!m_firstWavePending.empty() || !m_firstBloodkeeperSummoned)
                {
                    RetryHakkarSummon(m_uiAvatarSummonTimer, m_waveRetries);
                    return;
                }
                m_bCanSummonBloodkeeper = false;
                m_bIsFirstHakkarWave = false;
                m_uiAvatarSummonTimer = 50000;
            }
            else
            {
                // Freeze this wave's roll and circle choices until all requests succeed.
                // Retrying only unresolved requests cannot duplicate its successful mobs.
                if (m_waveRequests.empty())
                {
                    uint32 const roll = urand(0, 99);
                    uint8 count = roll < 75 ? 1 : roll < 95 ? 2 : 3;
                    if (m_bCanSummonBloodkeeper && roll_chance_i(30))
                    {
                        --count;
                        GameObject* circle = GetRandomCircle();
                        m_waveRequests.emplace_back(NPC_BLOODKEEPER, circle ? circle->GetObjectGuid() : ObjectGuid());
                    }
                    for (uint8 i = 0; i < count; ++i)
                    {
                        GameObject* circle = GetRandomCircle();
                        m_waveRequests.emplace_back(NPC_HAKKARI_MINION, circle ? circle->GetObjectGuid() : ObjectGuid());
                    }
                    m_waveRetries = 0;
                }
                for (auto it = m_waveRequests.begin(); it != m_waveRequests.end();)
                {
                    // A missing circle can become available after its grid loads.
                    if (!it->second)
                        if (GameObject* circle = GetRandomCircle())
                            it->second = circle->GetObjectGuid();
                    if (SummonHakkarMob(it->first, instance->GetGameObject(it->second)))
                    {
                        if (it->first == NPC_BLOODKEEPER)
                            m_bCanSummonBloodkeeper = false;
                        it = m_waveRequests.erase(it);
                    }
                    else
                        ++it;
                }
                if (!m_waveRequests.empty())
                {
                    RetryHakkarSummon(m_uiAvatarSummonTimer, m_waveRetries);
                    return;
                }
                m_uiAvatarSummonTimer = urand(3000, 15000);
            }
        }
        else
            m_uiAvatarSummonTimer -= diff;
    }
    if (m_uiSupressorTimer)
    {
        if (m_uiSupressorTimer <= diff)
        {
            m_uiSupressorTimer = 0;
            uint8 const door = urand(0, 1);
            SummonLocations const& pos = aHakkariDoorLocations[door];
            if (Creature* suppressor = shade->SummonCreature(NPC_SUPPRESSOR, pos.m_fX, pos.m_fY, pos.m_fZ, 0, TEMPSPAWN_DEAD_DESPAWN, 0))
            {
                m_hakkarSummons.insert(suppressor->GetObjectGuid());
                suppressor->GetMotionMaster()->MoveWaypoint(door);
                int32 const texts[4] = { SAY_SUPPRESSOR_1, SAY_SUPPRESSOR_2, SAY_SUPPRESSOR_3, SAY_SUPPRESSOR_4 };
                DoScriptText(texts[urand(0, 3)], suppressor);
                m_suppressorRetries = 0;
            }
            else
                RetryHakkarSummon(m_uiSupressorTimer, m_suppressorRetries);
        }
        else
            m_uiSupressorTimer -= diff;
    }
}

InstanceData* GetInstanceData_instance_sunken_temple(Map* map)
{
    return new instance_sunken_temple(map);
}

void AddSC_instance_sunken_temple()
{
    Script* script = new Script;
    script->Name = "instance_sunken_temple";
    script->GetInstanceData = &GetInstanceData_instance_sunken_temple;
    script->RegisterSelf();
}
