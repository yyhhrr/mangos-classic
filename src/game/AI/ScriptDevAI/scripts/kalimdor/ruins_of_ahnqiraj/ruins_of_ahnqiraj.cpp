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
SDName: Instance_Ruins_of_Ahnqiraj
SD%Complete: 80
SDComment: It's not clear if the Rajaxx event should reset if Andorov dies, or party wipes.
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "ruins_of_ahnqiraj.h"
#include "Groups/Group.h"
#include "Maps/MapPersistentStateMgr.h"

instance_ruins_of_ahnqiraj::instance_ruins_of_ahnqiraj(Map* map) : ScriptedInstance(map)
{
    Initialize();
}

// The whole Rajaxx encounter owns these eight groups. They must be rebuilt
// together; reviving Rajaxx alone leaves the original army waves exhausted.
static char const* const aRajaxxResetGroups[] = {
    "AQ20_RAJAXX", "AQ20_QEEZ", "AQ20_TUUBID", "AQ20_DRENN",
    "AQ20_XURREM", "AQ20_YEGGETH", "AQ20_PAKKON", "AQ20_ZERRAN"
};

bool instance_ruins_of_ahnqiraj::CanStartRajaxxEvent()
{
    if (GetData(TYPE_KURINNAXX) != DONE || GetData(TYPE_RAJAXX) != NOT_STARTED ||
        m_rajaxxResetPending || m_rajaxxResetTimer)
        return false;

    return std::all_of(std::begin(aRajaxxResetGroups), std::end(aRajaxxResetGroups), [this](char const* name)
    {
        return instance->GetSpawnManager().GetSpawnGroup(std::string(name));
    });
}

std::vector<InstanceEventReset> instance_ruins_of_ahnqiraj::GetResettableEvents() const
{
    return {{TYPE_RAJAXX, "General Rajaxx / Andorov's Army",
        {NPC_RAJAXX, NPC_GENERAL_ANDOROV, NPC_CAPTAIN_QEEZ, NPC_CAPTAIN_TUUBID, NPC_CAPTAIN_DRENN,
         NPC_CAPTAIN_XURREM, NPC_MAJOR_YEGGETH, NPC_MAJOR_PAKKON, NPC_COLONEL_ZERRAN}, 0}};
}

bool instance_ruins_of_ahnqiraj::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId != TYPE_RAJAXX || m_rajaxxResetPending || m_rajaxxResetTimer || GetData(TYPE_KURINNAXX) != DONE)
        return false;
    // Refuse unsupported/partial army data before changing any live event state.
    for (char const* name : aRajaxxResetGroups)
        if (!instance->GetSpawnManager().GetSpawnGroup(std::string(name)))
            return false;
    std::ostringstream out;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i) out << ' ';
        out << (i == TYPE_RAJAXX ? uint32(NOT_STARTED) : m_encounter[i]);
    }
    data = out.str();
    return true;
}

void instance_ruins_of_ahnqiraj::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId != TYPE_RAJAXX)
        return;
    m_encounter[TYPE_RAJAXX] = NOT_STARTED;
    m_strInstData = data;
    ResetRajaxxEvent();
}

void instance_ruins_of_ahnqiraj::ResetRajaxxEvent()
{
    m_rajaxxResetPending = false;
    m_armyDelayTimer = 0;
    m_nextArmyWave = 0;
    m_rajaxxReleased = false;
    for (GuidSet& members : m_livingArmyWaves)
        members.clear();

    SetRajaxxGateClosed(false);
    for (char const* name : aRajaxxResetGroups)
        if (SpawnGroup* group = instance->GetSpawnManager().GetSpawnGroup(std::string(name)))
        {
            group->SetEnabled(false);
            if (CreatureGroup* army = group->GetCreatureGroup())
                army->Despawn(0, false, 0);
            for (auto const& member : group->GetGroupEntry().DbGuids)
                instance->GetPersistentState()->SaveCreatureRespawnTime(member.DbGuid, 0);
        }
    DespawnRajaxxHelpers();
    m_npcEntryGuidStore.erase(NPC_GENERAL_ANDOROV);
    m_kaldoreiGuidList.clear();

    // Allow the map to remove the previous actors before recreating the groups.
    m_rajaxxResetTimer = IN_MILLISECONDS;
}

