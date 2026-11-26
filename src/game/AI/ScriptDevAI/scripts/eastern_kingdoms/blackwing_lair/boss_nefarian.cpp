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
SDName: Boss_Nefarian
SD%Complete: 100
SDComment:
SDCategory: Blackwing Lair
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "blackwing_lair.h"
#include "Entities/TemporarySpawn.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "MotionGenerators/WaypointManager.h"
#include "Spells/Scripts/SpellScript.h"
#include <G3D/Vector3.h>

enum
{
    SAY_AGGRO                   = -1469007,
    SAY_XHEALTH                 = -1469008,             // at 5% hp
    SAY_SHADOW_FLAME            = -1469009,
    SAY_RAISE_SKELETONS         = -1469010,
    SAY_SLAY                    = -1469011,
    SAY_DEATH                   = -1469012,

    SAY_MAGE                    = -1469013,
    SAY_WARRIOR                 = -1469014,
    SAY_DRUID                   = -1469015,
    SAY_PRIEST                  = -1469016,
    SAY_PALADIN                 = -1469017,
    SAY_SHAMAN                  = -1469018,
    SAY_WARLOCK                 = -1469019,
    SAY_HUNTER                  = -1469020,
    SAY_ROGUE                   = -1469021,
    SPELL_SHADOW_FLAME_INITIAL  = 22986,
    SPELL_SHADOWFLAME           = 22539,
    SPELL_BELLOWING_ROAR        = 22686,
    SPELL_VEIL_OF_SHADOW        = 22687,
    SPELL_CLEAVE                = 19983,
    SPELL_TAIL_LASH             = 23364,
    SPELL_RAISE_DRAKONID        = 23362,
    SPELL_DOUBLE_ATTACK         = 19818,
    SPELL_SPEED_BURST           = 25184,

    SPELL_MAGE                  = 23410,                // wild magic
    SPELL_WARRIOR               = 23397,                // beserk
    SPELL_DRUID                 = 23398,                // cat form
    SPELL_PRIEST                = 23401,                // corrupted healing
    SPELL_PALADIN               = 23418,                // syphon blessing
    SPELL_SHAMAN                = 23425,                // totems
    SPELL_WARLOCK               = 23427,                // infernals    -> should trigger 23426
    SPELL_HUNTER                = 23436,                // bow broke
    SPELL_ROGUE                 = 23414,                // Paralise

    SPELL_FIRE_NOVA_TOTEM       = 11311,
    SPELL_STONESKIN_TOTEM       = 10405,
    SPELL_HEALING_STREAM_TOTEM  = 10461,
    SPELL_WINDFURY_TOTEM_AURA   = 10612,
    SPELL_WINDFURY_TOTEM_ATTACK = 10610,
};

enum NefarianActions
{
    NEFARIAN_PHASE_3,
    NEFARIAN_LOW_HP_YELL,
    NEFARIAN_SHADOW_FLAME,
    NEFARIAN_BELLOWING_ROAR,
    NEFARIAN_VEIL_OF_SHADOW,
    NEFARIAN_CLEAVE,
    NEFARIAN_TAIL_LASH,
    NEFARIAN_CLASS_CALL,
    NEFARIAN_ACTION_MAX,
    NEFARIAN_ATTACK_START,
};

struct boss_nefarianAI : public CombatAI
{
    boss_nefarianAI(Creature* creature) : CombatAI(creature, NEFARIAN_ACTION_MAX), m_instance(dynamic_cast<instance_blackwing_lair*>(creature->GetInstanceData()))
    {
        AddTimerlessCombatAction(NEFARIAN_PHASE_3, true);
        AddTimerlessCombatAction(NEFARIAN_LOW_HP_YELL, true);
        AddCombatAction(NEFARIAN_SHADOW_FLAME, true);
        AddCombatAction(NEFARIAN_BELLOWING_ROAR, true);
        AddCombatAction(NEFARIAN_VEIL_OF_SHADOW, true);
        AddCombatAction(NEFARIAN_CLEAVE, true);
        AddCombatAction(NEFARIAN_TAIL_LASH, true);
        AddCombatAction(NEFARIAN_CLASS_CALL, true);
        AddCustomAction(NEFARIAN_ATTACK_START, true, [&]() { HandleAttackStart(); });
        SetReactState(REACT_PASSIVE);
        m_creature->SetHover(true);
        SetMeleeEnabled(false);
        SetCombatMovement(false);
    }

