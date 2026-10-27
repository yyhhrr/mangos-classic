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
SDName: instance_maraudon
SD%Complete: 100
SDComment:
SDCategory: Maraudon
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "maraudon.h"
#include "Entities/Player.h"
#include "Groups/Group.h"

instance_maraudon::instance_maraudon(Map* map) : ScriptedInstance(map)
{
    Initialize();
}

void instance_maraudon::Initialize()
{
    memset(m_encounter, 0, sizeof(m_encounter));
    m_spewLarvaTimer = MINUTE * IN_MILLISECONDS;
    m_recoveryTimer = 1000;
    m_celebrasExpiry = 0;
}

void instance_maraudon::ApplySpewerState(GameObject* go)
{
    bool stopped = (go->GetEntry() == GO_LARVA_SPEWER && m_encounter[TYPE_LARVA_SPEWER] == DONE) ||
        (go->GetEntry() == GO_CORRUPTION_SPEWER && m_encounter[TYPE_NOXXION] == DONE);
    if (!stopped)
        return;

    // GO_ACTIVATED re-arms ordinary traps after their respawn delay. The
    // instance owns this destruction; keep the broken model without that timer.
    if (go->GetLootState() != GO_READY)
        go->SetLootState(GO_READY);
    go->SetGoState(GO_STATE_ACTIVE_ALTERNATIVE);
    go->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
}

void instance_maraudon::OnObjectCreate(GameObject* go)
{
    switch (go->GetEntry())
    {
        case GO_LARVA_SPEWER:
        case GO_CORRUPTION_SPEWER:
        case GO_CELEBRAS_BOOK:
        case GO_CELEBRAS_AURA:
        case GO_STAFF_CREATOR:
            m_goEntryGuidStore[go->GetEntry()] = go->GetObjectGuid();
            ApplySpewerState(go);
            break;
    }
}

void instance_maraudon::OnObjectSpawn(GameObject* go)
{
    ApplySpewerState(go);
}

void instance_maraudon::OnCreatureCreate(Creature* creature)
{
    switch (creature->GetEntry())
    {
        case NPC_NOXXION:
        case NPC_CELEBRAS:
        case NPC_CELEBRAS_REDEEMED:
            m_npcEntryGuidStore[creature->GetEntry()] = creature->GetObjectGuid();
            if (creature->GetEntry() == NPC_CELEBRAS_REDEEMED)
                UpdateCelebrasVisibility(creature);
            break;
    }
}

void instance_maraudon::UpdateCelebrasVisibility(Creature* celebras)
{
    if (!celebras)
        celebras = GetSingleCreatureFromStorage(NPC_CELEBRAS_REDEEMED, true);
    if (celebras)
    {
        bool visible = m_encounter[TYPE_CELEBRAS] == DONE && m_celebrasExpiry > uint64(time(nullptr));
        celebras->SetVisibility(visible ? VISIBILITY_ON : VISIBILITY_OFF);
        if (visible)
            celebras->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        else
            celebras->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
    }
}

void instance_maraudon::OnCreatureRespawn(Creature* creature)
{
    if (creature->GetEntry() == NPC_CELEBRAS && creature->IsAlive() && m_encounter[TYPE_CELEBRAS] == DONE)
    {
        // A genuine boss respawn starts a new rescue; a reloaded corpse does not.
        m_encounter[TYPE_CELEBRAS] = NOT_STARTED;
        m_celebrasExpiry = 0;
        UpdateCelebrasVisibility();
        CleanupRitualObjects();
        StoreProgress();
    }
    else if (creature->GetEntry() == NPC_CELEBRAS_REDEEMED)
        UpdateCelebrasVisibility(creature);
}

void instance_maraudon::StoreProgress()
{
    std::ostringstream out;
    out << "M2 " << m_encounter[TYPE_NOXXION] << ' ' << m_encounter[TYPE_LARVA_SPEWER]
        << ' ' << m_encounter[TYPE_CELEBRAS] << ' ' << m_celebrasExpiry;
    m_strInstData = out.str();
    SaveToDB();
}

