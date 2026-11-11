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
SDName: Boss_Moam
SD%Complete: 100
SDComment:
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Entities/Pet.h"
#include "Spells/Scripts/SpellScript.h"

#include <optional>

enum
{
    EMOTE_AGGRO              = -1509000,
    EMOTE_MANA_FULL          = -1509001,
    EMOTE_ENERGIZING         = -1509028,

    SPELL_TRAMPLE            = 15550,
    SPELL_DOUBLE_ATTACK      = 18941,
    SPELL_DRAIN_MANA         = 25754,
    SPELL_ARCANE_ERUPTION    = 25672,
    SPELL_SUMMON_MANAFIENDS  = 25684,
    SPELL_ENERGIZE           = 25685,

    SPELL_SUMMON_MANA_FIEND_1 = 25681,
    SPELL_SUMMON_MANA_FIEND_2 = 25682,
    SPELL_SUMMON_MANA_FIEND_3 = 25683,

    NPC_MANA_FIEND           = 15527,
};

enum MoamActions
{
    MOAM_ARCANE_ERUPTION,
    MOAM_STONE_FORM,
    MOAM_TRAMPLE,
    MOAM_DOUBLE_ATTACK,
    MOAM_MANA_DRAIN,
    MOAM_ACTION_MAX,
};

static constexpr auto STONE_FORM_INTERVAL = 90s;
static constexpr auto STONE_FORM_DURATION = 90s;

struct boss_moamAI : public CombatAI
{
    boss_moamAI(Creature* creature) : CombatAI(creature, MOAM_ACTION_MAX)
    {
        AddCombatAction(MOAM_ARCANE_ERUPTION, 0u);
        AddCombatAction(MOAM_TRAMPLE, 9000u);
        AddCombatAction(MOAM_DOUBLE_ATTACK, 8000, 12000);
        AddCombatAction(MOAM_MANA_DRAIN, 6000u);
        AddCombatAction(MOAM_STONE_FORM, STONE_FORM_INTERVAL);

        if (m_creature->GetInstanceData())
        {
            m_creature->GetCombatManager().SetLeashingCheck([](Unit* unit, float, float, float)
            {
                auto const& players = unit->GetMap()->GetPlayers();
                return std::none_of(players.begin(), players.end(), [](auto const& ref)
                {
                    Player* player = ref.getSource();
                    return player && player->IsAlive() && !player->IsGameMaster();
                });
            });
        }
    }

    std::optional<TimePoint> m_stoneFormEndsAt;
    GuidVector m_stoneFiends;

    void Reset() override
    {
        CombatAI::Reset();
        m_stoneFormEndsAt.reset();
        m_stoneFiends.clear();
        m_creature->RemoveAurasDueToSpell(SPELL_ENERGIZE);
        m_creature->SetPower(POWER_MANA, 0);
        while (Pet* fiend = m_creature->FindGuardianWithEntry(NPC_MANA_FIEND))
            fiend->Unsummon(PET_SAVE_AS_DELETED, m_creature);
    }

    void JustReachedHome() override
    {
        CombatAI::JustReachedHome();
        m_creature->SetHealth(m_creature->GetMaxHealth());
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(EMOTE_AGGRO, m_creature);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() != NPC_MANA_FIEND)
            return;

        if (m_stoneFormEndsAt)
            m_stoneFiends.push_back(summoned->GetObjectGuid());
        summoned->SetCorpseDelay(2);
        summoned->SetInCombatWithZone();
        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
        {
            summoned->AddThreat(target, 100.f);
            summoned->AI()->AttackStart(target);
        }
    }

    void UpdateAI(uint32 diff) override
    {
        CombatAI::UpdateAI(diff);
        if (!m_stoneFormEndsAt || !m_creature->IsInCombat())
            return;

        Map* map = m_creature->GetMap();
        bool const manaFull = m_creature->GetPower(POWER_MANA) >= m_creature->GetMaxPower(POWER_MANA);
        bool const fiendsGone = std::none_of(m_stoneFiends.begin(), m_stoneFiends.end(), [map](ObjectGuid guid)
        {
            Creature* fiend = map->GetAnyTypeCreature(guid);
            return fiend && fiend->IsAlive();
        });
        if (manaFull || fiendsGone || map->GetCurrentClockTime() >= *m_stoneFormEndsAt || !m_creature->HasAura(SPELL_ENERGIZE))
        {
            m_stoneFormEndsAt.reset();
            m_stoneFiends.clear();
            m_creature->RemoveAurasDueToSpell(SPELL_ENERGIZE);
            ResetCombatAction(MOAM_STONE_FORM, STONE_FORM_INTERVAL);
        }
    }

    void ExecuteAction(uint32 action) override
    {
        if (m_stoneFormEndsAt)
            return;

        switch (action)
        {
            case MOAM_ARCANE_ERUPTION:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_ARCANE_ERUPTION) == CAST_OK)
                {
                    DoScriptText(EMOTE_MANA_FULL, m_creature);
                    ResetCombatAction(action, 5000); // small CD to prevent AI spam
                }
                break;
            }
            case MOAM_TRAMPLE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_TRAMPLE) == CAST_OK)
                    ResetCombatAction(action, 15000);
                break;
            }
            case MOAM_DOUBLE_ATTACK:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK) == CAST_OK)
                    ResetCombatAction(action, urand(8000, 12000));
                break;
            }
            case MOAM_MANA_DRAIN:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_DRAIN_MANA) == CAST_OK)
                    ResetCombatAction(action, 6000);
                break;
            }
            case MOAM_STONE_FORM:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_ENERGIZE) == CAST_OK)
                {
                    DisableCombatAction(action);
                    m_stoneFormEndsAt = m_creature->GetMap()->GetCurrentClockTime() + STONE_FORM_DURATION;
                    m_creature->CastSpell(nullptr, SPELL_SUMMON_MANAFIENDS, TRIGGERED_OLD_TRIGGERED);
                    DoScriptText(EMOTE_ENERGIZING, m_creature);
                }
                break;
            }
        }
    }
};

struct SummonManaFiendsMoam : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx != EFFECT_INDEX_0)
            return;

        spell->GetCaster()->CastSpell(nullptr, SPELL_SUMMON_MANA_FIEND_1, TRIGGERED_OLD_TRIGGERED);
        spell->GetCaster()->CastSpell(nullptr, SPELL_SUMMON_MANA_FIEND_2, TRIGGERED_OLD_TRIGGERED);
        spell->GetCaster()->CastSpell(nullptr, SPELL_SUMMON_MANA_FIEND_3, TRIGGERED_OLD_TRIGGERED);
    }
};

void AddSC_boss_moam()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_moam";
    pNewScript->GetAI = &GetNewAIInstance<boss_moamAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<SummonManaFiendsMoam>("spell_summon_mana_fiends_moam");
}
