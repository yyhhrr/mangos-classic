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
SDName: Razorfen_Kraul
SD%Complete: 100
SDComment: Quest support: 1144, 1221
SDCategory: Razorfen Kraul
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
quest_willix_the_importer
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "AI/ScriptDevAI/base/pet_ai.h"

/*######
## npc_willix_the_importer
######*/

enum
{
    QUEST_WILLIX_THE_IMPORTER  = 1144,

    SAY_WILLIX_READY           = -1047000,
    SAY_WILLIX_1               = -1047001,
    SAY_WILLIX_2               = -1047002,
    SAY_WILLIX_3               = -1047003,
    SAY_WILLIX_4               = -1047004,
    SAY_WILLIX_5               = -1047005,
    SAY_WILLIX_6               = -1047006,
    SAY_WILLIX_7               = -1047007,
    SAY_WILLIX_END             = -1047008,

    SAY_WILLIX_AGGRO_1         = -1047009,
    SAY_WILLIX_AGGRO_2         = -1047010,
    SAY_WILLIX_AGGRO_3         = -1047011,
    SAY_WILLIX_AGGRO_4         = -1047012,

    NPC_RAGING_AGAMAR          = 4514
};

static const float aBoarSpawn[4][3] =
{
    {2151.420f, 1733.18f, 52.10f},
    {2144.463f, 1726.89f, 51.93f},
    {1956.433f, 1597.97f, 81.75f},
    {1958.971f, 1599.01f, 81.44f}
};

struct npc_willix_the_importerAI : public npc_escortAI
{
    npc_willix_the_importerAI(Creature* m_creature) : npc_escortAI(m_creature), m_turnInTimer(0) { Reset(); }

    uint32 m_turnInTimer;

    void Reset() override
    {
        // A combat reset must not restart the completed escort's hand-in window.
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            m_turnInTimer = 0;
    }

    bool CanOfferQuest(uint32 questId) const override
    {
        // HasInvolvedQuest remains available to the original participants.
        // HasQuest also guards stale accept packets, not just the gossip menu.
        return questId != QUEST_WILLIX_THE_IMPORTER || !HasEscortState(STATE_ESCORT_ESCORTING);
    }

    void UpdateEscortAI(const uint32 diff) override
    {
        if (m_turnInTimer)
        {
            if (m_turnInTimer <= diff)
            {
                m_turnInTimer = 0;
                End();
                m_creature->ForcedDespawn();
                return;
            }
            m_turnInTimer -= diff;
        }

        npc_escortAI::UpdateEscortAI(diff);
    }

    // Exact use of these texts remains unknown, it seems that he should only talk when he initiates the attack or he is the first who is attacked by a npc
    void Aggro(Unit* pWho) override
    {
        switch (urand(0, 6))                                // Not always said
        {
            case 0: DoScriptText(SAY_WILLIX_AGGRO_1, m_creature, pWho); break;
            case 1: DoScriptText(SAY_WILLIX_AGGRO_2, m_creature, pWho); break;
            case 2: DoScriptText(SAY_WILLIX_AGGRO_3, m_creature, pWho); break;
            case 3: DoScriptText(SAY_WILLIX_AGGRO_4, m_creature, pWho); break;
        }
    }

