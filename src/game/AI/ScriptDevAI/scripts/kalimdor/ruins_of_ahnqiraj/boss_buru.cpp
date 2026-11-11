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
SDName: Boss_Buru
SD%Complete: 70
SDComment:
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "ruins_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Maps/MapPersistentStateMgr.h"

enum
{
    EMOTE_TARGET            = -1509002,

    // boss spells
    SPELL_CREEPING_PLAGUE   = 20512,
    SPELL_DISMEMBER         = 96,
    SPELL_GATHERING_SPEED   = 1834,
    SPELL_FULL_SPEED        = 1557,
    SPELL_THORNS            = 25640,
    SPELL_BURU_TRANSFORM    = 24721,
    SPELL_CREATURE_SPECIAL  = 7155, // unk purpose

    // egg spells
    SPELL_SUMMON_HATCHLING  = 1881,
    SPELL_EXPLODE           = 19593,
    SPELL_BURU_EGG_TRIGGER  = 26646,
    SPELL_EXPLOSION         = 5255,

    // npcs
    NPC_BURU_EGG_TRIGGER    = 15964,
    NPC_BURU_EGG            = 15514,
    NPC_HATCHLING           = 15521,

    BURU_EGG_DAMAGE         = 78650, // About 45000 damage after Buru's normal armor.
    PHASE_EGG               = 1,
    PHASE_TRANSFORM         = 2,
};

enum BuruActions
{
    BURU_PHASE_2_TRANSITION,
    BURU_NEW_TARGET,
    BURU_DISMEMBER,
    BURU_CREEPING_PLAGUE,
    BURU_GATHERING_SPEED,
    BURU_FULL_SPEED,
    BURU_ACTION_MAX,
    BURU_PHASE_2_TRANSITION_STEP,
    BURU_RETARGET_FALLBACK,
};

struct boss_buruAI : public CombatAI
{
    boss_buruAI(Creature* creature) :
        CombatAI(creature, BURU_ACTION_MAX),
        m_uiPhase(0),
        m_transitionStep(0)
    {
        AddTimerlessCombatAction(BURU_PHASE_2_TRANSITION, true);
        AddTimerlessCombatAction(BURU_NEW_TARGET, false);
        AddCombatAction(BURU_DISMEMBER, 5000u);
        AddCombatAction(BURU_CREEPING_PLAGUE, true);
        AddCombatAction(BURU_GATHERING_SPEED, 9000u);
        AddCombatAction(BURU_FULL_SPEED, 60000u);
        AddCustomAction(BURU_PHASE_2_TRANSITION_STEP, true, [this]() { HandlePhaseTwo(); }, TIMER_COMBAT_COMBAT);
        AddCustomAction(BURU_RETARGET_FALLBACK, true, [this]() { DoAttackNewTarget(); }, TIMER_COMBAT_COMBAT);
    }

    uint8 m_uiPhase;
    uint32 m_transitionStep;
    ObjectGuid m_fixateTarget;
    bool m_retargeting = false;
    std::set<uint32> m_eggDbGuids;
    std::map<uint32, TimePoint> m_eggRespawns;
    GuidSet m_hatchlings;

    void Reset() override
    {
        CombatAI::Reset();
        m_uiPhase = PHASE_EGG;
        m_transitionStep = 0;
        m_retargeting = false;
        m_fixateTarget.Clear();
        m_creature->FixateTarget(nullptr);
        m_eggRespawns.clear();
        ClearHatchlings(false);
        ClearPlague();
        SetCombatMovement(true);
        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
    }

    void JustReachedHome() override
    {
        CombatAI::JustReachedHome();
        m_creature->SetHealth(m_creature->GetMaxHealth());
        for (uint32 guid : m_eggDbGuids)
            RestoreEgg(guid);
    }

    void Aggro(Unit* /*who*/) override
    {
        CollectEggs();
        for (uint32 guid : m_eggDbGuids)
            RestoreEgg(guid);
        DoCastSpellIfCan(nullptr, SPELL_THORNS);
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        ClearPlague();
        ClearHatchlings(true);
        m_eggRespawns.clear();
        for (uint32 guid : m_eggDbGuids)
            if (Creature* egg = m_creature->GetMap()->GetCreature(guid))
                if (egg->IsAlive())
                    egg->ForcedDespawn();
    }

