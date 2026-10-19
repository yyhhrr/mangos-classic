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
SDName: Stranglethorn_Vale
SD%Complete: 100
SDComment: Quest support: 215, 592
SDCategory: Stranglethorn Vale
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/TemporarySpawn.h"
#include "Entities/GossipDef.h"
#include "Server/WorldSession.h"

/* ContentData
npc_private_thorsen
mob_yenniku
mob_colonel_kurzen
EndContentData */

/*######
## mob_yenniku
######*/

enum
{
    SPELL_YENNIKUS_RELEASE      = 3607,

    QUEST_ID_SAVING_YENNIKU     = 592,

    FACTION_ID_HORDE_GENERIC    = 83,                       // Note: faction may not be correct!
};

struct mob_yennikuAI : public ScriptedAI
{
    mob_yennikuAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiResetTimer;

    void Reset() override { m_uiResetTimer = 0; }

    void SpellHit(Unit* pCaster, const SpellEntry* pSpell) override
    {
        if (pSpell->Id == SPELL_YENNIKUS_RELEASE && pCaster->GetTypeId() == TYPEID_PLAYER)
        {
            if (!m_uiResetTimer && ((Player*)pCaster)->GetQuestStatus(QUEST_ID_SAVING_YENNIKU) == QUEST_STATUS_INCOMPLETE)
            {
                m_uiResetTimer = 60000;
                EnterEvadeMode();
            }
        }
    }

    void EnterEvadeMode() override
    {
        if (m_uiResetTimer)
        {
            m_creature->RemoveAllAurasOnEvade();
            m_creature->CombatStop(true);
            m_creature->LoadCreatureAddon(true);

            m_creature->SetLootRecipient(nullptr);

            m_creature->HandleEmote(EMOTE_STATE_STUN);
            m_creature->SetFactionTemporary(FACTION_ID_HORDE_GENERIC, TEMPFACTION_RESTORE_REACH_HOME);
        }
        else
            ScriptedAI::EnterEvadeMode();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (m_uiResetTimer)
        {
            if (m_uiResetTimer <= uiDiff)
            {
                m_creature->HandleEmote(EMOTE_STATE_NONE);
                m_uiResetTimer = 0;
                EnterEvadeMode();
            }
            else
                m_uiResetTimer -= uiDiff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_mob_yenniku(Creature* _Creature)
{
    return new mob_yennikuAI(_Creature);
}

enum
{
    SPELL_SMOKE_BOMB = 8817,
    SPELL_GARROTE = 8818,
    SPELL_STEALTH = 8822 // spell that get applied to npc after using smoke bomb
};

enum KurzenActions
{
    KURZEN_ACTION_VANISH,
    KURZEN_ACTION_GAROTTE,
    KURZEN_ACTION_MAX,
};

struct mob_colonel_kurzenAI : public CombatAI
{
    mob_colonel_kurzenAI(Creature* creature) : CombatAI(creature, KURZEN_ACTION_MAX)
    {
        AddCombatAction(KURZEN_ACTION_VANISH, urand(6000, 12000));
        AddCombatAction(KURZEN_ACTION_GAROTTE, true);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case KURZEN_ACTION_VANISH:
            {
                m_creature->SetCombatOnlyStealth(true); // must be before smoke bomb
                DoCastSpellIfCan(nullptr, SPELL_SMOKE_BOMB);
                ResetCombatAction(KURZEN_ACTION_GAROTTE, urand(2000, 6000));
                DisableCombatAction(action);
                break;
            }
            case KURZEN_ACTION_GAROTTE:
            {
                // Safety check if we still have Stealth Aura
                if (m_creature->HasAura(SPELL_STEALTH))
                    DoCastSpellIfCan(m_creature->GetVictim(), SPELL_GARROTE);

                m_creature->SetCombatOnlyStealth(false);
                ResetCombatAction(KURZEN_ACTION_VANISH, urand(6000, 12000));
                DisableCombatAction(action);
                break;
            }
        }
    }
};

/*######
## npc_private_thorsen
######*/

enum ThorsenRescueConstants
{
    QUEST_JUNGLE_SECRETS       = 215,
    NPC_THORSEN_KURZEN_AGENT   = 775,
    SAY_THORSEN_REJOIN         = 23,
    SAY_THORSEN_RESCUED        = 100,
    SAY_THORSEN_RETURN         = 105,
    THORSEN_RESCUE_FACTION     = 42,
    THORSEN_AGENT_COUNT        = 2,
    THORSEN_RESCUE_TIMEOUT     = 10 * MINUTE * IN_MILLISECONDS,
    THORSEN_QUEST_WINDOW       = 3 * MINUTE * IN_MILLISECONDS,
    THORSEN_AGENT_LIFETIME     = 5 * MINUTE * IN_MILLISECONDS,
};

struct npc_private_thorsenAI : public CreatureEventAI
{
    explicit npc_private_thorsenAI(Creature* creature) : CreatureEventAI(creature) { Reset(); }

    ObjectGuid m_agentGuids[THORSEN_AGENT_COUNT];
    bool m_agentDead[THORSEN_AGENT_COUNT] = { false, false };
    uint32 m_summoningSlot = THORSEN_AGENT_COUNT;
    bool m_rescueActive = false;
    bool m_summonsComplete = false;
    bool m_rescueSucceeded = false;
    uint32 m_rescueTimer = 0;
    uint32 m_questTimer = 0;
    uint32 m_returnTextTimer = 0;
    bool m_lastCanOfferQuest = false;

    // Evade resets combat AI too: do not discard this patrol's rescue roster.
    void Reset() override { CreatureEventAI::Reset(); }

    bool CanOfferQuest(uint32 questId) const override
    {
        if (questId != QUEST_JUNGLE_SECRETS)
            return true;

        return m_rescueActive && m_rescueSucceeded && m_questTimer &&
            m_creature->IsAlive() && !m_creature->IsInCombat();
    }

    void RefreshQuestAvailability()
    {
        bool canOffer = CanOfferQuest(QUEST_JUNGLE_SECRETS);
        if (canOffer == m_lastCanOfferQuest)
            return;

        m_lastCanOfferQuest = canOffer;
        // The NPC keeps its questgiver flag for quest 198. Update the marker
        // explicitly because the availability of only quest 215 has changed.
        for (ObjectGuid guid : m_creature->GetClientGuidsIAmAt())
        {
            if (Player* player = m_creature->GetMap()->GetPlayer(guid))
            {
                uint32 status = DIALOG_STATUS_NONE;
                if (player->CanInteract(m_creature))
                    status = player->GetSession()->getDialogStatus(player, m_creature, DIALOG_STATUS_NONE);
                player->GetPlayerMenu()->SendQuestGiverStatus(uint8(status), m_creature->GetObjectGuid());
            }
        }
    }

    void CloseRescue()
    {
        ObjectGuid agentGuids[THORSEN_AGENT_COUNT];
        bool agentDead[THORSEN_AGENT_COUNT];
        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            agentGuids[i] = m_agentGuids[i];
            agentDead[i] = m_agentDead[i];
            m_agentGuids[i].Clear();
            m_agentDead[i] = false;
        }

        // Clear state before despawning: cleanup can synchronously call back.
        m_rescueActive = false;
        m_summonsComplete = false;
        m_rescueSucceeded = false;
        m_summoningSlot = THORSEN_AGENT_COUNT;
        m_rescueTimer = 0;
        m_questTimer = 0;
        m_returnTextTimer = 0;

        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            if (!agentGuids[i] || agentDead[i])
                continue;
            if (Creature* agent = m_creature->GetMap()->GetCreature(agentGuids[i]))
                if (agent->IsAlive())
                    agent->ForcedDespawn();
        }

        // Preserve dead agents and their loot; only unfinished attackers leave.
        m_creature->ClearTemporaryFaction();
        RefreshQuestAvailability();
    }

