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
SDName: instance_dire_maul
SD%Complete: 70
SDComment: East CreatureAI scenes use a versioned coordinator; West uses acknowledged ritual progress and fixed pylon identities
SDCategory: Dire Maul
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"


namespace
{
bool ParseDireMaulSave(char const* text, std::array<uint32, MAX_ENCOUNTER>& prefix, std::string& east, std::string& west)
{
    if (!text || std::char_traits<char>::length(text) > 65536) return false;
    std::istringstream in(text);
    for (uint32& value : prefix)
    {
        std::string token;
        if (!(in >> token) || token.size() != 1 || token[0] < '0' || token[0] > '0' + SPECIAL) return false;
        value = uint32(token[0] - '0');
    }
    in >> std::ws;
    std::string tail(std::istreambuf_iterator<char>(in), {});
    west.clear();
    if (tail.compare(0, 5, "DMW1 ") != 0) { east = tail; return true; }
    std::istringstream framed(tail.substr(5));
    std::string token;
    if (!(framed >> token) || token.empty()) return false;
    uint32 length = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9' || length > 2400 || (length == 2400 && c != '0')) return false;
        length = length * 10 + uint32(c - '0');
    }
    if (!length || framed.get() != ' ') return false;
    west.resize(length);
    if (!framed.read(&west[0], length) || framed.get() != ' ') return false;
    east.assign(std::istreambuf_iterator<char>(framed), {});
    // An explicit West frame always carries the East extension. Treat a
    // truncated new save as damaged, rather than silently loading East legacy.
    return !east.empty();
}
std::array<uint32, 9> WestPrefix(std::array<uint32, MAX_ENCOUNTER> const& prefix)
{
    std::array<uint32, 9> result{};
    for (uint32 i = 0; i < 8; ++i) result[i] = prefix[TYPE_WARPWOOD + i];
    result[8] = prefix[TYPE_DREADSTEED];
    return result;
}
}

std::string instance_dire_maul::BuildSave(std::array<uint32, MAX_ENCOUNTER> const* prefix) const
{
    std::ostringstream out;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i) out << ' ';
        out << (prefix ? (*prefix)[i] : GetData(i));
    }
    std::string west = m_west.Save();
    out << " DMW1 " << west.size() << ' ' << west << ' ' << m_east.Save();
    return out.str();
}
void instance_dire_maul::SaveState() { m_strInstData = BuildSave(); if (!m_loading) SaveToDB(); }
void instance_dire_maul::OnCreatureRespawn(Creature* actor) { m_east.OnCreatureRespawn(actor); m_west.OnCreatureRespawn(actor); }
void instance_dire_maul::OnCreatureEvade(Creature* actor) { m_east.OnCreatureEvade(actor); m_west.OnCreatureEvade(actor); }
void instance_dire_maul::OnCreatureDespawn(Creature* actor) { m_east.OnCreatureDespawn(actor); m_west.OnCreatureDespawn(actor); }
void instance_dire_maul::SetData64(uint32 type, uint64 value)
{
    if (type == 1002)
        if (Creature* trigger = instance->GetCreature(ObjectGuid(value))) m_east.RequestIsalien(trigger);
}
bool instance_dire_maul::GetGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string& snapshot) const
{
    return m_east.GetFelvineLootSnapshot(guid, entry, snapshot);
}
void instance_dire_maul::OnGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string const& snapshot, bool exhausted)
{
    m_east.OnFelvineLootSnapshot(guid, entry, snapshot, exhausted);
}

instance_dire_maul::instance_dire_maul(Map* pMap) : ScriptedInstance(pMap),
    m_east(*this, [this]() { SaveState(); }),
    m_west(*this, [this]() { SaveState(); })
{
    Initialize();
}

void instance_dire_maul::Initialize()
{
    memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));
    m_strInstData = BuildSave();
}

void instance_dire_maul::Update(uint32 diff)
{
    m_east.Update(diff);
    m_west.Update(diff);
}

void instance_dire_maul::OnPlayerEnter(Player* player)
{
    m_east.OnPlayerEnter(player);
    m_west.OnPlayerEnter(player);
}

void instance_dire_maul::OnPlayerLeave(Player* pPlayer) {
    m_east.OnPlayerLeave(pPlayer);
    m_west.OnPlayerLeave(pPlayer);
    // Remove King of the Gordok aura
    if (pPlayer->HasAura(SPELL_KING_OF_GORDOK))
        pPlayer->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
}

