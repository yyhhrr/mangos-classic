/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#ifndef BLACKROCK_SPIRE_UPPER_H
#define BLACKROCK_SPIRE_UPPER_H
#include "Common.h"
#include "Entities/ObjectGuid.h"
#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>
class Creature;
class GameObject;
class Player;
class ScriptedInstance;

class BlackrockSpireUpper
{
public:
    BlackrockSpireUpper(ScriptedInstance&, std::function<void()> saveCallback);
    void OnCreatureCreate(Creature*);
    void OnCreatureRespawn(Creature*);
    void OnCreatureDeath(Creature*);
    void OnCreatureEvade(Creature*);
    void OnCreatureDespawn(Creature*);
    void OnCreatureEnterCombat(Creature*);
    void OnObjectCreate(GameObject*);
    void OnPlayerEnter(Player*);
    void OnPlayerLeave(Player*);
    void Update(uint32);
    bool StartStadium(Player*);
    bool StartRookery(Player*);
    bool TryUnlockUpper(Player*);
    bool StartEmberseer(Player*);
    bool RequestValthalak(Creature*);
    bool ConfirmStadiumRend(Creature* gyth, Creature* rend);
    bool IsStadiumGyth(Creature*) const;
    bool WasStadiumRendReleased(Creature*) const;
    void FailStadium();
    void FailEmberseer();
    void EmberseerReleased(Creature*);
    bool IsEmberseerGrowing() const;
    bool IsEmberseerReleased() const;
    bool ShouldEmberseerBeCaged() const;
    uint32 GetEmberseerGeneration() const;
    uint32 GetEmberseerGrowingStacks() const;
    void EmberseerGrowingTick(Creature*, uint32);
    uint32 GetEmberseerReleaseStep() const;
    void EmberseerReleaseStep(Creature*, uint32);
    void SetEmberseerRunes(bool reset = false);
    void GetIncarceratorGUIDList(GuidList&) const;
    uint32 GetData(uint32) const;
    void SetData(uint32, uint32);
    bool GetEventResetData(uint32, std::string&) const;
    bool ApplyEventReset(uint32, std::string const&);
    std::string Save() const;
    static bool ValidateSave(std::array<uint32, 6> const& prefix, std::string const& text);
    bool Load(std::array<uint32, 6> const& prefix, std::string const& text);

private:
    enum Kind { INTRO_NEF = 1, INTRO_REND, SPECTATOR, STADIUM_WAVE, GYTH, COMBAT_REND, ROOKERY, SOLAKAR, VALTHALAK, VALTH_MINION };
    enum State { PENDING, ALIVE, KILLED, EXPIRED };
    struct Actor
    {
        uint32 kind = 0, key = 0, entry = 0, state = PENDING;
        std::array<float, 4> position{};
        std::array<float, 3> destination{};
        uint64 until = 0;
        ObjectGuid guid;
        uint32 attempts = 0;
        uint64 retry = 0;
    };
    struct Progress
    {
        std::array<uint32, 6> encounter{};
        uint32 opened = 0, unlockRequested = 0, unlockStep = 0, unlockTimer = 0, runes = 0;
        uint32 emberStage = 0, emberGeneration = 0, emberTimer = 0, emberTicks = 0, emberReleaseStep = 0;
        uint32 stadiumGeneration = 0, stadiumPhase = 0, stadiumTimer = 0, stadiumWave = 0;
        uint64 stadiumGateUntil = 0;
        uint32 stadiumReset = 0;
        uint32 rookeryGeneration = 0, rookeryWave = 0, rookeryTimer = 0;
        uint32 valthGeneration = 0;
        std::vector<Actor> actors;
    };
    static bool Decode(std::array<uint32, 6> const&, std::string const&, Progress&);
    static std::string Encode(Progress const&);
    static uint64 Now();
    static bool IsStadium(uint32 kind);
    static bool IsRookery(uint32 kind);
    Actor* Find(uint32 kind, uint32 key = 0);
    Actor const* Find(uint32 kind, uint32 key = 0) const;
    void Queue(uint32 kind, uint32 key, uint32 entry, std::array<float, 4> position);
    bool Spawn(Actor&);
    void Clear(uint32 scene, bool removeSlots);
    void FailRookery();
    void FailValthalak();
    void Commit();
    void SyncObjects();
    void RestoreStadiumDoor(GameObject*);
    void ProcessActors(uint32 diff);
    void UpdateStadium(uint32 diff);
    void UpdateRookery(uint32 diff);
    void UpdateEmberseer(uint32 diff);
    void SendWave();
    void Say(int32 text, uint32 kind);
    void Move(uint32 kind, uint32 position);
    uint32 RemainingStadium() const;
    void Pause();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    std::map<uint32, ObjectGuid> m_objects, m_staticActors;
    GuidSet m_minions;
    GuidSet m_retiringStadium;
    GuidSet m_encagePending;
    ObjectGuid m_emberseer, m_valthTrigger;
    uint32 m_checkpoint = 5000, m_emberAttempts = 0, m_unlockAttempts = 0;
    uint32 m_spawningKind = 0;
    bool m_paused = false, m_emberStartPending = false, m_emberResetPending = false, m_emberRestorePending = false;
};
#endif
