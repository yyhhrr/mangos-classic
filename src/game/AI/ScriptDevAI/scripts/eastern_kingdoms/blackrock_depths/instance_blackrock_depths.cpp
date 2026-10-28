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
SDName: Instance_Blackrock_Depths
SD%Complete: 80
SDComment:
SDCategory: Blackrock Depths
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_depths.h"
#include "Maps/MapPersistentStateMgr.h"
#include <iomanip>
#include <cmath>

instance_blackrock_depths::instance_blackrock_depths(Map* pMap) : ScriptedInstance(pMap),
    m_bIsBarDoorOpen(false),
    m_uiBarAleCount(0),
    m_uiPatronEmoteTimer(2000),
    m_uiBrokenKegs(0),
    m_uiPatrolTimer(0),
    m_uiStolenAles(0),
    m_uiDagranTimer(0),
    m_uiCofferDoorsOpened(0),
    m_uiDwarfRound(0),

    m_uiDwarfFightTimer(0),
    m_fArenaCenterX(0.0f),
    m_fArenaCenterY(0.0f),
    m_fArenaCenterZ(0.0f)
{
    Initialize();
}

void instance_blackrock_depths::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
}

void instance_blackrock_depths::OnCreatureCreate(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        case NPC_PRINCESS:
            // replace the princess if required
            if (CanReplacePrincess())
                pCreature->UpdateEntry(NPC_PRIESTESS);
        // no break;
        case NPC_EMPEROR:
        case NPC_MAGMUS:
        case NPC_PHALANX:
        case NPC_PLUGGER_SPAZZRING:
        case NPC_HATEREL:
        case NPC_ANGERREL:
        case NPC_VILEREL:
        case NPC_GLOOMREL:
        case NPC_SEETHREL:
        case NPC_DOOMREL:
        case NPC_DOPEREL:
        case NPC_SHILL:
        case NPC_CREST:
        case NPC_JAZ:
        case NPC_TOBIAS:
        case NPC_DUGHAL:
        case NPC_LOREGRAIN:
        case NPC_RIBBLY_SCREWSPIGGOT:
            m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            m_restorePending = true;
            break;
        case NPC_WARBRINGER_CONST:
            // Golems not in the Relict Vault?
            if (std::abs(pCreature->GetPositionZ() - aVaultPositions[2]) > 1.0f || !pCreature->IsWithinDist2d(aVaultPositions[0], aVaultPositions[1], 20.0f))
                break;
            // Golems in Relict Vault need to have a stoned aura, set manually to prevent reapply when reached home
            if (m_vaultExpected.insert(pCreature->GetDbGuid()).second && m_cofferMask == 4095 && GetData(TYPE_VAULT) != DONE)
            {
                m_vaultRetries = 0; m_vaultRetryTimer = 1;
            }
            m_restorePending = true;
        // Store the Relict Vault Golems into m_sVaultNpcGuids
        case NPC_WATCHER_DOOMGRIP:
            m_sVaultNpcGuids.insert(pCreature->GetObjectGuid());
            break;
        // Arena crowd
        case NPC_ARENA_SPECTATOR:
        case NPC_SHADOWFORGE_PEASANT:
        case NPC_SHADOWFORGE_CITIZEN:
        case NPC_SHADOWFORGE_SENATOR:
        case NPC_ANVILRAGE_SOLDIER:
        case NPC_ANVILRAGE_MEDIC:
        case NPC_ANVILRAGE_OFFICER:
            if (pCreature->GetPositionZ() < aArenaCrowdVolume.m_fCenterZ || pCreature->GetPositionZ() > aArenaCrowdVolume.m_fCenterZ + aArenaCrowdVolume.m_uiHeight ||
                    !pCreature->IsWithinDist2d(aArenaCrowdVolume.m_fCenterX, aArenaCrowdVolume.m_fCenterY, aArenaCrowdVolume.m_uiRadius))
                break;
            m_sArenaCrowdNpcGuids.insert(pCreature->GetObjectGuid());
            if (m_auiEncounter[0] == DONE)
                pCreature->SetFactionTemporary(FACTION_ARENA_NEUTRAL, TEMPFACTION_RESTORE_RESPAWN);
            break;
        // Grim Guzzler bar crowd
        case NPC_GRIM_PATRON:
        case NPC_GUZZLING_PATRON:
        case NPC_HAMMERED_PATRON:
            m_sBarPatronNpcGuids.insert(pCreature->GetObjectGuid());
            if (m_auiEncounter[11] == DONE)
            {
                pCreature->SetFactionTemporary(FACTION_DARK_IRON, TEMPFACTION_RESTORE_RESPAWN);
                pCreature->SetStandState(UNIT_STAND_STATE_STAND);
            }
            break;
        case NPC_PRIVATE_ROCKNOT:
        case NPC_MISTRESS_NAGMARA:
            m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            m_restorePending = true;
            break;
    }
}

void instance_blackrock_depths::OnObjectCreate(GameObject* go)
{
    // Create precedes LoadFromDB's state/respawn setup. Restore only after that setup.
    uint32 entry = go->GetEntry();
    if (RelicIndex(entry) >= 0 || entry == 174744 || entry == 174745)
        m_goEntryGuidStore[entry] = go->GetObjectGuid();
    else
    {
        switch (entry)
        {
            case GO_ARENA_1: case GO_ARENA_2: case GO_ARENA_3: case GO_ARENA_4:
            case GO_SHADOW_LOCK: case GO_SHADOW_MECHANISM: case GO_SHADOW_GIANT_DOOR: case GO_SHADOW_DUMMY:
            case GO_BAR_KEG_SHOT: case GO_BAR_KEG_TRAP: case GO_BAR_DOOR:
            case GO_TOMB_ENTER: case GO_TOMB_EXIT: case GO_LYCEUM:
            case GO_GOLEM_ROOM_N: case GO_GOLEM_ROOM_S: case GO_THRONE_ROOM:
            case GO_SPECTRAL_CHALICE: case GO_CHEST_SEVEN: case GO_ARENA_SPOILS:
            case GO_SECRET_DOOR: case GO_SECRET_SAFE: case GO_JAIL_DOOR_SUPPLY: case GO_JAIL_SUPPLY_CRATE:
            case GO_DWARFRUNE_A01: case GO_DWARFRUNE_B01: case GO_DWARFRUNE_C01:
            case GO_DWARFRUNE_D01: case GO_DWARFRUNE_E01: case GO_DWARFRUNE_F01: case GO_DWARFRUNE_G01:
                m_goEntryGuidStore[entry] = go->GetObjectGuid();
                break;
            default: return;
        }
    }
    m_restorePending = true;
}

