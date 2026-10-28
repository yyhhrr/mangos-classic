/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_spire.h"
#include "blackrock_spire_spells.h"
#include <sstream>

enum
{
    AREATRIGGER_ENTER_UBRS = 2046,
    AREATRIGGER_STADIUM = 2026,
    AREATRIGGER_BEAST_AGGRO = 2066,
    AREATRIGGER_BEAST_INTRO = 2067,
};

namespace
{
// Six original encounter fields remain the prefix. LBR1 has exactly 40 tokens;
// everything after it belongs to the independently versioned upper ledger.
bool ParseBlackrockSave(char const* text, std::array<uint32, MAX_ENCOUNTER>& states,
    std::string& lower, std::string& upper)
{
    if (!text || std::char_traits<char>::length(text) > 65536) return false;
    std::istringstream stream(text);
    for (uint32& state : states)
        if (!(stream >> state) || state > SPECIAL) return false;
    stream >> std::ws;
    lower.clear();
    upper.clear();
    if (stream.eof()) return true;
    std::ostringstream ledger;
    for (uint32 index = 0; index < 40; ++index)
    {
        std::string token;
        if (!(stream >> token) || (!index && token != "LBR1")) return false;
        if (index) ledger << ' ';
        ledger << token;
    }
    lower = ledger.str();
    stream >> std::ws;
    upper.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}
}

instance_blackrock_spire::instance_blackrock_spire(Map* map) : ScriptedInstance(map),
    m_lower(*this, [this]() { SaveState(); }), m_upper(*this, [this]() { SaveState(); })
{
    Initialize();
}

void instance_blackrock_spire::Initialize() { m_strInstData = BuildSave(); }

std::string instance_blackrock_spire::BuildSave(std::string const* upperOverride, bool resetStadium) const
{
    std::ostringstream out;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
    {
        if (i) out << ' ';
        out << (resetStadium && i == TYPE_STADIUM ? uint32(NOT_STARTED) : GetData(i));
    }
    out << ' ' << m_lower.Save() << ' ' << (upperOverride ? *upperOverride : m_upper.Save());
    return out.str();
}

void instance_blackrock_spire::SaveState()
{
    m_strInstData = BuildSave();
    SaveToDB();
}

void instance_blackrock_spire::Load(char const* text)
{
    std::array<uint32, MAX_ENCOUNTER> states{};
    std::string lower, upper;
    // Parse and validate the complete record first: a malformed upper suffix
    // must not erase a valid lower event (and conversely).
    if (!ParseBlackrockSave(text, states, lower, upper) ||
        !BlackrockSpireLower::ValidateSave(lower) || !BlackrockSpireUpper::ValidateSave(states, upper))
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }
    m_lower.Load(lower);
    m_upper.Load(states, upper);
    m_strInstData = BuildSave();
    OUT_LOAD_INST_DATA_COMPLETE;
}

void instance_blackrock_spire::OnObjectCreate(GameObject* object)
{
    m_goEntryGuidStore[object->GetEntry()] = object->GetObjectGuid();
    m_lower.OnObjectCreate(object);
    m_upper.OnObjectCreate(object);
}

void instance_blackrock_spire::OnCreatureCreate(Creature* actor)
{
    switch (actor->GetEntry())
    {
        case NPC_THE_BEAST:
        case NPC_PYROGUARD_EMBERSEER:
        case NPC_SOLAKAR_FLAMEWREATH:
        case NPC_LORD_VICTOR_NEFARIUS:
        case NPC_GYTH:
        case NPC_REND_BLACKHAND:
        case NPC_SCARSHIELD_INFILTRATOR:
            m_npcEntryGuidStore[actor->GetEntry()] = actor->GetObjectGuid();
            break;
    }
    m_lower.OnCreatureCreate(actor);
    m_upper.OnCreatureCreate(actor);
}

