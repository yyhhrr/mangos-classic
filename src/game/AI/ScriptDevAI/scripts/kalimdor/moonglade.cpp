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
SDName: Moonglade
SD%Complete: 100
SDComment: Quest support: 8736, 8868, 10965.
SDCategory: Moonglade
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_keeper_remulos
boss_eranikus
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Globals/ObjectMgr.h"
#include "AI/ScriptDevAI/scripts/kalimdor/world_kalimdor.h"

/*######
## npc_keeper_remulos
######*/

enum
{
    SPELL_CONJURE_RIFT          = 25813,            // summon Eranikus
    SPELL_DRAGON_HOVER          = 18430,
    SPELL_HEALING_TOUCH         = 23381,
    SPELL_REGROWTH              = 20665,
    SPELL_REJUVENATION          = 20664,
    SPELL_STARFIRE              = 21668,
    SPELL_ERANIKUS_REDEEMED     = 25846,            // transform Eranikus
    // SPELL_MOONGLADE_TRANQUILITY = unk,            // spell which acts as a spotlight over Eranikus after he is redeemed

    NPC_ERANIKUS_TYRANT         = 15491,
    NPC_NIGHTMARE_PHANTASM      = 15629,            // shadows summoned during the event - should cast 17228 and 21307
    NPC_REMULOS                 = 11832,
    NPC_TYRANDE_WHISPERWIND     = 15633,            // appears with the priestess during the event to help the players - should cast healing spells
    NPC_ELUNE_PRIESTESS         = 15634,
    NPC_NIGHTHAVEN_DEFENDER     = 15495,

    FACTION_CENARION_CIRCLE     = 996,

    QUEST_NIGHTMARE_MANIFESTS   = 8736,

    // yells -> in chronological order
    SAY_REMULOS_INTRO_1         = -1000669,        // Remulos intro
    SAY_REMULOS_INTRO_2         = -1000670,
    SAY_REMULOS_INTRO_3         = -1000671,
    SAY_REMULOS_INTRO_4         = -1000672,
    SAY_REMULOS_INTRO_5         = -1000673,

    EMOTE_SUMMON_ERANIKUS       = -1000674,        // Eranikus spawn - world emote
    SAY_ERANIKUS_SPAWN          = -1000675,

    SAY_REMULOS_TAUNT_1         = -1000676,        // Eranikus and Remulos discussion
    EMOTE_ERANIKUS_LAUGH        = -1000677,
    SAY_ERANIKUS_TAUNT_2        = -1000678,
    SAY_REMULOS_TAUNT_3         = -1000679,
    SAY_ERANIKUS_TAUNT_4        = -1000680,

    EMOTE_ERANIKUS_ATTACK       = -1000681,        // start attack
    SAY_REMULOS_DEFEND_1        = -1000682,
    SAY_REMULOS_DEFEND_2        = -1000683,
    SAY_ERANIKUS_SHADOWS        = -1000684,
    SAY_REMULOS_DEFEND_3        = -1000685,
    SAY_ERANIKUS_ATTACK_1       = -1000686,
    SAY_ERANIKUS_ATTACK_2       = -1000687,
    SAY_ERANIKUS_ATTACK_3       = -1000688,
    SAY_ERANIKUS_KILL           = -1000706,

    SAY_TYRANDE_APPEAR          = -1000689,        // Tyrande appears
    SAY_TYRANDE_HEAL            = -1000690,        // yelled by Tyrande when healing is needed
    SAY_TYRANDE_FORGIVEN_1      = -1000691,
    SAY_TYRANDE_FORGIVEN_2      = -1000692,
    SAY_TYRANDE_FORGIVEN_3      = -1000693,
    SAY_ERANIKUS_DEFEAT_1       = -1000694,
    SAY_ERANIKUS_DEFEAT_2       = -1000695,
    SAY_ERANIKUS_DEFEAT_3       = -1000696,
    EMOTE_ERANIKUS_REDEEM       = -1000697,        // world emote before WotLK

    PRIESTESS_MOUNT_ID          = 9695,

    EMOTE_TYRANDE_KNEEL         = -1000698,
    SAY_TYRANDE_REDEEMED        = -1000699,

    SAY_REDEEMED_1              = -1000700,        // Eranikus redeemed
    SAY_REDEEMED_2              = -1000701,
    SAY_REDEEMED_3              = -1000702,
    SAY_REDEEMED_4              = -1000703,

    SAY_REMULOS_OUTRO_1         = -1000704,        // Remulos outro
    SAY_REMULOS_OUTRO_2         = -1000705,

    POINT_ID_ERANIKUS_FLIGHT    = 1,
    POINT_ID_ERANIKUS_COMBAT    = 2,
    POINT_ID_ERANIKUS_REDEEMED  = 3,

    MAX_SHADOWS                 = 3,                // the max shadows summoned per turn
    MAX_DEFENDERS               = 10,               // The max number of Moonglade Defender summoned to fight the Nightmare Phantasm
    MAX_SUMMON_TURNS            = 10,               // There are about 10 summoned shade waves
};

static const DialogueEntry aIntroDialogue[] =
{
    {NPC_REMULOS,           0,                   14000},    // target player
    {SAY_REMULOS_INTRO_4,   NPC_REMULOS,         12000},
    {SAY_REMULOS_INTRO_5,   NPC_REMULOS,         5000},
    {SPELL_CONJURE_RIFT,    0,                   13000},    // conjure rift spell
    {SAY_ERANIKUS_SPAWN,    NPC_ERANIKUS_TYRANT, 11000},
    {SAY_REMULOS_TAUNT_1,   NPC_REMULOS,         5000},
    {EMOTE_ERANIKUS_LAUGH,  NPC_ERANIKUS_TYRANT, 3000},
    {SAY_ERANIKUS_TAUNT_2,  NPC_ERANIKUS_TYRANT, 10000},
    {SAY_REMULOS_TAUNT_3,   NPC_REMULOS,         12000},
    {SAY_ERANIKUS_TAUNT_4,  NPC_ERANIKUS_TYRANT, 6000},
    {EMOTE_ERANIKUS_ATTACK, NPC_ERANIKUS_TYRANT, 7000},
    {NPC_ERANIKUS_TYRANT,   0,                   0},        // target player - restart the escort and move Eranikus above the village
    {SAY_REMULOS_DEFEND_2,  NPC_REMULOS,         6000},     // face Eranikus
    {SAY_ERANIKUS_SHADOWS,  NPC_ERANIKUS_TYRANT, 4000},
    {SAY_REMULOS_DEFEND_3,  NPC_REMULOS,         0},
    {0, 0, 0},
};

