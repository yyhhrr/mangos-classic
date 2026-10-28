/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#ifndef DIRE_MAUL_WEST_H
#define DIRE_MAUL_WEST_H

#include "dire_maul_ritual.h"
#include <array>
#include <functional>
#include <map>
#include <string>

class DireMaulWest
{
public:
    DireMaulWest(ScriptedInstance& owner, std::function<void()> save);
    uint32 GetData(uint32 type) const;
    void SetData(uint32 type, uint32 value);
    std::string Save() const;
    static bool ValidateSave(std::array<uint32, 9> const& states, std::string const& text);
    bool Load(std::array<uint32, 9> const& states, std::string const& text);
    void OnPlayerEnter(Player* player);
    void OnPlayerLeave(Player* player);
    void OnCreatureCreate(Creature* actor);
    void OnCreatureRespawn(Creature* actor);
    void OnCreatureDeath(Creature* actor);
    void OnCreatureEvade(Creature* actor);
    void OnCreatureDespawn(Creature* actor);
    void OnCreatureEnterCombat(Creature* actor);
    void OnObjectCreate(GameObject* object);
    void Update(uint32 diff);
    DireMaulRitual& Ritual() { return m_ritual; }
    DireMaulRitual const& Ritual() const { return m_ritual; }

private:
    struct Progress
    {
        std::array<uint32, 8> encounters{};
        uint32 route = 0; // North entrance observed / West entrance observed.
        uint32 releaseSaid = 0;
        uint64 guardsDead = 0;
    };
    static bool Decode(std::array<uint32, 9> const&, std::string const&, Progress&, std::string& ritual);
    bool Current(Creature*) const;
    int32 GuardIndex(Creature*) const;
    bool AllPylonsDone() const;
    void CheckPylons();
    void SyncObjects();
    void SyncActors();
    void Commit();
    ScriptedInstance& m_owner;
    std::function<void()> m_save;
    Progress m_progress;
    DireMaulRitual m_ritual;
    std::array<ObjectGuid, 40> m_guards{};
    std::array<ObjectGuid, 18> m_summoners{};
    std::map<uint32, ObjectGuid> m_objects;
    ObjectGuid m_prince, m_immolthar;
    uint32 m_syncTimer = 0;
};
#endif
