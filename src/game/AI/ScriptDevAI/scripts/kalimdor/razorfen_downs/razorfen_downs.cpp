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
SDName: Razorfen_Downs
SD%Complete: 100
SDComment: Quest 3525
SDCategory: Razorfen Downs
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_belnistrasz
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Groups/Group.h"
#include "razorfen_downs.h"

/*###
# npc_belnistrasz
####*/

enum
{
    QUEST_EXTINGUISHING_THE_IDOL    = 3525,

    SAY_BELNISTRASZ_READY           = -1129005,
    SAY_BELNISTRASZ_START_RIT       = -1129006,
    SAY_BELNISTRASZ_AGGRO_1         = -1129007,
    SAY_BELNISTRASZ_AGGRO_2         = -1129008,
    SAY_BELNISTRASZ_3_MIN           = -1129009,
    SAY_BELNISTRASZ_2_MIN           = -1129010,
    SAY_BELNISTRASZ_1_MIN           = -1129011,
    SAY_BELNISTRASZ_FINISH          = -1129012,

    NPC_IDOL_ROOM_SPAWNER           = 8611,

    NPC_WITHERED_BATTLE_BOAR        = 7333,
    NPC_WITHERED_QUILGUARD          = 7329,
    NPC_DEATHS_HEAD_GEOMANCER       = 7335,
    NPC_PLAGUEMAW_THE_ROTTING       = 7356,

    GO_BELNISTRASZ_BRAZIER          = 152097,
    GO_IDOL_OVEN_FIRE               = 151951,
    GO_IDOL_CUP_FIRE                = 151952,
    GO_IDOL_MOUTH_FIRE              = 151973,

    SPELL_ARCANE_INTELLECT          = 13326,                // use this somewhere (he has it as default)
    SPELL_FIREBALL                  = 9053,
    SPELL_FROST_NOVA                = 11831,
    SPELL_IDOL_SHUTDOWN             = 12774,
    SPELL_IDOL_ROOM_SHAKE           = 12816,

    // summon spells only exist in 1.x
    // SPELL_SUMMON_1                  = 12694,             // NPC_WITHERED_BATTLE_BOAR
    // SPELL_SUMMON_2                  = 14802,             // NPC_DEATHS_HEAD_GEOMANCER
    // SPELL_SUMMON_3                  = 14801,             // NPC_WITHERED_QUILGUARD
};

static float m_fSpawnerCoord[3][4] =
{
    {2582.79f, 954.392f, 52.4821f, 3.78736f},
    {2569.42f, 956.380f, 52.2732f, 5.42797f},
    {2570.62f, 942.393f, 53.7433f, 0.71558f}
};

static const uint32 aGOList[] = {GO_IDOL_OVEN_FIRE, GO_IDOL_CUP_FIRE, GO_IDOL_MOUTH_FIRE};

struct npc_belnistraszAI : public npc_escortAI
{
    explicit npc_belnistraszAI(Creature* creature) : npc_escortAI(creature)
    {
        Reset();
    }

    uint8 m_uiRitualPhase = 0;
    uint32 m_uiRitualTimer = 1000;
    bool m_bAggro = false;
    bool m_ritualActive = false;
    bool m_eventCompleted = false;
    bool m_stationary = false;
    bool m_restoreMovement = true;
    bool m_restoreMelee = true;
    bool m_restoreScriptStatus = false;
    ReactStates m_restoreReact = REACT_AGGRESSIVE;
    bool m_lastWaveSucceeded = false;
    uint8 m_waveFailures = 0;
    ObjectGuid m_ownerGuid;
    uint32 m_initialGroupId = 0;
    GuidSet m_participants;
    GuidSet m_summons;
    uint32 m_uiFireballTimer = 1000;
    uint32 m_uiFrostNovaTimer = 6000;

    void Reset() override
    {
        // Combat evade must not restart an already running ritual.
        m_uiFireballTimer = 1000;
        m_uiFrostNovaTimer = 6000;
    }

