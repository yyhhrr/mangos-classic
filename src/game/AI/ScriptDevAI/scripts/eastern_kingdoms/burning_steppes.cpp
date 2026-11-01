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
SDName: Burning_Steppes
SD%Complete: 100
SDComment: Quest support: 4121, 4122
SDCategory: Burning Steppes
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_grark_lorkrub
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"

/*######
## npc_grark_lorkrub
######*/

enum
{
    SAY_START                   = -1000873,
    SAY_PAY                     = -1000874,
    SAY_FIRST_AMBUSH_START      = -1000875,
    SAY_FIRST_AMBUSH_END        = -1000876,
    SAY_SEC_AMBUSH_START        = -1000877,
    SAY_SEC_AMBUSH_END          = -1000878,
    SAY_THIRD_AMBUSH_START      = -1000879,
    SAY_THIRD_AMBUSH_END        = -1000880,
    EMOTE_LAUGH                 = -1000881,
    SAY_LAST_STAND              = -1000882,
    SAY_LEXLORT_1               = -1000883,
    SAY_LEXLORT_2               = -1000884,
    EMOTE_RAISE_AXE             = -1000885,
    EMOTE_LOWER_HAND            = -1000886,
    SAY_LEXLORT_3               = -1000887,
    SAY_LEXLORT_4               = -1000888,

    EMOTE_SUBMIT                = -1000889,
    SAY_AGGRO                   = -1000890,

    SPELL_CAPTURE_GRARK             = 14250,

    NPC_BLACKROCK_AMBUSHER          = 9522,
    NPC_BLACKROCK_RAIDER            = 9605,
    NPC_FLAMESCALE_DRAGONSPAWN      = 7042,
    NPC_SEARSCALE_DRAKE             = 7046,

    NPC_GRARK_LORKRUB               = 9520,
    NPC_HIGH_EXECUTIONER_NUZARK     = 9538,
    NPC_SHADOW_OF_LEXLORT           = 9539,

    FACTION_GRARK_FRIENDLY          = 29,

    QUEST_ID_PRECARIOUS_PREDICAMENT = 4121
};

static const DialogueEntry aOutroDialogue[] =
{
    {SAY_LAST_STAND,    NPC_GRARK_LORKRUB,              5000},
    {SAY_LEXLORT_1,     NPC_SHADOW_OF_LEXLORT,          3000},
    {SAY_LEXLORT_2,     NPC_SHADOW_OF_LEXLORT,          5000},
    {EMOTE_RAISE_AXE,   NPC_HIGH_EXECUTIONER_NUZARK,    4000},
    {EMOTE_LOWER_HAND,  NPC_SHADOW_OF_LEXLORT,          3000},
    {SAY_LEXLORT_3,     NPC_SHADOW_OF_LEXLORT,          3000},
    {NPC_GRARK_LORKRUB, 0,                              5000},
    {SAY_LEXLORT_4,     NPC_SHADOW_OF_LEXLORT,          0},
    {0, 0, 0},
};

struct npc_grark_lorkrubAI : public npc_escortAI, private DialogueHelper
{
    npc_grark_lorkrubAI(Creature* creature) : npc_escortAI(creature), DialogueHelper(aOutroDialogue) { Reset(); }

    ObjectGuid m_nuzarkGuid;
    ObjectGuid m_lexlortGuid;
    GuidList m_lSearscaleGuidList;
    GuidList m_ownedActors;
    GuidList m_waveActors;
    uint32 m_round = 0;
    uint32 m_dialogueRound = 0;
    uint32 m_expectedEntry = 0;
    uint8 m_uiKilledCreatures = 0;
    uint8 m_wave = 0;
    uint8 m_waveDeaths = 0;
    uint8 m_completedWaves = 0;
    bool m_bIsFirstSearScale = true;
    bool m_shackled = false;
    bool m_eventActive = false;
    bool m_spawningWave = false;
    bool m_drakesAnnounced = false;
    bool m_outroStarted = false;
    bool m_abortPending = false;
    bool m_creditPending = false;
    bool m_finished = false;
    bool m_updatingDialogue = false;
    bool m_dialoguePending = false;

    bool IsEventEntry(uint32 entry) const
    {
        return entry == NPC_BLACKROCK_AMBUSHER || entry == NPC_BLACKROCK_RAIDER ||
            entry == NPC_FLAMESCALE_DRAGONSPAWN || entry == NPC_SEARSCALE_DRAKE ||
            entry == NPC_HIGH_EXECUTIONER_NUZARK || entry == NPC_SHADOW_OF_LEXLORT;
    }

