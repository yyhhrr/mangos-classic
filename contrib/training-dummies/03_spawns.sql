-- Target: checked CMaNGOS Classic world schema, not AzerothCore schema.
-- Two WotLK reference points verified against Classic raw terrain: Z is about 0.08 yd above ground.
-- No random spawn entries, spawn data, events, pools or extra NPCs are created.
-- Run while the target world is stopped; back up first. Do not use --force.
SET @training_spawn_ids_free =
    (SELECT COUNT(*) = 0 FROM creature WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM creature_spawn_entry WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM creature_spawn_data WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM game_event_creature WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM pool_creature WHERE guid IN (9000421,9000422));
SET @training_templates_valid = (
    SELECT COUNT(*) = 2 FROM creature_template
    WHERE ScriptName='npc_training_dummy'
      AND ((Entry=900000 AND MinLevel=34 AND MaxLevel=34)
        OR (Entry=900001 AND MinLevel=63 AND MaxLevel=63))
);
INSERT INTO creature
    (guid,id,map,spawnMask,position_x,position_y,position_z,orientation,
     spawntimesecsmin,spawntimesecsmax,spawndist,MovementType)
SELECT 9000421,900000,1,1,9992.76,2259.30,1330.94,3.80482,120,120,0,0
FROM DUAL WHERE @training_spawn_ids_free AND @training_templates_valid
UNION ALL
SELECT 9000422,900001,1,1,9908.89,2251.49,1330.83,5.82940,120,120,0,0
FROM DUAL WHERE @training_spawn_ids_free AND @training_templates_valid;
SELECT ROW_COUNT() AS spawns_inserted,
       @training_spawn_ids_free AS ids_free,
       @training_templates_valid AS templates_valid;
SELECT guid,id,map,position_x,position_y,position_z,orientation,spawndist,MovementType
FROM creature WHERE guid IN (9000421,9000422);

