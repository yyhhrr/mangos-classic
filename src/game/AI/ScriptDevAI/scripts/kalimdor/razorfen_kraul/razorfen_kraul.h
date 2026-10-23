/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_RFK_H
#define DEF_RFK_H

#include <set>

enum
{
    MAX_ENCOUNTER     = 1,

    TYPE_AGATHELOS    = 1,

    GO_AGATHELOS_WARD = 21099,

    NPC_AGATHELOS     = 4422,
    NPC_WARD_KEEPER   = 4625
};

class instance_razorfen_kraul : public ScriptedInstance
{
    public:
        instance_razorfen_kraul(Map* pMap);
        ~instance_razorfen_kraul() {}

        void Initialize() override;

        void OnObjectCreate(GameObject* pGo) override;
        void OnCreatureCreate(Creature* pCreature) override;
        void OnCreatureDeath(Creature* pCreature) override;
        void OnCreatureRespawn(Creature* pCreature) override;
        void OnCreatureEvade(Creature* pCreature) override;
        void Update(uint32 diff) override;

        void SetData(uint32 uiType, uint32 uiData) override;
        uint32 GetData(uint32 uiType) const override;

        const char* Save() const override { return m_strInstData.c_str(); }
        void Load(const char* chrIn) override;

    protected:
        void CheckWardKeepers();
        void ReleaseAgathelos();

        uint32 m_auiEncounter[MAX_ENCOUNTER];
        std::string m_strInstData;

        std::set<uint32> m_wardKeeperGuids;
        uint32 m_uiWardCheckTimer;
        bool m_agathelosReleased;
};
#endif
