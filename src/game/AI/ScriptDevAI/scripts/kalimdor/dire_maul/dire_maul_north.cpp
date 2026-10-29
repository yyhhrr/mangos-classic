/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul.h"
#include "dire_maul_north.h"
#include "Loot/LootMgr.h"
#include <algorithm>
#include <ctime>
#include <sstream>

namespace
{
uint32 const dmnActors[] = {11501, 14326, 14321, 14323, 14325, 14324, 14322, 14353, 14338};
uint32 const dmnRewardEntries[] = {179564, 179501};
uint32 const dmnRewardGuids[] = {4290331, 4290042};
uint32 const dmnRewardSeconds[] = {3600, 43200};
uint64 const dmnMaximumTime = 4102444800000ULL;
template<class T> bool NorthNumber(std::istream& in, T& result, uint64 maximum)
{
    std::string token;
    if (!(in >> token) || token.empty()) return false;
    uint64 value = 0;
    for (char c : token)
    {
        if (c < '0' || c > '9' || value > maximum / 10 ||
            (value == maximum / 10 && uint64(c - '0') > maximum % 10)) return false;
        value = value * 10 + uint64(c - '0');
    }
    result = T(value); return true;
}
bool NorthSnapshot(std::string const& text)
{
    // This is the native IL1 envelope bound (16KiB plain). Loot itself validates
    // every row/claim against native prototypes when it rehydrates the chest.
    return text.size() >= 5 && text.size() <= 32771 && !text.compare(0, 3, "IL1") && !((text.size() - 3) % 2) &&
        std::all_of(text.begin() + 3, text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool NorthScene(uint32 which, DireMaulNorth::SceneState const& s)
{
    if (!which)
    {
        if (s.stage > DMN_MIZZLE_DONE || s.node > 8) return false;
        if (s.stage == DMN_MIZZLE_INITIAL) return !s.node && !s.timer;
        if (s.stage == DMN_MIZZLE_TO_KING) return s.node <= 6 && s.timer <= 60000;
        if (s.stage == DMN_MIZZLE_TO_KING_SAY) return s.node >= 1 && s.node <= 6 && s.timer <= 1000;
        if (s.stage == DMN_MIZZLE_WAIT_TRIBUTE) return s.node == 7 && !s.timer;
        if (s.stage == DMN_MIZZLE_TO_TRIBUTE) return (s.node == 7 || s.node == 8) && s.timer <= 60000;
        return s.node == 8 && !s.timer;
    }
    if (which == 1)
    {
        if (s.stage > DMN_KROM_DONE || s.node > 25) return false;
        if (s.stage == DMN_KROM_IDLE) return !s.node && !s.timer;
        if (s.stage == DMN_KROM_ENRAGE) return !s.node && s.timer <= 1000;
        if (s.stage == DMN_KROM_WALKING) return s.timer <= 60000;
        return s.node == 25 && !s.timer;
    }
    static uint32 const maximum[] = {0, 1000, 1000, 2000, 2000, 9000, 0};
    return s.stage <= DMN_KNOT_AWAY && !s.node && s.timer <= maximum[s.stage];
}
}

DireMaulNorth::DireMaulNorth(ScriptedInstance& owner, std::function<void()> save) : m_owner(owner), m_save(std::move(save)) {}
uint64 DireMaulNorth::Now() { return uint64(time(nullptr)) * 1000; }
void DireMaulNorth::Commit() { if (m_save) m_save(); }
uint32 DireMaulNorth::GetData(uint32 type) const { return type >= 11 && type <= 17 ? m_progress.encounters[type - 11] : 0; }
DireMaulNorth::SceneState DireMaulNorth::GetMizzleState() const { return m_progress.mizzle; }
DireMaulNorth::SceneState DireMaulNorth::GetKromcrushState() const { return m_progress.krom; }
DireMaulNorth::SceneState DireMaulNorth::GetKnotState() const { return m_progress.knot; }

std::string DireMaulNorth::Encode(Progress const& p)
{
    std::ostringstream out;
    out << "DMN1 " << p.mizzleAcknowledged << ' ' << p.mizzleDead << ' ' << p.kingSaid << ' '
        << p.knotGeneration << ' ' << p.knotReturn << ' ' << p.trapUntil << ' ' << p.frozen << ' ' << p.gordokFaction << ' ' << p.chainUntil;
    for (auto const* scene : {&p.mizzle, &p.krom, &p.knot}) out << ' ' << scene->stage << ' ' << scene->node << ' ' << scene->timer;
    for (auto const& reward : p.rewards)
        out << ' ' << reward.state << ' ' << reward.until << ' ' << reward.chestUntil << ' ' << reward.opened << ' ' << (reward.loot.empty() ? "-" : reward.loot);
    return out.str();
}
std::string DireMaulNorth::Save() const { return Encode(m_progress); }
bool DireMaulNorth::Decode(std::array<uint32, 7> const& prefix, std::string const& text, Progress& result)
{
    Progress p; p.encounters = prefix;
    for (uint32 state : prefix) if (state > SPECIAL) return false;
    if (text.empty())
    {
        for (uint32& state : p.encounters) if (state == IN_PROGRESS) state = NOT_STARTED;
        // The old prefix proves the King died, not that an unclaimed reward
        // remains. Migration must not manufacture a second tribute roll.
        if (p.encounters[0] == DONE)
        {
            p.mizzleAcknowledged = 1; p.kingSaid = 1;
            p.mizzle = {DMN_MIZZLE_DONE, 8, 0}; p.rewards[0].state = 3;
        }
        result = p; return true;
    }
    if (text.size() > 128000) return false;
    std::istringstream in(text); std::string version;
    if (!(in >> version) || version != "DMN1" || !NorthNumber(in, p.mizzleAcknowledged, 1) || !NorthNumber(in, p.mizzleDead, 1) ||
        !NorthNumber(in, p.kingSaid, 1) || !NorthNumber(in, p.knotGeneration, 1000000) || !NorthNumber(in, p.knotReturn, dmnMaximumTime) ||
        !NorthNumber(in, p.trapUntil, dmnMaximumTime) || !NorthNumber(in, p.frozen, 1) || !NorthNumber(in, p.gordokFaction, 1) ||
        !NorthNumber(in, p.chainUntil, dmnMaximumTime)) return false;
    uint32 which = 0;
    for (auto* scene : {&p.mizzle, &p.krom, &p.knot})
    {
        if (!NorthNumber(in, scene->stage, 6) || !NorthNumber(in, scene->node, 25) || !NorthNumber(in, scene->timer, 60000) || !NorthScene(which++, *scene)) return false;
    }
    for (uint32 i = 0; i < 2; ++i)
    {
        Reward& r = p.rewards[i]; std::string loot;
        if (!NorthNumber(in, r.state, 3) || !NorthNumber(in, r.until, dmnMaximumTime) || !NorthNumber(in, r.chestUntil, dmnMaximumTime) ||
            !NorthNumber(in, r.opened, 1) || !(in >> loot)) return false;
        if (loot != "-") { if (!NorthSnapshot(loot)) return false; r.loot = loot; }
        if (r.opened != !r.loot.empty() || (r.chestUntil && !r.opened) || r.chestUntil > Now() + 301000) return false;
        if ((r.state != 2 && (r.until || r.chestUntil || r.opened)) || (r.state == 2 && !r.until) ||
            r.until > Now() + uint64(dmnRewardSeconds[i]) * 1000) return false;
    }
    in >> std::ws; if (!in.eof()) return false;
    if (p.encounters[0] != DONE && (p.mizzleAcknowledged || p.mizzleDead || p.kingSaid || p.mizzle.stage || p.rewards[0].state)) return false;
    if ((p.mizzle.stage != 0) != (p.mizzleAcknowledged != 0) || (p.mizzleDead && !p.mizzleAcknowledged)) return false;
    if (p.rewards[0].state && p.mizzle.stage < DMN_MIZZLE_TO_TRIBUTE) return false;
    if (p.rewards[0].state && p.mizzle.node != 8) return false;
    if (p.mizzle.stage == DMN_MIZZLE_DONE && p.rewards[0].state < 2) return false;
    if (p.rewards[1].state && !p.knotGeneration) return false;
    if (p.rewards[1].state == 1 && p.knot.stage != DMN_KNOT_CACHE) return false;
    if (p.knot.stage && !p.knotGeneration) return false;
    if ((p.knot.stage == DMN_KNOT_AWAY) != (p.knotReturn != 0) || p.knotReturn > Now() + 7200000) return false;
    if (p.knot.stage >= DMN_KNOT_CHAIN && p.rewards[1].state < 2) return false;
    if (p.chainUntil > Now() + 43200000 || (p.chainUntil && !p.knotGeneration)) return false;
    if (p.knot.stage >= DMN_KNOT_RUN && !p.chainUntil) return false;
    if (p.trapUntil > Now() + 3600000 || (p.frozen && p.trapUntil) || (p.gordokFaction && !p.frozen)) return false;
    result = p; return true;
}
bool DireMaulNorth::ValidateSave(std::array<uint32, 7> const& prefix, std::string const& text) { Progress p; return Decode(prefix, text, p); }
bool DireMaulNorth::Load(std::array<uint32, 7> const& prefix, std::string const& text)
{
    Progress next; if (!Decode(prefix, text, next)) return false;
    for (uint32& state : next.encounters) if (state == IN_PROGRESS) state = NOT_STARTED;
    if (text.empty() && next.encounters[0] == DONE)
        if (GameObject* live = m_owner.instance->GetGameObject(m_rewards[0]))
            if (live->IsSpawned() && live->GetRespawnTime() > 0 && live->GetLootState() != GO_JUST_DEACTIVATED &&
                ((live->GetLootState() == GO_ACTIVATED && live->m_loot) || live->GetRespawnTime() > time(nullptr)))
            {
                // A live legacy object is direct evidence of this reward. A
                // cold negative DB spawn has neither a window nor rolled loot,
                // and cannot enter this branch merely because King is DONE.
                Reward& r = next.rewards[0]; r.state = 2;
                r.until = std::min(uint64(live->GetRespawnTime()) * 1000, Now() + 3600000);
                if (live->m_loot)
                {
                    std::string loot = live->m_loot->GetInstanceSnapshot();
                    if (NorthSnapshot(loot))
                    { r.opened = 1; r.loot = loot; uint32 rem = live->GetChestDespawnRemainingMS(); r.chestUntil = rem ? Now() + rem : 0; }
                    else { r = Reward(); r.state = 3; }
                }
            }
    Progress old = m_progress; m_progress = next;
    m_retry = 0; m_attempts = 0; m_paused = false;
    for (uint32 i = 0; i < 2; ++i)
        if (old.rewards[i].state != next.rewards[i].state || old.rewards[i].until != next.rewards[i].until ||
            old.rewards[i].chestUntil != next.rewards[i].chestUntil || old.rewards[i].loot != next.rewards[i].loot)
            if (GameObject* go = m_owner.instance->GetGameObject(m_rewards[i]))
            {
                delete go->m_loot; go->m_loot = nullptr; go->RestoreChestDespawnRemainingMS(0);
                go->SetRespawnTime(0); go->SetLootState(GO_READY);
            }
    SyncObjects(); SyncActors(); return true;
}
void DireMaulNorth::ClearKing(Progress& p)
{
    p.encounters[0] = NOT_STARTED; p.mizzle = SceneState(); p.mizzleAcknowledged = p.mizzleDead = p.kingSaid = 0;
    p.rewards[0] = Reward();
}
std::string DireMaulNorth::SaveForKingReset() const { Progress p = m_progress; ClearKing(p); return Encode(p); }
void DireMaulNorth::ResetKing()
{
    ClearKing(m_progress); m_attempts = 0; m_retry = 0;
    ObjectGuid old = m_actors[7]; m_actors[7].Clear();
    if (Creature* mizzle = m_owner.instance->GetCreature(old))
    { mizzle->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP); mizzle->ForcedDespawn(); }
    m_syncing = true;
    if (GameObject* go = m_owner.instance->GetGameObject(m_rewards[0]))
    { delete go->m_loot; go->m_loot = nullptr; go->RestoreChestDespawnRemainingMS(0); go->ForcedDespawn(); }
    m_syncing = false;
    for (uint32 index : {4u, 5u}) if (Creature* guard = GetActor(dmnActors[index]))
    {
        guard->ClearTemporaryFaction();
        if (index == 4 && m_progress.krom.stage == DMN_KROM_DONE && guard->IsAlive())
            guard->SetFactionTemporary(45, TEMPFACTION_RESTORE_RESPAWN);
        if (guard->IsAlive()) guard->SetStandState(UNIT_STAND_STATE_STAND);
    }
    Commit();
}
Creature* DireMaulNorth::GetActor(uint32 entry) const
{
    for (uint32 i = 0; i < 9; ++i) if (dmnActors[i] == entry) return m_owner.instance->GetCreature(m_actors[i]);
    return nullptr;
}
bool DireMaulNorth::IsCurrentActor(Creature* actor) const
{
    return actor && actor->GetMap() == m_owner.instance && actor->GetMapId() == 429 && GetActor(actor->GetEntry()) == actor;
}
void DireMaulNorth::SetData(uint32 type, uint32 value)
{
    if (type < 11 || type > 17 || value > SPECIAL || GetData(type) == DONE || GetData(type) == value) return;
    m_progress.encounters[type - 11] = value;
    if (type == 11 && value == DONE)
    {
        // Persist the request before allocating its actor. Only the returned,
        // in-world summon acknowledges the first scene; failures remain retryable.
        m_attempts = 0; m_retry = 0; Commit(); SyncActors(); ProcessMizzle(); return;
    }
    Commit();
}
bool DireMaulNorth::SetScene(Creature* actor, SceneState const& scene, uint32 which)
{
    if (!IsCurrentActor(actor) || !actor->IsAlive() || !NorthScene(which, scene) || actor->GetEntry() != (which == 0 ? 14353u : which == 1 ? 14325u : 14338u)) return false;
    SceneState& current = which == 0 ? m_progress.mizzle : which == 1 ? m_progress.krom : m_progress.knot;
    if (which == 0 && (!m_progress.mizzleAcknowledged || GetData(11) != DONE || m_progress.mizzleDead)) return false;
    if (scene.node < current.node || scene.node > current.node + 1) return false;
    bool allowed = scene.stage == current.stage;
    if (which == 0)
    {
        allowed |= current.stage == DMN_MIZZLE_TO_KING && scene.stage == DMN_MIZZLE_TO_KING_SAY && scene.node == 1;
        allowed |= current.stage == DMN_MIZZLE_TO_KING_SAY && scene.stage == DMN_MIZZLE_TO_KING;
        allowed |= (current.stage == DMN_MIZZLE_TO_KING || current.stage == DMN_MIZZLE_TO_KING_SAY) && scene.stage == DMN_MIZZLE_WAIT_TRIBUTE;
        // Reward request and its completion are atomic coordinator operations.
        if ((current.stage == DMN_MIZZLE_WAIT_TRIBUTE || current.stage == DMN_MIZZLE_DONE) && scene.stage != current.stage) return false;
    }
    else if (which == 1) allowed |= current.stage && scene.stage == current.stage + 1;
    else
    {
        allowed |= current.stage && current.stage < DMN_KNOT_DESPAWN && scene.stage == current.stage + 1;
        if (scene.stage >= DMN_KNOT_CHAIN && m_progress.rewards[1].state < 2) return false;
        if (scene.stage == DMN_KNOT_AWAY) return false; // FinishKnot also writes the original return deadline.
        if (current.stage == DMN_KNOT_CHAIN && scene.stage == DMN_KNOT_RUN)
        {
            GameObject* chain = m_owner.instance->GetGameObject(m_chain);
            if (!chain || (chain->GetLootState() != GO_JUST_DEACTIVATED && chain->IsSpawned())) return false;
            // The chain's original positive12h respawn is independent of the
            // NPC's2h absence. An already hidden chain is never extended.
            if (m_progress.chainUntil <= Now())
                m_progress.chainUntil = chain->GetRespawnTime() > time(nullptr) ? uint64(chain->GetRespawnTime()) * 1000 : Now() + 43200000;
        }
    }
    if (!allowed) return false;
    bool changed = scene.stage != current.stage || scene.node != current.node;
    current = scene; if (changed) Commit(); return true;
}
bool DireMaulNorth::SetMizzleState(Creature* actor, SceneState const& scene) { return SetScene(actor, scene, 0); }
bool DireMaulNorth::SetKromcrushState(Creature* actor, SceneState const& scene) { return SetScene(actor, scene, 1); }
bool DireMaulNorth::SetKnotState(Creature* actor, SceneState const& scene) { return SetScene(actor, scene, 2); }
bool DireMaulNorth::CanUseMizzle(Creature* actor) const
{
    return IsCurrentActor(actor) && actor->GetEntry() == 14353 && actor->IsAlive() && m_progress.mizzleAcknowledged && !m_progress.mizzleDead &&
        GetData(11) == DONE && (m_progress.mizzle.stage == DMN_MIZZLE_WAIT_TRIBUTE || m_progress.mizzle.stage == DMN_MIZZLE_DONE);
}
bool DireMaulNorth::RequestTribute(Creature* actor)
{
    if (!CanUseMizzle(actor) || m_progress.mizzle.stage != DMN_MIZZLE_WAIT_TRIBUTE || m_progress.rewards[0].state) return false;
    m_progress.mizzle = {DMN_MIZZLE_TO_TRIBUTE, 7, 60000}; Commit(); return true;
}
bool DireMaulNorth::OpenTribute(Creature* actor)
{
    if (!IsCurrentActor(actor) || actor->GetEntry() != 14353 || !actor->IsAlive() || GetData(11) != DONE ||
        m_progress.mizzle.stage != DMN_MIZZLE_TO_TRIBUTE || m_progress.mizzle.node != 8) return false;
    if (!RequestReward(0)) return false;
    m_progress.mizzle = {DMN_MIZZLE_DONE, 8, 0}; Commit(); return true;
}
bool DireMaulNorth::CanStartKromcrush(Creature* actor) const
{
    return IsCurrentActor(actor) && actor->GetEntry() == 14325 && actor->IsAlive() && !actor->IsInCombat() &&
        m_progress.krom.stage == DMN_KROM_IDLE && GetData(11) != DONE;
}
bool DireMaulNorth::BeginKromcrush(Creature* actor)
{
    if (!CanStartKromcrush(actor)) return false;
    m_progress.krom = {DMN_KROM_ENRAGE, 0, 1000}; Commit(); return true;
}
bool DireMaulNorth::IsKnotAway() const { return m_progress.knot.stage == DMN_KNOT_AWAY && m_progress.knotReturn > Now(); }
uint32 DireMaulNorth::KnotReturnRemainingMS() const { return IsKnotAway() ? uint32(m_progress.knotReturn - Now()) : 0; }
bool DireMaulNorth::CanStartKnot(Creature* actor) const
{
    return IsCurrentActor(actor) && actor->GetEntry() == 14338 && actor->IsAlive() && !actor->IsInCombat() &&
        (m_progress.knot.stage == DMN_KNOT_IDLE || (m_progress.knot.stage == DMN_KNOT_AWAY && !IsKnotAway()));
}
bool DireMaulNorth::BeginKnot(Creature* actor)
{
    if (!CanStartKnot(actor) || m_progress.knotGeneration >= 1000000) return false;
    ++m_progress.knotGeneration; m_progress.knotReturn = 0; m_progress.knot = {DMN_KNOT_CACHE, 0, 1000};
    // A second legal rescue while the old cache still exists has the original
    // command9 semantics: preserve its roll and window, rather than refresh it.
    if (!RewardAvailable(1) && m_progress.rewards[1].state != 1) m_progress.rewards[1] = Reward();
    Commit(); return true;
}
bool DireMaulNorth::CompleteKnot(Creature* actor)
{
    return IsCurrentActor(actor) && actor->GetEntry() == 14338 && actor->IsAlive() && m_progress.knot.stage == DMN_KNOT_CACHE && RequestReward(1);
}
bool DireMaulNorth::FinishKnot(Creature* actor)
{
    if (!IsCurrentActor(actor) || actor->GetEntry() != 14338 || !actor->IsAlive() || m_progress.knot.stage != DMN_KNOT_DESPAWN || m_progress.rewards[1].state < 2) return false;
    m_progress.knot = {DMN_KNOT_AWAY, 0, 0}; m_progress.knotReturn = Now() + 7200000;
    actor->SetRespawnDelay(7200, true); Commit(); return true;
}
bool DireMaulNorth::TrapRepaired(GameObject* trap)
{
    if (!trap || trap->GetEntry() != 179512 || trap->GetDbGuid() != 4290049 || trap->GetMap() != m_owner.instance ||
        m_owner.instance->GetGameObject(trap->GetObjectGuid()) != trap || !trap->IsSpawned() || trap->GetRespawnTime() <= time(nullptr) || m_progress.frozen) return false;
    if (!m_progress.trapUntil)
    { m_progress.trapUntil = std::min(uint64(trap->GetRespawnTime()) * 1000, Now() + 3600000); Commit(); }
    return IsTrapActive();
}
bool DireMaulNorth::IsTrapActive() const { return !m_progress.frozen && m_progress.trapUntil > Now(); }
bool DireMaulNorth::IsSlipkikFrozen() const { return m_progress.frozen != 0; }
bool DireMaulNorth::IsSlipkikGordok() const { return m_progress.gordokFaction != 0; }
bool DireMaulNorth::FreezeSlipkik(Creature* actor)
{
    if (!IsCurrentActor(actor) || actor->GetEntry() != 14323 || !actor->IsAlive() || !actor->HasAura(22856) || !IsTrapActive()) return false;
    m_progress.frozen = 1; m_progress.gordokFaction = 0; m_progress.trapUntil = 0; Commit(); return true;
}
bool DireMaulNorth::ThawSlipkik(Creature* actor)
{
    if (!IsCurrentActor(actor) || actor->GetEntry() != 14323 || !m_progress.frozen) return false;
    if (!m_progress.gordokFaction) { m_progress.gordokFaction = 1; Commit(); }
    return true; // Native31770 changes faction only;22856 and immunity remain.
}

bool DireMaulNorth::RequestReward(uint32 index)
{
    Reward& r = m_progress.rewards[index];
    if (r.state == 3) return true; // Acknowledged earlier and already consumed, never replace it in this rescue.
    if (!r.state) { r.state = 1; Commit(); }
    SyncObjects();
    return r.state == 2 || r.state == 3;
}
bool DireMaulNorth::RewardAvailable(uint32 index) const
{
    Reward const& r = m_progress.rewards[index]; if (r.state != 2) return false;
    if (r.opened)
    {
        if (GameObject* go = m_owner.instance->GetGameObject(m_rewards[index]))
            if (go->GetLootState() == GO_ACTIVATED) return true; // Native owns the live five minute deadline.
        return !r.chestUntil || r.chestUntil > Now();
    }
    return r.until > Now();
}
void DireMaulNorth::CloseReward(uint32 index) { m_progress.rewards[index] = Reward(); m_progress.rewards[index].state = 3; }
int32 DireMaulNorth::RewardIndex(ObjectGuid const& guid, uint32 entry) const
{
    for (uint32 i = 0; i < 2; ++i) if (entry == dmnRewardEntries[i])
    {
        GameObject* go = m_owner.instance->GetGameObject(guid);
        if (go && go->GetMap() == m_owner.instance && go->GetDbGuid() == dmnRewardGuids[i] && m_rewards[i] == guid) return int32(i);
    }
    return -1;
}
bool DireMaulNorth::GetLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string& snapshot) const
{
    int32 found = RewardIndex(guid, entry); if (found < 0) return false;
    snapshot = RewardAvailable(uint32(found)) ? m_progress.rewards[found].loot : "closed"; return true;
}
void DireMaulNorth::OnLootSnapshot(ObjectGuid const& guid, uint32 entry, std::string const& snapshot, bool exhausted)
{
    if (m_syncing) return;
    int32 found = RewardIndex(guid, entry); if (found < 0) return;
    uint32 i = uint32(found); Reward& r = m_progress.rewards[i]; if (r.state != 2) return;
    GameObject* go = m_owner.instance->GetGameObject(guid);
    if (exhausted || go->GetLootState() == GO_JUST_DEACTIVATED || !NorthSnapshot(snapshot)) { CloseReward(i); Commit(); return; }
    uint32 remaining = go->GetChestDespawnRemainingMS(); uint64 until = remaining ? Now() + remaining : 0;
    bool changed = !r.opened || r.chestUntil != until || r.loot != snapshot;
    r.opened = 1; r.chestUntil = until; r.loot = snapshot; if (changed) Commit();
}
void DireMaulNorth::LootStateChanged(GameObject* go)
{
    if (m_syncing || !go || go->GetLootState() != GO_JUST_DEACTIVATED) return;
    int32 found = RewardIndex(go->GetObjectGuid(), go->GetEntry());
    if (found >= 0 && m_progress.rewards[found].state == 2) { CloseReward(uint32(found)); Commit(); }
}
bool DireMaulNorth::ObserveRewards()
{
    bool changed = false;
    for (uint32 i = 0; i < 2; ++i) if (m_progress.rewards[i].state == 2)
    {
        GameObject* go = m_owner.instance->GetGameObject(m_rewards[i]);
        if ((go && go->GetLootState() == GO_JUST_DEACTIVATED) || !RewardAvailable(i)) { CloseReward(i); changed = true; }
    }
    return changed;
}
void DireMaulNorth::ObserveTrap()
{
    if (GameObject* trap = m_owner.instance->GetGameObject(m_trap))
    {
        if (!m_progress.trapUntil) TrapRepaired(trap);
        if (m_progress.trapUntil && (m_progress.trapUntil <= Now() || trap->GetLootState() == GO_JUST_DEACTIVATED))
        { m_progress.trapUntil = 0; Commit(); }
    }
    else if (m_progress.trapUntil && m_progress.trapUntil <= Now()) { m_progress.trapUntil = 0; Commit(); }
}
void DireMaulNorth::SyncObjects()
{
    if (m_syncing) return;
    m_syncing = true; bool changed = false; uint64 now = Now();
    for (uint32 i = 0; i < 2; ++i) if (GameObject* go = m_owner.instance->GetGameObject(m_rewards[i]))
    {
        Reward& r = m_progress.rewards[i];
        if (r.state == 1)
        {
            if (!go->IsSpawned())
            {
                // This request has never acknowledged a live reward. Discard
                // any exhausted native object left by the previous rescue,
                // rather than adopting that older generation's claim bytes.
                delete go->m_loot; go->m_loot = nullptr; go->RestoreChestDespawnRemainingMS(0);
                go->SetLootState(GO_READY); m_owner.DoRespawnGameObject(go->GetObjectGuid(), dmnRewardSeconds[i]);
            }
            if (go->IsSpawned() && go->GetLootState() != GO_JUST_DEACTIVATED)
            {
                r.state = 2; r.until = uint64(go->GetRespawnTime()) * 1000;
                if (!r.until || r.until <= now) r.until = now + uint64(dmnRewardSeconds[i]) * 1000;
                // Adopting an already visible native cache preserves its exact
                // roll and deadline if another legal rescue reaches it.
                if (go->m_loot)
                {
                    std::string loot = go->m_loot->GetInstanceSnapshot();
                    if (NorthSnapshot(loot))
                    { r.opened = 1; r.loot = loot; uint32 rem = go->GetChestDespawnRemainingMS(); r.chestUntil = rem ? now + rem : 0; }
                    else CloseReward(i);
                }
                changed = true;
            }
        }
        bool available = RewardAvailable(i);
        if (available && !go->IsSpawned())
        {
            uint64 until = r.opened ? r.chestUntil : r.until;
            go->SetLootState(GO_READY);
            m_owner.DoRespawnGameObject(go->GetObjectGuid(), until ? (until > now ? uint32((until - now + 999) / 1000) : 1) : 1);
        }
        if (available && r.opened && !go->m_loot && go->IsSpawned())
        {
            go->m_loot = new Loot(go, r.loot);
            if (go->GetLootState() == GO_ACTIVATED) go->RestoreChestDespawnRemainingMS(r.chestUntil ? (r.chestUntil > now ? uint32(r.chestUntil - now) : 1) : 0);
            else { CloseReward(i); changed = true; available = false; }
        }
        if (!available && r.state != 1 && go->IsSpawned()) go->ForcedDespawn();
    }
    if (GameObject* trap = m_owner.instance->GetGameObject(m_trap))
    {
        if (IsTrapActive() && !trap->IsSpawned())
        { trap->SetLootState(GO_READY); m_owner.DoRespawnGameObject(trap->GetObjectGuid(), uint32((m_progress.trapUntil - now + 999) / 1000)); }
        else if (m_progress.frozen && trap->IsSpawned()) trap->ForcedDespawn();
    }
    if (m_progress.chainUntil > now) if (GameObject* chain = m_owner.instance->GetGameObject(m_chain))
        if (chain->IsSpawned() && chain->GetLootState() != GO_JUST_DEACTIVATED)
        { chain->SetRespawnDelay(uint32((m_progress.chainUntil - now + 999) / 1000), true); chain->ForcedDespawn(); }
    m_syncing = false; if (changed) Commit();
}
void DireMaulNorth::ProcessMizzle()
{
    if (GetData(11) != DONE || m_progress.mizzleDead || m_paused || m_retry || m_attempts >= 5 || GetActor(14353)) return;
    WorldObject* source = GetActor(11501);
    if (!source) source = GetActor(14324);
    if (!source) source = m_owner.GetPlayerInMap(true, false);
    if (!source) return;
    ++m_attempts; m_retry = 1000; m_summoning = true;
    Creature* actor = source->SummonCreature(14353, 683.296f, 484.384f, 29.544f, 0.0174f, TEMPSPAWN_DEAD_DESPAWN, 0, true);
    m_summoning = false;
    if (actor && actor->IsAlive() && actor->GetMap() == m_owner.instance && m_owner.instance->GetCreature(actor->GetObjectGuid()) == actor)
    {
        m_actors[7] = actor->GetObjectGuid(); m_attempts = 0;
        if (!m_progress.mizzleAcknowledged)
        { m_progress.mizzleAcknowledged = 1; m_progress.mizzle = {DMN_MIZZLE_TO_KING, 0, 60000}; }
        Commit();
    }
    else
    {
        // Create is called before AddToWorld; an unreturned/incomplete actor is
        // never acknowledgement and its provisional identity is discarded.
        m_actors[7].Clear();
        if (m_attempts == 5) script_error_log("Dire Maul North: Mizzle allocation exhausted bounded retries; pending scene retained");
    }
}
void DireMaulNorth::SyncActors()
{
    if (GetData(11) == DONE)
    {
        for (uint32 i : {4u, 5u}) if (Creature* actor = GetActor(dmnActors[i])) if (actor->IsAlive())
        {
            if (actor->GetFaction() != 35) actor->SetFactionTemporary(35, TEMPFACTION_RESTORE_RESPAWN);
            if (actor->GetVictim()) actor->AI()->EnterEvadeMode();
            if (i == 5 && actor->getStandState() != UNIT_STAND_STATE_SIT) actor->SetStandState(UNIT_STAND_STATE_SIT);
        }
        if (!m_progress.kingSaid) if (Creature* cho = GetActor(14324)) if (cho->IsAlive())
        { m_progress.kingSaid = 1; Commit(); DoScriptText(-1429003, cho); }
    }
    if (m_progress.knot.stage == DMN_KNOT_AWAY && !IsKnotAway())
    { m_progress.knot = SceneState(); m_progress.knotReturn = 0; Commit(); }
    if (IsKnotAway()) if (Creature* knot = GetActor(14338)) if (knot->IsAlive())
    {
        knot->SetRespawnDelay((KnotReturnRemainingMS() + 999) / 1000, true);
        knot->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP | UNIT_NPC_FLAG_QUESTGIVER); knot->ForcedDespawn();
    }
}
void DireMaulNorth::OnCreatureCreate(Creature* actor)
{
    if (!actor || actor->GetMap() != m_owner.instance) return;
    for (uint32 i = 0; i < 9; ++i) if (actor->GetEntry() == dmnActors[i])
    {
        if ((i == 7 && m_summoning) || (i != 7 && actor->HasStaticDBSpawnData())) m_actors[i] = actor->GetObjectGuid();
        return; // Only register identity; Map has not yet acknowledged AddToWorld.
    }
}
void DireMaulNorth::OnCreatureRespawn(Creature* actor) { OnCreatureCreate(actor); SyncActors(); }
void DireMaulNorth::OnCreatureEnterCombat(Creature* actor) { if (IsCurrentActor(actor) && actor->GetEntry() == 11501) SetData(11, IN_PROGRESS); }
void DireMaulNorth::OnCreatureEvade(Creature* actor) { if (IsCurrentActor(actor) && actor->GetEntry() == 11501 && GetData(11) == IN_PROGRESS) SetData(11, NOT_STARTED); }
void DireMaulNorth::OnCreatureDeath(Creature* actor)
{
    if (!IsCurrentActor(actor)) return;
    // DONE is a historical tribute kill record, retained across the static
    // twelve-hour respawn. The currently frozen body can die again while that
    // record is already DONE, so its physical state must clear independently.
    if (actor->GetEntry() == 14323 && (m_progress.frozen || m_progress.gordokFaction))
    { m_progress.frozen = m_progress.gordokFaction = 0; Commit(); }
    for (uint32 i = 0; i < 7; ++i) if (actor->GetEntry() == dmnActors[i]) { SetData(11 + i, DONE); return; }
    if (actor->GetEntry() == 14353) { m_progress.mizzleDead = 1; Commit(); }
}
void DireMaulNorth::OnCreatureDespawn(Creature* actor)
{
    if (actor && m_actors[7] == actor->GetObjectGuid()) { m_actors[7].Clear(); m_retry = 0; }
}
void DireMaulNorth::OnObjectCreate(GameObject* go)
{
    if (!go || go->GetMap() != m_owner.instance) return;
    for (uint32 i = 0; i < 2; ++i) if (go->GetEntry() == dmnRewardEntries[i] && go->GetDbGuid() == dmnRewardGuids[i]) m_rewards[i] = go->GetObjectGuid();
    if (go->GetEntry() == 179512 && go->GetDbGuid() == 4290049) m_trap = go->GetObjectGuid();
    if (go->GetEntry() == 179511) m_chain = go->GetObjectGuid();
}
void DireMaulNorth::OnPlayerEnter(Player*) { m_paused = false; m_attempts = 0; m_retry = 0; SyncActors(); ProcessMizzle(); SyncObjects(); }
void DireMaulNorth::OnPlayerLeave(Player*) { Commit(); }
void DireMaulNorth::Update(uint32 diff)
{
    if (m_retry > diff) m_retry -= diff; else m_retry = 0;
    ObserveTrap(); if (ObserveRewards()) Commit(); SyncObjects();
    if (!m_owner.GetPlayerInMap(false, false)) { if (!m_paused) { m_paused = true; Commit(); } return; }
    m_paused = false; SyncActors(); ProcessMizzle();
    if (m_checkpoint > diff) m_checkpoint -= diff;
    else
    {
        if (m_progress.mizzle.timer || m_progress.krom.timer || m_progress.knot.timer ||
            m_progress.rewards[0].state == 1 || m_progress.rewards[1].state == 1 || m_progress.trapUntil) Commit();
        m_checkpoint = 5000;
    }
}

struct go_dmn_rewardAI : public GameObjectAI
{
    explicit go_dmn_rewardAI(GameObject* go) : GameObjectAI(go) {}
    void OnLootStateChange(Unit*) override
    {
        if (auto* data = static_cast<instance_dire_maul*>(m_go->GetInstanceData())) data->North().LootStateChanged(m_go);
    }
};
void AddSC_dire_maul_north_objects()
{
    Script* script = new Script;
    script->Name = "go_dmn_reward"; script->GetGameObjectAI = &GetNewAIInstance<go_dmn_rewardAI>; script->RegisterSelf();
}
