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
SDName: instance_dire_maul
SD%Complete: 70
SDComment: East CreatureAI scenes use a versioned coordinator; West and North retain their existing behaviour
SDCategory: Dire Maul
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"


namespace
{
bool ParseDireMaulSave(char const* text, std::array<uint32, MAX_ENCOUNTER>& prefix, std::string& east)
{
    if (!text || std::char_traits<char>::length(text) > 65536) return false;
    std::istringstream in(text);
    for (uint32& value : prefix)
    {
        std::string token;
        if (!(in >> token) || token.size() != 1 || token[0] < '0' || token[0] > '0' + SPECIAL) return false;
        value = uint32(token[0] - '0');
    }
    in >> std::ws;
    east.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}
}

std::string instance_dire_maul::BuildSave(std::array<uint32, MAX_ENCOUNTER> const* prefix) const
{
    std::ostringstream out;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i) out << ' ';
        out << (prefix ? (*prefix)[i] : GetData(i));
    }
    out << ' ' << m_east.Save();
    return out.str();
}
void instance_dire_maul::SaveState() { m_strInstData = BuildSave(); SaveToDB(); }
void instance_dire_maul::OnCreatureRespawn(Creature* actor) { m_east.OnCreatureRespawn(actor); }
void instance_dire_maul::OnCreatureEvade(Creature* actor) { m_east.OnCreatureEvade(actor); }
void instance_dire_maul::OnCreatureDespawn(Creature* actor) { m_east.OnCreatureDespawn(actor); }
void instance_dire_maul::SetData64(uint32 type, uint64 value)
{
    if (type == 1002)
        if (Creature* trigger = instance->GetCreature(ObjectGuid(value))) m_east.RequestIsalien(trigger);
}
bool instance_dire_maul::GetGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string& snapshot) const
{
    return m_east.GetFelvineLootSnapshot(guid, entry, snapshot);
}
void instance_dire_maul::OnGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string const& snapshot, bool exhausted)
{
    m_east.OnFelvineLootSnapshot(guid, entry, snapshot, exhausted);
}

instance_dire_maul::instance_dire_maul(Map* pMap) : ScriptedInstance(pMap),
    m_bDoNorthBeforeWest(false),
    m_east(*this, [this]() { SaveState(); })
{
    Initialize();
}

void instance_dire_maul::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_strInstData = BuildSave();
}

void instance_dire_maul::Update(uint32 diff)
{
    m_east.Update(diff);
    if (GetData(TYPE_DREADSTEED) == IN_PROGRESS)
    {
        // While J'eevee is spawned, failure condition is simply no players found
        if (Creature* imp = instance->GetCreature(m_npcEntryGuidStore[NPC_JEEVEE]))
        {
            if (instance->GetPlayersCountExceptGMs() == 0)
                SetData(TYPE_DREADSTEED, FAIL);
        }
        else
        {
            GameObject* bell = instance->GetGameObject(m_goEntryGuidStore[GO_BELL_OF_DETHMOORA]);
            GameObject* wheel = instance->GetGameObject(m_goEntryGuidStore[GO_WHEEL_OF_BLACK_MARCH]);
            GameObject* candle = instance->GetGameObject(m_goEntryGuidStore[GO_DOOMSDAY_CANDLE]);
            if (bell && bell->GetLootState() != GO_ACTIVATED && wheel && wheel->GetLootState() != GO_ACTIVATED && candle && candle->GetLootState() != GO_ACTIVATED)
                SetData(TYPE_DREADSTEED, FAIL);
        }
    }
}

