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
SDName: Instance_Molten_Core
SD%Complete: 100
SDComment:
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "molten_core.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/CreatureLinkingMgr.h"
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnManager.h"
#include <array>

namespace
{
struct EncounterSpawnGroup
{
    uint32 encounter;
    uint32 groupId;
};

constexpr EncounterSpawnGroup ENCOUNTER_SPAWN_GROUPS[] =
{
    {TYPE_LUCIFRON, 4090000},
    {TYPE_GEHENNAS, 4090002},
    {TYPE_GARR, 4090001},
};

constexpr size_t CORE_HOUND_PACK_SIZE = 5;
constexpr auto CORE_HOUND_REVIVE_DELAY = std::chrono::seconds(10);
constexpr uint32 SPELL_CORE_HOUND_REVIVE_VISUAL = 19823;
constexpr int32 EMOTE_CORE_HOUND_FALL = 7866;
constexpr int32 EMOTE_CORE_HOUND_REVIVE = 7867;

class MoltenCoreHoundAI : public CreatureEventAI
{
public:
    explicit MoltenCoreHoundAI(Creature* creature) : CreatureEventAI(creature) {}

    bool HandleLethalDamage(Unit* dealer) override
    {
        if (IsDown())
            return true;

        auto* data = dynamic_cast<instance_molten_core*>(m_creature->GetInstanceData());
        if (!data || !data->QueueCoreHoundPack(*m_creature))
            return false;

        m_downedAt = m_creature->GetMap()->GetCurrentClockTime();
        if (dealer)
        {
            m_defeater = dealer->GetObjectGuid();
            if (!m_creature->IsInCombat() && dealer->IsAlive() && m_creature->CanAttack(dealer))
                m_creature->EngageInCombatWith(dealer);
        }

        m_creature->SetHealth(1);
        m_creature->AttackStop(true, true, true);
        m_creature->RemoveAllAttackers();
        DoFakeDeath();
        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
        m_creature->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        SetCombatScriptStatus(true);
        SetCombatMovement(false);
        SetMeleeEnabled(false);
        DoBroadcastText(EMOTE_CORE_HOUND_FALL, m_creature, nullptr, CHAT_TYPE_TEXT_EMOTE);
        return true;
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        if (IsDown() && damageType != INSTAKILL)
            damage = 0;
        else
            CreatureEventAI::DamageTaken(dealer, damage, damageType, spellInfo);
    }

    void UpdateAI(uint32 diff) override
    {
        if (!IsDown())
            CreatureEventAI::UpdateAI(diff);
    }

    void AttackStart(Unit* target) override
    {
        if (!IsDown())
            CreatureEventAI::AttackStart(target);
    }

    void Reset() override
    {
        RestoreStanding();
        CreatureEventAI::Reset();
    }

    void EnterEvadeMode() override
    {
        if (IsDown())
            m_creature->SetHealth(m_creature->GetMaxHealth());
        RestoreStanding();
        CreatureEventAI::EnterEvadeMode();
    }

    bool IsDown() const { return m_downedAt != TimePoint{}; }
    TimePoint GetDownedAt() const { return m_downedAt; }

    Unit* GetCombatTarget() const
    {
        if (!m_creature->IsInCombat() || m_creature->GetCombatManager().IsEvadingHome())
            return nullptr;

        for (auto const* ref : m_creature->getThreatManager().getThreatList())
            if (Unit* target = m_creature->GetMap()->GetUnit(ref->getUnitGuid()))
                if (target->IsAlive() && !target->IsFeigningDeath() && m_creature->CanAttack(target))
                    return target;

        return nullptr;
    }

    void Revive(Unit* target)
    {
        RestoreStanding();
        m_creature->SetHealth(m_creature->GetMaxHealth());
        DoCastSpellIfCan(nullptr, SPELL_CORE_HOUND_REVIVE_VISUAL, CAST_TRIGGERED);
        DoBroadcastText(EMOTE_CORE_HOUND_REVIVE, m_creature, nullptr, CHAT_TYPE_TEXT_EMOTE);
        AttackStart(target);
    }

