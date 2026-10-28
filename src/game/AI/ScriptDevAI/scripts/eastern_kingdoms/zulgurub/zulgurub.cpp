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
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnManager.h"

instance_zulgurub::instance_zulgurub(Map* map) : ScriptedInstance(map),
    m_bHasIntroYelled(false),
    m_bHasAltarYelled(false),
    m_resetMarliObjects(false),
    m_resetArlokkObjects(false)
{
    Initialize();
}

void instance_zulgurub::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
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

void instance_zulgurub::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_GONG_OF_BETHEKK:
        case GO_FORCEFIELD:
            if (m_resetArlokkObjects && GetData(TYPE_ARLOKK) == NOT_STARTED)
                RestoreEventObject(go, TYPE_ARLOKK);
            break;
        case GO_SPIDER_EGG:
            m_lSpiderEggGUIDList.push_back(go->GetObjectGuid());
            if (m_resetMarliObjects && GetData(TYPE_MARLI) == NOT_STARTED)
                RestoreEventObject(go, TYPE_MARLI);
            return;
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
                RemoveHakkarPowerStack();
            break;
        case TYPE_MARLI:
            m_auiEncounter[type] = data;
            if (data == DONE)
                RemoveHakkarPowerStack();
            if (data == FAIL)
            {
                for (GuidList::const_iterator itr = m_lSpiderEggGUIDList.begin(); itr != m_lSpiderEggGUIDList.end(); ++itr)
                {
                    if (GameObject* pEgg = instance->GetGameObject(*itr))
                    {
                        // Note: this type of Gameobject needs to be respawned manually
                        pEgg->SetRespawnTime(2 * DAY);
                        pEgg->Respawn();
                    }
                }
            }
            break;
        case TYPE_ARLOKK:
            m_auiEncounter[type] = data;
            if (data == DONE || data == FAIL)
                DoUseOpenableObject(GO_FORCEFIELD, true);
            else if (data == IN_PROGRESS)
                DoUseOpenableObject(GO_FORCEFIELD, false);

            if (data == DONE)
                RemoveHakkarPowerStack();
            if (data == FAIL)
            {
                // Note: this gameobject should change flags - currently it despawns which isn't correct
                if (GameObject* pGong = GetSingleGameObjectFromStorage(GO_GONG_OF_BETHEKK))
                {
                    pGong->SetRespawnTime(2 * DAY);
                    pGong->Respawn();
                }
            }
            break;
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

        std::ostringstream saveStream;
        saveStream << m_auiEncounter[0] << " " << m_auiEncounter[1] << " " << m_auiEncounter[2] << " "
                   << m_auiEncounter[3] << " " << m_auiEncounter[4] << " " << m_auiEncounter[5] << " "
                   << m_auiEncounter[6] << " " << m_auiEncounter[7];

        m_strInstData = saveStream.str();

        SaveToDB();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

// Each time one of the High Priests dies, remove one stack of Hakkar's Power (lowering Hakkar's HP)
void instance_zulgurub::RemoveHakkarPowerStack()
{
    if (Creature* hakkar = GetSingleCreatureFromStorage(NPC_HAKKAR))
    {
        if (!hakkar->IsAlive())
            return;

        hakkar->CastSpell(hakkar, SPELL_HAKKAR_POWER_DOWN, TRIGGERED_NONE);
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
    loadStream >> m_auiEncounter[0] >> m_auiEncounter[1] >> m_auiEncounter[2] >> m_auiEncounter[3]
               >> m_auiEncounter[4] >> m_auiEncounter[5] >> m_auiEncounter[6] >> m_auiEncounter[7];

    for (uint32& i : m_auiEncounter)
    {
        if (i == IN_PROGRESS)
            i = NOT_STARTED;
    }

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

    std::ostringstream stream;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i)
            stream << ' ';
        stream << states[i];
    }
    data = stream.str();
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

void instance_zulgurub::RemoveEventSummons(uint32 eventId)
{
    if (eventId >= MAX_PRIESTS)
        return;

    uint32 const bosses[MAX_PRIESTS] = {NPC_JEKLIK, NPC_VENOXIS, NPC_MARLI, NPC_THEKAL, NPC_ARLOKK};
    std::vector<ObjectGuid> summons;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
    {
        Creature* creature = itr->second;
        if (!creature->IsTemporarySummon())
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

    if (eventId == TYPE_ARLOKK)
    {
        for (GuidList const* triggers : {&m_lLeftPantherTriggerGUIDList, &m_lRightPantherTriggerGUIDList})
            for (ObjectGuid guid : *triggers)
                if (Creature* trigger = instance->GetCreature(guid))
                    trigger->RemoveAurasDueToSpell(24247);
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
    stream >> std::ws;
    if (!stream.eof())
        return;

    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i];
    m_strInstData = data;
    if (eventId == TYPE_MARLI)
        m_resetMarliObjects = true;
    if (eventId == TYPE_ARLOKK)
        m_resetArlokkObjects = true;
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