    instance_blackwing_lair* m_instance;
    bool m_landed = false;

    void JustRespawned() override
    {
        CombatAI::JustRespawned();
        m_landed = false;
        DoCastSpellIfCan(nullptr, SPELL_SPEED_BURST, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        m_creature->SetWalk(false);
        StartLanding();
    }

    void KilledUnit(Unit* victim) override
    {
        if (urand(0, 4))
            return;

        DoScriptText(SAY_SLAY, m_creature, victim);
    }

    void DamageDeal(Unit* /*target*/, uint32& damage, DamageEffectType type, SpellEntry const* spell) override
    {
        if (damage && type == DIRECT_DAMAGE && !spell && m_creature->HasAura(SPELL_WINDFURY_TOTEM_AURA) &&
            !m_creature->HasAura(SPELL_WINDFURY_TOTEM_ATTACK) && roll_chance_i(20))
            m_creature->CastSpell(nullptr, SPELL_WINDFURY_TOTEM_ATTACK, TRIGGERED_OLD_TRIGGERED);
    }

    void JustDied(Unit* /*killer*/) override
    {
        DoScriptText(SAY_DEATH, m_creature);

        if (m_instance)
            m_instance->NefarianDefeated(*m_creature);
    }

    void EnterEvadeMode() override
    {
        if (m_instance)
            m_instance->NefarianEvaded(*m_creature);
        else
            CombatAI::EnterEvadeMode();
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_BONE_CONSTRUCT)
            summoned->SetCorpseDelay(5);
    }

    void StartLanding()
    {
        m_creature->SetWalk(false);
        m_creature->GetMotionMaster()->MoveWaypoint(0, FORCED_MOVEMENT_NONE, true);
    }

    void MovementInform(uint32 type, uint32 pointId) override
    {
        if (type != WAYPOINT_MOTION_TYPE)
            return;

        switch (pointId)
        {
            case 1:
                DoScriptText(SAY_AGGRO, m_creature);
                break;
            case 9:
                if (m_landed)
                    break;
                m_landed = true;
                // Stop flying and land
                m_creature->HandleEmote(EMOTE_ONESHOT_LAND);
                m_creature->SetLevitate(false);
                m_creature->SetHover(false);
                if (DoCastSpellIfCan(nullptr, SPELL_SHADOW_FLAME_INITIAL, CAST_TRIGGERED) == CAST_OK)
                    DoScriptText(SAY_SHADOW_FLAME, m_creature);
                ResetTimer(NEFARIAN_ATTACK_START, 4000);
                m_creature->GetMotionMaster()->MoveIdle();
                break;
        }
    }

