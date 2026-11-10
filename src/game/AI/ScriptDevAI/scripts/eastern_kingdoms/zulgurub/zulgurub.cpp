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
SDName: Instance_ZulGurub
SD%Complete: 80
SDComment: Missing reset function after killing a boss for Ohgan, Thekal.
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "Database/DatabaseEnv.h"
#include "GameEvents/GameEventMgr.h"
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnManager.h"

namespace
{
constexpr uint32 ArlokkSummonDelay = 10000;
constexpr uint32 ArlokkLifetime = 15 * MINUTE * IN_MILLISECONDS;
constexpr size_t ArlokkProwlerLimit = 40;
constexpr uint32 SummonZulianProwlers = 24247;
constexpr uint32 GahzrankaSummonDelay = 2 * IN_MILLISECONDS;
constexpr uint32 GahzrankaLifetime = 150 * MINUTE * IN_MILLISECONDS;
constexpr float GahzrankaSummonRange = 100.0f;
constexpr float GahzrankaSpawnX = -11675.7f;
constexpr float GahzrankaSpawnY = -1728.91f;
constexpr float GahzrankaSpawnZ = -7.08374f;
constexpr float GahzrankaSpawnOrientation = 4.20624f;

constexpr uint32 EdgeOfMadnessSummonDelay = 5 * IN_MILLISECONDS;
constexpr float EdgeOfMadnessSpawnX = -11901.3f;
constexpr float EdgeOfMadnessSpawnY = -1906.52f;
constexpr float EdgeOfMadnessSpawnZ = 65.36f;
constexpr float EdgeOfMadnessSpawnOrientation = 0.87f;
constexpr float EdgeOfMadnessRestoreRange = 60.0f;

struct EdgeOfMadnessRotation
{
    uint16 eventId;
    uint32 bossEntry;
    uint32 tabletEntry;
    uint32 inactiveText;
    uint32 activeText;
};

constexpr EdgeOfMadnessRotation EdgeOfMadnessBosses[] =
{
    {29, NPC_GRILEK, 180358, 7643, 7669},
    {30, NPC_HAZZARAH, 180364, 7670, 7671},
    {31, NPC_RENATAKI, 180365, 7672, 7673},
    {32, NPC_WUSHOOLAY, 180393, 7674, 7675},
};

std::string MakeSaveData(uint32 const (&states)[MAX_ENCOUNTER], uint32 gahzrankaState,
    EdgeOfMadnessProgress const& madness)
{
    std::ostringstream stream;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i)
            stream << ' ';
        stream << states[i];
    }
    if (gahzrankaState != NOT_STARTED || madness.state != NOT_STARTED)
        stream << ' ' << gahzrankaState;
    if (madness.state != NOT_STARTED)
        stream << ' ' << madness.state << ' ' << madness.bossEntry << ' ' << madness.summoner.GetCounter();
    return stream.str();
}

bool ReadOptionalEncounters(std::istream& stream, uint32& gahzrankaState, EdgeOfMadnessProgress& madness)
{
    // Older saves end after the eight encounter states.
    stream >> std::ws;
    if (stream.eof())
    {
        gahzrankaState = NOT_STARTED;
        return true;
    }

    if (!(stream >> gahzrankaState) ||
        (gahzrankaState != NOT_STARTED && gahzrankaState != IN_PROGRESS && gahzrankaState != DONE))
        return false;

    stream >> std::ws;
    if (stream.eof())
        return true;                                        // Gahz'ranka-only saves end after the ninth state.

    uint32 summonerCounter;
    if (!(stream >> madness.state >> madness.bossEntry >> summonerCounter) ||
        (madness.state != SPECIAL && madness.state != IN_PROGRESS && madness.state != DONE) ||
        (madness.state == SPECIAL) != (summonerCounter != 0))
        return false;

    bool knownBoss = false;
    for (EdgeOfMadnessRotation const& boss : EdgeOfMadnessBosses)
        if (boss.bossEntry == madness.bossEntry)
            knownBoss = true;
    if (!knownBoss)
        return false;

    if (summonerCounter)
        madness.summoner = ObjectGuid(HIGHGUID_PLAYER, summonerCounter);
    stream >> std::ws;
    return stream.eof();
}
}

instance_zulgurub::instance_zulgurub(Map* map) : ScriptedInstance(map),
    m_bHasIntroYelled(false),
    m_bHasAltarYelled(false),
    m_resetMarliObjects(false),
    m_resetArlokkObjects(false)
{
    Initialize();
}

