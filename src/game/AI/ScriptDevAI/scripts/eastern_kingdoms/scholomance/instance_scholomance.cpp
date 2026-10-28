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
SDName: Instance_Scholomance
SD%Complete: 99
SDComment: Possible some D2 or other exotic missing
SDCategory: Scholomance
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "scholomance.h"
#include "scholomance_spawns.h"
#include "GameEvents/GameEventMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Spells/SpellMgr.h"

namespace
{
uint32 const roomDoors[MAX_EVENTS] = {GO_GATE_POLKELT, GO_GATE_THEOLEN, GO_GATE_MALICIA, GO_GATE_ILLUCIA, GO_GATE_BAROV, GO_GATE_RAVENIAN};
uint32 const roomBosses[MAX_EVENTS] = {TYPE_POLKELT, TYPE_THEOLEN, TYPE_MALICIA, TYPE_ILLUCIA_BAROV, TYPE_ALEXEI_BAROV, TYPE_RAVENIAN};

uint32 BossType(uint32 entry)
{
    switch (entry)
    {
        case NPC_KIRTONOS: return TYPE_KIRTONOS;
        case NPC_RATTLEGORE: return TYPE_RATTLEGORE;
        case NPC_RAS_FROSTWHISPER: return TYPE_RAS_FROSTWHISPER;
        case NPC_INSTRUCTOR_MALICIA: return TYPE_MALICIA;
        case NPC_THEOLEN_KRASTINOV: return TYPE_THEOLEN;
        case NPC_LOREKEEPER_POLKELT: return TYPE_POLKELT;
        case NPC_RAVENIAN: return TYPE_RAVENIAN;
        case NPC_ALEXEI_BAROV: return TYPE_ALEXEI_BAROV;
        case NPC_ILLUCIA_BAROV: return TYPE_ILLUCIA_BAROV;
        case NPC_DARKMASTER_GANDLING: return TYPE_GANDLING;
        default: return MAX_ENCOUNTER;
    }
}

bool IsEntranceActor(Creature* creature)
{
    uint32 entry = creature->GetEntry();
    return creature->HasStaticDBSpawnData() &&
        (entry == NPC_REANIMATED_CORPSE || entry == NPC_DISEASED_GHOUL || entry == NPC_RISEN_ABERRATION) &&
        creature->GetPositionX() >= aEntranceRoom.m_fCornerX && creature->GetPositionX() < aEntranceRoom.m_fCornerX + aEntranceRoom.m_uiLength &&
        creature->GetPositionY() >= aEntranceRoom.m_fCornerY && creature->GetPositionY() < aEntranceRoom.m_fCornerY + aEntranceRoom.m_uiWidth &&
        creature->GetPositionZ() > aEntranceRoom.m_fCenterZ && creature->GetPositionZ() < aEntranceRoom.m_fCenterZ + 6.0f;
}

void DespawnLiving(Map* map, ObjectGuid guid)
{
    if (Creature* creature = map->GetAnyTypeCreature(guid))
        if (creature->IsAlive()) creature->ForcedDespawn();
}
}

instance_scholomance::instance_scholomance(Map* map) : ScriptedInstance(map)
{
    Initialize();
}

void instance_scholomance::Initialize()
{
    m_progress = ScholomanceProgress();
    m_strInstData = m_progress.Encode();
}

void instance_scholomance::SaveState()
{
    m_strInstData = m_progress.Encode();
    SaveToDB();
}

void instance_scholomance::Load(char const* data)
{
    ScholomanceProgress candidate;
    if (!ScholomanceProgress::Decode(data, candidate))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }
    m_progress = candidate;
    m_strInstData = m_progress.Encode();
    m_entranceActors = {};
    m_paladinActors = {};
    m_portals = {};
    m_kirtonosTimer = 0;
    m_retryTimer = 0;
    m_gandlingAttempts = m_chargerAttempts = 0;
    m_paladinPaused = false;
    SyncDoors();
}

void instance_scholomance::OnPlayerEnter(Player* /*player*/)
{
    // A new participant can retry a request which exhausted its short retry budget.
    m_gandlingAttempts = m_chargerAttempts = 0;
    for (auto& actor : m_entranceActors) actor.attempts = 0;
    for (auto& actor : m_paladinActors) actor.attempts = 0;
    if (m_progress.gambit == DONE)
        for (ObjectGuid guid : m_viewingRoom) m_gambitPending[guid] = 0;
    RestoreGandling(false);
    RestoreEntrance();
    SyncDoors();
}

