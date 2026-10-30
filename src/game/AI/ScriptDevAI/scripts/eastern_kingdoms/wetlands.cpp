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
SDName: Wetlands
SD%Complete: 100
SDComment: Quest support: 1249
SDCategory: Wetlands
EndScriptData

*/

/* ContentData
npc_mikhail
npc_tapoke_slim_jahn
EndContentData */


#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/escort_ai.h"
#include "Entities/Pet.h"

/*######
## npc_tapoke_slim_jahn
######*/

enum
{
    SAY_SLIM_AGGRO              = -1000977,
    SAY_SLIM_DEFEAT             = -1000978,
    SAY_FRIEND_DEFEAT           = -1000979,
    SAY_SLIM_NOTES              = -1000980,

    QUEST_MISSING_DIPLOMAT11    = 1249,
    FACTION_ENEMY               = 168,                      // ToDo: faction needs to be confirmed!

    SPELL_STEALTH               = 1785,
    SPELL_CALL_FRIENDS          = 16457,                    // summon npc 4971
    SPELL_PUMMEL               = 12555,

    NPC_MIKHAIL                = 4963,
    NPC_SLIMS_FRIEND            = 4971,
    NPC_TAPOKE_SLIM_JAHN        = 4962
};

static const DialogueEntry aDiplomatDialogue[] =
{
    {SAY_SLIM_DEFEAT,           NPC_TAPOKE_SLIM_JAHN,   4000},
    {SAY_SLIM_NOTES,            NPC_TAPOKE_SLIM_JAHN,   7000},
    {QUEST_MISSING_DIPLOMAT11,  0,                      0},
    {0, 0, 0},
};

struct npc_tapoke_slim_jahnAI : public npc_escortAI, private DialogueHelper
{
    enum Phase
    {
        IDLE,
        ESCAPING,
        FIGHTING,
        DEFEATED,
        COMPLETE,
        ABORTED
    };

    explicit npc_tapoke_slim_jahnAI(Creature* creature) : npc_escortAI(creature), DialogueHelper(aDiplomatDialogue),
        m_originalDeathPrevention(creature->IsPreventingDeath()) { Reset(); }

    Phase m_phase = IDLE;
    GuidSet m_friends;
    bool m_acceptFriend = false;
    bool m_friendConfirmed = false;
    bool m_dialogueStarted = false;
    bool m_originalDeathPrevention;
    uint32 m_friendRetry = 0;
    uint32 m_friendSpawnFailures = 0;
    uint32 m_friendCastFailures = 0;
    uint32 m_pummelTimer = 0;

    bool OwnedFriend(Creature const* actor) const
    {
        return actor && actor->GetEntry() == NPC_SLIMS_FRIEND && actor->IsPet() &&
            static_cast<Pet const*>(actor)->IsGuardian() && actor->GetOwnerGuid() == m_creature->GetObjectGuid();
    }

    void CleanupFriends(uint32 delay = 0, bool sayDefeat = false)
    {
        m_acceptFriend = false;
        GuidSet actors;
        actors.swap(m_friends);
        // Also find pre-existing owned guardians when starting or respawning.
        m_creature->CallForAllControlledUnits([&](Unit* unit)
        {
            if (unit->GetTypeId() == TYPEID_UNIT && OwnedFriend(static_cast<Creature*>(unit)))
                actors.insert(unit->GetObjectGuid());
        }, CONTROLLED_GUARDIANS);
        for (ObjectGuid guid : actors)
        {
            Pet* actor = m_creature->GetMap()->GetPet(guid);
            if (!OwnedFriend(actor) || !actor->IsAlive())
                continue; // Preserve corpses and do not touch another summoner's pet.
            if (sayDefeat)
                DoScriptText(SAY_FRIEND_DEFEAT, actor);
            if (delay)
                actor->ForcedDespawn(delay);
            else
                actor->Unsummon(PET_SAVE_AS_DELETED, m_creature);
        }
    }