void instance_blackrock_depths::SetData(uint32 type, uint32 data)
{
    if (!type || type > MAX_ENCOUNTER)
        return;
    uint32& state = m_auiEncounter[type - 1];
    if (state == DONE && type != TYPE_QUEST_JAIL_BREAK)
        return;
    switch (type)
    {
        case TYPE_RING_OF_LAW:
            if ((data == SPECIAL || data == 5) && m_arenaKilled.empty() && !m_arenaRewardWaveStarted)
            {
                m_arenaChallenge = true;
                state = IsArenaController(instance->GetCreature(m_arenaController)) ? uint32(SPECIAL) : 5u;
            }
            else if (data == FAIL)
            {
                FinishArena(instance->GetCreature(m_arenaController), false);
                return;
            }
            else
                return; // The qualified controller owns starts and completions.
            break;
        case TYPE_VAULT:
            if (data == SPECIAL)
                return; // Use OpenRelicCoffer with the actual door identity.
            if (data == DONE && m_doomgripKilled && m_vaultExpected.size() == 4 && m_vaultDead.size() >= 4)
                state = DONE;
            else
                return;
            break;
        case TYPE_TOMB_OF_SEVEN:
            if (data == DONE)
            {
                if (m_tombDeadMask != 127)
                    return;
                state = DONE;
                m_tombResumePending = false;
                m_uiDwarfFightTimer = m_tombResetTimer = 0;
                StartReward(0);
            }
            else if (data == FAIL)
            {
                if (state != IN_PROGRESS || m_resettingTomb)
                    return;
                // EventAI also reports FAIL on reached-home. One member returning
                // must not revive opponents while another member is still fighting.
                for (uint32 i = 0; i < MAX_DWARFS; ++i)
                    if ((m_tombActivatedMask & (1u << i)) && !(m_tombDeadMask & (1u << i)))
                        if (Creature* dwarf = GetSingleCreatureFromStorage(aTombDwarfes[i], true))
                            if (dwarf->IsAlive() && dwarf->IsInCombat())
                            {
                                m_tombResetTimer = 1000;
                                return;
                            }
                state = FAIL;
                ResetTomb();
            }
            else if (data == IN_PROGRESS && state != IN_PROGRESS)
            {
                state = IN_PROGRESS;
                m_tombResumePending = m_tombRecoveredRun = false;
                m_uiDwarfRound = 0;
                m_uiDwarfFightTimer = 1;
            }
            else return;
            break;
        case TYPE_LYCEUM:
            if (data != DONE || m_brazierMask != 3)
                return;
            state = DONE;
            if (Creature* magmus = GetSingleCreatureFromStorage(NPC_MAGMUS, true))
                DoScriptText(YELL_MAGMUS_INTRO, magmus);
            break;
        case TYPE_IRON_HALL:
            if (data != IN_PROGRESS && data != FAIL && data != DONE)
                return;
            if (data == IN_PROGRESS)
                m_ironHallStarted = true;
            state = data;
            break;
        case TYPE_QUEST_JAIL_BREAK:
            state = data;
            break;
        case TYPE_FLAMELASH:
            if (state == data)
                return;
            state = data;
            break;
        case TYPE_ROCKNOT:
            if (data == SPECIAL)
            {
                if (m_uiBarAleCount < 3)
                    ++m_uiBarAleCount;
            }
            else
            {
                if (data == DONE)
                {
                    HandleBarPatrons(PATRON_PISSED);
                    SetBarDoorIsOpen();
                }
                state = data;
            }
            break;
        case TYPE_HURLEY:
            if (data == SPECIAL)
            {
                if (m_uiBrokenKegs < 3)
                    ++m_uiBrokenKegs;
                if (m_uiBrokenKegs == 3 && !m_hurleyRequested)
                {
                    m_hurleyRequested = true;
                    m_hurleyRetries = 0;
                    m_hurleyRetryTimer = 1;
                }
            }
            else if (data == DONE)
            {
                state = DONE;
                m_hurleyDeadMask |= 1;
                m_hurleyRetryTimer = 0;
            }
            else if (data == IN_PROGRESS && m_hurleyRequested)
                state = data;
            break;
        case TYPE_BAR:
            if (data == IN_PROGRESS)
            {
                ScheduleBarPatrol(1);
                return;
            }
            if (data == SPECIAL || data == DONE)
                state = data;
            break;
        case TYPE_PLUGGER:
            if (data == SPECIAL)
            {
                if (GetSingleCreatureFromStorage(NPC_PLUGGER_SPAZZRING, true))
                {
                    if (m_uiStolenAles < 3)
                        ++m_uiStolenAles;
                    if (m_uiStolenAles == 3)
                        data = IN_PROGRESS;
                }
            }
            state = data;
            break;
        case TYPE_BRIDGE:
        case TYPE_NAGMARA:
            state = data;
            break;
        default: return;
    }
    SaveState();
    SyncObjects();
}

bool instance_blackrock_depths::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    // Theldren is summoned only for the challenge variant, which awards Arena Spoils.
    if (bossEntry == NPC_THELDREN)
    {
        reward.gameObjectEntry = GO_ARENA_SPOILS;
        reward.completed = GetData(TYPE_RING_OF_LAW) == DONE;
        return true;
    }

    switch (bossEntry)
    {
        case NPC_HATEREL:
        case NPC_ANGERREL:
        case NPC_VILEREL:
        case NPC_GLOOMREL:
        case NPC_SEETHREL:
        case NPC_DOOMREL:
        case NPC_DOPEREL:
            reward.gameObjectEntry = GO_CHEST_SEVEN;
            reward.completed = GetData(TYPE_TOMB_OF_SEVEN) == DONE;
            return true;
        default:
            return false;
    }
}

uint32 instance_blackrock_depths::GetData(uint32 uiType) const
{
    switch (uiType)
    {
        case TYPE_RING_OF_LAW:
            return m_auiEncounter[0];
        case TYPE_VAULT:
            return m_auiEncounter[1];
        case TYPE_ROCKNOT:
        {
            if (m_auiEncounter[2] == IN_PROGRESS && m_uiBarAleCount == 3)
                return SPECIAL;
            return m_auiEncounter[2];
        }
        case TYPE_TOMB_OF_SEVEN:
            return m_auiEncounter[3];
        case TYPE_LYCEUM:
            return m_auiEncounter[4];
        case TYPE_IRON_HALL:
            return m_auiEncounter[5];
        case TYPE_QUEST_JAIL_BREAK:
            return m_auiEncounter[6];
        case TYPE_FLAMELASH:
            return m_auiEncounter[7];
        case TYPE_HURLEY:
            return m_auiEncounter[8];
        case TYPE_BRIDGE:
            return m_auiEncounter[9];
        case TYPE_BAR:
            return m_auiEncounter[10];
        case TYPE_PLUGGER:
            return m_auiEncounter[11];
        case TYPE_NAGMARA:
            return m_auiEncounter[12];
        default:
            return 0;
    }
}

