/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "dire_maul.h"
#include "dire_maul_north.h"
#include "dire_maul_north_creatures.h"
#include "dire_maul_north_scene_data.h"
#include <map>
#include <vector>

namespace
{
enum { SPELL_OGRE_DISGUISE = 22736, SPELL_KROM_ENRAGE = 8599,
    NPC_KNOT_THIMBLEJACK = 14338, QUEST_FREE_KNOT = 5525, QUEST_FREE_KNOT_REPEAT = 7429 };
DireMaulNorth* NorthFor(Creature* creature)
{
    if (!creature || creature->GetMapId() != 429) return nullptr;
    auto* owner = static_cast<instance_dire_maul*>(creature->GetInstanceData());
    return owner ? &owner->North() : nullptr;
}
bool CanUse(Player* player, Creature* creature, bool sameStage = true)
{
    auto* north = NorthFor(creature);
    return player && north && DmnScene::CanUseMenu(north->IsCurrentActor(creature),
        player->IsAlive(), creature->IsAlive(), player->GetMap() == creature->GetMap(),
        creature->IsWithinDistInMap(player, INTERACTION_DISTANCE), creature->IsInCombat(), sameStage);
}
void StartRoute(Creature* creature, uint32 node)
{
    creature->GetMotionMaster()->Clear(false, true);
    creature->GetMotionMaster()->MoveIdle();
    creature->GetMotionMaster()->UnpauseWaypoints();
    creature->GetMotionMaster()->MoveWaypoint();
    if (node) creature->GetMotionMaster()->SetNextWaypoint(node);
}
void RestoreRoute(Creature* creature, DmnScene::Point const* route, uint32 node)
{
    if (node)
    {
        auto const& p = route[node - 1];
        creature->NearTeleportTo(p.x, p.y, p.z, p.o == 100.0f ? creature->GetOrientation() : p.o);
    }
    // Reaching the acknowledged node again restores native LastWaypoint too.
    // MovementInform recognizes completed phases and does not repeat rewards.
    StartRoute(creature, node);
}
struct MenuReceipt
{
    uint32 stage = 0, menu = 0, token = 0;
    std::vector<GossipMenuItemData> options;
};
class SceneMenus
{
public:
    void Clear() { m_receipts.clear(); }
    bool Show(Player* player, Creature* creature, uint32 stage, uint32 menuId)
    {
        player->PrepareGossipMenu(creature, menuId);
        GossipMenu& menu = player->GetPlayerMenu()->GetGossipMenu();
        MenuReceipt receipt;
        receipt.stage = stage; receipt.menu = menuId;
        if (!++m_token) ++m_token;
        receipt.token = m_token;
        std::vector<GossipMenuItem> items;
        for (uint32 i = 0; i < menu.MenuItemCount(); ++i)
        {
            auto* data = menu.GetItemData(i);
            if (!data || menu.GetItem(i).m_gOptionId != GOSSIP_OPTION_GOSSIP) continue;
            items.push_back(menu.GetItem(i)); receipt.options.push_back(*data);
        }
        menu.ClearMenu(); menu.SetMenuId(menuId);
        for (uint32 i = 0; i < items.size(); ++i)
        {
            auto const& item = items[i]; auto const& data = receipt.options[i];
            menu.AddMenuItem(item.m_gIcon, item.m_gMessage, receipt.token, i + 1, item.m_gBoxMessage, item.m_gCoded);
            menu.AddGossipMenuItemData(data.m_gAction_menu, data.m_gAction_poi, 0);
        }
        if (m_receipts.size() >= 128) m_receipts.erase(m_receipts.begin());
        m_receipts[player->GetObjectGuid()] = receipt;
        player->SendPreparedGossip(creature);
        return true;
    }
    bool Take(Player* player, Creature* creature, uint32 sender, uint32 action, uint32 stage,
        uint32& menu, GossipMenuItemData& option)
    {
        auto found = m_receipts.find(player->GetObjectGuid());
        if (found == m_receipts.end()) return false;
        MenuReceipt receipt = found->second;
        m_receipts.erase(found);
        if (!CanUse(player, creature, stage == receipt.stage) || sender != receipt.token ||
            !action || action > receipt.options.size()) return false;
        menu = receipt.menu; option = receipt.options[action - 1];
        return true;
    }
private:
    uint32 m_token = 0;
    std::map<ObjectGuid, MenuReceipt> m_receipts;
};
}

