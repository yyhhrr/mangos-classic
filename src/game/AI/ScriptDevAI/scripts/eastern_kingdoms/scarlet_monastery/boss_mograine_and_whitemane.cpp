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
SDName: Boss_Mograine_And_Whitemane
SD%Complete: 95
SDComment:
SDCategory: Scarlet Monastery
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "Globals/SharedDefines.h"
#include "scarlet_monastery.h"
#include "AI/ScriptDevAI/base/CombatAI.h"

enum
{
    // Mograine says
    SAY_MO_AGGRO                 = -1189005,
    SAY_MO_KILL                  = -1189006,
    SAY_MO_RESSURECTED           = -1189007,

    // Whitemane says
    SAY_WH_INTRO                 = -1189008,
    SAY_WH_KILL                  = -1189009,
    SAY_WH_RESSURECT             = -1189010,

    // Mograine Spells
    SPELL_CRUSADERSTRIKE         = 14518,
    SPELL_HAMMEROFJUSTICE        = 5589,
    SPELL_LAYONHANDS             = 9257,
    SPELL_RETRIBUTIONAURA        = 8990,
    SPELL_DIVINESHIELD           = 642,

    // Whitemanes Spells
    SPELL_DEEPSLEEP              = 9256,
    SPELL_SCARLETRESURRECTION    = 9232,
    SPELL_DOMINATEMIND           = 14515,
    SPELL_HOLYSMITE              = 9481,
    SPELL_HEAL                   = 12039,
    SPELL_POWERWORDSHIELD        = 22187,

    NPC_SCARLET_ABBOT               = 4303,
    NPC_SCARLET_MONK                = 4540,
    NPC_SCARLET_CHAMPION            = 4302,
    NPC_SCARLET_WIZARD              = 4300,
    NPC_SCARLET_CENTURION           = 4301,
    NPC_SCARLET_CHAPLAIN            = 4299,

    ASHBRINGER_RELAY_SCRIPT_ID   = 9001,
    SPELL_AB_EFFECT_000          = 28441,

    SOUND_MOGRAINE_FAKE_DEATH    = 1326,
};

enum MograineActions
{
    MOGRAINE_ACTION_CRUSADER_STRIKE,
    MOGRAINE_ACTION_HAMMER_OF_JUSTICE,
    MOGRAINE_ACTION_DIVINE_SHIELD,
    MOGRAINE_ACTION_MAX,
    MOGRAINE_ACTION_LAY_ON_HANDS,
    MOGRAINE_ACTION_REVIVED,
    MOGRAINE_ACTION_ROAR,
};

struct boss_scarlet_commander_mograineAI : public CombatAI
{
    boss_scarlet_commander_mograineAI(Creature* creature) : CombatAI(creature, MOGRAINE_ACTION_MAX), m_instance(static_cast<instance_scarlet_monastery*>(creature->GetInstanceData()))
    {
        AddCombatAction(MOGRAINE_ACTION_CRUSADER_STRIKE, 8400u);
        AddCombatAction(MOGRAINE_ACTION_HAMMER_OF_JUSTICE, 9600u);
        AddCombatAction(MOGRAINE_ACTION_DIVINE_SHIELD, 40000u);
        AddCustomAction(MOGRAINE_ACTION_LAY_ON_HANDS, false, [&]()
        {
            HandleLayOnHandsTimer(); 
        }, TIMER_COMBAT_COMBAT);
        AddCustomAction(MOGRAINE_ACTION_REVIVED, true, [&]()
        {
            HandleRevivedTimer();
        });
        AddCustomAction(MOGRAINE_ACTION_ROAR, true, [&]()
        {
            HandleRoar();
        });
        Reset();
    }

    instance_scarlet_monastery* m_instance;

    bool m_bHasDied;
    bool m_bHeal;

    void Reset() override
    {
        CombatAI::Reset();
        m_bHasDied = false;
        m_bHeal = false;

        // Incase wipe during phase that mograine fake death
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        ResetDeathPrevented();
        SetDeathPrevention(true);
        SetCombatScriptStatus(false);
        SetCombatMovement(true);
        SetMeleeEnabled(true);
        SetReactState(REACT_AGGRESSIVE);
        DisableTimer(MOGRAINE_ACTION_LAY_ON_HANDS);
        DisableTimer(MOGRAINE_ACTION_REVIVED);
        DisableTimer(MOGRAINE_ACTION_ROAR);

        // A truly killed partner stays dead; only the surviving boss respawns.
        if (m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == FAIL &&
            m_instance->IsBossDefeated(NPC_WHITEMANE))
        {
            m_bHasDied = m_bHeal = true;
            SetDeathPrevention(false);
            SetActionReadyStatus(MOGRAINE_ACTION_DIVINE_SHIELD, true);
        }
    }

