/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */
#ifndef DEF_ZULFARRAK_H
#define DEF_ZULFARRAK_H

#include <array>

enum
{
    MAX_ENCOUNTER = 9,
    TYPE_VELRATHA = 0,
    TYPE_GAHZRILLA = 1,
    TYPE_ANTUSUL = 2,
    TYPE_THEKA = 3,
    TYPE_ZUMRAH = 4,
    TYPE_NEKRUM = 5,
    TYPE_SEZZZIZ = 6,
    TYPE_CHIEF_SANDSCALP = 7,
    TYPE_PYRAMID_EVENT = 8,
    TYPE_END_DOOR = 9,
    TYPE_BLY_BETRAYAL = 10,
    NPC_VELRATHA = 7795,
    NPC_GAHZRILLA = 7273,
    NPC_ANTUSUL = 8127,
    NPC_THEKA = 7272,
    NPC_ZUMRAH = 7271,
    NPC_NEKRUM = 7796,
    NPC_SEZZZIZ = 7275,
    NPC_CHIEF_SANDSCALP = 7267,
    NPC_SERGEANT_BLY = 7604,
    NPC_RAVEN = 7605,
    NPC_ORO = 7606,
    NPC_WEEGLI = 7607,
    NPC_MURTA = 7608,
    NPC_SANDFURY_SLAVE = 7787,
    NPC_SANDFURY_DRUDGE = 7788,
    NPC_SANDFURY_CRETIN = 7789,
    NPC_SANDFURY_ACOLYTE = 8876,
    NPC_SANDFURY_ZEALOT = 8877,
    GO_SHALLOW_GRAVE = 128403,
    GO_END_DOOR = 146084,
    GO_GAHZRILLA_GONG = 141832,
    GO_FIRST_TROLL_CAGE = 141070,
    GO_LAST_TROLL_CAGE = 141074,
    GO_WEEGLI_BARREL = 141612,
    AREATRIGGER_ANTUSUL = 1447,
    ZF_PYRAMID_MEMBERS = 85,
    ZF_FIRST_TWO_WAVES = 76,
    ZF_CREW_COUNT = 5,
    ZF_MEMBER_WAITING = 0,
    ZF_MEMBER_REQUESTED = 1,
    ZF_MEMBER_KILLED = 2,
    ZF_CREW_ALIVE = 0,
    ZF_CREW_DEAD = 1,
    ZF_CREW_ESCAPED = 2,
    ZF_CONDITION_BLY = 100,
    ZF_CONDITION_WEEGLI = 101,
    ZF_RELAY_RELEASE = 209010,
    ZF_RELAY_DESCEND = 209011,
    ZF_RELAY_DOOR_START = 209012,
    ZF_RELAY_BLY_START = 209013,
    ZF_RELAY_DOOR_FROM_BLY = 209015
};

struct ZulFarrakCrewState
{
    uint32 life = ZF_CREW_ALIVE;
    uint32 waypoint = 1;
    uint32 faction = 35;
    bool paused = true;
    bool positioned = false;
    float x = 0, y = 0, z = 0, o = 0;
};

class instance_zulfarrak : public ScriptedInstance
{
  public:
    explicit instance_zulfarrak(Map *map);
    void Initialize() override;
    void OnCreatureEnterCombat(Creature *creature) override;
    void OnCreatureEvade(Creature *creature) override;
    void OnCreatureDeath(Creature *creature) override;
    void OnCreatureCreate(Creature *creature) override;
    void OnCreatureRespawn(Creature *creature) override;
    void OnObjectCreate(GameObject *go) override;
    void OnObjectSpawn(GameObject *go) override;
    void SetData(uint32 type, uint32 state) override;
    uint32 GetData(uint32 type) const override;
    bool CheckConditionCriteriaMeet(Player const *player, uint32 id, WorldObject const *source,
                                    uint32 sourceType) const override;
    void GetShallowGravesGuidList(GuidList &list) const
    {
        list.assign(m_graves.begin(), m_graves.end());
    }
    const char *Save() const override
    {
        return m_strInstData.c_str();
    }
    void Load(const char *input) override;
    void Update(uint32 diff) override;

    bool StartGahzrillaEvent(Player *player);
    bool StartPyramidEvent(Player *player);
    bool CanStartBlyBetrayal(Player const *player) const;
    bool StartBlyBetrayal(Player *player);
    bool CanStartDoorDemolition(Player const *player) const;
    bool StartDoorDemolition(Player *player, bool fromBly = false);
    bool ConfirmDoorDestroyed(GameObject *door, WorldObject *caster);
    void NotifyCrewEscaped(Creature *creature);

  protected:
    void BuildSaveData();
    void SaveState();
    void SyncObject(GameObject *go);
    void CaptureCrew();
    void RestoreCrew();
    void SetLegacyCrewPositions();
    void RegisterBarrel(GameObject *go);
    void UpdatePyramid(uint32 diff);
    void UpdateGahzrilla(uint32 diff);
    void UpdateBetrayal(uint32 diff);
    void UpdateDoor(uint32 diff);
    void SpawnRequestedMembers();
    void DispatchMember();
    void PausePyramid();
    void ResumePyramid();
    bool AllMembersKilled(uint32 count) const;
    bool HasPyramidPlayers() const;
    bool IsValidPlayer(Player const *player, WorldObject const *origin, float distance,
                       bool requireLineOfSight = true) const;
    Player *SelectPlayer(WorldObject const *origin, ObjectGuid preferred, float distance) const;
    Creature *Crew(uint32 index) const;
    GameObject *PyramidAnchor() const;
    void RunActorRelay(uint32 relay, WorldObject *source, WorldObject *target = nullptr);
    int MemberIndex(ObjectGuid guid) const;

    uint32 m_auiEncounter[MAX_ENCOUNTER];
    std::string m_strInstData;
    GuidSet m_graves, m_cages, m_rejectedGahzrilla;
    std::array<ObjectGuid, ZF_CREW_COUNT> m_crewGuids;
    std::array<ZulFarrakCrewState, ZF_CREW_COUNT> m_crew;
    std::array<bool, ZF_CREW_COUNT> m_crewRestorePending;
    std::array<ObjectGuid, ZF_PYRAMID_MEMBERS> m_memberGuids;
    std::array<uint8, ZF_PYRAMID_MEMBERS> m_memberStates;
    std::array<bool, ZF_PYRAMID_MEMBERS> m_dispatched;
    int m_creatingMember;
    bool m_creatingGahzrilla;
    bool m_spawnedThisUpdate;
    bool m_restoringCrew;
    bool m_descended;
    uint32 m_pyramidElapsed, m_dispatchTimer, m_missingPlayersTimer;
    uint32 m_pollTimer, m_checkpointTimer;
    uint32 m_doorState, m_doorTimer, m_doorRetries;
    ObjectGuid m_barrelGuid;
    GuidSet m_barrelCandidates;
    uint32 m_betrayalState, m_betrayalTimer;
    ObjectGuid m_betrayalPlayer;
    ObjectGuid m_gahzrillaGuid;
    uint32 m_gahzrillaTimer, m_gahzrillaMoveTimer, m_gahzrillaFailures;
};
#endif
