/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_spire_upper.h"
#include "blackrock_spire_upper_data.h"
#include "Maps/MapPersistentStateMgr.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
template<class T> bool UpperUnsigned(std::istream& in, T& value)
{
    std::string token;
    if (!(in >> token) || token.empty()) return false;
    uint64 number = 0, maximum = std::numeric_limits<T>::max();
    for (char c : token)
    {
        if (c < '0' || c > '9' || number > (maximum - uint32(c - '0')) / 10) return false;
        number = number * 10 + uint32(c - '0');
    }
    value = T(number); return true;
}
template<class... T> bool UpperNumbers(std::istream& in, T&... values) { return (UpperUnsigned(in, values) && ...); }
void DespawnUpperAlive(Map* map, ObjectGuid guid)
{
    if (Creature* actor = map->GetAnyTypeCreature(guid)) if (actor->IsAlive()) actor->ForcedDespawn();
}
uint32 UpperLifetime(uint32 kind, uint32 key = 0) { return kind == 10 ? (key & 1 ? 300000 : 60000) : kind == 7 ? 300000 : kind == 8 ? 3600000 : kind == 9 ? 7200000 : 0; }
bool UpperDoor(Map* map, std::map<uint32, ObjectGuid> const& objects, uint32 entry, bool active)
{
    auto it = objects.find(entry);
    if (it == objects.end()) return false;
    if (GameObject* object = map->GetGameObject(it->second))
    {
        object->SetLootState(GO_READY);
        object->SetGoState(active ? GO_STATE_ACTIVE : GO_STATE_READY);
        return true;
    }
    return false;
}
}

BlackrockSpireUpper::BlackrockSpireUpper(ScriptedInstance& owner, std::function<void()> callback) : m_owner(owner), m_save(std::move(callback)) {}
uint64 BlackrockSpireUpper::Now() { return uint64(time(nullptr)) * 1000; }
bool BlackrockSpireUpper::IsStadium(uint32 kind) { return kind >= INTRO_NEF && kind <= COMBAT_REND; }
bool BlackrockSpireUpper::IsRookery(uint32 kind) { return kind == ROOKERY || kind == SOLAKAR; }
void BlackrockSpireUpper::Commit() { if (m_save) m_save(); }
uint32 BlackrockSpireUpper::GetData(uint32 type) const { return type < 6 ? m_progress.encounter[type] : 0; }

std::string BlackrockSpireUpper::Encode(Progress const& p)
{
    std::ostringstream out;
    out << "UPR1 " << p.opened << ' ' << p.unlockRequested << ' ' << p.unlockStep << ' ' << p.unlockTimer << ' ' << p.runes << ' '
        << p.emberStage << ' ' << p.emberGeneration << ' ' << p.emberTimer << ' ' << p.emberTicks << ' ' << p.emberReleaseStep << ' '
        << p.stadiumGeneration << ' ' << p.stadiumPhase << ' ' << p.stadiumTimer << ' ' << p.stadiumWave << ' ' << p.stadiumGateUntil << ' ' << p.stadiumReset << ' '
        << p.rookeryGeneration << ' ' << p.rookeryWave << ' ' << p.rookeryTimer << ' ' << p.valthGeneration << ' ' << p.actors.size();
    out << std::setprecision(9);
    for (auto const& actor : p.actors)
    {
        out << ' ' << actor.kind << ' ' << actor.key << ' ' << actor.entry << ' ' << actor.state << ' ' << actor.until;
        for (float value : actor.position) out << ' ' << value;
        for (float value : actor.destination) out << ' ' << value;
    }
    return out.str();
}
std::string BlackrockSpireUpper::Save() const { return Encode(m_progress); }