void instance_blackrock_spire::OnCreatureRespawn(Creature* actor)
{
    m_lower.OnCreatureRespawn(actor);
    m_upper.OnCreatureRespawn(actor);
}
void instance_blackrock_spire::OnCreatureDeath(Creature* actor) { m_lower.OnCreatureDeath(actor); m_upper.OnCreatureDeath(actor); }
void instance_blackrock_spire::OnCreatureEvade(Creature* actor) { m_lower.OnCreatureEvade(actor); m_upper.OnCreatureEvade(actor); }
void instance_blackrock_spire::OnCreatureEnterCombat(Creature* actor) { m_upper.OnCreatureEnterCombat(actor); }
void instance_blackrock_spire::OnCreatureDespawn(Creature* actor)
{
    m_lower.OnCreatureDespawn(actor);
    m_upper.OnCreatureDespawn(actor);
    auto stored = m_npcEntryGuidStore.find(actor->GetEntry());
    if (stored != m_npcEntryGuidStore.end() && stored->second == actor->GetObjectGuid()) m_npcEntryGuidStore.erase(stored);
}
void instance_blackrock_spire::OnPlayerEnter(Player* player) { m_lower.OnPlayerEnter(player); m_upper.OnPlayerEnter(player); }
void instance_blackrock_spire::OnPlayerLeave(Player* player) { m_lower.OnPlayerLeave(player); m_upper.OnPlayerLeave(player); }
void instance_blackrock_spire::Update(uint32 diff) { m_lower.Update(diff); m_upper.Update(diff); }

void instance_blackrock_spire::SetData64(uint32 type, uint64 value)
{
    if (type != 1000 && type != 1001) return;
    if (Creature* trigger = instance->GetCreature(ObjectGuid(value)))
    {
        if (type == 1000) m_lower.HandleGrayhoofSpellHit(trigger, nullptr);
        else m_upper.RequestValthalak(trigger);
    }
}

std::vector<InstanceEventReset> instance_blackrock_spire::GetResettableEvents() const
{
    return {{TYPE_STADIUM, "Rend Blackhand Stadium", {NPC_REND_BLACKHAND, NPC_GYTH}}};
}

bool instance_blackrock_spire::GetEventResetData(uint32 eventId, std::string& data) const
{
    std::string upper;
    if (!m_upper.GetEventResetData(eventId, upper)) return false;
    data = BuildSave(&upper, true);
    return true;
}

void instance_blackrock_spire::ApplyEventReset(uint32 eventId, std::string const& data)
{
    std::array<uint32, MAX_ENCOUNTER> states{};
    std::string lower, upper;
    if (eventId != TYPE_STADIUM || !ParseBlackrockSave(data.c_str(), states, lower, upper) ||
        lower != m_lower.Save() || states[TYPE_STADIUM] != NOT_STARTED ||
        !BlackrockSpireUpper::ValidateSave(states, upper)) return;
    for (uint32 i = 0; i < MAX_ENCOUNTER; ++i)
        if (i != TYPE_STADIUM && states[i] != GetData(i)) return;
    // Apply only the upper reset. Lower runtime GUIDs must remain attached.
    if (m_upper.ApplyEventReset(eventId, upper)) m_strInstData = BuildSave();
}

InstanceData* GetInstanceData_instance_blackrock_spire(Map* pMap)
{
    return new instance_blackrock_spire(pMap);
}

