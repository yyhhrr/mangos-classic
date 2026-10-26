/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
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

#include "Entities/Player.h"
#include "BattleGround.h"
#include "BattleGroundAV.h"
#include "BattleGroundMgr.h"
#include "Entities/Creature.h"
#include "Entities/GameObject.h"
#include "Tools/Language.h"
#include "Server/WorldPacket.h"
#include "Globals/ObjectMgr.h"
#include "Quests/QuestDef.h"
#include "Chat/Chat.h"
#include "AI/ScriptDevAI/include/sc_grid_searchers.h"

BattleGroundAV::BattleGroundAV(): m_ending(false), m_mineYellTimer(BG_AV_MINE_YELL), m_honorMapComplete(0), m_repTowerDestruction(0), m_repCaptain(0), m_repBoss(0), m_repOwnedGrave(0), m_repOwnedMine(0), m_repSurviveCaptain(0), m_repSurviveTower(0)
{
    m_startMessageIds[BG_STARTING_EVENT_FIRST]  = 0;
    m_startMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_AV_START_ONE_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_AV_START_HALF_MINUTE;
    m_startMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_AV_HAS_BEGUN;
}

void BattleGroundAV::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS || m_ending)
        return;

    BattleGround::HandleKillPlayer(player, killer);
    UpdateScore(GetTeamIndexByTeamId(player->GetTeam()), -1);
}

// Handle creature kill
void BattleGroundAV::HandleKillUnit(Creature* creature, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS || m_ending || !killer)
        return;

    DEBUG_LOG("BattleGroundAV: Handle creature entry %u kill by team %u.", creature->GetEntry(), killer->GetTeam());

    switch (creature->GetEntry())
    {
        case BG_AV_NPC_VANNDAR_STORMPIKE:
        {
            // cast quest complete spells
            CastSpellOnTeam(BG_AV_SPELL_COMPLETE_AV_QUEST, HORDE);

            RewardReputationToTeam(BG_AV_FACTION_ID_FROSTWOLF, m_repBoss, HORDE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), HORDE);

            DoSendYellToTeam(TEAM_INDEX_ALLIANCE, LANG_BG_AV_A_GENERAL_DEAD, 0);

            EndBattleGround(HORDE);
            break;
        }
        case BG_AV_NPC_DREKTHAR:
        {
            // cast quest complete spells
            CastSpellOnTeam(BG_AV_SPELL_COMPLETE_AV_QUEST, ALLIANCE);

            RewardReputationToTeam(BG_AV_FACTION_ID_STORMPIKE, m_repBoss, ALLIANCE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), ALLIANCE);

            DoSendYellToTeam(TEAM_INDEX_HORDE, LANG_BG_AV_H_GENERAL_DEAD, 0);

            EndBattleGround(ALLIANCE);
            break;
        }
        case BG_AV_NPC_CAPTAIN_STONEHEARTH:
        {
            RewardReputationToTeam(BG_AV_FACTION_ID_FROSTWOLF, m_repCaptain, HORDE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), HORDE);

            // Record the dead captain event before zero reinforcements can end the battle.
            SpawnEvent(BG_AV_NODE_CAPTAIN_DEAD_A, 0, true);
            UpdateScore(TEAM_INDEX_ALLIANCE, (-1) * BG_AV_RES_CAPTAIN);
            break;
        }
        case BG_AV_NPC_CAPTAIN_GALVANGAR:
        {
            RewardReputationToTeam(BG_AV_FACTION_ID_STORMPIKE, m_repCaptain, ALLIANCE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), ALLIANCE);

            // Record the dead captain event before zero reinforcements can end the battle.
            SpawnEvent(BG_AV_NODE_CAPTAIN_DEAD_H, 0, true);
            UpdateScore(TEAM_INDEX_HORDE, (-1) * BG_AV_RES_CAPTAIN);
            break;
        }
        case BG_AV_NPC_MORLOCH:
        case BG_AV_NPC_UMI_THORSON:
        case BG_AV_NPC_KEETAR:
            ChangeMineOwner(BG_AV_IRONDEEP_MINE_ID, GetTeamIndexByTeamId(killer->GetTeam()));
            break;
        case BG_AV_NPC_TASKMASTER_SNIVVLE:
        case BG_AV_NPC_AGGI_RUMBLESTOMP:
        case BG_AV_NPC_MASHA_SWIFTCUT:
            ChangeMineOwner(BG_AV_COLDTOOTH_MINE_ID, GetTeamIndexByTeamId(killer->GetTeam()));
            break;
    }
}

void BattleGroundAV::HandleCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case BG_AV_NPC_ICEWING_MARSHAL:
        case BG_AV_NPC_STONEHEARTH_MARSHAL:
        case BG_AV_NPC_NORTH_MARSHAL:
        case BG_AV_NPC_SOUTH_MARSHAL:
        case BG_AV_NPC_CAPTAIN_STONEHEARTH:
        case BG_AV_NPC_EAST_WARMASTER:
        case BG_AV_NPC_ICEBLOOD_WARMASTER:
        case BG_AV_NPC_TOWERPOINT_WARMASTER:
        case BG_AV_NPC_WEST_WARMASTER:
        case BG_AV_NPC_CAPTAIN_GALVANGAR:
        case BG_AV_NPC_STORMPIKE_HERALD:
        case BG_AV_NPC_FROSTWOLF_HERALD:
        case BG_AV_NPC_HERALD:
        case BG_AV_NPC_MORLOCH:
        case BG_AV_NPC_TASKMASTER_SNIVVLE:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            break;
    }
}

void BattleGroundAV::HandleGameObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case BG_AV_GO_IRONDEEP_SUPPLIES:
        case BG_AV_GO_COLDTOOTH_SUPPLIES:
        case BG_AV_GO_FROSTWOLF_BANNER:
        case BG_AV_GO_STORMPIKE_BANNER:
            m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
            break;
    }
}

namespace
{
    bool AVCommanderInfo(uint32 entry, PvpTeamIndex& team, AVSupportType& tier)
    {
        switch (entry)
        {
            case 13438: team = TEAM_INDEX_ALLIANCE; tier = AV_SUPPORT_AIR_1; return true;
            case 13439: team = TEAM_INDEX_ALLIANCE; tier = AV_SUPPORT_AIR_2; return true;
            case 13437: team = TEAM_INDEX_ALLIANCE; tier = AV_SUPPORT_AIR_3; return true;
            case 13179: team = TEAM_INDEX_HORDE; tier = AV_SUPPORT_AIR_1; return true;
            case 13180: team = TEAM_INDEX_HORDE; tier = AV_SUPPORT_AIR_2; return true;
            case 13181: team = TEAM_INDEX_HORDE; tier = AV_SUPPORT_AIR_3; return true;
            default: return false;
        }
    }