    bool CanStart() const
    {
        return m_phase == IDLE && m_creature->IsAlive() && !m_creature->IsInCombat() &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER) && !HasEscortState(STATE_ESCORT_ESCORTING);
    }

    bool TryStart(Player* player, Quest const* quest)
    {
        if (!player || !quest || quest->GetQuestId() != QUEST_MISSING_DIPLOMAT11 || !player->IsAlive() ||
            player->GetMap() != m_creature->GetMap() || player->GetQuestStatus(QUEST_MISSING_DIPLOMAT11) != QUEST_STATUS_INCOMPLETE)
            return false;
        // Repeated callbacks from the admitted player must not fail his own event.
        if (HasEscortState(STATE_ESCORT_ESCORTING) && GetPlayerForEscort() == player && m_questForEscort == quest)
            return true;
        if (!CanStart())
            return false;
        Start(false, player, quest, true);
        if (!HasEscortState(STATE_ESCORT_ESCORTING) || GetPlayerForEscort() != player || m_questForEscort != quest)
            return false;
        if (!m_creature->HasAura(SPELL_STEALTH))
            m_creature->CastSpell(m_creature, SPELL_STEALTH, TRIGGERED_OLD_TRIGGERED);
        return true;
    }

    void JustStartedEscort() override
    {
        CleanupFriends();
        DisableDialogue();
        m_phase = ESCAPING;
        m_friendConfirmed = m_dialogueStarted = false;
        m_friendRetry = m_friendSpawnFailures = m_friendCastFailures = 0;
        m_pummelTimer = urand(1400, 2700);
    }

    void Reset() override
    {
        if (m_phase == FIGHTING)
        {
            SetDeathPrevention(true);
            // CombatStop restores the temporary faction. An ordinary evade must
            // leave an unfinished chase attackable without replaying waypoint3.
            m_creature->SetFactionTemporary(FACTION_ENEMY, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_RESTORE_COMBAT_STOP);
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            SetReactState(REACT_AGGRESSIVE);
        }
        else
        {
            SetDeathPrevention(m_originalDeathPrevention);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
            SetReactState(REACT_PASSIVE);
        }
    }

    void JustRespawned() override
    {
        CleanupFriends();
        DisableDialogue();
        SetEscortPaused(false);
        End();
        m_phase = IDLE;
        m_friendConfirmed = m_dialogueStarted = false;
        m_friendRetry = m_friendSpawnFailures = m_friendCastFailures = m_pummelTimer = 0;
        npc_escortAI::JustRespawned();
    }

    void FailQuestForPlayerAndGroup() override
    {
        SetDeathPrevention(m_originalDeathPrevention);
        CleanupFriends();
        DisableDialogue();
        if (m_phase == COMPLETE || m_phase == ABORTED)
            return;
        m_phase = ABORTED;
        npc_escortAI::FailQuestForPlayerAndGroup();
        SetEscortPaused(false);
        End();
    }

    void JustDied(Unit* /*killer*/) override { FailQuestForPlayerAndGroup(); }

    void AbortAndRespawn()
    {
        FailQuestForPlayerAndGroup();
        m_creature->SetDeathState(JUST_DIED);
        m_creature->Respawn();
    }

    void JustReachedHome() override
    {
        if (m_phase == DEFEATED && !m_dialogueStarted)
        {
            m_dialogueStarted = true;
            if (Player* player = GetPlayerForEscort())
                m_creature->SetFacingToObject(player);
            StartNextDialogueText(SAY_SLIM_DEFEAT);
        }
    }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 3:
                if (m_phase != ESCAPING)
                    break;
                m_phase = FIGHTING;
                SetRun();
                m_creature->RemoveAurasDueToSpell(SPELL_STEALTH);
                Reset();
                break;
            case 7:
                if (m_phase == ESCAPING || m_phase == FIGHTING)
                    AbortAndRespawn();
                break;
        }
    }

    void TrySummonFriend()
    {
        if (m_phase != FIGHTING || m_friendConfirmed || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        m_acceptFriend = true;
        CanCastResult result = DoCastSpellIfCan(m_creature, SPELL_CALL_FRIENDS);
        m_acceptFriend = false;
        if (result != CAST_OK)
        {
            CleanupFriends();
            // Stuns or other ordinary cast failures must not punish the player.
            m_friendRetry = ++m_friendCastFailures >= 3 ? 5000 : 1000;
            if (m_friendCastFailures >= 3)
                m_friendCastFailures = 0;
            return;
        }
        m_friendCastFailures = 0;
        if (m_friends.size() == 1)
            if (Pet* actor = m_creature->GetMap()->GetPet(*m_friends.begin()))
                if (OwnedFriend(actor) && actor->IsAlive())
                {
                    m_friendConfirmed = true;
                    m_friendRetry = m_friendSpawnFailures = 0;
                    DoScriptText(SAY_SLIM_AGGRO, m_creature);
                    return;
                }
        CleanupFriends();
        // A cast result alone is not proof that the guardian was created.
        if (++m_friendSpawnFailures >= 3)
            AbortAndRespawn();
        else
            m_friendRetry = 1000;
    }

    void Aggro(Unit* /*enemy*/) override
    {
        if (m_phase == FIGHTING && !m_friendConfirmed && !m_friendRetry)
            TrySummonFriend();
    }

    void JustSummoned(Creature* actor) override
    {
        if (!OwnedFriend(actor))
            return;
        if (!m_acceptFriend || m_phase != FIGHTING || m_friendConfirmed || !m_friends.empty() || !actor->IsAlive())
        {
            if (actor->IsAlive())
                static_cast<Pet*>(actor)->Unsummon(PET_SAVE_AS_DELETED, m_creature);
            return;
        }
        m_friends.insert(actor->GetObjectGuid());
        if (Player* player = GetPlayerForEscort())
            if (player->IsAlive())
                actor->AI()->AttackStart(player);
    }

    void SummonedCreatureJustDied(Creature* actor) override { m_friends.erase(actor->GetObjectGuid()); }
    void SummonedCreatureDespawn(Creature* actor) override { m_friends.erase(actor->GetObjectGuid()); }

    void Surrender()
    {
        if (m_phase != FIGHTING)
            return;
        m_phase = DEFEATED; // Set before callbacks so damage and home events are idempotent.
        CleanupFriends(1000, true);
        SetEscortPaused(true);
        EnterEvadeMode();
    }

    void DamageTaken(Unit* /*dealer*/, uint32& damage, DamageEffectType /*damagetype*/, SpellEntry const* /*spellInfo*/) override
    {
        if (m_phase == DEFEATED || m_phase == COMPLETE)
        {
            damage = 0;
            return;
        }
        if (m_phase != FIGHTING || !HasEscortState(STATE_ESCORT_ESCORTING) || !damage || !m_creature->IsAlive())
            return;
        uint32 health = m_creature->GetHealth();
        if (damage >= health || uint64(health - damage) * 100 <= uint64(m_creature->GetMaxHealth()) * 20)
        {
            damage = std::min(damage, health - 1);
            Surrender();
        }
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 questId) override
    {
        if (eventType != AI_EVENT_START_ESCORT || questId != QUEST_MISSING_DIPLOMAT11 || !sender || !invoker ||
            sender->GetTypeId() != TYPEID_UNIT || sender->GetEntry() != NPC_MIKHAIL || invoker->GetTypeId() != TYPEID_PLAYER ||
            sender->GetMap() != m_creature->GetMap() || !sender->IsAlive() || sender->IsInCombat() ||
            !sender->IsWithinDistInMap(m_creature, 25.0f))
            return;
        Player* player = static_cast<Player*>(invoker);
        if (!TryStart(player, GetQuestTemplateStore(questId)) && player->GetQuestStatus(questId) == QUEST_STATUS_INCOMPLETE)
            player->FailQuest(questId); // Stale/busy acceptance is explicit and can be abandoned/reaccepted.
    }

    void JustDidDialogueStep(int32 iEntry) override
    {
        if (iEntry == QUEST_MISSING_DIPLOMAT11 && m_phase == DEFEATED && m_dialogueStarted && HasEscortState(STATE_ESCORT_ESCORTING))
        {
            m_phase = COMPLETE;
            if (Player* player = GetPlayerForEscort())
                player->RewardPlayerAndGroupAtEventExplored(QUEST_MISSING_DIPLOMAT11, m_creature);
            QuestEnd();
            m_creature->SetRespawnDelay(2, true);
            m_creature->ForcedDespawn(1000);
        }
    }

    Creature* GetSpeakerByEntry(uint32 uiEntry) override
    {
        if (uiEntry == NPC_TAPOKE_SLIM_JAHN)
            return m_creature;

        return nullptr;
    }

    void UpdateEscortAI(const uint32 uiDiff) override
    {
        // Melee damage may aggregate several damage lines after individual
        // DamageTaken callbacks. Native death prevention keeps that hit safe;
        // finish the same surrender on the next AI update.
        if (m_phase == FIGHTING && m_creature->IsAlive() &&
            uint64(m_creature->GetHealth()) * 100 <= uint64(m_creature->GetMaxHealth()) * 20)
            Surrender();
        if (m_phase == DEFEATED)
            DialogueUpdate(uiDiff);
        if (m_phase != FIGHTING || !m_creature->IsInCombat() || !m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;
        if (!m_friendConfirmed)
        {
            if (m_friendRetry <= uiDiff)
                TrySummonFriend();
            else
                m_friendRetry -= uiDiff;
            if (m_phase != FIGHTING)
                return;
        }
        // This optional interrupt belongs only to the actual quest actor.
        if (m_creature->GetDbGuid() == 10873)
        {
            if (m_pummelTimer <= uiDiff)
            {
                Unit* target = m_creature->GetVictim();
                if (target->IsNonMeleeSpellCasted(false) && m_creature->CanReachWithMeleeAttack(target) &&
                    DoCastSpellIfCan(target, SPELL_PUMMEL) == CAST_OK)
                    m_pummelTimer = urand(7300, 15000);
                else
                    m_pummelTimer = 1000;
            }
            else
                m_pummelTimer -= uiDiff;
        }
        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_tapoke_slim_jahn(Creature* pCreature)
{
    return new npc_tapoke_slim_jahnAI(pCreature);
}

struct npc_mikhailAI : public ScriptedAI
{
    explicit npc_mikhailAI(Creature* creature) : ScriptedAI(creature) {}

    bool CanOfferQuest(uint32 questId) const override
    {
        if (questId != QUEST_MISSING_DIPLOMAT11)
            return true;
        if (!m_creature->IsAlive() || m_creature->IsInCombat())
            return false;
        Creature* slim = GetClosestCreatureWithEntry(m_creature, NPC_TAPOKE_SLIM_JAHN, 25.0f);
        auto ai = slim ? dynamic_cast<npc_tapoke_slim_jahnAI*>(slim->AI()) : nullptr;
        return ai && ai->CanStart();
    }
};

UnitAI* GetAI_npc_mikhail(Creature* creature) { return new npc_mikhailAI(creature); }

/*######
## npc_mikhail
######*/

bool QuestAccept_npc_mikhail(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (!pPlayer || !pCreature || !pQuest)
        return false;
    if (pQuest->GetQuestId() == QUEST_MISSING_DIPLOMAT11)
    {
        if (!pCreature->IsAlive() || pCreature->IsInCombat())
        {
            if (pPlayer->GetQuestStatus(QUEST_MISSING_DIPLOMAT11) == QUEST_STATUS_INCOMPLETE)
                pPlayer->FailQuest(QUEST_MISSING_DIPLOMAT11);
            return false;
        }
        Creature* pSlim = GetClosestCreatureWithEntry(pCreature, NPC_TAPOKE_SLIM_JAHN, 25.0f);
        if (!pSlim || !dynamic_cast<npc_tapoke_slim_jahnAI*>(pSlim->AI()))
        {
            if (pPlayer->GetQuestStatus(QUEST_MISSING_DIPLOMAT11) == QUEST_STATUS_INCOMPLETE)
                pPlayer->FailQuest(QUEST_MISSING_DIPLOMAT11);
            return false;
        }
        pCreature->AI()->SendAIEvent(AI_EVENT_START_ESCORT, pPlayer, pSlim, pQuest->GetQuestId());
        return true;
    }

    return false;
}

enum
{
    NPC_PROSPECTOR_WHELGAR  = 1077,
    SAY_ITEM_LOOTED         = 1235
};

// Flagongut's Fossil ID: 9630

struct go_flagongut_fossil : public GameObjectAI
{
    go_flagongut_fossil(GameObject* go) : GameObjectAI(go) {}

    void OnLootStateChange(Unit* /*user*/)
    {
        if (m_go->GetLootState() == GO_JUST_DEACTIVATED)
        {
            if (Creature* whelgar = GetClosestCreatureWithEntry(m_go, NPC_PROSPECTOR_WHELGAR, 10.0f))
            {
                whelgar->HandleEmote(EMOTE_ONESHOT_TALK);
                DoBroadcastText(SAY_ITEM_LOOTED, whelgar);
            }
        }
    }
};

GameObjectAI* GetAI_go_flagongut_fossil(GameObject* go)
{
    return new go_flagongut_fossil(go);
}


/*######
## AddSC
######*/

void AddSC_wetlands()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_tapoke_slim_jahn";
    pNewScript->GetAI = &GetAI_npc_tapoke_slim_jahn;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_mikhail";
    pNewScript->GetAI = &GetAI_npc_mikhail;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_mikhail;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_flagongut_fossil";
    pNewScript->GetGameObjectAI = &GetAI_go_flagongut_fossil;
    pNewScript->RegisterSelf();
}