    void SpellHitTarget(Unit* target, const SpellEntry* spellInfo, SpellMissInfo missInfo) override
    {
        if (spellInfo->Id == SPELL_CREATURE_SPECIAL && target == m_creature && missInfo == SPELL_MISS_NONE && m_retargeting)
            DoAttackNewTarget();
    }

    bool IsActiveEggPhase() const
    {
        return m_uiPhase == PHASE_EGG && m_creature->IsAlive() && m_creature->IsInCombat() && m_creature->GetHealthPercent() > 20.0f;
    }

    void ScheduleNewTarget()
    {
        if (IsActiveEggPhase() && !m_retargeting)
            SetActionReadyStatus(BURU_NEW_TARGET, true);
    }

    bool IsValidFixateTarget(Player const* player) const
    {
        return player && player->IsAlive() && !player->IsGameMaster() && !player->IsFeigningDeathSuccessfully() && m_creature->CanAttack(player);
    }

    void DoAttackNewTarget()
    {
        m_retargeting = false;
        DisableTimer(BURU_RETARGET_FALLBACK);
        SetActionReadyStatus(BURU_NEW_TARGET, false);

        if (!IsActiveEggPhase())
            return;

        Player* previous = nullptr;
        std::vector<Player*> candidates;
        for (HostileReference const* ref : m_creature->getThreatManager().getThreatList())
        {
            Player* player = m_creature->GetMap()->GetPlayer(ref->getUnitGuid());
            if (!IsValidFixateTarget(player))
                continue;

            if (player->GetObjectGuid() == m_fixateTarget)
                previous = player;
            else
                candidates.push_back(player);
        }

        Player* target = candidates.empty() ? previous : candidates[urand(0, candidates.size() - 1)];
        SetCombatMovement(true, true);
        m_creature->FixateTarget(target);
        if (!target)
        {
            m_fixateTarget.Clear();
            return;
        }

        m_creature->RemoveAurasDueToSpell(SPELL_FULL_SPEED);
        m_creature->RemoveAurasDueToSpell(SPELL_GATHERING_SPEED);
        ResetCombatAction(BURU_FULL_SPEED, 60000);
        ResetCombatAction(BURU_GATHERING_SPEED, 9000);
        AttackStart(target);
        if (target->GetObjectGuid() != m_fixateTarget)
            DoScriptText(EMOTE_TARGET, m_creature, target, CHAT_TYPE_BOSS_EMOTE);
        m_fixateTarget = target->GetObjectGuid();
    }

    void RegisterEgg(Creature const* egg)
    {
        if (egg->HasStaticDBSpawnData())
            m_eggDbGuids.insert(egg->GetDbGuid());
    }

    void EggDied(Creature const* egg)
    {
        RegisterEgg(egg);
        if (IsActiveEggPhase() && egg->HasStaticDBSpawnData())
            m_eggRespawns[egg->GetDbGuid()] = m_creature->GetMap()->GetCurrentClockTime() + 120s;
    }

    void HatchlingSummoned(Creature* hatchling)
    {
        if (!m_hatchlings.insert(hatchling->GetObjectGuid()).second)
            return;

        hatchling->SetInCombatWithZone();
        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
            hatchling->AI()->AttackStart(target);
    }

    void CollectEggs()
    {
        CreatureList eggs;
        GetCreatureListWithEntryInGrid(eggs, m_creature, NPC_BURU_EGG, 150.0f);
        for (Creature const* egg : eggs)
            RegisterEgg(egg);
    }

    void RestoreEgg(uint32 guid)
    {
        Map* map = m_creature->GetMap();
        if (Creature* egg = map->GetCreature(guid))
        {
            if (!egg->IsAlive())
                egg->Respawn();
            return;
        }

        if (CreatureData const* data = sObjectMgr.GetCreatureData(guid))
        {
            map->GetPersistentState()->SaveCreatureRespawnTime(guid, 0);
            if (!map->IsLoaded(data->posX, data->posY))
                map->LoadGrid(data->posX, data->posY);
            if (!map->GetCreature(guid))
                WorldObject::SpawnCreature(guid, map);
        }
    }

