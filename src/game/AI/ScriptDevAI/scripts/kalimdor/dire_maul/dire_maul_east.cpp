/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"
#include "dire_maul_east.h"
#include "dire_maul_east_creatures.h"
#include "dire_maul_east_data.h"
#include "Loot/LootMgr.h"
#include <algorithm>
#include <ctime>
#include <sstream>

namespace
{
template<class T> bool EastNumber(std::istream& in, T& result, uint64 maximum)
{
    std::string token;
    if (!(in >> token) || token.empty()) return false;
    uint64 value = 0;
    for (char digit : token)
    {
        if (digit < '0' || digit > '9' || value > maximum / 10 ||
            (value == maximum / 10 && uint64(digit - '0') > maximum % 10)) return false;
        value = value * 10 + uint64(digit - '0');
    }
    result = T(value);
    return true;
}
void EastDespawnAlive(Map* map, ObjectGuid guid)
{
    if (Creature* actor = map->GetCreature(guid)) if (actor->IsAlive()) actor->ForcedDespawn();
}
bool ValidIron(DireMaulEast::SceneState const& s)
{
    static uint32 const timers[] = {0,0,1000,2000,4000,60000,1000,5000,3000,5000,11000,0};
    if (s.stage > DME_IRON_DONE || s.node > 5 || s.timer > timers[s.stage]) return false;
    if (s.stage == DME_IRON_LOCKED && s.node) return false;
    if (s.stage == DME_IRON_WALKING && s.node >= 5) return false;
    if (s.stage >= DME_IRON_WAIT_EMOTE_1 && s.node != 5) return false;
    return true;
}
bool ValidPusillin(DireMaulEast::SceneState const& s)
{
    static uint32 const timers[] = {0,0,60000,0,60000,0,60000,0,60000,0,3000,1000,1000,0,0,0,1000,1000,1000,1000,1000};
    if (s.stage > 20 || s.node > 38 || s.timer > timers[s.stage]) return false;
    if (!s.stage && s.node) return false;
    static uint32 const stops[] = {1,9,19,32,38};
    if (s.stage >= 1 && s.stage <= 9 && (s.stage & 1) && s.node != stops[s.stage / 2]) return false;
    if ((s.stage >= 10 && s.stage <= 14) || s.stage == 20) if (s.node != 38) return false;
    if (s.stage == 2 || s.stage == 4 || s.stage == 6 || s.stage == 8 || (s.stage >= 16 && s.stage <= 19))
    {
        uint32 leg = s.stage >= 16 ? s.stage - 16 : s.stage / 2 - 1;
        if (s.node < stops[leg] || s.node > stops[leg + 1]) return false;
    }
    return true;
}
bool ValidShardSnapshot(std::string const& text)
{
    // The fixed one-item Felvine chest is well below this bound, including all
    // forty native per-player claims. Native Loot validates the full payload
    // again before using it and never rerolls an invalid/closed snapshot.
    if (text.size() < 5 || text.size() > 8195 || text.compare(0, 3, "IL1") || (text.size() - 3) % 2) return false;
    return std::all_of(text.begin() + 3, text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}

DireMaulEast::DireMaulEast(ScriptedInstance& owner, std::function<void()> callback) : m_owner(owner), m_save(std::move(callback)) {}
uint64 DireMaulEast::Now() { return uint64(time(nullptr)) * 1000; }
void DireMaulEast::Commit() { if (m_save) m_save(); }
uint32 DireMaulEast::GetData(uint32 type) const { return type < 3 ? m_progress.encounters[type] : 0; }
DireMaulEast::SceneState DireMaulEast::GetIronbarkState() const { return m_progress.iron; }
DireMaulEast::SceneState DireMaulEast::GetPusillinState() const { return m_progress.pusillin; }
bool DireMaulEast::IsIronbarkRedeemed() const { return GetData(1) == DONE; }
bool DireMaulEast::IsAlzzinWaveStarted() const { return m_progress.alzzinWave != 0; }
bool DireMaulEast::IsPusillinTransformed() const { return m_progress.pusillinTransformed != 0; }
bool DireMaulEast::IsPusillinDead() const { return m_progress.pusillinDead != 0; }

std::string DireMaulEast::Encode(Progress const& p)
{
    std::ostringstream out;
    out << "DME1 " << p.wall << ' ' << p.alzzinWave << ' ' << p.alzzinGeneration << ' ' << p.redemptionSaid << ' '
        << p.pusillinRequested << ' ' << p.pusillinTransformed << ' ' << p.pusillinDead << ' ' << p.pusillinGeneration << ' '
        << p.isalienGeneration << ' ' << p.shardUntil << ' ' << p.shardMask << ' ' << p.shardOpened;
    for (SceneState const* scene : {&p.iron, &p.pusillin}) out << ' ' << scene->stage << ' ' << scene->node << ' ' << scene->timer;
    for (auto const& actor : p.actors) out << ' ' << actor.state << ' ' << actor.until;
    for (uint32 i = 0; i < 5; ++i) out << ' ' << p.shardChestUntil[i] << ' ' << (p.shardLoot[i].empty() ? "-" : p.shardLoot[i]);
    return out.str();
}
std::string DireMaulEast::Save() const { return Encode(m_progress); }

bool DireMaulEast::Decode(std::array<uint32, 3> const& prefix, std::string const& text, Progress& next)
{
    Progress p;
    p.encounters = prefix;
    for (uint32 state : prefix) if (state > SPECIAL) return false;
    if (text.empty())
    {
        for (uint32& state : p.encounters) if (state == IN_PROGRESS) state = NOT_STARTED;
        p.wall = p.encounters[0] >= DONE;
        if (p.encounters[2] == DONE) p.iron = {DME_IRON_DONE, 5, 0};
        else if (p.encounters[1] == DONE) p.iron.stage = DME_IRON_READY;
        p.redemptionSaid = p.encounters[1] == DONE;
        next = p;
        return true;
    }
    if (text.size() > 60000) return false;
    std::istringstream in(text);
    std::string version;
    if (!(in >> version) || version != "DME1") return false;
    if (!EastNumber(in, p.wall, 1) || !EastNumber(in, p.alzzinWave, 1) || !EastNumber(in, p.alzzinGeneration, 1000000) || !EastNumber(in, p.redemptionSaid, 1) ||
        !EastNumber(in, p.pusillinRequested, 1) || !EastNumber(in, p.pusillinTransformed, 1) || !EastNumber(in, p.pusillinDead, 1) ||
        !EastNumber(in, p.pusillinGeneration, 1000000) || !EastNumber(in, p.isalienGeneration, 1000000) ||
        !EastNumber(in, p.shardUntil, 4102444800000ULL) || !EastNumber(in, p.shardMask, 31) || !EastNumber(in, p.shardOpened, 31)) return false;
    for (SceneState* scene : {&p.iron, &p.pusillin})
        if (!EastNumber(in, scene->stage, 20) || !EastNumber(in, scene->node, 38) || !EastNumber(in, scene->timer, 60000)) return false;
    if (!ValidIron(p.iron) || !ValidPusillin(p.pusillin)) return false;
    if (p.iron.stage && p.encounters[1] != DONE) return false;
    if (p.redemptionSaid && p.encounters[1] != DONE) return false;
    if (p.encounters[1] == DONE && !p.iron.stage) return false;
    if (p.encounters[2] == DONE && p.iron.stage < DME_IRON_WAIT_OPEN) return false;
    if (p.iron.stage >= DME_IRON_WAIT_FAKE && p.encounters[2] != DONE) return false;
    if ((p.encounters[0] == DONE || p.encounters[0] == SPECIAL || p.alzzinWave) && !p.wall) return false;
    if (p.alzzinWave && (!p.alzzinGeneration || (p.encounters[0] != SPECIAL && p.encounters[0] != DONE))) return false;
    if ((p.shardMask && !p.shardUntil) || p.shardUntil > Now() + 60000 || (p.shardUntil && p.encounters[0] != DONE)) return false;
    if (p.shardOpened & ~p.shardMask) return false;
    if (p.pusillinTransformed && (!p.pusillinRequested || (p.pusillin.stage != DME_PU_CONFIRM_FINAL && p.pusillin.stage != DME_PU_RETRY_READY && p.pusillin.stage != DME_PU_COMBAT && p.pusillin.stage != DME_PU_DEAD))) return false;
    if (p.pusillin.stage == DME_PU_COMBAT && (!p.pusillinTransformed || p.pusillinDead)) return false;
    if ((p.pusillin.stage == DME_PU_DEAD) != (p.pusillinDead != 0)) return false;
    if ((p.pusillinRequested != 0) != (p.pusillinGeneration != 0)) return false;
    if ((p.pusillin.stage == DME_PU_WAIT_FORM || p.pusillin.stage == DME_PU_CONFIRM_FINAL || p.pusillin.stage == DME_PU_RETRY_READY) && !p.pusillinRequested) return false;
    for (uint32 i = 0; i < ACTORS; ++i)
    {
        Actor& actor = p.actors[i];
        if (!EastNumber(in, actor.state, EXPIRED) || !EastNumber(in, actor.until, 4102444800000ULL)) return false;
        if (((actor.state == EMPTY || actor.state == PENDING) && actor.until) ||
            ((actor.state == ALIVE || actor.state == KILLED) && !actor.until) || actor.until > Now() + dmeSpawns[i].lifetime) return false;
        if (i < ALZZIN_END)
        {
            if (p.alzzinWave && actor.state == EMPTY) return false;
            if ((!p.alzzinWave || !p.alzzinGeneration) && (actor.state == PENDING || actor.state == ALIVE)) return false;
            if (!p.alzzinGeneration && actor.state != EMPTY) return false;
            if (p.encounters[0] == DONE && actor.state == PENDING) return false;
        }
        else if (i < PUSILLIN_END)
        {
            if (p.pusillinRequested && actor.state == EMPTY) return false;
            if (!p.pusillinRequested && actor.state != EMPTY) return false;
            if (p.pusillinDead && actor.state == PENDING) return false;
        }
        else if ((p.isalienGeneration != 0) != (actor.state != EMPTY)) return false;
    }
    if (p.pusillinTransformed)
        for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i) if (p.actors[i].state < ALIVE) return false;
    for (uint32 i = 0; i < 5; ++i)
    {
        std::string snapshot;
        if (!EastNumber(in, p.shardChestUntil[i], 4102444800000ULL) || p.shardChestUntil[i] > Now() + 301000 || !(in >> snapshot)) return false;
        if (snapshot != "-") { if (!ValidShardSnapshot(snapshot)) return false; p.shardLoot[i] = snapshot; }
        bool opened = (p.shardOpened & (1u << i)) != 0;
        if (opened != !p.shardLoot[i].empty() || (!opened && p.shardChestUntil[i])) return false;
    }
    in >> std::ws;
    if (!in.eof()) return false;
    next = p;
    return true;
}
bool DireMaulEast::ValidateSave(std::array<uint32, 3> const& states, std::string const& text) { Progress p; return Decode(states, text, p); }
bool DireMaulEast::Load(std::array<uint32, 3> const& states, std::string const& text)
{
    Progress next;
    if (!Decode(states, text, next)) return false;
    Progress previous = m_progress;
    for (uint32 i = 0; i < ACTORS; ++i)
    {
        bool same = i < ALZZIN_END ? next.alzzinGeneration == previous.alzzinGeneration :
            i < PUSILLIN_END ? next.pusillinGeneration == previous.pusillinGeneration : next.isalienGeneration == previous.isalienGeneration;
        if (same && next.actors[i].state == ALIVE && previous.actors[i].state == ALIVE) next.actors[i].guid = previous.actors[i].guid;
    }
    m_progress = next;
    m_paused = false; m_retry = 0; m_alzzinFailurePending = false;
    for (uint32 i = 0; i < ACTORS; ++i) if (previous.actors[i].guid != next.actors[i].guid) EastDespawnAlive(m_owner.instance, previous.actors[i].guid);
    for (uint32 i = 0; i < 5; ++i)
        if (previous.shardUntil != next.shardUntil || previous.shardChestUntil[i] != next.shardChestUntil[i] ||
            previous.shardLoot[i] != next.shardLoot[i] || ((previous.shardMask ^ next.shardMask) & (1u << i)))
            if (GameObject* shard = m_owner.instance->GetGameObject(m_shards[i]))
            {
                delete shard->m_loot; shard->m_loot = nullptr;
                shard->RestoreChestDespawnRemainingMS(0);
                shard->SetRespawnTime(0); shard->SetLootState(GO_READY);
            }
    SyncObjects();
    return true;
}

bool DireMaulEast::Current(Creature* actor, ObjectGuid guid, uint32 entry) const
{
    return actor && actor->GetObjectGuid() == guid && actor->GetEntry() == entry && actor->GetMap() == m_owner.instance &&
        m_owner.instance->GetId() == 429 && m_owner.instance->GetCreature(guid) == actor;
}
bool DireMaulEast::CurrentIronbark(Creature* actor) const { return actor && (Current(actor, m_ironbark, 11491) || Current(actor, m_ironbark, 14241)); }
bool DireMaulEast::SetIronbarkState(Creature* actor, SceneState const& scene)
{
    if (!CurrentIronbark(actor) || !actor->IsAlive() || !ValidIron(scene) || (scene.stage && !IsIronbarkRedeemed())) return false;
    if (GetData(2) == DONE && scene.stage < DME_IRON_WAIT_OPEN) return false;
    if (scene.stage >= DME_IRON_WAIT_FAKE && GetData(2) != DONE) return false;
    Progress candidate = m_progress, checked;
    candidate.iron = scene;
    if (!Decode(candidate.encounters, Encode(candidate), checked)) return false;
    bool changed = scene.stage != m_progress.iron.stage || scene.node != m_progress.iron.node;
    m_progress.iron = scene;
    if (changed) Commit();
    return true;
}
bool DireMaulEast::SetPusillinState(Creature* actor, SceneState const& scene)
{
    if (!Current(actor, m_pusillin, 14354) || !actor->IsAlive() || !ValidPusillin(scene) || IsPusillinDead()) return false;
    if (IsPusillinTransformed() && scene.stage != DME_PU_COMBAT && scene.stage != DME_PU_CONFIRM_FINAL && scene.stage != DME_PU_RETRY_READY) return false;
    if (scene.stage == DME_PU_DEAD) return false;
    if (scene.stage == DME_PU_COMBAT && !IsPusillinTransformed()) return false;
    Progress candidate = m_progress, checked;
    candidate.pusillin = scene;
    if (!Decode(candidate.encounters, Encode(candidate), checked)) return false;
    bool changed = scene.stage != m_progress.pusillin.stage || scene.node != m_progress.pusillin.node;
    m_progress.pusillin = scene;
    if (changed) Commit();
    return true;
}
bool DireMaulEast::OpenIronbarkDoor(Creature* actor)
{
    if (!CurrentIronbark(actor) || !actor->IsAlive() || !IsIronbarkRedeemed() ||
        (m_progress.iron.stage != DME_IRON_WAIT_OPEN && GetData(2) != DONE)) return false;
    GameObject* door = m_owner.instance->GetGameObject(m_doors[0]);
    if (!door) return false;
    door->SetGoState(GO_STATE_ACTIVE);
    if (GetData(2) != DONE)
    {
        m_progress.encounters[2] = DONE;
        m_progress.iron = {DME_IRON_WAIT_FAKE, 5, 5000};
        Commit();
    }
    return true;
}
void DireMaulEast::SetData(uint32 type, uint32 data)
{
    if (type >= 3 || data > SPECIAL || GetData(type) == data || GetData(type) == DONE) { SyncObjects(); return; }
    Progress previous = m_progress;
    if (type == 0 && data == DONE)
    {
        m_progress.encounters[0] = DONE; m_progress.wall = 1;
        for (uint32 i = ALZZIN_FIRST; i < ALZZIN_END; ++i) if (m_progress.actors[i].state == PENDING) m_progress.actors[i].state = EXPIRED;
        m_progress.shardUntil = Now() + 60000; m_progress.shardMask = 31;
        m_progress.shardOpened = 0; m_progress.shardChestUntil = {}; m_progress.shardLoot = {};
    }
    else
    {
        m_progress.encounters[type] = data;
        if (type == 0 && data == SPECIAL) m_progress.wall = 1;
        if (type == 1 && data == DONE && !m_progress.iron.stage) m_progress.iron.stage = DME_IRON_READY;
        if (type == 2 && data == DONE) m_progress.iron = {DME_IRON_DONE, 5, 0};
    }
    Progress checked;
    if (!Decode(m_progress.encounters, Encode(m_progress), checked)) { m_progress = previous; return; }
    SyncObjects(); Commit();
}

void DireMaulEast::ClearLiving(uint32 first, uint32 last, bool closeSlots)
{
    std::array<ObjectGuid, ACTORS> detach{};
    for (uint32 i = first; i < last; ++i)
    {
        detach[i] = m_progress.actors[i].guid; m_progress.actors[i].guid.Clear();
        if (closeSlots && (m_progress.actors[i].state == PENDING || m_progress.actors[i].state == ALIVE)) m_progress.actors[i].state = EXPIRED;
    }
    for (ObjectGuid guid : detach) EastDespawnAlive(m_owner.instance, guid);
}
void DireMaulEast::BeginAlzzinWave(Creature* boss)
{
    if (!Current(boss, m_alzzin, 11492) || !boss->IsAlive() || IsAlzzinWaveStarted() || GetData(0) == DONE) return;
    ClearLiving(ALZZIN_FIRST, ALZZIN_END, true);
    ++m_progress.alzzinGeneration; m_progress.alzzinWave = 1; m_progress.wall = 1; m_progress.encounters[0] = SPECIAL;
    m_alzzinFailurePending = false;
    for (uint32 i = ALZZIN_FIRST; i < ALZZIN_END; ++i) { m_progress.actors[i] = Actor(); m_progress.actors[i].state = PENDING; }
    SyncObjects(); Commit(); m_retry = 0; ProcessActors();
}
void DireMaulEast::AlzzinEvaded(Creature* boss)
{
    if (!Current(boss, m_alzzin, 11492) || GetData(0) == DONE || !IsAlzzinWaveStarted() || m_paused || !m_owner.GetPlayerInMap(false, false)) return;
    ResetAlzzinWave();
}
void DireMaulEast::ResetAlzzinWave()
{
    if (GetData(0) == DONE || !IsAlzzinWaveStarted()) return;
    ClearLiving(ALZZIN_FIRST, ALZZIN_END, true);
    m_progress.alzzinWave = 0;
    m_alzzinFailurePending = false;
    m_progress.encounters[0] = m_progress.wall ? uint32(SPECIAL) : uint32(NOT_STARTED);
    Commit();
}
void DireMaulEast::AlzzinDied(Creature* boss) { if (Current(boss, m_alzzin, 11492)) SetData(0, DONE); }
void DireMaulEast::QueuePusillinAdds(Creature* pusillin)
{
    if (!Current(pusillin, m_pusillin, 14354) || !pusillin->IsAlive() || IsPusillinDead() || m_progress.pusillinRequested) return;
    m_progress.pusillinRequested = 1; ++m_progress.pusillinGeneration;
    for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i) m_progress.actors[i].state = PENDING;
    Commit(); m_retry = 0; ProcessActors();
}
bool DireMaulEast::ArePusillinAddsReady() const
{
    if (!m_progress.pusillinRequested) return false;
    for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i)
    {
        Actor const& slot = m_progress.actors[i];
        if (slot.state < ALIVE) return false;
        if (slot.state == ALIVE && slot.until > Now() && !m_owner.instance->GetCreature(slot.guid)) return false;
    }
    return true;
}
bool DireMaulEast::PusillinAddsFailed() const
{
    for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i)
        if ((m_progress.actors[i].state == PENDING || (m_progress.actors[i].state == ALIVE && !m_progress.actors[i].guid)) && m_progress.actors[i].attempts >= 5) return true;
    return false;
}
void DireMaulEast::RetryPusillinAdds(Creature* pusillin)
{
    if (!Current(pusillin, m_pusillin, 14354) || IsPusillinDead()) return;
    for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i)
        if (m_progress.actors[i].state == PENDING || (m_progress.actors[i].state == ALIVE && !m_progress.actors[i].guid)) { m_progress.actors[i].attempts = 0; m_progress.actors[i].retry = 0; }
    m_retry = 0; ProcessActors();
}
void DireMaulEast::PusillinTransformed(Creature* pusillin)
{
    if (!Current(pusillin, m_pusillin, 14354) || !pusillin->IsAlive() || !ArePusillinAddsReady() || IsPusillinDead()) return;
    if (IsPusillinTransformed() && m_progress.pusillin.stage == DME_PU_COMBAT) return;
    m_progress.pusillinTransformed = 1; m_progress.pusillin = {DME_PU_COMBAT, 38, 0}; Commit();
}
void DireMaulEast::PusillinDied(Creature* pusillin)
{
    if (!Current(pusillin, m_pusillin, 14354) || IsPusillinDead()) return;
    m_progress.pusillinDead = 1; m_progress.pusillin.stage = DME_PU_DEAD; m_progress.pusillin.timer = 0;
    for (uint32 i = PUSILLIN_FIRST; i < PUSILLIN_END; ++i) if (m_progress.actors[i].state == PENDING) m_progress.actors[i].state = EXPIRED;
    Commit();
}
bool DireMaulEast::RequestIsalien(Creature* trigger)
{
    if (!Current(trigger, m_isalienTrigger, 16045) || !trigger->HasStaticDBSpawnData()) return false;
    Actor& slot = m_progress.actors[ISALIEN_SLOT];
    if (Creature* current = m_owner.instance->GetCreature(slot.guid))
        if (current->IsAlive() && (current->IsInCombat() || current->GetCharmer())) { slot.until = Now() + dmeSpawns[ISALIEN_SLOT].lifetime; return false; }
    if (slot.state == PENDING || (slot.state == ALIVE && slot.until > Now()))
    {
        if (!slot.guid && slot.attempts >= 5) { slot.attempts = 0; slot.retry = 0; Spawn(ISALIEN_SLOT); }
        return false;
    }
    ObjectGuid old = slot.guid;
    slot = Actor(); slot.state = PENDING; ++m_progress.isalienGeneration;
    EastDespawnAlive(m_owner.instance, old); Commit(); Spawn(ISALIEN_SLOT);
    return true;
}

