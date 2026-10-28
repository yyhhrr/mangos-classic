/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_BLACKROCK_SPIRE_H
#define DEF_BLACKROCK_SPIRE_H

#include "blackrock_spire_lower.h"
#include "blackrock_spire_upper.h"
#include <array>

enum
{
    MAX_ENCOUNTER               = 6,
    MAX_ROOMS                   = 7,

    TYPE_ROOM_EVENT             = 0,
    TYPE_EMBERSEER              = 1,
    TYPE_FLAMEWREATH            = 2,                        // Only summon once per instance
    TYPE_STADIUM                = 3,
    TYPE_DRAKKISATH             = 4,
    TYPE_VALTHALAK              = 5,                        // Only summon once per instance

    NPC_SCARSHIELD_INFILTRATOR  = 10299,
    NPC_BLACKHAND_SUMMONER      = 9818,
    NPC_BLACKHAND_VETERAN       = 9819,
    NPC_PYROGUARD_EMBERSEER     = 9816,
    NPC_SOLAKAR_FLAMEWREATH     = 10264,
    NPC_BLACKHAND_INCARCERATOR  = 10316,
    NPC_BLACKHAND_ELITE         = 10317,
    NPC_LORD_VICTOR_NEFARIUS    = 10162,
    NPC_REND_BLACKHAND          = 10429,
    NPC_GYTH                    = 10339,
    NPC_THE_BEAST               = 10430,
    NPC_DRAKKISATH              = 10363,
    NPC_CHROMATIC_WHELP         = 10442,                    // related to Gyth arena event
    NPC_CHROMATIC_DRAGON        = 10447,
    NPC_BLACKHAND_HANDLER       = 10742,

    // Doors
    GO_EMBERSEER_IN             = 175244,
    GO_DOORS                    = 175705,
    GO_EMBERSEER_OUT            = 175153,
    GO_FATHER_FLAME             = 175245,
    GO_GYTH_ENTRY_DOOR          = 164726,
    GO_GYTH_COMBAT_DOOR         = 175185,
    GO_GYTH_EXIT_DOOR           = 175186,
    GO_DRAKKISATH_DOOR_1        = 175946,
    GO_DRAKKISATH_DOOR_2        = 175947,

    // upper spire entrance
    GO_DRAGONSPINE              = 164725,
    GO_BRAZIER_1                = 175528,
    GO_BRAZIER_2                = 175529,
    GO_BRAZIER_3                = 175530,
    GO_BRAZIER_4                = 175531,
    GO_BRAZIER_5                = 175532,
    GO_BRAZIER_6                = 175533,

    GO_ROOM_7_RUNE              = 175194,
    GO_ROOM_3_RUNE              = 175195,
    GO_ROOM_6_RUNE              = 175196,
    GO_ROOM_1_RUNE              = 175197,
    GO_ROOM_5_RUNE              = 175198,
    GO_ROOM_2_RUNE              = 175199,
    GO_ROOM_4_RUNE              = 175200,

    GO_ROOKERY_EGG              = 175124,

    GO_EMBERSEER_RUNE_1         = 175266,
    GO_EMBERSEER_RUNE_2         = 175267,
    GO_EMBERSEER_RUNE_3         = 175268,
    GO_EMBERSEER_RUNE_4         = 175269,
    GO_EMBERSEER_RUNE_5         = 175270,
    GO_EMBERSEER_RUNE_6         = 175271,
    GO_EMBERSEER_RUNE_7         = 175272,

    ITEM_SEAL_OF_ASCENSION      = 12344,

    MAX_STADIUM_WAVES           = 7,
    MAX_STADIUM_MOBS_PER_WAVE   = 5,

    FACTION_BLACK_DRAGON        = 103
};

static const Position aBeastLocs[3] =
{
    {98.09f, -563.45f, 109.86f, 2.37f},                     // Summon points
    {99.81f, -561.47f, 109.24f, 2.43f},
    {96.92f, -560.98f, 110.18f, 2.43f},
};

class instance_blackrock_spire : public ScriptedInstance
{
public:
    explicit instance_blackrock_spire(Map* map);
    void Initialize() override;
    void OnObjectCreate(GameObject*) override;
    void OnCreatureCreate(Creature*) override;
    void OnCreatureRespawn(Creature*) override;
    void OnCreatureDeath(Creature*) override;
    void OnCreatureEvade(Creature*) override;
    void OnCreatureDespawn(Creature*) override;
    void OnCreatureEnterCombat(Creature*) override;
    void OnPlayerEnter(Player*) override;
    void OnPlayerLeave(Player*) override;
    void Update(uint32) override;
    uint32 GetData(uint32 type) const override { return m_upper.GetData(type); }
    void SetData(uint32 type, uint32 value) override { m_upper.SetData(type, value); }
    void SetData64(uint32, uint64) override;
    bool StartUrok(Object* source, Object* target) { return m_lower.StartUrok(source, target); }
    bool StartStadium(Player* player) { return m_upper.StartStadium(player); }
    bool StartRookery(Player* player) { return m_upper.StartRookery(player); }
    bool TryUnlockUpper(Player* player) { return m_upper.TryUnlockUpper(player); }
    bool StartEmberseer(Player* player) { return m_upper.StartEmberseer(player); }
    bool IsStadiumGyth(Creature* actor) const { return m_upper.IsStadiumGyth(actor); }
    bool WasStadiumRendReleased(Creature* actor) const { return m_upper.WasStadiumRendReleased(actor); }
    bool ConfirmStadiumRend(Creature* gyth, Creature* rend) { return m_upper.ConfirmStadiumRend(gyth, rend); }
    void FailStadium() { m_upper.FailStadium(); }
    void FailEmberseer() { m_upper.FailEmberseer(); }
    bool ShouldEmberseerBeCaged() const { return m_upper.ShouldEmberseerBeCaged(); }
    bool IsEmberseerGrowing() const { return m_upper.IsEmberseerGrowing(); }
    bool IsEmberseerReleased() const { return m_upper.IsEmberseerReleased(); }
    uint32 GetEmberseerGeneration() const { return m_upper.GetEmberseerGeneration(); }
    uint32 GetEmberseerGrowingStacks() const { return m_upper.GetEmberseerGrowingStacks(); }
    void EmberseerGrowingTick(Creature* actor, uint32 stacks) { m_upper.EmberseerGrowingTick(actor, stacks); }
    uint32 GetEmberseerReleaseStep() const { return m_upper.GetEmberseerReleaseStep(); }
    void EmberseerReleaseStep(Creature* actor, uint32 step) { m_upper.EmberseerReleaseStep(actor, step); }
    void EmberseerReleased(Creature* actor) { m_upper.EmberseerReleased(actor); }
    void GetIncarceratorGUIDList(GuidList& list) const { m_upper.GetIncarceratorGUIDList(list); }
    std::vector<InstanceEventReset> GetResettableEvents() const override;
    bool GetEventResetData(uint32, std::string&) const override;
    void ApplyEventReset(uint32, std::string const&) override;
    char const* Save() const override { return m_strInstData.c_str(); }
    void Load(char const*) override;
    bool m_bBeastIntroDone = false;
    bool m_bBeastOutOfLair = false;
private:
    std::string BuildSave(std::string const* upperOverride = nullptr, bool resetStadium = false) const;
    void SaveState();
    std::string m_strInstData;
    BlackrockSpireLower m_lower;
    BlackrockSpireUpper m_upper;
};
#endif
