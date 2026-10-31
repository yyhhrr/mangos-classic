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
SDName: Winterspring
SD%Complete: 100
SDComment: Quest support: 4901.
SDCategory: Winterspring
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_ranshalla
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"

/*######
# npc_ranshalla
####*/

enum
{
    // Escort texts
    SAY_QUEST_START             = -1000739,
    SAY_ENTER_OWL_THICKET       = -1000707,
    SAY_REACH_TORCH_1           = -1000708,
    SAY_REACH_TORCH_2           = -1000709,
    SAY_REACH_TORCH_3           = -1000710,
    SAY_AFTER_TORCH_1           = -1000711,
    SAY_AFTER_TORCH_2           = -1000712,
    SAY_REACH_ALTAR_1           = -1000713,
    SAY_REACH_ALTAR_2           = -1000714,

    // After lighting the altar cinematic
    SAY_RANSHALLA_ALTAR_1       = -1000715,
    SAY_RANSHALLA_ALTAR_2       = -1000716,
    SAY_PRIESTESS_ALTAR_3       = -1000717,
    SAY_PRIESTESS_ALTAR_4       = -1000718,
    SAY_RANSHALLA_ALTAR_5       = -1000719,
    SAY_RANSHALLA_ALTAR_6       = -1000720,
    SAY_PRIESTESS_ALTAR_7       = -1000721,
    SAY_PRIESTESS_ALTAR_8       = -1000722,
    SAY_PRIESTESS_ALTAR_9       = -1000723,
    SAY_PRIESTESS_ALTAR_10      = -1000724,
    SAY_PRIESTESS_ALTAR_11      = -1000725,
    SAY_PRIESTESS_ALTAR_12      = -1000726,
    SAY_PRIESTESS_ALTAR_13      = -1000727,
    SAY_PRIESTESS_ALTAR_14      = -1000728,
    SAY_VOICE_ALTAR_15          = -1000729,
    SAY_PRIESTESS_ALTAR_16      = -1000730,
    SAY_PRIESTESS_ALTAR_17      = -1000731,
    SAY_PRIESTESS_ALTAR_18      = -1000732,
    SAY_PRIESTESS_ALTAR_19      = -1000733,
    SAY_PRIESTESS_ALTAR_20      = -1000734,
    SAY_PRIESTESS_ALTAR_21      = -1000735,
    SAY_QUEST_END_1             = -1000736,
    SAY_QUEST_END_2             = -1000737,

    EMOTE_CHANT_SPELL           = -1000738,

    SPELL_LIGHT_TORCH           = 18953,        // channeled spell by Ranshalla while waiting for the torches / altar
    SPELL_RANSHALLA_DESPAWN     = 18954,
    SPELL_BIND_WILDKIN          = 18994,

    NPC_RANSHALLA               = 10300,
    NPC_PRIESTESS_ELUNE         = 12116,
    NPC_VOICE_ELUNE             = 12152,
    NPC_GUARDIAN_ELUNE          = 12140,

    NPC_PRIESTESS_DATA_1        = 1,            // dummy member for the first priestess (right)
    NPC_PRIESTESS_DATA_2        = 2,            // dummy member for the second priestess (left)
    DATA_MOVE_PRIESTESS         = 3,            // dummy member to check the priestess movement
    DATA_EVENT_END              = 4,            // dummy member to indicate the event end

    GO_ELUNE_ALTAR              = 177404,
    GO_ELUNE_FIRE               = 177417,
    GO_ELUNE_GEM                = 177414,       // is respawned in script
    GO_ELUNE_LIGHT              = 177415,       // are respawned in script
    GO_ELUNE_AURA               = 177416,       // is respawned in script

    QUEST_GUARDIANS_ALTAR       = 4901,
};

