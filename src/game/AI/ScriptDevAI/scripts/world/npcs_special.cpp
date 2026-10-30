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
SDName: Npcs_Special
SD%Complete: 100
SDComment: To be used for special NPCs that are located globally.
SDCategory: NPCs
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include <map>
#include <algorithm>
#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Globals/ObjectMgr.h"
#include "GameEvents/GameEventMgr.h"
#include "Entities/TemporarySpawn.h"
#include "Spells/EngineeringTargetDummy.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "World/WorldState.h"
#include "AI/ScriptDevAI/base/TimerAI.h"

/* ContentData
npc_chicken_cluck       100%    support for quest 3861 (Cluck!)
npc_guardian            100%    guardianAI used to prevent players from accessing off-limits areas. Not in use by SD2
npc_garments_of_quests   80%    NPC's related to all Garments of-quests 5621, 5624, 5625, 5648, 5650
npc_injured_patient     100%    patients for triage-quests (6622 and 6624)
npc_doctor              100%    Gustaf Vanhowzen and Gregory Victor, quest 6622 and 6624 (Triage)
npc_innkeeper            25%    ScriptName not assigned. Innkeepers in general.
npc_redemption_target   100%    Used for the paladin quests: 1779,1781,9600,9685
npc_advanced_target_dummy
npc_training_dummy       Custom permanent training targets (not engineering summons)
EndContentData */

/*########
# npc_chicken_cluck
#########*/

enum
{
    EMOTE_CLUCK_TEXT1       = -1000204,
    EMOTE_CLUCK_TEXT2       = -1000205,

    QUEST_CLUCK             = 3861,
    FACTION_FRIENDLY        = 35,
    FACTION_CHICKEN         = 31
};

struct npc_chicken_cluckAI : public ScriptedAI
{
    npc_chicken_cluckAI(Creature* pCreature) : ScriptedAI(pCreature) {Reset();}

    uint32 m_uiResetFlagTimer;

