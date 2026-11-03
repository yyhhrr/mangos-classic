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
SDName: Western_Plaguelands
SD%Complete: 90
SDComment: Quest support: 5216, 5219, 5222, 5225, 5229, 5231, 5233, 5235, 5862, 5944.
SDCategory: Western Plaguelands
EndScriptData

*/

/* ContentData
npc_the_scourge_cauldron
npc_taelan_fordring
npc_isillien
npc_tirion_fordring
EndContentData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Spells/Scripts/SpellScript.h"
#include "Entities/TemporarySpawn.h"

/*######
## npc_the_scourge_cauldron
######*/

struct npc_the_scourge_cauldronAI : public ScriptedAI
{
    npc_the_scourge_cauldronAI(Creature* creature) : ScriptedAI(creature) { Reset(); }

    uint32 m_round = 0;
    uint32 m_retryTimer = 0;
    bool m_summoning = false;
    bool m_triggered = false;

    void Reset() override {}

    void JustRespawned() override
    {
        ++m_round;
        m_retryTimer = 0;
        m_summoning = m_triggered = false;
        ScriptedAI::JustRespawned();
    }

    void DoDie()
    {
        // Creature schedules its respawn during Suicide, so set the delay first.
        if (m_creature->GetRespawnDelay() < 600)
            m_creature->SetRespawnDelay(600);
        m_creature->Suicide();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!who || who->GetTypeId() != TYPEID_PLAYER || !who->IsAlive() || !who->IsInWorld() ||
            !m_creature->IsAlive() || !m_creature->IsInWorld() || m_summoning || m_triggered || m_retryTimer)
            return;

        uint32 firstQuest = 0, secondQuest = 0, entry = 0;
        switch (m_creature->GetAreaId())
        {
            case 199: firstQuest = 5216; secondQuest = 5229; entry = 11075; break;
            case 200: firstQuest = 5219; secondQuest = 5231; entry = 11077; break;
            case 201: firstQuest = 5225; secondQuest = 5235; entry = 11078; break;
            case 202: firstQuest = 5222; secondQuest = 5233; entry = 11076; break;
            default: return;
        }
        Player* player = static_cast<Player*>(who);
        if (player->GetQuestStatus(firstQuest) != QUEST_STATUS_INCOMPLETE &&
            player->GetQuestStatus(secondQuest) != QUEST_STATUS_INCOMPLETE)
            return;

        const uint32 round = m_round;
        m_summoning = true;
        Creature* guardian = m_creature->SummonCreature(entry, 0.0f, 0.0f, 0.0f, 0.0f,
            TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 600000);
        if (round != m_round || !m_creature->IsAlive() || !m_creature->IsInWorld())
        {
            // A synchronous spawn callback may have ended this attempt.
            if (guardian)
                static_cast<TemporarySpawn*>(guardian)->UnSummon();
            return;
        }
        m_summoning = false;
        if (!guardian)
        {
            m_retryTimer = 5000;
            return;
        }

        m_triggered = true;
        DoDie();
    }

    void UpdateAI(const uint32 diff) override
    {
        if (m_retryTimer)
            m_retryTimer = diff >= m_retryTimer ? 0 : m_retryTimer - diff;
        ScriptedAI::UpdateAI(diff);
    }
};

UnitAI* GetAI_npc_the_scourge_cauldron(Creature* creature)
{
    return new npc_the_scourge_cauldronAI(creature);
}

/*######
## npc_taelan_fordring
######*/

enum
{
    // scarlet end quest texts
    SAY_SCARLET_COMPLETE_1          = -1001076,
    SAY_SCARLET_COMPLETE_2          = -1001077,

    // escort text
    SAY_ESCORT_START                = -1001078,
    SAY_EXIT_KEEP                   = -1001079,
    EMOTE_MOUNT                     = -1001080,
    SAY_REACH_TOWER                 = -1001081,
    SAY_ISILLIEN_1                  = -1001082,
    SAY_ISILLIEN_2                  = -1001083,
    SAY_ISILLIEN_3                  = -1001084,
    SAY_ISILLIEN_4                  = -1001085,
    SAY_ISILLIEN_5                  = -1001086,
    SAY_ISILLIEN_6                  = -1001087,
    EMOTE_ISILLIEN_ATTACK           = -1001088,
    SAY_ISILLIEN_ATTACK             = -1001089,
    SAY_KILL_TAELAN_1               = -1001090,
    EMOTE_ISILLIEN_LAUGH            = -1001091,
    SAY_KILL_TAELAN_2               = -1001092,
    EMOTE_ATTACK_PLAYER             = -1001093,
    SAY_TIRION_1                    = -1001094,
    SAY_TIRION_2                    = -1001095,
    SAY_TIRION_3                    = -1001096,
    SAY_TIRION_4                    = -1001097,
    SAY_TIRION_5                    = -1001098,
    SAY_EPILOG_1                    = -1001099,
    EMOTE_KNEEL                     = -1001100,
    SAY_EPILOG_2                    = -1001101,
    EMOTE_HOLD_TAELAN               = -1001102,
    SAY_EPILOG_3                    = -1001103,
    SAY_EPILOG_4                    = -1001104,
    SAY_EPILOG_5                    = -1001105,

    // spells
    SPELL_DEVOTION_AURA             = 17232,
    SPELL_CRUSADER_STRIKE           = 14518,
    SPELL_HOLY_STRIKE               = 17143,
    SPELL_HOLY_CLEAVE               = 18819,
    SPELL_HOLY_LIGHT                = 15493,
    SPELL_LAY_ON_HANDS              = 17233,
    SPELL_TAELAN_SUFFERING          = 18810,

    // isillen spells
    SPELL_DOMINATE_MIND             = 14515,
    SPELL_FLASH_HEAL                = 10917,
    SPELL_GREATER_HEAL              = 10965,
    SPELL_MANA_BURN                 = 15800,
    SPELL_MIND_BLAST                = 17194,
    SPELL_MIND_FLAY                 = 17165,
    SPELL_TAELAN_DEATH              = 18969,

    // npcs
    NPC_TAELAN_FORDRING             = 1842,
    NPC_SCARLET_CAVALIER            = 1836,
    NPC_ISILLIEN                    = 1840,
    NPC_TIRION_FORDRING             = 12126,
    NPC_CRIMSON_ELITE               = 12128,

    MODEL_TAELAN_MOUNT              = 239,

    // quests
    QUEST_ID_SCARLET_SUBTERFUGE     = 5862,
    QUEST_ID_IN_DREAMS              = 5944,

    TIRION_KNEELS_1                 = 1,
};

static const int32 aCavalierYells[] = { -1001072, -1001073, -1001074, -1001075 };

