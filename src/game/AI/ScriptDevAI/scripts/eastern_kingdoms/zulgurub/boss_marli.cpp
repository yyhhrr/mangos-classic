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
SDName: Boss_Marli
SD%Complete: 90
SDCategory: Zul'Gurub
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Entities/Player.h"
#include "Spells/Scripts/SpellScript.h"
#include "Spells/Spell.h"
#include "Spells/SpellMgr.h"

enum
{
    SAY_AGGRO                   = -1309005,
    SAY_TRANSFORM               = -1309006,
    SAY_TRANSFORM_BACK          = -1309025,
    SAY_SPIDER_SPAWN            = -1309007,
    SAY_DEATH                   = -1309008,

    // Spider form spells
    SPELL_CORROSIVE_POISON      = 24111,
    SPELL_CHARGE                = 22911,
    SPELL_ENVELOPING_WEBS       = 24110,
    SPELL_POISON_SHOCK          = 24112,

    // Troll form spells
    SPELL_POISON_VOLLEY         = 24099,
    SPELL_DRAIN_LIFE            = 24300,
    SPELL_ENLARGE               = 24109,                    // cast on friendlies
    // common spells
    SPELL_SPIDER_FORM           = 24084,
    SPELL_TRANSFORM_BACK        = 24085,
    SPELL_TRASH                 = 3391,
    SPELL_HATCH_EGGS            = 24083,

    SPELL_LIST_PHASE_1          = 1451001,
    SPELL_LIST_PHASE_2          = 1451002,
};

enum MarliActions
{
    MARLI_PHASE_CHANGE,
    MARLI_CHARGE,
    MARLI_HATCH_OPENING,
    MARLI_ACTIONS_MAX,
};

struct boss_marliAI : public CombatAI
{
    boss_marliAI(Creature* creature) : CombatAI(creature, MARLI_ACTIONS_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddCombatAction(MARLI_PHASE_CHANGE, 60000u);
        AddCombatAction(MARLI_CHARGE, true);
        AddCombatAction(MARLI_HATCH_OPENING, true);
    }

    ScriptedInstance* m_instance;
    GuidSet m_spiders;
    bool m_phaseTwo;

    void Reset() override
    {
        CombatAI::Reset();
        m_spiders.clear();
        m_phaseTwo = false;
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
        if (m_instance)
            m_instance->SetData(TYPE_MARLI, IN_PROGRESS);

        ResetCombatAction(MARLI_HATCH_OPENING, 1000);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoScriptText(SAY_DEATH, m_creature);

        if (m_instance)
            m_instance->SetData(TYPE_MARLI, DONE);
    }