static Position eranikusLocations[] =
{
    {7881.72f, -2651.23f, 493.29f, 0.40f},          // Eranikus spawn loc
    {7929.86f, -2574.88f, 505.35f},                 // Eranikus flight move loc
    {7912.98f, -2568.99f, 488.71f},                 // Eranikus combat move loc
    {7906.57f, -2565.63f, 488.39f},                 // Eranikus redeemed loc
};

static Position tyrandeLocations[] =
{
    // Tyrande should appear along the pathway, but because of the missing pathfinding we'll summon here closer to Eranikus
    {7948.89f, -2575.58f, 490.05f, 3.03f},          // Tyrande spawn loc
    {7888.32f, -2566.25f, 487.02f},                 // Tyrande heal loc
    {7901.83f, -2565.24f, 488.04f},                 // Tyrande Eranikus loc
};

static Position shadowsLocations[] =
{
    // Inside the house shades - first wave only
    {7832.78f, -2604.57f, 489.29f},
    {7826.68f, -2538.46f, 489.30f},
    {7811.48f, -2573.20f, 488.49f},
    // Outside shade points - basically only the first set of coords is used for the summoning; there is no solid proof of using the other coords
    {7888.32f, -2566.25f, 487.02f},
    {7946.12f, -2577.10f, 489.97f},
    {7963.00f, -2492.03f, 487.84f}
};

// Spawn points of the 10 Moonglade Defenders that are periodically summoned to fight the Nightmare Phantasms
static Position defendersLocations [] =
{
    {7868.226f, -2556.95f, 487.07f},
    {7867.39f, -2578.96f, 486.95f}
};

struct npc_keeper_remulosAI : public npc_escortAI, private DialogueHelper
{
    npc_keeper_remulosAI(Creature* creature) : npc_escortAI(creature), DialogueHelper(aIntroDialogue) { Reset(); }

    uint32 m_healTimer = 0;
    uint32 m_starfireTimer = 0;
    uint32 m_shadesummonTimer = 0;
    uint32 m_outroTimer = 0;
    uint32 m_finishTimer = 0;
    uint32 m_riftRetryTimer = 0;
    uint32 m_round = 0;
    ObjectGuid m_eranikusGuid;
    GuidList m_defendersList;
    GuidList m_shadowsList;
    uint8 m_outroPhase = 0;
    uint8 m_summonCount = 0;
    uint8 m_riftAttempts = 0;
    bool m_isFirstWave = true;
    bool m_eventActive = false;
    bool m_finishing = false;
    bool m_riftAwaiting = false;
    bool m_abortPending = false;

    bool IsOwned(Creature* actor, uint32 entry) const
    {
        return actor && actor->GetEntry() == entry && actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    bool IsCurrentBoss(Creature* actor) const
    {
        return m_eventActive && actor && actor->GetObjectGuid() == m_eranikusGuid &&
            actor->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
            (actor->GetEntry() == NPC_ERANIKUS_TYRANT || actor->GetEntry() == 15628);
    }

    Creature* GetEranikus() const
    {
        Creature* actor = m_creature->GetMap()->GetCreature(m_eranikusGuid);
        return IsCurrentBoss(actor) && actor->IsAlive() ? actor : nullptr;
    }

    void DespawnList(GuidList& actors, uint32 entry)
    {
        GuidList owned;
        owned.swap(actors); // UnSummon may immediately call back into this AI.
        for (ObjectGuid guid : owned)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (IsOwned(actor, entry))
                    actor->ForcedDespawn();
    }

    void DoDespawnSummoned()
    {
        GuidList defenders, shadows;
        defenders.swap(m_defendersList);
        shadows.swap(m_shadowsList);
        DespawnList(defenders, NPC_NIGHTHAVEN_DEFENDER);
        DespawnList(shadows, NPC_NIGHTMARE_PHANTASM);
    }

    void CleanupEvent()
    {
        if (m_eventActive || m_riftAttempts)
            m_creature->InterruptNonMeleeSpells(false, SPELL_CONJURE_RIFT);
        m_eventActive = false;
        m_finishing = false;
        m_riftAwaiting = false;
        m_abortPending = false;
        ++m_round;
        DisableDialogue();
        m_shadesummonTimer = m_outroTimer = m_finishTimer = m_riftRetryTimer = 0;
        m_outroPhase = m_summonCount = m_riftAttempts = 0;
        m_isFirstWave = true;
        ObjectGuid bossGuid = m_eranikusGuid;
        m_eranikusGuid.Clear();
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
        DoDespawnSummoned();
        if (Creature* boss = m_creature->GetMap()->GetCreature(bossGuid))
            if (boss->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
                (boss->GetEntry() == NPC_ERANIKUS_TYRANT || boss->GetEntry() == 15628))
                boss->ForcedDespawn();
    }

    void Reset() override
    {
        // An ordinary combat evade must not discard a live escort or its dialogue.
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            CleanupEvent();
            m_healTimer = 10 * IN_MILLISECONDS;
            m_starfireTimer = 25 * IN_MILLISECONDS;
        }
    }

    void JustStartedEscort() override
    {
        CleanupEvent();
        m_eventActive = true;
    }

    void AbortEvent()
    {
        if (!m_eventActive || m_finishing)
            return;
        // Close the round before quest/actor callbacks can re-enter it.
        m_eventActive = false;
        const uint32 round = m_round;
        FailQuestForPlayerAndGroup();
        if (round != m_round) return;
        CleanupEvent();
        if (m_round != round + 1) return;
        m_creature->ForcedDespawn();
    }