    uint32 GetSubsequentActionTimer(uint32 id)
    {
        switch (id)
        {
            case MOGRAINE_ACTION_CRUSADER_STRIKE: return urand(6000, 15000);
            case MOGRAINE_ACTION_HAMMER_OF_JUSTICE: return urand(7000, 18500);
            default: return 0; // never occurs but for compiler
        }
    }

    void EnterEvadeMode() override
    {
        if (m_instance)
        {
            const uint32 state = m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT);
            if (state == IN_PROGRESS || state == SPECIAL)
            {
                m_instance->SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, FAIL);
                return;
            }
        }
        CombatAI::EnterEvadeMode();
    }

    void Aggro(Unit* /*pWho*/) override
    {
        DoScriptText(SAY_MO_AGGRO, m_creature);
        DoCastSpellIfCan(m_creature, SPELL_RETRIBUTIONAURA);

        MograineCallForHelp();
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        DoScriptText(SAY_MO_KILL, m_creature);
    }

    void JustPreventedDeath(Unit* /*attacker*/) override
    {
        if (!m_instance || m_bHasDied || m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == DONE)
            return;

        if (m_instance->IsBossDefeated(NPC_WHITEMANE))
        {
            SetDeathPrevention(false);
            return;
        }
        Creature* pWhitemane = m_instance->GetSingleCreatureFromStorage(NPC_WHITEMANE);
        if (!pWhitemane || !pWhitemane->IsAlive())
        {
            // Forced despawn/unloaded is not a kill. Keep protection and retry
            // on the next lethal hit after the real encounter partner returns.
            ResetDeathPrevented();
            return;
        }
        {
            m_instance->SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, IN_PROGRESS);

            pWhitemane->GetMotionMaster()->MovePoint(1, 1163.113370f, 1398.856812f, 32.527786f, FORCED_MOVEMENT_RUN);
            pWhitemane->AI()->SetCombatScriptStatus(true);

            DoScriptText(SAY_WH_INTRO, pWhitemane);

            m_creature->GetMotionMaster()->MovementExpired();
            m_creature->GetMotionMaster()->MoveIdle();

            if (m_creature->IsNonMeleeSpellCasted(false))
                m_creature->InterruptNonMeleeSpells(false);

            m_creature->ClearComboPointHolders();
            m_creature->RemoveAllAurasOnDeath();
            m_creature->ClearAllReactives();
            m_creature->AttackStop(true, true, true);

            SetCombatScriptStatus(true);
            m_creature->SetTarget(nullptr);

            m_creature->PlayDistanceSound(SOUND_MOGRAINE_FAKE_DEATH);

            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            m_creature->SetStandState(UNIT_STAND_STATE_DEAD);

            m_bHasDied = true;
        }
    }

    void SpellHit(Unit* pWho, const SpellEntry* pSpell) override
    {
        // When hit with ressurection say text
        if (pSpell->Id == SPELL_SCARLETRESURRECTION)
        {
            // Only the encounter's real caster can release this fake-death phase.
            if (!m_instance || !pWho || !m_bHasDied || m_bHeal ||
                m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != IN_PROGRESS ||
                pWho != m_instance->GetSingleCreatureFromStorage(NPC_WHITEMANE))
                return;
            m_instance->SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, SPECIAL);
            ResetTimer(MOGRAINE_ACTION_LAY_ON_HANDS, 3000u);
            pWho->AI()->ReceiveAIEvent(AI_EVENT_CUSTOM_A, m_creature, m_creature, SPELL_SCARLETRESURRECTION);
        }
        // casted by player every 5 seconds while wielding Corrupted Ashbringer
        else if (pSpell->Id == SPELL_AB_EFFECT_000)
        {
            if (m_instance && pWho && m_instance->GetData(TYPE_ASHBRINGER_EVENT) == IN_PROGRESS)
            {
                pWho->GetMap()->ScriptsStart(SCRIPT_TYPE_RELAY, ASHBRINGER_RELAY_SCRIPT_ID, m_creature, pWho);
                m_instance->SetData(TYPE_ASHBRINGER_EVENT, DONE);
            }
        }
    }

    void HandleLayOnHandsTimer()
    {
        if (m_bHasDied && !m_bHeal && m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == SPECIAL)
        {
            if (Creature* pWhitemane = m_instance->GetSingleCreatureFromStorage(NPC_WHITEMANE))
            {
                // On resurrection, stop fake death and heal whitemane and resume fight
                m_creature->SetStandState(UNIT_STAND_STATE_STAND);
                DoScriptText(SAY_MO_RESSURECTED, m_creature);
                SetDeathPrevention(false);

                // spell has script target on Whitemane but sometimes wouldn't work
                m_creature->CastSpell(nullptr, SPELL_LAYONHANDS, TRIGGERED_OLD_TRIGGERED);

                m_creature->GetMotionMaster()->Clear();
                ResetTimer(MOGRAINE_ACTION_ROAR, 500u);

                ResetTimer(MOGRAINE_ACTION_REVIVED, 2000u);

                m_bHeal = true;
            }
        }
    }

    void MograineCallForHelp()
    {
        std::vector<uint32> entries = {NPC_SCARLET_ABBOT, NPC_SCARLET_CENTURION, NPC_SCARLET_CHAMPION, NPC_SCARLET_CHAPLAIN, NPC_SCARLET_MONK, NPC_SCARLET_WIZARD};
        for (uint32 entry : entries)
        {
            CreatureList scarletTrash;
            GetCreatureListWithEntryInGrid(scarletTrash, m_creature, entry, 80.f);
            for (Creature* creature : scarletTrash)
            {
                if (creature && creature->IsAlive())
                {
                    creature->SetInCombatWithZone();
                    creature->AI()->AttackClosestEnemy();
                }
            }
        }
    }

    void HandleRoar()
    {
        m_creature->HandleEmote(EMOTE_ONESHOT_ROAR);
    }

    void HandleRevivedTimer()
    {
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);

        SetActionReadyStatus(MOGRAINE_ACTION_DIVINE_SHIELD, true);

        ResetCombatAction(MOGRAINE_ACTION_CRUSADER_STRIKE, GetSubsequentActionTimer(MOGRAINE_ACTION_CRUSADER_STRIKE));
        ResetCombatAction(MOGRAINE_ACTION_HAMMER_OF_JUSTICE, GetSubsequentActionTimer(MOGRAINE_ACTION_HAMMER_OF_JUSTICE));

        DoCastSpellIfCan(m_creature, SPELL_RETRIBUTIONAURA);

        SetCombatScriptStatus(false);

        if (m_creature->GetVictim())
            m_creature->GetMotionMaster()->MoveChase(m_creature->GetVictim());
    }

    void ExecuteAction(uint32 action) override
    {
        if (!m_instance)
            return;

        switch (action)
        {
            case MOGRAINE_ACTION_CRUSADER_STRIKE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CRUSADERSTRIKE) == CAST_OK)
                    ResetCombatAction(action, GetSubsequentActionTimer(action));
                return;
            }
            case MOGRAINE_ACTION_HAMMER_OF_JUSTICE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HAMMEROFJUSTICE) == CAST_OK)
                    ResetCombatAction(action, GetSubsequentActionTimer(action));
                return;
            }
            case MOGRAINE_ACTION_DIVINE_SHIELD:
            {
                if (m_creature->GetHealthPercent() <= 50.f)
                    if (DoCastSpellIfCan(nullptr, SPELL_DIVINESHIELD) == CAST_OK)
                        DisableCombatAction(action);
                return;
            }
        }
    }
};

