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
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"
#include "Spells/Spell.h"

/* ContentData
npc_kurzen_medicine_man
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
## npc_kurzen_medicine_man
######*/

enum
{
    SPELL_KURZEN_RENEW       = 6077,
    SPELL_KURZEN_HEAL        = 6064,
    KURZEN_HEAL_RETRY_MS     = 500,
};

// Preserve EventAI's Inner Fire, low-health flee and normal melee behavior.
// Only the two friendly-healing actions move here, so the initial check is
// independent of the cooldown that starts after a successful cast.
struct npc_kurzen_medicine_manAI : public CreatureEventAI
{
    explicit npc_kurzen_medicine_manAI(Creature* creature) : CreatureEventAI(creature) { }

    uint32 m_renewTimer = 0;
    uint32 m_healTimer = 0;

    void ResetHealingTimers()
    {
        m_renewTimer = 0;
        m_healTimer = 0;
    }

    void Reset() override
    {
        CreatureEventAI::Reset();
        ResetHealingTimers();
    }

    void JustRespawned() override
    {
        CreatureEventAI::JustRespawned();
        ResetHealingTimers();
    }

    void EnterEvadeMode() override
    {
        ResetHealingTimers();
        CreatureEventAI::EnterEvadeMode();
    }

    bool HasRenewAtLeastAsStrong(Unit const* target) const
    {
        // Inspect every caster's holders, rather than HasAura(spell, caster).
        // Compare actual chain ancestry, not numeric spell IDs or rank values;
        // unrelated heal-over-time spells must not suppress Renew.
        for (auto const& pair : target->GetSpellAuraHolderMap())
        {
            SpellAuraHolder const* holder = pair.second;
            if (!holder || holder->IsDeleted())
                continue;

            uint32 auraId = holder->GetId();
            if (auraId == SPELL_KURZEN_RENEW ||
                sSpellMgr.IsSpellHigherRankOfSpell(auraId, SPELL_KURZEN_RENEW))
                return true;
        }
        return false;
    }

    bool CanReceiveHealingSpell(Unit* target, uint32 spellId) const
    {
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        if (!spellInfo || !Spell::CheckTargetCreatureType(target, spellInfo))
            return false;

        if (spellInfo->HasAttribute(SPELL_ATTR_NO_IMMUNITIES))
            return true;

        bool self = target == m_creature;
        if (target->IsImmuneToSpell(spellInfo, self, GetCheckCastEffectMask(spellInfo), m_creature))
            return false;

        // Some friendly spells can start successfully and only report immune
        // at impact. Do not spend the cooldown on an entirely immune patient.
        for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
        {
            if (spellInfo->Effect[i] != SPELL_EFFECT_HEAL &&
                !(spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA &&
                    spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_HEAL))
                continue;

            if (!target->IsImmuneToSpellEffect(spellInfo, SpellEffectIndex(i), self))
                return true;
        }
        return false;
    }

    bool IsEligiblePatient(Unit* target, uint32 spellId, float missingPercent) const
    {
        if (!target || !target->IsAlive() || !target->IsInCombat() ||
            !target->GetMaxHealth() || !m_creature->CanAssist(target) ||
            !m_creature->IsWithinDistInMap(target, 30.0f))
            return false;

        // Strictly greater than the deficit threshold, matching the VMaNGOS
        // rule. Percentage deficits also determine the selection order.
        if (100.0f - target->GetHealthPercent() <= missingPercent)
            return false;

        if (!CanReceiveHealingSpell(target, spellId))
            return false;

        // Include spell visibility (including invisibility/hidden targets)
        // and line of sight before sorting, so an unreachable most-injured
        // ally cannot hide another eligible patient.
        if (target != m_creature &&
            (!target->IsVisibleForOrDetect(m_creature, m_creature, true, false, true, true) ||
                !m_creature->IsWithinLOSInMap(target)))
            return false;

        return spellId != SPELL_KURZEN_RENEW || !HasRenewAtLeastAsStrong(target);
    }

    bool TryHealingSpell(uint32 spellId, float missingPercent)
    {
        struct PatientCheck
        {
            npc_kurzen_medicine_manAI const& ai;
            uint32 spell;
            float deficit;

            WorldObject const& GetFocusObject() const { return *ai.m_creature; }
            bool operator()(Unit* target) const { return ai.IsEligiblePatient(target, spell, deficit); }
        } check{ *this, spellId, missingPercent };

        UnitList patients;
        MaNGOS::UnitListSearcher<PatientCheck> searcher(patients, check);
        Cell::VisitGridObjects(m_creature, searcher, 30.0f);

        // Explicitly include self, independently of the grid visitor's handling
        // of its focus creature, without allowing a duplicate cast attempt.
        patients.remove(m_creature);
        if (IsEligiblePatient(m_creature, spellId, missingPercent))
            patients.push_back(m_creature);

        patients.sort([](Unit const* left, Unit const* right)
        {
            return left->GetHealthPercent() < right->GetHealthPercent();
        });

        for (Unit* target : patients)
        {
            // Recheck immediately before casting: another healer or an aura
            // application must not cause us to refresh an equal/stronger Renew.
            if (!IsEligiblePatient(target, spellId, missingPercent))
                continue;

            uint32 castFlags = spellId == SPELL_KURZEN_RENEW ? CAST_AURA_NOT_PRESENT : 0;
            if (DoCastSpellIfCan(target, spellId, castFlags) == CAST_OK)
                return true;

            // Normal spell validation checks mana, silence, GCD, immunities,
            // target rules, range and LOS. A target-specific rejection should
            // not prevent us from trying another injured ally this check.
        }
        return false;
    }

    static void AdvanceTimer(uint32& timer, uint32 diff)
    {
        timer = diff >= timer ? 0 : timer - diff;
    }

    void UpdateAI(uint32 diff) override
    {
        // EventAI processes the existing 15% flee event first. Its normal
        // casting and movement restrictions continue to apply to healing.
        CreatureEventAI::UpdateAI(diff);

        if (!m_creature->IsAlive() || !m_creature->IsInCombat())
        {
            ResetHealingTimers();
            return;
        }

        AdvanceTimer(m_renewTimer, diff);
        AdvanceTimer(m_healTimer, diff);

        if (GetAIOrder() == ORDER_FLEEING || !CanExecuteCombatAction() || !CanCastSpell())
            return;

        // Prefer the direct heal when an ally has lost over half its health.
        // Consume a long cooldown only once normal casting actually succeeds;
        // interruptions after a successful start retain the usual cooldown.
        if (!m_healTimer)
        {
            if (TryHealingSpell(SPELL_KURZEN_HEAL, 50.0f))
            {
                m_healTimer = urand(30000, 40000);
                return;
            }
            m_healTimer = KURZEN_HEAL_RETRY_MS;
        }

        if (!m_renewTimer)
        {
            if (TryHealingSpell(SPELL_KURZEN_RENEW, 20.0f))
                m_renewTimer = urand(20000, 30000);
            else
                m_renewTimer = KURZEN_HEAL_RETRY_MS;
        }
    }
};

/*######
##
######*/

void AddSC_stranglethorn_vale()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_kurzen_medicine_man";
    pNewScript->GetAI = &GetNewAIInstance<npc_kurzen_medicine_manAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
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
