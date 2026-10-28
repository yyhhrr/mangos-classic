/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "dire_maul.h"
#include "dire_maul_east.h"
#include "dire_maul_east_creatures.h"
#include <map>

namespace
{
enum { DME_ROUTE_TIMEOUT = 60000, DME_RETRY_DELAY = 1000, DME_MAX_ATTEMPTS = 5, SPELL_PUSILLIN_TRANSFORM = 22735 };
Position const ironbarkRoute[5] =
{
    {-46.5028992f, -268.536011f, -57.4977989f, 0.0f},
    {-10.6114998f, -281.675995f, -53.7415009f, 0.0f},
    {17.2824001f, -286.946991f, -52.2817993f, 0.0f},
    {74.1724014f, -272.765015f, -54.8266983f, 0.0f},
    {117.401001f, -277.829987f, -55.8552017f, 0.0f},
};
Position const pusillinRoute[38] =
{
    {86.1934967f, -197.888f, -4.05657005f, 0.0f},
    {34.1274986f, -199.425995f, -4.13395977f, 0.0f},
    {-14.7334003f, -200.126007f, -4.12813997f, 0.0f},
    {-60.0110016f, -200.013f, -4.12135983f, 0.0f},
    {-101.190002f, -198.520004f, -4.10678005f, 0.0f},
    {-145.653f, -196.347f, -4.22942019f, 0.0f},
    {-162.492004f, -217.0f, -4.16860008f, 0.0f},
    {-154.345993f, -248.684998f, -4.14807987f, 0.0f},
    {-150.171005f, -278.347992f, -4.14787006f, 0.0f},
    {-153.940002f, -296.790009f, -4.16216993f, 0.0f},
    {-140.615005f, -316.778992f, -4.09557009f, 0.0f},
    {-152.940002f, -347.556f, -4.15129995f, 0.0f},
    {-135.044998f, -353.381012f, -4.10267019f, 0.0f},
    {-111.202003f, -354.226013f, -4.08814001f, 0.0f},
    {-77.8243027f, -348.997986f, -4.05656004f, 0.0f},
    {-36.0909004f, -356.858002f, -4.13243008f, 0.0f},
    {2.71265006f, -354.184998f, -4.11718988f, 0.0f},
    {40.973999f, -351.713989f, -4.07476997f, 0.0f},
    {107.902f, -355.903992f, -4.12403011f, 0.0f},
    {110.332001f, -376.069f, -2.89068007f, 0.0f},
    {111.228996f, -397.17099f, -3.27356005f, 0.0f},
    {111.639999f, -417.140991f, -3.27357006f, 0.0f},
    {111.973f, -443.786987f, -3.27357006f, 0.0f},
    {112.216003f, -466.26001f, -3.27357006f, 0.0f},
    {112.223999f, -488.812012f, -6.88435984f, 0.0f},
    {111.822998f, -510.511993f, -6.90470982f, 0.0f},
    {111.677002f, -532.219971f, -11.0802002f, 0.0f},
    {102.893997f, -541.950989f, -11.0810003f, 0.0f},
    {85.3122025f, -544.937988f, -13.3078003f, 0.0f},
    {62.3759995f, -551.554993f, -16.8896008f, 0.0f},
    {50.4953995f, -566.22998f, -19.4087009f, 0.0f},
    {50.4384003f, -631.018005f, -25.1082001f, 0.0f},
    {32.7456017f, -687.052979f, -25.1616001f, 0.0f},
    {2.18578005f, -703.163025f, -25.1616001f, 0.0f},
    {2.24896002f, -673.875977f, -13.6079998f, 0.0f},
    {12.2960997f, -668.86499f, -12.6426001f, 0.0f},
    {21.7663002f, -676.580994f, -12.6426001f, 0.0f},
    {12.7601004f, -706.166992f, -12.6426001f, 0.0f},
};


DireMaulEast* EastFor(Creature* creature)
{
    if (!creature || creature->GetMapId() != 429) return nullptr;
    auto* owner = static_cast<instance_dire_maul*>(creature->GetInstanceData());
    return owner ? &owner->East() : nullptr;
}
bool CanUseDmeScene(Player* player, Creature* creature)
{
    return player && player->IsAlive() && creature->IsAlive() && !creature->IsInCombat() &&
        player->GetMap() == creature->GetMap() && creature->IsWithinDistInMap(player, INTERACTION_DISTANCE);
}
void StartRoute(Creature* creature, uint32 node)
{
    // MoveIdle pushes a static idle generator and does not clear the old path
    // or UNIT_STAT_WAYPOINT_PAUSED. Explicitly reset both before a new path.
    creature->GetMotionMaster()->Clear(false, true);
    creature->GetMotionMaster()->MoveIdle();
    creature->GetMotionMaster()->UnpauseWaypoints();
    creature->GetMotionMaster()->MoveWaypoint();
    if (node) creature->GetMotionMaster()->SetNextWaypoint(node);
}
void RestoreRoute(Creature* creature, Position const* route, uint32 node)
{
    if (node)
    {
        Position const& point = route[node - 1];
        creature->NearTeleportTo(point.x, point.y, point.z, point.o);
    }
    StartRoute(creature, node);
}
void PrepareSceneMenu(Player* player, Creature* creature)
{
    // Keep the actual DB menus, localized broadcast options, waypoint conditions
    // and quest menu. Only the old action_script_id is handed to this AI.
    player->PrepareGossipMenu(creature, creature->GetCreatureInfo()->GossipMenuId);
    player->SendPreparedGossip(creature);
}
}

