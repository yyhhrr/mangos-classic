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
SDName: Dustwallow_Marsh
SD%Complete: 95
SDComment: Quest support: 1173, 1222, 1270, 1273, 1324.
SDCategory: Dustwallow Marsh
EndScriptData

*/

/* ContentData
npc_morokk
npc_ogron
npc_private_hendel
npc_stinky_ignatz
at_nats_landing
EndContentData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/escort_ai.h"
#include "AI/ScriptDevAI/scripts/kalimdor/world_kalimdor.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include <unordered_map>
#include "AI/EventAI/CreatureEventAI.h"
#include <algorithm>
#include <iterator>
#include <map>

/*######
## npc_morokk
######*/

enum
{
    SAY_MOR_CHALLENGE       = -1000499,
    SAY_MOR_SCARED          = -1000500,

    NPC_MOROKK              = 4500,
    QUEST_CHALLENGE_MOROKK  = 1173,

    FACTION_MOR_HOSTILE     = 168,
    FACTION_MOR_RUNNING     = 35,
    FACTION_MOR_SPAWN       = 29
};

// Paused escorts do not run the native distance failure check. Keep the same
// group radius while also retiring abandoned task rounds.
static bool DustwallowQuestGroupInRange(Creature* actor, Player* player, uint32 quest, float radius)
{
    return player && player->CheckForGroup([&](Player const* member)
    {
        return member->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE &&
            actor->IsWithinDistInMap(member, radius);
    });
}

struct npc_morokkAI : public npc_escortAI
{
    enum Phase { IDLE, APPROACH, FIGHT, ESCAPING, ABORTED };

    explicit npc_morokkAI(Creature* creature) : npc_escortAI(creature),
        m_originalDeathPrevention(creature->IsPreventingDeath()) { Reset(); }

    Phase m_phase = IDLE;
    bool m_originalDeathPrevention;
    uint32 m_playerTimer = 1000;

    void Reset() override { SetDeathPrevention(m_phase == FIGHT || m_originalDeathPrevention); }

    bool CanStart() const
    {
        return m_phase == IDLE && m_creature->IsAlive() && !m_creature->IsInCombat() &&
            !HasEscortState(STATE_ESCORT_ESCORTING) &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    bool CanOfferQuest(uint32 quest) const override { return quest != QUEST_CHALLENGE_MOROKK || CanStart(); }

    bool TryStart(Player* player, Quest const* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_CHALLENGE_MOROKK || !player->IsAlive() ||
            player->GetMap() != m_creature->GetMap() || player->GetQuestStatus(QUEST_CHALLENGE_MOROKK) != QUEST_STATUS_INCOMPLETE)
            return false;
        if (HasEscortState(STATE_ESCORT_ESCORTING) && GetPlayerForEscort() == player && m_questForEscort == quest)
            return true;
        if (!CanStart())
            return false;
        Start(true, player, quest);
        return HasEscortState(STATE_ESCORT_ESCORTING) && GetPlayerForEscort() == player && m_questForEscort == quest;
    }

    void JustStartedEscort() override
    {
        m_phase = APPROACH;
        m_playerTimer = 1000;
        Reset();
    }

    void JustRespawned() override
    {
        SetEscortPaused(false);
        End();
        m_phase = IDLE;
        m_playerTimer = 1000;
        m_creature->setFaction(FACTION_MOR_SPAWN);
        npc_escortAI::JustRespawned();
    }

    void FailQuestForPlayerAndGroup() override
    {
        if (m_phase != ESCAPING && m_phase != ABORTED)
            npc_escortAI::FailQuestForPlayerAndGroup();
        m_phase = ABORTED;
        SetDeathPrevention(m_originalDeathPrevention);
        SetEscortPaused(false);
        End();
    }