void instance_maraudon::SetData(uint32 type, uint32 data)
{
    // Accept the old spewer notification without ever downgrading a boss kill.
    if (type == TYPE_NOXXION && data == SPECIAL)
    {
        type = TYPE_LARVA_SPEWER;
        data = DONE;
    }
    if (type >= MAX_ENCOUNTER || data > SPECIAL || m_encounter[type] == DONE || m_encounter[type] == data)
        return;
    m_encounter[type] = data;
    if (type == TYPE_NOXXION && data == DONE)
        m_encounter[TYPE_LARVA_SPEWER] = DONE;
    if (m_encounter[TYPE_LARVA_SPEWER] == DONE)
        m_spewLarvaTimer = 0;
    for (uint32 entry : { uint32(GO_LARVA_SPEWER), uint32(GO_CORRUPTION_SPEWER) })
        if (GameObject* go = GetSingleGameObjectFromStorage(entry, true))
            ApplySpewerState(go);
    if (type == TYPE_CELEBRAS && data == DONE)
    {
        // Preserve the old five-hour availability, including across restarts.
        m_celebrasExpiry = uint64(time(nullptr)) + 5 * HOUR;
        UpdateCelebrasVisibility();
    }
    if (data == DONE)
        StoreProgress();
}

uint32 instance_maraudon::GetData(uint32 type) const
{
    return type < MAX_ENCOUNTER ? m_encounter[type] : 0;
}

void instance_maraudon::Load(const char* data)
{
    if (!data || !*data)
        return;
    uint32 noxxion = 0, spewer = 0, celebras = 0;
    uint64 expiry = 0;
    std::istringstream in(data);
    std::string tag;
    in >> tag;
    if (tag == "M2")
    {
        std::string expiryText;
        if (!(in >> noxxion >> spewer >> celebras >> expiryText) || noxxion > SPECIAL || spewer > SPECIAL || celebras > SPECIAL ||
            expiryText.empty() || expiryText.find_first_not_of("0123456789") != std::string::npos)
            return;
        std::istringstream expiryStream(expiryText);
        if (!(expiryStream >> expiry))
            return;
    }
    else
    {
        // Only DONE caused the legacy script to save. Its nested callback
        // sometimes serialized SPECIAL instead, so both mean a completed kill.
        if (tag.size() != 1 || tag[0] < '0' || tag[0] > '4')
            return;
        noxxion = tag[0] - '0';
        if (noxxion == SPECIAL)
            noxxion = DONE;
        spewer = noxxion == DONE ? DONE : NOT_STARTED;
    }
    std::string extra;
    if (in >> extra)
        return;
    m_encounter[TYPE_NOXXION] = noxxion == DONE ? DONE : NOT_STARTED;
    m_encounter[TYPE_LARVA_SPEWER] = spewer == DONE || noxxion == DONE ? DONE : NOT_STARTED;
    m_encounter[TYPE_CELEBRAS] = celebras == DONE ? DONE : NOT_STARTED;
    m_celebrasExpiry = celebras == DONE ? expiry : 0;
    m_spewLarvaTimer = m_encounter[TYPE_LARVA_SPEWER] == DONE ? 0 : MINUTE * IN_MILLISECONDS;
    m_strInstData = data;
    UpdateCelebrasVisibility();
}

void instance_maraudon::OnCreatureDeath(Creature* creature)
{
    if (creature->GetEntry() == NPC_NOXXION)
        SetData(TYPE_NOXXION, DONE);
    else if (creature->GetEntry() == NPC_CELEBRAS)
        SetData(TYPE_CELEBRAS, DONE);
}

void instance_maraudon::OnCreatureEvade(Creature* creature)
{
    if (creature->GetEntry() == NPC_NOXXION)
        SetData(TYPE_NOXXION, FAIL);
}

void instance_maraudon::CleanupRitualObjects()
{
    for (uint32 entry : { uint32(GO_CELEBRAS_BOOK), uint32(GO_CELEBRAS_AURA) })
        if (GameObject* go = GetSingleGameObjectFromStorage(entry, true))
        {
            go->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            go->ForcedDespawn();
        }
    if (GameObject* go = GetSingleGameObjectFromStorage(GO_STAFF_CREATOR, true))
        go->ResetDoorOrButton();
}

