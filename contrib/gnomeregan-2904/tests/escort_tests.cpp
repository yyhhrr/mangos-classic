// Tests of the actual quest AI with narrow CMaNGOS engine interface doubles.
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using uint32 = std::uint32_t;
enum { TYPEID_PLAYER = 4, TYPEID_UNIT = 3, REACT_PASSIVE = 0, UNIT_STAND_STATE_STAND = 0 };
enum { STATE_FOLLOW_INPROGRESS = 1, STATE_FOLLOW_COMPLETE = 8, NPC_ALARM_A_BOMB_2600 = 7897 };
enum { UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER = 0x100, UNIT_FLAG_IMMUNE_TO_NPC = 0x200 };
enum { TRIGGERED_OLD_TRIGGERED = 1, TRIGGERED_NORMAL_COMBAT_CAST = 0x100, SPELL_STATE_FINISHED = 7 };
enum { DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, QUEST_STATUS_INCOMPLETE, QUEST_STATUS_COMPLETE, QUEST_STATUS_FAILED };
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_BAD_TARGETS };
enum AIEventType { AI_EVENT_START_EVENT, AI_EVENT_CUSTOM_A };
constexpr float PET_FOLLOW_DIST = 1.0f, PET_FOLLOW_ANGLE = 1.0f, INTERACTION_DISTANCE = 5.0f;

struct ObjectGuid
{
    uint32 value = 0;
    bool IsEmpty() const { return value == 0; }
    explicit operator bool() const { return !IsEmpty(); }
    void Clear() { value = 0; }
    bool operator==(ObjectGuid other) const { return value == other.value; }
    bool operator!=(ObjectGuid other) const { return !(*this == other); }
};

struct Quest { uint32 id = 2904; uint32 GetQuestId() const { return id; } };
const Quest* GetQuestTemplateStore(uint32 id)
{
    static Quest quest;
    return id == quest.id ? &quest : nullptr;
}

struct WorldObject { virtual ~WorldObject() = default; };
struct Unit : WorldObject
{
    virtual uint32 GetTypeId() const { return TYPEID_UNIT; }
    virtual ObjectGuid GetObjectGuid() const { return {}; }
    static uint32 DealDamage(Unit*, Unit*, uint32, void*, int, int, void*, bool);
};
struct Creature;
struct Player;
struct GroupReference
{
    Player* player; GroupReference* following = nullptr;
    Player* getSource() { return player; }
    GroupReference* next() { return following; }
};
struct Group
{
    GroupReference* first = nullptr;
    GroupReference* GetFirstMember() { return first; }
};
struct Player : Unit
{
    unsigned rewards = 0;
    unsigned failures = 0;
    int questStatus = QUEST_STATUS_INCOMPLETE;
    bool alive = true;
    ObjectGuid guid;
    Group* group = nullptr;
    uint32 rewardedQuest = 0;
    Creature* rewardSource = nullptr;
    uint32 GetTypeId() const override { return TYPEID_PLAYER; }
    ObjectGuid GetObjectGuid() const override { return guid; }
    Group* GetGroup() { return group; }
    int GetQuestStatus(uint32) { return questStatus; }
    void FailQuest(uint32) { ++failures; questStatus = QUEST_STATUS_FAILED; }
    void FailQuestForGroup(uint32);
    void RewardPlayerAndGroupAtEventExplored(uint32 id, Creature* source)
    {
        ++rewards;
        rewardedQuest = id;
        rewardSource = source;
        questStatus = QUEST_STATUS_COMPLETE;
    }
};

struct MotionMaster
{
    enum Mode { Waypoint, Idle, Follow, Home, Point } mode = Waypoint;
    Unit* target = nullptr;
    unsigned homeMoves = 0;
    void Clear() { mode = Idle; target = nullptr; }
    void MoveFollow(Unit* who, float, float) { mode = Follow; target = who; }
    void MoveTargetedHome() { mode = Home; ++homeMoves; }
    void MovePoint(uint32, float, float, float) { mode = Point; }
};