bool BlackrockSpireUpper::Decode(std::array<uint32, 6> const& prefix, std::string const& text, Progress& output)
{
    Progress p;
    p.encounter = prefix;
    for (uint32 value : prefix) if (value > SPECIAL) return false;
    if (text.empty())
    {
        for (uint32& value : p.encounter) if (value == IN_PROGRESS) value = NOT_STARTED;
        // The legacy SPECIAL flag had no durable Solakar request or actor.
        if (p.encounter[2] == SPECIAL) p.encounter[2] = NOT_STARTED;
        if (p.encounter[0] == DONE) p.runes = 127;
        if (p.encounter[1] == DONE) p.emberStage = 3;
        output = p;
        return true;
    }
    if (text.size() > 32768) return false;
    std::istringstream in(text);
    std::string version;
    uint32 count;
    if (!(in >> version) || !UpperNumbers(in, p.opened, p.unlockRequested, p.unlockStep, p.unlockTimer, p.runes, p.emberStage, p.emberGeneration, p.emberTimer, p.emberTicks, p.emberReleaseStep,
        p.stadiumGeneration, p.stadiumPhase, p.stadiumTimer, p.stadiumWave, p.stadiumGateUntil, p.stadiumReset,
        p.rookeryGeneration, p.rookeryWave, p.rookeryTimer, p.valthGeneration, count) || version != "UPR1" ||
        p.opened > 1 || p.unlockRequested > 1 || p.unlockStep > 4 || p.unlockTimer > 1000 || p.runes > 127 || p.emberStage > 3 || p.stadiumReset > 1 ||
        p.emberGeneration > 1000000 || p.emberTimer > 120000 || p.emberTicks > 20 || p.emberReleaseStep > 3 || p.stadiumGeneration > 1000000 || p.stadiumPhase > 10 ||
        p.stadiumTimer > 60000 || p.stadiumWave > 7 || p.stadiumGateUntil > Now() + 10000 ||
        p.rookeryGeneration > 1000000 || p.rookeryWave > 7 || p.rookeryTimer > 40000 || p.valthGeneration > 1000000 || count > 128) return false;
    if ((p.opened && p.unlockStep != 4) || (!p.unlockRequested && (p.opened || p.unlockStep || p.unlockTimer))) return false;
    if (p.encounter[0] == DONE && p.runes != 127) return false;
    if (p.encounter[1] == DONE && p.emberStage != 3) return false;
    if ((p.emberStage == 0 && (p.emberTicks || p.emberTimer || p.emberReleaseStep)) || (p.emberTicks < 20 && p.emberReleaseStep)) return false;
    if (p.emberStage == 1 && (!p.emberGeneration || !p.emberTimer)) return false;
    if (p.emberStage == 2 && (!p.emberGeneration || p.emberTicks != 20 || p.emberReleaseStep != 3 || p.emberTimer)) return false;
    if (p.emberStage == 3 && p.encounter[1] != DONE) return false;
    if (p.encounter[1] == IN_PROGRESS && p.emberStage != 1 && p.emberStage != 2) return false;
    if ((p.emberStage == 1 || p.emberStage == 2) && p.encounter[1] != IN_PROGRESS) return false;
    if (p.encounter[3] == IN_PROGRESS && (!p.stadiumGeneration || !p.stadiumPhase || p.stadiumPhase == 10)) return false;
    if (p.encounter[3] != IN_PROGRESS && p.stadiumPhase && !(p.encounter[3] == DONE && p.stadiumPhase == 10)) return false;
    if ((p.stadiumPhase && p.stadiumPhase < 4 && p.stadiumWave) || (p.stadiumPhase >= 5 && p.stadiumWave != 7)) return false;
    if (p.encounter[2] == IN_PROGRESS && !p.rookeryGeneration) return false;
    if ((p.encounter[2] == IN_PROGRESS && p.rookeryWave < 7 && !p.rookeryTimer) ||
        (p.encounter[2] == SPECIAL && (p.rookeryWave != 7 || !p.rookeryGeneration || p.rookeryTimer))) return false;
    std::set<std::pair<uint32, uint32>> keys;
    for (uint32 i = 0; i < count; ++i)
    {
        Actor actor;
        if (!UpperNumbers(in, actor.kind, actor.key, actor.entry, actor.state, actor.until)) return false;
        for (float& value : actor.position) if (!(in >> value) || !std::isfinite(value) || std::abs(value) > 10000) return false;
        for (float& value : actor.destination) if (!(in >> value) || !std::isfinite(value) || std::abs(value) > 10000) return false;
        if (actor.kind < INTRO_NEF || actor.kind > VALTH_MINION || actor.state > EXPIRED || !keys.insert({actor.kind, actor.key}).second) return false;
        uint32 expected = actor.kind == INTRO_NEF ? 10162 : actor.kind == INTRO_REND || actor.kind == COMBAT_REND ? 10429 :
            actor.kind == GYTH ? 10339 : actor.kind == SOLAKAR ? 10264 : actor.kind == VALTHALAK ? 16042 : actor.kind == VALTH_MINION ? 16066 : 0;
        if (expected && actor.entry != expected) return false;
        if (actor.kind == SPECTATOR && (actor.key >= 12 || actor.entry != upper_aStadiumSpectators[actor.key])) return false;
        if (actor.kind == STADIUM_WAVE && (actor.key >= 35 || actor.key / 5 >= p.stadiumWave || !upper_aStadiumEventNpcs[actor.key / 5][actor.key % 5] || actor.entry != upper_aStadiumEventNpcs[actor.key / 5][actor.key % 5])) return false;
        if (actor.kind == ROOKERY && (actor.key >= 12 || actor.key / 2 >= p.rookeryWave || (actor.entry != 10258 && actor.entry != 10683) || (actor.key < 2 && actor.entry != 10683))) return false;
        if (expected && actor.kind != VALTH_MINION && actor.key) return false;
        uint32 lifetime = UpperLifetime(actor.kind, actor.key);
        if ((actor.state == PENDING && actor.until) || (!lifetime && actor.until) ||
            (lifetime && actor.state == ALIVE && !actor.until) || actor.until > (lifetime ? Now() + lifetime : 0)) return false;
        if (IsStadium(actor.kind) && (actor.state == PENDING || actor.state == ALIVE) && p.encounter[3] != IN_PROGRESS) return false;
        if (IsRookery(actor.kind) && (actor.state == PENDING || actor.state == ALIVE) && p.encounter[2] != IN_PROGRESS && p.encounter[2] != SPECIAL && !(actor.kind == ROOKERY && p.encounter[2] == DONE)) return false;
        if ((actor.kind == VALTHALAK || actor.kind == VALTH_MINION) && (actor.state == PENDING || actor.state == ALIVE) && !p.valthGeneration) return false;
        if (actor.kind == VALTHALAK && ((actor.state == PENDING || actor.state == ALIVE) ? p.encounter[5] != IN_PROGRESS : actor.state == KILLED && p.encounter[5] != DONE)) return false;
        if (actor.kind == VALTH_MINION && actor.state != ALIVE) return false;
        p.actors.push_back(actor);
    }
    auto has = [&p](uint32 kind, uint32 key) { return std::any_of(p.actors.begin(), p.actors.end(), [kind, key](Actor const& a) { return a.kind == kind && a.key == key; }); };
    if (p.encounter[3] == IN_PROGRESS)
    {
        if (!has(INTRO_NEF, 0) || !has(INTRO_REND, 0)) return false;
        if (p.stadiumPhase >= 4) for (uint32 i = 0; i < 12; ++i) if (!has(SPECTATOR, i)) return false;
        for (uint32 wave = 0; wave < p.stadiumWave; ++wave) for (uint32 slot = 0; slot < 5; ++slot)
            if (upper_aStadiumEventNpcs[wave][slot] && !has(STADIUM_WAVE, wave * 5 + slot)) return false;
        if (p.stadiumPhase >= 8 && (!has(GYTH, 0) || !has(COMBAT_REND, 0))) return false;
        for (auto const& actor : p.actors) if ((actor.kind == GYTH || actor.kind == COMBAT_REND) && actor.state == EXPIRED) return false;
    }
    if (p.encounter[2] == IN_PROGRESS || p.encounter[2] == SPECIAL)
    {
        for (uint32 wave = 0; wave < std::min(p.rookeryWave, 6u); ++wave) for (uint32 slot = 0; slot < 2; ++slot) if (!has(ROOKERY, wave * 2 + slot)) return false;
        if (p.rookeryWave == 7 && !has(SOLAKAR, 0)) return false;
        for (auto const& actor : p.actors) if (actor.kind == SOLAKAR && (actor.state == KILLED || actor.state == EXPIRED)) return false;
    }
    if (p.valthGeneration && !has(VALTHALAK, 0)) return false;
    in >> std::ws;
    if (!in.eof()) return false;
    output = p;
    return true;
}

bool BlackrockSpireUpper::ValidateSave(std::array<uint32, 6> const& prefix, std::string const& text) { Progress p; return Decode(prefix, text, p); }
bool BlackrockSpireUpper::Load(std::array<uint32, 6> const& prefix, std::string const& text)
{
    Progress next;
    if (!Decode(prefix, text, next)) return false;
    auto old = m_progress;
    for (auto& actor : next.actors)
        for (auto const& previous : old.actors)
            if (actor.kind == previous.kind && actor.key == previous.key && actor.state == ALIVE && previous.state == ALIVE &&
                (IsStadium(actor.kind) ? next.stadiumGeneration == old.stadiumGeneration : IsRookery(actor.kind) ? next.rookeryGeneration == old.rookeryGeneration : next.valthGeneration == old.valthGeneration))
                actor.guid = previous.guid;
    m_progress = next;
    for (auto const& previous : old.actors)
    {
        bool retained = false;
        for (auto const& actor : next.actors) if (actor.guid && actor.guid == previous.guid) retained = true;
        if (!retained) DespawnUpperAlive(m_owner.instance, previous.guid);
    }
    m_emberStartPending = m_progress.emberStage == 1;
    m_emberRestorePending = m_progress.emberStage == 2;
    m_emberResetPending = ShouldEmberseerBeCaged();
    if (m_emberStartPending) m_progress.emberTimer = std::max(m_progress.emberTimer, (20 - m_progress.emberTicks) * 3000 + 10000);
    if (m_progress.unlockRequested && !m_progress.opened && !m_progress.unlockTimer) { m_unlockAttempts = 0; m_progress.unlockTimer = 1000; }
    m_minions.clear();
    for (auto const& actor : m_progress.actors) if (actor.kind == VALTH_MINION && actor.guid) m_minions.insert(actor.guid);
    m_paused = false;
    SyncObjects();
    return true;
}

