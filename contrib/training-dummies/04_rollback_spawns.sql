-- Stop the target world first. Removes only the two recorded training spawn identities.
-- If new event/pool/random/spawn-data references were added later, stop and review them.
SET @training_spawn_rollback_safe =
    (SELECT COUNT(*) = 0 FROM creature_spawn_entry WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM creature_spawn_data WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM game_event_creature WHERE guid IN (9000421,9000422))
    AND (SELECT COUNT(*) = 0 FROM pool_creature WHERE guid IN (9000421,9000422));
DELETE c FROM creature c JOIN creature_template t ON t.Entry=c.id
WHERE @training_spawn_rollback_safe AND t.ScriptName='npc_training_dummy'
  AND ((c.guid=9000421 AND c.id=900000 AND t.Name='Training Dummy (34)')
    OR (c.guid=9000422 AND c.id=900001 AND t.Name='Training Dummy (63)'));
SELECT ROW_COUNT() AS spawns_deleted, @training_spawn_rollback_safe AS safe;
SELECT guid,id,map FROM creature WHERE guid IN (9000421,9000422);

