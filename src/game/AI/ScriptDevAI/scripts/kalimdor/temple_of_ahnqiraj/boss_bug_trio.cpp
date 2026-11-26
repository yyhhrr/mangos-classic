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
SDName: bug_trio
SD%Complete: 99
SDComment: Summon Player spell NYI (when current target is unreachable)
SDCategory: Temple of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "temple_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Spells/SpellAuras.h"

enum
{
    EMOTE_CONSUMED          = -1531048,

    SPELL_BLOODY_DEATH      = 25770,
    SPELL_FULL_HEAL         = 17683,

    // Lord Kri
    SPELL_CLEAVE            = 26350,
    SPELL_TOXIC_VOLLEY      = 25812,
    SPELL_SUMMON_CLOUD      = 26590,            // summons 15933
    SPELL_THRASH            = 3391,

    // Vem
    SPELL_KNOCK_AWAY        = 18670,
    SPELL_KNOCKDOWN         = 19128,
    SPELL_CHARGE            = 26561,
    SPELL_VENGEANCE         = 25790,

    // Princess Yauj
    SPELL_HEAL              = 25807,
    SPELL_FEAR              = 26580,
    SPELL_RAVAGE            = 3242,
    SPELL_SUMMON_BROOD      = 25789,
    SPELL_DISPEL            = 25808,

    POINT_CONSUME           = 0,
};

static const Position resetPoint = { -8582.0f, 2047.0f, -1.62f, 0.0 };

enum RoyaltyActions
{
    ROYALTY_DEVOUR_DELAY = 10,
};

struct boss_silithidRoyaltyAI : public CombatAI
{
    boss_silithidRoyaltyAI(Creature* creature, uint32 actionCount, uint32 deathAbility) :
        CombatAI(creature, actionCount),
        m_instance(static_cast<instance_temple_of_ahnqiraj*>(creature->GetInstanceData())),
        m_deathAbility(deathAbility),
        m_corpseDelay(creature->GetCorpseDelay())
    {
        AddCustomAction(ROYALTY_DEVOUR_DELAY, true, [&]() { HandleDevourDelay(); });
        m_creature->GetCombatManager().SetLeashingCheck([&](Unit* unit, float x, float y, float z) -> bool
        {
            return m_creature->GetDistance(resetPoint.x, resetPoint.y, resetPoint.z, DIST_CALC_COMBAT_REACH) < 10.0f;
        });
    }

    instance_temple_of_ahnqiraj* m_instance;
    uint32 const m_deathAbility;
    uint32 const m_corpseDelay;
    bool m_movingToCorpse = false;
    bool m_wasConsumed = false;

    void Reset() override
    {
        CombatAI::Reset();
        DisableTimer(ROYALTY_DEVOUR_DELAY);
        if (m_movingToCorpse)
            m_creature->SetBaseRunSpeed(15.f / 7.f);
        m_movingToCorpse = false;
        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        if (m_wasConsumed)
        {
            m_creature->SetNoLoot(false);
            m_creature->SetCorpseDelay(m_corpseDelay);
            m_wasConsumed = false;
        }
    }

    void Aggro(Unit* /*who*/) override
    {
        if (m_instance)
            m_instance->SetData(TYPE_BUG_TRIO, IN_PROGRESS);
    }

    void EnterEvadeMode() override
    {
        if (m_instance && m_instance->GetData(TYPE_BUG_TRIO) == IN_PROGRESS)
            m_instance->SetData(TYPE_BUG_TRIO, FAIL);
        else
            CombatAI::EnterEvadeMode();
    }

