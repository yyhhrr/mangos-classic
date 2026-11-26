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
SDName: Boss_Skeram
SD%Complete: 100
SDComment:
SDCategory: Temple of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "temple_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"

enum
{
    SAY_AGGRO                       = -1531000,
    SAY_SLAY                        = 11446,
    SAY_DEATH                       = -1531002,

    SPELL_ARCANE_EXPLOSION          = 26192,
    SPELL_EARTH_SHOCK               = 26194,
    SPELL_TRUE_FULFILLMENT          = 785,
    SPELL_FULFILLMENT_HASTE         = 2313,
    SPELL_FULFILLMENT_HEALING       = 26525,
    SPELL_FULFILLMENT_IMMUNITY      = 26526,
    SPELL_TELEPORT_1                = 4801,
    SPELL_TELEPORT_2                = 8195,
    SPELL_TELEPORT_3                = 20449,
    SPELL_INITIALIZE_IMAGES         = 794,
    SPELL_INITIALIZE_IMAGE          = 3730,
    SPELL_SUMMON_IMAGES             = 747,
    SPELL_TELEPORT_IMAGE            = 26591,
    SPELL_BIRTH                     = 26262,
};

enum SkeramActions
{
    SKERAM_SPLIT,
    SKERAM_ARCANE_EXPLOSION,
    SKERAM_TRUE_FULFILMENT,
    SKERAM_BLINK,
    SKERAM_EARTH_SHOCK,
    SKERAM_ACTION_MAX,
    SKERAM_BLINK_DELAY,
    SKERAM_INIT_IMAGES,
};

static constexpr std::array<uint32, 3> skeramTeleports = { SPELL_TELEPORT_1, SPELL_TELEPORT_2, SPELL_TELEPORT_3 };
static constexpr std::array<float, 3> skeramSplitThresholds = { 75.0f, 50.0f, 25.0f };
static constexpr std::array<float, 3> skeramImageHealthMultipliers = { 0.10f, 0.20f, 0.50f };
static constexpr std::array<uint32, 3> skeramFulfillmentBuffs = { SPELL_FULFILLMENT_HASTE, SPELL_FULFILLMENT_HEALING, SPELL_FULFILLMENT_IMMUNITY };

static void RemoveFulfillmentBuffs(Unit* target, ObjectGuid caster)
{
    for (uint32 spellId : skeramFulfillmentBuffs)
        target->RemoveAurasByCasterSpell(spellId, caster, AURA_REMOVE_BY_CANCEL);
}

struct boss_skeramAI : public CombatAI
{
    boss_skeramAI(Creature* creature) : CombatAI(creature, SKERAM_ACTION_MAX),
        m_instance(static_cast<ScriptedInstance*>(creature->GetInstanceData())), m_isImage(!creature->GetSpawnerGuid().IsEmpty())
    {
        if (!m_isImage)
        {
            AddTimerlessCombatAction(SKERAM_SPLIT, true);
            AddCustomAction(SKERAM_INIT_IMAGES, true, [&]() { HandleInitImages(); }, TIMER_COMBAT_COMBAT);
        }

        AddCombatAction(SKERAM_BLINK, 30000, 45000);
        AddCombatAction(SKERAM_ARCANE_EXPLOSION, 6000, 12000);
        AddCombatAction(SKERAM_TRUE_FULFILMENT, 10000, 15000);
        AddCombatAction(SKERAM_EARTH_SHOCK, 1200u);
        AddCustomAction(SKERAM_BLINK_DELAY, true, [&]() { HandleBlinkDelay(); }, TIMER_COMBAT_COMBAT);
        AddOnKillText(SAY_SLAY);
    }

    ScriptedInstance* m_instance;
    bool const m_isImage;
    uint8 m_nextSplit = 0;
    float m_splitHealthPercent = 0.0f;
    uint32 m_maxMeleeAllowed = 0;
    int32 m_rangeCheckState = -1;

    GuidVector m_images;
    GuidVector m_newImages;
    std::array<uint32, 3> m_splitTeleports = skeramTeleports;
    ObjectGuid m_fulfilledPlayer;