BlackrockSpireUpper::Actor* BlackrockSpireUpper::Find(uint32 kind, uint32 key) { for (auto& actor : m_progress.actors) if (actor.kind == kind && actor.key == key) return &actor; return nullptr; }
BlackrockSpireUpper::Actor const* BlackrockSpireUpper::Find(uint32 kind, uint32 key) const { for (auto const& actor : m_progress.actors) if (actor.kind == kind && actor.key == key) return &actor; return nullptr; }
void BlackrockSpireUpper::Queue(uint32 kind, uint32 key, uint32 entry, std::array<float, 4> position)
{
    if (Find(kind, key)) return;
    Actor actor;
    actor.kind = kind; actor.key = key; actor.entry = entry; actor.position = position;
    if (kind == STADIUM_WAVE)
    {
        actor.destination = {{upper_aStadiumLocs[2][0], upper_aStadiumLocs[2][1], upper_aStadiumLocs[2][2]}};
        if (Player* player = m_owner.GetPlayerInMap(true, false)) player->GetRandomPoint(actor.destination[0], actor.destination[1], actor.destination[2], 5, actor.destination[0], actor.destination[1], actor.destination[2]);
        actor.destination[0] = std::min(actor.destination[0], upper_aStadiumLocs[2][0]);
    }
    m_progress.actors.push_back(actor);
}

bool BlackrockSpireUpper::Spawn(Actor& slot)
{
    if (slot.guid || (slot.state != PENDING && slot.state != ALIVE) || (slot.kind == COMBAT_REND && slot.state == PENDING) || slot.attempts >= 5 || slot.retry > Now()) return false;
    if (slot.state == ALIVE && slot.until && slot.until <= Now())
    {
        slot.state = EXPIRED;
        if (slot.kind == SOLAKAR) FailRookery(); else if (slot.kind == VALTHALAK) FailValthalak();
        return false;
    }
    WorldObject* owner = m_owner.GetPlayerInMap(true, false);
    if (slot.kind == SPECTATOR || slot.kind == STADIUM_WAVE || slot.kind == GYTH)
        if (Actor* nef = Find(INTRO_NEF)) if (Creature* actor = m_owner.instance->GetCreature(nef->guid)) owner = actor;
    if (slot.kind == ROOKERY || slot.kind == SOLAKAR)
        if (GameObject* object = m_owner.instance->GetGameObject(m_objects[175245])) owner = object;
    if (slot.kind == VALTHALAK) if (Creature* trigger = m_owner.instance->GetCreature(m_valthTrigger)) owner = trigger;
    if (slot.kind == VALTH_MINION)
    {
        owner = nullptr;
        if (Actor* boss = Find(VALTHALAK)) if (Creature* actor = m_owner.instance->GetCreature(boss->guid)) owner = actor;
        if (!owner) owner = m_owner.instance->GetCreature(m_valthTrigger);
    }
    if (!owner && slot.kind != VALTH_MINION) return false;
    ++slot.attempts; slot.retry = Now() + 1000;
    auto const& p = slot.position;
    m_spawningKind = slot.kind;
    Creature* actor;
    if (slot.kind == VALTH_MINION)
        actor = WorldObject::SummonCreature(TempSpawnSettings(owner, slot.entry, p[0], p[1], p[2], p[3], TEMPSPAWN_TIMED_OR_DEAD_DESPAWN, uint32(slot.until - Now()), true, false, 0, 0, 0, false, false, slot.key & 1 ? 27178 : 27249), m_owner.instance);
    else actor = owner->SummonCreature(slot.entry, p[0], p[1], p[2], p[3], TEMPSPAWN_DEAD_DESPAWN, 0, true);
    m_spawningKind = 0;
    if (actor)
    {
        slot.guid = actor->GetObjectGuid(); slot.state = ALIVE; slot.attempts = 0;
        if (!slot.until && UpperLifetime(slot.kind, slot.key)) slot.until = Now() + UpperLifetime(slot.kind, slot.key);
        if (slot.kind == INTRO_NEF) actor->SetFactionTemporary(103, TEMPFACTION_NONE);
        if (slot.kind == INTRO_REND) actor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
        if (slot.kind == SPECTATOR)
        {
            auto const& dest = upper_aSpectatorsTargetLocs[slot.key];
            actor->SetWalk(false); actor->GetMotionMaster()->MovePoint(0, dest[0], dest[1], dest[2]); actor->SetFacingTo(dest[3]);
            actor->AI()->SetReactState(REACT_DEFENSIVE);
        }
        if (slot.kind == STADIUM_WAVE || slot.kind == GYTH)
        {
            auto const& dest = upper_aStadiumLocs[2];
            if (slot.kind == STADIUM_WAVE) actor->GetMotionMaster()->MovePoint(0, slot.destination[0], slot.destination[1], slot.destination[2]);
            else actor->GetMotionMaster()->MovePoint(0, dest[0], dest[1], dest[2]);
        }
        if (slot.kind == INTRO_NEF && m_progress.stadiumPhase >= 4) { auto const& dest = upper_aStadiumLocs[5]; actor->GetMotionMaster()->MovePoint(0, dest[0], dest[1], dest[2]); }
        Commit(); return true;
    }
    if (slot.attempts == 5)
    {
        script_error_log("UBRS: summon request failed entry%u kind%u key%u", slot.entry, slot.kind, slot.key);
        if (IsStadium(slot.kind)) FailStadium();
        else if (IsRookery(slot.kind)) FailRookery();
    }
    return false;
}

void BlackrockSpireUpper::Clear(uint32 scene, bool removeSlots)
{
    GuidVector detach;
    for (auto& actor : m_progress.actors)
        if ((scene == 3 && IsStadium(actor.kind)) || (scene == 2 && IsRookery(actor.kind)) || (scene == 5 && actor.kind >= VALTHALAK))
        {
            detach.push_back(actor.guid); actor.guid.Clear();
            if (actor.state != KILLED) actor.state = EXPIRED;
        }
    if (removeSlots) m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [scene](Actor const& a) { return scene == 3 ? IsStadium(a.kind) : scene == 2 ? IsRookery(a.kind) : a.kind >= VALTHALAK; }), m_progress.actors.end());
    for (ObjectGuid guid : detach) DespawnUpperAlive(m_owner.instance, guid);
    if (scene == 3) { auto retiring = m_retiringStadium; m_retiringStadium.clear(); for (ObjectGuid guid : retiring) DespawnUpperAlive(m_owner.instance, guid); }
    if (scene == 5)
    {
        m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION; }), m_progress.actors.end());
        auto minions = m_minions; m_minions.clear();
        for (ObjectGuid guid : minions) DespawnUpperAlive(m_owner.instance, guid);
    }
}

