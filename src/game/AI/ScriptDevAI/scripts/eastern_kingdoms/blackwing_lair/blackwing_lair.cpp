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
#include "Spells/Scripts/SpellScript.h"
#include "World/World.h"

namespace
{
constexpr time_t ScepterQuestDuration = 5 * HOUR;
constexpr time_t ScepterWarningOffsets[] = { ScepterQuestDuration - 2, 3 * HOUR, HOUR, 30 * MINUTE, 0 };
constexpr uint32 NefarianTransitionDelay = 5 * IN_MILLISECONDS;
constexpr uint32 NefarianLandingTimeout = MINUTE * IN_MILLISECONDS;
constexpr uint32 VictorOutroDelay = 2500;

void DisableSuppressionDevice(GameObject& device)
{
    device.SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    device.SetGoState(GO_STATE_ACTIVE_ALTERNATIVE);
    // Exit GO_ACTIVATED so the core no longer processes a pending trap rearm.
    device.SetLootState(GO_READY);
}
}

instance_blackwing_lair::instance_blackwing_lair(Map* pMap) : ScriptedInstance(pMap),
    m_uiResetTimer(0),
    m_uiDefenseTimer(0),
    m_uiScepterQuestStep(0),
    m_uiDragonspawnCount(0),
    m_uiBlackwingDefCount(0)
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
            if (m_nefarianPhase == NefarianPhase::Intro &&
                pCreature->GetSpawnerGuid().GetEntry() == NPC_LORD_VICTOR_NEFARIUS)
                m_drakonidSpawners.insert(pCreature->GetObjectGuid());
            break;
        case NPC_BLACK_DRAKONID:
        case NPC_RED_DRAKONID:
        case NPC_GREEN_DRAKONID:
        case NPC_BLUE_DRAKONID:
        case NPC_BRONZE_DRAKONID:
        case NPC_CHROMATIC_DRAKONID:
            if (m_drakonidSpawners.count(pCreature->GetSpawnerGuid()))
                m_drakonids.insert(pCreature->GetObjectGuid());
            break;
        case NPC_BONE_CONSTRUCT:
        case NPC_CORRUPTED_FIRE_NOVA_TOTEM:
        case NPC_CORRUPTED_STONESKIN_TOTEM:
        case NPC_CORRUPTED_HEALING_TOTEM:
        case NPC_CORRUPTED_WINDFURY_TOTEM:
        case NPC_CORRUPTED_INFERNAL:
            if (m_nefarianPhase == NefarianPhase::Dragon && pCreature->IsTemporarySummon())
                m_nefarianAdds.insert(pCreature->GetObjectGuid());
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
        case GO_DOOR_LASHLAYER:
        case GO_DOOR_NEFARIAN:
            SetEncounterDoorState(*pGo);
            break;
        case GO_ORB_OF_DOMINATION:
            pGo->ApplyModFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT, !IsRazorgoreOrbAvailable());
            break;
        case GO_DOOR_CHROMAGGUS_EXIT:
            if (m_auiEncounter[TYPE_CHROMAGGUS] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_BLACK_DRAGON_EGG:
            pGo->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            if (auto egg = m_razorgoreEggs.find(pGo->GetDbGuid()); egg != m_razorgoreEggs.end())
                egg->second = pGo->GetObjectGuid();
            return;
        case GO_DRAKONID_BONES:
            if (m_nefarianPhase != NefarianPhase::Idle)
                m_drakonidBones.insert(pGo->GetObjectGuid());
            return;
        default:
            return;
    }
    m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
}

