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
SDName: Thousand_Needles
SD%Complete: 90
SDComment: Quest support: 1950, 4770, 4904, 4966
SDCategory: Thousand Needles
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
npc_kanati
npc_lakota_windsong
npc_paoka_swiftmountain
npc_plucky_johnson
EndContentData */


#include "AI/ScriptDevAI/base/escort_ai.h"
#include "AI/EventAI/CreatureEventAI.h"
#include <algorithm>
#include <iterator>

/*######
# npc_kanati
######*/

enum
{
    SAY_KAN_START               = 6149,

    QUEST_PROTECT_KANATI        = 4966,
    NPC_GALAK_ASS               = 10720,

    FACTION_KANATI_ESCORT       = 232,

    PATH_ID_KANATI              = 10638,
};

const float m_afGalakLoc[] = { -4867.387695f, -1357.353760f, -48.226f};

struct npc_kanatiAI : public npc_escortAI
{
    npc_kanatiAI(Creature* creature) : npc_escortAI(creature) { Reset(); }

    ObjectGuid m_waveGuids[3];
    bool m_defeated[3] = {};
    bool m_waveStarted = false;
    bool m_waveReady = false;
    bool m_endReached = false;
    bool m_rewarded = false;
    int32 m_spawnSlot = -1;
    uint32 m_retryTimer = 0;
    uint32 m_spawnFailures = 0;
    uint32 m_playerTimer = 1000;

    // Ordinary combat evades must retain the admitted round and defeated slots.
    void Reset() override { }

    bool OwnedAssassin(Creature const* actor) const
    {
        return actor && actor->GetEntry() == NPC_GALAK_ASS &&
            actor->GetSpawnerGuid() == m_creature->GetObjectGuid();
    }

    void CleanupWave()
    {
        m_waveStarted = false;
        m_spawnSlot = -1;
        for (uint32 i = 0; i < 3; ++i)
        {
            ObjectGuid guid = m_waveGuids[i];
            m_waveGuids[i].Clear();
            if (Creature* actor = m_creature->GetMap()->GetCreature(guid))
                if (OwnedAssassin(actor) && actor->IsAlive())
                    actor->ForcedDespawn(); // Keep corpses and other summoners' actors.
            m_defeated[i] = false;
        }
        m_retryTimer = m_spawnFailures = 0;
    }

    void JustStartedEscort() override
    {
        CleanupWave();
        m_waveReady = m_endReached = m_rewarded = false;
        m_playerTimer = 1000;
    }

    void JustRespawned() override
    {
        CleanupWave();
        SetEscortPaused(false);
        End();
        m_waveReady = m_endReached = m_rewarded = false;
        m_playerTimer = 1000;
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        npc_escortAI::JustRespawned();
    }

    void FailQuestForPlayerAndGroup() override
    {
        if (!m_rewarded)
            npc_escortAI::FailQuestForPlayerAndGroup();
        CleanupWave();
        SetEscortPaused(false);
        End();
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->RestoreOriginalFaction();
    }

    void AbortAndRespawn()
    {
        FailQuestForPlayerAndGroup();
        m_creature->SetDeathState(JUST_DIED);
        m_creature->Respawn();
    }

