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
SDName: Instance_Blackwing_Lair
SD%Complete: 100
SDComment:
SDCategory: Blackwing Lair
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackwing_lair.h"
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "World/World.h"

namespace
{
constexpr time_t ScepterQuestDuration = 5 * HOUR;
constexpr time_t ScepterWarningOffsets[] = { ScepterQuestDuration - 2, 3 * HOUR, HOUR, 30 * MINUTE, 0 };
}

instance_blackwing_lair::instance_blackwing_lair(Map* pMap) : ScriptedInstance(pMap),
    m_uiResetTimer(0),
    m_uiDefenseTimer(0),
    m_uiNefarianSpawnTimer(0),
    m_uiScepterQuestStep(0),
    m_uiDragonspawnCount(0),
    m_uiBlackwingDefCount(0),
    m_uiDeadDrakonidsCount(0)
{
    Initialize();
}

void instance_blackwing_lair::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

    auto findEggs = [this](GameObjectDataPair const& spawn)
    {
        if (spawn.second.mapid == instance->GetId() && spawn.second.id == GO_BLACK_DRAGON_EGG &&
            spawn.second.spawntimesecsmin >= 0 && spawn.second.IsNotPartOfPoolOrEvent())
            m_razorgoreEggs.try_emplace(spawn.first);
        return false;
    };
    sObjectMgr.DoGOData(findEggs);
}

bool instance_blackwing_lair::IsEncounterInProgress() const
{
    if (GetData(TYPE_RAZORGORE) == SPECIAL || GetData(TYPE_NEFARIAN) == SPECIAL)
        return true;

    for (uint32 boss = TYPE_RAZORGORE; boss <= TYPE_NEFARIAN; ++boss)
    {
        if (GetData(boss) == IN_PROGRESS)
            return true;
    }
    return false;
}

void instance_blackwing_lair::OnCreatureCreate(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        case NPC_MONSTER_GENERATOR:
            if (std::find(m_vGeneratorGuids.begin(), m_vGeneratorGuids.end(), pCreature->GetObjectGuid()) == m_vGeneratorGuids.end())
                m_vGeneratorGuids.push_back(pCreature->GetObjectGuid());
            break;
        case NPC_GRETHOK_CONTROLLER:
        case NPC_BLACKWING_GUARDSMAN:
            m_razorgoreGuards.insert(pCreature->GetObjectGuid());
            break;
        // Nefarian encounter
        case NPC_BLACK_SPAWNER:
        case NPC_RED_SPAWNER:
        case NPC_GREEN_SPAWNER:
        case NPC_BRONZE_SPAWNER:
        case NPC_BLUE_SPAWNER:
            m_lDrakonidSpawnerGuids.push_back(pCreature->GetObjectGuid());
            break;
        case NPC_BLACK_DRAKONID:
        case NPC_RED_DRAKONID:
        case NPC_GREEN_DRAKONID:
        case NPC_BLUE_DRAKONID:
        case NPC_BRONZE_DRAKONID:
        case NPC_CHROMATIC_DRAKONID:
            m_drakonids.push_back(pCreature->GetObjectGuid());
            break;
        case NPC_LORD_VICTOR_NEFARIUS:
            if (!pCreature->IsTemporarySummon())
                m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            break;
        case NPC_RAZORGORE:
        case NPC_NEFARIANS_TROOPS:
        case NPC_BLACKWING_ORB_TRIGGER:
        case NPC_VAELASTRASZ:
            m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            break;
    }
}

