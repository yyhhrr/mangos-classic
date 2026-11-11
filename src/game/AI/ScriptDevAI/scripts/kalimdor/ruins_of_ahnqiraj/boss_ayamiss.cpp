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
SDName: Boss_Ayamiss
SD%Complete: 95
SDComment: Swarmers need movement added for RP before they attack
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "ruins_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "MotionGenerators/WaypointManager.h"
#include "Spells/SpellMgr.h"
#include <G3D/Vector3.h>

enum
{
    EMOTE_GENERIC_FRENZY    = -1000002,

    SPELL_STINGER_SPRAY     = 25749,
    SPELL_POISON_STINGER    = 25748,                // only used in phase1
    SPELL_SUMMON_SWARMER    = 25708,
    SPELL_PARALYZE          = 25725,
    SPELL_LASH              = 25852,
    SPELL_FRENZY            = 8269,
    SPELL_THRASH            = 3391,

    SPELL_FEED              = 25721,                // cast by the Larva when reaches the player on the altar

    SPELL_SWARMER_TELEPORT_1 = 25709,
    SPELL_SWARMER_TELEPORT_2 = 25825,
    SPELL_SWARMER_TELEPORT_3 = 25826,
    SPELL_SWARMER_TELEPORT_4 = 25827,
    SPELL_SWARMER_TELEPORT_5 = 25828,

    SPELL_SUMMON_LARVA_1     = 26538,
    SPELL_SUMMON_LARVA_2     = 26539,

    NPC_LARVA               = 15555,
    NPC_SWARMER             = 15546,
    NPC_HORNET              = 15934,

    PHASE_AIR               = 0,
    PHASE_LANDING,
    PHASE_GROUND,

    POINT_AIR               = 2,
    POINT_GROUND            = 3,
    POINT_LARVA_ALTAR       = 4,
    POINT_SWARMER_DIVE      = 5,
};

// Route around the Comb, adapted from the AzerothCore swarmer waypoints.
static const G3D::Vector3 aSwarmerFlightPath[] =
{
    { -9748.13f, 1488.49f, 45.76f },
    { -9701.45f, 1522.10f, 43.54f },
    { -9647.55f, 1544.99f, 42.96f },
    { -9657.68f, 1593.86f, 45.07f },
    { -9645.76f, 1642.12f, 50.04f },
    { -9618.59f, 1606.99f, 45.07f },
    { -9662.09f, 1559.25f, 45.07f },
    { -9692.73f, 1523.67f, 45.07f },
    { -9729.18f, 1490.00f, 45.07f },
};

static const Position ayamissLanding = { -9677.917f, 1558.839f, 27.24953f, 0.0f };

struct npc_hive_zara_larvaAI : public ScriptedAI
{
    explicit npc_hive_zara_larvaAI(Creature* creature) : ScriptedAI(creature) {}

    ObjectGuid m_bossGuid;
    ObjectGuid m_targetGuid;
    bool m_fed = false;

    void StartSacrifice(Creature* boss, Player* target)
    {
        m_bossGuid = boss->GetObjectGuid();
        m_targetGuid = target->GetObjectGuid();
        SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
        SetMeleeEnabled(false);
        m_creature->SetCorpseDelay(5);

        if (SpellTargetPosition const* altar = sSpellMgr.GetSpellTargetPosition(SPELL_PARALYZE))
            m_creature->GetMotionMaster()->MovePoint(POINT_LARVA_ALTAR, altar->target_X, altar->target_Y, altar->target_Z, FORCED_MOVEMENT_RUN);
        else
            m_creature->ForcedDespawn();
    }

    Player* GetSacrificeTarget() const
    {
        Creature* boss = m_creature->GetMap()->GetCreature(m_bossGuid);
        if (!boss || !boss->IsAlive() || !boss->IsInCombat())
            return nullptr;

        Player* target = m_creature->GetMap()->GetPlayer(m_targetGuid);
        if (!target || !target->IsAlive() || !target->GetSpellAuraHolder(SPELL_PARALYZE, m_bossGuid))
            return nullptr;

        return target;
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (!m_bossGuid)
            ScriptedAI::MoveInLineOfSight(who);
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType != POINT_MOTION_TYPE || pointId != POINT_LARVA_ALTAR || m_fed || !m_creature->IsAlive())
            return;