    void FinishDefense()
    {
        if (!m_waveReady || !m_endReached || m_rewarded || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        m_rewarded = true;
        if (Player* player = GetPlayerForEscort())
            player->RewardPlayerAndGroupAtEventExplored(QUEST_PROTECT_KANATI, m_creature);
        QuestEnd();
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
        m_creature->RestoreOriginalFaction();
    }

    void CheckWaveComplete()
    {
        if (!m_waveStarted || m_waveReady || !m_defeated[0] || !m_defeated[1] || !m_defeated[2])
            return;
        m_waveReady = true;
        SetEscortPaused(false);
        FinishDefense();
    }

    void WaypointReached(uint32 point) override
    {
        if (!HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        if (point == 1 && !m_waveStarted)
        {
            m_waveStarted = true;
            SetEscortPaused(true);
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC);
            DoBroadcastText(SAY_KAN_START, m_creature);
            DoSpawnGalak();
        }
        else if (point == 2)
        {
            m_endReached = true;
            FinishDefense();
        }
    }

    void DoSpawnGalak()
    {
        if (!m_waveStarted || m_waveReady || !HasEscortState(STATE_ESCORT_ESCORTING))
            return;
        bool failed = false;
        for (uint32 i = 0; i < 3; ++i)
        {
            if (m_defeated[i] || m_waveGuids[i])
                continue;
            m_spawnSlot = i;
            Creature* actor = m_creature->SummonCreature(NPC_GALAK_ASS,
                m_afGalakLoc[0], m_afGalakLoc[1], m_afGalakLoc[2], 0.0f,
                TEMPSPAWN_TIMED_OOC_DESPAWN, 25000);
            m_spawnSlot = -1;
            // A returned actor and the synchronous callback must agree.
            if (!OwnedAssassin(actor) || !actor->IsAlive() || m_waveGuids[i] != actor->GetObjectGuid())
            {
                if (OwnedAssassin(actor) && actor->IsAlive())
                    actor->ForcedDespawn();
                m_waveGuids[i].Clear();
                failed = true;
            }
        }
        if (failed)
        {
            if (++m_spawnFailures >= 3)
                AbortAndRespawn();
            else
                m_retryTimer = 1000;
        }
        else
            m_spawnFailures = m_retryTimer = 0;
    }

    void JustSummoned(Creature* actor) override
    {
        if (!OwnedAssassin(actor))
            return;
        if (!m_waveStarted || m_waveReady || m_spawnSlot < 0 || m_spawnSlot >= 3 ||
            m_waveGuids[m_spawnSlot] || !actor->IsAlive())
        {
            if (actor->IsAlive())
                actor->ForcedDespawn();
            return;
        }
        m_waveGuids[m_spawnSlot] = actor->GetObjectGuid();
        actor->AI()->AttackStart(m_creature);
    }

    void SummonedCreatureJustDied(Creature* actor) override
    {
        if (!m_waveStarted || !OwnedAssassin(actor) || actor->IsAlive())
            return;
        for (uint32 i = 0; i < 3; ++i)
            if (m_waveGuids[i] == actor->GetObjectGuid())
                m_defeated[i] = true;
        CheckWaveComplete();
    }

    void SummonedCreatureDespawn(Creature* actor) override
    {
        if (!m_waveStarted || m_waveReady || !OwnedAssassin(actor))
            return;
        for (uint32 i = 0; i < 3; ++i)
            if (!m_defeated[i] && m_waveGuids[i] == actor->GetObjectGuid())
            {
                m_waveGuids[i].Clear();
                m_retryTimer = 1000;
            }
    }

    void UpdateEscortAI(uint32 diff) override
    {
        if (m_waveStarted && !m_waveReady && HasEscortState(STATE_ESCORT_ESCORTING))
        {
            // The escort base deliberately skips the leash while paused. Keep
            // its 66-yard group rule, and retire a round abandoned by everyone.
            if (m_playerTimer <= diff)
            {
                Player* player = GetPlayerForEscort();
                if (!player || !player->CheckForGroup([&](Player const* member)
                    {
                        return member->GetQuestStatus(QUEST_PROTECT_KANATI) == QUEST_STATUS_INCOMPLETE &&
                            m_creature->IsWithinDistInMap(member, 66.0f);
                    }))
                {
                    AbortAndRespawn();
                    return;
                }
                m_playerTimer = 1000;
            }
            else
                m_playerTimer -= diff;

            if (m_retryTimer)
            {
                if (m_retryTimer <= diff)
                    DoSpawnGalak();
                else
                    m_retryTimer -= diff;
            }
        }
        npc_escortAI::UpdateEscortAI(diff);
    }
};

bool QuestAccept_npc_kanati(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->GetQuestId() == QUEST_PROTECT_KANATI)
    {
        if (npc_kanatiAI* escortAI = dynamic_cast<npc_kanatiAI*>(creature->AI()))
        {
            creature->SetFactionTemporary(FACTION_KANATI_ESCORT, TEMPFACTION_RESTORE_RESPAWN);
            escortAI->Start(false, player, quest, true, false, PATH_ID_KANATI);
        }
    }
    return true;
}