void instance_blackwing_lair::OnObjectCreate(GameObject* pGo)
{
    switch (pGo->GetEntry())
    {
        case GO_DOOR_RAZORGORE_ENTER:
        case GO_DOOR_RAZORGORE_EXIT:
        case GO_DOOR_VAELASTRASZ:
            SetEncounterDoorState(*pGo);
            break;
        case GO_DOOR_NEFARIAN:
            break;
        case GO_ORB_OF_DOMINATION:
            pGo->ApplyModFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT, !IsRazorgoreOrbAvailable());
            break;
        case GO_DOOR_CHROMAGGUS_EXIT:
            if (m_auiEncounter[TYPE_CHROMAGGUS] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_DOOR_LASHLAYER:
            if (m_auiEncounter[TYPE_LASHLAYER] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_BLACK_DRAGON_EGG:
            pGo->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            if (auto egg = m_razorgoreEggs.find(pGo->GetDbGuid()); egg != m_razorgoreEggs.end())
                egg->second = pGo->GetObjectGuid();
            return;
        case GO_DRAKONID_BONES:
            m_lDrakonidBonesGuids.push_back(pGo->GetObjectGuid());
            return;
        case GO_SUPPRESSION_DEVICE:
            // Do not spawn the Suppression Device GOs if Broodlord Lashlayer is dead
            if (GetData(TYPE_LASHLAYER) == DONE)
                pGo->SetLootState(GO_JUST_DEACTIVATED);
            return;

        default:
            return;
    }
    m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
}

void instance_blackwing_lair::SetEncounterDoorState(GameObject& door) const
{
    bool open;
    switch (door.GetEntry())
    {
        case GO_DOOR_RAZORGORE_ENTER:
            open = !IsEncounterInProgress();
            break;
        case GO_DOOR_RAZORGORE_EXIT:
            open = GetData(TYPE_RAZORGORE) == DONE && GetData(TYPE_VAELASTRASZ) != IN_PROGRESS;
            break;
        case GO_DOOR_VAELASTRASZ:
            open = GetData(TYPE_VAELASTRASZ) == DONE;
            break;
        default:
            return;
    }
    door.SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
}

void instance_blackwing_lair::UpdateEncounterDoor(uint32 entry)
{
    if (GameObject* door = GetSingleGameObjectFromStorage(entry))
        SetEncounterDoorState(*door);
}

void instance_blackwing_lair::SetData(uint32 uiType, uint32 uiData)
{
    const uint32 previousRazorgoreState = GetData(TYPE_RAZORGORE);
    if (uiType == TYPE_RAZORGORE &&
        (previousRazorgoreState == uiData || previousRazorgoreState == DONE ||
        (uiData == DONE && previousRazorgoreState != SPECIAL)))
        return;

    switch (uiType)
    {
        case TYPE_RAZORGORE:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
            {
                UpdateEncounterDoor(GO_DOOR_RAZORGORE_EXIT);
                if (Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true))
                    razorgore->SetActiveObjectState(false);
            }
            else if (uiData == IN_PROGRESS)
            {
                m_uiDefenseTimer = 40000;
                if (Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true))
                    razorgore->SetActiveObjectState(true);
            }
            else if (uiData == FAIL)
            {
                m_uiResetTimer = 30000;
                m_uiDefenseTimer = 0;
                m_razorgoreOrbUnlocked = false;
                DoToggleGameObjectFlags(GO_ORB_OF_DOMINATION, GO_FLAG_NO_INTERACT, true);

                if (Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true))
                {
                    razorgore->RemoveAurasDueToSpell(SPELL_POSSESS);
                    if (previousRazorgoreState != SPECIAL)
                    {
                        DoScriptText(SAY_RAZORGORE_DEATH, razorgore);
                        razorgore->CastSpell(nullptr, SPELL_FIREBALL, TRIGGERED_OLD_TRIGGERED);
                    }
                    razorgore->AI()->SetReactState(REACT_PASSIVE);
                    razorgore->CombatStop(true);
                    razorgore->GetMotionMaster()->Clear();
                    razorgore->ForcedDespawn(5000);
                }

                if (Creature* pOrb = GetSingleCreatureFromStorage(NPC_BLACKWING_ORB_TRIGGER))
                    if (pOrb->IsAlive())
                        pOrb->AI()->EnterEvadeMode();

                StopRazorgoreDefenders(false);
                for (ObjectGuid guid : m_razorgoreGuards)
                    if (Creature* guard = instance->GetCreature(guid))
                        guard->ForcedDespawn();
            }
            break;
        case TYPE_VAELASTRASZ:
            if (GetData(uiType) == uiData || GetData(uiType) == DONE)
                return;
            m_auiEncounter[uiType] = uiData;
            UpdateEncounterDoor(GO_DOOR_RAZORGORE_EXIT);
            UpdateEncounterDoor(GO_DOOR_VAELASTRASZ);
            break;
        case TYPE_LASHLAYER:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
                DoUseDoorOrButton(GO_DOOR_LASHLAYER);
            break;
        case TYPE_FIREMAW:
        case TYPE_EBONROC:
        case TYPE_FLAMEGOR:
            m_auiEncounter[uiType] = uiData;
            break;
        case TYPE_CHROMAGGUS:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
                DoUseDoorOrButton(GO_DOOR_CHROMAGGUS_EXIT);
            break;
        case TYPE_NEFARIAN:
            // If epic quest for Scepter of the Shifting Sands is in progress when Nefarian is defeated mark it as complete
            if (uiData == DONE)
            {
                UpdateScepterQuest();
                if (GetData(TYPE_QUEST_SCEPTER) == IN_PROGRESS)
                {
                    // Save quest success with the kill in the same snapshot.
                    m_auiEncounter[TYPE_QUEST_SCEPTER] = DONE;
                    m_scepterDeadline = 0;
                }
            }

            // Don't store the same thing twice
            if (m_auiEncounter[uiType] == uiData)
                break;

            if (uiData == SPECIAL)
            {
                m_auiEncounter[uiType] = uiData;
                m_uiNefarianSpawnTimer = 5 * IN_MILLISECONDS;   // End of phase 1: spawn Nefarian 5 seconds later

                // Remove drakonids spawners and Lord Victor Nefarius
                CleanupNefarianStage(false);
                break;
            }
            if (uiData == FAIL)
                CleanupNefarianStage(true); // Cleanup the drakonid bones, dead drakonids count and spawners
            m_auiEncounter[uiType] = uiData;
            DoUseDoorOrButton(GO_DOOR_NEFARIAN);
            break;
        case TYPE_QUEST_SCEPTER:
            if (uiData == IN_PROGRESS && GetData(uiType) != NOT_STARTED)
                return;
            m_auiEncounter[uiType] = uiData;
            if (uiData == IN_PROGRESS)
            {
                m_scepterDeadline = sWorld.GetGameTime() + ScepterQuestDuration;
                m_uiScepterQuestStep = 0;
            }
            else
                m_scepterDeadline = 0;
            break;
        case TYPE_CHROMA_LBREATH:
        case TYPE_CHROMA_RBREATH:
        case TYPE_NEFA_LTUNNEL:
        case TYPE_NEFA_RTUNNEL:
            m_auiEncounter[uiType] = uiData;    // Store the spell IDs/NPC entries of the two breaths/drakonids used by Chromaggus/Nefarian for all the instance lifetime. Breaths/Drakonids are picked randomly in Chromaggus/Nefarian script
            break;
    }

    if (uiType <= TYPE_NEFARIAN)
        UpdateEncounterDoor(GO_DOOR_RAZORGORE_ENTER);

    if (uiType == TYPE_VAELASTRASZ || uiType == TYPE_QUEST_SCEPTER ||
        (uiData >= DONE && (uiType != TYPE_RAZORGORE || uiData == DONE)))
        SaveEncounterData();
}