    void JustSummoned(Creature* pSummoned) override
    {
        pSummoned->AI()->AttackStart(m_creature);
    }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 3:
                DoScriptText(SAY_WILLIX_1, m_creature);
                break;
            case 7:
                DoScriptText(SAY_WILLIX_2, m_creature);
                break;
            case 10:
                DoScriptText(SAY_WILLIX_3, m_creature);
                break;
            case 15:
                DoScriptText(SAY_WILLIX_4, m_creature);
                // Summon 2 boars on the pathway
                m_creature->SummonCreature(NPC_RAGING_AGAMAR, aBoarSpawn[0][0], aBoarSpawn[0][1], aBoarSpawn[0][2], 0, TEMPSPAWN_TIMED_OOC_DESPAWN, 25000);
                m_creature->SummonCreature(NPC_RAGING_AGAMAR, aBoarSpawn[1][0], aBoarSpawn[1][1], aBoarSpawn[1][2], 0, TEMPSPAWN_TIMED_OOC_DESPAWN, 25000);
                break;
            case 26:
                DoScriptText(SAY_WILLIX_5, m_creature);
                break;
            case 34:
                DoScriptText(SAY_WILLIX_6, m_creature);
                break;
            case 45:
                DoScriptText(SAY_WILLIX_7, m_creature);
                // Summon 2 boars at the end
                m_creature->SummonCreature(NPC_RAGING_AGAMAR, aBoarSpawn[2][0], aBoarSpawn[2][1], aBoarSpawn[2][2], 0, TEMPSPAWN_TIMED_OOC_DESPAWN, 25000);
                m_creature->SummonCreature(NPC_RAGING_AGAMAR, aBoarSpawn[3][0], aBoarSpawn[3][1], aBoarSpawn[3][2], 0, TEMPSPAWN_TIMED_OOC_DESPAWN, 25000);
                break;
            case 46:
                if (m_turnInTimer)
                    break;
                DoScriptText(SAY_WILLIX_END, m_creature);
                m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                // Complete event
                if (Player* pPlayer = GetPlayerForEscort())
                    pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_WILLIX_THE_IMPORTER, m_creature);
                SetEscortPaused(true);
                QuestEnd();
                m_turnInTimer = 2 * MINUTE * IN_MILLISECONDS;
                break;
        }
    }
};

UnitAI* GetAI_npc_willix_the_importer(Creature* pCreature)
{
    return new npc_willix_the_importerAI(pCreature);
}

bool QuestAccept_npc_willix_the_importer(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_WILLIX_THE_IMPORTER)
    {
        if (npc_willix_the_importerAI* pEscortAI = dynamic_cast<npc_willix_the_importerAI*>(pCreature->AI()))
        {
            if (!pEscortAI->CanOfferQuest(QUEST_WILLIX_THE_IMPORTER))
                return true;

            // After 4.0.1 set run = true
            pEscortAI->Start(false, pPlayer, pQuest);
            if (!pEscortAI->HasEscortState(STATE_ESCORT_ESCORTING))
                return true;

            DoScriptText(SAY_WILLIX_READY, pCreature, pPlayer);
            pCreature->SetFactionTemporary(FACTION_ESCORT_N_NEUTRAL_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
        }
    }

    return true;
}

/*######
## npc_snufflenose_gopher
######*/

enum
{
    SPELL_SNUFFLENOSE_COMMAND   = 8283,
    NPC_SNUFFLENOSE_GOPHER      = 4781,
    GO_BLUELEAF_TUBBER          = 20920,
};

struct npc_snufflenose_gopherAI : public ScriptedPetAI
{
    npc_snufflenose_gopherAI(Creature* pCreature) : ScriptedPetAI(pCreature), m_movePointId(0) { Reset(); }

    bool m_bIsMovementActive;
    uint32 m_movePointId;
    uint32 m_searchTimer;
    ObjectGuid m_targetTubberGuid;
    ObjectGuid m_searchOwnerGuid;

    void Reset() override
    {
        m_bIsMovementActive = false;
        m_searchTimer = 0;
        m_targetTubberGuid.Clear();
        m_searchOwnerGuid.Clear();
    }

    Unit* GetSearchOwner() const
    {
        Unit* owner = m_creature->GetMaster();
        return owner && owner->IsAlive() && owner->IsInWorld() && owner->GetMap() == m_creature->GetMap() ? owner : nullptr;
    }

    GameObject* GetSearchTarget() const
    {
        Unit* owner = GetSearchOwner();
        if (!owner || owner->GetObjectGuid() != m_searchOwnerGuid)
            return nullptr;

        GameObject* tubber = m_creature->GetMap()->GetGameObject(m_targetTubberGuid);
        if (!tubber || tubber->GetEntry() != GO_BLUELEAF_TUBBER || tubber->IsSpawned() ||
            !tubber->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND) || !tubber->IsWithinDistInMap(m_creature, 40.0f))
            return nullptr;

