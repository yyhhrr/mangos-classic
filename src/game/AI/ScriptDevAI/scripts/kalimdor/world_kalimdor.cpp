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

#include "AI/ScriptDevAI/include/sc_common.h"
#include "world_kalimdor.h"
#include "dire_maul/dire_maul_arena.h"
#include "AI/ScriptDevAI/scripts/world/world_map_scripts.h"
#include "World/WorldState.h"
#include "World/WorldStateDefines.h"
#include "GameEvents/GameEventMgr.h"
#include "AI/ScriptDevAI/scripts/world/scourge_invasion.h"

/* *********************************************************
 *                     KALIMDOR
 */

struct GhostOPlasmEvent
{
    ObjectGuid guid;
    uint32 despawnTimer;
    uint8 phaseCounter;
    std::vector<ObjectGuid> summonedMagrami;
    uint32 nextAttemptAt = 0;
    uint8 failedAttempts = 0;
};

/* *********************************************************
 *                     KALIMDOR
 */
struct world_map_kalimdor : public ScriptedMap
{
    world_map_kalimdor(Map* pMap) : ScriptedMap(pMap),
        m_arena(*this, [this]() { SaveArena(); }) { Initialize(); m_arenaSave = m_arena.Save(); }

    DireMaulArena m_arena;
    std::string m_arenaSave;

    void SaveArena() { m_arenaSave = m_arena.Save(); SaveToDB(); }
    char const* Save() const override { return m_arenaSave.c_str(); }
    void Load(char const* data) override
    {
        if (m_arena.Load(data ? data : "")) m_arenaSave = m_arena.Save();
        else
        {
            // Preserve a damaged saved value and keep this area's scheduler
            // closed; other Kalimdor event state remains attached and usable.
            m_arenaSave = data ? data : "";
            OUT_LOAD_INST_DATA_FAIL;
        }
    }
    void OnCreatureRespawn(Creature* actor) override { m_arena.OnCreatureRespawn(actor); }
    void OnCreatureDespawn(Creature* actor) override
    {
        m_arena.OnCreatureDespawn(actor);
        m_freedomEvent.OwnerRemoved(actor->GetObjectGuid());
        if (actor->GetEntry() == NPC_OMEN && actor->GetObjectGuid() == m_omenGuid &&
            GetData(TYPE_OMEN) == IN_PROGRESS)
            ResetOmenEncounter();
    }
    void OnPlayerEnter(Player* player) override { m_arena.OnPlayerEnter(player); }
    void OnPlayerLeave(Player* player) override { m_arena.OnPlayerLeave(player); }


    uint8 m_uiMurkdeepAdds_KilledAddCount;
    std::vector<GhostOPlasmEvent> m_vGOEvents;
    uint32 m_uiOmenResetTimer;
    uint32 m_uiOmenMoonlightTimer;
    uint8 m_uiRocketsCounter;
    uint32 m_encounter[MAX_ENCOUNTER];
    bool b_isOmenSpellCreditDone;
    ObjectGuid m_omenGuid;
    bool m_omenSummoning;
    std::array<std::vector<ObjectGuid>, MAX_ELEMENTS> m_aElementalRiftGUIDs;
    uint32 m_uiDronesTimer;
    FeralasFreedomEvent m_freedomEvent;

    void Initialize() override
    {
        m_uiMurkdeepAdds_KilledAddCount = 0;
        m_vGOEvents.clear();
        m_omenGuid.Clear();
        m_omenSummoning = false;
        m_uiOmenResetTimer = 0;
        m_uiOmenMoonlightTimer = 0;
        m_uiRocketsCounter = 0;
        m_freedomEvent.Reset();
        b_isOmenSpellCreditDone = false;
        for (auto& riftList : m_aElementalRiftGUIDs)
            riftList.clear();
        m_uiDronesTimer = 0;
        memset(&m_encounter, 0, sizeof(m_encounter));
    }

    void OnCreatureCreate(Creature* pCreature) override
    {
        m_arena.OnCreatureCreate(pCreature);
        switch (pCreature->GetEntry())
        {
            case NPC_HORNIZZ:
            case NPC_MURKDEEP:
            case NPC_AVALANCHION:
            case NPC_PRINCESS_TEMPESTRIA:
            case NPC_THE_WINDREAVER:
            case NPC_BARON_CHARR:
                m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
                break;
            case NPC_OMEN:
                if (m_omenSummoning)
                    m_omenGuid = pCreature->GetObjectGuid();
                m_npcEntryGuidStore[NPC_OMEN] = pCreature->GetObjectGuid();
                break;
            case NPC_NECROPOLIS_HEALTH:
                m_npcEntryGuidCollection[pCreature->GetEntry()].push_back(pCreature->GetObjectGuid());
                break;
        }
    }

