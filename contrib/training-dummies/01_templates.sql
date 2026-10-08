-- Optional local extension for CMaNGOS Classic 1.12.x; NOT a database version update.
-- Review README.md and back up the world DB before applying. Do not use --force.
-- Creates exactly two templates, no spawns and no client/DBC changes.
-- Both requested IDs must be free; if either is occupied this entire insert is skipped.
-- Explicit armour snapshots: level 34 = 1340; level 63 = 4691 (local Classic DB baseline).
-- Legacy stat fallback (ArmorMultiplier=-1) also activates explicit health/mana fields.
-- Zero NPC attribute multipliers avoid adding agility armour/stamina health on top.
-- StaticFlags1=NO_XP|NO_LOOT|UNKILLABLE|SESSILE|NO_AUTOMATIC_REGEN.
-- StaticFlags2=NO_SKILL_GAINS. ExtraFlags=NO_AGGRO_ON_SIGHT|NO_BLOCK|NO_CALL_ASSIST|NO_MELEE.
-- MechanicImmuneMask=1 prevents charm/possession from replacing the passive AI.
-- No player/NPC immunity, school immunity, absorb shield or player resource restoration.
SET NAMES utf8;
SET @training_dummy_ids_free = (
    SELECT COUNT(*) = 0 FROM creature_template WHERE Entry IN (900000, 900001)
);

INSERT INTO creature_template (
    Entry,
    Name,
    SubName,
    MinLevel,
    MaxLevel,
    DisplayId1,
    DisplayIdProbability1,
    Faction,
    Scale,
    CreatureType,
    InhabitType,
    RegenerateStats,
    UnitClass,
    Rank,
    HealthMultiplier,
    PowerMultiplier,
    DamageMultiplier,
    ArmorMultiplier,
    StrengthMultiplier,
    AgilityMultiplier,
    StaminaMultiplier,
    IntellectMultiplier,
    SpiritMultiplier,
    MinLevelHealth,
    MaxLevelHealth,
    MinLevelMana,
    MaxLevelMana,
    Armor,
    NpcFlags,
    UnitFlags,
    ExtraFlags,
    StaticFlags1,
    StaticFlags2,
    MechanicImmuneMask,
    SchoolImmuneMask,
    ResistanceHoly,
    ResistanceFire,
    ResistanceNature,
    ResistanceFrost,
    ResistanceShadow,
    ResistanceArcane,
    MinLootGold,
    MaxLootGold,
    LootId,
    PickpocketLootId,
    SkinningLootId,
    KillCredit1,
    KillCredit2,
    MovementType,
    SpellList,
    AIName,
    ScriptName
)
SELECT 900000, 'Training Dummy (34)', 'Normal output baseline', 34, 34, 1555, 100, 7, 1, 7, 1, 0, 1, 0, -1, -1, 1, -1, 0, 0, 0, 0, 0, 10000000, 10000000, 0, 0, 1340, 0, 0, 133138, 1294, 1073741824, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 'npc_training_dummy'
FROM DUAL WHERE @training_dummy_ids_free
UNION ALL
SELECT 900001, 'Training Dummy (63)', 'Raid output approximation', 63, 63, 1555, 100, 7, 1, 7, 1, 0, 1, 0, -1, -1, 1, -1, 0, 0, 0, 0, 0, 10000000, 10000000, 0, 0, 4691, 0, 0, 133138, 1294, 1073741824, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, '', 'npc_training_dummy'
FROM DUAL WHERE @training_dummy_ids_free;

SELECT ROW_COUNT() AS templates_inserted,
       IF(@training_dummy_ids_free, 'Verify two rows below', 'BLOCKED: entry IDs are occupied') AS result;
SELECT Entry, Name, MinLevel, MaxLevel, DisplayId1, DisplayIdProbability1, Faction, UnitClass, Rank, ArmorMultiplier,
       MinLevelHealth, MechanicImmuneMask, SchoolImmuneMask, ScriptName
FROM creature_template WHERE Entry IN (900000, 900001);