    void JustReachedHome() override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING) && m_phase != ESCAPING)
        {
            FailQuestForPlayerAndGroup();
            m_creature->ForcedDespawn();
        }
    }

    void WaypointReached(uint32 point) override
    {
        if (point == 1 && m_phase == APPROACH && HasEscortState(STATE_ESCORT_ESCORTING))
            SetEscortPaused(true);
    }

    void Surrender()
    {
        if (m_phase != FIGHT || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        Player* player = GetPlayerForEscort();
        if (!player)
        {
            FailQuestForPlayerAndGroup();
            m_creature->ForcedDespawn();
            return;
        }
        m_phase = ESCAPING;
        player->RewardPlayerAndGroupAtEventExplored(QUEST_CHALLENGE_MOROKK, m_creature);
        QuestEnd();
        SetDeathPrevention(m_originalDeathPrevention);
        m_creature->setFaction(FACTION_MOR_RUNNING);
        m_creature->RemoveAllAurasOnEvade();
        m_creature->CombatStop(true);
        m_creature->SetImmuneToPlayer(true);
        SetEscortPaused(false);
        SetReactState(REACT_PASSIVE);
        DoScriptText(SAY_MOR_SCARED, m_creature);
        m_creature->GetMotionMaster()->Clear(false);
    }

    void DamageTaken(Unit*, uint32& damage, DamageEffectType, SpellEntry const*) override
    {
        if (m_phase == ESCAPING)
        {
            damage = 0;
            return;
        }
        if (m_phase != FIGHT || !damage || !m_creature->IsAlive())
            return;
        uint32 health = m_creature->GetHealth();
        if (damage >= health || uint64(health - damage) * 100 <= uint64(m_creature->GetMaxHealth()) * 30)
        {
            damage = std::min(damage, health - 1);
            Surrender();
        }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (m_phase == FIGHT && m_creature->IsAlive() &&
            uint64(m_creature->GetHealth()) * 100 <= uint64(m_creature->GetMaxHealth()) * 30)
            Surrender(); // Native protection also covers aggregated weapon lines.
        if (m_phase == FIGHT && HasEscortState(STATE_ESCORT_PAUSED))
        {
            if (m_playerTimer <= diff)
            {
                if (!DustwallowQuestGroupInRange(m_creature, GetPlayerForEscort(), QUEST_CHALLENGE_MOROKK, 70.0f))
                {
                    FailQuestForPlayerAndGroup();
                    m_creature->ForcedDespawn();
                    return;
                }
                m_playerTimer = 1000;
            }
            else
                m_playerTimer -= diff;
        }
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
        {
            if (m_phase == APPROACH && HasEscortState(STATE_ESCORT_PAUSED))
            {
                Player* player = GetPlayerForEscort();
                if (player && player->IsAlive() && player->IsInRange(m_creature, 0, 70))
                {
                    m_phase = FIGHT;
                    SetDeathPrevention(true);
                    DoScriptText(SAY_MOR_CHALLENGE, m_creature, player);
                    m_creature->SetImmuneToPlayer(false);
                    SetReactState(REACT_AGGRESSIVE);
                    m_creature->setFaction(FACTION_MOR_HOSTILE);
                    AttackStart(player);
                }
                else
                    SetEscortPaused(false);
            }
            return;
        }
        if (m_phase == FIGHT)
            DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_morokk(Creature* pCreature)
{
    return new npc_morokkAI(pCreature);
}

bool QuestAccept_npc_morokk(Player* player, Creature* creature, Quest const* quest)
{
    if (!player || !creature || !quest || quest->GetQuestId() != QUEST_CHALLENGE_MOROKK)
        return false;
    auto ai = dynamic_cast<npc_morokkAI*>(creature->AI());
    if (!ai || !ai->TryStart(player, quest))
    {
        if (player->GetQuestStatus(QUEST_CHALLENGE_MOROKK) == QUEST_STATUS_INCOMPLETE)
            player->FailQuest(QUEST_CHALLENGE_MOROKK);
        return false;
    }
    return true;
}

/*######
## npc_ogron
######*/

enum
{
    SAY_OGR_START                       = -1000452,
    SAY_OGR_SPOT                        = -1000453,
    SAY_OGR_RET_WHAT                    = -1000454,
    SAY_OGR_RET_SWEAR                   = -1000455,
    SAY_OGR_REPLY_RET                   = -1000456,
    SAY_OGR_RET_TAKEN                   = -1000457,
    SAY_OGR_TELL_FIRE                   = -1000458,
    SAY_OGR_RET_NOCLOSER                = -1000459,
    SAY_OGR_RET_NOFIRE                  = -1000460,
    SAY_OGR_RET_HEAR                    = -1000461,
    SAY_OGR_CAL_FOUND                   = -1000462,
    SAY_OGR_CAL_MERCY                   = -1000463,
    SAY_OGR_HALL_GLAD                   = -1000464,
    EMOTE_OGR_RET_ARROW                 = -1000465,
    SAY_OGR_RET_ARROW                   = -1000466,
    SAY_OGR_CAL_CLEANUP                 = -1000467,
    SAY_OGR_NODIE                       = -1000468,
    SAY_OGR_SURVIVE                     = -1000469,
    SAY_OGR_RET_LUCKY                   = -1000470,
    SAY_OGR_THANKS                      = -1000471,

    QUEST_QUESTIONING                   = 1273,

    FACTION_GENERIC_FRIENDLY            = 35,
    FACTION_THER_HOSTILE                = 151,

    NPC_REETHE                          = 4980,
    NPC_CALDWELL                        = 5046,
    NPC_HALLAN                          = 5045,
    NPC_SKIRMISHER                      = 5044,

    SPELL_FAKE_SHOT                     = 7105,

    PHASE_INTRO                         = 0,
    PHASE_GUESTS                        = 1,
    PHASE_FIGHT                         = 2,
    PHASE_COMPLETE                      = 3
};

static float m_afSpawn[] = { -3383.501953f, -3203.383301f, 36.149f};
static float m_afMoveTo[] = { -3371.414795f, -3212.179932f, 34.210f};

struct npc_ogronAI : public npc_escortAI
{
    explicit npc_ogronAI(Creature* creature) : npc_escortAI(creature) { Reset(); }

    ObjectGuid m_reetheGuid;
    ObjectGuid m_guests[4];
    bool m_defeated[4] = {};
    int32 m_spawnSlot = -1;
    uint32 m_uiPhase = PHASE_INTRO;
    uint32 m_uiPhaseCounter = 0;
    uint32 m_uiGlobalTimer = 5000;
    uint32 m_retryTimer = 0;
    uint32 m_spawnFailures = 0;
    uint32 m_playerTimer = 1000;
    bool m_cleaning = false;
    bool m_rewarded = false;

    void Reset() override { } // Evade is neither victory nor a lost wave.

    uint32 GuestEntry(uint32 slot) const
    {
        return slot == 0 ? NPC_CALDWELL : slot == 1 ? NPC_HALLAN : NPC_SKIRMISHER;
    }

    bool OwnedGuest(Creature const* actor, uint32 slot) const
    {
        return actor && actor->GetEntry() == GuestEntry(slot) &&
            actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    void CleanupGuests()
    {
        m_cleaning = true;
        for (uint32 i = 0; i < 4; ++i)
        {
            ObjectGuid guid = m_guests[i];
            m_guests[i].Clear();
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (OwnedGuest(actor, i) && actor->IsAlive())
                    actor->ForcedDespawn();
            m_defeated[i] = false;
        }
        m_cleaning = false;
        m_spawnSlot = -1;
        m_retryTimer = m_spawnFailures = 0;
    }

    void JustStartedEscort() override
    {
        CleanupGuests();
        m_reetheGuid.Clear();
        m_uiPhase = PHASE_INTRO;
        m_uiPhaseCounter = 0;
        m_uiGlobalTimer = 5000;
        m_playerTimer = 1000;
        m_rewarded = false;
    }

    void JustRespawned() override
    {
        CleanupGuests();
        SetEscortPaused(false);
        End();
        m_reetheGuid.Clear();
        m_uiPhase = PHASE_INTRO;
        m_uiPhaseCounter = 0;
        m_uiGlobalTimer = 5000;
        m_playerTimer = 1000;
        m_rewarded = false;
        npc_escortAI::JustRespawned();
    }

    void FailQuestForPlayerAndGroup() override
    {
        if (!m_rewarded)
            npc_escortAI::FailQuestForPlayerAndGroup();
        CleanupGuests();
        SetEscortPaused(false);
        End();
        m_reetheGuid.Clear();
        m_creature->ClearTemporaryFaction();
    }

    void AbortAndRespawn()
    {
        FailQuestForPlayerAndGroup();
        m_creature->SetDeathState(JUST_DIED);
        m_creature->Respawn();
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (HasEscortState(STATE_ESCORT_ESCORTING) && who && who->GetEntry() == NPC_REETHE &&
            who->GetTypeId() == TYPEID_UNIT && who->IsAlive() && !m_reetheGuid)
            m_reetheGuid = who->GetObjectGuid();
        npc_escortAI::MoveInLineOfSight(who);
    }

    Creature* GetCreature(uint32 entry)
    {
        if (entry == NPC_REETHE)
        {
            Creature* actor = m_creature->GetMap()->GetCreature(m_reetheGuid);
            if (!actor || actor->GetEntry() != NPC_REETHE || !actor->IsAlive())
            {
                actor = GetClosestCreatureWithEntry(m_creature, NPC_REETHE, 100.0f);
                m_reetheGuid = actor ? actor->GetObjectGuid() : ObjectGuid();
            }
            return actor;
        }
        for (uint32 i = 0; i < 4; ++i)
            if (GuestEntry(i) == entry)
                if (Creature* actor = m_creature->GetMap()->GetCreature(m_guests[i]))
                    if (OwnedGuest(actor, i) && actor->IsAlive())
                        return actor;
        return nullptr;
    }

    void WaypointReached(uint32 point) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        switch (point)
        {
            case 10:
                DoScriptText(SAY_OGR_SPOT, m_creature);
                break;
            case 11:
                if (Creature* reethe = GetCreature(NPC_REETHE))
                    DoScriptText(SAY_OGR_RET_WHAT, reethe);
                break;
            case 12:
                SetEscortPaused(true);
                break;
        }
    }

    void JustSummoned(Creature* actor) override
    {
        if (m_spawnSlot < 0 || m_spawnSlot >= 4 || !HasEscortState(STATE_ESCORT_ESCORTING) ||
            !OwnedGuest(actor, m_spawnSlot) || !actor->IsAlive() || m_guests[m_spawnSlot])
            return;
        m_guests[m_spawnSlot] = actor->GetObjectGuid();
        actor->setFaction(m_uiPhase == PHASE_FIGHT ? FACTION_THER_HOSTILE : FACTION_GENERIC_FRIENDLY);
        if (m_uiPhase == PHASE_FIGHT)
            actor->AI()->AttackStart(m_creature);
        else if (m_spawnSlot == 0)
            actor->GetMotionMaster()->MovePoint(0, m_afMoveTo[0], m_afMoveTo[1], m_afMoveTo[2]);
        else if (Creature* caldwell = GetCreature(NPC_CALDWELL))
            actor->GetMotionMaster()->MoveFollow(caldwell, 0.5f, (M_PI / 2) * int(m_spawnSlot + 2));
    }

    bool EnsureGuests()
    {
        bool failed = false;
        for (uint32 i = 0; i < 4; ++i)
        {
            if (m_defeated[i])
                continue;
            Creature* actor = m_creature->GetMap()->GetCreature(m_guests[i]);
            if (OwnedGuest(actor, i) && actor->IsAlive())
                continue;
            m_guests[i].Clear();
            m_spawnSlot = i;
            actor = m_creature->SummonCreature(GuestEntry(i), m_afSpawn[0], m_afSpawn[1], m_afSpawn[2],
                0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 300000);
            m_spawnSlot = -1;
            if (!OwnedGuest(actor, i) || !actor->IsAlive() || m_guests[i] != actor->GetObjectGuid())
            {
                if (OwnedGuest(actor, i) && actor->IsAlive())
                    actor->ForcedDespawn();
                m_guests[i].Clear();
                failed = true;
            }
        }
        if (failed)
        {
            if (++m_spawnFailures >= 3)
                AbortAndRespawn();
            else
                m_retryTimer = 1000;
            return false;
        }
        m_spawnFailures = m_retryTimer = 0;
        return true;
    }

    void CheckWaveComplete()
    {
        if (m_uiPhase == PHASE_FIGHT && m_defeated[0] && m_defeated[1] && m_defeated[2] && m_defeated[3])
        {
            m_uiPhase = PHASE_COMPLETE;
            m_uiPhaseCounter = 12;
            m_uiGlobalTimer = 5000;
        }
    }

    void SummonedCreatureJustDied(Creature* actor) override
    {
        if (m_cleaning || m_uiPhase != PHASE_FIGHT || !actor || actor->IsAlive())
            return;
        for (uint32 i = 0; i < 4; ++i)
            if (OwnedGuest(actor, i) && m_guests[i] == actor->GetObjectGuid())
                m_defeated[i] = true;
        CheckWaveComplete();
    }

    void SummonedCreatureDespawn(Creature* actor) override
    {
        if (m_cleaning || !actor || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        for (uint32 i = 0; i < 4; ++i)
            if (!m_defeated[i] && OwnedGuest(actor, i) && m_guests[i] == actor->GetObjectGuid())
            {
                m_guests[i].Clear();
                m_retryTimer = 1000;
            }
    }

    void DoStartAttackMe()
    {
        for (uint32 i = 0; i < 4; ++i)
            if (!m_defeated[i])
                if (Creature* actor = m_creature->GetMap()->GetCreature(m_guests[i]))
                    if (OwnedGuest(actor, i) && actor->IsAlive() && !actor->GetBeneficiaryPlayer())
                    {
                        actor->setFaction(FACTION_THER_HOSTILE);
                        if (!actor->IsInCombat())
                            actor->AI()->AttackStart(m_creature);
                    }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (HasEscortState(STATE_ESCORT_PAUSED) && !m_rewarded)
        {
            if (m_playerTimer <= diff)
            {
                if (!DustwallowQuestGroupInRange(m_creature, GetPlayerForEscort(), QUEST_QUESTIONING, 66.0f))
                {
                    AbortAndRespawn();
                    return;
                }
                m_playerTimer = 1000;
            }
            else
                m_playerTimer -= diff;
        }
        if (m_retryTimer && HasEscortState(STATE_ESCORT_ESCORTING))
        {
            if (m_retryTimer <= diff)
                EnsureGuests();
            else
                m_retryTimer -= diff;
            if (!HasEscortState(STATE_ESCORT_ESCORTING))
                return;
        }
        if (m_uiPhase == PHASE_FIGHT)
        {
            for (uint32 i = 0; i < 4; ++i)
                if (!m_defeated[i])
                {
                    Creature* actor = m_creature->GetMap()->GetCreature(m_guests[i]);
                    if (OwnedGuest(actor, i) && !actor->IsAlive())
                        m_defeated[i] = true;
                    else if (!OwnedGuest(actor, i))
                    {
                        m_guests[i].Clear();
                        if (!m_retryTimer)
                            m_retryTimer = 1000;
                    }
                }
            CheckWaveComplete();
            if (m_uiPhase == PHASE_FIGHT && m_playerTimer == 1000)
                DoStartAttackMe();
        }
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
        {
            if (HasEscortState(STATE_ESCORT_PAUSED))
            {
                if (m_uiGlobalTimer <= diff)
                {
                    m_uiGlobalTimer = 5000;
                    if (m_uiPhase == PHASE_GUESTS && !EnsureGuests())
                        return;
                    switch (m_uiPhase)
                    {
                        case PHASE_INTRO:
                        {
                            switch (m_uiPhaseCounter)
                            {
                                case 0:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_SWEAR, pReethe);
                                    break;
                                case 1:
                                    DoScriptText(SAY_OGR_REPLY_RET, m_creature);
                                    break;
                                case 2:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_TAKEN, pReethe);
                                    break;
                                case 3:
                                    DoScriptText(SAY_OGR_TELL_FIRE, m_creature);
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_NOCLOSER, pReethe);
                                    break;
                                case 4:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_NOFIRE, pReethe);
                                    break;
                                case 5:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_HEAR, pReethe);

                                    if (!EnsureGuests())
                                    {
                                        m_uiGlobalTimer = 1000;
                                        return;
                                    }

                                    m_uiPhase = PHASE_GUESTS;
                                    break;
                            }
                            break;
                        }
                        case PHASE_GUESTS:
                        {
                            switch (m_uiPhaseCounter)
                            {
                                case 6:
                                    if (Creature* pCaldwell = GetCreature(NPC_CALDWELL))
                                        DoScriptText(SAY_OGR_CAL_FOUND, pCaldwell);
                                    break;
                                case 7:
                                    if (Creature* pCaldwell = GetCreature(NPC_CALDWELL))
                                        DoScriptText(SAY_OGR_CAL_MERCY, pCaldwell);
                                    break;
                                case 8:
                                    if (Creature* pHallan = GetCreature(NPC_HALLAN))
                                    {
                                        DoScriptText(SAY_OGR_HALL_GLAD, pHallan);

                                        if (Creature* pReethe = GetCreature(NPC_REETHE))
                                            pHallan->CastSpell(pReethe, SPELL_FAKE_SHOT, TRIGGERED_NONE);
                                    }
                                    break;
                                case 9:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                    {
                                        DoScriptText(EMOTE_OGR_RET_ARROW, pReethe);
                                        DoScriptText(SAY_OGR_RET_ARROW, pReethe);
                                    }
                                    break;
                                case 10:
                                    if (Creature* pCaldwell = GetCreature(NPC_CALDWELL))
                                        DoScriptText(SAY_OGR_CAL_CLEANUP, pCaldwell);

                                    DoScriptText(SAY_OGR_NODIE, m_creature);
                                    break;
                                case 11:
                                    m_uiPhase = PHASE_FIGHT;
                                    DoStartAttackMe();
                                    break;
                            }
                            break;
                        }
                        case PHASE_COMPLETE:
                        {
                            switch (m_uiPhaseCounter)
                            {
                                case 12:
                                    if (!m_rewarded)
                                    {
                                        m_rewarded = true;
                                        if (Player* pPlayer = GetPlayerForEscort())
                                            pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_QUESTIONING, m_creature);
                                        QuestEnd();
                                    }

                                    DoScriptText(SAY_OGR_SURVIVE, m_creature);
                                    break;
                                case 13:
                                    if (Creature* pReethe = GetCreature(NPC_REETHE))
                                        DoScriptText(SAY_OGR_RET_LUCKY, pReethe);
                                    break;
                                case 14:
                                    DoScriptText(SAY_OGR_THANKS, m_creature);
                                    SetRun();
                                    SetEscortPaused(false);
                                    break;
                            }
                            break;
                        }
                    }

                    if (m_uiPhase != PHASE_FIGHT)
                        ++m_uiPhaseCounter;
                }
                else
                    m_uiGlobalTimer -= diff;
            }
            return;
        }
        DoMeleeAttackIfReady();
    }
};

