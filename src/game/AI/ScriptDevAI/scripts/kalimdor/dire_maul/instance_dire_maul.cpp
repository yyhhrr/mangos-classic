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
SD%Complete: 100
SDComment: Versioned East/West/North coordinators with acknowledged scenes and persistent reward claims
SDCategory: Dire Maul
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"


namespace
{
bool ReadDireMaulFrame(std::string const& text, char const* marker, uint32 maximum, std::string& payload, std::string& rest)
{
    std::istringstream in(text.substr(std::char_traits<char>::length(marker)));
    std::string token;
    if (!(in >> token) || token.empty() || token.size() > 6) return false;
    uint32 length = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9') return false;
        length = length * 10 + uint32(c - '0');
        if (length > maximum) return false;
    }
    if (!length || in.get() != ' ') return false;
    payload.resize(length);
    if (!in.read(&payload[0], length) || in.get() != ' ') return false;
    rest.assign(std::istreambuf_iterator<char>(in), {});
    return !rest.empty();
}
bool ParseDireMaulSave(char const* text, std::array<uint32, MAX_ENCOUNTER>& prefix, std::string& east, std::string& west, std::string& north)
{
    if (!text || std::char_traits<char>::length(text) > 262144) return false;
    std::istringstream in(text);
    for (uint32& value : prefix)
    {
        std::string token;
        if (!(in >> token) || token.size() != 1 || token[0] < '0' || token[0] > '0' + SPECIAL) return false;
        value = uint32(token[0] - '0');
    }
    in >> std::ws;
    std::string tail(std::istreambuf_iterator<char>(in), {}), rest;
    north.clear(); west.clear();
    bool hasNorth = tail.compare(0, 5, "DMN1 ") == 0;
    if (hasNorth)
    {
        if (!ReadDireMaulFrame(tail, "DMN1 ", 128000, north, rest)) return false;
        tail.swap(rest);
        // A new North save always contains both earlier area extensions.
        if (tail.compare(0, 5, "DMW1 ") != 0) return false;
    }
    if (tail.compare(0, 5, "DMW1 ") != 0) { east = tail; return true; }
    return ReadDireMaulFrame(tail, "DMW1 ", 24000, west, east);
}
std::array<uint32, 9> WestPrefix(std::array<uint32, MAX_ENCOUNTER> const& prefix)
{
    std::array<uint32, 9> result{};
    for (uint32 i = 0; i < 8; ++i) result[i] = prefix[TYPE_WARPWOOD + i];
    result[8] = prefix[TYPE_DREADSTEED];
    return result;
}
std::array<uint32, 7> NorthPrefix(std::array<uint32, MAX_ENCOUNTER> const& prefix)
{
    std::array<uint32, 7> result{};
    for (uint32 i = 0; i < 7; ++i) result[i] = prefix[TYPE_KING_GORDOK + i];
    return result;
}
}

std::string instance_dire_maul::BuildSave(std::array<uint32, MAX_ENCOUNTER> const* prefix, std::string const* north) const
{
    std::ostringstream out;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i) out << ' ';
        out << (prefix ? (*prefix)[i] : GetData(i));
    }
    std::string west = m_west.Save();
    std::string northText = north ? *north : m_north.Save();
    out << " DMN1 " << northText.size() << ' ' << northText << " DMW1 " << west.size() << ' ' << west << ' ' << m_east.Save();
    return out.str();
}

void instance_dire_maul::SaveState() { m_strInstData = BuildSave(); if (!m_loading) SaveToDB(); }
void instance_dire_maul::OnCreatureRespawn(Creature* actor) { m_east.OnCreatureRespawn(actor); m_west.OnCreatureRespawn(actor); m_north.OnCreatureRespawn(actor); }
void instance_dire_maul::OnCreatureEvade(Creature* actor) { m_east.OnCreatureEvade(actor); m_west.OnCreatureEvade(actor); m_north.OnCreatureEvade(actor); }
void instance_dire_maul::OnCreatureDespawn(Creature* actor) { m_east.OnCreatureDespawn(actor); m_west.OnCreatureDespawn(actor); m_north.OnCreatureDespawn(actor); }
void instance_dire_maul::SetData64(uint32 type, uint64 value)
{
    if (type == 1002)
        if (Creature* trigger = instance->GetCreature(ObjectGuid(value))) m_east.RequestIsalien(trigger);
}
bool instance_dire_maul::GetGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string& snapshot) const
{
    return m_north.GetLootSnapshot(guid, entry, snapshot) || m_east.GetFelvineLootSnapshot(guid, entry, snapshot);
}
void instance_dire_maul::OnGameObjectLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string const& snapshot, bool exhausted)
{
    m_east.OnFelvineLootSnapshot(guid, entry, snapshot, exhausted);
    m_north.OnLootSnapshot(guid, entry, snapshot, exhausted);
}

instance_dire_maul::instance_dire_maul(Map* pMap) : ScriptedInstance(pMap),
    m_east(*this, [this]() { SaveState(); }),
    m_west(*this, [this]() { SaveState(); }),
    m_north(*this, [this]() { SaveState(); })
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
    m_north.Update(diff);
}

void instance_dire_maul::OnPlayerEnter(Player* player)
{
    m_east.OnPlayerEnter(player);
    m_west.OnPlayerEnter(player);
    m_north.OnPlayerEnter(player);
}

