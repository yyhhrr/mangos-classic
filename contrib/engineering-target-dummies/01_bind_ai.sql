-- CMaNGOS Classic 1.12: install the source fix before applying this file.
-- No creature/item loot rows, probabilities, flags, levels or factions change.
-- Only bindings inspected on classicmangos1 on 2026-10-11 are eligible.
UPDATE creature_template
SET AIName = '', ScriptName = 'npc_engineering_target_dummy'
WHERE (Entry IN (2673,12426) AND AIName = 'EventAI' AND ScriptName = '')
   OR (Entry = 2674 AND AIName = 'EventAI' AND ScriptName = 'npc_advanced_target_dummy');
SELECT ROW_COUNT() AS changed_bindings;

-- Attach an existing material table only when currently unbound. This does not
-- create missing materials or overwrite a customized LootId. The inspected
-- live DB has no table for 2673 or 12426, so neither changes there.
UPDATE creature_template AS c
SET c.LootId = c.Entry
WHERE c.Entry IN (2673,2674,12426)
  AND c.ScriptName = 'npc_engineering_target_dummy' AND c.AIName = ''
  AND c.LootId = 0
  AND EXISTS (SELECT 1 FROM creature_loot_template AS l WHERE l.entry = c.Entry);
SELECT ROW_COUNT() AS attached_existing_loot_tables;
