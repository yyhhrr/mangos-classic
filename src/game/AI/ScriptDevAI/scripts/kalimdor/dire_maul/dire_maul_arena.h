/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#ifndef DIRE_MAUL_ARENA_H
#define DIRE_MAUL_ARENA_H
#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <deque>
#include <functional>
#include <string>

class Map;
class Creature;
class Player;
class ScriptedInstance;

enum DireMaulArenaIdentity
{
    DMA_MAP = 1, DMA_SPAWN_GUID = 1321701, DMA_GROUP = 1321701,
    DMA_MUSHGOG = 11447, DMA_RAZZA = 11497, DMA_SKARR = 11498,
    DMA_SPECTATOR = 14395, DMA_SPECTATOR_GUID = 50061
};

// Map1 outdoor scheduling. The single static DB identity retains native corpse,
// loot and creature_respawn handling; selected entry survives world-map reload.
class DireMaulArena
{
public:
    DireMaulArena(ScriptedInstance&, std::function<void()> saveCallback);
    std::string Save() const;
    static bool ValidateSave(std::string const&);
    bool Load(std::string const&);
    void OnCreatureCreate(Creature*);
    void OnCreatureRespawn(Creature*);
    void OnCreatureDeath(Creature*);
    void OnCreatureDespawn(Creature*);
    void OnPlayerEnter(Player*);
    void OnPlayerLeave(Player*);
    void Update(uint32 diff);
    bool IsCurrentBoss(Creature*) const;
    Creature* GetBoss() const;
    uint32 GetGeneration() const;
    uint32 GetSelectedEntry() const;
    uint64 GetNextSpawnTime() const;
    void BossEnterCombat(Creature*);
    void BossKilledPlayer(Creature*, Player*);
    void BossEvaded(Creature*);
    void BossDied(Creature*);
    void SpectatorUpdate(Creature*, uint32 diff);
private:
    enum Phase { EMPTY, REQUESTED, ALIVE, COOLDOWN };
    struct State
    {
        uint32 phase = EMPTY, entry = 0, generation = 0, spawnSaid = 0;
        uint64 next = 0;
    };
    struct Notice { uint32 kind, entry, generation; uint64 expires; };
    static uint64 Now();
    static bool Decode(std::string const&, State&);
    static std::string Encode(State const&);
    static bool ArenaEntry(uint32);
    void Commit();
    bool Ready();
    bool LocalGridLoaded() const;
    bool ObserveBoss();
    void ReconcileCooldown();
    void TrySpawn();
    void QueueNotice(uint32 kind);
    void DropNotices();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    State m_state;
    ObjectGuid m_actor, m_spectator;
    std::deque<Notice> m_notices;
    uint64 m_retryAt = 0, m_noticeAt = 0;
    uint32 m_attempts = 0;
    bool m_spawning = false, m_ready = false, m_combat = false, m_deathHandling = false, m_valid = true;
};

// Defined by the existing world_map_kalimdor script; no new global singleton.
DireMaulArena* GetDireMaulArena(Map*);
void AddSC_dire_maul_arena_creatures();
#endif
