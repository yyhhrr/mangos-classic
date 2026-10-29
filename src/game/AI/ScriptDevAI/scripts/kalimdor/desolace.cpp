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
SDName: Desolace
SD%Complete: 100
SDComment: Quest support: 1440, 5561, 5821, 5943, 6132
SDCategory: Desolace
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_aged_dying_ancient_kodo
npc_dalinda_malem
npc_melizza_brimbuzzle
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "AI/ScriptDevAI/scripts/kalimdor/world_kalimdor.h"
#include "Maps/SpawnManager.h"
#include "Maps/SpawnGroup.h"
#include "Entities/TemporarySpawn.h"

/*######
## npc_aged_dying_ancient_kodo
######*/

enum
{
    SAY_SMEED_HOME_1                = -1000348,
    SAY_SMEED_HOME_2                = -1000349,
    SAY_SMEED_HOME_3                = -1000350,

    QUEST_KODO                      = 5561,

    NPC_SMEED                       = 11596,
    NPC_AGED_KODO                   = 4700,
    NPC_DYING_KODO                  = 4701,
    NPC_ANCIENT_KODO                = 4702,
    NPC_TAMED_KODO                  = 11627,

    SPELL_KODO_KOMBO_ITEM           = 18153,
    SPELL_KODO_KOMBO_PLAYER_BUFF    = 18172,                // spells here have unclear function, but using them at least for visual parts and checks
    SPELL_KODO_KOMBO_DESPAWN_BUFF   = 18377,
    SPELL_KODO_KOMBO_GOSSIP         = 18362,
    SPELL_KODO_KOMBOBULATOR         = 18793,                // spell only exists in classic dbc, used by kodo on player after gossip menu is used
    SPELL_KODO_DESPAWN              = 22970,                // spell only exists in classic dbc, used by kodo during following?
};

constexpr float KODO_DELIVERY_DISTANCE = 10.0f;

struct npc_aged_dying_ancient_kodoAI : public ScriptedAI
{
    npc_aged_dying_ancient_kodoAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiDespawnTimer;

    void Reset() override
    {
        m_uiDespawnTimer = 0;
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (m_creature->GetEntry() != NPC_TAMED_KODO || !m_creature->HasAura(SPELL_KODO_KOMBO_DESPAWN_BUFF))
            return;

        if (pWho->GetEntry() == NPC_SMEED && pWho->IsAlive())
        {
            if (m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP))
                return;

            if (GetClosestCreatureWithEntry(m_creature, NPC_SMEED, KODO_DELIVERY_DISTANCE) == pWho)
            {
                switch (urand(0, 2))
                {
                    case 0: DoScriptText(SAY_SMEED_HOME_1, pWho); break;
                    case 1: DoScriptText(SAY_SMEED_HOME_2, pWho); break;
                    case 2: DoScriptText(SAY_SMEED_HOME_3, pWho); break;
                }

                // spell have no implemented effect (dummy), so useful to notify spellHit
                m_creature->CastSpell(m_creature, SPELL_KODO_KOMBO_GOSSIP, TRIGGERED_OLD_TRIGGERED);
            }
        }
    }

    void SpellHit(Unit* /*pCaster*/, SpellEntry const* pSpell) override
    {
        if (pSpell->Id == SPELL_KODO_KOMBO_GOSSIP &&
            m_creature->GetEntry() == NPC_TAMED_KODO &&
            m_creature->HasAura(SPELL_KODO_KOMBO_DESPAWN_BUFF) &&
            GetClosestCreatureWithEntry(m_creature, NPC_SMEED, KODO_DELIVERY_DISTANCE))
        {
            m_creature->GetMotionMaster()->MoveIdle();
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            m_uiDespawnTimer = 60000;
        }
    }

    void UpdateAI(const uint32 diff) override
    {
        // timer should always be == 0 unless we already updated entry of creature. Then not expect this updated to ever be in combat.
        if (m_uiDespawnTimer)
        {
            if (m_uiDespawnTimer <= diff)
            {
                if (!m_creature->GetVictim() && m_creature->IsAlive())
                {
                    Reset();
                    m_creature->SetDeathState(JUST_DIED);
                    m_creature->Respawn();
                    return;
                }
            }
            else
                m_uiDespawnTimer -= diff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_aged_dying_ancient_kodo(Creature* pCreature)
{
    return new npc_aged_dying_ancient_kodoAI(pCreature);
}

// 18153 - Kodo Kombobulator
struct KodoKombobulator : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* target = spell->m_targets.getUnitTarget();
        if (!target || (target->GetEntry() != NPC_AGED_KODO && target->GetEntry() != NPC_DYING_KODO && target->GetEntry() != NPC_ANCIENT_KODO))
            return SPELL_FAILED_BAD_TARGETS;

        if (spell->GetCaster()->HasAura(SPELL_KODO_KOMBO_PLAYER_BUFF) || target->HasAura(SPELL_KODO_KOMBO_DESPAWN_BUFF))
            return SPELL_FAILED_CASTER_AURASTATE;

        return SPELL_CAST_OK;
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        // The player buff requires a friendly target: tame the kodo before casting it.
        static_cast<Creature*>(target)->UpdateEntry(NPC_TAMED_KODO);
        // Delivery interaction opens only after this kodo reaches Smeed.
        target->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        target->CastSpell(caster, SPELL_KODO_KOMBO_PLAYER_BUFF, TRIGGERED_OLD_TRIGGERED);
        target->CastSpell(nullptr, SPELL_KODO_KOMBO_DESPAWN_BUFF, TRIGGERED_NONE);

        if (target->GetMotionMaster()->GetCurrentMovementGeneratorType() == WAYPOINT_MOTION_TYPE)
            target->GetMotionMaster()->MoveIdle();

        target->GetMotionMaster()->MoveFollow(caster, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE, true);
    }
};