void instance_dire_maul::OnPlayerEnter(Player* pPlayer)
{
    m_east.OnPlayerEnter(pPlayer);
    // figure where to enter to set library doors accordingly
    // Enter DM North first
    if (pPlayer->IsWithinDist2d(260.0f, -20.0f, 20.0f) && m_auiEncounter[TYPE_WARPWOOD] != DONE)
        m_bDoNorthBeforeWest = true;
    else
        m_bDoNorthBeforeWest = false;

    DoToggleGameObjectFlags(GO_WEST_LIBRARY_DOOR, GO_FLAG_NO_INTERACT, m_bDoNorthBeforeWest);
    DoToggleGameObjectFlags(GO_WEST_LIBRARY_DOOR, GO_FLAG_LOCKED, !m_bDoNorthBeforeWest);
}

void instance_dire_maul::OnPlayerLeave(Player* pPlayer) {
    m_east.OnPlayerLeave(pPlayer);
    // Remove King of the Gordok aura
    if (pPlayer->HasAura(SPELL_KING_OF_GORDOK))
        pPlayer->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
}

void instance_dire_maul::OnCreatureCreate(Creature* pCreature)
{
    m_east.OnCreatureCreate(pCreature);
    switch (pCreature->GetEntry())
    {
        // East
        case NPC_OLD_IRONBARK:
        case NPC_IRONBARK_REDEEMED:
            break;

        // West
        case NPC_PRINCE_TORTHELDRIN:
            if (m_auiEncounter[TYPE_IMMOLTHAR] == DONE)
                pCreature->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_PLAYER);
            break;
        case NPC_ARCANE_ABERRATION:
        case NPC_MANA_REMNANT:
            m_lGeneratorGuardGUIDs.push_back(pCreature->GetObjectGuid());
            return;
        case NPC_IMMOLTHAR:
        case NPC_WARLOCK_DUMMY_INFERNAL:
        case NPC_JEEVEE:
            break;
        case NPC_HIGHBORNE_SUMMONER:
            m_luiHighborneSummonerGUIDs.push_back(pCreature->GetObjectGuid());
            return;

        // North
        case NPC_CHO_RUSH_THE_OBSERVER:
            if (m_auiEncounter[TYPE_KING_GORDOK] == DONE)
                pCreature->SetStandState(UNIT_STAND_STATE_SIT);
            break;
        case NPC_KING_GORDOK:
        case NPC_CAPTAIN_KROMCRUSH:
        case NPC_GUARD_SLIPKIK:
            break;

        default:
            return;
    }
    m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
}

