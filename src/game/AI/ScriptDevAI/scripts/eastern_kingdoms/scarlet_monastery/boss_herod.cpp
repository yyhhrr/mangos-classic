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
SDName: Boss_Herod
SD%Complete: 100
SDComment:
SDCategory: Scarlet Monastery
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/escort_ai.h"
#include "AI/ScriptDevAI/include/sc_grid_searchers.h"
#include "Spells/Spell.h"
#include <algorithm>

enum
{
    SAY_AGGRO              = -1189000,
    SAY_WHIRLWIND          = -1189001,
    SAY_ENRAGE             = -1189002,
    SAY_KILL               = -1189003,
    EMOTE_GENERIC_ENRAGED  = -1000003,

    SAY_TRAINEE_SPAWN      = -1189035,

    SPELL_RUSHINGCHARGE    = 8260,
    SPELL_CLEAVE           = 15496,
    SPELL_WHIRLWIND        = 8989,
    SPELL_FRENZY           = 8269,

    NPC_SCARLET_MYRMIDON   = 4295,
    NPC_SCARLET_TRAINEE    = 6575,
    GO_HEROD_DOOR         = 101854
};

// VMaNGOS reference positions: hallway guard post and the arena centre.
static const float aMyrmidonSpawn[4] = {1926.03f, -370.61f, 18.0f, 0.05f};
static const float aHerodRoomCenter[2] = {1965.09f, -431.61f};

struct boss_herodAI : public ScriptedAI
{
    boss_herodAI(Creature* pCreature) : ScriptedAI(pCreature) {Reset();}

    bool m_bEnrage = false;
    bool m_bTraineeSay = false;
    bool m_bDeathHandled = false;
    bool m_bMyrmidonsSpawned = false;
    bool m_bWhirlwind = false;
    bool m_bWhirlwindRootOwned = false;
    bool m_restoreMovement = true;
    bool m_restoreMelee = true;
    Spell const* m_whirlwindSpell = nullptr;
    GuidVector m_myrmidonGuids;
    uint32 m_uiCleaveTimer;
    uint32 m_uiWhirlwindTimer;
    uint32 m_uiChargeTimer;
    uint32 m_uiRoomCheckTimer;

    void StopWhirlwind(bool resumeCombat)
    {
        if (!m_bWhirlwind)
            return;
        m_bWhirlwind = false;
        m_whirlwindSpell = nullptr;
        if (m_bWhirlwindRootOwned)
            m_creature->clearUnitState(UNIT_STAT_AI_ROOT);
        m_bWhirlwindRootOwned = false;
        SetMeleeEnabled(m_restoreMelee && m_creature->IsAlive());
        SetCombatMovement(m_restoreMovement, resumeCombat && m_creature->IsAlive());
    }

    void DespawnMyrmidons(bool onlyIdle = false)
    {
        GuidVector guards;
        guards.swap(m_myrmidonGuids); // Despawn callbacks cannot invalidate iteration.
        m_bMyrmidonsSpawned = false;
        for (ObjectGuid guid : guards)
            if (Creature* guard = m_creature->GetMap()->GetCreature(guid))
                if (guard->IsAlive())
                {
                    if (onlyIdle && guard->GetVictim())
                        m_myrmidonGuids.push_back(guid);
                    else
                        guard->ForcedDespawn();
                }
    }

    void Reset() override
    {
        StopWhirlwind(false);
        DespawnMyrmidons();
        SetCombatMovement(true);
        SetMeleeEnabled(true);
        m_bTraineeSay = false;
        m_bDeathHandled = false;
        m_bEnrage = false;
        m_uiCleaveTimer = 7500;
        m_uiWhirlwindTimer = 14500;
        m_uiChargeTimer = 1500;
        m_uiRoomCheckTimer = 500;
    }

    void SpawnMyrmidons()
    {
        if (m_bMyrmidonsSpawned)
            return;
        m_bMyrmidonsSpawned = true; // A partial summon failure cannot create repeated waves.
        for (uint8 i = 0; i < 4; ++i)
            m_creature->SummonCreature(NPC_SCARLET_MYRMIDON,
                aMyrmidonSpawn[0] + frand(-3.0f, 3.0f), aMyrmidonSpawn[1] + frand(-3.0f, 3.0f),
                aMyrmidonSpawn[2], aMyrmidonSpawn[3], TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 600000);
    }

    void EngageMyrmidons(Unit* victim)
    {
        if (!victim || !victim->IsAlive())
            return;
        for (ObjectGuid guid : m_myrmidonGuids)
            if (Creature* guard = m_creature->GetMap()->GetCreature(guid))
                if (guard->IsAlive() && !guard->GetVictim() && guard->AI())
                    guard->AI()->AttackStart(victim);
    }