bool QuestAccept_npc_ogron(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_QUESTIONING)
    {
        if (npc_ogronAI* pEscortAI = dynamic_cast<npc_ogronAI*>(pCreature->AI()))
        {
            pEscortAI->Start(false, pPlayer, pQuest, true);
            pCreature->SetFactionTemporary(FACTION_ESCORT_N_FRIEND_PASSIVE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_NPC);
            DoScriptText(SAY_OGR_START, pCreature, pPlayer);
        }
    }

    return true;
}

UnitAI* GetAI_npc_ogron(Creature* pCreature)
{
    return new npc_ogronAI(pCreature);
}

/*######
## npc_private_hendel
######*/

enum
{
    EMOTE_SURRENDER             = -1000415,

    QUEST_MISSING_DIPLO_PT16    = 1324,
    FACTION_HOSTILE             = 168,                      // guessed, may be different

    NPC_SENTRY                  = 5184,                     // helps hendel
    NPC_JAINA                   = 4968,                     // appears once hendel gives up
    NPC_TERVOSH                 = 4967,
    NPC_PAINED                  = 4965,

    SPELL_TELEPORT_VISUAL       = 12980,
    SPELL_TELEPORT              = 7079
};

struct OutroSpawnLocation
{
    float fX, fY, fZ, fO;
    float fDestX, fDestY, fDestZ;
    uint32 uiEntry;
};

const OutroSpawnLocation lOutroSpawns[] =
{
    {
        -2857.604492f, -3354.784912f, 35.369640f, 3.16604f,
        -2881.546631f, -3346.477539f, 34.143719f,
        NPC_TERVOSH
    },
    {
        -2858.120117f, -3358.469971f, 36.086300f, 3.16604f,
        -2879.697998f, -3347.789063f, 34.772892f,
        NPC_JAINA
    },
    {
        -2857.379883f, -3351.370117f, 34.178001f, 3.16604f,
        -2879.959961f, -3344.469971f, 34.670502f,
        NPC_PAINED
    }
};

const float fSentryFleePoint[] {-2917.56f, -3329.90f, 30.37f};

struct npc_private_hendelAI : public ScriptedAI
{
    enum Phase { IDLE, FIGHT, OUTRO, COMPLETE, ABORTED };

    explicit npc_private_hendelAI(Creature* creature) : ScriptedAI(creature),
        m_originalDeathPrevention(creature->IsPreventingDeath()) { Reset(); }

    Phase m_phase = IDLE;
    bool m_originalDeathPrevention;
    ObjectGuid m_playerGuid;
    ObjectGuid m_tervoshGuid;
    GuidSet m_sentries;
    GuidSet m_outroActors;
    uint32 m_expectedOutroEntry = 0;
    uint32 m_spawnFailures = 0;
    uint32 m_retryTimer = 0;
    uint32 m_playerTimer = 1000;
    uint32 m_rendTimer = 20000;
    uint32 m_outroTimer = 0;
    bool m_cleaningActors = false;
    bool m_outroFailed = false;

    bool OwnedOutro(Creature const* actor) const
    {
        return actor && actor->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
            (actor->GetEntry() == NPC_TERVOSH || actor->GetEntry() == NPC_JAINA || actor->GetEntry() == NPC_PAINED);
    }

    void CleanupOutro(bool despawnAlive)
    {
        m_cleaningActors = true;
        GuidSet actors;
        actors.swap(m_outroActors);
        m_tervoshGuid.Clear();
        m_expectedOutroEntry = 0;
        for (ObjectGuid guid : actors)
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (despawnAlive && OwnedOutro(actor) && actor->IsAlive())
                    actor->ForcedDespawn();
        m_cleaningActors = false;
    }

    void RestoreSentries()
    {
        for (ObjectGuid guid : m_sentries)
            if (Creature* sentry = m_creature->GetMap()->GetCreature(guid))
                if (sentry->GetEntry() == NPC_SENTRY && sentry->IsAlive())
                    sentry->AI()->EnterEvadeMode();
        m_sentries.clear();
    }

    void Reset() override
    {
        SetDeathPrevention(m_phase == FIGHT || m_originalDeathPrevention);
        // EnterEvadeMode is part of a successful surrender. Preserve its round
        // and actors; ordinary fight evades instead retire the admitted round.
        if (m_phase == FIGHT)
            m_outroFailed = true;
    }