bool GossipHello_npc_aged_dying_ancient_kodo(Player* pPlayer, Creature* pCreature)
{
    // Recheck the location here as stale or direct gossip requests can bypass the UI.
    if (pCreature->GetEntry() == NPC_TAMED_KODO &&
        pCreature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP) &&
        GetClosestCreatureWithEntry(pCreature, NPC_SMEED, KODO_DELIVERY_DISTANCE) &&
        pPlayer->GetQuestStatus(QUEST_KODO) == QUEST_STATUS_INCOMPLETE &&
        pPlayer->GetSpellAuraHolder(SPELL_KODO_KOMBO_PLAYER_BUFF, pCreature->GetObjectGuid()) &&
        pCreature->HasAura(SPELL_KODO_KOMBO_DESPAWN_BUFF))
    {
        // the expected quest objective
        pPlayer->TalkedToCreature(pCreature->GetEntry(), pCreature->GetObjectGuid());

        pPlayer->RemoveAurasDueToSpell(SPELL_KODO_KOMBO_PLAYER_BUFF);
        // Consume this kodo's delivery state before the delayed despawn.
        pCreature->RemoveAurasDueToSpell(SPELL_KODO_KOMBO_DESPAWN_BUFF);
        pCreature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        pCreature->ForcedDespawn(10000);
    }

    pPlayer->PrepareGossipMenu(pCreature, pPlayer->GetDefaultGossipMenuForSource(pCreature));
    pPlayer->SendPreparedGossip(pCreature);
    return true;
}

/*######
## npc_dalinda_malem
######*/

enum
{
    QUEST_RETURN_TO_VAHLARRIEL  = 1440,
    SAY_DALINDA_START          = 2143,
    SAY_DALINDA_FINISH         = 2144,
};

struct npc_dalinda_malemAI : public npc_escortAI
{
    enum EscortPhase
    {
        PHASE_WAITING,
        PHASE_STARTING,
        PHASE_ESCORTING,
        PHASE_GOODBYE,
        PHASE_DEPARTING,
        PHASE_DESPAWNING,
    };

    npc_dalinda_malemAI(Creature* m_creature) : npc_escortAI(m_creature), m_phase(PHASE_WAITING), m_pauseTimer(0)
    {
        m_creature->SetStandState(UNIT_STAND_STATE_KNEEL, true);
        Reset();
    }

    EscortPhase m_phase;
    uint32 m_pauseTimer;

