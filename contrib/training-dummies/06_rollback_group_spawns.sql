-- Stop world before rollback so its live spawn cache cannot preserve removed NPCs.
-- Removes only the five exact recorded spawn identities. Original two single targets are never selected.
-- Does not change templates or existing single-target saved levels.
SET NAMES utf8;
DROP TEMPORARY TABLE IF EXISTS training_aoe_positions;
CREATE TEMPORARY TABLE training_aoe_positions (
    guid INT UNSIGNED PRIMARY KEY,
    x DECIMAL(40,20), y DECIMAL(40,20), z DECIMAL(40,20), o DECIMAL(40,20)
);
INSERT INTO training_aoe_positions VALUES
    (9000431,9982.51,2262.49,1333.40,4.69494),
    (9000432,9976.21,2262.37,1334.50,4.69494),
    (9000433,9979.36,2262.43,1334.00,4.69494),
    (9000434,9979.30,2265.58,1333.91,4.69494),
    (9000435,9979.42,2259.28,1333.85,4.69494);
SET @training_aoe_rollback_safe = 1;
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM creature_spawn_entry r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM creature_spawn_data r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM game_event_creature r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM pool_creature r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM creature_addon r JOIN training_aoe_positions p ON r.guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM creature_linking r JOIN training_aoe_positions p ON r.guid=p.guid OR r.master_guid=p.guid);
SET @training_aoe_rollback_safe = @training_aoe_rollback_safe AND (SELECT COUNT(*)=0 FROM training_dummy_settings r JOIN training_aoe_positions p ON r.guid=p.guid WHERE r.entry<>900000);
-- Remove only per-GUID levels saved for the exact group spawns being removed.
DELETE s FROM training_dummy_settings s JOIN creature c ON c.guid=s.guid AND c.id=s.entry
JOIN training_aoe_positions p ON p.guid=c.guid JOIN creature_template t ON t.Entry=c.id
WHERE @training_aoe_rollback_safe AND s.entry=900000 AND t.ScriptName='npc_training_dummy'
  AND c.map=1 AND c.spawnMask=1 AND c.position_x=p.x AND c.position_y=p.y
  AND c.position_z=p.z AND c.orientation=p.o AND c.spawndist=0 AND c.MovementType=0;
SELECT ROW_COUNT() AS group_saved_levels_deleted;
DELETE c FROM creature c JOIN training_aoe_positions p ON p.guid=c.guid
JOIN creature_template t ON t.Entry=c.id
WHERE @training_aoe_rollback_safe AND c.id=900000 AND t.ScriptName='npc_training_dummy'
  AND c.map=1 AND c.spawnMask=1 AND c.position_x=p.x AND c.position_y=p.y
  AND c.position_z=p.z AND c.orientation=p.o AND c.spawndist=0 AND c.MovementType=0;
SELECT ROW_COUNT() AS group_spawns_deleted,@training_aoe_rollback_safe AS rollback_safe;
SELECT c.guid,c.id,c.map FROM creature c JOIN training_aoe_positions p ON p.guid=c.guid;
DROP TEMPORARY TABLE training_aoe_positions;