void instance_maraudon::Update(uint32 diff)
{
    if (m_recoveryTimer <= diff)
    {
        // Also adopt already-dead bosses from an old instance save, which did
        // not record Celebras. Do this after creature loading has set death state.
        for (uint32 entry : { uint32(NPC_NOXXION), uint32(NPC_CELEBRAS) })
            if (Creature* boss = GetSingleCreatureFromStorage(entry, true))
                if (!boss->IsAlive())
                    SetData(entry == NPC_NOXXION ? TYPE_NOXXION : TYPE_CELEBRAS, DONE);
        if (m_celebrasExpiry && m_celebrasExpiry <= uint64(time(nullptr)))
        {
            m_celebrasExpiry = 0;
            UpdateCelebrasVisibility();
            CleanupRitualObjects();
            StoreProgress();
        }
        m_recoveryTimer = 1000;
    }
    else
        m_recoveryTimer -= diff;
    if (m_spewLarvaTimer && m_encounter[TYPE_LARVA_SPEWER] != DONE)
    {
        if (m_spewLarvaTimer <= diff)
        {
            if (GameObject* go = GetSingleGameObjectFromStorage(GO_LARVA_SPEWER, true))
                go->SummonCreature(NPC_SPEWED_LARVA, spawnLocation.x, spawnLocation.y, spawnLocation.z, spawnLocation.o,
                    TEMPSPAWN_TIMED_DESPAWN, 10 * MINUTE * IN_MILLISECONDS);
            m_spewLarvaTimer = MINUTE * IN_MILLISECONDS;
        }
        else
            m_spewLarvaTimer -= diff;
    }
}

struct go_ai_larva_spewer : public GameObjectAI
{
    go_ai_larva_spewer(GameObject* go) : GameObjectAI(go) {}
    void OnLootStateChange(Unit*) override
    {
        if (m_go->GetLootState() == GO_ACTIVATED)
            if (InstanceData* data = m_go->GetInstanceData())
                data->SetData(TYPE_LARVA_SPEWER, DONE);
    }
};

namespace
{
    enum
    {
        QUEST_SCEPTER = 7046,
        SPELL_WAITING = 21916,
        SPELL_QUIT_RITUAL = 21914,
        SPELL_RECITE = 21950,
    };
}

// Routes and dialogue remain in DBscripts. This adapter owns only participation,
// phase transitions and completion, which must not be inferred from a book timer.
struct npc_celebras_redeemedAI : public ScriptedAI
{
    enum Phase { IDLE, APPROACHING, WAITING, RECITING, RESETTING };
    npc_celebras_redeemedAI(Creature* creature) : ScriptedAI(creature), m_phase(IDLE), m_timer(0), m_recited(false)
    {
        SetReactState(REACT_PASSIVE);
        SetCombatMovement(false);
    }

    Phase m_phase;
    uint32 m_timer;
    bool m_recited;
    GuidSet m_participants;

    instance_maraudon* Instance() const
    {
        return dynamic_cast<instance_maraudon*>(m_creature->GetInstanceData());
    }

    bool Available() const
    {
        return m_phase == IDLE && (m_creature->GetVisibility() == VISIBILITY_ON) && m_creature->IsAlive();
    }

    void Include(Player* player)
    {
        if (!player || player->GetQuestStatus(QUEST_SCEPTER) != QUEST_STATUS_INCOMPLETE ||
            !m_creature->IsWithinDistInMap(player, 60.0f))
            return;
        m_participants.insert(player->GetObjectGuid());
        if (Group* group = player->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->getSource())
                    if (member->GetQuestStatus(QUEST_SCEPTER) == QUEST_STATUS_INCOMPLETE && m_creature->IsWithinDistInMap(member, 60.0f))
                        m_participants.insert(member->GetObjectGuid());
    }

