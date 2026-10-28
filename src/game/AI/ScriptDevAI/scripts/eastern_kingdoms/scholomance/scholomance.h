/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

#ifndef DEF_SCHOLOMANCE_H
#define DEF_SCHOLOMANCE_H

#include "scholomance_state.h"
#include <array>

enum
{
    MAX_ENCOUNTER           = 10,
    MAX_EVENTS              = 6,
    MAX_GROUPS              = 4,
    MAX_NPC_PER_GROUP       = 4,

    TYPE_KIRTONOS           = 0,
    TYPE_RATTLEGORE         = 1,
    TYPE_RAS_FROSTWHISPER   = 2,
    TYPE_MALICIA            = 3,
    TYPE_THEOLEN            = 4,
    TYPE_POLKELT            = 5,
    TYPE_RAVENIAN           = 6,
    TYPE_ALEXEI_BAROV       = 7,
    TYPE_ILLUCIA_BAROV      = 8,
    TYPE_GANDLING           = 9,

    NPC_NECROFIEND          = 11551,
    NPC_RISEN_ABERRATION    = 10485,
    NPC_DISEASED_GHOUL      = 10495,
    NPC_REANIMATED_CORPSE   = 10481,
    NPC_SCHOLOMANCE_STUDENT = 10475,

    NPC_KIRTONOS            = 10506,
    NPC_RATTLEGORE          = 11622,
    NPC_RAS_FROSTWHISPER    = 10508,
    NPC_THEOLEN_KRASTINOV   = 11261,
    NPC_LOREKEEPER_POLKELT  = 10901,
    NPC_RAVENIAN            = 10507,
    NPC_ILLUCIA_BAROV       = 10502,
    NPC_ALEXEI_BAROV        = 10504,
    NPC_INSTRUCTOR_MALICIA  = 10505,
    NPC_VECTUS              = 10432,
    NPC_MARDUK_BLACKPOOL    = 10433,
    NPC_DARKMASTER_GANDLING = 1853,
    NPC_RISEN_GUARDIAN      = 11598,                        // summoned in random rooms by gandling

    GO_GATE_KIRTONOS        = 175570,
    GO_VIEWING_ROOM_DOOR    = 175167,                       // Must be opened in reload case
    GO_GATE_RAS             = 177370,
    GO_GATE_MALICIA         = 177375,
    GO_GATE_THEOLEN         = 177377,
    GO_GATE_POLKELT         = 177376,
    GO_GATE_RAVENIAN        = 177372,
    GO_GATE_BAROV           = 177373,
    GO_GATE_ILLUCIA         = 177371,
    GO_GATE_GANDLING        = 177374,

    // Because the shadow portal teleport coordinates are guesswork (taken from old script) these IDs might be randomized
    // TODO Syncronise with correct DB coordinates when they will be known
    EVENT_ID_POLKELT        = 5618,
    EVENT_ID_THEOLEN        = 5619,
    EVENT_ID_MALICIA        = 5620,
    EVENT_ID_ILLUCIA        = 5621,
    EVENT_ID_BAROV          = 5622,
    EVENT_ID_RAVENIAN       = 5623,

    EVENT_ID_DAWN_GAMBIT    = 5140,
    EVENT_ID_PALADIN        = 8436,
    QUEST_JUDGMENT_REDEMPTION = 7647,
    NPC_DARKREAVER          = 14516,
    NPC_FALLEN_CHARGER      = 14568,
    GO_BRAZIER_HERALD       = 175564,

    FACTION_SCOURGE         = 233,

    SAY_GANDLING_SPAWN      = -1289000,
    YELL_VECTUS_GAMBIT      = -1289001,

    SPELL_STUDENT_TRANSFORM = 18115,
    SPELL_XMAS_GANDLING     = 26199,
};

static const Position aGandlingSpawnLocs[1] =
{
    {180.771f, -5.4286f, 75.5702f, 1.29154f}
};