    bool CanStart() const
    {
        return m_phase == IDLE && m_creature->IsAlive() && !m_creature->IsInCombat() &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    bool CanOfferQuest(uint32 quest) const override { return quest != QUEST_MISSING_DIPLO_PT16 || CanStart(); }

    bool IsParticipant(Player* player) const
    {
        Player* owner = m_creature->GetMap()->GetPlayer(m_playerGuid);
        return owner && (owner == player || (owner->GetGroup() && owner->GetGroup() == player->GetGroup()));
    }

    bool TryStart(Player* player)
    {
        if (!player || !player->IsAlive() || player->GetMap() != m_creature->GetMap() ||
            player->GetQuestStatus(QUEST_MISSING_DIPLO_PT16) != QUEST_STATUS_INCOMPLETE)
            return false;
        if ((m_phase == FIGHT || m_phase == OUTRO) && IsParticipant(player))
            return true;
        if (!CanStart())
            return false;
        CleanupOutro(true);
        m_playerGuid = player->GetObjectGuid();
        m_phase = FIGHT;
        m_outroFailed = false;
        m_spawnFailures = m_retryTimer = m_outroTimer = 0;
        m_playerTimer = 1000;
        m_rendTimer = 20000;
        SetDeathPrevention(true);
        SetReactState(REACT_AGGRESSIVE);
        m_creature->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_COMBAT_STOP | TEMPFACTION_RESTORE_RESPAWN);
        AttackStart(player);
        CreatureList sentries;
        GetCreatureListWithEntryInGrid(sentries, m_creature, NPC_SENTRY, 40.0f);
        for (Creature* sentry : sentries)
            if (sentry->IsAlive())
            {
                m_sentries.insert(sentry->GetObjectGuid());
                sentry->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_COMBAT_STOP | TEMPFACTION_RESTORE_RESPAWN);
                sentry->AI()->AttackStart(player);
            }
        return true;
    }

    void AbortRound()
    {
        if (m_phase == COMPLETE || m_phase == ABORTED)
            return;
        m_phase = ABORTED;
        if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
            player->FailQuestForGroup(QUEST_MISSING_DIPLO_PT16);
        SetDeathPrevention(m_originalDeathPrevention);
        CleanupOutro(true);
        RestoreSentries();
        m_playerGuid.Clear();
        m_retryTimer = m_outroTimer = 0;
        m_creature->ClearTemporaryFaction();
        m_creature->ForcedDespawn();
    }

    void JustRespawned() override
    {
        CleanupOutro(m_phase != COMPLETE);
        RestoreSentries();
        m_playerGuid.Clear();
        m_phase = IDLE;
        m_outroFailed = false;
        m_spawnFailures = m_retryTimer = m_outroTimer = 0;
        m_playerTimer = 1000;
        SetDeathPrevention(m_originalDeathPrevention);
        ScriptedAI::JustRespawned();
    }

    void JustDied(Unit*) override
    {
        if (m_phase == COMPLETE)
        {
            // DB496701 intentionally removes Hendel at15s while the summoned
            // actors keep speaking until124s. Do not cut that scene short.
            CleanupOutro(false);
            m_sentries.clear();
            m_playerGuid.Clear();
            SetDeathPrevention(m_originalDeathPrevention);
        }
        else if (m_phase != IDLE && m_phase != ABORTED)
            AbortRound();
    }

    void AttackedBy(Unit* attacker) override
    {
        if (m_phase != OUTRO && m_phase != COMPLETE && m_phase != ABORTED &&
            !m_creature->GetVictim() && attacker && m_creature->CanAttackNow(attacker))
            AttackStart(attacker);
    }

    void JustSummoned(Creature* actor) override
    {
        if (m_phase != OUTRO || !m_expectedOutroEntry || !OwnedOutro(actor) ||
            actor->GetEntry() != m_expectedOutroEntry || !actor->IsAlive())
            return;
        m_outroActors.insert(actor->GetObjectGuid());
        if (actor->GetEntry() == NPC_TERVOSH)
            m_tervoshGuid = actor->GetObjectGuid();
    }

    void SummonedCreatureDespawn(Creature* actor) override
    {
        if (m_cleaningActors || !OwnedOutro(actor))
            return;
        m_outroActors.erase(actor->GetObjectGuid());
        if (m_phase == OUTRO && actor->GetObjectGuid() == m_tervoshGuid)
            m_outroFailed = true;
    }

    void SummonedCreatureJustDied(Creature* actor) override
    {
        if (m_phase == OUTRO && OwnedOutro(actor) && actor->GetObjectGuid() == m_tervoshGuid)
            m_outroFailed = true;
    }

    void SpawnOutro()
    {
        CleanupOutro(true);
        bool complete = true;
        for (OutroSpawnLocation const& spawn : lOutroSpawns)
        {
            m_expectedOutroEntry = spawn.uiEntry;
            Creature* actor = m_creature->SummonCreature(spawn.uiEntry, spawn.fX, spawn.fY, spawn.fZ, spawn.fO,
                TEMPSPAWN_TIMED_DESPAWN, 3 * MINUTE * IN_MILLISECONDS, false, true);
            m_expectedOutroEntry = 0;
            if (!OwnedOutro(actor) || !actor->IsAlive() || !m_outroActors.count(actor->GetObjectGuid()))
            {
                if (OwnedOutro(actor) && actor->IsAlive())
                    actor->ForcedDespawn();
                complete = false;
                continue;
            }
            actor->CastSpell(actor, SPELL_TELEPORT_VISUAL, TRIGGERED_NONE);
            actor->GetMotionMaster()->MovePoint(0, spawn.fDestX, spawn.fDestY, spawn.fDestZ);
        }
        Creature* tervosh = m_creature->GetMap()->GetCreature(m_tervoshGuid);
        if (!complete || m_outroActors.size() != 3 || !OwnedOutro(tervosh) || !tervosh->IsAlive())
        {
            CleanupOutro(true);
            if (++m_spawnFailures >= 3)
                m_outroFailed = true;
            else
                m_retryTimer = 1000;
            return;
        }
        m_spawnFailures = m_retryTimer = 0;
        m_outroTimer = 190000;
        tervosh->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_GOSSIP);
        tervosh->GetMotionMaster()->MoveWaypoint(0); // Keep the existing DB496701 speech and7079 credit.
    }

    void Surrender()
    {
        if (m_phase != FIGHT || m_outroFailed)
            return;
        m_phase = OUTRO;
        SetDeathPrevention(m_originalDeathPrevention);
        DoScriptText(EMOTE_SURRENDER, m_creature);
        EnterEvadeMode();
        SetReactState(REACT_PASSIVE);
        for (ObjectGuid guid : m_sentries)
            if (Creature* sentry = m_creature->GetMap()->GetCreature(guid))
                if (sentry->GetEntry() == NPC_SENTRY && sentry->IsAlive())
                {
                    sentry->RemoveAllAurasOnEvade();
                    sentry->CombatStop(true);
                    sentry->SetWalk(false);
                    sentry->GetMotionMaster()->MovePoint(0, fSentryFleePoint[0], fSentryFleePoint[1], fSentryFleePoint[2]);
                    sentry->ForcedDespawn(4000);
                }
        SpawnOutro();
    }

    void SpellHit(Unit* caster, SpellEntry const* spell) override
    {
        if (m_phase != OUTRO || m_outroFailed || !m_outroTimer || !spell || spell->Id != SPELL_TELEPORT || !caster ||
            caster->GetTypeId() != TYPEID_UNIT || caster->GetObjectGuid() != m_tervoshGuid ||
            !OwnedOutro(static_cast<Creature*>(caster)) || !caster->IsAlive() || !caster->IsInWorld() ||
            caster->GetMap() != m_creature->GetMap() || m_creature->GetMap()->GetCreature(m_tervoshGuid) != caster)
            return;
        Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid);
        if (!player)
        {
            m_outroFailed = true;
            return;
        }
        m_phase = COMPLETE; // Set before group-credit callbacks; accept this7079 only once.
        player->RewardPlayerAndGroupAtEventExplored(QUEST_MISSING_DIPLO_PT16, m_creature);
        CleanupOutro(false); // Existing DB actor timers own the successful remainder.
        m_sentries.clear();
        m_retryTimer = m_outroTimer = 0;
    }

    void DamageTaken(Unit*, uint32& damage, DamageEffectType, SpellEntry const*) override
    {
        if (m_phase == OUTRO || m_phase == COMPLETE)
        {
            damage = 0;
            return;
        }
        if (m_phase != FIGHT || !damage || !m_creature->IsAlive())
            return;
        uint32 health = m_creature->GetHealth();
        if (damage >= health || uint64(health - damage) * 100 <= uint64(m_creature->GetMaxHealth()) * 20)
        {
            damage = std::min(damage, health - 1);
            Surrender();
        }
    }

    void UpdateAI(uint32 diff) override
    {
        if ((m_phase == FIGHT || m_phase == OUTRO) && m_outroFailed)
        {
            AbortRound();
            return;
        }
        if (m_phase == FIGHT && m_creature->IsAlive() &&
            uint64(m_creature->GetHealth()) * 100 <= uint64(m_creature->GetMaxHealth()) * 20)
            Surrender();
        if (m_phase == FIGHT || m_phase == OUTRO)
        {
            if (m_playerTimer <= diff)
            {
                if (!DustwallowQuestGroupInRange(m_creature, m_creature->GetMap()->GetPlayer(m_playerGuid), QUEST_MISSING_DIPLO_PT16, 70.0f))
                {
                    AbortRound();
                    return;
                }
                m_playerTimer = 1000;
            }
            else
                m_playerTimer -= diff;
        }
        if (m_phase == OUTRO)
        {
            if (m_retryTimer)
            {
                if (m_retryTimer <= diff)
                    SpawnOutro();
                else
                    m_retryTimer -= diff;
            }
            if (m_outroTimer)
            {
                if (m_outroTimer <= diff)
                    AbortRound();
                else
                    m_outroTimer -= diff;
            }
            return;
        }
        if (m_phase == IDLE)
        {
            ScriptedAI::UpdateAI(diff);
            return;
        }
        if (m_phase != FIGHT || !m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;
        if (m_creature->GetMapId() == 1 && m_creature->GetDbGuid() == 31274 &&
            !m_creature->GetBeneficiaryPlayer() && CanExecuteCombatAction())
        {
            if (m_rendTimer <= diff)
            {
                Unit* victim = m_creature->GetVictim();
                if (victim->IsAlive() && m_creature->CanReachWithMeleeAttack(victim) &&
                    DoCastSpellIfCan(victim, 11977) == CAST_OK)
                    m_rendTimer = 20000;
                else
                    m_rendTimer = 1000;
            }
            else
                m_rendTimer -= diff;
        }
        DoMeleeAttackIfReady();
    }
};

bool QuestAccept_npc_private_hendel(Player* player, Creature* creature, Quest const* quest)
{
    if (!player || !creature || !quest)
        return false;
    if (quest->GetQuestId() == QUEST_MISSING_DIPLO_PT16)
    {
        auto ai = dynamic_cast<npc_private_hendelAI*>(creature->AI());
        if (!ai || !ai->TryStart(player))
        {
            if (player->GetQuestStatus(QUEST_MISSING_DIPLO_PT16) == QUEST_STATUS_INCOMPLETE)
                player->FailQuest(QUEST_MISSING_DIPLO_PT16);
            return false;
        }
    }
    return true;
}

UnitAI* GetAI_npc_private_hendel(Creature* pCreature)
{
    return new npc_private_hendelAI(pCreature);
}