    void ClearHatchlings(bool keepFighting)
    {
        for (ObjectGuid guid : m_hatchlings)
            if (Creature* hatchling = m_creature->GetMap()->GetCreature(guid))
                if (!keepFighting || !hatchling->IsInCombat())
                    hatchling->ForcedDespawn();
        m_hatchlings.clear();
    }

    void ClearPlague()
    {
        for (auto const& ref : m_creature->GetMap()->GetPlayers())
            if (Player* player = ref.getSource())
                player->RemoveAurasByCasterSpell(SPELL_CREEPING_PLAGUE, m_creature->GetObjectGuid());
    }

    void UpdateAI(uint32 diff) override
    {
        if (IsActiveEggPhase())
        {
            const ThreatList& threats = m_creature->getThreatManager().getThreatList();
            if (!IsValidFixateTarget(m_creature->GetMap()->GetPlayer(m_fixateTarget)) ||
                std::none_of(threats.begin(), threats.end(), [this](HostileReference const* ref) { return ref->getUnitGuid() == m_fixateTarget; }))
                DoAttackNewTarget();

            const TimePoint now = m_creature->GetMap()->GetCurrentClockTime();
            for (auto itr = m_eggRespawns.begin(); itr != m_eggRespawns.end(); )
            {
                if (itr->second > now)
                {
                    ++itr;
                    continue;
                }

                const uint32 guid = itr->first;
                itr = m_eggRespawns.erase(itr);
                RestoreEgg(guid);
            }
        }
        CombatAI::UpdateAI(diff);
    }

    void HandlePhaseTwo()
    {
        switch (m_transitionStep)
        {
            case 0:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_BURU_TRANSFORM, CAST_TRIGGERED) != CAST_OK)
                {
                    ResetTimer(BURU_PHASE_2_TRANSITION_STEP, 1000);
                    return;
                }
                m_creature->RemoveAurasDueToSpell(SPELL_THORNS);
                ResetTimer(BURU_PHASE_2_TRANSITION_STEP, 5000);
                break;
            }
            case 1:
            {
                DoCastSpellIfCan(nullptr, SPELL_FULL_SPEED, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
                SetCombatScriptStatus(false);
                SetMeleeEnabled(true);
                SetCombatMovement(true, true);
                ResetCombatAction(BURU_CREEPING_PLAGUE, 0);
                break;
            }
        }
        ++m_transitionStep;
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case BURU_PHASE_2_TRANSITION:
            {
                if (m_creature->GetHealthPercent() <= 20.0f)
                {
                    SetActionReadyStatus(action, false);
                    m_uiPhase = PHASE_TRANSFORM;
                    m_eggRespawns.clear();
                    m_retargeting = false;
                    DisableTimer(BURU_RETARGET_FALLBACK);
                    m_fixateTarget.Clear();
                    m_creature->FixateTarget(nullptr);
                    DisableCombatAction(BURU_DISMEMBER);
                    SetActionReadyStatus(BURU_NEW_TARGET, false);
                    DisableCombatAction(BURU_GATHERING_SPEED);
                    DisableCombatAction(BURU_FULL_SPEED);
                    m_creature->RemoveAurasDueToSpell(SPELL_GATHERING_SPEED);
                    m_creature->RemoveAurasDueToSpell(SPELL_FULL_SPEED);
                    DoResetThreat();
                    ResetTimer(BURU_PHASE_2_TRANSITION_STEP, 2000);
                    SetCombatScriptStatus(true);
                    SetMeleeEnabled(false);
                    SetCombatMovement(false, true);
                    m_creature->SetTarget(nullptr);
                }
                break;
            }
            case BURU_NEW_TARGET:
            {
                m_retargeting = true;
                SetActionReadyStatus(action, false);
                if (DoCastSpellIfCan(nullptr, SPELL_CREATURE_SPECIAL) == CAST_OK)
                {
                    if (m_retargeting)
                    {
                        SetCombatMovement(false, true);
                        ResetTimer(BURU_RETARGET_FALLBACK, 3000);
                    }
                }
                else
                    DoAttackNewTarget();
                break;
            }
            case BURU_DISMEMBER:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_DISMEMBER) == CAST_OK)
                    ResetCombatAction(action, 5000);
                break;
            }
            case BURU_CREEPING_PLAGUE:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_CREEPING_PLAGUE) == CAST_OK)
                    ResetCombatAction(action, 6000);
                break;
            }
            case BURU_GATHERING_SPEED:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_GATHERING_SPEED) == CAST_OK)
                    ResetCombatAction(action, 9000);
                break;
            }
            case BURU_FULL_SPEED:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_FULL_SPEED) == CAST_OK)
                {
                    m_creature->RemoveAurasDueToSpell(SPELL_GATHERING_SPEED);
                    DisableCombatAction(action);
                    DisableCombatAction(BURU_GATHERING_SPEED);
                }
                break;
            }
        }
    }
};