    bool HandleLethalDamage(Unit* /*dealer*/) override
    {
        if (GetCombatScriptStatus())
            return true;

        if (m_instance && m_instance->IsBugTrioDeathConsumed())
            m_creature->CastSpell(nullptr, SPELL_BLOODY_DEATH, TRIGGERED_OLD_TRIGGERED);
        return !m_creature->IsAlive();
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        if (m_instance && m_instance->IsBugTrioDeathConsumed())
        {
            m_wasConsumed = true;
            m_creature->SetNoLoot(true);
            m_creature->SetCorpseDelay(3);
            m_creature->SetStandState(UNIT_STAND_STATE_DEAD);
            DoScriptText(EMOTE_CONSUMED, m_creature);
        }
        m_creature->CastSpell(nullptr, m_deathAbility, TRIGGERED_OLD_TRIGGERED);
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType == POINT_MOTION_TYPE && pointId == POINT_CONSUME && m_movingToCorpse)
            StartEating();
    }

    void StartEating()
    {
        m_movingToCorpse = false;
        m_creature->SetBaseRunSpeed(15.f / 7.f); // sniffed
        m_creature->CastSpell(nullptr, SPELL_FULL_HEAL, TRIGGERED_OLD_TRIGGERED);
        if (!m_creature->getThreatManager().isThreatListEmpty())
            DoResetThreat();
        ResetTimer(ROYALTY_DEVOUR_DELAY, 2000);
    }

    void HandleDevourDelay()
    {
        if (m_movingToCorpse)
        {
            // A blocked point movement may never report arrival.
            StartEating();
            if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
                m_creature->GetMotionMaster()->Clear();
            return;
        }

        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true, true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (GetCombatScriptStatus() && m_creature->getThreatManager().isThreatListEmpty())
        {
            EnterEvadeMode();
            return;
        }
        CombatAI::UpdateAI(diff);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* defeated, uint32 /*miscValue*/) override
    {
        if (eventType != AI_EVENT_CUSTOM_A)
            return;

        m_movingToCorpse = true;
        m_creature->InterruptNonMeleeSpells(false);
        SetCombatScriptStatus(true);
        SetMeleeEnabled(false);
        SetCombatMovement(false);
        m_creature->SetTarget(nullptr);
        m_creature->SetBaseRunSpeed(45.f / 7.f); // sniffed
        float x, y, z;
        defeated->GetClosePoint(x, y, z, 0.f);
        m_creature->GetMotionMaster()->MovePoint(POINT_CONSUME, x, y, z);
        ResetTimer(ROYALTY_DEVOUR_DELAY, 5000);
    }
};

enum KriActions
{
    KRI_CLEAVE,
    KRI_TOXIC_VOLLEY,
    KRI_THRASH,
    KRI_ACTION_MAX,
};

struct boss_kriAI : public boss_silithidRoyaltyAI
{
    boss_kriAI(Creature* creature) : boss_silithidRoyaltyAI(creature, KRI_ACTION_MAX, SPELL_SUMMON_CLOUD)
    {
        AddCombatAction(KRI_CLEAVE, 4000, 8000);
        AddCombatAction(KRI_TOXIC_VOLLEY, 6000, 30000);
        AddCombatAction(KRI_THRASH, 6u * IN_MILLISECONDS);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case KRI_CLEAVE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CLEAVE) == CAST_OK)
                    ResetCombatAction(action, urand(5, 12) * IN_MILLISECONDS);
                break;
            }
            case KRI_TOXIC_VOLLEY:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_TOXIC_VOLLEY) == CAST_OK)
                    ResetCombatAction(action, urand(15, 25) * IN_MILLISECONDS);
                break;
            }
            case KRI_THRASH:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_THRASH) == CAST_OK)
                    ResetCombatAction(action, urand(2, 6) * IN_MILLISECONDS);
                break;
            }
        }
    }
};

enum VemActions
{
    VEM_CHARGE,
    VEM_KNOCK_AWAY,
    VEM_KNOCK_DOWN,
    VEM_ACTION_MAX,
};

struct boss_vemAI : public boss_silithidRoyaltyAI
{
    boss_vemAI(Creature* creature) : boss_silithidRoyaltyAI(creature, VEM_ACTION_MAX, SPELL_VENGEANCE)
    {
        AddCombatAction(VEM_CHARGE, urand(15, 27) * IN_MILLISECONDS);
        AddCombatAction(VEM_KNOCK_AWAY, urand(10, 20) * IN_MILLISECONDS);
        AddCombatAction(VEM_KNOCK_DOWN, urand(5, 8) * IN_MILLISECONDS);
    }

    void Reset() override
    {
        boss_silithidRoyaltyAI::Reset();
        m_creature->SetWalk(false);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case VEM_CHARGE:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_CHARGE, SELECT_FLAG_NOT_IN_MELEE_RANGE | SELECT_FLAG_PLAYER))
                    if (DoCastSpellIfCan(target, SPELL_CHARGE) == CAST_OK)
                        ResetCombatAction(action, urand(8, 16) * IN_MILLISECONDS);
                break;
            }
            case VEM_KNOCK_AWAY:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_KNOCK_AWAY) == CAST_OK)
                    ResetCombatAction(action, urand(10, 20) * IN_MILLISECONDS);
                break;
            }
            case VEM_KNOCK_DOWN:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_KNOCKDOWN) == CAST_OK)
                    ResetCombatAction(action, urand(15, 20) * IN_MILLISECONDS);
                break;
            }
        }
    }
};

