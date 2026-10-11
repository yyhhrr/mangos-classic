// Compile the actual CMaNGOS CanAttackSpell/CanAttackInCombat/CanAttackServerside
// functions. Peripheral faction/player services are narrow fixture doubles.
#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <vector>
using uint32 = std::uint32_t;
#define MANGOS_ASSERT(value) assert(value);
enum { TYPEID_UNIT = 3, TYPEID_PLAYER = 4, UNIT_FIELD_FLAGS, PLAYER_FLAGS,
    PLAYER_FLAGS_GHOST = 0x10, PLAYER_DUEL_TEAM, PLAYER_DUEL_ARBITER };
enum { UNIT_FLAG_PLAYER_CONTROLLED = 8, UNIT_FLAG_IMMUNE_TO_PLAYER = 0x100,
    UNIT_FLAG_IMMUNE_TO_NPC = 0x200, UNIT_FLAG_SPAWNING = 2,
    UNIT_FLAG_NOT_ATTACKABLE_1 = 0x80, UNIT_FLAG_UNTARGETABLE = 0x10000,
    UNIT_FLAG_TAXI_FLIGHT = 0x100000, UNIT_FLAG_UNINTERACTIBLE = 0x2000000 };
enum { SPELL_ATTR_EX2_ALLOW_DEAD_TARGET, SPELL_ATTR_EX3_IGNORE_CASTER_AND_TARGET_RESTRICTIONS };
enum class CreatureTypeFlags { VISIBLE_TO_GHOSTS };
struct SpellEntry { bool HasAttribute(int) const { return false; } }; // 9874: all attribute fields zero.
struct FactionTemplateEntry { uint32 faction = 32; };
struct FactionEntry { bool HasReputation() const { return false; } };
struct FactionStore
{
    FactionEntry entry;
    FactionEntry const* LookupEntry(uint32) const { return &entry; }
} sFactionStore;
struct Reputation { bool IsAtWar(FactionEntry const*) const { return false; } };
struct Player;
struct Unit
{
    virtual ~Unit() = default;
    uint32 flags = 0;
    bool alive = true, friendly = false, enemy = false;
    bool inMap = true, taxi = false, aoeImmune = false;
    float x = 0, y = 0, z = 0;
    Player const* controller = nullptr;
    bool IsAlive() const { return alive; }
    bool HasFlag(uint32, uint32 bits) const { return (flags & bits) != 0; }
    virtual uint32 GetTypeId() const { return TYPEID_UNIT; }
    bool IsPlayerControlled() const { return HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED); }
    bool IsControlledByPlayer() const { return IsPlayerControlled(); }
    bool IsInMap(Unit const*) const { return inMap; }
    bool IsTaxiFlying() const { return taxi; }
    bool IsChainImmune() const { return false; }
    bool IsAOEImmune() const { return aoeImmune; }
    bool CanAssistSpell(Unit const*, SpellEntry const*) const { return false; }
    float GetPositionZ() const { return z; }
    float GetCombatReach() const { return 1.5f; }
    float GetDistance2d(float dx, float dy, int) const { return (x-dx)*(x-dx)+(y-dy)*(y-dy); }
    float GetDistance(float dx, float dy, float dz, int) const { return (x-dx)*(x-dx)+(y-dy)*(y-dy)+(z-dz)*(z-dz); }
    bool isInFront(Unit const*, float, float) const { return true; }
    bool isInBack(Unit const*, float, float) const { return false; }
    bool IsFriend(Unit const* unit) const { return friendly || unit->friendly; }
    bool IsEnemy(Unit const* unit) const { return enemy || unit->enemy; }
    Player const* GetControllingPlayer() const { return controller; }
    FactionTemplateEntry const* GetFactionTemplateEntry() const { static FactionTemplateEntry neutral594; return &neutral594; }
    bool CanAttackSpell(Unit const*, SpellEntry const* = nullptr, bool = false) const;
    bool CanAttackInCombat(Unit const*, bool, bool) const;
    bool CanAttackServerside(Unit const*, bool, bool) const;
};
struct Player : Unit
{
    Player() { flags = UNIT_FLAG_PLAYER_CONTROLLED; controller = this; }
    uint32 GetTypeId() const override { return TYPEID_PLAYER; }
    bool IsInDuelWith(Player const*) const { return false; }
    bool IsPvP() const { return false; }
    bool IsPvPFreeForAll() const { return false; }
    uint32 GetUInt32Value(uint32) const { return 0; }
    uint32 GetGuidValue(uint32) const { return 0; }
    Reputation const& GetReputationMgr() const { static Reputation rep; return rep; }
};
struct CreatureInfo { bool HasFlag(CreatureTypeFlags) const { return false; } };
struct Creature : Unit
{
    CreatureInfo const* GetCreatureInfo() const { static CreatureInfo info; return &info; }
};
#include "relations_under_test.h"