struct npc_dmn_mizzleAI : public ScriptedAI
{
    explicit npc_dmn_mizzleAI(Creature* creature) : ScriptedAI(creature) {}
    bool m_restore = true, m_stalled = false;
    uint32 m_routeAttempts = 0, m_gossipTimer = 0;
    SceneMenus m_menus;
    bool Set(uint32 stage, uint32 node, uint32 timer)
    {
        auto* north = NorthFor(m_creature);
        return north && north->SetMizzleState(m_creature, {stage, node, timer});
    }
    void FinishPresentation()
    {
        // A restored TO_TRIBUTE has no volatile flag callback in the map's
        // old DB queue. Its completed scene must remain available to a new
        // king; a still-pending original one-second callback retains its wait.
        if (!m_gossipTimer) m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        m_creature->SetActiveObjectState(false);
    }
    void Reset() override { m_menus.Clear(); m_routeAttempts = 0; m_stalled = false; }
    void JustRespawned() override { ScriptedAI::JustRespawned(); m_restore = true; }
    bool Menu(Player* player)
    {
        auto* north = NorthFor(m_creature);
        if (!north || !north->CanUseMizzle(m_creature) || !CanUse(player, m_creature)) return true;
        return m_menus.Show(player, m_creature, north->GetMizzleState().stage, 5708);
    }
    bool Select(Player* player, uint32 sender, uint32 action)
    {
        auto* north = NorthFor(m_creature);
        uint32 menu = 0; GossipMenuItemData option{};
        if (!north || !north->CanUseMizzle(m_creature) ||
            !m_menus.Take(player, m_creature, sender, action, north->GetMizzleState().stage, menu, option))
        { player->CLOSE_GOSSIP_MENU(); return true; }
        if (menu == 5708 && option.m_gAction_menu == 5715)
        {
            // Recheck the original NOT aura condition at selection, including
            // after map changes and another player's tribute request.
            if (player->HasAura(SPELL_KING_OF_GORDOK)) { player->CLOSE_GOSSIP_MENU(); return true; }
            m_creature->CastSpell(player, SPELL_KING_OF_GORDOK, TRIGGERED_OLD_TRIGGERED);
            if (!player->HasAura(SPELL_KING_OF_GORDOK)) { player->CLOSE_GOSSIP_MENU(); return true; }
            return m_menus.Show(player, m_creature, north->GetMizzleState().stage, 5715);
        }
        if (menu == 5715 && option.m_gAction_menu == 5708)
            return m_menus.Show(player, m_creature, north->GetMizzleState().stage, 5708);
        if (menu == 5708 && option.m_gAction_menu == 5740 && player->HasAura(SPELL_KING_OF_GORDOK) &&
            north->GetMizzleState().stage == DMN_MIZZLE_WAIT_TRIBUTE)
            return m_menus.Show(player, m_creature, DMN_MIZZLE_WAIT_TRIBUTE, 5740);
        if (menu == 5740 && option.m_gAction_menu == 5742 && player->HasAura(SPELL_KING_OF_GORDOK) &&
            north->GetMizzleState().stage == DMN_MIZZLE_WAIT_TRIBUTE && north->RequestTribute(m_creature))
        {
            m_menus.Clear(); m_routeAttempts = 0; m_stalled = false;
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            m_creature->SetActiveObjectState(true);
            m_gossipTimer = 1000; // Original 574001 adds this flag after 1s.
            m_creature->GetMotionMaster()->UnpauseWaypoints();
            return m_menus.Show(player, m_creature, DMN_MIZZLE_TO_TRIBUTE, 5742);
        }
        player->CLOSE_GOSSIP_MENU(); return true;
    }
    void MovementInform(uint32 type, uint32 point) override
    {
        if (type != WAYPOINT_MOTION_TYPE || point < 1 || point > 8) return;
        auto* north = NorthFor(m_creature); if (!north || !north->IsCurrentActor(m_creature)) return;
        auto state = north->GetMizzleState();
        if (point > state.node) m_routeAttempts = 0;
        if (state.stage == DMN_MIZZLE_WAIT_TRIBUTE || state.stage == DMN_MIZZLE_DONE)
        { m_creature->GetMotionMaster()->PauseWaypoints(0); return; }
        if (state.stage == DMN_MIZZLE_TO_KING || state.stage == DMN_MIZZLE_TO_KING_SAY)
        {
            if (point == 7)
            {
                m_creature->GetMotionMaster()->PauseWaypoints(0);
                if (Set(DMN_MIZZLE_WAIT_TRIBUTE, 7, 0))
                {
                    DoBroadcastText(9411, m_creature);
                    m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                    m_creature->SetActiveObjectState(false);
                }
            }
            else if (point == 1 && state.node == 0) Set(DMN_MIZZLE_TO_KING_SAY, point, 1000);
            else Set(state.stage, point, state.stage == DMN_MIZZLE_TO_KING_SAY ? state.timer : DmnScene::RouteTimeout);
        }
        else if (state.stage == DMN_MIZZLE_TO_TRIBUTE && point == 8)
        {
            m_creature->GetMotionMaster()->PauseWaypoints(0);
            if (Set(DMN_MIZZLE_TO_TRIBUTE, 8, 0) && north->OpenTribute(m_creature))
                FinishPresentation();
        }
    }
    void UpdateAI(uint32 diff) override
    {
        auto* north = NorthFor(m_creature);
        if (!north || !m_creature->IsAlive() || !north->IsCurrentActor(m_creature)) return;
        auto state = north->GetMizzleState();
        if (m_restore)
        {
            m_restore = false;
            m_creature->SetWalk(false); // The original Mizzle scene runs.
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            if (state.stage == DMN_MIZZLE_INITIAL)
            {
                if (!Set(DMN_MIZZLE_TO_KING, 0, DmnScene::RouteTimeout)) return;
                state = north->GetMizzleState();
            }
            RestoreRoute(m_creature, DmnScene::MizzleRoute, state.node);
            if (state.stage == DMN_MIZZLE_WAIT_TRIBUTE || state.stage == DMN_MIZZLE_DONE)
            {
                m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                m_creature->SetActiveObjectState(false);
            }
            else
            {
                m_creature->SetActiveObjectState(true);
                if (state.stage == DMN_MIZZLE_TO_TRIBUTE && state.node == 7)
                {
                    // RequestTribute starts the saved route budget at60s;
                    // its first second is the original574001 flag callback.
                    m_gossipTimer = state.timer > 59000 ? state.timer - 59000 : 0;
                    if (!m_gossipTimer) m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
                }
            }
        }
        if (m_gossipTimer)
        {
            if (m_gossipTimer <= diff) { m_gossipTimer = 0; m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP); }
            else m_gossipTimer -= diff;
        }
        if (state.stage == DMN_MIZZLE_WAIT_TRIBUTE || state.stage == DMN_MIZZLE_DONE || m_stalled) return;
        if (state.stage == DMN_MIZZLE_TO_TRIBUTE && state.node == 8)
        {
            if (north->OpenTribute(m_creature)) FinishPresentation();
            return;
        }
        uint32 budget = diff;
        if (!DmnScene::ConsumeTimer(state.timer, budget)) { Set(state.stage, state.node, state.timer); return; }
        if (state.stage == DMN_MIZZLE_TO_KING_SAY)
        {
            if (Set(DMN_MIZZLE_TO_KING, state.node, DmnScene::RouteTimeout)) DoBroadcastText(9348, m_creature);
            return;
        }
        if (++m_routeAttempts > DmnScene::MaxRouteRetries)
        {
            m_stalled = true; m_creature->GetMotionMaster()->PauseWaypoints(0);
            script_error_log("Dire Maul North: Mizzle route stalled at checkpoint %u; reward remains unacknowledged", state.node);
            return;
        }
        Set(state.stage, state.node, DmnScene::RouteTimeout);
        RestoreRoute(m_creature, DmnScene::MizzleRoute, state.node);
    }
};