    char const* AVResourceName(AVSupportResource resource)
    {
        static char const* names[AV_RESOURCE_MAX] =
        {
            "\xe6\x8a\xa4\xe7\x94\xb2\xe7\xa2\x8e\xe7\x89\x87", "\xe4\xbd\x8e\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81", "\xe4\xb8\xad\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81", "\xe9\xab\x98\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81", "\xe5\x85\x83\xe7\xb4\xa0\xe9\xa6\x96\xe9\xa2\x86\xe5\x8f\xac\xe5\x94\xa4\xe6\x9d\x90\xe6\x96\x99",
            "\xe6\x9c\xac\xe6\x96\xb9\xe7\x9f\xbf\xe6\xb4\x9e\xe8\xa1\xa5\xe7\xbb\x99", "\xe6\x95\x8c\xe6\x96\xb9\xe7\x9f\xbf\xe6\xb4\x9e\xe8\xa1\xa5\xe7\xbb\x99", "\xe9\xaa\x91\xe5\x85\xb5\xe7\x89\xa9\xe8\xb5\x84", "\xe9\x80\x81\xe5\x9b\x9e\xe7\x9a\x84\xe5\x9d\x90\xe9\xaa\x91"
        };
        return resource < AV_RESOURCE_MAX ? names[resource] : "\xe6\x9c\xaa\xe7\x9f\xa5\xe8\xb5\x84\xe6\xba\x90";
    }
}

bool BattleGroundAV::IsSupportAvailable() const
{
    return GetStatus() == STATUS_IN_PROGRESS && !m_ending;
}

uint32 BattleGroundAV::GetSupportProgress(PvpTeamIndex team, AVSupportResource resource) const
{
    return m_supportState.GetProgress(team, resource);
}

uint32 BattleGroundAV::GetSupportRequirement(AVSupportResource resource) const
{
    return AVSupport::Requirement(resource);
}

AVSupportStage BattleGroundAV::GetSupportStage(PvpTeamIndex team, AVSupportType type) const
{
    return IsSupportAvailable() ? m_supportState.GetStage(team, type) : AV_SUPPORT_UNAVAILABLE;
}

bool BattleGroundAV::IsSupportReady(PvpTeamIndex team, AVSupportType type) const
{
    return GetSupportStage(team, type) == AV_SUPPORT_READY;
}

bool BattleGroundAV::IsSupportActive(PvpTeamIndex team, AVSupportType type) const
{
    return GetSupportStage(team, type) == AV_SUPPORT_ACTIVE;
}

bool BattleGroundAV::TryBeginSupport(PvpTeamIndex team, AVSupportType type)
{
    return IsSupportAvailable() && m_supportState.TryBegin(team, type);
}

void BattleGroundAV::FinishSupport(PvpTeamIndex team, AVSupportType type, bool success)
{
    if (IsSupportAvailable())
        m_supportState.Finish(team, type, success);
}

bool BattleGroundAV::NotifyCommanderRescued(uint32 entry)
{
    PvpTeamIndex team; AVSupportType tier;
    return IsSupportAvailable() && AVCommanderInfo(entry, team, tier) && m_supportState.Rescue(team, tier);
}

bool BattleGroundAV::IsCommanderRescued(uint32 entry) const
{
    PvpTeamIndex team; AVSupportType tier;
    return AVCommanderInfo(entry, team, tier) && m_supportState.IsRescued(team, tier);
}

bool BattleGroundAV::RegisterSupportCreature(PvpTeamIndex team, AVSupportType type, Creature* creature)
{
    if (!creature || !IsSupportActive(team, type) || creature->GetMap() != GetBgMap())
        return false;
    const ObjectGuid guid = creature->GetObjectGuid();
    for (uint8 t = 0; t < PVP_TEAM_COUNT; ++t)
        for (uint8 s = 0; s < AV_SUPPORT_MAX; ++s)
            if (std::find(m_supportCreatures[t][s].begin(), m_supportCreatures[t][s].end(), guid) != m_supportCreatures[t][s].end())
                return t == team && s == type;
    m_supportCreatures[team][type].push_back(guid);
    return true;
}

bool BattleGroundAV::RegisterSupportGameObject(PvpTeamIndex team, AVSupportType type, GameObject* object)
{
    if (!object || !IsSupportActive(team, type) || object->GetMap() != GetBgMap())
        return false;
    const ObjectGuid guid = object->GetObjectGuid();
    for (uint8 t = 0; t < PVP_TEAM_COUNT; ++t)
        for (uint8 s = 0; s < AV_SUPPORT_MAX; ++s)
            if (std::find(m_supportGameObjects[t][s].begin(), m_supportGameObjects[t][s].end(), guid) != m_supportGameObjects[t][s].end())
                return t == team && s == type;
    m_supportGameObjects[team][type].push_back(guid);
    return true;
}

bool BattleGroundAV::RegisterCommanderEscort(Creature* creature)
{
    PvpTeamIndex team; AVSupportType tier;
    if (!creature || !IsSupportAvailable() || creature->GetMap() != GetBgMap() || !AVCommanderInfo(creature->GetEntry(), team, tier))
        return false;
    const ObjectGuid guid = creature->GetObjectGuid();
    if (std::find(m_commanderEscorts.begin(), m_commanderEscorts.end(), guid) == m_commanderEscorts.end())
        m_commanderEscorts.push_back(guid);
    return true;
}

bool BattleGroundAV::RegisterTamedCreature(Creature* creature)
{
    if (!creature || !IsSupportAvailable() || creature->GetMap() != GetBgMap() ||
        (creature->GetEntry() != 10981 && creature->GetEntry() != 10990))
        return false;
    const ObjectGuid guid = creature->GetObjectGuid();
    if (std::find(m_tamedCreatures.begin(), m_tamedCreatures.end(), guid) == m_tamedCreatures.end())
        m_tamedCreatures.push_back(guid);
    return true;
}

void BattleGroundAV::CleanupSupportActors()
{
    GuidSet creatures, objects;
    creatures.insert(m_commanderEscorts.begin(), m_commanderEscorts.end());
    creatures.insert(m_tamedCreatures.begin(), m_tamedCreatures.end());
    m_commanderEscorts.clear();
    m_tamedCreatures.clear();
    for (uint8 team = 0; team < PVP_TEAM_COUNT; ++team)
        for (uint8 support = 0; support < AV_SUPPORT_MAX; ++support)
        {
            creatures.insert(m_supportCreatures[team][support].begin(), m_supportCreatures[team][support].end());
            objects.insert(m_supportGameObjects[team][support].begin(), m_supportGameObjects[team][support].end());
            m_supportCreatures[team][support].clear();
            m_supportGameObjects[team][support].clear();
        }
    if (!GetBgMap())
        return;
    // Clear registries before callbacks; teardown cannot register a replacement event.
    for (ObjectGuid guid : creatures)
        if (Creature* creature = GetBgMap()->GetCreature(guid))
            if (creature->IsAlive())
            {
                // Let event scripts release player ritual channels before actor removal.
                if (creature->AI())
                    creature->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_A, creature, creature, 0x4156434c); // AVCL
                creature->ForcedDespawn(); // Preserve dead actors' lootable corpses until normal map cleanup.
            }
    for (ObjectGuid guid : objects)
        if (GameObject* object = GetBgMap()->GetGameObject(guid))
            object->Delete();
}