void instance_blackwing_lair::SaveEncounterData()
{
    OUT_SAVE_INST_DATA;

    // An interrupted Vaelastrasz fight resumes at the hostile retry.
    const uint32 vaelState = GetData(TYPE_VAELASTRASZ);
    std::ostringstream saveStream;
    saveStream << (GetData(TYPE_RAZORGORE) == DONE ? DONE : NOT_STARTED) << ' '
               << (vaelState == IN_PROGRESS ? FAIL : vaelState);
    for (uint32 i = TYPE_LASHLAYER; i < MAX_ENCOUNTER; ++i)
        saveStream << ' ' << m_auiEncounter[i];
    saveStream << ' ' << m_scepterDeadline;
    m_strInstData = saveStream.str();

    SaveToDB();
    OUT_SAVE_INST_DATA_COMPLETE;
}

void instance_blackwing_lair::Load(const char* chrIn)
{
    if (!chrIn)
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    OUT_LOAD_INST_DATA(chrIn);

    std::istringstream loadStream(chrIn);
    loadStream >> m_auiEncounter[0] >> m_auiEncounter[1] >> m_auiEncounter[2] >> m_auiEncounter[3]
               >> m_auiEncounter[4] >> m_auiEncounter[5] >> m_auiEncounter[6] >> m_auiEncounter[7]
               >> m_auiEncounter[8] >> m_auiEncounter[9] >> m_auiEncounter[10]>> m_auiEncounter[11]
               >> m_auiEncounter[12];

    if (GetData(TYPE_VAELASTRASZ) == IN_PROGRESS)
        m_auiEncounter[TYPE_VAELASTRASZ] = FAIL;

    // The in-memory phase transition timer is unavailable after a reload.
    if (GetData(TYPE_NEFARIAN) == SPECIAL)
        m_auiEncounter[TYPE_NEFARIAN] = NOT_STARTED;

    for (uint32 boss = TYPE_RAZORGORE; boss <= TYPE_NEFARIAN; ++boss)
    {
        if (GetData(boss) == IN_PROGRESS)
            m_auiEncounter[boss] = NOT_STARTED;
    }

    // Legacy SPECIAL is safe to resume as the ready-to-talk state.
    if (GetData(TYPE_RAZORGORE) != DONE && GetData(TYPE_VAELASTRASZ) != DONE)
        m_auiEncounter[TYPE_VAELASTRASZ] = NOT_STARTED;

    if (GetData(TYPE_QUEST_SCEPTER) == IN_PROGRESS)
    {
        loadStream >> m_scepterDeadline;
        const time_t now = sWorld.GetGameTime();
        while (m_uiScepterQuestStep < std::size(ScepterWarningOffsets) - 1 && m_scepterDeadline &&
               now >= m_scepterDeadline - ScepterWarningOffsets[m_uiScepterQuestStep])
            ++m_uiScepterQuestStep;
    }

    if (GetData(TYPE_RAZORGORE) != DONE)
    {
        m_auiEncounter[TYPE_RAZORGORE] = NOT_STARTED;
        if (auto* state = sMapPersistentStateMgr.GetPersistentState(instance->GetId(), instance->GetInstanceId()))
        {
            // Load runs before Map attaches the persistent state or loads spawn grids.
            state->SetUsedByMapState(instance);
            ResetRazorgoreEggs(*state);

            // A restart can preserve respawn delays from an interrupted attempt.
            auto resetGuards = [this, state](CreatureDataPair const& spawn)
            {
                if (spawn.second.mapid == instance->GetId() &&
                    (spawn.second.id == NPC_RAZORGORE || spawn.second.id == NPC_GRETHOK_CONTROLLER || spawn.second.id == NPC_BLACKWING_GUARDSMAN) &&
                    state->GetCreatureRespawnTime(spawn.first))
                    state->SaveCreatureRespawnTime(spawn.first, 0);
                return false;
            };
            sObjectMgr.DoCreatureData(resetGuards);
        }
    }

    OUT_LOAD_INST_DATA_COMPLETE;
}

uint32 instance_blackwing_lair::GetData(uint32 uiType) const
{
    if (uiType < MAX_ENCOUNTER)
        return m_auiEncounter[uiType];

    return 0;
}

bool instance_blackwing_lair::IsRazorgoreOrbAvailable() const
{
    return GetData(TYPE_RAZORGORE) == IN_PROGRESS && m_razorgoreOrbUnlocked;
}