struct npc_dmn_kromcrushAI : public CreatureEventAI
{
    explicit npc_dmn_kromcrushAI(Creature* creature) : CreatureEventAI(creature) {}
    bool m_restore = true, m_stalled = false;
    uint32 m_routeAttempts = 0;
    SceneMenus m_menus;
    bool Set(uint32 stage, uint32 node, uint32 timer)
    {
        auto* north = NorthFor(m_creature);
        return north && north->SetKromcrushState(m_creature, {stage, node, timer});
    }
    void Reset() override { CreatureEventAI::Reset(); m_menus.Clear(); m_routeAttempts = 0; m_stalled = false; }
    void JustRespawned() override { CreatureEventAI::JustRespawned(); m_restore = true; }
    bool Menu(Player* player)
    {
        auto* north = NorthFor(m_creature);
        if (!north || !CanUse(player, m_creature)) return true;
        return m_menus.Show(player, m_creature, north->GetKromcrushState().stage, 5739);
    }
    bool Select(Player* player, uint32 sender, uint32 action)
    {
        auto* north = NorthFor(m_creature);
        uint32 menu = 0; GossipMenuItemData option{};
        if (!north || !m_menus.Take(player, m_creature, sender, action, north->GetKromcrushState().stage, menu, option))
        { player->CLOSE_GOSSIP_MENU(); return true; }
        auto state = north->GetKromcrushState();
        if (menu == 5739 && option.m_gAction_menu == 5738 && player->HasAura(SPELL_OGRE_DISGUISE) && north->CanStartKromcrush(m_creature))
            return m_menus.Show(player, m_creature, state.stage, 5738);
        if (menu == 5738 && option.m_gAction_menu < 0 && player->HasAura(SPELL_OGRE_DISGUISE) &&
            north->CanStartKromcrush(m_creature) && north->BeginKromcrush(m_creature))
        {
            m_menus.Clear(); m_routeAttempts = 0; m_stalled = false;
            DoBroadcastText(9416, m_creature);
            m_creature->CastSpell(m_creature, SPELL_KROM_ENRAGE, TRIGGERED_OLD_TRIGGERED);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            player->CLOSE_GOSSIP_MENU(); return true;
        }
        // Keep the classic king's task menus and native quest availability.
        if (player->HasAura(SPELL_KING_OF_GORDOK) &&
            ((menu == 5739 && option.m_gAction_menu == 5744) || (menu == 5744 && option.m_gAction_menu == 5746)))
            return m_menus.Show(player, m_creature, state.stage, uint32(option.m_gAction_menu));
        player->CLOSE_GOSSIP_MENU(); return true;
    }
    void ApplyDone()
    {
        m_creature->RemoveAurasDueToSpell(SPELL_KROM_ENRAGE);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        auto* north = NorthFor(m_creature);
        uint32 faction = north && north->GetData(TYPE_KING_GORDOK) == DONE ? FACTION_FRIENDLY : FACTION_OGRE;
        if (m_creature->GetFaction() != faction)
            m_creature->SetFactionTemporary(faction, TEMPFACTION_RESTORE_RESPAWN);
        m_creature->SetActiveObjectState(false);
    }
    void MovementInform(uint32 type, uint32 point) override
    {
        CreatureEventAI::MovementInform(type, point);
        if (type != WAYPOINT_MOTION_TYPE || point < 1 || point > 26) return;
        auto* north = NorthFor(m_creature); if (!north || !north->IsCurrentActor(m_creature)) return;
        auto state = north->GetKromcrushState();
        if (state.stage == DMN_KROM_DONE) { m_creature->GetMotionMaster()->PauseWaypoints(0); ApplyDone(); return; }
        if (state.stage != DMN_KROM_WALKING) return;
        if (point > state.node) m_routeAttempts = 0;
        if (point >= 25)
        {
            m_creature->GetMotionMaster()->PauseWaypoints(0);
            if (Set(DMN_KROM_DONE, 25, 0)) { ApplyDone(); DoBroadcastText(9424, m_creature); }
        }
        else Set(DMN_KROM_WALKING, point, DmnScene::RouteTimeout);
    }
    void UpdateScene(uint32 diff)
    {
        auto* north = NorthFor(m_creature);
        if (!north || !m_creature->IsAlive() || !north->IsCurrentActor(m_creature)) return;
        auto state = north->GetKromcrushState();
        if (m_restore)
        {
            m_restore = false;
            if (state.stage == DMN_KROM_DONE) { ApplyDone(); RestoreRoute(m_creature, DmnScene::KromcrushRoute, 25); }
            else if (state.stage == DMN_KROM_WALKING)
            {
                m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
                m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                m_creature->SetActiveObjectState(true);
                RestoreRoute(m_creature, DmnScene::KromcrushRoute, state.node);
            }
            else if (state.stage == DMN_KROM_ENRAGE)
            {
                m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
                m_creature->CastSpell(m_creature, SPELL_KROM_ENRAGE, TRIGGERED_OLD_TRIGGERED);
            }
        }
        if (state.stage == DMN_KROM_IDLE || state.stage == DMN_KROM_DONE || m_stalled || m_creature->IsInCombat()) return;
        uint32 budget = diff;
        if (!DmnScene::ConsumeTimer(state.timer, budget)) { Set(state.stage, state.node, state.timer); return; }
        if (state.stage == DMN_KROM_ENRAGE)
        {
            if (!Set(DMN_KROM_WALKING, state.node, DmnScene::RouteTimeout)) return;
            m_creature->RemoveAurasDueToSpell(SPELL_KROM_ENRAGE);
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->SetActiveObjectState(true);
            StartRoute(m_creature, state.node); return;
        }
        if (++m_routeAttempts > DmnScene::MaxRouteRetries)
        {
            m_stalled = true; m_creature->GetMotionMaster()->PauseWaypoints(0);
            script_error_log("Dire Maul North: Kromcrush route stalled at checkpoint %u", state.node);
            return;
        }
        Set(state.stage, state.node, DmnScene::RouteTimeout);
        RestoreRoute(m_creature, DmnScene::KromcrushRoute, state.node);
    }
    void UpdateAI(uint32 diff) override
    {
        UpdateScene(diff);
        // The original six native EventAI rows own every combat spell/timer.
        if (m_creature->IsAlive()) CreatureEventAI::UpdateAI(diff);
    }
};

