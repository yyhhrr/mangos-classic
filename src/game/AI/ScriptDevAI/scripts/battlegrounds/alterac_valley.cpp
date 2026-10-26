/*
 * Alterac Valley support events. Licensed under GPL version 2 or later.
 * Routes and event actors are based on VMaNGOS; existing database EventAI
 * remains responsible for combat. Reinforcement/capture rules are unchanged.
 */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/include/sc_grid_searchers.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "BattleGround/BattleGroundAV.h"
#include "MotionGenerators/WaypointManager.h"
#include "Spells/Spell.h"
#include "AlteracValleyEventRules.h"
#include <cmath>
#include <sstream>

namespace
{
    using Phase = AVEvents::Phase;
    constexpr uint32 ACTION_PROGRESS = GOSSIP_ACTION_INFO_DEF + 1;
    constexpr uint32 ACTION_RESCUE = GOSSIP_ACTION_INFO_DEF + 2;
    constexpr uint32 ACTION_BEGIN = GOSSIP_ACTION_INFO_DEF + 3;
    constexpr uint32 ACTION_VENDOR = GOSSIP_ACTION_INFO_DEF + 4;
    constexpr uint32 INVOCATION = 11206;
    constexpr uint32 ROUTE_POINT_BASE = 10000;

    BattleGroundAV* GetAV(WorldObject* object)
    {
        if (!object || !object->GetMap() || !object->GetMap()->IsBattleGround())
            return nullptr;
        return dynamic_cast<BattleGroundAV*>(static_cast<BattleGroundMap*>(object->GetMap())->GetBG());
    }

    struct SupportInfo
    {
        PvpTeamIndex team = TEAM_INDEX_NEUTRAL;
        AVSupportType type = AV_SUPPORT_MAX;
        bool commander = false;
        bool quartermaster = false;
    };

    SupportInfo GetInfo(uint32 entry)
    {
        switch (entry)
        {
            case 13438: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_AIR_1, true};
            case 13439: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_AIR_2, true};
            case 13437: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_AIR_3, true};
            case 13179: return {TEAM_INDEX_HORDE, AV_SUPPORT_AIR_1, true};
            case 13180: return {TEAM_INDEX_HORDE, AV_SUPPORT_AIR_2, true};
            case 13181: return {TEAM_INDEX_HORDE, AV_SUPPORT_AIR_3, true};
            case 12096: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_GROUND, false, true};
            case 12097: return {TEAM_INDEX_HORDE, AV_SUPPORT_GROUND, false, true};
            case 13446: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_GROUND};
            case 13449: return {TEAM_INDEX_HORDE, AV_SUPPORT_GROUND};
            case 13577: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_CAVALRY};
            case 13441: return {TEAM_INDEX_HORDE, AV_SUPPORT_CAVALRY};
            case 13442: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_ELEMENTAL};
            case 13236: return {TEAM_INDEX_HORDE, AV_SUPPORT_ELEMENTAL};
            case 13257: return {TEAM_INDEX_ALLIANCE, AV_SUPPORT_MAX};
            case 13176: return {TEAM_INDEX_HORDE, AV_SUPPORT_MAX};
            default: return {};
        }
    }

    bool FriendlyPlayer(Player* player, Creature* creature, PvpTeamIndex team)
    {
        BattleGroundAV* bg = GetAV(creature);
        return player && creature->IsAlive() && player->IsAlive() && team < PVP_TEAM_COUNT &&
            bg && bg->GetStatus() == STATUS_IN_PROGRESS && player->GetMap() == creature->GetMap() &&
            player->GetBattleGround() == bg && GetTeamIndexByTeamId(player->GetTeam()) == team &&
            player->IsWithinDistInMap(creature, INTERACTION_DISTANCE);
    }

    bool CopyRoute(uint32 entry, std::vector<WaypointNode>& route)
    {
        WaypointPath* path = sWaypointMgr.GetPathFromOrigin(entry, 0, 0, PATH_FROM_ENTRY);
        if (!path || path->empty())
            return false;
        route.clear();
        for (auto const& point : *path)
            route.push_back(point.second);
        return true;
    }

    char const* StageText(AVSupportStage stage)
    {
        switch (stage)
        {
            case AV_SUPPORT_COLLECTING: return "\xe6\xad\xa3\xe5\x9c\xa8\xe6\x94\xb6\xe9\x9b\x86\xe7\x89\xa9\xe8\xb5\x84";
            case AV_SUPPORT_READY: return "\xe5\xb7\xb2\xe7\xbb\x8f\xe5\x87\x86\xe5\xa4\x87\xe5\xb0\xb1\xe7\xbb\xaa";
            case AV_SUPPORT_ACTIVE: return "\xe6\xad\xa3\xe5\x9c\xa8\xe6\x89\xa7\xe8\xa1\x8c";
            case AV_SUPPORT_DONE: return "\xe6\x9c\xac\xe5\x9c\xba\xe5\xb7\xb2\xe7\xbb\x8f\xe5\x87\xba\xe5\x8a\xa8";
            default: return "\xe6\x88\x98\xe5\x9c\xba\xe5\xb0\x9a\xe6\x9c\xaa\xe5\xbc\x80\xe5\xa7\x8b\xe6\x88\x96\xe5\xb7\xb2\xe7\xbb\x8f\xe7\xbb\x93\xe6\x9d\x9f";
        }
    }
}

struct npc_av_supportAI : public CreatureEventAI
{
    explicit npc_av_supportAI(Creature* creature) : CreatureEventAI(creature),
        m_info(GetInfo(creature->GetEntry())), m_originalHome(creature->GetRespawnPosition()),
        m_originalNpcFlags(creature->GetUInt32Value(UNIT_NPC_FLAGS))
    {
        InitializeMenu();
    }