/*######
# npc_lakota_windsong
######*/

enum
{
    SAY_LAKO_START              = -1000365,
    SAY_LAKO_LOOK_OUT           = -1000366,
    SAY_LAKO_HERE_COME          = -1000367,
    SAY_LAKO_MORE               = -1000368,
    SAY_LAKO_END                = -1000369,

    QUEST_FREE_AT_LAST          = 4904,
    NPC_GRIM_BANDIT             = 10758,

    ID_AMBUSH_1                 = 0,
    ID_AMBUSH_2                 = 2,
    ID_AMBUSH_3                 = 4
};

float m_afBanditLoc[6][6] =
{
    { -4905.479492f, -2062.732666f, 84.352f},
    { -4915.201172f, -2073.528320f, 84.733f},
    { -4878.883301f, -1986.947876f, 91.966f},
    { -4877.503906f, -1966.113403f, 91.859f},
    { -4767.985352f, -1873.169189f, 90.192f},
    { -4788.861328f, -1888.007813f, 89.888f}
};

struct npc_lakota_windsongAI : public npc_escortAI
{
    npc_lakota_windsongAI(Creature* pCreature) : npc_escortAI(pCreature)
    {
        SetReactState(REACT_DEFENSIVE);
        Reset();
    }

    void Reset() override { }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 9:
                DoScriptText(SAY_LAKO_LOOK_OUT, m_creature);
                DoSpawnBandits(ID_AMBUSH_1);
                break;
            case 15:
                DoScriptText(SAY_LAKO_HERE_COME, m_creature);
                DoSpawnBandits(ID_AMBUSH_2);
                break;
            case 22:
                DoScriptText(SAY_LAKO_MORE, m_creature);
                DoSpawnBandits(ID_AMBUSH_3);
                break;
            case 46:
                DoScriptText(SAY_LAKO_END, m_creature);
                if (Player* pPlayer = GetPlayerForEscort())
                    pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_FREE_AT_LAST, m_creature);
                break;
        }
    }

    void DoSpawnBandits(int uiAmbushId)
    {
        for (int i = 0; i < 2; ++i)
            m_creature->SummonCreature(NPC_GRIM_BANDIT,
                                       m_afBanditLoc[i + uiAmbushId][0], m_afBanditLoc[i + uiAmbushId][1], m_afBanditLoc[i + uiAmbushId][2], 0.0f,
                                       TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 60000);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        pSummoned->AI()->AttackStart(m_creature);
    }
};

UnitAI* GetAI_npc_lakota_windsong(Creature* pCreature)
{
    return new npc_lakota_windsongAI(pCreature);
}

bool QuestAccept_npc_lakota_windsong(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_FREE_AT_LAST)
    {
        DoScriptText(SAY_LAKO_START, pCreature, pPlayer);
        pCreature->SetFactionTemporary(FACTION_ESCORT_H_NEUTRAL_ACTIVE, TEMPFACTION_RESTORE_RESPAWN);

        if (npc_lakota_windsongAI* pEscortAI = dynamic_cast<npc_lakota_windsongAI*>(pCreature->AI()))
            pEscortAI->Start(false, pPlayer, pQuest);
    }
    return true;
}

/*######
# npc_paoka_swiftmountain
######*/

enum
{
    SAY_START           = -1000362,
    SAY_WYVERN          = -1000363,
    SAY_COMPLETE        = -1000364,

    QUEST_HOMEWARD      = 4770,
    NPC_WYVERN          = 4107
};

