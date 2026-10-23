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
SDName: instance_razorfen_kraul
SD%Complete: 50
SDComment:
SDCategory: Razorfen Kraul
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "razorfen_kraul.h"
#include "Maps/MapPersistentStateMgr.h"
#include "MotionGenerators/WaypointManager.h"

instance_razorfen_kraul::instance_razorfen_kraul(Map* pMap) : ScriptedInstance(pMap)
{
    Initialize();
}

void instance_razorfen_kraul::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_uiWardCheckTimer = 0;
    m_agathelosReleased = false;

    // Enumerate the complete static roster before any grid is loaded. OnCreatureCreate
    // is called before LoadFromDB restores the creature's death/respawn state.
    m_wardKeeperGuids.clear();
    auto collectWardKeeper = [this](CreatureDataPair const& data)
    {
        if (data.second.mapid == instance->GetId() && data.second.id == NPC_WARD_KEEPER)
            m_wardKeeperGuids.insert(data.first);
        return false;
    };
    sObjectMgr.DoCreatureData(collectWardKeeper);
}

void instance_razorfen_kraul::OnObjectCreate(GameObject* pGo)
{
    if (pGo->GetEntry() == GO_AGATHELOS_WARD)
    {
        m_goEntryGuidStore[GO_AGATHELOS_WARD] = pGo->GetObjectGuid();
        if (m_auiEncounter[0] == DONE)
            pGo->SetGoState(GO_STATE_ACTIVE);
    }
}

void instance_razorfen_kraul::OnCreatureCreate(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_AGATHELOS)
    {
        if (m_npcEntryGuidStore[NPC_AGATHELOS] != pCreature->GetObjectGuid())
            m_agathelosReleased = false;
        m_npcEntryGuidStore[NPC_AGATHELOS] = pCreature->GetObjectGuid();
    }
    // Never count a keeper here: its restored death state is not available yet.
}

void instance_razorfen_kraul::OnCreatureDeath(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_WARD_KEEPER && m_wardKeeperGuids.count(pCreature->GetDbGuid()))
        m_uiWardCheckTimer = 0;
    // Death hooks precede SetDeathState(JUST_DIED). Defer inspection and persistence
    // until the next instance update, after the normal respawn time has been set.
}

void instance_razorfen_kraul::OnCreatureRespawn(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_AGATHELOS)
        m_agathelosReleased = false;
    else if (pCreature->GetEntry() == NPC_WARD_KEEPER)
        m_uiWardCheckTimer = 0;
}

void instance_razorfen_kraul::OnCreatureEvade(Creature* pCreature)
{
    if (pCreature->GetEntry() == NPC_AGATHELOS)
        m_agathelosReleased = false;
    // The existing AI completes its normal home movement first. Do not re-close
    // the ward or replace the boss's EventAI combat logic.
}

void instance_razorfen_kraul::CheckWardKeepers()
{
    if (m_auiEncounter[0] == DONE || m_wardKeeperGuids.empty())
        return;

    bool allDead = true;
    time_t const now = time(nullptr);
    for (uint32 dbGuid : m_wardKeeperGuids)
    {
        Creature* keeper = instance->GetCreature(dbGuid);
        if (keeper && keeper->IsAlive())
        {
            allDead = false;
            continue;
        }

        if (keeper)
        {
            // Save a partial kill even when global immediate-respawn saving is off.
            // Use the normal respawn deadline; do not extend its configured delay.
            if (keeper->GetRespawnTimeEx() > now &&
                instance->GetPersistentState()->GetCreatureRespawnTime(dbGuid) != keeper->GetRespawnTimeEx())
                keeper->SaveRespawnTime();
        }
        else if (instance->GetPersistentState()->GetCreatureRespawnTime(dbGuid) <= now)
            allDead = false; // Unloaded and not known dead is not a completed keeper.
    }

    if (!allDead)
        return;

    m_auiEncounter[0] = DONE;
    if (GameObject* ward = GetSingleGameObjectFromStorage(GO_AGATHELOS_WARD, true))
        ward->SetGoState(GO_STATE_ACTIVE); // Idempotent: never toggle an already-open door.

    OUT_SAVE_INST_DATA;
    m_strInstData = std::to_string(DONE);
    SaveToDB();
    OUT_SAVE_INST_DATA_COMPLETE;
}