    void HandleAttackStart()
    {
        Player* target = m_instance ? m_instance->StartNefarianDragon(*m_creature) : nullptr;
        if (!target)
            return;

        SetMeleeEnabled(true);
        SetReactState(REACT_AGGRESSIVE);
        m_creature->RemoveAurasDueToSpell(SPELL_SPEED_BURST);
        SetCombatMovement(true);
        m_creature->SetInCombatWithZone();
        // Make attackable and attack
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE);
        AttackStart(target);
        ResetCombatAction(NEFARIAN_SHADOW_FLAME, 12000u);
        ResetCombatAction(NEFARIAN_BELLOWING_ROAR, 30000u);
        ResetCombatAction(NEFARIAN_VEIL_OF_SHADOW, 15000u);
        ResetCombatAction(NEFARIAN_CLEAVE, 7000u);
        ResetCombatAction(NEFARIAN_TAIL_LASH, 10000u);
        ResetCombatAction(NEFARIAN_CLASS_CALL, urand(30000, 35000));
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case NEFARIAN_PHASE_3:
            {
                // Phase3 begins when we are below 20% health
                if (m_creature->GetHealthPercent() < 20.0f)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_RAISE_DRAKONID) == CAST_OK)
                    {
                        SetActionReadyStatus(action, false);
                        DoScriptText(SAY_RAISE_SKELETONS, m_creature);
                    }
                }
                break;
            }
            case NEFARIAN_LOW_HP_YELL:
            {
                if (m_creature->GetHealthPercent() < 5.0f)
                {
                    SetActionReadyStatus(action, false);
                    DoScriptText(SAY_XHEALTH, m_creature);
                }
                break;
            }
            case NEFARIAN_SHADOW_FLAME:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_SHADOWFLAME) == CAST_OK)
                    ResetCombatAction(action, 12000);
                break;
            }
            case NEFARIAN_BELLOWING_ROAR:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_BELLOWING_ROAR) == CAST_OK)
                    ResetCombatAction(action, 30000);
                break;
            }
            case NEFARIAN_VEIL_OF_SHADOW:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_VEIL_OF_SHADOW) == CAST_OK)
                    ResetCombatAction(action, 15000);
                break;
            }
            case NEFARIAN_CLEAVE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CLEAVE) == CAST_OK)
                    ResetCombatAction(action, 7000);
                break;
            }
            case NEFARIAN_TAIL_LASH:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_TAIL_LASH) == CAST_OK)
                    ResetCombatAction(action, 10000);
                break;
            }
            case NEFARIAN_CLASS_CALL:
            {
                // Cast a random class call based on what classes are currently on the hostile list
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, uint32(0), SELECT_FLAG_PLAYER))
                {
                    uint32 spellId = 0;
                    int32 textId = -1;
                    switch (target->getClass())
                    {
                        case CLASS_WARRIOR: spellId = SPELL_WARRIOR; textId = SAY_WARRIOR; break;
                        case CLASS_PALADIN: spellId = SPELL_PALADIN; textId = SAY_PALADIN; break;
                        case CLASS_HUNTER:  spellId = SPELL_HUNTER;  textId = SAY_HUNTER;  break;
                        case CLASS_ROGUE:   spellId = SPELL_ROGUE;   textId = SAY_ROGUE;   break;
                        case CLASS_PRIEST:  spellId = SPELL_PRIEST;  textId = SAY_PRIEST;  break;
                        case CLASS_SHAMAN:  spellId = SPELL_SHAMAN;  textId = SAY_SHAMAN;  break;
                        case CLASS_MAGE:    spellId = SPELL_MAGE;    textId = SAY_MAGE;    break;
                        case CLASS_WARLOCK: spellId = SPELL_WARLOCK; textId = SAY_WARLOCK; break;
                        case CLASS_DRUID:   spellId = SPELL_DRUID;   textId = SAY_DRUID;   break;
                    }
                    if (DoCastSpellIfCan(nullptr, spellId) == CAST_OK)
                    {
                        DoScriptText(textId, m_creature);
                        ResetCombatAction(action, urand(30000, 35000));
                    }
                }
                break;
            }
        }
    }
};

struct NefarianParalyze : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effect) const override
    {
        Unit* caster = spell->GetCaster();
        Unit* target = spell->GetUnitTarget();
        if (effect != EFFECT_INDEX_0 || !caster || caster->GetEntry() != NPC_NEFARIAN ||
            !target || !target->IsPlayer() || target->getClass() != CLASS_ROGUE)
            return;

        Position destination;
        caster->GetFirstCollisionPosition(destination, 5.0f, caster->GetOrientation());
        float const dx = destination.x - caster->GetPositionX();
        float const dy = destination.y - caster->GetPositionY();
        if (MaNGOS::IsValidMapCoord(destination.x, destination.y, destination.z) &&
            dx * dx + dy * dy >= 1.0f &&
            std::abs(destination.z - caster->GetPositionZ()) < 3.0f &&
            caster->IsWithinLOS(destination.x, destination.y, destination.z + 1.0f))
            target->NearTeleportTo(destination.x, destination.y, destination.z, target->GetOrientation());
    }
};

// The corrupted NPCs need to buff their encounter allies across faction-template
// boundaries. Use the native totem effect as a single aura from this totem.
class CorruptedTotemBuff : public Aura
{
public:
    CorruptedTotemBuff(SpellEntry const* spell, SpellAuraHolder* holder, Unit* target, Creature* totem)
        : Aura(spell, EFFECT_INDEX_0, nullptr, nullptr, holder, target, totem) {}
};

struct npc_nefarian_corrupted_totemAI : public ScriptedAI
{
    explicit npc_nefarian_corrupted_totemAI(Creature* creature) : ScriptedAI(creature)
    {
        SetReactState(REACT_PASSIVE);
        SetMeleeEnabled(false);
        SetCombatMovement(false);
        creature->SetImmobilizedState(true);
        switch (creature->GetEntry())
        {
            case NPC_CORRUPTED_STONESKIN_TOTEM: m_buff = SPELL_STONESKIN_TOTEM; break;
            case NPC_CORRUPTED_HEALING_TOTEM: m_buff = SPELL_HEALING_STREAM_TOTEM; break;
            case NPC_CORRUPTED_WINDFURY_TOTEM: m_buff = SPELL_WINDFURY_TOTEM_AURA; break;
        }
    }