static const DialogueEntry aScarletDialogue[] =
{
    // Scarlet Subterfuge ending dialogue
    {NPC_SCARLET_CAVALIER,          0,                          3000},
    {QUEST_ID_SCARLET_SUBTERFUGE,   0,                          7000},
    {SAY_SCARLET_COMPLETE_1,        NPC_TAELAN_FORDRING,        2000},
    {QUEST_ID_IN_DREAMS,            0,                          0},
    {SPELL_DEVOTION_AURA,           0,                          3000},
    {SAY_SCARLET_COMPLETE_2,        NPC_TAELAN_FORDRING,        0},
    // In Dreams event dialogue
    {SAY_EXIT_KEEP,                 NPC_TAELAN_FORDRING,        6000},
    {EMOTE_MOUNT,                   NPC_TAELAN_FORDRING,        4000},
    {MODEL_TAELAN_MOUNT,            0,                          0},
    {SAY_REACH_TOWER,               NPC_TAELAN_FORDRING,        2000},
    {SAY_ISILLIEN_1,                NPC_ISILLIEN,               5000},
    {SAY_ISILLIEN_2,                NPC_TAELAN_FORDRING,        4000},
    {SAY_ISILLIEN_3,                NPC_TAELAN_FORDRING,        10000},
    {SAY_ISILLIEN_4,                NPC_ISILLIEN,               7000},
    {SAY_ISILLIEN_5,                NPC_ISILLIEN,               5000},
    {SAY_ISILLIEN_6,                NPC_ISILLIEN,               6000},
    {EMOTE_ISILLIEN_ATTACK,         NPC_ISILLIEN,               3000},
    {SAY_ISILLIEN_ATTACK,           NPC_ISILLIEN,               3000},
    {SPELL_CRUSADER_STRIKE,         0,                          0},
    {SAY_KILL_TAELAN_1,             NPC_ISILLIEN,               1000},
    {EMOTE_ISILLIEN_LAUGH,          NPC_ISILLIEN,               4000},
    {SAY_KILL_TAELAN_2,             NPC_ISILLIEN,               10000},
    {EMOTE_ATTACK_PLAYER,           NPC_ISILLIEN,               0},
    {NPC_TIRION_FORDRING,           0,                          5000},
    {NPC_ISILLIEN,                  0,                          10000},
    {SAY_TIRION_1,                  NPC_TIRION_FORDRING,        4000},
    {SAY_TIRION_2,                  NPC_ISILLIEN,               6000},
    {SAY_TIRION_3,                  NPC_TIRION_FORDRING,        4000},
    {SAY_TIRION_4,                  NPC_TIRION_FORDRING,        3000},
    {SAY_TIRION_5,                  NPC_ISILLIEN,               0},
    {SAY_EPILOG_1,                  NPC_TIRION_FORDRING,        3000},
    {TIRION_KNEELS_1,               0,                          3000},
    {EMOTE_KNEEL,                   NPC_TIRION_FORDRING,        4000},
    {SAY_EPILOG_2,                  NPC_TIRION_FORDRING,        5000},
    {EMOTE_HOLD_TAELAN,             NPC_TIRION_FORDRING,        5000},
    {SAY_EPILOG_3,                  NPC_TIRION_FORDRING,        6000},
    {SAY_EPILOG_4,                  NPC_TIRION_FORDRING,        7000},
    {SAY_EPILOG_5,                  NPC_TIRION_FORDRING,        0},
    {0, 0, 0},
};

struct npc_taelan_fordringAI: public npc_escortAI, private DialogueHelper
{
    npc_taelan_fordringAI(Creature* creature): npc_escortAI(creature), DialogueHelper(aScarletDialogue)
    {
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);

