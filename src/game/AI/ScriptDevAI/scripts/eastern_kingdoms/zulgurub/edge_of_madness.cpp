/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "zulgurub.h"
#include "Entities/Item.h"
#include "Mails/Mail.h"

namespace
{
enum
{
    ITEM_GURUBASHI_MOJO_MADNESS = 19931,
};

struct EdgeOfMadnessMojo : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Player* player = GetMojoCaster(spell);
        GameObject* brazier = spell->m_targets.getGOTarget();
        if (!player || !brazier)
            return SPELL_FAILED_NOT_HERE;

        auto* instance = dynamic_cast<instance_zulgurub*>(player->GetInstanceData());
        return instance && instance->CanSummonEdgeOfMadness(*player, *brazier)
            ? SPELL_CAST_OK : SPELL_FAILED_NOT_HERE;
    }

    void OnCast(Spell* spell) const override
    {
        if (GetMojoCaster(spell))
            if (GameObject* brazier = spell->m_targets.getGOTarget())
                spell->SetScriptValue(brazier->GetObjectGuid().GetRawValue());
    }

    void OnSuccessfulFinish(Spell* spell) const override
    {
        uint64 const brazierGuid = spell->GetScriptValue();
        spell->SetScriptValue(0);
        Unit* caster = spell->GetCaster();
        if (!brazierGuid || !caster || !caster->IsPlayer())
            return;

        auto* player = static_cast<Player*>(caster);
        auto* instance = dynamic_cast<instance_zulgurub*>(player->GetInstanceData());
        GameObject* brazier = player->GetMap()->GetGameObject(ObjectGuid(brazierGuid));
        // The stock item cast uses up the real bottle before this hook runs.
        bool const paid = !spell->GetCastItem();
        if (instance && brazier)
        {
            if (paid && instance->StartEdgeOfMadnessSummon(*player, *brazier))
                return;
            instance->RestoreEdgeOfMadnessBrazier(*brazier);
        }
        if (paid)
            RefundEdgeOfMadnessSummon(player->GetObjectGuid(), player->GetMap());
    }

private:
    static Player* GetMojoCaster(Spell* spell)
    {
        Unit* caster = spell->GetCaster();
        Item* mojo = spell->GetCastItem();
        if (!caster || !caster->IsPlayer() || !mojo || mojo->GetEntry() != ITEM_GURUBASHI_MOJO_MADNESS ||
            mojo->GetOwnerGuid() != caster->GetObjectGuid() || spell->m_IsTriggeredSpell ||
            spell->m_ignoreCosts || spell->m_channelOnly)
            return nullptr;
        return static_cast<Player*>(caster);
    }
};
}

void RefundEdgeOfMadnessSummon(ObjectGuid playerGuid, Map* map)
{
    Item* mojo = Item::CreateItem(ITEM_GURUBASHI_MOJO_MADNESS, 1);
    if (!mojo)
    {
        sLog.outError("Could not create Edge of Madness summon refund for %s", playerGuid.GetString().c_str());
        return;
    }

    mojo->SaveToDB();
    MailDraft("Edge of Madness summoning material", "The ritual failed. Your Gurubashi Mojo Madness is enclosed.")
        .AddItem(mojo)
        .SendMailTo(MailReceiver(map->GetPlayer(playerGuid), playerGuid), MailSender(MAIL_NORMAL, 0u, MAIL_STATIONERY_GM));
}

void AddSC_edge_of_madness()
{
    RegisterSpellScript<EdgeOfMadnessMojo>("spell_edge_of_madness_mojo");
}