void instance_blackrock_depths::Load(const char* data)
{
    if (!data)
        return;
    std::istringstream in(data);
    uint32 states[MAX_ENCOUNTER] = {};
    for (uint32& state : states)
        if (!(in >> state) || state > 5)
            return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = states[i] == DONE ? DONE : uint32(NOT_STARTED);
    m_arenaChallenge = states[0] == SPECIAL || states[0] == 5;
    m_ironHallStarted = states[5] != NOT_STARTED;
    m_bIsBarDoorOpen = states[2] == DONE || states[10] == DONE || states[11] == DONE || states[12] == DONE;
    m_rewards[0].state = states[3] == DONE ? 2 : 0;
    m_rewards[1].state = states[0] == DONE ? 2 : 0;
    // Legacy active/failed records cannot prove which reward NPC already died.
    if (states[1] == IN_PROGRESS || states[1] == FAIL) m_doomgripKilled = true;
    if (states[0] == IN_PROGRESS || states[0] == FAIL || states[0] == SPECIAL)
        for (uint32 entry : aArenaNPCs)
            if (entry == NPC_THELDREN || (entry >= NPC_GOROSH && entry <= NPC_HEDRUM) || (entry >= NPC_LEFTY && entry <= NPC_VOLIDA))
                m_arenaKilled.insert(entry);
    std::string version;
    if (in >> version && version == "BR2")
    {
        // Parse into a detached value object. A truncated or corrupt suffix cannot
        // partially authorize a chest, spawn, or progression in the live instance.
        instance_blackrock_depths payload(instance);
        std::copy(m_auiEncounter, m_auiEncounter + MAX_ENCOUNTER, payload.m_auiEncounter);
        payload.m_rewards[0] = m_rewards[0]; payload.m_rewards[1] = m_rewards[1];
        auto readPayload = [&]() -> bool
        {
        uint32 challenge, choice, iron, doom, hurley, patrol, hurleyPos, patrolPos, dwarfRound, kegs, stolen;
        uint32 n = 0;
        if (!(in >> payload.m_cofferMask >> payload.m_brazierMask >> payload.m_tombDeadMask >> payload.m_tombActivatedMask >> dwarfRound
              >> iron >> doom >> challenge >> choice >> payload.m_arenaMobIndex >> payload.m_arenaBossEntry))
            return false;
        payload.m_cofferMask &= 4095; payload.m_brazierMask &= 3; payload.m_tombDeadMask &= 127; payload.m_tombActivatedMask &= 127;
        payload.m_uiDwarfRound = uint8(std::min<uint32>(dwarfRound, MAX_DWARFS));
        payload.m_ironHallStarted = iron == 1;
        payload.m_doomgripKilled = doom == 1;
        payload.m_arenaChallenge = challenge == 1;
        payload.m_arenaSelectionStored = choice == 1;
        if (!(in >> n) || n > 4) return false;
        for (uint32 i = 0, entry; i < n; ++i) { if (!(in >> entry)) return false; payload.m_arenaGladiators.push_back(entry); }
        auto readSet = [&](std::set<uint32>& set, uint32 limit) -> bool
        {
            uint32 count, entry;
            if (!(in >> count) || count > limit) return false;
            for (uint32 i = 0; i < count; ++i) { if (!(in >> entry) || !entry) return false; set.insert(entry); }
            return true;
        };
        if (!readSet(payload.m_arenaKilled, 15) || !readSet(payload.m_vaultDead, 4) || !readSet(payload.m_jailDead, 8)) return false;
        for (SavedReward& reward : payload.m_rewards)
        {
            std::string snapshot;
            if (!(in >> reward.state >> reward.expires >> snapshot)) return false;
            uint64 now = uint64(time(nullptr));
            if (reward.state != 1 || reward.expires <= now || reward.expires > now + HOUR || snapshot.size() > 32771 ||
                (snapshot != "-" && snapshot.compare(0, 3, "IL1")))
            {
                reward.state = reward.state ? 2 : 0; reward.expires = 0; reward.snapshot.clear();
            }
            else reward.snapshot = snapshot == "-" ? std::string() : snapshot;
        }
        if (!(in >> kegs >> payload.m_uiBarAleCount >> stolen >> hurley >> payload.m_hurleyDeadMask
              >> patrol >> payload.m_patrolDeadMask >> payload.m_patrolStage >> hurleyPos >> patrolPos)) return false;
        payload.m_uiBrokenKegs = uint8(std::min<uint32>(kegs, 3));
        payload.m_uiStolenAles = uint8(std::min<uint32>(stolen, 3));
        payload.m_hurleyRequested = hurley == 1 && states[8] != DONE;
        payload.m_patrolRequested = patrol == 1 && states[10] != DONE;
        payload.m_hurleyDeadMask &= 15; payload.m_patrolDeadMask &= 7;
        payload.m_hurleyPositionsReady = hurleyPos == 1;
        payload.m_patrolPositionsReady = patrolPos == 1;
        for (auto& pos : payload.m_hurleySlotPositions) for (float& value : pos) if (!(in >> value)) return false;
        for (auto& pos : payload.m_patrolSlotPositions) for (float& value : pos) if (!(in >> value)) return false;
        uint32 barOpen; uint64 patrolNext, tombNext;
        if (!(in >> barOpen >> patrolNext >> tombNext) || barOpen > 1) return false;
        payload.m_bIsBarDoorOpen = barOpen == 1;
        if (payload.m_arenaMobIndex >= 6 || (payload.m_arenaSelectionStored &&
            (payload.m_arenaGladiators.size() != 4 || payload.m_arenaBossEntry < NPC_GOROSH || payload.m_arenaBossEntry > NPC_HEDRUM))) return false;
        for (uint32 i = 0; i < 2; ++i)
            if (states[i ? 0 : 3] != DONE) payload.m_rewards[i] = SavedReward();
        uint64 const resumeNow = uint64(time(nullptr));
        if (states[3] == IN_PROGRESS)
        {
            payload.m_auiEncounter[3] = IN_PROGRESS;
            payload.m_tombResumePending = payload.m_tombRecoveredRun = true;
            payload.m_uiDwarfFightTimer = tombNext > resumeNow ? uint32(std::min<uint64>(tombNext - resumeNow, 30)) * 1000 : 1;
        }
        if (payload.m_patrolRequested) { payload.m_auiEncounter[10] = payload.m_patrolStage ? uint32(SPECIAL) : uint32(IN_PROGRESS); payload.m_uiPatrolTimer = patrolNext > resumeNow ? uint32(std::min<uint64>(patrolNext - resumeNow, 3600)) * 1000 : 1; }
        if (payload.m_hurleyRequested) payload.m_hurleyRetryTimer = 1000;
        if (payload.m_cofferMask == 4095 && states[1] != DONE) payload.m_vaultRetryTimer = 1000;
                in >> std::ws;
            return in.eof();
        };
        if (readPayload())
            *this = payload;
    }    // Legacy DONE rewards have unknown claim history. Do not invent another chest.
    // No access to MapPersistentState in Load: Map creates it after loading scripts.
    m_restorePending = true;
    BuildSaveData();
}

void instance_blackrock_depths::OnCreatureEnterCombat(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_MAGMUS)
        SetData(TYPE_IRON_HALL, IN_PROGRESS);
}

void instance_blackrock_depths::OnCreatureEvade(Creature* creature)
{
    if (m_arenaSummons.count(creature->GetObjectGuid()))
        FinishArena(instance->GetCreature(m_arenaController), false);
    for (uint32 entry : aTombDwarfes)
        if (creature->GetEntry() == entry && GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS && !m_resettingTomb)
            m_tombResetTimer = 1000;
    if (creature->GetEntry() == NPC_MAGMUS)
        SetData(TYPE_IRON_HALL, FAIL);
}

void instance_blackrock_depths::OnCreatureDeath(Creature* creature)
{
    if (m_arenaSummons.count(creature->GetObjectGuid()) && IsArenaRewardEntry(creature->GetEntry()))
    {
        m_arenaKilled.insert(creature->GetEntry());
        SaveState();
    }
    for (uint32 i = 0; i < MAX_DWARFS; ++i)
        if (creature->GetEntry() == aTombDwarfes[i] && GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS &&
            creature->GetObjectGuid() == m_npcEntryGuidStore[aTombDwarfes[i]])
        {
            uint32 bit = 1u << i;
            if (m_tombDeadMask & bit) return;
            m_tombDeadMask |= bit;
            if (m_tombDeadMask == 127)
                SetData(TYPE_TOMB_OF_SEVEN, DONE);
            else
            {
                if (m_uiDwarfRound && i == m_uiDwarfRound - 1)
                    DoCallNextDwarf();
                SaveState();
            }
            return;
        }
    if (IsVaultConstruct(creature))
    {
        m_vaultDead.insert(creature->GetDbGuid());
        SaveState();
        CheckVaultComplete();
    }
    if (creature->GetEntry() == NPC_WATCHER_DOOMGRIP && creature->GetObjectGuid() == m_doomgripGuid)
    {
        m_doomgripKilled = true;
        SaveState();
        CheckVaultComplete();
    }
    for (uint32 i = 0; i < 4; ++i)
        if (creature->GetObjectGuid() == m_hurleySlots[i])
        {
            m_hurleyDeadMask |= 1u << i;
            if (!i) SetData(TYPE_HURLEY, DONE);
            else SaveState();
        }
    for (uint32 i = 0; i < 3; ++i)
        if (creature->GetObjectGuid() == m_patrolSlots[i])
        {
            m_patrolDeadMask |= 1u << i;
            SaveState();
        }
    switch (creature->GetEntry())
    {
        case NPC_OGRABISI: case NPC_SHILL: case NPC_CREST: case NPC_JAZ:
            if (GetData(TYPE_QUEST_JAIL_BREAK) == IN_PROGRESS || GetData(TYPE_QUEST_JAIL_BREAK) == SPECIAL)
            {
                m_jailDead.insert(creature->GetEntry());
                SaveState();
            }
            break;
        case NPC_MAGMUS: SetData(TYPE_IRON_HALL, DONE); break;
        case NPC_RIBBLY_SCREWSPIGGOT:
            if (GetData(TYPE_BAR) != DONE && GetData(TYPE_PLUGGER) != IN_PROGRESS && GetData(TYPE_PLUGGER) != DONE)
                ScheduleBarPatrol(1);
            break;
        case NPC_SHADOWFORGE_SENATOR:
            if (!m_uiDagranTimer)
                if (Creature* emperor = GetSingleCreatureFromStorage(NPC_EMPEROR, true))
                    if (emperor->IsAlive())
                    {
                        int32 const texts[] = { YELL_SENATOR_1, YELL_SENATOR_2, YELL_SENATOR_3, YELL_SENATOR_4 };
                        DoScriptText(texts[urand(0, 3)], emperor);
                        m_uiDagranTimer = 30000;
                    }
            break;
    }
}

