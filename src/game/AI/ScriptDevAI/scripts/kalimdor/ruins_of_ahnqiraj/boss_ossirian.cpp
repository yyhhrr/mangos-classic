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
SDName: Boss_Ossirian
SD%Complete: 100%
SDComment:
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "ruins_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "Globals/ObjectMgr.h"
#include "Spells/SpellAuras.h"

enum
{
    SAY_SUPREME_1           = -1509018,
    SAY_SUPREME_2           = -1509019,
    SAY_SUPREME_3           = -1509020,
    SAY_RAND_INTRO_1        = -1509021,
    SAY_RAND_INTRO_2        = -1509023,
    SAY_RAND_INTRO_3        = -1509024,
    SAY_AGGRO               = -1509025,
    SAY_SLAY                = 11450,
    SAY_DEATH               = -1509027,

    SPELL_CURSE_OF_TONGUES  = 25195,
    SPELL_CYCLONE           = 25189,
    SPELL_STOMP             = 25188,
    SPELL_SUPREME           = 25176,
    SPELL_SUMMON_CRYSTAL    = 25192,
    SPELL_DOUBLE_ATTACK     = 19818,

    OSSIRIAN_REGULAR_CRYSTALS = 5,

    SPELL_WEAKNESS_FIRE     = 25177,
    SPELL_WEAKNESS_FROST    = 25178,
    SPELL_WEAKNESS_NATURE   = 25180,
    SPELL_WEAKNESS_ARCANE   = 25181,
    SPELL_WEAKNESS_SHADOW   = 25183,

    NPC_SAND_VORTEX         = 15428,                        // tornado npc

    ZONE_ID_RUINS_AQ        = 3429,
};

static const float aSandVortexSpawnPos[2][4] =
{
    { -9523.482f, 1880.435f, 85.645f, 5.08f},
    { -9321.39f,  1822.968f, 84.266f, 3.16f},
};

static const float aCrystalSpawnPos[3] = { -9355.75f, 1905.43f, 85.55f};
static constexpr std::array<uint32, 5> aWeaknessSpell = {SPELL_WEAKNESS_FIRE, SPELL_WEAKNESS_FROST, SPELL_WEAKNESS_NATURE, SPELL_WEAKNESS_ARCANE, SPELL_WEAKNESS_SHADOW};

static void DespawnOssirianCrystal(GameObject* crystal)
{
    crystal->Delete();
    // Delete queues removal and restores template flags in the meantime.
    crystal->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
}

void instance_ruins_of_ahnqiraj::LoadOssirianCrystalPoints()
{
    auto collect = [this](CreatureDataMap::value_type const& spawn)
    {
        CreatureData const& data = spawn.second;
        if (data.id != NPC_OSSIRIAN_TRIGGER || data.mapid != instance->GetId())
            return false;

        ObjectGuid guid(HIGHGUID_UNIT, NPC_OSSIRIAN_TRIGGER, spawn.first);
        if (data.spawntimesecsmin > 0)
            m_ossirianOpener = guid;
        else
            m_ossirianCrystalPoints.push_back(guid);
        return false;
    };
    sObjectMgr.DoCreatureData(collect);
    m_ossirianPointsLoaded = true;
}

bool instance_ruins_of_ahnqiraj::SpawnOssirianCrystal(ObjectGuid triggerGuid)
{
    Creature* trigger = instance->GetCreature(triggerGuid);
    if (!trigger)
    {
        if (CreatureData const* data = sObjectMgr.GetCreatureData(triggerGuid.GetCounter()))
        {
            if (!instance->IsLoaded(data->posX, data->posY))
                instance->LoadGrid(data->posX, data->posY);
            trigger = instance->GetCreature(triggerGuid);
        }
        if (!trigger)
            return false;
    }

    if (!trigger->IsAlive())
    {
        // Older instances can still have a despawn timer from a previous use.
        trigger->Respawn();
        return false;
    }

    m_creatingOssirianCrystal = true;
    m_ossirianCreatedCrystal.Clear();
    trigger->CastSpell(nullptr, SPELL_SUMMON_CRYSTAL, TRIGGERED_OLD_TRIGGERED);
    m_creatingOssirianCrystal = false;

    GameObject* crystal = instance->GetGameObject(m_ossirianCreatedCrystal);
    if (!crystal || crystal->GetSpawnerGuid() != triggerGuid)
        return false;

    if (GetData(TYPE_OSSIRIAN) == IN_PROGRESS)
        crystal->SetActiveObjectState(true);
    else
        crystal->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);

    m_ossirianCrystals.emplace(triggerGuid, OssirianCrystal{crystal->GetObjectGuid()});
    return true;
}