    bool IsOwned(Creature* actor) const
    {
        return actor && IsEventEntry(actor->GetEntry()) && actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    bool IsMember(GuidList const& actors, ObjectGuid guid) const
    {
        return std::find(actors.begin(), actors.end(), guid) != actors.end();
    }

    void CleanupEvent()
    {
        m_eventActive = false;
        ++m_round;
        DisableDialogue();
        m_shackled = m_spawningWave = m_drakesAnnounced = m_outroStarted = false;
        m_abortPending = m_creditPending = m_finished = m_dialoguePending = false;
        m_bIsFirstSearScale = true;
        m_expectedEntry = m_uiKilledCreatures = m_wave = m_waveDeaths = m_completedWaves = 0;
        m_nuzarkGuid.Clear();
        m_lexlortGuid.Clear();
        m_lSearscaleGuidList.clear();
        m_waveActors.clear();
        GuidList actors;
        actors.swap(m_ownedActors); // Clear every local identity before synchronous despawn callbacks.
        SetDeathPrevention(false);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        for (ObjectGuid guid : actors)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (IsOwned(actor)) actor->ForcedDespawn();
    }

    void Reset() override
    {
        // Combat evade preserves a live escort, its wave and its valid outro.
        if (!HasEscortState(STATE_ESCORT_ESCORTING)) CleanupEvent();
        SetDeathPrevention(false);
    }

    void JustStartedEscort() override
    {
        CleanupEvent();
        m_eventActive = true;
    }

    void AbortEvent()
    {
        if (!m_eventActive || m_finished) return;
        const uint32 round = m_round;
        m_eventActive = false;
        m_shackled = m_abortPending = m_creditPending = false;
        DisableDialogue();
        FailQuestForPlayerAndGroup();
        if (round != m_round) return;
        CleanupEvent();
        if (m_round != round + 1) return;
        // Retain the creature's ordinary respawn policy after a failed escort.
        m_creature->ForcedDespawn();
    }

    void JustDied(Unit* killer) override
    {
        const uint32 round = m_round;
        m_eventActive = false;
        m_shackled = false;
        DisableDialogue();
        npc_escortAI::JustDied(killer);
        if (round == m_round) CleanupEvent();
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        const uint32 round = m_round;
        const bool fail = m_eventActive && !m_finished;
        m_eventActive = false;
        m_shackled = false;
        DisableDialogue();
        npc_escortAI::CorpseRemoved(respawnDelay);
        if (fail) FailQuestForPlayerAndGroup();
        if (round == m_round) CleanupEvent();
    }

    void JustRespawned() override
    {
        const uint32 round = m_round;
        uint32 respawnDelay = 0;
        CorpseRemoved(respawnDelay); // Also covers a direct same-AI respawn.
        if (m_round != round + 1) return;
        npc_escortAI::JustRespawned();
    }

    void Aggro(Unit* /*who*/) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING)) DoScriptText(SAY_AGGRO, m_creature);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING)) npc_escortAI::MoveInLineOfSight(who);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* /*invoker*/, uint32 /*miscValue*/) override
    {
        if (eventType == AI_EVENT_CUSTOM_A && !m_eventActive && !HasEscortState(STATE_ESCORT_ESCORTING) && m_creature->IsAlive())
        {
            m_shackled = true;
            SetDeathPrevention(true);
        }
    }

    void HandleLowHp()
    {
        m_shackled = false; // Capture submission is one-shot, including across a later respawn.
        DoScriptText(EMOTE_SUBMIT, m_creature);
        m_creature->SetFactionTemporary(FACTION_GRARK_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);
        EnterEvadeMode();
        SetDeathPrevention(false);
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    Creature* SummonOwned(uint32 entry, float x, float y, float z, float o, TempSpawnType type, uint32 lifetime)
    {
        const uint32 round = m_round;
        m_expectedEntry = entry;
        Creature* actor = m_creature->SummonCreature(entry, x, y, z, o, type, lifetime);
        m_expectedEntry = 0;
        if (round != m_round || !m_eventActive) return nullptr;
        if (!actor || !IsMember(m_ownedActors, actor->GetObjectGuid()) || !actor->IsAlive()) m_abortPending = true;
        return actor;
    }

    void FinishWaveIfReady()
    {
        const uint8 required = m_wave == 3 ? 3 : 4;
        if (!m_wave || m_spawningWave || m_abortPending || m_waveDeaths != required || !m_waveActors.empty()) return;
        m_completedWaves = m_wave;
        m_wave = 0;
        if (m_completedWaves == 1) DoScriptText(SAY_FIRST_AMBUSH_END, m_creature);
        else if (m_completedWaves == 2) DoScriptText(SAY_SEC_AMBUSH_END, m_creature);
        else if (m_drakesAnnounced) DoScriptText(SAY_THIRD_AMBUSH_END, m_creature);
        // Drakes may be killed before point30. Their completed state survives that arrival.
        SetEscortPaused(false);
    }

    void StartWave(uint8 wave)
    {
        m_wave = wave;
        m_waveDeaths = 0;
        m_waveActors.clear();
        m_spawningWave = true;
    }

    void StartOutroDialogue()
    {
        if (m_updatingDialogue) { m_dialoguePending = true; return; }
        m_dialoguePending = false;
        m_updatingDialogue = true;
        m_dialogueRound = m_round;
        StartNextDialogueText(SAY_LAST_STAND);
        m_updatingDialogue = false;
    }

    void WaypointReached(uint32 pointId) override
    {
        if (!m_eventActive || m_finished || m_abortPending) return;
        const uint32 round = m_round;
        switch (pointId)
        {
            case 1: DoScriptText(SAY_START, m_creature); break;
            case 7: DoScriptText(SAY_PAY, m_creature); break;
            case 12:
                if (m_wave || m_completedWaves != 0) return;
                DoScriptText(SAY_FIRST_AMBUSH_START, m_creature);
                SetEscortPaused(true);
                StartWave(1);
                SummonOwned(NPC_BLACKROCK_AMBUSHER, -7844.3f, -1521.6f, 139.2f, 0.0f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_BLACKROCK_AMBUSHER, -7860.4f, -1507.8f, 141.0f, 6.0f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_BLACKROCK_RAIDER, -7845.6f, -1508.1f, 138.8f, 6.1f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_BLACKROCK_RAIDER, -7859.8f, -1521.8f, 139.2f, 6.2f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                break;
            case 24:
                if (m_wave || m_completedWaves != 1) return;
                DoScriptText(SAY_SEC_AMBUSH_START, m_creature);
                SetEscortPaused(true);
                StartWave(2);
                SummonOwned(NPC_BLACKROCK_AMBUSHER, -8035.3f, -1222.2f, 135.5f, 5.1f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_FLAMESCALE_DRAGONSPAWN, -8037.5f, -1216.9f, 135.8f, 5.1f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_BLACKROCK_AMBUSHER, -8009.5f, -1222.1f, 139.2f, 3.9f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                if (round != m_round) return;
                SummonOwned(NPC_FLAMESCALE_DRAGONSPAWN, -8007.1f, -1219.4f, 140.1f, 3.9f, TEMPSPAWN_TIMED_OOC_DESPAWN, 20000);
                break;
            case 28:
                if (m_wave || m_completedWaves != 2) return;
                StartWave(3);
                SummonOwned(NPC_SEARSCALE_DRAKE, -7897.8f, -1123.1f, 233.4f, 3.0f, TEMPSPAWN_TIMED_OOC_DESPAWN, 60000);
                if (round != m_round) return;
                SummonOwned(NPC_SEARSCALE_DRAKE, -7898.8f, -1125.1f, 193.9f, 3.0f, TEMPSPAWN_TIMED_OOC_DESPAWN, 60000);
                if (round != m_round) return;
                SummonOwned(NPC_SEARSCALE_DRAKE, -7895.6f, -1119.5f, 194.5f, 3.1f, TEMPSPAWN_TIMED_OOC_DESPAWN, 60000);
                break;
            case 30:
                if (m_drakesAnnounced || (m_wave != 3 && m_completedWaves != 3)) return;
                m_drakesAnnounced = true;
                DoScriptText(SAY_THIRD_AMBUSH_START, m_creature);
                if (m_completedWaves == 3)
                {
                    DoScriptText(SAY_THIRD_AMBUSH_END, m_creature);
                    SetEscortPaused(false);
                    return;
                }
                SetEscortPaused(true);
                if (Player* player = GetPlayerForEscort())
                {
                    for (ObjectGuid guid : m_waveActors)
                        if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                            if (IsOwned(actor) && actor->IsAlive()) actor->AI()->AttackStart(player);
                }
                else m_abortPending = true;
                return;
            case 36: DoScriptText(EMOTE_LAUGH, m_creature); break;
            case 45:
                if (m_completedWaves != 3 || !m_drakesAnnounced || m_outroStarted) return;
                m_outroStarted = true;
                SetEscortPaused(true);
                SummonOwned(NPC_HIGH_EXECUTIONER_NUZARK, -7532.3f, -1029.4f, 258.0f, 2.7f, TEMPSPAWN_TIMED_DESPAWN, 40000);
                if (round != m_round) return;
                SummonOwned(NPC_SHADOW_OF_LEXLORT, -7532.8f, -1032.9f, 258.2f, 2.5f, TEMPSPAWN_TIMED_DESPAWN, 40000);
                if (round == m_round && !m_abortPending) StartOutroDialogue();
                return;
            default: return;
        }
        if (round != m_round) return;
        if (m_spawningWave)
        {
            m_spawningWave = false;
            FinishWaveIfReady();
            if (m_abortPending) SetEscortPaused(true);
        }
    }

    void JustDidDialogueStep(int32 entry) override
    {
        if (!m_eventActive || !m_outroStarted || m_finished || m_abortPending || !m_updatingDialogue || m_dialogueRound != m_round) return;
        switch (entry)
        {
            case SAY_LEXLORT_1: m_creature->SetStandState(UNIT_STAND_STATE_KNEEL); break;
            case SAY_LEXLORT_3:
                if (Creature* actor = GetSpeakerByEntry(NPC_HIGH_EXECUTIONER_NUZARK)) actor->HandleEmote(EMOTE_ONESHOT_ATTACK2HTIGHT);
                break;
            case NPC_GRARK_LORKRUB:
                m_creature->InterruptNonMeleeSpells(true);
                m_creature->StopMoving();
                m_creature->ClearComboPointHolders();
                m_creature->RemoveAllAurasOnDeath();
                m_creature->ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);
                m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
                m_creature->ClearAllReactives();
                m_creature->GetMotionMaster()->Clear();
                m_creature->GetMotionMaster()->MoveIdle();
                m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
                break;
            case SAY_LEXLORT_4:
                m_finished = m_creditPending = true;
                DisableDialogue(); // Reward only after DialogueHelper's cursor increment has returned.
                break;
        }
    }

    void JustSummoned(Creature* actor) override
    {
        if (!IsOwned(actor)) return;
        ObjectGuid guid = actor->GetObjectGuid();
        if (IsMember(m_ownedActors, guid)) return;
        if (!m_eventActive || m_finished || actor->GetEntry() != m_expectedEntry)
        {
            actor->ForcedDespawn();
            return;
        }
        m_ownedActors.push_back(guid);
        switch (actor->GetEntry())
        {
            case NPC_HIGH_EXECUTIONER_NUZARK: m_nuzarkGuid = guid; break;
            case NPC_SHADOW_OF_LEXLORT: m_lexlortGuid = guid; break;
            case NPC_SEARSCALE_DRAKE:
                m_waveActors.push_back(guid);
                if (m_bIsFirstSearScale)
                {
                    m_bIsFirstSearScale = false;
                    actor->SetLevitate(true);
                }
                m_lSearscaleGuidList.push_back(guid);
                break;
            default:
                m_waveActors.push_back(guid);
                if (Player* player = GetPlayerForEscort()) actor->AI()->AttackStart(player);
                break;
        }
    }

    void SummonedCreatureJustDied(Creature* actor) override
    {
        if (!m_eventActive || m_finished || !IsOwned(actor) || !IsMember(m_ownedActors, actor->GetObjectGuid())) return;
        if (!IsMember(m_waveActors, actor->GetObjectGuid()))
        {
            if (actor->GetObjectGuid() == m_nuzarkGuid || actor->GetObjectGuid() == m_lexlortGuid) m_abortPending = true;
            return;
        }
        m_waveActors.remove(actor->GetObjectGuid());
        ++m_waveDeaths;
        ++m_uiKilledCreatures;
        FinishWaveIfReady();
    }

    void SummonedCreatureDespawn(Creature* actor) override
    {
        if (!IsOwned(actor) || !IsMember(m_ownedActors, actor->GetObjectGuid())) return;
        ObjectGuid guid = actor->GetObjectGuid();
        m_ownedActors.remove(guid);
        m_lSearscaleGuidList.remove(guid);
        // Death already removed legitimate kills from m_waveActors. An uncounted
        // departure must fail safely, never manufacture kill progress.
        if (m_eventActive && !m_finished && (IsMember(m_waveActors, guid) || guid == m_nuzarkGuid || guid == m_lexlortGuid)) m_abortPending = true;
        m_waveActors.remove(guid);
        if (m_nuzarkGuid == guid) m_nuzarkGuid.Clear();
        if (m_lexlortGuid == guid) m_lexlortGuid.Clear();
    }

    Creature* GetSpeakerByEntry(uint32 entry) override
    {
        if (!m_eventActive || !m_outroStarted || m_finished || (m_updatingDialogue && m_dialogueRound != m_round)) return nullptr;
        if (entry == NPC_GRARK_LORKRUB) return m_creature;
        ObjectGuid guid = entry == NPC_HIGH_EXECUTIONER_NUZARK ? m_nuzarkGuid : entry == NPC_SHADOW_OF_LEXLORT ? m_lexlortGuid : ObjectGuid();
        Creature* actor = m_creature->GetMap()->GetCreature(guid);
        return IsOwned(actor) && actor->GetEntry() == entry && actor->IsAlive() && IsMember(m_ownedActors, guid) ? actor : nullptr;
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (m_updatingDialogue) return;
        const uint32 round = m_round;
        if (m_eventActive)
        {
            if (m_dialoguePending) StartOutroDialogue();
            else
            {
                m_updatingDialogue = true;
                m_dialogueRound = m_round;
                DialogueUpdate(diff);
                m_updatingDialogue = false;
            }
            if (round != m_round || !m_eventActive) return;
            if (m_abortPending) { AbortEvent(); return; }
            if (m_creditPending)
            {
                m_creditPending = false;
                if (Player* player = GetPlayerForEscort()) player->RewardPlayerAndGroupAtEventExplored(QUEST_ID_PRECARIOUS_PREDICAMENT, m_creature);
                if (round == m_round && m_eventActive && m_finished) m_creature->Suicide();
                return;
            }
            if (m_finished) return;
        }
        CreatureAI::UpdateAI(diff);
        if (m_shackled && !HasEscortState(STATE_ESCORT_ESCORTING) && m_creature->GetHealthPercent() <= 25.f) HandleLowHp();
    }
};

UnitAI* GetAI_npc_grark_lorkrub(Creature* pCreature)
{
    return new npc_grark_lorkrubAI(pCreature);
}

bool QuestAccept_npc_grark_lorkrub(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_ID_PRECARIOUS_PREDICAMENT)
    {
        if (npc_grark_lorkrubAI* pEscortAI = dynamic_cast<npc_grark_lorkrubAI*>(pCreature->AI()))
            pEscortAI->Start(false, pPlayer, pQuest);

        return true;
    }

    return false;
}

