/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_RFD_H
#define DEF_RFD_H

enum
{
    MAX_ENCOUNTER               = 2,
    MAX_WAVES                   = 3,
    MAX_COUNT_POS               = 2,

    TYPE_TUTEN_KASH             = 0,
    TYPE_IDOL_EVENT             = 1,

    NPC_TOMB_FIEND              = 7349,
    NPC_TOMB_REAVER             = 7351,
    NPC_TUTEN_KASH              = 7355,

    GO_GONG                     = 148917,
    GO_IDOL_BRAZIER             = 152097,
    GO_IDOL_FIRE_OVEN           = 151951,
    GO_IDOL_FIRE_CUP            = 151952,
    GO_IDOL_FIRE_MOUTH          = 151973,
};

static const Position aSpawnLocations[MAX_COUNT_POS] =
{
    {2484.83f, 811.11f, 43.40f, 1.67f},                 // Right corridor
    {2546.03f, 902.77f, 47.16f, 5.04f},                 // Left corridor
};

struct RFDSummonInformation
{
    uint32 m_uiNpcEntry;
    uint8 m_uiNPCperWave;
};

static const RFDSummonInformation aWaveSummonInformation[] =
{
    {NPC_TOMB_FIEND,  8},
    {NPC_TOMB_REAVER, 4},
    {NPC_TUTEN_KASH,  1}
};

class instance_razorfen_downs : public ScriptedInstance
{
    public:
        instance_razorfen_downs(Map* pMap);
        ~instance_razorfen_downs() {}

        void Initialize() override;

        void OnCreatureCreate(Creature* pCreature) override;
        void OnObjectCreate(GameObject* pGo) override;
        void OnCreatureDeath(Creature* pCreature) override;
        void OnCreatureEvade(Creature* pCreature) override;
        void OnCreatureDespawn(Creature* pCreature) override;
        void OnObjectSpawn(GameObject* pGo) override;
        void Update(uint32 diff) override;

        void SetData(uint32 uiType, uint32 uiData) override;
        uint32 GetData(uint32 uiType) const override;

        const char* Save() const override { return m_strInstData.c_str(); }
        void Load(const char* chrIn) override;

        void DoSpawnWaveIfCan(GameObject* pGo);
        void CompleteIdolEvent();

    protected:
        uint32 m_auiEncounter[MAX_ENCOUNTER];
        std::string m_strInstData;

        uint8 m_uiWaveCounter;

        GuidSet m_activeWaveGuids;
        GuidSet m_idolFireGuids;
        uint8 m_activeWaveIndex;
        uint8 m_retryWaveMembers;
        bool m_creatingWave;
        bool m_cleaningWave;
        bool m_pendingWaveReset;
        bool m_gongBossKilled;
        uint64 m_idolCompletedAt;
        uint32 m_worldStateTimer;

        void SaveState();
        void SyncGong(GameObject* go = nullptr);
        void AbortWave();
        void ApplyIdolState(GameObject* go);
        void UpdateIdolObjects();
};

#endif
