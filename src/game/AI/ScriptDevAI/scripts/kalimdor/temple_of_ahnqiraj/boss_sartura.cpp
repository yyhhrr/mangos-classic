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
 SDName: Boss_Sartura
 SD%Complete: 100
 SDComment:
 SDCategory: Temple of Ahn'Qiraj
 EndScriptData
 */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "temple_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Spells/Scripts/SpellScript.h"

enum
{
    SAY_AGGRO                   = -1531008,
    SAY_SLAY                    = 11443,
    SAY_DEATH                   = -1531010,

    EMOTE_FRENZY                = -1000002,
    EMOTE_BERSERK               = -1000004,

    SPELL_ENRAGE                = 8269,
    SPELL_SUNDERING_CLEAVE      = 25174,
    SPELL_WHIRLWIND             = 26083,
    SPELL_BERSERK               = 27680,

    SPELL_WHIRLWIND_TRIGGER     = 26084,
    SPELL_GUARD_WHIRLWIND       = 26038,
    SPELL_GUARD_WHIRLWIND_TRIGGER = 26686,
};

enum SarturaActions
{
    SARTURA_ENRAGE,
    SARTURA_WHIRLWIND,
    SARTURA_SUNDERING_CLEAVE,
    SARTURA_BERSERK,
    SARTURA_ACTION_MAX,
};

struct boss_sarturaAI : public CombatAI
{
    boss_sarturaAI(Creature* creature) : CombatAI(creature, SARTURA_ACTION_MAX)
    {
        AddTimerlessCombatAction(SARTURA_ENRAGE, true);
        AddCombatAction(SARTURA_WHIRLWIND, 10000, 20000);
        AddCombatAction(SARTURA_SUNDERING_CLEAVE, 2000, 5000);
        AddCombatAction(SARTURA_BERSERK, uint32(10 * MINUTE * IN_MILLISECONDS));
        AddOnKillText(SAY_SLAY);
        Reset();
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        DoScriptText(SAY_DEATH, m_creature);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case SARTURA_ENRAGE:
            {
                if (m_creature->GetHealthPercent() <= 25.0f)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_ENRAGE) == CAST_OK)
                    {
                        DoScriptText(EMOTE_FRENZY, m_creature);
                        SetActionReadyStatus(action, false);
                    }
                }
                break;
            }
            case SARTURA_WHIRLWIND:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_WHIRLWIND) == CAST_OK)
                    ResetCombatAction(action, urand(20, 25) * IN_MILLISECONDS);
                break;
            }
            case SARTURA_SUNDERING_CLEAVE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_SUNDERING_CLEAVE) == CAST_OK)
                    ResetCombatAction(action, urand(2, 5) * IN_MILLISECONDS);
                break;
            }
            case SARTURA_BERSERK:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_BERSERK) == CAST_OK)
                {
                    DoScriptText(EMOTE_BERSERK, m_creature);
                    DisableCombatAction(action);
                }
                break;
            }
        }
    }
};

class SarturaWhirlwindEndEvent : public BasicEvent
{
    public:
        SarturaWhirlwindEndEvent(Unit& caster, Aura& whirlwind)
            : m_caster(caster), m_whirlwind(whirlwind.GetWeakPtr()), m_spellId(whirlwind.GetId()) {}

        bool Execute(uint64 /*time*/, uint32 /*diff*/) override
        {
            if (!m_caster.IsAlive())
                return true;

            if (!m_whirlwind.expired())
            {
                m_caster.m_events.AddEvent(this, m_caster.m_events.CalculateTime(1), false);
                return false;
            }

            if (!m_caster.IsInCombat() || !m_caster.CanHaveThreatList())
                return true;

            // A replacement aura may already have selected a new target.
            Aura* replacement = m_caster.GetAura(m_spellId, EFFECT_INDEX_0);
            if (!replacement || !replacement->GetScriptValue())
                m_caster.getThreatManager().modifyAllThreatPercent(-100);
            return true;
        }

    private:
        Unit& m_caster;
        MaNGOS::unique_weak_ptr<Aura> m_whirlwind;
        uint32 m_spellId;
};

struct AQWhirlwind : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_1)
            return;

        Unit* caster = spell->GetCaster();
        SpellEntry const* auraSpell = spell->GetTriggeredByAuraSpellInfo();
        if (!caster || !caster->IsAlive() || !caster->IsInCombat() || !caster->CanHaveThreatList() || !auraSpell)
            return;

        bool const bossWhirlwind = caster->GetEntry() == NPC_SARTURA && auraSpell->Id == SPELL_WHIRLWIND && spell->m_spellInfo->Id == SPELL_WHIRLWIND_TRIGGER;
        bool const guardWhirlwind = caster->GetEntry() == NPC_SARTURA_ROYAL_GUARD && auraSpell->Id == SPELL_GUARD_WHIRLWIND && spell->m_spellInfo->Id == SPELL_GUARD_WHIRLWIND_TRIGGER;
        if (!bossWhirlwind && !guardWhirlwind)
            return;

        SpellAuraHolder* holder = caster->GetSpellAuraHolder(auraSpell->Id, caster->GetObjectGuid());
        Aura* whirlwind = holder ? holder->GetAuraByEffectIndex(EFFECT_INDEX_0) : nullptr;
        if (!whirlwind || whirlwind->GetAuraTicks() >= whirlwind->GetAuraMaxTicks())
            return;

        ThreatManager& threat = caster->getThreatManager();
        if (threat.isThreatListEmpty() || (guardWhirlwind && urand(0, 2) != 0))
            return;

        std::vector<Player*> candidates;
        for (HostileReference const* reference : threat.getThreatList())
        {
            Player* player = caster->GetMap()->GetPlayer(reference->getUnitGuid());
            if (player && player->IsAlive() && !player->IsGameMaster() && !caster->IsOfflineTarget(player) &&
                    IsInSarturaRoom(player->GetPositionX(), player->GetPositionY(), player->GetPositionZ()) &&
                    caster->CanAttack(player) && caster->IsWithinLOSInMap(player, true))
                candidates.push_back(player);
        }
        if (candidates.empty())
            return;

        if (!whirlwind->GetScriptValue())
        {
            // The trigger is bound in the DB, but the parent aura is not. Its weak pointer expires on removal.
            whirlwind->SetScriptValue(1);
            caster->m_events.AddEvent(new SarturaWhirlwindEndEvent(*caster, *whirlwind), caster->m_events.CalculateTime(1));
        }

        Player* target = candidates[urand(0, uint32(candidates.size() - 1))];
        threat.modifyAllThreatPercent(-100);
        threat.addThreat(target, 100000.f);
    }
};

void AddSC_boss_sartura()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_sartura";
    pNewScript->GetAI = &GetNewAIInstance<boss_sarturaAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<AQWhirlwind>("spell_aq_whirlwind");
}