void instance_blackrock_depths::DoCallNextDwarf()
{
    if (GetData(TYPE_TOMB_OF_SEVEN) != IN_PROGRESS || m_resettingTomb || m_tombResumePending)
        return;
    while (m_uiDwarfRound < MAX_DWARFS && (m_tombDeadMask & (1u << m_uiDwarfRound)))
        ++m_uiDwarfRound;
    if (m_uiDwarfRound == MAX_DWARFS)
    {
        m_uiDwarfFightTimer = 0;
        return;
    }
    Creature* dwarf = GetSingleCreatureFromStorage(aTombDwarfes[m_uiDwarfRound], true);
    Player* target = nullptr;
    if (dwarf && dwarf->IsAlive())
        target = SelectTombTarget(dwarf);
    if (!target)
    {
        m_uiDwarfFightTimer = 1000;
        return;
    }
    dwarf->SetFactionTemporary(FACTION_DWARF_HOSTILE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_RESTORE_REACH_HOME);
    dwarf->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
    dwarf->AI()->AttackStart(target);
    if (!dwarf->IsInCombat())
    {
        m_uiDwarfFightTimer = 1000;
        return;
    }
    m_tombActivatedMask |= 1u << m_uiDwarfRound;
    ++m_uiDwarfRound;
    m_uiDwarfFightTimer = 30000;
    SaveState();
}

// function that replaces the princess if requirements are met
bool instance_blackrock_depths::CanReplacePrincess() const
{
    Map::PlayerList const& players = instance->GetPlayers();
    if (players.isEmpty())
        return false;

    for (const auto& player : players)
    {
        if (Player* pPlayer = player.getSource())
        {
            // if at least one player didn't complete the quest, return false
            if ((pPlayer->GetTeam() == ALLIANCE && !pPlayer->GetQuestRewardStatus(QUEST_FATE_KINGDOM))
                    || (pPlayer->GetTeam() == HORDE && !pPlayer->GetQuestRewardStatus(QUEST_ROYAL_RESCUE)))
                return false;
        }
    }

    return true;
}

void instance_blackrock_depths::HandleBarPatrons(uint8 uiEventType)
{
    switch (uiEventType)
    {
        // case for periodical handle of random emotes
        case PATRON_EMOTE:
            if (GetData(TYPE_PLUGGER) == DONE)
                return;

            for (auto m_sBarPatronNpcGuid : m_sBarPatronNpcGuids)
            {
                // About 5% of patrons do emote at a given time
                // So avoid executing follow up code for the 95% others
                if (urand(0, 100) < 4)
                {
                    // Only three emotes are seen in data: laugh, cheer and exclamation
                    // the last one appearing the least and the first one appearing the most
                    // emotes are stored in a table and frequency is handled there
                    if (Creature* pPatron = instance->GetCreature(m_sBarPatronNpcGuid))
                        pPatron->HandleEmote(aPatronsEmotes[urand(0, 5)]);
                }
            }
            return;
        // case for Rocknot event when breaking the barrel
        case PATRON_PISSED:
            // Three texts are said, one less often than the two others
            // Only by patrons near the broken barrel react to Rocknot's rampage
            if (GameObject* pGo = GetSingleGameObjectFromStorage(GO_BAR_KEG_SHOT))
            {
                for (auto m_sBarPatronNpcGuid : m_sBarPatronNpcGuids)
                {
                    if (Creature* pPatron = instance->GetCreature(m_sBarPatronNpcGuid))
                    {
                        if (pPatron->GetPositionZ() > pGo->GetPositionZ() - 1 && pPatron->IsWithinDist2d(pGo->GetPositionX(), pGo->GetPositionY(), 18.0f))
                        {
                            switch (urand(0, 4))
                            {
                                case 0: DoScriptText(SAY_PISSED_PATRON_3, pPatron); break;
                                case 1:  // case is double to give this text twice the chance of the previous one do be displayed
                                case 2: DoScriptText(SAY_PISSED_PATRON_2, pPatron); break;
                                // covers the two remaining cases
                                default: DoScriptText(SAY_PISSED_PATRON_1, pPatron); break;
                            }
                        }
                    }
                }
            }
            return;
        // case when Plugger is killed
        case PATRON_HOSTILE:
            for (auto m_sBarPatronNpcGuid : m_sBarPatronNpcGuids)
            {
                if (Creature* pPatron = instance->GetCreature(m_sBarPatronNpcGuid))
                {
                    pPatron->SetFactionTemporary(FACTION_DARK_IRON, TEMPFACTION_RESTORE_RESPAWN);
                    pPatron->SetStandState(UNIT_STAND_STATE_STAND);
                    pPatron->HandleEmote(EMOTE_ONESHOT_NONE);
                    pPatron->GetMotionMaster()->MoveRandomAroundPoint(pPatron->GetPositionX(), pPatron->GetPositionY(), pPatron->GetPositionZ(), 2.0f);
                }
            }
            // Mistress Nagmara and Private Rocknot despawn if the bar turns hostile
            if (Creature* pRocknot = GetSingleCreatureFromStorage(NPC_PRIVATE_ROCKNOT))
            {
                DoScriptText(SAY_ROCKNOT_DESPAWN, pRocknot);
                pRocknot->ForcedDespawn();
            }
            if (Creature* pNagmara = GetSingleCreatureFromStorage(NPC_MISTRESS_NAGMARA))
            {
                pNagmara->CastSpell(pNagmara, SPELL_NAGMARA_VANISH, TRIGGERED_OLD_TRIGGERED);
                pNagmara->ForcedDespawn();
            }
    }
}

void instance_blackrock_depths::HandleBarPatrol(uint8 step)
{
    if (!m_patrolRequested || GetData(TYPE_BAR) == DONE)
        return;
    if (!step)
    {
        if (!m_bIsBarDoorOpen && (GetSingleCreatureFromStorage(NPC_PLUGGER_SPAZZRING, true) || GetPlayerInMap()))
        {
            SetBarDoorIsOpen();
            SyncObjects();
        }
        if (!SpawnPatrolSlots())
        {
            if (++m_patrolRetries < 5) m_uiPatrolTimer = 1000;
            else { m_uiPatrolTimer = 0; m_restorePending = false; }
            return;
        }
        m_auiEncounter[10] = IN_PROGRESS;
        m_patrolStage = 1;
        m_uiPatrolTimer = 4000;
        SaveState();
        return;
    }
    Creature* speaker = instance->GetCreature(m_patrolSlots[0]);
    if (speaker && speaker->IsAlive())
        DoScriptText(step == 1 ? YELL_PATROL_1 : YELL_PATROL_2, speaker);
    // A killed/missing speaker must not deadlock a successfully dispatched patrol.
    if (step == 1)
    {
        m_patrolStage = 2;
        m_auiEncounter[10] = SPECIAL;
        m_uiPatrolTimer = 2000;
    }
    else
    {
        m_patrolStage = 3;
        m_auiEncounter[10] = DONE;
        m_uiPatrolTimer = 0;
    }
    SaveState();
    SyncObjects();
}

