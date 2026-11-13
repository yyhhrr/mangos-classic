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
SDName: Boss_Magmadar
SD%Complete: 99
SDComment: Lava bomb timers may need some adjustements
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "molten_core.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Spells/Scripts/SpellScript.h"

enum
{
    EMOTE_GENERIC_FRENZY_KILL   = -1000001,

    // SPELL_LAVA_BREATH           = 19272,                    // Triggered by SPELL_FRENZY (19451)
    SPELL_FRENZY                = 19451,
    SPELL_MAGMASPIT             = 19449,                    // This is actually a buff he gives himself
    SPELL_PANIC                 = 19408,
    SPELL_LAVABOMB              = 19411,
    SPELL_LAVABOMB_MANA         = 20474,
    SPELL_LAVABOMB_SUMMON       = 20494,                    // 30 second ground fire
    SPELL_LAVABOMB_MANA_SUMMON  = 20495,                    // 60 second ground fire
    GO_LAVABOMB                 = 177704,
    // SPELL_CONFLAGRATION         = 19428                     // Trap spell for GO 177704
};

enum MagmadarActions
{
    MAGMADAR_FRENZY,
    MAGMADAR_PANIC,
    MAGMADAR_LAVABOMB,
    MAGMADAR_LAVABOMB_MANA,
    MAGMADAR_ACTION_MAX,
};

struct boss_magmadarAI : public CombatAI
{
    boss_magmadarAI(Creature* creature) : CombatAI(creature, MAGMADAR_ACTION_MAX), m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData()))
    {
        AddCombatAction(MAGMADAR_FRENZY, 15000u);
        AddCombatAction(MAGMADAR_PANIC, 6000, 10000);
        AddCombatAction(MAGMADAR_LAVABOMB, 12000u);
        AddCombatAction(MAGMADAR_LAVABOMB_MANA, 18000u);
        Reset();
    }

    ScriptedInstance* m_instance;
    GuidList m_lavaBombs;
    uint64 m_pullId = 0;
    bool m_pullActive = false;

    void Reset() override
    {
        CombatAI::Reset();
        ClearLavaBombs();
        DoCastSpellIfCan(nullptr, SPELL_MAGMASPIT, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void Aggro(Unit* /*who*/) override
    {
        ++m_pullId;
        m_pullActive = true;
        if (m_instance)
            m_instance->SetData(TYPE_MAGMADAR, IN_PROGRESS);
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        ClearLavaBombs();
        if (m_instance)
            m_instance->SetData(TYPE_MAGMADAR, DONE);
    }

    void JustReachedHome() override
    {
        if (m_instance)
            m_instance->SetData(TYPE_MAGMADAR, NOT_STARTED);
    }

    void JustSummoned(GameObject* bomb) override
    {
        if (bomb->GetEntry() != GO_LAVABOMB)
            return;

        Map* map = bomb->GetMap();
        m_lavaBombs.remove_if([map](ObjectGuid guid) { return !map->GetGameObject(guid); });
        m_lavaBombs.push_back(bomb->GetObjectGuid());
    }

    uint64 GetPullId() const { return m_pullId; }

    bool CanLandLavaBomb(uint64 pullId) const
    {
        // A missile from a previous pull can land after the boss re-enters combat.
        return m_pullActive && pullId == m_pullId && m_creature->IsAlive() && m_creature->IsInCombat();
    }

    void ClearLavaBombs()
    {
        m_pullActive = false;
        Map* map = m_creature->GetMap();
        for (ObjectGuid guid : m_lavaBombs)
            if (GameObject* bomb = map->GetGameObject(guid); bomb && bomb->IsInWorld() &&
                bomb->GetEntry() == GO_LAVABOMB &&
                (bomb->GetSpellId() == SPELL_LAVABOMB_SUMMON || bomb->GetSpellId() == SPELL_LAVABOMB_MANA_SUMMON))
                bomb->Delete();
        m_lavaBombs.clear();
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case MAGMADAR_FRENZY:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_FRENZY) == CAST_OK)
                {
                    DoScriptText(EMOTE_GENERIC_FRENZY_KILL, m_creature);
                    ResetCombatAction(action, urand(15000, 20000));
                }
                break;
            }
            case MAGMADAR_PANIC:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_PANIC) == CAST_OK)
                    ResetCombatAction(action, urand(30000, 35000));
                break;
            }
            case MAGMADAR_LAVABOMB:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_LAVABOMB, SELECT_FLAG_POWER_NOT_MANA))
                    if (DoCastSpellIfCan(target, SPELL_LAVABOMB) == CAST_OK)
                        ResetCombatAction(action, urand(12000, 15000));
                break;
            }
            case MAGMADAR_LAVABOMB_MANA:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_LAVABOMB_MANA, SELECT_FLAG_POWER_MANA))
                    if (DoCastSpellIfCan(target, SPELL_LAVABOMB_MANA) == CAST_OK)
                        ResetCombatAction(action, urand(12000, 15000));
                break;
            }
        }
    }
};

struct MagmadarLavaBomb : public SpellScript
{
    void OnCast(Spell* spell) const override
    {
        if (Unit* caster = spell->GetCaster())
            if (auto* magmadar = dynamic_cast<boss_magmadarAI*>(caster->AI()))
                spell->SetScriptValue(magmadar->GetPullId());
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        if (effIdx != EFFECT_INDEX_0 || !caster || !target)
            return;

        auto* magmadar = dynamic_cast<boss_magmadarAI*>(caster->AI());
        if (magmadar && !magmadar->CanLandLavaBomb(spell->GetScriptValue()))
            return;

        uint32 summonSpell = spell->m_spellInfo->Id == SPELL_LAVABOMB ? SPELL_LAVABOMB_SUMMON : SPELL_LAVABOMB_MANA_SUMMON;
        target->CastSpell(target, summonSpell, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr,
            magmadar ? caster->GetObjectGuid() : ObjectGuid());
    }
};

void AddSC_boss_magmadar()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_magmadar";
    pNewScript->GetAI = &GetNewAIInstance<boss_magmadarAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<MagmadarLavaBomb>("spell_magmadar_lava_bomb");
}