    void FinishDeath()
    {
        Unit* killer = m_creature->GetMap()->GetUnit(m_defeater);
        if (!killer)
            killer = m_creature->GetLootRecipient();

        // Keep the damage guard until EventAI calls Reset from the real death callback.
        Unit::Kill(killer, m_creature, DIRECT_DAMAGE, nullptr, false, false);
    }

private:
    void RestoreStanding()
    {
        if (!IsDown())
            return;

        m_downedAt = {};
        m_defeater.Clear();
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        SetCombatScriptStatus(false);
        SetCombatMovement(true);
        SetMeleeEnabled(true);
    }

    TimePoint m_downedAt{};
    ObjectGuid m_defeater;
};
}

static SpawnLocation m_aBosspawnLocs[MAX_MAJORDOMO_ADDS] =
{
    {NPC_FLAMEWAKER_ELITE,  737.945f, -1156.48f, -118.945f, 4.46804f, 0, 0},
    {NPC_FLAMEWAKER_ELITE,  752.520f, -1191.02f, -118.218f, 2.49582f, 0, 0},
    {NPC_FLAMEWAKER_ELITE,  752.953f, -1163.94f, -118.869f, 3.70010f, 0, 0},
    {NPC_FLAMEWAKER_ELITE,  738.814f, -1197.40f, -118.018f, 1.83260f, 0, 0},
    {NPC_FLAMEWAKER_HEALER, 746.939f, -1194.87f, -118.016f, 2.21657f, 0, 0},
    {NPC_FLAMEWAKER_HEALER, 747.132f, -1158.87f, -118.897f, 4.03171f, 0, 0},
    {NPC_FLAMEWAKER_HEALER, 757.116f, -1170.12f, -118.793f, 3.40339f, 0, 0},
    {NPC_FLAMEWAKER_HEALER, 755.910f, -1184.46f, -118.449f, 2.80998f, 0, 0}
};

instance_molten_core::instance_molten_core(Map* pMap) : ScriptedInstance(pMap)
{
    Initialize();
}

void instance_molten_core::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
}

void instance_molten_core::Update(uint32 /*diff*/)
{
    UpdateCoreHoundPacks();

    for (auto const& encounterGroup : ENCOUNTER_SPAWN_GROUPS)
    {
        if (GetData(encounterGroup.encounter) != FAIL)
            continue;

        SpawnGroup* spawnGroup = instance->GetSpawnManager().GetSpawnGroup(encounterGroup.groupId);
        CreatureGroup* group = spawnGroup ? spawnGroup->GetCreatureGroup() : nullptr;
        // The boss may reach home while a surviving guard is still fighting or returning.
        if (group && (!group->IsOutOfCombat() || group->IsEvading()))
            continue;

        if (group)
            group->ClearRespawnTimes();
        SetData(encounterGroup.encounter, NOT_STARTED);
    }
}

void instance_molten_core::LoadCoreHoundPacks()
{
    auto collect = [this](CreatureDataPair const& spawn)
    {
        if (spawn.second.id != NPC_CORE_HOUND || spawn.second.mapid != instance->GetId())
            return false;

        auto const* link = sCreatureLinkingMgr.GetLinkedTriggerInformation(NPC_CORE_HOUND, spawn.first, instance->GetId());
        if (!link || !(link->linkingFlag & FLAG_CANT_SPAWN_IF_BOSS_DEAD))
            return false;

        auto const* master = sObjectMgr.GetCreatureData(link->masterDBGuid);
        if (master && master->mapid == instance->GetId())
        {
            if (master->id == NPC_MAGMADAR)
                m_coreHoundPacks[spawn.first].push_back(spawn.first);
            else if (master->id == NPC_CORE_HOUND &&
                     (link->linkingFlag & FLAG_AGGRO_ON_AGGRO) && (link->linkingFlag & FLAG_TO_AGGRO_ON_AGGRO))
                m_coreHoundPacks[link->masterDBGuid].push_back(spawn.first);
        }
        return false;
    };
    sObjectMgr.DoCreatureData(collect);

    for (auto pack = m_coreHoundPacks.begin(); pack != m_coreHoundPacks.end();)
    {
        auto const& members = pack->second;
        if (members.size() != CORE_HOUND_PACK_SIZE || std::find(members.begin(), members.end(), pack->first) == members.end())
            pack = m_coreHoundPacks.erase(pack);
        else
            ++pack;
    }
    m_coreHoundPacksLoaded = true;
}