    void OnCreatureDeath(Creature* pCreature) override
    {
        m_arena.OnCreatureDeath(pCreature);
        switch (pCreature->GetEntry())
        {
            case NPC_GREYMIST_COASTRUNNNER:
                if (pCreature->IsTemporarySummon())         // Only count the ones summoned for Murkdeep quest
                {
                    ++m_uiMurkdeepAdds_KilledAddCount;

                    // If all 3 coastrunners are killed, summon 2 warriors
                    if (m_uiMurkdeepAdds_KilledAddCount == 3)
                    {
                        float fX, fY, fZ;
                        for (uint8 i = 0; i < 2; ++i)
                        {
                            pCreature->GetRandomPoint(aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][0], aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][1], aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][2], 5.0f, fX, fY, fZ);

                            if (Creature* pTemp = pCreature->SummonCreature(NPC_GREYMIST_WARRIOR, fX, fY, fZ, aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][3], TEMPSPAWN_DEAD_DESPAWN, 0))
                            {
                                pTemp->SetWalk(false);
                                pTemp->GetRandomPoint(aSpawnLocations[POS_IDX_MURKDEEP_MOVE][0], aSpawnLocations[POS_IDX_MURKDEEP_MOVE][1], aSpawnLocations[POS_IDX_MURKDEEP_MOVE][2], 5.0f, fX, fY, fZ);
                                pTemp->GetMotionMaster()->MovePoint(0, fX, fY, fZ);
                            }
                        }