    SupportInfo m_info;
    Position m_originalHome;
    uint32 m_originalNpcFlags;
    Phase m_phase = Phase::Idle;
    std::vector<WaypointNode> m_route;
    size_t m_point = 0;
    bool m_moving = false;
    bool m_controlsEvent = false;
    bool m_eventActor = false;
    uint32 m_wait = 0, m_tick = 1000, m_stalled = 0, m_routeTimeout = 0, m_ritualDelay = 0;
    ObjectGuid m_follow, m_ritual;
    GuidVector m_children;
    AVEvents::Participants<ObjectGuid> m_participants;
    AVEvents::Once m_summoned;

    void InitializeMenu()
    {
        if (m_info.team >= PVP_TEAM_COUNT)
            return;
        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        if (m_info.commander)
        {
            if (BattleGroundAV* bg = GetAV(m_creature))
                if (!bg->IsCommanderRescued(m_creature->GetEntry()))
                    m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        }
    }

    void Reset() override
    {
        CreatureEventAI::Reset(); // Combat resets must not restart the support event.
    }

    void JustRespawned() override
    {
        m_phase = Phase::Idle;
        m_route.clear(); m_point = 0; m_moving = false;
        m_controlsEvent = false; m_eventActor = false;
        m_follow.Clear(); m_ritual.Clear(); m_children.clear();
        m_participants.Clear(); m_summoned.Reset();
        m_wait = 0; m_tick = 1000; m_stalled = 0; m_routeTimeout = 0;
        CreatureEventAI::JustRespawned();
        m_creature->SetUInt32Value(UNIT_NPC_FLAGS, m_originalNpcFlags);
        InitializeMenu();
        // A rescued commander respawns at camp, not in prison, for this match.
        if (m_info.commander)
            if (BattleGroundAV* bg = GetAV(m_creature))
                if (bg->IsCommanderRescued(m_creature->GetEntry()))
                {
                    std::vector<WaypointNode> path;
                    if (CopyRoute(m_creature->GetEntry(), path))
                    {
                        auto const& p = path.back();
                        m_creature->NearTeleportTo(p.x, p.y, p.z, m_creature->GetOrientation());
                        m_creature->SetRespawnCoord(p.x, p.y, p.z, m_creature->GetOrientation());
                        m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                        m_creature->GetMotionMaster()->MoveIdle();
                    }
                }
    }

    void EnterCombat(Unit* enemy) override
    {
        m_moving = false;
        m_creature->Unmount();
        if (m_phase == Phase::Ritual || m_phase == Phase::Channel)
            CancelChannels();
        CreatureEventAI::EnterCombat(enemy);
    }

    void JustReachedHome() override
    {
        CreatureEventAI::JustReachedHome();
        m_moving = false;
    }

    void JustDied(Unit* killer) override
    {
        if (m_controlsEvent)
            Finish(false);
        if (m_phase == Phase::Rescue || !m_info.commander)
            RestoreHome();
        m_phase = Phase::Idle;
        CreatureEventAI::JustDied(killer);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 miscValue) override
    {
        if (eventType == AI_EVENT_CUSTOM_A && miscValue == 0x4156434c)
        {
            m_controlsEvent = false;
            CancelChannels(); DeleteRitual(); CleanupChildren();
            m_phase = Phase::Idle;
            return;
        }
        CreatureEventAI::ReceiveAIEvent(eventType, sender, invoker, miscValue);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        if (m_phase == Phase::Air)
        {
            if (who && !m_creature->GetVictim() && who->IsAlive() && m_creature->CanAttackOnSight(who) &&
                who->IsVisibleForOrDetect(m_creature, m_creature, true) &&
                m_creature->IsWithinDistInMap(who, 50.0f) && m_creature->IsWithinLOSInMap(who))
                AttackStart(who);
            return;
        }
        CreatureEventAI::MoveInLineOfSight(who);
    }

    void RestoreHome()
    {
        m_creature->SetRespawnCoord(m_originalHome.x, m_originalHome.y, m_originalHome.z, m_originalHome.o);
    }

    bool StartPath(uint32 entry, Phase phase)
    {
        if (!CopyRoute(entry, m_route))
            return false;
        m_phase = phase; m_point = 0; m_moving = false; m_wait = 0; m_stalled = 0; m_routeTimeout = 0;
        m_creature->SetActiveObjectState(true);
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MoveIdle();
        return true;
    }