enum YaujActions
{
    YAUJ_HEAL,
    YAUJ_DISPEL,
    YAUJ_FEAR,
    YAUJ_RAVAGE,
    YAUJ_ACTION_MAX,
};

struct boss_yaujAI : public boss_silithidRoyaltyAI
{
    boss_yaujAI(Creature* creature) : boss_silithidRoyaltyAI(creature, YAUJ_ACTION_MAX, SPELL_SUMMON_BROOD)
    {
        AddCombatAction(YAUJ_HEAL, urand(20, 30) * IN_MILLISECONDS);
        AddCombatAction(YAUJ_DISPEL, urand(10, 30) * IN_MILLISECONDS);
        AddCombatAction(YAUJ_FEAR, urand(12, 24) * IN_MILLISECONDS);
        AddCombatAction(YAUJ_RAVAGE, 12u * IN_MILLISECONDS);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_YAUJ_BROOD)
            summoned->SetInCombatWithZone();
    }

    std::vector<Creature*> GetLivingRoyalty() const
    {
        std::vector<Creature*> targets{ m_creature };
        if (m_instance)
            for (uint32 entry : bugTrioEntries)
            {
                if (entry == m_creature->GetEntry())
                    continue;
                if (Creature* ally = m_instance->GetSingleCreatureFromStorage(entry, true))
                    if (ally->IsAlive())
                        targets.push_back(ally);
            }
        return targets;
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case YAUJ_HEAL:
            {
                auto targets = GetLivingRoyalty();
                std::sort(targets.begin(), targets.end(), [](Creature* lhs, Creature* rhs)
                {
                    return lhs->GetHealthPercent() < rhs->GetHealthPercent();
                });
                for (Creature* target : targets)
                {
                    if (target->GetHealth() < target->GetMaxHealth() && DoCastSpellIfCan(target, SPELL_HEAL) == CAST_OK)
                    {
                        ResetCombatAction(action, urand(10, 30) * IN_MILLISECONDS);
                        break;
                    }
                }
                break;
            }
            case YAUJ_FEAR:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_FEAR) == CAST_OK)
                {
                    DoResetThreat();
                    ResetCombatAction(action, 20 * IN_MILLISECONDS);
                }
                break;
            }
            case YAUJ_RAVAGE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_RAVAGE) == CAST_OK)
                    ResetCombatAction(action, urand(10, 15) * IN_MILLISECONDS);
                break;
            }
            case YAUJ_DISPEL:
            {
                SpellEntry const* dispel = sSpellTemplate.LookupEntry<SpellEntry>(SPELL_DISPEL);
                if (!dispel)
                    break;

                uint32 mask = GetDispellMask(dispel->EffectMiscValue[EFFECT_INDEX_0]);
                for (Creature* target : GetLivingRoyalty())
                {
                    auto const& auras = target->GetSpellAuraHolderMap();
                    bool canDispel = std::any_of(auras.begin(), auras.end(), [&](auto const& aura)
                    {
                        SpellAuraHolder const* holder = aura.second;
                        return !holder->IsPositive() && holder->GetId() != 29448 &&
                            holder->IsDispellableByMask(mask, m_creature, dispel);
                    });
                    if (canDispel && DoCastSpellIfCan(target, SPELL_DISPEL) == CAST_OK)
                    {
                        ResetCombatAction(action, urand(10000, 15000));
                        break;
                    }
                }
                break;
            }
        }
    }
};

void AddSC_bug_trio()
{
    Script* newScript = new Script;
    newScript->Name = "boss_kri";
    newScript->GetAI = &GetNewAIInstance<boss_kriAI>;
    newScript->RegisterSelf();

    newScript = new Script;
    newScript->Name = "boss_vem";
    newScript->GetAI = &GetNewAIInstance<boss_vemAI>;
    newScript->RegisterSelf();

    newScript = new Script;
    newScript->Name = "boss_yauj";
    newScript->GetAI = &GetNewAIInstance<boss_yaujAI>;
    newScript->RegisterSelf();
}