                        m_uiMurkdeepAdds_KilledAddCount = 0;
                    }
                }
                break;
            case NPC_GREYMIST_WARRIOR:
                if (pCreature->IsTemporarySummon())         // Only count the ones summoned for Murkdeep quest
                {
                    ++m_uiMurkdeepAdds_KilledAddCount;

                    // After the 2 warriors are killed, Murkdeep spawns, along with a hunter
                    if (m_uiMurkdeepAdds_KilledAddCount == 2)
                    {
                        float fX, fY, fZ;
                        for (uint8 i = 0; i < 2; ++i)
                        {
                            pCreature->GetRandomPoint(aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][0], aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][1], aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][2], 5.0f, fX, fY, fZ);

                            if (Creature* pTemp = pCreature->SummonCreature(!i ? NPC_MURKDEEP : NPC_GREYMIST_HUNTER, fX, fY, fZ, aSpawnLocations[POS_IDX_MURKDEEP_SPAWN][3], TEMPSPAWN_DEAD_DESPAWN, 0))
                            {
                                pTemp->SetWalk(false);
                                pTemp->GetRandomPoint(aSpawnLocations[POS_IDX_MURKDEEP_MOVE][0], aSpawnLocations[POS_IDX_MURKDEEP_MOVE][1], aSpawnLocations[POS_IDX_MURKDEEP_MOVE][2], 5.0f, fX, fY, fZ);
                                pTemp->GetMotionMaster()->MovePoint(0, fX, fY, fZ);
                            }
                        }

                        m_uiMurkdeepAdds_KilledAddCount = 0;
                    }
                }
                break;
            case NPC_OMEN:
                if (pCreature->GetObjectGuid() == m_omenGuid)
                    SetData(TYPE_OMEN, DONE);
                break;
            case NPC_THE_WINDREAVER:
                DoDespawnElementalRifts(ELEMENTAL_AIR);
                break;
            case NPC_PRINCESS_TEMPESTRIA:
                DoDespawnElementalRifts(ELEMENTAL_WATER);
                break;
            case NPC_BARON_CHARR:
                DoDespawnElementalRifts(ELEMENTAL_FIRE);
                break;
            case NPC_AVALANCHION:
                DoDespawnElementalRifts(ELEMENTAL_EARTH);
                break;
            case NPC_COLOSSUS_OF_ZORA:
                WorldObject::SpawnCreature(155122, instance);
                break;
            case NPC_COLOSSUS_OF_REGAL:
                WorldObject::SpawnCreature(155124, instance);
                break;
            case NPC_COLOSSUS_OF_ASHI:
                WorldObject::SpawnCreature(155123, instance);
                break;
            case NPC_NECROPOLIS_HEALTH:
                m_npcEntryGuidCollection.erase(pCreature->GetObjectGuid());
                break;
        }
    }

    void OnObjectCreate(GameObject* pGo) override
    {
        switch (pGo->GetEntry())
        {
            case GO_GHOST_MAGNET:
                m_vGOEvents.push_back({ pGo->GetObjectGuid(), 0, 0, {} }); // insert new event with 0 timer
                pGo->SetActiveObjectState(true);
                break;
            case GO_ROCKET_CLUSTER:
                m_goEntryGuidStore[GO_ROCKET_CLUSTER] = pGo->GetObjectGuid();
                break;
            case GO_EARTH_RIFT:
                m_aElementalRiftGUIDs[ELEMENTAL_EARTH].push_back(pGo->GetObjectGuid());
                break;
            case GO_WATER_RIFT:
                m_aElementalRiftGUIDs[ELEMENTAL_WATER].push_back(pGo->GetObjectGuid());
                break;
            case GO_FIRE_RIFT:
                m_aElementalRiftGUIDs[ELEMENTAL_FIRE].push_back(pGo->GetObjectGuid());
                break;
            case GO_AIR_RIFT:
                m_aElementalRiftGUIDs[ELEMENTAL_AIR].push_back(pGo->GetObjectGuid());
                break;
            case GO_SUMMON_CIRCLE:
                m_goEntryGuidCollection[pGo->GetEntry()].push_back(pGo->GetObjectGuid());
                break;
        }
    }

    void DoDespawnElementalRifts(uint8 index)
    {
        // Despawn all GO rifts for a given element type, erase the GUIDs for the GOs
        for (auto guid : m_aElementalRiftGUIDs[index])
        {
            if (GameObject* pRift = instance->GetGameObject(guid))
                pRift->SetLootState(GO_JUST_DEACTIVATED);
        }
        m_aElementalRiftGUIDs[index].clear();
    }

    void CleanupGhostOPlasmActors(GhostOPlasmEvent& eventData)
    {
        for (auto guid : eventData.summonedMagrami)
        {
            if (Creature* magrami = instance->GetCreature(guid))
            {
                if (magrami->IsAlive()) // Keep corpses available for quest loot.
                {
                    magrami->CastSpell(nullptr, SPELL_SPIRIT_SPAWN_OUT, TRIGGERED_OLD_TRIGGERED);
                    magrami->ForcedDespawn(1000);
                }
            }
        }
        eventData.summonedMagrami.clear();
    }

    void FailedGhostOPlasmAttempt(GhostOPlasmEvent& eventData)
    {
        // Do not spin every map update or block later fifteen-second slots.
        if (++eventData.failedAttempts >= 3)
        {
            ++eventData.phaseCounter;
            eventData.failedAttempts = 0;
            eventData.nextAttemptAt = 0;
        }
        else
            eventData.nextAttemptAt = eventData.despawnTimer + 1000;
    }

    bool GhostOPlasmEventStep(GhostOPlasmEvent& eventData)
    {
        GameObject* go = instance->GetGameObject(eventData.guid);
        if (!go || eventData.despawnTimer > 180000)
        {
            CleanupGhostOPlasmActors(eventData);
            if (go)
                go->AddObjectToRemoveList(); // TODO: Establish rules for despawning temporary GOs that were used in their lifetime (buttons for example)
            return false;
        }

        if (eventData.despawnTimer / 15000 >= eventData.phaseCounter && eventData.despawnTimer >= eventData.nextAttemptAt)
        {
            float x, y, z;
            go->GetPosition(x, y, z);
            uint32 random = urand(0, 35);
            float xR = x + random, yR = y + (40 - random), zR = z;
            if (!instance->GetHeightInRange(xR, yR, zR) || !instance->GetReachableRandomPointOnGround(x, y, z, 10.0f))
            {
                FailedGhostOPlasmAttempt(eventData);
                return true;
            }
            Creature* creature = go->SummonCreature(NPC_MAGRAMI_SPECTRE, xR, yR, zR, 0, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 180000);
            if (!creature)
            {
                FailedGhostOPlasmAttempt(eventData);
                return true;
            }
            ++eventData.phaseCounter;
            eventData.failedAttempts = 0;
            eventData.nextAttemptAt = 0;
            eventData.summonedMagrami.push_back(creature->GetObjectGuid());
            creature->GetMotionMaster()->MovePoint(1, x, y, z);
        }
        return true;
    }

    void ResetOmenEncounter()
    {
        auto stored = m_npcEntryGuidStore.find(NPC_OMEN);
        if (stored != m_npcEntryGuidStore.end() && stored->second == m_omenGuid)
            m_npcEntryGuidStore.erase(stored);
        m_omenGuid.Clear();
        m_encounter[TYPE_OMEN] = NOT_STARTED;
        m_uiOmenResetTimer = 0;
        m_uiOmenMoonlightTimer = 0;
        m_uiRocketsCounter = 0;
        b_isOmenSpellCreditDone = false;
    }

    void Update(uint32 diff) override
    {
        m_arena.Update(diff);
        if (!m_vGOEvents.empty())
        {
            for (auto iter = m_vGOEvents.begin(); iter != m_vGOEvents.end();)
            {
                iter->despawnTimer += diff;
                if (!GhostOPlasmEventStep((*iter)))
                    iter = m_vGOEvents.erase(iter);
                else
                    ++iter;
            }
        }

        if (!m_omenSummoning && GetData(TYPE_OMEN) == IN_PROGRESS)
        {
            if (Creature* omen = instance->GetCreature(m_omenGuid))
            {
                if (omen->IsInCombat())
                    m_uiOmenResetTimer = 15 * MINUTE * IN_MILLISECONDS;
                else if (m_uiOmenResetTimer <= diff)
                {
                    // Retire ownership before callbacks can refer to this round.
                    ResetOmenEncounter();
                    omen->ForcedDespawn();
                }
                else
                    m_uiOmenResetTimer -= diff;
            }
            else
                ResetOmenEncounter();
        }

        if (GetData(TYPE_OMEN) == DONE)
        {
            // Timer before Omen event reset (OOC)
            if (m_uiOmenResetTimer < diff)
            {
                if (Creature* pOmen = instance->GetCreature(m_omenGuid))
                {
                    // Return is Omen is in fight
                    if (pOmen->IsInCombat())
                        return;
                    pOmen->ForcedDespawn();
                }
                ResetOmenEncounter();
            }
            else
                m_uiOmenResetTimer -= diff;

            // Spell summoning GO traps for buff and quest credit for Omen
            if (GetData(TYPE_OMEN) == DONE && !b_isOmenSpellCreditDone)
            {
                if (m_uiOmenMoonlightTimer < diff)
                {
                    if (Creature* pOmen = instance->GetCreature(m_omenGuid))
                    {
                        pOmen->CastSpell(pOmen, SPELL_OMEN_MOONLIGHT, TRIGGERED_OLD_TRIGGERED);
                        b_isOmenSpellCreditDone = true;
                    }
                    m_uiOmenMoonlightTimer = 0;
                }
                else
                    m_uiOmenMoonlightTimer -= diff;
            }
        }
        // Used for Hive Tower area trigger in Silithus
        if (m_uiDronesTimer)
        {
            if (m_uiDronesTimer <= diff)
            {
                SetData(TYPE_HIVE, NOT_STARTED);
                m_uiDronesTimer = 0;
            }
            else
                m_uiDronesTimer -= diff;
        }
    }

    void SetData(uint32 uiType, uint32 uiData) override
    {
        switch (uiType)
        {
            case TYPE_OMEN:
            {
                switch (uiData)
                {
                    case NOT_STARTED:
                        if (m_omenSummoning || GetData(TYPE_OMEN) == IN_PROGRESS || GetData(TYPE_OMEN) == DONE)
                            return;
                        if (m_uiRocketsCounter < MAX_ROCKETS)
                            ++m_uiRocketsCounter;
                        if (m_uiRocketsCounter < MAX_ROCKETS)
                        {
                            if (GetData(TYPE_OMEN) == NOT_STARTED && urand(0, 1) < 1)
                                SetData(TYPE_OMEN, SPECIAL);
                        }
                        else
                            SetData(TYPE_OMEN, IN_PROGRESS);
                        return;
                    case SPECIAL:
                        if (GameObject* pRocketCluster = GetSingleGameObjectFromStorage(GO_ROCKET_CLUSTER))
                        {
                            for (uint8 i = POS_IDX_MINION_OMEN_START; i <= POS_IDX_MINION_OMEN_STOP; i++)
                                pRocketCluster->SummonCreature(NPC_MINION_OMEN, aSpawnLocations[i][0], aSpawnLocations[i][1], aSpawnLocations[i][2], aSpawnLocations[i][3], TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
                        }
                        break;
                    case IN_PROGRESS:
                    {
                        if (m_omenSummoning || GetData(TYPE_OMEN) == IN_PROGRESS || GetData(TYPE_OMEN) == DONE)
                            return;
                        m_omenSummoning = true;
                        m_omenGuid.Clear();
                        Creature* omen = nullptr;
                        if (GameObject* launcher = GetSingleGameObjectFromStorage(GO_ROCKET_CLUSTER))
                            omen = launcher->SummonCreature(NPC_OMEN, aSpawnLocations[POS_IDX_OMEN_SPAWN][0], aSpawnLocations[POS_IDX_OMEN_SPAWN][1], aSpawnLocations[POS_IDX_OMEN_SPAWN][2], aSpawnLocations[POS_IDX_OMEN_SPAWN][3], TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
                        m_omenSummoning = false;
                        if (!omen)
                        {
                            // Keep the saturated rocket count so the next launch can retry.
                            Creature* partial = instance->GetCreature(m_omenGuid);
                            auto stored = m_npcEntryGuidStore.find(NPC_OMEN);
                            if (stored != m_npcEntryGuidStore.end() && stored->second == m_omenGuid)
                                m_npcEntryGuidStore.erase(stored);
                            m_omenGuid.Clear();
                            if (partial)
                                partial->ForcedDespawn();
                            return;
                        }
                        m_omenGuid = omen->GetObjectGuid();
                        m_npcEntryGuidStore[NPC_OMEN] = m_omenGuid;
                        if (GetData(TYPE_OMEN) == DONE)
                            return;
                        if (!omen->IsAlive())
                        {
                            m_encounter[TYPE_OMEN] = IN_PROGRESS;
                            SetData(TYPE_OMEN, DONE);
                            return;
                        }
                        m_uiOmenResetTimer = 15 * MINUTE * IN_MILLISECONDS;
                        m_encounter[TYPE_OMEN] = IN_PROGRESS;
                        omen->SetWalk(true);
                        omen->GetMotionMaster()->MovePoint(1, aSpawnLocations[POS_IDX_OMEN_MOVE][0], aSpawnLocations[POS_IDX_OMEN_MOVE][1], aSpawnLocations[POS_IDX_OMEN_MOVE][2]);
                        return;
                    }
                    case DONE:
                        if (!m_omenGuid || (GetData(TYPE_OMEN) != IN_PROGRESS && !m_omenSummoning))
                            return;
                        m_uiOmenMoonlightTimer = 5 * IN_MILLISECONDS;
                        m_uiOmenResetTimer = 5 * MINUTE * IN_MILLISECONDS;
                        break;
                }
                break;
            }
            case TYPE_GONG_TIME:
                // TODO: Handle initial first gong only
                break;
            case TYPE_HIVE:
                if (uiData == IN_PROGRESS)
                    m_uiDronesTimer = 5 * MINUTE * IN_MILLISECONDS;
                break;
            case TYPE_FREEDOM_CREATURES:
                // Anonymous SPECIAL callbacks cannot prove round or actor identity.
                // The Feralas callbacks use m_freedomEvent's explicit GUID protocol.
                if (uiData == NOT_STARTED)
                    m_freedomEvent.Reset();
                return;
        }
        m_encounter[uiType] = uiData;
    }

    uint32 GetData(uint32 uiType) const override
    {
        return uiType == TYPE_FREEDOM_CREATURES ? m_freedomEvent.GetState() : m_encounter[uiType];
    }
};

DireMaulArena* GetDireMaulArena(Map* map)
{
    if (!map || map->GetId() != DMA_MAP || map->Instanceable()) return nullptr;
    auto* owner = dynamic_cast<world_map_kalimdor*>(map->GetInstanceData());
    return owner ? &owner->m_arena : nullptr;
}

FeralasFreedomEvent* GetFeralasFreedomEvent(Map* map)
{
    if (!map || map->GetId() != 1 || map->Instanceable())
        return nullptr;
    auto* owner = dynamic_cast<world_map_kalimdor*>(map->GetInstanceData());
    return owner ? &owner->m_freedomEvent : nullptr;
}

InstanceData* GetInstanceData_world_map_kalimdor(Map* pMap)
{
    return new world_map_kalimdor(pMap);
}

void AddSC_world_kalimdor()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "world_map_kalimdor";
    pNewScript->GetInstanceData = &GetInstanceData_world_map_kalimdor;
    pNewScript->RegisterSelf();
}