    bool Rescue(Player* player)
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!m_info.commander || !FriendlyPlayer(player, m_creature, m_info.team) ||
            m_creature->IsInCombat() || m_phase != Phase::Idle || bg->IsCommanderRescued(m_creature->GetEntry()))
            return false;
        if (!StartPath(m_creature->GetEntry(), Phase::Rescue) || !bg->RegisterCommanderEscort(m_creature))
        { m_phase = Phase::Idle; return false; }
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
        m_creature->SetStandState(UNIT_STAND_STATE_STAND);
        m_creature->MonsterSay("\xe7\xbb\x88\xe4\xba\x8e\xe8\x87\xaa\xe7\x94\xb1\xe4\xba\x86\xef\xbc\x81\xe8\xaf\xb7\xe6\x8e\xa9\xe6\x8a\xa4\xe6\x88\x91\xe8\xbf\x94\xe5\x9b\x9e\xe8\x90\xa5\xe5\x9c\xb0\xe3\x80\x82", LANG_UNIVERSAL);
        return true;
    }

    npc_av_supportAI* SpawnActor(uint32 entry, float x, float y, float z)
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!bg || !bg->IsSupportActive(m_info.team, m_info.type))
            return nullptr;
        Creature* actor = m_creature->SummonCreature(entry, x, y, z, m_creature->GetOrientation(),
            TEMPSPAWN_DEAD_DESPAWN, 0, true, true);
        if (!actor)
            return nullptr;
        npc_av_supportAI* ai = dynamic_cast<npc_av_supportAI*>(actor->AI());
        if (!ai || !bg->RegisterSupportCreature(m_info.team, m_info.type, actor))
        { actor->ForcedDespawn(); return nullptr; }
        ai->m_info = {m_info.team, m_info.type};
        ai->m_eventActor = true;
        actor->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_VENDOR);
        m_children.push_back(actor->GetObjectGuid());
        return ai;
    }

    bool Begin(Player* player)
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!FriendlyPlayer(player, m_creature, m_info.team) || m_info.type >= AV_SUPPORT_MAX ||
            m_phase != Phase::Idle || m_creature->IsInCombat() || !bg->IsSupportReady(m_info.team, m_info.type))
            return false;
        std::vector<WaypointNode> path;
        const uint32 routeEntry = m_info.quartermaster ? (m_info.team == TEAM_INDEX_ALLIANCE ? 13446 : 13449) : m_creature->GetEntry();
        if (m_info.type > AV_SUPPORT_AIR_3 && !CopyRoute(routeEntry, path))
        { ChatHandler(player).PSendSysMessage("\xe6\x94\xaf\xe6\x8f\xb4\xe8\xb7\xaf\xe7\xba\xbf\xe6\x9c\xaa\xe5\x8a\xa0\xe8\xbd\xbd\xef\xbc\x8c\xe6\x9c\xaa\xe6\x89\xa3\xe9\x99\xa4\xe7\x89\xa9\xe8\xb5\x84\xe3\x80\x82\xe8\xaf\xb7\xe6\xa3\x80\xe6\x9f\xa5\xe5\xa5\xa5\xe5\xb1\xb1\xe6\x95\xb0\xe6\x8d\xae\xe5\xba\x93\xe6\x9b\xb4\xe6\x96\xb0\xe3\x80\x82"); return false; }
        if (!bg->TryBeginSupport(m_info.team, m_info.type))
            return false;
        m_controlsEvent = true;
        m_summoned.Reset();
        m_children.clear();
        if (m_info.quartermaster)
        {
            auto const& p = path.front();
            npc_av_supportAI* leader = SpawnActor(routeEntry, p.x, p.y, p.z);
            if (!leader || !leader->StartPath(routeEntry, Phase::March))
            { Finish(false); return false; }
            leader->m_controlsEvent = true;
            m_controlsEvent = false; // The marching leader owns death/failure/arrival.
            m_children.clear();
            if (!leader->GatherTroops())
            { leader->Finish(false); leader->m_creature->ForcedDespawn(); return false; }
        }
        else if (m_info.type <= AV_SUPPORT_AIR_3)
        {
            bg->RegisterSupportCreature(m_info.team, m_info.type, m_creature);
            m_phase = Phase::Rally; m_wait = 5000;
        }
        else
        {
            bg->RegisterSupportCreature(m_info.team, m_info.type, m_creature);
            if (!StartPath(routeEntry, Phase::March) || !GatherTroops())
            { Finish(false); return false; }
        }
        m_creature->MonsterSay("\xe7\x89\xa9\xe8\xb5\x84\xe5\xb7\xb2\xe7\xbb\x8f\xe5\xa4\x87\xe9\xbd\x90\xef\xbc\x8c\xe5\x87\x86\xe5\xa4\x87\xe5\x87\xba\xe5\x8f\x91\xef\xbc\x81", LANG_UNIVERSAL);
        ChatHandler(player).PSendSysMessage("\xe5\xa5\xa5\xe5\xb1\xb1\xe6\x94\xaf\xe6\x8f\xb4\xe5\xb7\xb2\xe5\x90\xaf\xe5\x8a\xa8\xef\xbc\x8c\xe6\x89\x80\xe9\x9c\x80\xe7\x89\xa9\xe8\xb5\x84\xe5\xb7\xb2\xe6\x89\xa3\xe9\x99\xa4\xe3\x80\x82\xe6\x9c\xac\xe6\xac\xa1\xe6\x94\xaf\xe6\x8f\xb4\xe6\x88\x90\xe5\x8a\x9f\xe5\x87\xba\xe5\x8a\xa8\xe5\x90\x8e\xef\xbc\x8c\xe6\x9c\xac\xe5\x9c\xba\xe4\xb8\x8d\xe4\xbc\x9a\xe9\x87\x8d\xe5\xa4\x8d\xe5\x90\xaf\xe5\x8a\xa8\xe3\x80\x82");
        return true;
    }

    bool GatherTroops()
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!bg || !bg->IsSupportActive(m_info.team, m_info.type))
            return false;
        const bool alliance = m_info.team == TEAM_INDEX_ALLIANCE;
        if (m_info.type == AV_SUPPORT_ELEMENTAL)
        {
            // Use the existing linked druids/shamans before creating replacements.
            const uint32 entry = alliance ? 13443 : 13284;
            CreatureList existing;
            GetCreatureListWithEntryInGrid(existing, m_creature, entry, 100.0f);
            unsigned adopted = 0;
            for (Creature* follower : existing)
                if (follower->IsAlive())
                    if (auto ai = dynamic_cast<npc_av_supportAI*>(follower->AI()))
                        if (ai->m_phase == Phase::Idle && bg->RegisterSupportCreature(m_info.team, m_info.type, follower))
                        {
                            ai->m_info = {m_info.team, m_info.type}; ai->m_eventActor = true;
                            ai->Follow(m_creature); m_children.push_back(follower->GetObjectGuid()); ++adopted;
                        }
            for (; adopted < 3; ++adopted)
            {
                auto ai = SpawnActor(entry, m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());
                if (!ai) return false;
                ai->Follow(m_creature);
            }
            return true;
        }
        uint32 entry = m_info.type == AV_SUPPORT_CAVALRY ? (alliance ? 13576 : 13440) :
            (alliance ? 13524 : 13528) + AVSupport::DefenderTier(bg->GetSupportProgress(m_info.team, AV_RESOURCE_SCRAPS));
        const unsigned troopCount = m_info.type == AV_SUPPORT_CAVALRY ? 8 : 10;
        for (unsigned i = 0; i < troopCount; ++i)
        {
            auto ai = SpawnActor(entry, m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ());
            if (!ai || !ai->StartPath(entry, Phase::March)) return false;
            ai->m_wait = 500 * (i + 1);
            if (m_info.type == AV_SUPPORT_CAVALRY) ai->m_creature->Mount(alliance ? 2786 : 1166);
        }
        if (m_info.type == AV_SUPPORT_CAVALRY) m_creature->Mount(alliance ? 2786 : 1166);
        return true;
    }

    void Follow(Creature* leader)
    {
        m_phase = Phase::Follow; m_follow = leader->GetObjectGuid();
        m_creature->SetActiveObjectState(true);
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MoveFollow(leader, 3.0f, frand(0.0f, 6.28f));
    }

    void CleanupChildren()
    {
        GuidVector children; children.swap(m_children);
        for (ObjectGuid guid : children)
            if (Creature* child = m_creature->GetMap()->GetCreature(guid))
                if (child->IsAlive()) child->ForcedDespawn();
    }

    void CancelChannels()
    {
        for (ObjectGuid guid : m_participants.Players())
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
                if (Spell* channel = player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                    if (channel->m_spellInfo->Id == INVOCATION)
                        player->InterruptNonMeleeSpells(false, INVOCATION);
        m_participants.Clear();
        m_creature->InterruptNonMeleeSpells(false, INVOCATION);
        for (ObjectGuid guid : m_children)
            if (Creature* child = m_creature->GetMap()->GetCreature(guid))
                child->InterruptNonMeleeSpells(false, INVOCATION);
        if (m_phase == Phase::Channel) m_phase = Phase::Ritual;
    }

    void DeleteRitual()
    {
        ObjectGuid guid = m_ritual; m_ritual.Clear();
        if (GameObject* go = m_creature->GetMap()->GetGameObject(guid)) go->Delete();
    }

    void Finish(bool success)
    {
        const bool controller = m_controlsEvent;
        m_controlsEvent = false; // Clear before callbacks from despawn/channel cleanup.
        CancelChannels(); DeleteRitual();
        if (controller)
            if (BattleGroundAV* bg = GetAV(m_creature)) bg->FinishSupport(m_info.team, m_info.type, success);
        if (!success)
        {
            CleanupChildren();
            m_phase = Phase::Idle; m_route.clear(); m_moving = false;
            RestoreHome();
            if (m_creature->IsAlive()) m_creature->GetMotionMaster()->MoveTargetedHome();
        }
        else m_phase = Phase::Assault;
    }

    void MovementInform(uint32 type, uint32 point) override
    {
        if (type != POINT_MOTION_TYPE || point != ROUTE_POINT_BASE + m_point || !m_moving || m_point >= m_route.size())
            return;
        m_moving = false;
        // A partial/unreachable path must not award rescue or complete an assault.
        auto const& destination = m_route[m_point];
        const float dx = m_creature->GetPositionX() - destination.x;
        const float dy = m_creature->GetPositionY() - destination.y;
        const float dz = m_creature->GetPositionZ() - destination.z;
        if (dx*dx + dy*dy + dz*dz > 9.0f) { m_wait = 1000; return; }
        m_stalled = 0; m_routeTimeout = 0;
        m_wait = m_route[m_point].delay;
        m_creature->SetRespawnCoord(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ(), m_creature->GetOrientation());
        ++m_point;
        if (m_point < m_route.size()) return;
        if (m_phase == Phase::Rescue)
        {
            if (BattleGroundAV* bg = GetAV(m_creature))
                if (bg->NotifyCommanderRescued(m_creature->GetEntry()))
                    m_creature->MonsterSay("\xe6\x88\x91\xe5\xb7\xb2\xe5\xae\x89\xe5\x85\xa8\xe8\xbf\x94\xe5\x9b\x9e\xe3\x80\x82\xe6\x94\xb6\xe9\x9b\x86\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81\xe5\x90\x8e\xef\xbc\x8c\xe5\xb0\xb1\xe8\x83\xbd\xe4\xb8\xba\xe4\xbd\xa0\xe4\xbb\xac\xe6\x8f\x90\xe4\xbe\x9b\xe7\xa9\xba\xe4\xb8\xad\xe6\x94\xaf\xe6\x8f\xb4\xef\xbc\x81", LANG_UNIVERSAL);
            m_phase = Phase::Idle;
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER);
            m_creature->GetMotionMaster()->MoveIdle();
        }
        else if (m_phase == Phase::Air)
        {
            m_point = 0; // Continue an aerial circuit; no ground pathfinding.
        }
        else if (m_controlsEvent && m_info.type == AV_SUPPORT_ELEMENTAL)
            StartRitual();
        else if (m_controlsEvent)
            Finish(true);
        else
            m_phase = Phase::Assault;
    }

    void StartRitual()
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!bg || !bg->IsSupportActive(m_info.team, m_info.type)) { Finish(false); return; }
        const bool alliance = m_info.team == TEAM_INDEX_ALLIANCE;
        const uint32 entry = alliance ? 178670 : 178465;
        const Position pos = alliance ? Position(-199.993f, -343.217f, 6.77662f, 3.68265f) : Position(-360.139f, -133.403f, 26.4856f, 4.41568f);
        GameObject* go = new GameObject;
        const uint32 guid = m_creature->GetMap()->GenerateLocalLowGuid(HIGHGUID_GAMEOBJECT);
        if (!go->Create(guid, guid, entry, m_creature->GetMap(), pos.x, pos.y, pos.z, pos.o))
        { delete go; Finish(false); return; }
        // NPC ownership disables the core's player-owned ritual branch. Our use
        // handler validates live participants and is the only summoning path.
        go->SetOwnerGuid(m_creature->GetObjectGuid());
        go->SetSpawnerGuid(m_creature->GetObjectGuid());
        go->SetUInt32Value(GAMEOBJECT_FACTION, m_creature->GetFaction());
        go->SetRespawnTime(1800);
        m_creature->GetMap()->Add(go); go->AIM_Initialize();
        if (!bg->RegisterSupportGameObject(m_info.team, m_info.type, go))
        { go->Delete(); Finish(false); return; }
        m_ritualDelay = go->GetGOInfo()->summoningRitualCustom.delay;
        m_ritual = go->GetObjectGuid(); m_phase = Phase::Ritual; m_stalled = 0; m_wait = m_ritualDelay;
        m_creature->Unmount(); m_creature->GetMotionMaster()->MoveIdle();
        m_creature->MonsterSay("\xe4\xbb\xaa\xe5\xbc\x8f\xe5\x87\x86\xe5\xa4\x87\xe5\xb0\xb1\xe7\xbb\xaa\xef\xbc\x81\xe9\x9c\x80\xe8\xa6\x81\xe5\x8d\x81\xe5\x90\x8d\xe5\x90\x8c\xe4\xbc\xb4\xe9\x9d\xa0\xe8\xbf\x91\xe7\xa5\xad\xe5\x9d\x9b\xe5\xb9\xb6\xe5\x85\xb1\xe5\x90\x8c\xe5\xbc\x95\xe5\xaf\xbc\xe3\x80\x82", LANG_UNIVERSAL);
    }

    bool ValidParticipant(ObjectGuid guid) const
    {
        Player* player = m_creature->GetMap()->GetPlayer(guid);
        GameObject* go = m_creature->GetMap()->GetGameObject(m_ritual);
        BattleGroundAV* bg = GetAV(m_creature);
        if (!player || !go || !bg || !bg->IsSupportActive(m_info.team, AV_SUPPORT_ELEMENTAL) ||
            !player->IsAlive() || player->IsInCombat() || player->GetBattleGround() != bg ||
            GetTeamIndexByTeamId(player->GetTeam()) != m_info.team || !player->IsWithinDistInMap(go, 8.0f)) return false;
        Spell* channel = player->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        return channel && channel->m_spellInfo->Id == INVOCATION;
    }

    bool JoinRitual(Player* player, GameObject* go)
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (!bg || !bg->IsSupportActive(m_info.team, AV_SUPPORT_ELEMENTAL) || !m_creature->IsAlive() ||
            m_creature->IsInCombat() || go->GetObjectGuid() != m_ritual ||
            (m_phase != Phase::Ritual && m_phase != Phase::Channel) || !player || !player->IsAlive() || player->IsInCombat() ||
            player->GetBattleGround() != bg || player->GetMap() != m_creature->GetMap() ||
            GetTeamIndexByTeamId(player->GetTeam()) != m_info.team || !player->IsWithinDistInMap(go, INTERACTION_DISTANCE)) return false;
        player->CastSpell(player, INVOCATION, TRIGGERED_NONE);
        m_participants.Add(player->GetObjectGuid(), [this](ObjectGuid guid) { return ValidParticipant(guid); });
        ChatHandler(player).PSendSysMessage("\xe4\xbb\xaa\xe5\xbc\x8f\xe5\xbd\x93\xe5\x89\x8d\xe6\x9c\x89\xe6\x95\x88\xe5\xbc\x95\xe5\xaf\xbc\xe4\xba\xba\xe6\x95\xb0\xef\xbc\x9a\x25\x75\x2f\x31\x30\xe3\x80\x82", m_participants.Prune([this](ObjectGuid guid) { return ValidParticipant(guid); }));
        return true;
    }

    void CompleteRitual()
    {
        if (!m_summoned.Claim()) return;
        const uint32 entry = m_info.team == TEAM_INDEX_ALLIANCE ? 13419 : 13256;
        std::vector<WaypointNode> path;
        if (!CopyRoute(entry, path)) { Finish(false); return; }
        auto const& point = path.front();
        npc_av_supportAI* boss = SpawnActor(entry, point.x, point.y, point.z);
        if (!boss || !boss->StartPath(entry, Phase::March)) { Finish(false); return; }
        boss->m_wait = 1000; // Matches the VM activation delay; original combat is EventAI.
        for (ObjectGuid guid : m_children)
            if (guid != boss->m_creature->GetObjectGuid())
                if (Creature* child = m_creature->GetMap()->GetCreature(guid))
                    if (child->IsAlive()) child->ForcedDespawn();
        m_children.clear();
        Finish(true);
        m_phase = Phase::Idle;
        m_creature->MonsterYell("\xe4\xbb\xaa\xe5\xbc\x8f\xe5\xae\x8c\xe6\x88\x90\xef\xbc\x81\xe8\xae\xa9\xe6\x88\x91\xe4\xbb\xac\xe7\x9a\x84\xe7\x9b\x9f\xe5\x8f\x8b\xe5\x90\x91\xe6\x95\x8c\xe4\xba\xba\xe7\x9a\x84\xe9\x98\xb5\xe5\x9c\xb0\xe8\xbf\x9b\xe5\x86\x9b\xef\xbc\x81", LANG_UNIVERSAL);
    }

    void LaunchAir()
    {
        static uint32 const entries[2][3] = {{14946,14948,14947},{14943,14944,14945}};
        if (!m_summoned.Claim()) return;
        npc_av_supportAI* air = SpawnActor(entries[m_info.team][m_info.type],
            m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ()+25.0f);
        if (!air) { Finish(false); return; }
        const Position center = m_info.team == TEAM_INDEX_ALLIANCE ? Position(-1311.53f,-355.28f,130.93f) : Position(618.4f,-87.97f,85.77f);
        for (unsigned i = 0; i < 4; ++i)
        {
            const float angle = float(i) * 1.5707963f;
            air->m_route.emplace_back(center.x + 20.0f*std::cos(angle), center.y + 20.0f*std::sin(angle), center.z, std::nullopt, 0, 0);
        }
        air->m_phase = Phase::Air; air->m_point = 0; air->m_moving = false;
        air->m_creature->SetLevitate(true); air->m_creature->SetCanFly(true); air->SetCombatMovement(false);
        m_children.clear(); // The battleground registry now owns the dispatched air unit.
        Finish(true); m_phase = Phase::Idle;
        m_creature->MonsterYell("\xe7\xa9\xba\xe4\xb8\xad\xe6\x94\xaf\xe6\x8f\xb4\xe5\x87\xba\xe5\x8f\x91\xef\xbc\x81", LANG_UNIVERSAL);
    }

    void UpdateAI(uint32 diff) override
    {
        BattleGroundAV* bg = GetAV(m_creature);
        if (m_phase != Phase::Idle && (!bg || bg->GetStatus() != STATUS_IN_PROGRESS))
        {
            CancelChannels(); DeleteRitual(); CleanupChildren();
            m_controlsEvent = false; m_phase = Phase::Idle;
            if (m_eventActor) { m_creature->ForcedDespawn(); return; }
        }
        CreatureEventAI::UpdateAI(diff);
        if (!m_creature->IsAlive() || !bg || bg->GetStatus() != STATUS_IN_PROGRESS) return;
        if (m_phase == Phase::Idle || m_phase == Phase::Assault) return;
        const bool busy = m_creature->IsInCombat() || m_creature->GetCombatManager().IsEvadingHome();
        if (busy) { m_moving = false; return; }
        if (m_phase == Phase::Rally)
        {
            if (m_wait > diff) m_wait -= diff; else LaunchAir();
            return;
        }
        if (m_phase == Phase::Follow)
        {
            if (m_tick > diff) { m_tick -= diff; return; } m_tick = 1000;
            Creature* leader = m_creature->GetMap()->GetCreature(m_follow);
            if (!leader || !leader->IsAlive()) { m_creature->ForcedDespawn(); return; }
            auto ai = dynamic_cast<npc_av_supportAI*>(leader->AI());
            if (ai && (ai->m_phase == Phase::Ritual || ai->m_phase == Phase::Channel))
            {
                if (m_creature->IsWithinDistInMap(leader, 15.0f))
                {
                    m_creature->GetMotionMaster()->MoveIdle();
                    if (!m_creature->GetCurrentSpell(CURRENT_CHANNELED_SPELL)) m_creature->CastSpell(m_creature, INVOCATION, TRIGGERED_NONE);
                }
            }
            else if (m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                m_creature->GetMotionMaster()->MoveFollow(leader, 3.0f, 3.14f);
            // Combat evade must return to the march, not all the way to camp.
            m_creature->SetRespawnCoord(m_creature->GetPositionX(), m_creature->GetPositionY(), m_creature->GetPositionZ(), m_creature->GetOrientation());
            return;
        }
        if (m_phase == Phase::Ritual || m_phase == Phase::Channel)
        {
            if (!m_creature->GetCurrentSpell(CURRENT_CHANNELED_SPELL)) m_creature->CastSpell(m_creature, INVOCATION, TRIGGERED_NONE);
            const unsigned valid = m_participants.Prune([this](ObjectGuid guid) { return ValidParticipant(guid); });
            if (valid < AVEvents::RitualParticipants) { m_phase = Phase::Ritual; m_wait = m_ritualDelay; return; }
            m_phase = Phase::Channel;
            if (m_wait > diff) m_wait -= diff; else CompleteRitual();
            return;
        }
        if (m_phase != Phase::March && m_phase != Phase::Rescue && m_phase != Phase::Air) return;
        if (m_wait > diff) { m_wait -= diff; return; } m_wait = 0;
        if (m_point >= m_route.size()) return;
        m_routeTimeout += diff;
        if (m_routeTimeout >= 180000)
        {
            if (m_controlsEvent) Finish(false);
            else if (m_phase == Phase::Rescue)
            {
                m_phase = Phase::Idle; m_route.clear(); RestoreHome(); InitializeMenu();
                m_creature->GetMotionMaster()->MoveTargetedHome();
            }
            else m_creature->ForcedDespawn();
            return;
        }
        if (m_moving)
        {
            // Retry an interrupted path; never skip a blocked waypoint or teleport an army.
            m_stalled += diff;
            if (m_stalled < 30000) return;
            m_moving = false; m_stalled = 0;
        }
        auto const& point = m_route[m_point];
        m_moving = true;
        m_creature->GetMotionMaster()->MovePoint(ROUTE_POINT_BASE + uint32(m_point), point.x, point.y, point.z,
            FORCED_MOVEMENT_RUN, m_phase != Phase::Air);
    }
};