    // An evade must preserve the escort phase, dialogue delay and standing pose.
    void Reset() override {}

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        SetEscortPaused(false);
        End();
        m_phase = PHASE_WAITING;
        m_pauseTimer = 0;
        m_creature->SetStandState(UNIT_STAND_STATE_KNEEL, true);
    }

    bool CanOfferQuest(uint32 questId) const override
    {
        // Also guards stale accept packets while the completed escort leaves.
        return questId != QUEST_RETURN_TO_VAHLARRIEL ||
            (m_phase == PHASE_WAITING && !HasEscortState(STATE_ESCORT_ESCORTING) && !m_creature->IsInCombat());
    }

    void JustStartedEscort() override
    {
        m_phase = PHASE_STARTING;
        m_pauseTimer = 5000;
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        SetEscortPaused(true);
        DoBroadcastText(SAY_DALINDA_START, m_creature, GetPlayerForEscort());
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // Her template is defensive: the generic sight handler otherwise returns
        // before reaching the escort helper. Preserve its attack/sight gates.
        if (HasReactState(REACT_DEFENSIVE) && HasEscortState(STATE_ESCORT_ESCORTING) &&
            (m_phase == PHASE_STARTING || m_phase == PHASE_ESCORTING) &&
            GetAIOrder() != ORDER_EVADE && !m_creature->GetVictim() &&
            !m_creature->IsNeutralToAll() && m_creature->CanInitiateAttack())
        {
            if (!m_creature->CanFly() && who->IsFlying() &&
                m_creature->GetDistanceZ(who) > (IsRangedUnit() ? CREATURE_Z_ATTACK_RANGE_RANGED : CREATURE_Z_ATTACK_RANGE_MELEE))
                return;

            if (AssistPlayerInCombat(who))
                return;
        }

        npc_escortAI::MoveInLineOfSight(who);
    }

    void WaypointReached(uint32 uiPointId) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            return;

        if (uiPointId == 18 && m_phase == PHASE_ESCORTING)
        {
            // The goodbye and credit are one-shot, including repeated movement callbacks.
            m_phase = PHASE_GOODBYE;
            m_pauseTimer = 8000;
            SetEscortPaused(true);
            Player* player = GetPlayerForEscort();
            if (player)
                player->RewardPlayerAndGroupAtEventExplored(QUEST_RETURN_TO_VAHLARRIEL, m_creature);
            DoBroadcastText(SAY_DALINDA_FINISH, m_creature, player);

            // Keep the last escape leg, without further quest failure or distance checks.
            QuestEnd();
        }
        else if (uiPointId == 19 && m_phase == PHASE_DEPARTING)
        {
            m_phase = PHASE_DESPAWNING;
            m_pauseTimer = 0;
            // The final database waypoint also has a wait to prevent a pre-sent return spline.
            SetEscortPaused(true);
            End();
            m_creature->ForcedDespawn(1000);
        }
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        // Fighting or returning from a fight must not consume the dialogue pause.
        if (m_pauseTimer && !m_creature->IsInCombat() && GetAIOrder() != ORDER_EVADE)
        {
            if (m_pauseTimer <= diff)
            {
                m_pauseTimer = 0;
                if (m_phase == PHASE_STARTING)
                    m_phase = PHASE_ESCORTING;
                else if (m_phase == PHASE_GOODBYE)
                {
                    m_phase = PHASE_DEPARTING;
                    SetRun(true);
                }
                SetEscortPaused(false);
            }
            else
                m_pauseTimer -= diff;
        }

        npc_escortAI::UpdateEscortAI(diff);
    }
};

UnitAI* GetAI_npc_dalinda_malem(Creature* pCreature)
{
    return new npc_dalinda_malemAI(pCreature);
}

bool QuestAccept_npc_dalinda_malem(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_RETURN_TO_VAHLARRIEL)
    {
        if (npc_dalinda_malemAI* pEscortAI = dynamic_cast<npc_dalinda_malemAI*>(pCreature->AI()))
        {
            if (!pEscortAI->CanOfferQuest(QUEST_RETURN_TO_VAHLARRIEL))
                return true;

            pEscortAI->Start(false, pPlayer, pQuest);
            if (!pEscortAI->HasEscortState(STATE_ESCORT_ESCORTING))
                return true;

            pCreature->SetFactionTemporary(FACTION_ESCORT_A_NEUTRAL_PASSIVE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_NPC);
        }
    }
    return true;
}

/*######
## npc_melizza_brimbuzzle
######*/

enum
{
    QUEST_GET_ME_OUT_OF_HERE    = 6132,

    GO_MELIZZAS_CAGE            = 177706,

    SAY_MELIZZA_START           = -1000784,
    SAY_MELIZZA_FINISH          = -1000785,
    SAY_MELIZZA_1               = -1000786,
    SAY_MELIZZA_2               = -1000787,
    SAY_MELIZZA_3               = -1000788,

    SAY_HORNIZZ_1               = -1010030,
    SAY_HORNIZZ_2               = -1010031,

    NPC_MARAUDINE_MARAUDER      = 4659,
    NPC_MARAUDINE_BONEPAW       = 4660,
    NPC_MARAUDINE_WRANGLER      = 4655,

    NPC_MELIZZA                 = 12277,
    // NPC_HORNIZZ                 = 6019,

    POINT_ID_QUEST_COMPLETE     = 1,
    POINT_ID_EVENT_COMPLETE     = 2,