/*#####
## npc_stinky_ignatz
## TODO Note: Faction change is guessed
#####*/

enum
{
    QUEST_ID_STINKYS_ESCAPE_ALLIANCE    = 1222,
    QUEST_ID_STINKYS_ESCAPE_HORDE       = 1270,

    SAY_STINKY_BEGIN                    = -1000958,
    SAY_STINKY_FIRST_STOP               = -1000959,
    SAY_STINKY_SECOND_STOP              = -1001141,
    SAY_STINKY_THIRD_STOP_1             = -1001142,
    SAY_STINKY_THIRD_STOP_2             = -1001143,
    SAY_STINKY_THIRD_STOP_3             = -1001144,
    SAY_STINKY_PLANT_GATHERED           = -1001145,
    SAY_STINKY_END                      = -1000962,
    SAY_STINKY_END_EMOTE                = -1010032,
    SAY_STINKY_AGGRO_1                  = -1000960,
    SAY_STINKY_AGGRO_2                  = -1000961,
    SAY_STINKY_AGGRO_3                  = -1001146,
    SAY_STINKY_AGGRO_4                  = -1001147,

    GO_BOGBEAN_PLANT                    = 20939,
};

struct npc_stinky_ignatzAI : public npc_escortAI
{
    npc_stinky_ignatzAI(Creature* pCreature) : npc_escortAI(pCreature)
    {
        SetReactState(REACT_DEFENSIVE);
        Reset();
    }

    ObjectGuid m_bogbeanPlantGuid;

    void Reset() override {}

    void Aggro(Unit* pWho) override
    {
        switch (urand(0, 3))
        {
            case 0: DoScriptText(SAY_STINKY_AGGRO_1, m_creature); break;
            case 1: DoScriptText(SAY_STINKY_AGGRO_2, m_creature); break;
            case 2: DoScriptText(SAY_STINKY_AGGRO_3, m_creature); break;
            case 3: DoScriptText(SAY_STINKY_AGGRO_4, m_creature, pWho); break;
        }
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*pSender*/, Unit* pInvoker, uint32 uiMiscValue) override
    {
        if (eventType == AI_EVENT_START_ESCORT && pInvoker->GetTypeId() == TYPEID_PLAYER)
        {
            DoScriptText(SAY_STINKY_BEGIN, m_creature);
            Start(false, (Player*)pInvoker, GetQuestTemplateStore(uiMiscValue));
            m_creature->SetFactionTemporary(FACTION_ESCORT_N_NEUTRAL_PASSIVE, TEMPFACTION_RESTORE_RESPAWN);
            m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        }
    }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 6:
                DoScriptText(SAY_STINKY_FIRST_STOP, m_creature);
                break;
            case 11:
                DoScriptText(SAY_STINKY_SECOND_STOP, m_creature);
                break;
            case 25:
                DoScriptText(SAY_STINKY_THIRD_STOP_1, m_creature);
                break;
            case 26:
                DoScriptText(SAY_STINKY_THIRD_STOP_2, m_creature);
                if (GameObject* pBogbeanPlant = GetClosestGameObjectWithEntry(m_creature, GO_BOGBEAN_PLANT, DEFAULT_VISIBILITY_DISTANCE))
                {
                    m_bogbeanPlantGuid = pBogbeanPlant->GetObjectGuid();
                    m_creature->SetFacingToObject(pBogbeanPlant);
                }
                break;
            case 27:
                if (Player* pPlayer = GetPlayerForEscort())
                    DoScriptText(SAY_STINKY_THIRD_STOP_3, m_creature, pPlayer);
                break;
            case 30:
                m_creature->HandleEmote(EMOTE_STATE_USESTANDING);
                break;
            case 31:
                DoScriptText(SAY_STINKY_PLANT_GATHERED, m_creature);
                break;
            case 40:
                if (Player* pPlayer = GetPlayerForEscort())
                {
                    pPlayer->RewardPlayerAndGroupAtEventExplored(pPlayer->GetTeam() == ALLIANCE ? QUEST_ID_STINKYS_ESCAPE_ALLIANCE : QUEST_ID_STINKYS_ESCAPE_HORDE, m_creature);
                    DoScriptText(SAY_STINKY_END, m_creature, pPlayer);
                }
                break;
            case 41:
                DoScriptText(SAY_STINKY_END_EMOTE, m_creature);
                break;
        }
    }

    void WaypointStart(uint32 uiPointId) override
    {
        if (uiPointId == 31)
        {
            if (GameObject* pBogbeanPlant = m_creature->GetMap()->GetGameObject(m_bogbeanPlantGuid))
                pBogbeanPlant->Use(m_creature);
            m_bogbeanPlantGuid.Clear();
            m_creature->HandleEmote(EMOTE_ONESHOT_NONE);
        }
    }
};

UnitAI* GetAI_npc_stinky_ignatz(Creature* pCreature)
{
    return new npc_stinky_ignatzAI(pCreature);
}

bool QuestAccept_npc_stinky_ignatz(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_ID_STINKYS_ESCAPE_ALLIANCE || pQuest->GetQuestId() == QUEST_ID_STINKYS_ESCAPE_HORDE)
        pCreature->AI()->SendAIEvent(AI_EVENT_START_ESCORT, pPlayer, pCreature, pQuest->GetQuestId());

    return true;
}

/*######
## at_sentry_point
######*/

enum SentryPoint
{
    QUEST_MISSING_DIPLO_PT14    = 1265,
    SPELL_TELEPORT_VISUAL_2     = 799,  // TODO Find the correct spell
    NPC_SENTRY_POINT_GUARD      = 5085
};

bool AreaTrigger_at_sentry_point(Player* pPlayer, const AreaTriggerEntry* /*pAt*/)
{
    QuestStatus quest_status = pPlayer->GetQuestStatus(QUEST_MISSING_DIPLO_PT14);
    if (pPlayer->IsDead() || quest_status == QUEST_STATUS_NONE || quest_status == QUEST_STATUS_COMPLETE)
        return false;

    if (!GetClosestCreatureWithEntry(pPlayer, NPC_TERVOSH, 100.0f))
    {
        if (Creature* pTervosh = pPlayer->SummonCreature(NPC_TERVOSH, -3476.51f, -4105.94f, 17.1f, 5.3816f, TEMPSPAWN_TIMED_DESPAWN, 60000))
        {
            pTervosh->CastSpell(pTervosh, SPELL_TELEPORT_VISUAL_2, TRIGGERED_OLD_TRIGGERED);

            if (Creature* pGuard = GetClosestCreatureWithEntry(pTervosh, NPC_SENTRY_POINT_GUARD, 15.0f))
            {
                pGuard->SetFacingToObject(pTervosh);
                pGuard->HandleEmote(EMOTE_ONESHOT_SALUTE);
            }
        }
    }

    return true;
};

/*######
## npc_theramore_combatant
######*/

enum
{
    NPC_GUARD_NARRISHA = 5093,
    NPC_GUARD_KAHIL = 5091,
    NPC_GUARD_EDWARD = 4922,
    NPC_GUARD_TARK = 5094,
    NPC_GUARD_LANA = 5092,
    NPC_GUARD_JARAD = 4923,

    NPC_MEDIC_HELAINA = 5200, //Red healer
    NPC_MEDIC_TAMBERLYN = 5199, //Blue healer

    NPC_MASTER_SZIGETI = 5090,
    NPC_MASTER_CRITON = 4924,

    NPC_BLUE_CAPTAIN = 5096,
    NPC_RED_CAPTAIN = 5095,

    SPELL_FIRST_AID = 7162,

    BLUE_TEAM = 1621,
    RED_TEAM = 1622,

    FACTION_THERAMORE_FRIENDLY = 151,

    SAY_RED_WINNER = 1794, //Szigeti
    SAY_BLUE_WINNER = 1795, //Szigeti
    SAY_RED_WINNER_OVERALL = 1773, //Criton
    SAY_BLUE_WINNER_OVERALL = 1774, //Criton

    SAY_BACK_TO_CORNERS = 1636, //Criton

    SAY_FOR_THE_RED = 1778,
    SAY_FOR_THE_BLUE = 1777,

    SAY_FIRST_COMBATANTS = 1771,
    SAY_FIGHT_INTRO = 1643,
    SAY_FIGHT_START = 1641,

    SAY_EDWARD_LOSS = 1760,
    SAY_KAHIL_LOSS = 1762,
    SAY_NARRISHA_LOSS = 1763,

    SAY_JARAD_LOSS = 1766,
    SAY_LANA_LOSS = 1767,
    SAY_TARK_LOSS = 1768,

    SAY_RED_MOTIVATION_WIN = 1769,
    SAY_RED_MOTIVATION_LOSS = 1770,

    SAY_BLUE_MOTIVATION_LOSS = 1765,
    SAY_BLUE_MOTIVATION_WIN = 1764,

    SAY_RED_FIRST = 1776,
    SAY_BLUE_FIRST = 1775,
};

enum CombatantActions
{
    COMBATANT_CHECK_HP,
    COMBATANT_BLUE_WIN,
    COMBATANT_RED_WIN,
    COMBATANT_START_POSITION,
    COMBATANT_START_SPAR,
    COMBATANT_GET_ALL,
    COMBATANT_SALUTE,
    COMBATANT_SWITCH,
    COMBATANT_SEPARATE,
    COMBATANT_INTRO,
    COMBATANT_MOTIVATION,
    COMBATANT_ACTION_MAX,
};

static std::unordered_map<uint32, const Position> startingPositions ({
    {BLUE_TEAM, {-3643.574707f, -4505.717773f, 9.462863f, 3.855592f}},
    {RED_TEAM, {-3652.405273f, -4514.126953f, 9.462863f, 0.717925f}}
});