void instance_dire_maul::OnCreatureCreate(Creature* pCreature)
{
    m_east.OnCreatureCreate(pCreature);
    m_west.OnCreatureCreate(pCreature);
    switch (pCreature->GetEntry())
    {
        // East
        case NPC_OLD_IRONBARK:
        case NPC_IRONBARK_REDEEMED:
            break;

        // West actor ownership is maintained by the coordinator.
        case NPC_PRINCE_TORTHELDRIN:
        case NPC_IMMOLTHAR:
        case NPC_WARLOCK_DUMMY_INFERNAL:
        case NPC_JEEVEE:
            break;

        // North
        case NPC_CHO_RUSH_THE_OBSERVER:
            if (m_auiEncounter[TYPE_KING_GORDOK] == DONE)
                pCreature->SetStandState(UNIT_STAND_STATE_SIT);
            break;
        case NPC_KING_GORDOK:
        case NPC_CAPTAIN_KROMCRUSH:
        case NPC_GUARD_SLIPKIK:
            break;

        default:
            return;
    }
    m_npcEntryGuidStore[pCreature->GetEntry()] = pCreature->GetObjectGuid();
}

void instance_dire_maul::OnObjectCreate(GameObject* pGo)
{
    m_east.OnObjectCreate(pGo);
    m_west.OnObjectCreate(pGo);
    switch (pGo->GetEntry())
    {
        // East object state is synchronized by the versioned coordinator.
        case GO_CONSERVATORY_DOOR:
        case GO_CRUMBLE_WALL:
        case GO_CORRUPT_VINE:
        case GO_FELVINE_SHARD:
            break;

        // West final state and fixed ritual identities are synchronized separately.
        case GO_CRYSTAL_GENERATOR_1: case GO_CRYSTAL_GENERATOR_2: case GO_CRYSTAL_GENERATOR_3:
        case GO_CRYSTAL_GENERATOR_4: case GO_CRYSTAL_GENERATOR_5: case GO_FORCEFIELD:
        case GO_PRINCES_CHEST_AURA: case GO_WARPWOOD_DOOR: case GO_WEST_LIBRARY_DOOR:
        case GO_DREADSTEED_PORTAL: case GO_BELL_OF_DETHMOORA: case GO_WHEEL_OF_BLACK_MARCH:
        case GO_DOOMSDAY_CANDLE: case GO_WARLOCK_RITUAL_CIRCLE: case GO_RITUAL_CANDLE_AURA:
        case GO_MOUNT_QUEST_SYMBOL1: case GO_MOUNT_QUEST_SYMBOL2: case GO_MOUNT_QUEST_SYMBOL3:
            break;

        // North
        case GO_NORTH_LIBRARY_DOOR:
            break;

        default:
            return;
    }
    m_goEntryGuidStore[pGo->GetEntry()] = pGo->GetObjectGuid();
}

void instance_dire_maul::SetData(uint32 uiType, uint32 uiData)
{
    if (uiType < 3) { m_east.SetData(uiType, uiData); return; }
    if (uiType >= MAX_ENCOUNTER || uiData > SPECIAL) return;
    if ((uiType >= TYPE_WARPWOOD && uiType <= TYPE_PYLON_5) || uiType == TYPE_DREADSTEED) { m_west.SetData(uiType, uiData); return; }
    switch (uiType)
    {
        // North
        case TYPE_KING_GORDOK:
            m_auiEncounter[uiType] = uiData;
            if (uiData == DONE)
            {
                // change faction to certian ogres
                if (Creature* pOgre = GetSingleCreatureFromStorage(NPC_CAPTAIN_KROMCRUSH))
                {
                    if (pOgre->IsAlive())
                    {
                        pOgre->SetFactionTemporary(FACTION_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);

                        // only evade if required
                        if (pOgre->GetVictim())
                            pOgre->AI()->EnterEvadeMode();
                    }
                }

                if (Creature* pOgre = GetSingleCreatureFromStorage(NPC_CHO_RUSH_THE_OBSERVER))
                {
                    // Chorush evades and yells on king death (if alive)
                    if (pOgre->IsAlive())
                    {
                        DoScriptText(SAY_CHORUSH_KING_DEAD, pOgre);
                        pOgre->SetFactionTemporary(FACTION_FRIENDLY, TEMPFACTION_RESTORE_RESPAWN);
                        pOgre->AI()->EnterEvadeMode();
                    }

                    // start WP movement for Mizzle; event handled by movement and gossip dbscripts
                    if (Creature* pMizzle = pOgre->SummonCreature(NPC_MIZZLE_THE_CRAFTY, afMizzleSpawnLoc[0], afMizzleSpawnLoc[1], afMizzleSpawnLoc[2], afMizzleSpawnLoc[3], TEMPSPAWN_DEAD_DESPAWN, 0, true))
                    {
                        pMizzle->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                        pMizzle->SetWalk(false);
                        pMizzle->GetMotionMaster()->MoveWaypoint();
                    }
                }
            }
            break;
        case TYPE_MOLDAR:
        case TYPE_FENGUS:
        case TYPE_SLIPKIK:
        case TYPE_KROMCRUSH:
        case TYPE_CHORUSH:
        case TYPE_STOMPER_KREEG:
            m_auiEncounter[uiType] = uiData;
            break;
    }

    if (uiData == DONE)
    {
        OUT_SAVE_INST_DATA;
        SaveState();
        OUT_SAVE_INST_DATA_COMPLETE;
    }
}