// Quest rewards are counted in actual required item units, not assumed turn-ins.
void BattleGroundAV::HandleQuestRewarded(uint32 questid, Player* player)
{
    if (!player || !IsSupportAvailable() || player->GetMap() != GetBgMap())
        return;
    const PvpTeamIndex team = GetTeamIndexByTeamId(player->GetTeam());
    if (team >= PVP_TEAM_COUNT)
        return;
    AVSupportResource resource = AV_RESOURCE_MAX;
    PvpTeamIndex requiredTeam = TEAM_INDEX_NEUTRAL;
    uint32 item = 0, reputation = 0;
    switch (questid)
    {
        case BG_AV_QUEST_ARMOR_SCRAPS_A:
        case BG_AV_QUEST_ARMOR_SCRAPS_A_REPEAT:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_SCRAPS; item = 17422; reputation = 1; break;
        case BG_AV_QUEST_ARMOR_SCRAPS_H:
        case BG_AV_QUEST_ARMOR_SCRAPS_H_REPEAT:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_SCRAPS; item = 17422; reputation = 1; break;
        case BG_AV_QUEST_SLIDORES_FLEET:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_AIR_1; item = 17502; reputation = 1; break;
        case BG_AV_QUEST_GUSES_FLEET:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_AIR_1; item = 17326; reputation = 1; break;
        case BG_AV_QUEST_VIPORES_FLEET:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_AIR_2; item = 17503; reputation = 2; break;
        case BG_AV_QUEST_JEZTORS_FLEET:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_AIR_2; item = 17327; reputation = 2; break;
        case BG_AV_QUEST_ICHMANS_FLEET:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_AIR_3; item = 17504; reputation = 5; break;
        case BG_AV_QUEST_MULVERICKS_FLEET:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_AIR_3; item = 17328; reputation = 5; break;
        case BG_AV_QUEST_CRYSTAL_CLUSTER:
        case BG_AV_QUEST_IVUS_FOREST_LORD:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_ELEMENTAL; item = 17423; break;
        case BG_AV_QUEST_GALLON_OF_BLOOD:
        case BG_AV_QUEST_LOKHOLAR_ICE_LORD:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_ELEMENTAL; item = 17306; break;
        case BG_AV_QUEST_IRONDEEP_SUPPLIES_A:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_SUPPLIES_NEAR; item = 17522; reputation = 2; break;
        case BG_AV_QUEST_COLDTOOTH_SUPPLIES_H:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_SUPPLIES_NEAR; item = 17542; reputation = 2; break;
        case BG_AV_QUEST_COLDTOOTH_SUPPLIES_A:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_SUPPLIES_FAR; item = 17542; reputation = 3; break;
        case BG_AV_QUEST_IRONDEEP_SUPPLIES_H:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_SUPPLIES_FAR; item = 17522; reputation = 3; break;
        case BG_AV_QUEST_RAM_RIDING:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_HIDES; item = 17643; reputation = 1; break;
        case BG_AV_QUEST_RAM_HIDE:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_HIDES; item = 17642; reputation = 1; break;
        case BG_AV_QUEST_EMPTY_STABLES_A:
            requiredTeam = TEAM_INDEX_ALLIANCE; resource = AV_RESOURCE_MOUNTS; reputation = 1; break;
        case BG_AV_QUEST_EMPTY_STABLES_H:
            requiredTeam = TEAM_INDEX_HORDE; resource = AV_RESOURCE_MOUNTS; reputation = 1; break;
        default: return;
    }
    if (team != requiredTeam)
        return;
    Quest const* quest = sObjectMgr.GetQuestTemplate(questid);
    if (!quest)
        return;
    uint32 count = resource == AV_RESOURCE_MOUNTS ? 1 : 0;
    for (uint32 i = 0; item && i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        if (quest->ReqItemId[i] == item)
            count += quest->ReqItemCount[i];
    if (!count)
        return;
    if (resource == AV_RESOURCE_ELEMENTAL)
        reputation = count;
    const unsigned oldTier = AVSupport::DefenderTier(m_supportState.GetProgress(team, AV_RESOURCE_SCRAPS));
    m_supportState.AddProgress(team, resource, count);
    if (resource == AV_RESOURCE_SCRAPS && oldTier != AVSupport::DefenderTier(m_supportState.GetProgress(team, AV_RESOURCE_SCRAPS)))
        for (uint8 node = BG_AV_NODES_FIRSTAID_STATION; node <= BG_AV_NODES_FROSTWOLF_HUT; ++node)
            if (m_nodes[node].owner == team && m_nodes[node].state == POINT_CONTROLLED)
                PopulateNode(AVNodeIds(node));
    if (reputation)
        RewardReputationToTeam(avTeamFactions[team], reputation, player->GetTeam());
    ChatHandler(player).PSendSysMessage("\xe5\xa5\xa5\xe5\xb1\xb1\xe8\xb5\x84\xe6\xba\x90\xef\xbc\x9a\x25\x73\x20\x2b\x25\x75\xef\xbc\x8c\xe9\x98\xb5\xe8\x90\xa5\xe7\xb4\xaf\xe8\xae\xa1\x20\x25\x75\x2f\x25\x75\xe3\x80\x82", AVResourceName(resource), count,
        GetSupportProgress(team, resource), GetSupportRequirement(resource));
}

// Method that updates team score
void BattleGroundAV::UpdateScore(PvpTeamIndex teamIdx, int32 points)
{
    if (!IsSupportAvailable() || teamIdx >= PVP_TEAM_COUNT)
        return;
    int32 worldStateId = teamIdx == TEAM_INDEX_ALLIANCE ? BG_AV_STATE_SCORE_A : BG_AV_STATE_SCORE_H;

    // note: to remove reinforcements points must be negative, for adding reinforcements points must be positive
    int32 newVal = std::max(GetBgMap()->GetVariableManager().GetVariable(worldStateId) + points, 0);
    GetBgMap()->GetVariableManager().SetVariable(worldStateId, newVal);

    if (points < 0)
    {
        // end battleground and set the winner
        if (newVal < 1)
        {
            // other team will win:
            EndBattleGround((teamIdx == TEAM_INDEX_ALLIANCE) ? HORDE : ALLIANCE);
        }
        // inform the players about getting close to losing
        else if (!m_wasInformedNearLose[teamIdx] && newVal < BG_AV_SCORE_NEAR_LOSE)
        {
            SendMessageToAll((teamIdx == TEAM_INDEX_HORDE) ? LANG_BG_AV_H_NEAR_LOSE : LANG_BG_AV_A_NEAR_LOSE, CHAT_MSG_BG_SYSTEM_NEUTRAL);

            PlaySoundToAll(BG_AV_SOUND_NEAR_LOSE);
            m_wasInformedNearLose[teamIdx] = true;
        }
    }
}

void BattleGroundAV::Update(uint32 diff)
{
    BattleGround::Update(diff);
    if (m_ending)
        return;

    if (m_mineYellTimer)
    {
        if (m_mineYellTimer <= diff)
        {
            if (Creature* morloch = GetSingleCreatureFromStorage(BG_AV_NPC_MORLOCH))
                SendBcdToTeam(YELL_MORLOCH_MINE, CHAT_MSG_MONSTER_YELL, morloch, ALLIANCE);
            if (Creature* snivvle = GetSingleCreatureFromStorage(BG_AV_NPC_TASKMASTER_SNIVVLE))
                SendBcdToTeam(YELL_SNIVVLE_MINE, CHAT_MSG_MONSTER_YELL, snivvle, HORDE);
            m_mineYellTimer = 0;
        }
        else
            m_mineYellTimer -= diff;
    }

    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    // add points from mine owning, and look if the neutral team can reclaim the mine
    for (uint8 mine = 0; mine < BG_AV_MAX_MINES; ++mine)
    {
        if (m_mineOwner[mine] != TEAM_INDEX_NEUTRAL)
        {
            if (m_mineTimer[mine] < diff)
            {
                UpdateScore(PvpTeamIndex(m_mineOwner[mine]), 1);
                m_mineTimer[mine] = BG_AV_MINE_TICK_TIMER;
            }
            else
                m_mineTimer[mine] -= diff;

            if (m_mineReclaimTimer[mine] > diff)
                m_mineReclaimTimer[mine] -= diff;
            else
                ChangeMineOwner(AVMineIds(mine), TEAM_INDEX_NEUTRAL);
        }
    }

    // looks for all timers of the nodes and destroy the building (for graveyards the building wont get destroyed, it goes just to the other team
    for (uint8 i = 0; i < BG_AV_MAX_NODES && IsSupportAvailable(); ++i)
    {
        if (m_nodes[i].state == POINT_ASSAULTED)
        {
            if (m_nodes[i].timer > diff)
                m_nodes[i].timer -= diff;
            else
                ProcessPlayerDestroyedPoint(AVNodeIds(i));
        }
    }
}

void BattleGroundAV::StartingEventOpenDoors()
{
    GetBgMap()->GetVariableManager().SetVariable(BG_AV_STATE_SCORE_SHOW_H, WORLD_STATE_ADD);
    GetBgMap()->GetVariableManager().SetVariable(BG_AV_STATE_SCORE_SHOW_A, WORLD_STATE_ADD);

    OpenDoorEvent(BG_EVENT_DOOR);
}

void BattleGroundAV::AddPlayer(Player* player)
{
    BattleGround::AddPlayer(player);
    // create score and add it to map, default values are set in constructor
    BattleGroundAVScore* sc = new BattleGroundAVScore;
    m_playerScores[player->GetObjectGuid()] = sc;
}

void BattleGroundAV::EndBattleGround(Team winner)
{
    if (!IsSupportAvailable())
        return;
    m_ending = true;
    CleanupSupportActors();
    // calculate bonuskills for both teams:
    uint32 gravesOwned[PVP_TEAM_COUNT]    = {0, 0};
    uint32 minesOwned[PVP_TEAM_COUNT]     = {0, 0};

    // graves all controlled
    for (uint8 i = 0; i < BG_AV_MAX_NODES; ++i)
        if (m_nodes[i].graveyardId && m_nodes[i].state == POINT_CONTROLLED && m_nodes[i].owner < PVP_TEAM_COUNT)
            ++gravesOwned[m_nodes[i].owner];

    for (auto& i : m_mineOwner)
        if (i != TEAM_INDEX_NEUTRAL)
            ++minesOwned[i];

    for (uint8 i = 0; i < PVP_TEAM_COUNT; ++i)
    {
        Team team = GetTeamIdByTeamIndex(PvpTeamIndex(i));

        uint32 bonusHonor = GetBonusHonorFromKill(m_homeTowersControlled[i] * BG_AV_KILL_SURVIVING_TOWER);
        uint32 bonusReputation = m_homeTowersControlled[i] * m_repSurviveTower;

        // bonus tower reputation and honor
        if (bonusReputation)
            RewardReputationToTeam(avTeamFactions[i], m_homeTowersControlled[i] * m_repSurviveTower, team);
        if (bonusHonor)
            RewardHonorToTeam(bonusHonor, team);

        DEBUG_LOG("BattleGroundAV: Team %u wins the battleground. Controlled towers: %u bonus towers honor: %u bonus towers reputation: %u", i, m_homeTowersControlled[i], bonusHonor, bonusReputation);

        // bonus graveyards and mines reputation
        if (gravesOwned[i])
            RewardReputationToTeam(avTeamFactions[i], gravesOwned[i] * m_repOwnedGrave, team);
        if (minesOwned[i])
            RewardReputationToTeam(avTeamFactions[i], minesOwned[i] * m_repOwnedMine, team);

        // captain survived; bonus reputation and honor
        Creature* captain = GetSingleCreatureFromStorage(avTeamCaptains[i]);
        if (captain && captain->IsAlive())
        {
            RewardReputationToTeam(avTeamFactions[i], m_repSurviveCaptain, team);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_SURVIVING_CAPTAIN), team);
        }
    }

    // both teams get honor for completing the BG
    if (m_honorMapComplete)
    {
        RewardHonorToTeam(GetBonusHonorFromKill(m_honorMapComplete), ALLIANCE);
        RewardHonorToTeam(GetBonusHonorFromKill(m_honorMapComplete), HORDE);
    }

    BattleGround::EndBattleGround(winner);

    if (Creature* herald = GetSingleCreatureFromStorage(BG_AV_NPC_HERALD))
    {
        if (winner == ALLIANCE)
        {
            SendBcdToAll(YELL_FROSTWOLF_DEAD, CHAT_MSG_MONSTER_YELL, herald);
            SendBcdToAll(YELL_ALLIANCE_WINS, CHAT_MSG_MONSTER_YELL, herald);
        }
        else
        {
            SendBcdToAll(YELL_STORMPIKE_DEAD, CHAT_MSG_MONSTER_YELL, herald);
            SendBcdToAll(YELL_HORDE_WINS, CHAT_MSG_MONSTER_YELL, herald);
        }
    }

}