struct InstanceData { virtual ~InstanceData() = default; };
struct Map
{
    std::map<uint32, Creature*> creatures;
    std::map<uint32, Player*> players;
    Player* GetPlayer(ObjectGuid guid)
    {
        auto found = players.find(guid.value);
        return found == players.end() ? nullptr : found->second;
    }
    Creature* GetCreature(ObjectGuid guid)
    {
        auto found = creatures.find(guid.value);
        return found == creatures.end() ? nullptr : found->second;
    }
};

struct SpellEntry { uint32 Id = 9874; };
struct Spell
{
    SpellEntry entry;
    SpellEntry const* m_spellInfo = &entry;
    Unit* caster;
    uint32 state = SPELL_STATE_FINISHED;
    Unit* GetCaster() const { return caster; }
    uint32 getState() const { return state; }
};

struct UnitAI
{
    explicit UnitAI(Creature* creature) : m_creature(creature) {}
    virtual ~UnitAI() = default;
    virtual void Reset() {}
    virtual void JustRespawned() { Reset(); }
    virtual void AttackStart(Unit*) {}
    virtual void UpdateAI(uint32) {}
    virtual void JustDied(Unit*) {}
    virtual void CorpseRemoved(uint32&) {}
    virtual void ReceiveAIEvent(AIEventType, Unit*, Unit*, uint32) {}
    virtual void OnSpellCastStateChange(Spell const*, bool, WorldObject* = nullptr) {}
    Creature* m_creature;
};

struct Creature : Unit
{
    Creature(Map& map, uint32 guid, uint32 entry) : map(&map), guid{guid}, entry(entry)
    {
        map.creatures[guid] = this;
    }
    ~Creature() override { map->creatures.erase(guid.value); }
    Map* map;
    ObjectGuid guid;
    uint32 entry;
    UnitAI* ai = nullptr;
    InstanceData* instance = nullptr;
    MotionMaster motion;
    bool alive = true, combat = false;
    uint32 flags = 0x8300, health = 1000;
    unsigned casts = 0, directHits = 0;
    bool walk = true, deferSpell = false;
    SpellCastResult castResult = SPELL_CAST_OK;
    uint32 castId = 0, castFlags = 0;
    uint32 standState = 7, despawnDelay = 0;
    float x = -500, y = 100, z = -200;
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    uint32 GetEntry() const { return entry; }
    ObjectGuid GetObjectGuid() const override { return guid; }
    uint32 GetHealth() const { return health; }
    bool HasFlag(uint32, uint32 mask) const { return (flags & mask) != 0; }
    void SetFlag(uint32, uint32 mask) { flags |= mask; }
    void RemoveFlag(uint32, uint32 mask) { flags &= ~mask; }
    void SetWalk(bool value) { walk = value; }
    bool IsWithinDistInMap(Creature* other, float distance) const
    {
        return IsWithinDist3d(other->x, other->y, other->z, distance);
    }
    SpellCastResult CastSpell(Unit* target, uint32 id, uint32 triggerFlags)
    {
        ++casts; castId = id; castFlags = triggerFlags;
        if (castResult == SPELL_CAST_OK && !deferSpell)
            FinishSpell();
        return castResult;
    }
    void FinishSpell()
    {
        Spell spell; spell.caster = this; spell.entry.Id = castId;
        ai->OnSpellCastStateChange(&spell, false);
    }
    Map* GetMap() const { return map; }
    UnitAI* AI() const { return ai; }
    InstanceData* GetInstanceData() const { return instance; }
    MotionMaster* GetMotionMaster() { return &motion; }
    void SetStandState(uint32 state) { standState = state; }
    void ForcedDespawn(uint32 delay) { despawnDelay = delay; }
    bool IsWithinDist3d(float dx, float dy, float dz, float distance) const
    {
        return std::sqrt((x-dx)*(x-dx) + (y-dy)*(y-dy) + (z-dz)*(z-dz)) <= distance;
    }
};

uint32 Unit::DealDamage(Unit* killer, Unit* victim, uint32 damage, void*, int, int, void*, bool)
{
    Creature* creature = dynamic_cast<Creature*>(victim);
    if (!creature || !creature->alive)
        throw std::runtime_error("duplicate damage to dead creature");
    ++creature->directHits;
    creature->health = damage >= creature->health ? 0 : creature->health - damage;
    if (!creature->health)
    {
        creature->alive = false;
        creature->ai->JustDied(killer);
    }
    return damage;
}

