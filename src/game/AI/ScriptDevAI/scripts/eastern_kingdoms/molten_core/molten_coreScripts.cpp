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
SDName: Molten_Core
SD%Complete:
SDComment:
SDCategory: Molten Core
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"/* ContentData
EndContentData */


#include "molten_core.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "Entities/Corpse.h"
#include "Entities/TemporarySpawn.h"
#include "Maps/MapPersistentStateMgr.h"

namespace
{
    constexpr uint32 NPC_LAVA_SPAWN = 12265;
    constexpr uint32 SPELL_SPLIT_LEFT = 19569;
    constexpr uint32 SPELL_SPLIT_RIGHT = 19570;
    constexpr uint32 EMOTE_LAVA_SPAWN_SPLITS = 7570;
    constexpr auto LAVA_SPAWN_SPLIT_DELAY = std::chrono::seconds(30);
    constexpr uint32 LAVA_SPAWN_LIMIT = 16;
    constexpr float LAVA_SPAWN_COUNT_RANGE = 100.0f;
    constexpr uint32 MAP_MOLTEN_CORE = 409;
    constexpr uint32 TRIGGER_MOLTEN_BRIDGE = 2886;

    uint32 CountLavaSpawns(WorldObject* source)
    {
        CreatureList spawns;
        GetCreatureListWithEntryInGrid(spawns, source, NPC_LAVA_SPAWN, LAVA_SPAWN_COUNT_RANGE);
        return std::count_if(spawns.begin(), spawns.end(), [](Creature const* spawn) { return spawn->IsAlive(); });
    }

    class MoltenCoreLavaSpawnAI;

    class MoltenCoreFirelordAI : public CreatureEventAI
    {
    public:
        explicit MoltenCoreFirelordAI(Creature* creature) : CreatureEventAI(creature) {}

        void JustSummoned(Creature* summon) override;

        void TrackSpawn(Creature const& summon)
        {
            for (auto spawn = m_spawns.begin(); spawn != m_spawns.end();)
            {
                Creature* creature = m_creature->GetMap()->GetCreature(*spawn);
                if (!creature || !creature->IsAlive())
                    spawn = m_spawns.erase(spawn);
                else
                    ++spawn;
            }
            m_spawns.insert(summon.GetObjectGuid());
        }

        void EnterEvadeMode() override
        {
            for (ObjectGuid guid : m_spawns)
                if (Creature* spawn = m_creature->GetMap()->GetCreature(guid))
                    static_cast<TemporarySpawn*>(spawn)->UnSummon();
            m_spawns.clear();
            CreatureEventAI::EnterEvadeMode();
        }

    private:
        GuidSet m_spawns;
    };

    class MoltenCoreLavaSpawnAI : public CreatureEventAI
    {
    public:
        explicit MoltenCoreLavaSpawnAI(Creature* creature) : CreatureEventAI(creature) {}

        void JoinFight(ObjectGuid firelord, Unit* target)
        {
            if (m_firelord)
                return;
            m_firelord = firelord;
            m_splitAt = m_creature->GetMap()->GetCurrentClockTime() + LAVA_SPAWN_SPLIT_DELAY;
            if (Creature* owner = m_creature->GetMap()->GetCreature(m_firelord))
                if (auto* ai = dynamic_cast<MoltenCoreFirelordAI*>(owner->AI()))
                    ai->TrackSpawn(*m_creature);
            if (target)
                AttackStart(target);
        }

        void JustSummoned(Creature* summon) override
        {
            CreatureEventAI::JustSummoned(summon);
            if (m_splitting && summon->GetEntry() == NPC_LAVA_SPAWN && summon->IsTemporarySummon())
                m_splitSummons.insert(summon->GetObjectGuid());
        }

        void EnterEvadeMode() override
        {
            if (m_firelord && m_creature->IsTemporarySummon())
                static_cast<TemporarySpawn*>(m_creature)->UnSummon();
            else
                CreatureEventAI::EnterEvadeMode();
        }

        void UpdateAI(uint32 diff) override
        {
            CreatureEventAI::UpdateAI(diff);
            Unit* target = m_creature->GetVictim();
            if (!target || !m_firelord || !m_creature->IsInCombat())
                return;

            auto const now = m_creature->GetMap()->GetCurrentClockTime();
            if (now < m_splitAt)
                return;

            m_splitAt = now + std::chrono::seconds(1);
            // Each split replaces one living spawn with two.
            if (CountLavaSpawns(m_creature) >= LAVA_SPAWN_LIMIT)
                return;

            m_splitting = true;
            DoCastSpellIfCan(m_creature, SPELL_SPLIT_LEFT, CAST_TRIGGERED);
            DoCastSpellIfCan(m_creature, SPELL_SPLIT_RIGHT, CAST_TRIGGERED);
            m_splitting = false;

            if (m_splitSummons.size() == 2)
            {
                for (ObjectGuid guid : m_splitSummons)
                    if (Creature* spawn = m_creature->GetMap()->GetCreature(guid))
                        if (auto* ai = dynamic_cast<MoltenCoreLavaSpawnAI*>(spawn->AI()))
                            ai->JoinFight(m_firelord, target);
                DoBroadcastText(EMOTE_LAVA_SPAWN_SPLITS, m_creature, nullptr, CHAT_TYPE_TEXT_EMOTE);
                static_cast<TemporarySpawn*>(m_creature)->UnSummon();
            }
            else
            {
                for (ObjectGuid guid : m_splitSummons)
                    if (Creature* spawn = m_creature->GetMap()->GetCreature(guid))
                        static_cast<TemporarySpawn*>(spawn)->UnSummon();
            }
            m_splitSummons.clear();
        }

