/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
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

#include "Grids/GridNotifiers.h"
#include "Server/WorldPacket.h"
#include "Server/WorldSession.h"
#include "Entities/UpdateData.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/Map.h"
#include "Entities/Transports.h"
#include "Globals/ObjectAccessor.h"
#include "BattleGround/BattleGroundMgr.h"
#include "AI/BaseAI/UnitAI.h"
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace MaNGOS;

void VisibleNotifier::Notify()
{
    Player& player = *i_camera.GetOwner();
#ifdef ENABLE_PLAYERBOTS
    if (!player.isRealPlayer())
        return;
#endif

    // the objects at the client that were not met around the camera
    GuidVector notMetGuids;
    player.GetObjectsNotMetByCameraVisit(notMetGuids);

    GenericTransport* transport = player.GetTransport();
    for (ObjectGuid const& guid : notMetGuids)
    {
        // a transport of the map is never met around the camera nor found by GetWorldObject: it stays at the client while
        // it is on the map (Map::SendInitInfiniteObjects, Transport::UpdateForMap)
        if (guid.IsMOTransport())
            continue;

        WorldObject* target = player.GetMap()->GetWorldObject(guid);
        if (!target)
        {
            sLog.outCustomLog("Object was %s in current map.", player.GetMap()->m_objRemoveList.find(guid) == player.GetMap()->m_objRemoveList.end() ? "not found" : "found");
            player.ScheduleOutOfRange(guid);
            continue;
        }

        // the passengers of the player's transport are not always met around the camera but stay visible
        if (transport && target->GetTransport() == transport)
        {
            // ignore far sight case
            if (target->IsPlayer())
                static_cast<Player*>(target)->UpdateVisibilityOf(target, &player);
            player.UpdateVisibilityOf(&player, target);
            continue;
        }

        // far objects update on player notify
        if (target->GetVisibilityData().IsVisibilityOverridden())
        {
            player.UpdateVisibilityOf(&player, target);
            continue;
        }

        if (target->GetTypeId() == TYPEID_UNIT)
            player.BeforeVisibilityDestroy(static_cast<Creature*>(target));
        player.ScheduleOutOfRange(target);

        DEBUG_FILTER_LOG(LOG_FILTER_VISIBILITY_CHANGES, "%s is out of range (no in active cells set) now for %s",
                         guid.GetString().c_str(), player.GetGuidStr().c_str());
    }
}

void MessageDeliverer::Visit(CameraMapType& m)
{
    for (auto& iter : m)
    {
        Player* owner = iter.getSource()->GetOwner();

        if (i_toSelf || owner != &i_player)
        {
            if (WorldSession* session = owner->GetSession())
                session->SendPacket(i_message);
        }
    }
}

void MessageDelivererExcept::Visit(CameraMapType& m)
{
    for (auto& iter : m)
    {
        Player* owner = iter.getSource()->GetOwner();

        if (owner == i_skipped_receiver)
            continue;

        if (WorldSession* session = owner->GetSession())
            session->SendPacket(i_message);
    }
}

void ObjectMessageDeliverer::Visit(CameraMapType& m)
{
    for (auto& iter : m)
    {
        if (WorldSession* session = iter.getSource()->GetOwner()->GetSession())
            session->SendPacket(i_message);
    }
}

void MessageDistDeliverer::Visit(CameraMapType& m)
{
    for (auto& iter : m)
    {
        Player* owner = iter.getSource()->GetOwner();

        if ((i_toSelf || owner != &i_player) &&
                (!i_ownTeamOnly || owner->GetTeam() == i_player.GetTeam()) &&
                (!i_dist || iter.getSource()->GetBody()->IsWithinDist(&i_player, i_dist)))
        {
            if (WorldSession* session = owner->GetSession())
                session->SendPacket(i_message);
        }
    }
}

void ObjectMessageDistDeliverer::Visit(CameraMapType& m)
{
    for (auto& iter : m)
    {
        if (!i_dist || iter.getSource()->GetBody()->IsWithinDist(&i_object, i_dist))
        {
            if (WorldSession* session = iter.getSource()->GetOwner()->GetSession())
                session->SendPacket(i_message);
        }
    }
}

namespace
{
    bool IsObjectAddressInRange(WorldObject const* object)
    {
        if (!object)
            return false;
#ifdef _WIN32
        static auto const maxApplicationAddress = []
        {
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            return reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
        }();
        return reinterpret_cast<std::uintptr_t>(object) <= maxApplicationAddress;
#else
        return true;
#endif
    }

    WorldObject* ReadUpdateSlot(ObjectUpdateDiagnostics::Slot slot)
    {
        // Force a fresh read of the set element at each diagnostic boundary.
        WorldObject* const volatile* observedSlot = slot;
        return *observedSlot;
    }

    char const* UpdatePointerSourceName(ObjectUpdateDiagnostics::Source source)
    {
        using Source = ObjectUpdateDiagnostics::Source;
        switch (source)
        {
            case Source::ActiveNonPlayer: return "active-nonplayer";
            case Source::GridCreatureOrPet: return "grid-creature-or-pet";
            case Source::GridGameObject: return "grid-gameobject";
            case Source::GridDynamicObject: return "grid-dynamicobject";
            default: return "unknown";
        }
    }
}