instance_zulgurub::~instance_zulgurub()
{
    if (!m_gahzrankaSummoner.IsEmpty())
        RefundGahzrankaSummon(m_gahzrankaSummoner, instance);
}

void instance_zulgurub::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
}

void instance_zulgurub::StartArlokkEncounter(GameObject& gong)
{
    uint32 const state = GetData(TYPE_ARLOKK);
    if (state == IN_PROGRESS || state == DONE)
        return;

    m_arlokkSummonTimer = ArlokkSummonDelay;
    SetData(TYPE_ARLOKK, IN_PROGRESS);
    SyncArlokkObject(gong);
}

void instance_zulgurub::Update(uint32 diff)
{
    UpdateGahzranka(diff);
    UpdateEdgeOfMadness(diff);

    if (GetData(TYPE_ARLOKK) != IN_PROGRESS)
        return;

    if (!m_arlokkSummonTimer)
    {
        Creature* arlokk = instance->GetCreature(m_arlokkBossGuid);
        if (!arlokk || !arlokk->IsAlive())
            SetData(TYPE_ARLOKK, FAIL);
        return;
    }

    if (diff < m_arlokkSummonTimer)
    {
        m_arlokkSummonTimer -= diff;
        return;
    }

    m_arlokkSummonTimer = 0;
    GameObject* gong = GetSingleGameObjectFromStorage(GO_GONG_OF_BETHEKK, true);
    Creature* arlokk = gong ? gong->SummonCreature(NPC_ARLOKK, -11540.7f, -1627.71f, 41.27f, 0.1f,
        TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, ArlokkLifetime) : nullptr;
    if (!arlokk)
    {
        SetData(TYPE_ARLOKK, FAIL);
        return;
    }

    m_arlokkBossGuid = arlokk->GetObjectGuid();
    if (GameObject* field = GetSingleGameObjectFromStorage(GO_FORCEFIELD, true))
        SyncArlokkObject(*field);
}

bool instance_zulgurub::CanSummonGahzranka(Player const& player) const
{
    if (m_gahzrankaState != NOT_STARTED || !m_gahzrankaSummoner.IsEmpty() || !player.IsAlive() || player.GetMap() != instance)
        return false;

    Creature const* trigger = GetSingleCreatureFromStorage(NPC_GAHZRANKA_TRIGGER, true);
    return trigger && trigger->IsAlive() && player.IsWithinDistInMap(trigger, GahzrankaSummonRange) &&
        !GetSingleCreatureFromStorage(NPC_GAHZRANKA, true);
}

bool instance_zulgurub::StartGahzrankaSummon(Player& player, uint32 travelTime)
{
    if (!CanSummonGahzranka(player))
        return false;

    m_gahzrankaSummoner = player.GetObjectGuid();
    m_gahzrankaSummonTimer = travelTime + GahzrankaSummonDelay;
    return true;
}

void instance_zulgurub::UpdateGahzranka(uint32 diff)
{
    bool const pending = !m_gahzrankaSummoner.IsEmpty();
    if (!pending && (m_gahzrankaState != IN_PROGRESS || GetSingleCreatureFromStorage(NPC_GAHZRANKA, true)))
        return;

    if (diff < m_gahzrankaSummonTimer)
    {
        m_gahzrankaSummonTimer -= diff;
        return;
    }

    if (!pending)
    {
        // Restore a paid encounter when someone returns to the area after an unload or timeout.
        m_gahzrankaSummonTimer = GahzrankaSummonDelay;
        for (auto const& reference : instance->GetPlayers())
        {
            Player const* player = reference.getSource();
            if (player && player->IsAlive() && player->IsWithinDist3d(GahzrankaSpawnX, GahzrankaSpawnY, GahzrankaSpawnZ, GahzrankaSummonRange))
            {
                SummonGahzranka();
                break;
            }
        }
        return;
    }

    Creature* trigger = GetSingleCreatureFromStorage(NPC_GAHZRANKA_TRIGGER, true);
    bool const summoned = trigger && trigger->IsAlive() && !GetSingleCreatureFromStorage(NPC_GAHZRANKA, true) && SummonGahzranka();
    ObjectGuid const summoner = m_gahzrankaSummoner;
    m_gahzrankaSummoner.Clear();
    m_gahzrankaSummonTimer = 0;

    if (!summoned)
    {
        RefundGahzrankaSummon(summoner, instance);
        return;
    }

    m_gahzrankaState = IN_PROGRESS;
    SaveEncounterProgress();
    trigger->ForcedDespawn();
}