    void JustDied(Unit* killer) override
    {
        const uint32 round = m_round;
        m_eventActive = false;
        DisableDialogue();
        npc_escortAI::JustDied(killer);
        if (round == m_round) CleanupEvent();
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        npc_escortAI::CorpseRemoved(respawnDelay);
        CleanupEvent();
    }

    void JustRespawned() override
    {
        // Also handles a direct same-AI respawn, without requiring a preceding corpse.
        uint32 respawnDelay = 0;
        npc_escortAI::CorpseRemoved(respawnDelay);
        const uint32 round = m_round;
        CleanupEvent();
        if (m_round != round + 1) return;
        npc_escortAI::JustRespawned();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;
        if (!m_eventActive || m_finishing)
        {
            if (summoned->GetEntry() == NPC_ERANIKUS_TYRANT || summoned->GetEntry() == NPC_NIGHTMARE_PHANTASM || summoned->GetEntry() == NPC_NIGHTHAVEN_DEFENDER)
                summoned->ForcedDespawn();
            return;
        }
        switch (summoned->GetEntry())
        {
            case NPC_ERANIKUS_TYRANT:
                if (summoned->GetObjectGuid() == m_eranikusGuid)
                    return;
                if (GetEranikus())
                {
                    summoned->ForcedDespawn();
                    return;
                }
                if (!m_riftAwaiting)
                {
                    summoned->ForcedDespawn();
                    return;
                }
                m_riftAwaiting = false;
                m_eranikusGuid = summoned->GetObjectGuid();
                summoned->SetImmuneToNPC(true);
                summoned->SetImmuneToPlayer(true);
                break;
            case NPC_NIGHTMARE_PHANTASM:
                if (std::find(m_shadowsList.begin(), m_shadowsList.end(), summoned->GetObjectGuid()) != m_shadowsList.end())
                    return;
                m_shadowsList.push_back(summoned->GetObjectGuid());
                summoned->setFaction(14);
                summoned->AI()->AttackStart(m_creature);
                break;
            case NPC_NIGHTHAVEN_DEFENDER:
                if (std::find(m_defendersList.begin(), m_defendersList.end(), summoned->GetObjectGuid()) != m_defendersList.end())
                    return;
                PruneDefenders();
                if (m_defendersList.size() >= MAX_DEFENDERS)
                {
                    summoned->ForcedDespawn();
                    return;
                }
                m_defendersList.push_back(summoned->GetObjectGuid());
                summoned->SetWalk(false);
                summoned->GetMotionMaster()->MoveWaypoint(0);
                break;
        }
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        if (IsOwned(summoned, NPC_NIGHTHAVEN_DEFENDER))
            m_defendersList.remove(summoned->GetObjectGuid());
        else if (IsOwned(summoned, NPC_NIGHTMARE_PHANTASM))
            m_shadowsList.remove(summoned->GetObjectGuid());
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        SummonedCreatureJustDied(summoned);
        if (IsCurrentBoss(summoned) && !m_finishing)
            AbortEvent();
    }

    void SummonedJustReachedHome(Creature* summoned) override
    {
        if (m_eventActive && !m_finishing && IsOwned(summoned, NPC_NIGHTHAVEN_DEFENDER) &&
            std::find(m_defendersList.begin(), m_defendersList.end(), summoned->GetObjectGuid()) != m_defendersList.end())
            summoned->GetMotionMaster()->MoveWaypoint(0);
    }

    void SummonedMovementInform(Creature* summoned, uint32 type, uint32 pointId) override
    {
        if (type == POINT_MOTION_TYPE && pointId == POINT_ID_ERANIKUS_FLIGHT && IsCurrentBoss(summoned) && !m_finishing)
            summoned->SetFacingToObject(m_creature);
    }

    void WaypointReached(uint32 pointId) override
    {
        if (!m_eventActive || m_finishing)
            return;
        switch (pointId)
        {
            case 1:
                if (Player* player = GetPlayerForEscort())
                {
                    DoScriptText(SAY_REMULOS_INTRO_1, m_creature, player);
                    m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
                }
                break;
            case 2: DoScriptText(SAY_REMULOS_INTRO_2, m_creature); break;
            case 14:
                StartNextDialogueText(NPC_REMULOS);
                SetEscortPaused(true);
                break;
            case 18:
                if (!GetEranikus()) { AbortEvent(); return; }
                StartNextDialogueText(SAY_REMULOS_DEFEND_2);
                SetEscortPaused(true);
                break;
            case 19: SetEscortPaused(true); break;
        }
    }

    Creature* GetSpeakerByEntry(uint32 entry) override
    {
        if (!m_eventActive || m_finishing)
            return nullptr;
        switch (entry)
        {
            case NPC_REMULOS: return m_creature;
            case NPC_ERANIKUS_TYRANT: return GetEranikus();
            default: return nullptr;
        }
    }

    void PruneDefenders()
    {
        m_defendersList.remove_if([this](ObjectGuid guid)
        {
            Creature* defender = m_creature->GetMap()->GetCreature(guid);
            return !IsOwned(defender, NPC_NIGHTHAVEN_DEFENDER) || !defender->IsAlive();
        });
    }

    void RetryRift()
    {
        if (m_riftAttempts < 3)
            m_riftRetryTimer = 5 * IN_MILLISECONDS;
        else
        {
            // DialogueHelper advances its cursor after this callback returns.
            // Defer quest callbacks until that helper frame has fully unwound.
            m_abortPending = true;
            DisableDialogue();
        }
    }

