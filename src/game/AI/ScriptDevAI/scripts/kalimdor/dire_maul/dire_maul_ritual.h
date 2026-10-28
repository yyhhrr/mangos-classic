/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#ifndef DIRE_MAUL_RITUAL_H
#define DIRE_MAUL_RITUAL_H
#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <array>
#include <functional>
#include <string>
class ScriptedInstance;
class Creature;
class GameObject;
class Player;

// CreatureAI owns the scene; this instance ledger owns its acknowledged actors
// and checkpoints. The enclosing West coordinator supplies the save callback.
class DireMaulRitual
{
public:
    DireMaulRitual(ScriptedInstance&, std::function<void()>);
    uint32 GetData() const;
    void SetData(uint32);
    std::string Save() const;
    static bool ValidateSave(uint32, std::string const&);
    bool Load(uint32, std::string const&);
    void OnCreatureCreate(Creature*);
    void OnCreatureRespawn(Creature*);
    void OnCreatureDeath(Creature*);
    void OnCreatureEvade(Creature*);
    void OnCreatureDespawn(Creature*);
    void OnObjectCreate(GameObject*);
    void OnPlayerEnter(Player*);
    void OnPlayerLeave(Player*);
    void Update(uint32);
    bool Start(Player*);
    bool RequestDreadsteed(Player*);
    void UpdateJeevee(Creature*, uint32);
    void JeeveeReached(Creature*, uint32 type, uint32 point);
    void UpdateController(Creature*, uint32);
    void UpdateHorse(Creature*);
    void HorseSummoned(Creature*, Creature*);
    void NodeUsed(GameObject*, Player*);
    void NodeStateChanged(GameObject*);
    void WaveMovement(Creature*, uint32 type, uint32 point);
    bool IsCurrentJeevee(Creature*) const;
    bool IsCurrentController(Creature*) const;
    bool IsCurrentHorse(Creature*) const;
private:
    enum { CONTROLLER = 0, JEEVEE = 1, HORSE = 2, LORD = 3, SPIRIT = 4, WAVE_FIRST = 5, ACTORS = 138 };
    enum ActorState { EMPTY, PENDING, ALIVE, KILLED, EXPIRED };
    enum Phase { IDLE, PREPARING, WAVES, WAIT_GLYPH, WAIT_HORSE, HORSE_ACTIVE, FINISHED, FAILED };
    struct Actor
    {
        uint32 state = EMPTY;
        uint64 until = 0;
        ObjectGuid guid;
        uint32 attempts = 0;
        uint64 retry = 0;
        std::array<uint32, 4> position{};
    };
    struct Progress
    {
        uint32 encounter = 0, legacy = 1, generation = 0, phase = IDLE;
        uint32 wave = 0, wavePending = 0, timer = 0, deactivate = 0, elapsed = 0, dialogue = 0;
        uint32 jeeveeNode = 0, jeeveeMoving = 0, jeeveeElapsed = 0, jeeveeActions = 0, jeeveeWatchdog = 0;
        uint32 born = 0, active = 0;
        std::array<uint64, 3> nodeUntil{};
        std::array<uint32, 3> pulse{};
        uint64 circleUntil = 0, portalUntil = 0, bossPortalUntil = 0;
        std::array<uint64, 9> symbolUntil{};
        std::array<Actor, ACTORS> actors{};
    };
    static uint64 Now();
    static bool Decode(uint32, std::string const&, Progress&);
    static std::string Encode(Progress const&);
    static uint32 Entry(uint32);
    static uint32 Lifetime(uint32);
    bool Current(Creature*, uint32) const;
    bool PlayerHere(Player*) const;
    Player* Participant() const;
    void Commit();
    void Pause();
    void ClearLiving(bool expire);
    void Fail();
    void ProcessActors();
    bool Spawn(uint32);
    void Acknowledge(uint32, Creature*);
    void SyncObjects();
    bool NodeAvailable(uint32) const;
    void AdvanceJeevee(Creature*);
    bool JeeveeAction(Creature*, uint32 bit, uint32 due, uint32 kind, uint32 argument);
    void FinishWaves(Creature*);
    void PulseNodes(uint32);
    int32 NodeIndex(GameObject*) const;
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    std::array<ObjectGuid, 3> m_nodes{};
    std::array<ObjectGuid, 9> m_symbols{};
    std::array<ObjectGuid, 18> m_portals{};
    ObjectGuid m_circle, m_candleTrap, m_bossPortal, m_lastUser;
    bool m_paused = false, m_syncing = false;
    uint32 m_checkpoint = 5000, m_sceneAttempts = 0;
};
#endif
