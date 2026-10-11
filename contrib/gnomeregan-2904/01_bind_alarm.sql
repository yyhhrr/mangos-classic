-- Quest 2904 / Alarm-a-bomb 2600: follow, timeout explosion and successful disarm.
-- Apply only together with a mangosd build registering npc_alarm_a_bomb_2600.
-- Enables the user-approved approximation: spell 9874 and VMaNGOS 300s/20s timers.
-- This is not a claim that the values reproduce original retail behavior.
-- Back up Entry 7897 first. Existing custom script/AI bindings are left untouched.

UPDATE creature_template
SET ScriptName = 'npc_alarm_a_bomb_2600'
WHERE Entry = 7897
  AND AIName = ''
  AND ScriptName = ''
  AND (SELECT COUNT(*) FROM broadcast_text WHERE Id IN (3881, 3927, 3929, 3949, 3950)) = 5;

SELECT ROW_COUNT() AS bindings_changed;
SELECT Entry, Name, AIName, ScriptName, UnitFlags, Faction
FROM creature_template WHERE Entry IN (7850, 7897);