enum WhitemaneActions
{
    WHITEMANE_ACTION_DEEP_SLEEP,
    WHITEMANE_ACTION_SCARLET_RESURRECTION,
    WHITEMANE_ACTION_DOMINATE_MIND,
    WHITEMANE_ACTION_HOLY_SMITE,
    WHITEMANE_ACTION_HEAL,
    WHITEMANE_ACTION_POWERWORD_SHIELD,
    WHITEMANE_ACTION_MAX,
    WHITEMANE_ACTION_SALUTE,
    WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT,
    WHITEMANE_ACTION_RESURRECTION_TIMEOUT,
};

struct boss_high_inquisitor_whitemaneAI : public CombatAI
{
    boss_high_inquisitor_whitemaneAI(Creature* creature) : CombatAI(creature, WHITEMANE_ACTION_MAX), m_instance(static_cast<instance_scarlet_monastery*>(creature->GetInstanceData()))
    {
        AddTimerlessCombatAction(WHITEMANE_ACTION_DEEP_SLEEP, true);
        AddCombatAction(WHITEMANE_ACTION_HEAL, true);
        AddCombatAction(WHITEMANE_ACTION_POWERWORD_SHIELD, true);
        AddCombatAction(WHITEMANE_ACTION_HOLY_SMITE, true);
        AddCustomAction(WHITEMANE_ACTION_SCARLET_RESURRECTION, true, [&]()
        {
            HandleResurrection();
        });
        AddCustomAction(WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT, true, [&]()
        {
            HandleResurrectionCombat();
        });
        AddCustomAction(WHITEMANE_ACTION_SALUTE, true, [&]()
        {
            HandleSalute();
        });
        AddCombatAction(WHITEMANE_ACTION_DOMINATE_MIND, true);
        AddCustomAction(WHITEMANE_ACTION_RESURRECTION_TIMEOUT, true, [&]()
        {
            AbortResurrection();
        });
        AddMainSpell(SPELL_HOLYSMITE);
        SetRangedMode(true, 25.f, TYPE_PROXIMITY);
        Reset();
    }