bool BlackrockSpireUpper::StartStadium(Player* player)
{
    if (!player || !player->IsAlive() || player->GetMap() != m_owner.instance || GetData(3) == IN_PROGRESS || GetData(3) == DONE) return false;
    Clear(3, true); ++m_progress.stadiumGeneration; m_progress.encounter[3] = IN_PROGRESS;
    m_progress.stadiumPhase = 1; m_progress.stadiumTimer = m_progress.stadiumWave = 0;
    Queue(INTRO_NEF, 0, 10162, upper_aStadiumLocs[3]); Queue(INTRO_REND, 0, 10429, upper_aStadiumLocs[4]);
    Commit(); SyncObjects(); return true;
}

void BlackrockSpireUpper::FailStadium()
{
    if (GetData(3) != IN_PROGRESS) return;
    m_progress.encounter[3] = FAIL; m_progress.stadiumPhase = m_progress.stadiumTimer = 0; m_progress.stadiumGateUntil = 0;
    Clear(3, false); SyncObjects(); Commit();
}

void BlackrockSpireUpper::Say(int32 text, uint32 kind)
{
    if (Actor* slot = Find(kind)) if (Creature* actor = m_owner.instance->GetCreature(slot->guid)) DoScriptText(text, actor);
}
void BlackrockSpireUpper::Move(uint32 kind, uint32 position)
{
    if (Actor* slot = Find(kind)) if (Creature* actor = m_owner.instance->GetCreature(slot->guid))
    { auto const& dest = upper_aStadiumLocs[position]; actor->GetMotionMaster()->MovePoint(0, dest[0], dest[1], dest[2]); }
}
uint32 BlackrockSpireUpper::RemainingStadium() const
{
    uint32 count = 0;
    for (auto const& actor : m_progress.actors) if ((actor.kind == STADIUM_WAVE || actor.kind == GYTH || actor.kind == COMBAT_REND) && (actor.state == ALIVE || actor.state == PENDING)) ++count;
    return count;
}

void BlackrockSpireUpper::SendWave()
{
    uint32 wave = m_progress.stadiumWave;
    if (wave >= 7) return;
    ++m_progress.stadiumWave;
    for (uint32 i = 0; i < 5; ++i)
        if (uint32 entry = upper_aStadiumEventNpcs[wave][i])
        {
            auto pos = upper_aStadiumLocs[0];
            if (Player* player = m_owner.GetPlayerInMap(true, false)) player->GetRandomPoint(pos[0], pos[1], pos[2], 7, pos[0], pos[1], pos[2]);
            pos[0] = std::min(pos[0], upper_aStadiumLocs[0][0]);
            Queue(STADIUM_WAVE, wave * 5 + i, entry, pos);
        }
    m_progress.stadiumTimer = wave == 6 ? 0 : 60000;
    m_progress.stadiumGateUntil = Now() + 10000;
    SyncObjects(); Commit();
}

void BlackrockSpireUpper::UpdateStadium(uint32 diff)
{
    if (GetData(3) != IN_PROGRESS) return;
    if (m_progress.stadiumPhase == 1)
    {
        Actor* nef = Find(INTRO_NEF); Actor* rend = Find(INTRO_REND);
        if (!nef || !rend || nef->state != ALIVE || rend->state != ALIVE) return;
        Say(-1229004, INTRO_NEF); m_progress.stadiumPhase = 2; m_progress.stadiumTimer = 7000; Commit(); return;
    }
    if (m_progress.stadiumPhase == 4 && m_progress.stadiumWave && !RemainingStadium())
    {
        if (m_progress.stadiumWave < 7) SendWave();
        else { Say(-1229014, INTRO_NEF); m_progress.stadiumPhase = 5; m_progress.stadiumTimer = 3000; Commit(); }
        return;
    }
    if (m_progress.stadiumPhase == 8)
    {
        Actor* gyth = Find(GYTH); Actor* rend = Find(COMBAT_REND);
        if (gyth && rend && gyth->state == KILLED && rend->state == KILLED)
        { Say(-1229018, INTRO_NEF); m_progress.stadiumPhase = 9; m_progress.stadiumTimer = 5000; Commit(); }
        return;
    }
    if (!m_progress.stadiumTimer) return;
    if (m_progress.stadiumTimer > diff) { m_progress.stadiumTimer -= diff; return; }
    m_progress.stadiumTimer = 0;
    switch (m_progress.stadiumPhase)
    {
        case 2: Say(-1229005, INTRO_NEF); m_progress.stadiumPhase = 3; m_progress.stadiumTimer = 5000; break;
        case 3:
            Move(INTRO_NEF, 5);
            for (uint32 i = 0; i < 12; ++i) Queue(SPECTATOR, i, upper_aStadiumSpectators[i], upper_aSpectatorsSpawnLocs[i]);
            m_progress.stadiumPhase = 4; m_progress.stadiumTimer = 1000; break;
        case 4: SendWave(); break;
        case 5: Say(-1229015, INTRO_REND); m_progress.stadiumPhase = 6; m_progress.stadiumTimer = 2000; break;
        case 6:
            Say(-1229016, INTRO_NEF); Move(INTRO_REND, 6);
            if (Actor* rend = Find(INTRO_REND)) { ObjectGuid guid = rend->guid; rend->guid.Clear(); rend->state = EXPIRED; m_retiringStadium.insert(guid); if (Creature* actor = m_owner.instance->GetCreature(guid)) actor->ForcedDespawn(5000); }
            if (Actor* nef = Find(INTRO_NEF)) if (Creature* actor = m_owner.instance->GetCreature(nef->guid)) actor->GetMotionMaster()->MoveWaypoint(0);
            m_progress.stadiumPhase = 7; m_progress.stadiumTimer = 30000; break;
        case 7:
            Move(INTRO_NEF, 5); Queue(GYTH, 0, 10339, upper_aStadiumLocs[1]); Queue(COMBAT_REND, 0, 10429, upper_aStadiumLocs[2]);
            m_progress.stadiumPhase = 8; m_progress.stadiumGateUntil = Now() + 10000; break;
        case 9:
        {
            ObjectGuid nefGuid; if (Actor* nef = Find(INTRO_NEF)) { nefGuid = nef->guid; nef->guid.Clear(); nef->state = EXPIRED; }
            m_progress.encounter[3] = DONE; m_progress.stadiumPhase = 10; Clear(3, false);
            if (Creature* nef = m_owner.instance->GetCreature(nefGuid)) { auto const& pos = upper_aStadiumLocs[6]; nef->GetMotionMaster()->MovePoint(0, pos[0], pos[1], pos[2]); nef->ForcedDespawn(5000); m_retiringStadium.insert(nefGuid); }
            break;
        }
    }
    SyncObjects(); Commit();
}