struct npc_dmn_knotAI : public ScriptedAI
{
    explicit npc_dmn_knotAI(Creature* creature) : ScriptedAI(creature) {}
    bool m_restore = true;
    bool Set(uint32 stage, uint32 timer)
    {
        auto* north = NorthFor(m_creature);
        return north && north->SetKnotState(m_creature, {stage, 0, timer});
    }
    void JustRespawned() override { ScriptedAI::JustRespawned(); m_restore = true; }
    bool Reward(Player* player, Quest const* quest)
    {
        if (quest->GetQuestId() != QUEST_FREE_KNOT && quest->GetQuestId() != QUEST_FREE_KNOT_REPEAT) return false;
        auto* north = NorthFor(m_creature);
        // Native RewardQuest has already charged exactly one shackle key and
        // committed the real first/repeat quest before this callback runs.
        if (player && north && player->GetMap() == m_creature->GetMap() && north->BeginKnot(m_creature))
        {
            DoBroadcastText(9346, m_creature);
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->SetActiveObjectState(true);
        }
        return true; // This CreatureAI owns script5525; never schedule it twice.
    }
    void UpdateAI(uint32 diff) override
    {
        auto* north = NorthFor(m_creature);
        if (!north || !m_creature->IsAlive() || !north->IsCurrentActor(m_creature)) return;
        auto state = north->GetKnotState();
        if (north->IsKnotAway())
        {
            // Reconstruct the original absolute return deadline, rather than
            // restarting two hours every time a live DB spawn is recreated.
            m_creature->SetRespawnDelay((north->KnotReturnRemainingMS() + 999) / 1000, true);
            m_creature->ForcedDespawn(); return;
        }
        if (m_restore)
        {
            m_restore = false;
            if (state.stage != DMN_KNOT_IDLE)
            {
                m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                m_creature->SetActiveObjectState(true);
                if (state.stage >= DMN_KNOT_MOVE) m_creature->SetWalk(false);
                if (state.stage == DMN_KNOT_DESPAWN)
                    m_creature->GetMotionMaster()->MovePoint(0, 500.420013428f, 540.966003418f, -25.384599686f);
            }
        }
        if (state.stage == DMN_KNOT_IDLE || state.stage == DMN_KNOT_AWAY) return;
        uint32 budget = diff;
        for (uint32 transitions = 0; transitions < 6; ++transitions)
        {
            state = north->GetKnotState();
            if (!DmnScene::ConsumeTimer(state.timer, budget)) { Set(state.stage, state.timer); return; }
            if (state.stage == DMN_KNOT_CACHE)
            {
                if (!north->CompleteKnot(m_creature)) { Set(state.stage, DmnScene::RetryDelay); return; }
                if (!Set(DMN_KNOT_CHAIN, 1000)) return;
            }
            else if (state.stage == DMN_KNOT_CHAIN)
            {
                GameObject* chain = GetClosestGameObjectWithEntry(m_creature, GO_KNOTS_BALL_AND_CHAIN, 10.0f);
                if (!chain) { Set(state.stage, DmnScene::RetryDelay); return; }
                if (chain->GetLootState() != GO_JUST_DEACTIVATED && chain->IsSpawned()) chain->Use(m_creature);
                if (chain->IsSpawned() && chain->GetLootState() != GO_JUST_DEACTIVATED)
                { Set(state.stage, DmnScene::RetryDelay); return; }
                if (!Set(DMN_KNOT_RUN, 2000)) return;
            }
            else if (state.stage == DMN_KNOT_RUN)
            {
                m_creature->SetWalk(false);
                if (!Set(DMN_KNOT_MOVE, 2000)) return;
            }
            else if (state.stage == DMN_KNOT_MOVE)
            {
                m_creature->GetMotionMaster()->MovePoint(0, 500.420013428f, 540.966003418f, -25.384599686f);
                if (!Set(DMN_KNOT_DESPAWN, 9000)) return;
            }
            else if (state.stage == DMN_KNOT_DESPAWN)
            {
                if (north->FinishKnot(m_creature)) m_creature->ForcedDespawn();
                return;
            }
            else return;
            if (!budget) return;
        }
    }
};