struct npc_av_tameableAI : public CreatureEventAI
{
    explicit npc_av_tameableAI(Creature* creature) : CreatureEventAI(creature) {}
    ObjectGuid m_owner;
    uint32 m_check = 1000, m_followAura = 0;
    AVEvents::Once m_delivered;

    void ClearFollow(bool returnHome)
    {
        m_owner.Clear();
        if (m_followAura) m_creature->RemoveAurasDueToSpell(m_followAura);
        m_followAura = 0;
        if (returnHome && m_creature->IsAlive()) m_creature->GetMotionMaster()->MoveTargetedHome();
    }
    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 miscValue) override
    {
        if (eventType == AI_EVENT_CUSTOM_A && miscValue == 0x4156434c) { ClearFollow(false); return; }
        CreatureEventAI::ReceiveAIEvent(eventType, sender, invoker, miscValue);
    }
    void JustDied(Unit* killer) override { ClearFollow(false); CreatureEventAI::JustDied(killer); }
    void JustRespawned() override
    { ClearFollow(false); m_delivered.Reset(); m_check = 1000; CreatureEventAI::JustRespawned(); }

    void SpellHit(Unit* caster, SpellEntry const* spell) override
    {
        CreatureEventAI::SpellHit(caster, spell);
        const bool wolf = m_creature->GetEntry() == 10981;
        if (!caster || caster->GetTypeId() != TYPEID_PLAYER || !spell || spell->Id != (wolf ? 21795 : 21867) ||
            !m_owner.IsEmpty() || m_delivered.Claimed() || m_creature->IsInCombat()) return;
        Player* player = static_cast<Player*>(caster);
        const PvpTeamIndex team = wolf ? TEAM_INDEX_HORDE : TEAM_INDEX_ALLIANCE;
        const uint32 quest = wolf ? 7001 : 7027;
        BattleGroundAV* bg = GetAV(m_creature);
        if (!FriendlyPlayer(player, m_creature, team) || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE ||
            !bg->RegisterTamedCreature(m_creature)) return;
        m_owner = player->GetObjectGuid(); m_followAura = wolf ? 21827 : 21863;
        player->CastSpell(m_creature, m_followAura, TRIGGERED_OLD_TRIGGERED);
        m_creature->SetActiveObjectState(true); m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MoveFollow(player, 2.0f, 3.14f);
    }

    void UpdateAI(uint32 diff) override
    {
        CreatureEventAI::UpdateAI(diff);
        if (m_owner.IsEmpty() || !m_creature->IsAlive()) return;
        if (m_check > diff) { m_check -= diff; return; } m_check = 1000;
        const bool wolf = m_creature->GetEntry() == 10981;
        const PvpTeamIndex team = wolf ? TEAM_INDEX_HORDE : TEAM_INDEX_ALLIANCE;
        const uint32 quest = wolf ? 7001 : 7027;
        Player* player = m_creature->GetMap()->GetPlayer(m_owner);
        BattleGroundAV* bg = GetAV(m_creature);
        if (!player || !player->IsAlive() || !bg || bg->GetStatus() != STATUS_IN_PROGRESS ||
            player->GetBattleGround() != bg || GetTeamIndexByTeamId(player->GetTeam()) != team ||
            !player->IsWithinDistInMap(m_creature, 100.0f) || player->GetQuestStatus(quest) != QUEST_STATUS_INCOMPLETE)
        { ClearFollow(true); return; }
        Creature* stable = GetClosestCreatureWithEntry(m_creature, wolf ? 13441 : 13577, 10.0f);
        if (stable && stable->IsAlive() && AVEvents::ValidDelivery(true, player->GetObjectGuid() == m_owner, m_creature->IsAlive(),
            player->GetBattleGround() == bg, GetTeamIndexByTeamId(player->GetTeam()) == team,
            player->GetQuestStatus(quest) == QUEST_STATUS_INCOMPLETE,
            m_creature->IsWithinDistInMap(stable, 10.0f), player->IsWithinDistInMap(stable, 20.0f)))
        {
            if (m_delivered.Claim())
            {
                player->AreaExploredOrEventHappens(quest); // Empty Stables is an exploration/event objective.
                ChatHandler(player).PSendSysMessage("\xe5\x9d\x90\xe9\xaa\x91\xe5\xb7\xb2\xe9\x80\x81\xe5\x88\xb0\xe5\x85\xbd\xe6\xa0\x8f\xef\xbc\x8c\xe8\xaf\xb7\xe5\x90\x91\xe6\x8c\x87\xe6\x8c\xa5\xe5\xae\x98\xe4\xba\xa4\xe4\xbb\x98\xe4\xbb\xbb\xe5\x8a\xa1\xe3\x80\x82");
                ClearFollow(false); m_creature->ForcedDespawn();
            }
            return;
        }
        if (!m_creature->IsInCombat() && !m_creature->GetCombatManager().IsEvadingHome() &&
            m_creature->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
            m_creature->GetMotionMaster()->MoveFollow(player, 2.0f, 3.14f);
    }
};