void instance_ruins_of_ahnqiraj::DespawnRajaxxHelpers(uint32 delay)
{
    GuidVector helpers;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
    {
        Creature* actor = itr->second;
        if ((actor->GetEntry() == NPC_GENERAL_ANDOROV || actor->GetEntry() == NPC_KALDOREI_ELITE) &&
            !actor->HasStaticDBSpawnData())
            helpers.push_back(actor->GetObjectGuid());
    }
    for (ObjectGuid guid : helpers)
        if (Creature* actor = instance->GetCreature(guid))
        {
            actor->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_VENDOR);
            actor->ForcedDespawn(delay);
        }
}

void instance_ruins_of_ahnqiraj::SetRajaxxGateClosed(bool closed)
{
    if (GameObject* gate = instance->GetGameObject(m_rajaxxGateGuid))
    {
        if (!closed)
        {
            gate->SetGoState(GO_STATE_ACTIVE);
            gate->ForcedDespawn();
            m_rajaxxGateGuid.Clear();
        }
        return;
    }

    m_rajaxxGateGuid.Clear();
    if (!closed)
        return;

    GameObject* gate = new GameObject;
    const uint32 guid = instance->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT, GO_RAJAXX_GATE);
    if (!gate->Create(guid, guid, GO_RAJAXX_GATE, instance, -8526.0f, 1507.4f, 49.0f, 4.20662f, 0.0f, 0.0f, 0.861534f, -0.5077f))
    {
        delete gate;
        return;
    }
    gate->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    gate->SetActiveObjectState(true);
    instance->Add(gate);
    gate->AIM_Initialize();
    m_rajaxxGateGuid = gate->GetObjectGuid();
}

void instance_ruins_of_ahnqiraj::Initialize()
{
    memset(&m_encounter, 0, sizeof(m_encounter));
}

bool instance_ruins_of_ahnqiraj::IsEncounterInProgress() const
{
    for (uint32 i : m_encounter)
    {
        if (i == IN_PROGRESS)
            return true;
    }
    return false;
}

void instance_ruins_of_ahnqiraj::OnPlayerEnter(Player* /*player*/)
{
    // Respawn times can outlive an interrupted attempt even when its state was never saved.
    // Check here, after the map has initialized both persistent state and spawn groups.
    if (!m_npcEntryGuidStore.count(NPC_GENERAL_ANDOROV) && CanStartRajaxxEvent())
    {
        auto const* state = instance->GetPersistentState();
        const time_t now = time(nullptr);
        for (char const* name : aRajaxxResetGroups)
        {
            auto const& members = instance->GetSpawnManager().GetSpawnGroup(std::string(name))->GetGroupEntry().DbGuids;
            if (std::any_of(members.begin(), members.end(), [state, now](auto const& member)
                {
                    return state->GetCreatureRespawnTime(member.DbGuid) > now;
                }))
            {
                m_rajaxxResetPending = true;
                return;
            }
        }
    }

    DoSpawnAndorovIfCan();
}

void instance_ruins_of_ahnqiraj::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_OSSIRIAN_TRIGGER:
        {
            GuidVector& vector = m_npcEntryGuidCollection[creature->GetEntry()];
            vector.push_back(creature->GetObjectGuid());
            std::sort(vector.begin(), vector.end(), [&](ObjectGuid const& a, ObjectGuid const& b) -> bool
            {
                return a.GetCounter() < b.GetCounter();
            });
            break;
        }
        case NPC_BURU:
        case NPC_OSSIRIAN:
        case NPC_RAJAXX:
        case NPC_GENERAL_ANDOROV:
        case NPC_COLONEL_ZERRAN:
        case NPC_MAJOR_PAKKON:
        case NPC_MAJOR_YEGGETH:
        case NPC_CAPTAIN_XURREM:
        case NPC_CAPTAIN_DRENN:
        case NPC_CAPTAIN_TUUBID:
        case NPC_CAPTAIN_QEEZ:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
        case NPC_KALDOREI_ELITE:
            m_kaldoreiGuidList.push_back(creature->GetObjectGuid());
            break;
    }
    LockRajaxxActor(creature);
}