static const DialogueEntry aIntroDialogue[] =
{
    {SAY_REACH_ALTAR_1,      NPC_RANSHALLA,        2000},
    {SAY_REACH_ALTAR_2,      NPC_RANSHALLA,        3000},
    {NPC_RANSHALLA,          0,                    0},      // start the altar channeling
    {SAY_PRIESTESS_ALTAR_3,  NPC_PRIESTESS_DATA_2, 1000},
    {SAY_PRIESTESS_ALTAR_4,  NPC_PRIESTESS_DATA_1, 4000},
    {SAY_RANSHALLA_ALTAR_5,  NPC_RANSHALLA,        4000},
    {SAY_RANSHALLA_ALTAR_6,  NPC_RANSHALLA,        4000},   // start the escort here
    {SAY_PRIESTESS_ALTAR_7,  NPC_PRIESTESS_DATA_2, 4000},
    {SAY_PRIESTESS_ALTAR_8,  NPC_PRIESTESS_DATA_2, 5000},   // show the gem
    {GO_ELUNE_GEM,           0,                    5000},
    {SAY_PRIESTESS_ALTAR_9,  NPC_PRIESTESS_DATA_1, 4000},   // move priestess 1 near m_creature
    {NPC_PRIESTESS_DATA_1,   0,                    3000},
    {SAY_PRIESTESS_ALTAR_10, NPC_PRIESTESS_DATA_1, 5000},
    {SAY_PRIESTESS_ALTAR_11, NPC_PRIESTESS_DATA_1, 4000},
    {SAY_PRIESTESS_ALTAR_12, NPC_PRIESTESS_DATA_1, 5000},
    {SAY_PRIESTESS_ALTAR_13, NPC_PRIESTESS_DATA_1, 8000},   // summon voice and guard of elune
    {NPC_VOICE_ELUNE,        0,                    12000},
    {SAY_VOICE_ALTAR_15,     NPC_VOICE_ELUNE,      5000},   // move priestess 2 near m_creature
    {NPC_PRIESTESS_DATA_2,   0,                    3000},
    {SAY_PRIESTESS_ALTAR_16, NPC_PRIESTESS_DATA_2, 4000},
    {SAY_PRIESTESS_ALTAR_17, NPC_PRIESTESS_DATA_2, 6000},
    {SAY_PRIESTESS_ALTAR_18, NPC_PRIESTESS_DATA_1, 5000},
    {SAY_PRIESTESS_ALTAR_19, NPC_PRIESTESS_DATA_1, 3000},   // move the owlbeast
    {NPC_GUARDIAN_ELUNE,     0,                    2000},
    {SAY_PRIESTESS_ALTAR_20, NPC_PRIESTESS_DATA_1, 4000},   // move the first priestess up
    {SAY_PRIESTESS_ALTAR_21, NPC_PRIESTESS_DATA_2, 10000},  // move second priestess up
    {DATA_MOVE_PRIESTESS,    0,                    6000},   // despawn the gem
    {DATA_EVENT_END,         0,                    2000},   // turn towards the player
    {SAY_QUEST_END_2,        NPC_RANSHALLA,        0},
    {0, 0, 0},
};

static Position aWingThicketLocations[] =
{
    {5515.98f, -4903.43f, 846.30f, 4.58f},      // 0 right priestess summon loc
    {5501.94f, -4920.20f, 848.69f, 6.15f},      // 1 left priestess summon loc
    {5497.35f, -4906.49f, 850.83f, 2.76f},      // 2 guard of elune summon loc
    {5518.38f, -4913.47f, 845.57f},             // 3 right priestess move loc
    {5510.36f, -4921.17f, 846.33f},             // 4 left priestess move loc
    {5511.31f, -4913.82f, 847.17f},             // 5 guard of elune move loc
    {5518.51f, -4917.56f, 845.23f},             // 6 right priestess second move loc
    {5514.40f, -4921.16f, 845.49f}              // 7 left priestess second move loc
};

struct npc_ranshallaAI : public npc_escortAI, private DialogueHelper
{
    enum EventPhase
    {
        PHASE_IDLE, PHASE_ESCORTING, PHASE_WAIT_FIRE, PHASE_ALTAR_INTRO,
        PHASE_WAIT_ALTAR, PHASE_CONTINUING, PHASE_WAIT_PRIESTESS,
        PHASE_CINEMATIC, PHASE_COMPLETE
    };

    npc_ranshallaAI(Creature* pCreature) : npc_escortAI(pCreature),
        DialogueHelper(aIntroDialogue), m_uiDelayTimer(0), m_uiCurrentWaypoint(0),
        m_uiFinishTimer(0), m_round(0), m_phase(PHASE_IDLE), m_cleaning(false),
        m_creditPending(false), m_dialogueUpdating(false)
    {
        Reset();
    }

    uint32 m_uiDelayTimer;
    uint32 m_uiCurrentWaypoint;
    uint32 m_uiFinishTimer;
    uint32 m_round;
    EventPhase m_phase;
    bool m_cleaning;
    bool m_creditPending;
    bool m_dialogueUpdating;
    ObjectGuid m_waitingGoGuid;

    ObjectGuid m_firstPriestessGuid;
    ObjectGuid m_secondPriestessGuid;
    ObjectGuid m_guardEluneGuid;
    ObjectGuid m_voiceEluneGuid;
    ObjectGuid m_altarGuid;


    Creature* GetOwnedActor(ObjectGuid guid, uint32 entry, bool requireAlive = true)
    {
        Creature* actor = m_creature->GetMap()->GetCreature(guid);
        if (!actor || actor->GetEntry() != entry || actor->GetSpawnerGuid() != m_creature->GetObjectGuid() ||
            (requireAlive && !actor->IsAlive()))
            return nullptr;
        return actor;
    }

    void DespawnOwnedActor(ObjectGuid guid, uint32 entry)
    {
        if (Creature* actor = GetOwnedActor(guid, entry, false))
            actor->ForcedDespawn();
    }