    void RestoreRitualMovement()
    {
        if (!m_stationary)
            return;
        m_stationary = false;
        SetCombatMovement(m_restoreMovement);
        SetMeleeEnabled(m_restoreMelee);
        SetReactState(m_restoreReact);
        SetCombatScriptStatus(m_restoreScriptStatus);
    }

    void CleanupSummons()
    {
        GuidSet summons;
        summons.swap(m_summons); // Despawn callbacks cannot invalidate the traversal.
        for (ObjectGuid guid : summons)
            if (Creature* creature = m_creature->GetMap()->GetCreature(guid))
                if (creature->IsAlive())
                    creature->ForcedDespawn(); // Leave genuinely killed enemies' corpses alone.
    }

    void ClearRitualState(bool cleanup)
    {
        m_ritualActive = false;
        RestoreRitualMovement();
        if (cleanup)
            CleanupSummons();
        else
            m_summons.clear();
        m_uiRitualPhase = 0;
        m_uiRitualTimer = 1000;
        m_bAggro = false;
        m_eventCompleted = false;
        m_lastWaveSucceeded = false;
        m_waveFailures = 0;
        m_participants.clear();
        m_ownerGuid.Clear();
        m_initialGroupId = 0;
    }

    bool IsInitialParticipant(Player* player) const
    {
        if (!player || m_participants.find(player->GetObjectGuid()) == m_participants.end())
            return false;
        if (player->GetObjectGuid() == m_ownerGuid)
            return true;
        Group* group = player->GetGroup();
        return m_initialGroupId && group && group->GetId() == m_initialGroupId;
    }

