/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/Pet.h"

#include <algorithm>

namespace
{
enum
{
    NPC_NIGHTMARE_ILLUSION = 15163,
    SPELL_SUMMON_ILLUSION_RIGHT = 24681,
    SPELL_SUMMON_ILLUSION_BACK = 24728,
    SPELL_SUMMON_ILLUSION_LEFT = 24729,
};

struct boss_hazzarahAI : public CreatureEventAI
{
    explicit boss_hazzarahAI(Creature* creature) : CreatureEventAI(creature) {}

    void Reset() override
    {
        DespawnIllusions();
        CreatureEventAI::Reset();
    }

    void EnterEvadeMode() override
    {
        DespawnIllusions();
        CreatureEventAI::EnterEvadeMode();
    }

    void PrepareIllusionWave()
    {
        m_waveActive = true;
        m_usedTargets.clear();

        // Naturally expired guardians do not notify the summoner.
        std::erase_if(m_illusions, [this](ObjectGuid guid)
        {
            Pet* illusion = m_creature->GetMap()->GetPet(guid);
            return !illusion || illusion->m_removed;
        });
    }

    void FinishIllusionWave()
    {
        m_waveActive = false;
        m_usedTargets.clear();
    }

    void JustSummoned(Creature* summoned) override
    {
        CreatureEventAI::JustSummoned(summoned);

        if (summoned->GetEntry() != NPC_NIGHTMARE_ILLUSION || !summoned->IsPet() ||
            summoned->GetOwnerGuid() != m_creature->GetObjectGuid())
            return;

        Pet* illusion = static_cast<Pet*>(summoned);
        uint32 const summonSpell = illusion->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        if (!illusion->IsGuardian() || illusion->m_removed ||
            (summonSpell != SPELL_SUMMON_ILLUSION_BACK && summonSpell != SPELL_SUMMON_ILLUSION_RIGHT &&
                summonSpell != SPELL_SUMMON_ILLUSION_LEFT))
            return;

        if (!m_illusions.insert(illusion->GetObjectGuid()).second || !m_waveActive)
            return;

        std::vector<Unit*> players;
        m_creature->SelectAttackingTargets(players, ATTACKING_TARGET_ALL_SUITABLE, 0, nullptr, SELECT_FLAG_PLAYER);
        players.erase(std::remove_if(players.begin(), players.end(), [illusion](Unit* player)
        {
            return !player->IsAlive() || !illusion->CanAttack(player);
        }), players.end());
        if (players.empty())
            return;

        auto const unusedEnd = std::partition(players.begin(), players.end(), [this](Unit* player)
        {
            return m_usedTargets.find(player->GetObjectGuid()) == m_usedTargets.end();
        });
        auto const targetEnd = unusedEnd == players.begin() ? players.end() : unusedEnd;
        Unit* target = players[urand(0, uint32(targetEnd - players.begin() - 1))];
        illusion->AI()->AttackStart(target);
        if (illusion->GetVictim() == target)
        {
            illusion->AddThreat(target, 1.0f);
            m_usedTargets.insert(target->GetObjectGuid());
        }
    }

private:
    void DespawnIllusions()
    {
        FinishIllusionWave();
        GuidSet illusions;
        illusions.swap(m_illusions);
        for (ObjectGuid guid : illusions)
            if (Pet* illusion = m_creature->GetMap()->GetPet(guid))
                if (!illusion->m_removed && illusion->GetOwnerGuid() == m_creature->GetObjectGuid())
                    illusion->Unsummon(PET_SAVE_AS_DELETED, m_creature);
    }

    GuidSet m_illusions;
    GuidSet m_usedTargets;
    bool m_waveActive = false;
};

struct SummonNightmareIllusion : public SpellScript
{
    void OnCast(Spell* spell) const override
    {
        if (Unit* caster = spell->GetCaster())
            if (auto* ai = dynamic_cast<boss_hazzarahAI*>(caster->AI()))
                ai->PrepareIllusionWave();
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_1)
        {
            Unit* caster = spell->GetCaster();
            caster->CastSpell(nullptr, SPELL_SUMMON_ILLUSION_RIGHT, TRIGGERED_OLD_TRIGGERED);
            caster->CastSpell(nullptr, SPELL_SUMMON_ILLUSION_LEFT, TRIGGERED_OLD_TRIGGERED);
        }
    }

    void OnSuccessfulFinish(Spell* spell) const override
    {
        if (Unit* caster = spell->GetCaster())
            if (auto* ai = dynamic_cast<boss_hazzarahAI*>(caster->AI()))
                ai->FinishIllusionWave();
    }
};
}

void AddSC_boss_hazzarah()
{
    Script* script = new Script;
    script->Name = "boss_hazzarah";
    script->GetAI = &GetNewAIInstance<boss_hazzarahAI>;
    script->RegisterSelf();

    RegisterSpellScript<SummonNightmareIllusion>("spell_summon_nightmare_illusion_hazzarah");
}