struct ScriptedAI : UnitAI
{
    using UnitAI::UnitAI;
    int reactState = 1;
    bool meleeEnabled = true, combatMovement = true;
    void SetReactState(int state) { reactState = state; }
    void SetMeleeEnabled(bool enabled) { meleeEnabled = enabled; }
    void SetCombatMovement(bool enabled) { combatMovement = enabled; }
};

struct FollowerAI : ScriptedAI
{
    using ScriptedAI::ScriptedAI;
    uint32 followState = 0;
    Player* leader = nullptr;
    const Quest* quest = nullptr;
    unsigned corpseCallbacks = 0;
    ObjectGuid m_leaderGuid;
    const Quest* m_questForFollow = nullptr;
    bool paused = false;
    bool HasFollowState(uint32 state) const { return (followState & state) != 0; }
    void StartFollow(Player* player, uint32, const Quest* acceptedQuest)
    {
        if (HasFollowState(STATE_FOLLOW_INPROGRESS))
            throw std::runtime_error("duplicate StartFollow reached base AI");
        followState = STATE_FOLLOW_INPROGRESS;
        leader = player;
        quest = acceptedQuest;
        m_questForFollow = acceptedQuest; m_leaderGuid = player->guid;
        m_creature->motion.MoveFollow(player, PET_FOLLOW_DIST, PET_FOLLOW_ANGLE);
    }
    Player* GetLeaderForFollower()
    {
        Player* player = m_creature->map->GetPlayer(m_leaderGuid);
        if (player && player->alive) return player;
        if (player && player->group)
            for (GroupReference* ref = player->group->first; ref; ref = ref->following)
                if (ref->player && ref->player->alive) return ref->player;
        return nullptr;
    }
    void SetFollowPaused(bool value) { paused = value; }
    void SetFollowComplete(bool) { followState |= STATE_FOLLOW_COMPLETE; }
    void JustDied(Unit*) override;
    void CorpseRemoved(uint32&) override { ++corpseCallbacks; followState = 0; }
    virtual void UpdateFollowerAI(uint32) {}
    void UpdateAI(uint32 diff) override { UpdateFollowerAI(diff); }
};

#include "failure_under_test.h"

struct instance_gnomeregan : InstanceData
{
    Creature* alarm = nullptr;
    Creature* GetSingleCreatureFromStorage(uint32 entry, bool)
    {
        return entry == NPC_ALARM_A_BOMB_2600 ? alarm : nullptr;
    }
};

std::vector<std::pair<uint32, Creature*>> broadcasts;
void DoBroadcastText(uint32 id, Creature* speaker) { broadcasts.emplace_back(id, speaker); }
void script_error_log(const char*, uint32) {}

#include "escort_under_test.h"

void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

unsigned CountText(uint32 id)
{
    unsigned count = 0;
    for (auto const& text : broadcasts)
        if (text.first == id)
            ++count;
    return count;
}

struct Fixture
{
    Map map;
    instance_gnomeregan instance;
    Player player, otherPlayer;
    Creature escort{map, 1, 7850}, bomb{map, 2, 7897}, otherEscort{map, 3, 7850};
    npc_kernobeeAI escortAI{&escort}, otherEscortAI{&otherEscort};
    npc_alarm_a_bomb_2600AI bombAI{&bomb};
    Fixture()
    {
        broadcasts.clear();
        player.guid = {4}; otherPlayer.guid = {5};
        map.players[4] = &player; map.players[5] = &otherPlayer;
        escort.ai = &escortAI;
        otherEscort.ai = &otherEscortAI;
        bomb.ai = &bombAI;
        escort.instance = otherEscort.instance = &instance;
        instance.alarm = &bomb;
    }
    void Accept(npc_kernobeeAI& ai, Player& owner)
    {
        ai.ReceiveAIEvent(AI_EVENT_START_EVENT, nullptr, &owner, 2904);
    }
    void Accept() { Accept(escortAI, player); }
    void AtExit()
    {
        escort.x = -330.92f; escort.y = -3.03f; escort.z = -152.85f;
    }
    void Arm()
    {
        Accept(); escortAI.UpdateAI(2000);
        Check(escortAI.m_alarmState == npc_kernobeeAI::COUNTDOWN, "bomb did not arm");
    }
    void Warn()
    {
        bomb.x = -390.82f; bomb.y = 42.34f; bomb.z = -154.795f;
        escortAI.UpdateAI(1);
    }
};

