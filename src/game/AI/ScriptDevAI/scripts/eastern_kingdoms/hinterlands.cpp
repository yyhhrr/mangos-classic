/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* ScriptData
SDName: Hinterlands
SD%Complete: 100
SDComment: Quest support: 836, 2742
SDCategory: The Hinterlands
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_00x09hl
npc_rinji
spell_gammerita_turtle_camera - s.11610
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "world_eastern_kingdoms.h"
#include "Spells/Scripts/SpellScript.h"

/*######
## npc_00x09hl
######*/

enum
{
    SAY_OOX_START           = -1000287,
    SAY_OOX_AGGRO1          = -1000288,
    SAY_OOX_AGGRO2          = -1000289,
    SAY_OOX_AMBUSH          = -1000290,
    SAY_OOX_END             = -1000292,

    QUEST_RESQUE_OOX_09     = 836,

    NPC_MARAUDING_OWL       = 7808,
    NPC_VILE_AMBUSHER       = 7809
};

struct npc_00x09hlAI : public npc_escortAI
{
    npc_00x09hlAI(Creature* pCreature) : npc_escortAI(pCreature) { Reset(); }

    uint8 m_uiSummonCount;
    GuidList m_lSummonsList;

    void Reset() override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            DespawnSummons();
            m_uiSummonCount = 0;
            m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
        }
    }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 27:
                DoScriptText(SAY_OOX_AMBUSH, m_creature);
                break;
            case 44:
                DoScriptText(SAY_OOX_AMBUSH, m_creature);
                break;
            case 65:
                DoScriptText(SAY_OOX_END, m_creature);
                if (Player* pPlayer = GetPlayerForEscort())
                    pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_RESQUE_OOX_09, m_creature);
                break;
        }
    }

    void WaypointStart(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 28:
                if (m_uiSummonCount >= 3)
                    break;

                for (uint8 i = 0; i < 3; ++i)
                {
                    float fX, fY, fZ;
                    m_creature->GetRandomPoint(147.927444f, -3851.513428f, 130.893f, 7.0f, fX, fY, fZ);

                    m_creature->SummonCreature(NPC_MARAUDING_OWL, fX, fY, fZ, 0.0f, TEMPSPAWN_CORPSE_TIMED_DESPAWN, 25000);
                    ++m_uiSummonCount;
                }
                break;
            case 45:
                if (m_uiSummonCount >= 6)
                    break;

                for (uint8 i = 0; i < 3; ++i)
                {
                    float fX, fY, fZ;
                    m_creature->GetRandomPoint(-141.151581f, -4291.213867f, 120.130f, 7.0f, fX, fY, fZ);

                    m_creature->SummonCreature(NPC_VILE_AMBUSHER, fX, fY, fZ, 0.0f, TEMPSPAWN_CORPSE_TIMED_DESPAWN, 25000);
                    ++m_uiSummonCount;
                }
                break;
        }
    }

    void Aggro(Unit* pWho) override
    {
        if (pWho->GetEntry() == NPC_MARAUDING_OWL || pWho->GetEntry() == NPC_VILE_AMBUSHER)
            return;

        DoScriptText(urand(0, 1) ? SAY_OOX_AGGRO1 : SAY_OOX_AGGRO2, m_creature);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        pSummoned->GetMotionMaster()->MovePoint(0, m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());
        m_lSummonsList.push_back(pSummoned->GetObjectGuid());
    }

    void DespawnSummons()
    {
        GuidList summons;
        summons.swap(m_lSummonsList);

        for (GuidList::const_iterator itr = summons.begin(); itr != summons.end(); ++itr)
        {
            if (Creature* pSummoned = m_creature->GetMap()->GetCreature(*itr))
                if (pSummoned->GetSpawnerGuid() == m_creature->GetObjectGuid())
                    pSummoned->ForcedDespawn();
        }
    }

    void JustDied(Unit* pKiller) override
    {
        DespawnSummons();
        npc_escortAI::JustDied(pKiller);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        DespawnSummons();
        npc_escortAI::CorpseRemoved(respawnDelay);
    }
};