void instance_blackwing_lair::OnObjectSpawn(GameObject* go)
{
    if (go->GetEntry() != GO_SUPPRESSION_DEVICE)
        return;

    if (GetData(TYPE_LASHLAYER) == DONE)
        DisableSuppressionDevice(*go);
    else
        m_suppressionDevices.insert(go->GetObjectGuid());
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
        case GO_DOOR_LASHLAYER:
            open = GetData(TYPE_LASHLAYER) == DONE;
            break;
        case GO_DOOR_NEFARIAN:
            open = m_nefarianPhase == NefarianPhase::Idle;
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
            if (GetData(TYPE_LASHLAYER) == DONE)
                return;

            m_auiEncounter[uiType] = uiData;
            UpdateEncounterDoor(GO_DOOR_LASHLAYER);
            if (uiData == DONE)
            {
                for (ObjectGuid guid : m_suppressionDevices)
                    if (GameObject* device = instance->GetGameObject(guid))
                        DisableSuppressionDevice(*device);
                m_suppressionDevices.clear();
            }
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
            if (GetData(uiType) == uiData || GetData(uiType) == DONE)
                return;

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

            m_auiEncounter[uiType] = uiData;
            if (uiData == FAIL || uiData == DONE)
                CleanupNefarianEncounter(uiData == FAIL);
            UpdateEncounterDoor(GO_DOOR_NEFARIAN);
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
            if (m_nefarianPhase == NefarianPhase::Drakonids && m_drakonids.count(pCreature->GetObjectGuid()) &&
                m_deadDrakonids.insert(pCreature->GetObjectGuid()).second && m_deadDrakonids.size() == MAX_DRAKONID_SUMMONS)
            {
                m_nefarianPhase = NefarianPhase::Transition;
                m_nefarianPhaseTimer = NefarianTransitionDelay;
                SetData(TYPE_NEFARIAN, SPECIAL);
                DespawnNefarianCreatures(m_drakonidSpawners);
            }
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
        case NPC_LORD_VICTOR_NEFARIUS:
            if (!creature->IsTemporarySummon() && GetData(TYPE_NEFARIAN) == DONE)
                creature->ForcedDespawn(1, true);
            break;
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
    ObjectGuid const guid = creature->GetObjectGuid();
    m_drakonidSpawners.erase(guid);
    m_drakonids.erase(guid);
    m_nefarianAdds.erase(guid);
    if (guid == m_nefarianGuid)
    {
        m_nefarianGuid.Clear();
        SetData(TYPE_NEFARIAN, FAIL);
    }
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
    UpdateNefarianEncounter(uiDiff);

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

void instance_blackwing_lair::SelectChromaggusBreaths()
{
    // Frost Burn, Incinerate, Time Lapse, Corrosive Acid, Ignite Flesh.
    constexpr std::array<uint32, 5> leftSpells = {23187, 23308, 23310, 23313, 23315};
    constexpr std::array<uint32, 5> rightSpells = {23189, 23309, 23312, 23314, 23316};
    constexpr uint32 breathCount = leftSpells.size();
    uint32& leftSpell = m_auiEncounter[TYPE_CHROMA_LBREATH];
    uint32& rightSpell = m_auiEncounter[TYPE_CHROMA_RBREATH];
    uint32 left = std::find(leftSpells.begin(), leftSpells.end(), leftSpell) - leftSpells.begin();
    uint32 right = std::find(rightSpells.begin(), rightSpells.end(), rightSpell) - rightSpells.begin();
    if (left < breathCount && right < breathCount && left != right)
        return;

    // Keep a valid slot from an older partial save. For a duplicate, keep the left slot.
    if (left == breathCount)
        left = right == breathCount ? urand(0, breathCount - 1) : (right + urand(1, breathCount - 1)) % breathCount;
    if (right == breathCount || right == left)
        right = (left + urand(1, breathCount - 1)) % breathCount;

    leftSpell = leftSpells[left];
    rightSpell = rightSpells[right];
    SaveEncounterData();
}

void instance_blackwing_lair::SelectNefarianTunnels()
{
    constexpr std::array<uint32, 5> spawners = {NPC_BLACK_SPAWNER, NPC_RED_SPAWNER, NPC_GREEN_SPAWNER, NPC_BRONZE_SPAWNER, NPC_BLUE_SPAWNER};
    constexpr uint32 colorCount = spawners.size();
    uint32& leftSpawner = m_auiEncounter[TYPE_NEFA_LTUNNEL];
    uint32& rightSpawner = m_auiEncounter[TYPE_NEFA_RTUNNEL];
    uint32 left = std::find(spawners.begin(), spawners.end(), leftSpawner) - spawners.begin();
    uint32 right = std::find(spawners.begin(), spawners.end(), rightSpawner) - spawners.begin();
    if (left < colorCount && right < colorCount && left != right)
        return;

    // Preserve a valid color in a partial save and the left color in a duplicate pair.
    if (left == colorCount)
        left = right == colorCount ? urand(0, colorCount - 1) : (right + urand(1, colorCount - 1)) % colorCount;
    if (right == colorCount || right == left)
        right = (left + urand(1, colorCount - 1)) % colorCount;

    leftSpawner = spawners[left];
    rightSpawner = spawners[right];
    SaveEncounterData();
}

Player* instance_blackwing_lair::FindNefarianParticipant() const
{
    // Center of the platform between Victor's throne, the tunnels and the landing point.
    constexpr float roomX = -7540.0f;
    constexpr float roomY = -1220.0f;
    constexpr float roomZ = 476.75f;
    for (auto const& reference : instance->GetPlayers())
        if (Player* player = reference.getSource(); player && player->IsAlive() && !player->IsGameMaster() &&
            std::abs(player->GetPositionZ() - roomZ) < 20.0f && player->IsWithinDist3d(roomX, roomY, roomZ, 135.0f))
            return player;

    return nullptr;
}

bool instance_blackwing_lair::CanStartNefarian() const
{
    return m_nefarianPhase == NefarianPhase::Idle && GetData(TYPE_NEFARIAN) != DONE;
}

bool instance_blackwing_lair::BeginNefarianIntro(Creature const& victor)
{
    if (!CanStartNefarian() || &victor != GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true))
        return false;

    SelectNefarianTunnels();
    m_nefarianPhase = NefarianPhase::Intro;
    SetData(TYPE_NEFARIAN, IN_PROGRESS);
    return true;
}

