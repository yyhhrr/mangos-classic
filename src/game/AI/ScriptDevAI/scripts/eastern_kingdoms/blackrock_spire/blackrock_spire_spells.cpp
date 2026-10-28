/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 or any later version.
 */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "Spells/Scripts/SpellScript.h"
#include "blackrock_spire_spells.h"

namespace
{
enum
{
    NPC_SPECTRAL_ASSASSIN = 16066,
    SPELL_DEFILE = 27177,
    SPELL_DEFILE_COMPLETION = 27178,
    DEFILE_CHANNEL_TIME = 10000,
};

struct DefileChannelState : public ScriptStorage
{
    DefileChannelState(Spell* spell, uint32 time) : channel(spell->GetWeakPtr()), started(time) {}
    MaNGOS::unique_weak_ptr<Spell> channel;
    uint32 started;
    bool completed = false;
};

bool IsDefileCompletion(Spell* spell)
{
    return spell->m_triggeredBySpellInfo && spell->m_triggeredBySpellInfo->Id == SPELL_DEFILE &&
        spell->GetCaster() && spell->GetCaster()->GetEntry() == NPC_SPECTRAL_ASSASSIN;
}

// The 1.12 client supplies a ten-second stun/channel, and a separate completion
// spell containing summon-16066 and self-instakill. Do not invent periodic drain
// damage: only a complete, still-current original channel executes that payload.
struct UbrsDefile : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_0)
            return;

        if (!apply)
        {
            if (auto* state = static_cast<DefileChannelState*>(aura->GetScriptStorage()))
                state->completed = true;
            return;
        }

        Unit* caster = aura->GetCaster();
        if (!caster || caster->GetEntry() != NPC_SPECTRAL_ASSASSIN || !caster->IsInWorld())
            return;
        Spell* channel = caster->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (channel && channel->m_spellInfo->Id == SPELL_DEFILE &&
            channel->m_targets.getUnitTargetGuid() == aura->GetTarget()->GetObjectGuid())
            aura->SetScriptStorage(new DefileChannelState(channel, caster->GetMap()->GetCurrentMSTime()));
    }

    void OnPeriodicTrigger(Aura* aura, PeriodicTriggerData& data) const override
    {
        data.spellInfo = nullptr;
        auto* state = static_cast<DefileChannelState*>(aura->GetScriptStorage());
        Unit* caster = aura->GetCaster();
        Unit* victim = aura->GetTarget();
        if (!state || state->completed || aura->GetHolder()->IsDeleted() ||
            aura->GetAuraMaxDuration() != DEFILE_CHANNEL_TIME || !caster || !victim ||
            !caster->IsInWorld() || !victim->IsInWorld() || !caster->IsAlive() || !victim->IsAlive() ||
            caster->GetMap() != victim->GetMap())
            return;

        // Unsigned subtraction also handles the map millisecond clock wrapping.
        if (uint32(caster->GetMap()->GetCurrentMSTime() - state->started) < DEFILE_CHANNEL_TIME)
            return;

        auto channel = state->channel.lock();
        if (!channel || channel.get() != caster->GetCurrentSpell(CURRENT_CHANNELED_SPELL) ||
            channel->m_spellInfo->Id != SPELL_DEFILE ||
            channel->m_targets.getUnitTargetGuid() != victim->GetObjectGuid() ||
            !caster->HasChannelObject(victim->GetObjectGuid()))
            return;

        // Mark before a synchronous triggered cast can remove the victim/aura.
        state->completed = true;
        SpellCastArgs args;
        args.SetTarget(victim).SetScriptValue(victim->GetObjectGuid().GetRawValue())
            .SetDestination(victim->GetPositionX(), victim->GetPositionY(), victim->GetPositionZ());
        caster->CastSpell(args, SPELL_DEFILE_COMPLETION, TRIGGERED_OLD_TRIGGERED, nullptr, aura);
    }
};

struct UbrsDefileCompletion : public SpellScript
{
    void OnInit(Spell* spell) const override
    {
        if (IsDefileCompletion(spell))
            spell->SetEffectSkipMask(1 << EFFECT_INDEX_1);
    }

    void OnSuccessfulFinish(Spell* spell) const override
    {
        if (!IsDefileCompletion(spell))
            return;
        Unit* caster = spell->GetCaster();
        Unit* victim = spell->m_targets.getUnitTarget();
        if (!victim || !spell->GetScriptValue() ||
            victim->GetObjectGuid().GetRawValue() != spell->GetScriptValue())
            return;
        spell->SetScriptValue(0);
        if (!caster->IsInWorld() || !victim->IsInWorld() || !caster->IsAlive() ||
            !victim->IsAlive() || caster->GetMap() != victim->GetMap())
            return;

        // Native 27178's self target would kill the assassin. Its summon effect
        // remains native (assassin spawner, original five-minute lifetime), while
        // this uses EffectInstaKill's damage call on the original channel victim.
        WorldPacket data(SMSG_SPELLINSTAKILLLOG, (8 + 4));
        data << victim->GetObjectGuid();
        data << uint32(spell->m_spellInfo->Id);
        caster->SendMessageToSet(data, true);
        Unit::DealDamage(caster, victim, victim->GetHealth(), nullptr, INSTAKILL,
            SPELL_SCHOOL_MASK_NORMAL, spell->m_spellInfo, false);
    }
};
}

void AddSC_blackrock_spire_spells()
{
    RegisterSpellScript<UbrsDefile>("spell_ubrs_defile");
    RegisterSpellScript<UbrsDefileCompletion>("spell_ubrs_defile_completion");
}
