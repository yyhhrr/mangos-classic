-- Read-only preflight. Save results before applying the companion update.
SELECT Entry, Name, AIName, ScriptName, UnitFlags, Faction
FROM creature_template WHERE Entry IN (7850, 7897);
SELECT Id, Text, ChatTypeID, LanguageID
FROM broadcast_text WHERE Id IN (3881, 3927, 3929, 3949, 3950);
SELECT guid, id, map, MovementType
FROM creature WHERE id = 7897;
SELECT Entry, PathId, Point, PositionX, PositionY, PositionZ, WaitTime, ScriptId
FROM creature_movement_template WHERE Entry = 7897 ORDER BY PathId, Point;
SELECT entry, LimitTime, StartScript, CompleteScript
FROM quest_template WHERE entry = 2904;