bool instance_blackwing_lair::StartNefarianDrakonids(Creature& victor)
{
    if (!IsNefarianIntro() || &victor != GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true))
        return false;

    uint32 const entries[] = {GetData(TYPE_NEFA_LTUNNEL), GetData(TYPE_NEFA_RTUNNEL)};
    for (uint32 i = 0; i < std::size(entries); ++i)
    {
        Position const& tunnel = aNefarianLocs[i];
        Creature* spawner = victor.SummonCreature(entries[i], tunnel.x, tunnel.y, tunnel.z, 5.0f,
            TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN, HOUR * IN_MILLISECONDS);
        if (!spawner)
        {
            SetData(TYPE_NEFARIAN, FAIL);
            return false;
        }
        m_drakonidSpawners.insert(spawner->GetObjectGuid());
    }

    m_nefarianPhase = NefarianPhase::Drakonids;
    return true;
}

Player* instance_blackwing_lair::StartNefarianDragon(Creature& dragon)
{
    if (m_nefarianPhase != NefarianPhase::Landing || dragon.GetObjectGuid() != m_nefarianGuid)
        return nullptr;

    Player* player = FindNefarianParticipant();
    if (!player)
    {
        SetData(TYPE_NEFARIAN, FAIL);
        return nullptr;
    }

    m_nefarianPhase = NefarianPhase::Dragon;
    m_nefarianPhaseTimer = 0;
    SetData(TYPE_NEFARIAN, IN_PROGRESS);
    return player;
}

void instance_blackwing_lair::NefarianEvaded(Creature const& creature)
{
    if ((m_nefarianPhase == NefarianPhase::Drakonids &&
        &creature == GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true)) ||
        ((m_nefarianPhase == NefarianPhase::Landing || m_nefarianPhase == NefarianPhase::Dragon) &&
        creature.GetObjectGuid() == m_nefarianGuid))
        SetData(TYPE_NEFARIAN, FAIL);
}

void instance_blackwing_lair::NefarianDefeated(Creature const& dragon)
{
    if (m_nefarianPhase == NefarianPhase::Dragon && dragon.GetObjectGuid() == m_nefarianGuid)
        SetData(TYPE_NEFARIAN, DONE);
}

void instance_blackwing_lair::UpdateNefarianEncounter(uint32 diff)
{
    if (m_nefarianPhase == NefarianPhase::Idle)
        return;

    if (!FindNefarianParticipant() ||
        (m_nefarianPhase == NefarianPhase::Drakonids && m_drakonidSpawners.size() != 2) ||
        ((m_nefarianPhase == NefarianPhase::Landing || m_nefarianPhase == NefarianPhase::Dragon) &&
        !instance->GetCreature(m_nefarianGuid)))
    {
        SetData(TYPE_NEFARIAN, FAIL);
        return;
    }

    if (m_victorOutroTimer)
    {
        if (m_victorOutroTimer > diff)
            m_victorOutroTimer -= diff;
        else
        {
            m_victorOutroTimer = 0;
            if (Creature* victor = GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true))
                victor->ForcedDespawn();
        }
    }

    if (!m_nefarianPhaseTimer)
        return;
    if (m_nefarianPhaseTimer > diff)
    {
        m_nefarianPhaseTimer -= diff;
        return;
    }

    m_nefarianPhaseTimer = 0;
    if (m_nefarianPhase == NefarianPhase::Transition)
    {
        if (Creature* victor = GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true); victor && victor->IsAlive())
        {
            Position const& spawn = aNefarianLocs[2];
            if (Creature* dragon = victor->SummonCreature(NPC_NEFARIAN, spawn.x, spawn.y, spawn.z, spawn.o, TEMPSPAWN_DEAD_DESPAWN, 0, true))
            {
                m_nefarianGuid = dragon->GetObjectGuid();
                m_nefarianPhase = NefarianPhase::Landing;
                m_nefarianPhaseTimer = NefarianLandingTimeout;
                victor->CastSpell(nullptr, SPELL_SHADOWBLINK_OUTRO, TRIGGERED_OLD_TRIGGERED);
                victor->AI()->SetCombatScriptStatus(true);
                victor->CombatStop(true);
                m_victorOutroTimer = VictorOutroDelay;
                return;
            }
        }
    }

    // A failed summon or a dragon that never reached the landing point can be retried.
    SetData(TYPE_NEFARIAN, FAIL);
}