bool instance_blackwing_lair::CanDestroyRazorgoreEgg(Unit const& caster, GameObject const& egg) const
{
    if (!IsRazorgoreOrbAvailable() || !caster.IsCreature() || !caster.IsAlive() || caster.GetEntry() != NPC_RAZORGORE ||
        !caster.HasCharmer() || !caster.HasAura(SPELL_POSSESS) ||
        egg.GetEntry() != GO_BLACK_DRAGON_EGG || !egg.IsSpawned())
        return false;

    auto spawn = m_razorgoreEggs.find(egg.GetDbGuid());
    return spawn != m_razorgoreEggs.end() && spawn->second == egg.GetObjectGuid() &&
        m_destroyedRazorgoreEggs.find(spawn->first) == m_destroyedRazorgoreEggs.end();
}

bool instance_blackwing_lair::RazorgoreEggDestroyed(Unit& caster, GameObject& egg)
{
    if (!CanDestroyRazorgoreEgg(caster, egg) || egg.GetLootState() != GO_ACTIVATED)
        return false;

    m_destroyedRazorgoreEggs.insert(egg.GetDbGuid());
    if (m_destroyedRazorgoreEggs.size() == m_razorgoreEggs.size())
        FinishRazorgoreEggPhase(static_cast<Creature&>(caster));
    return true;
}

void instance_blackwing_lair::FinishRazorgoreEggPhase(Creature& razorgore)
{
    Unit const* charmer = razorgore.GetCharmer();
    ObjectGuid const controller = charmer ? charmer->GetObjectGuid() : ObjectGuid();
    SetData(TYPE_RAZORGORE, SPECIAL);
    m_uiDefenseTimer = 0;
    m_razorgoreOrbUnlocked = false;
    DoToggleGameObjectFlags(GO_ORB_OF_DOMINATION, GO_FLAG_NO_INTERACT, true);

    if (Creature* troops = GetSingleCreatureFromStorage(NPC_NEFARIANS_TROOPS))
    {
        DoScriptText(EMOTE_ORB_SHUT_OFF, troops);
        DoScriptText(EMOTE_TROOPS_FLEE, troops);
    }

    StopRazorgoreDefenders(true);
    razorgore.RemoveAurasDueToSpell(SPELL_POSSESS);
    razorgore.AI()->SendAIEvent(AI_EVENT_CUSTOM_A, &razorgore, &razorgore);
    EngageRazorgore(razorgore, controller);
}

void instance_blackwing_lair::EngageRazorgore(Creature& razorgore, ObjectGuid controller)
{
    razorgore.AttackStop(true, true);
    razorgore.DeleteThreatList();
    razorgore.AI()->SetCombatMovement(true);
    if (Player* player = instance->GetPlayer(controller); player && player->IsAlive() && razorgore.CanAttack(player))
    {
        razorgore.AI()->AttackStart(player);
        razorgore.AddThreat(player, 1000000.0f);
    }
    razorgore.SetInCombatWithZone(true);
}

void instance_blackwing_lair::OnRazorgorePossessionEnded(Creature& razorgore, ObjectGuid controller)
{
    // The last egg and a wipe each own their own transition.
    if (!IsRazorgoreOrbAvailable() || !razorgore.IsAlive() || razorgore.HasCharmer())
        return;

    for (ObjectGuid guid : m_razorgoreDefenders)
        if (Creature* defender = instance->GetCreature(guid))
            defender->getThreatManager().modifyThreatPercent(&razorgore, -100);

    EngageRazorgore(razorgore, controller);
}

void instance_blackwing_lair::UpdateRazorgoreDefenderThreat()
{
    Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true);
    if (!razorgore || !razorgore->HasAura(SPELL_POSSESS))
        return;

    Unit* controller = razorgore->GetCharmer();
    if (!controller || !controller->IsPlayer() || !controller->IsAlive())
        return;

    for (ObjectGuid guid : m_razorgoreDefenders)
    {
        Creature* defender = instance->GetCreature(guid);
        if (!defender || !defender->CanAttack(controller))
            continue;

        ThreatManager& threat = defender->getThreatManager();
        float const razorgoreThreat = threat.getThreat(razorgore, true);
        if (razorgoreThreat > 0.0f && threat.getThreat(controller, true) != razorgoreThreat)
        {
            // Mirror only the controller's entry. Tanks retain their earned threat.
            threat.modifyThreatPercent(controller, -100);
            threat.addThreatDirectly(controller, razorgoreThreat, false);
        }
    }
}

void instance_blackwing_lair::StopRazorgoreDefenders(bool retreat)
{
    GuidSet defenders;
    defenders.swap(m_razorgoreDefenders);
    m_uiBlackwingDefCount = 0;
    m_uiDragonspawnCount = 0;

    for (ObjectGuid guid : defenders)
    {
        Creature* defender = instance->GetCreature(guid);
        if (!defender || !defender->IsAlive())
            continue;

        defender->AI()->SetReactState(REACT_PASSIVE);
        defender->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED | UNIT_FLAG_SILENCED |
            UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        defender->CombatStop(true);
        // Cancel delayed spell events as well as casts still in progress.
        defender->m_events.KillAllEvents(false);
        if (retreat)
        {
            defender->GetMotionMaster()->Clear();
            defender->GetMotionMaster()->MovePoint(0, -7555.55f, -1025.16f, 408.4914f);
        }
        defender->ForcedDespawn(retreat ? 10000 : 0);
    }
}

void instance_blackwing_lair::RemoveRazorgoreDefender(Creature const& creature)
{
    if (!m_razorgoreDefenders.erase(creature.GetObjectGuid()))
        return;

    if (creature.GetEntry() == NPC_DRAGONSPAWN)
        --m_uiDragonspawnCount;
    else
        --m_uiBlackwingDefCount;
}