bool BlackrockSpireUpper::IsStadiumGyth(Creature* creature) const
{
    Actor const* slot = Find(GYTH);
    return creature && GetData(3) == IN_PROGRESS && m_progress.stadiumPhase == 8 && slot && slot->guid == creature->GetObjectGuid() && slot->state == ALIVE;
}
bool BlackrockSpireUpper::WasStadiumRendReleased(Creature* gyth) const
{
    Actor const* rend = Find(COMBAT_REND);
    return IsStadiumGyth(gyth) && rend && (rend->state == ALIVE || rend->state == KILLED);
}
bool BlackrockSpireUpper::ConfirmStadiumRend(Creature* gyth, Creature* rend)
{
    if (!IsStadiumGyth(gyth) || !rend || !rend->IsAlive() || rend->GetEntry() != 10429 || rend->GetMap() != m_owner.instance ||
        (rend->GetSpawnerGuid() != gyth->GetObjectGuid() && rend->GetOwnerGuid() != gyth->GetObjectGuid())) return false;
    Actor* slot = Find(COMBAT_REND);
    if (!slot || slot->state != PENDING) return slot && slot->guid == rend->GetObjectGuid();
    slot->guid = rend->GetObjectGuid(); slot->state = ALIVE; rend->SetActiveObjectState(true); Commit(); return true;
}

bool BlackrockSpireUpper::StartRookery(Player* player)
{
    if (!player || !player->IsAlive() || player->GetMap() != m_owner.instance || GetData(2) == IN_PROGRESS || GetData(2) == SPECIAL || GetData(2) == DONE || GetData(4) == DONE) return false;
    Clear(2, true); ++m_progress.rookeryGeneration; m_progress.encounter[2] = IN_PROGRESS;
    m_progress.rookeryWave = 0; m_progress.rookeryTimer = 1; Commit(); return true;
}
void BlackrockSpireUpper::FailRookery()
{
    if (GetData(2) != IN_PROGRESS && GetData(2) != SPECIAL) return;
    m_progress.encounter[2] = FAIL; m_progress.rookeryTimer = 0; Clear(2, false); Commit();
}
void BlackrockSpireUpper::UpdateRookery(uint32 diff)
{
    if (GetData(2) != IN_PROGRESS || !m_progress.rookeryTimer) return;
    if (m_progress.rookeryTimer > diff) { m_progress.rookeryTimer -= diff; return; }
    m_progress.rookeryTimer = 0;
    uint32 wave = m_progress.rookeryWave++;
    if (wave < 6)
    {
        for (uint32 i = 0; i < 2; ++i)
        {
            std::array<float, 4> pos = {{51.11098f, -266.0549f, 92.87846f, 0}};
            if (Player* player = m_owner.GetPlayerInMap(true, false)) player->GetRandomPoint(pos[0], pos[1], pos[2], 2.5f, pos[0], pos[1], pos[2]);
            Queue(ROOKERY, wave * 2 + i, wave && urand(0, 1) ? 10258 : 10683, pos);
        }
        m_progress.rookeryTimer = wave < 4 ? 30000 : 40000;
    }
    else Queue(SOLAKAR, 0, 10264, {{51.11098f, -266.0549f, 92.87846f, 0}});
    Commit();
}

bool BlackrockSpireUpper::TryUnlockUpper(Player* player)
{
    if (!player || !player->IsAlive() || player->GetMap() != m_owner.instance) return false;
    if (GetData(0) == NOT_STARTED) m_progress.encounter[0] = IN_PROGRESS;
    if (!m_progress.opened && player->HasItemCount(12344, 1))
    { m_progress.unlockRequested = 1; m_progress.unlockTimer = 100; m_unlockAttempts = 0; Commit(); return true; }
    SyncObjects(); return m_progress.opened != 0;
}
void BlackrockSpireUpper::SetEmberseerRunes(bool reset)
{
    for (uint32 entry = 175266; entry <= 175272; ++entry) UpperDoor(m_owner.instance, m_objects, entry, !reset);
}
bool BlackrockSpireUpper::IsEmberseerGrowing() const { return m_progress.emberStage == 1; }
bool BlackrockSpireUpper::IsEmberseerReleased() const { return m_progress.emberStage == 2 && GetData(1) != DONE; }
bool BlackrockSpireUpper::ShouldEmberseerBeCaged() const { return m_progress.emberStage == 0 && GetData(1) != DONE; }
uint32 BlackrockSpireUpper::GetEmberseerGeneration() const { return m_progress.emberGeneration; }
uint32 BlackrockSpireUpper::GetEmberseerGrowingStacks() const { return m_progress.emberTicks; }
uint32 BlackrockSpireUpper::GetEmberseerReleaseStep() const { return m_progress.emberReleaseStep; }
void BlackrockSpireUpper::EmberseerReleaseStep(Creature* boss, uint32 step)
{
    if (!boss || boss->GetObjectGuid() != m_emberseer || !IsEmberseerGrowing() || m_progress.emberTicks != 20 || step > 3 || step < m_progress.emberReleaseStep) return;
    m_progress.emberReleaseStep = step; Commit();
}
void BlackrockSpireUpper::EmberseerGrowingTick(Creature* boss, uint32 stacks)
{
    if (!boss || boss->GetObjectGuid() != m_emberseer || !IsEmberseerGrowing() || stacks > 20 || stacks < m_progress.emberTicks) return;
    m_progress.emberTicks = stacks; Commit();
}
void BlackrockSpireUpper::GetIncarceratorGUIDList(GuidList& out) const
{
    out.clear();
    for (uint32 dbguid : upperIncarcerators) { auto it = m_staticActors.find(dbguid); if (it != m_staticActors.end()) out.push_back(it->second); }
}
bool BlackrockSpireUpper::StartEmberseer(Player* player)
{
    if (!player || player->GetMap() != m_owner.instance || !player->IsAlive() || GetData(1) == DONE || m_progress.emberStage == 1 || m_progress.emberStage == 2) return false;
    if (m_emberResetPending) UpdateEmberseer(0);
    if (m_emberResetPending) return false;
    m_progress.encounter[1] = IN_PROGRESS; m_progress.emberStage = 1; ++m_progress.emberGeneration; m_progress.emberTimer = 120000;
    m_progress.emberTicks = m_progress.emberReleaseStep = 0;
    m_emberStartPending = true; m_emberAttempts = 0; Commit(); return true;
}
void BlackrockSpireUpper::EmberseerReleased(Creature* creature)
{
    if (!creature || creature->GetObjectGuid() != m_emberseer || !IsEmberseerGrowing()) return;
    m_progress.emberStage = 2; m_progress.emberTicks = 20; m_progress.emberReleaseStep = 3; m_progress.emberTimer = 0; m_emberStartPending = false; SyncObjects(); Commit();
}
void BlackrockSpireUpper::FailEmberseer()
{
    if (GetData(1) == DONE) return;
    m_progress.encounter[1] = FAIL; m_progress.emberStage = m_progress.emberTimer = 0;
    m_progress.emberTicks = m_progress.emberReleaseStep = 0;
    m_emberStartPending = false; m_emberResetPending = true; SetEmberseerRunes(true); SyncObjects(); Commit();
}
void BlackrockSpireUpper::UpdateEmberseer(uint32 diff)
{
    Creature* boss = m_owner.instance->GetCreature(m_emberseer);
    if (m_emberResetPending && boss && boss->IsAlive() && !boss->IsInCombat() && ShouldEmberseerBeCaged())
    {
        m_emberResetPending = false; boss->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_E, boss, boss, m_progress.emberGeneration);
        for (uint32 dbguid : upperIncarcerators)
        {
            auto it = m_staticActors.find(dbguid);
            if (it != m_staticActors.end()) if (Creature* actor = m_owner.instance->GetCreature(it->second))
            {
                if (!actor->IsAlive()) actor->Respawn();
                actor->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PLAYER); m_encagePending.insert(actor->GetObjectGuid());
            }
        }
    }
    if (ShouldEmberseerBeCaged() && boss)
    {
        auto pending = m_encagePending; m_encagePending.clear();
        for (ObjectGuid guid : pending) if (Creature* actor = m_owner.instance->GetCreature(guid)) boss->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_C, actor, boss, 0);
    }
    if (boss && m_emberRestorePending && IsEmberseerReleased())
    { m_emberRestorePending = false; boss->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_D, boss, boss, m_progress.emberGeneration); }
    if (!ShouldEmberseerBeCaged())
    {
        auto pending = m_encagePending; m_encagePending.clear();
        for (ObjectGuid guid : pending) if (Creature* actor = m_owner.instance->GetCreature(guid)) if (actor->IsAlive())
        { actor->InterruptNonMeleeSpells(false); actor->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PLAYER); }
    }
    if (m_emberStartPending && boss && boss->IsAlive())
    {
        m_emberStartPending = false;
        boss->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_B, boss, boss, m_progress.emberGeneration);
        if (!m_progress.emberTicks) DoScriptText(-1229000, boss);
        if (m_progress.emberTicks < 20 && boss->CastSpell(boss, 16048, TRIGGERED_OLD_TRIGGERED) != SPELL_CAST_OK) { FailEmberseer(); return; }
        for (uint32 dbguid : upperIncarcerators)
        {
            auto it = m_staticActors.find(dbguid);
            if (it != m_staticActors.end()) if (Creature* actor = m_owner.instance->GetCreature(it->second)) if (actor->IsAlive())
            { actor->InterruptNonMeleeSpells(false); actor->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC | UNIT_FLAG_IMMUNE_TO_PLAYER); }
        }
    }
    if (IsEmberseerGrowing())
    {
        if (m_progress.emberTimer <= diff) FailEmberseer();
        else m_progress.emberTimer -= diff;
    }
}