bool BattleGroundAV::HandleAreaTrigger(Player* source, uint32 trigger)
{
    // this is wrong way to implement these things. On official it done by gameobject spell cast.
    switch (trigger)
    {
        case BG_AV_AT_ENTRANCE_ALLIANCE1:
        case BG_AV_AT_ENTRANCE_ALLIANCE2:
            if (source->GetTeam() != ALLIANCE)
                source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_ALLIANCE_USE);
            else
                source->LeaveBattleground();
            break;
        case BG_AV_AT_ENTRANCE_HORDE:
            if (source->GetTeam() != HORDE)
                source->GetSession()->SendNotification(LANG_BATTLEGROUND_ONLY_HORDE_USE);
            else
                source->LeaveBattleground();
            break;
        default:
            return false;
    }
    return true;
}

void BattleGroundAV::UpdatePlayerScore(Player* source, uint32 type, uint32 value)
{
    BattleGroundScoreMap::iterator itr = m_playerScores.find(source->GetObjectGuid());
    if (itr == m_playerScores.end())                        // player not found...
        return;

    switch (type)
    {
        case SCORE_GRAVEYARDS_ASSAULTED:
            ((BattleGroundAVScore*)itr->second)->graveyardsAssaulted += value;
            break;
        case SCORE_GRAVEYARDS_DEFENDED:
            ((BattleGroundAVScore*)itr->second)->graveyardsDefended += value;
            break;
        case SCORE_TOWERS_ASSAULTED:
            ((BattleGroundAVScore*)itr->second)->towersAssaulted += value;
            break;
        case SCORE_TOWERS_DEFENDED:
            ((BattleGroundAVScore*)itr->second)->towersDefended += value;
            break;
        case SCORE_SECONDARY_OBJECTIVES:
            ((BattleGroundAVScore*)itr->second)->secondaryObjectives += value;
            break;
        default:
            BattleGround::UpdatePlayerScore(source, type, value);
            break;
    }
}

