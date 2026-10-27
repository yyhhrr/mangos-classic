/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_ULDAMAN_H
#define DEF_ULDAMAN_H

#include "World/WorldStateDefines.h"

enum
{
    MAX_ENCOUNTER               = 3,
    TYPE_ALTAR_EVENT            = 1,
    TYPE_ARCHAEDAS              = 2,
    DATA_EVENT_STARTER          = 3,
    TYPE_IRONAYA                = 4,

    GO_TEMPLE_DOOR_UPPER        = 124367,
    GO_TEMPLE_DOOR_LOWER        = 141869,
    GO_ANCIENT_VAULT            = 124369,
    GO_ANCIENT_TREASURE         = 141979,
    GO_KEYSTONE                = 124371,
    GO_IRONAYA_SEAL             = 124372,

    NPC_ARCHAEDAS               = 2748,
    NPC_CUSTODIAN               = 7309,
    NPC_HALLSHAPER              = 7077,
    NPC_GUARDIAN                = 7076,
    NPC_VAULT_WARDER            = 10120,
    NPC_STONE_KEEPER            = 4857,
    NPC_IRONAYA                 = 7228,
    NPC_ANNORA_SCORPID          = 7078,
    GROUP_ANNORA_SCORPIDS       = 700000,
    MAX_STONE_KEEPERS           = 4,

    SPELL_STONED               = 10255,
    SPELL_FREEZE_ANIM          = 16245,
    EVENT_ID_ALTAR_KEEPER      = 2228,
    EVENT_ID_ALTAR_ARCHAEDAS   = 2268,

    TREASURE_UNAVAILABLE       = 0,
    TREASURE_AVAILABLE         = 1,
    TREASURE_CLOSED            = 2
};

class instance_uldaman : public ScriptedInstance
{
    public:
        explicit instance_uldaman(Map* map);
        void Initialize() override;
        void OnObjectCreate(GameObject* go) override;
        void OnObjectSpawn(GameObject* go) override;
        void OnObjectDespawn(GameObject* go) override;
        void OnCreatureCreate(Creature* creature) override;
        void OnCreatureRespawn(Creature* creature) override;
        void OnCreatureDeath(Creature* creature) override;
        void OnCreatureEvade(Creature* creature) override;
        void OnCreatureDespawn(Creature* creature) override;
        void OnCreatureGroupDespawn(CreatureGroup* group, Creature* creature) override;
        void Update(uint32 diff) override;
        void SetData(uint32 type, uint32 state) override;
        void SetData64(uint32 type, uint64 guid) override;
        uint32 GetData(uint32 type) const override;
        uint64 GetData64(uint32 type) const override;
        bool GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const override;
        bool GetGameObjectLootSnapshot(uint32 entry, std::string& snapshot) const override;
        void OnGameObjectLootSnapshot(uint32 entry, std::string const& snapshot, bool exhausted) override;
        void StartEvent(uint32 eventId, Player* player);
        bool StartIronayaEvent(GameObject* keystone);
        Player* SelectEventTarget(Creature* searcher, float maxDistance = 50.0f) const;
        Creature* GetClosestDwarfNotInCombat(Creature* searcher);
        const char* Save() const override { return m_strInstData.c_str(); }
        void Load(const char* input) override;

    protected:
        void BuildSaveData();
        void SaveState();
        void SyncObjects();
        void SyncObject(GameObject* go);
        void DoResetKeeperEvent();
        void CheckAnnoraUnlock();
        void RestoreAnnoraKills();
        void UpdateIronaya(uint32 diff);
        Creature* SummonIronaya(bool startMoving);
        void CloseTreasure();

        uint32 m_auiEncounter[MAX_ENCOUNTER];
        std::string m_strInstData;
        ObjectGuid m_playerGuid;
        GuidSet m_lWardens;
        GuidSet m_lKeepers;
        GuidSet m_deadKeepers;
        ObjectGuid m_activeKeeper;
        uint32 m_uiKeeperCooldown;
        bool m_resettingKeepers;
        bool m_pendingKeeperReset;
        bool m_annoraUnlocked;
        std::set<uint32> m_annoraScorpionDbGuids;
        std::set<uint32> m_annoraDeadScorpionDbGuids;
        ObjectGuid m_ironayaGuid;
        uint8 m_ironayaStage;
        uint8 m_ironayaSummonFailures;
        uint32 m_ironayaTimer;
        bool m_restoreIronaya;
        uint32 m_worldStateTimer;
        uint32 m_treasureState;
        uint64 m_treasureExpires;
        std::string m_treasureSnapshot;
};

#endif