void instance_ruins_of_ahnqiraj::OnObjectCreate(GameObject* go)
{
    if (go->GetEntry() == GO_OSSIRIAN_CRYSTAL)
        m_goEntryGuidCollection[go->GetEntry()].push_back(go->GetObjectGuid());
}

void instance_ruins_of_ahnqiraj::OnCreatureEnterCombat(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_KURINNAXX: SetData(TYPE_KURINNAXX, IN_PROGRESS); break;
        case NPC_MOAM:      SetData(TYPE_MOAM, IN_PROGRESS);      break;
        case NPC_BURU:      SetData(TYPE_BURU, IN_PROGRESS);      break;
        case NPC_AYAMISS:   SetData(TYPE_AYAMISS, IN_PROGRESS);   break;
        case NPC_OSSIRIAN:  SetData(TYPE_OSSIRIAN, IN_PROGRESS);  break;
    }
}

void instance_ruins_of_ahnqiraj::OnCreatureEvade(Creature* creature)
{
    if (GetData(TYPE_RAJAXX) == IN_PROGRESS)
    {
        const ObjectGuid guid = creature->GetObjectGuid();
        const bool releasedMember = std::any_of(m_livingArmyWaves.begin(), m_livingArmyWaves.end(), [guid](GuidSet const& members)
        {
            return members.count(guid) != 0;
        });
        const auto waveInCombat = [this](GuidSet const& members)
        {
            return std::any_of(members.begin(), members.end(), [this](ObjectGuid member)
            {
                Creature* attacker = instance->GetCreature(member);
                return attacker && attacker->IsAlive() && attacker->IsInCombat();
            });
        };
        if ((m_rajaxxReleased && creature->GetEntry() == NPC_RAJAXX) ||
            (releasedMember && std::none_of(m_livingArmyWaves.begin(), m_livingArmyWaves.end(), waveInCombat)))
        {
            SetData(TYPE_RAJAXX, FAIL);
            return;
        }
    }

    switch (creature->GetEntry())
    {
        case NPC_KURINNAXX: SetData(TYPE_KURINNAXX, FAIL); break;
        case NPC_MOAM:      SetData(TYPE_MOAM, FAIL);      break;
        case NPC_BURU:      SetData(TYPE_BURU, FAIL);      break;
        case NPC_AYAMISS:   SetData(TYPE_AYAMISS, FAIL);   break;
        case NPC_OSSIRIAN:  SetData(TYPE_OSSIRIAN, FAIL);  break;
    }
}

void instance_ruins_of_ahnqiraj::OnCreatureDeath(Creature* creature)
{
    if (GetData(TYPE_RAJAXX) == IN_PROGRESS)
    {
        for (GuidSet& members : m_livingArmyWaves)
            if (members.erase(creature->GetObjectGuid()))
            {
                ScheduleRajaxxWaveIfCleared();
                break;
            }
    }

    switch (creature->GetEntry())
    {
        case NPC_KURINNAXX: SetData(TYPE_KURINNAXX, DONE); break;
        case NPC_RAJAXX:
            if (GetData(TYPE_RAJAXX) == IN_PROGRESS && m_rajaxxReleased)
            {
                RewardRajaxxSurvivors(creature);
                SetData(TYPE_RAJAXX, DONE);
            }
            break;
        case NPC_MOAM:      SetData(TYPE_MOAM, DONE);      break;
        case NPC_BURU:      SetData(TYPE_BURU, DONE);      break;
        case NPC_AYAMISS:   SetData(TYPE_AYAMISS, DONE);   break;
        case NPC_OSSIRIAN:  SetData(TYPE_OSSIRIAN, DONE);  break;
        case NPC_GENERAL_ANDOROV:
            if (GetData(TYPE_RAJAXX) == DONE)
                DespawnRajaxxHelpers(2500);
            break;
    }
}