Creature* instance_zulgurub::SummonGahzranka()
{
    return WorldObject::SummonCreature(TempSpawnSettings(nullptr, NPC_GAHZRANKA, GahzrankaSpawnX, GahzrankaSpawnY, GahzrankaSpawnZ,
        GahzrankaSpawnOrientation, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, GahzrankaLifetime, false, false, 1), instance);
}

uint32 instance_zulgurub::GetEdgeOfMadnessBossEntry() const
{
    if (m_edgeOfMadness.state != NOT_STARTED)
        return m_edgeOfMadness.bossEntry;

    uint32 selected = 0;
    for (EdgeOfMadnessRotation const& boss : EdgeOfMadnessBosses)
    {
        if (!sGameEventMgr.IsActiveEvent(boss.eventId))
            continue;
        if (selected)
            return 0;                                       // Ambiguous schedules cannot consume a flask.
        selected = boss.bossEntry;
    }
    return selected;
}

uint32 instance_zulgurub::GetEdgeOfMadnessTabletText(uint32 tabletEntry) const
{
    uint32 const selected = GetEdgeOfMadnessBossEntry();
    for (EdgeOfMadnessRotation const& boss : EdgeOfMadnessBosses)
        if (boss.tabletEntry == tabletEntry)
            return boss.bossEntry == selected ? boss.activeText : boss.inactiveText;
    return 0;
}

uint32 instance_zulgurub::GetAvailableEdgeOfMadnessBoss(Player const& player, GameObject const& brazier) const
{
    if (m_edgeOfMadness.state != NOT_STARTED || !player.IsAlive() || player.GetMap() != instance ||
        brazier.GetEntry() != GO_BRAZIER_OF_MADNESS || !brazier.IsSpawned() ||
        &brazier != GetSingleGameObjectFromStorage(GO_BRAZIER_OF_MADNESS, true))
        return 0;

    for (EdgeOfMadnessRotation const& boss : EdgeOfMadnessBosses)
        if (Creature* creature = GetSingleCreatureFromStorage(boss.bossEntry, true))
            if (creature->IsAlive())
                return 0;

    return GetEdgeOfMadnessBossEntry();
}

bool instance_zulgurub::CanSummonEdgeOfMadness(Player const& player, GameObject const& brazier) const
{
    return GetAvailableEdgeOfMadnessBoss(player, brazier) != 0;
}

bool instance_zulgurub::StartEdgeOfMadnessSummon(Player& player, GameObject& brazier)
{
    uint32 const boss = GetAvailableEdgeOfMadnessBoss(player, brazier);
    if (!boss)
        return false;

    m_edgeOfMadness = {SPECIAL, boss, player.GetObjectGuid()};
    m_edgeOfMadnessSummonTimer = EdgeOfMadnessSummonDelay;
    RestoreEdgeOfMadnessBrazier(brazier);

    // Persist the consumed bottle and its paid reservation together.
    CharacterDatabase.BeginTransaction();
    player.SaveInventoryAndGoldToDB();
    SaveEncounterProgress();
    CharacterDatabase.CommitTransaction();
    return true;
}

void instance_zulgurub::RestoreEdgeOfMadnessBrazier(GameObject& brazier) const
{
    if (brazier.GetEntry() != GO_BRAZIER_OF_MADNESS)
        return;

    brazier.SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_LOCKED);
    if (m_edgeOfMadness.state == NOT_STARTED)
    {
        brazier.RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE | GO_FLAG_NO_INTERACT);
        brazier.SetLootState(GO_READY);
        brazier.SetGoState(GO_STATE_READY);
    }
    else
    {
        brazier.SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE | GO_FLAG_NO_INTERACT);
        brazier.SetLootState(GO_ACTIVATED);
        brazier.SetGoState(GO_STATE_ACTIVE);
    }
}