    void JustDidDialogueStep(int32 entry) override
    {
        if (!m_eventActive || m_finishing)
            return;
        switch (entry)
        {
            case NPC_REMULOS:
                if (Player* player = GetPlayerForEscort())
                    DoScriptText(SAY_REMULOS_INTRO_3, m_creature, player);
                break;
            case SPELL_CONJURE_RIFT:
                if (GetEranikus()) break;
                ++m_riftAttempts;
                // Cast preparation is not the summon: this spell has a ten-second
                // cast. Preserve the original thirteen-second dialogue wait.
                m_riftAwaiting = true;
                if (DoCastSpellIfCan(m_creature, SPELL_CONJURE_RIFT) != CAST_OK)
                {
                    m_riftAwaiting = false;
                    RetryRift();
                }
                break;
            case SAY_ERANIKUS_SPAWN:
                if (Creature* eranikus = GetEranikus())
                    DoScriptText(EMOTE_SUMMON_ERANIKUS, eranikus);
                else
                    RetryRift();
                break;
            case NPC_ERANIKUS_TYRANT:
                if (Creature* eranikus = GetEranikus())
                {
                    if (Player* player = GetPlayerForEscort())
                        DoScriptText(SAY_REMULOS_DEFEND_1, m_creature, player);
                    eranikus->RemoveAurasDueToSpell(SPELL_DRAGON_HOVER);
                    eranikus->GetMotionMaster()->MovePoint(POINT_ID_ERANIKUS_FLIGHT, eranikusLocations[1].x, eranikusLocations[1].y, eranikusLocations[1].z, FORCED_MOVEMENT_FLIGHT);
                    SetEscortPaused(false);
                }
                else
                {
                    m_abortPending = true;
                    DisableDialogue();
                }
                break;
            case SAY_REMULOS_DEFEND_2:
                if (Creature* eranikus = GetEranikus()) m_creature->SetFacingToObject(eranikus);
                break;
            case SAY_REMULOS_DEFEND_3:
                SetEscortPaused(true);
                m_creature->SetFactionTemporary(996, TEMPFACTION_RESTORE_RESPAWN);
                m_shadesummonTimer = 5 * IN_MILLISECONDS;
                break;
        }
    }

    void DoHandleOutro(Creature* target)
    {
        if (!IsCurrentBoss(target) || !target->IsAlive() || m_finishing || target->GetEntry() != 15628)
            return;
        m_finishing = true;
        DisableDialogue();
        m_shadesummonTimer = m_riftRetryTimer = 0;
        m_outroPhase = 0;
        m_outroTimer = 3 * IN_MILLISECONDS;
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
        const uint32 round = m_round;
        Player* player = GetPlayerForEscort();
        DoDespawnSummoned();
        if (round != m_round || !IsCurrentBoss(target)) return;
        // Commit local completion before the group-credit callback can re-enter.
        if (player)
            player->RewardPlayerAndGroupAtEventExplored(QUEST_NIGHTMARE_MANIFESTS, target);
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (!m_eventActive)
            return;
        const uint32 round = m_round;
        if (m_finishTimer)
        {
            if (m_finishTimer <= diff)
            {
                CleanupEvent();
                if (m_round != round + 1) return;
                m_creature->ForcedDespawn();
            }
            else m_finishTimer -= diff;
            return;
        }
        if (m_riftRetryTimer)
        {
            if (m_riftRetryTimer <= diff)
            {
                m_riftRetryTimer = 0;
                // A delayed successful cast may have completed during the retry wait.
                StartNextDialogueText(GetEranikus() ? SAY_ERANIKUS_SPAWN : SPELL_CONJURE_RIFT);
            }
            else m_riftRetryTimer -= diff;
        }
        else if (!m_finishing)
            DialogueUpdate(diff);
        if (!m_eventActive || round != m_round)
            return;
        if (m_abortPending)
        {
            m_abortPending = false;
            AbortEvent();
            return;
        }

        if (m_outroTimer)
        {
            if (m_outroTimer <= diff)
            {
                if (m_outroPhase++ == 0)
                {
                    DoScriptText(SAY_REMULOS_OUTRO_1, m_creature);
                    m_outroTimer = 3 * IN_MILLISECONDS;
                }
                else
                {
                    DoScriptText(SAY_REMULOS_OUTRO_2, m_creature);
                    m_creature->SetRespawnDelay(1 * MINUTE);
                    m_outroTimer = 0;
                    // AI-local retirement cannot fire against a later same-AI respawn.
                    m_finishTimer = 3 * IN_MILLISECONDS;
                }
            }
            else m_outroTimer -= diff;
        }

        if (m_shadesummonTimer)
        {
            if (m_shadesummonTimer <= diff)
            {
                if (m_isFirstWave)
                {
                    for (uint8 i = 0; i < MAX_SHADOWS; ++i)
                        m_creature->SummonCreature(NPC_NIGHTMARE_PHANTASM, shadowsLocations[i].x, shadowsLocations[i].y, shadowsLocations[i].z, 0, TEMPSPAWN_DEAD_DESPAWN, 0);
                    for (uint8 i = 0; i < MAX_DEFENDERS; ++i)
                    {
                        uint8 index = i % 2;
                        float fX, fY, fZ;
                        m_creature->GetRandomPoint(defendersLocations[index].x, defendersLocations[index].y, defendersLocations[index].z, 3.0f, fX, fY, fZ);
                        m_creature->SummonCreature(NPC_NIGHTHAVEN_DEFENDER, fX, fY, fZ, 0, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 3 * MINUTE * IN_MILLISECONDS);
                    }
                    if (Creature* eranikus = GetEranikus()) DoScriptText(SAY_ERANIKUS_ATTACK_1, eranikus);
                    ++m_summonCount;
                    m_isFirstWave = false;
                }

                float fX, fY, fZ;
                uint8 summonPoint = roll_chance_i(70) ? uint32(MAX_SHADOWS) : urand(MAX_SHADOWS + 1, MAX_SHADOWS + 2);
                if (m_summonCount < MAX_SUMMON_TURNS)
                {
                    for (uint8 i = 0; i < MAX_SHADOWS; ++i)
                    {
                        m_creature->GetRandomPoint(shadowsLocations[summonPoint].x, shadowsLocations[summonPoint].y, shadowsLocations[summonPoint].z, 10.0f, fX, fY, fZ);
                        m_creature->SummonCreature(NPC_NIGHTMARE_PHANTASM, fX, fY, fZ, 0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 3 * MINUTE * IN_MILLISECONDS);
                    }
                    // Prune deaths/despawns even when their callback was unavailable.
                    PruneDefenders();
                    const uint32 missing = m_defendersList.size() < MAX_DEFENDERS ? MAX_DEFENDERS - m_defendersList.size() : 0;
                    for (uint32 i = 0; i < missing; ++i)
                    {
                        uint8 index = i % 2;
                        m_creature->GetRandomPoint(defendersLocations[index].x, defendersLocations[index].y, defendersLocations[index].z, 3.0f, fX, fY, fZ);
                        m_creature->SummonCreature(NPC_NIGHTHAVEN_DEFENDER, fX, fY, fZ, 0, TEMPSPAWN_DEAD_DESPAWN, 0);
                    }
                    ++m_summonCount;
                }
                if (m_summonCount == MAX_SUMMON_TURNS)
                {
                    m_shadesummonTimer = 0;
                    if (Creature* eranikus = GetEranikus())
                        eranikus->GetMotionMaster()->MovePoint(POINT_ID_ERANIKUS_COMBAT, eranikusLocations[2].x, eranikusLocations[2].y, eranikusLocations[2].z);
                    else AbortEvent();
                }
                else m_shadesummonTimer = urand(20 * IN_MILLISECONDS, 30 * IN_MILLISECONDS);
            }
            else m_shadesummonTimer -= diff;
        }

        // Combat spells
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_healTimer < diff)
        {
            if (Unit* target = DoSelectLowestHpFriendly(DEFAULT_VISIBILITY_DISTANCE))
            {
                switch (urand(0, 2))
                {
                    case 0: DoCastSpellIfCan(target, SPELL_HEALING_TOUCH); break;
                    case 1: DoCastSpellIfCan(target, SPELL_REJUVENATION);  break;
                    case 2: DoCastSpellIfCan(target, SPELL_REGROWTH);      break;
                }
            }
            m_healTimer = 10 * IN_MILLISECONDS;
        }
        else
            m_healTimer -= diff;