    bool BeginEscort(Player* player, Quest const* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_EXTINGUISHING_THE_IDOL ||
            HasEscortState(STATE_ESCORT_ESCORTING) || m_creature->GetVictim())
            return false;
        ClearRitualState(true);
        m_ownerGuid = player->GetObjectGuid();
        m_participants.insert(m_ownerGuid);
        if (Group* group = player->GetGroup())
        {
            m_initialGroupId = group->GetId();
            for (auto const& member : group->GetMemberSlots())
                m_participants.insert(member.guid);
        }
        Start(true, player, quest);
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
        {
            ClearRitualState(true);
            return false;
        }
        return true;
    }

    void FailQuestForPlayerAndGroup() override
    {
        // The base escort still owns death/distance failure detection. Its virtual
        // helper limits notification to the party that actually began this run.
        if (!m_questForEscort || m_eventCompleted)
            return;
        for (ObjectGuid guid : m_participants)
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                if (IsInitialParticipant(player) && player->GetQuestStatus(QUEST_EXTINGUISHING_THE_IDOL) == QUEST_STATUS_INCOMPLETE)
                    player->FailQuest(QUEST_EXTINGUISHING_THE_IDOL);
    }

    void JustDied(Unit* killer) override
    {
        npc_escortAI::JustDied(killer);
        ClearRitualState(!m_eventCompleted);
    }

    void CorpseRemoved(uint32& respawnDelay) override
    {
        // ForcedDespawn, including the base escort's distance-failure path,
        // does not call JustDied. Clean that path too without erasing loot.
        if (!m_eventCompleted && HasEscortState(STATE_ESCORT_ESCORTING))
            FailQuestForPlayerAndGroup();
        ClearRitualState(!m_eventCompleted);
        npc_escortAI::CorpseRemoved(respawnDelay);
    }

    void JustRespawned() override
    {
        ClearRitualState(!m_eventCompleted);
        npc_escortAI::JustRespawned();
    }

    void AttackStart(Unit* attacker) override
    {
        if (!m_ritualActive)
            npc_escortAI::AttackStart(attacker);
    }

    void AttackedBy(Unit* attacker) override
    {
        if (m_ritualActive)
        {
            if (!m_bAggro)
            {
                DoScriptText(urand(0, 1) ? SAY_BELNISTRASZ_AGGRO_1 : SAY_BELNISTRASZ_AGGRO_2, m_creature, attacker);
                m_bAggro = true;
            }
            return;
        }
        ScriptedAI::AttackedBy(attacker);
    }

    void TrackSummon(Creature* creature)
    {
        if (creature)
            m_summons.insert(creature->GetObjectGuid());
    }

    bool SpawnerSummon(Creature* summoner)
    {
        GuidVector wave;
        const uint32 count = m_uiRitualPhase > 7 ? 1u : 4u;
        for (uint32 i = 0; i < count; ++i)
        {
            const uint32 entry = count == 1 ? NPC_PLAGUEMAW_THE_ROTTING :
                (i < 2 ? NPC_WITHERED_BATTLE_BOAR : (i == 2 ? NPC_WITHERED_QUILGUARD : NPC_DEATHS_HEAD_GEOMANCER));
            float x = summoner->GetPositionX(), y = summoner->GetPositionY(), z = summoner->GetPositionZ();
            if (count > 1)
                summoner->GetClosePoint(x, y, z, 0.0f, 2.0f, 2.0f * M_PI_F * rand_norm_f());
            if (Creature* creature = summoner->SummonCreature(entry, x, y, z, summoner->GetOrientation(), TEMPSPAWN_TIMED_OOC_DESPAWN, 60000))
            {
                TrackSummon(creature);
                wave.push_back(creature->GetObjectGuid());
            }
        }
        if (wave.size() == count)
            return true;
        // Retry an incomplete wave as one unit; never accumulate partial waves.
        for (ObjectGuid guid : wave)
        {
            m_summons.erase(guid);
            if (Creature* creature = m_creature->GetMap()->GetCreature(guid))
                if (creature->IsAlive())
                    creature->ForcedDespawn();
        }
        return false;
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() != NPC_IDOL_ROOM_SPAWNER)
            return;
        TrackSummon(summoned);
        if (m_ritualActive)
            m_lastWaveSucceeded = SpawnerSummon(summoned);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        m_summons.erase(summoned->GetObjectGuid());
    }

    bool DoSummonSpawner(int32 index)
    {
        m_lastWaveSucceeded = false;
        Creature* spawner = m_creature->SummonCreature(NPC_IDOL_ROOM_SPAWNER,
            m_fSpawnerCoord[index][0], m_fSpawnerCoord[index][1], m_fSpawnerCoord[index][2], m_fSpawnerCoord[index][3], TEMPSPAWN_TIMED_DESPAWN, 10000);
        if (spawner && m_lastWaveSucceeded)
        {
            m_waveFailures = 0;
            return true;
        }
        if (spawner)
        {
            m_summons.erase(spawner->GetObjectGuid());
            if (spawner->IsAlive())
                spawner->ForcedDespawn();
        }
        m_uiRitualTimer = 1000;
        if (++m_waveFailures >= 3)
        {
            FailQuestForPlayerAndGroup();
            ClearRitualState(true);
            m_creature->ForcedDespawn();
        }
        return false;
    }

    void WaypointReached(uint32 point) override
    {
        if (point != 24 || m_ritualActive || m_eventCompleted)
            return;
        m_uiRitualPhase = 0;
        m_uiRitualTimer = 1000;
        m_bAggro = false;
        m_ritualActive = true;
        m_restoreMovement = IsCombatMovement();
        m_restoreMelee = IsMeleeEnabled();
        m_restoreReact = GetReactState();
        m_restoreScriptStatus = GetCombatScriptStatus();
        m_stationary = true;
        DoScriptText(SAY_BELNISTRASZ_START_RIT, m_creature);
        SetEscortPaused(true);
        SetCombatMovement(false, true);
        SetMeleeEnabled(false);
        SetReactState(REACT_PASSIVE);
        SetCombatScriptStatus(true);
        m_creature->AttackStop(true);
        m_creature->InterruptMoving();
    }

    void CompleteRitual()
    {
        if (m_eventCompleted)
            return;
        m_eventCompleted = true; // Clear before world/quest callbacks can re-enter.
        if (auto instance = static_cast<instance_razorfen_downs*>(m_creature->GetInstanceData()))
            instance->CompleteIdolEvent(); // Seconds-based brazier and persistent world state, even with owner offline.
        for (ObjectGuid guid : m_participants)
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                if (IsInitialParticipant(player) && player->GetQuestStatus(QUEST_EXTINGUISHING_THE_IDOL) == QUEST_STATUS_INCOMPLETE &&
                    player->IsAtGroupRewardDistance(m_creature) && !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                    player->AreaExploredOrEventHappens(QUEST_EXTINGUISHING_THE_IDOL);
        m_creature->RemoveAurasDueToSpell(SPELL_IDOL_SHUTDOWN);
        DoCastSpellIfCan(m_creature, SPELL_IDOL_ROOM_SHAKE);
        m_ritualActive = false;
        RestoreRitualMovement();
        SetEscortPaused(false);
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (m_ritualActive)
        {
            if (m_uiRitualTimer > diff)
            {
                m_uiRitualTimer -= diff;
                return;
            }
            switch (m_uiRitualPhase)
            {
                case 0:
                    if (DoCastSpellIfCan(m_creature, SPELL_IDOL_SHUTDOWN) != CAST_OK)
                    { m_uiRitualTimer = 1000; return; }
                    m_uiRitualTimer = 1000;
                    break;
                case 1:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    m_uiRitualTimer = 39000;
                    break;
                case 2:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    m_uiRitualTimer = 20000;
                    break;
                case 3:
                    DoScriptText(SAY_BELNISTRASZ_3_MIN, m_creature, m_creature);
                    m_uiRitualTimer = 20000;
                    break;
                case 4:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    m_uiRitualTimer = 40000;
                    break;
                case 5:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    DoScriptText(SAY_BELNISTRASZ_2_MIN, m_creature, m_creature);
                    m_uiRitualTimer = 40000;
                    break;
                case 6:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    m_uiRitualTimer = 20000;
                    break;
                case 7:
                    DoScriptText(SAY_BELNISTRASZ_1_MIN, m_creature, m_creature);
                    m_uiRitualTimer = 40000;
                    break;
                case 8:
                    if (!DoSummonSpawner(irand(0, 2))) return;
                    m_uiRitualTimer = 20000;
                    break;
                case 9:
                    DoScriptText(SAY_BELNISTRASZ_FINISH, m_creature, m_creature);
                    m_uiRitualTimer = 3000;
                    break;
                case 10:
                    CompleteRitual();
                    break;
                default:
                    return;
            }
            ++m_uiRitualPhase;
            return;
        }
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;
        if (m_uiFireballTimer <= diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_FIREBALL) == CAST_OK)
                m_uiFireballTimer = urand(2000, 3000);
        }
        else
            m_uiFireballTimer -= diff;
        if (m_uiFrostNovaTimer <= diff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_FROST_NOVA) == CAST_OK)
                m_uiFrostNovaTimer = urand(10000, 15000);
        }
        else
            m_uiFrostNovaTimer -= diff;
        DoMeleeAttackIfReady();
    }
};


UnitAI* GetAI_npc_belnistrasz(Creature* pCreature)
{
    return new npc_belnistraszAI(pCreature);
}

bool QuestAccept_npc_belnistrasz(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_EXTINGUISHING_THE_IDOL)
    {
        if (npc_belnistraszAI* pEscortAI = dynamic_cast<npc_belnistraszAI*>(pCreature->AI()))
        {
            if (pEscortAI->BeginEscort(pPlayer, pQuest))
            {
                DoScriptText(SAY_BELNISTRASZ_READY, pCreature, pPlayer);
                pCreature->SetFactionTemporary(FACTION_ESCORT_N_NEUTRAL_ACTIVE, TEMPFACTION_RESTORE_RESPAWN);
            }
        }
    }

    return true;
}

void AddSC_razorfen_downs()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_belnistrasz";
    pNewScript->GetAI = &GetAI_npc_belnistrasz;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_belnistrasz;
    pNewScript->RegisterSelf();
}