bool DireMaulEast::Spawn(uint32 index)
{
    Actor& slot = m_progress.actors[index];
    if (slot.guid || (slot.state != PENDING && slot.state != ALIVE) || slot.attempts >= 5 || slot.retry > Now()) return false;
    if (slot.state == ALIVE && slot.until <= Now()) { slot.state = EXPIRED; Commit(); return false; }
    if (!m_owner.GetPlayerInMap(true, false)) return false;
    ObjectGuid ownerGuid = index < ALZZIN_END ? m_alzzin : index < PUSILLIN_END ? m_pusillin : m_isalienTrigger;
    WorldObject* owner = m_owner.instance->GetCreature(ownerGuid);
    // A successful boss death does not remove its surviving adds. Only that
    // completed scene may restore them when the static owner's corpse is gone.
    if (!owner && ((index < ALZZIN_END && GetData(0) == DONE) || (index >= PUSILLIN_FIRST && index < PUSILLIN_END && IsPusillinDead()))) owner = m_owner.GetPlayerInMap(true, false);
    if (!owner) return false;
    auto const& request = dmeSpawns[index]; auto const& p = request.position;
    ++slot.attempts; slot.retry = Now() + 1000;
    // The coordinator owns the original OOC deadline; native DEAD_DESPAWN
    // retains the same corpse lifecycle and never turns a retry into a new TTL.
    if (Creature* actor = owner->SummonCreature(request.entry, p[0], p[1], p[2], p[3], TEMPSPAWN_DEAD_DESPAWN, 0, true))
    {
        if (slot.state == PENDING) slot.until = Now() + request.lifetime;
        slot.guid = actor->GetObjectGuid(); slot.state = ALIVE; slot.attempts = 0;
        if (index < ALZZIN_END)
            if (Creature* boss = m_owner.instance->GetCreature(m_alzzin)) if (Unit* target = boss->GetVictim()) actor->AI()->AttackStart(target);
        Commit(); return true;
    }
    if (slot.attempts >= 5)
    {
        script_error_log("Dire Maul East: summon entry%u slot%u exhausted bounded retries", request.entry, index);
        if (index < ALZZIN_END && GetData(0) != DONE) m_alzzinFailurePending = true;
    }
    return false;
}
void DireMaulEast::ProcessActors()
{
    bool changed = false;
    for (uint32 i = 0; i < ACTORS; ++i)
    {
        Actor& slot = m_progress.actors[i];
        if (slot.state != ALIVE && slot.state != PENDING) continue;
        if (slot.guid)
        {
            Creature* actor = m_owner.instance->GetCreature(slot.guid);
            if (actor && actor->IsAlive() && (actor->IsInCombat() || actor->GetCharmer())) slot.until = Now() + dmeSpawns[i].lifetime;
            else if (!actor || !actor->IsAlive() || slot.until <= Now())
            {
                ObjectGuid guid = slot.guid; slot.guid.Clear();
                slot.state = actor && !actor->IsAlive() ? KILLED : EXPIRED;
                EastDespawnAlive(m_owner.instance, guid); changed = true;
            }
        }
        else if (slot.state == ALIVE && slot.until <= Now()) { slot.state = EXPIRED; changed = true; }
        else if (!m_retry) Spawn(i);
    }
    if (changed) Commit();
}

