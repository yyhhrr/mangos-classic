/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"
#include "dire_maul_west_data.h"
#include <sstream>

namespace
{
bool WestNumber(std::istream& input, uint64& value, uint64 maximum)
{
    std::string token;
    if (!(input >> token) || token.empty()) return false;
    value = 0;
    for (char digit : token)
    {
        if (digit < '0' || digit > '9' || value > maximum / 10 ||
            (value == maximum / 10 && uint64(digit - '0') > maximum % 10)) return false;
        value = value * 10 + uint64(digit - '0');
    }
    return true;
}
}

DireMaulWest::DireMaulWest(ScriptedInstance& owner, std::function<void()> save) :
    m_owner(owner), m_save(std::move(save)), m_ritual(owner, [this]() { Commit(); }) {}
void DireMaulWest::Commit() { if (m_save) m_save(); }
uint32 DireMaulWest::GetData(uint32 type) const
{
    if (type == TYPE_DREADSTEED) return m_ritual.GetData();
    return type >= TYPE_WARPWOOD && type <= TYPE_PYLON_5 ? m_progress.encounters[type - TYPE_WARPWOOD] : 0;
}
std::string DireMaulWest::Save() const
{
    std::string ritual = m_ritual.Save();
    std::ostringstream out;
    out << "W1 " << m_progress.route << ' ' << m_progress.releaseSaid << ' ' << m_progress.guardsDead << ' ' << ritual.size() << ' ' << ritual;
    return out.str();
}
bool DireMaulWest::Decode(std::array<uint32, 9> const& states, std::string const& text, Progress& next, std::string& ritual)
{
    Progress candidate;
    for (uint32 i = 0; i < states.size(); ++i) if (states[i] > SPECIAL) return false;
    for (uint32 i = 0; i < 8; ++i) candidate.encounters[i] = states[i] == IN_PROGRESS ? uint32(NOT_STARTED) : states[i];
    ritual.clear();
    if (!text.empty())
    {
        if (text.size() > 24000) return false;
        std::istringstream in(text);
        std::string version;
        uint64 route, said, dead, length;
        if (!(in >> version) || version != "W1" || !WestNumber(in, route, 3) || !WestNumber(in, said, 1) ||
            !WestNumber(in, dead, (uint64(1) << 40) - 1) || !WestNumber(in, length, 23900)) return false;
        if (in.get() != ' ') return false;
        ritual.resize(size_t(length));
        if (length && !in.read(&ritual[0], std::streamsize(length))) return false;
        in >> std::ws;
        if (!in.eof()) return false;
        candidate.route = uint32(route); candidate.releaseSaid = uint32(said); candidate.guardsDead = dead;
        if (said) for (uint32 i = 3; i < 8; ++i) if (candidate.encounters[i] != DONE) return false;
    }
    if (!DireMaulRitual::ValidateSave(states[8], ritual)) return false;
    next = candidate;
    return true;
}
bool DireMaulWest::ValidateSave(std::array<uint32, 9> const& states, std::string const& text)
{
    Progress next; std::string ritual;
    return Decode(states, text, next, ritual);
}
bool DireMaulWest::Load(std::array<uint32, 9> const& states, std::string const& text)
{
    Progress next; std::string ritual;
    if (!Decode(states, text, next, ritual)) return false;
    m_progress = next;
    if (!m_ritual.Load(states[8], ritual)) return false;
    m_syncTimer = 0;
    SyncObjects();
    return true;
}
bool DireMaulWest::Current(Creature* actor) const
{
    return actor && actor->GetMap() == m_owner.instance && m_owner.instance->GetCreature(actor->GetObjectGuid()) == actor;
}
int32 DireMaulWest::GuardIndex(Creature* actor) const
{
    if (!actor || actor->GetMap() != m_owner.instance || !actor->HasStaticDBSpawnData()) return -1;
    for (uint32 i = 0; i < 40; ++i)
        if (actor->GetDbGuid() == dmwPylonGuards[i].guid && actor->GetEntry() == dmwPylonGuards[i].entry) return int32(i);
    return -1;
}
bool DireMaulWest::AllPylonsDone() const
{
    for (uint32 type = TYPE_PYLON_1; type <= TYPE_PYLON_5; ++type) if (GetData(type) != DONE) return false;
    return true;
}
void DireMaulWest::SetData(uint32 type, uint32 value)
{
    if (type == TYPE_DREADSTEED) { m_ritual.SetData(value); return; }
    if (type < TYPE_WARPWOOD || type > TYPE_PYLON_5 || value > SPECIAL) return;
    uint32& state = m_progress.encounters[type - TYPE_WARPWOOD];
    if (state == DONE || state == value) { SyncObjects(); return; }
    state = value;
    SyncObjects(); Commit();
}
void DireMaulWest::CheckPylons()
{
    bool changed = false;
    for (uint32 pylon = 0; pylon < 5; ++pylon)
    {
        if (GetData(TYPE_PYLON_1 + pylon) == DONE) continue;
        bool cleared = true;
        for (uint32 i = 0; i < 40; ++i) if (dmwPylonGuards[i].pylonIndex == pylon)
            if (!(m_progress.guardsDead & (uint64(1) << i))) cleared = false;
        if (cleared) { m_progress.encounters[TYPE_PYLON_1 - TYPE_WARPWOOD + pylon] = DONE; changed = true; }
    }
    if (changed) { SyncObjects(); Commit(); }
}
void DireMaulWest::OnCreatureCreate(Creature* actor)
{
    m_ritual.OnCreatureCreate(actor);
    // Creature::Create notifies before AddToWorld inserts the object in Map.
    if (!actor || actor->GetMap() != m_owner.instance || !actor->HasStaticDBSpawnData()) return;
    if (actor->GetEntry() == NPC_PRINCE_TORTHELDRIN) m_prince = actor->GetObjectGuid();
    if (actor->GetEntry() == NPC_IMMOLTHAR) m_immolthar = actor->GetObjectGuid();
    int32 guard = GuardIndex(actor);
    if (guard >= 0) m_guards[size_t(guard)] = actor->GetObjectGuid();
    if (actor->GetEntry() == NPC_HIGHBORNE_SUMMONER)
        for (uint32 i = 0; i < 18; ++i) if (actor->GetDbGuid() == dmwSummonerGuids[i]) m_summoners[i] = actor->GetObjectGuid();
    m_syncTimer = 0;
}
void DireMaulWest::OnCreatureRespawn(Creature* actor)
{
    OnCreatureCreate(actor);
    m_ritual.OnCreatureRespawn(actor);
    int32 guard = GuardIndex(actor);
    if (guard >= 0 && GetData(TYPE_PYLON_1 + dmwPylonGuards[size_t(guard)].pylonIndex) != DONE)
    {
        uint64 old = m_progress.guardsDead;
        m_progress.guardsDead &= ~(uint64(1) << guard);
        if (old != m_progress.guardsDead) Commit();
    }
}
void DireMaulWest::OnCreatureDeath(Creature* actor)
{
    m_ritual.OnCreatureDeath(actor);
    if (!Current(actor) || !actor->HasStaticDBSpawnData()) return;
    switch (actor->GetEntry())
    {
        case NPC_TENDRIS_WARPWOOD: SetData(TYPE_WARPWOOD, DONE); break;
        case NPC_IMMOLTHAR:
            if (GetData(TYPE_IMMOLTHAR) != DONE)
                if (Creature* prince = m_owner.instance->GetCreature(m_prince)) DoScriptText(SAY_FREE_IMMOLTHAR, prince);
            SetData(TYPE_IMMOLTHAR, DONE);
            break;
        case NPC_PRINCE_TORTHELDRIN: SetData(TYPE_PRINCE, DONE); break;
    }
    int32 guard = GuardIndex(actor);
    if (guard >= 0)
    {
        uint64 bit = uint64(1) << guard;
        if (!(m_progress.guardsDead & bit)) { m_progress.guardsDead |= bit; CheckPylons(); Commit(); }
    }
    m_syncTimer = 0;
}
void DireMaulWest::OnCreatureEnterCombat(Creature* actor)
{
    if (!Current(actor) || !actor->HasStaticDBSpawnData()) return;
    if (actor->GetEntry() == NPC_IMMOLTHAR) SetData(TYPE_IMMOLTHAR, IN_PROGRESS);
    if (actor->GetEntry() == NPC_PRINCE_TORTHELDRIN) SetData(TYPE_PRINCE, IN_PROGRESS);
}
void DireMaulWest::OnCreatureEvade(Creature* actor)
{
    m_ritual.OnCreatureEvade(actor);
    if (!Current(actor) || !actor->HasStaticDBSpawnData()) return;
    if (actor->GetEntry() == NPC_IMMOLTHAR) SetData(TYPE_IMMOLTHAR, NOT_STARTED);
    if (actor->GetEntry() == NPC_PRINCE_TORTHELDRIN) SetData(TYPE_PRINCE, NOT_STARTED);
    m_syncTimer = 0;
}
void DireMaulWest::OnCreatureDespawn(Creature* actor) { m_ritual.OnCreatureDespawn(actor); }
void DireMaulWest::OnPlayerEnter(Player* player)
{
    m_ritual.OnPlayerEnter(player);
    if (player && !player->IsGameMaster())
    {
        uint32 old = m_progress.route;
        if (player->IsWithinDist3d(254.919998f, -19.463f, -2.559600f, 20.0f)) m_progress.route |= 1;
        // Only a real West entrance grants the original keyed-door route.
        // Reconnecting elsewhere and entering East do not revoke existing access.
        if (player->IsWithinDist3d(-65.655899f, 159.561005f, -3.464700f, 20.0f) ||
            player->IsWithinDist3d(33.108299f, 158.977005f, -3.471260f, 20.0f)) m_progress.route |= 2;
        if (old != m_progress.route) Commit();
    }
    SyncObjects(); m_syncTimer = 0;
}
void DireMaulWest::OnPlayerLeave(Player* player) { m_ritual.OnPlayerLeave(player); }
void DireMaulWest::OnObjectCreate(GameObject* object)
{
    m_ritual.OnObjectCreate(object);
    if (object->GetMap() != m_owner.instance) return;
    switch (object->GetEntry())
    {
        case GO_CRYSTAL_GENERATOR_1: case GO_CRYSTAL_GENERATOR_2: case GO_CRYSTAL_GENERATOR_3:
        case GO_CRYSTAL_GENERATOR_4: case GO_CRYSTAL_GENERATOR_5: case GO_FORCEFIELD:
        case GO_WARPWOOD_DOOR: case GO_WEST_LIBRARY_DOOR: case GO_PRINCES_CHEST_AURA:
            m_objects[object->GetEntry()] = object->GetObjectGuid(); break;
        default: return;
    }
    SyncObjects();
}
void DireMaulWest::SyncObjects()
{
    auto find = [this](uint32 entry) -> GameObject* {
        auto it = m_objects.find(entry);
        return it == m_objects.end() ? nullptr : m_owner.instance->GetGameObject(it->second);
    };
    for (uint32 i = 0; i < 5; ++i) if (GetData(TYPE_PYLON_1 + i) == DONE)
        if (GameObject* tower = find(dmwTowerEntries[i])) tower->SetGoState(GO_STATE_ACTIVE);
    if (AllPylonsDone()) if (GameObject* field = find(GO_FORCEFIELD)) field->SetGoState(GO_STATE_ACTIVE);
    if (GetData(TYPE_WARPWOOD) == DONE) if (GameObject* door = find(GO_WARPWOOD_DOOR)) door->SetGoState(GO_STATE_ACTIVE);
    if (GameObject* door = find(GO_WEST_LIBRARY_DOOR))
    {
        bool qualified = GetData(TYPE_WARPWOOD) == DONE || (m_progress.route & 2);
        if (qualified) door->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        else door->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        // Keep native LOCKED/OPEN_LOCK handling, including a player who has
        // already unlocked it. Route synchronization never relocks the door.
    }
    if (GetData(TYPE_IMMOLTHAR) == DONE)
        if (GameObject* aura = find(GO_PRINCES_CHEST_AURA))
            if (aura->IsSpawned() && aura->GetLootState() != GO_JUST_DEACTIVATED) aura->SetLootState(GO_JUST_DEACTIVATED);
}
void DireMaulWest::SyncActors()
{
    if (GetData(TYPE_IMMOLTHAR) == DONE)
        if (Creature* prince = m_owner.instance->GetCreature(m_prince))
            if (prince->IsAlive() && prince->GetFaction() != FACTION_HOSTILE)
                prince->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_PLAYER);
    if (!AllPylonsDone() || GetData(TYPE_IMMOLTHAR) == DONE) return;
    Creature* boss = m_owner.instance->GetCreature(m_immolthar);
    if (!boss || !boss->IsAlive()) return;
    for (ObjectGuid guid : m_summoners)
        if (Creature* summoner = m_owner.instance->GetCreature(guid))
            if (summoner->IsAlive() && !summoner->GetVictim() && !summoner->IsInCombat())
            {
                if (!m_progress.releaseSaid) { DoScriptText(SAY_KILL_IMMOLTHAR, summoner); m_progress.releaseSaid = 1; Commit(); }
                summoner->AI()->AttackStart(boss);
            }
}
void DireMaulWest::Update(uint32 diff)
{
    m_ritual.Update(diff);
    if (m_syncTimer > diff) { m_syncTimer -= diff; return; }
    m_syncTimer = 1000;
    uint64 previous = m_progress.guardsDead;
    // Create fires before the complete native DB load. Reconcile only after the
    // world update, when original respawn/death state is available.
    for (uint32 i = 0; i < 40; ++i)
        if (GetData(TYPE_PYLON_1 + dmwPylonGuards[i].pylonIndex) != DONE)
            if (Creature* guard = m_owner.instance->GetCreature(m_guards[i]))
            {
                if (guard->IsAlive()) m_progress.guardsDead &= ~(uint64(1) << i);
                else m_progress.guardsDead |= uint64(1) << i;
            }
    if (previous != m_progress.guardsDead) { CheckPylons(); Commit(); }
    SyncObjects(); SyncActors();
}