        m_scarletComplete = false;
        m_fightStarted = false;
        m_taelanDead = false;
        m_hasMount = false;
        Reset();
    }

    bool m_scarletComplete;
    bool m_fightStarted;
    bool m_hasMount;
    bool m_taelanDead;

    ObjectGuid m_isillenGuid;
    ObjectGuid m_tirionGuid;
    GuidList m_lCavalierGuids;

    uint32 m_holyCleaveTimer;
    uint32 m_holyStrikeTimer;
    uint32 m_crusaderStrike;
    uint32 m_holyLightTimer;

    enum ScenePhase
    {
        SCENE_IDLE, SCENE_SCARLET, SCENE_SCARLET_COMBAT, SCENE_SCARLET_FINISH,
        SCENE_ESCORT, SCENE_TOWER, SCENE_DUEL, SCENE_WAIT_TIRION,
        SCENE_TIRION_APPROACH, SCENE_TIRION_TALK, SCENE_TIRION_COMBAT,
        SCENE_EPILOGUE, SCENE_FINISHING
    };

    ScenePhase m_scenePhase = SCENE_IDLE;
    uint32 m_sceneSerial = 0;
    uint32 m_dialogueDepth = 0;
    uint32 m_dialogueFrameSerial = 0;
    uint32 m_cleanupDepth = 0;
    bool m_draining = false;
    bool m_abortPending = false;
    bool m_scarletFinishPending = false;
    bool m_creditPending = false;
    bool m_deathApplied = false;
    bool m_isillienDefeated = false;
    int32 m_dialoguePending = 0;
    uint32 m_finishTimer = 0;
    GuidList m_ownedActors;

    bool IsOwnActor(Creature* actor, uint32 entry) const
    {
        return actor && actor->GetEntry() == entry && actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    bool IsCurrentIsillien(Creature* actor) const
    {
        return IsOwnActor(actor, NPC_ISILLIEN) && actor->GetObjectGuid() == m_isillenGuid &&
            m_scenePhase >= SCENE_TOWER && m_scenePhase < SCENE_FINISHING &&
            HasEscortState(STATE_ESCORT_ESCORTING);
    }

    bool IsCurrentTirion(Creature* actor) const
    {
        return IsOwnActor(actor, NPC_TIRION_FORDRING) && actor->GetObjectGuid() == m_tirionGuid &&
            m_scenePhase >= SCENE_TIRION_APPROACH && HasEscortState(STATE_ESCORT_ESCORTING);
    }

    bool CanApplyTaelanDeath(Creature* actor) const
    {
        return IsCurrentIsillien(actor) && m_scenePhase == SCENE_WAIT_TIRION &&
            m_taelanDead && !m_deathApplied;
    }

    bool CanSummonTirion(Creature* actor) const
    {
        return IsCurrentIsillien(actor) && m_scenePhase == SCENE_WAIT_TIRION && m_deathApplied;
    }

    bool HasTirionFor(Creature* actor) const
    {
        if (!IsCurrentIsillien(actor))
            return false;
        Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid);
        return IsCurrentTirion(tirion) && tirion->IsAlive();
    }

    bool CanBeginEpilogue(Creature* actor) const
    {
        return IsCurrentTirion(actor) && m_scenePhase == SCENE_TIRION_COMBAT && m_isillienDefeated;
    }

    void RequestAbort()
    {
        if (!m_dialogueDepth || m_dialogueFrameSerial == m_sceneSerial)
            m_abortPending = true;
    }

    void UpdateAI(uint32 diff) override
    {
        // Dialogue callbacks may enter the map/AI again before the helper has
        // advanced its cursor. They must not replace the suspended frame token.
        if (m_dialogueDepth || m_draining || m_cleanupDepth)
            return;
        npc_escortAI::UpdateAI(diff);
    }

    void ClearScene()
    {
        ++m_cleanupDepth;
        GuidList actors;
        actors.swap(m_ownedActors);
        ++m_sceneSerial;
        DisableDialogue();
        m_scenePhase = SCENE_IDLE;
        m_abortPending = false;
        m_scarletFinishPending = false;
        m_creditPending = false;
        m_dialoguePending = 0;
        m_finishTimer = 0;
        m_scarletComplete = false;
        m_fightStarted = false;
        m_taelanDead = false;
        m_hasMount = false;
        m_deathApplied = false;
        m_isillienDefeated = false;
        m_isillenGuid.Clear();
        m_tirionGuid.Clear();
        m_lCavalierGuids.clear();

        // Close local state before callbacks. Each actor may only retire its own
        // children; foreign creatures and already-retired rounds are untouched.
        for (ObjectGuid guid : actors)
        {
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
            {
                if (!IsOwnActor(actor, NPC_ISILLIEN) && !IsOwnActor(actor, NPC_TIRION_FORDRING) &&
                    !IsOwnActor(actor, NPC_CRIMSON_ELITE))
                    continue;
                if (actor->GetEntry() == NPC_ISILLIEN || actor->GetEntry() == NPC_TIRION_FORDRING)
                    SendAIEvent(AI_EVENT_CUSTOM_E, m_creature, actor);
                actor->ForcedDespawn();
            }
        }
        --m_cleanupDepth;
    }

    void JustDied(Unit* killer) override
    {
        ClearScene();
        npc_escortAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        ClearScene();
        npc_escortAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        ClearScene();
        npc_escortAI::JustRespawned();
        m_creature->SetImmobilizedState(false, true);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->Unmount();
    }

    void RequestDialogue(int32 entry)
    {
        if (m_dialogueDepth && m_dialogueFrameSerial != m_sceneSerial)
            return;
        if (m_dialogueDepth)
        {
            m_dialoguePending = entry;
            return;
        }
        m_dialogueFrameSerial = m_sceneSerial;
        ++m_dialogueDepth;
        StartNextDialogueText(entry);
        --m_dialogueDepth;
    }

    void DrainSceneActions()
    {
        if (m_dialogueDepth || m_draining)
            return;
        m_draining = true;
        if (m_abortPending)
        {
            bool failEscort = HasEscortState(STATE_ESCORT_ESCORTING);
            ClearScene();
            uint32 serial = m_sceneSerial;
            m_draining = false;
            if (failEscort)
                FailQuestForPlayerAndGroup();
            if (serial == m_sceneSerial)
                m_creature->ForcedDespawn();
            return;
        }
        if (m_scarletFinishPending)
        {
            m_scarletFinishPending = false;
            m_scarletComplete = false;
            m_scenePhase = SCENE_SCARLET_FINISH;
            RequestDialogue(SPELL_DEVOTION_AURA);
        }
        if (m_dialoguePending)
        {
            int32 entry = m_dialoguePending;
            m_dialoguePending = 0;
            RequestDialogue(entry);
        }
        if (m_abortPending)
        {
            m_draining = false;
            DrainSceneActions();
            return;
        }
        if (m_creditPending)
        {
            m_creditPending = false;
            Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid);
            if (m_scenePhase != SCENE_EPILOGUE || !IsCurrentTirion(tirion) || !tirion->IsAlive() || !m_isillienDefeated)
            {
                m_abortPending = true;
                m_draining = false;
                DrainSceneActions();
                return;
            }
            // Commit before reward callbacks. A true respawn/new round may run
            // reentrantly; there is no old-round write after this call.
            m_scenePhase = SCENE_FINISHING;
            m_finishTimer = 3 * MINUTE * IN_MILLISECONDS;
            tirion->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            Player* player = GetPlayerForEscort();
            m_draining = false;
            if (player)
                player->RewardPlayerAndGroupAtEventExplored(QUEST_ID_IN_DREAMS, m_creature);
            return;
        }
        m_draining = false;
    }

    void JustReachedHome() override
    {
        if (m_scarletComplete && m_scenePhase == SCENE_SCARLET_COMBAT)
            m_scarletFinishPending = true;
        DrainSceneActions();
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 miscValue) override
    {
        if (!sender || !invoker || !m_creature->IsAlive() || !m_creature->IsInWorld())
            return;
        if (eventType == AI_EVENT_START_ESCORT && sender == m_creature && invoker->GetTypeId() == TYPEID_PLAYER &&
            miscValue == QUEST_ID_IN_DREAMS && m_scenePhase == SCENE_IDLE && !m_dialogueDepth && !m_cleanupDepth &&
            !HasEscortState(STATE_ESCORT_ESCORTING))
        {
            uint32 serial = m_sceneSerial;
            Start(false, static_cast<Player*>(invoker), GetQuestTemplateStore(miscValue));
            if (serial != m_sceneSerial)
                return;
            if (!HasEscortState(STATE_ESCORT_ESCORTING))
                return;
            m_scenePhase = SCENE_ESCORT;
            ++m_sceneSerial;
            DoScriptText(SAY_ESCORT_START, m_creature);
            m_creature->SetFactionTemporary(FACTION_ESCORT_N_FRIEND_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
        }
        else if (eventType == AI_EVENT_CUSTOM_A && sender == m_creature && invoker->GetTypeId() == TYPEID_PLAYER &&
            miscValue == QUEST_ID_SCARLET_SUBTERFUGE && m_scenePhase == SCENE_IDLE && !m_dialogueDepth && !m_cleanupDepth &&
            !HasEscortState(STATE_ESCORT_ESCORTING) && !m_creature->IsInCombat())
        {
            m_lCavalierGuids.clear();
            ++m_sceneSerial;
            m_scenePhase = SCENE_SCARLET;
            RequestDialogue(NPC_SCARLET_CAVALIER);
        }
        else if (eventType == AI_EVENT_CUSTOM_B && invoker == sender && sender->GetTypeId() == TYPEID_UNIT &&
            CanSummonTirion(static_cast<Creature*>(sender)))
        {
            uint32 serial = m_sceneSerial;
            m_scenePhase = SCENE_TIRION_APPROACH;
            Creature* tirion = m_creature->SummonCreature(NPC_TIRION_FORDRING, 2620.273f, -1920.917f, 74.25f, 0,
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
            if (serial != m_sceneSerial)
            {
                if (IsOwnActor(tirion, NPC_TIRION_FORDRING))
                    tirion->ForcedDespawn();
                return;
            }
            if (!IsCurrentTirion(tirion))
                RequestAbort();
            else
            {
                tirion->SetWalk(false);
                tirion->SetImmuneToNPC(true);
                if (serial != m_sceneSerial)
                {
                    tirion->ForcedDespawn();
                    return;
                }
                RequestDialogue(NPC_TIRION_FORDRING);
            }
        }
        else if (eventType == AI_EVENT_CUSTOM_C && invoker == sender && sender->GetTypeId() == TYPEID_UNIT &&
            (IsCurrentIsillien(static_cast<Creature*>(sender)) || IsCurrentTirion(static_cast<Creature*>(sender))))
            RequestAbort();
        DrainSceneActions();
    }

    void WaypointReached(uint32 pointId) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_scenePhase != SCENE_ESCORT)
            return;
        if (pointId == 26)
        {
            SetEscortPaused(true);
            RequestDialogue(SAY_EXIT_KEEP);
        }
        else if (pointId == 56)
        {
            m_scenePhase = SCENE_TOWER;
            SetEscortPaused(true);
            m_creature->GetMotionMaster()->Clear();
            m_creature->GetMotionMaster()->MoveIdle();
            RequestDialogue(SAY_REACH_TOWER);
        }
        DrainSceneActions();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (!summoned || summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;
        uint32 entry = summoned->GetEntry();
        if (entry != NPC_ISILLIEN && entry != NPC_TIRION_FORDRING && entry != NPC_CRIMSON_ELITE)
            return;
        if (std::find(m_ownedActors.begin(), m_ownedActors.end(), summoned->GetObjectGuid()) != m_ownedActors.end())
            return;
        if ((entry == NPC_ISILLIEN && (m_scenePhase != SCENE_TOWER || m_isillenGuid)) ||
            (entry == NPC_TIRION_FORDRING && (m_scenePhase != SCENE_TIRION_APPROACH || m_tirionGuid)) ||
            (entry == NPC_CRIMSON_ELITE && m_scenePhase != SCENE_DUEL))
        {
            summoned->ForcedDespawn();
            return;
        }
        m_ownedActors.push_back(summoned->GetObjectGuid());
        uint32 serial = m_sceneSerial;
        if (entry == NPC_ISILLIEN || entry == NPC_TIRION_FORDRING)
        {
            if (entry == NPC_ISILLIEN)
                m_isillenGuid = summoned->GetObjectGuid();
            else
                m_tirionGuid = summoned->GetObjectGuid();
            SendAIEvent(AI_EVENT_START_ESCORT, m_creature, summoned);
            if (serial != m_sceneSerial)
            {
                summoned->ForcedDespawn();
                return;
            }
            npc_escortAI* escort = dynamic_cast<npc_escortAI*>(summoned->AI());
            if (!escort || !escort->HasEscortState(STATE_ESCORT_ESCORTING))
            {
                RequestAbort();
                return;
            }
        }
        if (entry == NPC_ISILLIEN)
        {
            float x, y, z;
            summoned->GetNearPoint(summoned, x, y, z, 0, 5.0f, M_PI_F * 1.25f);
            Creature* first = summoned->SummonCreature(NPC_CRIMSON_ELITE, x, y, z, summoned->GetOrientation(),
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
            if (serial != m_sceneSerial)
                return;
            if (!first)
                RequestAbort();
            summoned->GetNearPoint(summoned, x, y, z, 0, 5.0f, 0);
            Creature* second = summoned->SummonCreature(NPC_CRIMSON_ELITE, x, y, z, summoned->GetOrientation(),
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
            if (serial == m_sceneSerial && !second)
                RequestAbort();
        }
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        if (IsCurrentIsillien(summoned))
        {
            if (m_scenePhase == SCENE_TIRION_COMBAT)
                m_isillienDefeated = true;
            else
                RequestAbort();
        }
        else if (IsCurrentTirion(summoned) && m_scenePhase != SCENE_FINISHING)
            RequestAbort();
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        if (IsCurrentIsillien(summoned) && !m_isillienDefeated)
            RequestAbort();
        else if (IsCurrentTirion(summoned) && m_scenePhase != SCENE_FINISHING)
            RequestAbort();
    }

    void SummonedMovementInform(Creature* summoned, uint32 motionType, uint32 pointId) override
    {
        if (motionType != POINT_MOTION_TYPE || !IsCurrentTirion(summoned) || !summoned->IsAlive())
            return;
        if (pointId == 100 && m_scenePhase == SCENE_TIRION_APPROACH)
        {
            m_scenePhase = SCENE_TIRION_TALK;
            RequestDialogue(NPC_ISILLIEN);
            summoned->SetFacingToObject(m_creature);
            summoned->SetStandState(UNIT_STAND_STATE_KNEEL);
        }
        else if (pointId == 200 && CanBeginEpilogue(summoned))
        {
            m_scenePhase = SCENE_EPILOGUE;
            RequestDialogue(SAY_EPILOG_1);
        }
        DrainSceneActions();
    }

    void Reset() override
    {
        m_holyCleaveTimer = urand(11000, 15000);
        m_holyStrikeTimer = urand(6000, 8000);
        m_crusaderStrike  = urand(1000, 5000);
        m_holyLightTimer  = 0;
    }

    void Aggro(Unit* /*who*/) override
    {
        if (m_hasMount)
            m_creature->Unmount();

        DoCastSpellIfCan(m_creature, SPELL_DEVOTION_AURA);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (m_taelanDead)
            return;

        npc_escortAI::MoveInLineOfSight(who);
    }

    void EnterEvadeMode() override
    {
        if (m_taelanDead)
        {
            m_deathApplied = true;
            m_creature->RemoveAllAurasOnEvade();
            m_creature->CombatStop(true);
            m_creature->SetLootRecipient(nullptr);

            m_creature->InterruptNonMeleeSpells(true);
            m_creature->SetHealth(0);
            m_creature->StopMoving();
            m_creature->ClearComboPointHolders();
            m_creature->RemoveAllAurasOnDeath();
            m_creature->ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            m_creature->ClearAllReactives();
            m_creature->SetImmobilizedState(true, true);
            m_creature->SetStandState(UNIT_STAND_STATE_DEAD);

            Reset();
        }
        else
        {
            if (m_hasMount)
                m_creature->Mount(MODEL_TAELAN_MOUNT);

            npc_escortAI::EnterEvadeMode();
        }
    }











    void JustDidDialogueStep(int32 entry) override
    {
        if (m_dialogueFrameSerial != m_sceneSerial)
            return;
        uint32 dialogueSerial = m_sceneSerial;
        switch (entry)
        {
            case NPC_SCARLET_CAVALIER:
            {
                // kneel and make everyone worried
                m_creature->SetStandState(UNIT_STAND_STATE_KNEEL);
                m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);

                CreatureList lCavaliersInRange;
                GetCreatureListWithEntryInGrid(lCavaliersInRange, m_creature, NPC_SCARLET_CAVALIER, 10.0f);

                uint8 index = 0;
                for (auto* cavalier : lCavaliersInRange)
                {
                    if (cavalier->IsAlive() && !cavalier->IsInCombat() && cavalier->GetPositionZ() > 164.0f) // Prevent picking NPC from another floor
                    {
                        m_lCavalierGuids.push_back(cavalier->GetObjectGuid());
                        cavalier->SetFacingToObject(m_creature);
                        if (index < sizeof(aCavalierYells) / sizeof(aCavalierYells[0]))
                            DoScriptText(aCavalierYells[index], cavalier);
                        if (dialogueSerial != m_sceneSerial)
                            return;
                        ++index;
                    }
                }
                break;
            }
            case QUEST_ID_SCARLET_SUBTERFUGE:
            {
                float fX, fY, fZ;
                for (GuidList::const_iterator itr = m_lCavalierGuids.begin(); itr != m_lCavalierGuids.end(); ++itr)
                {
                    if (Creature* cavalier = m_creature->GetMap()->GetCreature(*itr))
                    {
                        m_creature->GetContactPoint(cavalier, fX, fY, fZ);
                        cavalier->GetMotionMaster()->MovePoint(0, fX, fY, fZ);
                    }
                }
                break;
            }
            case SAY_SCARLET_COMPLETE_1:
                // stand up and knock down effect
                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                DoCastSpellIfCan(m_creature, SPELL_TAELAN_SUFFERING);
                break;
            case QUEST_ID_IN_DREAMS:
            {
                if (m_scenePhase != SCENE_SCARLET)
                    break;
                m_scenePhase = SCENE_SCARLET_COMBAT;
                bool engaged = false;
                uint32 serial = m_sceneSerial;
                GuidList cavaliers = m_lCavalierGuids;
                // force attack
                m_creature->SetFactionTemporary(FACTION_ESCORT_N_FRIEND_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
                for (GuidList::const_iterator itr = cavaliers.begin(); itr != cavaliers.end(); ++itr)
                {
                    if (Creature* cavalier = m_creature->GetMap()->GetCreature(*itr))
                    {
                        if (cavalier->GetEntry() != NPC_SCARLET_CAVALIER || !cavalier->IsAlive() || !cavalier->AI())
                            continue;
                        cavalier->AI()->AttackStart(m_creature);
                        if (serial != m_sceneSerial)
                            return;
                        engaged = engaged || cavalier->GetVictim() == m_creature;
                    }
                }
                m_scarletComplete = true;
                if (!engaged)
                    m_scarletFinishPending = true;
                break;
            }
            case SAY_SCARLET_COMPLETE_2:
                if (m_scenePhase != SCENE_SCARLET_FINISH)
                    break;
                m_scenePhase = SCENE_IDLE;
                m_lCavalierGuids.clear();
                m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                break;
            case MODEL_TAELAN_MOUNT:
                // mount when outside
                m_hasMount = true;
                SetEscortPaused(false);
                m_creature->Mount(MODEL_TAELAN_MOUNT);
                break;
            case SAY_REACH_TOWER:
            {
                if (m_scenePhase != SCENE_TOWER)
                    break;
                if (Player* player = GetPlayerForEscort())
                    m_creature->SetFacingToObject(player);
                Creature* isillien = m_creature->SummonCreature(NPC_ISILLIEN, 2693.12f, -1943.04f, 72.04f, 2.11f,
                    TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
                if (dialogueSerial != m_sceneSerial)
                {
                    if (IsOwnActor(isillien, NPC_ISILLIEN))
                        isillien->ForcedDespawn();
                    return;
                }
                if (!isillien)
                    RequestAbort();
                break;
            }
            case SAY_ISILLIEN_2:
                if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                    m_creature->SetFacingToObject(isillien);
                break;
            case SAY_ISILLIEN_3:
                m_creature->Unmount();
                break;
            case SPELL_CRUSADER_STRIKE:
            {
                if (m_scenePhase != SCENE_TOWER)
                    break;
                m_scenePhase = SCENE_DUEL;
                static const float positions[3][3] =
                {
                    {2711.32f, -1882.67f, 67.89f},
                    {2710.93f, -1878.90f, 67.97f},
                    {2710.53f, -1875.28f, 67.90f}
                };
                for (const auto& position : positions)
                {
                    Creature* elite = m_creature->SummonCreature(NPC_CRIMSON_ELITE, position[0], position[1], position[2], 3.2f,
                        TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 15 * MINUTE * IN_MILLISECONDS);
                    if (dialogueSerial != m_sceneSerial)
                    {
                        if (IsOwnActor(elite, NPC_CRIMSON_ELITE))
                            elite->ForcedDespawn();
                        return;
                    }
                    if (!elite)
                    {
                        RequestAbort();
                        return;
                    }
                    elite->SetWalk(false);
                    float x, y, z;
                    m_creature->GetContactPoint(elite, x, y, z);
                    elite->GetMotionMaster()->MovePoint(0, x, y, z);
                    if (dialogueSerial != m_sceneSerial)
                        return;
                }
                Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid);
                if (!IsCurrentIsillien(isillien) || !isillien->IsAlive())
                {
                    RequestAbort();
                    return;
                }
                isillien->AI()->AttackStart(m_creature);
                if (dialogueSerial != m_sceneSerial)
                    return;
                AttackStart(isillien);
                if (dialogueSerial == m_sceneSerial)
                    m_fightStarted = true;
                break;
            }
            case SAY_KILL_TAELAN_1:
                if (m_scenePhase != SCENE_WAIT_TIRION || !m_taelanDead)
                    break;
                // Kill Taelan and attack players
                if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                    SendAIEvent(AI_EVENT_CUSTOM_A, m_creature, isillien);
                else
                    RequestAbort();
                break;
            case EMOTE_ATTACK_PLAYER:
                if (!m_deathApplied)
                {
                    RequestAbort();
                    break;
                }
                // Attack players
                if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                {
                    if (Player* player = GetPlayerForEscort())
                        isillien->AI()->AttackStart(player);
                }
                break;
            // Tirion event
            case SAY_TIRION_1:
                if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                {
                    tirion->SetStandState(UNIT_STAND_STATE_STAND);
                    if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                    {
                        tirion->SetFacingToObject(isillien);
                        isillien->CombatStop();
                    }
                }
                break;
            case SAY_TIRION_5:
                if (m_scenePhase != SCENE_TIRION_TALK)
                    break;
                m_scenePhase = SCENE_TIRION_COMBAT;
                if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                {
                    if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                    {
                        tirion->SetImmuneToNPC(false);
                        tirion->AI()->AttackStart(isillien);
                        isillien->AI()->AttackStart(tirion);
                    }
                }
                break;
            // Epilogue dialogue
            case SAY_EPILOG_1:
                if (Creature* isillien = m_creature->GetMap()->GetCreature(m_isillenGuid))
                {
                    if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                        tirion->SetFacingToObject(isillien);
                }
                break;
            case TIRION_KNEELS_1:
                if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                    tirion->SetFacingToObject(m_creature);
                break;
            case EMOTE_HOLD_TAELAN:
                if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                    tirion->SetStandState(UNIT_STAND_STATE_KNEEL);
                break;
            case SAY_EPILOG_4:
                if (Creature* tirion = m_creature->GetMap()->GetCreature(m_tirionGuid))
                    tirion->SetStandState(UNIT_STAND_STATE_STAND);
                break;
            case SAY_EPILOG_5:
                m_creditPending = true;
                break;
        }
    }

    Creature* GetSpeakerByEntry(uint32 entry) override
    {
        if (m_dialogueDepth && m_dialogueFrameSerial != m_sceneSerial)
            return nullptr;
        switch (entry)
        {
            case NPC_TAELAN_FORDRING:   return m_creature;
            case NPC_ISILLIEN:          return m_creature->GetMap()->GetCreature(m_isillenGuid);
            case NPC_TIRION_FORDRING:   return m_creature->GetMap()->GetCreature(m_tirionGuid);

            default:
                return nullptr;
        }
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (m_dialogueDepth || m_draining || m_cleanupDepth)
            return;
        uint32 serial = m_sceneSerial;
        if (m_scenePhase == SCENE_FINISHING)
        {
            if (m_finishTimer <= diff)
            {
                ClearScene();
                m_creature->ForcedDespawn();
                return;
            }
            m_finishTimer -= diff;
        }
        if (m_scenePhase >= SCENE_ESCORT && !HasEscortState(STATE_ESCORT_ESCORTING))
            RequestAbort();
        m_dialogueFrameSerial = m_sceneSerial;
        ++m_dialogueDepth;
        DialogueUpdate(diff);
        --m_dialogueDepth;
        DrainSceneActions();
        if (serial != m_sceneSerial || !m_creature->IsAlive() || m_creature->IsDespawned())
            return;

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_taelanDead)
            return;

        // If Taelan is in fight with Isillien and drops below 50% make him "dead"
        if (!m_taelanDead && m_fightStarted && m_creature->GetHealthPercent() < 50.0f)
        {
            m_taelanDead = true;
            m_scenePhase = SCENE_WAIT_TIRION;
            RequestDialogue(SAY_KILL_TAELAN_1);
            DrainSceneActions();
            return;
        }

        if (m_holyCleaveTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HOLY_CLEAVE) == CAST_OK)
                m_holyCleaveTimer = urand(11000, 13000);
        }
        else
            m_holyCleaveTimer -= diff;

        if (m_holyStrikeTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HOLY_STRIKE) == CAST_OK)
                m_holyStrikeTimer = urand(9000, 14000);
        }
        else
            m_holyStrikeTimer -= diff;

        if (m_crusaderStrike < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CRUSADER_STRIKE) == CAST_OK)
                m_crusaderStrike = urand(7000, 12000);
        }
        else
            m_crusaderStrike -= diff;

        if (m_creature->GetHealthPercent() < 75.0f)
        {
            if (m_holyLightTimer < diff)
            {
                if (Unit* target = DoSelectLowestHpFriendly(50.0f))
                {
                    if (DoCastSpellIfCan(target, SPELL_HOLY_LIGHT) == CAST_OK)
                        m_holyLightTimer = urand(10000, 15000);
                }
            }
            else
                m_holyLightTimer -= diff;
        }

        if (!m_fightStarted && m_creature->GetHealthPercent() < 15.0f)
            DoCastSpellIfCan(m_creature, SPELL_LAY_ON_HANDS);

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_taelan_fordring(Creature* creature)
{
    return new npc_taelan_fordringAI(creature);
}

