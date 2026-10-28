/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#ifndef DIRE_MAUL_EAST_H
#define DIRE_MAUL_EAST_H
#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <array>
#include <functional>
#include <string>
class Creature;
class GameObject;
class Player;
class ScriptedInstance;

// East owns its scenes and acknowledged summons. The shared instance retains
// the original nineteen-field prefix and all North/West reward behaviour.
class DireMaulEast
{
public:
    struct SceneState { uint32 stage = 0, node = 0, timer = 0; };
    DireMaulEast(ScriptedInstance&, std::function<void()> saveCallback);
    void OnCreatureCreate(Creature*);
    void OnCreatureRespawn(Creature*);
    void OnCreatureDeath(Creature*);
    void OnCreatureEvade(Creature*);
    void OnCreatureDespawn(Creature*);
    void OnObjectCreate(GameObject*);
    void OnPlayerEnter(Player*);
    void OnPlayerLeave(Player*);
    void Update(uint32);
    uint32 GetData(uint32) const;
    void SetData(uint32, uint32);
    SceneState GetIronbarkState() const;
    SceneState GetPusillinState() const;
    bool SetIronbarkState(Creature*, SceneState const&);
    bool SetPusillinState(Creature*, SceneState const&);
    bool IsIronbarkRedeemed() const;
    bool OpenIronbarkDoor(Creature*);
    bool IsAlzzinWaveStarted() const;
    void BeginAlzzinWave(Creature*);
    void AlzzinEvaded(Creature*);
    void AlzzinDied(Creature*);
    void QueuePusillinAdds(Creature*);
    bool ArePusillinAddsReady() const;
    bool PusillinAddsFailed() const;
    void RetryPusillinAdds(Creature*);
    bool IsPusillinTransformed() const;
    void PusillinTransformed(Creature*);
    bool IsPusillinDead() const;
    void PusillinDied(Creature*);
    bool RequestIsalien(Creature*);
    void FelvineLootStateChanged(GameObject*);
    bool GetFelvineLootSnapshot(ObjectGuid const&, uint32 entry, std::string&) const;
    void OnFelvineLootSnapshot(ObjectGuid const&, uint32 entry, std::string const&, bool exhausted);
    std::string Save() const;
    static bool ValidateSave(std::array<uint32, 3> const&, std::string const&);
    bool Load(std::array<uint32, 3> const&, std::string const&);
private:
    enum State { EMPTY, PENDING, ALIVE, KILLED, EXPIRED };
    enum { ALZZIN_FIRST = 0, ALZZIN_END = 15, PUSILLIN_FIRST = 15, PUSILLIN_END = 19, ISALIEN_SLOT = 19, ACTORS = 20 };
    struct Actor
    {
        uint32 state = EMPTY;
        uint64 until = 0;
        ObjectGuid guid;
        uint32 attempts = 0;
        uint64 retry = 0;
    };
    struct Progress
    {
        std::array<uint32, 3> encounters{};
        SceneState iron, pusillin;
        uint32 wall = 0, alzzinWave = 0, alzzinGeneration = 0, redemptionSaid = 0;
        uint32 pusillinRequested = 0, pusillinTransformed = 0, pusillinDead = 0, pusillinGeneration = 0;
        uint32 isalienGeneration = 0;
        uint64 shardUntil = 0;
        uint32 shardMask = 0, shardOpened = 0;
        std::array<uint64, 5> shardChestUntil{};
        std::array<std::string, 5> shardLoot{};
        std::array<Actor, ACTORS> actors{};
    };
    static uint64 Now();
    static bool Decode(std::array<uint32, 3> const&, std::string const&, Progress&);
    static std::string Encode(Progress const&);
    bool Current(Creature*, ObjectGuid, uint32 entry) const;
    bool CurrentIronbark(Creature*) const;
    void Commit();
    void SyncObjects();
    void SyncIronbark();
    int32 ShardIndex(ObjectGuid const&, uint32 entry) const;
    bool ShardAvailable(uint32) const;
    bool ObserveShards();
    void CloseShard(uint32);
    void ClearLiving(uint32, uint32, bool closeSlots);
    void ResetAlzzinWave();
    void ProcessActors();
    bool Spawn(uint32);
    void Pause();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    ObjectGuid m_alzzin, m_ironbark, m_pusillin, m_isalienTrigger;
    std::array<ObjectGuid, 3> m_doors{};
    std::array<ObjectGuid, 5> m_shards{};
    uint32 m_checkpoint = 5000, m_retry = 0;
    bool m_paused = false, m_syncingShards = false, m_alzzinFailurePending = false;
};
void AddSC_dire_maul_east_objects();
#endif