void instance_blackwing_lair::ResetRazorgoreEggs(MapPersistentState& state)
{
    m_destroyedRazorgoreEggs.clear();
    for (auto const& [dbGuid, guid] : m_razorgoreEggs)
    {
        if (state.GetGORespawnTime(dbGuid))
            state.SaveGORespawnTime(dbGuid, 0);
        if (GameObject* egg = instance->GetGameObject(guid))
        {
            if (!egg->IsSpawned())
                egg->Respawn();
            else if (egg->GetLootState() != GO_READY)
            {
                egg->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
                egg->SetGoState(GO_STATE_READY);
                egg->SetLootState(GO_READY);
            }
        }
    }
}

void instance_blackwing_lair::ResetRazorgore()
{
    ResetRazorgoreEggs(*instance->GetPersistentState());
    if (Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true))
    {
        if (!razorgore->IsAlive())
            razorgore->Respawn();
        razorgore->SetActiveObjectState(false);
    }
    for (ObjectGuid guid : m_razorgoreGuards)
        if (Creature* guard = instance->GetCreature(guid); guard && !guard->IsAlive())
            guard->Respawn();

    SetData(TYPE_RAZORGORE, NOT_STARTED);
}

void instance_blackwing_lair::OnCreatureEnterCombat(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_GRETHOK_CONTROLLER && GetData(TYPE_RAZORGORE) == NOT_STARTED)
        SetData(TYPE_RAZORGORE, IN_PROGRESS);
}

bool instance_blackwing_lair::OnCreatureLethalDamage(Creature& creature)
{
    if (creature.GetEntry() != NPC_RAZORGORE || GetData(TYPE_RAZORGORE) == SPECIAL)
        return false;

    SetData(TYPE_RAZORGORE, FAIL);
    return true;
}

void instance_blackwing_lair::OnCreatureDeath(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        case NPC_GRETHOK_CONTROLLER:
            if (GetData(TYPE_RAZORGORE) == NOT_STARTED)
                SetData(TYPE_RAZORGORE, IN_PROGRESS);
            if (GetData(TYPE_RAZORGORE) == IN_PROGRESS)
            {
                m_razorgoreOrbUnlocked = true;
                DoToggleGameObjectFlags(GO_ORB_OF_DOMINATION, GO_FLAG_NO_INTERACT, false);
            }

            if (Creature* pOrbTrigger = GetSingleCreatureFromStorage(NPC_BLACKWING_ORB_TRIGGER))
                pOrbTrigger->InterruptNonMeleeSpells(false);
            break;
        case NPC_RAZORGORE:
            // Only set the event as done if Razorgore dies in last phase
            if (GetData(TYPE_RAZORGORE) == SPECIAL)
            {
                SetData(TYPE_RAZORGORE, DONE);
                break;
            }
            break;
        case NPC_BLACKWING_LEGIONNAIRE:
        case NPC_BLACKWING_MAGE:
        case NPC_DRAGONSPAWN:
            RemoveRazorgoreDefender(*pCreature);
            break;
        case NPC_BLUE_DRAKONID:
        case NPC_GREEN_DRAKONID:
        case NPC_BRONZE_DRAKONID:
        case NPC_RED_DRAKONID:
        case NPC_BLACK_DRAKONID:
        case NPC_CHROMATIC_DRAKONID:
            m_uiDeadDrakonidsCount++;
            // If the requiered amount of drakonids are killed, start phase 2
            if (m_uiDeadDrakonidsCount >= MAX_DRAKONID_SUMMONS && GetData(TYPE_NEFARIAN) == IN_PROGRESS)
                SetData(TYPE_NEFARIAN, SPECIAL);
            break;
    }
}

void instance_blackwing_lair::OnCreatureRespawn(Creature* creature)
{
    if (creature->GetSpawnerGuid().GetEntry() == NPC_MONSTER_GENERATOR)
    {
        if (GetData(TYPE_RAZORGORE) != IN_PROGRESS)
        {
            creature->ForcedDespawn(1);
            return;
        }

        if (m_razorgoreDefenders.insert(creature->GetObjectGuid()).second)
        {
            if (creature->GetEntry() == NPC_DRAGONSPAWN)
                ++m_uiDragonspawnCount;
            else
                ++m_uiBlackwingDefCount;
        }
        creature->SetNoLoot(true);
        creature->SetCorpseDelay(5);
        creature->SetInCombatWithZone();
        if (Creature* razorgore = GetSingleCreatureFromStorage(NPC_RAZORGORE, true))
        {
            creature->AI()->AttackStart(razorgore);
            creature->AddThreat(razorgore, 10.0f);
        }
        return;
    }

    switch (creature->GetEntry())
    {
        case NPC_VAELASTRASZ:
            // The saved kill also covers spawns with a missing or expired respawn timestamp.
            if (creature->IsAlive() && GetData(TYPE_VAELASTRASZ) == DONE)
                creature->ForcedDespawn(1, true);
            break;
        case NPC_GRETHOK_CONTROLLER:
            m_razorgoreOrbUnlocked = false;
            DoToggleGameObjectFlags(GO_ORB_OF_DOMINATION, GO_FLAG_NO_INTERACT, true);
            break;
        case NPC_BLACKWING_LEGIONNAIRE:
        case NPC_BLACKWING_MAGE:
        case NPC_DRAGONSPAWN:
        case NPC_BLACK_DRAKONID:
        case NPC_RED_DRAKONID:
        case NPC_GREEN_DRAKONID:
        case NPC_BLUE_DRAKONID:
        case NPC_BRONZE_DRAKONID:
        case NPC_CHROMATIC_DRAKONID:
            creature->SetInCombatWithZone();
            break;
    }
}