bool instance_molten_core::QueueCoreHoundPack(Creature const& hound)
{
    if (!m_coreHoundPacksLoaded)
        LoadCoreHoundPacks();

    for (auto const& [leader, members] : m_coreHoundPacks)
    {
        if (std::find(members.begin(), members.end(), hound.GetDbGuid()) == members.end())
            continue;

        for (uint32 guid : members)
        {
            Creature* member = instance->GetCreature(guid);
            if (!member || !member->IsAlive() || member->GetEntry() != NPC_CORE_HOUND || !dynamic_cast<MoltenCoreHoundAI*>(member->AI()))
                return false;
            if (guid == hound.GetDbGuid() && member != &hound)
                return false;
        }

        m_pendingCoreHoundPacks.insert(leader);
        return true;
    }
    return false;
}

void instance_molten_core::UpdateCoreHoundPacks()
{
    auto const now = instance->GetCurrentClockTime();
    for (auto pack = m_pendingCoreHoundPacks.begin(); pack != m_pendingCoreHoundPacks.end();)
    {
        std::array<MoltenCoreHoundAI*, CORE_HOUND_PACK_SIZE> downed{};
        size_t downedCount = 0;
        bool missingMember = false;
        Unit* target = nullptr;
        TimePoint firstDown = TimePoint::max();
        TimePoint lastDown{};
        for (uint32 guid : m_coreHoundPacks.at(*pack))
        {
            Creature* member = instance->GetCreature(guid);
            auto* ai = member && member->IsAlive() ? dynamic_cast<MoltenCoreHoundAI*>(member->AI()) : nullptr;
            if (!ai)
            {
                missingMember = true;
                continue;
            }
            if (!target)
                target = ai->GetCombatTarget();
            if (ai->IsDown())
            {
                downed[downedCount++] = ai;
                firstDown = std::min(firstDown, ai->GetDownedAt());
                lastDown = std::max(lastDown, ai->GetDownedAt());
            }
        }

        if (downedCount == 0)
        {
            pack = m_pendingCoreHoundPacks.erase(pack);
            continue;
        }
        // The map updates creature combat before instance scripts, so compare the actual fall times.
        if (downedCount == CORE_HOUND_PACK_SIZE && lastDown - firstDown < CORE_HOUND_REVIVE_DELAY)
        {
            pack = m_pendingCoreHoundPacks.erase(pack);
            for (auto* ai : downed)
                ai->FinishDeath();
            continue;
        }

        if (missingMember || !target)
        {
            pack = m_pendingCoreHoundPacks.erase(pack);
            for (size_t i = 0; i < downedCount; ++i)
                downed[i]->EnterEvadeMode();
            continue;
        }

        size_t revivedCount = 0;
        for (size_t i = 0; i < downedCount; ++i)
            if (now - downed[i]->GetDownedAt() >= CORE_HOUND_REVIVE_DELAY)
            {
                Unit* ownTarget = downed[i]->GetCombatTarget();
                downed[i]->Revive(ownTarget ? ownTarget : target);
                ++revivedCount;
            }

        if (revivedCount == downedCount)
            pack = m_pendingCoreHoundPacks.erase(pack);
        else
            ++pack;
    }
}

bool instance_molten_core::CanSpawnCreatureGroup(uint32 groupId) const
{
    for (auto const& encounterGroup : ENCOUNTER_SPAWN_GROUPS)
        if (encounterGroup.groupId == groupId)
            return GetData(encounterGroup.encounter) == NOT_STARTED;
    return true;
}