bool QuestAccept_npc_00x09hl(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_RESQUE_OOX_09)
    {
        DoScriptText(SAY_OOX_START, pCreature, pPlayer);
        pCreature->SetActiveObjectState(true);
        pCreature->SetStandState(UNIT_STAND_STATE_STAND);
        pCreature->SetFactionTemporary(FACTION_ESCORT_N_FRIEND_ACTIVE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_NPC);

        if (npc_00x09hlAI* pEscortAI = dynamic_cast<npc_00x09hlAI*>(pCreature->AI()))
            pEscortAI->Start(false, pPlayer, pQuest);
    }
    return true;
}

UnitAI* GetAI_npc_00x09hl(Creature* pCreature)
{
    return new npc_00x09hlAI(pCreature);
}

/*######
## npc_rinji
######*/

enum
{
    SAY_RIN_FREE            = -1000403,
    SAY_RIN_BY_OUTRUNNER    = -1000404,
    SAY_RIN_HELP_1          = -1000405,
    SAY_RIN_HELP_2          = -1000406,
    SAY_RIN_COMPLETE        = -1000407,
    SAY_RIN_PROGRESS_1      = -1000408,
    SAY_RIN_PROGRESS_2      = -1000409,

    QUEST_RINJI_TRAPPED     = 2742,
    NPC_RANGER              = 2694,
    NPC_OUTRUNNER           = 2691,
    GO_RINJI_CAGE           = 142036
};

Position m_afAmbushSpawn[] =
{
    {191.29620f, -2839.329346f, 107.388f, 0.f},
    {70.972466f, -2848.674805f, 109.459f, 0.f}
};

Position m_afAmbushMoveTo[] =
{
    {166.63038f, -2824.780273f, 108.153f, 0.f},
    {70.886589f, -2874.335449f, 116.675f, 0.f}
};

struct npc_rinjiAI : public npc_escortAI
{
    npc_rinjiAI(Creature* pCreature) : npc_escortAI(pCreature)
    {
        m_bIsByOutrunner = false;
        m_iSpawnId = 0;
        Reset();
    }

    bool m_bIsByOutrunner;
    uint32 m_uiPostEventCount;
    uint32 m_uiPostEventTimer;
    int m_iSpawnId;

    void Reset() override
    {
        m_uiPostEventCount = 0;
        m_uiPostEventTimer = 3000;
    }

    void JustRespawned() override
    {
        m_bIsByOutrunner = false;
        m_iSpawnId = 0;

        npc_escortAI::JustRespawned();
    }

    void Aggro(Unit* pWho) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING))
        {
            if (pWho->GetEntry() == NPC_OUTRUNNER && !m_bIsByOutrunner)
            {
                DoScriptText(SAY_RIN_BY_OUTRUNNER, pWho);
                m_bIsByOutrunner = true;
            }

            if (urand(0, 3))
                return;

            // only if attacked and escorter is not in combat?
            DoScriptText(urand(0, 1) ? SAY_RIN_HELP_1 : SAY_RIN_HELP_2, m_creature);
        }
    }

    void DoSpawnAmbush(bool bFirst)
    {
        if (!bFirst)
            m_iSpawnId = 1;

        m_creature->SummonCreature(NPC_RANGER,
                                   m_afAmbushSpawn[m_iSpawnId].x, m_afAmbushSpawn[m_iSpawnId].y, m_afAmbushSpawn[m_iSpawnId].z, 0.0f,
                                   TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN, 60000);

        for (int i = 0; i < 2; ++i)
        {
            m_creature->SummonCreature(NPC_OUTRUNNER,
                                       m_afAmbushSpawn[m_iSpawnId].x, m_afAmbushSpawn[m_iSpawnId].y, m_afAmbushSpawn[m_iSpawnId].z, 0.0f,
                                       TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN, 60000);
        }
    }

    void JustSummoned(Creature* pSummoned) override
    {
        pSummoned->SetWalk(false);
        pSummoned->GetMotionMaster()->MovePoint(0, m_afAmbushMoveTo[m_iSpawnId].x, m_afAmbushMoveTo[m_iSpawnId].y, m_afAmbushMoveTo[m_iSpawnId].z);
    }

    void WaypointReached(uint32 uiPointId) override
    {
        Player* pPlayer = GetPlayerForEscort();

        if (!pPlayer)
            return;

        switch (uiPointId)
        {
            case 2:
                DoScriptText(SAY_RIN_FREE, m_creature, pPlayer);
                break;
            case 8:
                DoSpawnAmbush(true);
                break;
            case 14:
                DoSpawnAmbush(false);
                break;
            case 18:
                DoScriptText(SAY_RIN_COMPLETE, m_creature, pPlayer);
                pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_RINJI_TRAPPED, m_creature);
                SetRun();
                m_uiPostEventCount = 1;
                break;
        }
    }

    void UpdateEscortAI(const uint32 uiDiff) override
    {
        // Check if we have a current target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
        {
            if (HasEscortState(STATE_ESCORT_ESCORTING) && m_uiPostEventCount)
            {
                if (m_uiPostEventTimer < uiDiff)
                {
                    m_uiPostEventTimer = 3000;

                    if (Player* pPlayer = GetPlayerForEscort())
                    {
                        switch (m_uiPostEventCount)
                        {
                            case 1:
                                DoScriptText(SAY_RIN_PROGRESS_1, m_creature, pPlayer);
                                ++m_uiPostEventCount;
                                break;
                            case 2:
                                DoScriptText(SAY_RIN_PROGRESS_2, m_creature, pPlayer);
                                m_uiPostEventCount = 0;
                                break;
                        }
                    }
                    else
                    {
                        m_creature->ForcedDespawn();
                        return;
                    }
                }
                else
                    m_uiPostEventTimer -= uiDiff;
            }

            return;
        }

        DoMeleeAttackIfReady();
    }
};