void ObjectUpdateDiagnostics::CheckInput(WorldObject* object, Source source, void const* sourceSlot) const
{
    if (!IsObjectAddressInRange(object))
        Fail("before-insert", {object, nullptr, source, sourceSlot}, nullptr, object);
}

ObjectUpdateDiagnostics::~ObjectUpdateDiagnostics()
{
#if defined(_MSVC_STL_VERSION)
    if (m_headSample.next)
        CheckLinks("before-destroy");
#endif
}

ObjectUpdateDiagnostics::SampleKey ObjectUpdateDiagnostics::Key(Iterator const& it)
{
#if defined(_MSVC_STL_VERSION)
    return it._Ptr;
#else
    return std::addressof(*it);
#endif
}

void ObjectUpdateDiagnostics::Insert(WorldObject* object, Source source, void const* sourceSlot)
{
    CheckInput(object, source, sourceSlot);
    auto const [it, inserted] = m_objects.insert(object);
    Slot const slot = std::addressof(*it);
    Sample const sample{object, ReadUpdateSlot(slot), source, sourceSlot};
    if (sample.after != sample.before)
        Fail("after-insert", sample, slot, sample.after);
    if (inserted)
        m_samples.emplace(Key(it), sample);
}

void ObjectUpdateDiagnostics::SealLinks()
{
#if defined(_MSVC_STL_VERSION)
    constexpr char stage[] = "before-update";
    m_head = m_objects._Unchecked_end()._Ptr;
    m_tail = ReadLink(m_head->_Prev);
    Node successor = m_head;
    Node node = m_tail;
    Sample const* successorSample = &m_headSample;
    std::size_t count = 0;

    // Reconstruct the expected forward links from the reverse links once insertion and rehash are done.
    // Resolve each predecessor against the known nodes before reading it.
    while (node != m_head)
    {
        auto const it = m_samples.find(node);
        if (it == m_samples.end() || it->second.next)
            FailLink(stage, "_Prev", "invalid-predecessor", successor, *successorSample, nullptr, node);

        Sample& sample = it->second;
        sample.next = successor;
        CheckNext(stage, node, sample);
        ++count;
        successor = node;
        successorSample = &sample;
        node = ReadLink(node->_Prev);
    }
    if (count != m_samples.size())
    {
        for (auto const& [unreachable, sample] : m_samples)
            if (!sample.next)
                FailLink(stage, "_Next", "unreachable-node", unreachable, sample, nullptr, ReadLink(unreachable->_Next));
    }

    m_headSample.next = successor;
    CheckNext(stage, m_head, m_headSample);
#endif
}

ObjectUpdateDiagnostics::Iterator ObjectUpdateDiagnostics::BeginUpdate() const
{
#if defined(_MSVC_STL_VERSION)
    auto it = m_objects.end();
    it._Ptr = CheckNext("during-update", m_head, m_headSample);
    return it;
#else
    return m_objects.begin();
#endif
}

WorldObject* ObjectUpdateDiagnostics::BeforeUpdate(Iterator const& iterator) const
{
    Slot const slot = std::addressof(*iterator);
    WorldObject* const current = ReadUpdateSlot(slot);
    auto const it = m_samples.find(Key(iterator));
    if (it == m_samples.end())
        Fail("before-update-untracked", {nullptr, nullptr, Source::Unknown, nullptr}, slot, current);
    if (current != it->second.after || !IsObjectAddressInRange(current))
        Fail("before-update", it->second, slot, current);
    return current;
}

void ObjectUpdateDiagnostics::Advance(Iterator& it) const
{
#if defined(_MSVC_STL_VERSION)
    Node const node = it._Ptr;
    // Follow the checked address without letting iterator++ read the link again.
    it._Ptr = CheckNext("during-update", node, m_samples.find(node)->second);
#else
    ++it;
#endif
}

void ObjectUpdateDiagnostics::CheckLinks(char const* stage) const
{
#if defined(_MSVC_STL_VERSION)
    // MSVC's list teardown also follows the sentinel's tail link when it terminates the chain.
    Node const tail = ReadLink(m_head->_Prev);
    if (tail != m_tail)
        FailLink(stage, "_Prev", "tail-changed", m_head, m_headSample, m_tail, tail);
    CheckNext(stage, m_head, m_headSample);
    for (auto const& [node, sample] : m_samples)
        CheckNext(stage, node, sample);
#else
    (void)stage;
#endif
}

#if defined(_MSVC_STL_VERSION)
ObjectUpdateDiagnostics::Node ObjectUpdateDiagnostics::ReadLink(Node const volatile& link)
{
    return link;
}

ObjectUpdateDiagnostics::Node ObjectUpdateDiagnostics::CheckNext(char const* stage, Node node, Sample const& sample) const
{
    Node const observed = ReadLink(node->_Next);
    if (observed != sample.next)
        FailLink(stage, "_Next", "next-mismatch", node, sample, sample.next, observed);
    return observed;
}