    void ResetScene()
    {
        if (m_cleaning)
            return;

        m_cleaning = true;
        ++m_round;
        m_phase = PHASE_IDLE;
        DisableDialogue();
        m_uiDelayTimer = 0;
        m_uiCurrentWaypoint = 0;
        m_uiFinishTimer = 0;
        m_creditPending = false;
        m_waitingGoGuid.Clear();
        m_altarGuid.Clear();

        ObjectGuid first = m_firstPriestessGuid;
        ObjectGuid second = m_secondPriestessGuid;
        ObjectGuid guard = m_guardEluneGuid;
        ObjectGuid voice = m_voiceEluneGuid;
        m_firstPriestessGuid.Clear();
        m_secondPriestessGuid.Clear();
        m_guardEluneGuid.Clear();
        m_voiceEluneGuid.Clear();

        // Clear local ownership before despawn callbacks can re-enter cleanup.
        DespawnOwnedActor(first, NPC_PRIESTESS_ELUNE);
        DespawnOwnedActor(second, NPC_PRIESTESS_ELUNE);
        DespawnOwnedActor(guard, NPC_GUARDIAN_ELUNE);
        DespawnOwnedActor(voice, NPC_VOICE_ELUNE);
        m_cleaning = false;
    }

    void BeginEscort(Player* player, const Quest* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_GUARDIANS_ALTAR || m_cleaning || m_dialogueUpdating ||
            m_phase != PHASE_IDLE || HasEscortState(STATE_ESCORT_ESCORTING) ||
            !m_creature->IsAlive() || m_creature->GetVictim())
            return;

