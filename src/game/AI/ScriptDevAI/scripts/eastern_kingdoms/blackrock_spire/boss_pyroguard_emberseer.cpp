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
SDName: Boss_Pyroguard_Emberseer
SD%Complete: 90
SDComment: Dummy spells used during the transformation may need further research
SDCategory: Blackrock Spire
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackrock_spire.h"
#include "Spells/Spell.h"

#include <algorithm>
#include <map>

enum
{
    // Intro emote/say
    EMOTE_NEAR              = -1229001,
    EMOTE_FULL              = -1229002,
    SAY_FREE                = -1229003,

    MAX_GROWING_STACKS      = 20,
    EMBERSEER_RETRY_DELAY   = 1000,
    MAX_EMBERSEER_ATTEMPTS  = 5,
    SPELL_GROWING_CHANNEL  = 16048,

    // Intro spells
    SPELL_ENCAGE_EMBERSEER  = 15281,                        // cast by Blackhand Incarcerator

    SPELL_FIRE_SHIELD       = 13377,
    SPELL_DESPAWN_EMBERSEER = 16078,                        // not sure what's the purpose of this
    SPELL_FREEZE_ANIM       = 16245,                        // not sure what's the purpose of this
    SPELL_FULL_STRENGHT     = 16047,
    SPELL_GROWING           = 16049,                        // stacking aura
    SPELL_BONUS_DAMAGE      = 16534,                        // triggered on full grow
    SPELL_TRANSFORM         = 16052,

    // Combat spells
    SPELL_FIRENOVA          = 23462,
    SPELL_FLAMEBUFFET       = 23341,
    SPELL_PYROBLAST         = 20228                         // guesswork, but best fitting in spells-area, was 17274 (has mana cost)
};