void instance_dire_maul::OnObjectCreate(GameObject* pGo)
{
    m_east.OnObjectCreate(pGo);
    switch (pGo->GetEntry())
    {
        // East object state is synchronized by the versioned coordinator.
        case GO_CONSERVATORY_DOOR:
        case GO_CRUMBLE_WALL:
        case GO_CORRUPT_VINE:
        case GO_FELVINE_SHARD:
            break;

        // West
        case GO_CRYSTAL_GENERATOR_1:
            m_aCrystalGeneratorGuid[0] = pGo->GetObjectGuid();
            if (m_auiEncounter[TYPE_PYLON_1] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            return;
        case GO_CRYSTAL_GENERATOR_2:
            m_aCrystalGeneratorGuid[1] = pGo->GetObjectGuid();
            if (m_auiEncounter[TYPE_PYLON_2] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            return;
        case GO_CRYSTAL_GENERATOR_3:
            m_aCrystalGeneratorGuid[2] = pGo->GetObjectGuid();
            if (m_auiEncounter[TYPE_PYLON_3] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            return;
        case GO_CRYSTAL_GENERATOR_4:
            m_aCrystalGeneratorGuid[3] = pGo->GetObjectGuid();
            if (m_auiEncounter[TYPE_PYLON_4] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            return;
        case GO_CRYSTAL_GENERATOR_5:
            m_aCrystalGeneratorGuid[4] = pGo->GetObjectGuid();
            if (m_auiEncounter[TYPE_PYLON_5] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            return;
        case GO_FORCEFIELD:
            if (CheckAllGeneratorsDestroyed())
                pGo->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_PRINCES_CHEST_AURA:
            break;
        case GO_WARPWOOD_DOOR:
            if (m_auiEncounter[TYPE_WARPWOOD] == DONE)
                pGo->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_WEST_LIBRARY_DOOR:
            pGo->SetFlag(GAMEOBJECT_FLAGS, m_bDoNorthBeforeWest ? GO_FLAG_NO_INTERACT : GO_FLAG_LOCKED);
            pGo->RemoveFlag(GAMEOBJECT_FLAGS, m_bDoNorthBeforeWest ? GO_FLAG_LOCKED : GO_FLAG_NO_INTERACT);
            break;
        case GO_DREADSTEED_PORTAL:
            // exclude the central portal
            if (pGo->GetPositionZ() > -29.0f)
                m_lDreadsteedPortalsGUIDs.push_back(pGo->GetObjectGuid());
            return; 
        case GO_BELL_OF_DETHMOORA:
        case GO_WHEEL_OF_BLACK_MARCH:
        case GO_DOOMSDAY_CANDLE:
        case GO_WARLOCK_RITUAL_CIRCLE:
        case GO_RITUAL_CANDLE_AURA:
            break;
        case GO_MOUNT_QUEST_SYMBOL1:
        case GO_MOUNT_QUEST_SYMBOL2:
        case GO_MOUNT_QUEST_SYMBOL3:
            m_lRitualSymbolGUIDs.push_back(pGo->GetObjectGuid());
            break;

        // North
        case GO_NORTH_LIBRARY_DOOR:
            break;

        default:
            return;
    }
    m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
}

void instance_dire_maul::SetData(uint32 uiType, uint32 uiData)
{
    if (uiType < 3) { m_east.SetData(uiType, uiData); return; }
    if (uiType >= MAX_ENCOUNTER || uiData > SPECIAL) return;
    switch (uiType)
    {
        // West
        case TYPE_WARPWOOD:
            if (uiData == DONE)
                DoUseDoorOrButton(GO_WARPWOOD_DOOR);
            m_auiEncounter[uiType] = uiData;
            break;
        case TYPE_IMMOLTHAR:
            if (uiData == DONE)
            {
                if (Creature* pPrince = GetSingleCreatureFromStorage(NPC_PRINCE_TORTHELDRIN))
                {
                    DoScriptText(SAY_FREE_IMMOLTHAR, pPrince);
                    pPrince->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_PLAYER);
                    // Despawn Chest-Aura
                    if (GameObject* pChestAura = GetSingleGameObjectFromStorage(GO_PRINCES_CHEST_AURA))
                        pChestAura->Use(pPrince);
                }
            }
            m_auiEncounter[uiType] = uiData;
            break;
        case TYPE_PRINCE:
            m_auiEncounter[uiType] = uiData;
            break;
        case TYPE_PYLON_1:
        case TYPE_PYLON_2:
        case TYPE_PYLON_3:
        case TYPE_PYLON_4:
        case TYPE_PYLON_5:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
            {
                DoUseDoorOrButton(m_aCrystalGeneratorGuid[uiType - TYPE_PYLON_1]);
                if (CheckAllGeneratorsDestroyed())
                    ProcessForceFieldOpening();
            }
            break;
        case TYPE_DREADSTEED:
            m_auiEncounter[uiType] = uiData;
            if (uiData == FAIL)
            {
                if (Creature* pRitual = GetSingleCreatureFromStorage(NPC_WARLOCK_DUMMY_INFERNAL))
                    pRitual->ForcedDespawn();

                for (auto portal : m_lDreadsteedPortalsGUIDs)
                    instance->GetGameObject(portal)->SetLootState(GO_JUST_DEACTIVATED);

                for (auto symbol : m_lRitualSymbolGUIDs)
                    instance->GetGameObject(symbol)->SetLootState(GO_JUST_DEACTIVATED);

                std::vector<uint32> ids = { GO_BELL_OF_DETHMOORA , GO_DOOMSDAY_CANDLE , GO_WHEEL_OF_BLACK_MARCH };
                for (uint32 id : ids)
                {
                    if (GameObject* pGo = GetSingleGameObjectFromStorage(id))
                    {
                        pGo->SetLootState(GO_JUST_DEACTIVATED);
                        pGo->SetRespawnDelay(1);
                    }
                }

                if (GameObject *pGo = GetSingleGameObjectFromStorage(GO_WARLOCK_RITUAL_CIRCLE))
                    pGo->SetLootState(GO_JUST_DEACTIVATED);
            }
            break;
        // North
        case TYPE_KING_GORDOK:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
            {
                // change faction to certian ogres
                if (Creature* pOgre = GetSingleCreatureFromStorage(NPC_CAPTAIN_KROMCRUSH))
                {
                    if (pOgre->IsAlive())
                    {
                        pOgre->SetFactionTemporary(FACTION_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);

                        // only evade if required
                        if (pOgre->GetVictim())
                            pOgre->AI()->EnterEvadeMode();
                    }
                }

                if (Creature* pOgre = GetSingleCreatureFromStorage(NPC_CHO_RUSH_THE_OBSERVER))
                {
                    // Chorush evades and yells on king death (if alive)
                    if (pOgre->IsAlive())
                    {
                        DoScriptText(SAY_CHORUSH_KING_DEAD, pOgre);
                        pOgre->SetFactionTemporary(FACTION_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);
                        pOgre->AI()->EnterEvadeMode();
                    }

                    // start WP movement for Mizzle; event handled by movement and gossip dbscripts
                    if (Creature* pMizzle = pOgre->SummonCreature(NPC_MIZZLE_THE_CRAFTY, afMizzleSpawnLoc[0], afMizzleSpawnLoc[1], afMizzleSpawnLoc[2], afMizzleSpawnLoc[3], TEMPSPAWN_DEAD_DESPAWN, 0, true))
                    {
                        pMizzle->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                        pMizzle->SetWalk(false);
                        pMizzle->GetMotionMaster()->MoveWaypoint();
                    }
                }
            }
            break;
        case TYPE_MOLDAR:
        case TYPE_FENGUS:
        case TYPE_SLIPKIK:
        case TYPE_KROMCRUSH:
        case TYPE_CHORUSH:
        case TYPE_STOMPER_KREEG:
            m_auiEncounter[uiType] = uiData;
            break;
    }

    if (uiData == DONE)
    {
        OUT_SAVE_INST_DATA;
        SaveState();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

std::vector<InstanceEventReset> instance_dire_maul::GetResettableEvents() const
{
    return {{TYPE_KING_GORDOK, "King Gordok / Gordok Tribute", {NPC_KING_GORDOK}, GO_GORDOK_TRIBUTE}};
}

bool instance_dire_maul::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId != TYPE_KING_GORDOK || GetData(TYPE_KING_GORDOK) == IN_PROGRESS) return false;
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i) prefix[i] = GetData(i);
    prefix[TYPE_KING_GORDOK] = NOT_STARTED;
    data = BuildSave(&prefix);
    return true;
}

void instance_dire_maul::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId != TYPE_KING_GORDOK) return;
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east;
    if (!ParseDireMaulSave(data.c_str(), prefix, east) || east != m_east.Save() || prefix[TYPE_KING_GORDOK] != NOT_STARTED) return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        if (i != TYPE_KING_GORDOK && prefix[i] != GetData(i)) return;
    // Reset only North. Loading East here would detach its live actor GUIDs.
    m_auiEncounter[TYPE_KING_GORDOK] = NOT_STARTED;
    m_strInstData = BuildSave();
    ResetEventRewardGameObject(GO_GORDOK_TRIBUTE);
    GuidVector mizzles;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
        if (itr->second->GetEntry() == NPC_MIZZLE_THE_CRAFTY && !itr->second->HasStaticDBSpawnData())
            mizzles.push_back(itr->first);
    for (ObjectGuid guid : mizzles)
        if (Creature* mizzle = instance->GetCreature(guid))
        {
            mizzle->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            mizzle->ForcedDespawn();
        }
    for (uint32 entry : {NPC_CAPTAIN_KROMCRUSH, NPC_CHO_RUSH_THE_OBSERVER})
        if (Creature* guard = GetSingleCreatureFromStorage(entry, true))
        {
            guard->ClearTemporaryFaction();
            if (guard->IsAlive()) guard->SetStandState(UNIT_STAND_STATE_STAND);
        }
    for (auto const& reference : instance->GetPlayers())
        reference.getSource()->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
    // Surviving tribute guards retain their own kill records and reward quality.
}

bool instance_dire_maul::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    if (bossEntry != NPC_KING_GORDOK)
        return false;

    reward.gameObjectEntry = GO_GORDOK_TRIBUTE;
    reward.completed = GetData(TYPE_KING_GORDOK) == DONE;
    return true;
}

