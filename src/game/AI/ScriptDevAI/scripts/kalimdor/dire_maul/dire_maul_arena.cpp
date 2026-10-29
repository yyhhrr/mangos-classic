/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul_arena.h"
#include "Globals/ObjectMgr.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnManager.h"
#include "Maps/SpawnGroup.h"
#include <algorithm>
#include <ctime>
#include <limits>
#include <map>
#include <sstream>

namespace
{
enum { DMA_NOTICE_SPAWN = 1, DMA_NOTICE_AGGRO = 2, DMA_NOTICE_KILL = 3 };
uint64 const dmaMaximumTime = 4102444800ULL;
uint64 const dmaMaximumPeriod = 31 * 24 * 60 * 60;
template<class T> bool ArenaNumber(std::istream& in, T& result, uint64 maximum)
{
    std::string token;
    if (!(in >> token) || token.empty()) return false;
    uint64 number = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9' || number > maximum / 10 ||
            (number == maximum / 10 && uint64(c - '0') > maximum % 10)) return false;
        number = number * 10 + uint64(c - '0');
    }
    result = T(number); return true;
}
uint32 ArenaText(uint32 entry, uint32 kind)
{
    if (entry == DMA_MUSHGOG) return kind == DMA_NOTICE_SPAWN ? 9501 : kind == DMA_NOTICE_AGGRO ? 9502 : 0;
    if (entry == DMA_RAZZA) return kind == DMA_NOTICE_SPAWN ? 9503 : kind == DMA_NOTICE_AGGRO ? 9504 : kind == DMA_NOTICE_KILL ? 9505 : 0;
    if (entry == DMA_SKARR) return kind == DMA_NOTICE_SPAWN ? 9506 : kind == DMA_NOTICE_AGGRO ? 9507 : 0;
    return 0; //9500 is Mushgog's original EventAI text;9508 has no proven trigger.
}
}