struct npc_dmn_slipkikAI : public CreatureEventAI
{
    explicit npc_dmn_slipkikAI(Creature* creature) : CreatureEventAI(creature) {}
    uint32 m_retry = 0;
    void JustRespawned() override { CreatureEventAI::JustRespawned(); m_retry = 0; }
    void UpdateAI(uint32 diff) override
    {
        auto* north = NorthFor(m_creature);
        if (north && north->IsCurrentActor(m_creature) && m_creature->IsAlive() && north->IsSlipkikFrozen())
        {
            if (m_retry > diff) m_retry -= diff;
            else
            {
                m_retry = DmnScene::RetryDelay;
                if (!m_creature->HasAura(SPELL_ICE_LOCK))
                    m_creature->CastSpell(m_creature, SPELL_ICE_LOCK, TRIGGERED_OLD_TRIGGERED);
                // Never make a failed restoration into a completed freeze.
                if (m_creature->HasAura(SPELL_ICE_LOCK))
                {
                    if (m_creature->IsInCombat()) CreatureEventAI::EnterEvadeMode();
                    m_creature->SetImmuneToPlayer(true);
                    uint32 faction = north->IsSlipkikGordok() ? FACTION_OGRE : FACTION_HOSTILE;
                    if (m_creature->GetFaction() != faction)
                        m_creature->SetFactionTemporary(faction, TEMPFACTION_RESTORE_RESPAWN);
                }
            }
        }
        // Preserve all six native rows, including interrupt and ranged flags.
        if (m_creature->IsAlive()) CreatureEventAI::UpdateAI(diff);
    }
};