void instance_ruins_of_ahnqiraj::RewardRajaxxSurvivors(Creature* rajaxx)
{
    if (rajaxx->IsNoReputation())
        return;

    uint32 survivors = 0;
    if (Creature* andorov = GetSingleCreatureFromStorage(NPC_GENERAL_ANDOROV, true))
        if (andorov->IsAlive())
            ++survivors;
    for (ObjectGuid guid : m_kaldoreiGuidList)
        if (Creature* elite = instance->GetCreature(guid))
            if (elite->IsAlive())
                ++survivors;

    if (!survivors)
        return;

    // Apply the encounter spell's reputation effect to the same players who qualify
    // for the Boss kill reward, including members who died before the kill.
    constexpr uint32 bonusReputationSpell = 26342;
    SpellEntry const* bonus = sSpellTemplate.LookupEntry<SpellEntry>(bonusReputationSpell);
    if (!bonus)
        return;

    const uint32 factionId = bonus->EffectMiscValue[EFFECT_INDEX_0];
    FactionEntry const* faction = sFactionStore.LookupEntry(factionId);
    if (!faction)
        return;

    const int32 reputation = bonus->CalculateSimpleValue(EFFECT_INDEX_0);
    auto reward = [rajaxx, faction, factionId, reputation, survivors](Player* player)
    {
        if (player && player->IsAtGroupRewardDistance(rajaxx))
        {
            int32 perSurvivor = player->CalculateReputationGain(REPUTATION_SOURCE_SPELL, reputation, factionId);
            player->GetReputationMgr().ModifyReputation(faction, perSurvivor * int32(survivors));
        }
    };

    Player* tapper = rajaxx->GetOriginalLootRecipient();
    reward(tapper);
    if (Group* group = rajaxx->GetGroupLootRecipient())
        for (GroupReference* member = group->GetFirstMember(); member; member = member->next())
            if (member->getSource() != tapper)
                reward(member->getSource());
}

void instance_ruins_of_ahnqiraj::OnCreatureRespawn(Creature* creature)
{
    LockRajaxxActor(creature);
    switch (creature->GetEntry())
    {
        case NPC_OSSIRIAN_TRIGGER:
        {
            if (Creature* ossirian = GetSingleCreatureFromStorage(NPC_OSSIRIAN))
                ossirian->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, creature, ossirian);
            break;
        }
        case NPC_QIRAJI_WARRIOR:
        case NPC_SWARMGUARD_NEEDLER:
        {
            creature->SetCorpseDelay(5);
            break;
        }
    }
}