void instance_blackwing_lair::OnCreatureDespawn(Creature* creature)
{
    RemoveRazorgoreDefender(*creature);
}

bool instance_blackwing_lair::CheckConditionCriteriaMeet(Player const* pPlayer, uint32 uiInstanceConditionId, WorldObject const* pConditionSource, uint32 conditionSourceType) const
{
    switch (uiInstanceConditionId)
    {
        case INSTANCE_CONDITION_ID_HARD_MODE:               // Event failed
            return (GetData(TYPE_QUEST_SCEPTER) == FAIL);
        case INSTANCE_CONDITION_ID_HARD_MODE_2:             // Event succeeded
            return (GetData(TYPE_QUEST_SCEPTER) == DONE);
    }

    script_error_log("instance_blackwing_lair::CheckConditionCriteriaMeet called with unsupported Id %u. Called with param plr %s, src %s, condition source type %u",
                     uiInstanceConditionId, pPlayer ? pPlayer->GetGuidStr().c_str() : "NULL", pConditionSource ? pConditionSource->GetGuidStr().c_str() : "NULL", conditionSourceType);
    return false;
}

void instance_blackwing_lair::UpdateScepterQuest()
{
    if (GetData(TYPE_QUEST_SCEPTER) != IN_PROGRESS)
        return;

    const time_t now = sWorld.GetGameTime();
    if (!m_scepterDeadline)
    {
        // Legacy saves carried the quest state but had no timestamp to recover.
        m_scepterDeadline = now + ScepterQuestDuration;
        SaveEncounterData();
    }

    if (now >= m_scepterDeadline)
    {
        SetData(TYPE_QUEST_SCEPTER, FAIL);
        if (GetData(TYPE_NEFARIAN) == NOT_STARTED)
        {
            DoOrSimulateScriptTextForThisInstance(EMOTE_REDSHARD_TAUNT_2, NPC_LORD_VICTOR_NEFARIUS);
            DoOrSimulateScriptTextForThisInstance(YELL_REDSHARD_TAUNT_7, NPC_LORD_VICTOR_NEFARIUS);
        }
        return;
    }

    if (now < m_scepterDeadline - ScepterWarningOffsets[m_uiScepterQuestStep])
        return;

    switch (m_uiScepterQuestStep++)
    {
        case 0:
            DoOrSimulateScriptTextForThisInstance(YELL_REDSHARD_TAUNT_1, NPC_LORD_VICTOR_NEFARIUS);
            break;
        case 1:
            if (urand(0, 1))
                DoOrSimulateScriptTextForThisInstance(YELL_REDSHARD_TAUNT_3, NPC_LORD_VICTOR_NEFARIUS);
            else
            {
                DoOrSimulateScriptTextForThisInstance(YELL_REDSHARD_TAUNT_2, NPC_LORD_VICTOR_NEFARIUS);
                DoOrSimulateScriptTextForThisInstance(EMOTE_REDSHARD_TAUNT_1, NPC_LORD_VICTOR_NEFARIUS);
            }
            break;
        case 2:
            DoOrSimulateScriptTextForThisInstance(urand(0, 1) ? YELL_REDSHARD_TAUNT_5 : YELL_REDSHARD_TAUNT_4, NPC_LORD_VICTOR_NEFARIUS);
            break;
        case 3:
            DoOrSimulateScriptTextForThisInstance(YELL_REDSHARD_TAUNT_6, NPC_LORD_VICTOR_NEFARIUS);
            break;
    }
}

