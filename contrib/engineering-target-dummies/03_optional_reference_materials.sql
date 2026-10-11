-- OPTIONAL: user must accept approximate reference rates before importing.
-- Enable deliberately in the SAME SQL session:
-- SET @engineering_dummy_allow_reference = 1;
-- No entry 2674 rows or probabilities are touched.
-- Normal: sample-informed correction of the still-incorrect VMaNGOS table.
-- Masterwork: VMaNGOS official db-4641790 snapshot, rates NOT proven for 1.12.
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
    (2673,2589,75,0,1,1), -- Linen Cloth; not Wool Cloth 2592
    (2673,4359,75,0,1,1), -- Handful of Copper Bolts
    (2673,2841,50,0,1,1), -- Bronze Bar
    (2673,4363,30,0,1,1), -- Copper Modulator
    (2673,7191,15,0,1,1), -- Fused Wiring
    (12426,7191,35,0,1,1), -- Fused Wiring
    (12426,10561,75,1,1,1), -- Mithril Casing
    (12426,16000,75,2,1,1), -- Thorium Tube
    (12426,15994,75,3,1,2), -- Thorium Widget
    (12426,6037,75,4,1,1), -- Truesilver Bar
    (12426,8170,75,5,1,2), -- Rugged Leather
    (12426,14047,75,6,1,4); -- Runecloth; historical rate remains unverified

DROP TEMPORARY TABLE IF EXISTS engineering_dummy_material_eligible;
CREATE TEMPORARY TABLE engineering_dummy_material_eligible AS
SELECT c.Entry
FROM creature_template AS c
WHERE IFNULL(@engineering_dummy_allow_reference,0)=1
  AND c.Entry IN (2673,12426) AND c.LootId=0
  AND c.AIName='' AND c.ScriptName='npc_engineering_target_dummy'
  AND NOT EXISTS (SELECT 1 FROM creature_loot_template AS l WHERE l.entry=c.Entry)
  AND NOT EXISTS (
      SELECT 1 FROM engineering_dummy_material_profile AS p
      LEFT JOIN item_template AS i ON i.entry=p.item
      WHERE p.entry=c.Entry AND i.entry IS NULL
  );

INSERT INTO creature_loot_template
    (entry,item,ChanceOrQuestChance,groupid,mincountOrRef,maxcount,condition_id,comments)
SELECT p.entry,p.item,p.chance,p.groupid,p.mincount,p.maxcount,0,
    'Engineering dummy reference profile 2026-10-11; 1.12 rates unverified'
FROM engineering_dummy_material_profile AS p
JOIN engineering_dummy_material_eligible AS e ON e.Entry=p.entry;
UPDATE creature_template AS c
JOIN engineering_dummy_material_eligible AS e ON e.Entry=c.Entry
SET c.LootId=c.Entry
WHERE c.LootId=0;
SELECT COUNT(*) AS reference_profiles_added FROM engineering_dummy_material_eligible;
DROP TEMPORARY TABLE engineering_dummy_material_eligible;
DROP TEMPORARY TABLE engineering_dummy_material_profile;
