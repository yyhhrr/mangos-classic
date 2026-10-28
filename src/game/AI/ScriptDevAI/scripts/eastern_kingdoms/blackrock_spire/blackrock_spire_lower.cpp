/* This file is part of the ScriptDev2 Project. See AUTHORS for copyright.
 * This program is free software licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_spire_lower.h"
#include <algorithm>
#include <ctime>
#include <sstream>

namespace
{
struct LowerSpawn { uint32 entry; uint32 delay; uint32 lifetime; Position position; };
// Same classic DB event4845 timings, coordinates and quantities. CreatureAI
// still owns each creature's combat and its existing waypoint route.
LowerSpawn const lowerSpawns[] =
{
    {10601, 5000, 432000, {-25.129999161f, -376.600006104f, 48.939998627f, 5.170000076f}},
    {10601, 5000, 432000, {-16.120000839f, -376.989990234f, 49.200000763f, 4.889999866f}},
    {10602, 5000, 432000, {-33.040000916f, -385.570007324f, 48.599998474f, 5.980000019f}},
    {10602, 13000, 432000, {-14.930000305f, -381.989990234f, 49.040000916f, 4.699999809f}},
    {10601, 28000, 432000, {-47.540000916f, -368.109985352f, 51.669998169f, 5.699999809f}},
    {10602, 28000, 432000, {-35.470001221f, -371.359985352f, 50.220001221f, 4.789999962f}},
    {10601, 42000, 432000, {-24.909999847f, -376.410003662f, 48.950000763f, 4.670000076f}},
    {10602, 42000, 432000, {-33.040000916f, -385.570007324f, 48.599998474f, 5.980000019f}},
    {10602, 42000, 432000, {-14.579999924f, -375.160003662f, 49.310001373f, 4.730000019f}},
    {10602, 58000, 432000, {-47.540000916f, -368.109985352f, 51.669998169f, 5.699999809f}},
    {10584, 66000, 432000, {-23.969999313f, -391.890014648f, 48.580001831f, 1.679999948f}},
    {10268, 0, 10800000, {-167.538f, -415.014f, 76.1469f, 1.56698f}},
    {16080, 0, 7200000, {-16.8468f, -454.377f, -18.6442f, 2.91211f}},
};
uint32 const objectDbGuids[7] = {2290295, 2290296, 2290301, 2290297, 2290298, 2290299, 2290300};
uint32 const objectDelays[7] = {0, 0, 0, 1000, 1000, 1000, 1000};
uint32 const objectLifetimes[7] = {65000, 65000, 7200000, 65000, 65000, 65000, 65000};
void DespawnAlive(Map* map, ObjectGuid guid)
{
    if (Creature* actor = map->GetAnyTypeCreature(guid))
        if (actor->IsAlive()) actor->ForcedDespawn();
}
}

BlackrockSpireLower::BlackrockSpireLower(ScriptedInstance& owner, std::function<void()> saveCallback)
    : m_owner(owner), m_save(std::move(saveCallback)) {}

uint64 BlackrockSpireLower::Now() { return uint64(time(nullptr)) * 1000; }
void BlackrockSpireLower::Commit() { if (m_save) m_save(); }

std::string BlackrockSpireLower::Save() const
{
    std::ostringstream out;
    out << "LBR1 " << m_progress.urok << ' ' << m_progress.urokElapsed << ' ' << m_progress.urokGeneration
        << ' ' << m_progress.halyconDead << ' ' << m_progress.grayhoofGeneration << ' ' << m_progress.objects;
    for (uint64 until : m_progress.objectUntil) out << ' ' << until;
    for (auto const& actor : m_progress.actors) out << ' ' << actor.state << ' ' << actor.until;
    return out.str();
}

bool BlackrockSpireLower::Load(std::string const& text)
{
    if (text.size() > 2048) return false;
    Progress next;
    if (!text.empty())
    {
        std::istringstream in(text);
        std::string version;
        uint32 halycon;
        if (!(in >> version >> next.urok >> next.urokElapsed >> next.urokGeneration >> halycon >> next.grayhoofGeneration >> next.objects) ||
            version != "LBR1" || halycon > 1 || next.urok > DONE || next.urokElapsed > 66000 ||
            next.urokGeneration > 1000000 || next.grayhoofGeneration > 1000000 || next.objects > 127) return false;
        next.halyconDead = halycon != 0;
        for (uint64& until : next.objectUntil) if (!(in >> until) || until > 4102444800000ULL) return false;
        for (uint32 index = 0; index < ACTOR_SLOTS; ++index)
        {
            auto& actor = next.actors[index];
            if (!(in >> actor.state >> actor.until) || actor.state > EXPIRED || actor.until > 4102444800000ULL ||
                ((actor.state == ALIVE || actor.state == KILLED) && !actor.until) ||
                ((actor.state == EMPTY || actor.state == PENDING) && actor.until) || actor.until > Now() + lowerSpawns[index].lifetime) return false;
        }
        in >> std::ws;
        if (!in.eof()) return false;
        if ((next.urok == NOT_STARTED) != (next.urokGeneration == 0)) return false;
        if (next.urok == NOT_STARTED && (next.urokElapsed || next.objects)) return false;
        if ((next.urok == DONE) != (next.actors[10].state == KILLED)) return false;
        if (next.halyconDead != (next.actors[GIZRUL_SLOT].state != EMPTY)) return false;
        if ((next.grayhoofGeneration != 0) != (next.actors[GRAYHOOF_SLOT].state != EMPTY)) return false;
        for (uint32 i = 0; i < UROK_SLOTS; ++i)
        {
            if (next.actors[i].state != EMPTY && next.urokElapsed < lowerSpawns[i].delay) return false;
            if (next.urok == IN_PROGRESS && next.actors[i].state == EXPIRED) return false;
            if (next.urok == NOT_STARTED && next.actors[i].state != EMPTY) return false;
            if ((next.urok == FAIL || next.urok == DONE) && (next.actors[i].state == PENDING || next.actors[i].state == ALIVE)) return false;
        }
        for (uint32 i = 0; i < 7; ++i)
            if ((!(next.objects & (1u << i)) && next.objectUntil[i]) ||
                ((next.objects & (1u << i)) && next.urokElapsed < objectDelays[i])) return false;
    }
    // Preserve runtime identities when an upper-spire event reset reloads the
    // same durable lower progress. Only changed generations detach actors.
    auto previous = m_progress;
    for (uint32 i = 0; i < ACTOR_SLOTS; ++i)
    {
        bool sameGeneration = i < UROK_SLOTS ? next.urokGeneration == previous.urokGeneration :
            i == GIZRUL_SLOT ? next.halyconDead == previous.halyconDead : next.grayhoofGeneration == previous.grayhoofGeneration;
        if (sameGeneration && next.actors[i].state == ALIVE && previous.actors[i].state == ALIVE)
            next.actors[i].guid = previous.actors[i].guid;
    }
    m_progress = next;
    m_retryTimer = 0;
    m_paused = false;
    for (uint32 i = 0; i < ACTOR_SLOTS; ++i)
        if (previous.actors[i].guid != next.actors[i].guid) DespawnAlive(m_owner.instance, previous.actors[i].guid);
    SyncUrokObjects();
    return true;
}

void BlackrockSpireLower::OnCreatureCreate(Creature* creature)
{
    if (creature->GetEntry() == 16044 && creature->HasStaticDBSpawnData()) m_grayTrigger = creature->GetObjectGuid();
}

void BlackrockSpireLower::OnCreatureRespawn(Creature* creature) { OnCreatureCreate(creature); }

void BlackrockSpireLower::OnObjectCreate(GameObject* object)
{
    if (object->GetEntry() == 175621) m_pile = object->GetObjectGuid();
    for (uint32 i = 0; i < 7; ++i)
        if (object->GetDbGuid() == objectDbGuids[i]) m_objects[i] = object->GetObjectGuid();
    SyncUrokObjects();
}

bool BlackrockSpireLower::StartUrok(Object* source, Object* target)
{
    if (!source || !target || source->GetTypeId() != TYPEID_PLAYER || target->GetTypeId() != TYPEID_GAMEOBJECT) return false;
    auto* player = static_cast<Player*>(source);
    auto* pile = static_cast<GameObject*>(target);
    GameObjectInfo const* focus = pile->GetGOInfo();
    if (!player->IsAlive() || player->GetMap() != m_owner.instance || pile->GetMap() != m_owner.instance ||
        pile->GetEntry() != 175621 || m_owner.instance->GetId() != 229 || !pile->IsSpawned() ||
        focus->type != GAMEOBJECT_TYPE_SPELL_FOCUS || focus->spellFocus.focusId != 743 ||
        !pile->IsWithinDistInMap(player, float(focus->spellFocus.dist)) || m_progress.urok == IN_PROGRESS) return false;
    ClearLiving(0, UROK_SLOTS);
    for (uint32 i = 0; i < UROK_SLOTS; ++i) m_progress.actors[i] = Actor();
    m_progress.urok = IN_PROGRESS;
    m_progress.urokElapsed = 0;
    ++m_progress.urokGeneration;
    m_progress.objects = 0;
    m_progress.objectUntil = {};
    m_pile = pile->GetObjectGuid();
    m_paused = false;
    m_retryTimer = 0;
    SyncUrokObjects();
    Commit();
    return true;
}

bool BlackrockSpireLower::HandleGrayhoofSpellHit(Creature* trigger, SpellEntry const* spell)
{
    if (!trigger || trigger->GetEntry() != 16044 || !trigger->HasStaticDBSpawnData() ||
        trigger->GetMap() != m_owner.instance || m_owner.instance->GetId() != 229 ||
        m_owner.instance->GetCreature(trigger->GetObjectGuid()) != trigger ||
        (spell && spell->Id != 27184 && spell->Id != 27203)) return false;
    auto& slot = m_progress.actors[GRAYHOOF_SLOT];
    if (Creature* current = m_owner.instance->GetCreature(slot.guid))
        if (current->IsAlive() && (current->IsInCombat() || current->GetCharmer()))
        {
            slot.until = Now() + lowerSpawns[GRAYHOOF_SLOT].lifetime;
            return false;
        }
    if (slot.state == PENDING || (slot.state == ALIVE && slot.until > Now())) return false;
    ObjectGuid old = slot.guid;
    slot = Actor();
    slot.state = PENDING;
    m_grayTrigger = trigger->GetObjectGuid();
    ++m_progress.grayhoofGeneration;
    DespawnAlive(m_owner.instance, old);
    Commit();
    Spawn(GRAYHOOF_SLOT);
    return true;
}

void BlackrockSpireLower::ClearLiving(uint32 first, uint32 last)
{
    std::array<ObjectGuid, ACTOR_SLOTS> detached{};
    for (uint32 i = first; i < last; ++i)
    {
        detached[i] = m_progress.actors[i].guid;
        m_progress.actors[i].guid.Clear();
    }
    for (ObjectGuid guid : detached) DespawnAlive(m_owner.instance, guid);
}

void BlackrockSpireLower::FailUrok()
{
    if (m_progress.urok != IN_PROGRESS) return;
    m_progress.urok = FAIL;
    ClearLiving(0, UROK_SLOTS);
    for (uint32 i = 0; i < UROK_SLOTS; ++i)
        if (m_progress.actors[i].state == ALIVE || m_progress.actors[i].state == PENDING) m_progress.actors[i].state = EXPIRED;
    m_progress.objectUntil = {};
    SyncUrokObjects();
    if (GameObject* pile = m_owner.instance->GetGameObject(m_pile))
    {
        pile->SetLootState(GO_READY);
        pile->Respawn();
    }
    Commit();
}

void BlackrockSpireLower::SyncUrokObjects()
{
    uint64 now = Now();
    for (uint32 i = 0; i < 7; ++i)
    {
        if (m_progress.urok == IN_PROGRESS && !(m_progress.objects & (1u << i)) && m_progress.urokElapsed >= objectDelays[i])
        {
            m_progress.objects |= 1u << i;
            m_progress.objectUntil[i] = now + objectLifetimes[i];
        }
        if (GameObject* object = m_owner.instance->GetGameObject(m_objects[i]))
        {
            bool active = m_progress.urok == IN_PROGRESS && m_progress.objectUntil[i] > now;
            if (active && !object->IsSpawned())
            {
                object->SetLootState(GO_READY);
                m_owner.DoRespawnGameObject(object->GetObjectGuid(), uint32((m_progress.objectUntil[i] - now + 999) / 1000));
            }
            else if (!active && m_progress.urok != NOT_STARTED && object->IsSpawned())
                object->ForcedDespawn();
        }
    }
}

bool BlackrockSpireLower::Spawn(uint32 index)
{
    auto& slot = m_progress.actors[index];
    if ((slot.state != PENDING && slot.state != ALIVE) || slot.guid || slot.attempts >= 5 || slot.nextAttempt > Now()) return false;
    if (slot.state == ALIVE && slot.until <= Now()) { slot.state = EXPIRED; Commit(); return false; }
    Player* player = m_owner.GetPlayerInMap(true, false);
    if (!player) return false;
    auto const& request = lowerSpawns[index];
    auto const& pos = request.position;
    WorldObject* summoner = player;
    if (index == GRAYHOOF_SLOT)
        if (Creature* trigger = m_owner.instance->GetCreature(m_grayTrigger)) summoner = trigger;
    ++slot.attempts;
    slot.nextAttempt = Now() + 1000;
    // The coordinator enforces the original OOC deadline itself, so recovery
    // keeps its remaining lifetime while later combat still resets the full TTL.
    if (Creature* actor = summoner->SummonCreature(request.entry, pos.x, pos.y, pos.z, pos.o, TEMPSPAWN_DEAD_DESPAWN, 0, true, index < UROK_SLOTS))
    {
        if (slot.state == PENDING) slot.until = Now() + request.lifetime;
        slot.state = ALIVE;
        slot.guid = actor->GetObjectGuid();
        slot.attempts = 0;
        slot.nextAttempt = 0;
        if (index == 10) m_progress.objectUntil[2] = 0; // Existing Urok spawn relay also removes the challenge.
        Commit();
        return true;
    }
    if (slot.attempts >= 5)
    {
        script_error_log("Lower Blackrock Spire: exhausted summon request for entry %u, slot %u", request.entry, index);
        if (index == GIZRUL_SLOT)
        {
            // Halycon cannot die again to issue another request. Keep a blocked
            // request; a new entrant or reconstruction gets a fresh
            // bounded retry budget, without reviving Halycon or earned loot.
            // A failed recovery of an ALIVE actor retains its original deadline.
            Commit();
        }
        else if (index < UROK_SLOTS) { slot.state = EXPIRED; FailUrok(); }
        else { slot.state = EXPIRED; Commit(); }
    }
    return false;
}

void BlackrockSpireLower::ProcessActors()
{
    bool changed = false;
    uint64 now = Now();
    for (uint32 i = 0; i < ACTOR_SLOTS; ++i)
    {
        auto& slot = m_progress.actors[i];
        if (slot.state != ALIVE && slot.state != PENDING) continue;
        if (slot.guid)
        {
            Creature* actor = m_owner.instance->GetCreature(slot.guid);
            if (actor && actor->IsAlive() && (actor->IsInCombat() || actor->GetCharmer()))
            {
                slot.until = now + lowerSpawns[i].lifetime;
                continue;
            }
            if (!actor || !actor->IsAlive() || slot.until <= now)
            {
                ObjectGuid guid = slot.guid;
                slot.guid.Clear();
                slot.state = EXPIRED;
                DespawnAlive(m_owner.instance, guid);
                changed = true;
                if (i < UROK_SLOTS) { FailUrok(); break; }
            }
            continue;
        }
        if (slot.state == ALIVE && slot.until <= now)
        {
            slot.state = EXPIRED;
            changed = true;
            if (i < UROK_SLOTS) { FailUrok(); break; }
        }
        else if (!m_retryTimer) Spawn(i);
    }
    if (changed) Commit();
}

void BlackrockSpireLower::OnCreatureDeath(Creature* creature)
{
    if (creature->GetEntry() == 10220 && creature->HasStaticDBSpawnData() && !m_progress.halyconDead)
    {
        m_progress.halyconDead = true;
        m_progress.actors[GIZRUL_SLOT].state = PENDING;
        Commit();
        Spawn(GIZRUL_SLOT);
    }
    for (uint32 i = 0; i < ACTOR_SLOTS; ++i)
        if (m_progress.actors[i].guid == creature->GetObjectGuid())
        {
            m_progress.actors[i].state = KILLED;
            m_progress.actors[i].guid.Clear();
            if (i == 10)
            {
                m_progress.urok = DONE;
                ClearLiving(0, UROK_SLOTS);
                for (uint32 j = 0; j < UROK_SLOTS; ++j)
                    if (m_progress.actors[j].state == ALIVE || m_progress.actors[j].state == PENDING) m_progress.actors[j].state = EXPIRED;
                m_progress.objectUntil = {};
                SyncUrokObjects();
            }
            Commit();
            return;
        }
}

void BlackrockSpireLower::OnCreatureEvade(Creature* creature)
{
    for (uint32 i = 0; i < UROK_SLOTS; ++i)
        if (m_progress.actors[i].guid == creature->GetObjectGuid()) { FailUrok(); return; }
    // Gizrul and Grayhoof retain their existing actor and normal OOC lifetime.
}

void BlackrockSpireLower::OnCreatureDespawn(Creature* creature)
{
    for (uint32 i = 0; i < ACTOR_SLOTS; ++i)
        if (m_progress.actors[i].guid == creature->GetObjectGuid())
        {
            m_progress.actors[i].guid.Clear();
            m_progress.actors[i].state = EXPIRED;
            if (i < UROK_SLOTS) FailUrok();
            else Commit();
            return;
        }
}

void BlackrockSpireLower::Pause()
{
    if (m_paused) return;
    m_paused = true;
    ClearLiving(0, ACTOR_SLOTS);
    Commit();
}

void BlackrockSpireLower::OnPlayerEnter(Player* /*player*/)
{
    m_paused = false;
    m_retryTimer = 0;
    for (auto& actor : m_progress.actors) { actor.attempts = 0; actor.nextAttempt = 0; }
    ProcessActors();
    SyncUrokObjects();
}