void instance_blackwing_lair::Update(uint32 uiDiff)
{
    UpdateScepterQuest();
    // Spawn Nefarian
    if (m_uiNefarianSpawnTimer)
    {
        if (m_uiNefarianSpawnTimer <= uiDiff)
        {
            if (Creature* nefarius = GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS))
            {
                nefarius->SummonCreature(NPC_NEFARIAN, aNefarianLocs[2].x, aNefarianLocs[2].y, aNefarianLocs[2].z, 0, TEMPSPAWN_DEAD_DESPAWN, 0, true);
                // Despawn Nefarius
                if (!nefarius->IsDespawned() && GetData(TYPE_NEFARIAN) == SPECIAL)
                {
                    nefarius->CastSpell(nullptr, SPELL_SHADOWBLINK_OUTRO, TRIGGERED_OLD_TRIGGERED);
                    nefarius->AI()->SetCombatScriptStatus(true);
                    nefarius->ForcedDespawn(2500);
                }
            }
            m_uiNefarianSpawnTimer = 0;
        }
        else
            m_uiNefarianSpawnTimer -= uiDiff;
    }

    // Reset Razorgore in case of wipe
    if (m_uiResetTimer)
    {
        if (m_uiResetTimer <= uiDiff)
        {
            m_uiResetTimer = 0;
            ResetRazorgore();
        }
        else
            m_uiResetTimer -= uiDiff;
    }

    uint32 const razorgoreState = GetData(TYPE_RAZORGORE);
    if ((razorgoreState == IN_PROGRESS || razorgoreState == SPECIAL) && !GetPlayerInMap(true, false))
    {
        SetData(TYPE_RAZORGORE, FAIL);
        return;
    }

    if (razorgoreState != IN_PROGRESS)
        return;

    if (m_uiDefenseTimer <= uiDiff)
    {
        // Randomize generators
        std::shuffle(m_vGeneratorGuids.begin(), m_vGeneratorGuids.end(), *GetRandomGenerator());

        // Spawn the defenders
        std::size_t const generators = std::min(m_vGeneratorGuids.size(), std::size_t(MAX_EGGS_DEFENDERS));
        for (std::size_t i = 0; i < generators; ++i)
        {
            bool const canSpawnOrc = m_uiBlackwingDefCount < MAX_BLACKWING_DEFENDER;
            bool const canSpawnDragon = m_uiDragonspawnCount < MAX_DRAGONSPAWN;
            if (!canSpawnOrc && !canSpawnDragon)
                break;

            Creature* pGenerator = instance->GetCreature(m_vGeneratorGuids[i]);
            if (!pGenerator)
                continue;

            uint32 const roll = urand(canSpawnOrc ? 0 : 2, canSpawnDragon ? 3 : 1);
            uint32 const summonSpells[] = {SPELL_SUMMON_LEGIONNAIRE, SPELL_SUMMON_MAGE, SPELL_SUMMON_DRAGONSPAWN, SPELL_SUMMON_DRAGONSPAWN};
            pGenerator->CastSpell(nullptr, summonSpells[roll], TRIGGERED_NONE);
        }

        m_uiDefenseTimer = 15000;
    }
    else
        m_uiDefenseTimer -= uiDiff;

    UpdateRazorgoreDefenderThreat();
}

void instance_blackwing_lair::InitiateBreath(uint32 uiEventId)
{
    uint32 leftBreath = 0;
    uint32 rightBreath = 0;
    switch (uiEventId)
    {
        // Left Chromaggus breath
        case 8446: leftBreath = 23187; break;  // Frost Burn
        case 8447: leftBreath = 23308; break;  // Incinerate
        case 8448: leftBreath = 23310; break;  // Time Lapse
        case 8449: leftBreath = 23313; break;  // Corrosive Acid
        case 8450: leftBreath = 23315; break;  // Ignite Flesh
        // Right Chromaggus breath
        case 8451: rightBreath = 23189; break;  // Frost Burn
        case 8452: rightBreath = 23309; break;  // Incinerate
        case 8453: rightBreath = 23312; break;  // Time Lapse
        case 8454: rightBreath = 23314; break;  // Corrosive Acid
        case 8455: rightBreath = 23316; break;  // Ignite Flesh
    }
    if (leftBreath)
    {
        debug_log("SD2 Instance Blackwing Lair: initiating Chromaggus' left breath");
        if (GetData(TYPE_CHROMA_LBREATH) == NOT_STARTED)
            SetData(TYPE_CHROMA_LBREATH, leftBreath);
        debug_log("SD2 Instance Blackwing Lair: Chromaggus' left breath set to spell %u", GetData(TYPE_CHROMA_LBREATH));
    }
    if (rightBreath)
    {
        debug_log("SD2 Instance Blackwing Lair: initiating Chromaggus' right breath");
        if (GetData(TYPE_CHROMA_RBREATH) == NOT_STARTED)
            SetData(TYPE_CHROMA_RBREATH, rightBreath);
        debug_log("SD2 Instance Blackwing Lair: Chromaggus' right breath set to spell %u", GetData(TYPE_CHROMA_RBREATH));
    }
}

void instance_blackwing_lair::InitiateDrakonid(uint32 uiEventId)
{
    uint32 leftTunnel = 0;
    uint32 rightTunnel = 0;
    switch (uiEventId)
    {
        // Left tunnel
        case 8520: leftTunnel = NPC_BLACK_SPAWNER; break;
        case 8521: leftTunnel = NPC_RED_SPAWNER; break;
        case 8522: leftTunnel = NPC_GREEN_SPAWNER; break;
        case 8523: leftTunnel = NPC_BRONZE_SPAWNER; break;
        case 8524: leftTunnel = NPC_BLUE_SPAWNER; break;
        // Right tunnel
        case 8525: rightTunnel = NPC_BLACK_SPAWNER; break;
        case 8526: rightTunnel = NPC_RED_SPAWNER; break;
        case 8527: rightTunnel = NPC_GREEN_SPAWNER; break;
        case 8528: rightTunnel = NPC_BRONZE_SPAWNER; break;
        case 8529: rightTunnel = NPC_BLUE_SPAWNER; break;
    }
    if (leftTunnel)
    {
        debug_log("SD2 Instance Blackwing Lair: initiating drakonid for left tunnel in Nefarian's lair");
        if (GetData(TYPE_NEFA_LTUNNEL) == NOT_STARTED)
            SetData(TYPE_NEFA_LTUNNEL, leftTunnel);
        debug_log("SD2 Instance Blackwing Lair: Nefarian's lair left tunnel set with drakonid spawner %u", GetData(TYPE_NEFA_LTUNNEL));
    }
    if (rightTunnel)
    {
        debug_log("SD2 Instance Blackwing Lair: initiating drakonid for right tunnel in Nefarian's lair");
        if (GetData(TYPE_NEFA_RTUNNEL) == NOT_STARTED)
            SetData(TYPE_NEFA_RTUNNEL, rightTunnel);
        debug_log("SD2 Instance Blackwing Lair: Nefarian's lair left tunnel set with drakonid spawner %u", GetData(TYPE_NEFA_RTUNNEL));
    }
}