static const uint32 theramore_blue_team[] = {NPC_GUARD_EDWARD, NPC_GUARD_KAHIL, NPC_GUARD_NARRISHA};
static const uint32 theramore_red_team[] = {NPC_GUARD_JARAD, NPC_GUARD_LANA, NPC_GUARD_TARK};

struct npc_theramore_spar_controller : public CombatAI
{
    npc_theramore_spar_controller(Creature* creature) : CombatAI(creature, COMBATANT_ACTION_MAX)
    {
        AddCustomAction(COMBATANT_CHECK_HP, true, [&](){ CheckCombatantHP(); });
        AddCustomAction(COMBATANT_BLUE_WIN, true, [&](){ HandleBlueWin(); });
        AddCustomAction(COMBATANT_RED_WIN, true, [&](){ HandleRedWin(); });
        AddCustomAction(COMBATANT_START_SPAR, true, [&](){ StartSparring(); });
        AddCustomAction(COMBATANT_START_POSITION, 10000u, [&](){ MoveIntoPosition(); });
        AddCustomAction(COMBATANT_SALUTE, true, [&](){ CombatantSalute(); });
        AddCustomAction(COMBATANT_INTRO, true, [&](){ CombatantIntro(); });
        AddCustomAction(COMBATANT_SWITCH, true, [&](){ SwitchMembers(); });
        AddCustomAction(COMBATANT_SEPARATE, true, [&](){ CombatantSeparate(); });
        AddCustomAction(COMBATANT_GET_ALL, 2000u, [&](){ GetCombatants(); });
        AddCustomAction(COMBATANT_MOTIVATION, true, [&](){ HandleMotivation(); });
    }

    std::unordered_map<uint32, GuidList> teams;
    std::unordered_map<uint32, ObjectGuid> medics;
    std::unordered_map<uint32, ObjectGuid> active;

    uint32 m_uiWinningTeam = 0;
    bool m_roundActive = false;
    bool m_recovering = false;
    uint32 m_readyWait = 0;
    struct SavedState { uint32 faction; ReactStates reaction; };
    std::map<ObjectGuid, SavedState> m_savedStates;

    bool IsApprovedController() const
    {
        return m_creature->GetMapId() == 1 && m_creature->GetEntry() == NPC_MASTER_SZIGETI &&
            m_creature->GetDbGuid() == 30659;
    }

    Creature* GetSceneActor(uint32 entry) const
    {
        if (!IsApprovedController())
            return nullptr;
        uint32 guid = 0;
        switch (entry)
        {
            case NPC_GUARD_EDWARD: guid = 30668; break;
            case NPC_GUARD_JARAD: guid = 30669; break;
            case NPC_GUARD_KAHIL: guid = 30663; break;
            case NPC_GUARD_LANA: guid = 30670; break;
            case NPC_GUARD_NARRISHA: guid = 30658; break;
            case NPC_GUARD_TARK: guid = 30664; break;
            case NPC_MEDIC_TAMBERLYN: guid = 30665; break;
            case NPC_MEDIC_HELAINA: guid = 30662; break;
            case NPC_MASTER_CRITON: guid = 30660; break;
            case NPC_RED_CAPTAIN: guid = 30661; break;
            case NPC_BLUE_CAPTAIN: guid = 30667; break;
            case NPC_MASTER_SZIGETI: return m_creature;
            default: return nullptr;
        }
        Creature* actor = GetClosestCreatureWithEntry(m_creature, entry, 40.0f);
        if (!actor || actor->GetEntry() != entry || actor->GetMapId() != 1 || actor->GetDbGuid() != guid)
            return nullptr;
        return actor;
    }

    bool Available(Creature const* actor) const
    {
        return actor && actor->IsAlive() && !actor->GetBeneficiaryPlayer();
    }

    ObjectGuid ActiveGuid(uint32 team) const
    {
        auto it = active.find(team);
        return it == active.end() ? ObjectGuid() : it->second;
    }

    Creature* ActiveActor(uint32 team) const
    {
        ObjectGuid guid = ActiveGuid(team);
        return guid ? m_creature->GetMap()->GetCreature(guid) : nullptr;
    }

    bool TeamEmpty(uint32 team) const
    {
        auto it = teams.find(team);
        return it != teams.end() && it->second.empty();
    }

    void RestoreParticipants()
    {
        GuidSet participants;
        for (auto const& state : m_savedStates)
            participants.insert(state.first);
        for (auto it = m_savedStates.begin(); it != m_savedStates.end(); )
        {
            Creature* actor = m_creature->GetMap()->GetCreature(it->first);
            if (!actor || actor->GetBeneficiaryPlayer())
            {
                ++it; // Restore after the actor returns or player control ends.
                continue;
            }
            Unit* victim = actor->GetVictim();
            if (victim && participants.find(victim->GetObjectGuid()) != participants.end())
                actor->CombatStop(true);
            actor->ClearTemporaryFaction();
            actor->setFaction(it->second.faction);
            actor->AI()->SetReactState(it->second.reaction);
            if (actor->IsAlive() && !actor->IsInCombat())
            {
                Position home = actor->GetRespawnPosition();
                actor->GetMotionMaster()->Clear(true, true);
                actor->GetMotionMaster()->MovePoint(0, home, FORCED_MOVEMENT_RUN);
            }
            it = m_savedStates.erase(it);
        }
    }

    void RecoverRound()
    {
        for (uint32 action = 0; action < COMBATANT_ACTION_MAX; ++action)
            DisableTimer(action);
        RestoreParticipants();
        active.clear(); teams.clear(); medics.clear();
        m_uiWinningTeam = 0;
        m_roundActive = false;
        m_recovering = true;
        if (IsApprovedController())
            ResetTimer(COMBATANT_GET_ALL, 2000u);
    }

    bool RosterAvailable() const
    {
        for (uint32 entry : {NPC_GUARD_EDWARD, NPC_GUARD_JARAD, NPC_GUARD_KAHIL, NPC_GUARD_LANA,
            NPC_GUARD_NARRISHA, NPC_GUARD_TARK, NPC_MEDIC_TAMBERLYN, NPC_MEDIC_HELAINA,
            NPC_MASTER_CRITON, NPC_RED_CAPTAIN, NPC_BLUE_CAPTAIN})
            if (!Available(GetSceneActor(entry)))
                return false;
        return true;
    }

    bool ReadyAtStart(uint32 team) const
    {
        Creature* actor = ActiveActor(team);
        auto const& pos = startingPositions.at(team);
        return Available(actor) && actor->IsStopped() && actor->GetDistance(pos.x, pos.y, pos.z) < 2.0f;
    }

    bool ValidateActive()
    {
        if (!m_roundActive || !RosterAvailable() || !Available(ActiveActor(RED_TEAM)) ||
            !Available(ActiveActor(BLUE_TEAM)))
        {
            RecoverRound();
            return false;
        }
        return true;
    }


    void GetCombatants()
    {
        RestoreParticipants();
        teams.clear(); active.clear(); medics.clear();
        if (!RosterAvailable())
        {
            RecoverRound();
            return;
        }
        for (uint32 entry : {NPC_GUARD_EDWARD, NPC_GUARD_JARAD, NPC_GUARD_KAHIL, NPC_GUARD_LANA,
            NPC_GUARD_NARRISHA, NPC_GUARD_TARK, NPC_MEDIC_TAMBERLYN, NPC_MEDIC_HELAINA,
            NPC_MASTER_CRITON, NPC_RED_CAPTAIN, NPC_BLUE_CAPTAIN})
            if (GetSceneActor(entry)->IsInCombat())
            {
                RecoverRound();
                return;
            }
        teams.emplace(RED_TEAM, GuidList());
        teams.emplace(BLUE_TEAM, GuidList());
        for (uint32 team : {RED_TEAM, BLUE_TEAM})
        {
            auto& queue = teams.find(team)->second;
            auto members = team == RED_TEAM ? theramore_red_team : theramore_blue_team;
            for (uint32 i = 0; i < 3; ++i)
            {
                Creature* actor = GetSceneActor(members[i]);
                m_savedStates.emplace(actor->GetObjectGuid(), SavedState{actor->GetFaction(), actor->AI()->GetReactState()});
                actor->setFaction(FACTION_THERAMORE_FRIENDLY);
                actor->AI()->SetReactState(REACT_PASSIVE);
                queue.push_back(actor->GetObjectGuid());
            }
        }
        medics.emplace(RED_TEAM, GetSceneActor(NPC_MEDIC_HELAINA)->GetObjectGuid());
        medics.emplace(BLUE_TEAM, GetSceneActor(NPC_MEDIC_TAMBERLYN)->GetObjectGuid());
        m_roundActive = true;
        m_readyWait = 0;
        if (m_recovering)
            ResetTimer(COMBATANT_START_POSITION, 20000u);
        m_recovering = false;
    }

    void CheckCombatantHP()
    {
        if (!ValidateActive())
            return;
        Creature* red = ActiveActor(RED_TEAM);
        Creature* blue = ActiveActor(BLUE_TEAM);
        if (!red->IsInCombat() || !blue->IsInCombat() || red->GetVictim() != blue || blue->GetVictim() != red)
        {
            RecoverRound();
            return;
        }
        float redHealth = red->GetHealthPercent(), blueHealth = blue->GetHealthPercent();
        if (redHealth <= 30 || blueHealth <= 30)
        {
            ResetTimer(redHealth < blueHealth ? COMBATANT_BLUE_WIN : COMBATANT_RED_WIN, 100u);
            red->RestoreOriginalFaction(); blue->RestoreOriginalFaction();
            red->CombatStop(true); blue->CombatStop(true);
            for (uint32 team : {RED_TEAM, BLUE_TEAM})
            {
                auto it = medics.find(team);
                Creature* medic = it == medics.end() ? nullptr : m_creature->GetMap()->GetCreature(it->second);
                if (Available(medic))
                    medic->AI()->DoCastSpellIfCan(ActiveActor(team), SPELL_FIRST_AID);
            }
            DisableTimer(COMBATANT_CHECK_HP);
            return;
        }
        ResetTimer(COMBATANT_CHECK_HP, 1000u);
    }