    void Aggro(Unit* /*pWho*/) override
    {
        SpawnMyrmidons();
        DoScriptText(SAY_AGGRO, m_creature);
        DoCastSpellIfCan(m_creature, SPELL_RUSHINGCHARGE);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_SCARLET_MYRMIDON)
        {
            // These anti-kiting guards must not become a repeatable reward source.
            summoned->SetNoRewards();
            m_myrmidonGuids.push_back(summoned->GetObjectGuid());
        }
        else if (summoned->GetEntry() == NPC_SCARLET_TRAINEE && !m_bTraineeSay)
        {
            DoScriptText(SAY_TRAINEE_SPAWN, summoned);
            m_bTraineeSay = true;
        }
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        auto it = std::find(m_myrmidonGuids.begin(), m_myrmidonGuids.end(), summoned->GetObjectGuid());
        if (it != m_myrmidonGuids.end())
            m_myrmidonGuids.erase(it);
    }

    void OnSpellCastStateChange(Spell const* spell, bool state, WorldObject* target = nullptr) override
    {
        ScriptedAI::OnSpellCastStateChange(spell, state, target);
        if (spell->m_spellInfo->Id != SPELL_WHIRLWIND || spell->GetCaster() != m_creature)
            return;
        if (state && !m_bWhirlwind)
        {
            m_bWhirlwind = true;
            m_whirlwindSpell = spell;
            m_restoreMovement = IsCombatMovement();
            m_restoreMelee = IsMeleeEnabled();
            m_bWhirlwindRootOwned = !m_creature->hasUnitState(UNIT_STAT_AI_ROOT);
            m_creature->addUnitState(UNIT_STAT_AI_ROOT);
            SetCombatMovement(false, true);
            SetMeleeEnabled(false);
            m_creature->InterruptMoving();
        }
        else if (!state && spell == m_whirlwindSpell)
            StopWhirlwind(true); // Both natural completion and interruption use Spell::finish.
    }

    void EnterEvadeMode() override
    {
        StopWhirlwind(false);
        DespawnMyrmidons();
        ScriptedAI::EnterEvadeMode();
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        DoScriptText(SAY_KILL, m_creature);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        if (m_bDeathHandled)
            return;
        m_bDeathHandled = true;
        StopWhirlwind(false);
        DespawnMyrmidons(true); // Keep already engaged guards fighting, as in the reference.
        for (uint8 i = 0; i < 20; ++i)
            m_creature->SummonCreature(NPC_SCARLET_TRAINEE, 1939.18f, -431.58f, 17.09f, 6.22f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 600000);
        if (GameObject* door = GetClosestGameObjectWithEntry(m_creature, GO_HEROD_DOOR, 100.0f))
            if (door->GetGoState() != GO_STATE_ACTIVE)
                door->UseDoorOrButton();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_bMyrmidonsSpawned)
        {
            if (m_uiRoomCheckTimer <= uiDiff)
            {
                if (!m_creature->IsWithinDist2d(aHerodRoomCenter[0], aHerodRoomCenter[1], 32.0f))
                    EngageMyrmidons(m_creature->GetVictim());
                m_uiRoomCheckTimer = 500;
            }
            else
                m_uiRoomCheckTimer -= uiDiff;
        }

        // The channel callback restores movement even if no hostile target remains.
        if (m_bWhirlwind)
            return;

        if (!m_bEnrage && m_creature->GetHealthPercent() <= 50.0f && !m_creature->IsNonMeleeSpellCasted(false))
        {
            if (DoCastSpellIfCan(m_creature, SPELL_FRENZY) == CAST_OK)
            {
                DoScriptText(EMOTE_GENERIC_ENRAGED, m_creature);
                DoScriptText(SAY_ENRAGE, m_creature);
                m_bEnrage = true;
            }
        }

        if (m_uiChargeTimer <= uiDiff)
        {
            m_uiChargeTimer = 0;
            if (!m_creature->GetVictim()->IsInRange(m_creature, 0.0f, ATTACK_DISTANCE + 10.0f))
                if (DoCastSpellIfCan(m_creature, SPELL_RUSHINGCHARGE) == CAST_OK)
                    m_uiChargeTimer = 4500;
        }
        else
            m_uiChargeTimer -= uiDiff;

        if (m_uiCleaveTimer <= uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CLEAVE) == CAST_OK)
                m_uiCleaveTimer = urand(7500, 17500);
        }
        else
            m_uiCleaveTimer -= uiDiff;

        if (m_uiWhirlwindTimer <= uiDiff)
        {
            if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_WHIRLWIND) == CAST_OK)
            {
                DoScriptText(SAY_WHIRLWIND, m_creature);
                m_uiWhirlwindTimer = urand(15000, 25000);
                return; // Never perform a melee swing in the update that starts the channel.
            }
        }
        else
            m_uiWhirlwindTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_herod(Creature* pCreature)
{
    return new boss_herodAI(pCreature);
}

struct mob_scarlet_traineeAI : public npc_escortAI
{
    mob_scarlet_traineeAI(Creature* pCreature) : npc_escortAI(pCreature)
    {
        m_uiStartTimer = urand(1000, 6000);
        Reset();
    }

    uint32 m_uiStartTimer;

    void Reset() override { }
    void WaypointReached(uint32 /*uiPointId*/) override {}

    void UpdateEscortAI(const uint32 uiDiff) override
    {
        if (m_uiStartTimer)
        {
            if (m_uiStartTimer <= uiDiff)
            {
                Start(true);
                m_uiStartTimer = 0;
            }
            else
                m_uiStartTimer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_mob_scarlet_trainee(Creature* pCreature)
{
    return new mob_scarlet_traineeAI(pCreature);
}

void AddSC_boss_herod()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_herod";
    pNewScript->GetAI = &GetAI_boss_herod;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_scarlet_trainee";
    pNewScript->GetAI = &GetAI_mob_scarlet_trainee;
    pNewScript->RegisterSelf();
}