    void Reset() override
    {
        m_uiResetFlagTimer = 20000;

        m_creature->setFaction(FACTION_CHICKEN);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    void ReceiveEmote(Player* pPlayer, uint32 uiEmote) override
    {
        if (uiEmote == TEXTEMOTE_CHICKEN && !urand(0, 49))
        {
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->setFaction(FACTION_FRIENDLY);
            DoScriptText(EMOTE_CLUCK_TEXT1, m_creature);
        }
        else if (uiEmote == TEXTEMOTE_CHEER && pPlayer->GetQuestStatus(QUEST_CLUCK) == QUEST_STATUS_COMPLETE)
        {
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->setFaction(FACTION_FRIENDLY);
            DoScriptText(EMOTE_CLUCK_TEXT2, m_creature);
        }
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // Reset flags after a certain time has passed so that the next player has to start the 'event' again
        if (m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER))
        {
            if (m_uiResetFlagTimer < uiDiff)
                EnterEvadeMode();
            else
                m_uiResetFlagTimer -= uiDiff;
        }

        if (m_creature->SelectHostileTarget() && m_creature->GetVictim())
            DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_chicken_cluck(Creature* pCreature)
{
    return new npc_chicken_cluckAI(pCreature);
}

/*######
## Triage quest
######*/

enum
{
    SAY_DOC1                    = -1000201,
    SAY_DOC2                    = -1000202,
    SAY_DOC3                    = -1000203,

    QUEST_TRIAGE_H              = 6622,
    QUEST_TRIAGE_A              = 6624,

    DOCTOR_ALLIANCE             = 12939,
    DOCTOR_HORDE                = 12920,
    ALLIANCE_COORDS             = 7,
    HORDE_COORDS                = 6
};

static Position AllianceCoords[] =
{
    { -3757.38f, -4533.05f, 14.16f, 3.62f},                 // Top-far-right bunk as seen from entrance
    { -3754.36f, -4539.13f, 14.16f, 5.13f},                 // Top-far-left bunk
    { -3749.54f, -4540.25f, 14.28f, 3.34f},                 // Far-right bunk
    { -3742.10f, -4536.85f, 14.28f, 3.64f},                 // Right bunk near entrance
    { -3755.89f, -4529.07f, 14.05f, 0.57f},                 // Far-left bunk
    { -3749.51f, -4527.08f, 14.07f, 5.26f},                 // Mid-left bunk
    { -3746.37f, -4525.35f, 14.16f, 5.22f},                 // Left bunk near entrance
};

// alliance run to where
#define A_RUNTOX -3742.96f
#define A_RUNTOY -4531.52f
#define A_RUNTOZ 11.91f

static Position HordeCoords[] =
{
    { -1013.75f, -3492.59f, 62.62f, 4.34f},                 // Left, Behind
    { -1017.72f, -3490.92f, 62.62f, 4.34f},                 // Right, Behind
    { -1015.77f, -3497.15f, 62.82f, 4.34f},                 // Left, Mid
    { -1019.51f, -3495.49f, 62.82f, 4.34f},                 // Right, Mid
    { -1017.25f, -3500.85f, 62.98f, 4.34f},                 // Left, front
    { -1020.95f, -3499.21f, 62.98f, 4.34f}                  // Right, Front
};

// horde run to where
#define H_RUNTOX -1016.44f
#define H_RUNTOY -3508.48f
#define H_RUNTOZ 62.96f

const uint32 AllianceSoldierId[3] =
{
    12938,                                                  // 12938 Injured Alliance Soldier
    12936,                                                  // 12936 Badly injured Alliance Soldier
    12937                                                   // 12937 Critically injured Alliance Soldier
};

const uint32 HordeSoldierId[3] =
{
    12923,                                                  // 12923 Injured Soldier
    12924,                                                  // 12924 Badly injured Soldier
    12925                                                   // 12925 Critically injured Soldier
};

/*######
## npc_doctor (handles both Gustaf Vanhowzen and Gregory Victor)
######*/

struct npc_doctorAI : public ScriptedAI
{
    explicit npc_doctorAI(Creature* creature) : ScriptedAI(creature) { Reset(); }

    ObjectGuid m_playerGuid;
    uint32 m_round = 0;
    uint32 m_uiSummonPatientTimer = 10000;
    uint32 m_uiSummonPatientCount = 0;
    uint32 m_uiPatientDiedCount = 0;
    uint32 m_uiPatientSavedCount = 0;
    bool m_bIsEventInProgress = false;
    GuidList m_lPatientGuids;
    std::map<ObjectGuid, Position*> m_pendingPatients;
    std::vector<Position*> m_vPatientSummonCoordinates;

    uint32 EventQuest() const
    {
        return m_creature->GetEntry() == DOCTOR_ALLIANCE ? QUEST_TRIAGE_A :
            m_creature->GetEntry() == DOCTOR_HORDE ? QUEST_TRIAGE_H : 0;
    }

    bool CanOfferQuest(uint32 quest) const override
    {
        return quest != QUEST_TRIAGE_A && quest != QUEST_TRIAGE_H ? true : !m_bIsEventInProgress;
    }

    void Reset() override;
    bool BeginEvent(Player* player);
    bool IsCurrentPatient(Creature* patient, Position* point, uint32 round) const;
    bool PatientSaved(Creature* patient, Player* player, Position* point, uint32 round);
    void PatientDied(Creature* patient, Position* point, uint32 round);
    void FinishEvent(bool success);
    void ReleaseBed(Position* point);
    void SummonedCreatureJustDied(Creature* patient) override;
    void SummonedCreatureDespawn(Creature* patient) override;
    void UpdateAI(uint32 diff) override;
};

struct npc_injured_patientAI : public ScriptedAI
{
    explicit npc_injured_patientAI(Creature* creature) : ScriptedAI(creature) { Reset(); }

    ObjectGuid m_doctorGuid;
    Position* m_pCoord = nullptr;
    uint32 m_round = 0;
    bool isSaved = false;

    void EnterEvadeMode() override
    {
        if (isSaved)
            ScriptedAI::EnterEvadeMode();
    }

    void Reset() override
    {
        m_doctorGuid.Clear();
        m_pCoord = nullptr;
        m_round = 0;
        // A departing, already treated patient must not become injured again.
        if (isSaved)
        {
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
            return;
        }
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
        switch (m_creature->GetEntry())
        {
            case 12923:
            case 12938:
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth() * .75));
                break;
            case 12924:
            case 12936:
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth() * .50));
                break;
            case 12925:
            case 12937:
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth() * .25));
                break;
        }
    }

    void SpellHit(Unit* caster, SpellEntry const* spell) override
    {
        if (!caster || !spell || caster->GetTypeId() != TYPEID_PLAYER ||
            !m_creature->IsAlive() || isSaved || spell->Id != 20804)
            return;
        Creature* doctor = m_creature->GetMap()->GetCreature(m_doctorGuid);
        auto ai = doctor ? dynamic_cast<npc_doctorAI*>(doctor->AI()) : nullptr;
        if (!ai || !ai->PatientSaved(m_creature, static_cast<Player*>(caster), m_pCoord, m_round))
            return;

        isSaved = true;
        m_doctorGuid.Clear();
        m_round = 0;
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        switch (urand(0, 2))
        {
            case 0: DoScriptText(SAY_DOC1, m_creature); break;
            case 1: DoScriptText(SAY_DOC2, m_creature); break;
            case 2: DoScriptText(SAY_DOC3, m_creature); break;
        }
        m_creature->SetWalk(false);
        if (m_creature->GetEntry() == 12923 || m_creature->GetEntry() == 12924 || m_creature->GetEntry() == 12925)
            m_creature->GetMotionMaster()->MovePoint(0, H_RUNTOX, H_RUNTOY, H_RUNTOZ);
        else
            m_creature->GetMotionMaster()->MovePoint(0, A_RUNTOX, A_RUNTOY, A_RUNTOZ);
    }

    void UpdateAI(uint32 diff) override
    {
        if (isSaved)
            return;
        uint32 loss = uint32(0.05f * diff);
        if (m_creature->IsAlive() && m_creature->GetHealth() > 1 + loss)
            m_creature->SetHealth(m_creature->GetHealth() - loss);
        if (m_creature->IsAlive() && m_creature->GetHealth() <= 1 + loss)
        {
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            m_creature->SetDeathState(JUST_DIED);
            m_creature->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
            if (Creature* doctor = m_creature->GetMap()->GetCreature(m_doctorGuid))
                if (auto ai = dynamic_cast<npc_doctorAI*>(doctor->AI()))
                    ai->PatientDied(m_creature, m_pCoord, m_round);
        }
    }
};

void npc_doctorAI::Reset()
{
    // Retire the round before synchronous despawn callbacks can return a bed.
    m_bIsEventInProgress = false;
    uint32 retiredRound = m_round;
    if (++m_round == 0)
        ++m_round;
    std::map<ObjectGuid, Position*> retired;
    retired.swap(m_pendingPatients);
    m_lPatientGuids.clear();
    m_vPatientSummonCoordinates.clear();
    m_playerGuid.Clear();
    m_uiSummonPatientTimer = 10000;
    m_uiSummonPatientCount = m_uiPatientDiedCount = m_uiPatientSavedCount = 0;
    for (auto const& slot : retired)
        if (Creature* patient = m_creature->GetMap()->GetCreature(slot.first))
            if (patient->GetSpawnerGuid() == m_creature->GetObjectGuid())
                if (auto ai = dynamic_cast<npc_injured_patientAI*>(patient->AI()))
                    if (ai->m_doctorGuid == m_creature->GetObjectGuid() && ai->m_round == retiredRound)
                    {
                        ai->m_doctorGuid.Clear();
                        ai->m_round = 0;
                        if (patient->IsAlive())
                            patient->ForcedDespawn();
                    }
    m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
}