void ObjectUpdateDiagnostics::FailLink(char const* stage, char const* field, char const* reason, Node node,
    Sample const& sample, Node expected, Node observed) const
{
    sLog.outError("[OBJECT-UPDATE-POINTER] stage=%s field=%s reason=%s map=%u instance=%u source=%s source_slot=%p node=%p object_after_insert=%p expected=%p observed=%p",
        stage, field, reason, m_map.GetId(), m_map.GetInstanceId(), UpdatePointerSourceName(sample.source), sample.sourceSlot,
        static_cast<void*>(node), static_cast<void*>(sample.after), static_cast<void*>(expected), static_cast<void*>(observed));
    std::abort();
}
#endif

void ObjectUpdateDiagnostics::Fail(char const* stage, Sample const& sample, Slot slot, WorldObject* current) const
{
    sLog.outError("[OBJECT-UPDATE-POINTER] stage=%s map=%u instance=%u source=%s source_slot=%p set_slot=%p before_insert=%p after_insert=%p observed=%p",
        stage, m_map.GetId(), m_map.GetInstanceId(), UpdatePointerSourceName(sample.source), sample.sourceSlot, static_cast<void const*>(slot),
        static_cast<void*>(sample.before), static_cast<void*>(sample.after), static_cast<void*>(current));
    std::abort();
}

void ObjectUpdater::Add(WorldObject* object, ObjectUpdateDiagnostics::Source source, void const* sourceSlot)
{
    if (m_diagnostics)
        m_diagnostics->Insert(object, source, sourceSlot);
    else
        m_objectToUpdateSet.emplace(object);
}

template<class T>
void ObjectUpdater::Visit(GridRefManager<T>& m)
{
    using Source = ObjectUpdateDiagnostics::Source;
    constexpr Source source = std::is_same_v<T, GameObject> ? Source::GridGameObject : Source::GridDynamicObject;
    for (auto& iter : m)
        Add(iter.getSource(), source, std::addressof(iter));
}

bool CannibalizeObjectCheck::operator()(Corpse* u)
{
    // ignore bones
    if (u->GetType() == CORPSE_BONES)
        return false;

    Player* owner = ObjectAccessor::FindPlayer(u->GetOwnerGuid());

    if (!owner || i_fobj->CanAssist(owner))
        return false;

    return i_fobj->IsWithinDistInMap(u, i_range);
}

void MaNGOS::RespawnDo::operator()(Creature* u) const
{
    // prevent respawn creatures for not active BG event
    Map* map = u->GetMap();
    if (map->IsBattleGround())
    {
        BattleGroundEventIdx eventId = map->GetMapDataContainer().GetCreatureEventIndex(u->GetDbGuid());
        if (!((BattleGroundMap*)map)->GetBG()->IsActiveEvent(eventId.event1, eventId.event2))
            return;
    }

    if (u->IsUsingNewSpawningSystem())
    {
        if (u->IsDead() && !u->GetCreatureGroup())
        {
            if (u->GetMap()->GetMapDataContainer().GetSpawnGroupByGuid(u->GetDbGuid(), TYPEID_UNIT))
                u->GetMap()->GetPersistentState()->SaveCreatureRespawnTime(u->GetDbGuid(), time(nullptr));
            else
                u->GetMap()->GetSpawnManager().RespawnCreature(u->GetDbGuid(), 0);
        }
    }
    else
        u->Respawn();
}

void MaNGOS::RespawnDo::operator()(GameObject* u) const
{
    // prevent respawn gameobject for not active BG event
    Map* map = u->GetMap();
    if (map->IsBattleGround())
    {
        BattleGroundEventIdx eventId = map->GetMapDataContainer().GetGameObjectEventIndex(u->GetDbGuid());
        if (!static_cast<BattleGroundMap*>(map)->GetBG()->IsActiveEvent(eventId.event1, eventId.event2))
            return;
    }

    u->Respawn();
}

void MaNGOS::CallOfHelpCreatureInRangeDo::operator()(Creature* u)
{
    if (u == i_funit)
        return;

    if (!u->CanAssist(i_funit) || !u->CanAttack(i_enemy))
        return;

    // too far
    if (!i_funit->IsWithinDistInMap(u, i_range))
        return;

    // only if see assisted creature
    if (!i_funit->IsWithinLOSInMap(u))
        return;

    if (u->AI())
        u->AI()->OnCallForHelp(i_enemy);
}

bool MaNGOS::AnyAssistCreatureInRangeCheck::operator()(Creature* u)
{
    if (u == i_funit)
        return false;

    if (!u->CanAssistInCombatAgainst(i_funit, i_enemy))
        return false;

    // too far
    if (!i_funit->IsWithinDistInMap(u, i_range))
        return false;

    // only if see assisted creature
    if (!i_funit->IsWithinLOSInMap(u))
        return false;

    return true;
}

template void ObjectUpdater::Visit<GameObject>(GameObjectMapType&);
template void ObjectUpdater::Visit<DynamicObject>(DynamicObjectMapType&);