struct npc_dme_ironbarkAI : public ScriptedAI
{
    explicit npc_dme_ironbarkAI(Creature* creature) : ScriptedAI(creature) {}
    bool m_restore = true;
    uint32 m_attempts = 0;
    std::map<ObjectGuid, uint32> m_menus;

    bool Set(uint32 stage, uint32 node, uint32 timer)
    {
        DireMaulEast* east = EastFor(m_creature);
        return east && east->SetIronbarkState(m_creature, {stage, node, timer});
    }
    void Reset() override { m_attempts = 0; m_menus.clear(); }
    void JustRespawned() override { ScriptedAI::JustRespawned(); m_restore = true; }
    bool Menu(Player* player)
    {
        if (DireMaulEast* east = EastFor(m_creature))
        {
            auto state = east->GetIronbarkState();
            if (state.stage == DME_IRON_READY && CanUseDmeScene(player, m_creature)) m_menus[player->GetObjectGuid()] = state.stage;
        }
        PrepareSceneMenu(player, m_creature);
        return true;
    }
    bool Select(Player* player, uint32 sender, uint32 action)
    {
        player->CLOSE_GOSSIP_MENU();
        auto menu = m_menus.find(player->GetObjectGuid());
        DireMaulEast* east = EastFor(m_creature);
        if (!east || sender || action != GOSSIP_OPTION_GOSSIP || !CanUseDmeScene(player, m_creature) || menu == m_menus.end()) return true;
        auto state = east->GetIronbarkState();
        if (state.stage != DME_IRON_READY || menu->second != state.stage || !east->IsIronbarkRedeemed()) return true;
        if (Set(DME_IRON_WAIT_DROP, state.node, 1000)) { m_attempts = 0; m_menus.clear(); }
        return true;
    }
    void MovementInform(uint32 type, uint32 point) override
    {
        if (type != WAYPOINT_MOTION_TYPE || point < 1 || point > 5) return;
        DireMaulEast* east = EastFor(m_creature);
        if (!east) return;
        auto state = east->GetIronbarkState();
        if (state.stage != DME_IRON_WALKING) { m_creature->GetMotionMaster()->PauseWaypoints(0); return; }
        if (point > state.node) m_attempts = 0;
        if (point == 5)
        {
            m_creature->GetMotionMaster()->PauseWaypoints(0);
            Set(DME_IRON_WAIT_EMOTE_1, point, 1000);
        }
        else Set(state.stage, point, DME_ROUTE_TIMEOUT);
    }
    void ReadyForRetry(uint32 node)
    {
        if (!Set(DME_IRON_READY, node, 0)) return;
        m_creature->GetMotionMaster()->MoveIdle();
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        m_creature->SetActiveObjectState(false);
        m_menus.clear();
    }
    void UpdateAI(uint32 diff) override
    {
        DireMaulEast* east = EastFor(m_creature);
        if (!east || !m_creature->IsAlive()) return;
        auto state = east->GetIronbarkState();
        if (!east->SetIronbarkState(m_creature, state)) return; // Current actor only.
        if (state.stage == DME_IRON_LOCKED)
        {
            if (!east->IsIronbarkRedeemed()) return;
            if (!Set(DME_IRON_READY, 0, 0)) return;
            state = east->GetIronbarkState();
        }
        if (m_creature->GetEntry() == NPC_OLD_IRONBARK)
        {
            m_creature->UpdateEntry(NPC_IRONBARK_REDEEMED);
            return; // UpdateEntry may replace the AI; never use this afterward.
        }
        if (m_restore)
        {
            m_restore = false;
            if (state.node)
            {
                Position const& point = ironbarkRoute[state.node - 1];
                m_creature->NearTeleportTo(point.x, point.y, point.z, point.o);
            }
            if (state.stage == DME_IRON_WALKING) RestoreRoute(m_creature, ironbarkRoute, state.node);
            if (state.stage >= DME_IRON_WAIT_SAY) m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            m_creature->SetStandState(state.stage == DME_IRON_WAIT_DESPAWN ? UNIT_STAND_STATE_DEAD : UNIT_STAND_STATE_STAND);
        }
        if (state.stage == DME_IRON_READY) { m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP); return; }
        uint32 remaining = diff;
        for (uint32 transitions = 0; transitions < 16; ++transitions)
        {
            state = east->GetIronbarkState();
            if (state.stage == DME_IRON_DONE)
            {
                m_creature->SetActiveObjectState(false);
                m_creature->ForcedDespawn();
                return;
            }
            if (state.timer > remaining) { Set(state.stage, state.node, state.timer - remaining); return; }
            remaining -= state.timer;
            switch (state.stage)
            {
                case DME_IRON_WAIT_DROP:
                    if (!Set(DME_IRON_WAIT_SAY, state.node, 2000)) return;
                    m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP); break;
                case DME_IRON_WAIT_SAY:
                    if (!Set(DME_IRON_WAIT_WALK, state.node, 4000)) return;
                    DoBroadcastText(9099, m_creature); break;
                case DME_IRON_WAIT_WALK:
                    if (state.node == 5)
                    {
                        // A missing-door retry is already at the final point.
                        // Do not publish the impossible WALKING/node5 state.
                        if (!Set(DME_IRON_WAIT_EMOTE_1, 5, 1000)) return;
                        m_creature->SetActiveObjectState(true);
                        break;
                    }
                    if (!Set(DME_IRON_WALKING, state.node, DME_ROUTE_TIMEOUT)) return;
                    m_creature->SetActiveObjectState(true);
                    StartRoute(m_creature, state.node ? std::min(state.node + 1, 5u) : 0);
                    break;
                case DME_IRON_WALKING:
                    if (++m_attempts >= DME_MAX_ATTEMPTS) { ReadyForRetry(state.node); return; }
                    Set(state.stage, state.node, DME_ROUTE_TIMEOUT);
                    RestoreRoute(m_creature, ironbarkRoute, state.node); return;
                case DME_IRON_WAIT_EMOTE_1:
                    if (!Set(DME_IRON_WAIT_EMOTE_2, 5, 5000)) return;
                    m_creature->HandleEmote(35); break;
                case DME_IRON_WAIT_EMOTE_2:
                    if (!Set(DME_IRON_WAIT_OPEN, 5, 3000)) return;
                    m_creature->HandleEmote(35); break;
                case DME_IRON_WAIT_OPEN:
                    if (!east->OpenIronbarkDoor(m_creature))
                    {
                        if (++m_attempts >= DME_MAX_ATTEMPTS) ReadyForRetry(5);
                        else Set(state.stage, 5, DME_RETRY_DELAY);
                        return;
                    }
                    m_attempts = 0;
                    if (!Set(DME_IRON_WAIT_FAKE, 5, 5000)) return;
                    break;
                case DME_IRON_WAIT_FAKE:
                    if (!Set(DME_IRON_WAIT_DESPAWN, 5, 11000)) return;
                    m_creature->SetStandState(UNIT_STAND_STATE_DEAD); break;
                case DME_IRON_WAIT_DESPAWN:
                    if (!Set(DME_IRON_DONE, 5, 0)) return;
                    m_creature->SetActiveObjectState(false);
                    m_creature->ForcedDespawn(); return;
                default: return;
            }
            if (!remaining) return;
        }
    }
};