namespace
{
UnitAI* GetAI_mizzle(Creature* c) { return new npc_dmn_mizzleAI(c); }
UnitAI* GetAI_kromcrush(Creature* c) { return new npc_dmn_kromcrushAI(c); }
UnitAI* GetAI_knot(Creature* c) { return new npc_dmn_knotAI(c); }
UnitAI* GetAI_slipkik(Creature* c) { return new npc_dmn_slipkikAI(c); }
bool Hello_mizzle(Player* p, Creature* c) { auto* ai = dynamic_cast<npc_dmn_mizzleAI*>(c->AI()); return ai && ai->Menu(p); }
bool Hello_kromcrush(Player* p, Creature* c) { auto* ai = dynamic_cast<npc_dmn_kromcrushAI*>(c->AI()); return ai && ai->Menu(p); }
bool Select_mizzle(Player* p, Creature* c, uint32 s, uint32 a) { auto* ai = dynamic_cast<npc_dmn_mizzleAI*>(c->AI()); return ai && ai->Select(p, s, a); }
bool Select_kromcrush(Player* p, Creature* c, uint32 s, uint32 a) { auto* ai = dynamic_cast<npc_dmn_kromcrushAI*>(c->AI()); return ai && ai->Select(p, s, a); }
bool Reward_knot(Player* p, Creature* c, Quest const* q) { auto* ai = dynamic_cast<npc_dmn_knotAI*>(c->AI()); return ai && ai->Reward(p, q); }
}
void AddSC_dire_maul_north_creatures()
{
    Script* script = new Script;
    script->Name = "npc_dmn_mizzle"; script->GetAI = &GetAI_mizzle;
    script->pGossipHello = &Hello_mizzle; script->pGossipSelect = &Select_mizzle; script->RegisterSelf();
    script = new Script;
    script->Name = "npc_dmn_kromcrush"; script->GetAI = &GetAI_kromcrush;
    script->pGossipHello = &Hello_kromcrush; script->pGossipSelect = &Select_kromcrush; script->RegisterSelf();
    script = new Script;
    script->Name = "npc_dmn_knot"; script->GetAI = &GetAI_knot;
    script->pQuestRewardedNPC = &Reward_knot; script->RegisterSelf();
    script = new Script;
    script->Name = "npc_dmn_slipkik"; script->GetAI = &GetAI_slipkik; script->RegisterSelf();
}
