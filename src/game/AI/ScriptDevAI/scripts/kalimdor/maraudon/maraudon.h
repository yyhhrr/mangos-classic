/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_MAR_H
#define DEF_MAR_H

enum
{
    MAX_ENCOUNTER               = 3,

    TYPE_NOXXION                = 0,
    TYPE_LARVA_SPEWER           = 1,
    TYPE_CELEBRAS               = 2,

    NPC_NOXXION                 = 13282,
    NPC_SPEWED_LARVA            = 13533,
    NPC_CELEBRAS                = 12225,
    NPC_CELEBRAS_REDEEMED       = 13716,

    GO_LARVA_SPEWER             = 178559,
    GO_CORRUPTION_SPEWER        = 178570,
    GO_CELEBRAS_BOOK            = 178965,
    GO_CELEBRAS_AURA            = 178964,
    GO_STAFF_CREATOR            = 178560,
};

static const Position spawnLocation = {937.213f, -377.967f, -50.346f, 2.578f};

class instance_maraudon : public ScriptedInstance
{
    public:
        instance_maraudon(Map* map);
        ~instance_maraudon() {}

        void Initialize() override;

        void OnObjectCreate(GameObject* go) override;
        void OnObjectSpawn(GameObject* go) override;
        void OnCreatureCreate(Creature* creature) override;
        void OnCreatureRespawn(Creature* creature) override;
        void OnCreatureDeath(Creature* creature) override;
        void OnCreatureEvade(Creature* creature) override;

        void SetData(uint32 type, uint32 data) override;
        uint32 GetData(uint32 type) const override;

        const char* Save() const override { return m_strInstData.c_str(); }
        void Load(const char* chrIn) override;

        void Update(const uint32 diff) override;
        void CleanupRitualObjects();

    protected:
        uint32 m_encounter[MAX_ENCOUNTER];
        uint32 m_spewLarvaTimer;
        uint32 m_recoveryTimer;
        uint64 m_celebrasExpiry;
        std::string m_strInstData;
        void ApplySpewerState(GameObject* go);
        void UpdateCelebrasVisibility(Creature* celebras = nullptr);
        void StoreProgress();
};


#endif
