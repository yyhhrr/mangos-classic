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
SDName: Boss_Thekal
SD%Complete: 90
SDComment: Needs a good shake-up.
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Entities/Pet.h"

enum
{
    SAY_TIGER_TRANSFORM     = -1309009,
    SAY_DEATH               = -1309010,

    SAY_DIES_EMOTE          = 10453,

    SPELL_PACIFY_SELF       = 19951,

    SPELL_MORTAL_CLEAVE     = 22859,
    SPELL_SILENCE           = 22666,
    SPELL_FRENZY            = 23128,
    SPELL_FORCE_PUNCH       = 24189,
    SPELL_CHARGE            = 24193,
    SPELL_ENRAGE            = 8269,
    SPELL_SUMMON_TIGERS     = 24183,
    SPELL_TIGER_FORM        = 24169,
    SPELL_RESURRECT         = 24173,
    SPELL_THEKAL_TRIGGER    = 24172,        // TODO: Need to find use for this
    SPELL_BLOODLUST         = 24185,
    SPELL_RESURRECTION_IMPACT_VISUAL = 24171,

    // Zealot Lor'Khan Spells
    SPELL_LIGHTNING_SHIELD  = 20545,
    SPELL_DISPEL_MAGIC      = 17201,
    SPELL_GREAT_HEAL        = 24208,
    SPELL_DISARM            = 6713,

    // Zealot Zath Spells
    SPELL_SWEEPING_STRIKES  = 18765,
    SPELL_SINISTER_STRIKE   = 15581,
    SPELL_GOUGE             = 12540,
    SPELL_KICK              = 15614,
    SPELL_BLIND             = 21060,

    NPC_ZULIAN_GUARDIAN     = 15068,

    SPELL_LIST_PHASE_1      = 1450901,
    SPELL_LIST_PHASE_2      = 1450902,
};

enum ThekalPhase : uint8
{
    PHASE_NORMAL,
    PHASE_FAKE_DEATH,
    PHASE_RESURRECTING,
    PHASE_TIGER_RESURRECTING,
    PHASE_TIGER_TRANSITION,
    PHASE_TIGER
};

enum ThekalActions
{
    THEKAL_TIGER_ENRAGE,
    THEKAL_ACTION_MAX,
    ACTION_RESURRECTION = 10,
    THEKAL_TIGER_TRANSFORM
};

constexpr uint32 THEKAL_RESURRECTION_WINDOW = 10000;
constexpr uint32 THEKAL_RESURRECTION_RETRY = 500;