    MAX_MARAUDERS               = 2,
    MAX_WRANGLERS               = 3,
};

static const DialogueEntry aIntroDialogue[] =
{
    {POINT_ID_QUEST_COMPLETE,   0,              1000},
    {QUEST_GET_ME_OUT_OF_HERE,  NPC_MELIZZA,    0},
    {POINT_ID_EVENT_COMPLETE,   0,              2000},
    {SAY_MELIZZA_1,             NPC_MELIZZA,    4000},
    {SAY_MELIZZA_2,             NPC_MELIZZA,    5000},
    {SAY_MELIZZA_3,             NPC_MELIZZA,    4000},
    {NPC_MELIZZA,               0,              6000},
    {SAY_HORNIZZ_1,             NPC_HORNIZZ,    10000},
    {SAY_HORNIZZ_2,             NPC_HORNIZZ,    0},
    {0, 0, 0},
};

struct SummonLocation
{
    float m_fX, m_fY, m_fZ;
};

static const SummonLocation aMarauderSpawn[] =
{
    { -1291.492f, 2644.650f, 111.556f},
    { -1306.730f, 2675.163f, 111.561f},
};

static const SummonLocation wranglerSpawn = { -1393.194f, 2429.465f, 88.689f };

struct npc_melizza_brimbuzzleAI : public npc_escortAI, private DialogueHelper
{
    explicit npc_melizza_brimbuzzleAI(Creature* creature) : npc_escortAI(creature), DialogueHelper(aIntroDialogue) {}

    GuidSet m_summons;
    bool m_firstAmbush = false;
    bool m_secondAmbush = false;
    bool m_completionStarted = false;
    bool m_completed = false;
    bool m_homeDialogue = false;
    bool m_aborted = false;
    bool m_startedText = false;

    // Preserve dialogue and ambush state across ordinary combat evades.
    void Reset() override {}

    bool CanOfferQuest(uint32 questId) const override
    {
        return questId != QUEST_GET_ME_OUT_OF_HERE ||
            (!HasEscortState(STATE_ESCORT_ESCORTING) && !m_completed && !m_aborted && !m_creature->IsInCombat());
    }

    bool IsOwnedAmbusher(Creature* actor) const
    {
        return actor && actor->IsTemporarySummon() && actor->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
            (actor->GetEntry() == NPC_MARAUDINE_MARAUDER || actor->GetEntry() == NPC_MARAUDINE_BONEPAW ||
                actor->GetEntry() == NPC_MARAUDINE_WRANGLER);
    }