bool npc_doctorAI::BeginEvent(Player* player)
{
    if (m_bIsEventInProgress)
        return player && m_playerGuid == player->GetObjectGuid();
    uint32 quest = EventQuest();
    if (!player || !quest || !m_creature->IsAlive() || !player->IsAlive() || player->GetMap() != m_creature->GetMap() ||
        player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE)
        return false;
    Reset();
    m_playerGuid = player->GetObjectGuid();
    if (m_creature->GetEntry() == DOCTOR_ALLIANCE)
        for (auto& point : AllianceCoords)
            m_vPatientSummonCoordinates.push_back(&point);
    else
        for (auto& point : HordeCoords)
            m_vPatientSummonCoordinates.push_back(&point);
    m_bIsEventInProgress = true;
    m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
    return true;
}

bool npc_doctorAI::IsCurrentPatient(Creature* patient, Position* point, uint32 round) const
{
    if (!m_bIsEventInProgress || round != m_round || !patient ||
        patient->GetSpawnerGuid() != m_creature->GetObjectGuid())
        return false;
    auto slot = m_pendingPatients.find(patient->GetObjectGuid());
    if (slot == m_pendingPatients.end() || slot->second != point)
        return false;
    auto ai = dynamic_cast<npc_injured_patientAI*>(patient->AI());
    return ai && ai->m_doctorGuid == m_creature->GetObjectGuid() && ai->m_round == round;
}

void npc_doctorAI::ReleaseBed(Position* point)
{
    if (point && std::find(m_vPatientSummonCoordinates.begin(), m_vPatientSummonCoordinates.end(), point) == m_vPatientSummonCoordinates.end())
        m_vPatientSummonCoordinates.push_back(point);
}

void npc_doctorAI::FinishEvent(bool success)
{
    if (!m_bIsEventInProgress)
        return;
    m_bIsEventInProgress = false;
    if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
        if (player->GetQuestStatus(EventQuest()) == QUEST_STATUS_INCOMPLETE)
        {
            if (success)
                player->RewardPlayerAndGroupAtEventExplored(EventQuest(), m_creature);
            else
                player->FailQuest(EventQuest());
        }
    Reset();
}

bool npc_doctorAI::PatientSaved(Creature* patient, Player* player, Position* point, uint32 round)
{
    if (!player || player->GetObjectGuid() != m_playerGuid || !patient || !patient->IsAlive() ||
        player->GetQuestStatus(EventQuest()) != QUEST_STATUS_INCOMPLETE || !IsCurrentPatient(patient, point, round))
        return false;
    m_pendingPatients.erase(patient->GetObjectGuid());
    ReleaseBed(point);
    if (++m_uiPatientSavedCount >= 15)
        FinishEvent(true);
    return true;
}

void npc_doctorAI::PatientDied(Creature* patient, Position* point, uint32 round)
{
    if (!IsCurrentPatient(patient, point, round) || patient->IsAlive())
        return;
    m_pendingPatients.erase(patient->GetObjectGuid());
    ReleaseBed(point);
    if (++m_uiPatientDiedCount >= 6)
        FinishEvent(false);
}

void npc_doctorAI::SummonedCreatureJustDied(Creature* patient)
{
    if (auto ai = dynamic_cast<npc_injured_patientAI*>(patient->AI()))
        PatientDied(patient, ai->m_pCoord, ai->m_round);
}

void npc_doctorAI::SummonedCreatureDespawn(Creature* patient)
{
    auto ai = dynamic_cast<npc_injured_patientAI*>(patient->AI());
    if (!ai || !IsCurrentPatient(patient, ai->m_pCoord, ai->m_round))
        return;
    // An unresolved actor that disappears cannot leave its bed reserved forever.
    m_pendingPatients.erase(patient->GetObjectGuid());
    ReleaseBed(ai->m_pCoord);
    if (++m_uiPatientDiedCount >= 6)
        FinishEvent(false);
}