void instance_blackwing_lair::CleanupNefarianStage(bool fullCleanup)
{
    for (auto guid : m_lDrakonidSpawnerGuids)
        if (Creature* temp = instance->GetCreature(guid))
            temp->ForcedDespawn();

    m_lDrakonidSpawnerGuids.clear();

    // Stop the cleanup here if we are only moving from P1 to P2
    if (!fullCleanup)
        return;

    m_uiDeadDrakonidsCount = 0;

    for (auto guid : m_drakonids)
        if (Creature* temp = instance->GetCreature(guid))
            temp->ForcedDespawn();

    m_drakonids.clear();

    for (GuidList::const_iterator itr = m_lDrakonidBonesGuids.begin(); itr != m_lDrakonidBonesGuids.end(); ++itr)
    {
        if (GameObject* pGo = instance->GetGameObject(*itr))
            pGo->SetLootState(GO_JUST_DEACTIVATED);
    }
    m_lDrakonidBonesGuids.clear();

    if (Creature* nefarius = GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS))
    {
        // Respawn Nefarius if wipe in Phase 2
        if (nefarius->IsDespawned())
            nefarius->Respawn();
    }
}

InstanceData* GetInstanceData_instance_blackwing_lair(Map* pMap)
{
    return new instance_blackwing_lair(pMap);
}

/*###############
## go_suppression
################*/

struct go_ai_suppression : public GameObjectAI
{
    go_ai_suppression(GameObject* go) : GameObjectAI(go), m_uiFumeTimer(urand(0, 5 * IN_MILLISECONDS)) {}

    uint32 m_uiFumeTimer;

    void OnLootStateChange(Unit* /*user*/) override
    {
        ScriptedInstance* pInstance = (ScriptedInstance*)m_go->GetMap()->GetInstanceData();
        if (!pInstance)
            return;

        // As long as Broodlord Lashlayer is alive, the GO will rearm on a random timer from 30 sec to 2 min
        // It will not rearm for the instance lifetime after Broodlord Lashlayer death
        if (m_go->GetLootState() == GO_ACTIVATED)
        {
            if (pInstance->GetData(TYPE_LASHLAYER) != DONE)
                m_go->SetRespawnTime(urand(30, 2 * MINUTE));
            else
                m_go->SetRespawnTime(7 * 24 * HOUR);
        }
    }

    // Visual effects for each GO is played on a 5 seconds timer. Sniff show that the GO should also be used (trap spell is cast)
    // but we need core support for GO casting for that
    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_uiFumeTimer)
        {
            if (m_uiFumeTimer <= uiDiff)
            {
                // The loot state check may be removed in that case because it should probably be handled in the Gameobject::Use() code
                if (m_go->GetLootState() == GO_READY)
                {
                    m_go->SendGameObjectCustomAnim(m_go->GetObjectGuid());
                    m_go->CastSpell(nullptr, nullptr, m_go->GetGOInfo()->trap.spellId, TRIGGERED_OLD_TRIGGERED);
                }
                m_uiFumeTimer = 5 * IN_MILLISECONDS;
            }
            else
                m_uiFumeTimer -= uiDiff;
        }
    }
};


GameObjectAI* GetAI_go_suppression(GameObject* go)
{
    return new go_ai_suppression(go);
}

/*###################################################
## Chromaggus' breaths and Nefarian tunnels selection
####################################################*/

bool ProcessEventId_event_weekly_chromatic_selection(uint32 uiEventId, Object* pSource, Object* /*pTarget*/, bool /*bIsStart*/)
{
    if (pSource->GetTypeId() == TYPEID_UNIT)
    {
        if (instance_blackwing_lair* pInstance = (instance_blackwing_lair*)((Creature*)pSource)->GetInstanceData())
        {
            switch (uiEventId)
            {
                // Left Chromaggus breath
                case 8446:
                case 8447:
                case 8448:
                case 8449:
                case 8450:
                // Right Chromaggus breath
                case 8451:
                case 8452:
                case 8453:
                case 8454:
                case 8455:
                    pInstance->InitiateBreath(uiEventId);
                    break;
                // Left tunnel in Nefarian's lair
                case 8520:
                case 8521:
                case 8522:
                case 8523:
                case 8524:
                // Right tunnel in Nefarian's lair
                case 8525:
                case 8526:
                case 8527:
                case 8528:
                case 8529:
                    pInstance->InitiateDrakonid(uiEventId);
                    break;
            }
        }
    }
    return false;
}

void AddSC_instance_blackwing_lair()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_blackwing_lair";
    pNewScript->GetInstanceData = &GetInstanceData_instance_blackwing_lair;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_suppression";
    pNewScript->GetGameObjectAI = &GetAI_go_suppression;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "event_weekly_chromatic_selection";
    pNewScript->pProcessEventId = &ProcessEventId_event_weekly_chromatic_selection;
    pNewScript->RegisterSelf();
}
