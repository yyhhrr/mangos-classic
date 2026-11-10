/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/Item.h"
#include "Mails/Mail.h"

#ifdef ENABLE_PLAYERBOTS
#include "playerbot/PlayerbotAI.h"
#endif

#include <algorithm>
#include <memory>

namespace
{
enum
{
    ITEM_MUDSKUNK_LURE = 19974,
    ITEM_ZULIAN_MUDSKUNK = 19975,
    NPC_GEYSER = 14122,
    SPELL_GEYSER_SUMMON = 22421,
};

uint32 constexpr MudskunkCount = 5;

struct boss_gahzrankaAI : public CreatureEventAI
{
    explicit boss_gahzrankaAI(Creature* creature) : CreatureEventAI(creature) {}

    Unit* GetTargetByType(uint32 target, Unit* actionInvoker, Unit* eventSender, Unit* eventTarget,
        bool& isError, uint32 forSpellId, uint32 selectFlags) const override
    {
        if (forSpellId == SPELL_GEYSER_SUMMON)
        {
            std::vector<Unit*> players;
            m_creature->SelectAttackingTargets(players, ATTACKING_TARGET_ALL_SUITABLE, 0, forSpellId, selectFlags | SELECT_FLAG_PLAYER);
            if (players.size() > 1)
                players.erase(std::remove(players.begin(), players.end(), m_creature->GetVictim()), players.end());

            if (!players.empty())
                return players[urand(0, players.size() - 1)];
        }

        return CreatureEventAI::GetTargetByType(target, actionInvoker, eventSender, eventTarget, isError, forSpellId, selectFlags);
    }
};

struct spell_gahzranka_lure : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Player* caster = GetLureCaster(spell);
        if (!caster)
            return SPELL_FAILED_NOT_HERE;

        auto* instance = dynamic_cast<instance_zulgurub*>(caster->GetInstanceData());
        if (!instance || !instance->CanSummonGahzranka(*caster))
            return SPELL_FAILED_NOT_HERE;

        return caster->HasItemCount(ITEM_ZULIAN_MUDSKUNK, MudskunkCount) ? SPELL_CAST_OK : SPELL_FAILED_REAGENTS;
    }

    void OnCast(Spell* spell) const override
    {
        Player* caster = GetLureCaster(spell);
        if (!caster)
            return;

#ifdef ENABLE_PLAYERBOTS
        // The item cheat skips TakeReagents. This encounter still requires the actual fish.
        if (PlayerbotAI* bot = caster->GetPlayerbotAI())
            if (bot->HasSpellItems(spell->m_spellInfo->Id, spell->GetCastItem()))
                caster->DestroyItemCount(ITEM_ZULIAN_MUDSKUNK, MudskunkCount, true);
#endif

        // Reserve as soon as this item cast commits. Its projectile may outlive the caster.
        auto* instance = dynamic_cast<instance_zulgurub*>(caster->GetInstanceData());
        if (!instance || !instance->StartGahzrankaSummon(*caster, static_cast<uint32>(spell->GetDelayMoment())))
            RefundGahzrankaSummon(caster->GetObjectGuid(), caster->GetMap());
    }

private:
    static Player* GetLureCaster(Spell* spell)
    {
        Unit* caster = spell->GetCaster();
        Item* lure = spell->GetCastItem();
        if (!caster || !caster->IsPlayer() || !lure || lure->GetEntry() != ITEM_MUDSKUNK_LURE ||
            spell->m_IsTriggeredSpell || spell->m_ignoreCosts || spell->m_channelOnly)
            return nullptr;

        return static_cast<Player*>(caster);
    }
};

struct spell_gahzranka_geyser : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        // Knockback is executed on impact. Damage can be calculated for every target before the first impact.
        if (spell->m_spellInfo->Effect[effIdx] == SPELL_EFFECT_KNOCK_BACK)
            MarkTarget(spell);
    }

    void OnHit(Spell* spell, SpellMissInfo missInfo) const override
    {
        if (missInfo == SPELL_MISS_NONE)
            MarkTarget(spell);
        else
            spell->SetScriptValue(0);
    }

    void OnAfterHit(Spell* spell) const override
    {
        Unit* target = spell->GetUnitTarget();
        uint64 hitGuid = spell->GetScriptValue();
        spell->SetScriptValue(0);
        if (!target || !hitGuid || target->GetObjectGuid().GetRawValue() != hitGuid)
            return;

        Unit* geyser = spell->GetCaster();
        if (!geyser || geyser->GetEntry() != NPC_GEYSER)
            return;

        // Hydrospawn uses the same geyser creature and spell.
        Unit* boss = geyser->GetSpawner();
        if (boss && boss->GetEntry() == NPC_GAHZRANKA && boss->IsAlive() && boss->IsInCombat())
            boss->getThreatManager().modifyThreatPercent(target, -100);
    }

private:
    static void MarkTarget(Spell* spell)
    {
        Unit* target = spell->GetUnitTarget();
        if (target && target != spell->GetCaster())
            spell->SetScriptValue(target->GetObjectGuid().GetRawValue());
    }
};
}

void RefundGahzrankaSummon(ObjectGuid playerGuid, Map* map)
{
    std::unique_ptr<Item> lure(Item::CreateItem(ITEM_MUDSKUNK_LURE, 1));
    std::unique_ptr<Item> fish(Item::CreateItem(ITEM_ZULIAN_MUDSKUNK, MudskunkCount));
    if (!lure || !fish)
    {
        sLog.outError("Could not create Gahz'ranka summon refund for %s", playerGuid.GetString().c_str());
        return;
    }

    lure->SaveToDB();
    fish->SaveToDB();

    MailDraft draft("Gahz'ranka summoning materials", "The summoning failed. Your lure and Zulian Mudskunks are enclosed.");
    draft.AddItem(lure.release()).AddItem(fish.release());
    draft.SendMailTo(MailReceiver(map->GetPlayer(playerGuid), playerGuid),
        MailSender(MAIL_CREATURE, static_cast<uint32>(NPC_GAHZRANKA)));
}

void AddSC_boss_gahzranka()
{
    Script* script = new Script;
    script->Name = "boss_gahzranka";
    script->GetAI = &GetNewAIInstance<boss_gahzrankaAI>;
    script->RegisterSelf();

    RegisterSpellScript<spell_gahzranka_lure>("spell_gahzranka_lure");
    RegisterSpellScript<spell_gahzranka_geyser>("spell_gahzranka_geyser");
}