void instance_scholomance::OnPlayerLeave(Player* player)
{
    if (m_progress.gambit == IN_PROGRESS || m_progress.paladin == IN_PROGRESS) SaveState();
    for (auto const& ref : instance->GetPlayers())
        if (Player* other = ref.getSource())
            if (other != player && !other->IsGameMaster()) return;
    // Detach GUIDs before callbacks. Completed kills remain in the durable ledger.
    if (m_progress.paladin == IN_PROGRESS)
    {
        m_paladinPaused = true;
        ClearPaladinActors();
    }
    for (uint32 room = 0; room < MAX_EVENTS; ++room) ClearPortal(room);
}

void instance_scholomance::OnCreatureCreate(Creature* creature)
{
    if (IsEntranceActor(creature))
    {
        m_staticEntrance.insert(creature->GetObjectGuid());
        if (GetData(TYPE_RATTLEGORE) == DONE && creature->IsAlive())
            creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_UNINTERACTIBLE);
    }
    uint32 entry = creature->GetEntry();
    if (entry == NPC_SCHOLOMANCE_STUDENT || entry == NPC_VECTUS || entry == NPC_MARDUK_BLACKPOOL)
    {
        m_viewingRoom.insert(creature->GetObjectGuid());
        if (m_progress.gambit == DONE) m_gambitPending[creature->GetObjectGuid()] = 0;
    }
    if (entry == NPC_VECTUS || entry == NPC_MARDUK_BLACKPOOL || entry == NPC_DARKMASTER_GANDLING || entry == NPC_KIRTONOS)
        m_npcEntryGuidStore[entry] = creature->GetObjectGuid();
    if (entry == NPC_KIRTONOS && m_kirtonosTimer) m_kirtonosTimer = 600000;
    if (entry == NPC_FALLEN_CHARGER)
    {
        m_charger = creature->GetObjectGuid();
        creature->SetActiveObjectState(true);
    }
}

void instance_scholomance::OnCreatureRespawn(Creature* creature)
{
    if (IsEntranceActor(creature))
    {
        m_staticEntrance.insert(creature->GetObjectGuid());
        if (GetData(TYPE_RATTLEGORE) == DONE && creature->IsAlive())
            creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_UNINTERACTIBLE);
    }
    if (m_progress.gambit == DONE && m_viewingRoom.count(creature->GetObjectGuid()))
        m_gambitPending[creature->GetObjectGuid()] = 0;
}

void instance_scholomance::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_VIEWING_ROOM_DOOR:
            if (GetData(TYPE_RATTLEGORE) == DONE) go->SetGoState(GO_STATE_ACTIVE);
            break;
        case GO_BRAZIER_HERALD:
        case GO_GATE_KIRTONOS: case GO_GATE_RAS: case GO_GATE_GANDLING:
        case GO_GATE_POLKELT: case GO_GATE_THEOLEN: case GO_GATE_MALICIA:
        case GO_GATE_ILLUCIA: case GO_GATE_BAROV: case GO_GATE_RAVENIAN:
            m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
            SyncDoors();
            break;
    }
}

void instance_scholomance::SetDoor(uint32 entry, bool open)
{
    if (GameObject* go = GetSingleGameObjectFromStorage(entry))
        go->SetGoState(open ? GO_STATE_ACTIVE : GO_STATE_READY);
}

void instance_scholomance::SyncDoors()
{
    SetDoor(GO_GATE_KIRTONOS, GetData(TYPE_KIRTONOS) != IN_PROGRESS && !m_kirtonosTimer);
    SetDoor(GO_GATE_RAS, GetData(TYPE_RAS_FROSTWHISPER) != IN_PROGRESS);
    SetDoor(GO_GATE_GANDLING, GetData(TYPE_GANDLING) != IN_PROGRESS);
    for (uint32 i = 0; i < MAX_EVENTS; ++i)
        SetDoor(roomDoors[i], GetData(roomBosses[i]) != IN_PROGRESS && !m_portals[i].active);
}

uint32 instance_scholomance::GetData(uint32 type) const
{
    return type < MAX_ENCOUNTER ? m_progress.encounters[type] : 0;
}

