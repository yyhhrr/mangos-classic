-- Restore the inspected baseline: Entry 7897 had an empty AIName and ScriptName.
-- Run only if 01_bind_alarm.sql was applied and the baseline backup agrees.
-- A later custom script/AI binding will not be overwritten.

UPDATE creature_template
SET ScriptName = ''
WHERE Entry = 7897
  AND AIName = ''
  AND ScriptName = 'npc_alarm_a_bomb_2600';

SELECT ROW_COUNT() AS bindings_restored;
SELECT Entry, Name, AIName, ScriptName, UnitFlags, Faction
FROM creature_template WHERE Entry = 7897;