bool instance_molten_core::IsEncounterInProgress() const
{
    for (auto const& encounterGroup : ENCOUNTER_SPAWN_GROUPS)
        if (GetData(encounterGroup.encounter) == FAIL)
            return true;

    for (uint32 i : m_auiEncounter)
    {
        if (i == IN_PROGRESS)
            return true;
    }

    return false;
}

void instance_molten_core::OnPlayerEnter(Player* /*pPlayer*/)
{
    // Summon Majordomo if can
    DoSpawnMajordomoIfCan(true);
}

void instance_molten_core::OnCreatureCreate(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        // Bosses
        case NPC_GARR:
        case NPC_SULFURON:
        case NPC_MAJORDOMO:
        case NPC_LUCIFRON:
        case NPC_MAGMADAR:
        case NPC_GEHENNAS:
        case NPC_SHAZZRAH:
        case NPC_GEDDON:
        case NPC_GOLEMAGG:
        case NPC_RAGNAROS:
            m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            break;
    }
}

void instance_molten_core::OnObjectCreate(GameObject* pGo)
{
    switch (pGo->GetEntry())
    {
        // Runes
        case GO_RUNE_KRESS:
        case GO_RUNE_MOHN:
        case GO_RUNE_BLAZ:
        case GO_RUNE_MAZJ:
        case GO_RUNE_ZETH:
        case GO_RUNE_THERI:
        case GO_RUNE_KORO:
            // Activate the rune if it was previously doused by a player (encounter set to SPECIAL)
            m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
            for (auto m_aMoltenCoreRune : m_aMoltenCoreRunes)
            {
                if (m_aMoltenCoreRune.m_uiRuneEntry == pGo->GetEntry() && GetData(m_aMoltenCoreRune.m_uiType) == SPECIAL)
                {
                    pGo->UseDoorOrButton();
                    break;
                }
            }
            break;
        // Runes' Flames Circles
        case GO_CIRCLE_MAGMADAR:
        case GO_CIRCLE_GEHENNAS:
        case GO_CIRCLE_GARR:
        case GO_CIRCLE_SHAZZRAH:
        case GO_CIRCLE_BARON_GEDDON:
        case GO_CIRCLE_SULFURON:
        case GO_CIRCLE_GOLEMAGG:
            // Delete the Flames Circle around the rune if the boss guarding it is killed
            m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
            for (auto m_aMoltenCoreRune : m_aMoltenCoreRunes)
            {
                if (m_aMoltenCoreRune.m_uiFlamesCircleEntry == pGo->GetEntry() && (GetData(m_aMoltenCoreRune.m_uiType) == SPECIAL || GetData(m_aMoltenCoreRune.m_uiType) == DONE))
                {
                    pGo->SetLootState(GO_JUST_DEACTIVATED);
                    break;
                }
            }
            break;

        // Majordomo event chest
        case GO_CACHE_OF_THE_FIRE_LORD:
        // Ragnaros GOs
        case GO_LAVA_STEAM:
        case GO_LAVA_SPLASH:
            m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
            break;
    }
    for (uint32 eventId : m_resetRuneEvents)
        if (GetData(eventId) == NOT_STARTED)
            RestoreResetRune(pGo, eventId);
}