void instance_ruins_of_ahnqiraj::BeginOssirianCrystals()
{
    m_ossirianOpeningTimer = 10000;
    auto opener = m_ossirianCrystals.find(m_ossirianOpener);
    if (opener != m_ossirianCrystals.end())
        if (GameObject* crystal = instance->GetGameObject(opener->second.gameObject))
        {
            crystal->RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_NO_INTERACT);
            crystal->SetActiveObjectState(true);
        }
}

void instance_ruins_of_ahnqiraj::ClearOssirianCrystals()
{
    for (auto const& [guid, crystal] : m_ossirianCrystals)
    {
        if (crystal.weaknessSpell)
            if (Creature* trigger = instance->GetCreature(guid))
                trigger->InterruptNonMeleeSpells(false, crystal.weaknessSpell);
        if (GameObject* go = instance->GetGameObject(crystal.gameObject))
            DespawnOssirianCrystal(go);
    }

    m_ossirianCrystals.clear();
    m_ossirianOpenerUsed = false;
    m_ossirianOpeningTimer = 0;
    m_ossirianRegularGoal = 0;
    instance->SetWeather(ZONE_ID_RUINS_AQ, WEATHER_TYPE_FINE, 0.0f, false);
}

void instance_ruins_of_ahnqiraj::UpdateOssirianCrystals(uint32 diff)
{
    uint32 const state = GetData(TYPE_OSSIRIAN);
    if (state == DONE)
        return;

    if (state == IN_PROGRESS && !m_ossirianPointsLoaded)
        LoadOssirianCrystalPoints();

    GuidSet spentPoints;
    uint32 available = 0;
    for (auto it = m_ossirianCrystals.begin(); it != m_ossirianCrystals.end();)
    {
        auto const& [guid, crystal] = *it;
        GameObject* go = instance->GetGameObject(crystal.gameObject);
        if (crystal.weaknessSpell)
        {
            Creature* trigger = instance->GetCreature(guid);
            Spell* cast = trigger ? trigger->FindCurrentSpellBySpellId(crystal.weaknessSpell) : nullptr;
            if (cast && cast->getState() != SPELL_STATE_FINISHED)
            {
                ++it;
                continue;
            }

            spentPoints.insert(guid);
            if (go)
                DespawnOssirianCrystal(go);
        }
        else if (go)
        {
            if (guid != m_ossirianOpener)
                ++available;
            ++it;
            continue;
        }

        it = m_ossirianCrystals.erase(it);
    }

    if (m_ossirianOpener && !m_ossirianOpenerUsed && !m_ossirianCrystals.count(m_ossirianOpener))
        SpawnOssirianCrystal(m_ossirianOpener);

    if (state != IN_PROGRESS)
        return;

    if (m_ossirianOpeningTimer)
    {
        if (m_ossirianOpeningTimer > diff)
            m_ossirianOpeningTimer -= diff;
        else
        {
            m_ossirianOpeningTimer = 0;
            m_ossirianRegularGoal = OSSIRIAN_REGULAR_CRYSTALS;
        }
    }

    if (available >= m_ossirianRegularGoal)
        return;

    GuidVector candidates = m_ossirianCrystalPoints;
    std::shuffle(candidates.begin(), candidates.end(), *GetRandomGenerator());
    for (ObjectGuid guid : candidates)
    {
        if (m_ossirianCrystals.count(guid) || spentPoints.count(guid))
            continue;
        if (SpawnOssirianCrystal(guid) && ++available >= m_ossirianRegularGoal)
            break;
    }
}