void npc_doctorAI::UpdateAI(uint32 diff)
{
    if (!m_bIsEventInProgress)
        return;
    Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid);
    if (!player || player->GetQuestStatus(EventQuest()) != QUEST_STATUS_INCOMPLETE)
    {
        FinishEvent(false);
        return;
    }
    // Preserve the original alive-patient cadence, including saved actors' exit.
    uint32 totalAlive = 0;
    for (auto itr = m_lPatientGuids.begin(); itr != m_lPatientGuids.end();)
    {
        Creature* patient = m_creature->GetMap()->GetCreature(*itr);
        if (!patient)
        {
            auto slot = m_pendingPatients.find(*itr);
            if (slot != m_pendingPatients.end())
            {
                ReleaseBed(slot->second);
                m_pendingPatients.erase(slot);
                if (++m_uiPatientDiedCount >= 6)
                {
                    FinishEvent(false);
                    return;
                }
            }
            itr = m_lPatientGuids.erase(itr);
        }
        else
        {
            if (patient->IsAlive())
                ++totalAlive;
            ++itr;
        }
    }
    if (m_uiSummonPatientCount >= 21)
    {
        // Stop spawning, but keep the owner, actors and callbacks until resolved.
        if (m_pendingPatients.empty())
            FinishEvent(false);
        return;
    }
    if (m_vPatientSummonCoordinates.empty())
        return;
    if (m_uiSummonPatientTimer > diff)
    {
        m_uiSummonPatientTimer -= diff;
        return;
    }
    uint32 count = totalAlive == 0 ? urand(2, 3) : totalAlive == 1 ? urand(1, 2) : 0;
    count = std::min(count, 21 - m_uiSummonPatientCount);
    uint32 spawned = 0;
    for (uint32 i = 0; i < count && !m_vPatientSummonCoordinates.empty(); ++i)
    {
        auto point = m_vPatientSummonCoordinates.begin() + urand(0, m_vPatientSummonCoordinates.size() - 1);
        uint32 entry = m_creature->GetEntry() == DOCTOR_ALLIANCE ? AllianceSoldierId[urand(0, 2)] : HordeSoldierId[urand(0, 2)];
        Creature* patient = m_creature->SummonCreature(entry, (*point)->x, (*point)->y, (*point)->z, (*point)->o,
            TEMPSPAWN_TIMED_OOC_OR_CORPSE_DESPAWN, 5000);
        auto ai = patient ? dynamic_cast<npc_injured_patientAI*>(patient->AI()) : nullptr;
        if (!patient || !patient->IsAlive() || !patient->IsInWorld() || !ai ||
            patient->GetSpawnerGuid() != m_creature->GetObjectGuid())
        {
            if (patient && patient->GetSpawnerGuid() == m_creature->GetObjectGuid() && patient->IsAlive())
                patient->ForcedDespawn();
            continue;
        }
        patient->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
        ai->m_doctorGuid = m_creature->GetObjectGuid();
        ai->m_pCoord = *point;
        ai->m_round = m_round;
        m_pendingPatients.emplace(patient->GetObjectGuid(), *point);
        m_lPatientGuids.push_back(patient->GetObjectGuid());
        m_vPatientSummonCoordinates.erase(point);
        ++spawned;
    }
    m_uiSummonPatientCount += spawned;
    m_uiSummonPatientTimer = spawned == 0 ? urand(2000, 3000) : spawned == 1 ? urand(8000, 9000) : 10000;
}

bool QuestAccept_npc_doctor(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_TRIAGE_A || quest->GetQuestId() == QUEST_TRIAGE_H)
        if (auto ai = dynamic_cast<npc_doctorAI*>(creature->AI()))
            if (!ai->BeginEvent(player) && player->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_INCOMPLETE)
                player->FailQuest(quest->GetQuestId());
    return true;
}

UnitAI* GetAI_npc_injured_patient(Creature* creature)
{
    return new npc_injured_patientAI(creature);
}

UnitAI* GetAI_npc_doctor(Creature* creature)
{
    return new npc_doctorAI(creature);
}


/*######
## npc_garments_of_quests
######*/

enum
{
    SPELL_LESSER_HEAL_R2    = 2052,
    SPELL_FORTITUDE_R1      = 1243,

    QUEST_MOON              = 5621,
    QUEST_LIGHT_1           = 5624,
    QUEST_LIGHT_2           = 5625,
    QUEST_SPIRIT            = 5648,
    QUEST_DARKNESS          = 5650,

    ENTRY_SHAYA             = 12429,
    ENTRY_ROBERTS           = 12423,
    ENTRY_DOLF              = 12427,
    ENTRY_KORJA             = 12430,
    ENTRY_DG_KEL            = 12428,

    SAY_COMMON_HEALED       = -1000231,
    SAY_DG_KEL_THANKS       = -1000232,
    SAY_DG_KEL_GOODBYE      = -1000233,
    SAY_ROBERTS_THANKS      = -1000256,
    SAY_ROBERTS_GOODBYE     = -1000257,
    SAY_KORJA_THANKS        = -1000258,
    SAY_KORJA_GOODBYE       = -1000259,
    SAY_DOLF_THANKS         = -1000260,
    SAY_DOLF_GOODBYE        = -1000261,
    SAY_SHAYA_THANKS        = -1000262,
    SAY_SHAYA_GOODBYE       = -1000263,
};

struct npc_garments_of_questsAI : public npc_escortAI
{
    npc_garments_of_questsAI(Creature* pCreature) : npc_escortAI(pCreature) { Reset(); }

    ObjectGuid m_playerGuid;

    bool m_bIsHealed;
    bool m_bCanRun;

    uint32 m_uiRunAwayTimer;