    void Reset() override
    {
        CombatAI::Reset();
        m_nextSplit = 0;
        m_maxMeleeAllowed = 0;
        m_rangeCheckState = -1;
        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true);
    }

    void JustRespawned() override
    {
        CombatAI::JustRespawned();
        if (m_isImage)
            StopAttacking();
        DoCastSpellIfCan(nullptr, SPELL_BIRTH);
    }

    void Aggro(Unit* /*who*/) override
    {
        m_maxMeleeAllowed = m_creature->GetMap()->GetPlayersCountExceptGMs() / 10 + 1;
        if (m_isImage)
            return;

        DoScriptText(SAY_AGGRO, m_creature);
        if (m_instance)
            m_instance->SetData(TYPE_SKERAM, IN_PROGRESS);
    }

    void EnterEvadeMode() override
    {
        CancelFulfillment();
        if (!m_isImage)
            ClearImages();

        CombatAI::EnterEvadeMode();
    }

    void JustReachedHome() override
    {
        if (m_isImage)
        {
            m_creature->ForcedDespawn();
            return;
        }

        if (m_instance)
            m_instance->SetData(TYPE_SKERAM, FAIL);
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_isImage)
        {
            CancelFulfillment();
            m_creature->ForcedDespawn(1);
            return;
        }

        ClearImages();
        DoScriptText(SAY_DEATH, m_creature);
        if (m_instance)
            m_instance->SetData(TYPE_SKERAM, DONE);
    }

    void ClearImages()
    {
        DisableTimer(SKERAM_INIT_IMAGES);
        DisableTimer(SKERAM_BLINK_DELAY);
        m_newImages.clear();
        for (ObjectGuid guid : m_images)
        {
            if (Creature* image = m_creature->GetMap()->GetCreature(guid))
                if (auto* ai = dynamic_cast<boss_skeramAI*>(image->AI()))
                    ai->CancelFulfillment();
        }
        DespawnGuids(m_images);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (m_isImage || summoned->GetEntry() != NPC_SKERAM || summoned->GetSpawnerGuid() != m_creature->GetObjectGuid())
            return;

        ObjectGuid guid = summoned->GetObjectGuid();
        if (std::find(m_images.begin(), m_images.end(), guid) != m_images.end())
            return;

        m_images.push_back(guid);
        m_newImages.push_back(guid);
        summoned->CastSpell(m_creature, SPELL_INITIALIZE_IMAGE, TRIGGERED_OLD_TRIGGERED);

        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
            summoned->AI()->AttackStart(target);
    }

    bool IsNewImage(Unit const* image) const
    {
        return !m_isImage && image->GetSpawnerGuid() == m_creature->GetObjectGuid() &&
            std::find(m_newImages.begin(), m_newImages.end(), image->GetObjectGuid()) != m_newImages.end();
    }

    void InitializeImage(Unit* image) const
    {
        if (m_nextSplit >= skeramImageHealthMultipliers.size())
            return;

        image->SetMaxHealth(m_creature->GetMaxHealth() * skeramImageHealthMultipliers[m_nextSplit]);
        image->SetHealth(std::max(1u, uint32(image->GetMaxHealth() * m_splitHealthPercent / 100.0f)));
    }

    void StopAttacking()
    {
        SetCombatScriptStatus(true);
        SetMeleeEnabled(false);
        SetCombatMovement(false);
        m_creature->SetTarget(nullptr);
    }

    bool DoTeleport(uint32 spellId)
    {
        SpellTargetPosition const* destination = sSpellMgr.GetSpellTargetPosition(spellId);
        if (!destination || destination->target_mapId != m_creature->GetMapId() || DoCastSpellIfCan(nullptr, spellId) != CAST_OK)
            return false;

        Position landing = { destination->target_X, destination->target_Y, destination->target_Z, destination->target_Orientation };
        if (m_creature->GetPosition().GetDistance(landing) > 0.25f)
            return false;

        if (!m_creature->getThreatManager().isThreatListEmpty())
            DoResetThreat();
        m_creature->RemoveAllAuras();
        m_rangeCheckState = -1;
        ResetCombatAction(SKERAM_EARTH_SHOCK, 500);
        StopAttacking();
        ResetTimer(SKERAM_BLINK_DELAY, 2000);
        return true;
    }

    void HandleBlinkDelay()
    {
        SetCombatScriptStatus(false);
        SetMeleeEnabled(true);
        SetCombatMovement(true);
    }

    void TeleportSplitImage(Unit* image)
    {
        auto itr = std::find(m_newImages.begin(), m_newImages.end(), image->GetObjectGuid());
        if (itr == m_newImages.end())
            return;

        size_t index = std::distance(m_newImages.begin(), itr);
        if (index >= m_splitTeleports.size() - 1)
            return;

        if (auto* ai = dynamic_cast<boss_skeramAI*>(image->AI()))
        {
            itr->Clear();
            if (!ai->DoTeleport(m_splitTeleports[index + 1]))
                ai->HandleBlinkDelay();
        }
    }

    void HandleInitImages()
    {
        // The normal evade check is suspended while the split pauses combat actions.
        if (m_creature->getThreatManager().isThreatListEmpty())
        {
            EnterEvadeMode();
            return;
        }

        DoCastSpellIfCan(nullptr, SPELL_INITIALIZE_IMAGES, CAST_TRIGGERED);

        // Resume any image the area spell could not reach.
        for (ObjectGuid guid : m_newImages)
        {
            if (!guid)
                continue;

            if (Creature* image = m_creature->GetMap()->GetCreature(guid))
                if (auto* ai = dynamic_cast<boss_skeramAI*>(image->AI()))
                    ai->HandleBlinkDelay();
        }
        m_newImages.clear();

        if (!DoTeleport(m_splitTeleports.front()))
            HandleBlinkDelay();
        ResetCombatAction(SKERAM_BLINK, urand(30000, 40000));
    }

    Player* GetFulfillmentTarget() const
    {
        Player* nearest = nullptr;
        for (auto const& reference : m_creature->GetMap()->GetPlayers())
        {
            Player* player = reference.getSource();
            if (!player || player->IsGameMaster() || !player->IsAlive() ||
                !player->CanBeDetected() || player->IsFeigningDeathSuccessfully() ||
                !m_creature->IsWithinDistInMap(player, 40.0f) ||
                !m_creature->IsEnemy(player) || !m_creature->CanAttack(player))
                continue;

            if (!nearest || m_creature->GetDistanceOrder(player, nearest, true, DIST_CALC_BOUNDING_RADIUS))
                nearest = player;
        }
        return nearest;
    }

    void ApplyFulfillment(Unit* target)
    {
        if (m_fulfilledPlayer != target->GetObjectGuid())
            CancelFulfillment();

        m_fulfilledPlayer = target->GetObjectGuid();
        for (uint32 spellId : skeramFulfillmentBuffs)
            m_creature->CastSpell(target, spellId, TRIGGERED_OLD_TRIGGERED);
    }

    void CancelFulfillment()
    {
        if (!m_fulfilledPlayer)
            return;

        Player* target = m_creature->GetMap()->GetPlayer(m_fulfilledPlayer);
        m_fulfilledPlayer.Clear();
        if (!target)
            return;

        ObjectGuid const& caster = m_creature->GetObjectGuid();
        target->RemoveAurasByCasterSpell(SPELL_TRUE_FULFILLMENT, caster, AURA_REMOVE_BY_CANCEL);
        RemoveFulfillmentBuffs(target, caster);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case SKERAM_SPLIT:
            {
                float healthPercent = m_creature->GetHealthPercent();
                if (m_nextSplit >= skeramSplitThresholds.size() || healthPercent >= skeramSplitThresholds[m_nextSplit])
                    break;

                m_splitHealthPercent = healthPercent;
                if (DoCastSpellIfCan(m_creature, SPELL_SUMMON_IMAGES) == CAST_OK && !m_newImages.empty())
                {
                    ++m_nextSplit;
                    std::shuffle(m_splitTeleports.begin(), m_splitTeleports.end(), *GetRandomGenerator());
                    StopAttacking();
                    ResetTimer(SKERAM_INIT_IMAGES, 1500);
                }
                break;
            }
            case SKERAM_ARCANE_EXPLOSION:
            {
                uint32 meleePlayers = 0;
                for (auto const& reference : m_creature->GetMap()->GetPlayers())
                {
                    Player const* player = reference.getSource();
                    if (player && player->IsAlive() && !player->IsGameMaster() && m_creature->CanAttack(player) &&
                        m_creature->CanReachWithMeleeAttack(player))
                        ++meleePlayers;
                }

                if (meleePlayers >= m_maxMeleeAllowed)
                {
                    if (DoCastSpellIfCan(m_creature, SPELL_ARCANE_EXPLOSION) == CAST_OK)
                        ResetCombatAction(action, urand(8000, 18000));
                }
                else
                    ResetCombatAction(action, 1000);
                break;
            }
            case SKERAM_TRUE_FULFILMENT:
            {
                if (Player* target = GetFulfillmentTarget())
                    if (DoCastSpellIfCan(target, SPELL_TRUE_FULFILLMENT, CAST_AURA_NOT_PRESENT) == CAST_OK)
                        ResetCombatAction(action, urand(20500, 25000));
                break;
            }
            case SKERAM_BLINK:
            {
                if (DoTeleport(skeramTeleports[urand(0, skeramTeleports.size() - 1)]))
                    ResetCombatAction(action, urand(10000, 30000));
                else
                    ResetCombatAction(action, 500);
                break;
            }
            case SKERAM_EARTH_SHOCK:
            {
                uint32 timer = 500;
                Unit* victim = m_creature->GetVictim();
                if (!victim || m_creature->CanReachWithMeleeAttack(victim))
                    m_rangeCheckState = -1;
                else if (m_rangeCheckState < 2)
                    ++m_rangeCheckState;

                if (m_rangeCheckState >= 2 && DoCastSpellIfCan(victim, SPELL_EARTH_SHOCK) == CAST_OK)
                    timer = 2500;

                ResetCombatAction(action, timer);
                break;
            }
        }
    }
};