bool QuestAccept_npc_taelan_fordring(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->GetQuestId() == QUEST_ID_IN_DREAMS)
        creature->AI()->SendAIEvent(AI_EVENT_START_ESCORT, player, creature, quest->GetQuestId());

    return true;
}

bool QuestRewarded_npc_taelan_fordring(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->GetQuestId() == QUEST_ID_SCARLET_SUBTERFUGE)
        creature->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, player, creature, quest->GetQuestId());

    return true;
}

// 18969 - Taelan Death
struct TaelanDeath : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        if (!caster || !target || caster->GetTypeId() != TYPEID_UNIT || target->GetTypeId() != TYPEID_UNIT ||
            caster->GetEntry() != NPC_ISILLIEN || target->GetEntry() != NPC_TAELAN_FORDRING)
            return;
        npc_taelan_fordringAI* taelan = dynamic_cast<npc_taelan_fordringAI*>(target->AI());
        if (!taelan || !taelan->CanApplyTaelanDeath(static_cast<Creature*>(caster)))
            return;
        taelan->EnterEvadeMode();
        caster->SetFacingToObject(target);
        if (caster->AI())
            caster->AI()->EnterEvadeMode();
    }
};

/*######
## npc_isillien
######*/

struct npc_isillienAI: public npc_escortAI
{
    npc_isillienAI(Creature* creature): npc_escortAI(creature)
    {
        m_tirionSpawned = false;
        m_taelanDead = false;
        m_summonTirionTimer = 0;
        Reset();
    }