bool BlackrockSpireUpper::RequestValthalak(Creature* trigger)
{
    if (!trigger || trigger->GetEntry() != 16048 || !trigger->HasStaticDBSpawnData() || trigger->GetMap() != m_owner.instance || m_owner.instance->GetCreature(trigger->GetObjectGuid()) != trigger) return false;
    Actor* previous = Find(VALTHALAK);
    if (previous)
    {
        if (Creature* actor = m_owner.instance->GetCreature(previous->guid)) if (actor->IsAlive() && (actor->IsInCombat() || actor->GetCharmer())) { previous->until = Now() + 7200000; return false; }
        if (previous->state == PENDING || (previous->state == ALIVE && previous->until > Now())) return false;
    }
    // A new legitimate ritual does not shorten surviving assassins' native TTL.
    // They keep their original summon timers and cease to belong to the new round.
    m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION; }), m_progress.actors.end());
    m_minions.clear();
    Clear(5, true); ++m_progress.valthGeneration; m_progress.encounter[5] = IN_PROGRESS; m_valthTrigger = trigger->GetObjectGuid();
    Queue(VALTHALAK, 0, 16042, {{57.5800018f, -537.265991f, 110.934998f, 3.0999999f}}); Commit(); return true;
}
void BlackrockSpireUpper::FailValthalak()
{
    m_progress.encounter[5] = FAIL; Clear(5, false); Commit();
}

void BlackrockSpireUpper::SyncObjects()
{
    if (GetData(0) != NOT_STARTED && m_progress.runes != 127)
    {
        uint32 mask = m_progress.runes;
        for (uint32 room = 0; room < 7; ++room)
        {
            bool cleared = true;
            for (auto const& expected : upperRuneGuards) if (expected.rune == room)
            {
                bool dead = m_owner.instance->GetPersistentState()->GetCreatureRespawnTime(expected.guid) > time(nullptr);
                auto it = m_staticActors.find(expected.guid);
                if (it != m_staticActors.end()) if (Creature* actor = m_owner.instance->GetCreature(it->second)) dead = !actor->IsAlive();
                if (!dead) cleared = false;
            }
            if (cleared) mask |= 1u << room;
        }
        if (mask != m_progress.runes) { m_progress.runes = mask; if (mask == 127) m_progress.encounter[0] = DONE; Commit(); }
    }
    UpperDoor(m_owner.instance, m_objects, 164725, m_progress.opened != 0);
    for (uint32 i = 0; i < 6; ++i) UpperDoor(m_owner.instance, m_objects, 175528 + i, m_progress.unlockStep > i / 2);
    for (uint32 i = 0; i < 7; ++i) UpperDoor(m_owner.instance, m_objects, upperRuneEntries[i], !(m_progress.runes & (1u << i)));
    UpperDoor(m_owner.instance, m_objects, 175244, GetData(0) == DONE);
    UpperDoor(m_owner.instance, m_objects, 175705, GetData(1) != IN_PROGRESS);
    UpperDoor(m_owner.instance, m_objects, 175153, GetData(1) == DONE);
    for (uint32 entry = 175266; entry <= 175272; ++entry) UpperDoor(m_owner.instance, m_objects, entry, m_progress.emberStage == 2);
    UpperDoor(m_owner.instance, m_objects, 164726, GetData(3) != IN_PROGRESS);
    UpperDoor(m_owner.instance, m_objects, 175185, m_progress.stadiumGateUntil > Now());
    UpperDoor(m_owner.instance, m_objects, 175186, GetData(3) == DONE);
    UpperDoor(m_owner.instance, m_objects, 175946, GetData(4) == DONE);
    UpperDoor(m_owner.instance, m_objects, 175947, GetData(4) == DONE);
}