uint32 instance_dire_maul::GetData(uint32 uiType) const
{
    if (uiType < 3) return m_east.GetData(uiType);
    if (uiType < MAX_ENCOUNTER)
        return m_auiEncounter[uiType];

    return 0;
}

void instance_dire_maul::OnCreatureEnterCombat(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        // West
        // - Handling of guards of generators
        case NPC_ARCANE_ABERRATION:
        case NPC_MANA_REMNANT:
            SortPylonGuards();
            break;
        // - Set InstData for ImmolThar
        case NPC_IMMOLTHAR:
            SetData(TYPE_IMMOLTHAR, IN_PROGRESS);
            break;
    }
}

void instance_dire_maul::OnCreatureDeath(Creature* pCreature)
{
    m_east.OnCreatureDeath(pCreature);
    switch (pCreature->GetEntry())
    {
        // West
        // - Handling of guards of generators
        case NPC_ARCANE_ABERRATION:
        case NPC_MANA_REMNANT:
            PylonGuardJustDied(pCreature);
            break;
        // - InstData settings
        case NPC_TENDRIS_WARPWOOD:
            SetData(TYPE_WARPWOOD, DONE);
            break;
        case NPC_IMMOLTHAR:
            SetData(TYPE_IMMOLTHAR, DONE);
            break;
        case NPC_LORD_HELNURATH:
            SetData(TYPE_DREADSTEED, DONE);
            break;

        // North
        // - Handling of Ogre Boss (Assume boss can be handled in Acid)
        case NPC_KING_GORDOK:
            SetData(TYPE_KING_GORDOK, DONE);
            break;
        // Handle Ogre guards for Tribute Run chest
        case NPC_GUARD_MOLDAR:
            SetData(TYPE_MOLDAR, DONE);
            break;
        case NPC_GUARD_FENGUS:
            SetData(TYPE_FENGUS, DONE);
            break;
        case NPC_GUARD_SLIPKIK:
            SetData(TYPE_SLIPKIK, DONE);
            break;
        case NPC_CAPTAIN_KROMCRUSH:
            SetData(TYPE_KROMCRUSH, DONE);
            break;
        case NPC_CHO_RUSH_THE_OBSERVER:
            SetData(TYPE_CHORUSH, DONE);
            break;
        case NPC_STOMPER_KREEG:
            SetData(TYPE_STOMPER_KREEG, DONE);
            break;
    }
}