    bool m_tirionSpawned;
    bool m_taelanDead;

    ObjectGuid m_taelanGuid;

    uint32 m_summonTirionTimer;
    uint32 m_manaBurnTimer;
    uint32 m_flashHealTimer;
    uint32 m_greaterHealTimer;
    uint32 m_holyLightTimer;
    uint32 m_dominateTimer;
    uint32 m_mindBlastTimer;
    uint32 m_mindFlayTimer;

    void Reset() override
    {
        m_manaBurnTimer   = urand(7000, 12000);
        m_flashHealTimer   = urand(10000, 15000);
        m_dominateTimer   = urand(10000, 14000);
        m_mindBlastTimer  = urand(0, 1000);
        m_mindFlayTimer   = urand(3000, 7000);
        m_greaterHealTimer = 0;
    }

    void MoveInLineOfSight(Unit* /*who*/) override
    {
        // attack only on request
    }

    void EnterEvadeMode() override
    {
        // evade and keep the same posiiton
        if (m_taelanDead)
        {
            m_creature->RemoveAllAurasOnEvade();
            m_creature->CombatStop(true);
            m_creature->SetLootRecipient(nullptr);

            m_creature->GetMotionMaster()->MoveIdle();

            Reset();
        }
        else
            npc_escortAI::EnterEvadeMode();
    }