bool GossipHello_av_support(Player* player, Creature* creature)
{
    auto ai = dynamic_cast<npc_av_supportAI*>(creature->AI());
    if (!ai || !FriendlyPlayer(player, creature, ai->m_info.team)) return true;
    BattleGroundAV* bg = GetAV(creature);
    player->GetPlayerMenu()->ClearMenus();
    if (ai->m_info.commander && !bg->IsCommanderRescued(creature->GetEntry()))
    {
        if (ai->m_phase == Phase::Idle) player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "\xe6\x88\x91\xe4\xbb\xac\xe6\x9d\xa5\xe6\x95\x91\xe4\xbd\xa0\xe4\xba\x86\xef\xbc\x8c\xe5\x87\xba\xe5\x8f\x91\xe5\x90\xa7\xef\xbc\x81", GOSSIP_SENDER_MAIN, ACTION_RESCUE);
    }
    else
    {
        if (creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER)) player->PrepareQuestMenu(creature->GetObjectGuid());
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, "\xe6\x9f\xa5\xe7\x9c\x8b\xe6\x88\x91\xe6\x96\xb9\xe6\x94\xaf\xe6\x8f\xb4\xe7\x89\xa9\xe8\xb5\x84\xe5\x92\x8c\xe5\xbd\x93\xe5\x89\x8d\xe8\xbf\x9b\xe5\xba\xa6\xe3\x80\x82", GOSSIP_SENDER_MAIN, ACTION_PROGRESS);
        if (creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR)) player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, "\xe8\xae\xa9\xe6\x88\x91\xe7\x9c\x8b\xe7\x9c\x8b\xe4\xbd\xa0\xe7\x9a\x84\xe8\xb4\xa7\xe7\x89\xa9\xe3\x80\x82", GOSSIP_SENDER_MAIN, ACTION_VENDOR);
        if (ai->m_info.type < AV_SUPPORT_MAX)
        {
            const AVSupportStage stage = bg->GetSupportStage(ai->m_info.team, ai->m_info.type);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, stage == AV_SUPPORT_READY ? "\xe7\x89\xa9\xe8\xb5\x84\xe9\xbd\x90\xe5\xa4\x87\xef\xbc\x8c\xe7\xab\x8b\xe5\x8d\xb3\xe5\x90\xaf\xe5\x8a\xa8\xe6\x94\xaf\xe6\x8f\xb4\xef\xbc\x81" : StageText(stage),
                GOSSIP_SENDER_MAIN, stage == AV_SUPPORT_READY ? ACTION_BEGIN : ACTION_PROGRESS);
        }
    }
    player->SEND_GOSSIP_MENU(player->GetGossipTextId(creature), creature->GetObjectGuid());
    return true;
}

