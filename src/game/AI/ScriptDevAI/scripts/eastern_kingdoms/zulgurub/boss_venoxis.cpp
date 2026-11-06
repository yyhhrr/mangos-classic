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
SDName: Boss_Venoxis
SD%Complete: 100
SDComment:
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Entities/Player.h"

enum
{
    SAY_TRANSFORM           = -1309000,
    SAY_DEATH               = -1309001,

    // troll spells
    SPELL_HOLY_FIRE         = 23860,
    SPELL_HOLY_WRATH        = 23979,
    SPELL_HOLY_NOVA         = 23858,
    SPELL_DISPELL           = 23859,
    SPELL_RENEW             = 23895,

    // serpent spells
    SPELL_VENOMSPIT         = 23862,
    SPELL_POISON_CLOUD      = 23861,
    SPELL_PARASITIC_SERPENT = 23867,

    // common spells
    SPELL_SNAKE_FORM            = 23849,
    SPELL_FRENZY                = 23537,
    SPELL_VIRULENT_POISON_PROC  = 22413,
    SPELL_TRASH                 = 3391,

    SPELL_LIST_PHASE_1 = 1450701,
    SPELL_LIST_PHASE_2 = 1450702,
    SPELL_LIST_PHASE_3 = 1450703,

    NPC_PARASITIC_SERPENT = 14884,
};

enum VenoxisActions
{
    VENOXIS_PHASE_2,
    VENOXIS_PHASE_3,
    VENOXIS_FRENZY,
    VENOXIS_ACTION_MAX,
};

struct boss_venoxisAI : public CombatAI
{
    boss_venoxisAI(Creature* creature) : CombatAI(creature, VENOXIS_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddTimerlessCombatAction(VENOXIS_PHASE_2, true);
        AddTimerlessCombatAction(VENOXIS_PHASE_3, false);
        AddTimerlessCombatAction(VENOXIS_FRENZY, false);
    }

    ScriptedInstance* m_instance;
    GuidSet m_parasiticSerpents;

    void EnterEvadeMode() override
    {
        m_creature->RemoveDynObject(SPELL_POISON_CLOUD);

        GuidSet serpents;
        serpents.swap(m_parasiticSerpents);
        for (ObjectGuid guid : serpents)
            if (Creature* serpent = m_creature->GetMap()->GetCreature(guid))
                if (serpent->IsAlive())
                    serpent->ForcedDespawn();

        CombatAI::EnterEvadeMode();
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_VENOXIS, FAIL);

        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        DoScriptText(SAY_DEATH, m_creature);

        if (m_instance)
            m_instance->SetData(TYPE_VENOXIS, DONE);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() != NPC_PARASITIC_SERPENT || !summoned->IsAlive() ||
            summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;

        // Wild summon spells notify the caster both on creation and after the effect.
        if (!m_parasiticSerpents.insert(summoned->GetObjectGuid()).second)
            return;

        std::vector<Unit*> targets;
        m_creature->SelectAttackingTargets(targets, ATTACKING_TARGET_ALL_SUITABLE, 0, nullptr, SELECT_FLAG_PLAYER);
        targets.erase(std::remove_if(targets.begin(), targets.end(), [summoned](Unit* player)
        {
            return static_cast<Player*>(player)->IsGameMaster() || !summoned->CanAttackNow(player);
        }), targets.end());

        if (targets.empty() || !summoned->AI())
        {
            summoned->ForcedDespawn();
            return;
        }

        Unit* target = targets[urand(0, targets.size() - 1)];
        summoned->CastSpell(summoned, SPELL_PARASITIC_SERPENT, TRIGGERED_OLD_TRIGGERED);
        summoned->AI()->AttackStart(target);
        if (summoned->GetVictim() == target)
            summoned->FixateTarget(target);
        else
            summoned->ForcedDespawn();
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        m_parasiticSerpents.erase(summoned->GetObjectGuid());
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case VENOXIS_PHASE_2:
                if (m_creature->GetHealthPercent() > 50.f)
                    break;

                if (DoCastSpellIfCan(nullptr, SPELL_SNAKE_FORM) == CAST_OK)
                {
                    DoScriptText(SAY_TRANSFORM, m_creature);
                    DoCastSpellIfCan(nullptr, SPELL_POISON_CLOUD); // an instant cloud on change
                    DoCastSpellIfCan(nullptr, SPELL_VIRULENT_POISON_PROC, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
                    DoResetThreat();
                    m_creature->SetSpellList(SPELL_LIST_PHASE_2);
                    DisableCombatAction(action);
                    SetActionReadyStatus(VENOXIS_PHASE_3, true);
                    SetActionReadyStatus(VENOXIS_FRENZY, true);
                }
                break;
            case VENOXIS_PHASE_3:
                if (m_creature->GetHealthPercent() < 25.f)
                {
                    m_creature->SetSpellList(SPELL_LIST_PHASE_3);
                    DisableCombatAction(action);
                }
                break;
            case VENOXIS_FRENZY:
                if (m_creature->GetHealthPercent() < 20.f) // maybe even 25
                    if (DoCastSpellIfCan(nullptr, SPELL_FRENZY) == CAST_OK)
                        DisableCombatAction(action);
                break;
        }
    }
};

void AddSC_boss_venoxis()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_venoxis";
    pNewScript->GetAI = &GetNewAIInstance<boss_venoxisAI>;
    pNewScript->RegisterSelf();
}