    void CleanupSummons()
    {
        GuidSet summons;
        summons.swap(m_summons); // Despawn callbacks can synchronously mutate the roster.
        for (ObjectGuid guid : summons)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (IsOwnedAmbusher(actor) && actor->IsAlive())
                    actor->ForcedDespawn(); // Keep dead actors and their loot.
    }

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        CleanupSummons();
        DisableDialogue();
        End();
        m_firstAmbush = m_secondAmbush = m_completionStarted = m_completed = false;
        m_homeDialogue = m_aborted = m_startedText = false;
    }

    void FailQuestForPlayerAndGroup() override
    {
        m_aborted = true;
        DisableDialogue();
        CleanupSummons();
        npc_escortAI::FailQuestForPlayerAndGroup();
    }

    void JustDied(Unit* killer) override
    {
        npc_escortAI::JustDied(killer);
        m_aborted = true;
        DisableDialogue();
        CleanupSummons();
    }

    void AbortEscort()
    {
        if (m_aborted)
            return;
        FailQuestForPlayerAndGroup();
        End();
        m_creature->ForcedDespawn();
    }

    void JustStartedEscort() override
    {
        CleanupSummons();
        DisableDialogue();
        m_firstAmbush = m_secondAmbush = m_completionStarted = m_completed = false;
        m_homeDialogue = m_aborted = m_startedText = false;
        if (GameObject* cage = GetClosestGameObjectWithEntry(m_creature, GO_MELIZZAS_CAGE, INTERACTION_DISTANCE))
            cage->UseDoorOrButton();
    }

    void JustSummoned(Creature* actor) override
    {
        if (!IsOwnedAmbusher(actor))
            return;
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_aborted || m_completed)
        {
            if (actor->IsAlive())
                actor->ForcedDespawn();
            return;
        }
        m_summons.insert(actor->GetObjectGuid());
    }

    void SummonedCreatureJustDied(Creature* actor) override { m_summons.erase(actor->GetObjectGuid()); }
    void SummonedCreatureDespawn(Creature* actor) override { m_summons.erase(actor->GetObjectGuid()); }

    bool SummonAmbusher(uint32 entry, float x, float y, float z)
    {
        // Combat continually refreshes this timeout. It only bounds actors left
        // idle after an interrupted escort, even if their owner has disappeared.
        Creature* actor = m_creature->SummonCreature(entry, x, y, z, 0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 120000);
        if (!actor || m_aborted || !HasEscortState(STATE_ESCORT_ESCORTING) || !m_summons.count(actor->GetObjectGuid()))
        {
            if (IsOwnedAmbusher(actor) && actor->IsAlive())
                actor->ForcedDespawn();
            AbortEscort();
            return false;
        }
        return true;
    }

    Creature* GetSpeakerByEntry(uint32 entry) override
    {
        if (entry == NPC_MELIZZA)
            return m_creature;
        if (entry == NPC_HORNIZZ)
            if (ScriptedInstance* instance = static_cast<ScriptedInstance*>(m_creature->GetInstanceData()))
                return instance->GetSingleCreatureFromStorage(NPC_HORNIZZ);
        return nullptr;
    }

    void WaypointReached(uint32 pointId) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_aborted)
            return;
        switch (pointId)
        {
            case 1:
                if (m_startedText)
                    break;
                m_startedText = true;
                if (Player* player = GetPlayerForEscort())
                    DoScriptText(SAY_MELIZZA_START, m_creature, player);
                m_creature->SetFactionTemporary(FACTION_ESCORT_N_NEUTRAL_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
                break;
            case 4:
                if (m_firstAmbush || m_completed)
                    break;
                m_firstAmbush = true;
                for (auto location : aMarauderSpawn)
                    for (uint8 j = 0; j < MAX_MARAUDERS; ++j)
                    {
                        float x, y, z;
                        m_creature->GetRandomPoint(location.m_fX, location.m_fY, location.m_fZ, 7.0f, x, y, z);
                        if (!SummonAmbusher(NPC_MARAUDINE_MARAUDER, x, y, z))
                            return;
                    }
                break;
            case 9:
                if (m_secondAmbush || m_completed)
                    break;
                m_secondAmbush = true;
                for (uint8 i = 0; i < MAX_WRANGLERS; ++i)
                {
                    float x, y, z;
                    m_creature->GetRandomPoint(wranglerSpawn.m_fX, wranglerSpawn.m_fY, wranglerSpawn.m_fZ, 10.0f, x, y, z);
                    if (!SummonAmbusher(NPC_MARAUDINE_BONEPAW, x, y, z))
                        return;
                    m_creature->GetRandomPoint(wranglerSpawn.m_fX, wranglerSpawn.m_fY, wranglerSpawn.m_fZ, 10.0f, x, y, z);
                    if (!SummonAmbusher(NPC_MARAUDINE_WRANGLER, x, y, z))
                        return;
                }
                break;
            case 12:
                if (!m_completionStarted)
                {
                    m_completionStarted = true;
                    StartNextDialogueText(POINT_ID_QUEST_COMPLETE);
                }
                break;
            case 19:
                if (m_completed && !m_homeDialogue)
                {
                    m_homeDialogue = true;
                    StartNextDialogueText(POINT_ID_EVENT_COMPLETE);
                }
                break;
        }
    }

    void JustDidDialogueStep(int32 entry) override
    {
        if (m_aborted || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        switch (entry)
        {
            case POINT_ID_QUEST_COMPLETE:
                SetEscortPaused(true);
                break;
            case QUEST_GET_ME_OUT_OF_HERE:
            {
                if (m_completed)
                    break;
                Player* player = GetPlayerForEscort();
                if (!player || (player->GetQuestStatus(QUEST_GET_ME_OUT_OF_HERE) != QUEST_STATUS_INCOMPLETE &&
                    player->GetQuestStatus(QUEST_GET_ME_OUT_OF_HERE) != QUEST_STATUS_COMPLETE))
                {
                    AbortEscort();
                    break;
                }
                m_completed = true; // Set before credit can invoke other script hooks.
                DoScriptText(SAY_MELIZZA_FINISH, m_creature, player);
                player->RewardPlayerAndGroupAtEventExplored(QUEST_GET_ME_OUT_OF_HERE, m_creature);
                QuestEnd(); // The escape-home scene no longer has a quest-distance lease.
                m_creature->ClearTemporaryFaction();
                SetRun(true);
                SetEscortPaused(false);
                break;
            }
            case POINT_ID_EVENT_COMPLETE:
                SetEscortPaused(true);
                m_creature->SetFacingTo(4.71f);
                break;
            case NPC_MELIZZA:
                SetEscortPaused(false);
                break;
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (!m_aborted && !m_creature->IsInCombat() && GetAIOrder() != ORDER_EVADE)
            DialogueUpdate(diff);
        npc_escortAI::UpdateEscortAI(diff);
    }
};

UnitAI* GetAI_npc_melizza_brimbuzzle(Creature* pCreature)
{
    return new npc_melizza_brimbuzzleAI(pCreature);
}

bool QuestAccept_npc_melizza_brimbuzzle(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_GET_ME_OUT_OF_HERE)
    {
        if (npc_melizza_brimbuzzleAI* pEscortAI = dynamic_cast<npc_melizza_brimbuzzleAI*>(pCreature->AI()))
            if (pEscortAI->CanOfferQuest(QUEST_GET_ME_OUT_OF_HERE))
                pEscortAI->Start(false, pPlayer, pQuest);
    }

    return true;
}