        if (m_starfireTimer < diff)
        {
            if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
            {
                if (DoCastSpellIfCan(target, SPELL_STARFIRE) == CAST_OK)
                    m_starfireTimer = 20 * IN_MILLISECONDS;
            }
        }
        else
            m_starfireTimer -= diff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_keeper_remulos(Creature* creature)
{
    return new npc_keeper_remulosAI(creature);
}

bool QuestAccept_npc_keeper_remulos(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->GetQuestId() == QUEST_NIGHTMARE_MANIFESTS)
    {
        if (npc_keeper_remulosAI* escortAI = dynamic_cast<npc_keeper_remulosAI*>(creature->AI()))
            escortAI->Start(true, player, quest);

        return true;
    }

    // Return false for other quests in order to handle DB scripts. Example: quest 8447
    return false;
}

/*######
## boss_eranikus
######*/

enum
{
    NPC_KEEPER_REMULOS          = 11832,
    NPC_ERANIKUS_REDEEMED       = 15628,

    SPELL_ACID_BREATH           = 24839,
    SPELL_NOXIOUS_BREATH        = 24818,
    SPELL_SHADOWBOLT_VOLLEY     = 25586,
    SPELL_ARCANE_CHANNELING     = 23017,        // used by Tyrande - not sure if it's the right id

    FACTION_FRIENDLY            = 35,
    MAX_PRIESTESS               = 7,

    POINT_ID_TYRANDE_HEAL       = 0,
    POINT_ID_TYRANDE_ABSOLUTION = 1,
};

struct boss_eranikusAI : public ScriptedAI
{
    boss_eranikusAI(Creature* creature) : ScriptedAI(creature) { Reset(); }

    uint32 m_acidBreathTimer;
    uint32 m_noxiousBreathTimer;
    uint32 m_shadowboltVolleyTimer;
    uint32 m_eventTimer;
    uint32 m_tyrandeMoveTimer;
    uint32 m_finishTimer = 0;
    uint32 m_generation = 0;
    bool m_terminating = false;
    bool m_redeeming = false;
    bool m_finished = false;
    bool m_landed = false;

    uint8 m_eventPhase;
    uint8 m_tyrandeMovePoint;
    uint8 m_healthCheck;

    ObjectGuid m_remulosGuid;
    ObjectGuid m_tyrandeGuid;
    GuidList m_priestessList;
    GuidList m_tyrandeList;

    void Reset() override
    {
        const uint32 generation = ++m_generation;
        CleanupActors();
        if (generation != m_generation) return;
        m_terminating = m_redeeming = m_finished = m_landed = false;
        m_finishTimer = 0;
        m_acidBreathTimer         = 10 * IN_MILLISECONDS;
        m_noxiousBreathTimer      = 3 * IN_MILLISECONDS;
        m_shadowboltVolleyTimer   = 5 * IN_MILLISECONDS;
        m_tyrandeMoveTimer        = 0;
        m_tyrandeMovePoint        = POINT_ID_TYRANDE_HEAL;

        m_remulosGuid = m_creature->GetSpawnerGuid();
        m_tyrandeGuid.Clear();

        m_healthCheck             = 85;
        m_eventPhase              = 0;
        m_eventTimer              = 0;

        // For some reason the boss doesn't move in combat
        SetCombatMovement(false);
    }

    npc_keeper_remulosAI* GetRemulosAI() const
    {
        Creature* remulos = m_creature->GetMap()->GetCreature(m_remulosGuid);
        if (!remulos || !remulos->IsAlive() || remulos->GetEntry() != NPC_KEEPER_REMULOS)
            return nullptr;
        npc_keeper_remulosAI* ai = dynamic_cast<npc_keeper_remulosAI*>(remulos->AI());
        return ai && ai->IsCurrentBoss(m_creature) ? ai : nullptr;
    }