        if (Player* target = GetSacrificeTarget())
        {
            if (m_creature->IsWithinDistInMap(target, INTERACTION_DISTANCE))
            {
                m_fed = true;
                m_creature->CastSpell(target, SPELL_FEED, TRIGGERED_OLD_TRIGGERED);
                return;
            }
        }

        m_creature->ForcedDespawn();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (m_bossGuid && summoned->GetEntry() == NPC_HORNET)
            if (Creature* boss = m_creature->GetMap()->GetCreature(m_bossGuid))
                boss->AI()->JustSummoned(summoned);
    }

    void UpdateAI(uint32 /*diff*/) override
    {
        if (m_bossGuid)
        {
            if (!GetSacrificeTarget())
                m_creature->ForcedDespawn();
            return;
        }

        if (m_creature->SelectHostileTarget() && m_creature->GetVictim())
            DoMeleeAttackIfReady();
    }
};

enum AyamissActions
{
    AYAMISS_FLY_UP,
    AYAMISS_PHASE_2,
    AYAMISS_FRENZY,
    AYAMISS_PARALYZE,
    AYAMISS_STINGER_SPRAY,
    AYAMISS_POISON_STINGER,
    AYAMISS_SWARMER_ATTACK,
    AYAMISS_SUMMON_SWARMER,
    AYAMISS_LASH,
    AYAMISS_THRASH,
    AYAMISS_ACTION_MAX,
    AYAMISS_LANDING_TIMEOUT = AYAMISS_ACTION_MAX,
};

struct boss_ayamissAI : public CombatAI
{
    boss_ayamissAI(Creature* creature) :
        CombatAI(creature, AYAMISS_ACTION_MAX),
        m_phase(PHASE_AIR)
    {
        AddTimerlessCombatAction(AYAMISS_FLY_UP, true);
        AddTimerlessCombatAction(AYAMISS_PHASE_2, true);
        AddTimerlessCombatAction(AYAMISS_FRENZY, true);
        AddCombatAction(AYAMISS_PARALYZE, 15000u);
        AddCombatAction(AYAMISS_STINGER_SPRAY, 20000, 30000);
        AddCombatAction(AYAMISS_POISON_STINGER, 5000u);
        AddCombatAction(AYAMISS_SUMMON_SWARMER, 5000u);
        AddCombatAction(AYAMISS_SWARMER_ATTACK, 60000u);
        AddCombatAction(AYAMISS_LASH, true);
        AddCombatAction(AYAMISS_THRASH, true);
        AddCustomAction(AYAMISS_LANDING_TIMEOUT, true, [this]() { EnterEvadeMode(); }, TIMER_COMBAT_COMBAT);
        m_creature->SetWalk(false);
    }

    uint8 m_phase;

    ObjectGuid m_pendingSacrifice;
    GuidSet m_paralyzedPlayers;
    GuidList m_swarmersGuidList;
    GuidSet m_spawns;

    void Reset() override
    {
        CombatAI::Reset();
        m_phase = PHASE_AIR;

        SetCombatMovement(false);
        SetMeleeEnabled(false);
        SetCombatScriptStatus(false);
        m_creature->SetLevitate(false);
        m_creature->SetHover(false);

        ClearEncounter(false);
    }

    void EnterEvadeMode() override
    {
        m_creature->SetImmobilizedState(false);
        CombatAI::EnterEvadeMode();
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        m_creature->SetImmobilizedState(false);
        ClearEncounter(true);
    }

    void ClearEncounter(bool keepCombatHornets)
    {
        m_pendingSacrifice.Clear();
        for (ObjectGuid const& guid : m_paralyzedPlayers)
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                player->RemoveAurasByCasterSpell(SPELL_PARALYZE, m_creature->GetObjectGuid());
        m_paralyzedPlayers.clear();
        m_swarmersGuidList.clear();

        GuidSet spawns;
        spawns.swap(m_spawns);
        for (ObjectGuid const& guid : spawns)
        {
            if (Creature* summon = m_creature->GetMap()->GetCreature(guid))
            {
                if (keepCombatHornets && summon->GetEntry() == NPC_HORNET && summon->IsAlive() && summon->IsInCombat())
                    continue;
                summon->ForcedDespawn();
            }
        }
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        m_spawns.erase(summoned->GetObjectGuid());
    }

