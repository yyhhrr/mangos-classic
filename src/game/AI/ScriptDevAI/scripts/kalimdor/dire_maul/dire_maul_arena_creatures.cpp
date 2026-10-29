/* This file is part of ScriptDevAI, licensed under GPL version 2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "dire_maul_arena.h"

// One native EventAI scheduler still owns all Classic spells, targeting, cast
// flags, timers, kill text and Mushgog's death22948. These wrappers only report
// real lifecycle callbacks to the map1 coordinator. No second combat loop.
struct boss_maul_rareAI : public CreatureEventAI
{
    explicit boss_maul_rareAI(Creature* creature) : CreatureEventAI(creature) {}

    void EnterCombat(Unit* enemy) override
    {
        CreatureEventAI::EnterCombat(enemy);
        if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
            arena->BossEnterCombat(m_creature);
    }

    void KilledUnit(Unit* victim) override
    {
        CreatureEventAI::KilledUnit(victim);
        // The spectator's9505 describes a player killed by The Razza. A dead
        // pet or other NPC must not trigger that commentary for its owner.
        if (victim && victim->IsPlayer())
            if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
                arena->BossKilledPlayer(m_creature, static_cast<Player*>(victim));
    }

    void EnterEvadeMode() override
    {
        CreatureEventAI::EnterEvadeMode();
        if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
            arena->BossEvaded(m_creature);
    }

    void JustReachedHome() override
    {
        CreatureEventAI::JustReachedHome();
        if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
            arena->BossEvaded(m_creature);
    }

    void JustDied(Unit* killer) override
    {
        // Choose and save the real next spawn deadline while the native death
        // path can still consume the one-time respawn-delay override. The
        // coordinator also receives world-map death notifications and makes
        // this operation idempotent for the current generation/actor GUID.
        if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
            arena->BossDied(m_creature);
        CreatureEventAI::JustDied(killer);
    }
};

struct npc_maul_griniblixAI : public ScriptedAI
{
    explicit npc_maul_griniblixAI(Creature* creature) : ScriptedAI(creature) {}

    void UpdateAI(uint32 diff) override
    {
        if (DireMaulArena* arena = GetDireMaulArena(m_creature->GetMap()))
            arena->SpectatorUpdate(m_creature, diff);
    }
};

namespace
{
UnitAI* GetAI_boss_maul_rare(Creature* creature) { return new boss_maul_rareAI(creature); }
UnitAI* GetAI_npc_maul_griniblix(Creature* creature) { return new npc_maul_griniblixAI(creature); }
}

void AddSC_dire_maul_arena_creatures()
{
    for (char const* name : {"boss_maul_mushgog", "boss_maul_razza", "boss_maul_skarr"})
    {
        Script* script = new Script;
        script->Name = name;
        script->GetAI = &GetAI_boss_maul_rare;
        script->RegisterSelf();
    }

    Script* script = new Script;
    script->Name = "npc_maul_griniblix";
    script->GetAI = &GetAI_npc_maul_griniblix;
    // No custom gossip hook: template11738 retains its native menu/text path.
    script->RegisterSelf();
}