// Process the destruction of a battleground node
void BattleGroundAV::ProcessPlayerDestroyedPoint(AVNodeIds node)
{
    if (!IsSupportAvailable() || node >= BG_AV_MAX_NODES || m_nodes[node].state != POINT_ASSAULTED)
        return;
    DEBUG_LOG("BattleGroundAV: Node %i was destroyed", node);

    PvpTeamIndex ownerTeamIdx = PvpTeamIndex(m_nodes[node].owner);
    PvpTeamIndex otherTeamIdx = GetOtherTeamIndex(ownerTeamIdx);
    Team ownerTeam = GetTeamIdByTeamIndex(ownerTeamIdx);

    bool isTower = m_nodes[node].graveyardId == 0;
    uint32 newState = ownerTeam == ALLIANCE ? avNodeWorldStates[node].worldStateAlly : avNodeWorldStates[node].worldStateHorde;

    // despawn banner
    DestroyNode(node);
    PopulateNode(node);
    UpdateNodeWorldState(node, newState);

    if (isTower)
    {
        uint8 tmp = node - BG_AV_NODES_DUNBALDAR_SOUTH;

        // despawn marshal (one of those guys protecting the boss)
        SpawnEvent(BG_AV_MARSHAL_A_SOUTH + tmp, 0, false);

        RewardReputationToTeam((ownerTeam == ALLIANCE) ? BG_AV_FACTION_ID_STORMPIKE : BG_AV_FACTION_ID_FROSTWOLF, m_repTowerDestruction, ownerTeam);
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_TOWER), ownerTeam);

        DoSendYellToTeam(ownerTeamIdx, LANG_BG_AV_TOWER_TAKEN, node);

        // update tower counters
        ++m_enemyTowersDestroyed[ownerTeamIdx];
        if (m_homeTowersControlled[otherTeamIdx])
            --m_homeTowersControlled[otherTeamIdx];
        // End-of-battle rewards must observe the destroyed tower before zero points ends the match.
        UpdateScore(otherTeamIdx, (-1) * BG_AV_RES_TOWER);
    }
    else
    {
        DoSendYellToTeam(ownerTeamIdx, LANG_BG_AV_GRAVE_TAKEN, node);

        // setup graveyard to new team
        GetBgMap()->GetGraveyardManager().SetGraveYardLinkTeam(avNodeDefaults[node].graveyardId, BG_AV_ZONE_MAIN, GetTeamIdByTeamIndex(ownerTeamIdx));
    }
}

// Process mine owner change; mineId = 0 => Irondeep Mine; mineId = 1 => Coldtooth Mine;
void BattleGroundAV::ChangeMineOwner(AVMineIds mineId, PvpTeamIndex newOwnerTeamIdx)
{
    // TODO changing the owner should result in setting respawntime to infinite for current creatures (they should fight the new ones), spawning new mine owners creatures and changing the chest - objects so that the current owning team can use them
    if (m_mineOwner[mineId] == newOwnerTeamIdx)
        return;

    DEBUG_LOG("BattleGroundAV: Mine id %i was claimed by team index %u", mineId, newOwnerTeamIdx);

    // update world states
    GetBgMap()->GetVariableManager().SetVariable(avMineWorldStates[mineId][m_mineOwner[mineId]], WORLD_STATE_REMOVE);
    m_mineOwner[mineId] = newOwnerTeamIdx;
    GetBgMap()->GetVariableManager().SetVariable(avMineWorldStates[mineId][m_mineOwner[mineId]], WORLD_STATE_ADD);

    SpawnEvent(static_cast<uint8>(BG_AV_MINE_EVENT) + static_cast<uint8>(mineId), newOwnerTeamIdx, true);
    SpawnEvent(static_cast<uint8>(BG_AV_MINE_BOSSES) + static_cast<uint8>(mineId), newOwnerTeamIdx, true);

    if (newOwnerTeamIdx == TEAM_INDEX_NEUTRAL)
        return;

    m_mineReclaimTimer[mineId] = BG_AV_MINE_RECLAIM_TIMER;
    m_mineTimer[mineId] = BG_AV_MINE_TICK_TIMER;

    DoSendYellToTeam(newOwnerTeamIdx, LANG_BG_AV_MINE_TAKEN, 0, true, (mineId == BG_AV_IRONDEEP_MINE_ID) ? LANG_BG_AV_MINE_NORTH : LANG_BG_AV_MINE_SOUTH);
    PlaySoundToAll((newOwnerTeamIdx == TEAM_INDEX_ALLIANCE) ? BG_AV_SOUND_ALLIANCE_GOOD : BG_AV_SOUND_HORDE_GOOD);

    Team team = GetTeamIdByTeamIndex(newOwnerTeamIdx);

    // quest kill credit
    for (auto& m_player : m_players)
    {
        if (!m_player.first)
            continue;

        Player* player = sObjectMgr.GetPlayer(m_player.first);
        if (player && player->GetTeam() == team)
            player->KilledMonsterCredit(BG_AV_NPC_PVP_MINE_CREDIT);
    }
}

// Check if player is allowed to proceed with the mine quest
bool BattleGroundAV::CanPlayerDoMineQuest(uint32 goEntry, Team team)
{
    if (goEntry == BG_AV_GO_IRONDEEP_SUPPLIES)
        return (m_mineOwner[BG_AV_IRONDEEP_MINE_ID] == GetTeamIndexByTeamId(team));
    else if (goEntry == BG_AV_GO_COLDTOOTH_SUPPLIES)
        return (m_mineOwner[BG_AV_COLDTOOTH_MINE_ID] == GetTeamIndexByTeamId(team));

    return true;                                            // cause it's no mine'object it is ok if this is true
}