bool GossipSelect_av_support(Player* player, Creature* creature, uint32 sender, uint32 action)
{
    auto ai = dynamic_cast<npc_av_supportAI*>(creature->AI());
    if (!ai || sender != GOSSIP_SENDER_MAIN || !FriendlyPlayer(player, creature, ai->m_info.team)) return true;
    player->CLOSE_GOSSIP_MENU();
    if (action == ACTION_RESCUE) ai->Rescue(player);
    else if (action == ACTION_BEGIN)
    {
        if (!ai->Begin(player)) ChatHandler(player).PSendSysMessage("\xe5\xbd\x93\xe5\x89\x8d\xe6\x97\xa0\xe6\xb3\x95\xe5\x90\xaf\xe5\x8a\xa8\xef\xbc\x9a\xe8\xaf\xb7\xe7\xa1\xae\xe8\xae\xa4\xe7\x89\xa9\xe8\xb5\x84\xe3\x80\x81\xe6\x8c\x87\xe6\x8c\xa5\xe5\xae\x98\xe7\x8a\xb6\xe6\x80\x81\xe4\xbb\xa5\xe5\x8f\x8a\xe6\x88\x98\xe5\x9c\xba\xe6\x98\xaf\xe5\x90\xa6\xe4\xbb\x8d\xe5\x9c\xa8\xe8\xbf\x9b\xe8\xa1\x8c\xe3\x80\x82");
    }
    else if (action == ACTION_VENDOR && creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_VENDOR)) player->GetSession()->SendListInventory(creature->GetObjectGuid());
    else if (action == ACTION_PROGRESS)
    {
        BattleGroundAV* bg = GetAV(creature);
        static char const* names[] = {"\xe6\x8a\xa4\xe7\x94\xb2\xe7\xa2\x8e\xe7\x89\x87","\xe4\xbd\x8e\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81","\xe4\xb8\xad\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81","\xe9\xab\x98\xe9\x98\xb6\xe7\xa9\xba\xe5\x86\x9b\xe6\x88\x98\xe5\x88\xa9\xe5\x93\x81","\xe5\x85\x83\xe7\xb4\xa0\xe5\x8f\xac\xe5\x94\xa4\xe6\x9d\x90\xe6\x96\x99","\xe6\x9c\xac\xe6\x96\xb9\xe7\x9f\xbf\xe6\xb4\x9e\xe8\xa1\xa5\xe7\xbb\x99\xe7\xae\xb1","\xe6\x95\x8c\xe6\x96\xb9\xe7\x9f\xbf\xe6\xb4\x9e\xe8\xa1\xa5\xe7\xbb\x99\xe7\xae\xb1","\xe9\xaa\x91\xe5\x85\xb5\xe7\x89\xa9\xe8\xb5\x84","\xe9\x80\x81\xe5\x9b\x9e\xe7\x9a\x84\xe5\x9d\x90\xe9\xaa\x91"};
        for (unsigned i = 0; i < AV_RESOURCE_MAX; ++i)
            ChatHandler(player).PSendSysMessage("\x25\x73\xef\xbc\x9a\x25\x75\x2f\x25\x75", names[i], bg->GetSupportProgress(ai->m_info.team, AVSupportResource(i)), bg->GetSupportRequirement(AVSupportResource(i)));
        if (ai->m_info.type < AV_SUPPORT_MAX) ChatHandler(player).PSendSysMessage("\xe5\xbd\x93\xe5\x89\x8d\xe6\x94\xaf\xe6\x8f\xb4\xef\xbc\x9a\x25\x73\xe3\x80\x82", StageText(bg->GetSupportStage(ai->m_info.team, ai->m_info.type)));
    }
    return true;
}