void instance_zulgurub::UpdateEdgeOfMadness(uint32 diff)
{
    bool const pending = m_edgeOfMadness.state == SPECIAL;
    if (!pending && (m_edgeOfMadness.state != IN_PROGRESS ||
        (!m_edgeOfMadnessBossGuid.IsEmpty() && instance->GetCreature(m_edgeOfMadnessBossGuid))))
        return;

    if (diff < m_edgeOfMadnessSummonTimer)
    {
        m_edgeOfMadnessSummonTimer -= diff;
        return;
    }
    m_edgeOfMadnessSummonTimer = EdgeOfMadnessSummonDelay;

    if (!pending)
    {
        // A paid encounter survives grid unloads and restarts. Restore it when the room is visited.
        bool someoneNearby = false;
        for (auto const& reference : instance->GetPlayers())
        {
            Player const* player = reference.getSource();
            if (player && player->IsAlive() &&
                player->IsWithinDist3d(EdgeOfMadnessSpawnX, EdgeOfMadnessSpawnY, EdgeOfMadnessSpawnZ, EdgeOfMadnessRestoreRange))
            {
                someoneNearby = true;
                break;
            }
        }
        if (!someoneNearby)
            return;
    }

    if (Creature* boss = WorldObject::SummonCreature(TempSpawnSettings(nullptr, m_edgeOfMadness.bossEntry,
        EdgeOfMadnessSpawnX, EdgeOfMadnessSpawnY, EdgeOfMadnessSpawnZ, EdgeOfMadnessSpawnOrientation,
        TEMPSPAWN_DEAD_DESPAWN, 0), instance))
    {
        m_edgeOfMadnessBossGuid = boss->GetObjectGuid();
        if (pending)
        {
            m_edgeOfMadness.state = IN_PROGRESS;
            m_edgeOfMadness.summoner.Clear();
            SaveEncounterProgress();
        }
        return;
    }

    if (pending)
    {
        ObjectGuid const summoner = m_edgeOfMadness.summoner;
        m_edgeOfMadness = {};
        if (GameObject* brazier = GetSingleGameObjectFromStorage(GO_BRAZIER_OF_MADNESS, true))
            RestoreEdgeOfMadnessBrazier(*brazier);
        SaveEncounterProgress();
        RefundEdgeOfMadnessSummon(summoner, instance);
    }
}

void instance_zulgurub::SaveEncounterProgress()
{
    m_strInstData = MakeSaveData(m_auiEncounter, m_gahzrankaState, m_edgeOfMadness);
    SaveToDB();
}

void instance_zulgurub::SyncArlokkObject(GameObject& go)
{
    uint32 const state = GetData(TYPE_ARLOKK);
    if (go.GetEntry() == GO_GONG_OF_BETHEKK)
    {
        if (state == IN_PROGRESS || state == DONE)
            go.SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        else
            go.RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    }
    else if (go.GetEntry() == GO_FORCEFIELD)
        go.SetGoState(state == IN_PROGRESS && !m_arlokkBossGuid.IsEmpty() ? GO_STATE_READY : GO_STATE_ACTIVE);
}

void instance_zulgurub::DoYellAtTriggerIfCan(uint32 triggerId)
{
    if (triggerId == AREATRIGGER_ENTER && !m_bHasIntroYelled)
    {
        DoOrSimulateScriptTextForThisInstance(SAY_HAKKAR_PROTECT, NPC_HAKKAR);
        m_bHasIntroYelled = true;
    }
    else if (triggerId == AREATRIGGER_ALTAR && !m_bHasAltarYelled)
    {
        DoOrSimulateScriptTextForThisInstance(SAY_MINION_DESTROY, NPC_HAKKAR);
        m_bHasAltarYelled = true;
    }
}

void instance_zulgurub::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_LORKHAN:
        case NPC_ZATH:
        case NPC_THEKAL:
        case NPC_JINDO:
        case NPC_HAKKAR:
        case NPC_BLOODLORD_MANDOKIR:
        case NPC_MARLI:
        case NPC_JEKLIK:
        case NPC_VENOXIS:
        case NPC_ARLOKK:
        case NPC_GAHZRANKA:
        case NPC_GAHZRANKA_TRIGGER:
        case NPC_GRILEK:
        case NPC_HAZZARAH:
        case NPC_RENATAKI:
        case NPC_WUSHOOLAY:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
        case NPC_PANTHER_TRIGGER:
            if (creature->GetPositionY() < -1626)
                m_lLeftPantherTriggerGUIDList.push_back(creature->GetObjectGuid());
            else
                m_lRightPantherTriggerGUIDList.push_back(creature->GetObjectGuid());
            break;
    }
}

void instance_zulgurub::OnCreatureDeath(Creature* creature)
{
    if (creature->GetEntry() == NPC_ZULIAN_PROWLER)
        m_lProwlerGUIDList.remove(creature->GetObjectGuid());
    else if (creature->GetEntry() == NPC_GAHZRANKA && m_gahzrankaState == IN_PROGRESS &&
        GetSingleCreatureFromStorage(NPC_GAHZRANKA, true) == creature)
    {
        m_gahzrankaState = DONE;
        SaveEncounterProgress();
    }
    else if (m_edgeOfMadness.state == IN_PROGRESS && creature->GetObjectGuid() == m_edgeOfMadnessBossGuid)
    {
        m_edgeOfMadness.state = DONE;
        m_edgeOfMadnessBossGuid.Clear();
        SaveEncounterProgress();
    }
}