bool QuestAccept_npc_rinji(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_RINJI_TRAPPED)
    {
        if (GameObject* pGo = GetClosestGameObjectWithEntry(pCreature, GO_RINJI_CAGE, INTERACTION_DISTANCE))
            pGo->UseDoorOrButton();

        if (npc_rinjiAI* pEscortAI = dynamic_cast<npc_rinjiAI*>(pCreature->AI()))
            pEscortAI->Start(false, pPlayer, pQuest);

        pCreature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
    }
    return true;
}

UnitAI* GetAI_npc_rinji(Creature* pCreature)
{
    return new npc_rinjiAI(pCreature);
}

/*######
## npc_sharpbeak / event_sharpbeak_freed
## Reconstruction: local staging times are not claimed as original sniff data.
######*/

enum
{
    QUEST_SAVING_SHARPBEAK       = 2994,
    EVENT_SHARPBEAK_FREED        = 2807,
    NPC_SHARPBEAK                = 8023,
    NPC_SHARPBEAK_FATHER         = 8024,
    NPC_SHARPBEAK_MOTHER         = 8025,
    GO_SHARPBEAK_CAGE            = 144070,
    SHARPBEAK_REUNION_DELAY      = 6000,
    SHARPBEAK_SCENE_DURATION     = 24000,
    SHARPBEAK_PARENT_LIFETIME    = 30000,
    SHARPBEAK_RESPAWN_DELAY      = 10
};

struct npc_sharpbeakAI : public ScriptedAI
{
    npc_sharpbeakAI(Creature* creature) : ScriptedAI(creature),
        m_active(false), m_stopping(false), m_stateSaved(false), m_departing(false),
        m_timer(0), m_remaining(0), m_sceneX(0.f), m_sceneY(0.f), m_sceneZ(0.f),
        m_savedStandState(0), m_savedImmunityFlags(0), m_savedLevitate(false),
        m_savedWalk(true), m_savedActive(false)
    {
        SetReactState(REACT_PASSIVE);
    }

    bool m_active;
    bool m_stopping;
    bool m_stateSaved;
    bool m_departing;
    uint32 m_timer;
    uint32 m_remaining;
    float m_sceneX, m_sceneY, m_sceneZ;
    uint8 m_savedStandState;
    uint32 m_savedImmunityFlags;
    bool m_savedLevitate;
    bool m_savedWalk;
    bool m_savedActive;
    GuidList m_parents;

    void DespawnParents()
    {
        GuidList parents;
        parents.swap(m_parents);
        for (GuidList::const_iterator itr = parents.begin(); itr != parents.end(); ++itr)
        {
            if (Creature* parent = m_creature->GetMap()->GetCreature(*itr))
                if (parent->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
                    (parent->GetEntry() == NPC_SHARPBEAK_FATHER || parent->GetEntry() == NPC_SHARPBEAK_MOTHER))
                    parent->ForcedDespawn();
        }
    }