    void HandleBlueWin()
    {
        if (!ValidateActive())
            return;
        Unit* szigeti = GetSceneActor(NPC_MASTER_SZIGETI);
        Creature* redCaptain = GetSceneActor(NPC_RED_CAPTAIN);
        if (szigeti && szigeti->IsAlive())
        {
            szigeti->HandleEmote(EMOTE_ONESHOT_POINT);
            DoBroadcastText(SAY_BLUE_WINNER, szigeti);
        }
        for (uint32 member : theramore_blue_team)
        {
            Unit* combatant = GetClosestCreatureWithEntry(m_creature, member, 20.f);
            if (combatant && combatant->GetEntry() != ActiveGuid(BLUE_TEAM).GetEntry() && combatant->IsAlive())
            {
                combatant->HandleEmote(EMOTE_ONESHOT_CHEER);
            }
        }
        if (redCaptain && redCaptain->IsAlive())
        {
            switch (ActiveGuid(RED_TEAM).GetEntry())
            {
                case NPC_GUARD_JARAD:
                    DoBroadcastText(SAY_JARAD_LOSS, redCaptain);
                    break;
                case NPC_GUARD_LANA:
                    DoBroadcastText(SAY_LANA_LOSS, redCaptain);
                    break;
                case NPC_GUARD_TARK:
                    DoBroadcastText(SAY_TARK_LOSS, redCaptain);
                    break;
                default:
                    break;
            }
        }
        HandleWin(BLUE_TEAM);
        m_uiWinningTeam = BLUE_TEAM;
    }

    void HandleRedWin()
    {
        if (!ValidateActive())
            return;
        Unit* szigeti = GetSceneActor(NPC_MASTER_SZIGETI);
        Creature* blueCaptain = GetSceneActor(NPC_BLUE_CAPTAIN);
        if (szigeti && szigeti->IsAlive())
        {
            szigeti->HandleEmote(EMOTE_ONESHOT_POINT);
            DoBroadcastText(SAY_RED_WINNER, szigeti);
        }
        for (uint32 member : theramore_red_team)
        {
            Unit* combatant = GetClosestCreatureWithEntry(m_creature, member, 20.f);
            if (combatant && combatant->GetEntry() != ActiveGuid(RED_TEAM).GetEntry() && combatant->IsAlive())
            {
                combatant->HandleEmote(EMOTE_ONESHOT_CHEER);
            }
        }
        if (blueCaptain && blueCaptain->IsAlive())
        {
            switch (ActiveGuid(BLUE_TEAM).GetEntry())
            {
                case NPC_GUARD_EDWARD:
                    DoBroadcastText(SAY_EDWARD_LOSS, blueCaptain);
                    break;
                case NPC_GUARD_KAHIL:
                    DoBroadcastText(SAY_KAHIL_LOSS, blueCaptain);
                    break;
                case NPC_GUARD_NARRISHA:
                    DoBroadcastText(SAY_NARRISHA_LOSS, blueCaptain);
                    break;
                default:
                    break;
            }
        }
        HandleWin(RED_TEAM);
        m_uiWinningTeam = RED_TEAM;
    }

    void HandleMotivation()
    {
        if (!m_roundActive || !RosterAvailable() || (m_uiWinningTeam != BLUE_TEAM && m_uiWinningTeam != RED_TEAM))
        {
            RecoverRound();
            return;
        }
        uint32 winningTeam = m_uiWinningTeam;
        Creature* redCaptain = GetSceneActor(NPC_RED_CAPTAIN);
        Creature* blueCaptain = GetSceneActor(NPC_BLUE_CAPTAIN);

        if (!redCaptain || !blueCaptain || !redCaptain->IsAlive() || !blueCaptain->IsAlive())
            return;

        switch (winningTeam)
        {
            case RED_TEAM:
                DoBroadcastText(SAY_RED_MOTIVATION_WIN, redCaptain);
                DoBroadcastText(SAY_BLUE_MOTIVATION_LOSS, blueCaptain);
                break;
            case BLUE_TEAM:
                DoBroadcastText(SAY_BLUE_MOTIVATION_WIN, blueCaptain);
                DoBroadcastText(SAY_RED_MOTIVATION_LOSS, redCaptain);
                break;
            default:
                break;
        }

    }

    void HandleWin(uint32 winningTeam)
    {
        if (!ValidateActive())
            return;
        Unit* criton = GetSceneActor(NPC_MASTER_CRITON);
        uint32 losingTeam = (winningTeam == BLUE_TEAM ? RED_TEAM : BLUE_TEAM);

        if (criton && criton->IsAlive())
        {
            DoBroadcastText(SAY_BACK_TO_CORNERS, criton);
            if (TeamEmpty(losingTeam))
            {
                DoBroadcastText(winningTeam == BLUE_TEAM ? SAY_BLUE_WINNER_OVERALL : SAY_RED_WINNER_OVERALL, criton);
                ResetTimer(COMBATANT_MOTIVATION, 8000u);
            }
        }
        ResetTimer(COMBATANT_SEPARATE, 100u);
    }

    void CombatantSalute()
    {
        if (!ValidateActive())
            return;
        if (!ReadyAtStart(RED_TEAM) || !ReadyAtStart(BLUE_TEAM))
        {
            if (++m_readyWait >= 15)
                RecoverRound();
            else
                ResetTimer(COMBATANT_SALUTE, 1000u);
            return;
        }
        m_readyWait = 0;
        Creature* redMember = dynamic_cast<Creature*>(m_creature->GetMap()->GetUnit(ActiveGuid(RED_TEAM)));
        Creature* blueMember = dynamic_cast<Creature*>(m_creature->GetMap()->GetUnit(ActiveGuid(BLUE_TEAM)));
        if (redMember && blueMember && redMember->IsAlive() && blueMember->IsAlive())
        {
            redMember->HandleEmote(EMOTE_ONESHOT_SALUTE);
            DoBroadcastText(SAY_FOR_THE_RED, redMember);
            blueMember->HandleEmote(EMOTE_ONESHOT_SALUTE);
            DoBroadcastText(SAY_FOR_THE_BLUE, blueMember);
        }
        ResetTimer(COMBATANT_INTRO, 3000u);
    }

    void CombatantIntro()
    {
        if (!ValidateActive())
            return;
        Unit* criton = GetSceneActor(NPC_MASTER_CRITON);
        if (criton && criton->IsAlive())
        {
            DoBroadcastText(SAY_FIGHT_INTRO, criton);
        }
        ResetTimer(COMBATANT_START_SPAR, 3000u);
    }

    void SwitchMembers()
    {
        if (!ValidateActive() || (m_uiWinningTeam != BLUE_TEAM && m_uiWinningTeam != RED_TEAM))
            return;
        uint32 winningTeam = m_uiWinningTeam;
        uint32 losingTeam = winningTeam == BLUE_TEAM ? RED_TEAM : BLUE_TEAM;
        Creature* loser = ActiveActor(losingTeam);
        Creature* winner = ActiveActor(winningTeam);
        if (loser->IsInCombat() || winner->IsInCombat())
        {
            RecoverRound();
            return;
        }
        Position home = loser->GetRespawnPosition();
        loser->AI()->SetReactState(REACT_PASSIVE);
        loser->GetMotionMaster()->Clear(true, true);
        loser->GetMotionMaster()->MovePoint(0, home, FORCED_MOVEMENT_RUN);
        winner->GetMotionMaster()->MovePoint(1, startingPositions.at(winningTeam), FORCED_MOVEMENT_WALK);
        auto next = teams.find(losingTeam);
        if (next == teams.end())
        {
            RecoverRound();
            return;
        }
        if (!next->second.empty())
        {
            active.insert_or_assign(losingTeam, next->second.front());
            next->second.pop_front();
            ResetTimer(COMBATANT_START_POSITION, 5000u);
        }
        else
        {
            GetCombatants();
            if (m_roundActive)
                ResetTimer(COMBATANT_START_POSITION, 20000u);
        }
    }

    void CombatantSeparate()
    {
        if (!ValidateActive())
            return;
        Creature* redMember = dynamic_cast<Creature*>(m_creature->GetMap()->GetUnit(ActiveGuid(RED_TEAM)));
        Creature* blueMember = dynamic_cast<Creature*>(m_creature->GetMap()->GetUnit(ActiveGuid(BLUE_TEAM)));

        if (redMember && blueMember)
        {
            redMember->GetMotionMaster()->Clear(true, true);
            redMember->GetMotionMaster()->MovePoint(1, startingPositions.at(RED_TEAM), FORCED_MOVEMENT_RUN);
            redMember->AI()->SetReactState(REACT_PASSIVE);
            blueMember->GetMotionMaster()->Clear(true, true);
            blueMember->GetMotionMaster()->MovePoint(1, startingPositions.at(BLUE_TEAM), FORCED_MOVEMENT_RUN);
            blueMember->AI()->SetReactState(REACT_PASSIVE);
        }
        ResetTimer(COMBATANT_SWITCH, 5000u);
    }