float m_afWyvernLoc[3][3] =
{
    { -4990.606f, -906.057f, -5.343f},
    { -4970.241f, -927.378f, -4.951f},
    { -4985.364f, -952.528f, -5.199f}
};

struct npc_paoka_swiftmountainAI : public npc_escortAI
{
    npc_paoka_swiftmountainAI(Creature* pCreature) : npc_escortAI(pCreature)
    {
        SetReactState(REACT_DEFENSIVE);
        Reset();
    }

    bool m_spawningWyverns = false;
    bool m_ambushStarted = false;

    void Reset() override { }

    void JustStartedEscort() override
    {
        m_spawningWyverns = m_ambushStarted = false;
    }

    void JustSummoned(Creature* actor) override
    {
        if (m_spawningWyverns && HasEscortState(STATE_ESCORT_ESCORTING) && actor->IsAlive() &&
            actor->GetEntry() == NPC_WYVERN && actor->GetSpawnerGuid() == m_creature->GetObjectGuid())
            actor->AI()->AttackStart(m_creature);
    }

    void WaypointReached(uint32 uiPointId) override
    {
        switch (uiPointId)
        {
            case 16:
                if (!m_ambushStarted && HasEscortState(STATE_ESCORT_ESCORTING))
                {
                    m_ambushStarted = true;
                    DoScriptText(SAY_WYVERN, m_creature);
                    DoSpawnWyvern();
                }
                break;
            case 27:
                DoScriptText(SAY_COMPLETE, m_creature);
                break;
            case 28:
                if (Player* pPlayer = GetPlayerForEscort())
                    pPlayer->RewardPlayerAndGroupAtEventExplored(QUEST_HOMEWARD, m_creature);
                break;
        }
    }

    void DoSpawnWyvern()
    {
        m_spawningWyverns = true;
        for (auto& i : m_afWyvernLoc)
            m_creature->SummonCreature(NPC_WYVERN,
                i[0], i[1], i[2], 0.0f,
                                       TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 60000);
        m_spawningWyverns = false;
    }
};

UnitAI* GetAI_npc_paoka_swiftmountain(Creature* pCreature)
{
    return new npc_paoka_swiftmountainAI(pCreature);
}

bool QuestAccept_npc_paoka_swiftmountain(Player* pPlayer, Creature* pCreature, const Quest* pQuest)
{
    if (pQuest->GetQuestId() == QUEST_HOMEWARD)
    {
        DoScriptText(SAY_START, pCreature, pPlayer);
        pCreature->SetFactionTemporary(FACTION_ESCORT_H_NEUTRAL_ACTIVE, TEMPFACTION_RESTORE_RESPAWN);

        if (npc_paoka_swiftmountainAI* pEscortAI = dynamic_cast<npc_paoka_swiftmountainAI*>(pCreature->AI()))
            pEscortAI->Start(false, pPlayer, pQuest);
    }
    return true;
}

/*######
# "Plucky" Johnson
######*/

enum
{
    FACTION_FRIENDLY        = 35,
    QUEST_SCOOP             = 1950,
    SPELL_PLUCKY_HUMAN      = 9192,
    SPELL_PLUCKY_CHICKEN    = 9220
};

#define GOSSIP_ITEM_QUEST   "Please tell me the Phrase.."

struct npc_plucky_johnsonAI : public ScriptedAI
{
    npc_plucky_johnsonAI(Creature* creature) : ScriptedAI(creature) { Reset(); }

    uint32 m_windowTimer = 0;
    uint32 m_retryTimer = 0;
    bool m_humanWindow = false;
    bool m_pendingHuman = false;
    bool m_requireQuest = false;
    bool m_waveOnHuman = false;
    ObjectGuid m_requester;