void instance_blackrock_depths::Update(uint32 diff)
{
    if (GetData(TYPE_RING_OF_LAW) == IN_PROGRESS || GetData(TYPE_RING_OF_LAW) == SPECIAL)
    {
        Creature* controller = instance->GetCreature(m_arenaController);
        if (!controller || !controller->IsAlive())
            AbortArenaGeneration();
    }
    if (m_tombResumePending && SelectTombTarget(nullptr))
    {
        // An actual participant has crossed the entrance. Never select an earlier
        // map-list player who is still outside the closed combat door.
        m_tombResumePending = false;
        m_restorePending = true;
        SaveState();
    }
    if (m_restorePending)
    {
        m_restorePending = false;
        RestoreProgress();
    }
    if (m_syncTimer <= diff)
    {
        m_syncTimer = 1000;
        SyncReward(0); SyncReward(1);
    }
    else m_syncTimer -= diff;
    if (m_uiDwarfFightTimer && GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS && !m_tombResumePending)
    {
        if (m_uiDwarfFightTimer <= diff) DoCallNextDwarf();
        else m_uiDwarfFightTimer -= diff;
    }
    if (m_tombResetTimer && GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS && !m_tombResumePending)
    {
        if (m_tombResetTimer <= diff)
        {
            bool fighting = false;
            for (uint32 i = 0; i < MAX_DWARFS; ++i)
                if ((m_tombActivatedMask & (1u << i)) && !(m_tombDeadMask & (1u << i)))
                    if (Creature* dwarf = GetSingleCreatureFromStorage(aTombDwarfes[i], true))
                        if (dwarf->IsAlive() && dwarf->IsInCombat()) fighting = true;
            if (fighting) m_tombResetTimer = 1000;
            else SetData(TYPE_TOMB_OF_SEVEN, FAIL);
        }
        else m_tombResetTimer -= diff;
    }
    if (m_vaultRetryTimer)
    {
        if (m_vaultRetryTimer <= diff)
        {
            m_vaultRetryTimer = 0;
            if (!StartVault() && ++m_vaultRetries < 5) m_vaultRetryTimer = 1000;
        }
        else m_vaultRetryTimer -= diff;
    }
    if (m_hurleyRetryTimer && GetData(TYPE_HURLEY) != DONE)
    {
        if (m_hurleyRetryTimer <= diff)
        {
            m_hurleyRetryTimer = 0;
            if (SpawnHurleySlots())
            {
                m_auiEncounter[8] = IN_PROGRESS;
                SaveState();
            }
            else if (++m_hurleyRetries < 5) m_hurleyRetryTimer = 1000;
        }
        else m_hurleyRetryTimer -= diff;
    }
    if (m_uiDagranTimer) m_uiDagranTimer = m_uiDagranTimer <= diff ? 0 : m_uiDagranTimer - diff;
    if (m_uiPatronEmoteTimer <= diff)
    {
        HandleBarPatrons(PATRON_EMOTE);
        m_uiPatronEmoteTimer = 1000;
    }
    else m_uiPatronEmoteTimer -= diff;
    if (m_uiPatrolTimer && m_patrolRequested && GetData(TYPE_BAR) != DONE)
    {
        if (m_uiPatrolTimer <= diff)
        {
            m_uiPatrolTimer = 0;
            if (m_patrolStage && !SpawnPatrolSlots())
            {
                if (++m_patrolRetries < 5) m_uiPatrolTimer = 1000;
            }
            else HandleBarPatrol(uint8(m_patrolStage));
        }
        else m_uiPatrolTimer -= diff;
    }
}

void instance_blackrock_depths::OnPlayerEnter(Player*)
{
    m_restorePending = true;
    m_vaultRetries = m_hurleyRetries = m_patrolRetries = 0;
    if (!m_vaultRetryTimer && m_cofferMask == 4095 && GetData(TYPE_VAULT) != DONE) m_vaultRetryTimer = 1000;
    if (!m_hurleyRetryTimer && m_hurleyRequested && GetData(TYPE_HURLEY) != DONE) m_hurleyRetryTimer = 1000;
    if (!m_uiPatrolTimer && m_patrolRequested && GetData(TYPE_BAR) != DONE) m_uiPatrolTimer = 1000;
}

void instance_blackrock_depths::OnObjectSpawn(GameObject*)
{
    m_restorePending = true;
}

int instance_blackrock_depths::RelicIndex(uint32 entry) const
{
    if (entry >= 174554 && entry <= 174564) return int(entry - 174554);
    return entry == 174566 ? 11 : -1; // 174565 is the separate Dark Coffer.
}

bool instance_blackrock_depths::OpenRelicCoffer(GameObject* go)
{
    if (!go || go->GetMap() != instance) return false;
    int index = RelicIndex(go->GetEntry());
    if (index < 0 || (m_cofferMask & (1u << index))) return false;
    m_cofferMask |= 1u << index;
    if (m_cofferMask == 4095 && GetData(TYPE_VAULT) != DONE)
    {
        m_vaultRetries = 0;
        m_vaultRetryTimer = 1;
    }
    m_restorePending = true;
    SaveState();
    return true;
}

bool instance_blackrock_depths::LightBrazier(GameObject* go)
{
    if (!go || go->GetMap() != instance || go->GetEntry() < 174744 || go->GetEntry() > 174745 || GetData(TYPE_LYCEUM) == DONE)
        return false;
    uint32 bit = 1u << (go->GetEntry() - 174744);
    if (m_brazierMask & bit) return false;
    m_brazierMask |= bit;
    m_auiEncounter[4] = m_brazierMask == 3 ? uint32(DONE) : uint32(IN_PROGRESS);
    if (m_brazierMask == 3)
        if (Creature* magmus = GetSingleCreatureFromStorage(NPC_MAGMUS, true))
            DoScriptText(YELL_MAGMUS_INTRO, magmus);
    m_restorePending = true;
    SaveState();
    return true;
}

bool instance_blackrock_depths::IsVaultConstruct(Creature* creature) const
{
    return creature && creature->GetEntry() == NPC_WARBRINGER_CONST && creature->GetDbGuid() &&
        std::abs(creature->GetPositionZ() - aVaultPositions[2]) <= 1.0f &&
        creature->IsWithinDist2d(aVaultPositions[0], aVaultPositions[1], 20.0f);
}

bool instance_blackrock_depths::StartVault()
{
    if (GetData(TYPE_VAULT) == DONE || m_cofferMask != 4095) return true;
    if (m_vaultExpected.size() != 4) return false;
    for (uint32 dbGuid : m_vaultExpected)
        if (Creature* construct = instance->GetCreature(dbGuid))
            if (construct->IsAlive()) construct->RemoveAurasDueToSpell(SPELL_STONED);
    if (!m_doomgripKilled)
    {
        Creature* doom = instance->GetCreature(m_doomgripGuid);
        if (!doom || !doom->IsAlive())
        {
            WorldObject* anchor = GetPlayerInMap();
            if (!anchor) return false;
            doom = anchor->SummonCreature(NPC_WATCHER_DOOMGRIP, aVaultPositions[0], aVaultPositions[1], aVaultPositions[2], aVaultPositions[3], TEMPSPAWN_DEAD_DESPAWN, 0);
            if (!doom) return false;
            m_doomgripGuid = doom->GetObjectGuid();
        }
    }
    m_auiEncounter[1] = IN_PROGRESS;
    CheckVaultComplete();
    SaveState();
    return true;
}

void instance_blackrock_depths::CheckVaultComplete()
{
    if (m_cofferMask == 4095 && m_doomgripKilled && m_vaultExpected.size() == 4)
    {
        for (uint32 guid : m_vaultExpected) if (!m_vaultDead.count(guid)) return;
        SetData(TYPE_VAULT, DONE);
    }
}

