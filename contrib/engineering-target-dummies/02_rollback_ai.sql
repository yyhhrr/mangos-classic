-- Restore only the inspected binding if it still matches this batch.
-- This rollback never deletes material tables or overwrites later custom AI.
UPDATE creature_template
SET AIName = 'EventAI',
    ScriptName = CASE WHEN Entry = 2674 THEN 'npc_advanced_target_dummy' ELSE '' END
WHERE Entry IN (2673,2674,12426)
  AND AIName = '' AND ScriptName = 'npc_engineering_target_dummy';
SELECT ROW_COUNT() AS restored_bindings;

-- No LootId rollback is included: the inspected live DB has no unbound material
-- tables to attach. A different DB must record its own before/after values.