void instance_dire_maul::Load(char const* text)
{
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east;
    if (!ParseDireMaulSave(text, prefix, east) ||
        !DireMaulEast::ValidateSave({{prefix[0], prefix[1], prefix[2]}}, east))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = i >= 3 && prefix[i] == IN_PROGRESS ? uint32(NOT_STARTED) : prefix[i];
    m_east.Load({{prefix[0], prefix[1], prefix[2]}}, east);
    m_strInstData = BuildSave();
    OUT_LOAD_INST_DATA_COMPLETE;
}

bool instance_dire_maul::CheckConditionCriteriaMeet(Player const* pPlayer, uint32 uiInstanceConditionId, WorldObject const* pConditionSource, uint32 conditionSourceType) const
{
    switch (uiInstanceConditionId)
    {
        case INSTANCE_CONDITION_ID_NORMAL_MODE:             // No guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE:               // One guard alive
        case INSTANCE_CONDITION_ID_HARD_MODE_2:             // Two guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_3:             // Three guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_4:             // Four guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_5:             // Five guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_6:             // Six guards alive
        {
            uint8 uiTributeRunAliveBosses = (GetData(TYPE_MOLDAR) != DONE ? 1 : 0) + (GetData(TYPE_FENGUS) != DONE ? 1 : 0) + (GetData(TYPE_SLIPKIK) != DONE ? 1 : 0)
                                            + (GetData(TYPE_KROMCRUSH) != DONE ? 1 : 0) + (GetData(TYPE_CHORUSH) != DONE ? 1 : 0) + (GetData(TYPE_STOMPER_KREEG) != DONE ? 1 : 0);

            return uiInstanceConditionId == uiTributeRunAliveBosses;
        }
    }

    script_error_log("instance_dire_maul::CheckConditionCriteriaMeet called with unsupported Id %u. Called with param plr %s, src %s, condition source type %u",
                     uiInstanceConditionId, pPlayer ? pPlayer->GetGuidStr().c_str() : "nullptr", pConditionSource ? pConditionSource->GetGuidStr().c_str() : "nullptr", conditionSourceType);
    return false;
}

