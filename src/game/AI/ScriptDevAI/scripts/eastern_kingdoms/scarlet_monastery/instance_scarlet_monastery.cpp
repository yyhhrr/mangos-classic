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
SDName: Instance_Scarlet_Monastery
SD%Complete: 50
SDComment:
SDCategory: Scarlet Monastery
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/include/sc_instance.h"
#include "scarlet_monastery.h"

instance_scarlet_monastery::instance_scarlet_monastery(Map* pMap) : ScriptedInstance(pMap)
{
    Initialize();
}

void instance_scarlet_monastery::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_defeatedBosses = 0;
}

void instance_scarlet_monastery::OnCreatureCreate(Creature* pCreature)
{
    switch (pCreature->GetEntry())
    {
        case NPC_SORCERER:
        case NPC_MYRMIDON:
        case NPC_DEFENDER:
        case NPC_CHAPLAIN:
        case NPC_WIZARD:
        case NPC_CENTURION:
        case NPC_CHAMPION:
        case NPC_ABBOT:
        case NPC_MONK:
        case NPC_FAIRBANKS:
            m_sAshbringerFriendlyGuids.insert(pCreature->GetObjectGuid());
            break;
        case NPC_MOGRAINE:
            m_sAshbringerFriendlyGuids.insert(pCreature->GetObjectGuid());
            // fall through
        case NPC_WHITEMANE:
        case NPC_VORREL:
            m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
            break;
    }
    // Creature creation can happen after the event has already finished.
    // Despawning Whitemane is deferred to OnCreatureRespawn, after world creation.
    if (pCreature->GetEntry() != NPC_WHITEMANE)
        ApplyAshbringerState(pCreature);
}


void instance_scarlet_monastery::OnCreatureDeath(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_INTERROGATOR_VISHAS)
    {
        if (Creature* pVorrel = GetSingleCreatureFromStorage(NPC_VORREL))
            DoScriptText(SAY_TRIGGER_VORREL, pVorrel);
    }
    else if (!pCreature->IsAlive() && (pCreature->GetEntry() == NPC_MOGRAINE || pCreature->GetEntry() == NPC_WHITEMANE))
    {
        // Only Unit::Kill reports a real death here; ForcedDespawn does not.
        m_defeatedBosses |= pCreature->GetEntry() == NPC_MOGRAINE ? 1u : 2u;
        if (m_defeatedBosses == 3u)
            SetData(TYPE_MOGRAINE_AND_WHITE_EVENT, DONE);
    }
}


void instance_scarlet_monastery::OnObjectCreate(GameObject* pGo)
{
    if (pGo->GetEntry() == GO_WHITEMANE_DOOR)
    {
        m_goEntryGuidStore[GO_WHITEMANE_DOOR] = pGo->GetObjectGuid();
        pGo->SetGoState(GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) == NOT_STARTED ? GO_STATE_READY : GO_STATE_ACTIVE);
    }
    else if (pGo->GetEntry() == GO_CHAPEL_DOOR)
    {
        m_goEntryGuidStore[GO_CHAPEL_DOOR] = pGo->GetObjectGuid();
        if (IsAshbringerActive())
            pGo->SetGoState(GO_STATE_ACTIVE);
    }
}


bool instance_scarlet_monastery::IsBossDefeated(uint32 entry) const
{
    const uint32 flag = entry == NPC_MOGRAINE ? 1u : (entry == NPC_WHITEMANE ? 2u : 0u);
    return (m_defeatedBosses & flag) != 0;
}

bool instance_scarlet_monastery::IsAshbringerActive() const
{
    return GetData(TYPE_ASHBRINGER_EVENT) == IN_PROGRESS || GetData(TYPE_ASHBRINGER_EVENT) == DONE;
}

void instance_scarlet_monastery::ApplyAshbringerState(Creature* creature)
{
    if (!IsAshbringerActive() || !creature->IsAlive() || creature->IsInCombat())
        return;
    if (creature->GetEntry() == NPC_WHITEMANE)
        creature->ForcedDespawn();
    else if (m_sAshbringerFriendlyGuids.find(creature->GetObjectGuid()) != m_sAshbringerFriendlyGuids.end())
        creature->setFaction(35);
}

void instance_scarlet_monastery::OnCreatureRespawn(Creature* creature)
{
    ApplyAshbringerState(creature);
}


void instance_scarlet_monastery::UpdateDoorStates()
{
    DoUseOpenableObject(GO_WHITEMANE_DOOR, GetData(TYPE_MOGRAINE_AND_WHITE_EVENT) != NOT_STARTED);
    if (IsAshbringerActive())
        DoUseOpenableObject(GO_CHAPEL_DOOR, true);
}