void instance_blackrock_depths::ResetTomb()
{
    m_resettingTomb = true;
    for (uint32 entry : aTombDwarfes)
        if (Creature* dwarf = GetSingleCreatureFromStorage(entry, true))
        {
            // These seven templates have no individual loot; this is the existing wipe reset.
            if (!dwarf->IsAlive()) dwarf->Respawn();
            else if (dwarf->IsInCombat()) dwarf->AI()->EnterEvadeMode();
            dwarf->setFaction(dwarf->GetCreatureInfo()->Faction);
            dwarf->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
        }
    m_uiDwarfRound = 0;
    m_uiDwarfFightTimer = m_tombResetTimer = 0;
    m_tombDeadMask = m_tombActivatedMask = 0;
    m_tombResumePending = m_tombRecoveredRun = false;
    m_resettingTomb = false;
}

bool instance_blackrock_depths::IsArenaRewardEntry(uint32 entry) const
{
    return entry == NPC_THELDREN || (entry >= NPC_GOROSH && entry <= NPC_HEDRUM) ||
        std::find(m_arenaGladiators.begin(), m_arenaGladiators.end(), entry) != m_arenaGladiators.end();
}

bool instance_blackrock_depths::IsArenaController(Creature* controller) const
{
    return controller && controller->IsAlive() && controller->GetObjectGuid() == m_arenaController &&
        (GetData(TYPE_RING_OF_LAW) == IN_PROGRESS || GetData(TYPE_RING_OF_LAW) == SPECIAL);
}

bool instance_blackrock_depths::BeginArenaRewardWave(Creature* controller)
{
    if (!IsArenaController(controller)) return false;
    // Freeze the selected reward mode before the first summon attempt, including
    // the retry window in which every slot may have failed to create an actor.
    m_arenaRewardWaveStarted = true;
    return true;
}

bool instance_blackrock_depths::RegisterArenaController(Creature* controller, bool challenge)
{
    if (!controller || !controller->IsAlive() || GetData(TYPE_RING_OF_LAW) == DONE ||
        IsArenaController(instance->GetCreature(m_arenaController))) return false;
    if (m_arenaController || !m_arenaSummons.empty())
        AbortArenaGeneration();
    if (m_arenaKilled.empty()) m_arenaChallenge = m_arenaChallenge || challenge;
    m_arenaController = controller->GetObjectGuid();
    m_arenaRewardWaveStarted = false;
    m_arenaSummons.clear();
    m_auiEncounter[0] = m_arenaChallenge ? uint32(SPECIAL) : uint32(IN_PROGRESS);
    SaveState();
    return true;
}

bool instance_blackrock_depths::RegisterArenaSummon(Creature* controller, Creature* summoned)
{
    if (!IsArenaController(controller) || !summoned || summoned->GetSpawnerGuid() != m_arenaController) return false;
    return m_arenaSummons.insert(summoned->GetObjectGuid()).second;
}

bool instance_blackrock_depths::IsArenaSummon(Creature* controller, Creature* summoned) const
{
    return IsArenaController(controller) && summoned && m_arenaSummons.count(summoned->GetObjectGuid()) &&
        summoned->GetSpawnerGuid() == m_arenaController;
}

bool instance_blackrock_depths::WasArenaEntryKilled(uint32 entry) const
{
    return m_arenaKilled.count(entry) != 0;
}

void instance_blackrock_depths::FinishArena(Creature* controller, bool success)
{
    if (!controller || controller->GetObjectGuid() != m_arenaController || GetData(TYPE_RING_OF_LAW) == DONE) return;
    if (success && !IsArenaController(controller)) return;
    if (!success)
    {
        AbortArenaGeneration();
        return;
    }
    if (success)
    {
        if (m_arenaChallenge)
        {
            if (!WasArenaEntryKilled(NPC_THELDREN) || m_arenaGladiators.size() != 4) return;
            for (uint32 entry : m_arenaGladiators) if (!WasArenaEntryKilled(entry)) return;
        }
        else if (!m_arenaBossEntry || !WasArenaEntryKilled(m_arenaBossEntry)) return;
    }
    // Release the generation before any despawn can re-enter the instance callbacks.
    GuidSet summons;
    summons.swap(m_arenaSummons);
    m_arenaController.Clear();
    m_auiEncounter[0] = success ? uint32(DONE) : uint32(FAIL);
    for (ObjectGuid guid : summons)
        if (Creature* summoned = instance->GetCreature(guid))
            if (summoned->IsAlive()) summoned->ForcedDespawn();
    if (success)
    {
        if (m_arenaChallenge) StartReward(1);
        else
            for (ObjectGuid guid : m_sArenaCrowdNpcGuids)
                if (Creature* spectator = instance->GetCreature(guid))
                    spectator->SetFactionTemporary(FACTION_ARENA_NEUTRAL, TEMPFACTION_RESTORE_RESPAWN);
    }
    SaveState();
    SyncObjects();
}

void instance_blackrock_depths::AbortArenaGeneration()
{
    if (GetData(TYPE_RING_OF_LAW) == DONE) return;
    GuidSet summons;
    summons.swap(m_arenaSummons);
    m_arenaController.Clear();
    m_arenaRewardWaveStarted = false;
    m_auiEncounter[0] = FAIL;
    // No spawner object is required: the instance owns the current generation.
    for (ObjectGuid guid : summons)
        if (Creature* summoned = instance->GetCreature(guid))
            if (summoned->IsAlive()) summoned->ForcedDespawn();
    SaveState();
    SyncObjects();
}

bool instance_blackrock_depths::GetArenaSelection(uint32& mob, uint32& boss, std::vector<uint32>& gladiators) const
{
    if (!m_arenaSelectionStored) return false;
    mob = m_arenaMobIndex; boss = m_arenaBossEntry; gladiators = m_arenaGladiators;
    return true;
}

void instance_blackrock_depths::StoreArenaSelection(uint32 mob, uint32 boss, std::vector<uint32> const& gladiators)
{
    if (m_arenaSelectionStored || mob >= 6 || gladiators.size() != 4 || boss < NPC_GOROSH || boss > NPC_HEDRUM) return;
    m_arenaMobIndex = mob; m_arenaBossEntry = boss; m_arenaGladiators = gladiators;
    m_arenaSelectionStored = true;
    SaveState();
}

bool instance_blackrock_depths::BeginJailBreak(Creature* windsor)
{
    if (!windsor || windsor->GetMap() != instance || (windsor->GetEntry() != 9023 && windsor->GetEntry() != 9682)) return false;
    m_jailController = windsor->GetObjectGuid();
    m_jailDead.clear();
    m_auiEncounter[6] = IN_PROGRESS;
    SaveState();
    return true;
}
bool instance_blackrock_depths::IsJailTargetDead(uint32 entry) const { return m_jailDead.count(entry) != 0; }
bool instance_blackrock_depths::AreJailTargetsDead(uint32 first, uint32 second) const
{
    return IsJailTargetDead(first) && IsJailTargetDead(second);
}

void instance_blackrock_depths::SetBarDoorIsOpen()
{
    if (m_bIsBarDoorOpen) return;
    m_bIsBarDoorOpen = true;
    SaveState();
    m_restorePending = true;
}
void instance_blackrock_depths::ScheduleBarPatrol(uint32 delay)
{
    if (GetData(TYPE_BAR) == DONE || m_patrolRequested) return;
    m_patrolRequested = true;
    m_patrolStage = 0;
    m_patrolRetries = 0;
    m_uiPatrolTimer = delay ? delay : 1;
    SaveState();
}