struct boss_thekalBaseAI : public CombatAI
{
    boss_thekalBaseAI(Creature* creature, uint32 actions, uint32 encounterType)
        : CombatAI(creature, actions), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData())),
          m_encounterType(encounterType)
    {
        SetDeathPrevention(true);
        AddCustomAction(ACTION_RESURRECTION, true, [this]() { TryResurrect(); }, TIMER_COMBAT_COMBAT);
    }

    ScriptedInstance* m_instance;
    uint32 const m_encounterType;
    ThekalPhase m_uiPhase = PHASE_NORMAL;

    void Reset() override
    {
        CombatAI::Reset();
        DisableTimer(ACTION_RESURRECTION);
        m_uiPhase = PHASE_NORMAL;
        RestoreStanding();
        ResetDeathPrevented();
        SetDeathPrevention(true);
        ResumeCombat();
        if (m_instance && (m_encounterType != TYPE_THEKAL || m_instance->GetData(TYPE_THEKAL) != DONE))
            m_instance->SetData(m_encounterType, NOT_STARTED);
    }

    void Aggro(Unit* /*who*/) override
    {
        if (m_instance)
            m_instance->SetData(m_encounterType, IN_PROGRESS);
    }

    bool IsActivePull() const
    {
        return m_creature->IsAlive() && m_creature->IsInCombat() && GetAIOrder() != ORDER_EVADE && HasCouncilThreat();
    }

    boss_thekalBaseAI* GetCouncilAI(uint32 entry) const
    {
        if (m_instance)
            if (Creature* member = m_instance->GetSingleCreatureFromStorage(entry))
                return dynamic_cast<boss_thekalBaseAI*>(member->AI());
        return nullptr;
    }

    bool HasCouncilThreat() const
    {
        if (!m_creature->getThreatManager().isThreatListEmpty())
            return true;

        for (uint32 entry : {NPC_THEKAL, NPC_LORKHAN, NPC_ZATH})
            if (auto* member = GetCouncilAI(entry))
                if (member->m_creature->IsAlive() && !member->m_creature->getThreatManager().isThreatListEmpty())
                    return true;
        return false;
    }

    void UpdateAI(uint32 diff) override
    {
        // The core defers ordinary evasion while a combat script is active.
        if (m_uiPhase != PHASE_NORMAL && m_uiPhase != PHASE_TIGER && !HasCouncilThreat())
        {
            if (auto* thekal = GetCouncilAI(NPC_THEKAL))
                thekal->EnterEvadeMode();
            else
                EnterEvadeMode();
            return;
        }

        CombatAI::UpdateAI(diff);
    }

    bool IsWithinResurrectionWindow() const
    {
        return m_uiPhase == PHASE_FAKE_DEATH && IsActivePull();
    }

    virtual void CheckForTigerPhase() {}
    virtual void OnTigerResurrect() {}

    void JustPreventedDeath(Unit* /*attacker*/) override
    {
        if (m_uiPhase != PHASE_NORMAL)
            return;

        m_creature->InterruptNonMeleeSpells(true);
        m_creature->StopMoving();
        m_creature->SetTarget(nullptr);
        m_creature->ClearComboPointHolders();
        m_creature->RemoveAllAurasOnDeath();
        m_creature->ModifyAuraState(AURA_STATE_HEALTHLESS_20_PERCENT, false);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->ClearAllReactives();
        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
        m_creature->CastSpell(nullptr, SPELL_PACIFY_SELF, TRIGGERED_OLD_TRIGGERED);

        SetCombatMovement(false);
        SetCombatScriptStatus(true);
        SetMeleeEnabled(false);

        m_uiPhase = PHASE_FAKE_DEATH;
        ResetTimer(ACTION_RESURRECTION, THEKAL_RESURRECTION_WINDOW);
        if (m_instance)
            m_instance->SetData(m_encounterType, SPECIAL);
        if (auto* thekal = GetCouncilAI(NPC_THEKAL))
            thekal->CheckForTigerPhase();
    }

    void TryResurrect()
    {
        if (!IsActivePull())
            return;

        if (m_uiPhase == PHASE_FAKE_DEATH)
            m_uiPhase = PHASE_RESURRECTING;
        if (m_uiPhase != PHASE_RESURRECTING && m_uiPhase != PHASE_TIGER_RESURRECTING)
            return;

        if (!m_creature->FindCurrentSpellBySpellId(SPELL_RESURRECT))
            DoCastSpellIfCan(nullptr, SPELL_RESURRECT);

        // The 24173 effect disarms this timer. A rejected or interrupted cast can retry.
        if (m_uiPhase == PHASE_RESURRECTING || m_uiPhase == PHASE_TIGER_RESURRECTING)
            ResetTimer(ACTION_RESURRECTION, THEKAL_RESURRECTION_RETRY);
    }

    void Revive()
    {
        if (!IsActivePull() || (m_uiPhase != PHASE_RESURRECTING && m_uiPhase != PHASE_TIGER_RESURRECTING))
            return;

        DisableTimer(ACTION_RESURRECTION);
        RestoreStanding();
        m_creature->SetHealth(m_creature->GetMaxHealth());
        if (!m_creature->getThreatManager().isThreatListEmpty())
            DoResetThreat();

        if (m_uiPhase == PHASE_TIGER_RESURRECTING)
        {
            OnTigerResurrect();
            return;
        }

        m_uiPhase = PHASE_NORMAL;
        ResetDeathPrevented();
        ResumeCombat(true);
        SpellListChanged();
        if (m_instance)
            m_instance->SetData(m_encounterType, IN_PROGRESS);
    }

    void RestoreStanding()
    {
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->RemoveAurasDueToSpell(SPELL_PACIFY_SELF);
    }

    void ResumeCombat(bool moveNow = false)
    {
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true, moveNow);
    }
};

struct boss_thekalAI : public boss_thekalBaseAI
{
    explicit boss_thekalAI(Creature* creature) : boss_thekalBaseAI(creature, THEKAL_ACTION_MAX, TYPE_THEKAL)
    {
        AddTimerlessCombatAction(THEKAL_TIGER_ENRAGE, false);
        AddCustomAction(THEKAL_TIGER_TRANSFORM, true, [this]() { TryTigerTransform(); }, TIMER_COMBAT_COMBAT);
    }

    void Reset() override
    {
        boss_thekalBaseAI::Reset();
        DisableTimer(THEKAL_TIGER_TRANSFORM);
        SetActionReadyStatus(THEKAL_TIGER_ENRAGE, false);
        m_forcePunchTarget.Clear();
        RemoveZulianGuardians();
        m_creature->RemoveAurasDueToSpell(SPELL_TIGER_FORM);
        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
    }

    void CheckForTigerPhase() override
    {
        if (!IsWithinResurrectionWindow())
            return;

        auto* lorkhan = GetCouncilAI(NPC_LORKHAN);
        auto* zath = GetCouncilAI(NPC_ZATH);
        if (!lorkhan || !zath || !lorkhan->IsWithinResurrectionWindow() || !zath->IsWithinResurrectionWindow())
            return;

        m_uiPhase = PHASE_TIGER_RESURRECTING;
        lorkhan->DisableTimer(ACTION_RESURRECTION);
        zath->DisableTimer(ACTION_RESURRECTION);
        ResetTimer(ACTION_RESURRECTION, 1000);
    }

    void OnTigerResurrect() override
    {
        m_uiPhase = PHASE_TIGER_TRANSITION;
        DoCastSpellIfCan(nullptr, SPELL_RESURRECTION_IMPACT_VISUAL, CAST_TRIGGERED);
        DoScriptText(SAY_TIGER_TRANSFORM, m_creature);
        ResetTimer(THEKAL_TIGER_TRANSFORM, 5000);
    }

