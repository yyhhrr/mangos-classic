/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/EventAI/CreatureEventAI.h"
#include "dire_maul.h"
#include "dire_maul_ritual.h"
#include "dire_maul_ritual_creatures.h"

namespace
{
DireMaulRitual* RitualFor(WorldObject* object)
{
    if(!object||object->GetMapId()!=429)return nullptr;
    if(auto* data=static_cast<instance_dire_maul*>(object->GetInstanceData()))return &data->West().Ritual();
    return nullptr;
}
}
struct npc_dmw_jeeveeAI : public ScriptedAI
{
    explicit npc_dmw_jeeveeAI(Creature* c):ScriptedAI(c){}
    void UpdateAI(uint32 diff)override{if(auto* r=RitualFor(m_creature))r->UpdateJeevee(m_creature,diff);}
    void MovementInform(uint32 type,uint32 point)override{if(auto* r=RitualFor(m_creature))r->JeeveeReached(m_creature,type,point);}
};
struct npc_dmw_ritual_controllerAI : public Scripted_NoMovementAI
{
    explicit npc_dmw_ritual_controllerAI(Creature* c):Scripted_NoMovementAI(c){}
    void UpdateAI(uint32 diff)override{if(auto* r=RitualFor(m_creature))r->UpdateController(m_creature,diff);}
    void SummonedMovementInform(Creature* c,uint32 type,uint32 point)override{if(auto* r=RitualFor(m_creature))r->WaveMovement(c,type,point);}
    void SummonedJustReachedHome(Creature* c)override{if(auto* r=RitualFor(m_creature))r->WaveMovement(c,POINT_MOTION_TYPE,1);}
};
struct boss_dmw_dreadsteedAI : public CreatureEventAI
{
    explicit boss_dmw_dreadsteedAI(Creature* c):CreatureEventAI(c){}
    void UpdateAI(uint32 diff)override{if(auto* r=RitualFor(m_creature))r->UpdateHorse(m_creature);CreatureEventAI::UpdateAI(diff);}
    void JustDied(Unit* killer)override{if(auto* r=RitualFor(m_creature))r->OnCreatureDeath(m_creature);CreatureEventAI::JustDied(killer);}
    void JustSummoned(Creature* c)override{CreatureEventAI::JustSummoned(c);if(auto* r=RitualFor(m_creature))r->HorseSummoned(m_creature,c);}
};
struct go_dmw_ritual_nodeAI : public GameObjectAI
{
    explicit go_dmw_ritual_nodeAI(GameObject* go):GameObjectAI(go){}
    void OnUse(Unit* user,SpellEntry const*)override{if(auto* r=RitualFor(m_go))if(user&&user->IsPlayer())r->NodeUsed(m_go,static_cast<Player*>(user));}
    void OnLootStateChange(Unit*)override{if(auto* r=RitualFor(m_go))r->NodeStateChanged(m_go);}
};
UnitAI* GetAI_npc_dmw_jeevee(Creature* creature)
{
    // A null script result resumes the core's complete AIName/Permit selection,
    // preserving Scholomance path0 and every original non-429 behaviour.
    return RitualFor(creature)?new npc_dmw_jeeveeAI(creature):nullptr;
}
void AddSC_dire_maul_ritual_creatures()
{
    Script* s=new Script;s->Name="npc_dmw_jeevee";s->GetAI=&GetAI_npc_dmw_jeevee;s->RegisterSelf();
    s=new Script;s->Name="npc_dmw_ritual_controller";s->GetAI=&GetNewAIInstance<npc_dmw_ritual_controllerAI>;s->RegisterSelf();
    s=new Script;s->Name="boss_dmw_dreadsteed";s->GetAI=&GetNewAIInstance<boss_dmw_dreadsteedAI>;s->RegisterSelf();
    s=new Script;s->Name="go_dmw_ritual_node";s->GetGameObjectAI=&GetNewAIInstance<go_dmw_ritual_nodeAI>;s->RegisterSelf();
}