bool instance_blackrock_depths::SpawnHurleySlots()
{
    WorldObject* anchor = GetSingleCreatureFromStorage(NPC_PLUGGER_SPAZZRING, true);
    if (!anchor) anchor = GetPlayerInMap();
    if (!anchor) return false;
    if (!m_hurleyPositionsReady)
    {
        for (uint32 i = 0; i < 4; ++i)
        {
            if (!i) { for (uint32 j = 0; j < 3; ++j) m_hurleySlotPositions[i][j] = aHurleyPositions[j]; }
            else anchor->GetRandomPoint(aHurleyPositions[0], aHurleyPositions[1], aHurleyPositions[2], 2.0f,
                m_hurleySlotPositions[i][0], m_hurleySlotPositions[i][1], m_hurleySlotPositions[i][2]);
        }
        m_hurleyPositionsReady = true;
        SaveState();
    }
    bool complete = true;
    for (uint32 i = 0; i < 4; ++i)
    {
        if (m_hurleyDeadMask & (1u << i)) continue;
        if (Creature* current = instance->GetCreature(m_hurleySlots[i])) if (current->IsAlive()) continue;
        if (i && !instance->GetCreature(m_hurleySlots[0])) { complete = false; continue; }
        float const* pos = m_hurleySlotPositions[i];
        if (Creature* summoned = anchor->SummonCreature(i ? NPC_BLACKBREATH_CRONY : NPC_HURLEY_BLACKBREATH,
            pos[0], pos[1], pos[2], aHurleyPositions[3], TEMPSPAWN_DEAD_DESPAWN, 0))
        {
            m_hurleySlots[i] = summoned->GetObjectGuid();
            if (i)
            {
                summoned->SetWalk(false);
                summoned->SetImmuneToNPC(true); summoned->SetImmuneToPlayer(true);
                summoned->GetMotionMaster()->MoveFollow(instance->GetCreature(m_hurleySlots[0]), 1.0f, 0);
            }
        }
        else complete = false;
    }
    return complete;
}

bool instance_blackrock_depths::SpawnPatrolSlots()
{
    WorldObject* anchor = GetSingleCreatureFromStorage(NPC_PLUGGER_SPAZZRING, true);
    if (!anchor) anchor = GetPlayerInMap();
    if (!anchor) return false;
    if (!m_patrolPositionsReady)
    {
        for (auto& pos : m_patrolSlotPositions)
            anchor->GetRandomPoint(aBarPatrolPositions[0][0], aBarPatrolPositions[0][1], aBarPatrolPositions[0][2], 2.0f, pos[0], pos[1], pos[2]);
        m_patrolPositionsReady = true;
        SaveState();
    }
    bool complete = true;
    for (uint32 i = 0; i < 3; ++i)
    {
        if (m_patrolDeadMask & (1u << i)) continue;
        if (Creature* current = instance->GetCreature(m_patrolSlots[i])) if (current->IsAlive()) continue;
        float const* pos = m_patrolSlotPositions[i];
        if (Creature* summoned = anchor->SummonCreature(aBarPatrolId[i], pos[0], pos[1], pos[2], aBarPatrolPositions[0][3], TEMPSPAWN_DEAD_DESPAWN, 0))
        {
            m_patrolSlots[i] = summoned->GetObjectGuid();
            m_sBarPatrolGuids.insert(summoned->GetObjectGuid());
            float x, y, z;
            summoned->SetWalk(false);
            anchor->GetRandomPoint(aBarPatrolPositions[1][0], aBarPatrolPositions[1][1], aBarPatrolPositions[1][2], 2.0f, x, y, z);
            summoned->GetMotionMaster()->MoveIdle();
            summoned->GetMotionMaster()->MovePoint(0, x, y, z);
        }
        else complete = false;
    }
    return complete;
}

void instance_blackrock_depths::BuildSaveData()
{
    std::ostringstream out;
    for (uint32 state : m_auiEncounter) out << state << ' ';
    out << "BR2 " << m_cofferMask << ' ' << m_brazierMask << ' ' << m_tombDeadMask << ' ' << m_tombActivatedMask << ' ' << uint32(m_uiDwarfRound)
        << ' ' << m_ironHallStarted << ' ' << m_doomgripKilled << ' ' << m_arenaChallenge << ' ' << m_arenaSelectionStored
        << ' ' << m_arenaMobIndex << ' ' << m_arenaBossEntry << ' ' << m_arenaGladiators.size();
    for (uint32 entry : m_arenaGladiators) out << ' ' << entry;
    for (std::set<uint32> const* set : { &m_arenaKilled, &m_vaultDead, &m_jailDead })
    {
        out << ' ' << set->size();
        for (uint32 value : *set) out << ' ' << value;
    }
    for (SavedReward const& reward : m_rewards)
        out << ' ' << reward.state << ' ' << reward.expires << ' ' << (reward.snapshot.empty() ? "-" : reward.snapshot);
    out << ' ' << uint32(m_uiBrokenKegs) << ' ' << m_uiBarAleCount << ' ' << uint32(m_uiStolenAles) << ' ' << m_hurleyRequested
        << ' ' << m_hurleyDeadMask << ' ' << m_patrolRequested << ' ' << m_patrolDeadMask << ' ' << m_patrolStage
        << ' ' << m_hurleyPositionsReady << ' ' << m_patrolPositionsReady;
    out << std::setprecision(9);
    for (auto const& pos : m_hurleySlotPositions) for (float value : pos) out << ' ' << value;
    for (auto const& pos : m_patrolSlotPositions) for (float value : pos) out << ' ' << value;
    out << ' ' << m_bIsBarDoorOpen << ' ' << (m_uiPatrolTimer ? uint64(time(nullptr)) + (m_uiPatrolTimer + 999) / 1000 : 0)
        << ' ' << (m_uiDwarfFightTimer ? uint64(time(nullptr)) + (m_uiDwarfFightTimer + 999) / 1000 : 0);
    m_strInstData = out.str();
}

void instance_blackrock_depths::SaveState()
{
    BuildSaveData();
    SaveToDB();
}

void instance_blackrock_depths::StartReward(uint32 index)
{
    if (index >= 2 || m_rewards[index].state != 0) return;
    m_rewards[index].state = 1;
    m_rewards[index].expires = uint64(time(nullptr)) + HOUR;
    m_rewards[index].snapshot.clear();
    m_restorePending = true;
}

void instance_blackrock_depths::CloseReward(uint32 index)
{
    if (index >= 2 || m_rewards[index].state != 1) return;
    m_rewards[index].state = 2;
    m_rewards[index].snapshot.clear();
    ClearBossLootItems(index ? GO_ARENA_SPOILS : GO_CHEST_SEVEN);
    SaveState();
    m_restorePending = true;
}

bool instance_blackrock_depths::GetGameObjectLootSnapshot(uint32 entry, std::string& snapshot) const
{
    int index = entry == GO_CHEST_SEVEN ? 0 : entry == GO_ARENA_SPOILS ? 1 : -1;
    if (index < 0) return false;
    SavedReward const& reward = m_rewards[index];
    snapshot = reward.state == 1 && reward.expires > uint64(time(nullptr)) ? reward.snapshot : "closed";
    return true;
}

void instance_blackrock_depths::OnGameObjectLootSnapshot(uint32 entry, std::string const& snapshot, bool exhausted)
{
    int index = entry == GO_CHEST_SEVEN ? 0 : entry == GO_ARENA_SPOILS ? 1 : -1;
    if (index < 0 || m_rewards[index].state != 1) return;
    if (exhausted || m_rewards[index].expires <= uint64(time(nullptr)) || snapshot.size() > 32771)
        CloseReward(uint32(index));
    else if (m_rewards[index].snapshot != snapshot)
    {
        m_rewards[index].snapshot = snapshot;
        SaveState();
    }
}

void instance_blackrock_depths::SyncReward(uint32 index)
{
    SavedReward& reward = m_rewards[index];
    uint64 const now = uint64(time(nullptr));
    if (reward.state == 1 && reward.expires <= now) CloseReward(index);
    GameObject* go = GetSingleGameObjectFromStorage(index ? GO_ARENA_SPOILS : GO_CHEST_SEVEN, true);
    if (!go) return;
    if (reward.state != 1)
    {
        go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        if (go->IsSpawned()) { go->SetForcedDespawn(); go->SetLootState(GO_JUST_DEACTIVATED); }
        return;
    }
    bool refresh = !go->IsSpawned();
    go->SetRespawnTime(uint32(reward.expires - now));
    go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    if (refresh) go->Refresh();
}