    void Reset() override
    {
        m_playerGuid.Clear();

        m_bIsHealed = false;
        m_bCanRun = false;

        m_uiRunAwayTimer = 5000;

        m_creature->SetStandState(UNIT_STAND_STATE_KNEEL);
        // special pvp flag applies to Classic only
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PVP);
        // expect database to have RegenHealth=0
        m_creature->SetHealth(int(m_creature->GetMaxHealth() * 0.7));
    }

    void SpellHit(Unit* pCaster, const SpellEntry* pSpell) override
    {
        if (pSpell->Id == SPELL_LESSER_HEAL_R2 || pSpell->Id == SPELL_FORTITUDE_R1)
        {
            // not while in combat
            if (m_creature->IsInCombat())
                return;

            // nothing to be done now
            if (m_bIsHealed && m_bCanRun)
                return;

            if (pCaster->GetTypeId() == TYPEID_PLAYER)
            {
                switch (m_creature->GetEntry())
                {
                    case ENTRY_SHAYA:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_MOON) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (m_bIsHealed && !m_bCanRun && pSpell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_SHAYA_THANKS, m_creature, pCaster);
                                m_bCanRun = true;
                            }
                            else if (!m_bIsHealed && pSpell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                m_playerGuid = pCaster->GetObjectGuid();
                                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                                DoScriptText(SAY_COMMON_HEALED, m_creature, pCaster);
                                m_bIsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_ROBERTS:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_LIGHT_1) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (m_bIsHealed && !m_bCanRun && pSpell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_ROBERTS_THANKS, m_creature, pCaster);
                                m_bCanRun = true;
                            }
                            else if (!m_bIsHealed && pSpell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                m_playerGuid = pCaster->GetObjectGuid();
                                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                                DoScriptText(SAY_COMMON_HEALED, m_creature, pCaster);
                                m_bIsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_DOLF:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_LIGHT_2) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (m_bIsHealed && !m_bCanRun && pSpell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_DOLF_THANKS, m_creature, pCaster);
                                m_bCanRun = true;
                            }
                            else if (!m_bIsHealed && pSpell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                m_playerGuid = pCaster->GetObjectGuid();
                                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                                DoScriptText(SAY_COMMON_HEALED, m_creature, pCaster);
                                m_bIsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_KORJA:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_SPIRIT) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (m_bIsHealed && !m_bCanRun && pSpell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_KORJA_THANKS, m_creature, pCaster);
                                m_bCanRun = true;
                            }
                            else if (!m_bIsHealed && pSpell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                m_playerGuid = pCaster->GetObjectGuid();
                                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                                DoScriptText(SAY_COMMON_HEALED, m_creature, pCaster);
                                m_bIsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_DG_KEL:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_DARKNESS) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (m_bIsHealed && !m_bCanRun && pSpell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_DG_KEL_THANKS, m_creature, pCaster);
                                m_bCanRun = true;
                            }
                            else if (!m_bIsHealed && pSpell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                m_playerGuid = pCaster->GetObjectGuid();
                                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                                DoScriptText(SAY_COMMON_HEALED, m_creature, pCaster);
                                m_bIsHealed = true;
                            }
                        }
                        break;
                }

                // give quest credit, not expect any special quest objectives
                if (m_bCanRun)
                    ((Player*)pCaster)->TalkedToCreature(m_creature->GetEntry(), m_creature->GetObjectGuid());
            }
        }
    }

    void WaypointReached(uint32 /*uiPointId*/) override {}

    void UpdateEscortAI(const uint32 uiDiff) override
    {
        if (m_bCanRun && !m_creature->IsInCombat())
        {
            if (m_uiRunAwayTimer <= uiDiff)
            {
                if (Player* pPlayer = m_creature->GetMap()->GetPlayer(m_playerGuid))
                {
                    switch (m_creature->GetEntry())
                    {
                        case ENTRY_SHAYA:   DoScriptText(SAY_SHAYA_GOODBYE, m_creature, pPlayer);   break;
                        case ENTRY_ROBERTS: DoScriptText(SAY_ROBERTS_GOODBYE, m_creature, pPlayer); break;
                        case ENTRY_DOLF:    DoScriptText(SAY_DOLF_GOODBYE, m_creature, pPlayer);    break;
                        case ENTRY_KORJA:   DoScriptText(SAY_KORJA_GOODBYE, m_creature, pPlayer);   break;
                        case ENTRY_DG_KEL:  DoScriptText(SAY_DG_KEL_GOODBYE, m_creature, pPlayer);  break;
                    }

                    Start(true);
                }
                else
                    EnterEvadeMode();                       // something went wrong

                m_uiRunAwayTimer = 30000;
            }
            else
                m_uiRunAwayTimer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_garments_of_quests(Creature* pCreature)
{
    return new npc_garments_of_questsAI(pCreature);
}

/*######
## npc_guardian
######*/

enum GuardianOfB
{
    SPELL_DEATHTOUCH            = 5,
    SAY_AGGRO                   = 2077
};

struct npc_guardianAI : public ScriptedAI
{
    npc_guardianAI(Creature* pCreature) : ScriptedAI(pCreature) {Reset();}

    void Aggro(Unit* who) override
    {
        DoBroadcastText(SAY_AGGRO, m_creature, who);
    }

    void UpdateAI(const uint32 /*diff*/) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_creature->isAttackReady())
        {
            m_creature->CastSpell(m_creature->GetVictim(), SPELL_DEATHTOUCH, TRIGGERED_OLD_TRIGGERED);
            m_creature->resetAttackTimer();
        }
    }
};

UnitAI* GetAI_npc_guardian(Creature* pCreature)
{
    return new npc_guardianAI(pCreature);
}

/*########
# npc_innkeeper
#########*/

// Script applied to all innkeepers by npcflag.
// Are there any known innkeepers that does not hape the options in the below?
// (remember gossipHello is not called unless npcflag|1 is present)

enum
{
    TEXT_ID_WHAT_TO_DO              = 1853,

    SPELL_TRICK_OR_TREAT            = 24751,                // create item or random buff
    SPELL_TRICK_OR_TREATED          = 24755,                // buff player get when tricked or treated
};

#define GOSSIP_ITEM_TRICK_OR_TREAT  "Trick or Treat!"
#define GOSSIP_ITEM_WHAT_TO_DO      "What can I do at an Inn?"

bool GossipHello_npc_innkeeper(Player* pPlayer, Creature* pCreature)
{
    pPlayer->PrepareGossipMenu(pCreature, pPlayer->GetDefaultGossipMenuForSource(pCreature));

    if (IsHolidayActive(HOLIDAY_HALLOWS_END) && !pPlayer->HasAura(SPELL_TRICK_OR_TREATED, EFFECT_INDEX_0))
        pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ITEM_TRICK_OR_TREAT, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);

    // Should only apply to innkeeper close to start areas.
    if (AreaTableEntry const* pAreaEntry = GetAreaEntryByAreaID(pCreature->GetAreaId()))
    {
        // Note: this area flag doesn't exist in 1.12.1. The behavior of this gossip require additional research
        //if (pAreaEntry->flags & AREA_FLAG_LOWLEVEL)
        pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ITEM_WHAT_TO_DO, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
    }

    pPlayer->TalkedToCreature(pCreature->GetEntry(), pCreature->GetObjectGuid());
    pPlayer->SendPreparedGossip(pCreature);
    return true;
}