    void CloseWindow()
    {
        m_humanWindow = false;
        m_windowTimer = 0;
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
        m_creature->RemoveAurasDueToSpell(SPELL_PLUCKY_HUMAN);
        m_creature->ClearTemporaryFaction();
        m_pendingHuman = false;
        m_waveOnHuman = false;
        m_requester.Clear();
        m_retryTimer = 0;
        if (!m_creature->HasAura(SPELL_PLUCKY_CHICKEN))
        {
            DoCastSpellIfCan(m_creature, SPELL_PLUCKY_CHICKEN);
            if (!m_creature->HasAura(SPELL_PLUCKY_CHICKEN))
                m_retryTimer = 1000;
        }
    }

    void Reset() override
    {
        m_pendingHuman = false;
        m_waveOnHuman = false;
        m_requester.Clear();
        CloseWindow();
    }

    bool CanDiscussPhrase(Player const* player) const
    {
        return player && player->IsAlive() && m_creature->IsAlive() &&
            m_humanWindow && m_windowTimer && m_creature->HasAura(SPELL_PLUCKY_HUMAN) &&
            m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP) &&
            player->GetQuestStatus(QUEST_SCOOP) == QUEST_STATUS_INCOMPLETE &&
            m_creature->IsWithinDistInMap(player, INTERACTION_DISTANCE);
    }

    void TryHumanForm()
    {
        Player* player = m_creature->GetMap()->GetPlayer(m_requester);
        if (!player || !player->IsAlive() ||
            (m_requireQuest && player->GetQuestStatus(QUEST_SCOOP) != QUEST_STATUS_INCOMPLETE))
        {
            m_pendingHuman = false;
            m_waveOnHuman = false;
            m_requester.Clear();
            m_retryTimer = 0;
            return;
        }
        if (DoCastSpellIfCan(m_creature, SPELL_PLUCKY_HUMAN) == CAST_OK &&
            m_creature->HasAura(SPELL_PLUCKY_HUMAN))
        {
            m_creature->RemoveAurasDueToSpell(SPELL_PLUCKY_CHICKEN);
            m_creature->SetFactionTemporary(FACTION_FRIENDLY,
                TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_RESTORE_COMBAT_STOP);
            m_creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_GOSSIP);
            m_humanWindow = true;
            m_windowTimer = 120000;
            m_pendingHuman = false;
            m_retryTimer = 0;
            if (m_waveOnHuman)
                m_creature->HandleEmote(EMOTE_ONESHOT_WAVE);
            m_waveOnHuman = false;
        }
        else
            m_retryTimer = 1000;
    }

    void ReceiveEmote(Player* player, uint32 emote) override
    {
        if (!player || !player->IsAlive() || !m_creature->IsAlive())
            return;
        if (emote == TEXTEMOTE_CHICKEN)
        {
            if (m_humanWindow)
                return;
        }
        else if (emote != TEXTEMOTE_BECKON || player->GetQuestStatus(QUEST_SCOOP) != QUEST_STATUS_INCOMPLETE)
            return;
        m_requester = player->GetObjectGuid();
        m_requireQuest = emote == TEXTEMOTE_BECKON;
        m_pendingHuman = true;
        m_waveOnHuman = emote == TEXTEMOTE_CHICKEN;
        TryHumanForm();
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_humanWindow)
        {
            if (!m_creature->HasAura(SPELL_PLUCKY_HUMAN) || m_windowTimer <= diff)
                CloseWindow();
            else
                m_windowTimer -= diff;
        }
        if (m_retryTimer)
        {
            if (m_retryTimer <= diff)
            {
                m_retryTimer = 0;
                if (m_pendingHuman)
                    TryHumanForm();
                else if (!m_humanWindow && !m_creature->HasAura(SPELL_PLUCKY_CHICKEN))
                {
                    DoCastSpellIfCan(m_creature, SPELL_PLUCKY_CHICKEN);
                    if (!m_creature->HasAura(SPELL_PLUCKY_CHICKEN))
                        m_retryTimer = 1000;
                }
            }
            else
                m_retryTimer -= diff;
        }
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;
        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_plucky_johnson(Creature* pCreature)
{
    return new npc_plucky_johnsonAI(pCreature);
}