    void TryTigerTransform()
    {
        if (!IsActivePull() || m_uiPhase != PHASE_TIGER_TRANSITION)
            return;

        if (!m_creature->HasAura(SPELL_TIGER_FORM))
            DoCastSpellIfCan(nullptr, SPELL_TIGER_FORM);
        if (!m_creature->HasAura(SPELL_TIGER_FORM))
        {
            ResetTimer(THEKAL_TIGER_TRANSFORM, THEKAL_RESURRECTION_RETRY);
            return;
        }

        m_uiPhase = PHASE_TIGER;
        SetDeathPrevention(false);
        m_creature->SetSpellList(SPELL_LIST_PHASE_2);
        SetActionReadyStatus(THEKAL_TIGER_ENRAGE, true);
        ResumeCombat(true);
        if (m_instance)
            m_instance->SetData(TYPE_THEKAL, IN_PROGRESS);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_ZULIAN_GUARDIAN && summoned->IsPet())
            m_zulianGuardians.push_back(summoned->GetObjectGuid());
    }

    void RemoveZulianGuardians()
    {
        for (ObjectGuid guid : m_zulianGuardians)
            if (Pet* guardian = m_creature->GetMap()->GetPet(guid))
                guardian->Unsummon(PET_SAVE_AS_DELETED, m_creature);
        m_zulianGuardians.clear();
    }

    void OnSpellCast(SpellEntry const* spellInfo, Unit* /*target*/) override
    {
        if (spellInfo->Id == SPELL_FORCE_PUNCH)
            m_forcePunchTarget = m_creature->GetVictim() ? m_creature->GetVictim()->GetObjectGuid() : ObjectGuid();
    }

    void DamageDeal(Unit* target, uint32& /*damage*/, DamageEffectType damageType, SpellEntry const* spellInfo) override
    {
        if (m_uiPhase != PHASE_TIGER || damageType != SPELL_DIRECT_DAMAGE || !spellInfo ||
            spellInfo->Id != SPELL_FORCE_PUNCH || target->GetObjectGuid() != m_forcePunchTarget)
            return;

        // This hook runs only for a landed damage effect, including fully absorbed hits.
        m_forcePunchTarget.Clear();
        m_creature->getThreatManager().modifyThreatPercent(target, -100);
    }

    void JustDied(Unit* /*killer*/) override
    {
        RemoveZulianGuardians();
        DoScriptText(SAY_DEATH, m_creature);

        if (!m_instance)
            return;

        m_instance->SetData(TYPE_THEKAL, DONE);
        for (uint32 entry : {NPC_LORKHAN, NPC_ZATH})
        {
            if (Creature* zealot = m_instance->GetSingleCreatureFromStorage(entry))
            {
                DoBroadcastText(SAY_DIES_EMOTE, zealot);
                zealot->ForcedDespawn(1000);
            }
        }
    }

    void JustReachedHome() override
    {
        if (m_instance && m_instance->GetData(TYPE_THEKAL) != DONE)
            m_instance->SetData(TYPE_THEKAL, FAIL);
    }

    void EnterEvadeMode() override
    {
        CombatAI::EnterEvadeMode();
        for (uint32 entry : {NPC_LORKHAN, NPC_ZATH})
            if (auto* zealot = GetCouncilAI(entry))
                zealot->EnterEvadeMode();
    }

    void ExecuteAction(uint32 action) override
    {
        if (action == THEKAL_TIGER_ENRAGE && m_creature->GetHealthPercent() < 20.0f)
            if (DoCastSpellIfCan(nullptr, SPELL_ENRAGE) == CAST_OK)
                DisableCombatAction(action);
    }

    std::vector<ObjectGuid> m_zulianGuardians;
    ObjectGuid m_forcePunchTarget;
};

struct mob_zealot_lorkhanAI : public boss_thekalBaseAI
{
    explicit mob_zealot_lorkhanAI(Creature* creature) : boss_thekalBaseAI(creature, 0, TYPE_LORKHAN) {}
};

struct mob_zealot_zathAI : public boss_thekalBaseAI
{
    explicit mob_zealot_zathAI(Creature* creature) : boss_thekalBaseAI(creature, 0, TYPE_ZATH) {}
};

struct ThekalResurrect : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return;
        if (Unit* caster = spell->GetCaster())
            if (auto* fakerAI = dynamic_cast<boss_thekalBaseAI*>(caster->AI()))
                fakerAI->Revive();
    }
};

void AddSC_boss_thekal()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_thekal";
    pNewScript->GetAI = &GetNewAIInstance<boss_thekalAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_zealot_lorkhan";
    pNewScript->GetAI = &GetNewAIInstance<mob_zealot_lorkhanAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_zealot_zath";
    pNewScript->GetAI = &GetNewAIInstance<mob_zealot_zathAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ThekalResurrect>("spell_thekal_resurrect");
}