// 14250 - Capture Grark
struct CaptureGrark : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* target = spell->GetUnitTarget();
        target->AI()->SendAIEvent(AI_EVENT_CUSTOM_A, spell->GetCaster(), target);
    }
};

enum
{
    SPELL_FOOLS_PLIGHT          = 23504,

    SPELL_DEMONIC_FRENZY        = 23257,
    SPELL_ENTROPIC_STING        = 23260,

    EMOTE_FRENZY                = -1000001,

    NPC_NELSON_THE_NICE         = 14529,
    NPC_KLINFRAN_THE_CRAZED     = 14534,
    NPC_THE_CLEANER             = 14503,

    QUEST_STAVE_OF_THE_ANCIENTS = 7636
};

#define GOSSIP_ITEM                 "Show me your real face, demon."

/*######
## npc_franklin_the_friendly
######*/

/*######
## npc_klinfran_the_crazed
######*/

struct npc_klinfranAI : public ScriptedAI
{
    npc_klinfranAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_bTransform = false;
        m_uiDespawn_Timer = 0;
        Reset();
    }

    uint32 m_uiTransform_Timer;
    uint32 m_uiTransformEmote_Timer;
    bool m_bTransform;

    ObjectGuid m_hunterGuid;
    uint32 m_uiDemonic_Frenzy_Timer;
    uint32 m_uiDespawn_Timer;

    void Reset() override
    {
        switch (m_creature->GetEntry())
        {
            case NPC_NELSON_THE_NICE:
                m_creature->SetRespawnDelay(35 * MINUTE);
                m_creature->SetRespawnTime(35 * MINUTE);
                m_creature->NearTeleportTo(-8318.19f, -993.662f, 176.956f, 5.65024f);
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
            case NPC_KLINFRAN_THE_CRAZED:
                if (!m_uiDespawn_Timer)
                    m_uiDespawn_Timer = 20 * MINUTE*IN_MILLISECONDS;

                m_hunterGuid.Clear();
                m_uiDemonic_Frenzy_Timer = 5000;
                break;
        }
    }

    /** Franklin the Friendly */
    void Transform()
    {
        m_creature->UpdateEntry(NPC_KLINFRAN_THE_CRAZED);
        Reset();
    }

    bool CanBeginEvent(Player* player) const
    {
        return player && player->IsAlive() && player->IsInWorld() &&
            player->GetMap() == m_creature->GetMap() &&
            player->getClass() == CLASS_HUNTER &&
            player->GetQuestStatus(QUEST_STAVE_OF_THE_ANCIENTS) == QUEST_STATUS_INCOMPLETE &&
            m_creature->AI() == this && m_creature->IsAlive() && m_creature->IsInWorld() &&
            m_creature->GetEntry() == NPC_NELSON_THE_NICE && !m_creature->IsInCombat() &&
            !m_bTransform && m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
    }

    bool BeginEvent(Player* player)
    {
        if (!CanBeginEvent(player))
            return false;

        m_hunterGuid = player->GetObjectGuid();
        m_creature->GetMotionMaster()->Clear(false);
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetUInt32Value(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_NONE);
        m_bTransform = true;
        return true;
    }

    /** Klinfran the Crazed */
    void Aggro(Unit* pWho) override
    {
        if (pWho->getClass() == CLASS_HUNTER && (m_hunterGuid.IsEmpty() || m_hunterGuid == pWho->GetObjectGuid()))
        {
            m_hunterGuid = pWho->GetObjectGuid();
        }
        else
        {
            DemonDespawn();
            return;
        }
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
        if (pSpell && pSpell->Id == 14277)   // Scorpid Sting (Rank 4)
        {
            m_creature->RemoveAurasDueToSpell(SPELL_DEMONIC_FRENZY);
            DoCastSpellIfCan(m_creature, SPELL_ENTROPIC_STING, CAST_TRIGGERED);
        }
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        /** Franklin the Friendly */
        if (m_bTransform)
        {
            if (m_uiTransformEmote_Timer)
            {
                if (m_uiTransformEmote_Timer <= uiDiff)
                {
                    m_creature->HandleEmote(EMOTE_ONESHOT_POINT);
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

        /** Klinfran the Crazed */
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

        if (m_creature->getThreatManager().getThreatList().size() > 1 /*|| pHunter->IsDead()*/)
        {
            DemonDespawn();
            return;
        }

        if (m_uiDemonic_Frenzy_Timer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_DEMONIC_FRENZY) == CAST_OK)
            {
                DoScriptText(EMOTE_FRENZY, m_creature);
                m_uiDemonic_Frenzy_Timer = 15000;
            }
        }
        else
            m_uiDemonic_Frenzy_Timer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

bool GossipHello_npc_klinfran(Player* pPlayer, Creature* pCreature)
{
    if (npc_klinfranAI* ai = dynamic_cast<npc_klinfranAI*>(pCreature->AI()))
        if (ai->CanBeginEvent(pPlayer))
            pPlayer->ADD_GOSSIP_ITEM(0, GOSSIP_ITEM, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

    pPlayer->SEND_GOSSIP_MENU(pPlayer->GetGossipTextId(pCreature), pCreature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_klinfran(Player* pPlayer, Creature* pCreature, uint32 sender, uint32 action)
{
    pPlayer->CLOSE_GOSSIP_MENU();
    if (sender != GOSSIP_SENDER_MAIN || action != GOSSIP_ACTION_INFO_DEF)
        return true;

    if (npc_klinfranAI* ai = dynamic_cast<npc_klinfranAI*>(pCreature->AI()))
        ai->BeginEvent(pPlayer);
    return true;
}

UnitAI* GetAI_npc_klinfran(Creature* pCreature)
{
    return new npc_klinfranAI(pCreature);
}

void AddSC_burning_steppes()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_grark_lorkrub";
    pNewScript->GetAI = &GetAI_npc_grark_lorkrub;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_grark_lorkrub;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_klinfran";
    pNewScript->GetAI = &GetAI_npc_klinfran;
    pNewScript->pGossipHello = &GossipHello_npc_klinfran;
    pNewScript->pGossipSelect = &GossipSelect_npc_klinfran;
    pNewScript->RegisterSelf();

    RegisterSpellScript<CaptureGrark>("spell_capture_grark");
}