void instance_zulgurub::OnCreatureDespawn(Creature* creature)
{
    if (creature->GetEntry() == NPC_ZULIAN_PROWLER)
        m_lProwlerGUIDList.remove(creature->GetObjectGuid());
    if (GetData(TYPE_ARLOKK) == IN_PROGRESS && creature->GetObjectGuid() == m_arlokkBossGuid)
        SetData(TYPE_ARLOKK, FAIL);
    if (creature->GetObjectGuid() == m_edgeOfMadnessBossGuid)
        m_edgeOfMadnessBossGuid.Clear();
}

void instance_zulgurub::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_GONG_OF_BETHEKK:
        case GO_FORCEFIELD:
            if (m_resetArlokkObjects && GetData(TYPE_ARLOKK) == NOT_STARTED)
                RestoreEventObject(go, TYPE_ARLOKK);
            SyncArlokkObject(*go);
            break;
        case GO_SPIDER_EGG:
            if (m_resetMarliObjects && GetData(TYPE_MARLI) == NOT_STARTED)
                RestoreEventObject(go, TYPE_MARLI);
            return;
        case GO_BRAZIER_OF_MADNESS:
            RestoreEdgeOfMadnessBrazier(*go);
            break;
    }

    m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
}

void instance_zulgurub::SetData(uint32 type, uint32 data)
{
    switch (type)
    {
        case TYPE_JEKLIK:
        case TYPE_VENOXIS:
        case TYPE_THEKAL:
            m_auiEncounter[type] = data;
            if (data == DONE)
                UpdateHakkarPowerStacks();
            break;
        case TYPE_MARLI:
        {
            bool const reset = data == FAIL && m_auiEncounter[type] != FAIL;
            m_auiEncounter[type] = data;
            if (data == DONE)
                UpdateHakkarPowerStacks();
            else if (reset)
            {
                RemoveEventSummons(TYPE_MARLI, NPC_SPAWN_OF_MARLI);
                RestoreEventObjects(TYPE_MARLI);
            }
            break;
        }
        case TYPE_ARLOKK:
        {
            if (m_auiEncounter[type] == data || m_auiEncounter[type] == DONE)
                return;
            m_auiEncounter[type] = data;
            if (data == DONE)
            {
                StopArlokkProwlers();
                ReleaseArlokkProwlers();
                UpdateHakkarPowerStacks();
            }
            if (data == FAIL)
                RemoveEventSummons(TYPE_ARLOKK, NPC_ZULIAN_PROWLER);
            if (data != IN_PROGRESS)
            {
                m_arlokkSummonTimer = 0;
                m_arlokkBossGuid.Clear();
            }

            if (GameObject* gong = GetSingleGameObjectFromStorage(GO_GONG_OF_BETHEKK, true))
            {
                if (data == FAIL)
                    RestoreEventObject(gong, TYPE_ARLOKK);
                SyncArlokkObject(*gong);
            }
            if (GameObject* field = GetSingleGameObjectFromStorage(GO_FORCEFIELD, true))
                SyncArlokkObject(*field);
            break;
        }
        case TYPE_OHGAN:
            // Note: SPECIAL instance data is set via ACID!
            if (data == SPECIAL)
            {
                if (Creature* pMandokir = GetSingleCreatureFromStorage(NPC_BLOODLORD_MANDOKIR))
                {
                    pMandokir->SetWalk(false);
                    pMandokir->GetMotionMaster()->MovePoint(1, aMandokirDownstairsPos[0], aMandokirDownstairsPos[1], aMandokirDownstairsPos[2]);
                }
            }
            m_auiEncounter[type] = data;
            break;
        case TYPE_LORKHAN:
        case TYPE_ZATH:
            m_auiEncounter[type] = data;
            break;
    }

    if (data == DONE)
    {
        OUT_SAVE_INST_DATA;

        SaveEncounterProgress();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

void instance_zulgurub::UpdateHakkarPowerStacks()
{
    if (Creature* hakkar = GetSingleCreatureFromStorage(NPC_HAKKAR))
    {
        if (!hakkar->IsAlive())
            return;

        hakkar->CastSpell(hakkar, SPELL_HAKKAR_POWER_DOWN, TRIGGERED_OLD_TRIGGERED);
    }
}

void instance_zulgurub::Load(const char* chrIn)
{
    if (!chrIn)
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    OUT_LOAD_INST_DATA(chrIn);

    std::istringstream loadStream(chrIn);
    uint32 states[MAX_ENCOUNTER];
    for (uint32& state : states)
    {
        if (!(loadStream >> state))
        {
            OUT_LOAD_INST_DATA_FAIL;
            return;
        }
    }
    uint32 gahzrankaState;
    EdgeOfMadnessProgress madness;
    if (!ReadOptionalEncounters(loadStream, gahzrankaState, madness))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i] == IN_PROGRESS ? NOT_STARTED : states[i];

    if (m_auiEncounter[TYPE_THEKAL] != DONE)
        m_auiEncounter[TYPE_THEKAL] = NOT_STARTED;
    m_auiEncounter[TYPE_LORKHAN] = NOT_STARTED;
    m_auiEncounter[TYPE_ZATH] = NOT_STARTED;
    m_gahzrankaState = gahzrankaState;
    m_edgeOfMadness = madness;
    m_edgeOfMadnessBossGuid.Clear();
    m_edgeOfMadnessSummonTimer = madness.state == SPECIAL ? EdgeOfMadnessSummonDelay : 0;
    m_strInstData = MakeSaveData(m_auiEncounter, m_gahzrankaState, m_edgeOfMadness);

    OUT_LOAD_INST_DATA_COMPLETE;
}

uint32 instance_zulgurub::GetData(uint32 uiType) const
{
    if (uiType < MAX_ENCOUNTER)
        return m_auiEncounter[uiType];

    return 0;
}

std::vector<InstanceEventReset> instance_zulgurub::GetResettableEvents() const
{
    return
    {
        {TYPE_JEKLIK, "High Priestess Jeklik", {NPC_JEKLIK}},
        {TYPE_VENOXIS, "High Priest Venoxis", {NPC_VENOXIS}},
        {TYPE_MARLI, "High Priestess Mar'li", {NPC_MARLI}},
        {TYPE_THEKAL, "High Priest Thekal", {NPC_THEKAL}},
        {TYPE_ARLOKK, "High Priestess Arlokk", {NPC_ARLOKK}},
        {RESET_EVENT_HAKKAR, "Hakkar", {NPC_HAKKAR}}
    };
}

bool instance_zulgurub::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId >= MAX_PRIESTS && eventId != RESET_EVENT_HAKKAR)
        return false;

    // A progress reset leaves static bosses in place. Do not detach a living
    // phase-two or fake-dead actor from the state its AI still owns.
    for (InstanceEventReset const& event : GetResettableEvents())
    {
        if (event.id != eventId)
            continue;
        for (uint32 entry : event.bossEntries)
        {
            Creature* boss = GetSingleCreatureFromStorage(entry, true);
            if (!boss || !boss->IsAlive())
                continue;
            if (boss->IsInCombat())
                return false;
            if ((eventId == TYPE_JEKLIK && boss->HasAura(23966)) ||
                (eventId == TYPE_VENOXIS && boss->HasAura(23849)) ||
                (eventId == TYPE_MARLI && boss->HasAura(24084)) ||
                (eventId == TYPE_THEKAL && (boss->HasAura(24169) || boss->HasAura(19951) ||
                    boss->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE))) ||
                (eventId == TYPE_ARLOKK && (boss->HasAura(24190) || boss->HasAura(24235))))
                return false;
        }
    }

    if (eventId == TYPE_THEKAL)
    {
        for (uint32 entry : {uint32(NPC_LORKHAN), uint32(NPC_ZATH)})
        {
            Creature* zealot = GetSingleCreatureFromStorage(entry, true);
            if (zealot && zealot->IsAlive() && (zealot->IsInCombat() || zealot->HasAura(19951) ||
                zealot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE)))
                return false;
        }
    }

    uint32 states[MAX_ENCOUNTER];
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        states[i] = m_auiEncounter[i];
    if (eventId < MAX_PRIESTS)
        states[eventId] = NOT_STARTED;
    if (eventId == TYPE_THEKAL)
        states[TYPE_LORKHAN] = states[TYPE_ZATH] = NOT_STARTED;

    data = MakeSaveData(states, m_gahzrankaState, m_edgeOfMadness);
    return true;
}