        bool m_sceneActive = false;
    bool m_tirionDue = false;
    bool m_requestingTirion = false;
    uint32 m_sceneSerial = 0;
    GuidList m_ownedElites;

    npc_taelan_fordringAI* GetTaelan() const
    {
        Creature* taelan = m_creature->GetMap()->GetCreature(m_taelanGuid);
        if (!taelan || taelan->GetEntry() != NPC_TAELAN_FORDRING ||
            taelan->GetObjectGuid() != m_creature->GetSpawnerGuid())
            return nullptr;
        return dynamic_cast<npc_taelan_fordringAI*>(taelan->AI());
    }

    void EndScene()
    {
        GuidList elites;
        elites.swap(m_ownedElites);
        ++m_sceneSerial;
        m_sceneActive = false;
        m_tirionDue = false;
        m_requestingTirion = false;
        m_tirionSpawned = false;
        m_taelanDead = false;
        m_summonTirionTimer = 0;
        m_taelanGuid.Clear();
        // This is the event's own pending death cast, not an ordinary evade.
        m_creature->InterruptNonMeleeSpells(true, SPELL_TAELAN_DEATH);
        for (ObjectGuid guid : elites)
        {
            if (Creature* elite = m_creature->GetMap()->GetCreature(guid))
                if (elite->GetEntry() == NPC_CRIMSON_ELITE && elite->GetSpawnerGuid() == m_creature->GetObjectGuid())
                    elite->ForcedDespawn();
        }
    }