struct npc_dme_pusillinAI : public CreatureEventAI
{
    explicit npc_dme_pusillinAI(Creature* creature) : CreatureEventAI(creature) {}
    bool m_restore = true;
    uint32 m_castAttempts = 0, m_moveAttempts = 0;
    std::map<ObjectGuid, uint32> m_menus;

    bool Set(uint32 stage, uint32 node, uint32 timer)
    {
        DireMaulEast* east = EastFor(m_creature);
        return east && east->SetPusillinState(m_creature, {stage, node, timer});
    }
    static bool Waiting(uint32 stage) { return stage == 1 || stage == 3 || stage == 5 || stage == 7 || stage == 9 || stage == DME_PU_RETRY_READY; }
    static uint32 RunTarget(uint32 stage)
    {
        return stage == DME_PU_RUN_9 || stage == DME_PU_RUN_9_SAY ? 9 :
            stage == DME_PU_RUN_19 || stage == DME_PU_RUN_19_SAY ? 19 :
            stage == DME_PU_RUN_32 || stage == DME_PU_RUN_32_SAY ? 32 :
            stage == DME_PU_RUN_38 || stage == DME_PU_RUN_38_SAY ? 38 : 0;
    }
    static uint32 WaitStage(uint32 point) { return point == 1 ? 1 : point == 9 ? 3 : point == 19 ? 5 : point == 32 ? 7 : 9; }
    void Reset() override { CreatureEventAI::Reset(); m_castAttempts = m_moveAttempts = 0; m_menus.clear(); }
    void JustRespawned() override { CreatureEventAI::JustRespawned(); m_restore = true; }
    void JustDied(Unit* killer) override
    {
        if (DireMaulEast* east = EastFor(m_creature)) east->PusillinDied(m_creature);
        CreatureEventAI::JustDied(killer);
    }
    void JustReachedHome() override
    {
        CreatureEventAI::JustReachedHome();
        if (DireMaulEast* east = EastFor(m_creature))
            if (east->IsPusillinTransformed() && east->GetPusillinState().stage == DME_PU_COMBAT)
                TryTransform(true);
    }
    bool Menu(Player* player)
    {
        if (DireMaulEast* east = EastFor(m_creature))
        {
            auto state = east->GetPusillinState();
            if (Waiting(state.stage) && CanUseDmeScene(player, m_creature)) m_menus[player->GetObjectGuid()] = state.stage;
        }
        PrepareSceneMenu(player, m_creature);
        return true;
    }
    bool Select(Player* player, uint32 sender, uint32 action)
    {
        player->CLOSE_GOSSIP_MENU();
        auto menu = m_menus.find(player->GetObjectGuid());
        DireMaulEast* east = EastFor(m_creature);
        if (!east || sender || action != GOSSIP_OPTION_GOSSIP || !CanUseDmeScene(player, m_creature) || menu == m_menus.end()) return true;
        auto state = east->GetPusillinState();
        if (!Waiting(state.stage) || state.stage != menu->second || east->IsPusillinDead()) return true;
        uint32 stage = state.stage == DME_PU_RETRY_READY ? DME_PU_CONFIRM_FINAL : state.stage == DME_PU_WAIT_38 ? DME_PU_FINAL_SAY : 16 + (state.stage - 1) / 2;
        if (!Set(stage, state.node, stage == DME_PU_CONFIRM_FINAL ? 0 : 1000)) return true;
        m_castAttempts = m_moveAttempts = 0; m_menus.clear();
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        if (stage >= DME_PU_RUN_9_SAY && stage <= DME_PU_RUN_38_SAY) m_creature->GetMotionMaster()->UnpauseWaypoints();
        if (stage == DME_PU_CONFIRM_FINAL) east->RetryPusillinAdds(m_creature);
        return true;
    }
    void MovementInform(uint32 type, uint32 point) override
    {
        CreatureEventAI::MovementInform(type, point);
        if (type != WAYPOINT_MOTION_TYPE || point < 1 || point > 38) return;
        DireMaulEast* east = EastFor(m_creature);
        if (!east) return;
        auto state = east->GetPusillinState();
        uint32 target = RunTarget(state.stage);
        if (state.stage == DME_PU_INITIAL && point == 1)
        {
            m_creature->SetWalk(false); m_creature->GetMotionMaster()->PauseWaypoints(0);
            Set(DME_PU_WAIT_1, point, 0);
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        }
        else if (Waiting(state.stage) || (state.stage >= DME_PU_WAIT_38 && !target))
        {
            m_creature->GetMotionMaster()->PauseWaypoints(0);
            if (Waiting(state.stage)) m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        }
        else if (target)
        {
            if (point > state.node) m_moveAttempts = 0;
            if (point == target)
            {
                m_creature->GetMotionMaster()->PauseWaypoints(0);
                if (state.stage >= DME_PU_RUN_9_SAY) Set(state.stage, point, state.timer);
                else
                {
                    Set(WaitStage(point), point, 0);
                    m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                }
            }
            else Set(state.stage, point, state.stage >= DME_PU_RUN_9_SAY ? state.timer : DME_ROUTE_TIMEOUT);
        }
    }
    void RetryReady()
    {
        if (!Set(DME_PU_RETRY_READY, 38, 0)) return;
        m_creature->SetFactionTemporary(35, TEMPFACTION_RESTORE_RESPAWN);
        m_creature->GetMotionMaster()->PauseWaypoints(0);
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        m_menus.clear();
    }
    bool TryTransform(bool restoring)
    {
        DireMaulEast* east = EastFor(m_creature);
        if (!east || east->IsPusillinDead()) return false;
        if (!m_creature->HasAura(SPELL_PUSILLIN_TRANSFORM))
        {
            ++m_castAttempts;
            DoCastSpellIfCan(m_creature, SPELL_PUSILLIN_TRANSFORM, restoring ? CAST_TRIGGERED : 0);
            if (!m_creature->HasAura(SPELL_PUSILLIN_TRANSFORM))
            {
                if (m_castAttempts >= DME_MAX_ATTEMPTS) RetryReady();
                else Set(DME_PU_CONFIRM_FINAL, 38, DME_RETRY_DELAY);
                return false;
            }
        }
        m_castAttempts = 0;
        // Actual transformation acknowledged before committing the final state.
        m_creature->SetFactionTemporary(90, TEMPFACTION_RESTORE_RESPAWN);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        east->PusillinTransformed(m_creature);
        return true;
    }
    void UpdateScene(uint32 diff)
    {
        DireMaulEast* east = EastFor(m_creature);
        if (!east || !m_creature->IsAlive()) return;
        auto state = east->GetPusillinState();
        if (east->IsPusillinDead()) { m_creature->ForcedDespawn(); return; }
        if (!east->SetPusillinState(m_creature, state)) return;
        if (m_restore)
        {
            m_restore = false;
            m_creature->SetWalk(false);
            m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
            RestoreRoute(m_creature, pusillinRoute, state.node);
            if (state.stage == DME_PU_COMBAT && east->IsPusillinTransformed())
            {
                if (east->PusillinAddsFailed()) RetryReady();
                else if (!east->ArePusillinAddsReady()) Set(DME_PU_CONFIRM_FINAL, 38, DME_RETRY_DELAY);
                else TryTransform(true);
            }
        }
        if (state.stage == DME_PU_INITIAL || Waiting(state.stage) || state.stage == DME_PU_COMBAT || state.stage == DME_PU_DEAD) return;
        uint32 remaining = diff;
        for (uint32 transitions = 0; transitions < 12; ++transitions)
        {
            state = east->GetPusillinState();
            if (state.timer > remaining) { Set(state.stage, state.node, state.timer - remaining); return; }
            remaining -= state.timer;
            if (state.stage >= DME_PU_RUN_9_SAY && state.stage <= DME_PU_RUN_38_SAY)
            {
                static uint32 const texts[] = {9349,9353,9357,9360};
                uint32 run = 2 + (state.stage - DME_PU_RUN_9_SAY) * 2;
                uint32 target = RunTarget(run);
                if (!Set(state.node == target ? WaitStage(target) : run, state.node, state.node == target ? 0 : DME_ROUTE_TIMEOUT)) return;
                DoBroadcastText(texts[state.stage - DME_PU_RUN_9_SAY], m_creature);
                if (state.node == target) m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
                m_creature->SetActiveObjectState(true);
            }
            else if (state.stage == DME_PU_FINAL_SAY)
            {
                if (!Set(DME_PU_WAIT_ADDS, 38, 2000)) return;
                DoBroadcastText(9363, m_creature);
            }
            else if (RunTarget(state.stage))
            {
                if (++m_moveAttempts >= DME_MAX_ATTEMPTS)
                {
                    // Return to the preceding real dialogue checkpoint. No
                    // additional encounter or final summons have been started.
                    uint32 target = RunTarget(state.stage);
                    uint32 previous = target == 9 ? 1 : target == 19 ? 9 : target == 32 ? 19 : 32;
                    Set(WaitStage(previous), previous, 0);
                    RestoreRoute(m_creature, pusillinRoute, previous);
                    return;
                }
                Set(state.stage, state.node, DME_ROUTE_TIMEOUT);
                RestoreRoute(m_creature, pusillinRoute, state.node); return;
            }
            else if (state.stage == DME_PU_WAIT_ADDS)
            {
                east->QueuePusillinAdds(m_creature);
                if (!Set(DME_PU_WAIT_FORM, 38, 1000)) return;
            }
            else if (state.stage == DME_PU_WAIT_FORM || state.stage == DME_PU_CONFIRM_FINAL)
            {
                if (east->PusillinAddsFailed()) { RetryReady(); return; }
                if (!east->ArePusillinAddsReady()) { Set(DME_PU_CONFIRM_FINAL, 38, DME_RETRY_DELAY); return; }
                TryTransform(east->IsPusillinTransformed()); return;
            }
            else return;
            if (!remaining) return;
        }
    }
    void UpdateAI(uint32 diff) override
    {
        UpdateScene(diff);
        // Existing five EventAI rows retain their precise spells, selection,
        // ranged/melee timers and35m ranged mode. No duplicate combat scheduler.
        if (m_creature->IsAlive()) CreatureEventAI::UpdateAI(diff);
    }
};