bool AreaTrigger_at_blackrock_spire(Player* pPlayer, AreaTriggerEntry const* pAt)
{
    if (!pPlayer->IsAlive() || pPlayer->IsGameMaster())
        return false;

    switch (pAt->id)
    {
        case AREATRIGGER_ENTER_UBRS:
            if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*) pPlayer->GetInstanceData())
            {
                pInstance->TryUnlockUpper(pPlayer);
            }
            break;
        case AREATRIGGER_STADIUM:
            if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*) pPlayer->GetInstanceData())
            {
                pInstance->StartStadium(pPlayer);
            }
            break;

        // Intro event when players enter The Furnace: three Blackhand Elite are spawned, flee from the Beast and are "killed" by it
        // The Blackhand Elite scripts are handled in database by waypoints movement
        case AREATRIGGER_BEAST_INTRO:
            if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*) pPlayer->GetInstanceData())
            {
                if (pInstance->m_bBeastIntroDone)
                    return false;

                if (Creature* pBeast = pInstance->GetSingleCreatureFromStorage(NPC_THE_BEAST))
                {
                    for (uint8 i = 0; i < 3; i++)
                    {
                        if (Creature* pTemp = pBeast->SummonCreature(NPC_BLACKHAND_ELITE, aBeastLocs[i].x, aBeastLocs[i].y, aBeastLocs[i].z, aBeastLocs[i].o, TEMPSPAWN_DEAD_DESPAWN, 0))
                            pTemp->GetMotionMaster()->MoveWaypoint(i);
                    }
                    pInstance->m_bBeastIntroDone = true;
                }
            }
            break;

        // Make the Beast move around the room unless it finds some target.
        case AREATRIGGER_BEAST_AGGRO:
            if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*) pPlayer->GetInstanceData())
            {
                if (pInstance->m_bBeastOutOfLair)
                    return false;

                if (Creature* pBeast = pInstance->GetSingleCreatureFromStorage(NPC_THE_BEAST))
                {
                    pBeast->GetMotionMaster()->MoveWaypoint(0);
                    pInstance->m_bBeastOutOfLair = true;

                    // Play the intro if not already done
                    if (!pInstance->m_bBeastIntroDone)
                    {
                        for (uint8 i = 0; i < 3; i++)
                        {
                            if (Creature* pTemp = pBeast->SummonCreature(NPC_BLACKHAND_ELITE, aBeastLocs[i].x, aBeastLocs[i].y, aBeastLocs[i].z, aBeastLocs[i].o, TEMPSPAWN_DEAD_DESPAWN, 0))
                                pTemp->GetMotionMaster()->MoveWaypoint(i);
                        }
                        pInstance->m_bBeastIntroDone = true;
                    }
                }
            }
            break;
    }
    return false;
}

bool ProcessEventId_event_spell_altar_emberseer(uint32 /*uiEventId*/, Object* pSource, Object* /*pTarget*/, bool bIsStart)
{
    if (bIsStart && pSource->GetTypeId() == TYPEID_PLAYER)
    {
        if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*)((Player*)pSource)->GetInstanceData())
        {
            pInstance->StartEmberseer(static_cast<Player*>(pSource));
            return true;
        }
    }
    return false;
}

bool GOUse_go_father_flame(Player* pPlayer, GameObject* pGo)
{
    if (instance_blackrock_spire* pInstance = (instance_blackrock_spire*)pGo->GetInstanceData())
        pInstance->StartRookery(pPlayer);

    return true;
}

enum
{
    SPELL_STRIKE            = 15580,
    SPELL_SUNDER_ARMOR      = 15572,
    SPELL_DISTURB_EGGS      = 15746,
};