struct npc_buru_eggAI : public Scripted_NoMovementAI
{
    npc_buru_eggAI(Creature* creature) : Scripted_NoMovementAI(creature), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        m_creature->SetCorpseDelay(5);
    }

    ScriptedInstance* m_instance;
    ObjectGuid m_triggerGuid;

    boss_buruAI* GetBuruAI() const
    {
        if (m_instance)
            if (Creature* buru = m_instance->GetSingleCreatureFromStorage(NPC_BURU, true))
                return dynamic_cast<boss_buruAI*>(buru->AI());
        return nullptr;
    }

    void Reset() override
    {
        SetCombatMovement(false);
        SetReactState(REACT_PASSIVE);
        m_triggerGuid.Clear();
        if (boss_buruAI* buru = GetBuruAI())
            buru->RegisterEgg(m_creature);
    }

    void DamageTaken(Unit* dealer, uint32& damage, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        Scripted_NoMovementAI::DamageTaken(dealer, damage, damageType, spellInfo);
        if (!damage || !dealer || !dealer->GetBeneficiaryPlayer() || !m_instance || m_instance->GetData(TYPE_BURU) == DONE)
            return;

        if (Creature* buru = m_instance->GetSingleCreatureFromStorage(NPC_BURU, true))
            if (buru->IsAlive() && !buru->IsInCombat() && buru->AI() && buru->CanAttack(dealer))
                buru->AI()->AttackStart(dealer);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_BURU_EGG_TRIGGER && summoned->GetObjectGuid() != m_triggerGuid)
        {
            m_triggerGuid = summoned->GetObjectGuid();
            summoned->CastSpell(summoned, SPELL_BURU_EGG_TRIGGER, TRIGGERED_OLD_TRIGGERED);
        }
        else if (summoned->GetEntry() == NPC_HATCHLING)
            if (boss_buruAI* buru = GetBuruAI())
                buru->HatchlingSummoned(summoned);
    }

    void SpellHitTarget(Unit* target, const SpellEntry* spellInfo, SpellMissInfo missInfo) override
    {
        if (!target || missInfo != SPELL_MISS_NONE)
            return;

        boss_buruAI* buru = target->GetEntry() == NPC_BURU ? dynamic_cast<boss_buruAI*>(target->AI()) : nullptr;
        if (spellInfo->Id == SPELL_EXPLOSION && buru)
        {
            buru->ScheduleNewTarget();
            return;
        }

        if (spellInfo->Id != SPELL_EXPLODE || (!target->IsPlayer() && (!buru || !buru->IsActiveEggPhase())))
            return;

        int32 damage = BURU_EGG_DAMAGE;
        if (target->IsPlayer())
        {
            const float distance = std::min(m_creature->GetDistance(target, true, DIST_CALC_COMBAT_REACH), 25.0f);
            damage = int32(500.0f - 16.0f * distance);
        }
        m_creature->CastCustomSpell(target, SPELL_EXPLOSION, &damage, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
    }

    void JustDied(Unit* killer) override
    {
        Scripted_NoMovementAI::JustDied(killer);
        if (boss_buruAI* buru = GetBuruAI())
            buru->EggDied(m_creature);
        DoCastSpellIfCan(nullptr, SPELL_EXPLODE);
        DoCastSpellIfCan(nullptr, SPELL_SUMMON_HATCHLING, CAST_TRIGGERED);
    }

    void UpdateAI(const uint32 /*uiDiff*/) override { }
};

void AddSC_boss_buru()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_buru";
    pNewScript->GetAI = &GetNewAIInstance<boss_buruAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_buru_egg";
    pNewScript->GetAI = &GetNewAIInstance<npc_buru_eggAI>;
    pNewScript->RegisterSelf();
}