void instance_molten_core::SetData(uint32 uiType, uint32 uiData)
{
    switch (uiType)
    {
        case TYPE_LUCIFRON:
            if (m_auiEncounter[uiType] == DONE)
                return;
            m_auiEncounter[uiType] = uiData;
            break;
        case TYPE_GEHENNAS:
        case TYPE_GARR:
            if (GetData(uiType) == SPECIAL || (GetData(uiType) == DONE && uiData != SPECIAL))
                return;
            [[fallthrough]];
        case TYPE_MAGMADAR:
        case TYPE_SHAZZRAH:
        case TYPE_GEDDON:
        case TYPE_GOLEMAGG:
        case TYPE_SULFURON:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
            {
                for (auto m_aMoltenCoreRune : m_aMoltenCoreRunes)
                {
                    if (m_aMoltenCoreRune.m_uiType == uiType)
                    {
                        if (GameObject* pGo = GetSingleGameObjectFromStorage(m_aMoltenCoreRune.m_uiFlamesCircleEntry))
                            pGo->SetLootState(GO_JUST_DEACTIVATED);
                        break;
                    }
                }
            }
            break;
        case TYPE_MAJORDOMO:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
                DoRespawnGameObject(GO_CACHE_OF_THE_FIRE_LORD, HOUR);
            break;
        case TYPE_RAGNAROS:
            m_auiEncounter[uiType] = uiData;
            break;
    }

    // Check if Majordomo can be summoned
    if (uiData == SPECIAL)
        DoSpawnMajordomoIfCan(false);

    // Restore dead group members if the instance unloads during a pull.
    bool const saveGroupReset = (uiType == TYPE_GEHENNAS || uiType == TYPE_GARR) &&
                                (uiData == IN_PROGRESS || uiData == NOT_STARTED);
    if (uiData == DONE || uiData == SPECIAL || saveGroupReset)
    {
        OUT_SAVE_INST_DATA;

        std::ostringstream saveStream;
        saveStream << m_auiEncounter[0] << " " << m_auiEncounter[1] << " " << m_auiEncounter[2] << " "
                   << m_auiEncounter[3] << " " << m_auiEncounter[4] << " " << m_auiEncounter[5] << " "
                   << m_auiEncounter[6] << " " << m_auiEncounter[7] << " " << m_auiEncounter[8] << " "
                   << m_auiEncounter[9];

        m_strInstData = saveStream.str();

        SaveToDB();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

bool instance_molten_core::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    if (bossEntry != NPC_MAJORDOMO)
        return false;

    reward.gameObjectEntry = GO_CACHE_OF_THE_FIRE_LORD;
    reward.completed = GetData(TYPE_MAJORDOMO) == DONE;
    return true;
}

uint32 instance_molten_core::GetData(uint32 uiType) const
{
    if (uiType < MAX_ENCOUNTER)
        return m_auiEncounter[uiType];

    return 0;
}

std::vector<InstanceEventReset> instance_molten_core::GetResettableEvents() const
{
    return
    {
        {TYPE_LUCIFRON, "Lucifron", {NPC_LUCIFRON}},
        {TYPE_MAGMADAR, "Magmadar", {NPC_MAGMADAR}},
        {TYPE_GEHENNAS, "Gehennas", {NPC_GEHENNAS}},
        {TYPE_GARR, "Garr", {NPC_GARR}},
        {TYPE_SHAZZRAH, "Shazzrah", {NPC_SHAZZRAH}},
        {TYPE_GEDDON, "Baron Geddon", {NPC_GEDDON}},
        {TYPE_GOLEMAGG, "Golemagg", {NPC_GOLEMAGG}},
        {TYPE_SULFURON, "Sulfuron Harbinger", {NPC_SULFURON}},
        {TYPE_MAJORDOMO, "Majordomo Executus", {NPC_MAJORDOMO}, GO_CACHE_OF_THE_FIRE_LORD},
        {TYPE_RAGNAROS, "Ragnaros", {NPC_RAGNAROS}}
    };
}

bool instance_molten_core::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId >= MAX_ENCOUNTER)
        return false;
    std::ostringstream stream;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i)
            stream << ' ';
        stream << (i == eventId ? uint32(NOT_STARTED) : m_auiEncounter[i]);
    }
    data = stream.str();
    return true;
}

void instance_molten_core::RestoreResetRune(GameObject* go, uint32 eventId)
{
    if (!go || !go->HasStaticDBSpawnData())
        return;
    for (sRuneEncounters const& rune : m_aMoltenCoreRunes)
    {
        if (rune.m_uiType != eventId ||
            (go->GetEntry() != rune.m_uiRuneEntry && go->GetEntry() != rune.m_uiFlamesCircleEntry))
            continue;
        go->Respawn();
        go->ResetDoorOrButton();
        go->ClearAllUsesData();
        go->SetUInt32Value(GAMEOBJECT_FLAGS, go->GetGOInfo()->flags);
        go->SetLootState(GO_READY);
        GameObjectData const* spawn = sObjectMgr.GetGOData(go->GetDbGuid());
        go->SetGoState(spawn ? GOState(spawn->goState) : GO_STATE_READY);
        break;
    }
}