// Coordinates used to respawn the NPCs in the entrance room
// (right before the Viewing Room) on Rattlegore's death
static const Position aEntranceRoomSpawnLocs[17] =
{
    {186.036f, 94.5f, 104.72f, 1.29154f},      // First corner
    {179.117f, 95.5166f, 104.81f, 1.29154f},
    {180.612f, 100.176f, 104.80f, 1.29154f},
    {185.926f, 100.079f, 104.80f, 1.29154f},
    {178.999f, 75.2952f, 104.72f, 1.29154f},   // Second corner
    {185.558f, 77.276f, 104.72f, 1.29154f},
    {187.556f, 70.4334f, 104.72f, 1.29154f},
    {180.51f, 82.3917f, 104.72f, 1.29154f},
    {212.915f, 70.6005f, 104.80f, 1.29154f},   // Third corner
    {221.199f, 77.0037f, 104.72f, 1.29154f},
    {214.381f, 76.233f, 104.80f, 1.29154f},
    {218.64f, 71.5957f, 104.72f, 1.29154f},
    {221.249f, 94.9361f, 104.72f, 1.29154f},   // Fourth corner
    {214.406f, 101.903f, 104.72f, 1.29154f},
    {217.521f, 95.4237f, 104.72f, 1.29154f},
    {223.296f, 105.101f, 104.72f, 1.29154f},
    {209.233f, 73.2819f, 104.80f, 1.29154f}    // patrolling necrofiend
};

struct ScholomanceActor
{
    ObjectGuid guid;
    uint8 attempts = 0;
};

struct GandlingEventData
{
    bool active = false;
    uint32 timer = 0;
    uint32 dead = 0;
    uint8 attempts = 0;
    std::array<ObjectGuid, 4> guards{};
};

struct BoxVolume
{
    float m_fCornerX;
    float m_fCornerY;
    float m_fCenterZ;
    uint32 m_uiLength;
    uint32 m_uiWidth;
};

static const BoxVolume aEntranceRoom = { 174.13f, 63.84f, 104.0f, 54, 44 };

static const uint32 aGandlingEvents[MAX_EVENTS] = {EVENT_ID_POLKELT, EVENT_ID_THEOLEN, EVENT_ID_MALICIA, EVENT_ID_ILLUCIA, EVENT_ID_BAROV, EVENT_ID_RAVENIAN};

class instance_scholomance : public ScriptedInstance
{
    public:
        instance_scholomance(Map* map);
        void Initialize() override;
        void OnCreatureEnterCombat(Creature* creature) override;
        void OnCreatureEvade(Creature* creature) override;
        void OnCreatureDeath(Creature* creature) override;
        void OnCreatureDespawn(Creature* creature) override;
        void OnCreatureRespawn(Creature* creature) override;
        void OnCreatureCreate(Creature* creature) override;
        void OnObjectCreate(GameObject* go) override;
        void OnPlayerEnter(Player* player) override;
        void OnPlayerLeave(Player* player) override;
        void HandlePortalEvent(uint32 eventId, Creature* source);
        void HandleDawnGambitEvent();
        bool StartPaladinEvent(Player* player);
        bool StartKirtonosEvent();
        void SetData(uint32 type, uint32 data) override;
        uint32 GetData(uint32 type) const override;
        void Update(uint32 diff) override;
        const char* Save() const override { return m_strInstData.c_str(); }
        void Load(char const* data) override;

    private:
        void SaveState();
        void SyncDoors();
        void SetDoor(uint32 entry, bool open);
        void PrepareEntrance();
        void RestoreEntrance();
        void SuppressEntranceActors();
        void RestoreGandling(bool announce);
        void ClearPortal(uint32 room);
        void UpdatePortals(uint32 diff);
        void ApplyGambit(Creature* creature);
        void UpdatePaladin(uint32 diff);
        void ClearPaladinActors();
        void FailPaladinEvent();
        void RestoreCharger();

        ScholomanceProgress m_progress;
        std::string m_strInstData;
        std::array<ScholomanceActor, 17> m_entranceActors{};
        std::array<ScholomanceActor, 14> m_paladinActors{};
        std::array<GandlingEventData, MAX_EVENTS> m_portals{};
        GuidSet m_staticEntrance;
        GuidSet m_viewingRoom;
        std::map<ObjectGuid, uint8> m_gambitPending;
        ObjectGuid m_charger;
        uint32 m_retryTimer = 0;
        uint32 m_checkpointTimer = 5000;
        uint32 m_kirtonosTimer = 0;
        uint8 m_gandlingAttempts = 0;
        uint8 m_chargerAttempts = 0;
        bool m_paladinPaused = false;
};

#endif