void instance_razorfen_kraul::ReleaseAgathelos()
{
    if (m_auiEncounter[0] != DONE || m_agathelosReleased)
        return;

    Creature* agathelos = GetSingleCreatureFromStorage(NPC_AGATHELOS, true);
    if (!agathelos || !agathelos->IsInWorld() || !agathelos->IsAlive() ||
        agathelos->IsInCombat() || agathelos->GetCombatManager().IsEvadingHome())
        return;

    WaypointPath const* path = sWaypointMgr.GetPathFromOrigin(NPC_AGATHELOS, agathelos->GetDbGuid(), 0, PATH_FROM_ENTRY);
    if (!path || path->empty())
        return;

    // An interrupted exit path resumes underneath the normal chase/home movement.
    // Reuse it instead of stacking another copy that would later restart the old one.
    if (agathelos->GetMotionMaster()->GetCurrentMovementGeneratorType() == PATH_MOTION_TYPE)
    {
        m_agathelosReleased = true;
        return;
    }

    // The existing three-point template is an exit route, not a looping patrol.
    // After evade, an interrupted path may already have returned the boss outside.
    WaypointNode const& last = path->rbegin()->second;
    if (!agathelos->IsWithinDist3d(last.x, last.y, last.z, 2.0f))
        agathelos->GetMotionMaster()->MovePath(0, PATH_FROM_ENTRY, FORCED_MOVEMENT_RUN, false, 0.0f, false);
    m_agathelosReleased = true;
}

void instance_razorfen_kraul::Update(uint32 diff)
{
    if (m_uiWardCheckTimer > diff)
    {
        m_uiWardCheckTimer -= diff;
        return;
    }
    m_uiWardCheckTimer = 1000;
    CheckWardKeepers();
    ReleaseAgathelos();
}

void instance_razorfen_kraul::SetData(uint32 uiType, uint32 uiData)
{
    // Keep the existing EventAI death notification compatible. It carries no GUID,
    // so it only requests a recheck and cannot decrement or force encounter state.
    if (uiType == TYPE_AGATHELOS && uiData == DONE)
        m_uiWardCheckTimer = 0;
}

void instance_razorfen_kraul::Load(const char* chrIn)
{
    if (!chrIn)
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    OUT_LOAD_INST_DATA(chrIn);
    std::istringstream loadStream(chrIn);
    uint32 state = NOT_STARTED;
    loadStream >> state;
    m_auiEncounter[0] = state == DONE ? DONE : NOT_STARTED;
    m_strInstData = std::to_string(m_auiEncounter[0]);
    m_uiWardCheckTimer = 0;
    m_agathelosReleased = false;
    if (m_auiEncounter[0] == DONE)
        if (GameObject* ward = GetSingleGameObjectFromStorage(GO_AGATHELOS_WARD, true))
            ward->SetGoState(GO_STATE_ACTIVE);
    OUT_LOAD_INST_DATA_COMPLETE;
}

uint32 instance_razorfen_kraul::GetData(uint32 uiType) const
{
    return uiType == TYPE_AGATHELOS ? m_auiEncounter[0] : 0;
}

InstanceData* GetInstanceData_instance_razorfen_kraul(Map* pMap)
{
    return new instance_razorfen_kraul(pMap);
}

void AddSC_instance_razorfen_kraul()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_razorfen_kraul";
    pNewScript->GetInstanceData = &GetInstanceData_instance_razorfen_kraul;
    pNewScript->RegisterSelf();
}