void instance_molten_core::RestoreResetRuneObjects(uint32 eventId)
{
    for (sRuneEncounters const& rune : m_aMoltenCoreRunes)
    {
        if (rune.m_uiType != eventId)
            continue;
        auto restoreSpawn = [this, &rune, eventId](GameObjectDataPair const& pair)
        {
            GameObjectData const& spawn = pair.second;
            if (spawn.mapid != instance->GetId() ||
                (spawn.id != rune.m_uiRuneEntry && spawn.id != rune.m_uiFlamesCircleEntry))
                return false;
            instance->GetPersistentState()->SaveGORespawnTime(pair.first, 0);
            if (GameObject* go = instance->GetGameObject(pair.first))
                RestoreResetRune(go, eventId);
            else if (sObjectMgr.IsGameObjectDbGuidDynGuided(pair.first))
                instance->GetSpawnManager().RespawnGameObject(pair.first);
            return false;
        };
        sObjectMgr.DoGOData(restoreSpawn);
        break;
    }
}

void instance_molten_core::RemoveResetActors(bool majordomo, bool ragnaros)
{
    std::vector<ObjectGuid> actors;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
    {
        Creature* creature = itr->second;
        if (!creature->IsTemporarySummon())
            continue;
        uint32 entry = creature->GetEntry();
        uint32 ownerEntry = creature->GetSpawnerGuid().GetEntry();
        if ((majordomo && (entry == NPC_MAJORDOMO || (ownerEntry == NPC_MAJORDOMO && entry != NPC_RAGNAROS))) ||
            (ragnaros && (entry == NPC_RAGNAROS || ownerEntry == NPC_RAGNAROS)))
            actors.push_back(itr->first);
    }
    for (ObjectGuid guid : actors)
        if (Creature* creature = instance->GetCreature(guid))
        {
            auto stored = m_npcEntryGuidStore.find(creature->GetEntry());
            if (stored != m_npcEntryGuidStore.end() && stored->second == guid)
                m_npcEntryGuidStore.erase(stored);
            creature->ForcedDespawn();
        }
}

void instance_molten_core::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId >= MAX_ENCOUNTER)
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

    if (eventId >= TYPE_MAGMADAR && eventId <= TYPE_SULFURON)
    {
        m_resetRuneEvents.insert(eventId);
        RestoreResetRuneObjects(eventId);
        // An unfinished Majordomo must wait for all seven runes again. His
        // completed event and Ragnaros progress remain independent records.
        if (GetData(TYPE_MAJORDOMO) != DONE)
            RemoveResetActors(true, false);
    }
    else if (eventId == TYPE_MAJORDOMO || eventId == TYPE_RAGNAROS)
    {
        bool resetRagnarosScene = eventId == TYPE_RAGNAROS || GetData(TYPE_RAGNAROS) != DONE;
        RemoveResetActors(true, resetRagnarosScene);
        if (eventId == TYPE_MAJORDOMO)
            ResetEventRewardGameObject(GO_CACHE_OF_THE_FIRE_LORD);
        if (resetRagnarosScene)
        {
            ResetEventRewardGameObject(GO_LAVA_STEAM);
            ResetEventRewardGameObject(GO_LAVA_SPLASH);
        }
        // A new actor also clears Majordomo AI's private completed flag and
        // any old summon dialogue/Ragnaros GUID. Static bosses use RSP later.
        if (GetData(TYPE_MAJORDOMO) == DONE)
        {
            if (Player* player = GetPlayerInMap())
                SpawnMajordomo(player, false, 1);
        }
        else
            DoSpawnMajordomoIfCan(true);
    }
}