    uint32 GetSubsequentActionTimer(uint32 id)
    {
        switch (id)
        {
            case WHITEMANE_ACTION_HEAL: return 13000u;
            case WHITEMANE_ACTION_POWERWORD_SHIELD: return urand(22000, 45000);
            case WHITEMANE_ACTION_HOLY_SMITE: return GetCurrentRangedMode() ? urand(2000, 3000) : urand(3500, 8000);
            case WHITEMANE_ACTION_DOMINATE_MIND: return urand(5000, 10000);
            default: return 0; // never occurs but for compiler
        }
    }
    instance_scarlet_monastery* m_instance;

    bool m_resurrectionPhase;
    bool m_resurrectionConfirmed;
    uint8 m_resurrectionAttempts;

    void Reset() override
    {
        CombatAI::Reset();
        m_resurrectionPhase = false;
        m_resurrectionConfirmed = false;
        m_resurrectionAttempts = 0;
        DisableTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION);
        DisableTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT);
        DisableTimer(WHITEMANE_ACTION_SALUTE);
        DisableTimer(WHITEMANE_ACTION_RESURRECTION_TIMEOUT);
        DisableCombatAction(WHITEMANE_ACTION_HEAL);
        DisableCombatAction(WHITEMANE_ACTION_POWERWORD_SHIELD);
        DisableCombatAction(WHITEMANE_ACTION_HOLY_SMITE);
        DisableCombatAction(WHITEMANE_ACTION_DOMINATE_MIND);
        SetActionReadyStatus(WHITEMANE_ACTION_DEEP_SLEEP, true);
        ResetDeathPrevented();
        SetDeathPrevention(true);
        SetCombatScriptStatus(false);
        SetCombatMovement(true);
        SetMeleeEnabled(false);
        SetReactState(REACT_PASSIVE);

        if (m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == FAIL &&
            m_instance->IsBossDefeated(NPC_MOGRAINE))
        {
            DisableCombatAction(WHITEMANE_ACTION_DEEP_SLEEP);
            SetDeathPrevention(false);
            BeginCombatActions();
            ResetCombatAction(WHITEMANE_ACTION_DOMINATE_MIND, GetSubsequentActionTimer(WHITEMANE_ACTION_DOMINATE_MIND));
        }
    }

    void JustRespawned() override
    {
        CombatAI::JustRespawned(); // Resets timers and invokes our complete Reset.
    }

    void EnterEvadeMode() override
    {
        if (m_instance)
        {
            const uint32 state = m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT);
            if (state == IN_PROGRESS || state == SPECIAL)
            {
                m_instance->SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, FAIL);
                return;
            }
        }
        CombatAI::EnterEvadeMode();
    }

    void BeginCombatActions()
    {
        SetCombatScriptStatus(false);
        ResetCombatAction(WHITEMANE_ACTION_HEAL, 10000u);
        ResetCombatAction(WHITEMANE_ACTION_POWERWORD_SHIELD, 15000u);
        ResetCombatAction(WHITEMANE_ACTION_HOLY_SMITE, 100u);
        SetMeleeEnabled(true);
        SetReactState(REACT_AGGRESSIVE);
    }

    void EnterCombat(Unit* enemy) override
    {
        CombatAI::EnterCombat(enemy);
        if (m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == IN_PROGRESS && !m_resurrectionPhase)
            BeginCombatActions();
    }

    void MovementInform(uint32 uiMotionType, uint32 uiPointId) override
    {
        if (uiMotionType == POINT_MOTION_TYPE && uiPointId == 2 && m_resurrectionPhase)
        {
            if (m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == IN_PROGRESS)
            {
                if (Creature* pMograine = m_instance->GetSingleCreatureFromStorage(NPC_MOGRAINE))
                {
                    m_creature->SetFacingToObject(pMograine);
                    ResetTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION, 3000u);
                }
            }
        }
        if (uiMotionType == POINT_MOTION_TYPE && uiPointId == 1 && m_instance &&
            m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == IN_PROGRESS && !m_resurrectionPhase)
        {
            SetReactState(REACT_AGGRESSIVE);
            m_creature->SetInCombatWithZone();
            AttackClosestEnemy();
        }
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        DoScriptText(SAY_WH_KILL, m_creature);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* /*invoker*/, uint32 miscValue) override
    {
        if (eventType == AI_EVENT_CUSTOM_A && miscValue == SPELL_SCARLETRESURRECTION && m_resurrectionPhase &&
            m_instance && sender == m_instance->GetSingleCreatureFromStorage(NPC_MOGRAINE) &&
            m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == SPECIAL)
            m_resurrectionConfirmed = true;
    }

    void AbortResurrection()
    {
        if (!m_resurrectionPhase)
            return;
        m_resurrectionPhase = false;
        m_resurrectionConfirmed = false;
        DisableTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION);
        DisableTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT);
        DisableTimer(WHITEMANE_ACTION_SALUTE);
        DisableTimer(WHITEMANE_ACTION_RESURRECTION_TIMEOUT);
        SetCombatScriptStatus(false);
        SetCombatMovement(true);
        if (m_instance && m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != DONE)
            m_instance->SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, FAIL);
        else
            CombatAI::EnterEvadeMode();
    }

    void HandleResurrection()
    {
        if (!m_resurrectionPhase || !m_instance)
            return;
        if (m_resurrectionConfirmed)
            return;
        Creature* mograine = m_instance->GetSingleCreatureFromStorage(NPC_MOGRAINE);
        if (!mograine || !mograine->IsAlive() ||
            m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != IN_PROGRESS || m_resurrectionAttempts >= 3)
        {
            AbortResurrection();
            return;
        }
        ++m_resurrectionAttempts;
        // A failed/interrupted cast never releases either boss's scripted phase.
        if (DoCastSpellIfCan(nullptr, SPELL_SCARLETRESURRECTION) != CAST_OK)
        {
            ResetTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION, 1000u);
            return;
        }
        DoScriptText(SAY_WH_RESSURECT, m_creature);
        ResetTimer(WHITEMANE_ACTION_SALUTE, 3400u);
        m_creature->GetMotionMaster()->Clear();
        ResetTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT, 5700u);
    }

    void HandleResurrectionCombat()
    {
        if (!m_resurrectionPhase || !m_instance)
            return;
        Creature* mograine = m_instance->GetSingleCreatureFromStorage(NPC_MOGRAINE);
        auto mograineAI = mograine ? dynamic_cast<boss_scarlet_commander_mograineAI*>(mograine->AI()) : nullptr;
        if (!mograine || !mograine->IsAlive())
        {
            AbortResurrection();
            return;
        }
        if (!m_resurrectionConfirmed)
        {
            if (m_resurrectionAttempts >= 3)
                AbortResurrection();
            else
                ResetTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION, 1000u);
            return;
        }
        // SpellHit is the handshake; wait for Mograine to actually leave fake death too.
        if (!mograineAI || !mograineAI->m_bHeal ||
            m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != SPECIAL)
        {
            ResetTimer(WHITEMANE_ACTION_SCARLET_RESURRECTION_ENTER_COMBAT, 200u);
            return;
        }
        m_resurrectionPhase = false;
        DisableTimer(WHITEMANE_ACTION_RESURRECTION_TIMEOUT);
        ResetCombatAction(WHITEMANE_ACTION_HEAL, GetSubsequentActionTimer(WHITEMANE_ACTION_HEAL));
        ResetCombatAction(WHITEMANE_ACTION_POWERWORD_SHIELD, GetSubsequentActionTimer(WHITEMANE_ACTION_POWERWORD_SHIELD));
        ResetCombatAction(WHITEMANE_ACTION_HOLY_SMITE, GetSubsequentActionTimer(WHITEMANE_ACTION_HOLY_SMITE));
        ResetCombatAction(WHITEMANE_ACTION_DOMINATE_MIND, GetSubsequentActionTimer(WHITEMANE_ACTION_DOMINATE_MIND));
        SetCombatScriptStatus(false);
        SetCombatMovement(true, true);
        SetMeleeEnabled(true);
        SetDeathPrevention(false);
    }

    void HandleSalute()
    {
        m_creature->HandleEmote(EMOTE_ONESHOT_SALUTE);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case WHITEMANE_ACTION_HEAL:
            {
                if (Unit* target = DoSelectLowestHpFriendly(50.0f))
                {
                    if (DoCastSpellIfCan(target, SPELL_HEAL) == CAST_OK)
                        ResetCombatAction(action, GetSubsequentActionTimer(action));
                }
                return;
            }
            case WHITEMANE_ACTION_HOLY_SMITE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_HOLYSMITE) == CAST_OK)
                    ResetCombatAction(action, GetSubsequentActionTimer(action));
                return;
            }
            case WHITEMANE_ACTION_POWERWORD_SHIELD:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_POWERWORDSHIELD) == CAST_OK)
                    ResetCombatAction(action, GetSubsequentActionTimer(action));
                return;
            }
            case WHITEMANE_ACTION_DEEP_SLEEP:
            {
                if (m_creature->GetHealthPercent() <= 50.0f)
                {
                    if (!m_instance || m_instance->GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != IN_PROGRESS)
                        return;
                    Creature* mograine = m_instance->GetSingleCreatureFromStorage(NPC_MOGRAINE);
                    if (!mograine || !mograine->IsAlive() ||
                        DoCastSpellIfCan(nullptr, SPELL_DEEPSLEEP, CAST_INTERRUPT_PREVIOUS) != CAST_OK)
                        return;
                    m_resurrectionPhase = true;
                    m_resurrectionConfirmed = false;
                    m_resurrectionAttempts = 0;
                    ResetTimer(WHITEMANE_ACTION_RESURRECTION_TIMEOUT, 45000u);
                    SetCombatMovement(false);
                    SetMeleeEnabled(false);
                    SetCombatScriptStatus(true);
                    m_creature->SetTarget(nullptr);
                    {
                        m_creature->AttackStop(true);
                        float fX, fY, fZ;
                        mograine->GetContactPoint(m_creature, fX, fY, fZ, INTERACTION_DISTANCE / 2.f);
                        m_creature->GetMotionMaster()->Clear();
                        m_creature->GetMotionMaster()->MovePoint(2, fX, fY, fZ, FORCED_MOVEMENT_RUN);
                    }
                    DisableCombatAction(action);
                }
                return;
            }
            case WHITEMANE_ACTION_DOMINATE_MIND:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_DOMINATEMIND, SELECT_FLAG_PLAYER))
                    if (DoCastSpellIfCan(target, SPELL_DOMINATEMIND) == CAST_OK)
                        ResetCombatAction(action, urand(20000, 30000));
                return;
            }
        }
    }
};

void AddSC_boss_mograine_and_whitemane()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_scarlet_commander_mograine";
    pNewScript->GetAI = &GetNewAIInstance<boss_scarlet_commander_mograineAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "boss_high_inquisitor_whitemane";
    pNewScript->GetAI = &GetNewAIInstance<boss_high_inquisitor_whitemaneAI>;
    pNewScript->RegisterSelf();
}