void BlackrockSpireUpper::SetData(uint32 type, uint32 value)
{
    if (type >= 6 || value > SPECIAL) return;
    if (type == 3 && value == FAIL) { FailStadium(); return; }
    if (type == 1 && value == FAIL) { FailEmberseer(); return; }
    if (type == 2 && value == FAIL) { FailRookery(); return; }
    if (GetData(type) == DONE && value != DONE) return;
    m_progress.encounter[type] = value;
    if (type == 0 && value == DONE) m_progress.runes = 127;
    if (type == 1 && value == DONE) { m_progress.emberStage = 3; m_progress.emberTimer = 0; }
    SyncObjects(); Commit();
}

void BlackrockSpireUpper::OnObjectCreate(GameObject* object)
{
    m_objects[object->GetEntry()] = object->GetObjectGuid();
    if (m_progress.stadiumReset) RestoreStadiumDoor(object);
    if (!m_progress.opened && m_progress.unlockStep < 4 && !m_progress.unlockTimer && m_progress.unlockRequested) { m_unlockAttempts = 0; m_progress.unlockTimer = 1000; }
    SyncObjects();
}
void BlackrockSpireUpper::OnCreatureCreate(Creature* creature)
{
    if (creature->HasStaticDBSpawnData()) m_staticActors[creature->GetDbGuid()] = creature->GetObjectGuid();
    if (creature->GetEntry() == 9816) { m_emberseer = creature->GetObjectGuid(); if (IsEmberseerGrowing()) { m_emberStartPending = true; m_progress.emberTimer = std::max(m_progress.emberTimer, (20 - m_progress.emberTicks) * 3000 + 10000); } if (IsEmberseerReleased()) m_emberRestorePending = true; if (ShouldEmberseerBeCaged()) { m_emberResetPending = true; for (uint32 dbguid : upperIncarcerators) { auto it = m_staticActors.find(dbguid); if (it != m_staticActors.end()) m_encagePending.insert(it->second); } } }
    if (creature->GetEntry() == 10316) m_encagePending.insert(creature->GetObjectGuid());
    if (creature->GetEntry() == 16048 && creature->HasStaticDBSpawnData()) m_valthTrigger = creature->GetObjectGuid();
    if (creature->GetEntry() == 16066 && m_spawningKind != VALTH_MINION)
    {
        Actor* boss = Find(VALTHALAK);
        bool owned = boss && boss->state == ALIVE && boss->guid && (creature->GetSpawnerGuid() == boss->guid || creature->GetOwnerGuid() == boss->guid);
        owned = owned || m_minions.count(creature->GetSpawnerGuid()) || m_minions.count(creature->GetOwnerGuid());
        if (owned && !m_minions.count(creature->GetObjectGuid()))
        {
            uint32 key = creature->GetUInt32Value(UNIT_CREATED_BY_SPELL) == 27178 ? 1 : 0;
            while (Find(VALTH_MINION, key)) key += 2;
            Actor slot; slot.kind = VALTH_MINION; slot.key = key; slot.entry = 16066; slot.state = ALIVE; slot.guid = creature->GetObjectGuid();
            slot.position = {{creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), creature->GetOrientation()}};
            slot.until = Now() + UpperLifetime(VALTH_MINION, key); m_progress.actors.push_back(slot); m_minions.insert(slot.guid); Commit();
        }
    }
}
void BlackrockSpireUpper::OnCreatureRespawn(Creature* creature) { OnCreatureCreate(creature); }
void BlackrockSpireUpper::OnCreatureEnterCombat(Creature* creature)
{
    if (creature->GetEntry() == 10316 && GetData(1) != DONE && m_progress.emberStage) SetData(1, IN_PROGRESS);
}
void BlackrockSpireUpper::OnCreatureDeath(Creature* creature)
{
    auto minion = std::find_if(m_progress.actors.begin(), m_progress.actors.end(), [creature](Actor const& a) { return a.kind == VALTH_MINION && a.guid == creature->GetObjectGuid(); });
    if (minion != m_progress.actors.end()) { m_minions.erase(creature->GetObjectGuid()); m_progress.actors.erase(minion); Commit(); return; }
    if (creature->GetEntry() == 9816 && creature->GetObjectGuid() == m_emberseer) SetData(1, DONE);
    if (creature->GetEntry() == 10363 && creature->HasStaticDBSpawnData()) SetData(4, DONE);
    for (auto& actor : m_progress.actors) if (actor.guid == creature->GetObjectGuid())
    {
        if (actor.kind == STADIUM_WAVE && GetData(3) == IN_PROGRESS)
        {
            static int32 const nef[] = {-1229006,-1229008,-1229009,-1229010,-1229013,-1229021,-1229022,-1229023};
            static int32 const rend[] = {-1229007,-1229011,-1229012,-1229024};
            if (urand(0, 100) < 5) Say(nef[urand(0, 7)], INTRO_NEF);
            if (urand(0, 100) < 5) Say(rend[urand(0, 3)], INTRO_REND);
        }
        actor.state = KILLED; actor.guid.Clear();
        if (actor.kind == SOLAKAR) m_progress.encounter[2] = DONE;
        if (actor.kind == VALTHALAK) m_progress.encounter[5] = DONE;
        Commit(); break;
    }
    m_minions.erase(creature->GetObjectGuid());
    auto previousSize = m_progress.actors.size();
    m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION && a.state != ALIVE; }), m_progress.actors.end());
    if (previousSize != m_progress.actors.size()) Commit();
    SyncObjects();
}
void BlackrockSpireUpper::OnCreatureEvade(Creature* creature)
{
    if (creature->GetEntry() == 10316 && m_progress.emberStage) { FailEmberseer(); return; }
    for (auto const& actor : m_progress.actors) if (actor.guid == creature->GetObjectGuid())
    {
        if (IsStadium(actor.kind)) FailStadium();
        else if (IsRookery(actor.kind)) FailRookery();
        else if (actor.kind == VALTHALAK)
        {
            // The original summoned boss stays available after returning home;
            // only its living assassins are removed on an ordinary wipe.
            GuidVector detach;
            for (auto& minion : m_progress.actors) if (minion.kind == VALTH_MINION) { detach.push_back(minion.guid); minion.guid.Clear(); }
            m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION; }), m_progress.actors.end());
            m_minions.clear();
            for (ObjectGuid guid : detach) DespawnUpperAlive(m_owner.instance, guid);
            Commit();
        }
        return;
    }
}
void BlackrockSpireUpper::OnCreatureDespawn(Creature* creature)
{
    m_minions.erase(creature->GetObjectGuid()); m_retiringStadium.erase(creature->GetObjectGuid());
    for (auto& actor : m_progress.actors) if (actor.guid == creature->GetObjectGuid())
    {
        actor.guid.Clear(); actor.state = EXPIRED;
        if (actor.kind == VALTH_MINION) { m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION && a.state != ALIVE; }), m_progress.actors.end()); Commit(); return; }
        if (IsStadium(actor.kind)) FailStadium(); else if (actor.kind == SOLAKAR) FailRookery(); else if (actor.kind == VALTHALAK) FailValthalak();
        Commit(); return;
    }
}