void instance_scholomance::SetData(uint32 type, uint32 data)
{
    if (type >= MAX_ENCOUNTER || data > DONE || GetData(type) == DONE || GetData(type) == data) return;
    m_progress.encounters[type] = data;
    if (type == TYPE_KIRTONOS) m_kirtonosTimer = 0;
    if (type == TYPE_RATTLEGORE && data == DONE) PrepareEntrance();
    if (type == TYPE_GANDLING && (data == FAIL || data == DONE))
        for (uint32 i = 0; i < MAX_EVENTS; ++i) ClearPortal(i);
    SyncDoors();
    SaveState();
    if (data == DONE)
    {
        m_gandlingAttempts = 0;
        RestoreGandling(true);
        RestoreEntrance();
    }
}

void instance_scholomance::PrepareEntrance()
{
    if (m_progress.entrance[16]) return;
    for (uint32 group = 0; group < MAX_GROUPS; ++group)
    {
        std::array<uint32, 4> entries = {{NPC_RISEN_ABERRATION, NPC_RISEN_ABERRATION, NPC_DISEASED_GHOUL,
            urand(0, 1) ? uint32(NPC_REANIMATED_CORPSE) : uint32(NPC_DISEASED_GHOUL)}};
        std::shuffle(entries.begin(), entries.end(), *GetRandomGenerator());
        for (uint32 slot = 0; slot < 4; ++slot) m_progress.entrance[group * 4 + slot] = entries[slot];
    }
    m_progress.entrance[16] = NPC_NECROFIEND;
}

void instance_scholomance::SuppressEntranceActors()
{
    if (GetData(TYPE_RATTLEGORE) != DONE) return;
    // Keep earned corpses. Suppression also runs for late grid loads and respawns.
    GuidSet pending;
    pending.swap(m_staticEntrance);
    for (ObjectGuid guid : pending)
        if (Creature* creature = instance->GetCreature(guid))
        {
            if (creature->IsAlive()) creature->ForcedDespawn();
            time_t until = static_cast<DungeonPersistentState*>(instance->GetPersistentState())->GetResetTime();
            if (until > time(nullptr)) instance->GetPersistentState()->SaveCreatureRespawnTime(creature->GetDbGuid(), until);
        }
}

void instance_scholomance::RestoreEntrance()
{
    if (!m_progress.entrance[16]) return;
    Player* player = GetPlayerInMap(true, false);
    if (!player) return;
    SuppressEntranceActors();
    bool changed = false;
    for (uint32 i = 0; i < m_entranceActors.size(); ++i)
    {
        auto& slot = m_entranceActors[i];
        if ((m_progress.entranceDead & (1u << i)) || instance->GetCreature(slot.guid) || slot.attempts >= 5) continue;
        ++slot.attempts;
        Position const& pos = aEntranceRoomSpawnLocs[i];
        if (Creature* actor = player->SummonCreature(m_progress.entrance[i], pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_DEAD_DESPAWN, 0))
        {
            slot.guid = actor->GetObjectGuid();
            slot.attempts = 0;
            actor->SetActiveObjectState(true);
            m_progress.entranceCreated |= 1u << i;
            if (i == 16) actor->GetMotionMaster()->MoveWaypoint();
            changed = true;
        }
    }
    if (changed) SaveState();
}

void instance_scholomance::RestoreGandling(bool announce)
{
    if (GetData(TYPE_GANDLING) == DONE || m_gandlingAttempts >= 5) return;
    if (Creature* boss = GetSingleCreatureFromStorage(NPC_DARKMASTER_GANDLING))
        if (boss->IsAlive()) return;
    if (GetData(TYPE_GANDLING) == IN_PROGRESS) SetData(TYPE_GANDLING, FAIL);
    for (uint32 type : roomBosses) if (GetData(type) != DONE) return;
    Player* player = GetPlayerInMap(true, false);
    if (!player) return;
    ++m_gandlingAttempts;
    Position const& pos = aGandlingSpawnLocs[0];
    if (Creature* boss = player->SummonCreature(NPC_DARKMASTER_GANDLING, pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_DEAD_DESPAWN, 0))
    {
        boss->SetActiveObjectState(true);
        m_gandlingAttempts = 0;
        if (IsHolidayActive(HOLIDAY_FEAST_OF_WINTER_VEIL)) boss->CastSpell(boss, SPELL_XMAS_GANDLING, TRIGGERED_NONE);
        if (announce) DoScriptText(SAY_GANDLING_SPAWN, boss);
    }
}