void instance_zulgurub::RestoreEventObject(GameObject* go, uint32 eventId)
{
    if (!go || !go->HasStaticDBSpawnData())
        return;
    if ((eventId == TYPE_MARLI && go->GetEntry() == GO_SPIDER_EGG) ||
        (eventId == TYPE_ARLOKK && go->GetEntry() == GO_GONG_OF_BETHEKK))
    {
        go->Respawn();
        go->ClearAllUsesData();
        go->SetUInt32Value(GAMEOBJECT_FLAGS, go->GetGOInfo()->flags);
        go->SetLootState(GO_READY);
        go->SetGoState(GO_STATE_READY);
    }
    else if (eventId == TYPE_ARLOKK && go->GetEntry() == GO_FORCEFIELD)
    {
        go->Respawn();
        go->SetUInt32Value(GAMEOBJECT_FLAGS, go->GetGOInfo()->flags);
        go->SetLootState(GO_READY);
        go->SetGoState(GO_STATE_ACTIVE);
    }
}

void instance_zulgurub::RestoreEventObjects(uint32 eventId)
{
    if (eventId != TYPE_MARLI && eventId != TYPE_ARLOKK)
        return;

    // Include unloaded DB spawns: removing only the loaded object's timer
    // would leave the used gong/eggs missing when their grid is loaded later.
    auto restoreSpawn = [this, eventId](GameObjectDataPair const& pair)
    {
        GameObjectData const& spawn = pair.second;
        if (spawn.mapid != instance->GetId())
            return false;
        bool relevant = eventId == TYPE_MARLI ? spawn.id == GO_SPIDER_EGG :
            (spawn.id == GO_GONG_OF_BETHEKK || spawn.id == GO_FORCEFIELD);
        if (!relevant)
            return false;

        instance->GetPersistentState()->SaveGORespawnTime(pair.first, 0);
        if (GameObject* go = instance->GetGameObject(pair.first))
            RestoreEventObject(go, eventId);
        else if (sObjectMgr.IsGameObjectDbGuidDynGuided(pair.first))
            instance->GetSpawnManager().RespawnGameObject(pair.first);
        return false;
    };
    sObjectMgr.DoGOData(restoreSpawn);
}