    bool Begin(Player* player)
    {
        if (!player || player->GetQuestStatus(QUEST_SCEPTER) != QUEST_STATUS_INCOMPLETE || !m_creature->IsAlive() ||
            !(m_creature->GetVisibility() == VISIBILITY_ON) || !m_creature->IsWithinDistInMap(player, 10.0f) || m_phase == RESETTING)
            return false;
        Include(player);
        if (m_phase != IDLE)
            return true; // A second accept joins the current ritual, not its timers.
        if (instance_maraudon* data = Instance())
            data->CleanupRitualObjects();
        m_phase = APPROACHING;
        m_timer = 10 * MINUTE * IN_MILLISECONDS;
        m_recited = false;
        m_creature->SetActiveObjectState(true);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->GetMap()->ScriptsStart(SCRIPT_TYPE_QUEST_START, QUEST_SCEPTER, m_creature, player, Map::SCRIPT_EXEC_PARAM_UNIQUE_BY_SOURCE);
        return true;
    }

    bool ReadBook(Player* player, GameObject* book)
    {
        if (m_phase != WAITING || !m_creature->HasAura(SPELL_WAITING) || !book->IsSpawned() ||
            !player->IsAlive() || player->GetQuestStatus(QUEST_SCEPTER) != QUEST_STATUS_INCOMPLETE ||
            !book->IsWithinDistInMap(player, INTERACTION_DISTANCE) || !m_creature->IsWithinDistInMap(book, 10.0f))
            return false;
        Include(player);
        m_phase = RECITING;
        m_timer = 2 * MINUTE * IN_MILLISECONDS;
        book->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
        // The existing script interrupts the wait, lights the stone and speaks.
        m_creature->GetMap()->ScriptsStart(SCRIPT_TYPE_GAMEOBJECT_TEMPLATE, GO_CELEBRAS_BOOK, player, book);
        return true;
    }

    void ClearScene()
    {
        m_creature->InterruptNonMeleeSpells(false);
        m_creature->RemoveAurasDueToSpell(SPELL_WAITING);
        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MoveIdle();
        if (instance_maraudon* data = Instance())
            data->CleanupRitualObjects();
    }

    void Finish(bool success)
    {
        if (m_phase == IDLE || m_phase == RESETTING)
            return;
        bool credit = success && m_phase == RECITING && m_recited;
        // The opening script has 23s of delayed actions; later waypoint visuals
        // need at most 7s. Do not hold a normally finished quest for the former.
        uint32 drainTime = m_phase == APPROACHING ? 30000 : 10000;
        m_phase = RESETTING; // Close the gate before despawn/cast callbacks.
        m_timer = drainTime;
        if (credit)
            for (ObjectGuid guid : m_participants)
                if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                    if (player->IsAlive() && player->GetQuestStatus(QUEST_SCEPTER) == QUEST_STATUS_INCOMPLETE &&
                        m_creature->IsWithinDistInMap(player, 60.0f))
                        player->AreaExploredOrEventHappens(QUEST_SCEPTER);
        m_participants.clear();
        ClearScene();
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
    }

    void MovementInform(uint32 type, uint32 point) override
    {
        if (m_phase == RESETTING)
        {
            // A delayed DB movement command can arrive after cancellation.
            // Stop it and drain any visual script just queued by that waypoint.
            ClearScene();
            m_timer = std::max(m_timer, uint32(10000));
            return;
        }
        if (type != WAYPOINT_MOTION_TYPE)
            return;
        if (point == 3 && m_phase == APPROACHING)
        {
            m_phase = WAITING;
            m_timer = 310000; // Native waiting aura times out after five minutes.
        }
        else if (point == 10 && m_phase == RECITING)
            Finish(true);
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_phase == IDLE)
            return;
        if (m_phase != RESETTING && m_creature->GetVisibility() != VISIBILITY_ON)
        {
            Finish(false);
            return;
        }
        if (m_timer > diff)
        {
            m_timer -= diff;
            return;
        }
        if (m_phase != RESETTING)
        {
            Finish(false);
            return;
        }
        ClearScene(); // Also remove any old visual callback that ran during reset.
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MovePoint(0, 654.905f, 87.382f, -86.8597f);
        if (m_creature->GetVisibility() == VISIBILITY_ON)
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetActiveObjectState(false);
        m_phase = IDLE;
        m_recited = false;
        m_timer = 0;
    }
};