    void SpellHitTarget(Unit* target, const SpellEntry* spellInfo, SpellMissInfo missInfo) override
    {
        if (!m_phaseTwo || spellInfo->Id != SPELL_ENVELOPING_WEBS || missInfo != SPELL_MISS_NONE ||
            !target->IsPlayer() || !target->HasAura(SPELL_ENVELOPING_WEBS))
            return;

        m_creature->getThreatManager().modifyThreatPercent(target, -100);
        ResetCombatAction(MARLI_CHARGE, 1000);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() != NPC_SPAWN_OF_MARLI || !summoned->IsAlive() || !summoned->AI() ||
            summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;

        // A wild summon can notify the caster both on creation and after the spell effect.
        if (!m_spiders.insert(summoned->GetObjectGuid()).second)
            return;

        std::vector<Unit*> targets;
        m_creature->SelectAttackingTargets(targets, ATTACKING_TARGET_ALL_SUITABLE, 0, nullptr, SELECT_FLAG_PLAYER);
        targets.erase(std::remove_if(targets.begin(), targets.end(), [summoned](Unit* player)
        {
            return static_cast<Player*>(player)->IsGameMaster() || !summoned->CanAttackNow(player);
        }), targets.end());
        if (!targets.empty())
        {
            summoned->AI()->AttackStart(targets[urand(0, targets.size() - 1)]);
            return;
        }

        // A charmed Speaker can be the only combat target on the pull.
        if (Unit* victim = m_creature->GetVictim())
            if (victim->IsAlive() && summoned->CanAttackNow(victim) &&
                (!victim->IsPlayer() || !static_cast<Player*>(victim)->IsGameMaster()))
                summoned->AI()->AttackStart(victim);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        m_spiders.erase(summoned->GetObjectGuid());
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_MARLI, FAIL);

        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case MARLI_PHASE_CHANGE:
                if (!m_phaseTwo)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_SPIDER_FORM) == CAST_OK)
                    {
                        DoScriptText(SAY_TRANSFORM, m_creature);
                        DoResetThreat();
                        DisableCombatAction(MARLI_HATCH_OPENING);
                        ResetCombatAction(action, 60000);
                        m_creature->SetSpellList(SPELL_LIST_PHASE_2);
                        m_phaseTwo = true;
                    }
                }
                else
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_TRANSFORM_BACK) == CAST_OK)
                    {
                        DoScriptText(SAY_TRANSFORM_BACK, m_creature);
                        m_creature->RemoveAurasDueToSpell(SPELL_SPIDER_FORM);
                        DisableCombatAction(MARLI_CHARGE);
                        ResetCombatAction(action, 60000);
                        m_creature->SetSpellList(SPELL_LIST_PHASE_1);
                        m_phaseTwo = false;
                    }
                }
                break;
            case MARLI_CHARGE:
            {
                DisableCombatAction(action);
                if (!m_phaseTwo)
                    break;

                std::vector<Unit*> targets;
                m_creature->SelectAttackingTargets(targets, ATTACKING_TARGET_ALL_SUITABLE, 0, SPELL_CHARGE,
                    SELECT_FLAG_PLAYER | SELECT_FLAG_NOT_IN_MELEE_RANGE | SELECT_FLAG_NOT_IMMUNE);
                Unit* target = nullptr;
                float highestThreat = 0.f;
                auto& threatManager = m_creature->getThreatManager();
                for (Unit* candidate : targets)
                {
                    if (candidate->HasAura(SPELL_ENVELOPING_WEBS) || static_cast<Player*>(candidate)->IsGameMaster() ||
                        !m_creature->CanAttackNow(candidate))
                        continue;

                    float threat = threatManager.getThreat(candidate);
                    if (!target || threat > highestThreat)
                    {
                        target = candidate;
                        highestThreat = threat;
                    }
                }
                if (target && DoCastSpellIfCan(target, SPELL_CHARGE) == CAST_OK)
                    AttackStart(target);
                break;
            }
            case MARLI_HATCH_OPENING:
                if (DoCastSpellIfCan(nullptr, SPELL_HATCH_EGGS) == CAST_OK)
                {
                    DoScriptText(SAY_SPIDER_SPAWN, m_creature);
                    DisableCombatAction(action);
                }
                break;
        }
    }
};

struct MarliHatchEggs : public SpellScript
{
    void OnInit(Spell* spell) const override
    {
        Unit* caster = spell->GetCaster();
        if (spell->m_spellInfo->Id != SPELL_HATCH_EGGS || !caster || !caster->IsInWorld())
            return;

        constexpr std::size_t openingEggCount = 4;
        float radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(spell->m_spellInfo->EffectRadiusIndex[EFFECT_INDEX_0]));
        GameObjectList eggs;
        GetGameObjectListWithEntryInGrid(eggs, caster, GO_SPIDER_EGG, radius);
        eggs.remove_if([](const GameObject* egg) { return !IsReadyEgg(egg); });
        if (eggs.size() <= openingEggCount)
            return;

        // Bound the area selection before the core randomly trims excess targets.
        eggs.sort([caster](const GameObject* left, const GameObject* right) { return IsCloser(caster, left, right); });
        const GameObject* last = *std::next(eggs.begin(), openingEggCount - 1);
        spell->SetScriptValue(last->GetObjectGuid().GetRawValue());
    }

    bool OnCheckTarget(const Spell* spell, GameObject* target, SpellEffectIndex /*eff*/) const override
    {
        if (!IsReadyEgg(target))
            return false;

        // The single-egg spell already chooses the nearest eligible object.
        uint64 boundary = spell->GetScriptValue();
        if (!boundary)
            return true;

        Unit* caster = spell->GetCaster();
        const GameObject* last = caster->GetMap()->GetGameObject(ObjectGuid(boundary));
        return last && !IsCloser(caster, last, target);
    }

private:
    static bool IsReadyEgg(const GameObject* egg)
    {
        return egg->GetEntry() == GO_SPIDER_EGG && egg->IsSpawned() &&
            egg->GetGoState() == GO_STATE_READY && egg->GetLootState() == GO_READY;
    }

    static bool IsCloser(const Unit* caster, const GameObject* left, const GameObject* right)
    {
        float leftDistance = caster->GetDistance(left, true, DIST_CALC_NONE);
        float rightDistance = caster->GetDistance(right, true, DIST_CALC_NONE);
        if (leftDistance != rightDistance)
            return leftDistance < rightDistance;
        return left->GetObjectGuid() < right->GetObjectGuid();
    }
};

void AddSC_boss_marli()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_marli";
    pNewScript->GetAI = &GetNewAIInstance<boss_marliAI>;
    pNewScript->RegisterSelf();
    RegisterSpellScript<MarliHatchEggs>("spell_marli_hatch_eggs");
}
