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
SDName: dire_maul
SD%Complete: 100%
SDComment: Quest support: 7631.
SDCategory: Dire Maul
EndScriptData */

/* ContentData
event_spells_warlock_dreadsteed
npc_warlock_ritual_mob
DreadsteedQuestObjects
EndContentData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"
#include "dire_maul_east_creatures.h"

void AddSC_dire_maul_north_creatures();
void AddSC_dire_maul_north_objects();
void AddSC_boss_alzzin();
void AddSC_boss_kalendris();
void AddSC_dire_maul_ritual_creatures();

/*######
## event_spells_warlock_dreadsteed
######*/

bool ProcessEventId_event_spells_warlock_dreadsteed(uint32 eventId, Object* source, Object*, bool start)
{
    if (start && source && source->GetTypeId() == TYPEID_PLAYER)
    {
        Player* player = static_cast<Player*>(source);
        if (player->GetMapId() == 429)
            if (auto* instance = static_cast<instance_dire_maul*>(player->GetInstanceData()))
            {
                if (eventId == 8420) instance->West().Ritual().Start(player);
                else if (eventId == 8428) instance->West().Ritual().RequestDreadsteed(player);
            }
    }
    // The owner performs every step, including original delays. Rejected or
    // coalesced requests must not fall through to unowned delayed DB actions.
    return true;
}

struct RitualCandleAura : public SpellScript
{
    bool OnCheckTarget(const Spell* /*spell*/, Unit* target, SpellEffectIndex /*eff*/) const override
    {
        if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
            return false;
        return true;
    }
};

// 22876 - Summon Netherwalker
struct SummonNetherWalker : public SpellScript
{
    void OnSuccessfulFinish(Spell* spell) const override
    {
        if (!spell->GetCaster()->IsCreature())
            return;
        Creature* caster = static_cast<Creature*>(spell->GetCaster());
        if (!caster || !caster->IsAlive())
            return;
        caster->ForcedDespawn(1000);
        caster->AI()->SetCombatScriptStatus(true);
        caster->AI()->SetReactState(REACT_PASSIVE);
    }
};

void AddSC_dire_maul()
{
    AddSC_dire_maul_north_creatures();
    AddSC_dire_maul_north_objects();
    AddSC_boss_alzzin();
    AddSC_boss_kalendris();
    AddSC_dire_maul_ritual_creatures();
    AddSC_dire_maul_east_creatures();
    AddSC_dire_maul_east_objects();
    Script* pNewScript = new Script;
    pNewScript->Name = "event_spells_warlock_dreadsteed";
    pNewScript->pProcessEventId = &ProcessEventId_event_spells_warlock_dreadsteed;
    pNewScript->RegisterSelf();

    RegisterSpellScript<RitualCandleAura>("spell_ritual_candle_aura");
    RegisterSpellScript<SummonNetherWalker>("spell_summon_netherwalker");
}
