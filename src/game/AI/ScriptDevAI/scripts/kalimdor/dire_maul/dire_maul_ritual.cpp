/* This file is part of the ScriptDev2 Project, licensed under GPL version2. */
#include "AI/ScriptDevAI/include/sc_common.h"
#include "dire_maul_ritual.h"
#include "dire_maul_ritual_data.h"
#include "Entities/TemporarySpawn.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <sstream>

namespace
{
template<class T> bool RitualNumber(std::istream& in, T& result, uint64 maximum)
{
    std::string s;
    if (!(in >> s) || s.empty()) return false;
    uint64 n = 0;
    for (char c : s)
    {
        if (c < '0' || c > '9' || n > maximum / 10 || (n == maximum / 10 && uint64(c - '0') > maximum % 10)) return false;
        n = n * 10 + uint64(c - '0');
    }
    result = T(n); return true;
}
uint32 RitualBits(float value) { uint32 bits; std::memcpy(&bits, &value, sizeof bits); return bits; }
float RitualFloat(uint32 bits) { float value; std::memcpy(&value, &bits, sizeof value); return value; }
std::array<uint32, 4> RitualPosition(Creature* actor)
{
    return {{RitualBits(actor->GetPositionX()), RitualBits(actor->GetPositionY()), RitualBits(actor->GetPositionZ()), RitualBits(actor->GetOrientation())}};
}
void RitualDespawn(Map* map, ObjectGuid guid)
{
    if (Creature* actor = map->GetCreature(guid)) if (actor->IsAlive()) actor->ForcedDespawn();
}
}