GuidVector instance_dire_maul::GetRitualSymbolGuids()
{
    return m_lRitualSymbolGUIDs;
}

bool instance_dire_maul::CheckAllGeneratorsDestroyed()
{
    return !(m_auiEncounter[TYPE_PYLON_1] != DONE || m_auiEncounter[TYPE_PYLON_2] != DONE || m_auiEncounter[TYPE_PYLON_3] != DONE || m_auiEncounter[TYPE_PYLON_4] != DONE || m_auiEncounter[TYPE_PYLON_5] != DONE);
}

void instance_dire_maul::ProcessForceFieldOpening()
{
    // 'Open' the force field
    DoUseDoorOrButton(GO_FORCEFIELD);

    // Let the summoners attack Immol'Thar
    Creature* pImmolThar = GetSingleCreatureFromStorage(NPC_IMMOLTHAR);
    if (!pImmolThar || pImmolThar->IsDead())
        return;

    bool bHasYelled = false;
    for (GuidList::const_iterator itr = m_luiHighborneSummonerGUIDs.begin(); itr != m_luiHighborneSummonerGUIDs.end(); ++itr)
    {
        Creature* pSummoner = instance->GetCreature(*itr);

        if (!bHasYelled && pSummoner)
        {
            DoScriptText(SAY_KILL_IMMOLTHAR, pSummoner);
            bHasYelled = true;
        }

        if (!pSummoner || pSummoner->IsDead())
            continue;

        pSummoner->AI()->AttackStart(pImmolThar);
    }
    m_luiHighborneSummonerGUIDs.clear();
}

void instance_dire_maul::SortPylonGuards()
{
    if (!m_lGeneratorGuardGUIDs.empty())
    {
        for (uint8 i = 0; i < MAX_GENERATORS; ++i)
        {
            GameObject* pGenerator = instance->GetGameObject(m_aCrystalGeneratorGuid[i]);
            // Skip non-existing or finished generators
            if (!pGenerator || GetData(TYPE_PYLON_1 + i) == DONE)
                continue;

            // Sort all remaining (alive) NPCs to unfinished generators
            for (GuidList::iterator itr = m_lGeneratorGuardGUIDs.begin(); itr != m_lGeneratorGuardGUIDs.end();)
            {
                Creature* pGuard = instance->GetCreature(*itr);
                if (!pGuard || pGuard->IsDead())    // Remove invalid guids and dead guards
                {
                    m_lGeneratorGuardGUIDs.erase(itr++);
                    continue;
                }

                if (pGuard->IsWithinDist2d(pGenerator->GetPositionX(), pGenerator->GetPositionY(), 20.0f))
                {
                    m_sSortedGeneratorGuards[i].insert(pGuard->GetGUIDLow());
                    m_lGeneratorGuardGUIDs.erase(itr++);
                }
                else
                    ++itr;
            }
        }
    }
}