void instance_blackwing_lair::DespawnNefarianCreatures(GuidSet& guids)
{
    GuidSet pending;
    pending.swap(guids);
    for (ObjectGuid guid : pending)
        if (Creature* creature = instance->GetCreature(guid))
            creature->ForcedDespawn();
}

void instance_blackwing_lair::CleanupNefarianEncounter(bool retry)
{
    m_nefarianPhase = NefarianPhase::Idle;
    m_nefarianPhaseTimer = 0;
    m_victorOutroTimer = 0;
    ObjectGuid const dragonGuid = m_nefarianGuid;
    m_nefarianGuid.Clear();
    m_deadDrakonids.clear();
    DespawnNefarianCreatures(m_drakonidSpawners);
    DespawnNefarianCreatures(m_drakonids);
    DespawnNefarianCreatures(m_nefarianAdds);

    for (ObjectGuid guid : m_drakonidBones)
        if (GameObject* bones = instance->GetGameObject(guid))
            bones->SetLootState(GO_JUST_DEACTIVATED);
    m_drakonidBones.clear();

    if (retry)
        if (Creature* dragon = instance->GetCreature(dragonGuid))
            dragon->ForcedDespawn();

    if (Creature* victor = GetSingleCreatureFromStorage(NPC_LORD_VICTOR_NEFARIUS, true))
    {
        victor->ForcedDespawn();
        if (retry)
            victor->Respawn();
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
    explicit go_ai_suppression(GameObject* go) : GameObjectAI(go),
        m_instance(static_cast<instance_blackwing_lair*>(go->GetMap()->GetInstanceData())),
        m_fumeTimer(urand(0, 5 * IN_MILLISECONDS)) {}

    void OnLootStateChange(Unit* /*user*/) override
    {
        if (!m_instance || m_go->GetLootState() != GO_ACTIVATED)
            return;

        if (m_instance->GetData(TYPE_LASHLAYER) == DONE)
            DisableSuppressionDevice(*m_go);
        else if (m_go->GetGoState() == GO_STATE_READY)
            m_go->SetRespawnDelay(urand(30, 2 * MINUTE));
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_fumeTimer > diff)
        {
            m_fumeTimer -= diff;
            return;
        }

        m_fumeTimer = 5 * IN_MILLISECONDS;
        if (m_go->IsSpawned() && m_go->GetLootState() == GO_READY && m_go->GetGoState() == GO_STATE_READY)
        {
            m_go->SendGameObjectCustomAnim(m_go->GetObjectGuid());
            m_go->CastSpell(nullptr, nullptr, m_go->GetGOInfo()->trap.spellId, TRIGGERED_OLD_TRIGGERED);
        }
    }

private:
    instance_blackwing_lair* const m_instance;
    uint32 m_fumeTimer;
};


GameObjectAI* GetAI_go_suppression(GameObject* go)
{
    return new go_ai_suppression(go);
}

struct SuppressionAura : public SpellScript
{
    bool OnCheckTarget(Spell const* /*spell*/, Unit* target, SpellEffectIndex /*eff*/) const override
    {
        return !target->HasStealthAura();
    }
};

/*###################################################
## Chromaggus' breaths and Nefarian tunnels selection
####################################################*/

struct NefarianTunnelSelection : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effect) const override
    {
        if (Unit* caster = spell->GetCaster(); effect == EFFECT_INDEX_0 && caster && caster->GetEntry() == NPC_LORD_VICTOR_NEFARIUS)
            if (auto* instance = dynamic_cast<instance_blackwing_lair*>(caster->GetInstanceData()))
                instance->SelectNefarianTunnels();
    }
};

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

    RegisterSpellScript<SuppressionAura>("spell_bwl_suppression_aura");
    RegisterSpellScript<NefarianTunnelSelection>("spell_nefarian_tunnel_selection");
}