void BlackrockSpireUpper::ProcessActors(uint32 /*diff*/)
{
    for (size_t i = 0; i < m_progress.actors.size(); ++i)
    {
        Actor& slot = m_progress.actors[i];
        if (slot.state != ALIVE && slot.state != PENDING) continue;
        if (slot.guid)
        {
            Creature* actor = m_owner.instance->GetCreature(slot.guid);
            if (actor && actor->IsAlive() && slot.kind != VALTH_MINION && UpperLifetime(slot.kind, slot.key) && (actor->IsInCombat() || actor->GetCharmer())) slot.until = Now() + UpperLifetime(slot.kind, slot.key);
            else if (!actor || !actor->IsAlive() || (slot.until && slot.until <= Now() && !(slot.kind == VALTH_MINION && actor->GetCharmer())))
            {
                ObjectGuid guid = slot.guid; slot.guid.Clear(); slot.state = EXPIRED; DespawnUpperAlive(m_owner.instance, guid);
                if (IsStadium(slot.kind)) FailStadium(); else if (slot.kind == SOLAKAR) FailRookery(); else if (slot.kind == VALTHALAK) FailValthalak();
                if (slot.kind == VALTH_MINION) m_progress.actors.erase(m_progress.actors.begin() + i);
                Commit(); return;
            }
        }
        else Spawn(slot);
        if (slot.kind == VALTH_MINION && slot.guid) m_minions.insert(slot.guid);
    }
    auto previousSize = m_progress.actors.size();
    m_progress.actors.erase(std::remove_if(m_progress.actors.begin(), m_progress.actors.end(), [](Actor const& a) { return a.kind == VALTH_MINION && a.state != ALIVE; }), m_progress.actors.end());
    if (previousSize != m_progress.actors.size()) Commit();
    Actor* solakar = Find(SOLAKAR);
    if (solakar && solakar->state == ALIVE && GetData(2) == IN_PROGRESS) { m_progress.encounter[2] = SPECIAL; Commit(); }
}
void BlackrockSpireUpper::Pause()
{
    if (m_paused) return;
    m_paused = true;
    GuidVector detach;
    for (auto& actor : m_progress.actors) { detach.push_back(actor.guid); actor.guid.Clear(); }
    for (ObjectGuid guid : detach) DespawnUpperAlive(m_owner.instance, guid);
    auto minions = m_minions; m_minions.clear(); for (ObjectGuid guid : minions) DespawnUpperAlive(m_owner.instance, guid);
    Commit();
}
void BlackrockSpireUpper::OnPlayerEnter(Player* /*player*/)
{
    m_paused = false; for (auto& actor : m_progress.actors) { actor.attempts = 0; actor.retry = 0; }
    ProcessActors(0); SyncObjects();
}
void BlackrockSpireUpper::OnPlayerLeave(Player* player)
{
    for (auto const& ref : m_owner.instance->GetPlayers()) if (Player* other = ref.getSource()) if (other != player && !other->IsGameMaster()) { Commit(); return; }
    Pause();
}
void BlackrockSpireUpper::Update(uint32 diff)
{
    if (!m_owner.GetPlayerInMap(true, false))
    {
        if (m_owner.GetPlayerInMap(false, false)) { FailStadium(); FailRookery(); if (m_progress.emberStage) FailEmberseer(); ProcessActors(diff); }
        else Pause();
        return;
    }
    m_paused = false;
    if (m_progress.unlockTimer)
    {
        if (m_progress.unlockTimer > diff) m_progress.unlockTimer -= diff;
        else
        {
            uint32 step = m_progress.unlockStep;
            bool success = step < 3 ? UpperDoor(m_owner.instance, m_objects, 175528 + step * 2, true) && UpperDoor(m_owner.instance, m_objects, 175529 + step * 2, true) : UpperDoor(m_owner.instance, m_objects, 164725, true);
            if (success) { ++m_progress.unlockStep; m_unlockAttempts = 0; if (m_progress.unlockStep == 4) m_progress.opened = 1; }
            else ++m_unlockAttempts;
            m_progress.unlockTimer = m_progress.opened || m_unlockAttempts >= 5 ? 0 : 1000;
            Commit();
        }
    }
    ProcessActors(diff); UpdateStadium(diff); UpdateRookery(diff); UpdateEmberseer(diff); ProcessActors(0); SyncObjects();
    if (m_checkpoint > diff) m_checkpoint -= diff;
    else { if (GetData(3) == IN_PROGRESS || GetData(2) == IN_PROGRESS || GetData(2) == SPECIAL || GetData(5) == IN_PROGRESS || IsEmberseerGrowing()) Commit(); m_checkpoint = 5000; }
}

bool BlackrockSpireUpper::GetEventResetData(uint32 event, std::string& text) const
{
    if (event != 3) return false;
    for (auto const& actor : m_progress.actors) if (IsStadium(actor.kind)) if (Creature* creature = m_owner.instance->GetCreature(actor.guid)) if (creature->IsInCombat()) return false;
    Progress next = m_progress; next.encounter[3] = NOT_STARTED; next.stadiumPhase = next.stadiumTimer = next.stadiumWave = 0; next.stadiumGateUntil = 0; next.stadiumReset = 1;
    next.actors.erase(std::remove_if(next.actors.begin(), next.actors.end(), [](Actor const& a) { return IsStadium(a.kind); }), next.actors.end());
    text = Encode(next); return true;
}
bool BlackrockSpireUpper::ApplyEventReset(uint32 event, std::string const& text)
{
    if (event != 3) return false;
    auto states = m_progress.encounter; states[3] = NOT_STARTED;
    Progress next; if (!Decode(states, text, next)) return false;
    Progress expected = m_progress; expected.encounter[3] = NOT_STARTED; expected.stadiumPhase = expected.stadiumTimer = expected.stadiumWave = 0; expected.stadiumGateUntil = 0; expected.stadiumReset = 1;
    expected.actors.erase(std::remove_if(expected.actors.begin(), expected.actors.end(), [](Actor const& a) { return IsStadium(a.kind); }), expected.actors.end());
    if (Encode(expected) != text) return false;
    Clear(3, true); m_progress = expected;
    for (uint32 entry : {164726u,175185u,175186u}) { auto it = m_objects.find(entry); if (it != m_objects.end()) RestoreStadiumDoor(m_owner.instance->GetGameObject(it->second)); }
    SyncObjects(); return true;
}

void BlackrockSpireUpper::RestoreStadiumDoor(GameObject* object)
{
    if (!object || !object->HasStaticDBSpawnData()) return;
    uint32 entry = object->GetEntry();
    if (entry != 164726 && entry != 175185 && entry != 175186) return;
    object->Respawn(); object->SetUInt32Value(GAMEOBJECT_FLAGS, object->GetGOInfo()->flags); object->SetLootState(GO_READY);
    object->SetGoState(entry == 164726 ? GO_STATE_ACTIVE : GO_STATE_READY);
}