void instance_dire_maul::OnPlayerLeave(Player* pPlayer) {
    m_east.OnPlayerLeave(pPlayer);
    m_west.OnPlayerLeave(pPlayer);
    m_north.OnPlayerLeave(pPlayer);
    // Remove King of the Gordok aura
    if (pPlayer->HasAura(SPELL_KING_OF_GORDOK))
        pPlayer->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
}

void instance_dire_maul::OnCreatureCreate(Creature* pCreature)
{
    m_east.OnCreatureCreate(pCreature);
    m_west.OnCreatureCreate(pCreature);
    m_north.OnCreatureCreate(pCreature);
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
    m_north.OnObjectCreate(pGo);
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

void instance_dire_maul::SetData(uint32 type, uint32 data)
{
    if (type >= MAX_ENCOUNTER || data > SPECIAL) return;
    if (type < 3) m_east.SetData(type, data);
    else if ((type >= TYPE_WARPWOOD && type <= TYPE_PYLON_5) || type == TYPE_DREADSTEED) m_west.SetData(type, data);
    else m_north.SetData(type, data);
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
    std::string north = m_north.SaveForKingReset();
    data = BuildSave(&prefix, &north);
    return true;
}


void instance_dire_maul::ApplyEventReset(uint32 eventId, std::string const& data)
{
    if (eventId != TYPE_KING_GORDOK) return;
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east, west, north;
    if (!ParseDireMaulSave(data.c_str(), prefix, east, west, north) || east != m_east.Save() || west != m_west.Save() ||
        north != m_north.SaveForKingReset() || prefix[TYPE_KING_GORDOK] != NOT_STARTED) return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        if (i != TYPE_KING_GORDOK && prefix[i] != GetData(i)) return;
    // The core has already saved this validated reset. Keep all live area
    // identities attached; loading any coordinator would detach its actors.
    m_loading = true;
    m_north.ResetKing();
    m_loading = false;
    ResetEventRewardGameObject(GO_GORDOK_TRIBUTE);
    for (auto const& reference : instance->GetPlayers())
        reference.getSource()->RemoveAurasDueToSpell(SPELL_KING_OF_GORDOK);
    m_strInstData = BuildSave();
}


bool instance_dire_maul::GetBossLootReward(uint32 bossEntry, BossLootReward& reward) const
{
    if (bossEntry != NPC_KING_GORDOK)
        return false;

    reward.gameObjectEntry = GO_GORDOK_TRIBUTE;
    reward.completed = GetData(TYPE_KING_GORDOK) == DONE;
    return true;
}

uint32 instance_dire_maul::GetData(uint32 type) const
{
    if (type < 3) return m_east.GetData(type);
    if ((type >= TYPE_WARPWOOD && type <= TYPE_PYLON_5) || type == TYPE_DREADSTEED) return m_west.GetData(type);
    if (type >= TYPE_KING_GORDOK && type <= TYPE_STOMPER_KREEG) return m_north.GetData(type);
    return 0;
}


void instance_dire_maul::OnCreatureEnterCombat(Creature* actor) { m_west.OnCreatureEnterCombat(actor); m_north.OnCreatureEnterCombat(actor); }

void instance_dire_maul::OnCreatureDeath(Creature* actor)
{
    m_east.OnCreatureDeath(actor);
    m_west.OnCreatureDeath(actor);
    m_north.OnCreatureDeath(actor);
}


void instance_dire_maul::Load(char const* text)
{
    std::array<uint32, MAX_ENCOUNTER> prefix{};
    std::string east, west, north;
    if (!ParseDireMaulSave(text, prefix, east, west, north) ||
        !DireMaulEast::ValidateSave({{prefix[0], prefix[1], prefix[2]}}, east) ||
        !DireMaulWest::ValidateSave(WestPrefix(prefix), west) ||
        !DireMaulNorth::ValidateSave(NorthPrefix(prefix), north))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }
    m_loading = true;
    m_east.Load({{prefix[0], prefix[1], prefix[2]}}, east);
    m_west.Load(WestPrefix(prefix), west);
    m_north.Load(NorthPrefix(prefix), north);
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
        auto* instance = static_cast<instance_dire_maul*>(m_go->GetInstanceData());
        if (!instance) return;
        instance->North().TrapRepaired(m_go);
        if (m_go->IsSpawned() && instance->North().IsTrapActive())
        {
            // Sniffs show that trap should only react to Guard Slip'kik
            // He then enters evade mode and becomes an invalid target to hostile actions from players
            // Faction is changed to 14 and restored by spell 22799 (King of the Gordok)
            // Additionnaly, Guard Slip'kik should get UnitFlags 16 but purpose is unknown so we skip it for now
            if (Creature* slipkik = GetClosestCreatureWithEntry(m_go, NPC_GUARD_SLIPKIK, 0.5f))
            {
                if (!slipkik->HasAura(SPELL_ICE_LOCK)) m_go->Use(slipkik);
                // The real 5249 -> 22856 spell chain must succeed first. A
                // failed cast retains the paid trap for a later attempt.
                if (!instance->North().FreezeSlipkik(slipkik)) return;
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
            {
                instance->North().ThawSlipkik(slipkik);
                // Original 31770 changes faction only; it does not remove
                // the indefinite ice lock or its player immunity.
                slipkik->setFaction(FACTION_OGRE);
            }
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