bool GossipHello_npc_plucky_johnson(Player* pPlayer, Creature* pCreature)
{
    auto ai = dynamic_cast<npc_plucky_johnsonAI*>(pCreature->AI());
    if (ai && ai->CanDiscussPhrase(pPlayer))
        pPlayer->ADD_GOSSIP_ITEM(GOSSIP_ICON_CHAT, GOSSIP_ITEM_QUEST, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF);

    pPlayer->SEND_GOSSIP_MENU(720, pCreature->GetObjectGuid());
    return true;
}

bool GossipSelect_npc_plucky_johnson(Player* pPlayer, Creature* pCreature, uint32 uiSender, uint32 uiAction)
{
    auto ai = dynamic_cast<npc_plucky_johnsonAI*>(pCreature->AI());
    if (uiSender == GOSSIP_SENDER_MAIN && uiAction == GOSSIP_ACTION_INFO_DEF &&
        ai && ai->CanDiscussPhrase(pPlayer))
    {
        pPlayer->SEND_GOSSIP_MENU(738, pCreature->GetObjectGuid());
        pPlayer->AreaExploredOrEventHappens(QUEST_SCOOP);
    }

    return true;
}

// Extra combat abilities are scoped to approved entry/GUID pairs. A negative
// EventAI GUID row alone would also run for every other random candidate.
struct ThousandNeedlesCombatSettings
{
    uint32 spell;
    uint32 initialMin;
    uint32 initialMax;
    uint32 repeatMin;
    uint32 repeatMax;
    bool meleeOnly;
    bool oncePerCombat;
    float victimRadius;
};

static bool GetThousandNeedlesCombatSettings(Creature const* creature, ThousandNeedlesCombatSettings& settings)
{
    if (creature->GetMapId() != 1)
        return false;
    uint32 guid = creature->GetDbGuid();
    switch (creature->GetEntry())
    {
        case 4097:
        {
            static const uint32 guids[] = {140029, 140031, 140032, 140033, 140034, 140035, 140038, 140042, 140043, 140044, 140045, 140046, 140047, 140048, 140050, 140053, 140054, 140124, 140125, 140126, 140127, 140128, 140129, 140130, 140131, 140132, 140133, 140134, 140135, 140136, 140137, 140138, 140139, 140140, 140141, 140142, 140143, 140144, 140145};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {6535, 0, 0, 0, 0, false, true, 0.0f};
                return true;
            }
            break;
        }
        case 4124:
        {
            static const uint32 guids[] = {21233, 21234, 21235, 21236, 21237, 21238, 21239, 21240, 21241, 21242, 21243, 21244, 21245, 21246, 21247, 21248, 21249, 21250, 21251, 21252, 21253, 21254, 21255, 21256, 21257, 21258, 21259, 21260, 21261, 21262, 21263, 21264, 21265, 21266, 21267, 21268, 21269, 21270, 21271, 21272, 21273};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {24331, 2100, 3700, 12700, 13400, true, false, 0.0f};
                return true;
            }
            break;
        }
        case 4248:
        {
            static const uint32 guids[] = {21595, 21596, 21597, 21598, 21599, 21600, 21601, 21602, 21603, 21604, 21605, 21606, 21607, 21608, 21609, 21610, 21611, 21612, 21613, 21614, 21615, 21616, 21617, 21618, 21619, 21620, 21621, 21622, 21623, 21624, 21625, 21626, 21627, 21628, 21629, 21630, 21631, 21632, 21633, 21634, 21635, 21636, 21637, 21638, 21639, 21640};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                settings = {3604, 1700, 2400, 9800, 12600, true, false, 0.0f};
                return true;
            }
            break;
        }
        case 9377:
        {
            static const uint32 guids[] = {21684, 21685, 21686, 21687, 21688, 21689};
            if (std::binary_search(std::begin(guids), std::end(guids), guid))
            {
                // SpellRadius.dbc index8 is 5 yards; the spell centers on self.
                settings = {10093, 1700, 2300, 8700, 9200, false, false, 5.0f};
                return true;
            }
            break;
        }
    }
    return false;
}