bool GossipSelect_npc_innkeeper(Player* pPlayer, Creature* pCreature, uint32 /*uiSender*/, uint32 uiAction)
{
    switch (uiAction)
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            pPlayer->SEND_GOSSIP_MENU(TEXT_ID_WHAT_TO_DO, pCreature->GetObjectGuid());
            break;
        case GOSSIP_ACTION_INFO_DEF+2:
            pPlayer->CLOSE_GOSSIP_MENU();
            pCreature->CastSpell(pPlayer, SPELL_TRICK_OR_TREAT, TRIGGERED_OLD_TRIGGERED);
            break;
        case GOSSIP_OPTION_VENDOR:
            pPlayer->SEND_VENDORLIST(pCreature->GetObjectGuid());
            break;
        case GOSSIP_OPTION_INNKEEPER:
            pPlayer->CLOSE_GOSSIP_MENU();
            pPlayer->SetBindPoint(pCreature->GetObjectGuid());
            break;
    }

    return true;
}

/*######
## npc_redemption_target
######*/

enum
{
    SAY_HEAL                    = -1000187,
    SAY_HEAL_HENZE_NARM_FAULK   = 2283,

    SPELL_SYMBOL_OF_LIFE        = 8593,
    SPELL_QUEST_SELF_HEALING    = 25155,
    SPELL_SHIMMERING_VESSEL     = 31225,        // Not used until TBC
    SPELL_REVIVE_SELF           = 32343,        // Not used until TBC

    NPC_HENZE_FAULK             = 6172,
    NPC_NARM_FAULK              = 6177,
    NPC_FURBOLG_SHAMAN          = 17542,        // Draenei side
    NPC_BLOOD_KNIGHT            = 17768,        // Blood Elf side
};

struct npc_redemption_targetAI : public ScriptedAI
{
    npc_redemption_targetAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiEvadeTimer;
    uint32 m_OrientationTimer;
    uint32 m_TextTimer;
    uint32 m_EmoteTimer;

    ObjectGuid m_playerGuid;

    void Reset() override
    {
        m_uiEvadeTimer = 0;
        m_OrientationTimer = 0;
        m_TextTimer = 0;
        m_EmoteTimer = 0;

        // Reset Orientation?
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
    }

    void DoReviveSelf(ObjectGuid m_guid)
    {
        // Wait until he resets again
        if (m_uiEvadeTimer)
            return;

        DoCastSpellIfCan(m_creature, SPELL_QUEST_SELF_HEALING);
        m_creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_DEAD);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_uiEvadeTimer = 2 * MINUTE * IN_MILLISECONDS;
        m_playerGuid = m_guid;
        m_OrientationTimer = 3000;
        m_TextTimer = 4000;
        m_EmoteTimer = 7000;
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_OrientationTimer)
        {
            if (m_OrientationTimer <= uiDiff)
            {
                if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
                {
                    m_creature->SetFacingToObject(player);
                    m_OrientationTimer = 0;
                }
            }
            else
                m_OrientationTimer -= uiDiff;
        }

        if (m_TextTimer)
        {
            if (m_TextTimer <= uiDiff)
            {
                if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
                {
                    // Quests 1783 and 1786 require NpcFlags i.6866
                    if (m_creature->GetEntry() == NPC_HENZE_FAULK || m_creature->GetEntry() == NPC_NARM_FAULK)
                    {
                        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                        DoBroadcastText(SAY_HEAL_HENZE_NARM_FAULK, m_creature, player);
                    }
                    // Quests 9600 and 9685 requires kill credit i.6866/24184
                    if (m_creature->GetEntry() == NPC_FURBOLG_SHAMAN || m_creature->GetEntry() == NPC_BLOOD_KNIGHT)
                    {
                        DoScriptText(SAY_HEAL, m_creature, player);
                        player->KilledMonsterCredit(m_creature->GetEntry(), m_creature->GetObjectGuid());
                    }

                    m_TextTimer = 0;
                }
            }
            else
                m_TextTimer -= uiDiff;
        }

        if (m_EmoteTimer)
        {
            if (m_EmoteTimer <= uiDiff)
            {
                if (m_creature->GetEntry() == NPC_HENZE_FAULK || m_creature->GetEntry() == NPC_NARM_FAULK)
                {
                    m_creature->HandleEmote(EMOTE_ONESHOT_KNEEL);
                    m_EmoteTimer = 0;
                }
            }
            else
                m_EmoteTimer -= uiDiff;
        }

        if (m_uiEvadeTimer)
        {
            if (m_uiEvadeTimer <= uiDiff)
            {
                EnterEvadeMode();
                m_uiEvadeTimer = 0;
            }
            else
                m_uiEvadeTimer -= uiDiff;
        }
    }
};

UnitAI* GetAI_npc_redemption_target(Creature* pCreature)
{
    return new npc_redemption_targetAI(pCreature);
}

// 8593 - Symbol of Life
// 31225 - Shimmering Vessel
struct PaladinQuestReviveSelf : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        if (npc_redemption_targetAI* pTargetAI = dynamic_cast<npc_redemption_targetAI*>(target->AI()))
            pTargetAI->DoReviveSelf(caster->GetObjectGuid());
    }
};

/*######
## npc_the_cleaner
######*/
enum
{
    SPELL_IMMUNITY      = 29230,
    SAY_CLEANER_AGGRO   = -1001253
};