/*######
## npc_cork_gizelton
######*/

enum
{
    NPC_CORK_GIZELTON = 11625,
    NPC_RIGGER_GIZELTON = 11626,
    NPC_VENDOR_TRON = 12245,
    NPC_SUPER_SELLER = 12246,

    QUEST_BODYGUARD_TO_HIRE = 5821,
    QUEST_GIZELTON_CARAVAN = 5943,

    WAYPOINT_PATH_ID = 19019,
    PATH_ID_CORK_GIZELTON = 11625,
    PATH_ID_RIGGER_GIZELTON = 11626,
    WAYPOINT_PATH_POINT_AFTER_ESCORT_1 = 33,
    WAYPOINT_PATH_POINT_AFTER_ESCORT_2 = 100,

    GIZELTON_TIMER_AFTER_FAIL = 60 * IN_MILLISECONDS, // forced despawn when one of the member die even if still in combat
    GIZELTON_CARAVAN_GROUP = 19019,
};

struct npc_cork_gizeltonAI : public npc_escortAI
{
    explicit npc_cork_gizeltonAI(Creature* creature) : npc_escortAI(creature) {}

    uint32 FailTimer = GIZELTON_TIMER_AFTER_FAIL;
    bool StatusFailed = false;
    bool ScriptDone = false;
    bool SouthQuest = false;
    uint32 m_groupId = 0;
    uint32 m_activeQuest = 0;
    GuidSet m_participants;

    // A combat evade must not forget a running escort or its failure countdown.
    void Reset() override {}

    CreatureGroup* CaravanGroup() const
    {
        if (CreatureGroup* group = m_creature->GetCreatureGroup())
            return group;
        // Death/removal can detach the NPC before a delayed cleanup callback.
        if (SpawnGroup* group = m_creature->GetMap()->GetSpawnManager().GetSpawnGroup(m_groupId ? m_groupId : GIZELTON_CARAVAN_GROUP))
            for (auto const& member : group->GetGroupEntry().DbGuids)
                if (member.DbGuid == m_creature->GetDbGuid())
                    return group->GetCreatureGroup();
        return nullptr;
    }

    void RememberGroup()
    {
        if (CreatureGroup* group = m_creature->GetCreatureGroup())
            m_groupId = group->GetGroupId();
    }

    bool CanOfferQuest(uint32 questId) const override
    {
        if (questId != QUEST_BODYGUARD_TO_HIRE && questId != QUEST_GIZELTON_CARAVAN)
            return true;
        return m_creature->IsAlive() && !m_creature->IsInCombat() &&
            !HasEscortState(STATE_ESCORT_ESCORTING) && !StatusFailed && !ScriptDone && CaravanGroup();
    }

    void FailRegisteredPlayers()
    {
        // Keep the admitted group even when its original leader logs out or
        // changes group. Never fail the other caravan quest opportunistically.
        for (ObjectGuid guid : m_participants)
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                if (m_activeQuest && player->GetQuestStatus(m_activeQuest) == QUEST_STATUS_INCOMPLETE)
                    player->FailQuest(m_activeQuest);
    }

    void MakeQuestFailAndClean()
    {
        if (ScriptDone)
            return;
        RememberGroup();
        ScriptDone = StatusFailed = true; // Set before despawn callbacks can reenter.
        FailRegisteredPlayers();
        m_participants.clear();
        m_activeQuest = 0;
        End();
        if (CreatureGroup* group = CaravanGroup())
            group->Despawn();
        else
            m_creature->ForcedDespawn();
    }

    void FailQuestForPlayerAndGroup() override
    {
        // The base distance/offline failure route calls this before despawn;
        // converge on the same cleanup even though JustDied is never called.
        MakeQuestFailAndClean();
    }