std::vector<InstanceEventReset> instance_dire_maul::GetResettableEvents() const
{
    return {{TYPE_KING_GORDOK, "King Gordok / Gordok Tribute", {NPC_KING_GORDOK}, GO_GORDOK_TRIBUTE}};
}

bool instance_dire_maul::GetEventResetData(uint32 eventId, std::string& data) const
{
    if (eventId != TYPE_KING_GORDOK || GetData(TYPE_KING_GORDOK) == IN_PROGRESS) return false;
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i) prefix[i] = GetData(i);
    prefix[TYPE_KING_GORDOK] = NOT_STARTED;
    data = BuildSave(&prefix);
    return true;
}

void instance_dire_maul::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId != TYPE_KING_GORDOK) return;
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east, west;
    if (!ParseDireMaulSave(data.c_str(), prefix, east, west) || east != m_east.Save() || west != m_west.Save() || prefix[TYPE_KING_GORDOK] != NOT_STARTED) return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        if (i != TYPE_KING_GORDOK && prefix[i] != GetData(i)) return;
    // Reset only North. Loading East here would detach its live actor GUIDs.
    m_auiEncounter[TYPE_KING_GORDOK] = NOT_STARTED;
    m_strInstData = BuildSave();
    ResetEventRewardGameObject(GO_GORDOK_TRIBUTE);
    GuidVector mizzles;
    auto& objects = instance->GetObjectsStore();
    for (auto itr = objects.begin<Creature>(); itr != objects.end<Creature>(); ++itr)
        if (itr->second->GetEntry() == NPC_MIZZLE_THE_CRAFTY && !itr->second->HasStaticDBSpawnData())
            mizzles.push_back(itr->first);
    for (ObjectGuid guid : mizzles)
        if (Creature* mizzle = instance->GetCreature(guid))
        {
            mizzle->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            mizzle->ForcedDespawn();
        }
    for (uint32 entry : {NPC_CAPTAIN_KROMCRUSH, NPC_CHO_RUSH_THE_OBSERVER})
        if (Creature* guard = GetSingleCreatureFromStorage(entry, true))
        {
            guard->ClearTemporaryFaction();
            if (guard->IsAlive()) guard->SetStandState(UNIT_STAND_STATE_STAND);
        }
    for (auto const& reference : instance->GetPlayers())
        reference.getSource()->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
    // Surviving tribute guards retain their own kill records and reward quality.
}

bool instance_dire_maul::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    if (bossEntry != NPC_KING_GORDOK)
        return false;

    reward.gameObjectEntry = GO_GORDOK_TRIBUTE;
    reward.completed = GetData(TYPE_KING_GORDOK) == DONE;
    return true;
}

uint32 instance_dire_maul::GetData(uint32 uiType) const
{
    if (uiType < 3) return m_east.GetData(uiType);
    if ((uiType >= TYPE_WARPWOOD && uiType <= TYPE_PYLON_5) || uiType == TYPE_DREADSTEED) return m_west.GetData(uiType);
    if (uiType < MAX_ENCOUNTER)
        return m_auiEncounter[uiType];

    return 0;
}

void instance_dire_maul::OnCreatureEnterCombat(Creature* actor) { m_west.OnCreatureEnterCombat(actor); }

void instance_dire_maul::OnCreatureDeath(Creature* pCreature)
{
    m_east.OnCreatureDeath(pCreature);
    m_west.OnCreatureDeath(pCreature);
    switch (pCreature->GetEntry())
    {
        // North
        // - Handling of Ogre Boss (Assume boss can be handled in Acid)
        case NPC_KING_GORDOK:
            SetData(TYPE_KING_GORDOK, DONE);
            break;
        // Handle Ogre guards for Tribute Run chest
        case NPC_GUARD_MOLDAR:
            SetData(TYPE_MOLDAR, DONE);
            break;
        case NPC_GUARD_FENGUS:
            SetData(TYPE_FENGUS, DONE);
            break;
        case NPC_GUARD_SLIPKIK:
            SetData(TYPE_SLIPKIK, DONE);
            break;
        case NPC_CAPTAIN_KROMCRUSH:
            SetData(TYPE_KROMCRUSH, DONE);
            break;
        case NPC_CHO_RUSH_THE_OBSERVER:
            SetData(TYPE_CHORUSH, DONE);
            break;
        case NPC_STOMPER_KREEG:
            SetData(TYPE_STOMPER_KREEG, DONE);
            break;
    }
}