    private:
        ObjectGuid m_firelord;
        GuidSet m_splitSummons;
        TimePoint m_splitAt{};
        bool m_splitting = false;
    };

    void MoltenCoreFirelordAI::JustSummoned(Creature* summon)
    {
        CreatureEventAI::JustSummoned(summon);
        if (summon->GetEntry() != NPC_LAVA_SPAWN || !summon->IsTemporarySummon())
            return;
        if (auto* ai = dynamic_cast<MoltenCoreLavaSpawnAI*>(summon->AI()))
        {
            Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER);
            ai->JoinFight(m_creature->GetObjectGuid(), target ? target : m_creature->GetVictim());
        }
    }

    struct SummonLavaSpawn : public SpellScript
    {
        SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
        {
            Unit* caster = spell->GetCaster();
            return caster && caster->GetMapId() == MAP_MOLTEN_CORE && CountLavaSpawns(caster) >= LAVA_SPAWN_LIMIT
                ? SPELL_FAILED_DONT_REPORT : SPELL_CAST_OK;
        }
    };

    bool AreaTrigger_at_molten_core_window(Player* player, AreaTriggerEntry const* /*trigger*/)
    {
        if (player->IsAlive() || !player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
            return false;

        Corpse const* corpse = player->GetCorpse();
        if (!corpse || corpse->GetMapId() != MAP_MOLTEN_CORE)
            return false;

        // The shortcut is only free of attunement for a ghost returning to its own lockout.
        DungeonPersistentState const* save = player->GetBoundInstanceSaveForSelfOrGroup(MAP_MOLTEN_CORE);
        if (!save || save->GetInstanceId() != corpse->GetInstanceId())
            return true;

        MapEntry const* map = sMapStore.LookupEntry(MAP_MOLTEN_CORE);
        AreaTrigger const* bridge = sObjectMgr.GetAreaTrigger(TRIGGER_MOLTEN_BRIDGE);
        if (!map || !bridge || bridge->target_mapId != MAP_MOLTEN_CORE)
            return true;

        uint32 requirement = 0;
        AreaLockStatus const status = player->GetAreaTriggerLockStatus(bridge, requirement);
        if (status != AREA_LOCKSTATUS_OK)
        {
            player->SendTransferAbortedByLockStatus(map, bridge, status, requirement);
            return true;
        }

        if (player->CheckAndRevivePlayerOnDungeonEnter(map, MAP_MOLTEN_CORE).first)
            player->TeleportTo(MAP_MOLTEN_CORE, bridge->target_X, bridge->target_Y, bridge->target_Z, bridge->target_Orientation, 0, bridge);
        return true;
    }
}

/*######
## go_molten_core_rune
######*/

bool GOUse_go_molten_core_rune(Player* /*pPlayer*/, GameObject* pGo)
{
    ScriptedInstance* pInstance = (ScriptedInstance*)pGo->GetInstanceData();

    if (!pInstance)
        return true;

    for (uint8 i = 0; i < MAX_MOLTEN_RUNES; ++i)
    {
        if (pGo->GetEntry() == m_aMoltenCoreRunes[i].m_uiRuneEntry)
        {
            // check if boss is already dead - if not return true
            if (pInstance->GetData(m_aMoltenCoreRunes[i].m_uiType) != DONE)
                return true;

            pInstance->SetData(m_aMoltenCoreRunes[i].m_uiType, SPECIAL);
            return false;
        }
    }

    return true;
}

// 19636 - Fire Blossom
struct FireBlossom : public AuraScript
{
    void OnPeriodicTrigger(Aura* /*aura*/, PeriodicTriggerData& data) const override
    {
        Creature* caster = static_cast<Creature*>(data.caster);
        if (!caster)
            return;
        Unit* target = caster->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, data.spellInfo);
        if (!target)
            return;
        data.target = target;
    }
};

void AddSC_molten_core()
{
    Script* pNewScript;

    pNewScript = new Script;
    pNewScript->Name = "go_molten_core_rune";
    pNewScript->pGOUse = &GOUse_go_molten_core_rune;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_molten_core_firelord";
    pNewScript->GetAI = &GetNewAIInstance<MoltenCoreFirelordAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_molten_core_lava_spawn";
    pNewScript->GetAI = &GetNewAIInstance<MoltenCoreLavaSpawnAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_molten_core_window";
    pNewScript->pAreaTrigger = &AreaTrigger_at_molten_core_window;
    pNewScript->RegisterSelf();

    RegisterSpellScript<FireBlossom>("spell_fire_blossom");
    RegisterSpellScript<SummonLavaSpawn>("spell_molten_core_summon_lava_spawn");
}