    void JustDied(Unit* /*killer*/) override { ClearBuffs(); }
    void CorpseRemoved(uint32& /*respawnDelay*/) override { ClearBuffs(); }
    void SummonedCreatureDespawn(Creature* summon) override
    {
        if (summon == m_creature)
            ClearBuffs();
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_creature->GetEntry() == NPC_CORRUPTED_FIRE_NOVA_TOTEM)
        {
            // The five-second summon expires after the aura's first four-second pulse.
            if (!m_novaArmed && DoCastSpellIfCan(nullptr, SPELL_FIRE_NOVA_TOTEM, CAST_TRIGGERED) == CAST_OK)
                m_novaArmed = true;
            return;
        }

        if (m_updateTimer > diff)
        {
            m_updateTimer -= diff;
            return;
        }
        m_updateTimer = IN_MILLISECONDS;
        UpdateBuffs();
    }

private:
    void UpdateBuffs()
    {
        SpellEntry const* spell = sSpellTemplate.LookupEntry<SpellEntry>(m_buff);
        if (!spell)
            return;

        constexpr float radius = 20.0f;
        static std::vector<uint32> const allies = {NPC_NEFARIAN, NPC_BONE_CONSTRUCT, NPC_BLUE_DRAKONID,
            NPC_GREEN_DRAKONID, NPC_BRONZE_DRAKONID, NPC_RED_DRAKONID, NPC_BLACK_DRAKONID, NPC_CHROMATIC_DRAKONID};
        CreatureList targets;
        if (m_buff == SPELL_WINDFURY_TOTEM_AURA)
            GetCreatureListWithEntryInGrid(targets, m_creature, NPC_NEFARIAN, radius);
        else
            GetCreatureListWithEntryInGrid(targets, m_creature, allies, radius);

        GuidSet inRange;
        ObjectGuid const caster = m_creature->GetObjectGuid();
        for (Creature* target : targets)
        {
            if (!target->IsAlive())
                continue;
            if (SpellAuraHolder* holder = target->GetSpellAuraHolder(m_buff, caster))
                holder->SetAuraDuration(3 * IN_MILLISECONDS);
            else
            {
                if (target->HasAura(m_buff))
                    continue;
                holder = CreateSpellAuraHolder(spell, target, m_creature);
                holder->SetAuraMaxDuration(3 * IN_MILLISECONDS);
                holder->SetAuraDuration(3 * IN_MILLISECONDS);
                holder->AddAura(new CorruptedTotemBuff(spell, holder, target, m_creature), EFFECT_INDEX_0);
                if (!target->AddSpellAuraHolder(holder))
                {
                    delete holder;
                    continue;
                }
                holder->SetState(SPELLAURAHOLDER_STATE_READY);
            }
            inRange.insert(target->GetObjectGuid());
        }

        for (ObjectGuid guid : m_buffTargets)
            if (!inRange.count(guid))
                RemoveBuff(guid);
        m_buffTargets.swap(inRange);
    }

    void ClearBuffs()
    {
        for (ObjectGuid guid : m_buffTargets)
            RemoveBuff(guid);
        m_buffTargets.clear();
    }

    void RemoveBuff(ObjectGuid guid)
    {
        if (Unit* target = m_creature->GetMap()->GetUnit(guid))
            target->RemoveAurasByCasterSpell(m_buff, m_creature->GetObjectGuid());
    }

    GuidSet m_buffTargets;
    uint32 m_buff = 0;
    uint32 m_updateTimer = 0;
    bool m_novaArmed = false;
};

void AddSC_boss_nefarian()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_nefarian";
    pNewScript->GetAI = &GetNewAIInstance<boss_nefarianAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_nefarian_corrupted_totem";
    pNewScript->GetAI = &GetNewAIInstance<npc_nefarian_corrupted_totemAI>;
    pNewScript->RegisterSelf();

    RegisterSpellScript<NefarianParalyze>("spell_nefarian_paralyze");
}