    void JustDied(Unit* killer) override
    {
        EndScene();
        npc_escortAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        EndScene();
        npc_escortAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        EndScene();
        npc_escortAI::JustRespawned();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (!summoned || summoned->GetEntry() != NPC_CRIMSON_ELITE ||
            summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;
        if (!m_sceneActive)
        {
            summoned->ForcedDespawn();
            return;
        }
        if (std::find(m_ownedElites.begin(), m_ownedElites.end(), summoned->GetObjectGuid()) == m_ownedElites.end())
            m_ownedElites.push_back(summoned->GetObjectGuid());
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 /*miscValue*/) override
    {
        if (!sender || sender != invoker || sender->GetTypeId() != TYPEID_UNIT ||
            sender->GetEntry() != NPC_TAELAN_FORDRING || sender->GetObjectGuid() != m_creature->GetSpawnerGuid())
            return;
        if (eventType == AI_EVENT_CUSTOM_E)
        {
            EndScene();
            return;
        }
        npc_taelan_fordringAI* taelan = dynamic_cast<npc_taelan_fordringAI*>(sender->AI());
        if (!taelan || !taelan->IsCurrentIsillien(m_creature))
            return;
        if (eventType == AI_EVENT_START_ESCORT && !m_sceneActive)
        {
            uint32 serial = m_sceneSerial;
            Start(false);
            if (serial != m_sceneSerial)
                return;
            if (HasEscortState(STATE_ESCORT_ESCORTING))
            {
                ++m_sceneSerial;
                m_sceneActive = true;
                m_taelanGuid = sender->GetObjectGuid();
            }
        }
        else if (eventType == AI_EVENT_CUSTOM_A && m_sceneActive && !m_taelanDead && taelan->CanApplyTaelanDeath(m_creature))
        {
            // Spell18969 has a real two-second cast; its completion still owns
            // the original fake-death callback. No synchronous replacement.
            uint32 serial = m_sceneSerial;
            CanCastResult result = DoCastSpellIfCan(invoker, SPELL_TAELAN_DEATH, CAST_INTERRUPT_PREVIOUS);
            if (serial != m_sceneSerial || !m_sceneActive)
                return;
            if (result != CAST_OK)
            {
                SendAIEvent(AI_EVENT_CUSTOM_C, m_creature, static_cast<Creature*>(sender));
                return;
            }
            m_taelanDead = true;
            m_summonTirionTimer = 120000;
        }
    }

    bool UpdateTirionArrival(uint32 diff)
    {
        if (!m_sceneActive)
            return false;
        npc_taelan_fordringAI* taelan = GetTaelan();
        if (!taelan || !taelan->IsCurrentIsillien(m_creature))
        {
            EndScene();
            m_creature->ForcedDespawn();
            return false;
        }
        if (!m_tirionSpawned && m_taelanDead)
        {
            if (m_summonTirionTimer)
            {
                if (m_summonTirionTimer <= diff)
                {
                    m_summonTirionTimer = 0;
                    m_tirionDue = true;
                }
                else
                    m_summonTirionTimer -= diff;
            }
            if (!m_requestingTirion && (m_tirionDue || m_creature->GetHealthPercent() < 20.0f) &&
                taelan->CanSummonTirion(m_creature))
            {
                uint32 serial = m_sceneSerial;
                m_requestingTirion = true;
                Creature* actor = m_creature->GetMap()->GetCreature(m_taelanGuid);
                SendAIEvent(AI_EVENT_CUSTOM_B, m_creature, actor);
                if (serial != m_sceneSerial || !m_sceneActive)
                    return false;
                m_requestingTirion = false;
                if (taelan->HasTirionFor(m_creature))
                    m_tirionSpawned = true;
                else
                    SendAIEvent(AI_EVENT_CUSTOM_C, m_creature, actor);
            }
        }
        return m_sceneActive;
    }

    void WaypointReached(uint32 pointId) override
    {
        switch (pointId)
        {
            case 3:
                SetEscortPaused(true);
                break;
        }
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (!UpdateTirionArrival(diff))
            return;

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Combat spells
        if (m_mindBlastTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MIND_BLAST) == CAST_OK)
                m_mindBlastTimer = urand(3000, 5000);
        }
        else
            m_mindBlastTimer -= diff;

        if (m_mindFlayTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MIND_FLAY) == CAST_OK)
                m_mindFlayTimer = urand(9000, 15000);
        }
        else
            m_mindFlayTimer -= diff;

        if (m_manaBurnTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_MANA_BURN) == CAST_OK)
                m_manaBurnTimer = urand(8000, 12000);
        }
        else
            m_manaBurnTimer -= diff;

        if (m_dominateTimer < diff)
        {
            if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 1, SPELL_DOMINATE_MIND, SELECT_FLAG_PLAYER))
            {
                if (DoCastSpellIfCan(target, SPELL_DOMINATE_MIND) == CAST_OK)
                    m_dominateTimer = urand(25000, 30000);
            }
        }
        else
            m_dominateTimer -= diff;

        if (m_flashHealTimer < diff)
        {
            if (Unit* target = DoSelectLowestHpFriendly(50.0f))
            {
                if (DoCastSpellIfCan(target, SPELL_FLASH_HEAL) == CAST_OK)
                    m_flashHealTimer = urand(10000, 15000);
            }
        }
        else
            m_flashHealTimer -= diff;

        if (m_creature->GetHealthPercent() < 50.0f)
        {
            if (m_greaterHealTimer < diff)
            {
                if (DoCastSpellIfCan(m_creature, SPELL_GREATER_HEAL) == CAST_OK)
                    m_greaterHealTimer = urand(15000, 20000);
            }
            else
                m_greaterHealTimer -= diff;
        }

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_isillien(Creature* creature)
{
    return new npc_isillienAI(creature);
}