void instance_zulgurub::RemoveEventSummons(uint32 eventId, uint32 summonEntry)
{
    if (eventId >= MAX_PRIESTS)
        return;

    if (eventId == TYPE_ARLOKK)
    {
        StopArlokkProwlers();
        m_lProwlerGUIDList.clear();
    }

    uint32 const bosses[MAX_PRIESTS] = {NPC_JEKLIK, NPC_VENOXIS, NPC_MARLI, NPC_THEKAL, NPC_ARLOKK};
    std::vector<ObjectGuid> summons;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
    {
        Creature* creature = itr->second;
        if (!creature->IsTemporarySummon() || (summonEntry && creature->GetEntry() != summonEntry))
            continue;
        uint32 ownerEntry = creature->GetSpawnerGuid().GetEntry();
        bool relevant = ownerEntry == bosses[eventId];
        if (eventId == TYPE_MARLI)
            relevant = relevant || ownerEntry == GO_SPIDER_EGG;
        if (eventId == TYPE_ARLOKK)
            relevant = relevant || ownerEntry == NPC_PANTHER_TRIGGER || creature->GetEntry() == NPC_ARLOKK;
        if (relevant)
            summons.push_back(creature->GetObjectGuid());
    }
    // Removing a temporary actor can change the object store.
    for (ObjectGuid guid : summons)
        if (Creature* creature = instance->GetCreature(guid))
            creature->ForcedDespawn();
}

void instance_zulgurub::StopArlokkProwlers()
{
    for (GuidList const* triggers : {&m_lLeftPantherTriggerGUIDList, &m_lRightPantherTriggerGUIDList})
        for (ObjectGuid guid : *triggers)
            if (Creature* trigger = instance->GetCreature(guid))
                trigger->RemoveAurasDueToSpell(SummonZulianProwlers);
}

void instance_zulgurub::ReleaseArlokkProwlers()
{
    std::vector<Player*> targets;
    if (Creature* arlokk = instance->GetCreature(m_arlokkBossGuid))
        for (HostileReference const* reference : arlokk->getThreatManager().getThreatList())
            if (Player* player = instance->GetPlayer(reference->getUnitGuid()))
                if (player->IsAlive() && !player->IsGameMaster())
                    targets.push_back(player);

    for (ObjectGuid guid : m_lProwlerGUIDList)
    {
        Creature* prowler = instance->GetCreature(guid);
        if (!prowler || !prowler->IsAlive())
            continue;

        prowler->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
        if (prowler->GetVictim() || !prowler->AI())
            continue;

        for (Player* target : targets)
        {
            if (!prowler->CanAttack(target))
                continue;
            prowler->AI()->AttackStart(target);
            break;
        }
    }
}