    bool IsOwned(Creature* actor, uint32 entry) const
    {
        return actor && actor->GetEntry() == entry && actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    void DespawnActors(GuidList& actors, uint32 entry)
    {
        GuidList owned;
        owned.swap(actors);
        for (ObjectGuid guid : owned)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (IsOwned(actor, entry)) actor->ForcedDespawn();
    }

    void CleanupActors()
    {
        GuidList priestesses, tyrande;
        priestesses.swap(m_priestessList);
        tyrande.swap(m_tyrandeList);
        m_tyrandeGuid.Clear();
        m_tyrandeMoveTimer = 0;
        DespawnActors(priestesses, NPC_ELUNE_PRIESTESS);
        DespawnActors(tyrande, NPC_TYRANDE_WHISPERWIND);
    }

    void EndEvent(bool notifyKeeper)
    {
        if (m_terminating) return;
        const uint32 generation = m_generation;
        m_terminating = true;
        m_eventTimer = m_finishTimer = 0;
        CleanupActors();
        if (generation != m_generation) return;
        if (notifyKeeper && !m_finished)
            if (npc_keeper_remulosAI* ai = GetRemulosAI()) ai->AbortEvent();
    }

    void JustDied(Unit* killer) override
    {
        EndEvent(true);
        ScriptedAI::JustDied(killer);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        EndEvent(true);
        ScriptedAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        EndEvent(true);
        ScriptedAI::JustRespawned();
    }

    void EnterEvadeMode() override
    {
        if (m_terminating || m_finished || m_redeeming) return;
        npc_keeper_remulosAI* ai = GetRemulosAI();
        if (m_creature->GetHealthPercent() < 20.0f && ai)
        {
            m_redeeming = true;
            m_creature->RemoveAllAurasOnEvade();
            m_creature->CombatStop(true);
            m_creature->LoadCreatureAddon(true);
            m_creature->SetLootRecipient(nullptr);
            ai->m_shadesummonTimer = 0;
            ai->EnterEvadeMode();
            const uint32 generation = m_generation;
            DoDespawnSummoned();
            if (generation != m_generation || !GetRemulosAI()) return;
            m_eventPhase = 0;
            m_eventTimer = 5 * IN_MILLISECONDS;
            m_creature->UpdateEntry(NPC_ERANIKUS_REDEEMED);
        }
        else
        {
            EndEvent(true);
            if (m_terminating) m_creature->ForcedDespawn();
        }
    }

    void KilledUnit(Unit* victim) override
    {
        if (victim->GetTypeId() != TYPEID_PLAYER)
            return;

        DoScriptText(SAY_ERANIKUS_KILL, m_creature);
    }

    void DoSummonHealers()
    {
        float fX, fY, fZ;
        for (uint8 j = 0; j < MAX_PRIESTESS; ++j)
        {
            m_creature->GetRandomPoint(tyrandeLocations[0].x, tyrandeLocations[0].y, tyrandeLocations[0].z, 10.0f, fX, fY, fZ);
            if (Creature* priestess = m_creature->SummonCreature(NPC_ELUNE_PRIESTESS, fX, fY, fZ, 0.0f, TEMPSPAWN_CORPSE_DESPAWN, 0))
                priestess->Mount(PRIESTESS_MOUNT_ID);
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        if (m_terminating || m_finished || !GetRemulosAI() || summoned->GetSpawnerGuid() != m_creature->GetObjectGuid()) return;
        switch (summoned->GetEntry())
        {
            case NPC_TYRANDE_WHISPERWIND:
                if (std::find(m_tyrandeList.begin(), m_tyrandeList.end(), summoned->GetObjectGuid()) != m_tyrandeList.end()) return;
                m_tyrandeMovePoint = POINT_ID_TYRANDE_HEAL;
                m_tyrandeList.push_back(summoned->GetObjectGuid());
                m_tyrandeGuid = summoned->GetObjectGuid();
                summoned->SetWalk(false);
                summoned->GetMotionMaster()->MovePoint(POINT_ID_TYRANDE_HEAL, tyrandeLocations[1].x, tyrandeLocations[1].y, tyrandeLocations[1].z);
                break;
            case NPC_ELUNE_PRIESTESS:
                if (std::find(m_priestessList.begin(), m_priestessList.end(), summoned->GetObjectGuid()) != m_priestessList.end()) return;
                m_priestessList.push_back(summoned->GetObjectGuid());
                float fX, fY, fZ;
                summoned->SetWalk(false);
                m_creature->GetRandomPoint(tyrandeLocations[1].x, tyrandeLocations[1].y, tyrandeLocations[1].z, 10.0f, fX, fY, fZ);
                summoned->GetMotionMaster()->MovePoint(POINT_ID_TYRANDE_HEAL, fX, fY, fZ);
                break;
        }
    }

    void DoDespawnSummoned()
    {
        DespawnActors(m_priestessList, NPC_ELUNE_PRIESTESS);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        if (IsOwned(summoned, NPC_ELUNE_PRIESTESS)) m_priestessList.remove(summoned->GetObjectGuid());
        if (IsOwned(summoned, NPC_TYRANDE_WHISPERWIND))
        {
            m_tyrandeList.remove(summoned->GetObjectGuid());
            if (m_tyrandeGuid == summoned->GetObjectGuid())
            {
                m_tyrandeGuid.Clear();
                m_tyrandeMoveTimer = 0;
            }
        }
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        SummonedCreatureDespawn(summoned);
    }

    void SummonedMovementInform(Creature* summoned, uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE || m_terminating || m_finished || m_redeeming || !GetRemulosAI())
            return;
        const bool tyrande = IsOwned(summoned, NPC_TYRANDE_WHISPERWIND) && summoned->GetObjectGuid() == m_tyrandeGuid;
        const bool priestess = IsOwned(summoned, NPC_ELUNE_PRIESTESS) &&
            std::find(m_priestessList.begin(), m_priestessList.end(), summoned->GetObjectGuid()) != m_priestessList.end();
        if (!tyrande && !priestess) return;

        switch (pointId)
        {
            case POINT_ID_TYRANDE_HEAL:
                if (tyrande)
                {
                    if (m_tyrandeMovePoint != pointId || m_tyrandeMoveTimer) return;
                    m_tyrandeMovePoint = POINT_ID_TYRANDE_ABSOLUTION;
                    // Unmont, yell and prepare to channel the spell on Eranikus
                    DoScriptText(SAY_TYRANDE_HEAL, summoned);
                    summoned->Unmount();
                    m_tyrandeMoveTimer = 5 * IN_MILLISECONDS;
                }
                // Unmount the priestess - unk what is their exact purpose (maybe healer)
                else if (priestess)
                {
                    summoned->Unmount();
                    summoned->AI()->AttackStart(m_creature);    // Prietess of Elune do not really attack Eranikus but they need to be in combat to periodically cast Mass Heal
                }
                break;
            case POINT_ID_TYRANDE_ABSOLUTION:
                if (tyrande)
                {
                    if (m_tyrandeMovePoint != pointId || m_tyrandeMoveTimer) return;
                    ++m_tyrandeMovePoint;
                    summoned->CastSpell(summoned, SPELL_ARCANE_CHANNELING, TRIGGERED_NONE);
                    DoScriptText(SAY_TYRANDE_FORGIVEN_1, summoned);
                }
                break;
        }
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != POINT_MOTION_TYPE || m_terminating || m_finished || !GetRemulosAI())
            return;

        switch (pointId)
        {
            case POINT_ID_ERANIKUS_COMBAT:
            {
                if (m_landed || m_redeeming || GetRemulosAI()->m_summonCount != MAX_SUMMON_TURNS) return;
                m_landed = true;
                // Land and attack the current event keeper, never the dragon itself.
                m_creature->SetImmuneToNPC(false);
                m_creature->SetImmuneToPlayer(false);
                if (Creature* remulos = m_creature->GetMap()->GetCreature(m_remulosGuid))
                    AttackStart(remulos);
                DoScriptText(SAY_ERANIKUS_ATTACK_2, m_creature);
                m_creature->RemoveAurasDueToSpell(SPELL_DRAGON_HOVER);
                m_creature->SetHover(false);
                m_creature->HandleEmote(EMOTE_ONESHOT_LAND);
                break;
            }
            case POINT_ID_ERANIKUS_REDEEMED:
            {
                if (!m_redeeming || m_eventPhase != 4 || m_eventTimer) return;
                DoScriptText(SAY_REDEEMED_1, m_creature);
                m_eventTimer = 11 * IN_MILLISECONDS;
                break;
            }
            default: break;
        }
    }