void BlackrockSpireLower::OnPlayerLeave(Player* player)
{
    for (auto const& ref : m_owner.instance->GetPlayers())
        if (Player* other = ref.getSource())
            if (other != player && !other->IsGameMaster()) { Commit(); return; }
    Pause();
}

void BlackrockSpireLower::Update(uint32 diff)
{
    if (!m_owner.GetPlayerInMap(true, false))
    {
        if (m_owner.GetPlayerInMap(false, false))
        {
            FailUrok();
            ProcessActors(); // Existing Gizrul/Grayhoof still age while players are dead.
        }
        else Pause();
        return;
    }
    m_paused = false;
    if (m_retryTimer > diff) m_retryTimer -= diff;
    else m_retryTimer = 0;
    if (m_progress.urok == IN_PROGRESS)
    {
        m_progress.urokElapsed += std::min(diff, 66000u - m_progress.urokElapsed);
        for (uint32 i = 0; i < UROK_SLOTS; ++i)
            if (m_progress.actors[i].state == EMPTY && m_progress.urokElapsed >= lowerSpawns[i].delay)
            {
                m_progress.actors[i].state = PENDING;
                Spawn(i);
            }
        SyncUrokObjects();
    }
    ProcessActors();
    if (!m_retryTimer) m_retryTimer = 1000;
    if (m_checkpointTimer > diff) m_checkpointTimer -= diff;
    else
    {
        if (m_progress.urok == IN_PROGRESS || m_progress.actors[GIZRUL_SLOT].state == ALIVE || m_progress.actors[GRAYHOOF_SLOT].state == ALIVE) Commit();
        m_checkpointTimer = 5000;
    }
}