/// will spawn and despawn creatures around a node
/// more a wrapper around spawnevent cause graveyards are special
void BattleGroundAV::PopulateNode(AVNodeIds node)
{
    PvpTeamIndex teamIdx = m_nodes[node].owner;
    bool isTower = !m_nodes[node].graveyardId;

    if (!isTower && teamIdx != TEAM_INDEX_NEUTRAL)
    {
        const uint8 defenderEvent = static_cast<uint8>(BG_AV_MAX_NODES) + static_cast<uint8>(node);
        if (m_nodes[node].state == POINT_CONTROLLED)
        {
            const uint8 tier = static_cast<uint8>(AVSupport::DefenderTier(m_supportState.GetProgress(teamIdx, AV_RESOURCE_SCRAPS)));
            SpawnEvent(defenderEvent, static_cast<uint8>(teamIdx) * static_cast<uint8>(BG_AV_MAX_GRAVETYPES) + tier, true);
        }
        else
        {
            // The active event records the actual old owner's tier, even if that team upgraded meanwhile.
            auto active = m_activeEvents.find(defenderEvent);
            if (active != m_activeEvents.end() && active->second != BG_EVENT_NONE)
                SpawnEvent(defenderEvent, active->second, false);
        }
    }
    SpawnEvent(node, (static_cast<uint8>(teamIdx) * static_cast<uint8>(BG_AV_MAX_STATES)) + static_cast<uint8>(m_nodes[node].state), true);
}

// Handle banner click
void BattleGroundAV::HandlePlayerClickedOnFlag(Player* player, GameObject* go)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    DEBUG_LOG("BattleGroundAV: Player from team %u clicked on gameobject entry %u", player->GetTeam(), go->GetEntry());

    uint8 event = (GetBgMap()->GetMapDataContainer().GetGameObjectEventIndex(go->GetDbGuid())).event1;
    if (event >= BG_AV_MAX_NODES)                           // not a node
        return;
    AVNodeIds node = AVNodeIds(event);

    switch ((GetBgMap()->GetMapDataContainer().GetGameObjectEventIndex(go->GetDbGuid())).event2 % BG_AV_MAX_STATES)
    {
        case POINT_CONTROLLED:
            ProcessPlayerAssaultsPoint(player, node);
            break;
        case POINT_ASSAULTED:
            ProcessPlayerDefendsPoint(player, node);
            break;
        default:
            break;
    }
}

// Handle node defend
void BattleGroundAV::ProcessPlayerDefendsPoint(Player* player, AVNodeIds node)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    PvpTeamIndex teamIdx = GetTeamIndexByTeamId(player->GetTeam());

    if (m_nodes[node].owner == teamIdx || m_nodes[node].state != POINT_ASSAULTED)
        return;

    // ToDo: improve this part; The middle graveyard should be handled directly as an assault
    if (m_nodes[node].totalOwner == TEAM_INDEX_NEUTRAL && node == BG_AV_NODES_SNOWFALL_GRAVE)     // initial snowfall capture
    {
        ProcessPlayerAssaultsPoint(player, node);
        return;
    }

    DEBUG_LOG("BattleGroundAV: Team %u defends node id %i", player->GetTeam(), node);

    if (m_nodes[node].prevOwner != teamIdx)
    {
        sLog.outError("BattleGroundAV: player defends point which doesn't belong to his team %i", node);
        return;
    }

    bool isTower = !m_nodes[node].graveyardId;

    uint32 newState  = teamIdx == TEAM_INDEX_ALLIANCE ? avNodeWorldStates[node].worldStateAlly : avNodeWorldStates[node].worldStateHorde;

    uint32 scoreType = isTower ? SCORE_TOWERS_DEFENDED : SCORE_GRAVEYARDS_DEFENDED;
    int32 yellId     = isTower ? LANG_BG_AV_TOWER_DEFENDED : LANG_BG_AV_GRAVE_DEFENDED;
    uint32 soundId   = isTower ? BG_AV_SOUND_BOTH_TOWER_DEFEND : (teamIdx == TEAM_INDEX_ALLIANCE) ? BG_AV_SOUND_ALLIANCE_GOOD : BG_AV_SOUND_HORDE_GOOD;

    // process world state

    // send yell and sound
    DoSendYellToTeam(teamIdx, yellId, node);
    PlaySoundToAll(soundId);

    // update score
    UpdatePlayerScore(player, scoreType, 1);

    // process node events
    DefendNode(node, teamIdx);                              // set the right variables for nodeinfo
    PopulateNode(node);                                     // spawn node-creatures (defender for example)
    UpdateNodeWorldState(node, newState);                   // send new mapicon to the player
}

// Handle node assault
void BattleGroundAV::ProcessPlayerAssaultsPoint(Player* player, AVNodeIds node)
{
    DEBUG_LOG("BattleGroundAV: Team %u assaults node id %i", player->GetTeam(), node);

    PvpTeamIndex teamIdx  = GetTeamIndexByTeamId(player->GetTeam());

    if (m_nodes[node].owner == teamIdx || teamIdx == m_nodes[node].totalOwner)
        return;

    bool isTower = !m_nodes[node].graveyardId;

    uint32 newState     = teamIdx == TEAM_INDEX_ALLIANCE ? avNodeWorldStates[node].worldStateAllyGrey : avNodeWorldStates[node].worldStateHordeGrey;
    uint32 scoreType    = isTower ? SCORE_TOWERS_ASSAULTED : SCORE_GRAVEYARDS_ASSAULTED;
    uint32 kreditMarker = isTower ? BG_AV_NPC_PVP_TOWER_CREDIT : BG_AV_NPC_PVP_GRAVEYARD_CREDIT;
    int32 yellId        = isTower ? LANG_BG_AV_TOWER_ASSAULTED : LANG_BG_AV_GRAVE_ASSAULTED;

    // process new world state
    if (!isTower)
    {
        // before despawn of GY spirit healer - need to TP players - must be after AssaultNode; opposite team of current player
        if (Creature* spiritHealer = GetClosestCreatureWithEntry(player, teamIdx == TEAM_INDEX_ALLIANCE ? BG_NPC_SPIRIT_GUIDE_HORDE : BG_NPC_SPIRIT_GUIDE_ALLIANCE, 100.f))
            spiritHealer->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, spiritHealer, spiritHealer);

        // make graveyard invalid
        GetBgMap()->GetGraveyardManager().SetGraveYardLinkTeam(avNodeDefaults[node].graveyardId, BG_AV_ZONE_MAIN, TEAM_INVALID);
    }

    // send yell and sound
    DoSendYellToTeam(teamIdx, yellId, node);
    PlaySoundToAll((teamIdx == TEAM_INDEX_ALLIANCE) ? BG_AV_SOUND_ALLIANCE_ASSAULTS : BG_AV_SOUND_HORDE_ASSAULTS);

    // update score
    UpdatePlayerScore(player, scoreType, 1);

    // send reward
    player->RewardPlayerAndGroupAtEventCredit(kreditMarker, player);

    // process node events
    AssaultNode(node, teamIdx);                             // update nodeinfo variables
    UpdateNodeWorldState(node, newState);                   // send mapicon
    PopulateNode(node);
}

// Update node world state
void BattleGroundAV::UpdateNodeWorldState(AVNodeIds node, uint32 newState)
{
    if (m_nodes[node].prevOwner == TEAM_INDEX_NEUTRAL)      // currently only snowfall is supported as neutral node
        GetBgMap()->GetVariableManager().SetVariable(BG_AV_STATE_GY_SNOWFALL_N, WORLD_STATE_REMOVE);
    else
        GetBgMap()->GetVariableManager().SetVariable(m_nodes[node].worldState, WORLD_STATE_REMOVE);

    m_nodes[node].worldState = newState;
    GetBgMap()->GetVariableManager().SetVariable(m_nodes[node].worldState, WORLD_STATE_ADD);
}