void instance_scholomance::HandlePortalEvent(uint32 eventId, Creature* source)
{
    if (!source || source->GetEntry() != NPC_DARKMASTER_GANDLING || source->GetMap() != instance ||
        source != GetSingleCreatureFromStorage(NPC_DARKMASTER_GANDLING) || GetData(TYPE_GANDLING) != IN_PROGRESS) return;
    for (uint32 i = 0; i < MAX_EVENTS; ++i)
        if (aGandlingEvents[i] == eventId && !m_portals[i].active)
        {
            m_portals[i] = GandlingEventData();
            m_portals[i].active = true;
            m_portals[i].timer = 2000;
            SyncDoors();
            return;
        }
    // A second player sent to an occupied room shares the existing guard batch.
}

void instance_scholomance::ClearPortal(uint32 room)
{
    auto guards = m_portals[room].guards;
    m_portals[room] = GandlingEventData();
    SyncDoors();
    for (ObjectGuid guid : guards) DespawnLiving(instance, guid);
}

void instance_scholomance::UpdatePortals(uint32 diff)
{
    for (uint32 room = 0; room < MAX_EVENTS; ++room)
    {
        auto& batch = m_portals[room];
        if (!batch.active) continue;
        if (GetData(TYPE_GANDLING) != IN_PROGRESS) { ClearPortal(room); continue; }
        bool vanished = false;
        for (ObjectGuid guid : batch.guards)
            if (guid && !instance->GetCreature(guid)) vanished = true;
        if (vanished) { ClearPortal(room); continue; }
        if (!batch.timer) continue;
        if (batch.timer > diff) { batch.timer -= diff; continue; }
        Creature* boss = GetSingleCreatureFromStorage(NPC_DARKMASTER_GANDLING);
        if (!boss || !boss->IsAlive()) { ClearPortal(room); continue; }
        bool missing = false;
        uint32 slot = 0;
        for (auto const& request : aPortalSpawns)
        {
            if (request.event != aGandlingEvents[room]) continue;
            if (!batch.guards[slot] && !(batch.dead & (1u << slot)))
            {
                auto const& pos = request.position;
                if (Creature* guard = boss->SummonCreature(request.entry, pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, request.lifetime))
                {
                    batch.guards[slot] = guard->GetObjectGuid();
                    guard->SetActiveObjectState(true);
                }
                else missing = true;
            }
            ++slot;
        }
        if (!missing) batch.timer = 0;
        else if (++batch.attempts >= 3) ClearPortal(room);
        else batch.timer = 1000;
    }
}

void instance_scholomance::HandleDawnGambitEvent()
{
    if (m_progress.gambit != NOT_STARTED) return;
    m_progress.gambit = IN_PROGRESS;
    m_progress.gambitRemaining = 12000;
    SaveState();
}

void instance_scholomance::ApplyGambit(Creature* creature)
{
    if (!creature->IsAlive()) return;
    creature->SetFactionTemporary(FACTION_SCOURGE, TEMPFACTION_RESTORE_RESPAWN);
    if (creature->GetEntry() != NPC_SCHOLOMANCE_STUDENT) return;
    creature->SetStandState(UNIT_STAND_STATE_STAND);
    if (!creature->HasAura(SPELL_STUDENT_TRANSFORM))
    {
        creature->CastSpell(creature, SPELL_STUDENT_TRANSFORM, TRIGGERED_OLD_TRIGGERED);
        if (!creature->IsInCombat())
            creature->GetMotionMaster()->MoveRandomAroundPoint(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), 2.0f);
    }
}

bool instance_scholomance::StartKirtonosEvent()
{
    if (GetData(TYPE_KIRTONOS) == DONE || GetData(TYPE_KIRTONOS) == IN_PROGRESS || m_kirtonosTimer) return false;
    if (Creature* creature = GetSingleCreatureFromStorage(NPC_KIRTONOS)) if (creature->IsAlive()) return false;
    m_kirtonosTimer = 10000; // DB summons after five seconds; release if that summon fails.
    SyncDoors();
    return true;
}