UnitAI* GetAI_npc_dme_ironbark(Creature* creature) { return new npc_dme_ironbarkAI(creature); }
UnitAI* GetAI_npc_dme_pusillin(Creature* creature) { return new npc_dme_pusillinAI(creature); }
bool GossipHello_npc_dme_ironbark(Player* player, Creature* creature)
{
    if (auto* ai = dynamic_cast<npc_dme_ironbarkAI*>(creature->AI())) return ai->Menu(player);
    return false;
}
bool GossipHello_npc_dme_pusillin(Player* player, Creature* creature)
{
    if (auto* ai = dynamic_cast<npc_dme_pusillinAI*>(creature->AI())) return ai->Menu(player);
    return false;
}
bool GossipSelect_npc_dme_ironbark(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (auto* ai = dynamic_cast<npc_dme_ironbarkAI*>(creature->AI())) return ai->Select(player, sender, action);
    return false;
}
bool GossipSelect_npc_dme_pusillin(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    if (auto* ai = dynamic_cast<npc_dme_pusillinAI*>(creature->AI())) return ai->Select(player, sender, action);
    return false;
}
void AddSC_dire_maul_east_creatures()
{
    Script* script = new Script;
    script->Name = "npc_dme_ironbark";
    script->GetAI = &GetAI_npc_dme_ironbark;
    script->pGossipHello = &GossipHello_npc_dme_ironbark;
    script->pGossipSelect = &GossipSelect_npc_dme_ironbark;
    script->RegisterSelf();
    script = new Script;
    script->Name = "npc_dme_pusillin";
    script->GetAI = &GetAI_npc_dme_pusillin;
    script->pGossipHello = &GossipHello_npc_dme_pusillin;
    script->pGossipSelect = &GossipSelect_npc_dme_pusillin;
    script->RegisterSelf();
}