    void JustDied(Unit* killer) override
    {
        RememberGroup();
        npc_escortAI::JustDied(killer);
        MakeQuestFailAndClean(); // A dead Cork cannot run the ordinary timer.
    }

    void JustRespawned() override
    {
        npc_escortAI::JustRespawned();
        End();
        FailTimer = GIZELTON_TIMER_AFTER_FAIL;
        StatusFailed = ScriptDone = SouthQuest = false;
        m_activeQuest = 0;
        m_participants.clear();
        RememberGroup();
    }

    bool TryStartEscort(Player* player, Quest const* quest, bool south, Creature* giver)
    {
        uint32 questId = south ? QUEST_GIZELTON_CARAVAN : QUEST_BODYGUARD_TO_HIRE;
        if (!player || !quest || quest->GetQuestId() != questId || !giver ||
            giver->GetMap() != m_creature->GetMap() || !giver->IsAlive() ||
            giver->GetEntry() != uint32(south ? NPC_RIGGER_GIZELTON : NPC_CORK_GIZELTON) ||
            !giver->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) ||
            !CanOfferQuest(questId) || player->GetMap() != m_creature->GetMap() ||
            player->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            return false;

        RememberGroup();
        Start(false, player, quest, false, false, south ? PATH_ID_RIGGER_GIZELTON : PATH_ID_CORK_GIZELTON);
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || m_questForEscort != quest || GetPlayerForEscort() != player)
            return false;

        SouthQuest = south; // Commit direction only after Start admitted this quest.
        FailTimer = GIZELTON_TIMER_AFTER_FAIL;
        m_activeQuest = questId;
        m_participants.clear();
        m_participants.insert(player->GetObjectGuid());
        if (Group* group = player->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->getSource())
                    if (member->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                        m_participants.insert(member->GetObjectGuid());

        // StartScript is zero in the quest row: rejected/duplicate acceptances
        // must not launch faction/flag scripts. Preserve the original giver.
        if (!m_creature->GetMap()->ScriptsStart(SCRIPT_TYPE_QUEST_START, questId, giver, player,
            Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE))
        {
            MakeQuestFailAndClean();
            return false;
        }
        return true;
    }

    bool StartRiggerEscort(Player* player, Quest const* quest, Creature* giver)
    {
        return TryStartEscort(player, quest, true, giver);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        RememberGroup();
        npc_escortAI::UpdateEscortAI(diff);
        if (ScriptDone || !StatusFailed)
            return;
        CreatureGroup* group = CaravanGroup();
        if (FailTimer > diff && group && !group->IsOutOfCombat())
        {
            FailTimer -= diff;
            return;
        }
        MakeQuestFailAndClean();
    }

    void WaypointReached(uint32 pointId) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || StatusFailed || ScriptDone)
            return;
        if ((!SouthQuest && pointId != 19) || (SouthQuest && pointId != 21))
            return;
        uint32 questId = SouthQuest ? QUEST_GIZELTON_CARAVAN : QUEST_BODYGUARD_TO_HIRE;
        if (m_activeQuest != questId)
            return;
        if (Player* player = GetPlayerForEscort())
            if (player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
                player->RewardPlayerAndGroupAtEventExplored(questId, m_creature);

        m_creature->GetMotionMaster()->Clear(false, true);
        m_creature->GetMotionMaster()->MoveWaypoint(WAYPOINT_PATH_ID, PATH_FROM_WAYPOINT_PATH);
        m_creature->GetMotionMaster()->SetNextWaypoint(SouthQuest ? WAYPOINT_PATH_POINT_AFTER_ESCORT_2 : WAYPOINT_PATH_POINT_AFTER_ESCORT_1);
        m_creature->SetWalk(false);
        End();
        m_activeQuest = 0;
        m_participants.clear();
        SouthQuest = false;
    }

    void CreatureGroupMemberDied(Unit* /*killed*/) override
    {
        RememberGroup();
        if (!ScriptDone && !StatusFailed)
        {
            StatusFailed = true;
            FailTimer = GIZELTON_TIMER_AFTER_FAIL;
        }
    }
};

bool QuestAccept_npc_cork_gizelton(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_BODYGUARD_TO_HIRE)
        if (auto* ai = dynamic_cast<npc_cork_gizeltonAI*>(creature->AI()))
            ai->TryStartEscort(player, quest, false, creature);
    return true;
}

/*######
## npc_rigger_gizelton
######*/

struct npc_rigger_gizeltonAI : public ScriptedAI
{
    explicit npc_rigger_gizeltonAI(Creature* creature) : ScriptedAI(creature) {}