struct npc_rookery_hatcherAI : public ScriptedAI
{
    npc_rookery_hatcherAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = (instance_blackrock_spire*) pCreature->GetInstanceData();
        Reset();
    }

    instance_blackrock_spire* m_pInstance;

    uint32 uiStrikeTimer;
    uint32 uiSunderArmorTimer;
    uint32 uiDisturbEggsTimer;
    uint32 uiWaitTimer;

    bool m_bIsMovementActive;

    void Reset() override
    {
        uiStrikeTimer           = urand(5000, 7000);
        uiSunderArmorTimer      = 5000;
        uiDisturbEggsTimer      = urand(8000, 10000);
        uiWaitTimer             = 0;

        m_bIsMovementActive     = false;
    }

    void MovementInform(uint32 uiMoveType, uint32 uiPointId) override
    {
        if (uiMoveType != POINT_MOTION_TYPE || !uiPointId)
            return;

        m_creature->GetMotionMaster()->MoveIdle();
        m_bIsMovementActive  = false;
        uiWaitTimer = 2000;

        if (DoCastSpellIfCan(m_creature, SPELL_DISTURB_EGGS) == CAST_OK)
            uiDisturbEggsTimer = urand(7500, 10000);
    }

    // Function to search for new rookery egg in range
    void DoFindNewEgg()
    {
        GameObjectList lEggsInRange;
        GetGameObjectListWithEntryInGrid(lEggsInRange, m_creature, GO_ROOKERY_EGG, 20.0f);

        if (lEggsInRange.empty())   // No GO found
            return;

        lEggsInRange.sort(ObjectDistanceOrder(m_creature));
        GameObject* pNearestEgg = nullptr;

        // Always need to find new ones
        for (GameObjectList::const_iterator itr = lEggsInRange.begin(); itr != lEggsInRange.end(); ++itr)
        {
            if (!((*itr)->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE)))
            {
                pNearestEgg = *itr;
                break;
            }
        }

        if (!pNearestEgg)
            return;

        float fX, fY, fZ;
        pNearestEgg->GetContactPoint(m_creature, fX, fY, fZ, 1.0f);
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MovePoint(1, fX, fY, fZ);
        m_bIsMovementActive = true;
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // Return since we have no target or are disturbing an egg
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim() || m_bIsMovementActive)
            return;

        if (uiWaitTimer)
        {
            if (uiWaitTimer < uiDiff)
            {
                uiWaitTimer = 0;
                m_creature->GetMotionMaster()->MoveChase(m_creature->GetVictim());
            }
            else
                uiWaitTimer -= uiDiff;
        }

        //  Strike Timer
        if (uiStrikeTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_STRIKE) == CAST_OK)
                uiStrikeTimer = urand(4000, 6000);
        }
        else
            uiStrikeTimer -= uiDiff;

        // Sunder Armor timer
        if (uiSunderArmorTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_SUNDER_ARMOR) == CAST_OK)
                uiSunderArmorTimer = 5000;
        }
        else
            uiSunderArmorTimer -= uiDiff;

        // Disturb Rookery Eggs timer
        if (uiDisturbEggsTimer < uiDiff)
        {
            m_bIsMovementActive = false;
            DoFindNewEgg();
        }
        else
            uiDisturbEggsTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_rookery_hatcher(Creature* pCreature)
{
    return new npc_rookery_hatcherAI(pCreature);
}

bool ProcessEventId_event_lbrs_urok(uint32 eventId, Object* source, Object* target, bool isStart)
{
    if (eventId != 4845 || !source || !isStart || !source->isType(TYPEMASK_WORLDOBJECT)) return false;
    WorldObject* object = static_cast<WorldObject*>(source);
    if (object->GetMapId() != 229) return false;
    if (auto* data = static_cast<instance_blackrock_spire*>(object->GetInstanceData()))
    {
        data->StartUrok(source, target);
        return true; // The instance confirms the existing DB batch; never run it twice.
    }
    return false;
}

void AddSC_instance_blackrock_spire()
{
    AddSC_blackrock_spire_spells();
    Script* lowerEvent = new Script;
    lowerEvent->Name = "event_lbrs_urok";
    lowerEvent->pProcessEventId = &ProcessEventId_event_lbrs_urok;
    lowerEvent->RegisterSelf();

    Script* pNewScript = new Script;
    pNewScript->Name = "instance_blackrock_spire";
    pNewScript->GetInstanceData = &GetInstanceData_instance_blackrock_spire;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_blackrock_spire";
    pNewScript->pAreaTrigger = &AreaTrigger_at_blackrock_spire;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "event_spell_altar_emberseer";
    pNewScript->pProcessEventId = &ProcessEventId_event_spell_altar_emberseer;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_father_flame";
    pNewScript->pGOUse = &GOUse_go_father_flame;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_rookery_hatcher";
    pNewScript->GetAI = &GetAI_npc_rookery_hatcher;
    pNewScript->RegisterSelf();
}