void instance_ruins_of_ahnqiraj::UseOssirianCrystal(GameObject* crystal)
{
    if (GetData(TYPE_OSSIRIAN) != IN_PROGRESS)
        return;

    ObjectGuid triggerGuid = crystal->GetSpawnerGuid();
    auto point = m_ossirianCrystals.find(triggerGuid);
    if (point == m_ossirianCrystals.end() || point->second.gameObject != crystal->GetObjectGuid() || point->second.weaknessSpell)
        return;

    Creature* trigger = instance->GetCreature(triggerGuid);
    if (!trigger || !trigger->IsAlive())
        return;

    // Lock immediately. The 5-second native cast determines when to close the GO,
    // and the replacement does not depend on whether the boss is in its radius.
    uint32 spellId = aWeaknessSpell[urand(0, aWeaknessSpell.size() - 1)];
    point->second.weaknessSpell = spellId;
    crystal->SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
    crystal->SetGoState(GO_STATE_ACTIVE);
    crystal->SendGameObjectCustomAnim(crystal->GetObjectGuid());

    if (triggerGuid == m_ossirianOpener)
    {
        m_ossirianOpenerUsed = true;
        m_ossirianRegularGoal = std::max(m_ossirianRegularGoal, 1u);
    }

    trigger->CastSpell(nullptr, spellId, TRIGGERED_NONE);
}

bool instance_ruins_of_ahnqiraj::IsOssirianCrystalCasting(ObjectGuid trigger, uint32 spellId) const
{
    auto crystal = m_ossirianCrystals.find(trigger);
    return GetData(TYPE_OSSIRIAN) == IN_PROGRESS && crystal != m_ossirianCrystals.end() && crystal->second.weaknessSpell == spellId;
}

enum OssirianActions
{
    OSSIRIAN_CYCLONE,
    OSSIRIAN_STOMP,
    OSSIRIAN_CURSE_OF_TONGUES,
    OSSIRIAN_SPEEDUP,
    OSSIRIAN_ACTION_MAX,
};

struct boss_ossirianAI : public CombatAI
{
    boss_ossirianAI(Creature* creature) :
        CombatAI(creature, OSSIRIAN_ACTION_MAX),
        m_instance(static_cast<instance_ruins_of_ahnqiraj*>(m_creature->GetInstanceData()))
    {
        AddCombatAction(OSSIRIAN_CYCLONE, 20000u);
        AddCombatAction(OSSIRIAN_STOMP, 30000u);
        AddCombatAction(OSSIRIAN_CURSE_OF_TONGUES, 30000u);
        AddCombatAction(OSSIRIAN_SPEEDUP, 10000u);
        AddOnKillText(SAY_SLAY);
    }

    instance_ruins_of_ahnqiraj* m_instance;

    bool m_saidIntro = false;
    bool m_weakened = false;