void instance_blackrock_depths::SyncObjects()
{
    auto door = [&](uint32 entry, bool open)
    {
        if (GameObject* go = GetSingleGameObjectFromStorage(entry, true))
            go->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
    };
    if (GetData(TYPE_RING_OF_LAW) != IN_PROGRESS && GetData(TYPE_RING_OF_LAW) != SPECIAL)
    {
        door(GO_ARENA_1, false); door(GO_ARENA_2, false);
        door(GO_ARENA_3, GetData(TYPE_RING_OF_LAW) == DONE); door(GO_ARENA_4, true);
    }
    door(GO_TOMB_ENTER, GetData(TYPE_TOMB_OF_SEVEN) != IN_PROGRESS || m_tombResumePending);
    door(GO_TOMB_EXIT, GetData(TYPE_TOMB_OF_SEVEN) == DONE);
    bool ironCombat = false;
    if (Creature* magmus = GetSingleCreatureFromStorage(NPC_MAGMUS, true)) ironCombat = magmus->IsAlive() && magmus->IsInCombat();
    door(GO_GOLEM_ROOM_N, GetData(TYPE_LYCEUM) == DONE && !ironCombat);
    door(GO_GOLEM_ROOM_S, GetData(TYPE_LYCEUM) == DONE && !ironCombat);
    door(GO_THRONE_ROOM, GetData(TYPE_IRON_HALL) == DONE);
    door(GO_SECRET_DOOR, GetData(TYPE_VAULT) == DONE);
    if (GameObject* safe = GetSingleGameObjectFromStorage(GO_SECRET_SAFE, true))
    {
        if (GetData(TYPE_VAULT) == DONE) safe->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        else safe->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
    }
    if (GameObject* bar = GetSingleGameObjectFromStorage(GO_BAR_DOOR, true))
    {
        if (GetData(TYPE_ROCKNOT) == DONE)
        {
            bar->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE); m_bIsBarDoorOpen = true;
        }
        else if (m_bIsBarDoorOpen)
        {
            bar->SetGoState(GO_STATE_ACTIVE); m_bIsBarDoorOpen = true;
        }
    }
    for (uint32 i = 0; i < 12; ++i)
    {
        uint32 entry = i < 11 ? 174554 + i : 174566;
        if (GameObject* go = GetSingleGameObjectFromStorage(entry, true))
        {
            bool opened = (m_cofferMask & (1u << i)) != 0;
            go->SetGoState(opened ? GO_STATE_ACTIVE : GO_STATE_READY);
            if (opened) go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
        }
    }
    for (uint32 i = 0; i < 2; ++i)
        if (GameObject* go = GetSingleGameObjectFromStorage(174744 + i, true))
        {
            bool lit = (m_brazierMask & (1u << i)) != 0 || GetData(TYPE_LYCEUM) == DONE;
            go->SetGoState(lit ? GO_STATE_ACTIVE : GO_STATE_READY);
            if (lit) go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
        }
    for (uint32 i = 0; i < MAX_DWARF_RUNES; ++i)
        door(GO_DWARFRUNE_A01 + i, GetData(TYPE_FLAMELASH) == IN_PROGRESS);
    SyncReward(0); SyncReward(1);
}

void instance_blackrock_depths::RestoreProgress()
{
    // The persistent state, respawn flags, and creature AIs now exist.
    time_t now = time(nullptr);
    for (uint32 guid : m_vaultExpected)
        if (instance->GetPersistentState()->GetCreatureRespawnTime(guid) > now) m_vaultDead.insert(guid);
    for (uint32 i = 0; i < MAX_DWARFS; ++i)
        if (Creature* dwarf = GetSingleCreatureFromStorage(aTombDwarfes[i], true))
            if (dwarf->GetDbGuid() && instance->GetPersistentState()->GetCreatureRespawnTime(dwarf->GetDbGuid()) > now)
                m_tombDeadMask |= 1u << i;
    if (GetData(TYPE_TOMB_OF_SEVEN) == NOT_STARTED && m_tombDeadMask && m_tombDeadMask != 127)
    {
        m_auiEncounter[3] = IN_PROGRESS; m_uiDwarfRound = 0; m_uiDwarfFightTimer = 1000;
        m_tombResumePending = m_tombRecoveredRun = true;
    }
    if (GetData(TYPE_TOMB_OF_SEVEN) != DONE && m_tombDeadMask == 127)
        SetData(TYPE_TOMB_OF_SEVEN, DONE);
    if (GetData(TYPE_TOMB_OF_SEVEN) == IN_PROGRESS && !m_tombResumePending)
    {
        for (uint32 i = 0; i < MAX_DWARFS; ++i)
            if ((m_tombActivatedMask & (1u << i)) && !(m_tombDeadMask & (1u << i)))
                if (Creature* dwarf = GetSingleCreatureFromStorage(aTombDwarfes[i], true))
                    if (dwarf->IsAlive() && !dwarf->IsInCombat())
                    {
                        dwarf->SetFactionTemporary(FACTION_DWARF_HOSTILE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_RESTORE_REACH_HOME);
                        dwarf->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
                        if (Player* target = SelectTombTarget(dwarf))
                            dwarf->AI()->AttackStart(target);
                    }
    }
    for (ObjectGuid guid : m_sVaultNpcGuids)
        if (Creature* construct = instance->GetCreature(guid))
            if (IsVaultConstruct(construct) && construct->IsAlive())
            {
                if (m_cofferMask == 4095) construct->RemoveAurasDueToSpell(SPELL_STONED);
                else if (!construct->HasAura(SPELL_STONED)) construct->CastSpell(construct, SPELL_STONED, TRIGGERED_OLD_TRIGGERED);
            }
    if (GetData(TYPE_PLUGGER) == DONE)
        for (uint32 entry : { NPC_PRIVATE_ROCKNOT, NPC_MISTRESS_NAGMARA })
            if (Creature* actor = GetSingleCreatureFromStorage(entry, true))
                if (actor->IsAlive()) actor->ForcedDespawn();
    SyncObjects();
}

bool instance_blackrock_depths::IsTombParticipant(Player* player) const
{
    if (!player || !player->IsAlive() || player->IsGameMaster() || player->GetMap() != instance)
        return false;
    GameObject* entrance = GetSingleGameObjectFromStorage(GO_TOMB_ENTER, true);
    if (!entrance) return false;
    // The DB entrance is (1230.3297,-205.2104,-85.6843), facing -0.767945.
    // The seven home positions span depth -0.27..90.00, side -57.78..33.05,
    // and height 0.09..7.63 relative to its inside-facing plane. Require three
    // yards beyond the threshold, within the room bounds and its actual floor.
    float dx = player->GetPositionX() - entrance->GetPositionX();
    float dy = player->GetPositionY() - entrance->GetPositionY();
    float c = std::cos(entrance->GetOrientation()), s = std::sin(entrance->GetOrientation());
    float depth = dx * c + dy * s;
    float side = -dx * s + dy * c;
    return depth >= 3.0f && depth <= 105.0f && std::abs(side) <= 65.0f &&
        dx * dx + dy * dy <= 110.0f * 110.0f &&
        std::abs(player->GetPositionZ() - entrance->GetPositionZ()) <= 12.0f;
}

Player* instance_blackrock_depths::SelectTombTarget(Creature* dwarf) const
{
    for (auto const& reference : instance->GetPlayers())
        if (Player* player = reference.getSource())
            if (player->IsAlive() && !player->IsGameMaster() && player->GetMap() == instance &&
                (!m_tombRecoveredRun || IsTombParticipant(player)) &&
                (!dwarf || dwarf->IsWithinDistInMap(player, 120.0f)))
                return player;
    return nullptr;
}
InstanceData* GetInstanceData_instance_blackrock_depths(Map* pMap)
{
    return new instance_blackrock_depths(pMap);
}

void AddSC_instance_blackrock_depths()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_blackrock_depths";
    pNewScript->GetInstanceData = &GetInstanceData_instance_blackrock_depths;
    pNewScript->RegisterSelf();
}