    void JustSummoned(Creature* summoned) override
    {
        // Wild summon spells can notify the caster twice for the same creature.
        if (!m_spawns.insert(summoned->GetObjectGuid()).second)
            return;

        if (summoned->GetEntry() == NPC_SWARMER)
            StartSwarmerFlight(summoned);
        else if (summoned->GetEntry() == NPC_LARVA)
        {
            auto* larva = dynamic_cast<npc_hive_zara_larvaAI*>(summoned->AI());
            Player* target = m_creature->GetMap()->GetPlayer(m_pendingSacrifice);
            if (larva && target)
                larva->StartSacrifice(m_creature, target);
            else
                summoned->ForcedDespawn();
        }
        else if (summoned->GetEntry() == NPC_HORNET)
        {
            if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
                summoned->AI()->AttackStart(target);
        }
    }

    void SpellHitTarget(Unit* target, SpellEntry const* spell, SpellMissInfo miss) override
    {
        if (spell->Id != SPELL_PARALYZE || miss != SPELL_MISS_NONE || !target->IsPlayer() || !target->IsAlive() ||
            !target->GetSpellAuraHolder(SPELL_PARALYZE, m_creature->GetObjectGuid()))
            return;

        m_pendingSacrifice = target->GetObjectGuid();
        m_paralyzedPlayers.insert(m_pendingSacrifice);
        m_creature->CastSpell(nullptr, urand(0, 1) ? SPELL_SUMMON_LARVA_1 : SPELL_SUMMON_LARVA_2, TRIGGERED_OLD_TRIGGERED);
        m_pendingSacrifice.Clear();
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType == POINT_MOTION_TYPE && pointId == POINT_AIR && m_phase == PHASE_AIR)
        {
            SetCombatScriptStatus(false);
            m_creature->SetImmobilizedState(true);
        }
        else if (pointId == POINT_GROUND && m_phase == PHASE_LANDING &&
                 (motionType == PATH_MOTION_TYPE || motionType == POINT_MOTION_TYPE))
        {
            DisableTimer(AYAMISS_LANDING_TIMEOUT);
            m_phase = PHASE_GROUND;
            m_creature->SetLevitate(false);
            m_creature->SetHover(false);
            DoResetThreat();
            SetCombatScriptStatus(false);
            SetMeleeEnabled(true);
            SetCombatMovement(true, true);
            ResetCombatAction(AYAMISS_LASH, urand(5000, 8000));
            ResetCombatAction(AYAMISS_THRASH, urand(3000, 6000));
        }
    }

    void StartSwarmerFlight(Creature* swarmer)
    {
        static constexpr uint32 teleportSpells[] =
        {
            SPELL_SWARMER_TELEPORT_1, SPELL_SWARMER_TELEPORT_2, SPELL_SWARMER_TELEPORT_3,
            SPELL_SWARMER_TELEPORT_4, SPELL_SWARMER_TELEPORT_5,
        };
        m_swarmersGuidList.push_back(swarmer->GetObjectGuid());
        swarmer->AI()->SetReactState(REACT_PASSIVE);
        swarmer->SetActiveObjectState(true);
        swarmer->SetLevitate(true);
        swarmer->SetHover(true);
        swarmer->CastSpell(nullptr, teleportSpells[urand(0, 4)], TRIGGERED_OLD_TRIGGERED);

        // Keep a little vertical separation between insects following the same route.
        const float heightOffset = frand(0.0f, 3.0f);
        std::vector<G3D::Vector3> path(std::begin(aSwarmerFlightPath), std::end(aSwarmerFlightPath));
        for (G3D::Vector3& point : path)
            point.z += heightOffset;
        swarmer->GetMotionMaster()->MovePath(path, FORCED_MOVEMENT_RUN, true, true);
    }

    void SummonedMovementInform(Creature* summoned, uint32 motionType, uint32 pointId) override
    {
        if (summoned->GetEntry() != NPC_SWARMER || motionType != POINT_MOTION_TYPE || pointId != POINT_SWARMER_DIVE ||
            !summoned->IsAlive() || !m_creature->IsAlive() || !m_creature->IsInCombat())
            return;

        summoned->SetLevitate(false);
        summoned->SetHover(false);
        summoned->SetActiveObjectState(false);
        summoned->AI()->SetReactState(REACT_AGGRESSIVE);
        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
            summoned->AI()->AttackStart(target);
    }

    void StartLanding()
    {
        WaypointPath const* path = sWaypointMgr.GetPathFromOrigin(m_creature->GetEntry(), m_creature->GetDbGuid(), 1, PATH_FROM_ENTRY);
        if (path && path->find(POINT_GROUND) != path->end())
            m_creature->GetMotionMaster()->MovePath(1, PATH_FROM_ENTRY, FORCED_MOVEMENT_NONE, true);
        else
            m_creature->GetMotionMaster()->MovePoint(POINT_GROUND, ayamissLanding, FORCED_MOVEMENT_FLIGHT, 0.0f, false);

        ResetTimer(AYAMISS_LANDING_TIMEOUT, 12000);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case AYAMISS_FLY_UP:
            {
                m_creature->SetLevitate(true);
                m_creature->SetHover(true);
                SetCombatScriptStatus(true);
                m_creature->GetMotionMaster()->MovePoint(POINT_AIR, -9689.292f, 1547.912f, 48.02729f, FORCED_MOVEMENT_FLIGHT, false);
                SetActionReadyStatus(action, false);
                break;
            }
            case AYAMISS_PHASE_2:
            {
                if (m_creature->GetHealthPercent() <= 70.0f)
                {
                    m_phase = PHASE_LANDING;
                    m_creature->SetImmobilizedState(false);
                    SetCombatScriptStatus(true);
                    SetActionReadyStatus(action, false);
                    DisableCombatAction(AYAMISS_POISON_STINGER);
                    StartLanding();
                }
                break;
            }
            case AYAMISS_FRENZY:
            {
                if (m_creature->GetHealthPercent() < 20.0f)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_FRENZY) == CAST_OK)
                    {
                        DoScriptText(EMOTE_GENERIC_FRENZY, m_creature);
                        SetActionReadyStatus(action, false);
                    }
                }
                break;
            }
            case AYAMISS_PARALYZE:
            {
                Unit* target = nullptr;
                if (m_phase == PHASE_AIR)
                    target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_PARALYZE, SELECT_FLAG_PLAYER | SELECT_FLAG_SKIP_TANK);
                if (!target)
                    target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_PARALYZE, SELECT_FLAG_PLAYER);

                if (target && DoCastSpellIfCan(target, SPELL_PARALYZE) == CAST_OK)
                    ResetCombatAction(action, 15000);
                break;
            }
            case AYAMISS_STINGER_SPRAY:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_STINGER_SPRAY) == CAST_OK)
                    ResetCombatAction(action, urand(15000, 20000));
                break;
            }
            case AYAMISS_POISON_STINGER:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 0, SPELL_POISON_STINGER, SELECT_FLAG_PLAYER))
                    if (DoCastSpellIfCan(target, SPELL_POISON_STINGER) == CAST_OK)
                        ResetCombatAction(action, urand(2000, 3000));
                break;
            }
            case AYAMISS_SUMMON_SWARMER:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_SUMMON_SWARMER) == CAST_OK)
                    ResetCombatAction(action, 5000);
                break;
            }
            case AYAMISS_SWARMER_ATTACK:
            {
                Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER);
                if (!target)
                    break;

                const Position landing = target->GetPosition();
                for (ObjectGuid guid : m_swarmersGuidList)
                {
                    Creature* swarmer = m_creature->GetMap()->GetCreature(guid);
                    if (!swarmer || !swarmer->IsAlive())
                        continue;

                    swarmer->GetMotionMaster()->Clear(false);
                    swarmer->GetMotionMaster()->MovePoint(POINT_SWARMER_DIVE, landing, FORCED_MOVEMENT_FLIGHT, 18.0f, false);
                }
                m_swarmersGuidList.clear();
                ResetCombatAction(action, 60000);
                break;
            }
            case AYAMISS_LASH:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_LASH) == CAST_OK)
                    ResetCombatAction(action, urand(8000, 15000));
                break;
            }
            case AYAMISS_THRASH:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_THRASH) == CAST_OK)
                    ResetCombatAction(action, urand(5000, 7000));
                break;
            }
        }
    }
};

void AddSC_boss_ayamiss()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_ayamiss";
    pNewScript->GetAI = &GetNewAIInstance<boss_ayamissAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_hive_zara_larva";
    pNewScript->GetAI = &GetNewAIInstance<npc_hive_zara_larvaAI>;
    pNewScript->RegisterSelf();
}