        uint32 round = m_round;
        Start(false, player, quest, true);
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_round != round)
            return;

        ++m_round;
        m_phase = PHASE_ESCORTING;
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->SetFactionTemporary(FACTION_ESCORT_A_NEUTRAL_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
        DoScriptText(SAY_QUEST_START, m_creature);
    }

    void AbortScene()
    {
        if (m_cleaning || m_phase == PHASE_IDLE || m_phase == PHASE_COMPLETE)
            return;
        ResetScene();
        uint32 round = m_round;
        FailQuestForPlayerAndGroup();
        if (round == m_round && m_phase == PHASE_IDLE)
            m_creature->ForcedDespawn();
    }

    void JustDied(Unit* killer) override
    {
        ResetScene();
        npc_escortAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        ResetScene();
        npc_escortAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        ResetScene();
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
    }

    void Reset() override
    {
        // Combat evade must retain a torch point and an accepted continuation.
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            ResetScene();
    }

    // Called when the player activates the torch / altar
    void DoContinueEscort(GameObject* object)
    {
        if (!object || m_cleaning || !m_creature->IsAlive() || !HasEscortState(STATE_ESCORT_ESCORTING) ||
            !HasEscortState(STATE_ESCORT_PAUSED) ||
            (m_phase != PHASE_WAIT_FIRE && m_phase != PHASE_WAIT_ALTAR) ||
            object->GetMap() != m_creature->GetMap() || object->GetObjectGuid() != m_waitingGoGuid ||
            !m_creature->IsWithinDistInMap(object, 10.0f))
            return;

        bool isAltar = m_phase == PHASE_WAIT_ALTAR;
        if (object->GetEntry() != (isAltar ? GO_ELUNE_ALTAR : GO_ELUNE_FIRE))
            return;

        m_waitingGoGuid.Clear();
        m_phase = PHASE_CONTINUING;
        m_uiDelayTimer = 2000;
        if (isAltar)
            DoScriptText(SAY_RANSHALLA_ALTAR_1, m_creature);
        else
        {
            switch (urand(0, 1))
            {
                case 0: DoScriptText(SAY_AFTER_TORCH_1, m_creature); break;
                case 1: DoScriptText(SAY_AFTER_TORCH_2, m_creature); break;
            }
        }
    }

    void SpellHit(Unit* /*pCaster*/, const SpellEntry* spell) override
    {
        if (spell && spell->Id == SPELL_RANSHALLA_DESPAWN &&
            (m_phase == PHASE_WAIT_FIRE || m_phase == PHASE_WAIT_ALTAR || m_phase == PHASE_CONTINUING))
            AbortScene();
    }

    // Called when Ranshalla starts to channel on a torch / altar
    void DoChannelTorchSpell(bool isAltar = false)
    {
        if (m_cleaning || !HasEscortState(STATE_ESCORT_ESCORTING) ||
            m_phase != (isAltar ? PHASE_ALTAR_INTRO : PHASE_ESCORTING))
            return;

        m_waitingGoGuid.Clear();
        m_phase = isAltar ? PHASE_WAIT_ALTAR : PHASE_WAIT_FIRE;
        if (GameObject* object = GetClosestGameObjectWithEntry(m_creature, isAltar ? GO_ELUNE_ALTAR : GO_ELUNE_FIRE, 10.0f))
        {
            m_waitingGoGuid = object->GetObjectGuid();
            object->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            if (isAltar)
            {
                m_creature->SetFacingToObject(object);
                m_altarGuid = object->GetObjectGuid();
            }
        }

        switch (urand(0, 2))
        {
            case 0: DoScriptText(SAY_REACH_TORCH_1, m_creature); break;
            case 1: DoScriptText(SAY_REACH_TORCH_2, m_creature); break;
            case 2: DoScriptText(SAY_REACH_TORCH_3, m_creature); break;
        }
        DoScriptText(EMOTE_CHANT_SPELL, m_creature);
        DoCastSpellIfCan(m_creature, SPELL_LIGHT_TORCH);
        SetEscortPaused(true);
    }

    bool DoSummonPriestess()
    {
        if (m_cleaning || m_phase != PHASE_WAIT_PRIESTESS || !HasEscortState(STATE_ESCORT_ESCORTING))
            return false;
        uint32 round = m_round;
        if (Creature* priestess = m_creature->SummonCreature(NPC_PRIESTESS_ELUNE, aWingThicketLocations[0].x, aWingThicketLocations[0].y, aWingThicketLocations[0].z, aWingThicketLocations[0].o, TEMPSPAWN_CORPSE_DESPAWN, 0))
        {
            if (round != m_round || m_phase != PHASE_WAIT_PRIESTESS)
            {
                DespawnOwnedActor(priestess->GetObjectGuid(), NPC_PRIESTESS_ELUNE);
                return false;
            }
            m_firstPriestessGuid = priestess->GetObjectGuid();
            priestess->GetMotionMaster()->MovePoint(0, aWingThicketLocations[3].x, aWingThicketLocations[3].y, aWingThicketLocations[3].z);
        }
        if (round != m_round || m_phase != PHASE_WAIT_PRIESTESS)
            return false;

        if (Creature* priestess = m_creature->SummonCreature(NPC_PRIESTESS_ELUNE, aWingThicketLocations[1].x, aWingThicketLocations[1].y, aWingThicketLocations[1].z, aWingThicketLocations[1].o, TEMPSPAWN_CORPSE_DESPAWN, 0))
        {
            if (round != m_round || m_phase != PHASE_WAIT_PRIESTESS)
            {
                DespawnOwnedActor(priestess->GetObjectGuid(), NPC_PRIESTESS_ELUNE);
                return false;
            }
            m_secondPriestessGuid = priestess->GetObjectGuid();
            priestess->GetMotionMaster()->MovePoint(1, aWingThicketLocations[4].x, aWingThicketLocations[4].y, aWingThicketLocations[4].z);
            return true;
        }

        AbortScene();
        return false;
    }

    void SummonedMovementInform(Creature* summoned, uint32 type, uint32 point) override
    {
        if (m_cleaning || m_phase != PHASE_WAIT_PRIESTESS || !HasEscortState(STATE_ESCORT_ESCORTING) ||
            !m_creature->IsAlive() || type != POINT_MOTION_TYPE || point != 1 || !summoned ||
            summoned != GetOwnedActor(m_secondPriestessGuid, NPC_PRIESTESS_ELUNE))
            return;

        m_phase = PHASE_CINEMATIC;
        StartNextDialogueText(SAY_PRIESTESS_ALTAR_3);
    }

    void WaypointReached(uint32 uiPointId) override
    {
        if (m_cleaning || !HasEscortState(STATE_ESCORT_ESCORTING) || m_phase == PHASE_IDLE || m_phase == PHASE_COMPLETE)
            return;
        switch (uiPointId)
        {
            case 3:
                DoScriptText(SAY_ENTER_OWL_THICKET, m_creature);
                break;
            case 10: // Cavern 1
            case 15: // Cavern 2
            case 20: // Cavern 3
            case 25: // Cavern 4
            case 36: // Cavern 5
                if (m_phase != PHASE_ESCORTING)
                    break;
                m_uiCurrentWaypoint = uiPointId;
                DoChannelTorchSpell();
                break;
            case 39:
                if (m_phase != PHASE_ESCORTING)
                    break;
                m_phase = PHASE_ALTAR_INTRO;
                m_uiCurrentWaypoint = uiPointId;
                StartNextDialogueText(SAY_REACH_ALTAR_1);
                SetEscortPaused(true);
                break;
            case 41:
            {
                // Search for all nearest lights and respawn them
                GameObjectList m_lEluneLights;
                GetGameObjectListWithEntryInGrid(m_lEluneLights, m_creature, GO_ELUNE_LIGHT, 20.0f);
                for (GameObjectList::const_iterator itr = m_lEluneLights.begin(); itr != m_lEluneLights.end(); ++itr)
                {
                    if ((*itr)->IsSpawned())
                        continue;

                    (*itr)->SetRespawnTime(115);
                    (*itr)->Refresh();
                }

                if (GameObject* pAltar = m_creature->GetMap()->GetGameObject(m_altarGuid))
                    m_creature->SetFacingToObject(pAltar);
                break;
            }
            case 42:
                if (m_phase != PHASE_ESCORTING)
                    break;
                m_phase = PHASE_WAIT_PRIESTESS;
                SetEscortPaused(true);
                if (DoSummonPriestess())
                    DoScriptText(SAY_RANSHALLA_ALTAR_2, m_creature);
                break;
            case 44:
                // Stop the escort and turn towards the altar
                SetEscortPaused(true);
                if (GameObject* pAltar = m_creature->GetMap()->GetGameObject(m_altarGuid))
                    m_creature->SetFacingToObject(pAltar);
                break;
        }
    }

    void JustDidDialogueStep(int32 iEntry) override
    {
        if (m_cleaning || !HasEscortState(STATE_ESCORT_ESCORTING) ||
            (m_phase != PHASE_ALTAR_INTRO && m_phase != PHASE_CINEMATIC))
            return;
        switch (iEntry)
        {
            case NPC_RANSHALLA:
                // Start the altar channeling
                DoChannelTorchSpell(true);
                break;
            case SAY_RANSHALLA_ALTAR_6:
                SetEscortPaused(false);
                break;
            case SAY_PRIESTESS_ALTAR_8:
                // make the gem and its aura respawn
                if (GameObject* pGem = GetClosestGameObjectWithEntry(m_creature, GO_ELUNE_GEM, 10.0f))
                {
                    if (pGem->IsSpawned())
                        break;

                    pGem->SetRespawnTime(90);
                    pGem->Refresh();
                }
                if (GameObject* pAura = GetClosestGameObjectWithEntry(m_creature, GO_ELUNE_AURA, 10.0f))
                {
                    if (pAura->IsSpawned())
                        break;

                    pAura->SetRespawnTime(90);
                    pAura->Refresh();
                }
                break;
            case SAY_PRIESTESS_ALTAR_9:
                // move near the escort npc
                if (Creature* pPriestess = GetOwnedActor(m_firstPriestessGuid, NPC_PRIESTESS_ELUNE))
                    pPriestess->GetMotionMaster()->MovePoint(0, aWingThicketLocations[6].x, aWingThicketLocations[6].y, aWingThicketLocations[6].z);
                break;
            case SAY_PRIESTESS_ALTAR_13:
            {
                uint32 round = m_round;
                if (Creature* guard = m_creature->SummonCreature(NPC_GUARDIAN_ELUNE, aWingThicketLocations[2].x, aWingThicketLocations[2].y, aWingThicketLocations[2].z, aWingThicketLocations[2].o, TEMPSPAWN_CORPSE_DESPAWN, 0))
                {
                    if (round != m_round || m_phase != PHASE_CINEMATIC)
                    {
                        DespawnOwnedActor(guard->GetObjectGuid(), NPC_GUARDIAN_ELUNE);
                        break;
                    }
                    m_guardEluneGuid = guard->GetObjectGuid();
                    guard->GetMotionMaster()->MovePoint(0, aWingThicketLocations[5].x, aWingThicketLocations[5].y, aWingThicketLocations[5].z);
                }
                if (round != m_round || m_phase != PHASE_CINEMATIC)
                    break;
                if (GameObject* altar = m_creature->GetMap()->GetGameObject(m_altarGuid))
                {
                    if (Creature* voice = m_creature->SummonCreature(NPC_VOICE_ELUNE, altar->GetPositionX(), altar->GetPositionY(), altar->GetPositionZ(), 0, TEMPSPAWN_TIMED_DESPAWN, 30000))
                    {
                        if (round == m_round && m_phase == PHASE_CINEMATIC)
                            m_voiceEluneGuid = voice->GetObjectGuid();
                        else
                            DespawnOwnedActor(voice->GetObjectGuid(), NPC_VOICE_ELUNE);
                    }
                }
                break;
            }
            case SAY_VOICE_ALTAR_15:
                // move near the escort npc and continue dialogue
                if (Creature* pPriestess = GetOwnedActor(m_secondPriestessGuid, NPC_PRIESTESS_ELUNE))
                {
                    DoScriptText(SAY_PRIESTESS_ALTAR_14, pPriestess);
                    pPriestess->GetMotionMaster()->MovePoint(0, aWingThicketLocations[7].x, aWingThicketLocations[7].y, aWingThicketLocations[7].z);
                }
                // make the voice of elune cast Bind Wildkin
                if (Creature* pGuard = GetOwnedActor(m_guardEluneGuid, NPC_GUARDIAN_ELUNE))
                    pGuard->CastSpell(pGuard, SPELL_BIND_WILDKIN, TRIGGERED_NONE);
                break;
            case SAY_PRIESTESS_ALTAR_19:
                // make the voice of elune leave
                if (Creature* pGuard = GetOwnedActor(m_guardEluneGuid, NPC_GUARDIAN_ELUNE))
                {
                    pGuard->GetMotionMaster()->MovePoint(0, aWingThicketLocations[2].x, aWingThicketLocations[2].y, aWingThicketLocations[2].z);
                    pGuard->ForcedDespawn(4000);
                }
                break;
            case SAY_PRIESTESS_ALTAR_20:
                // make the first priestess leave
                if (Creature* pPriestess = GetOwnedActor(m_firstPriestessGuid, NPC_PRIESTESS_ELUNE))
                {
                    pPriestess->GetMotionMaster()->MovePoint(0, aWingThicketLocations[0].x, aWingThicketLocations[0].y, aWingThicketLocations[0].z);
                    pPriestess->ForcedDespawn(4000);
                }
                break;
            case SAY_PRIESTESS_ALTAR_21:
                // make the second priestess leave
                if (Creature* pPriestess = GetOwnedActor(m_secondPriestessGuid, NPC_PRIESTESS_ELUNE))
                {
                    pPriestess->GetMotionMaster()->MovePoint(0, aWingThicketLocations[1].x, aWingThicketLocations[1].y, aWingThicketLocations[1].z);
                    pPriestess->ForcedDespawn(4000);
                }
                break;
            case DATA_EVENT_END:
                // Turn towards the player
                if (Player* pPlayer = GetPlayerForEscort())
                {
                    m_creature->SetFacingToObject(pPlayer);
                    DoScriptText(SAY_QUEST_END_1, m_creature, pPlayer);
                }
                break;
            case SAY_QUEST_END_2:
                m_phase = PHASE_COMPLETE;
                DisableDialogue();
                m_uiDelayTimer = 0;
                m_waitingGoGuid.Clear();
                m_uiFinishTimer = 1 * MINUTE * IN_MILLISECONDS;
                // Turn towards the altar and kneel - quest complete
                if (GameObject* pAltar = m_creature->GetMap()->GetGameObject(m_altarGuid))
                    m_creature->SetFacingToObject(pAltar);
                m_creature->SetStandState(UNIT_STAND_STATE_KNEEL);
                m_creditPending = true;
                break;
        }
    }

    Creature* GetSpeakerByEntry(uint32 uiEntry) override
    {
        switch (uiEntry)
        {
            case NPC_RANSHALLA:        return m_creature;
            case NPC_VOICE_ELUNE:      return GetOwnedActor(m_voiceEluneGuid, NPC_VOICE_ELUNE);
            case NPC_PRIESTESS_DATA_1: return GetOwnedActor(m_firstPriestessGuid, NPC_PRIESTESS_ELUNE);
            case NPC_PRIESTESS_DATA_2: return GetOwnedActor(m_secondPriestessGuid, NPC_PRIESTESS_ELUNE);

            default:
                return nullptr;
        }
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (m_dialogueUpdating)
            return;
        uint32 round = m_round;
        if (m_phase == PHASE_COMPLETE && m_uiFinishTimer)
        {
            if (m_uiFinishTimer <= diff)
            {
                ResetScene();
                m_creature->ForcedDespawn();
                return;
            }
            m_uiFinishTimer -= diff;
        }

        if (m_phase == PHASE_ALTAR_INTRO || m_phase == PHASE_CINEMATIC)
        {
            m_dialogueUpdating = true;
            DialogueUpdate(diff);
            m_dialogueUpdating = false;
        }
        if (round != m_round)
            return;

        // The helper has finished advancing its pointer before quest callbacks.
        if (m_creditPending && m_phase == PHASE_COMPLETE)
        {
            m_creditPending = false;
            if (Player* player = GetPlayerForEscort())
                player->RewardPlayerAndGroupAtEventExplored(QUEST_GUARDIANS_ALTAR, m_creature);
            if (round != m_round)
                return;
        }

        if (m_uiDelayTimer)
        {
            if (m_uiDelayTimer <= diff)
            {
                uint32 nextPoint = m_uiCurrentWaypoint + 1;
                m_uiDelayTimer = 0;
                m_phase = PHASE_ESCORTING;
                m_creature->InterruptNonMeleeSpells(false);
                if (round != m_round)
                    return;
                SetEscortPaused(false);
                if (round != m_round)
                    return;
                m_creature->GetMotionMaster()->SetNextWaypoint(nextPoint);
            }
            else
                m_uiDelayTimer -= diff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;
        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_ranshalla(Creature* pCreature)
{
    return new npc_ranshallaAI(pCreature);
}

bool QuestAccept_npc_ranshalla(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_GUARDIANS_ALTAR)
    {
        if (npc_ranshallaAI* pEscortAI = dynamic_cast<npc_ranshallaAI*>(pCreature->AI()))
            pEscortAI->BeginEscort(pPlayer, pQuest);

        return true;
    }

    return false;
}

bool GOUse_go_elune_fire(Player* /*pPlayer*/, GameObject* pGo)
{
    if (pGo->GetEntry() != GO_ELUNE_ALTAR && pGo->GetEntry() != GO_ELUNE_FIRE)
        return false;

    if (Creature* ranshalla = GetClosestCreatureWithEntry(pGo, NPC_RANSHALLA, 10.0f))
        if (npc_ranshallaAI* escortAI = dynamic_cast<npc_ranshallaAI*>(ranshalla->AI()))
            escortAI->DoContinueEscort(pGo);
    return false;
}

enum
{
    SPELL_FOOLS_PLIGHT              = 23504,

    SPELL_DEMONIC_FRENZY            = 23257,
    SPELL_DEMONIC_DOOM              = 23298,
    SPELL_STINGING_TRAUMA           = 23299,

    EMOTE_POISON                    = -1001251,

    NPC_ARTORIUS_THE_AMIABLE        = 14531,
    NPC_ARTORIUS_THE_DOOMBRINGER    = 14535,
    NPC_THE_CLEANER                 = 14503,

    QUEST_STAVE_OF_THE_ANCIENTS     = 7636
};

#define GOSSIP_ITEM                 "Show me your real face, demon."

/*######
## npc_artorius_the_amiable
######*/

/*######
## npc_artorius_the_doombringer
######*/

struct npc_artoriusAI : public ScriptedAI
{
    npc_artoriusAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_bTransform = false;
        m_uiDespawn_Timer = 0;
        Reset();
    }

    uint32 m_uiTransform_Timer;
    uint32 m_uiTransformEmote_Timer;
    bool m_bTransform;

    ObjectGuid m_hunterGuid;
    uint32 m_uiDemonic_Doom_Timer;
    uint32 m_uiDemonic_Frenzy_Timer;
    uint32 m_uiDespawn_Timer;

    void Reset() override
    {
        switch (m_creature->GetEntry())
        {
            case NPC_ARTORIUS_THE_AMIABLE:
                m_creature->SetRespawnDelay(35 * MINUTE);
                m_creature->SetRespawnTime(35 * MINUTE);
                m_creature->NearTeleportTo(7909.71f, -4598.67f, 710.008f, 0.606013f);
                if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != WAYPOINT_MOTION_TYPE)
                {
                    m_creature->SetDefaultMovementType(WAYPOINT_MOTION_TYPE);
                    m_creature->GetMotionMaster()->Initialize();
                }

                m_creature->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);

                m_uiTransform_Timer = 10000;
                m_uiTransformEmote_Timer = 5000;
                m_bTransform = false;
                m_uiDespawn_Timer = 0;
                break;
            case NPC_ARTORIUS_THE_DOOMBRINGER:
                if (!m_uiDespawn_Timer)
                    m_uiDespawn_Timer = 20 * MINUTE*IN_MILLISECONDS;

                m_hunterGuid.Clear();
                m_uiDemonic_Doom_Timer = 7500;
                m_uiDemonic_Frenzy_Timer = urand(5000, 8000);
                break;
        }
    }

    /** Artorius the Amiable */
    void Transform()
    {
        m_creature->UpdateEntry(NPC_ARTORIUS_THE_DOOMBRINGER);
        Reset();
    }

    bool CanBeginEvent(Player* player) const
    {
        return player && player->IsAlive() && player->IsInWorld() &&
            player->GetMap() == m_creature->GetMap() && player->getClass() == CLASS_HUNTER &&
            player->GetQuestStatus(QUEST_STAVE_OF_THE_ANCIENTS) == QUEST_STATUS_INCOMPLETE &&
            m_creature->GetEntry() == NPC_ARTORIUS_THE_AMIABLE && m_creature->IsAlive() &&
            m_creature->IsInWorld() && !m_creature->IsInCombat() && !m_bTransform &&
            !m_uiDespawn_Timer && m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    void BeginEvent(Player* player)
    {
        if (!CanBeginEvent(player))
            return;

        m_hunterGuid = player->GetObjectGuid();
        m_creature->GetMotionMaster()->Clear(false);
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_NONE);
        m_bTransform = true;
    }

    /** Artorius the Doombringer */
    void Aggro(Unit* pWho) override
    {
        if (pWho->getClass() == CLASS_HUNTER && (m_hunterGuid.IsEmpty() || m_hunterGuid == pWho->GetObjectGuid()))
        {
            m_hunterGuid = pWho->GetObjectGuid();
        }
        else
            DemonDespawn();
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        uint32 m_respawn_delay_Timer = 2 * HOUR;
        m_creature->SetRespawnDelay(m_respawn_delay_Timer);
        m_creature->SetRespawnTime(m_respawn_delay_Timer);
        m_creature->SaveRespawnTime();
    }

    void DemonDespawn(bool triggered = true)
    {
        m_creature->SetRespawnDelay(15 * MINUTE);
        m_creature->SetRespawnTime(15 * MINUTE);
        m_creature->SaveRespawnTime();

        if (triggered)
        {
            Creature* pCleaner = m_creature->SummonCreature(NPC_THE_CLEANER, m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ(), m_creature->GetAngle(m_creature), TEMPSPAWN_DEAD_DESPAWN, 20 * MINUTE*IN_MILLISECONDS);
            if (pCleaner)
            {
                ThreatList const& tList = m_creature->getThreatManager().getThreatList();

                for (auto itr : tList)
                {
                    if (Unit* pUnit = m_creature->GetMap()->GetUnit(itr->getUnitGuid()))
                    {
                        if (pUnit->IsAlive())
                            pCleaner->AI()->AttackStart(pUnit);
                    }
                }
            }
        }

        m_creature->ForcedDespawn();
    }

    void SpellHit(Unit* /*pCaster*/, const SpellEntry* pSpell) override
    {
        if (pSpell->Id == 13555 || pSpell->Id == 25295)             // Serpent Sting (Rank 8 or Rank 9)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_STINGING_TRAUMA, CAST_TRIGGERED) == CAST_OK)
                DoScriptText(EMOTE_POISON, m_creature);
        }
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        /** Artorius the Amiable */
        if (m_bTransform)
        {
            if (m_uiTransformEmote_Timer)
            {
                if (m_uiTransformEmote_Timer <= uiDiff)
                {
                    m_creature->HandleEmote(EMOTE_ONESHOT_ROAR);
                    m_uiTransformEmote_Timer = 0;
                }
                else
                    m_uiTransformEmote_Timer -= uiDiff;
            }

            if (m_uiTransform_Timer < uiDiff)
            {
                m_bTransform = false;
                Transform();
            }
            else
                m_uiTransform_Timer -= uiDiff;
        }

        /** Artorius the Doombringer */
        if (m_uiDespawn_Timer)
        {
            if (m_uiDespawn_Timer <= uiDiff)
            {
                if (m_creature->IsAlive() && !m_creature->IsInCombat())
                {
                    DemonDespawn(false);
                    return;
                }
            }
            else
                m_uiDespawn_Timer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_creature->getThreatManager().getThreatList().size() > 1)
        {
            DemonDespawn();
            return;
        }

        if (m_uiDemonic_Frenzy_Timer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_DEMONIC_FRENZY) == CAST_OK)
                m_uiDemonic_Frenzy_Timer = urand(15000, 20000);
        }
        else
            m_uiDemonic_Frenzy_Timer -= uiDiff;

        if (m_uiDemonic_Doom_Timer < uiDiff)
        {
            m_uiDemonic_Doom_Timer = 7500;
            // only attempt to cast this once every 7.5 seconds to give the hunter some leeway
            // LOWER max range for lag...
            if (m_creature->IsWithinDistInMap(m_creature->GetVictim(), 25))
                DoCastSpellIfCan(m_creature->GetVictim(), SPELL_DEMONIC_DOOM);
        }
        else
            m_uiDemonic_Doom_Timer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