void DireMaulEast::SyncObjects()
{
    uint64 now = Now();
    for (uint32 i = 0; i < 3; ++i)
        if (GameObject* door = m_owner.instance->GetGameObject(m_doors[i]))
            door->SetGoState((i == 0 ? GetData(2) == DONE : i == 1 ? m_progress.wall != 0 : GetData(0) == DONE) ? GO_STATE_ACTIVE : GO_STATE_READY);
    m_syncingShards = true;
    bool changed = false;
    for (uint32 i = 0; i < 5; ++i)
        if (GameObject* shard = m_owner.instance->GetGameObject(m_shards[i]))
        {
            bool available = ShardAvailable(i);
            if (available && !shard->IsSpawned())
            {
                shard->SetLootState(GO_READY);
                uint64 until = m_progress.shardOpened & (1u << i) ? m_progress.shardChestUntil[i] : m_progress.shardUntil;
                m_owner.DoRespawnGameObject(shard->GetObjectGuid(), until > now ? uint32((until - now + 999) / 1000) : 1);
            }
            if (available && (m_progress.shardOpened & (1u << i)) && !shard->m_loot && shard->IsSpawned())
            {
                // Rehydrate the already rolled loot directly, without a player,
                // a new roll, or a fresh pickup/expiration window.
                shard->m_loot = new Loot(shard, m_progress.shardLoot[i]);
                if (shard->GetLootState() == GO_ACTIVATED)
                {
                    uint64 until = m_progress.shardChestUntil[i];
                    shard->RestoreChestDespawnRemainingMS(until > now ? uint32(until - now) : until ? 1 : 0);
                }
                else { CloseShard(i); changed = true; available = false; }
            }
            else if (!available && shard->IsSpawned() && (m_progress.shardUntil || shard->m_loot)) shard->ForcedDespawn();
        }
    m_syncingShards = false;
    if (changed) Commit();
}
void DireMaulEast::SyncIronbark()
{
    Creature* tree = m_owner.instance->GetCreature(m_ironbark);
    if (!tree || !tree->IsAlive()) return;
    if (m_progress.iron.stage == DME_IRON_DONE) { tree->ForcedDespawn(); return; }
    if (IsIronbarkRedeemed())
    {
        if (!m_progress.redemptionSaid) { DoScriptText(-1429002, tree); m_progress.redemptionSaid = 1; Commit(); }
        if (tree->GetEntry() == 11491) tree->UpdateEntry(14241);
    }
    if (m_progress.iron.stage != DME_IRON_READY) tree->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
}
void DireMaulEast::FelvineLootStateChanged(GameObject* shard)
{
    if (m_syncingShards || !shard || shard->GetMap() != m_owner.instance || shard->GetEntry() != 179559 ||
        m_owner.instance->GetGameObject(shard->GetObjectGuid()) != shard || shard->GetLootState() != GO_JUST_DEACTIVATED || !m_progress.shardUntil) return;
    for (uint32 i = 0; i < 5; ++i)
        if (shard->GetDbGuid() == dmeShardGuids[i] && (m_progress.shardMask & (1u << i)))
        { CloseShard(i); Commit(); return; }
}
int32 DireMaulEast::ShardIndex(ObjectGuid const& guid, uint32 entry) const
{
    if (entry != 179559) return -1;
    GameObject* shard = m_owner.instance->GetGameObject(guid);
    if (!shard || shard->GetEntry() != entry || shard->GetMap() != m_owner.instance) return -1;
    for (uint32 i = 0; i < 5; ++i) if (shard->GetDbGuid() == dmeShardGuids[i]) return int32(i);
    return -1;
}
bool DireMaulEast::ShardAvailable(uint32 i) const
{
    if (!(m_progress.shardMask & (1u << i))) return false;
    if (m_progress.shardOpened & (1u << i))
    {
        if (GameObject* shard = m_owner.instance->GetGameObject(m_shards[i]))
            if (shard->GetLootState() == GO_ACTIVATED) return true; // Native ACTIVE update owns its deadline.
        return !m_progress.shardChestUntil[i] || m_progress.shardChestUntil[i] > Now();
    }
    return m_progress.shardUntil > Now();
}
void DireMaulEast::CloseShard(uint32 i)
{
    m_progress.shardMask &= ~(1u << i); m_progress.shardOpened &= ~(1u << i);
    m_progress.shardChestUntil[i] = 0; m_progress.shardLoot[i].clear();
}
bool DireMaulEast::GetFelvineLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string& snapshot) const
{
    int32 i = ShardIndex(guid, entry);
    if (i < 0) return false;
    snapshot = ShardAvailable(uint32(i)) ? m_progress.shardLoot[i] : "closed";
    return true;
}
void DireMaulEast::OnFelvineLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string const& snapshot, bool exhausted)
{
    if (m_syncingShards) return;
    int32 found = ShardIndex(guid, entry);
    if (found < 0) return;
    uint32 i = uint32(found);
    if (!(m_progress.shardMask & (1u << i))) return;
    GameObject* shard = m_owner.instance->GetGameObject(guid);
    if (exhausted || shard->GetLootState() == GO_JUST_DEACTIVATED || !ValidShardSnapshot(snapshot))
    { CloseShard(i); Commit(); return; }
    uint32 remaining = shard->GetChestDespawnRemainingMS();
    uint64 until = remaining ? Now() + remaining : 0;
    bool changed = !(m_progress.shardOpened & (1u << i)) || m_progress.shardChestUntil[i] != until || m_progress.shardLoot[i] != snapshot;
    m_progress.shardOpened |= 1u << i; m_progress.shardChestUntil[i] = until; m_progress.shardLoot[i] = snapshot;
    if (changed) Commit();
}
bool DireMaulEast::ObserveShards()
{
    bool changed = false;
    for (uint32 i = 0; i < 5; ++i)
    {
        if (!(m_progress.shardMask & (1u << i))) continue;
        GameObject* shard = m_owner.instance->GetGameObject(m_shards[i]);
        if (shard && shard->GetLootState() == GO_JUST_DEACTIVATED)
        { CloseShard(i); changed = true; continue; }
        if (!ShardAvailable(i)) { CloseShard(i); changed = true; }
    }
    return changed;
}
void DireMaulEast::OnObjectCreate(GameObject* object)
{
    uint32 entry = object->GetEntry();
    if (entry == 176907) m_doors[0] = object->GetObjectGuid();
    if (entry == 177220) m_doors[1] = object->GetObjectGuid();
    if (entry == 179502) m_doors[2] = object->GetObjectGuid();
    for (uint32 i = 0; i < 5; ++i) if (object->GetDbGuid() == dmeShardGuids[i]) m_shards[i] = object->GetObjectGuid();
    SyncObjects();
}
void DireMaulEast::OnCreatureCreate(Creature* actor)
{
    if (!actor->HasStaticDBSpawnData()) return;
    if (actor->GetEntry() == 11492) m_alzzin = actor->GetObjectGuid();
    if (actor->GetEntry() == 11491 || actor->GetEntry() == 14241) m_ironbark = actor->GetObjectGuid();
    if (actor->GetEntry() == 14354) m_pusillin = actor->GetObjectGuid();
    if (actor->GetEntry() == 16045) m_isalienTrigger = actor->GetObjectGuid();
}
void DireMaulEast::OnCreatureRespawn(Creature* actor) { OnCreatureCreate(actor); }
void DireMaulEast::OnCreatureDeath(Creature* actor)
{
    if (actor->GetEntry() == 11490 && actor->HasStaticDBSpawnData()) SetData(1, DONE);
    if (Current(actor, m_alzzin, 11492)) AlzzinDied(actor);
    if (Current(actor, m_pusillin, 14354)) PusillinDied(actor);
    for (Actor& slot : m_progress.actors) if (slot.guid == actor->GetObjectGuid()) { slot.state = KILLED; slot.guid.Clear(); Commit(); return; }
}
void DireMaulEast::OnCreatureEvade(Creature* actor) { if (Current(actor, m_alzzin, 11492)) AlzzinEvaded(actor); }
void DireMaulEast::OnCreatureDespawn(Creature* actor)
{
    for (Actor& slot : m_progress.actors)
        if (slot.guid == actor->GetObjectGuid()) { slot.guid.Clear(); slot.state = EXPIRED; Commit(); return; }
}
void DireMaulEast::Pause()
{
    if (m_paused) return;
    m_paused = true; ClearLiving(0, ACTORS, false); Commit();
}
void DireMaulEast::OnPlayerEnter(Player* /*player*/)
{
    m_paused = false; m_retry = 0;
    for (Actor& actor : m_progress.actors) { actor.attempts = 0; actor.retry = 0; }
    ProcessActors(); SyncObjects();
}
void DireMaulEast::OnPlayerLeave(Player* player)
{
    bool remaining = false, living = false;
    for (auto const& ref : m_owner.instance->GetPlayers()) if (Player* other = ref.getSource()) if (other != player && !other->IsGameMaster()) { remaining = true; living |= other->IsAlive(); }
    if (!living && player && !player->IsGameMaster() && !player->IsAlive()) ResetAlzzinWave();
    if (remaining) Commit(); else Pause();
}
void DireMaulEast::Update(uint32 diff)
{
    if (!m_owner.GetPlayerInMap(false, false)) { Pause(); return; }
    m_paused = false;
    if (m_retry > diff) m_retry -= diff; else m_retry = 0;
    ProcessActors();
    if (m_alzzinFailurePending && GetData(0) != DONE && m_owner.GetPlayerInMap(true, false))
        if (Creature* boss = m_owner.instance->GetCreature(m_alzzin)) if (boss->IsAlive()) { m_alzzinFailurePending = false; boss->AI()->EnterEvadeMode(); }
    if (!m_retry) m_retry = 1000;
    SyncIronbark();
    if (IsPusillinDead()) if (Creature* actor = m_owner.instance->GetCreature(m_pusillin)) if (actor->IsAlive()) actor->ForcedDespawn();
    if (ObserveShards()) Commit();
    SyncObjects();
    if (m_checkpoint > diff) m_checkpoint -= diff;
    else
    {
        bool active = m_progress.iron.timer || m_progress.pusillin.timer || m_progress.shardMask;
        for (auto const& slot : m_progress.actors) active |= slot.state == PENDING || slot.state == ALIVE;
        if (active) Commit();
        m_checkpoint = 5000;
    }
}

struct go_dme_felvine_shardAI : public GameObjectAI
{
    explicit go_dme_felvine_shardAI(GameObject* object) : GameObjectAI(object) {}
    void OnLootStateChange(Unit* /*user*/) override
    {
        if (auto* data = static_cast<instance_dire_maul*>(m_go->GetInstanceData())) data->East().FelvineLootStateChanged(m_go);
    }
};
void AddSC_dire_maul_east_objects()
{
    Script* script = new Script;
    script->Name = "go_dme_felvine_shard";
    script->GetGameObjectAI = &GetNewAIInstance<go_dme_felvine_shardAI>;
    script->RegisterSelf();
}
