-- CMaNGOS Classic: five additional spawns, reusing the existing adjustable template 900000.
-- First confirm target classicmangos1, schema, backups and GUIDs. This file never changes templates/settings.
-- Target classicmangos1 and all five GUIDs were checked live before the 2026-10-10 deployment.
-- Import while world is stopped. No ordinary reload command reloads the creature spawn cache.
-- No --force. Entire batch is blocked if any chosen GUID belongs to a different spawn or has foreign links.
SET NAMES utf8;
DROP TEMPORARY TABLE IF EXISTS training_aoe_positions;
CREATE TEMPORARY TABLE training_aoe_positions (
    guid INT UNSIGNED PRIMARY KEY,
    x DECIMAL(40,20), y DECIMAL(40,20), z DECIMAL(40,20), o DECIMAL(40,20)
);
INSERT INTO training_aoe_positions VALUES
    (9000431,9982.51,2262.49,1333.40,4.69494), -- Original WotLK GUID 48316, XYZO retained
    (9000432,9976.21,2262.37,1334.50,4.69494), -- Original WotLK GUID 48318, XYZO retained
    (9000433,9979.36,2262.43,1334.00,4.69494), -- New: midpoint; Z = Classic raw ground + approx .08
    (9000434,9979.30,2265.58,1333.91,4.69494), -- New: perpendicular offset +3.15 yd
    (9000435,9979.42,2259.28,1333.85,4.69494); -- New: perpendicular offset -3.15 yd
SET @training_aoe_template_valid = (
    SELECT COUNT(*)=1 FROM creature_template
    WHERE Entry=900000 AND ScriptName='npc_training_dummy'
      AND MinLevel BETWEEN 1 AND 63 AND MaxLevel=MinLevel
      AND DisplayId1>0 AND DisplayIdProbability1>0
);
SET @training_aoe_ids_safe = (
    SELECT COUNT(*)=0 FROM creature c JOIN training_aoe_positions p ON c.guid=p.guid
    WHERE NOT (c.id=900000 AND c.map=1 AND c.spawnMask=1
      AND c.position_x=p.x AND c.position_y=p.y AND c.position_z=p.z AND c.orientation=p.o
      AND c.spawntimesecsmin=120 AND c.spawntimesecsmax=120 AND c.spawndist=0 AND c.MovementType=0)
);
SET @training_aoe_refs_safe = 1;
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM creature_spawn_entry r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM creature_spawn_data r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM game_event_creature r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM pool_creature r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM creature_addon r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM creature_linking r JOIN training_aoe_positions p ON r.guid=p.guid OR r.master_guid=p.guid);
SET @training_aoe_refs_safe = @training_aoe_refs_safe AND (SELECT COUNT(*)=0 FROM training_dummy_settings r JOIN training_aoe_positions p ON r.guid=p.guid
         LEFT JOIN creature c ON c.guid=r.guid
         WHERE c.guid IS NULL OR r.entry<>900000);
INSERT INTO creature
    (guid,id,map,spawnMask,position_x,position_y,position_z,orientation,
     spawntimesecsmin,spawntimesecsmax,spawndist,MovementType)
SELECT p.guid,900000,1,1,p.x,p.y,p.z,p.o,120,120,0,0
FROM training_aoe_positions p LEFT JOIN creature existing ON existing.guid=p.guid
WHERE existing.guid IS NULL
  AND @training_aoe_template_valid AND @training_aoe_ids_safe AND @training_aoe_refs_safe;
SELECT ROW_COUNT() AS added_spawns, @training_aoe_template_valid AS template_valid,
       @training_aoe_ids_safe AS ids_safe,@training_aoe_refs_safe AS refs_safe;
SELECT c.guid,c.id,c.map,c.position_x,c.position_y,c.position_z,c.orientation,
       t.MinLevel AS default_level,s.level AS saved_level,
       t.DisplayId1,t.DisplayIdProbability1,t.ScriptName
FROM creature c JOIN training_aoe_positions p ON c.guid=p.guid
JOIN creature_template t ON t.Entry=c.id LEFT JOIN training_dummy_settings s ON s.guid=c.guid AND s.entry=c.id
ORDER BY c.guid;
DROP TEMPORARY TABLE training_aoe_positions;