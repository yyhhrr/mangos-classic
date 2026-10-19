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

#include "Maps/InstanceData.h"
#include "Database/DatabaseEnv.h"
#include "Maps/Map.h"

void InstanceData::SetBossLootItems(uint32 bossEntry, std::vector<uint32> const& itemIds)
{
    BossLootReward reward;
    if (!GetBossLootReward(bossEntry, reward) || !reward.gameObjectEntry || reward.completed)
        return;

    if (itemIds.empty())
        m_bossLootItems.erase(reward.gameObjectEntry);
    else
        m_bossLootItems[reward.gameObjectEntry] = std::make_pair(bossEntry, itemIds);
}

std::vector<uint32> const& InstanceData::GetBossLootItems(uint32 gameObjectEntry) const
{
    static std::vector<uint32> const emptyItems;
    auto itr = m_bossLootItems.find(gameObjectEntry);
    return itr == m_bossLootItems.end() ? emptyItems : itr->second.second;
}

std::vector<uint32> InstanceData::TakeBossLootItems(uint32 gameObjectEntry)
{
    auto itr = m_bossLootItems.find(gameObjectEntry);
    if (itr == m_bossLootItems.end())
        return {};

    BossLootReward reward;
    if (!GetBossLootReward(itr->second.first, reward) || reward.gameObjectEntry != gameObjectEntry || !reward.completed)
        return {};

    auto itemIds = std::move(itr->second.second);
    m_bossLootItems.erase(itr);
    return itemIds;
}

void InstanceData::ClearBossLootItems(uint32 gameObjectEntry)
{
    m_bossLootItems.erase(gameObjectEntry);
}

void InstanceData::SaveToDB() const
{
    // no reason to save BGs/Arenas
    if (instance->IsBattleGround())
        return;

    if (!Save())
        return;

    std::string data = Save();
    CharacterDatabase.escape_string(data);

    if (instance->Instanceable())
        CharacterDatabase.PExecute("UPDATE instance SET data = '%s' WHERE id = '%u'", data.c_str(), instance->GetInstanceId());
    else
        CharacterDatabase.PExecute("UPDATE world SET data = '%s' WHERE map = '%u'", data.c_str(), instance->GetId());
}

bool InstanceData::CheckConditionCriteriaMeet(Player const* /*source*/, uint32 instance_condition_id, WorldObject const* /*conditionSource*/, uint32 conditionSourceType) const
{
    sLog.outError("Condition system call InstanceData::CheckConditionCriteriaMeet but instance script for map %u not have implementation for player condition criteria with internal id %u (called from %u)",
                  instance->GetId(), instance_condition_id, uint32(conditionSourceType));
    return false;
}