void instance_dire_maul::PylonGuardJustDied(Creature* pCreature)
{
    for (uint8 i = 0; i < MAX_GENERATORS; ++i)
    {
        // Skip already activated generators
        if (GetData(TYPE_PYLON_1 + i) == DONE)
            continue;

        // Only process generator where the npc is sorted in
        if (m_sSortedGeneratorGuards[i].find(pCreature->GetGUIDLow()) != m_sSortedGeneratorGuards[i].end())
        {
            m_sSortedGeneratorGuards[i].erase(pCreature->GetGUIDLow());
            if (m_sSortedGeneratorGuards[i].empty())
                SetData(TYPE_PYLON_1 + i, DONE);

            break;
        }
    }
}

void instance_dire_maul::ProcessDreadsteedRitualStart()
{
    if (GameObject* go = GetSingleGameObjectFromStorage(GO_WARLOCK_RITUAL_CIRCLE))
    {
        go->SetRespawnTime(900);
        go->Refresh();
    }

    for (auto portal : m_lDreadsteedPortalsGUIDs)
        if (GameObject* go = instance->GetGameObject(portal))
            DoRespawnGameObject(portal, 380);
}

InstanceData* GetInstanceData_instance_dire_maul(Map* pMap)
{
    return new instance_dire_maul(pMap);
}

/*###############
## go_fixed_trap
################*/

struct go_ai_fixed_trap : public GameObjectAI
{
    go_ai_fixed_trap(GameObject* go) : GameObjectAI(go) {}

    void UpdateAI(const uint32 /*uiDiff*/) override
    {
        if (m_go->IsSpawned())
        {
            // Sniffs show that trap should only react to Guard Slip'kik
            // He then enters evade mode and becomes an invalid target to hostile actions from players
            // Faction is changed to 14 and restored by spell 22799 (King of the Gordok)
            // Additionnaly, Guard Slip'kik should get UnitFlags 16 but purpose is unknown so we skip it for now
            if (Creature* slipkik = GetClosestCreatureWithEntry(m_go, NPC_GUARD_SLIPKIK, 0.5f))
            {
                m_go->Use(slipkik);
                DoScriptText(SAY_SLIPKIK_TRAP, slipkik);
                slipkik->AI()->EnterEvadeMode();
                slipkik->SetImmuneToPlayer(true);
                slipkik->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_RESPAWN);
                m_go->SetLootState(GO_JUST_DEACTIVATED);    // Despawn the trap
            }
        }
    }
};

GameObjectAI* GetAI_go_fixed_trap(GameObject* go)
{
    return new go_ai_fixed_trap(go);
}

/*####################################
## Guard Slip'kik Trigger dummy effect
####################################*/

// 31770 - Guard Slip'kik Trigger
struct GuardSlipkikTrigger : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (instance_dire_maul* instance = static_cast<instance_dire_maul*>(spell->GetCaster()->GetInstanceData()))
        {
            if (Creature* slipkik = instance->GetSingleCreatureFromStorage(NPC_GUARD_SLIPKIK))
                slipkik->setFaction(FACTION_OGRE);
        }
    }
};

void AddSC_instance_dire_maul()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_dire_maul";
    pNewScript->GetInstanceData = &GetInstanceData_instance_dire_maul;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_fixed_trap";
    pNewScript->GetGameObjectAI = &GetAI_go_fixed_trap;
    pNewScript->RegisterSelf();

    RegisterSpellScript<GuardSlipkikTrigger>("spell_guard_slipkik_trigger");
}