void instance_ruins_of_ahnqiraj::SetData(uint32 type, uint32 data)
{
    switch (type)
    {
        case TYPE_KURINNAXX:
            if (m_encounter[type] == data)
                return;
            m_encounter[type] = data;
            if (data == DONE)
            {
                DoSpawnAndorovIfCan();
                DoOrSimulateScriptTextForThisInstance(SAY_OSSIRIAN_INTRO, NPC_OSSIRIAN);
            }
            break;
        case TYPE_RAJAXX:
            if (m_encounter[type] == data || (data == IN_PROGRESS && !CanStartRajaxxEvent()) ||
                ((data == DONE || data == FAIL) && m_encounter[type] != IN_PROGRESS))
                return;

            m_encounter[type] = data;
            if (data == IN_PROGRESS)
            {
                SetRajaxxGateClosed(true);
                m_armyDelayTimer = 1;
            }
            else if (data == FAIL)
            {
                m_armyDelayTimer = 0;
                m_rajaxxResetPending = true;
                SetRajaxxGateClosed(false);
            }
            else if (data == DONE)
            {
                m_armyDelayTimer = 0;
                SetRajaxxGateClosed(false);
                Creature* andorov = GetSingleCreatureFromStorage(NPC_GENERAL_ANDOROV, true);
                if (andorov && andorov->IsAlive())
                    andorov->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, andorov, andorov);
                else
                    DespawnRajaxxHelpers(2500);
            }
            break;
        case TYPE_MOAM:
        case TYPE_BURU:
        case TYPE_AYAMISS:
        case TYPE_OSSIRIAN:
            if (type == FAIL)
            {
                GuidVector crystals;
                GetGameObjectGuidVectorFromStorage(GO_OSSIRIAN_CRYSTAL, crystals);
                for (uint32 i = 1; i < crystals.size(); ++i) // skip first
                {
                    if (GameObject* go = instance->GetGameObject(crystals[i]))
                    {
                        go->SetLootState(GO_JUST_DEACTIVATED);
                        go->SetForcedDespawn();
                    }
                }
                m_goEntryGuidCollection[GO_OSSIRIAN_CRYSTAL].resize(1); // keeps first
            }
            m_encounter[type] = data;
            break;
    }

    if (data == DONE)
    {
        OUT_SAVE_INST_DATA;

        std::ostringstream saveStream;
        saveStream << m_encounter[0] << " " << m_encounter[1] << " " << m_encounter[2]
                   << " " << m_encounter[3] << " " << m_encounter[4] << " " << m_encounter[5];

        m_strInstData = saveStream.str();

        SaveToDB();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

uint32 instance_ruins_of_ahnqiraj::GetData(uint32 type) const
{
    if (type < MAX_ENCOUNTER)
        return m_encounter[type];

    return 0;
}

void instance_ruins_of_ahnqiraj::DoSpawnAndorovIfCan()
{
    if (GetData(TYPE_KURINNAXX) != DONE || GetData(TYPE_RAJAXX) != NOT_STARTED || m_rajaxxResetPending ||
        m_rajaxxResetTimer || m_npcEntryGuidStore.count(NPC_GENERAL_ANDOROV))
        return;

    Player* player = GetPlayerInMap();
    if (!player)
        return;

    for (const auto& aAndorovSpawnLoc : aAndorovSpawnLocs)
        player->SummonCreature(aAndorovSpawnLoc.m_spawnEntry, aAndorovSpawnLoc.m_fx, aAndorovSpawnLoc.m_fy, aAndorovSpawnLoc.m_fz, aAndorovSpawnLoc.m_fo, TEMPSPAWN_DEAD_DESPAWN, 0);
}

void instance_ruins_of_ahnqiraj::Load(const char* chrIn)
{
    if (!chrIn)
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    OUT_LOAD_INST_DATA(chrIn);

    std::istringstream loadStream(chrIn);

    loadStream >> m_encounter[0] >> m_encounter[1] >> m_encounter[2]
               >> m_encounter[3] >> m_encounter[4] >> m_encounter[5];

    m_rajaxxResetPending = GetData(TYPE_RAJAXX) == IN_PROGRESS || GetData(TYPE_RAJAXX) == FAIL;
    for (uint32& i : m_encounter)
    {
        if (i == IN_PROGRESS)
            i = NOT_STARTED;
    }

    OUT_LOAD_INST_DATA_COMPLETE;
}

void instance_ruins_of_ahnqiraj::Update(uint32 diff)
{
    if (m_rajaxxResetPending)
    {
        ResetRajaxxEvent();
        return;
    }

    if (m_rajaxxResetTimer)
    {
        if (m_rajaxxResetTimer > diff)
        {
            m_rajaxxResetTimer -= diff;
            return;
        }
        m_rajaxxResetTimer = 0;
        for (char const* name : aRajaxxResetGroups)
            if (SpawnGroup* group = instance->GetSpawnManager().GetSpawnGroup(std::string(name)))
            {
                group->SetEnabled(true);
                group->Spawn(true, true);
            }
        m_encounter[TYPE_RAJAXX] = NOT_STARTED;
        DoSpawnAndorovIfCan();
    }

    if (GetData(TYPE_RAJAXX) != IN_PROGRESS)
        return;

    auto const& players = instance->GetPlayers();
    if (std::none_of(players.begin(), players.end(), [](auto const& ref)
        {
            Player* player = ref.getSource();
            return player && player->IsAlive() && !player->IsGameMaster();
        }))
    {
        SetData(TYPE_RAJAXX, FAIL);
        return;
    }

    if (!m_armyDelayTimer)
        return;

    if (m_armyDelayTimer <= diff)
    {
        m_armyDelayTimer = 0;
        DoSendNextArmyWave();
    }
    else
        m_armyDelayTimer -= diff;
}

void instance_ruins_of_ahnqiraj::LockRajaxxActor(Creature* creature)
{
    if (GetData(TYPE_RAJAXX) == DONE)
        return;

    bool locked = creature->GetEntry() == NPC_RAJAXX && !m_rajaxxReleased;
    if (CreatureGroup* group = creature->GetCreatureGroup())
        for (uint8 wave = m_nextArmyWave; wave < MAX_ARMY_WAVES; ++wave)
            if (group->GetGroupEntry().StringId == instance->GetMapDataContainer().GetStringId(aArmySortingParameters[wave].m_stringId))
            {
                locked = true;
                break;
            }

    if (locked)
    {
        creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        if (creature->AI())
            creature->AI()->SetReactState(REACT_PASSIVE);
    }
}

GuidSet instance_ruins_of_ahnqiraj::GetLivingRajaxxGroup(std::string const& name)
{
    GuidSet living;
    if (SpawnGroup* group = instance->GetSpawnManager().GetSpawnGroup(name))
    {
        for (auto const& member : group->GetGroupEntry().DbGuids)
            if (CreatureData const* data = sObjectMgr.GetCreatureData(member.DbGuid))
                if (!instance->IsLoaded(data->posX, data->posY))
                    instance->LoadGrid(data->posX, data->posY);

        group->Spawn(true, false);
        for (auto const& member : group->GetGroupEntry().DbGuids)
            if (Creature* creature = instance->GetCreature(member.DbGuid))
                if (creature->IsAlive())
                    living.insert(creature->GetObjectGuid());
    }
    return living;
}

void instance_ruins_of_ahnqiraj::ScheduleRajaxxWaveIfCleared()
{
    if (!m_nextArmyWave || m_rajaxxReleased)
        return;

    if (m_nextArmyWave < MAX_ARMY_WAVES)
    {
        if (m_livingArmyWaves[m_nextArmyWave - 1].empty())
            m_armyDelayTimer = 1;
    }
    else if (std::all_of(m_livingArmyWaves.begin(), m_livingArmyWaves.end(), [](GuidSet const& members) { return members.empty(); }))
        m_armyDelayTimer = 1;
}

void instance_ruins_of_ahnqiraj::DoSendNextArmyWave()
{
    if (m_rajaxxReleased)
        return;

    GuidSet attackers;
    if (m_nextArmyWave < MAX_ARMY_WAVES)
    {
        SortingParameters const& wave = aArmySortingParameters[m_nextArmyWave];
        attackers = GetLivingRajaxxGroup(wave.m_stringId);
        if (attackers.empty())
        {
            script_error_log("Instance Ruins of Ahn'Qiraj: no living members of Rajaxx wave %s.", wave.m_stringId.c_str());
            SetData(TYPE_RAJAXX, FAIL);
            return;
        }

        m_livingArmyWaves[m_nextArmyWave++] = attackers;
        if (m_nextArmyWave < MAX_ARMY_WAVES)
            m_armyDelayTimer = 3 * MINUTE * IN_MILLISECONDS;

        if (wave.m_uiYellEntry)
            if (Creature* rajaxx = GetSingleCreatureFromStorage(NPC_RAJAXX, true))
                DoScriptText(wave.m_uiYellEntry, rajaxx);
    }
    else
    {
        if (std::any_of(m_livingArmyWaves.begin(), m_livingArmyWaves.end(), [](GuidSet const& members) { return !members.empty(); }))
            return;

        attackers = GetLivingRajaxxGroup("AQ20_RAJAXX");
        if (attackers.empty())
        {
            script_error_log("Instance Ruins of Ahn'Qiraj: General Rajaxx is unavailable after the last wave.");
            SetData(TYPE_RAJAXX, FAIL);
            return;
        }
        m_rajaxxReleased = true;
    }

    for (ObjectGuid guid : attackers)
        if (Creature* attacker = instance->GetCreature(guid))
        {
            attacker->SetActiveObjectState(true);
            attacker->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
            attacker->SetWalk(false);
            attacker->AI()->SetReactState(REACT_AGGRESSIVE);
            attacker->SetInCombatWithZone();
            attacker->AI()->AttackClosestEnemy();
        }
}

void AddSC_instance_ruins_of_ahnqiraj()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_ruins_of_ahnqiraj";
    pNewScript->GetInstanceData = &GetNewInstanceScript<instance_ruins_of_ahnqiraj>;
    pNewScript->RegisterSelf();
}