DireMaulArena::DireMaulArena(ScriptedInstance& owner, std::function<void()> save) : m_owner(owner), m_save(std::move(save)) {}
uint64 DireMaulArena::Now() { return uint64(time(nullptr)); }
bool DireMaulArena::ArenaEntry(uint32 entry) { return entry == DMA_MUSHGOG || entry == DMA_RAZZA || entry == DMA_SKARR; }
void DireMaulArena::Commit() { if (m_valid && m_save) m_save(); }
uint32 DireMaulArena::GetGeneration() const { return m_state.generation; }
uint32 DireMaulArena::GetSelectedEntry() const { return m_state.entry; }
uint64 DireMaulArena::GetNextSpawnTime() const { return m_state.next; }
std::string DireMaulArena::Encode(State const& state)
{
    std::ostringstream out;
    out << "DAM1 " << state.phase << ' ' << state.entry << ' ' << state.generation << ' ' << state.spawnSaid << ' ' << state.next;
    return out.str();
}
std::string DireMaulArena::Save() const { return Encode(m_state); }
bool DireMaulArena::Decode(std::string const& text, State& result)
{
    State state;
    if (text.empty()) { result = state; return true; }
    if (text.size() > 256) return false;
    std::istringstream in(text); std::string version;
    if (!(in >> version) || version != "DAM1" || !ArenaNumber(in, state.phase, COOLDOWN) ||
        !ArenaNumber(in, state.entry, DMA_SKARR) || !ArenaNumber(in, state.generation, 1000000000) ||
        !ArenaNumber(in, state.spawnSaid, 1) || !ArenaNumber(in, state.next, dmaMaximumTime)) return false;
    in >> std::ws; if (!in.eof()) return false;
    if ((state.generation == 0) != (state.entry == 0)) return false;
    if (state.entry && !ArenaEntry(state.entry)) return false;
    if (state.phase == EMPTY && (state.entry || state.generation || state.spawnSaid || state.next)) return false;
    if ((state.phase == REQUESTED || state.phase == ALIVE) && (!state.entry || !state.generation || state.next)) return false;
    if (state.phase == REQUESTED && state.spawnSaid) return false;
    if (state.phase == COOLDOWN && (!state.next || state.next > Now() + dmaMaximumPeriod)) return false;
    if (!state.generation && state.spawnSaid) return false;
    result = state; return true;
}
bool DireMaulArena::ValidateSave(std::string const& text) { State state; return Decode(text, state); }
bool DireMaulArena::Load(std::string const& text)
{
    State state;
    if (!Decode(text, state))
    {
        m_valid = false; DropNotices();
        script_error_log("Dire Maul arena: damaged world save; scheduling disabled until valid data is loaded");
        return false;
    }
    m_state = state; m_valid = true; m_actor.Clear(); m_spectator.Clear();
    m_retryAt = m_noticeAt = 0; m_attempts = 0; m_combat = false; DropNotices();
    // Map constructs/loads its script before persistent state and SpawnManager.
    // All native reconciliation therefore belongs to the first normal Update.
    return true;
}
bool DireMaulArena::Ready()
{
    if (!m_valid || !m_owner.instance || m_owner.instance->GetId() != DMA_MAP) return false;
    CreatureData const* data = sObjectMgr.GetCreatureData(DMA_SPAWN_GUID);
    SpawnGroup* group = m_owner.instance->GetSpawnManager().GetSpawnGroup(DMA_GROUP);
    if (!data || !group || data->mapid != DMA_MAP || data->spawnMask || data->id || !data->spawntimesecsmin ||
        data->spawntimesecsmin > data->spawntimesecsmax || uint64(data->spawntimesecsmax) > dmaMaximumPeriod) return false;
    auto const& definition = group->GetGroupEntry();
    if (definition.Type != SPAWN_GROUP_CREATURE || definition.MaxCount != 1 || definition.DbGuids.size() != 1 ||
        definition.DbGuids[0].DbGuid != DMA_SPAWN_GUID || definition.RandomEntries.size() != 3 || definition.HasChancedSpawns) return false;
    uint32 mask = 0;
    for (auto const& candidate : definition.RandomEntries)
    {
        if (!ArenaEntry(candidate.Entry)) return false;
        uint32 bit = candidate.Entry == DMA_MUSHGOG ? 1 : candidate.Entry == DMA_RAZZA ? 2 : 4;
        if (mask & bit) return false;
        mask |= bit;
    }
    if (mask != 7) return false;
    // Native automatic selection is deliberately disabled. It provides the
    // single static slot/entry policy; this coordinator restores the saved
    // selected entry before explicit native creation, so restart cannot reroll.
    group->SetEnabled(false); m_ready = true; return true;
}
bool DireMaulArena::LocalGridLoaded() const
{
    CreatureData const* data = sObjectMgr.GetCreatureData(DMA_SPAWN_GUID);
    return data && m_owner.instance->IsLoaded(data->posX, data->posY);
}
Creature* DireMaulArena::GetBoss() const
{
    return m_actor ? m_owner.instance->GetCreature(m_actor) : nullptr;
}
bool DireMaulArena::IsCurrentBoss(Creature* boss) const
{
    return m_valid && boss && boss->GetMap() == m_owner.instance && boss->GetMapId() == DMA_MAP &&
        boss->HasStaticDBSpawnData() && boss->GetDbGuid() == DMA_SPAWN_GUID && ArenaEntry(boss->GetEntry()) &&
        boss->GetEntry() == m_state.entry && m_actor == boss->GetObjectGuid() &&
        m_owner.instance->GetCreature(m_actor) == boss && m_state.phase == ALIVE;
}
bool DireMaulArena::ObserveBoss()
{
    // Native dbGuid lookup is bounded and includes dynamic live GUIDs. It
    // prioritizes a living actor, without scanning every creature on Kalimdor.
    Creature* boss = m_owner.instance->GetCreature(uint32(DMA_SPAWN_GUID));
    if (!boss) { m_actor.Clear(); m_combat = false; return false; }
    if (!ArenaEntry(boss->GetEntry()) || !boss->HasStaticDBSpawnData()) return true;
    if (!boss->IsAlive()) return true; // Never replace a body before native corpse/loot removal.
    if (m_state.phase == EMPTY)
    {
        m_state.entry = boss->GetEntry(); ++m_state.generation; m_state.phase = ALIVE; m_state.spawnSaid = 0;
        m_actor = boss->GetObjectGuid(); Commit(); QueueNotice(DMA_NOTICE_SPAWN); return true;
    }
    if ((m_state.phase == REQUESTED || m_state.phase == ALIVE) && boss->GetEntry() == m_state.entry)
    {
        m_actor = boss->GetObjectGuid(); m_attempts = 0;
        if (m_state.phase == REQUESTED) { m_state.phase = ALIVE; Commit(); }
        if (!m_state.spawnSaid) QueueNotice(DMA_NOTICE_SPAWN);
    }
    // A wrong/old-generation real actor is never killed to make space. The
    // static slot remains occupied until its native death/unload completes.
    return true;
}
void DireMaulArena::ReconcileCooldown()
{
    uint64 native = uint64(m_owner.instance->GetPersistentState()->GetCreatureRespawnTime(DMA_SPAWN_GUID));
    if (native > Now() + dmaMaximumPeriod)
    {
        m_valid = false; DropNotices();
        script_error_log("Dire Maul arena: invalid native cooldown; refusing an early spawn"); return;
    }
    Creature* live = GetBoss();
    if (live && live->IsAlive() && IsCurrentBoss(live)) return;
    if (native > Now() && (m_state.phase != COOLDOWN || m_state.next < native))
    {
        m_state.phase = COOLDOWN; m_state.next = std::max(native, m_state.next);
        m_combat = false; DropNotices(); Commit();
    }
    if (m_state.phase == COOLDOWN && m_state.next > Now() && native < m_state.next)
        m_owner.instance->GetPersistentState()->SaveCreatureRespawnTime(DMA_SPAWN_GUID, time_t(m_state.next));
}
void DireMaulArena::TrySpawn()
{
    if (!m_valid || m_spawning || m_retryAt > Now() || !LocalGridLoaded()) return;
    if (m_state.phase == COOLDOWN && m_state.next > Now()) return;
    if (ObserveBoss()) return;
    SpawnGroup* group = m_owner.instance->GetSpawnManager().GetSpawnGroup(DMA_GROUP);
    if (!group) return;
    if (m_state.phase == EMPTY || m_state.phase == COOLDOWN)
    {
        if (m_state.generation >= 1000000000) { m_valid = false; return; }
        std::map<uint32, uint32> available, minimum;
        for (auto const& candidate : group->GetGroupEntry().RandomEntries)
        {
            available[candidate.Entry] = candidate.MaxCount ? candidate.MaxCount : std::numeric_limits<uint32>::max();
            if (candidate.MinCount) minimum[candidate.Entry] = candidate.MinCount;
        }
        uint32 entry = group->GetEligibleEntry(available, minimum);
        if (!ArenaEntry(entry)) { m_retryAt = Now() + 5; return; }
        m_state.entry = entry; ++m_state.generation; m_state.phase = REQUESTED; m_state.spawnSaid = 0; m_state.next = 0;
        DropNotices(); Commit(); // Durable choice precedes allocation; retries/restart retain it.
    }
    if (m_state.phase != REQUESTED && m_state.phase != ALIVE) return;
    if (m_attempts >= 5) m_attempts = 0;
    ++m_attempts; m_spawning = true;
    Creature* boss = WorldObject::SpawnCreature(DMA_SPAWN_GUID, m_owner.instance, m_state.entry);
    m_spawning = false;
    if (boss && boss->IsAlive() && boss->GetDbGuid() == DMA_SPAWN_GUID && boss->GetEntry() == m_state.entry &&
        boss->GetMap() == m_owner.instance && m_owner.instance->GetCreature(boss->GetObjectGuid()) == boss)
    {
        // LoadFromDB already attaches the real actor to the native group. Do
        // not create a phantom AddObject slot when allocation fails.
        m_actor = boss->GetObjectGuid(); m_attempts = 0; m_retryAt = 0;
        if (m_state.phase != ALIVE) { m_state.phase = ALIVE; Commit(); }
        if (!m_state.spawnSaid) QueueNotice(DMA_NOTICE_SPAWN);
    }
    else
    {
        m_actor.Clear(); m_retryAt = Now() + (m_attempts >= 5 ? 60 : 1);
        if (m_attempts >= 5) script_error_log("Dire Maul arena: native spawn failed five times; retained entry%u, retrying after60s", m_state.entry);
    }
}
void DireMaulArena::OnCreatureCreate(Creature* actor)
{
    if (!m_valid || !actor || actor->GetMap() != m_owner.instance) return;
    if (actor->GetEntry() == DMA_SPECTATOR && actor->HasStaticDBSpawnData() && actor->GetDbGuid() == DMA_SPECTATOR_GUID)
        m_spectator = actor->GetObjectGuid();
    if (actor->GetDbGuid() == DMA_SPAWN_GUID && actor->HasStaticDBSpawnData() && actor->GetEntry() == m_state.entry && ArenaEntry(actor->GetEntry()))
        m_actor = actor->GetObjectGuid(); // Provisional identity only; callback precedes AddToWorld.
}
void DireMaulArena::OnCreatureRespawn(Creature* actor) { OnCreatureCreate(actor); }
void DireMaulArena::OnCreatureDeath(Creature* actor) { BossDied(actor); }
void DireMaulArena::OnCreatureDespawn(Creature* actor)
{
    if (actor && actor->GetObjectGuid() == m_actor) { m_actor.Clear(); m_combat = false; }
}
void DireMaulArena::OnPlayerEnter(Player*) {}
void DireMaulArena::OnPlayerLeave(Player*) {}
void DireMaulArena::Update(uint32)
{
    if (!Ready()) return;
    ObserveBoss(); ReconcileCooldown(); TrySpawn();
}
void DireMaulArena::BossEnterCombat(Creature* boss)
{
    if (!IsCurrentBoss(boss) || !boss->IsAlive() || m_combat) return;
    m_combat = true; QueueNotice(DMA_NOTICE_AGGRO);
}
void DireMaulArena::BossKilledPlayer(Creature* boss, Player* victim)
{
    if (!IsCurrentBoss(boss) || !boss->IsAlive() || !victim || victim->GetMap() != m_owner.instance) return;
    QueueNotice(DMA_NOTICE_KILL);
}
void DireMaulArena::BossEvaded(Creature* boss)
{
    if (!IsCurrentBoss(boss)) return;
    m_combat = false;
    m_notices.erase(std::remove_if(m_notices.begin(), m_notices.end(), [](Notice const& n) { return n.kind != DMA_NOTICE_SPAWN; }), m_notices.end());
}
void DireMaulArena::BossDied(Creature* boss)
{
    if (!IsCurrentBoss(boss) || m_deathHandling) return;
    CreatureData const* data = sObjectMgr.GetCreatureData(DMA_SPAWN_GUID);
    if (!data || !data->spawntimesecsmin || data->spawntimesecsmin > data->spawntimesecsmax || uint64(data->spawntimesecsmax) > dmaMaximumPeriod) return;
    m_deathHandling = true;
    // Real JustDied and instance death hooks run BEFORE SetDeathState. Choose
    // the original DB random delay once and hand it back through the native
    // one-shot override. Both native death processing and this durable record
    // therefore use that same draw; no GetRespawnTime/MAX inference is needed.
    uint32 delay = data->GetRandomRespawnTime();
    boss->SetRespawnDelay(delay, true);
    uint64 until = Now() + delay;
    m_owner.instance->GetPersistentState()->SaveCreatureRespawnTime(DMA_SPAWN_GUID, time_t(until));
    m_state.phase = COOLDOWN; m_state.next = until; m_combat = false; DropNotices(); Commit();
    m_deathHandling = false;
    // Native code continues preparing body loot, publishing the corpse and
    // saving the same deadline. This coordinator never removes its loot/body.
}
void DireMaulArena::DropNotices() { m_notices.clear(); }
void DireMaulArena::QueueNotice(uint32 kind)
{
    if (m_state.phase != ALIVE || !ArenaText(m_state.entry, kind) || (kind == DMA_NOTICE_SPAWN && m_state.spawnSaid)) return;
    for (auto const& existing : m_notices)
        if (existing.kind == kind && existing.generation == m_state.generation) return;
    if (m_notices.size() >= 8) m_notices.pop_back();
    m_notices.push_back({kind, m_state.entry, m_state.generation, Now() + (kind == DMA_NOTICE_SPAWN ? 60 : 15)});
}
void DireMaulArena::SpectatorUpdate(Creature* speaker, uint32)
{
    if (!m_valid || !speaker || !speaker->IsAlive() || speaker->GetEntry() != DMA_SPECTATOR || !speaker->HasStaticDBSpawnData() ||
        speaker->GetDbGuid() != DMA_SPECTATOR_GUID || speaker->GetMap() != m_owner.instance || speaker->GetObjectGuid() != m_spectator ||
        m_owner.instance->GetCreature(speaker->GetObjectGuid()) != speaker || m_noticeAt > Now()) return;
    Creature* boss = GetBoss();
    if (!IsCurrentBoss(boss) || !boss->IsAlive() || !speaker->IsWithinDistInMap(boss, 200.0f)) { DropNotices(); return; }
    while (!m_notices.empty())
    {
        Notice notice = m_notices.front(); m_notices.pop_front();
        if (notice.generation != m_state.generation || notice.entry != m_state.entry || notice.expires < Now() ||
            (notice.kind == DMA_NOTICE_SPAWN && m_state.spawnSaid) || (notice.kind != DMA_NOTICE_SPAWN && !m_combat)) continue;
        uint32 text = ArenaText(notice.entry, notice.kind); if (!text) continue;
        if (notice.kind == DMA_NOTICE_SPAWN) { m_state.spawnSaid = 1; Commit(); }
        m_noticeAt = Now() + 2; DoBroadcastText(text, speaker, nullptr, CHAT_TYPE_YELL); return;
    }
}