void instance_scarlet_monastery::OnObjectSpawn(GameObject* go)
{
    if (go->GetEntry() == GO_WHITEMANE_DOOR || go->GetEntry() == GO_CHAPEL_DOOR)
        UpdateDoorStates();
}


void instance_scarlet_monastery::SetData(uint32 uiType, uint32 uiData)
{
    if (uiType == TYPE_MOGRAINE_AND_WHITE_EVENT)
    {
        // Truly killed bosses and their corpses must never be recreated by a late evade.
        if (m_auiEncounter[0] == DONE || m_auiEncounter[0] == uiData)
            return;
        if (uiData == DONE && m_defeatedBosses != 3u)
            return;
        if (uiData == FAIL)
        {
            if (m_auiEncounter[0] == NOT_STARTED)
                return;
            Creature* whitemane = GetSingleCreatureFromStorage(NPC_WHITEMANE, true);
            Creature* mograine = GetSingleCreatureFromStorage(NPC_MOGRAINE, true);
            const bool whiteDead = IsBossDefeated(NPC_WHITEMANE);
            const bool mograineDead = IsBossDefeated(NPC_MOGRAINE);
            // Commit before ForcedDespawn callbacks can request another reset.
            m_auiEncounter[0] = whiteDead && mograineDead ? DONE :
                (whiteDead || mograineDead ? FAIL : NOT_STARTED);
            UpdateDoorStates();
            if (m_auiEncounter[0] != DONE)
                for (Creature* boss : {whitemane, mograine})
                    if (boss && boss->IsAlive() && !IsBossDefeated(boss->GetEntry()))
                    {
                        boss->SetRespawnDelay(30, true);
                        boss->ForcedDespawn();
                    }
            return;
        }
        m_auiEncounter[0] = uiData;
        UpdateDoorStates();
    }
    else if (uiType == TYPE_ASHBRINGER_EVENT)
    {
        if (m_auiEncounter[1] == DONE || m_auiEncounter[1] == uiData)
            return;
        m_auiEncounter[1] = uiData;
        if (IsAshbringerActive())
        {
            UpdateDoorStates();
            if (Creature* whitemane = GetSingleCreatureFromStorage(NPC_WHITEMANE, true))
                ApplyAshbringerState(whitemane);
            for (ObjectGuid guid : m_sAshbringerFriendlyGuids)
                if (Creature* creature = instance->GetCreature(guid))
                    ApplyAshbringerState(creature);
        }
    }
}


uint32 instance_scarlet_monastery::GetData(uint32 uiData) const
{
    if (uiData == TYPE_MOGRAINE_AND_WHITE_EVENT)
        return m_auiEncounter[0];
    if (uiData == TYPE_ASHBRINGER_EVENT)
        return m_auiEncounter[1];

    return 0;
}

InstanceData* GetInstanceData_instance_scarlet_monastery(Map* pMap)
{
    return new instance_scarlet_monastery(pMap);
}

bool instance_scarlet_monastery::DoHandleAreaTrigger(AreaTriggerEntry const* areaTrigger)
{
    if (areaTrigger->id == AREATRIGGER_CATHEDRAL_ENTRANCE)
    {
        if (GetData(TYPE_ASHBRINGER_EVENT) == NOT_STARTED)
        {
            SetData(TYPE_ASHBRINGER_EVENT, IN_PROGRESS);
            DoOrSimulateScriptTextForThisInstance(SAY_ASHBRINGER_ENTRANCE, NPC_MOGRAINE);
            return true;
        }
    }
    return false;
}

bool AreaTrigger_at_cathedral_entrance(Player* player, AreaTriggerEntry const* areaTrigger)
{
    if (player->IsGameMaster() || !player->IsAlive() || !player->HasAura(SPELL_CORRUPTED_ASHBRINGER_AURA))
        return false;

    if (auto* instance = (instance_scarlet_monastery*)player->GetInstanceData())
        return instance->DoHandleAreaTrigger(areaTrigger);

    return false;
}

struct ABEffect000 : public SpellScript
{
    bool OnCheckTarget(const Spell* spell, Unit* target, SpellEffectIndex /*eff*/) const override
    {
        if (WorldObject* caster = spell->GetCastingObject())
            if (!target->IsWithinLOSInMap(caster, true))
                return false;
        return true;
    }
};

void AddSC_instance_scarlet_monastery()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_scarlet_monastery";
    pNewScript->GetInstanceData = &GetInstanceData_instance_scarlet_monastery;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_cathedral_entrance";
    pNewScript->pAreaTrigger = &AreaTrigger_at_cathedral_entrance;
    pNewScript->RegisterSelf();

    RegisterSpellScript<ABEffect000>("spell_ab_effect_000");
}