/*######
## npc_tirion_fordring
######*/

struct npc_tirion_fordringAI: public npc_escortAI
{
    npc_tirion_fordringAI(Creature* creature): npc_escortAI(creature) { Reset(); }

    ObjectGuid m_taelanGuid;

    uint32 m_holyCleaveTimer;
    uint32 m_holyStrikeTimer;
    uint32 m_crusaderStrike;

    bool m_sceneActive = false;
    uint32 m_sceneSerial = 0;

    npc_taelan_fordringAI* GetTaelan() const
    {
        Creature* taelan = m_creature->GetMap()->GetCreature(m_taelanGuid);
        if (!taelan || taelan->GetEntry() != NPC_TAELAN_FORDRING ||
            taelan->GetObjectGuid() != m_creature->GetSpawnerGuid())
            return nullptr;
        npc_taelan_fordringAI* ai = dynamic_cast<npc_taelan_fordringAI*>(taelan->AI());
        return ai && ai->IsCurrentTirion(m_creature) ? ai : nullptr;
    }

    void EndScene()
    {
        ++m_sceneSerial;
        m_sceneActive = false;
        m_taelanGuid.Clear();
    }

    void JustDied(Unit* killer) override
    {
        EndScene();
        npc_escortAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        EndScene();
        npc_escortAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        EndScene();
        npc_escortAI::JustRespawned();
    }

    void EnterEvadeMode() override
    {
        m_creature->RemoveAllAurasOnEvade();
        m_creature->CombatStop(true);
        m_creature->SetLootRecipient(nullptr);
        if (m_sceneActive)
        {
            if (npc_taelan_fordringAI* taelanAI = GetTaelan())
            {
                Creature* taelan = m_creature->GetMap()->GetCreature(m_taelanGuid);
                if (taelanAI->CanBeginEpilogue(m_creature))
                {
                    float x, y, z;
                    taelan->GetContactPoint(m_creature, x, y, z);
                    m_creature->GetMotionMaster()->MovePoint(200, x, y, z);
                }
                else
                    SendAIEvent(AI_EVENT_CUSTOM_C, m_creature, taelan);
            }
        }
        Reset();
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 /*miscValue*/) override
    {
        if (!sender || sender != invoker || sender->GetTypeId() != TYPEID_UNIT ||
            sender->GetEntry() != NPC_TAELAN_FORDRING || sender->GetObjectGuid() != m_creature->GetSpawnerGuid())
            return;
        if (eventType == AI_EVENT_CUSTOM_E)
        {
            EndScene();
            return;
        }
        npc_taelan_fordringAI* taelan = dynamic_cast<npc_taelan_fordringAI*>(sender->AI());
        if (eventType == AI_EVENT_START_ESCORT && !m_sceneActive && taelan && taelan->IsCurrentTirion(m_creature))
        {
            uint32 serial = m_sceneSerial;
            Start(true);
            if (serial != m_sceneSerial)
                return;
            if (HasEscortState(STATE_ESCORT_ESCORTING))
            {
                m_sceneActive = true;
                m_taelanGuid = sender->GetObjectGuid();
            }
        }
    }

    void WaypointReached(uint32 pointId) override
    {
        if (pointId != 3 || !m_sceneActive || !GetTaelan())
            return;
        SetEscortPaused(true);
        m_creature->Unmount();
        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MoveIdle();
        if (Creature* taelan = m_creature->GetMap()->GetCreature(m_taelanGuid))
        {
            float x, y, z;
            taelan->GetContactPoint(m_creature, x, y, z);
            m_creature->GetMotionMaster()->MovePoint(100, x, y, z);
        }
    }

    void Reset() override
    {
        m_holyCleaveTimer = urand(11000, 15000);
        m_holyStrikeTimer = urand(6000, 8000);
        m_crusaderStrike  = urand(1000, 5000);
    }

    void Aggro(Unit* /*who*/) override
    {
        DoCastSpellIfCan(m_creature, SPELL_DEVOTION_AURA);
    }

    void MoveInLineOfSight(Unit* /*who*/) override
    {
        // attack only on request
    }







    void MovementInform(uint32 moveType, uint32 pointId) override
    {
        // custom points; ignore in escort AI
        if (pointId == 100 || pointId == 200)
            return;

        npc_escortAI::MovementInform(moveType, pointId);
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // combat spells
        if (m_holyCleaveTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HOLY_CLEAVE) == CAST_OK)
                m_holyCleaveTimer = urand(12000, 15000);
        }
        else
            m_holyCleaveTimer -= diff;

        if (m_holyStrikeTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HOLY_STRIKE) == CAST_OK)
                m_holyStrikeTimer = urand(8000, 11000);
        }
        else
            m_holyStrikeTimer -= diff;

        if (m_crusaderStrike < diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CRUSADER_STRIKE) == CAST_OK)
                m_crusaderStrike = urand(7000, 9000);
        }
        else
            m_crusaderStrike -= diff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_tirion_fordring(Creature* creature)
{
    return new npc_tirion_fordringAI(creature);
}

/*######
## spell_placing_beacon_torch
######*/

struct spell_placing_beacon_torch : public SpellScript
{
    void OnRadiusCalculate(Spell* /*spell*/, SpellEffectIndex effIdx, bool /*targetB*/, float& radius) const override
    {
        if (effIdx == EFFECT_INDEX_0)
            radius = 0.f;
        if (effIdx == EFFECT_INDEX_1)
            radius = 10.f;
    }
};


void AddSC_western_plaguelands()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_the_scourge_cauldron";
    pNewScript->GetAI = &GetAI_npc_the_scourge_cauldron;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_taelan_fordring";
    pNewScript->GetAI = &GetAI_npc_taelan_fordring;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_taelan_fordring;
    pNewScript->pQuestRewardedNPC = &QuestRewarded_npc_taelan_fordring;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_isillien";
    pNewScript->GetAI = &GetAI_npc_isillien;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_tirion_fordring";
    pNewScript->GetAI = &GetAI_npc_tirion_fordring;
    pNewScript->RegisterSelf();

    RegisterSpellScript<TaelanDeath>("spell_taelan_death");
    RegisterSpellScript<spell_placing_beacon_torch>("spell_placing_beacon_torch");
}