    void BeginRescue()
    {
        // Like VMaNGOS, success belongs to the two agents of this encounter,
        // rather than to an unconditional waypoint or a nearby creature count.
        CloseRescue();
        if (!m_creature->IsAlive())
            return;

        m_rescueActive = true;
        m_rescueTimer = THORSEN_RESCUE_TIMEOUT;
        m_creature->SetFactionTemporary(THORSEN_RESCUE_FACTION, TEMPFACTION_NONE);
        float const spawnPositions[THORSEN_AGENT_COUNT][4] =
        {
            { -11511.1f, -445.023f, 41.4481f, 1.03381f },
            { -11508.2f, -442.857f, 41.6781f, 2.53693f },
        };

        for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
        {
            m_summoningSlot = i;
            // Keep the existing spawn locations, five minute OOC lifetime and
            // paths 1/2, including the agents' dialogue and attack scripts.
            TempSpawnSettings settings(m_creature, NPC_THORSEN_KURZEN_AGENT,
                spawnPositions[i][0], spawnPositions[i][1], spawnPositions[i][2], spawnPositions[i][3],
                TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, THORSEN_AGENT_LIFETIME, false, false, i + 1);
            Creature* agent = WorldObject::SummonCreature(settings, m_creature->GetMap());
            m_summoningSlot = THORSEN_AGENT_COUNT;

            bool registered = agent && m_rescueActive &&
                agent->GetEntry() == NPC_THORSEN_KURZEN_AGENT && agent->IsTemporarySummon() &&
                agent->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
                m_agentGuids[i] == agent->GetObjectGuid() &&
                (!i || m_agentGuids[0] != m_agentGuids[i]) &&
                (agent->IsAlive() || m_agentDead[i]);
            if (!registered)
            {
                CloseRescue();
                // A rejected returned summon may not have entered the roster.
                if (agent && agent->GetSpawnerGuid() == m_creature->GetObjectGuid() && agent->IsAlive())
                    agent->ForcedDespawn();
                return;
            }
        }

        // A death during a summon callback must not complete a partial roster.
        m_summonsComplete = true;
        DoBroadcastText(SAY_THORSEN_REJOIN, m_creature);
    }