bool instance_scholomance::StartPaladinEvent(Player* player)
{
    if (!player || player->GetMap() != instance || !player->IsAlive() || player->IsGameMaster() ||
        player->GetQuestStatus(QUEST_JUDGMENT_REDEMPTION) != QUEST_STATUS_INCOMPLETE || GetData(TYPE_RATTLEGORE) != DONE ||
        player->GetPositionX() < 115.0f || player->GetPositionX() > 180.0f ||
        player->GetPositionY() < 125.0f || player->GetPositionY() > 205.0f ||
        player->GetPositionZ() < 85.0f || player->GetPositionZ() > 100.0f ||
        m_progress.paladin == IN_PROGRESS || m_progress.paladin == DONE) return false;
    ClearPaladinActors();
    m_progress.paladin = IN_PROGRESS;
    m_progress.paladinElapsed = m_progress.paladinCreated = m_progress.paladinDead = 0;
    m_progress.chargerUntil = 0;
    m_paladinPaused = false;
    SaveState();
    return true;
}

void instance_scholomance::ClearPaladinActors()
{
    auto actors = m_paladinActors;
    m_paladinActors = {};
    for (auto const& slot : actors) DespawnLiving(instance, slot.guid);
}

void instance_scholomance::FailPaladinEvent()
{
    if (m_progress.paladin != IN_PROGRESS) return;
    m_progress.paladin = FAIL;
    ClearPaladinActors();
    SaveState();
}

void instance_scholomance::UpdatePaladin(uint32 diff)
{
    if (m_progress.paladin != IN_PROGRESS) return;
    Player* player = GetPlayerInMap(true, false);
    if (!player)
    {
        if (GetPlayerInMap(false, false)) FailPaladinEvent(); // All participants died.
        else if (!m_paladinPaused)
        {
            m_paladinPaused = true;
            ClearPaladinActors();
            SaveState();
        }
        return;
    }
    m_paladinPaused = false;
    m_progress.paladinElapsed += std::min(diff, 300000u - m_progress.paladinElapsed);
    bool changed = false;
    for (uint32 i = 0; i < m_paladinActors.size(); ++i)
    {
        auto& slot = m_paladinActors[i];
        auto const& request = aPaladinSpawns[i];
        if (m_progress.paladinElapsed < request.delay || (m_progress.paladinDead & (1u << i))) continue;
        if (slot.guid)
        {
            if (!instance->GetCreature(slot.guid)) { FailPaladinEvent(); return; }
            continue;
        }
        // Retry at the shared one-second cadence, including reconstructed actors.
        if (slot.attempts && m_retryTimer) continue;
        auto const& pos = request.position;
        if (Creature* actor = player->SummonCreature(request.entry, pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, request.lifetime))
        {
            slot.guid = actor->GetObjectGuid();
            slot.attempts = 0;
            actor->SetActiveObjectState(true);
            m_progress.paladinCreated |= 1u << i;
            changed = true;
        }
        else if (++slot.attempts >= 5) { FailPaladinEvent(); return; }
    }
    if (changed) SaveState();
}

void instance_scholomance::RestoreCharger()
{
    if (m_progress.paladin != DONE || !m_progress.chargerUntil || m_progress.chargerUntil <= uint64(time(nullptr)) || m_chargerAttempts >= 5) return;
    if (Creature* horse = instance->GetCreature(m_charger)) if (horse->IsAlive()) return;
    Player* player = GetPlayerInMap(true, false);
    if (!player) return;
    ++m_chargerAttempts;
    auto const& pos = m_progress.chargerPosition;
    uint32 remaining = uint32(m_progress.chargerUntil - uint64(time(nullptr))) * 1000;
    if (Creature* horse = player->SummonCreature(NPC_FALLEN_CHARGER, pos[0], pos[1], pos[2], pos[3], TEMPSPAWN_TIMED_OR_DEAD_DESPAWN, remaining))
    {
        m_charger = horse->GetObjectGuid();
        m_chargerAttempts = 0;
        horse->SetActiveObjectState(true);
    }
}

void instance_scholomance::OnCreatureEnterCombat(Creature* creature)
{
    uint32 type = BossType(creature->GetEntry());
    if (type < MAX_ENCOUNTER) SetData(type, IN_PROGRESS);
}