// Get text message id for each node
int32 BattleGroundAV::GetNodeMessageId(AVNodeIds node) const
{
    switch (node)
    {
        case BG_AV_NODES_FIRSTAID_STATION:  return LANG_BG_AV_NODE_GRAVE_STORM_AID;
        case BG_AV_NODES_DUNBALDAR_SOUTH:   return LANG_BG_AV_NODE_TOWER_DUN_S;
        case BG_AV_NODES_DUNBALDAR_NORTH:   return LANG_BG_AV_NODE_TOWER_DUN_N;
        case BG_AV_NODES_STORMPIKE_GRAVE:   return LANG_BG_AV_NODE_GRAVE_STORMPIKE;
        case BG_AV_NODES_ICEWING_BUNKER:    return LANG_BG_AV_NODE_TOWER_ICEWING;
        case BG_AV_NODES_STONEHEART_GRAVE:  return LANG_BG_AV_NODE_GRAVE_STONE;
        case BG_AV_NODES_STONEHEART_BUNKER: return LANG_BG_AV_NODE_TOWER_STONE;
        case BG_AV_NODES_SNOWFALL_GRAVE:    return LANG_BG_AV_NODE_GRAVE_SNOW;
        case BG_AV_NODES_ICEBLOOD_TOWER:    return LANG_BG_AV_NODE_TOWER_ICE;
        case BG_AV_NODES_ICEBLOOD_GRAVE:    return LANG_BG_AV_NODE_GRAVE_ICE;
        case BG_AV_NODES_TOWER_POINT:       return LANG_BG_AV_NODE_TOWER_POINT;
        case BG_AV_NODES_FROSTWOLF_GRAVE:   return LANG_BG_AV_NODE_GRAVE_FROST;
        case BG_AV_NODES_FROSTWOLF_ETOWER:  return LANG_BG_AV_NODE_TOWER_FROST_E;
        case BG_AV_NODES_FROSTWOLF_WTOWER:  return LANG_BG_AV_NODE_TOWER_FROST_W;
        case BG_AV_NODES_FROSTWOLF_HUT:     return LANG_BG_AV_NODE_GRAVE_FROST_HUT;
    }

    return 0;
}

void BattleGroundAV::AssaultNode(AVNodeIds node, PvpTeamIndex teamIdx)
{
    MANGOS_ASSERT(m_nodes[node].totalOwner != teamIdx);
    MANGOS_ASSERT(m_nodes[node].owner != teamIdx);
    // only assault an assaulted node if no totalowner exists:
    MANGOS_ASSERT(m_nodes[node].state != POINT_ASSAULTED || m_nodes[node].totalOwner == TEAM_INDEX_NEUTRAL);

    // the timer gets another time, if the previous owner was 0 == Neutral
    m_nodes[node].timer      = (m_nodes[node].prevOwner != TEAM_INDEX_NEUTRAL) ? BG_AV_CAPTIME : BG_AV_SNOWFALL_FIRSTCAP;
    m_nodes[node].prevOwner  = m_nodes[node].owner;
    m_nodes[node].owner      = teamIdx;
    m_nodes[node].prevState  = m_nodes[node].state;
    m_nodes[node].state      = POINT_ASSAULTED;
}

void BattleGroundAV::DestroyNode(AVNodeIds node)
{
    MANGOS_ASSERT(m_nodes[node].state == POINT_ASSAULTED);

    m_nodes[node].totalOwner = m_nodes[node].owner;
    m_nodes[node].prevOwner  = m_nodes[node].owner;
    m_nodes[node].prevState  = m_nodes[node].state;
    m_nodes[node].state      = POINT_CONTROLLED;
    m_nodes[node].timer      = 0;
}

// Initialize each battleground node
void BattleGroundAV::InitializeNode(AVNodeIds node)
{
    m_nodes[node].totalOwner = avNodeDefaults[node].initialOwner;
    m_nodes[node].owner      = avNodeDefaults[node].initialOwner;
    m_nodes[node].prevOwner  = avNodeDefaults[node].initialOwner;
    m_nodes[node].state      = POINT_CONTROLLED;
    m_nodes[node].prevState  = m_nodes[node].state;
    m_nodes[node].state      = POINT_CONTROLLED;
    m_nodes[node].timer      = 0;
    m_nodes[node].graveyardId = avNodeDefaults[node].graveyardId;
    m_nodes[node].worldState = avNodeDefaults[node].defaultWorldState;
    GetBgMap()->GetVariableManager().SetVariable(m_nodes[node].worldState, WORLD_STATE_ADD);
    m_activeEvents[node] = avNodeDefaults[node].initialOwner * BG_AV_MAX_STATES + m_nodes[node].state;

    if (avNodeDefaults[node].graveyardId)                                      // grave-creatures are special cause of a quest
    {
        m_activeEvents[node + BG_AV_MAX_NODES] = static_cast<uint8>(avNodeDefaults[node].initialOwner) * static_cast<uint8>(BG_AV_MAX_GRAVETYPES);

        // initialize graveyards
        Team team = TEAM_INVALID;
        if (avNodeDefaults[node].initialOwner != TEAM_INDEX_NEUTRAL)
            team = GetTeamIdByTeamIndex(avNodeDefaults[node].initialOwner);

        // initalize graveyard
        GetBgMap()->GetGraveyardManager().SetGraveYardLinkTeam(avNodeDefaults[node].graveyardId, BG_AV_ZONE_MAIN, team);
    }
}

void BattleGroundAV::DefendNode(AVNodeIds node, PvpTeamIndex teamIdx)
{
    MANGOS_ASSERT(m_nodes[node].totalOwner == teamIdx);
    MANGOS_ASSERT(m_nodes[node].owner != teamIdx);
    MANGOS_ASSERT(m_nodes[node].state != POINT_CONTROLLED);

    m_nodes[node].prevOwner  = m_nodes[node].owner;
    m_nodes[node].owner      = teamIdx;
    m_nodes[node].prevState  = m_nodes[node].state;
    m_nodes[node].state      = POINT_CONTROLLED;
    m_nodes[node].timer      = 0;
}

