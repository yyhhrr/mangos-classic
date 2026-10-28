/* This file is part of the ScriptDev2 Project. See AUTHORS for copyright.
 * This program is free software licensed under GPL version 2. */
#ifndef BLACKROCK_SPIRE_LOWER_H
#define BLACKROCK_SPIRE_LOWER_H

#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <array>
#include <functional>
#include <string>

class Creature;
class GameObject;
class Object;
class Player;
class ScriptedInstance;
struct SpellEntry;

// Lower Spire actors share map229 with upper events. This coordinator owns only
// its acknowledged summons; combat remains in the existing CreatureAI scripts.
class BlackrockSpireLower
{
public:
    BlackrockSpireLower(ScriptedInstance& owner, std::function<void()> saveCallback);
    void OnCreatureCreate(Creature* creature);
    void OnCreatureRespawn(Creature* creature);
    void OnCreatureDeath(Creature* creature);
    void OnCreatureEvade(Creature* creature);
    void OnCreatureDespawn(Creature* creature);
    void OnObjectCreate(GameObject* object);
    void OnPlayerEnter(Player* player);
    void OnPlayerLeave(Player* player);
    void Update(uint32 diff);
    bool StartUrok(Object* source, Object* target);
    // nullptr spell is the instance's verified EventAI action35 request from16044.
    bool HandleGrayhoofSpellHit(Creature* trigger, SpellEntry const* spell);
    std::string Save() const;
    static bool ValidateSave(std::string const& text);
    bool Load(std::string const& text);

private:
    enum ActorState { EMPTY, PENDING, ALIVE, KILLED, EXPIRED };
    enum { UROK_SLOTS = 11, GIZRUL_SLOT = 11, GRAYHOOF_SLOT = 12, ACTOR_SLOTS = 13 };
    struct Actor
    {
        uint32 state = EMPTY;
        uint64 until = 0;
        ObjectGuid guid;
        uint8 attempts = 0;
        uint64 nextAttempt = 0;
    };
    struct Progress
    {
        uint32 urok = 0;
        uint32 urokElapsed = 0;
        uint32 urokGeneration = 0;
        bool halyconDead = false;
        uint32 grayhoofGeneration = 0;
        uint32 objects = 0;
        std::array<uint64, 7> objectUntil{};
        std::array<Actor, ACTOR_SLOTS> actors{};
    };

    static bool Decode(std::string const&, Progress&);
    static uint64 Now();
    void Commit();
    void ClearLiving(uint32 first, uint32 last);
    void FailUrok();
    void SyncUrokObjects();
    bool Spawn(uint32 slot);
    void ProcessActors();
    void Pause();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    std::array<ObjectGuid, 7> m_objects{};
    ObjectGuid m_pile;
    ObjectGuid m_grayTrigger;
    uint32 m_retryTimer = 0;
    uint32 m_checkpointTimer = 5000;
    bool m_paused = false;
};
#endif