struct TrueFulfillment : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (apply || aura->GetEffIndex() != EFFECT_INDEX_0)
            return;

        Unit* caster = aura->GetCaster();
        // As in vMaNGOS, the buffs can run out naturally after defeating the real Prophet.
        if (caster && caster->GetEntry() == NPC_SKERAM && !caster->GetSpawnerGuid() && !caster->IsAlive())
            return;

        RemoveFulfillmentBuffs(aura->GetTarget(), aura->GetCasterGuid());
    }

    void OnAfterApply(Aura* aura, bool apply) const override
    {
        if (!apply || aura->GetEffIndex() != EFFECT_INDEX_0 || !aura->GetTarget()->HasCharmer(aura->GetCasterGuid()))
            return;

        if (Unit* caster = aura->GetCaster())
            if (auto* ai = dynamic_cast<boss_skeramAI*>(caster->AI()))
                ai->ApplyFulfillment(aura->GetTarget());
    }
};

struct InitializeImages : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        auto* ai = dynamic_cast<boss_skeramAI*>(spell->GetCaster()->AI());
        return ai && ai->IsNewImage(target);
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (Unit* image = spell->GetUnitTarget())
            image->CastSpell(image, SPELL_TELEPORT_IMAGE, TRIGGERED_OLD_TRIGGERED);
    }
};