void BattleGroundAV::Reset()
{
    m_ending = true;
    CleanupSupportActors();
    m_supportState.Reset();
    BattleGround::Reset();
    m_ending = false;
    // set the reputation and honor variables:
    bool isBgWeekend = BattleGroundMgr::IsBgWeekend(GetTypeId());

    m_honorMapComplete    = (isBgWeekend) ? BG_AV_KILL_MAP_COMPLETE_HOLIDAY : BG_AV_KILL_MAP_COMPLETE;
    m_repTowerDestruction = (isBgWeekend) ? BG_AV_REP_TOWER_HOLIDAY         : BG_AV_REP_TOWER;
    m_repCaptain          = (isBgWeekend) ? BG_AV_REP_CAPTAIN_HOLIDAY       : BG_AV_REP_CAPTAIN;
    m_repBoss             = (isBgWeekend) ? BG_AV_REP_BOSS_HOLIDAY          : BG_AV_REP_BOSS;
    m_repOwnedGrave       = (isBgWeekend) ? BG_AV_REP_OWNED_GRAVE_HOLIDAY   : BG_AV_REP_OWNED_GRAVE;
    m_repSurviveCaptain   = (isBgWeekend) ? BG_AV_REP_SURVIVING_CAPTAIN_HOLIDAY : BG_AV_REP_SURVIVING_CAPTAIN;
    m_repSurviveTower     = (isBgWeekend) ? BG_AV_REP_SURVIVING_TOWER_HOLIDAY : BG_AV_REP_SURVIVING_TOWER;
    m_repOwnedMine        = (isBgWeekend) ? BG_AV_REP_OWNED_MINE_HOLIDAY    : BG_AV_REP_OWNED_MINE;

    // vanilla
    for (uint32 i = 0; i < BG_AV_MAX_MINES; ++i)
        for (uint32 k = 0; k < BG_AV_TEAMS_COUNT; ++k)
            GetBgMap()->GetVariableManager().SetVariableData(avMineWorldStates[i][k], true, 0, 0);

    for (auto& data : avNodeWorldStates)
    {
        GetBgMap()->GetVariableManager().SetVariableData(data.worldStateAlly, true, 0, 0);
        GetBgMap()->GetVariableManager().SetVariableData(data.worldStateHorde, true, 0, 0);
        GetBgMap()->GetVariableManager().SetVariableData(data.worldStateAllyGrey, true, 0, 0);
        GetBgMap()->GetVariableManager().SetVariableData(data.worldStateHordeGrey, true, 0, 0);
    }

    // tbc
    GetBgMap()->GetVariableManager().SetVariableData(BG_AV_STATE_SCORE_A, true, 0, 0);
    GetBgMap()->GetVariableManager().SetVariableData(BG_AV_STATE_SCORE_H, true, 0, 0);
    GetBgMap()->GetVariableManager().SetVariableData(BG_AV_STATE_SCORE_SHOW_H, true, 0, 0);
    GetBgMap()->GetVariableManager().SetVariableData(BG_AV_STATE_SCORE_SHOW_A, true, 0, 0);

    GetBgMap()->GetVariableManager().SetVariable(BG_AV_STATE_SCORE_A, BG_AV_SCORE_INITIAL_POINTS);
    GetBgMap()->GetVariableManager().SetVariable(BG_AV_STATE_SCORE_H, BG_AV_SCORE_INITIAL_POINTS);

    // initialize team variables
    for (uint8 i = 0; i < PVP_TEAM_COUNT; ++i)
    {
        m_wasInformedNearLose[i] = false;

        m_enemyTowersDestroyed[i] = 0;
        m_homeTowersControlled[i] = BG_AV_MAX_TOWERS_PER_TEAM;
        m_activeEvents[BG_AV_NODE_CAPTAIN_DEAD_A + i] = BG_EVENT_NONE;
    }

    // initialize mine variables and active events
    for (uint8 i = 0; i < BG_AV_MAX_MINES; ++i)
    {
        m_mineOwner[i] = TEAM_INDEX_NEUTRAL;
        GetBgMap()->GetVariableManager().SetVariable(avMineWorldStates[i][TEAM_INDEX_NEUTRAL], WORLD_STATE_ADD);
        m_mineTimer[i] = BG_AV_MINE_TICK_TIMER;

        m_activeEvents[BG_AV_MINE_BOSSES + i] = TEAM_INDEX_NEUTRAL;
        m_activeEvents[BG_AV_MINE_EVENT + i] = TEAM_INDEX_NEUTRAL;
    }

    m_activeEvents[BG_AV_CAPTAIN_A] = 0;
    m_activeEvents[BG_AV_CAPTAIN_H] = 0;
    m_activeEvents[BG_AV_HERALD] = 0;
    m_activeEvents[BG_AV_BOSS_A] = 0;
    m_activeEvents[BG_AV_BOSS_H] = 0;

    for (uint8 i = BG_AV_NODES_DUNBALDAR_SOUTH; i < BG_AV_MAX_NODES; ++i)  // towers
        m_activeEvents[BG_AV_MARSHAL_A_SOUTH + i - BG_AV_NODES_DUNBALDAR_SOUTH] = 0;

    // initialize all nodes
    for (uint8 i = 0; i < BG_AV_MAX_NODES; ++i)
        InitializeNode(AVNodeIds(i));

    // setup graveyards
    GetBgMap()->GetGraveyardManager().SetGraveYardLinkTeam(BG_AV_GRAVE_MAIN_ALLIANCE, BG_AV_ZONE_MAIN, ALLIANCE);
    GetBgMap()->GetGraveyardManager().SetGraveYardLinkTeam(BG_AV_GRAVE_MAIN_HORDE, BG_AV_ZONE_MAIN, HORDE);

    m_mineYellTimer = BG_AV_MINE_YELL;
}

// Handle battleground yells
void BattleGroundAV::DoSendYellToTeam(PvpTeamIndex teamIdx, int32 textId, uint8 nodeId, bool useNode /*= true*/, int32 mineLangId /*= 0*/)
{
    // Get the specific herald for each team
    Creature* pHerald = GetSingleCreatureFromStorage(teamIdx == TEAM_INDEX_ALLIANCE ? BG_AV_NPC_STORMPIKE_HERALD : BG_AV_NPC_FROSTWOLF_HERALD);
    if (!pHerald)
        return;

    // different types of yells, depending on parameters
    if (useNode)
    {
        if (mineLangId)
            SendYell2ToAll(textId, LANG_UNIVERSAL, pHerald, (teamIdx == TEAM_INDEX_ALLIANCE) ? LANG_BG_ALLY : LANG_BG_HORDE, mineLangId);
        else
            SendYell2ToAll(textId, LANG_UNIVERSAL, pHerald, GetNodeMessageId(AVNodeIds(nodeId)), (teamIdx == TEAM_INDEX_ALLIANCE) ? LANG_BG_ALLY : LANG_BG_HORDE);
    }
    else
        SendYellToAll(textId, LANG_UNIVERSAL, pHerald);
}

Team BattleGroundAV::GetPrematureWinner()
{
    int32 hordeScore = GetBgMap()->GetVariableManager().GetVariable(BG_AV_STATE_SCORE_H);
    int32 allianceScore = GetBgMap()->GetVariableManager().GetVariable(BG_AV_STATE_SCORE_A);

    if (hordeScore > allianceScore)
        return HORDE;
    if (allianceScore > hordeScore)
        return ALLIANCE;

    // If the values are equal, fall back to number of players on each team
    return BattleGround::GetPrematureWinner();
}

bool BattleGroundAV::IsConditionFulfilled(Player const* source, uint32 conditionId, WorldObject const* conditionSource, uint32 conditionSourceType)
{
    // ToDo: check condition for mine loot; check conditions for quest end script

    return false;
}