    void RestoreState()
    {
        if (!m_stateSaved)
            return;

        m_stateSaved = false;
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetStandState(m_savedStandState);
        m_creature->SetLevitate(m_savedLevitate);
        m_creature->SetWalk(m_savedWalk);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, m_savedImmunityFlags);
        m_creature->SetActiveObjectState(m_savedActive);
    }

    void AbortStart()
    {
        m_stopping = true;
        m_active = false;
        m_timer = m_remaining = 0;
        m_departing = false;
        DespawnParents();
        RestoreState();
        m_stopping = false;
    }

    void FinishScene()
    {
        if (!m_active || m_stopping)
            return;

        m_stopping = true;
        DespawnParents();
        // Keep m_active until CorpseRemoved sets the short scene respawn delay.
        if (m_active)
            m_creature->ForcedDespawn();
        m_stopping = false;
    }

    bool HaveParents() const
    {
        if (m_parents.size() != 2)
            return false;
        for (GuidList::const_iterator itr = m_parents.begin(); itr != m_parents.end(); ++itr)
        {
            Creature* parent = m_creature->GetMap()->GetCreature(*itr);
            if (!parent || !parent->IsAlive() || parent->GetSpawnerGuid() != m_creature->GetObjectGuid())
                return false;
        }
        return true;
    }

    bool StartScene(Player* player, GameObject* cage)
    {
        if (!player || !cage || m_stopping || !m_creature->IsAlive() ||
            !player->IsInWorld() || !player->IsAlive() || !cage->IsInWorld() || !cage->IsSpawned() ||
            cage->GetEntry() != GO_SHARPBEAK_CAGE || cage->GetGoType() != GAMEOBJECT_TYPE_GOOBER ||
            m_creature->GetMapId() != 0 || player->GetMap() != m_creature->GetMap() || cage->GetMap() != m_creature->GetMap() ||
            !player->IsWithinDistInMap(cage, INTERACTION_DISTANCE) || !m_creature->IsWithinDistInMap(cage, 10.f) ||
            player->GetQuestRewardStatus(QUEST_SAVING_SHARPBEAK))
            return false;

        QuestStatus status = player->GetQuestStatus(QUEST_SAVING_SHARPBEAK);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            return false;
        if (m_active)
            return true;

        m_active = true;
        m_departing = false;
        m_timer = SHARPBEAK_REUNION_DELAY;
        m_remaining = SHARPBEAK_SCENE_DURATION;
        m_sceneX = m_creature->GetPositionX();
        m_sceneY = m_creature->GetPositionY();
        m_sceneZ = m_creature->GetPositionZ();
        m_savedStandState = m_creature->GetByteValue(UNIT_FIELD_BYTES_1, 0);
        m_savedImmunityFlags = m_creature->GetUInt32Value(UNIT_FIELD_FLAGS) & (UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        m_savedLevitate = m_creature->IsLevitating();
        m_savedWalk = m_creature->IsWalking();
        m_savedActive = m_creature->HasActiveObjectState();
        m_stateSaved = true;
        m_creature->SetActiveObjectState(true);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);

        for (uint8 i = 0; i < 2; ++i)
        {
            float side = i == 0 ? -1.f : 1.f;
            Creature* parent = m_creature->SummonCreature(i == 0 ? NPC_SHARPBEAK_FATHER : NPC_SHARPBEAK_MOTHER,
                m_sceneX - 30.f, m_sceneY + side * 20.f, m_sceneZ + 20.f, 0.f,
                TEMPSPAWN_TIMED_DESPAWN, SHARPBEAK_PARENT_LIFETIME, true);
            if (!parent)
            {
                AbortStart();
                return false;
            }

            m_parents.push_back(parent->GetObjectGuid());
            parent->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER | UNIT_FLAG_IMMUNE_TO_NPC);
            if (parent->AI())
                parent->AI()->SetReactState(REACT_PASSIVE);
            parent->SetLevitate(true);
            parent->SetWalk(false);
            parent->GetMotionMaster()->MovePoint(1, m_sceneX - 3.f, m_sceneY + side * 3.f, m_sceneZ + 4.f,
                FORCED_MOVEMENT_FLIGHT, false);
        }
        return true;
    }

    void Reset() override
    {
        if (m_active)
            FinishScene();
    }

    void JustDied(Unit* killer) override
    {
        if (m_active)
            DespawnParents();
        ScriptedAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        if (m_active)
        {
            respawnDelay = SHARPBEAK_RESPAWN_DELAY;
            bool wasStopping = m_stopping;
            m_stopping = true;
            m_active = false;
            m_timer = m_remaining = 0;
            m_departing = false;
            DespawnParents();
            RestoreState();
            m_stopping = wasStopping;
        }
        ScriptedAI::CorpseRemoved(respawnDelay);
    }

    void UpdateAI(const uint32 diff) override
    {
        if (!m_active || m_stopping)
            return;
        if (!HaveParents() || diff >= m_remaining)
        {
            FinishScene();
            return;
        }
        m_remaining -= diff;
        if (m_departing)
            return;
        if (diff < m_timer)
        {
            m_timer -= diff;
            return;
        }

        m_departing = true;
        m_creature->SetLevitate(true);
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MovePoint(2, m_sceneX + 70.f, m_sceneY - 50.f, m_sceneZ + 55.f,
            FORCED_MOVEMENT_FLIGHT, false);
        uint8 i = 0;
        for (GuidList::const_iterator itr = m_parents.begin(); itr != m_parents.end(); ++itr, ++i)
        {
            if (Creature* parent = m_creature->GetMap()->GetCreature(*itr))
                parent->GetMotionMaster()->MovePoint(2, m_sceneX + 70.f, m_sceneY - 50.f + (i == 0 ? -4.f : 4.f), m_sceneZ + 57.f,
                    FORCED_MOVEMENT_FLIGHT, false);
        }
    }
};