    void UpdateAI(const uint32 diff) override
    {
        if (m_terminating) return;
        if (m_finishTimer)
        {
            if (m_finishTimer <= diff)
            {
                EndEvent(false);
                if (m_terminating) m_creature->ForcedDespawn();
            }
            else m_finishTimer -= diff;
            return;
        }
        if (!GetRemulosAI())
        {
            EndEvent(false);
            if (m_terminating) m_creature->ForcedDespawn();
            return;
        }
        if (m_eventTimer)
        {
            if (m_eventTimer <= diff)
            {
                switch (m_eventPhase)
                {
                    case 0:
                        // Eranikus is redeemed - make Tyrande kneel and stop casting
                        if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                        {
                            tyrande->InterruptNonMeleeSpells(false);
                            tyrande->SetStandState(UNIT_STAND_STATE_KNEEL);
                            DoScriptText(EMOTE_TYRANDE_KNEEL, tyrande);
                        }
                        if (Creature* remulos = m_creature->GetMap()->GetCreature(m_remulosGuid))
                            remulos->SetFacingToObject(m_creature);
                        // Note: this emote was a world wide yellow emote before WotLK
                        DoScriptText(EMOTE_ERANIKUS_REDEEM, m_creature);
                        // DoCastSpellIfCan(m_creature, SPELL_MOONGLADE_TRANQUILITY);        // spell id unk for the moment
                        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
                        m_eventTimer = 5 * IN_MILLISECONDS;
                        break;
                    case 1:
                        if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                            DoScriptText(SAY_TYRANDE_REDEEMED, tyrande);
                        m_eventTimer = 6 * IN_MILLISECONDS;
                        break;
                    case 2:
                        // Transform Eranikus into elf
                        DoCastSpellIfCan(m_creature, SPELL_ERANIKUS_REDEEMED);
                        m_eventTimer = 5 * IN_MILLISECONDS;
                        break;
                    case 3:
                        // Move Eranikus in front of Tyrande
                        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                        m_creature->SetWalk(true);
                        m_creature->GetMotionMaster()->MovePoint(POINT_ID_ERANIKUS_REDEEMED, eranikusLocations[3].x, eranikusLocations[3].y, eranikusLocations[3].z);
                        m_eventTimer = 0;
                        break;
                    case 4:
                        DoScriptText(SAY_REDEEMED_2, m_creature);
                        m_eventTimer = 11 * IN_MILLISECONDS;
                        break;
                    case 5:
                        DoScriptText(SAY_REDEEMED_3, m_creature);
                        m_eventTimer = 13 * IN_MILLISECONDS;
                        break;
                    case 6:
                        DoScriptText(SAY_REDEEMED_4, m_creature);
                        m_eventTimer = 7 * IN_MILLISECONDS;
                        break;
                    case 7:
                        // Complete Quest and end event
                        if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                        {
                            tyrande->SetStandState(UNIT_STAND_STATE_STAND);
                            tyrande->ForcedDespawn(9 * IN_MILLISECONDS);
                        }
                        m_finished = true;
                        m_eventTimer = 0;
                        m_finishTimer = 2 * IN_MILLISECONDS;
                        // Tyrande has her own bounded nine-second farewell. Detach it
                        // before the dragon's two-second retirement cleans the round.
                        m_tyrandeGuid.Clear();
                        m_tyrandeList.clear();
                        m_creature->HandleEmote(EMOTE_ONESHOT_BOW);
                        if (npc_keeper_remulosAI* ai = GetRemulosAI())
                            ai->DoHandleOutro(m_creature);
                        return;
                        break;
                }
                ++m_eventPhase;
            }
            else
                m_eventTimer -= diff;
        }

        // Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // Move Tyrande after she is summoned
        if (m_tyrandeMoveTimer)
        {
            if (m_tyrandeMoveTimer <= diff)
            {
                if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                    tyrande->GetMotionMaster()->MovePoint(POINT_ID_TYRANDE_ABSOLUTION, tyrandeLocations[2].x, tyrandeLocations[2].y, tyrandeLocations[2].z);
                m_tyrandeMoveTimer = 0;
            }
            else
                m_tyrandeMoveTimer -= diff;
        }

        // Not sure if this should be handled by health percent, but this is the only reasonable way
        if (m_creature->GetHealthPercent() < m_healthCheck)
        {
            switch (m_healthCheck)
            {
                case 85:
                    DoScriptText(SAY_ERANIKUS_ATTACK_3, m_creature);
                    // Here Tyrande only yells but she doesn't appear anywhere - we summon here for 1 second just to handle the yell
                    if (Creature* tyrande = m_creature->SummonCreature(NPC_TYRANDE_WHISPERWIND, tyrandeLocations[0].x, tyrandeLocations[0].y, tyrandeLocations[0].z, 0, TEMPSPAWN_TIMED_DESPAWN, 1000))
                        DoScriptText(SAY_TYRANDE_APPEAR, tyrande);
                    m_healthCheck = 75;
                    break;
                case 75:
                    // Eranikus yells again
                    DoScriptText(SAY_ERANIKUS_ATTACK_3, m_creature);
                    m_healthCheck = 50;
                    break;
                case 50:
                    // Summon Tyrande - she enters the fight this time
                    if (Creature* tyrande = m_creature->SummonCreature(NPC_TYRANDE_WHISPERWIND, tyrandeLocations[0].x, tyrandeLocations[0].y, tyrandeLocations[0].z, 0, TEMPSPAWN_CORPSE_DESPAWN, 0))
                        tyrande->Mount(PRIESTESS_MOUNT_ID);
                    m_healthCheck = 35;
                    break;
                case 35:
                    // Summon the priestess
                    DoSummonHealers();
                    DoScriptText(SAY_ERANIKUS_DEFEAT_1, m_creature);
                    m_healthCheck = 31;
                    break;
                case 31:
                    if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                        DoScriptText(SAY_TYRANDE_FORGIVEN_2, tyrande);
                    m_healthCheck = 27;
                    break;
                case 27:
                    if (Creature* tyrande = m_creature->GetMap()->GetCreature(m_tyrandeGuid))
                        DoScriptText(SAY_TYRANDE_FORGIVEN_3, tyrande);
                    m_healthCheck = 25;
                    break;
                case 25:
                    DoScriptText(SAY_ERANIKUS_DEFEAT_2, m_creature);
                    m_healthCheck = 20;
                    break;
                case 20:
                    // Eranikus is redeemed - stop the fight
                    DoScriptText(SAY_ERANIKUS_DEFEAT_3, m_creature);
                    m_creature->AI()->EnterEvadeMode();
                    m_healthCheck = 0;
                    break;
            }
        }

        // Combat spells
        if (m_acidBreathTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_ACID_BREATH) == CAST_OK)
                m_acidBreathTimer = 15 * IN_MILLISECONDS;
        }
        else
            m_acidBreathTimer -= diff;

        if (m_noxiousBreathTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_NOXIOUS_BREATH) == CAST_OK)
                m_noxiousBreathTimer = 30 * IN_MILLISECONDS;
        }
        else
            m_noxiousBreathTimer -= diff;

        if (m_shadowboltVolleyTimer < diff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_SHADOWBOLT_VOLLEY) == CAST_OK)
                m_shadowboltVolleyTimer = 25 * IN_MILLISECONDS;
        }
        else
            m_shadowboltVolleyTimer -= diff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_eranikus(Creature* creature)
{
    return new boss_eranikusAI(creature);
}