bool QuestAccept_npc_celebras_redeemed(Player* player, Creature* creature, Quest const* quest)
{
    if (quest->GetQuestId() == QUEST_SCEPTER)
        if (auto* ai = dynamic_cast<npc_celebras_redeemedAI*>(creature->AI()))
            ai->Begin(player);
    return true;
}

bool GossipHello_npc_celebras_redeemed(Player* player, Creature* creature)
{
    player->PrepareGossipMenu(creature, creature->GetDefaultGossipMenuId());
    if (auto* ai = dynamic_cast<npc_celebras_redeemedAI*>(creature->AI()))
        if (ai->Available() && player->GetQuestStatus(QUEST_SCEPTER) == QUEST_STATUS_INCOMPLETE)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "\xe6\x88\x91\xe5\xb7\xb2\xe7\xbb\x8f\xe5\x87\x86\xe5\xa4\x87\xe5\xa5\xbd\xe4\xba\x86\xef\xbc\x8c\xe8\xaf\xb7\xe9\x87\x8d\xe6\x96\xb0\xe5\xbc\x80\xe5\xa7\x8b\xe4\xbb\xaa\xe5\xbc\x8f\xe3\x80\x82", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
    player->SendPreparedGossip(creature);
    return true;
}

bool GossipSelect_npc_celebras_redeemed(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (sender != GOSSIP_SENDER_MAIN || action != GOSSIP_ACTION_INFO_DEF + 1)
        return false;
    if (auto* ai = dynamic_cast<npc_celebras_redeemedAI*>(creature->AI()))
        ai->Begin(player);
    player->CLOSE_GOSSIP_MENU();
    return true;
}

bool GOUse_go_celebras_book(Player* player, GameObject* book)
{
    if (auto* data = dynamic_cast<instance_maraudon*>(book->GetInstanceData()))
        if (Creature* celebras = data->GetSingleCreatureFromStorage(NPC_CELEBRAS_REDEEMED, true))
            if (auto* ai = dynamic_cast<npc_celebras_redeemedAI*>(celebras->AI()))
                ai->ReadBook(player, book);
    return true; // Block the unconditional DBscript, including stale book clicks.
}

struct spell_celebras_ritual : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex index) const override
    {
        if (index != EFFECT_INDEX_0 || !spell->GetCaster())
            return;
        if (auto* ai = dynamic_cast<npc_celebras_redeemedAI*>(spell->GetCaster()->AI()))
        {
            if (spell->m_spellInfo->Id == SPELL_QUIT_RITUAL && ai->m_phase == npc_celebras_redeemedAI::WAITING)
                ai->Finish(false);
            else if (spell->m_spellInfo->Id == SPELL_RECITE && ai->m_phase == npc_celebras_redeemedAI::RECITING)
                if (GameObject* go = spell->GetGOTarget())
                    if (go->GetEntry() == GO_STAFF_CREATOR)
                        ai->m_recited = true;
        }
    }
};

void AddSC_instance_maraudon()
{
    Script* script = new Script;
    script->Name = "instance_maraudon";
    script->GetInstanceData = [](Map* map) -> InstanceData* { return new instance_maraudon(map); };
    script->RegisterSelf();
    script = new Script;
    script->Name = "go_larva_spewer";
    script->GetGameObjectAI = [](GameObject* go) -> GameObjectAI* { return new go_ai_larva_spewer(go); };
    script->RegisterSelf();
    script = new Script;
    script->Name = "npc_celebras_redeemed";
    script->GetAI = [](Creature* creature) -> UnitAI* { return new npc_celebras_redeemedAI(creature); };
    script->pQuestAcceptNPC = &QuestAccept_npc_celebras_redeemed;
    script->pGossipHello = &GossipHello_npc_celebras_redeemed;
    script->pGossipSelect = &GossipSelect_npc_celebras_redeemed;
    script->RegisterSelf();
    script = new Script;
    script->Name = "go_celebras_book";
    script->pGOUse = &GOUse_go_celebras_book;
    script->RegisterSelf();
    RegisterSpellScript<spell_celebras_ritual>("spell_celebras_ritual");
}
