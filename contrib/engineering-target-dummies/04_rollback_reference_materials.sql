-- Roll back 03 only for wholly unchanged reference profiles. A partial change,
-- extra material, different LootId or later custom comment causes a skip.
-- Run this BEFORE 02_rollback_ai.sql. Never delete all three loot tables.
DROP TEMPORARY TABLE IF EXISTS engineering_dummy_material_profile;
CREATE TEMPORARY TABLE engineering_dummy_material_profile (
    entry MEDIUMINT UNSIGNED NOT NULL,
    item MEDIUMINT UNSIGNED NOT NULL,
    chance FLOAT NOT NULL,
    groupid TINYINT UNSIGNED NOT NULL,
    mincount INT NOT NULL,
    maxcount TINYINT UNSIGNED NOT NULL,
    PRIMARY KEY (entry,item)
);
INSERT INTO engineering_dummy_material_profile VALUES
    (2673,2589,75,0,1,1),(2673,4359,75,0,1,1),(2673,2841,50,0,1,1),
    (2673,4363,30,0,1,1),(2673,7191,15,0,1,1),
    (12426,7191,35,0,1,1),(12426,10561,75,1,1,1),(12426,16000,75,2,1,1),
    (12426,15994,75,3,1,2),(12426,6037,75,4,1,1),(12426,8170,75,5,1,2),
    (12426,14047,75,6,1,4);
DROP TEMPORARY TABLE IF EXISTS engineering_dummy_material_eligible;
CREATE TEMPORARY TABLE engineering_dummy_material_eligible AS
SELECT c.Entry FROM creature_template AS c
WHERE c.Entry IN (2673,12426) AND c.LootId=c.Entry
  AND (SELECT COUNT(*) FROM creature_loot_template AS l WHERE l.entry=c.Entry)
      = CASE WHEN c.Entry=2673 THEN 5 ELSE 7 END
  AND NOT EXISTS (
      SELECT 1 FROM engineering_dummy_material_profile AS p
      LEFT JOIN creature_loot_template AS l ON l.entry=p.entry AND l.item=p.item
      WHERE p.entry=c.Entry AND (
          l.item IS NULL OR l.ChanceOrQuestChance<>p.chance OR l.groupid<>p.groupid
          OR l.mincountOrRef<>p.mincount OR l.maxcount<>p.maxcount OR l.condition_id<>0
          OR l.comments IS NULL
          OR BINARY l.comments<>BINARY 'Engineering dummy reference profile 2026-10-11; 1.12 rates unverified'
      )
  );
DELETE l FROM creature_loot_template AS l
JOIN engineering_dummy_material_eligible AS e ON e.Entry=l.entry;
UPDATE creature_template AS c
JOIN engineering_dummy_material_eligible AS e ON e.Entry=c.Entry
SET c.LootId=0;
SELECT COUNT(*) AS reference_profiles_removed FROM engineering_dummy_material_eligible;
DROP TEMPORARY TABLE engineering_dummy_material_eligible;
DROP TEMPORARY TABLE engineering_dummy_material_profile;