void instance_zulgurub::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId >= MAX_PRIESTS && eventId != RESET_EVENT_HAKKAR)
        return;
    uint32 states[MAX_ENCOUNTER];
    std::istringstream stream(data);
    for (uint32& state : states)
        if (!(stream >> state))
            return;
    uint32 gahzrankaState;
    EdgeOfMadnessProgress madness;
    if (!ReadOptionalEncounters(stream, gahzrankaState, madness))
        return;

    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i];
    m_gahzrankaState = std::max(m_gahzrankaState, gahzrankaState);
    // Priest/Hakkar reset data must not revive a previously refunded reservation.
    m_strInstData = MakeSaveData(m_auiEncounter, m_gahzrankaState, m_edgeOfMadness);
    if (eventId == TYPE_MARLI)
        m_resetMarliObjects = true;
    if (eventId == TYPE_ARLOKK)
    {
        m_resetArlokkObjects = true;
        m_arlokkSummonTimer = 0;
        m_arlokkBossGuid.Clear();
    }
    RemoveEventSummons(eventId);
    RestoreEventObjects(eventId);

    if (Creature* hakkar = GetSingleCreatureFromStorage(NPC_HAKKAR, true))
        if (hakkar->IsAlive() && !hakkar->IsInCombat() && hakkar->AI())
            hakkar->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_B, hakkar, hakkar,
                eventId == RESET_EVENT_HAKKAR ? ZG_RESET_HAKKAR_EVENT : ZG_RESET_HAKKAR_POWER);
}

Creature* instance_zulgurub::SelectRandomPantherTrigger(bool isLeft)
{
    GuidList* plTempList = isLeft ? &m_lLeftPantherTriggerGUIDList : &m_lRightPantherTriggerGUIDList;
    std::vector<Creature*> vTriggers;
    vTriggers.reserve(plTempList->size());

    for (GuidList::const_iterator itr = plTempList->begin(); itr != plTempList->end(); ++itr)
    {
        if (Creature* pTemp = instance->GetCreature(*itr))
            vTriggers.push_back(pTemp);
    }

    if (vTriggers.empty())
        return nullptr;

    return vTriggers[urand(0, vTriggers.size() - 1)];
}

bool instance_zulgurub::RegisterArlokkProwler(Creature const& prowler)
{
    if (GetData(TYPE_ARLOKK) != IN_PROGRESS || prowler.GetEntry() != NPC_ZULIAN_PROWLER ||
        !prowler.IsTemporarySummon() || prowler.GetSpawnerGuid().GetEntry() != NPC_PANTHER_TRIGGER)
        return false;

    m_lProwlerGUIDList.remove_if([this](ObjectGuid guid)
    {
        Creature* summon = instance->GetCreature(guid);
        return !summon || !summon->IsAlive();
    });

    if (IsArlokkProwler(prowler))
        return true;
    if (m_lProwlerGUIDList.size() >= ArlokkProwlerLimit)
        return false;

    m_lProwlerGUIDList.push_back(prowler.GetObjectGuid());
    return true;
}

bool instance_zulgurub::IsArlokkProwler(Creature const& prowler) const
{
    return std::find(m_lProwlerGUIDList.begin(), m_lProwlerGUIDList.end(), prowler.GetObjectGuid()) != m_lProwlerGUIDList.end();
}

bool AreaTrigger_at_zulgurub(Player* pPlayer, AreaTriggerEntry const* pAt)
{
    if (pAt->id == AREATRIGGER_ENTER || pAt->id == AREATRIGGER_ALTAR)
    {
        if (pPlayer->IsGameMaster() || pPlayer->IsDead())
            return false;

        if (instance_zulgurub* pInstance = (instance_zulgurub*)pPlayer->GetInstanceData())
            pInstance->DoYellAtTriggerIfCan(pAt->id);
    }

    return false;
}

void AddSC_instance_zulgurub()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_zulgurub";
    pNewScript->GetInstanceData = &GetNewInstanceScript<instance_zulgurub>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_zulgurub";
    pNewScript->pAreaTrigger = &AreaTrigger_at_zulgurub;
    pNewScript->RegisterSelf();
}