bool GOUse_av_support_ritual(Player* player, GameObject* go)
{
    if (Unit* owner = go->GetOwner())
        if (owner->GetTypeId() == TYPEID_UNIT)
            if (auto ai = dynamic_cast<npc_av_supportAI*>(static_cast<Creature*>(owner)->AI())) ai->JoinRitual(player, go);
    return true;
}

void AddSC_alterac_valley()
{
    Script* script = new Script;
    script->Name = "npc_av_support";
    script->GetAI = [](Creature* creature) -> UnitAI* { return new npc_av_supportAI(creature); };
    script->pGossipHello = &GossipHello_av_support;
    script->pGossipSelect = &GossipSelect_av_support;
    script->RegisterSelf();
    script = new Script;
    script->Name = "npc_av_support_troop";
    script->GetAI = [](Creature* creature) -> UnitAI* { return new npc_av_supportAI(creature); };
    script->RegisterSelf();
    script = new Script;
    script->Name = "npc_av_tameable";
    script->GetAI = [](Creature* creature) -> UnitAI* { return new npc_av_tameableAI(creature); };
    script->RegisterSelf();
    script = new Script;
    script->Name = "go_av_support_ritual";
    script->pGOUse = &GOUse_av_support_ritual;
    script->RegisterSelf();
}