UnitAI* GetAI_npc_sharpbeak(Creature* creature)
{
    return new npc_sharpbeakAI(creature);
}

bool ProcessEventId_SharpbeakFreed(uint32 eventId, Object* source, Object* target, bool isStart)
{
    if (eventId != EVENT_SHARPBEAK_FREED || !isStart || !source || !source->IsPlayer())
        return false;

    Player* player = static_cast<Player*>(source);
    if (!player->IsInWorld() || player->GetMapId() != 0)
        return false;

    GameObject* cage = nullptr;
    if (target)
    {
        if (!target->IsGameObject())
            return false;
        cage = static_cast<GameObject*>(target);
    }
    else
        cage = GetClosestGameObjectWithEntry(player, GO_SHARPBEAK_CAGE, INTERACTION_DISTANCE);

    if (!cage || cage->GetEntry() != GO_SHARPBEAK_CAGE || cage->GetMap() != player->GetMap() ||
        !player->IsWithinDistInMap(cage, INTERACTION_DISTANCE))
        return false;

    if (Creature* sharpbeak = GetClosestCreatureWithEntry(cage, NPC_SHARPBEAK, 10.f))
        if (npc_sharpbeakAI* ai = dynamic_cast<npc_sharpbeakAI*>(sharpbeak->AI()))
            return ai->StartScene(player, cage);
    return false;
}


enum
{
    YELL_FALSTAD_INVADERS = -27,
};

bool ProcessEventId_WildhammerMessage(uint32 /*eventId*/, Object* source, Object* /*target*/, bool /*isStart*/)
{
    if (!source->IsPlayer())
        return true;

    Player* player = static_cast<Player*>(source);
    player->UpdatePvP(true);
    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_PVP_ACTIVE_CANCELS);
    if (Creature* falstad = static_cast<ScriptedInstance*>(player->GetInstanceData())->GetSingleCreatureFromStorage(NPC_FALSTAD_WILDHAMMER))
        DoScriptText(YELL_FALSTAD_INVADERS, falstad, player);
    return true;
}

/*######
## spell_gammerita_turtle_camera
######*/

struct GammeritaTurtleCamera : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* target = spell->m_targets.getUnitTarget();
        // Gammerita Turtle Camera can be cast only on this target
        if (!target || target->GetEntry() != 7977)
            return SPELL_FAILED_BAD_TARGETS;

        return SPELL_CAST_OK;
    }
};

void AddSC_hinterlands()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_00x09hl";
    pNewScript->GetAI = &GetAI_npc_00x09hl;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_00x09hl;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_rinji";
    pNewScript->GetAI = &GetAI_npc_rinji;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_rinji;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "event_wildhammer_message";
    pNewScript->pProcessEventId = &ProcessEventId_WildhammerMessage;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_sharpbeak";
    pNewScript->GetAI = &GetAI_npc_sharpbeak;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "event_sharpbeak_freed";
    pNewScript->pProcessEventId = &ProcessEventId_SharpbeakFreed;
    pNewScript->RegisterSelf();

    RegisterSpellScript<GammeritaTurtleCamera>("spell_gammerita_turtle_camera");
}