bool GossipHello_npc_artorius(Player* pPlayer, Creature* pCreature)
{
    if (npc_artoriusAI* ai = dynamic_cast<npc_artoriusAI*>(pCreature->AI()))
        if (ai->CanBeginEvent(pPlayer))
            pPlayer->ADD_GOSSIP_ITEM(0, GOSSIP_ITEM, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

    pPlayer->SEND_GOSSIP_MENU(pPlayer->GetGossipTextId(pCreature), pCreature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_artorius(Player* pPlayer, Creature* pCreature, uint32 uiSender, uint32 uiAction)
{
    pPlayer->CLOSE_GOSSIP_MENU();
    if (uiSender == GOSSIP_SENDER_MAIN && uiAction == GOSSIP_ACTION_INFO_DEF)
        if (npc_artoriusAI* ai = dynamic_cast<npc_artoriusAI*>(pCreature->AI()))
            ai->BeginEvent(pPlayer);
    return true;
}

UnitAI* GetAI_npc_artorius(Creature* pCreature)
{
    return new npc_artoriusAI(pCreature);
}

void AddSC_winterspring()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_ranshalla";
    pNewScript->GetAI = &GetAI_npc_ranshalla;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_ranshalla;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_elune_fire";
    pNewScript->pGOUse = &GOUse_go_elune_fire;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_artorius";
    pNewScript->GetAI = &GetAI_npc_artorius;
    pNewScript->pGossipHello = &GossipHello_npc_artorius;
    pNewScript->pGossipSelect = &GossipSelect_npc_artorius;
    pNewScript->RegisterSelf();
}
