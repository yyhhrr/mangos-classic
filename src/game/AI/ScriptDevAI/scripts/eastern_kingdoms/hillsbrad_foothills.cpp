/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) any later version.
 */

#include "AI/ScriptDevAI/include/sc_common.h"

enum
{
    QUEST_HELCULARS_REVENGE = 553,
    NPC_HELCULARS_REMAINS = 2433,
    HELCULAR_INITIAL_DELAY = 2000,
    HELCULAR_RETRY_DELAY = 1000,
    HELCULAR_MAX_ATTEMPTS = 3,
    HELCULAR_DESPAWN_DELAY = 600000
};

// Own the actor by GUID, including while he is pulled away from the grave.
// Keeping the grave active preserves that ownership across normal grid unloading.
struct go_helculars_graveAI : public GameObjectAI
{
    explicit go_helculars_graveAI(GameObject* go) : GameObjectAI(go),
        m_summonTimer(0), m_summonAttempts(0), m_originalActive(false) {}

    ObjectGuid m_helcularGuid;
    uint32 m_summonTimer;
    uint32 m_summonAttempts;
    bool m_originalActive;

    bool HasLivingHelcular() const
    {
        if (!m_helcularGuid)
            return false;
        if (Creature* helcular = m_go->GetMap()->GetCreature(m_helcularGuid))
            return helcular->IsAlive();
        return false;
    }

    void FinishEvent()
    {
        m_helcularGuid.Clear();
        m_summonTimer = 0;
        m_summonAttempts = 0;
        m_go->SetActiveObjectState(m_originalActive);
    }

    void StartEvent()
    {
        if (m_summonTimer || HasLivingHelcular())
            return;

        // A second reward can arrive before UpdateAI observes the old actor's death.
        if (m_helcularGuid)
            FinishEvent();

        m_originalActive = m_go->HasActiveObjectState();
        m_go->SetActiveObjectState(true);
        m_summonAttempts = 0;
        m_summonTimer = HELCULAR_INITIAL_DELAY;
    }

    void JustSpawned() override
    {
        // Visibility/respawn notifications must not reset a pending or living actor.
        if (m_summonTimer || HasLivingHelcular())
            m_go->SetActiveObjectState(true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_helcularGuid)
        {
            if (!HasLivingHelcular())
                FinishEvent();
            return;
        }

        if (!m_summonTimer)
            return;
        if (m_summonTimer > diff)
        {
            m_summonTimer -= diff;
            return;
        }

        m_summonTimer = 0;
        ++m_summonAttempts;
        // The template starts path 0 once in the native Summon/MMGen initialization.
        // Do not also call MoveWaypoint here or change the existing combat AI.
        if (Creature* helcular = m_go->SummonCreature(NPC_HELCULARS_REMAINS,
                -747.107f, -586.97f, 19.8435f, 3.17531f,
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, HELCULAR_DESPAWN_DELAY))
        {
            m_helcularGuid = helcular->GetObjectGuid();
            return;
        }

        if (m_summonAttempts < HELCULAR_MAX_ATTEMPTS)
            m_summonTimer = HELCULAR_RETRY_DELAY;
        else
            FinishEvent();
    }
};

bool QuestRewarded_go_helculars_grave(Player* /*player*/, GameObject* go, Quest const* quest)
{
    if (quest->GetQuestId() != QUEST_HELCULARS_REVENGE)
        return false;

    if (go_helculars_graveAI* grave = dynamic_cast<go_helculars_graveAI*>(go->AI()))
        grave->StartEvent();

    // Player::RewardQuest already granted this player's reward. Handling the hook
    // suppresses CompleteScript 553, so it cannot schedule a second summon.
    return true;
}

void AddSC_hillsbrad_foothills()
{
    Script* script = new Script;
    script->Name = "go_helculars_grave";
    script->GetGameObjectAI = &GetNewAIInstance<go_helculars_graveAI>;
    script->pQuestRewardedGO = &QuestRewarded_go_helculars_grave;
    script->RegisterSelf();
}