    void StartSparring()
    {
        if (!ValidateActive())
            return;
        Creature* red = ActiveActor(RED_TEAM);
        Creature* blue = ActiveActor(BLUE_TEAM);
        if (red->IsInCombat() || blue->IsInCombat())
        {
            RecoverRound();
            return;
        }
        if (!ReadyAtStart(RED_TEAM) || !ReadyAtStart(BLUE_TEAM) || red->GetHealthPercent() < 100.0f || blue->GetHealthPercent() < 100.0f)
        {
            for (uint32 team : {RED_TEAM, BLUE_TEAM})
            {
                Creature* actor = ActiveActor(team);
                auto it = medics.find(team);
                Creature* medic = it == medics.end() ? nullptr : m_creature->GetMap()->GetCreature(it->second);
                if (actor->GetHealthPercent() < 100.0f && Available(medic) && !medic->IsInCombat())
                    medic->AI()->DoCastSpellIfCan(actor, SPELL_FIRST_AID);
            }
            if (++m_readyWait >= 15)
                RecoverRound();
            else
                ResetTimer(COMBATANT_START_SPAR, 1000u);
            return;
        }
        DoBroadcastText(SAY_FIGHT_START, GetSceneActor(NPC_MASTER_CRITON));
        red->SetFactionTemporary(RED_TEAM, TEMPFACTION_RESTORE_COMBAT_STOP);
        blue->SetFactionTemporary(BLUE_TEAM, TEMPFACTION_RESTORE_COMBAT_STOP);
        red->AI()->SetReactState(REACT_DEFENSIVE); blue->AI()->SetReactState(REACT_DEFENSIVE);
        red->AI()->AttackStart(blue); blue->AI()->AttackStart(red);
        if (!red->IsInCombat() || !blue->IsInCombat() || red->GetVictim() != blue || blue->GetVictim() != red)
        {
            RecoverRound();
            return;
        }
        ResetTimer(COMBATANT_CHECK_HP, 1000u);
    }

    void MoveIntoPosition()
    {
        if (!m_roundActive || !RosterAvailable())
        {
            RecoverRound();
            return;
        }
        if (active.empty())
        {
            auto red = teams.find(RED_TEAM), blue = teams.find(BLUE_TEAM);
            if (red == teams.end() || blue == teams.end() || red->second.empty() || blue->second.empty())
            {
                RecoverRound();
                return;
            }
            active.emplace(RED_TEAM, red->second.front()); red->second.pop_front();
            active.emplace(BLUE_TEAM, blue->second.front()); blue->second.pop_front();
            DoBroadcastText(SAY_FIRST_COMBATANTS, GetSceneActor(NPC_MASTER_CRITON));
            DoBroadcastText(SAY_RED_FIRST, GetSceneActor(NPC_RED_CAPTAIN));
            DoBroadcastText(SAY_BLUE_FIRST, GetSceneActor(NPC_BLUE_CAPTAIN));
        }
        if (!ValidateActive())
            return;
        for (uint32 team : {RED_TEAM, BLUE_TEAM})
        {
            Creature* actor = ActiveActor(team);
            if (actor->IsInCombat())
            {
                RecoverRound();
                return;
            }
            actor->AI()->SetReactState(REACT_PASSIVE);
            actor->GetMotionMaster()->Clear(true, true);
            actor->GetMotionMaster()->MovePoint(1, startingPositions.at(team), FORCED_MOVEMENT_WALK);
        }
        m_readyWait = 0;
        ResetTimer(COMBATANT_SALUTE, 5000u);
    }
};

UnitAI* GetAI_npc_theramore_spar_controller(Creature* creature)
{
    if (creature->GetEntry() == NPC_MASTER_SZIGETI && creature->GetMapId() == 1 && creature->GetDbGuid() == 30659)
        return new npc_theramore_spar_controller(creature);
    return nullptr;
}

// Scoped ordinary combat additions. Never bind an entry-only negative-GUID event.
struct DustwallowCombatSettings
{
    uint32 spell, initialMin, initialMax, repeatMin, repeatMax;
    bool meleeOnly, oncePerCombat;
    uint32 hpMin, hpMax, chance;
    bool manaTargetOnly;
};

static bool GetDustwallowCombatSettings(Creature const* creature, DustwallowCombatSettings& settings)
{
    if (creature->GetMapId() != 1)
        return false;
    uint32 guid = creature->GetDbGuid();
    switch (creature->GetEntry())
    {
        case 4323:
        {
            static const uint32 guids[] = {30876, 30905, 31155, 31346, 31358, 31382, 31395, 31401, 31409, 31413, 31417, 31419, 31421, 31424, 31428, 31431, 31436, 31443, 31491, 31494, 31495, 31496, 31503, 31504, 31505, 31519, 31529, 33692, 33757, 33774, 33775, 33878, 33990, 34010, 34014};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {11985, 2400, 2700, 9400, 9700, false, false, 0, 100, 100, false};
                return true;
            }
            break;
        }
        case 4339:
        {
            static const uint32 guids[] = {31613};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {27641, 0, 0, 0, 0, false, true, 30, 60, 100, false};
                return true;
            }
            break;
        }
        case 4371:
        {
            static const uint32 guids[] = {25815, 25895, 28823, 30849, 30861, 30868};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {15785, 3000, 6000, 30000, 30000, false, false, 0, 100, 50, true};
                return true;
            }
            break;
        }
        case 5057:
        {
            static const uint32 guids[] = {8454, 8455, 23508, 30442, 30443, 30444, 30610, 30611, 30703, 30715, 30741, 30742, 30756, 30757, 30769, 30770, 30771, 30772, 30773, 30774, 30776, 30935, 31036, 31038, 31307, 31592, 31593, 31594, 31595, 31596, 31601, 33792, 33793, 33798, 33829, 33842, 33850};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {14873, 2100, 3200, 15100, 15900, true, false, 0, 100, 100, false};
                return true;
            }
            break;
        }
    }
    return false;
}

struct npc_dustwallow_combatAI : public CreatureEventAI
{
    npc_dustwallow_combatAI(Creature* creature, DustwallowCombatSettings const& settings) :
        CreatureEventAI(creature), m_settings(settings) { ResetExtraCombat(); }

    DustwallowCombatSettings m_settings;
    uint32 m_extraTimer = 0;
    bool m_extraDone = false;
    bool m_rollSucceeded = false;

    void ResetExtraCombat()
    {
        m_extraTimer = urand(m_settings.initialMin, m_settings.initialMax);
        m_extraDone = m_rollSucceeded = false;
    }

    void Reset() override
    {
        CreatureEventAI::Reset();
        ResetExtraCombat();
    }

    void EnterCombat(Unit* enemy) override
    {
        CreatureEventAI::EnterCombat(enemy);
        ResetExtraCombat();
    }

    void UpdateExtraCombat(uint32 diff)
    {
        DustwallowCombatSettings current;
        if (!GetDustwallowCombatSettings(m_creature, current) || current.spell != m_settings.spell ||
            !m_creature->IsAlive() || m_creature->GetBeneficiaryPlayer() || !m_creature->IsInCombat() ||
            !CanExecuteCombatAction() || (m_settings.oncePerCombat && m_extraDone))
            return;
        Unit* victim = m_creature->GetVictim();
        if (!victim || !victim->IsAlive() || !m_creature->CanAttack(victim))
            return;
        if (m_extraTimer > diff)
        {
            m_extraTimer -= diff;
            return;
        }
        float hp = m_creature->GetHealthPercent();
        if (hp < m_settings.hpMin || hp > m_settings.hpMax ||
            (m_settings.meleeOnly && !m_creature->CanReachWithMeleeAttack(victim)) ||
            (m_settings.manaTargetOnly && (victim->GetPowerType() != POWER_MANA || !victim->GetPower(POWER_MANA))))
        {
            m_extraTimer = 1000;
            return;
        }
        if (m_settings.chance != 100 && !m_rollSucceeded)
        {
            if (urand(1, 100) > m_settings.chance)
            {
                // A failed probability roll consumes this scheduled opportunity;
                // an ordinary cast failure below retries the admitted roll.
                m_extraTimer = urand(m_settings.repeatMin, m_settings.repeatMax);
                return;
            }
            m_rollSucceeded = true;
        }
        if (DoCastSpellIfCan(victim, m_settings.spell) == CAST_OK)
        {
            m_extraDone = true;
            m_rollSucceeded = false;
            m_extraTimer = urand(m_settings.repeatMin, m_settings.repeatMax);
        }
        else
            m_extraTimer = 1000;
    }

    void UpdateAI(uint32 diff) override
    {
        UpdateExtraCombat(diff);
        CreatureEventAI::UpdateAI(diff);
    }
};

UnitAI* GetAI_npc_dustwallow_combat(Creature* creature)
{
    DustwallowCombatSettings settings;
    if (GetDustwallowCombatSettings(creature, settings) && !creature->GetBeneficiaryPlayer())
        return new npc_dustwallow_combatAI(creature, settings);
    return nullptr; // Continue the native AIName/permit selection off scope.
}

void AddSC_dustwallow_marsh()
{
    Script* pNewScript;

    pNewScript = new Script;
    pNewScript->Name = "npc_morokk";
    pNewScript->GetAI = &GetAI_npc_morokk;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_morokk;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_ogron";
    pNewScript->GetAI = &GetAI_npc_ogron;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_ogron;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_private_hendel";
    pNewScript->GetAI = &GetAI_npc_private_hendel;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_private_hendel;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_stinky_ignatz";
    pNewScript->GetAI = &GetAI_npc_stinky_ignatz;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_stinky_ignatz;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_sentry_point";
    pNewScript->pAreaTrigger = &AreaTrigger_at_sentry_point;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_theramore_spar_controller";
    pNewScript->GetAI = &GetAI_npc_theramore_spar_controller;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_dustwallow_combat";
    pNewScript->GetAI = &GetAI_npc_dustwallow_combat;
    pNewScript->RegisterSelf();
}