struct npc_thousand_needles_combatAI : public CreatureEventAI
{
    npc_thousand_needles_combatAI(Creature* creature, ThousandNeedlesCombatSettings const& settings) :
        CreatureEventAI(creature), m_settings(settings) { ResetExtraCombat(); }

    ThousandNeedlesCombatSettings m_settings;
    uint32 m_extraTimer = 0;
    bool m_extraDone = false;

    void ResetExtraCombat()
    {
        m_extraTimer = urand(m_settings.initialMin, m_settings.initialMax);
        m_extraDone = false;
    }

    void Reset() override
    {
        CreatureEventAI::Reset();
        ResetExtraCombat();
    }

    void EnterCombat(Unit* target) override
    {
        CreatureEventAI::EnterCombat(target);
        ResetExtraCombat();
    }

    void UpdateExtraCombat(uint32 diff)
    {
        ThousandNeedlesCombatSettings current;
        if (!GetThousandNeedlesCombatSettings(m_creature, current) || current.spell != m_settings.spell ||
            m_creature->GetBeneficiaryPlayer() || !m_creature->IsAlive() || !m_creature->IsInCombat() || !CanExecuteCombatAction() ||
            (m_settings.oncePerCombat && m_extraDone))
            return;
        Unit* victim = m_creature->GetVictim();
        if (!victim || !victim->IsAlive() || !m_creature->CanAttack(victim))
            return;
        if (m_extraTimer > diff)
        {
            m_extraTimer -= diff;
            return;
        }
        if ((m_settings.meleeOnly && !m_creature->CanReachWithMeleeAttack(victim)) ||
            (m_settings.victimRadius && !m_creature->IsWithinDistInMap(victim, m_settings.victimRadius)))
        {
            m_extraTimer = 1000;
            return;
        }
        Unit* target = m_settings.spell == 10093 ? m_creature : victim;
        if (DoCastSpellIfCan(target, m_settings.spell) == CAST_OK)
        {
            m_extraDone = true;
            m_extraTimer = urand(m_settings.repeatMin, m_settings.repeatMax);
        }
        else
            m_extraTimer = 1000;
    }

    void UpdateAI(uint32 diff) override
    {
        UpdateExtraCombat(diff);
        // Keep the full native EventAI update, including existing cast, ranged
        // mode, poison, flee, melee, targeting and ordinary reset behavior.
        CreatureEventAI::UpdateAI(diff);
    }
};

UnitAI* GetAI_npc_thousand_needles_combat(Creature* creature)
{
    ThousandNeedlesCombatSettings settings;
    if (GetThousandNeedlesCombatSettings(creature, settings) && !creature->GetBeneficiaryPlayer())
        return new npc_thousand_needles_combatAI(creature, settings);
    // Native FactorySelector continues with the original AIName/permit rules.
    return nullptr;
}

void AddSC_thousand_needles()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_kanati";
    pNewScript->GetAI = &GetNewAIInstance<npc_kanatiAI>;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_kanati;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_lakota_windsong";
    pNewScript->GetAI = &GetAI_npc_lakota_windsong;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_lakota_windsong;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_paoka_swiftmountain";
    pNewScript->GetAI = &GetAI_npc_paoka_swiftmountain;
    pNewScript->pQuestAcceptNPC = &QuestAccept_npc_paoka_swiftmountain;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_plucky_johnson";
    pNewScript->GetAI = &GetAI_npc_plucky_johnson;
    pNewScript->pGossipHello = &GossipHello_npc_plucky_johnson;
    pNewScript->pGossipSelect = &GossipSelect_npc_plucky_johnson;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_thousand_needles_combat";
    pNewScript->GetAI = &GetAI_npc_thousand_needles_combat;
    pNewScript->RegisterSelf();
}