DireMaulRitual::DireMaulRitual(ScriptedInstance& owner, std::function<void()> save) : m_owner(owner), m_save(std::move(save)) {}
uint64 DireMaulRitual::Now() { return uint64(time(nullptr)) * 1000; }
uint32 DireMaulRitual::GetData() const { return m_progress.encounter; }
void DireMaulRitual::Commit() { if (m_save) m_save(); }
uint32 DireMaulRitual::Entry(uint32 i)
{
    static uint32 const entries[] = {14501, 14500, 14502, 14506, 14504};
    return i < WAVE_FIRST ? entries[i] : dmrWaveSpawns[i - WAVE_FIRST].entry;
}
uint32 DireMaulRitual::Lifetime(uint32 i)
{
    return i == HORSE ? dmrBossSpawn.lifetime : i == LORD ? 30000 : i == SPIRIT ? dmrSpiritLifetime : i < WAVE_FIRST ? 0 : dmrWaveSpawns[i - WAVE_FIRST].lifetime;
}
std::string DireMaulRitual::Encode(Progress const& p)
{
    std::ostringstream out;
    out << "R1 " << p.legacy << ' ' << p.generation << ' ' << p.phase << ' ' << p.wave << ' ' << p.wavePending << ' ' << p.timer << ' ' << p.deactivate << ' ' << p.elapsed << ' ' << p.dialogue
        << ' ' << p.jeeveeNode << ' ' << p.jeeveeMoving << ' ' << p.jeeveeElapsed << ' ' << p.jeeveeActions << ' ' << p.jeeveeWatchdog << ' ' << p.born << ' ' << p.active;
    for (uint32 i = 0; i < 3; ++i) out << ' ' << p.nodeUntil[i] << ' ' << p.pulse[i];
    out << ' ' << p.circleUntil << ' ' << p.portalUntil << ' ' << p.bossPortalUntil;
    for (uint64 until : p.symbolUntil) out << ' ' << until;
    for (Actor const& a : p.actors)
    {
        out << ' ' << a.state << ' ' << a.until;
        for (uint32 bits : a.position) out << ' ' << bits;
    }
    return out.str();
}
std::string DireMaulRitual::Save() const { return Encode(m_progress); }
bool DireMaulRitual::Decode(uint32 encounter, std::string const& text, Progress& result)
{
    // 138 receipts of at most 60 bytes plus scene metadata fit below 10 KiB.
    // Keep the bound below the enclosing West frame's 24 KiB limit.
    if (encounter > SPECIAL || text.size() > 12000) return false;
    Progress p; p.encounter = encounter;
    if (text.empty()) { if (encounter == IN_PROGRESS) p.encounter = NOT_STARTED; result = p; return true; }
    std::istringstream in(text); std::string version;
    if (!(in >> version) || version != "R1") return false;
    if (!RitualNumber(in,p.legacy,1) || !RitualNumber(in,p.generation,1000000) || !RitualNumber(in,p.phase,FAILED) || !RitualNumber(in,p.wave,9) || !RitualNumber(in,p.wavePending,1) ||
        !RitualNumber(in,p.timer,48000) || !RitualNumber(in,p.deactivate,70000) || !RitualNumber(in,p.elapsed,41000) || !RitualNumber(in,p.dialogue,3) ||
        !RitualNumber(in,p.jeeveeNode,5) || !RitualNumber(in,p.jeeveeMoving,1) || !RitualNumber(in,p.jeeveeElapsed,30000) || !RitualNumber(in,p.jeeveeActions,31) || !RitualNumber(in,p.jeeveeWatchdog,60000) ||
        !RitualNumber(in,p.born,7) || !RitualNumber(in,p.active,7)) return false;
    if (p.active & ~p.born) return false;
    uint64 now = Now();
    for (uint32 i = 0; i < 3; ++i)
    {
        if (!RitualNumber(in,p.nodeUntil[i],4102444800000ULL) || p.nodeUntil[i] > now + 900000 || !RitualNumber(in,p.pulse[i],6000)) return false;
        if (bool(p.nodeUntil[i]) != bool(p.born & (1u << i)) || bool(p.pulse[i]) != bool(p.active & (1u << i))) return false;
    }
    if (!RitualNumber(in,p.circleUntil,4102444800000ULL) || !RitualNumber(in,p.portalUntil,4102444800000ULL) || !RitualNumber(in,p.bossPortalUntil,4102444800000ULL) ||
        p.circleUntil > now + 900000 || p.portalUntil > now + 380000 || p.bossPortalUntil > now + 15000) return false;
    for (uint64& until : p.symbolUntil) if (!RitualNumber(in,until,4102444800000ULL) || until > now + 900000) return false;
    for (uint32 i = 0; i < ACTORS; ++i)
    {
        Actor& a = p.actors[i];
        if (!RitualNumber(in,a.state,EXPIRED) || !RitualNumber(in,a.until,4102444800000ULL)) return false;
        for (uint32& bits : a.position) if (!RitualNumber(in,bits,4294967295ULL)) return false;
        for (uint32 bits : a.position) if (!std::isfinite(RitualFloat(bits)) || std::fabs(RitualFloat(bits)) > 10000.0f) return false;
        if ((a.state == EMPTY || a.state == PENDING) && a.until) return false;
        if ((!Lifetime(i) && a.until) || a.until > now + Lifetime(i)) return false;
        if (a.state == ALIVE && Lifetime(i) && !a.until) return false;
        if (i >= WAVE_FIRST)
        {
            uint32 wave = 0;
            while (wave < 9 && i - WAVE_FIRST >= dmrWaves[wave].first + dmrWaves[wave].count) ++wave;
            if (wave >= p.wave + p.wavePending && a.state != EMPTY) return false;
            if (wave < p.wave && a.state == EMPTY) return false;
        }
    }
    in >> std::ws; if (!in.eof()) return false;
    if (p.legacy)
    {
        Progress empty; empty.encounter = encounter;
        if (Encode(p) != Encode(empty)) return false;
    }
    else
    {
        if (!p.generation || p.phase == IDLE) return false;
        uint32 state = p.phase == PREPARING || p.phase == WAVES ? IN_PROGRESS : p.phase == WAIT_GLYPH || p.phase == WAIT_HORSE ? SPECIAL : p.phase == FAILED ? FAIL : DONE;
        if (encounter != state || (p.wavePending && ((p.phase != WAVES && p.phase != FAILED) || p.wave == 9))) return false;
        if (p.phase == PREPARING && (p.wave || p.wavePending || p.timer > 48000)) return false;
        if (p.phase == WAVES && p.timer > 45000) return false;
        if (p.phase == WAIT_HORSE && p.timer > 7000) return false;
        if (p.phase != PREPARING && p.phase != WAVES && p.deactivate) return false;
        if (p.phase >= WAIT_GLYPH && p.phase <= FINISHED && p.wave != 9) return false;
        if (p.phase == HORSE_ACTIVE && p.actors[HORSE].state != ALIVE) return false;
        if (p.phase == FINISHED && p.actors[HORSE].state != KILLED && p.actors[HORSE].state != EXPIRED) return false;
        if (p.actors[SPIRIT].state != EMPTY && p.actors[HORSE].state != KILLED) return false;
        if (p.phase==PREPARING || p.phase==WAVES || p.phase==WAIT_HORSE)
            if (p.actors[CONTROLLER].state!=PENDING && p.actors[CONTROLLER].state!=ALIVE) return false;
        if (p.phase == FAILED)
            for (Actor const& a : p.actors) if (a.state == ALIVE || a.state == PENDING) return false;
    }
    result = p; return true;
}
bool DireMaulRitual::ValidateSave(uint32 state, std::string const& text) { Progress p; return Decode(state,text,p); }
bool DireMaulRitual::Load(uint32 state, std::string const& text)
{
    Progress next;
    if (!Decode(state,text,next)) return false;
    Progress old = m_progress;
    if (next.generation == old.generation && next.generation)
        for (uint32 i = 0; i < ACTORS; ++i) if (next.actors[i].state == old.actors[i].state && next.actors[i].until == old.actors[i].until && next.actors[i].position == old.actors[i].position) next.actors[i].guid = old.actors[i].guid;
    m_progress = next; m_paused = false; m_sceneAttempts = 0;
    for (uint32 i = 0; i < ACTORS; ++i) if (old.actors[i].guid != next.actors[i].guid) RitualDespawn(m_owner.instance,old.actors[i].guid);
    SyncObjects(); return true;
}
bool DireMaulRitual::Current(Creature* actor, uint32 slot) const
{
    return actor && actor->GetMap() == m_owner.instance && actor->GetEntry() == Entry(slot) && actor->GetObjectGuid() == m_progress.actors[slot].guid && m_owner.instance->GetCreature(actor->GetObjectGuid()) == actor;
}
bool DireMaulRitual::IsCurrentJeevee(Creature* actor) const { return Current(actor,JEEVEE); }
bool DireMaulRitual::IsCurrentController(Creature* actor) const { return Current(actor,CONTROLLER); }
bool DireMaulRitual::IsCurrentHorse(Creature* actor) const { return Current(actor,HORSE); }
bool DireMaulRitual::PlayerHere(Player* p) const { return p && p->IsInWorld() && p->GetMap() == m_owner.instance && !p->IsGameMaster(); }
Player* DireMaulRitual::Participant() const
{
    if (Player* p = m_owner.instance->GetPlayer(m_lastUser)) if (PlayerHere(p)&&p->IsAlive()) return p;
    for (auto const& r : m_owner.instance->GetPlayers()) if (Player* p = r.getSource()) if (PlayerHere(p)&&p->IsAlive()) return p;
    // Corpses still count as an occupied map, as in the original failure
    // check. Prefer a living participant for effects when one is available.
    for (auto const& r : m_owner.instance->GetPlayers()) if (Player* p = r.getSource()) if (PlayerHere(p)) return p;
    return nullptr;
}
void DireMaulRitual::ClearLiving(bool expire)
{
    std::array<ObjectGuid,ACTORS> guids{};
    for (uint32 i = 0; i < ACTORS; ++i)
    {
        Actor& a = m_progress.actors[i]; guids[i] = a.guid; a.guid.Clear();
        if (expire && (a.state == ALIVE || a.state == PENDING)) a.state = EXPIRED;
    }
    // The two old entry-wide linking rows are removed by the guarded migration.
    // Only these actual living identities are ours; dead loot corpses remain.
    for (ObjectGuid guid : guids) RitualDespawn(m_owner.instance,guid);
}
void DireMaulRitual::Pause() { if (!m_paused) { m_paused = true; ClearLiving(false); Commit(); } }
void DireMaulRitual::Fail()
{
    if (m_progress.legacy || m_progress.phase == FAILED || GetData() == DONE) return;
    ClearLiving(true); m_progress.phase = FAILED; m_progress.encounter = FAIL;
    // Retain whether the current wave had already requested its fixed slots;
    // those now-EXPIRED receipts belong to this failed attempt, not a future
    // unrequested wave. They are never alive or recreated after FAIL.
    m_progress.timer = m_progress.deactivate = 0;
    m_progress.active = m_progress.born = 0; m_progress.nodeUntil.fill(0); m_progress.pulse.fill(0);
    m_progress.circleUntil = m_progress.portalUntil = m_progress.bossPortalUntil = 0; m_progress.symbolUntil.fill(0);
    SyncObjects(); Commit();
}
void DireMaulRitual::SetData(uint32 state)
{
    if (state > SPECIAL || state == GetData()) return;
    if (state == FAIL && !m_progress.legacy) { Fail(); return; }
    // External legacy state setters may describe historical completion, never
    // fabricate the missing dynamic actors or reset an acknowledged encounter.
    if (!m_progress.legacy) return;
    m_progress.encounter = state; Commit();
}
bool DireMaulRitual::Start(Player* player)
{
    if (!PlayerHere(player) || !player->IsAlive() || (GetData() != NOT_STARTED && GetData() != FAIL)) return false;
    uint32 generation = m_progress.generation;
    if (generation >= 1000000) return false;
    ClearLiving(true); m_progress = Progress(); m_progress.legacy = 0; m_progress.generation = generation + 1;
    m_progress.encounter = IN_PROGRESS; m_progress.phase = PREPARING; m_progress.timer = 48000; m_progress.deactivate = 70000;
    m_progress.actors[CONTROLLER].state = m_progress.actors[JEEVEE].state = PENDING;
    m_lastUser = player->GetObjectGuid(); m_paused = false; m_sceneAttempts = 0;
    Commit(); ProcessActors(); return true;
}
bool DireMaulRitual::RequestDreadsteed(Player* player)
{
    if (!PlayerHere(player) || !player->IsAlive()) return false;
    m_lastUser = player->GetObjectGuid();
    if (!m_progress.legacy && m_progress.phase == WAIT_HORSE)
    {
        for (Actor& a : m_progress.actors) { a.attempts = 0; a.retry = 0; }
        return false;
    }
    if (GetData() != SPECIAL || (!m_progress.legacy && m_progress.phase != WAIT_GLYPH)) return false;
    // Preserve both original event8428 command31 guards, including actors
    // outside this ledger: block a duplicate, never adopt or despawn it.
    if (GetClosestCreatureWithEntry(player,14502,150.0f) || GetClosestCreatureWithEntry(player,14504,150.0f)) return false;
    // A legacy SPECIAL legitimately completed the ritual, but carries no actor
    // history. Continue this pending glyph only; legacy DONE never makes rewards.
    if (m_progress.legacy)
    {
        m_progress.legacy = 0; m_progress.generation = 1; m_progress.wave = 9;
        for (uint32 i = WAVE_FIRST; i < ACTORS; ++i) m_progress.actors[i].state = EXPIRED;
    }
    m_progress.phase = WAIT_HORSE; m_progress.timer = 7000; m_progress.dialogue = 0;
    m_progress.actors[CONTROLLER].state = PENDING; m_progress.actors[CONTROLLER].until = 0;
    m_paused = false; m_sceneAttempts = 0; Commit(); ProcessActors(); return true;
}
void DireMaulRitual::Acknowledge(uint32 slot, Creature* actor)
{
    Actor& a = m_progress.actors[slot];
    if (!actor || actor->GetEntry() != Entry(slot) || actor->GetMap() != m_owner.instance || m_owner.instance->GetCreature(actor->GetObjectGuid()) != actor || (a.state != PENDING && a.state != ALIVE)) return;
    if (a.guid && a.guid != actor->GetObjectGuid()) return;
    bool first = a.state == PENDING;
    if (first) { a.until = Lifetime(slot) ? Now() + Lifetime(slot) : 0; a.position = RitualPosition(actor); }
    a.guid = actor->GetObjectGuid(); a.state = ALIVE; a.attempts = 0; a.retry = 0;
    if (actor->IsTemporarySummon()) static_cast<TemporarySpawn*>(actor)->SetSummonProperties(TEMPSPAWN_DEAD_DESPAWN,0);
    if (slot == CONTROLLER || slot == JEEVEE) actor->SetActiveObjectState(true);
    if (slot == JEEVEE && !first)
    {
        if (m_progress.jeeveeNode)
        {
            auto const& p=dmrJeeveePath[m_progress.jeeveeNode-1];
            actor->NearTeleportTo(p.x,p.y,p.z,p.o<6.3f?p.o:actor->GetOrientation());
        }
        if (m_progress.jeeveeMoving) { m_progress.jeeveeMoving=0;AdvanceJeevee(actor); }
    }
    if (slot == HORSE && m_progress.phase == WAIT_HORSE)
    {
        m_progress.phase = HORSE_ACTIVE; m_progress.encounter = DONE; m_progress.timer = 0;
        Actor& controller = m_progress.actors[CONTROLLER]; ObjectGuid guid = controller.guid; controller.guid.Clear(); controller.state = EXPIRED;
        RitualDespawn(m_owner.instance,guid);
    }
    if (slot >= WAVE_FIRST)
    {
        actor->SetRespawnCoord(-38.5f,811.8f,-29.5f,0.0f);
        actor->GetMotionMaster()->MovePoint(0,(actor->GetPositionX()-38.5f)/2.0f,(actor->GetPositionY()+811.8f)/2.0f,-32.0f);
    }
    Commit();
}
bool DireMaulRitual::Spawn(uint32 slot)
{
    Actor& a = m_progress.actors[slot];
    if (a.guid || (a.state != ALIVE && a.state != PENDING) || a.attempts >= 5 || a.retry > Now() || !Participant()) return false;
    if (a.state == ALIVE && a.until && a.until <= Now()) { a.state = EXPIRED; Commit(); return false; }
    WorldObject* source = m_owner.instance->GetCreature(m_progress.actors[CONTROLLER].guid);
    // Wave movement callbacks belong to the actual controller. A player is
    // not an interchangeable source while its recreation is still pending.
    if (slot>=WAVE_FIRST&&!source) return false;
    if (slot<WAVE_FIRST) source=Participant();
    ++a.attempts; a.retry = Now() + 1000;
    if (!source) source = Participant();
    float x,y,z,o;
    if (a.state == ALIVE || slot == LORD || slot == SPIRIT)
    {
        x=RitualFloat(a.position[0]); y=RitualFloat(a.position[1]); z=RitualFloat(a.position[2]); o=RitualFloat(a.position[3]);
    }
    else if (slot == HORSE) { x=dmrBossSpawn.x; y=dmrBossSpawn.y; z=dmrBossSpawn.z; o=dmrBossSpawn.o; }
    else if (slot < WAVE_FIRST) { auto const& p=dmrJeeveePath[0]; x=p.x; y=p.y; z=p.z; o=4.81711f; }
    else { auto const& p=dmrWaveSpawns[slot-WAVE_FIRST]; x=p.x; y=p.y; z=p.z; o=p.o; }
    if (slot == LORD && a.state == PENDING)
        if (Creature* horse=m_owner.instance->GetCreature(m_progress.actors[HORSE].guid)) { source=horse; x=y=z=o=0.0f; }
    if (slot == SPIRIT)
        if (Creature* horse=m_owner.instance->GetCreature(m_progress.actors[HORSE].guid)) source=horse;
    if (!source) return false;
    Creature* actor=source->SummonCreature(Entry(slot),x,y,z,o,TEMPSPAWN_DEAD_DESPAWN,0,slot==CONTROLLER||slot==JEEVEE,slot==JEEVEE);
    if (actor)
    {
        Acknowledge(slot,actor);
        if (slot == LORD) if (Creature* horse=m_owner.instance->GetCreature(m_progress.actors[HORSE].guid)) if (Unit* target=horse->GetVictim()) actor->AI()->AttackStart(target);
        return true;
    }
    if (a.attempts == 5) script_error_log("Dire Maul ritual: entry%u slot%u needs explicit retry after five failed creates",Entry(slot),slot);
    return false;
}
void DireMaulRitual::ProcessActors()
{
    bool changed=false;
    for (uint32 i=0;i<ACTORS;++i)
    {
        Actor& a=m_progress.actors[i]; if (a.state!=PENDING && a.state!=ALIVE) continue;
        if (a.guid)
        {
            Creature* actor=m_owner.instance->GetCreature(a.guid);
            if (!actor)
            {
                a.guid.Clear(); changed=true;
                if(i==CONTROLLER)
                    for(uint32 j=WAVE_FIRST;j<ACTORS;++j)
                        if(m_owner.instance->GetCreature(m_progress.actors[j].guid)){Fail();return;}
            }
            else if (!actor->IsAlive()) { OnCreatureDeath(actor); continue; }
            else if (Lifetime(i) && i!=SPIRIT && (actor->IsInCombat()||actor->GetCharmer())) a.until=Now()+Lifetime(i);
            else if (a.until && a.until<=Now())
            {
                ObjectGuid guid=a.guid; a.guid.Clear(); a.state=EXPIRED; RitualDespawn(m_owner.instance,guid); changed=true;
                if (i==HORSE && m_progress.phase==HORSE_ACTIVE) m_progress.phase=FINISHED;
            }
        }
        if (!a.guid && (a.state==PENDING||a.state==ALIVE)) Spawn(i);
    }
    if (changed) Commit();
}
void DireMaulRitual::OnCreatureCreate(Creature* /*actor*/) {}
void DireMaulRitual::OnCreatureRespawn(Creature* actor) { OnCreatureCreate(actor); }
void DireMaulRitual::OnCreatureDeath(Creature* actor)
{
    for (uint32 i=0;i<ACTORS;++i) if (Current(actor,i))
    {
        Actor& a=m_progress.actors[i]; if (a.state==KILLED) return;
        a.state=KILLED;
        if ((i==CONTROLLER || i==JEEVEE) && (m_progress.phase==PREPARING || m_progress.phase==WAVES || m_progress.phase==WAIT_HORSE))
        { Fail(); return; }
        if (i==HORSE)
        {
            m_progress.phase=FINISHED;
            Actor& spirit=m_progress.actors[SPIRIT];
            if (spirit.state==EMPTY) { spirit.state=PENDING; spirit.position=RitualPosition(actor); spirit.retry=Now()+1000; }
        }
        Commit(); return;
    }
}
void DireMaulRitual::OnCreatureEvade(Creature* /*actor*/) {}
void DireMaulRitual::OnCreatureDespawn(Creature* actor)
{
    for (uint32 i=0;i<ACTORS;++i) if (Current(actor,i))
    {
        Actor& a=m_progress.actors[i]; a.guid.Clear();
        if (i==CONTROLLER && (m_progress.phase==PREPARING||m_progress.phase==WAVES||m_progress.phase==WAIT_HORSE))
        {a.state=EXPIRED;Fail();return;}
        if (a.state==ALIVE) a.state=EXPIRED;
        if (i==HORSE && m_progress.phase==HORSE_ACTIVE) m_progress.phase=FINISHED;
        Commit(); return;
    }
}
void DireMaulRitual::HorseSummoned(Creature* horse, Creature* summon)
{
    if (!Current(horse,HORSE)||!summon||summon->GetEntry()!=14504||m_progress.actors[HORSE].state!=KILLED) return;
    Actor& a=m_progress.actors[SPIRIT];
    if (a.state==EMPTY) a.state=PENDING;
    if (a.state==PENDING || (a.state==ALIVE&&!a.guid)) Acknowledge(SPIRIT,summon);
    else if (a.guid!=summon->GetObjectGuid()&&summon->IsAlive()) summon->ForcedDespawn();
}
void DireMaulRitual::UpdateHorse(Creature* horse)
{
    if (!Current(horse,HORSE)||!horse->IsAlive()||!horse->IsInCombat()||horse->GetHealthPercent()>50.0f) return;
    Actor& lord=m_progress.actors[LORD];
    if (lord.state==EMPTY) { lord.state=PENDING; lord.position=RitualPosition(horse); Commit(); Spawn(LORD); }
}
void DireMaulRitual::OnObjectCreate(GameObject* go)
{
    uint32 db=go->GetDbGuid();
    for(uint32 i=0;i<3;++i) if(db==dmrNodeGuids[i])m_nodes[i]=go->GetObjectGuid();
    for(uint32 i=0;i<9;++i) if(db==dmrSymbolGuids[i])m_symbols[i]=go->GetObjectGuid();
    for(uint32 i=0;i<18;++i) if(db==dmrPortalGuids[i])m_portals[i]=go->GetObjectGuid();
    if(db==dmrCircleGuid)m_circle=go->GetObjectGuid();
    if(db==dmrCandleTrapGuid)m_candleTrap=go->GetObjectGuid();
    if(db==dmrBossPortalGuid)m_bossPortal=go->GetObjectGuid();
}
bool DireMaulRitual::NodeAvailable(uint32 i) const { return (m_progress.born&(1u<<i)) && ((m_progress.active&(1u<<i))||m_progress.nodeUntil[i]>Now()); }
void DireMaulRitual::SyncObjects()
{
    m_syncing=true;
    auto sync=[&](ObjectGuid guid,uint64 until,bool active)
    {
        if(GameObject* go=m_owner.instance->GetGameObject(guid))
        {
            bool available=until>Now()||active;
            if(available&&!go->IsSpawned()) { go->SetLootState(GO_READY);m_owner.DoRespawnGameObject(guid,until>Now()?uint32((until-Now()+999)/1000):1); }
            if(available)
            {
                if(go->GetGoState()!=(active?GO_STATE_ACTIVE:GO_STATE_READY))go->SetGoState(active?GO_STATE_ACTIVE:GO_STATE_READY);
                if(active&&go->GetLootState()!=GO_ACTIVATED)go->SetLootState(GO_ACTIVATED);
                else if(!active&&go->GetLootState()!=GO_READY)go->SetLootState(GO_READY);
                if(go->GetGoType()==GAMEOBJECT_TYPE_BUTTON)
                {
                    if(active)go->SetFlag(GAMEOBJECT_FLAGS,GO_FLAG_IN_USE);
                    else go->RemoveFlag(GAMEOBJECT_FLAGS,GO_FLAG_IN_USE);
                }
            }
            else if(go->IsSpawned())go->ForcedDespawn();
        }
    };
    for(uint32 i=0;i<3;++i)sync(m_nodes[i],m_progress.nodeUntil[i],bool(m_progress.active&(1u<<i)));
    for(uint32 i=0;i<9;++i)sync(m_symbols[i],m_progress.symbolUntil[i],false);
    for(ObjectGuid guid:m_portals)sync(guid,m_progress.portalUntil,false);
    sync(m_circle,m_progress.circleUntil,m_progress.phase==WAIT_GLYPH);
    sync(m_bossPortal,m_progress.bossPortalUntil,false);
    m_syncing=false;
}
int32 DireMaulRitual::NodeIndex(GameObject* go) const
{
    if(!go||go->GetMap()!=m_owner.instance)return -1;
    for(uint32 i=0;i<3;++i)if(go->GetObjectGuid()==m_nodes[i]&&go->GetDbGuid()==dmrNodeGuids[i])return int32(i);
    return -1;
}
void DireMaulRitual::NodeUsed(GameObject* go,Player* player)
{
    int32 i=NodeIndex(go);if(i<0||!PlayerHere(player)||m_progress.legacy||!NodeAvailable(uint32(i)))return;
    m_lastUser=player->GetObjectGuid();
}
void DireMaulRitual::NodeStateChanged(GameObject* go)
{
    int32 found=NodeIndex(go);if(m_syncing||found<0||m_progress.legacy)return;
    uint32 i=uint32(found),bit=1u<<i;
    if(go->GetLootState()==GO_ACTIVATED&&NodeAvailable(i)) {m_progress.active|=bit;m_progress.pulse[i]=1;Commit();}
    else if(go->GetLootState()==GO_JUST_DEACTIVATED) {m_progress.active&=~bit;m_progress.pulse[i]=0;Commit();}
}
void DireMaulRitual::PulseNodes(uint32 diff)
{
    for(uint32 i=0;i<3;++i)if(m_progress.active&(1u<<i))
    {
        if(m_progress.pulse[i]>diff){m_progress.pulse[i]-=diff;continue;}
        GameObject* go=m_owner.instance->GetGameObject(m_nodes[i]);Player* target=Participant();
        if(go&&go->GetLootState()==GO_ACTIVATED&&target)
        {
            static uint32 const spells[]={23117,23120,23226};
            GameObject* caster=i==2?m_owner.instance->GetGameObject(m_candleTrap):go;
            if(caster)caster->CastSpell(target,nullptr,spells[i],TRIGGERED_OLD_TRIGGERED);
        }
        m_progress.pulse[i]=6000;
    }
}
void DireMaulRitual::JeeveeReached(Creature* imp,uint32 type,uint32 point)
{
    if(!Current(imp,JEEVEE)||type!=POINT_MOTION_TYPE||point<1||point>5||!m_progress.jeeveeMoving||point!=m_progress.jeeveeNode+1)return;
    m_progress.jeeveeNode=point;m_progress.jeeveeMoving=0;m_progress.jeeveeElapsed=m_progress.jeeveeActions=m_progress.jeeveeWatchdog=0;m_sceneAttempts=0;Commit();
}
void DireMaulRitual::AdvanceJeevee(Creature* imp)
{
    if(m_progress.jeeveeNode>=5)return;
    auto const& p=dmrJeeveePath[m_progress.jeeveeNode];
    imp->GetMotionMaster()->Clear();imp->GetMotionMaster()->MovePoint(m_progress.jeeveeNode+1,p.x,p.y,p.z);
    m_progress.jeeveeMoving=1;m_progress.jeeveeWatchdog=60000;Commit();
}
bool DireMaulRitual::JeeveeAction(Creature* imp,uint32 bit,uint32 due,uint32 kind,uint32 argument)
{
    if((m_progress.jeeveeActions&bit)||m_progress.jeeveeElapsed<due)return true;
    bool success=true;
    if(kind==0)DoBroadcastText(argument,imp);
    else if(kind==1)success=imp->CastSpell(imp,argument,TRIGGERED_NONE)==SPELL_CAST_OK;
    else if(kind==2)
    {
        GameObject* node=m_owner.instance->GetGameObject(m_nodes[argument]);
        if(!node)success=false;
        else
        {
            uint32 born=m_progress.born;uint64 until=m_progress.nodeUntil[argument];
            m_progress.born|=1u<<argument;m_progress.nodeUntil[argument]=Now()+dmrNodeBornSeconds[argument]*1000;
            SyncObjects();success=node->IsSpawned();
            if(!success){m_progress.born=born;m_progress.nodeUntil[argument]=until;}
        }
    }
    else if(kind==3)
    {
        GameObject* node=m_owner.instance->GetGameObject(m_nodes[argument]);
        if(!node||!node->IsSpawned())success=false;
        else {node->Use(imp);success=node->GetLootState()==GO_ACTIVATED;}
    }
    if(!success)return false;
    m_progress.jeeveeActions|=bit;m_sceneAttempts=0;Commit();return true;
}
void DireMaulRitual::UpdateJeevee(Creature* imp,uint32 diff)
{
    if(!Current(imp,JEEVEE)||m_paused||!imp->IsAlive()||(m_progress.phase!=PREPARING&&m_progress.phase!=WAVES))return;
    if(m_progress.jeeveeMoving)
    {
        if(m_progress.jeeveeWatchdog>diff){m_progress.jeeveeWatchdog-=diff;return;}
        if(++m_sceneAttempts>=5){Fail();return;}
        if(m_progress.jeeveeNode){auto const& p=dmrJeeveePath[m_progress.jeeveeNode-1];imp->NearTeleportTo(p.x,p.y,p.z,p.o<6.3f?p.o:imp->GetOrientation());}
        m_progress.jeeveeMoving=0;AdvanceJeevee(imp);return;
    }
    if(!m_progress.jeeveeNode){AdvanceJeevee(imp);return;}
    m_progress.jeeveeElapsed=std::min(30000u,m_progress.jeeveeElapsed+std::min(diff,30000u));
    uint32 node=m_progress.jeeveeNode;bool success=true;
    if(node==1)success=JeeveeAction(imp,1,2000,0,9705)&&JeeveeAction(imp,2,7000,0,9702);
    else if(node<=4)
    {
        uint32 i=node-2;
        success=JeeveeAction(imp,1,1000,1,23140)&&JeeveeAction(imp,2,3000,2,i)&&JeeveeAction(imp,4,4000,3,i);
        if(success&&node<4)success=JeeveeAction(imp,8,5000,0,node==2?9703:9704);
    }
    else
    {
        success=JeeveeAction(imp,1,5000,1,17680);
        if(success&&m_progress.jeeveeElapsed>=6000)
        {
            Actor& a=m_progress.actors[JEEVEE];a.state=EXPIRED;a.guid.Clear();Commit();imp->ForcedDespawn();return;
        }
    }
    if(!success)
    {
        // Bound retries in time rather than once per world frame.
        if(m_progress.jeeveeWatchdog>diff){m_progress.jeeveeWatchdog-=diff;return;}
        m_progress.jeeveeWatchdog=1000;if(++m_sceneAttempts>=5)Fail();return;
    }
    if(node<5&&m_progress.jeeveeElapsed>=dmrJeeveePath[node-1].wait)AdvanceJeevee(imp);
}
void DireMaulRitual::FinishWaves(Creature* controller)
{
    if(m_progress.circleUntil && m_progress.circleUntil<=Now()){Fail();return;}
    GameObject* circle=m_owner.instance->GetGameObject(m_circle);
    if(!circle){m_progress.timer=1000;if(++m_sceneAttempts>=5)Fail();return;}
    for(uint32 i=WAVE_FIRST;i<ACTORS;++i)
    {
        Actor& a=m_progress.actors[i];Creature* actor=m_owner.instance->GetCreature(a.guid);
        if(a.state==ALIVE||a.state==PENDING)a.state=EXPIRED;a.guid.Clear();
        if(actor&&actor->IsAlive()){actor->CastSpell(actor,29216,TRIGGERED_OLD_TRIGGERED);DoScriptText(-1429005,actor);actor->ForcedDespawn(1000);}
    }
    Actor& c=m_progress.actors[CONTROLLER];c.guid.Clear();c.state=EXPIRED;
    m_progress.phase=WAIT_GLYPH;m_progress.encounter=SPECIAL;m_progress.timer=m_progress.deactivate=0;
    SyncObjects();Commit();controller->ForcedDespawn();
}
void DireMaulRitual::UpdateController(Creature* controller,uint32 diff)
{
    if(!Current(controller,CONTROLLER)||m_paused||!controller->IsAlive())return;
    if(m_progress.phase==WAIT_HORSE)
    {
        uint32 before=m_progress.timer;
        m_progress.timer=before>diff?before-diff:0;
        if(before>6000&&m_progress.timer<=6000){m_progress.bossPortalUntil=Now()+15000;SyncObjects();Commit();}
        if(before>5000&&m_progress.timer<=5000){m_progress.circleUntil=0;SyncObjects();Commit();}
        if(!m_progress.timer&&m_progress.actors[HORSE].state==EMPTY){m_progress.actors[HORSE].state=PENDING;Commit();Spawn(HORSE);}
        return;
    }
    if(m_progress.phase!=PREPARING&&m_progress.phase!=WAVES)return;
    if(m_progress.phase==PREPARING&&(m_progress.actors[JEEVEE].state==PENDING||m_progress.actors[CONTROLLER].state!=ALIVE))return;
    m_progress.elapsed=std::min(41000u,m_progress.elapsed+std::min(diff,41000u));
    if(!(m_progress.dialogue&1)&&m_progress.elapsed>=40000)
    {
        if(Creature* imp=m_owner.instance->GetCreature(m_progress.actors[JEEVEE].guid)){DoBroadcastText(9706,imp,Participant());m_progress.dialogue|=1;Commit();}
    }
    if(!(m_progress.dialogue&2)&&m_progress.elapsed>=41000)
    {
        if(Creature* imp=m_owner.instance->GetCreature(m_progress.actors[JEEVEE].guid))if(Player* p=Participant()){imp->SetFacingToObject(p);m_progress.dialogue|=2;Commit();}
    }
    if(m_progress.deactivate>diff)m_progress.deactivate-=diff;
    else
    {
        uint32 start=urand(0,2);
        for(uint32 j=0;j<3;++j){uint32 i=(start+j)%3;if(m_progress.active&(1u<<i))if(GameObject* node=m_owner.instance->GetGameObject(m_nodes[i])){node->ResetDoorOrButton();break;}}
        m_progress.deactivate=urand(30000,45000);Commit();
    }
    if(m_progress.timer>diff){m_progress.timer-=diff;return;}
    m_progress.timer=0;
    if(m_progress.phase==PREPARING)
    {
        // Missing scenery is retried, never indexed blindly or treated as a
        // successful start. Its normal lifetime starts once actual GOs exist.
        bool ready=m_owner.instance->GetGameObject(m_circle)!=nullptr;
        for(ObjectGuid guid:m_symbols)ready&=m_owner.instance->GetGameObject(guid)!=nullptr;
        for(ObjectGuid guid:m_portals)ready&=m_owner.instance->GetGameObject(guid)!=nullptr;
        if(!ready){m_progress.timer=1000;if(++m_sceneAttempts>=5)Fail();return;}
        m_progress.circleUntil=Now()+900000;m_progress.portalUntil=Now()+380000;m_progress.phase=WAVES;m_progress.timer=1000;m_sceneAttempts=0;SyncObjects();Commit();return;
    }
    if(m_progress.wave==9){FinishWaves(controller);return;}
    auto const& wave=dmrWaves[m_progress.wave];
    if(!m_progress.wavePending)
    {
        if(!m_owner.instance->GetGameObject(m_symbols[m_progress.wave])){m_progress.timer=1000;if(++m_sceneAttempts>=5)Fail();return;}
        m_progress.wavePending=1;m_progress.symbolUntil[m_progress.wave]=Now()+900000;
        for(uint32 i=wave.first;i<wave.first+wave.count;++i)m_progress.actors[WAVE_FIRST+i].state=PENDING;
        SyncObjects();Commit();ProcessActors();
    }
    bool ready=true,failed=false;
    for(uint32 i=wave.first;i<wave.first+wave.count;++i){Actor const& a=m_progress.actors[WAVE_FIRST+i];ready&=a.state!=PENDING;failed|=a.state==PENDING&&a.attempts>=5;}
    if(failed){Fail();return;}
    if(!ready){m_progress.timer=1000;return;}
    ++m_progress.wave;m_progress.wavePending=0;m_progress.timer=m_progress.wave==9?17500:45000;Commit();
}
void DireMaulRitual::WaveMovement(Creature* actor,uint32 type,uint32 point)
{
    if(!actor||type!=POINT_MOTION_TYPE)return;
    for(uint32 i=WAVE_FIRST;i<ACTORS;++i)if(Current(actor,i))
    {
        if(point==0)actor->GetMotionMaster()->MovePoint(1,-38.5f,811.8f,-29.5f);
        else if(point==1)actor->GetMotionMaster()->MoveRandomAroundPoint(-38.5f,811.8f,-29.5f,15.0f);
        return;
    }
}
void DireMaulRitual::OnPlayerEnter(Player* player)
{
    if(!PlayerHere(player))return;
    m_paused=false;m_sceneAttempts=0;
    for(Actor& a:m_progress.actors){a.attempts=0;a.retry=0;}
    ProcessActors();SyncObjects();
}
void DireMaulRitual::OnPlayerLeave(Player* player)
{
    for(auto const& r:m_owner.instance->GetPlayers())if(Player* p=r.getSource())if(p!=player&&PlayerHere(p))return;
    Pause();
}
void DireMaulRitual::Update(uint32 diff)
{
    if(!Participant()){Pause();return;}
    m_paused=false;
    if(!m_progress.legacy)
    {
        ProcessActors();
        if(m_progress.phase==WAVES && m_progress.circleUntil && m_progress.circleUntil<=Now()){Fail();return;}
        if((m_progress.phase==PREPARING||m_progress.phase==WAVES)&&m_progress.actors[CONTROLLER].state==PENDING&&m_progress.actors[CONTROLLER].attempts>=5){Fail();return;}
        if((m_progress.phase==PREPARING||m_progress.phase==WAVES)&&m_progress.actors[JEEVEE].state==PENDING&&m_progress.actors[JEEVEE].attempts>=5){Fail();return;}
        if((m_progress.phase==PREPARING||m_progress.phase==WAVES)&&m_progress.actors[JEEVEE].state==EXPIRED&&m_progress.born==7&&m_progress.active==0){Fail();return;}
        PulseNodes(diff);
    }
    SyncObjects();
    if(m_checkpoint>diff)m_checkpoint-=diff;
    else {if(!m_progress.legacy&&m_progress.phase!=FAILED)Commit();m_checkpoint=5000;}
}