struct InitializeImage : public SpellScript
{
    bool OnCheckTarget(Spell const* spell, Unit* target, SpellEffectIndex /*effIdx*/) const override
    {
        auto* ai = dynamic_cast<boss_skeramAI*>(target->AI());
        return ai && ai->IsNewImage(spell->GetCaster());
    }

    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (Unit* target = spell->GetUnitTarget())
            if (auto* ai = dynamic_cast<boss_skeramAI*>(target->AI()))
                ai->InitializeImage(spell->GetCaster());
    }
};

struct TeleportImage : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (Unit* image = spell->GetUnitTarget())
            if (Unit* prophet = image->GetSpawner())
                if (auto* ai = dynamic_cast<boss_skeramAI*>(prophet->AI()))
                    ai->TeleportSplitImage(image);
    }
};

void AddSC_boss_skeram()
{
    Script* newScript = new Script;
    newScript->Name = "boss_skeram";
    newScript->GetAI = &GetNewAIInstance<boss_skeramAI>;
    newScript->RegisterSelf();

    RegisterSpellScript<TrueFulfillment>("spell_true_fulfillment");
    RegisterSpellScript<InitializeImages>("spell_initialize_images");
    RegisterSpellScript<TeleportImage>("spell_teleport_image");
    RegisterSpellScript<InitializeImage>("spell_initialize_image");
}