struct boss_pyroguard_emberseerAI : public ScriptedAI
{
    boss_pyroguard_emberseerAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = (instance_blackrock_spire*) pCreature->GetInstanceData();
        Reset();
    }

    instance_blackrock_spire* m_pInstance;

    uint32 m_uiEncageTimer;
    uint32 m_uiFireNovaTimer;
    uint32 m_uiFlameBuffetTimer;
    uint32 m_uiPyroBlastTimer;
    uint8 m_uiGrowingStacks;
    uint32 m_uiGrowingGeneration;
    uint32 m_uiReleaseRetryTimer;
    uint8 m_uiReleaseStep;
    uint8 m_uiReleaseAttempts;
    bool m_bReleasePending;
    bool m_bReleased;
    bool m_bRestoreReleased;
    bool m_bRestoreGrowing;
    bool m_bApplyingScale;
    uint32 m_uiScaleTarget;
    uint8 m_uiRestoreAttempts;
    uint32 m_uiRestoreRetryTimer;
    std::map<ObjectGuid, uint8> m_encageAttempts;

    void Reset() override
    {
        m_uiEncageTimer         = 10000;
        m_uiFireNovaTimer       = 6000;
        m_uiFlameBuffetTimer    = 3000;
        m_uiPyroBlastTimer      = 14000;
        m_uiGrowingStacks       = 0;
        m_uiGrowingGeneration   = 0;
        m_uiReleaseRetryTimer   = 0;
        m_uiReleaseStep         = 0;
        m_uiReleaseAttempts     = 0;
        m_bReleasePending       = false;
        m_bReleased             = false;
        m_bRestoreReleased      = false;
        m_bRestoreGrowing       = false;
        m_bApplyingScale        = false;
        m_uiScaleTarget         = 0;
        m_uiRestoreAttempts     = 0;
        m_uiRestoreRetryTimer   = 0;
        m_encageAttempts.clear();

        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);

        DoCastSpellIfCan(m_creature, SPELL_FIRE_SHIELD, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void JustDied(Unit* /*pKiller*/) override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_EMBERSEER, DONE);
    }

    void JustReachedHome() override
    {
        if (m_pInstance)
            m_pInstance->SetData(TYPE_EMBERSEER, FAIL);
    }

    void ReceiveAIEvent(AIEventType eventType, Unit* sender, Unit* invoker, uint32 miscValue) override
    {
        if (!m_pInstance || invoker != m_creature)
            return;

        if (eventType == AI_EVENT_CUSTOM_B)
        {
            // The instance commits the generation before starting spell 16048.
            // Repeated notifications for this generation must not reset its ticks.
            if (sender != m_creature || !miscValue || miscValue == m_uiGrowingGeneration ||
                !m_pInstance->IsEmberseerGrowing() || miscValue != m_pInstance->GetEmberseerGeneration())
                return;
            m_uiGrowingGeneration = miscValue;
            m_uiGrowingStacks = uint8(std::min(m_pInstance->GetEmberseerGrowingStacks(), uint32(MAX_GROWING_STACKS)));
            m_uiReleaseStep = uint8(std::min(m_pInstance->GetEmberseerReleaseStep(), uint32(3)));
            m_uiReleaseAttempts = 0;
            m_uiReleaseRetryTimer = 0;
            m_bReleasePending = false;
            m_bReleased = false;
            m_bRestoreReleased = false;
            m_bRestoreGrowing = m_uiGrowingStacks != 0;
            m_uiScaleTarget = m_uiGrowingStacks;
            m_uiRestoreAttempts = 0;
            m_uiRestoreRetryTimer = 0;
            m_uiEncageTimer = 0;
            if (m_bRestoreGrowing)
                RestoreGrowthScale();
        }
        else if (eventType == AI_EVENT_CUSTOM_C)
        {
            // Late-loaded or respawned incarcerators retry only their own slot.
            if (!sender || sender->GetEntry() != NPC_BLACKHAND_INCARCERATOR ||
                !m_pInstance->ShouldEmberseerBeCaged())
                return;
            GuidList incarcerators;
            m_pInstance->GetIncarceratorGUIDList(incarcerators);
            if (std::find(incarcerators.begin(), incarcerators.end(), sender->GetObjectGuid()) == incarcerators.end())
                return;
            m_encageAttempts.erase(sender->GetObjectGuid());
            m_uiEncageTimer = 1;
        }
        else if (eventType == AI_EVENT_CUSTOM_D)
        {
            if (sender != m_creature || !miscValue || miscValue != m_pInstance->GetEmberseerGeneration() ||
                !m_pInstance->IsEmberseerReleased() || m_bReleased)
                return;

            // Rebuilding an already released boss restores scale, never the
            // one-time 16534 damage, speech or event rewards.
            m_uiGrowingGeneration = miscValue;
            m_uiGrowingStacks = MAX_GROWING_STACKS;
            m_bReleased = true;
            m_bRestoreReleased = true;
            m_bRestoreGrowing = false;
            m_uiScaleTarget = MAX_GROWING_STACKS;
            m_bReleasePending = false;
            m_uiRestoreAttempts = 0;
            m_uiRestoreRetryTimer = 0;
            m_uiEncageTimer = 0;
            m_creature->RemoveAurasDueToSpell(SPELL_GROWING_CHANNEL);
            RestoreGrowthScale();
        }
        else if (eventType == AI_EVENT_CUSTOM_E)
        {
            if (sender == m_creature && miscValue == m_pInstance->GetEmberseerGeneration() &&
                m_pInstance->ShouldEmberseerBeCaged() && !m_creature->IsInCombat())
            {
                m_creature->RemoveAurasDueToSpell(SPELL_GROWING_CHANNEL);
                m_creature->RemoveAurasDueToSpell(SPELL_GROWING);
                m_creature->RemoveAurasDueToSpell(SPELL_FULL_STRENGHT);
                m_creature->RemoveAurasDueToSpell(SPELL_TRANSFORM);
                Reset();
            }
        }
        else if (eventType == AI_EVENT_CUSTOM_A && sender == m_creature &&
                 m_uiGrowingGeneration && m_uiGrowingGeneration == m_pInstance->GetEmberseerGeneration() &&
                 m_pInstance->IsEmberseerGrowing() && m_creature->HasAura(SPELL_GROWING_CHANNEL) &&
                 m_creature->IsAlive() && !m_bReleased && !m_bApplyingScale && !m_bRestoreGrowing &&
                 m_uiGrowingStacks < MAX_GROWING_STACKS)
            DoHandleEmberseerGrowing();
    }

    void RestoreGrowthScale()
    {
        if ((!m_bRestoreReleased && !m_bRestoreGrowing) || !m_creature->IsAlive() || !m_pInstance ||
            (m_bRestoreReleased ? !m_pInstance->IsEmberseerReleased() : !m_pInstance->IsEmberseerGrowing()) ||
            m_uiGrowingGeneration != m_pInstance->GetEmberseerGeneration())
            return;

        if (m_creature->GetAuraCount(SPELL_GROWING) > m_uiScaleTarget)
            m_creature->RemoveAurasDueToSpell(SPELL_GROWING);

        while (m_creature->GetAuraCount(SPELL_GROWING) < m_uiScaleTarget)
        {
            uint32 count = m_creature->GetAuraCount(SPELL_GROWING);
            m_bApplyingScale = true;
            CanCastResult result = DoCastSpellIfCan(m_creature, SPELL_GROWING, CAST_TRIGGERED);
            m_bApplyingScale = false;
            if (result != CAST_OK || m_creature->GetAuraCount(SPELL_GROWING) != count + 1)
            {
                m_uiRestoreRetryTimer = EMBERSEER_RETRY_DELAY;
                if (++m_uiRestoreAttempts >= MAX_EMBERSEER_ATTEMPTS)
                {
                    m_bRestoreReleased = false;
                    m_bRestoreGrowing = false;
                    m_pInstance->FailEmberseer();
                }
                return;
            }
        }

        m_uiRestoreAttempts = 0;
        if (m_bRestoreReleased)
        {
            m_bRestoreReleased = false;
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
        }
        else
        {
            m_bRestoreGrowing = false;
            if (m_uiGrowingStacks == MAX_GROWING_STACKS)
            {
                m_creature->RemoveAurasDueToSpell(SPELL_GROWING_CHANNEL);
                m_bReleasePending = true;
                TryRelease();
            }
        }
    }

    // Wrapper to handle the transformation
    void DoHandleEmberseerGrowing()
    {
        ++m_uiGrowingStacks;
        m_pInstance->EmberseerGrowingTick(m_creature, m_uiGrowingStacks);

        if (m_uiGrowingStacks == MAX_GROWING_STACKS / 2)
            DoScriptText(EMOTE_NEAR, m_creature);
        else if (m_uiGrowingStacks == MAX_GROWING_STACKS)
        {
            // A resumed 16048 still has a fresh 60s duration. Stop it at the
            // persisted twentieth tick so it cannot add extra scale afterward.
            m_creature->RemoveAurasDueToSpell(SPELL_GROWING_CHANNEL);
            DoScriptText(EMOTE_FULL, m_creature);
            DoScriptText(SAY_FREE, m_creature);
            m_bReleasePending = true;
            TryRelease();
        }
    }

    void TryRelease()
    {
        if (!m_bReleasePending || !m_pInstance || !m_pInstance->IsEmberseerGrowing() ||
            m_uiGrowingGeneration != m_pInstance->GetEmberseerGeneration())
            return;

        // Preserve the original order and do not repeat a successful damage step
        // when a later spell needs another attempt.
        static const uint32 spells[] = { SPELL_FULL_STRENGHT, SPELL_BONUS_DAMAGE, SPELL_TRANSFORM };
        while (m_uiReleaseStep < 3)
        {
            if (DoCastSpellIfCan(m_creature, spells[m_uiReleaseStep], CAST_TRIGGERED) != CAST_OK)
            {
                m_uiReleaseRetryTimer = EMBERSEER_RETRY_DELAY;
                if (++m_uiReleaseAttempts >= MAX_EMBERSEER_ATTEMPTS)
                {
                    m_bReleasePending = false;
                    m_pInstance->FailEmberseer();
                }
                return;
            }
            ++m_uiReleaseStep;
            m_pInstance->EmberseerReleaseStep(m_creature, m_uiReleaseStep);
        }

        m_bReleasePending = false;
        m_bReleased = true;
        m_pInstance->SetData(TYPE_EMBERSEER, IN_PROGRESS);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER);
        m_pInstance->EmberseerReleased(m_creature);
    }

    void RestoreEncage()
    {
        GuidList incarcerators;
        m_pInstance->GetIncarceratorGUIDList(incarcerators);
        for (ObjectGuid guid : incarcerators)
        {
            Creature* incarcerator = m_creature->GetMap()->GetCreature(guid);
            if (!incarcerator || !incarcerator->IsAlive() || incarcerator->IsInCombat())
                continue;

            Spell* channel = incarcerator->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (channel && channel->m_spellInfo->Id == SPELL_ENCAGE_EMBERSEER &&
                incarcerator->HasChannelObject(m_creature->GetObjectGuid()))
            {
                m_encageAttempts.erase(guid);
                continue;
            }

            uint8& attempts = m_encageAttempts[guid];
            if (attempts < MAX_EMBERSEER_ATTEMPTS)
            {
                ++attempts;
                incarcerator->CastSpell(m_creature, SPELL_ENCAGE_EMBERSEER, TRIGGERED_NONE);
            }
        }
        m_uiEncageTimer = EMBERSEER_RETRY_DELAY;
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        // The instance owns the event stage; growth/combat must never re-cage it.
        if (!m_pInstance || !m_pInstance->ShouldEmberseerBeCaged() || m_creature->IsInCombat() ||
            m_creature->HasAura(SPELL_GROWING_CHANNEL) || !m_creature->IsAlive())
            m_uiEncageTimer = 0;
        else if (m_uiEncageTimer)
        {
            if (m_uiEncageTimer <= uiDiff)
                RestoreEncage();
            else
                m_uiEncageTimer -= uiDiff;
        }

        if (m_bReleasePending)
        {
            if (m_uiReleaseRetryTimer > uiDiff)
                m_uiReleaseRetryTimer -= uiDiff;
            else
                TryRelease();
        }

        if (m_bRestoreReleased || m_bRestoreGrowing)
        {
            if (m_uiRestoreRetryTimer > uiDiff)
                m_uiRestoreRetryTimer -= uiDiff;
            else
                RestoreGrowthScale();
        }

        // Return since we have no target
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        // FireNova Timer
        if (m_uiFireNovaTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_FIRENOVA) == CAST_OK)
                m_uiFireNovaTimer = 6000;
        }
        else
            m_uiFireNovaTimer -= uiDiff;

        // FlameBuffet Timer
        if (m_uiFlameBuffetTimer < uiDiff)
        {
            if (DoCastSpellIfCan(m_creature, SPELL_FLAMEBUFFET) == CAST_OK)
                m_uiFlameBuffetTimer = 14000;
        }
        else
            m_uiFlameBuffetTimer -= uiDiff;

        // PyroBlast Timer
        if (m_uiPyroBlastTimer < uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
            {
                if (DoCastSpellIfCan(pTarget, SPELL_PYROBLAST) == CAST_OK)
                    m_uiPyroBlastTimer = 15000;
            }
        }
        else
            m_uiPyroBlastTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_boss_pyroguard_emberseer(Creature* pCreature)
{
    return new boss_pyroguard_emberseerAI(pCreature);
}

void AddSC_boss_pyroguard_emberseer()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_pyroguard_emberseer";
    pNewScript->GetAI = &GetAI_boss_pyroguard_emberseer;
    pNewScript->RegisterSelf();
}