    void JustRespawned() override
    {
        CloseRescue();
        CreatureEventAI::JustRespawned();
    }

    void JustDied(Unit* killer) override
    {
        CloseRescue();
        CreatureEventAI::JustDied(killer);
    }

    void EnterCombat(Unit* enemy) override
    {
        CreatureEventAI::EnterCombat(enemy);
        RefreshQuestAvailability();
    }

    void MovementInform(uint32 movementType, uint32 pointId) override
    {
        if (movementType != WAYPOINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case 23:
                BeginRescue();
                break;
            case 25:
                CloseRescue();
                if (m_creature->IsAlive())
                    m_returnTextTimer = 1000;
                break;
            case 1:
            case 47:
                CloseRescue();
                break;
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        // Register before invoking EventAI: its summon actions can call back.
        if (m_rescueActive && !m_summonsComplete && m_summoningSlot < THORSEN_AGENT_COUNT &&
            summoned->GetEntry() == NPC_THORSEN_KURZEN_AGENT && summoned->IsTemporarySummon() &&
            summoned->GetSpawnerGuid() == m_creature->GetObjectGuid() && !m_agentGuids[m_summoningSlot])
        {
            uint32 otherSlot = 1 - m_summoningSlot;
            if (m_agentGuids[otherSlot] != summoned->GetObjectGuid())
                m_agentGuids[m_summoningSlot] = summoned->GetObjectGuid();
        }
        CreatureEventAI::JustSummoned(summoned);
    }

    void SummonedCreatureJustDied(Creature* summoned) override
    {
        if (m_rescueActive && summoned->GetEntry() == NPC_THORSEN_KURZEN_AGENT && !summoned->IsAlive())
            for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
                if (m_agentGuids[i] == summoned->GetObjectGuid())
                    m_agentDead[i] = true;
        CreatureEventAI::SummonedCreatureJustDied(summoned);
    }

    void SummonedCreatureDespawn(Creature* summoned) override
    {
        if (m_rescueActive)
            for (uint32 i = 0; i < THORSEN_AGENT_COUNT; ++i)
                if (m_agentGuids[i] == summoned->GetObjectGuid() && !m_agentDead[i])
                {
                    CloseRescue();
                    break;
                }
        CreatureEventAI::SummonedCreatureDespawn(summoned);
    }

    void UpdateAI(uint32 diff) override
    {
        // SelectHostileTarget in the base may finish combat and trigger evade.
        CreatureEventAI::UpdateAI(diff);

        if (!m_creature->IsAlive())
        {
            if (m_rescueActive || m_returnTextTimer)
                CloseRescue();
            return;
        }

        if (m_returnTextTimer)
        {
            if (m_returnTextTimer <= diff)
            {
                m_returnTextTimer = 0;
                DoBroadcastText(SAY_THORSEN_RETURN, m_creature);
            }
            else
                m_returnTextTimer -= diff;
        }

        if (m_rescueActive)
        {
            if (m_rescueSucceeded)
            {
                if (m_questTimer <= diff)
                    CloseRescue();
                else
                    m_questTimer -= diff;
            }
            else if (m_rescueTimer <= diff)
                CloseRescue();
            else
            {
                m_rescueTimer -= diff;
                if (m_summonsComplete && m_agentDead[0] && m_agentDead[1] && !m_creature->IsInCombat())
                {
                    m_rescueSucceeded = true;
                    m_questTimer = THORSEN_QUEST_WINDOW;
                    m_creature->ClearTemporaryFaction();
                    DoBroadcastText(SAY_THORSEN_RESCUED, m_creature);
                }
            }
        }

        // Re-entering combat temporarily hides 215; the same window continues
        // to run, and the task is offered again only after combat has ended.
        RefreshQuestAvailability();
    }
};

/*######
##
######*/

void AddSC_stranglethorn_vale()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_private_thorsen";
    pNewScript->GetAI = &GetNewAIInstance<npc_private_thorsenAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_yenniku";
    pNewScript->GetAI = &GetAI_mob_yenniku;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "mob_colonel_kurzen";
    pNewScript->GetAI = &GetNewAIInstance<mob_colonel_kurzenAI>;
    pNewScript->RegisterSelf();
}