enum { SPELL_TARGETS_CHAIN_ATTACKABLE, SPELL_TARGETS_AOE_ATTACKABLE, SPELL_TARGETS_ASSISTABLE,
    SPELL_TARGETS_ALL, PUSH_CONE, PUSH_SELF_CENTER, PUSH_SRC_CENTER, PUSH_DEST_CENTER,
    PUSH_TARGET_CENTER, DIST_CALC_NONE };
template<class T> struct GridRefManager
{
    struct Reference { T* unit; T* getSource() const { return unit; } };
    std::vector<Reference> units;
    using iterator = typename std::vector<Reference>::iterator;
    iterator begin() { return units.begin(); }
    iterator end() { return units.end(); }
};
struct TestAoeVisitor
{
    struct TestSpell { SpellEntry const* m_spellInfo; } i_spell;
    Unit* i_originalCaster;
    Unit* i_castingObject;
    int i_TargetType = SPELL_TARGETS_AOE_ATTACKABLE, i_push_type = PUSH_SRC_CENTER;
    float i_radius = 5, i_cone = 0, i_centerX = 0, i_centerY = 0, i_centerZ = 0;
    std::vector<Unit*> i_data;
#include "aoe_visitor_under_test.h"
};

int main()
{
    unsigned checks = 0;
    auto Check = [&](bool ok, char const* text)
    {
        if (!ok) throw std::runtime_error(text);
        ++checks;
    };
    try
    {
        SpellEntry explosion;
        Creature bomb, kernobee, hostile, pet;
        Player alliance, horde;
        bomb.flags = kernobee.flags = 0x8300;
        kernobee.friendly = true; hostile.enemy = true;
        pet.flags = UNIT_FLAG_PLAYER_CONTROLLED; pet.controller = &alliance;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "baseline caster immunity must block player AoE");
        bomb.flags &= ~UNIT_FLAG_IMMUNE_TO_PLAYER;
        Check(bomb.CanAttackSpell(&alliance, &explosion, true), "neutral bomb must hit Alliance");
        Check(bomb.CanAttackSpell(&horde, &explosion, true), "neutral bomb must hit Horde");
        Check(bomb.CanAttackSpell(&pet, &explosion, true), "normal player-controlled pet target rejected");
        Check(!bomb.CanAttackSpell(&kernobee, &explosion, true), "friendly immune Kernobee must use later fallback");
        Check(!bomb.CanAttackSpell(&hostile, &explosion, true), "NPC caster immunity must remain in force");
        alliance.alive = false;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "dead player must be excluded");
        alliance.alive = true; alliance.flags |= UNIT_FLAG_IMMUNE_TO_NPC;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "target immunity must not be bypassed");
        alliance.flags = UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_UNINTERACTIBLE;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "unselectable target must be excluded");
        alliance.flags = UNIT_FLAG_PLAYER_CONTROLLED | UNIT_FLAG_TAXI_FLIGHT;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "taxi target must be excluded");
        bomb.flags |= UNIT_FLAG_IMMUNE_TO_PLAYER; alliance.flags = UNIT_FLAG_PLAYER_CONTROLLED;
        Check(!bomb.CanAttackSpell(&alliance, &explosion, true), "restoring caster immunity must restore protection");

        bomb.flags &= ~UNIT_FLAG_IMMUNE_TO_PLAYER;
        Player inside, boundary, outside, verticalOutside, taxi, otherMap, aoeProtected;
        inside.x = 4.999f; boundary.x = 5.0f; outside.x = 5.001f;
        verticalOutside.z = 5.001f; taxi.taxi = true; otherMap.inMap = false; aoeProtected.aoeImmune = true;
        GridRefManager<Unit> grid;
        grid.units = {{&inside},{&boundary},{&outside},{&verticalOutside},{&taxi},{&otherMap},{&aoeProtected},
            {&kernobee},{&hostile},{&pet}};
        TestAoeVisitor visitor{{&explosion}, &bomb, &bomb};
        visitor.Visit(grid);
        auto Selected = [&](Unit* unit) { return std::find(visitor.i_data.begin(),visitor.i_data.end(),unit) != visitor.i_data.end(); };
        Check(Selected(&inside) && Selected(&boundary), "5-yard inner and exact-boundary players were not selected");
        Check(!Selected(&outside) && !Selected(&verticalOutside), "normal AoE expanded beyond five yards in 3D");
        Check(!Selected(&taxi) && !Selected(&otherMap) && !Selected(&aoeProtected), "normal grid exclusion was bypassed");
        Check(!Selected(&kernobee) && !Selected(&hostile) && Selected(&pet), "normal NPC and player-controlled pet eligibility changed");
    }
    catch (std::exception const& error)
    {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
    std::cout << checks << " actual engine eligibility checks passed; grid, LOS and damage execution need live acceptance.\n";
}