void instance_scholomance::OnCreatureEvade(Creature* creature)
{
    uint32 type = BossType(creature->GetEntry());
    if (type < MAX_ENCOUNTER) SetData(type, FAIL);
    for (uint32 i = 0; i < MAX_EVENTS; ++i)
        for (ObjectGuid guid : m_portals[i].guards)
            if (guid == creature->GetObjectGuid()) { ClearPortal(i); return; }
    for (auto const& actor : m_paladinActors)
        if (actor.guid == creature->GetObjectGuid()) { FailPaladinEvent(); return; }
}

void instance_scholomance::OnCreatureDeath(Creature* creature)
{
    uint32 type = BossType(creature->GetEntry());
    if (type < MAX_ENCOUNTER) SetData(type, DONE);
    ObjectGuid guid = creature->GetObjectGuid();
    for (uint32 i = 0; i < m_entranceActors.size(); ++i)
        if (m_entranceActors[i].guid == guid)
        {
            m_progress.entranceDead |= 1u << i;
            m_entranceActors[i].guid.Clear();
            SaveState();
            return;
        }
    for (uint32 room = 0; room < MAX_EVENTS; ++room)
        for (uint32 slot = 0; slot < 4; ++slot)
            if (m_portals[room].guards[slot] == guid)
            {
                auto& batch = m_portals[room];
                batch.guards[slot].Clear();
                batch.dead |= 1u << slot;
                if (batch.dead == (room == 0 ? 15u : 7u)) ClearPortal(room);
                return;
            }
    for (uint32 i = 0; i < m_paladinActors.size(); ++i)
        if (m_paladinActors[i].guid == guid)
        {
            m_progress.paladinDead |= 1u << i;
            m_paladinActors[i].guid.Clear();
            if (creature->GetEntry() == NPC_DARKREAVER)
            {
                m_progress.paladin = DONE;
                m_progress.chargerPosition = {{creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation()}};
                // EventAI still performs the normal death summon. Only a missing
                // horse is restored, without replaying the boss death or loot.
                if (SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(23261))
                {
                    int32 duration = GetSpellDuration(spell);
                    if (duration > 0) m_progress.chargerUntil = uint64(time(nullptr)) + (duration + 999) / 1000;
                }
            }
            SaveState();
            return;
        }
}

void instance_scholomance::OnCreatureDespawn(Creature* creature)
{
    ObjectGuid guid = creature->GetObjectGuid();
    if ((creature->GetEntry() == NPC_DARKMASTER_GANDLING || creature->GetEntry() == NPC_KIRTONOS) &&
        m_npcEntryGuidStore[creature->GetEntry()] == guid)
    {
        m_npcEntryGuidStore.erase(creature->GetEntry());
        m_kirtonosTimer = creature->GetEntry() == NPC_KIRTONOS ? 0 : m_kirtonosTimer;
        SetData(BossType(creature->GetEntry()), FAIL);
        SyncDoors();
    }
    for (uint32 room = 0; room < MAX_EVENTS; ++room)
        for (ObjectGuid guard : m_portals[room].guards)
            if (guard == guid) { ClearPortal(room); return; }
    for (auto& actor : m_entranceActors)
        if (actor.guid == guid) actor.guid.Clear();
    for (auto const& actor : m_paladinActors)
        if (actor.guid == guid) { FailPaladinEvent(); return; }
    if (m_charger == guid && m_progress.paladin == DONE)
    {
        m_charger.Clear();
        m_progress.chargerUntil = 0;
        SaveState();
    }
}