// Handle Majordomo summon here
void instance_molten_core::DoSpawnMajordomoIfCan(bool bByPlayerEnter)
{
    // If both Majordomo and Ragnaros events are finished, return
    if (m_auiEncounter[TYPE_MAJORDOMO] == DONE && m_auiEncounter[TYPE_RAGNAROS] == DONE)
        return;

    // If already spawned return
    if (GetSingleCreatureFromStorage(NPC_MAJORDOMO, true))
        return;

    // Check if all rune bosses are done
    for (uint8 i = TYPE_MAGMADAR; i < TYPE_MAJORDOMO; ++i)
    {
        if (m_auiEncounter[i] != SPECIAL)
            return;
    }

    Player* pPlayer = GetPlayerInMap();
    if (!pPlayer)
        return;

    uint8 uiSummonPos = m_auiEncounter[TYPE_MAJORDOMO] == DONE ? 1 : 0;
    SpawnMajordomo(pPlayer, !bByPlayerEnter, uiSummonPos);
}

void instance_molten_core::SpawnMajordomo(Unit* summoner, bool initialSummon, uint8 summonPos)
{
    // Summon Majordomo
    // If Majordomo encounter isn't done, summon at encounter place, else near Ragnaros
    if (Creature* pMajordomo = summoner->SummonCreature(m_aMajordomoLocations[summonPos].m_spawnEntry, m_aMajordomoLocations[summonPos].m_fx, m_aMajordomoLocations[summonPos].m_fy, m_aMajordomoLocations[summonPos].m_fz, m_aMajordomoLocations[summonPos].m_fo, TEMPSPAWN_MANUAL_DESPAWN, 2 * HOUR * IN_MILLISECONDS))
    {
        if (summonPos)                                      // Majordomo encounter already done, set faction
        {
            pMajordomo->SetFactionTemporary(FACTION_MAJORDOMO_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);
            pMajordomo->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            pMajordomo->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        }
        else                                                // Else yell and summon adds
        {
            if (initialSummon)
                DoScriptText(SAY_MAJORDOMO_SPAWN, pMajordomo);

            for (auto& m_aBosspawnLoc : m_aBosspawnLocs)
                pMajordomo->SummonCreature(m_aBosspawnLoc.m_spawnEntry, m_aBosspawnLoc.m_fx, m_aBosspawnLoc.m_fy, m_aBosspawnLoc.m_fz, m_aBosspawnLoc.m_fo, TEMPSPAWN_MANUAL_DESPAWN, DAY * IN_MILLISECONDS);
        }
    }
}

void instance_molten_core::Load(const char* chrIn)
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
               >> m_auiEncounter[8] >> m_auiEncounter[9];

    // Restore the group together if a saved pull was interrupted by a map reload.
    for (auto const& encounterGroup : ENCOUNTER_SPAWN_GROUPS)
        if (GetData(encounterGroup.encounter) == IN_PROGRESS)
            m_auiEncounter[encounterGroup.encounter] = FAIL;

    for (uint32& i : m_auiEncounter)
    {
        if (i == IN_PROGRESS)
            i = NOT_STARTED;
    }

    OUT_LOAD_INST_DATA_COMPLETE;
}

void instance_molten_core::ShowChatCommands(ChatHandler* handler)
{
    handler->SendSysMessage("This instance supports the following commands:\n spawnmajordomo(0 - initial,1 - ragnaros)");
}

void instance_molten_core::ExecuteChatCommand(ChatHandler* handler, char* args)
{
    char* result = handler->ExtractLiteralArg(&args);
    if (!result)
        return;
    std::string val = result;
    if (val == "spawnmajordomo")
    {
        uint32 summonPos;
        handler->ExtractUInt32(&args, summonPos);

        if (summonPos > 1)
        {
            handler->PSendSysMessage("Could not spawn majordomo because spawn %u was invalid. Please specify a value between 0-1", summonPos);
            return;
        }

        SpawnMajordomo(handler->GetSession()->GetPlayer(), true, summonPos);
    }
}

InstanceData* GetInstance_instance_molten_core(Map* pMap)
{
    return new instance_molten_core(pMap);
}

void AddSC_instance_molten_core()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_molten_core";
    pNewScript->GetInstanceData = &GetInstance_instance_molten_core;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_molten_core_hound";
    pNewScript->GetAI = &GetNewAIInstance<MoltenCoreHoundAI>;
    pNewScript->RegisterSelf();
}