struct npc_the_cleanerAI : public ScriptedAI
{
    npc_the_cleanerAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiDespawnTimer;

    void Reset() override
    {
        DoCastSpellIfCan(m_creature, SPELL_IMMUNITY, CAST_TRIGGERED);
        m_uiDespawnTimer = 3000;
    }

    void Aggro(Unit* pWho) override
    {
        DoScriptText(SAY_CLEANER_AGGRO, m_creature);
    }

    void EnterEvadeMode() override
    {
        ScriptedAI::EnterEvadeMode();

        m_creature->ForcedDespawn();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_uiDespawnTimer < uiDiff)
        {
            if (m_creature->getThreatManager().getThreatList().empty())
                m_creature->ForcedDespawn();
        }
        else
            m_uiDespawnTimer -= uiDiff;

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_the_cleaner(Creature* pCreature)
{
    return new npc_the_cleanerAI(pCreature);
}

/*######
## npc_aoe_damage_trigger
######*/

enum npc_aoe_damage_trigger
{
    // trigger npcs
    NPC_VOID_ZONE = 16697,

    // m_uiAuraPassive
    SPELL_CONSUMPTION_NPC_16697 = 28874,
};

struct npc_aoe_damage_triggerAI : public ScriptedAI
{
    npc_aoe_damage_triggerAI(Creature* pCreature) : ScriptedAI(pCreature), m_uiAuraPassive(SetAuraPassive())
    {
        AddCustomAction(1, GetTimer(), [&]() {CastConsumption(); });
        SetReactState(REACT_PASSIVE);
    }

    uint32 m_uiAuraPassive;

    inline uint32 GetTimer()
    {
        return 2500; // adjust in future if other void zones are different this is for NPC_VOID_ZONE
    }

    inline uint32 SetAuraPassive()
    {
        switch (m_creature->GetCreatureInfo()->Entry)
        {
            case NPC_VOID_ZONE:
                return SPELL_CONSUMPTION_NPC_16697;
            default:
                return SPELL_CONSUMPTION_NPC_16697;
        }
    }

    void CastConsumption()
    {
        DoCastSpellIfCan(m_creature, m_uiAuraPassive, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void Reset() override {}
};

// 5166 - Harvest Silithid Egg
struct HarvestSilithidEgg : public SpellScript
{
    void OnInit(Spell* spell) const override
    {
        spell->SetEffectChance(75, EFFECT_INDEX_1);
        spell->SetEffectChance(1, EFFECT_INDEX_2);
    }
};

/*######
## npc_engineering_target_dummy
######*/

struct npc_engineering_target_dummyAI : public ScriptedAI
{
    explicit npc_engineering_target_dummyAI(Creature* creature) : ScriptedAI(creature), m_dieTimer(0)
    {
        auto const* definition = EngineeringTargetDummy::Find(m_creature->GetUInt32Value(UNIT_CREATED_BY_SPELL), m_creature->GetEntry());
        if (!definition || !m_creature->IsTemporarySummon())
            return;

        m_dieTimer = EngineeringTargetDummy::Lifetime;
        SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
        SetFollowMovement(false);
        SetMeleeEnabled(false);
        m_creature->SetNoXP(true);

        if (Unit* creator = m_creature->GetMap()->GetUnit(m_creature->GetSpawnerGuid()))
        {
            // In this core SetOwnerGuid writes UNIT_FIELD_CREATEDBY.
            m_creature->SetOwnerGuid(creator->GetObjectGuid());
            m_creature->SetLootRecipient(creator);
            // The old guardian template is immune to NPCs. Ordinary players'
            // dummies must be attackable; keep a GM creator's immunity.
            m_creature->SetImmuneToNPC(creator->IsImmuneToNPC());
            m_creature->SetImmuneToPlayer(creator->IsImmuneToPlayer());
            if (creator->IsPlayerControlled())
                m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
            m_creature->SetPvP(creator->IsPvP());
        }

        DoCastSpellIfCan(m_creature, definition->passiveSpell, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(m_creature, definition->spawnSpell, CAST_TRIGGERED);
    }

    void Reset() override {}
    void AttackStart(Unit* /*target*/) override {}
    void JustDied(Unit* /*killer*/) override { m_dieTimer = 0; }
    void EnterEvadeMode() override
    {
        // UnitAI's normal evade path removes periodic auras and clears the
        // loot recipient. A disposable dummy keeps both until it dies.
        m_creature->CombatStop(true);
    }

    uint32 m_dieTimer;

    void UpdateAI(const uint32 diff) override
    {
        if (!m_dieTimer || !m_creature->IsAlive())
            return;

        if (m_dieTimer <= diff)
        {
            m_dieTimer = 0;
            // Use the normal death pipeline for both expiry and combat deaths.
            // It generates body loot before clearing threat and keeps a corpse.
            m_creature->Suicide();
        }
        else
            m_dieTimer -= diff;
    }
};

/*######
## npc_training_dummy
######*/

struct npc_training_dummyAI : public ScriptedAI
{
    static constexpr uint32 INACTIVITY_TIMEOUT = 10000;

    explicit npc_training_dummyAI(Creature* creature) : ScriptedAI(creature), m_savedLevel(0)
    {
        if (m_creature->HasStaticDBSpawnData())
        {
            auto result = WorldDatabase.PQuery("SELECT level FROM training_dummy_settings WHERE guid=%u AND entry=%u",
                m_creature->GetGUIDLow(), m_creature->GetEntry());
            if (result)
            {
                uint32 level = result->Fetch()[0].GetUInt32();
                if (level >= 1 && level <= 63 && sObjectMgr.GetCreatureClassLvlStats(level, m_creature->GetCreatureInfo()->UnitClass))
                    m_savedLevel = level;
                else
                    sLog.outError("Invalid saved training dummy level %u for GUID %u", level, m_creature->GetGUIDLow());
            }
        }
        Reset();
    }

    uint32 m_savedLevel;
    uint32 m_inactivityTimer;
    bool m_hasActivity;

    void Reset() override
    {
        SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
        SetMeleeEnabled(false);
        SetAIImmobilizedState(true);
        SetDeathPrevention(true);
        m_creature->SetNoCallAssistance(true);
        m_creature->SetNoXP(true);
        m_creature->SetNoLoot(true);
        m_creature->SetNoWeaponSkillGain(true);
        // Keep per-spawn settings across combat resets and respawns. Read SQL only at AI creation.
        if (m_savedLevel)
        {
            if (CreatureClassLvlStats const* stats = sObjectMgr.GetCreatureClassLvlStats(m_savedLevel, m_creature->GetCreatureInfo()->UnitClass))
            {
                m_creature->SetLevel(m_savedLevel);
                m_creature->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE, float(stats->BaseArmor));
                m_creature->UpdateArmor();
            }
        }
        m_creature->SetHealth(m_creature->GetMaxHealth());
        m_inactivityTimer = INACTIVITY_TIMEOUT;
        m_hasActivity = false;
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* /*invoker*/, uint32 level) override
    {
        if (eventType != AI_EVENT_CUSTOM_A || sender != m_creature || level < 1 || level > 63
            || !sObjectMgr.GetCreatureClassLvlStats(level, m_creature->GetCreatureInfo()->UnitClass))
            return;

        // The GM command has synchronously saved this level before notifying the AI.
        m_savedLevel = level;
        EnterEvadeMode();
    }

    void MoveInLineOfSight(Unit*) override {}
    void AttackStart(Unit*) override {}

    void RecordActivity(Unit* source)
    {
        if (source && source != m_creature && source->GetBeneficiaryPlayer())
        {
            m_inactivityTimer = INACTIVITY_TIMEOUT;
            m_hasActivity = true;
        }
    }

    void AttackedBy(Unit* attacker) override { RecordActivity(attacker); }
    void SpellHit(Unit* caster, SpellEntry const*) override { RecordActivity(caster); }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        RecordActivity(dealer);
        // Keep the calculated damage unchanged: reducing it here is reported as absorption.
        // A large health pool and direct refill avoid reaching the death-prevention clamp
        // during ordinary attacks, without a healing spell, threat or player power gain.
        if (damageType != INSTAKILL)
            m_creature->SetHealth(m_creature->GetMaxHealth());
        CreatureAI::DamageTaken(dealer, damage, damageType, spellInfo);
    }

    bool HasPeriodicActivity() const
    {
        return m_creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE)
            || m_creature->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
            || m_creature->HasAuraType(SPELL_AURA_PERIODIC_LEECH)
            || m_creature->HasAuraType(SPELL_AURA_PERIODIC_MANA_LEECH)
            || m_creature->HasAuraType(SPELL_AURA_POWER_BURN_MANA);
    }

