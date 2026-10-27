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
SDName: boss_zumrah
SD%Complete: 100
SDComment:
SDCategory: Zul'Farrak
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulfarrak.h"

enum
{
    SAY_INTRO                   = -1209000,
    SAY_AGGRO                   = -1209001,
    SAY_KILL                    = -1209002,
    SAY_SUMMON                  = -1209003,

    SPELL_SHADOW_BOLT           = 12739,
    SPELL_SHADOW_BOLT_VOLLEY    = 15245,
    SPELL_WARD_OF_ZUMRAH        = 11086,
    SPELL_HEALING_WAVE          = 12491,
    SPELL_SUMMON_ZOMBIES        = 10247,            // same two summons as the linked shallow-grave trap

    // NPC_WARD_OF_ZUMRAH       = 7785,
    // NPC_SKELETON_OF_ZUMRAH   = 7786,
    NPC_ZULFARRAK_ZOMBIE        = 7286,             // spawned by the graves
    NPC_ZULFARRAK_DEAD_HERO     = 7276,             // spawned by the graves

    FACTION_HOSTILE             = 14,
};

struct boss_zumrahAI : public ScriptedAI
{
    boss_zumrahAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = dynamic_cast<instance_zulfarrak*>(pCreature->GetInstanceData());
        m_bHasTurnedHostile = false;
        Reset();
    }

    instance_zulfarrak* m_pInstance;

    uint32 m_uiShadowBoltTimer;
    uint32 m_uiShadowBoltVolleyTimer;
    uint32 m_uiWardOfZumrahTimer;
    uint32 m_uHealingWaveTimer;
    uint32 m_uiSpawnZombieTimer;

    bool m_bHasTurnedHostile;
    bool m_bSummoningFromGrave;
    uint8 m_uiGraveSummonMask;
    GuidSet m_pendingGraveSummons;

    void Reset() override
    {
        m_uiShadowBoltTimer         = 1000;
        m_uiShadowBoltVolleyTimer   = urand(6000, 30000);
        m_uiWardOfZumrahTimer       = urand(7000, 20000);
        m_uHealingWaveTimer         = urand(10000, 15000);
        m_uiSpawnZombieTimer        = 1000;
        m_bSummoningFromGrave        = false;
        m_uiGraveSummonMask          = 0;
        m_pendingGraveSummons.clear();

        // A normal combat reset must not repeat the first proximity introduction.
        m_attackDistance = 10.0f;
    }

    bool DespawnIfEncounterComplete()
    {
        if (!m_pInstance || m_pInstance->GetData(TYPE_ZUMRAH) != DONE)
            return false;
        // A late living spawn must not recreate a defeated boss. Keep the real
        // death corpse and its normal loot until ordinary corpse removal.
        if (m_creature->IsAlive())
            m_creature->ForcedDespawn();
        return true;
    }

    void JustRespawned() override
    {
        ScriptedAI::JustRespawned();
        DespawnIfEncounterComplete();
    }

    bool IsValidVictim(Unit* target) const
    {
        if (!target || !target->IsInWorld() || !target->IsAlive() || target->IsFeigningDeath() ||
            target->GetMap() != m_creature->GetMap() || target->GetInstanceData() != m_pInstance)
            return false;

        const uint32 invalidFlags = UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_UNTARGETABLE |
            UNIT_FLAG_TAXI_FLIGHT | UNIT_FLAG_UNINTERACTIBLE | UNIT_FLAG_SPAWNING | UNIT_FLAG_IMMUNE_TO_NPC;
        if (target->HasFlag(UNIT_FIELD_FLAGS, invalidFlags))
            return false;
        if (target->IsPlayer())
        {
            Player* player = static_cast<Player*>(target);
            if (player->IsGameMaster() || player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
                return false;
        }
        return true;
    }

    void Aggro(Unit* /*pWho*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void KilledUnit(Unit* /*pVictim*/) override
    {
        DoScriptText(SAY_KILL, m_creature);
    }

    void MoveInLineOfSight(Unit* pWho) override
    {
        if (!m_pInstance || !m_creature->IsAlive() || DespawnIfEncounterComplete() || !IsValidVictim(pWho))
            return;

        if (!m_bHasTurnedHostile && pWho->IsPlayer() && m_creature->IsWithinDistInMap(pWho, 9.0f) && m_creature->IsWithinLOSInMap(pWho))
        {
            m_creature->SetFactionTemporary(FACTION_HOSTILE, TEMPFACTION_TOGGLE_IMMUNE_TO_PLAYER);
            DoScriptText(SAY_INTRO, m_creature);
            m_bHasTurnedHostile = true;
            AttackStart(pWho);
        }

        ScriptedAI::MoveInLineOfSight(pWho);
    }

    void JustSummoned(Creature* pSummoned) override
    {
        if (!m_pInstance || !pSummoned || !pSummoned->IsAlive() || !pSummoned->IsInWorld() ||
            pSummoned->GetMap() != m_creature->GetMap() || pSummoned->GetInstanceData() != m_pInstance)
            return;
        if (pSummoned->GetEntry() != NPC_ZULFARRAK_ZOMBIE && pSummoned->GetEntry() != NPC_ZULFARRAK_DEAD_HERO)
            return;

        if (m_bSummoningFromGrave && m_pendingGraveSummons.insert(pSummoned->GetObjectGuid()).second)
            m_uiGraveSummonMask |= pSummoned->GetEntry() == NPC_ZULFARRAK_ZOMBIE ? 1 : 2;

        Unit* victim = m_creature->GetVictim();
        if (m_pInstance && m_pInstance->GetData(TYPE_ZUMRAH) != DONE && IsValidVictim(victim) && pSummoned->AI())
            pSummoned->AI()->AttackStart(victim);
    }

    bool TrySummonFromGrave(GameObject* grave)
    {
        if (!m_pInstance || m_bSummoningFromGrave || !m_creature->IsAlive() ||
            m_pInstance->GetData(TYPE_ZUMRAH) != IN_PROGRESS || !IsValidVictim(m_creature->GetVictim()) || !grave ||
            grave->GetEntry() != GO_SHALLOW_GRAVE || grave->GetMap() != m_creature->GetMap() ||
            !grave->IsSpawned() || grave->GetLootState() != GO_READY)
            return false;

        m_pendingGraveSummons.clear();
        m_uiGraveSummonMask = 0;
        m_bSummoningFromGrave = true;
        SpellCastResult result = m_creature->CastSpell(grave->GetPositionX(), grave->GetPositionY(), grave->GetPositionZ(),
            SPELL_SUMMON_ZOMBIES, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, grave->GetObjectGuid());
        m_bSummoningFromGrave = false;

        // Both existing spell effects must succeed. A spell can report success
        // while a missing creature template or failed allocation yields no add.
        if (result != SPELL_CAST_OK || m_uiGraveSummonMask != 3 || m_pendingGraveSummons.size() != 2)
        {
            GuidSet partial;
            partial.swap(m_pendingGraveSummons);
            for (ObjectGuid guid : partial)
                if (Creature* summoned = m_creature->GetMap()->GetCreature(guid))
                {
                    summoned->SetNoRewards();
                    summoned->ForcedDespawn();
                }
            return false;
        }

        m_pendingGraveSummons.clear();
        grave->SetLootState(GO_JUST_DEACTIVATED);
        return true;
    }

    GameObject* SelectNearbyShallowGrave()
    {
        if (!m_pInstance)
            return nullptr;

        // Get the list of usable graves (not used already by players)
        GuidList lTempList;
        GameObjectList lGravesInRange;

        m_pInstance->GetShallowGravesGuidList(lTempList);
        for (GuidList::const_iterator itr = lTempList.begin(); itr != lTempList.end(); ++itr)
        {
            GameObject* pGo = m_creature->GetMap()->GetGameObject(*itr);
            // Go spawned and no looting in process
            if (pGo && pGo->IsSpawned() && pGo->GetLootState() == GO_READY)
                lGravesInRange.push_back(pGo);
        }

        if (lGravesInRange.empty())
            return nullptr;

        // Sort the graves
        lGravesInRange.sort(ObjectDistanceOrder(m_creature));

        return *lGravesInRange.begin();
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_pInstance || !m_creature->IsAlive() || DespawnIfEncounterComplete() ||
            !m_creature->SelectHostileTarget() || !IsValidVictim(m_creature->GetVictim()))
            return;

        if (m_uiSpawnZombieTimer)
        {
            if (m_uiSpawnZombieTimer <= uiDiff)
            {
                // Use a nearby grave to spawn zombies
                if (GameObject* pGrave = SelectNearbyShallowGrave())
                {
                    if (TrySummonFromGrave(pGrave))
                    {
                        if (roll_chance_i(30))
                            DoScriptText(SAY_SUMMON, m_creature);

                        m_uiSpawnZombieTimer = 20000;
                    }
                    else
                        m_uiSpawnZombieTimer = 1000;
                }
                else
                    // Graves may be temporarily unloaded or in use by a player.
                    m_uiSpawnZombieTimer = 1000;
            }
            else
                m_uiSpawnZombieTimer -= uiDiff;
        }

        if (m_uiShadowBoltTimer < uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
            {
                if (DoCastSpellIfCan(pTarget, SPELL_SHADOW_BOLT) == CAST_OK)
                    m_uiShadowBoltTimer = urand(3500, 5000);
            }
        }
        else
            m_uiShadowBoltTimer -= uiDiff;

        if (m_uiShadowBoltVolleyTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_SHADOW_BOLT_VOLLEY) == CAST_OK)
                m_uiShadowBoltVolleyTimer = urand(10000, 18000);
        }
        else
            m_uiShadowBoltVolleyTimer -= uiDiff;

        if (m_uiWardOfZumrahTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_WARD_OF_ZUMRAH) == CAST_OK)
                m_uiWardOfZumrahTimer = urand(15000, 32000);
        }
        else
            m_uiWardOfZumrahTimer -= uiDiff;

        if (m_uHealingWaveTimer < uiDiff)
        {
            if (Unit* pTarget = DoSelectLowestHpFriendly(40.0f))
            {
                if (DoCastSpellIfCan(pTarget, SPELL_HEALING_WAVE) == CAST_OK)
                    m_uHealingWaveTimer = urand(15000, 23000);
            }
        }
        else
            m_uHealingWaveTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_zumrah(Creature* pCreature)
{
    return new boss_zumrahAI(pCreature);
}

void AddSC_boss_zumrah()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_zumrah";
    pNewScript->GetAI = &GetAI_boss_zumrah;
    pNewScript->RegisterSelf();
}