    void Reset() override
    {
        CombatAI::Reset();
        m_weakened = false;

        m_creature->UpdateSpeed(MOVE_RUN, false, 1.0f);

        DoCastSpellIfCan(nullptr, SPELL_DOUBLE_ATTACK, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
        DoCastSpellIfCan(nullptr, SPELL_SUPREME, CAST_TRIGGERED | CAST_AURA_NOT_PRESENT);
    }

    void Aggro(Unit* /*who*/) override
    {
        DoScriptText(SAY_AGGRO, m_creature);

        for (auto aSandVortexSpawnPo : aSandVortexSpawnPos)
            m_creature->SummonCreature(NPC_SAND_VORTEX, aSandVortexSpawnPo[0], aSandVortexSpawnPo[1], aSandVortexSpawnPo[2], aSandVortexSpawnPo[3], TEMPSPAWN_CORPSE_DESPAWN, 0);

        if (m_instance)
            m_instance->instance->SetWeather(ZONE_ID_RUINS_AQ, WEATHER_TYPE_STORM, 1.0f, true);
    }

    void JustDied(Unit* killer) override
    {
        CombatAI::JustDied(killer);
        DoScriptText(SAY_DEATH, m_creature);
    }

    void JustSummoned(Creature* summoned) override
    {
        if (summoned->GetEntry() == NPC_SAND_VORTEX)
            summoned->GetMotionMaster()->MoveRandomAroundPoint(aCrystalSpawnPos[0], aCrystalSpawnPos[1], aCrystalSpawnPos[2], 100.0f);
    }

    void SpellHit(Unit* caster, const SpellEntry* spellInfo) override
    {
        if (!caster || !m_instance || !m_instance->IsOssirianCrystalCasting(caster->GetObjectGuid(), spellInfo->Id))
            return;

        // SpellHit also runs on a miss. An older aura from a reused point
        // cannot be mistaken for one applied by the cast that just finished.
        SpellAuraHolder const* weakness = m_creature->GetSpellAuraHolder(spellInfo->Id, caster->GetObjectGuid());
        if (!weakness || weakness->GetAuraDuration() != weakness->GetAuraMaxDuration())
            return;

        m_creature->RemoveAurasDueToSpell(SPELL_SUPREME);
        m_weakened = true;
    }

    void UpdateAI(uint32 diff) override
    {
        if (m_weakened && m_creature->IsInCombat() &&
            std::none_of(aWeaknessSpell.begin(), aWeaknessSpell.end(), [this](uint32 spellId) { return m_creature->HasAura(spellId); }))
        {
            if (m_creature->HasAura(SPELL_SUPREME))
                m_weakened = false;
            else if (DoCastSpellIfCan(nullptr, SPELL_SUPREME, CAST_TRIGGERED) == CAST_OK)
            {
                m_weakened = false;
                static constexpr int32 supremeTexts[] = { SAY_SUPREME_1, SAY_SUPREME_2, SAY_SUPREME_3 };
                DoScriptText(supremeTexts[urand(0, 2)], m_creature);
            }
        }

        CombatAI::UpdateAI(diff);
    }

    void MoveInLineOfSight(Unit* who) override
    {
        // TODO: Range guesswork
        if (!m_saidIntro && who->GetTypeId() == TYPEID_PLAYER && m_creature->IsWithinDistInMap(who, 75.0f, false))
        {
            switch (urand(0, 2))
            {
                case 0: DoScriptText(SAY_RAND_INTRO_1, m_creature); break;
                case 1: DoScriptText(SAY_RAND_INTRO_2, m_creature); break;
                case 2: DoScriptText(SAY_RAND_INTRO_3, m_creature); break;
            }
            m_saidIntro = true;
        }
        CombatAI::MoveInLineOfSight(who);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case OSSIRIAN_CYCLONE:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_CYCLONE) == CAST_OK)
                    ResetCombatAction(action, 20000);
                break;
            }
            case OSSIRIAN_STOMP:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_STOMP) == CAST_OK)
                    ResetCombatAction(action, 30000);
                break;
            }
            case OSSIRIAN_CURSE_OF_TONGUES:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_CURSE_OF_TONGUES) == CAST_OK)
                    ResetCombatAction(action, urand(20000, 30000));
                break;
            }
            case OSSIRIAN_SPEEDUP:
            {
                m_creature->UpdateSpeed(MOVE_RUN, false, 2.2f);
                DisableCombatAction(action);
                break;
            }
        }
    }
};

bool GOUse_go_ossirian_crystal(Player* /*player*/, GameObject* go)
{
    if (auto* instance = dynamic_cast<instance_ruins_of_ahnqiraj*>(go->GetInstanceData()))
    {
        instance->UseOssirianCrystal(go);
        return true;
    }

    return false;
}

void AddSC_boss_ossirian()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_ossirian";
    pNewScript->GetAI = &GetNewAIInstance<boss_ossirianAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "go_ossirian_crystal";
    pNewScript->pGOUse = &GOUse_go_ossirian_crystal;
    pNewScript->RegisterSelf();
}