int main()
{
    unsigned passed = 0;
    auto Run = [&](const char* name, auto test)
    {
        test();
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    try
    {
        Run("inactive and unrelated events cannot acquire the bomb", []
        {
            Fixture f;
            Check(!f.bombAI.StartFollowing(&f.escort), "inactive Kernobee acquired bomb");
            f.escortAI.ReceiveAIEvent(AI_EVENT_START_EVENT, nullptr, nullptr, 2904);
            f.escortAI.ReceiveAIEvent(AI_EVENT_START_EVENT, nullptr, &f.player, 1);
            Check(!f.escortAI.HasFollowState(STATE_FOLLOW_INPROGRESS), "unrelated event started escort");
            Check(f.bomb.motion.mode == MotionMaster::Waypoint, "idle patrol changed");
        });
        Run("acceptance links bomb to Kernobee and preserves passive behavior", []
        {
            Fixture f; f.Accept();
            Check(f.escortAI.quest && f.escortAI.quest->id == 2904, "wrong quest passed to follower");
            Check(f.bomb.motion.target == &f.escort, "bomb followed the player instead of Kernobee");
            Check(f.bombAI.reactState == REACT_PASSIVE && !f.bombAI.meleeEnabled && !f.bombAI.combatMovement,
                "bomb enabled combat");
            Check(CountText(3927) == 1 && CountText(3881) == 1, "acceptance dialogue missing or repeated");
        });
        Run("duplicate acceptance and another escort cannot steal ownership", []
        {
            Fixture f; f.Accept(); f.escortAI.m_alarmTimer = 37;
            f.Accept(f.escortAI, f.otherPlayer); f.Accept(f.otherEscortAI, f.otherPlayer);
            f.bombAI.StopFollowing(&f.otherEscort, true);
            f.bombAI.Reset();
            Check(f.escortAI.leader == &f.player && f.escortAI.m_alarmTimer == 37,
                "duplicate acceptance reset active escort");
            Check(f.bombAI.m_kernobeeGuid == f.escort.guid && f.bomb.motion.target == &f.escort,
                "bomb ownership stolen or reset on evade");
            Check(CountText(3927) == 1 && CountText(3950) == 0, "foreign escort controlled bomb");
        });
        Run("existing exit awards once and disarms without explosion", []
        {
            Fixture f; f.Accept(); f.AtExit(); f.escortAI.UpdateAI(10000);
            Check(f.player.rewards == 1 && f.player.rewardedQuest == 2904 && f.player.rewardSource == &f.escort,
                "existing group reward API not called");
            Check(CountText(3950) == 1 && CountText(3929) == 1, "completion dialogue missing");
            Check(f.bombAI.m_kernobeeGuid.IsEmpty() && f.bomb.motion.mode == MotionMaster::Home,
                "bomb not disarmed and released");
            Check(f.escort.motion.mode == MotionMaster::Point && f.escort.despawnDelay == 2000,
                "existing exit movement or despawn changed");
            f.escortAI.UpdateAI(10000); f.bombAI.UpdateAI(10000);
            Check(f.player.rewards == 1 && CountText(3950) == 1, "completion repeated");
        });
        Run("missing or unbound bomb cannot prevent the original quest completion", []
        {
            Fixture f; f.instance.alarm = nullptr; f.Accept(); f.AtExit(); f.escortAI.UpdateAI(10000);
            Check(f.player.rewards == 1, "missing bomb blocked completion");
            Fixture g; ScriptedAI unbound(&g.bomb); g.bomb.ai = &unbound;
            g.Accept(); g.AtExit(); g.escortAI.UpdateAI(10000);
            Check(g.player.rewards == 1 && g.bomb.motion.mode == MotionMaster::Waypoint,
                "unbound bomb blocked completion or patrol");
        });
        Run("late loading and recreated bomb AI can acquire the active escort", []
        {
            Fixture f; f.instance.alarm = nullptr; f.Accept(); f.instance.alarm = &f.bomb;
            f.escortAI.UpdateAI(10000);
            Check(f.bomb.motion.target == &f.escort, "late-loaded bomb never linked");
            f.bombAI.JustRespawned(); f.escortAI.UpdateAI(500);
            Check(f.bombAI.m_kernobeeGuid == f.escort.guid, "recreated AI never relinked");
        });
        Run("disappearing or dead escort releases the bomb safely", []
        {
            Fixture f; f.Accept(); f.map.creatures.erase(f.escort.guid.value); f.bombAI.UpdateAI(1);
            Check(f.bombAI.m_kernobeeGuid.IsEmpty() && f.bomb.motion.mode == MotionMaster::Home,
                "lost escort left bomb following stale object");
            Fixture g; g.Accept(); g.escort.alive = false; g.bombAI.UpdateAI(1);
            Check(g.bombAI.m_kernobeeGuid.IsEmpty(), "dead escort retained bomb");
        });
        Run("death and corpse removal retain base follower handling", []
        {
            Fixture f; f.Accept(); f.escortAI.JustDied(nullptr);
            Check(f.player.failures == 1 && f.bombAI.m_kernobeeGuid.IsEmpty(),
                "base quest failure handling skipped");
            uint32 respawnDelay = 99; f.escortAI.CorpseRemoved(respawnDelay);
            Check(f.escortAI.corpseCallbacks == 1 && f.escortAI.m_alarmTimer == 2000,
                "corpse cleanup did not reset escort");
        });
        Run("combat retains the existing completion guard", []
        {
            Fixture f; f.Accept(); f.AtExit(); f.escort.combat = true; f.escortAI.UpdateAI(10000);
            Check(f.player.rewards == 0 && !f.bombAI.m_kernobeeGuid.IsEmpty(), "combat awarded prematurely");
            f.escort.combat = false; f.escortAI.UpdateAI(10000);
            Check(f.player.rewards == 1, "completion did not resume after combat");
        });
        Run("ordinary idle updates retain database waypoint patrol", []
        {
            Fixture f; f.bombAI.UpdateAI(300000);
            Check(f.bomb.motion.mode == MotionMaster::Waypoint && f.bomb.motion.homeMoves == 0,
                "idle AI interrupted patrol");
        });
        Run("arrival starts 300 seconds after the initial two-second check", []
        {
            Fixture f; f.Accept(); f.bomb.x -= 50;
            f.escortAI.UpdateAI(2000);
            Check(f.escortAI.m_alarmState == npc_kernobeeAI::WAITING_FOR_BOMB && f.escortAI.paused,
                "timer started before bomb arrived");
            f.bomb.x = f.escort.x; f.escortAI.UpdateAI(499);
            Check(f.escortAI.m_alarmState == npc_kernobeeAI::WAITING_FOR_BOMB, "arrival check fired early");
            f.escortAI.UpdateAI(1);
            Check(f.escortAI.m_alarmTimer == 300000 && !f.escortAI.paused && f.escort.walk,
                "arrival did not start full 300 seconds");
        });
        Run("300-second boundary casts the real spell id exactly once", []
        {
            Fixture f; f.Arm(); f.escortAI.UpdateAI(299999);
            Check(f.bomb.casts == 0 && f.escortAI.m_alarmTimer == 1, "300-second timer expired early");
            f.escortAI.UpdateAI(1);
            Check(f.bomb.casts == 1 && f.bomb.castId == 9874 &&
                f.bomb.castFlags == (TRIGGERED_OLD_TRIGGERED | TRIGGERED_NORMAL_COMBAT_CAST), "wrong engine spell invocation");
            Check(f.escort.alive && f.bomb.alive, "fallback killed NPCs inside cast stack");
            f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1); f.bombAI.UpdateAI(300000);
            Check(f.bomb.casts == 1 && f.escort.directHits == 1 && f.bomb.directHits == 1 &&
                f.player.failures == 1, "timeout, fallback or quest failure repeated");
        });
        Run("combat pauses initial, 300-second and final clocks", []
        {
            Fixture f; f.Accept(); f.escort.combat = true; f.escortAI.UpdateAI(999999);
            Check(f.escortAI.m_alarmTimer == 2000, "combat consumed arrival timer");
            f.escort.combat = false; f.escortAI.UpdateAI(2000);
            f.escort.combat = true; f.escortAI.UpdateAI(999999);
            Check(f.escortAI.m_alarmTimer == 300000 && f.bomb.casts == 0, "combat consumed active timer");
            f.escort.combat = false; f.Warn(); f.escort.combat = true; f.escortAI.UpdateAI(999999);
            Check(f.escortAI.m_alarmTimer == 20000 && f.bomb.casts == 0, "combat consumed final timer");
        });
        Run("exit warning resets the same timer once, including a coincident deadline", []
        {
            Fixture f; f.Arm(); f.escortAI.UpdateAI(299999); f.Warn();
            Check(f.bomb.casts == 0 && f.escortAI.m_alarmTimer == 20000 && CountText(3949) == 1,
                "warning did not reset imminent timeout to twenty seconds");
            f.escortAI.UpdateAI(10000); f.escortAI.UpdateAI(9999);
            Check(f.escortAI.m_alarmTimer == 1 && CountText(3949) == 1 && f.bomb.casts == 0,
                "remaining in warning area reset timer repeatedly");
            f.escortAI.UpdateAI(1);
            Check(f.bomb.casts == 1, "final timer did not expire at exact boundary");
            Fixture g; g.Arm(); g.Warn();
            Check(g.escortAI.m_alarmTimer == 20000, "warning added twenty seconds to five minutes");
        });
        Run("warning follows bomb position rather than Kernobee position", []
        {
            Fixture f; f.Arm(); f.escort.x = -390.82f; f.escort.y = 42.34f; f.escort.z = -154.795f;
            f.escortAI.UpdateAI(1);
            Check(f.escortAI.m_alarmState == npc_kernobeeAI::COUNTDOWN, "escort alone triggered bomb warning");
        });
        Run("success wins simultaneous initial and final deadlines", []
        {
            Fixture f; f.Arm(); f.escortAI.UpdateAI(299999); f.AtExit(); f.escortAI.UpdateAI(1);
            Check(f.player.rewards == 1 && f.bomb.casts == 0, "initial deadline beat success");
            Fixture g; g.Arm(); g.Warn(); g.escortAI.UpdateAI(19999); g.AtExit(); g.escortAI.UpdateAI(1);
            g.bombAI.UpdateAI(999999);
            Check(g.player.rewards == 1 && g.bomb.casts == 0 && g.escort.alive && g.bomb.alive,
                "final deadline or success cleanup exploded");
        });
        Run("deferred cast cannot kill or release NPCs before completion", []
        {
            Fixture f; f.Arm(); f.bomb.deferSpell = true; f.escortAI.UpdateAI(300000);
            Check(f.bomb.flags == (0x8300 & ~UNIT_FLAG_IMMUNE_TO_PLAYER), "wrong immunity temporarily released");
            f.bombAI.UpdateAI(10000); f.bombAI.Reset(); f.bombAI.StopFollowing(&f.escort, false);
            Check(f.escort.alive && f.bomb.alive && !f.bombAI.m_kernobeeGuid.IsEmpty(),
                "pending spell canceled by fallback, reset or owner cleanup");
            f.bomb.FinishSpell();
            Check(f.bomb.flags == 0x8300 && f.escort.alive, "completion did not restore flags or killed inside callback");
            f.bombAI.UpdateAI(1);
            Check(!f.escort.alive && !f.bomb.alive && f.player.failures == 1, "post-settlement fallback missing");
        });
        Run("unrelated or nonterminal spell callbacks cannot release pending explosion", []
        {
            Fixture f; f.Arm(); f.bomb.deferSpell = true; f.escortAI.UpdateAI(300000);
            Spell spell; spell.caster = &f.bomb; spell.entry.Id = 27745;
            f.bombAI.OnSpellCastStateChange(&spell, false);
            spell.entry.Id = 9874; spell.state = 1; f.bombAI.OnSpellCastStateChange(&spell, false);
            spell.state = SPELL_STATE_FINISHED; spell.caster = &f.escort;
            f.bombAI.OnSpellCastStateChange(&spell, false); f.bombAI.UpdateAI(1);
            Check(f.escort.alive && !f.bombAI.m_explosionFinished, "unrelated callback settled explosion");
        });
        Run("already-dead Kernobee receives no second damage or failure", []
        {
            Fixture f; f.Arm(); f.bomb.deferSpell = true; f.escortAI.UpdateAI(300000);
            Unit::DealDamage(&f.bomb, &f.escort, f.escort.health, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
            f.bomb.FinishSpell(); f.bombAI.UpdateAI(1); f.bombAI.UpdateAI(1);
            Check(f.escort.directHits == 1 && f.player.failures == 1, "dead Kernobee was damaged or failed twice");
        });
        Run("dead leader and group fail once through actual engine helpers", []
        {
            Fixture f; GroupReference second{&f.otherPlayer}, first{&f.player, &second}; Group group{&first};
            f.player.group = f.otherPlayer.group = &group; f.Arm();
            f.player.alive = f.otherPlayer.alive = false;
            f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1); f.escortAI.JustDied(&f.bomb);
            Check(f.player.failures == 1 && f.otherPlayer.failures == 1, "dead group did not fail once");
        });
        Run("completed group quests remain complete on escort failure", []
        {
            Fixture f; GroupReference second{&f.otherPlayer}, first{&f.player, &second}; Group group{&first};
            f.player.group = f.otherPlayer.group = &group; f.otherPlayer.questStatus = QUEST_STATUS_COMPLETE;
            f.Arm(); f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1);
            Check(f.player.failures == 1 && f.otherPlayer.failures == 0, "completed quest was failed");
        });
        Run("offline leader and despawn cleanup leave no stale patrol owner", []
        {
            Fixture f; f.Accept(); f.map.players.erase(f.player.guid.value);
            f.escortAI.JustDied(nullptr);
            Check(f.player.failures == 0 && f.bombAI.m_kernobeeGuid.IsEmpty(), "offline pointer was used");
            Fixture g; g.Accept(); g.map.creatures.erase(g.escort.guid.value); g.bombAI.UpdateAI(1);
            Check(g.bomb.motion.mode == MotionMaster::Home && !g.bombAI.m_exploding, "offline despawn left bomb active");
        });
        Run("corpse and respawn allow a clean new acceptance", []
        {
            Fixture f; f.Arm(); f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1);
            uint32 respawn = 99; f.escortAI.CorpseRemoved(respawn);
            f.escort.alive = f.bomb.alive = true; f.escort.health = f.bomb.health = 1000;
            f.bombAI.JustRespawned(); f.player.questStatus = QUEST_STATUS_INCOMPLETE; f.Accept();
            Check(f.escortAI.m_alarmTimer == 2000 && !f.bombAI.m_exploding && f.bomb.flags == 0x8300,
                "new run retained explosion state or modified flags");
            f.escortAI.UpdateAI(2000); f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1);
            Check(f.player.failures == 2 && f.bomb.casts == 2, "new run failed to reset once-only guards");
        });
        Run("failed cast start settles without retry and restores immunity", []
        {
            Fixture f; f.Arm(); f.bomb.castResult = SPELL_FAILED_BAD_TARGETS;
            f.escortAI.UpdateAI(300000); f.bombAI.UpdateAI(1); f.bombAI.UpdateAI(1);
            Check(f.bomb.casts == 1 && f.bomb.flags == 0x8300 && f.player.failures == 1,
                "failed cast remained pending, retried or leaked flags");
        });
    }
    catch (std::exception const& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << passed << " cases passed; these doubles do not test live pathfinding or client behavior.\n";
    return 0;
}