    void Reset() override {}

    bool CanOfferQuest(uint32 questId) const override
    {
        if (questId != QUEST_GIZELTON_CARAVAN)
            return true;
        if (!m_creature->IsAlive() || m_creature->IsInCombat())
            return false;
        if (Creature* cork = GetClosestCreatureWithEntry(m_creature, NPC_CORK_GIZELTON, 100.0f))
            if (auto* ai = dynamic_cast<npc_cork_gizeltonAI*>(cork->AI()))
                return ai->CanOfferQuest(questId);
        return false;
    }
};

bool QuestAccept_npc_rigger_gizelton(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_GIZELTON_CARAVAN)
        if (Creature* cork = GetClosestCreatureWithEntry(creature, NPC_CORK_GIZELTON, 100.0f))
            if (auto* ai = dynamic_cast<npc_cork_gizeltonAI*>(cork->AI()))
                ai->StartRiggerEscort(player, quest, creature);
    return true;
}

enum
{
    SPELL_CURSE_OF_MAGRAMI = 18159,
    SPELL_GHOST_SPAWN_IN   = 17321,
    SPELL_BLUE_AURA        = 17327,
    SPELL_GREEN_AURA       = 18951,
    SPELL_CHILLING_TOUCH   = 18146,

    FACTION_HOSTILE        = 16,

    SAY_EMOTE_1            = -1001226,
    SAY_EMOTE_2            = -1001227,
};

struct npc_magrami_spectre : public ScriptedAI
{
    npc_magrami_spectre(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiCurseTimer;
    uint32 m_curseAttempts;

    void Reset() override
    {
        m_uiCurseTimer = 4000;
        m_curseAttempts = 0;
    }

    void JustRespawned() override
    {
        m_creature->CastSpell(nullptr, SPELL_GHOST_SPAWN_IN, TRIGGERED_NONE);
        m_creature->CastSpell(nullptr, SPELL_BLUE_AURA, TRIGGERED_NONE);
        m_creature->CastSpell(nullptr, SPELL_CHILLING_TOUCH, TRIGGERED_NONE);

        switch (urand(0, 1))
        {
            case 0:
                DoScriptText(SAY_EMOTE_1, m_creature);
                break;
            case 1:
                DoScriptText(SAY_EMOTE_2, m_creature);
                break;
        }
    }

    void MovementInform(uint32 uiMovementType, uint32 uiData) override
    {
        if (uiMovementType == POINT_MOTION_TYPE)
        {
            if (uiData == 1)
            {
                float x, y, z;
                m_creature->GetPosition(x, y, z);
                m_creature->GetMotionMaster()->MoveRandomAroundPoint(x, y, z, 5.f);
                m_creature->RemoveAurasDueToSpell(SPELL_BLUE_AURA);
                m_creature->CastSpell(m_creature, SPELL_GREEN_AURA, TRIGGERED_NONE);
                m_creature->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_NONE);
            }
        }
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_uiCurseTimer)
        {
            if (m_uiCurseTimer <= uiDiff)
            {
                ++m_curseAttempts;
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CURSE_OF_MAGRAMI) == CAST_OK || m_curseAttempts >= 3)
                    m_uiCurseTimer = 0;
                else
                    m_uiCurseTimer = 1000;
            }
            else
                m_uiCurseTimer -= uiDiff;
        }

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_magrami_spectre(Creature* pCreature)
{
    return new npc_magrami_spectre(pCreature);
}

void AddSC_desolace()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_aged_dying_ancient_kodo";
    pNewScript->GetAI = &GetAI_npc_aged_dying_ancient_kodo;
    pNewScript->pGossipHello = &GossipHello_npc_aged_dying_ancient_kodo;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_dalinda_malem";
    pNewScript->GetAI = &GetAI_npc_dalinda_malem;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_dalinda_malem;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_melizza_brimbuzzle";
    pNewScript->GetAI = &GetAI_npc_melizza_brimbuzzle;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_melizza_brimbuzzle;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_cork_gizelton";
    pNewScript->GetAI = &GetNewAIInstance<npc_cork_gizeltonAI>;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_cork_gizelton;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_rigger_gizelton";
    pNewScript->GetAI = &GetNewAIInstance<npc_rigger_gizeltonAI>;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_rigger_gizelton;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_magrami_spectre";
    pNewScript->GetAI = &GetAI_npc_magrami_spectre;
    pNewScript->RegisterSelf();

    RegisterSpellScript<KodoKombobulator>("spell_kodo_kombobulator");
}
