-- Only reverses the two templates created by 01_templates.sql.
-- This script intentionally refuses deletion if either entry has persistent spawns.
-- If spawns were added later, back up and remove only their recorded GUIDs first.
-- MyISAM tables do not provide transaction rollback. Review and back up before execution.
SET @training_dummy_rollback_safe = (
    SELECT COUNT(*) = 0 FROM creature_spawn_entry WHERE entry IN (900000, 900001)
)
AND (SELECT COUNT(*) = 0 FROM creature WHERE id IN (900000, 900001));
SELECT guid,id FROM creature WHERE id IN (900000, 900001);
SELECT guid, entry FROM creature_spawn_entry WHERE entry IN (900000, 900001);

DELETE FROM creature_template
WHERE @training_dummy_rollback_safe
  AND ScriptName = 'npc_training_dummy'
  AND ((Entry = 900000 AND Name = 'Training Dummy (34)')
    OR (Entry = 900001 AND Name = 'Training Dummy (63)'));

SELECT ROW_COUNT() AS templates_deleted,
       IF(@training_dummy_rollback_safe, 'Verify remaining rows below', 'BLOCKED: remove recorded spawns first') AS result;
SELECT Entry, Name, ScriptName FROM creature_template WHERE Entry IN (900000, 900001);

