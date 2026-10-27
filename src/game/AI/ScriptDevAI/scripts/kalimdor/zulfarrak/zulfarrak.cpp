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
SDName: Zulfarrak
SD%Complete: 100
SDComment:
SDCategory: Zul'Farrak
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
event_go_zulfarrak_gong
event_spell_unlocking
at_zulfarrak
EndContentData */


#include "zulfarrak.h"
#include "Globals/ObjectMgr.h"

/*######
## event_go_zulfarrak_gong
######*/

bool ProcessEventId_event_go_zulfarrak_gong(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (!isStart || eventId != 2488 || !source || source->GetTypeId() != TYPEID_PLAYER)
        return false;
    Player* player = static_cast<Player*>(source);
    if (auto data = dynamic_cast<instance_zulfarrak*>(player->GetInstanceData()))
    {
        data->StartGahzrillaEvent(player);
        return true; // No player-owned delayed database summon remains.
    }
    return false;
}

bool ProcessEventId_event_spell_unlocking(uint32 eventId, Object* source, Object* /*target*/, bool isStart)
{
    if (!isStart || eventId != 2609 || !source || source->GetTypeId() != TYPEID_PLAYER)
        return false;
    Player* player = static_cast<Player*>(source);
    if (auto data = dynamic_cast<instance_zulfarrak*>(player->GetInstanceData()))
    {
        data->StartPyramidEvent(player);
        return true; // The instance preserves waves when the starter leaves.
    }
    return false;
}

// Default gossip preparation retains the database texts, translations and conditions.
// Both Bly choices request the same action, and Weegli has a single door choice.
bool GossipSelect_npc_zulfarrak_crew(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (!player || !creature)
        return true;
    player->CLOSE_GOSSIP_MENU();
    if (sender != 0 || action != GOSSIP_OPTION_GOSSIP || !player->IsAlive() || !player->IsInWorld() ||
        !creature->IsAlive() || !creature->IsInWorld() || player->GetMap() != creature->GetMap() ||
        !creature->IsWithinDistInMap(player, INTERACTION_DISTANCE))
        return true;
    if (auto data = dynamic_cast<instance_zulfarrak*>(creature->GetInstanceData()))
    {
        if (creature->GetEntry() == 7604)
            data->StartBlyBetrayal(player);
        else if (creature->GetEntry() == 7607)
            data->StartDoorDemolition(player);
    }
    return true;
}

struct ZulFarrakDoorExplosion : public SpellScript
{
    void OnHit(Spell* spell, SpellMissInfo miss) const override
    {
        if (miss != SPELL_MISS_NONE)
            return;
        GameObject* door = spell->GetGOTarget();
        WorldObject* caster = spell->GetTrueCaster();
        if (!door || !caster || door->GetEntry() != GO_END_DOOR ||
            door->GetGoState() != GO_STATE_ACTIVE_ALTERNATIVE || caster->GetMap() != door->GetMap())
            return;
        if (auto data = dynamic_cast<instance_zulfarrak*>(door->GetInstanceData()))
            data->ConfirmDoorDestroyed(door, caster);
    }
};

struct ZulFarrakCrewEscape : public SpellScript
{
    void OnHit(Spell* spell, SpellMissInfo miss) const override
    {
        Unit* target = spell->GetUnitTarget();
        WorldObject* caster = spell->GetTrueCaster();
        if (miss != SPELL_MISS_NONE || !target || !target->IsCreature() || caster != target || !target->IsAlive())
            return;
        if (auto data = dynamic_cast<instance_zulfarrak*>(target->GetInstanceData()))
            data->NotifyCrewEscaped(static_cast<Creature*>(target));
    }
};

// EventAI self buffs also run outside combat, before spell-list targeting.
// Only these two native NPC casts opt in to the same protective target test.
struct ZulFarrakProtection : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* caster = spell->GetCaster();
        if (!caster || !caster->IsCreature() || caster->GetMapId() != 209)
            return SPELL_CAST_OK;
        uint32 condition = 0;
        if (caster->GetEntry() == 7789 && spell->m_spellInfo->Id == 20798)
            condition = 2090090;
        else if (caster->GetEntry() == 7608 && spell->m_spellInfo->Id == 11974)
            condition = 2090091;
        if (!condition)
            return SPELL_CAST_OK;
        Unit* recipient = spell->m_targets.getUnitTarget();
        if (!recipient)
            recipient = caster; // Both spells support a native self cast.
        return sObjectMgr.IsUnitConditionSatisfied(condition, recipient, caster) ? SPELL_FAILED_AURA_BOUNCED : SPELL_CAST_OK;
    }
};

/*######
## at_zulfarrak
######*/

bool AreaTrigger_at_zulfarrak(Player* pPlayer, AreaTriggerEntry const* pAt)
{
    if (!pPlayer || !pAt || !pPlayer->IsInWorld())
        return false;
    if (pAt->id == AREATRIGGER_ANTUSUL)
    {
        if (pPlayer->IsGameMaster() || pPlayer->IsDead() || pPlayer->IsFeigningDeath())
            return false;

        instance_zulfarrak* pInstance = dynamic_cast<instance_zulfarrak*>(pPlayer->GetInstanceData());

        if (!pInstance)
            return false;

        if (pInstance->GetData(TYPE_ANTUSUL) == NOT_STARTED || pInstance->GetData(TYPE_ANTUSUL) == FAIL)
        {
            if (Creature* pAntuSul = pInstance->GetSingleCreatureFromStorage(NPC_ANTUSUL))
            {
                if (pAntuSul->IsAlive() && pAntuSul->IsInWorld() && pAntuSul->GetMap() == pPlayer->GetMap() && pAntuSul->AI())
                    pAntuSul->AI()->AttackStart(pPlayer);
            }
        }
    }

    return false;
}

void AddSC_zulfarrak()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "event_go_zulfarrak_gong";
    pNewScript->pProcessEventId = &ProcessEventId_event_go_zulfarrak_gong;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "event_spell_unlocking";
    pNewScript->pProcessEventId = &ProcessEventId_event_spell_unlocking;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_zulfarrak";
    pNewScript->pAreaTrigger = &AreaTrigger_at_zulfarrak;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_zulfarrak_crew";
    pNewScript->pGossipSelect = &GossipSelect_npc_zulfarrak_crew;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ZulFarrakDoorExplosion>("spell_zulfarrak_door_explosion");
    RegisterSpellScript<ZulFarrakCrewEscape>("spell_zulfarrak_crew_escape");
    RegisterSpellScript<ZulFarrakProtection>("spell_zulfarrak_protection");
}