struct ConjureDreamRift : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_0)
        {
            // Summon Eranikus Tyrant of the Dream and make him fly
            if (Creature* eranikus = spell->GetCaster()->SummonCreature(NPC_ERANIKUS_TYRANT, eranikusLocations[0].x, eranikusLocations[0].y, eranikusLocations[0].z, eranikusLocations[0].o, TEMPSPAWN_DEAD_DESPAWN, 0))
            {
                eranikus->CastSpell(nullptr, SPELL_DRAGON_HOVER, TRIGGERED_OLD_TRIGGERED);
                eranikus->SetHover(true);
                eranikus->NearTeleportTo(eranikusLocations[0].x, eranikusLocations[0].y, eranikusLocations[0].z, eranikusLocations[0].o);   // Teleport back Eranikus to its spawn position in case he fell to the ground/lake surface before hover/fly was applied (probably only client side)
            }
        }
    }
};

/*######
## go_omen_cluster
######*/

bool GOUse_go_omen_cluster(Player* /*player*/, GameObject* pGo)
{
    // The Cluster Launcher gameobject used to summon Omen is the same as the one that can be summoned from the Engineering-crafted launcher
    // So we need to make sure we are called from the statically spawned ones only
    if (pGo->GetSpawner())
        return false;

    ScriptedInstance* pInstance = (ScriptedInstance*)pGo->GetInstanceData();

    if (!pInstance)
        return false;

    // Omen encounter is set to NOT_STARTED every time the GO cluster is used
    // This increases an internal counter that handles the event in the map script
    pInstance->SetData(TYPE_OMEN, NOT_STARTED);

    // Keep the template-use script responsible for legitimate launcher quest credit.
    return false;
}

void AddSC_moonglade()
{
    Script* pNewScript;

    pNewScript = new Script;
    pNewScript->Name = "npc_keeper_remulos";
    pNewScript->GetAI = &GetAI_npc_keeper_remulos;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_keeper_remulos;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "boss_eranikus";
    pNewScript->GetAI = &GetAI_boss_eranikus;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_omen_cluster";
    pNewScript->pGOUse = &GOUse_go_omen_cluster;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ConjureDreamRift>("spell_conjure_dream_rift");
}