void instance_scholomance::Update(uint32 diff)
{
    if (m_retryTimer > diff) m_retryTimer -= diff;
    else m_retryTimer = 0;
    if (m_kirtonosTimer)
    {
        if (m_kirtonosTimer > diff) m_kirtonosTimer -= diff;
        else
        {
            m_kirtonosTimer = 0;
            SyncDoors();
            if (GameObject* brazier = GetSingleGameObjectFromStorage(GO_BRAZIER_HERALD))
            {
                brazier->SetLootState(GO_READY);
                brazier->SetGoState(GO_STATE_READY);
            }
        }
    }
    if (m_progress.gambit == IN_PROGRESS)
    {
        if (m_progress.gambitRemaining > diff) m_progress.gambitRemaining -= diff;
        else
        {
            m_progress.gambitRemaining = 0;
            m_progress.gambit = DONE;
            SaveState();
            for (ObjectGuid guid : m_viewingRoom) m_gambitPending[guid] = 0;
            if (Creature* vectus = GetSingleCreatureFromStorage(NPC_VECTUS))
                if (vectus->IsAlive()) DoScriptText(YELL_VECTUS_GAMBIT, vectus);
        }
    }
    UpdatePortals(diff);
    UpdatePaladin(diff);
    if (!m_retryTimer)
    {
        RestoreGandling(false);
        RestoreEntrance();
        RestoreCharger();
        for (auto itr = m_gambitPending.begin(); itr != m_gambitPending.end(); )
        {
            Creature* actor = instance->GetCreature(itr->first);
            if (actor) ApplyGambit(actor);
            if (!actor || !actor->IsAlive() || actor->GetEntry() != NPC_SCHOLOMANCE_STUDENT || actor->HasAura(SPELL_STUDENT_TRANSFORM) || ++itr->second >= 5)
                itr = m_gambitPending.erase(itr);
            else ++itr;
        }
        m_retryTimer = 1000;
    }
    if (m_checkpointTimer > diff) m_checkpointTimer -= diff;
    else
    {
        if (m_progress.gambit == IN_PROGRESS || (m_progress.paladin == IN_PROGRESS && !m_paladinPaused)) SaveState();
        m_checkpointTimer = 5000;
    }
}

InstanceData* GetInstanceData_instance_scholomance(Map* map) { return new instance_scholomance(map); }

bool ProcessEventId_event_spell_gandling_shadow_portal(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (!source || source->GetTypeId() != TYPEID_UNIT || !isStart) return false;
    Creature* boss = static_cast<Creature*>(source);
    if (boss->GetMapId() != 289 || boss->GetEntry() != NPC_DARKMASTER_GANDLING) return false;
    if (auto* data = static_cast<instance_scholomance*>(boss->GetInstanceData()))
    {
        for (uint32 event : aGandlingEvents)
            if (eventId == event)
            {
                data->HandlePortalEvent(eventId, boss);
                return true; // The instance acknowledges each summon; do not also run the DB batch.
            }
    }
    return false;
}

bool ProcessEventId_dawn_gambit(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (eventId != EVENT_ID_DAWN_GAMBIT || !source || !isStart || !source->isType(TYPEMASK_CREATURE_OR_GAMEOBJECT)) return false;
    auto* object = static_cast<WorldObject*>(source);
    if (object->GetMapId() == 289)
        if (auto* data = static_cast<instance_scholomance*>(object->GetInstanceData()))
        {
            data->HandleDawnGambitEvent();
            return true;
        }
    return false;
}

bool ProcessEventId_scholomance_paladin(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (eventId != EVENT_ID_PALADIN || !source || source->GetTypeId() != TYPEID_PLAYER || !isStart) return false;
    Player* player = static_cast<Player*>(source);
    if (player->GetMapId() != 289) return true;
    if (auto* data = static_cast<instance_scholomance*>(player->GetInstanceData())) data->StartPaladinEvent(player);
    return true;
}

bool GOUse_scholomance_brazier(Player* player, GameObject* go)
{
    if (!player || !player->IsAlive() || player->GetMap() != go->GetMap() || go->GetMapId() != 289) return true;
    if (auto* data = static_cast<instance_scholomance*>(go->GetInstanceData())) return !data->StartKirtonosEvent();
    return true;
}

void AddSC_instance_scholomance()
{
    Script* script = new Script;
    script->Name = "instance_scholomance";
    script->GetInstanceData = &GetInstanceData_instance_scholomance;
    script->RegisterSelf();
    script = new Script;
    script->Name = "event_spell_gandling_shadow_portal";
    script->pProcessEventId = &ProcessEventId_event_spell_gandling_shadow_portal;
    script->RegisterSelf();
    script = new Script;
    script->Name = "event_dawn_gambit";
    script->pProcessEventId = &ProcessEventId_dawn_gambit;
    script->RegisterSelf();
    script = new Script;
    script->Name = "event_scholomance_paladin";
    script->pProcessEventId = &ProcessEventId_scholomance_paladin;
    script->RegisterSelf();
    script = new Script;
    script->Name = "go_scholomance_brazier";
    script->pGOUse = &GOUse_scholomance_brazier;
    script->RegisterSelf();
}