        return tubber;
    }

    bool IsSearchingFor(ObjectGuid targetGuid) const
    {
        return m_bIsMovementActive && m_targetTubberGuid == targetGuid && m_searchTimer && GetSearchTarget() &&
            m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE;
    }

    void FinishSearch()
    {
        // Clear the claim before replacing movement: a finalized MovePoint may notify us again.
        Reset();
        m_creature->GetMotionMaster()->Clear(false);
        if (Unit* owner = GetSearchOwner())
            m_creature->GetMotionMaster()->MoveFollow(owner, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
        else
            m_creature->GetMotionMaster()->MoveIdle();
    }

    void MovementInform(uint32 uiMoveType, uint32 uiPointId) override
    {
        if (!m_bIsMovementActive || uiMoveType != POINT_MOTION_TYPE || uiPointId != m_movePointId)
            return;

        // Failed/partial paths can finish away from their requested destination.
        if (GameObject* tubber = GetSearchTarget())
        {
            if (tubber->IsWithinDistInMap(m_creature, INTERACTION_DISTANCE) && tubber->IsWithinLOSInMap(m_creature))
            {
                tubber->SetRespawnTime(5 * MINUTE);
                tubber->Refresh();
                tubber->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND);
            }
        }

        FinishSearch();
    }

    // Function to search for new tubber in range.
    void DoFindNewTubber()
    {
        if (m_bIsMovementActive || !GetSearchOwner())
            return;

        GameObjectList tubbers;
        GetGameObjectListWithEntryInGrid(tubbers, m_creature, GO_BLUELEAF_TUBBER, 40.0f);
        tubbers.sort(ObjectDistanceOrder(m_creature));

        for (GameObject* tubber : tubbers)
        {
            if (tubber->IsSpawned() || !tubber->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND) || !tubber->IsWithinLOSInMap(m_creature))
                continue;

            // Claims are local to live gophers on this map, so despawns leave no stale global reservation.
            // A valid search always stays within the same 40-yard target radius.
            CreatureList gophers;
            GetCreatureListWithEntryInGrid(gophers, tubber, NPC_SNUFFLENOSE_GOPHER, 40.0f);
            bool claimed = false;
            for (Creature* gopher : gophers)
            {
                if (gopher != m_creature && gopher->IsAlive())
                {
                    if (npc_snufflenose_gopherAI* ai = dynamic_cast<npc_snufflenose_gopherAI*>(gopher->AI()))
                        if (ai->IsSearchingFor(tubber->GetObjectGuid()))
                        {
                            claimed = true;
                            break;
                        }
                }
            }
            if (claimed)
                continue;

            m_targetTubberGuid = tubber->GetObjectGuid();
            m_searchOwnerGuid = GetSearchOwner()->GetObjectGuid();
            if (!++m_movePointId)
                ++m_movePointId;
            m_searchTimer = 30 * IN_MILLISECONDS;
            m_bIsMovementActive = true;

            float x, y, z;
            tubber->GetContactPoint(m_creature, x, y, z);
            m_creature->GetMotionMaster()->MovePoint(m_movePointId, x, y, z);
            return;
        }
    }

    void UpdateAI(const uint32 diff) override
    {
        if (m_bIsMovementActive)
        {
            if (!GetSearchTarget() || m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE || m_searchTimer <= diff)
                FinishSearch();
            else
                m_searchTimer -= diff;
        }

        if (!m_bIsMovementActive)
            ScriptedPetAI::UpdateAI(diff);
    }
};

UnitAI* GetAI_npc_snufflenose_gopher(Creature* pCreature)
{
    return new npc_snufflenose_gopherAI(pCreature);
}

// 8283 - Snufflenose Command
struct SnufflenoseCommand : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* target = spell->GetUnitTarget();
        if (!target || !spell->GetCaster() || target->GetMasterGuid() != spell->GetCaster()->GetObjectGuid())
            return;

        if (npc_snufflenose_gopherAI* gopherAI = dynamic_cast<npc_snufflenose_gopherAI*>(target->AI()))
            gopherAI->DoFindNewTubber();
    }
};

// 8555 - Left for Dead
struct LeftForDead : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* target = spell->GetUnitTarget();
        if (!target)
            return;

        target->CastSpell(nullptr, 8359, TRIGGERED_OLD_TRIGGERED);
    }
};

void AddSC_razorfen_kraul()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_willix_the_importer";
    pNewScript->GetAI = &GetAI_npc_willix_the_importer;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_willix_the_importer;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_snufflenose_gopher";
    pNewScript->GetAI = &GetAI_npc_snufflenose_gopher;
    pNewScript->RegisterSelf();

    RegisterSpellScript<SnufflenoseCommand>("spell_snufflenose_command");
    RegisterSpellScript<LeftForDead>("spell_left_for_dead");
}