    void EnterEvadeMode() override
    {
        // Only release this target's combat references. Do not clear the players' other fights.
        m_creature->RemoveAllAurasOnEvade();
        m_creature->CombatStop(true);
        m_creature->getHostileRefManager().deleteReferences();
        m_creature->SetLootRecipient(nullptr);
        Reset();
    }

    void UpdateAI(uint32 diff) override
    {
        if (!m_hasActivity && !m_creature->IsInCombat())
            return;

        if (m_inactivityTimer > diff)
        {
            m_inactivityTimer -= diff;
            return;
        }

        m_inactivityTimer = 0;
        // Core combat time also covers hostile attempts that missed. Wait for long-period
        // effects such as Curse of Doom instead of removing them before their first tick.
        if (m_creature->GetCombatManager().GetCombatTimer() || HasPeriodicActivity())
            return;

        EnterEvadeMode();
    }
};

void AddSC_npcs_special()
{
    Script* pNewScript;

    pNewScript = new Script;
    pNewScript->Name = "npc_chicken_cluck";
    pNewScript->GetAI = &GetAI_npc_chicken_cluck;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_injured_patient";
    pNewScript->GetAI = &GetAI_npc_injured_patient;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_doctor";
    pNewScript->GetAI = &GetAI_npc_doctor;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_doctor;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_garments_of_quests";
    pNewScript->GetAI = &GetAI_npc_garments_of_quests;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_guardian";
    pNewScript->GetAI = &GetAI_npc_guardian;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_innkeeper";
    pNewScript->pGossipHello = &GossipHello_npc_innkeeper;
    pNewScript->pGossipSelect = &GossipSelect_npc_innkeeper;
    pNewScript->RegisterSelf(false);                        // script and error report disabled, but script can be used for custom needs, adding ScriptName

    pNewScript = new Script;
    pNewScript->Name = "npc_redemption_target";
    pNewScript->GetAI = &GetAI_npc_redemption_target;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_the_cleaner";
    pNewScript->GetAI = &GetAI_npc_the_cleaner;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_aoe_damage_trigger";
    pNewScript->GetAI = &GetNewAIInstance<npc_aoe_damage_triggerAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_advanced_target_dummy";
    pNewScript->GetAI = &GetNewAIInstance<npc_engineering_target_dummyAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_engineering_target_dummy";
    pNewScript->GetAI = &GetNewAIInstance<npc_engineering_target_dummyAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_training_dummy";
    pNewScript->GetAI = &GetNewAIInstance<npc_training_dummyAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<PaladinQuestReviveSelf>("spell_paladin_quest_revive_self");
    RegisterSpellScript<HarvestSilithidEgg>("spell_harvest_silithid_egg");
}
