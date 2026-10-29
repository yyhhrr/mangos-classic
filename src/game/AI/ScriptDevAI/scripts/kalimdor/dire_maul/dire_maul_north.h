/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#ifndef DIRE_MAUL_NORTH_H
#define DIRE_MAUL_NORTH_H

#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <array>
#include <functional>
#include <string>

class Creature;
class GameObject;
class Player;
class ScriptedInstance;

enum DireMaulNorthScene
{
    DMN_MIZZLE_INITIAL = 0, DMN_MIZZLE_TO_KING = 1, DMN_MIZZLE_TO_KING_SAY = 2,
    DMN_MIZZLE_WAIT_TRIBUTE = 3, DMN_MIZZLE_TO_TRIBUTE = 4, DMN_MIZZLE_DONE = 5,
    DMN_KROM_IDLE = 0, DMN_KROM_ENRAGE = 1, DMN_KROM_WALKING = 2, DMN_KROM_DONE = 3,
    DMN_KNOT_IDLE = 0, DMN_KNOT_CACHE = 1, DMN_KNOT_CHAIN = 2, DMN_KNOT_RUN = 3,
    DMN_KNOT_MOVE = 4, DMN_KNOT_DESPAWN = 5, DMN_KNOT_AWAY = 6
};

// The shared instance owns the nineteen encounter fields' framing. North owns
// its seven fields, scene acknowledgements, and actual (already rolled) loot.
class DireMaulNorth
{
public:
    struct SceneState { uint32 stage = 0, node = 0, timer = 0; };
    DireMaulNorth(ScriptedInstance&, std::function<void()> saveCallback);
    uint32 GetData(uint32 type) const;
    void SetData(uint32 type, uint32 value);
    std::string Save() const;
    static bool ValidateSave(std::array<uint32, 7> const&, std::string const&);
    bool Load(std::array<uint32, 7> const&, std::string const&);
    std::string SaveForKingReset() const;
    void ResetKing();
    void OnPlayerEnter(Player*);
    void OnPlayerLeave(Player*);
    void OnCreatureCreate(Creature*);
    void OnCreatureRespawn(Creature*);
    void OnCreatureDeath(Creature*);
    void OnCreatureEvade(Creature*);
    void OnCreatureDespawn(Creature*);
    void OnCreatureEnterCombat(Creature*);
    void OnObjectCreate(GameObject*);
    void Update(uint32 diff);
    Creature* GetActor(uint32 entry) const;
    bool IsCurrentActor(Creature*) const;
    SceneState GetMizzleState() const;
    SceneState GetKromcrushState() const;
    SceneState GetKnotState() const;
    bool SetMizzleState(Creature*, SceneState const&);
    bool SetKromcrushState(Creature*, SceneState const&);
    bool SetKnotState(Creature*, SceneState const&);
    bool CanUseMizzle(Creature*) const;
    bool RequestTribute(Creature*);
    bool OpenTribute(Creature*);
    bool CanStartKromcrush(Creature*) const;
    bool BeginKromcrush(Creature*);
    bool CanStartKnot(Creature*) const;
    bool BeginKnot(Creature*);
    bool CompleteKnot(Creature*);
    bool FinishKnot(Creature*);
    bool IsKnotAway() const;
    uint32 KnotReturnRemainingMS() const;
    bool TrapRepaired(GameObject*);
    bool IsTrapActive() const;
    bool FreezeSlipkik(Creature*);
    bool ThawSlipkik(Creature*);
    bool IsSlipkikFrozen() const;
    bool IsSlipkikGordok() const;
    void LootStateChanged(GameObject*);
    bool GetLootSnapshot(ObjectGuid const&, uint32 entry, std::string&) const;
    void OnLootSnapshot(ObjectGuid const&, uint32 entry, std::string const&, bool exhausted);
private:
    struct Reward
    {
        uint32 state = 0; // 0 absent,1 requested,2 actual spawn acknowledged,3 closed.
        uint64 until = 0, chestUntil = 0;
        uint32 opened = 0;
        std::string loot;
    };
    struct Progress
    {
        std::array<uint32, 7> encounters{};
        SceneState mizzle, krom, knot;
        uint32 mizzleAcknowledged = 0, mizzleDead = 0, kingSaid = 0;
        uint32 knotGeneration = 0;
        uint64 knotReturn = 0, trapUntil = 0, chainUntil = 0;
        uint32 frozen = 0, gordokFaction = 0;
        std::array<Reward, 2> rewards{};
    };
    static uint64 Now();
    static bool Decode(std::array<uint32, 7> const&, std::string const&, Progress&);
    static std::string Encode(Progress const&);
    static void ClearKing(Progress&);
    bool SetScene(Creature*, SceneState const&, uint32 which);
    void Commit();
    void SyncActors();
    void SyncObjects();
    void ProcessMizzle();
    bool RequestReward(uint32 index);
    bool RewardAvailable(uint32 index) const;
    void CloseReward(uint32 index);
    int32 RewardIndex(ObjectGuid const&, uint32 entry) const;
    bool ObserveRewards();
    void ObserveTrap();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    std::array<ObjectGuid, 9> m_actors{};
    std::array<ObjectGuid, 2> m_rewards{};
    ObjectGuid m_trap, m_chain;
    uint32 m_retry = 0, m_attempts = 0, m_checkpoint = 5000;
    bool m_syncing = false, m_summoning = false, m_paused = false;
};

void AddSC_dire_maul_north_objects();
#endif