void instance_dire_maul::Load(char const* text)
{
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east, west;
    if (!ParseDireMaulSave(text, prefix, east, west) ||
        !DireMaulEast::ValidateSave({{prefix[0], prefix[1], prefix[2]}}, east) ||
        !DireMaulWest::ValidateSave(WestPrefix(prefix), west))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }
    m_loading = true;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        m_auiEncounter[i] = i >= 3 && prefix[i] == IN_PROGRESS ? uint32(NOT_STARTED) : prefix[i];
    m_east.Load({{prefix[0], prefix[1], prefix[2]}}, east);
    m_west.Load(WestPrefix(prefix), west);
    m_loading = false;
    m_strInstData = BuildSave();
    OUT_LOAD_INST_DATA_COMPLETE;
}

bool instance_dire_maul::CheckConditionCriteriaMeet(Player const* pPlayer, uint32 uiInstanceConditionId, WorldObject const* pConditionSource, uint32 conditionSourceType) const
{
    switch (uiInstanceConditionId)
    {
        case INSTANCE_CONDITION_ID_NORMAL_MODE:             // No guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE:               // One guard alive
        case INSTANCE_CONDITION_ID_HARD_MODE_2:             // Two guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_3:             // Three guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_4:             // Four guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_5:             // Five guards alive
        case INSTANCE_CONDITION_ID_HARD_MODE_6:             // Six guards alive
        {
            uint8 uiTributeRunAliveBosses = (GetData(TYPE_MOLDAR) != DONE ? 1 : 0) + (GetData(TYPE_FENGUS) != DONE ? 1 : 0) + (GetData(TYPE_SLIPKIK) != DONE ? 1 : 0)
                                            + (GetData(TYPE_KROMCRUSH) != DONE ? 1 : 0) + (GetData(TYPE_CHORUSH) != DONE ? 1 : 0) + (GetData(TYPE_STOMPER_KREEG) != DONE ? 1 : 0);

            return uiInstanceConditionId == uiTributeRunAliveBosses;
        }
    }

    script_error_log("instance_dire_maul::CheckConditionCriteriaMeet called with unsupported Id %u. Called with param plr %s, src %s, condition source type %u",
                     uiInstanceConditionId, pPlayer ? pPlayer->GetGuidStr().c_str() : "nullptr", pConditionSource ? pConditionSource->GetGuidStr().c_str() : "nullptr", conditionSourceType);
    return false;
}

InstanceData* GetInstanceData_instance_dire_maul(Map* pMap)
{
    return new instance_dire_maul(pMap);
}

/*###############
## go_fixed_trap
################*/

struct go_ai_fixed_trap : public GameObjectAI
{
    go_ai_fixed_trap(GameObject* go) : GameObjectAI(go) {}

    void UpdateAI(const uint32 /*uiDiff*/) override
    {
        if (m_go->IsSpawned())
        {
            // Sniffs show that trap should only react to Guard Slip'kik
            // He then enters evade mode and becomes an invalid target to hostile actions from players
            // Faction is changed to 14 and restored by spell 22799 (King of the Gordok)
            // Additionnaly, Guard Slip'kik should get UnitFlags 16 but purpose is unknown so we skip it for now
            if (Creature* slipkik = GetClosestCreatureWithEntry(m_go, NPC_GUARD_SLIPKIK, 0.5f))
            {
                m_go->Use(slipkik);
                DoScriptText(SAY_SLIPKIK_TRAP, slipkik);
                slipkik->AI()->EnterEvadeMode();
                slipkik->SetImmuneToPlayer(true);
                slipkik->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_RESTORE_RESPAWN);
                m_go->SetLootState(GO_JUST_DEACTIVATED);    // Despawn the trap
            }
        }
    }
};

GameObjectAI* GetAI_go_fixed_trap(GameObject* go)
{
    return new go_ai_fixed_trap(go);
}

/*####################################
## Guard Slip'kik Trigger dummy effect
####################################*/

// 31770 - Guard Slip'kik Trigger
struct GuardSlipkikTrigger : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (instance_dire_maul* instance = static_cast<instance_dire_maul*>(spell->GetCaster()->GetInstanceData()))
        {
            if (Creature* slipkik = instance->GetSingleCreatureFromStorage(NPC_GUARD_SLIPKIK))
                slipkik->setFaction(FACTION_OGRE);
        }
    }
};

void AddSC_instance_dire_maul()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "instance_dire_maul";
    pNewScript->GetInstanceData = &GetInstanceData_instance_dire_maul;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_fixed_trap";
    pNewScript->GetGameObjectAI = &GetAI_go_fixed_trap;
    pNewScript->RegisterSelf();

    RegisterSpellScript<GuardSlipkikTrigger>("spell_guard_slipkik_trigger");
}
