# Offline checks: compile the actual AI with interface stubs and validate the SQL shape.
# This does not start a server, connect to a database or replace any deployment files.
param(
    [string]$BuildDirectory = (Join-Path ([IO.Path]::GetTempPath()) ('cmangos-dummy-tests-' + [guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$taskSource = [IO.File]::ReadAllText((Join-Path $taskRepo 'src\game\AI\ScriptDevAI\scripts\world\npcs_special.cpp'))
$taskStart = $taskSource.IndexOf('struct npc_training_dummyAI : public ScriptedAI')
$taskEnd = $taskSource.IndexOf('void AddSC_npcs_special()', $taskStart)
if ($taskStart -lt 0 -or $taskEnd -lt 0) { throw 'Actual training AI not found' }
$taskAi = $taskSource.Substring($taskStart, $taskEnd - $taskStart)

$taskFixture = @'
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
using uint32 = std::uint32_t;
enum DamageEffectType { DIRECT_DAMAGE, DOT, INSTAKILL };
enum AuraType { SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_DAMAGE_PERCENT,
    SPELL_AURA_PERIODIC_LEECH, SPELL_AURA_PERIODIC_MANA_LEECH, SPELL_AURA_POWER_BURN_MANA };
enum ReactState { REACT_PASSIVE, REACT_AGGRESSIVE };
struct SpellEntry {};
struct Unit {
    Unit* beneficiary = nullptr;
    bool combat = false;
    int mana = 72, rage = 31, energy = 46;
    Unit* GetBeneficiaryPlayer() { return beneficiary; }
};
struct CombatManager {
    uint32 timer = 0;
    uint32 GetCombatTimer() const { return timer; }
};
struct HostileRefManager {
    bool cleared = false;
    void deleteReferences() { cleared = true; }
};
struct Creature : Unit {
    uint32 maximum = 10000000, health = 80;
    uint32 resets = 0;
    bool noAssist = false, noXP = false, noLoot = false, noSkill = false, tagCleared = false;
    bool combatStopped = false, aurasRemoved = false;
    CombatManager manager;
    HostileRefManager refs;
    std::set<AuraType> auras;
    void SetNoCallAssistance(bool b) { noAssist = b; }
    void SetNoXP(bool b) { noXP = b; }
    void SetNoLoot(bool b) { noLoot = b; }
    void SetNoWeaponSkillGain(bool b) { noSkill = b; }
    void SetHealth(uint32 n) { health = n; }
    uint32 GetMaxHealth() const { return maximum; }
    bool HasAuraType(AuraType type) const { return auras.count(type) != 0; }
    bool IsInCombat() const { return combat; }
    CombatManager& GetCombatManager() { return manager; }
    HostileRefManager& getHostileRefManager() { return refs; }
    void RemoveAllAurasOnEvade() { aurasRemoved = true; auras.clear(); }
    void CombatStop(bool) { combatStopped = true; combat = false; manager.timer = 0; ++resets; }
    void SetLootRecipient(Unit*) { tagCleared = true; }
};
struct CreatureAI {
    Creature* m_creature;
    ReactState react = REACT_AGGRESSIVE;
    bool movement = true, melee = true, rooted = false, deathProtection = false;
    explicit CreatureAI(Creature* c) : m_creature(c) {}
    virtual ~CreatureAI() = default;
    virtual void Reset() {}
    virtual void MoveInLineOfSight(Unit*) {}
    virtual void AttackStart(Unit*) {}
    virtual void AttackedBy(Unit*) {}
    virtual void SpellHit(Unit*, SpellEntry const*) {}
    virtual void DamageTaken(Unit*, uint32&, DamageEffectType, SpellEntry const*) {}
    virtual void EnterEvadeMode() {}
    virtual void UpdateAI(uint32) {}
    void SetReactState(ReactState s) { react = s; }
    void SetCombatMovement(bool s) { movement = s; }
    void SetMeleeEnabled(bool s) { melee = s; }
    void SetAIImmobilizedState(bool s) { rooted = s; }
    void SetDeathPrevention(bool s) { deathProtection = s; }
};
using ScriptedAI = CreatureAI;
void require(bool value, char const* label) { if (!value) throw std::runtime_error(label); }
'@

$taskCases = @'
int main() {
    try {
        Unit player; player.beneficiary = &player; player.combat = true;
        Unit pet; pet.beneficiary = &player;
        Unit otherPlayer; otherPlayer.beneficiary = &otherPlayer;
        SpellEntry spell;

        Creature c; npc_training_dummyAI ai(&c);
        require(ai.react == REACT_PASSIVE && !ai.movement && !ai.melee && ai.rooted,
            "passive stationary target");
        require(ai.deathProtection && c.noAssist && c.noXP && c.noLoot && c.noSkill,
            "death and reward safeguards");
        ai.MoveInLineOfSight(&player); ai.AttackStart(&player); ai.UpdateAI(60000);
        require(c.resets == 0 && c.health == c.maximum, "idle has no repeated reset");

        uint32 damage = 1234;
        c.health = 1; ai.DamageTaken(&player, damage, DIRECT_DAMAGE, &spell);
        require(damage == 1234 && c.health == c.maximum, "damage is not converted to absorption");
        require(player.mana == 72 && player.rage == 31 && player.energy == 46,
            "player resource state remains untouched");
        ai.UpdateAI(9999);
        require(c.resets == 0, "no early cleanup");
        ai.UpdateAI(1);
        require(c.resets == 1 && c.aurasRemoved && c.refs.cleared && c.tagCleared,
            "cleanup releases only target state");
        require(player.combat, "player combat is not globally cleared");

        Creature ongoing; npc_training_dummyAI ongoingAI(&ongoing);
        for (int i = 0; i < 20; ++i) {
            uint32 hit = 777;
            ongoingAI.DamageTaken(i % 2 ? &pet : &otherPlayer, hit, DOT, &spell);
            ongoingAI.UpdateAI(9000);
            require(hit == 777 && ongoing.resets == 0, "players and pets extend activity");
        }
        ongoingAI.UpdateAI(1000);
        require(ongoing.resets == 1, "cleanup after the last participant stops");

        for (AuraType type : { SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_DAMAGE_PERCENT,
                SPELL_AURA_PERIODIC_LEECH, SPELL_AURA_PERIODIC_MANA_LEECH, SPELL_AURA_POWER_BURN_MANA }) {
            Creature delayed; npc_training_dummyAI delayedAI(&delayed);
            delayedAI.SpellHit(&player, &spell); delayed.auras.insert(type);
            delayedAI.UpdateAI(60000);
            require(delayed.resets == 0, "long-period effects must reach their tick");
            uint32 tick = 500;
            delayedAI.DamageTaken(&player, tick, DOT, &spell);
            delayed.auras.clear(); delayedAI.UpdateAI(9999);
            require(delayed.resets == 0 && tick == 500, "last tick extends timeout");
            delayedAI.UpdateAI(1);
            require(delayed.resets == 1, "reset after long-period effect ends");
        }

        Creature missed; npc_training_dummyAI missedAI(&missed);
        missed.combat = true; missed.manager.timer = 100;
        missedAI.UpdateAI(10000);
        require(missed.resets == 0, "missed hostile attempt still has core combat time");
        missed.manager.timer = 0; missedAI.UpdateAI(1);
        require(missed.resets == 1, "miss-only session eventually ends");

        Creature attempts; npc_training_dummyAI attemptsAI(&attempts);
        attemptsAI.AttackedBy(&player); attemptsAI.UpdateAI(9999);
        attemptsAI.SpellHit(&pet, &spell); attemptsAI.UpdateAI(9999);
        require(attempts.resets == 0, "attacks and spells refresh the idle window");
        attemptsAI.UpdateAI(1); require(attempts.resets == 1, "attempt session ends");

        Creature admin; npc_training_dummyAI adminAI(&admin);
        admin.health = 1; uint32 instant = 1;
        adminAI.DamageTaken(&player, instant, INSTAKILL, &spell);
        require(admin.health == 1 && instant == 1, "GM instant kill is not rewritten");
        std::cout << "PASS: actual AI boundary cases (interface stubs, not server integration)\n";
        return 0;
    } catch (std::exception const& e) {
        std::cerr << "FAIL: " << e.what() << "\n"; return 1;
    }
}
'@

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$taskCpp = Join-Path $BuildDirectory 'training_dummy_checks.cpp'
[IO.File]::WriteAllText($taskCpp, $taskFixture + "`n" + $taskAi + "`n" + $taskCases, (New-Object Text.UTF8Encoding($false)))
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'Visual Studio C++ tools not found' }
$taskVcvars = Join-Path $taskVs 'VC\Auxiliary\Build\vcvars64.bat'
$taskCommand = '"' + $taskVcvars + '" >nul && cl /nologo /EHsc /std:c++20 /W4 /WX training_dummy_checks.cpp /Fe:training_dummy_checks.exe'
Push-Location $BuildDirectory
try {
    & $env:ComSpec /d /s /c $taskCommand
    if ($LASTEXITCODE -ne 0) { throw 'AI harness compilation failed' }
    & (Join-Path $BuildDirectory 'training_dummy_checks.exe')
    if ($LASTEXITCODE -ne 0) { throw 'AI boundary case failed' }
} finally { Pop-Location }

$taskSql = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '01_templates.sql'))
$taskColumnsMatch = [regex]::Match($taskSql, '(?s)INSERT INTO creature_template \((?<columns>.*?)\)\s*SELECT')
if (!$taskColumnsMatch.Success) { throw 'Template columns not found' }
$taskColumns = @($taskColumnsMatch.Groups['columns'].Value.Split(',') | ForEach-Object { $_.Trim() })
$taskCoreSchema = [IO.File]::ReadAllText((Join-Path $taskRepo 'sql\base\mangos.sql'))
$taskDefinition = [regex]::Match($taskCoreSchema, '(?s)CREATE TABLE \x60creature_template\x60\s*\((?<body>.*?)\)\s*ENGINE')
if (!$taskDefinition.Success) { throw 'Current core template schema not found' }
$taskDefinedColumns = @([regex]::Matches($taskDefinition.Groups['body'].Value, '\x60(?<name>[A-Za-z][A-Za-z0-9_]*)\x60\s+\w+') | ForEach-Object { $_.Groups['name'].Value })
foreach ($taskColumn in $taskColumns) {
    if ($taskDefinedColumns -notcontains $taskColumn) { throw ('Unknown current-core SQL field: ' + $taskColumn) }
}
$taskSqlRows = [regex]::Matches($taskSql, '(?m)^SELECT (?<row>90000[01],[^\r\n]+)')
if ($taskSqlRows.Count -ne 2) { throw 'Expected exactly two template rows' }
$taskExpectedLevels = @{ '900000' = '34'; '900001' = '63' }
foreach ($taskRowMatch in $taskSqlRows) {
    $taskValues = @([regex]::Matches($taskRowMatch.Groups['row'].Value, "'[^']*'|[^,]+") | ForEach-Object { $_.Value.Trim().Trim("'") })
    if ($taskValues.Count -ne $taskColumns.Count) { throw 'SQL field/value count mismatch' }
    $taskRow = @{}
    for ($taskIndex = 0; $taskIndex -lt $taskColumns.Count; $taskIndex++) { $taskRow[$taskColumns[$taskIndex]] = $taskValues[$taskIndex] }
    $taskLevel = $taskExpectedLevels[$taskRow['Entry']]
    if (!$taskLevel -or $taskRow['MinLevel'] -ne $taskLevel -or $taskRow['MaxLevel'] -ne $taskLevel) { throw 'Wrong fixed target level' }
    foreach ($taskPair in @(
        @('DisplayId1','1555'), @('DisplayIdProbability1','100'), @('Faction','7'), @('CreatureType','7'), @('UnitClass','1'), @('Rank','0'),
        @('UnitFlags','0'), @('MechanicImmuneMask','1'), @('SchoolImmuneMask','0'),
        @('HealthMultiplier','-1'), @('PowerMultiplier','-1'), @('DamageMultiplier','1'), @('ArmorMultiplier','-1'),
        @('StrengthMultiplier','0'), @('AgilityMultiplier','0'), @('StaminaMultiplier','0'),
        @('IntellectMultiplier','0'), @('SpiritMultiplier','0'),
        @('MinLevelHealth','10000000'), @('MaxLevelHealth','10000000'),
        @('StaticFlags1','1294'), @('StaticFlags2','1073741824'), @('ExtraFlags','133138'),
        @('ScriptName','npc_training_dummy'), @('AIName','')
    )) { if ($taskRow[$taskPair[0]] -ne $taskPair[1]) { throw ('Wrong template field: ' + $taskPair[0]) } }
    $taskExpectedArmor = if ($taskLevel -eq '34') { '1340' } else { '4691' }
    if ($taskRow['Armor'] -ne $taskExpectedArmor) { throw 'Wrong explicit armor baseline' }
    foreach ($taskZero in @('NpcFlags','MinLevelMana','MaxLevelMana','MinLootGold','MaxLootGold','LootId','PickpocketLootId',
        'SkinningLootId','KillCredit1','KillCredit2','ResistanceHoly','ResistanceFire','ResistanceNature',
        'ResistanceFrost','ResistanceShadow','ResistanceArcane','SpellList','MovementType')) {
        if ($taskRow[$taskZero] -ne '0') { throw ('Unexpected resource/reward/resistance: ' + $taskZero) }
    }
}
if ($taskSql -match '(?mi)^\s*(REPLACE|UPDATE|DELETE)\b' -or $taskSql -match '(?mi)^\s*INSERT INTO creature\b') { throw 'Unexpected overwrite/spawn mutation in template SQL' }
if ([regex]::Matches($taskSql, '(?m)^FROM DUAL WHERE @training_dummy_ids_free').Count -ne 2) { throw 'Missing MySQL 5.7 DUAL/collision guard' }
$taskRollback = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '02_rollback_templates.sql'))
if ($taskRollback -notmatch 'creature_spawn_entry' -or $taskRollback -notmatch 'WHERE @training_dummy_rollback_safe' -or
    $taskRollback -notmatch "ScriptName = 'npc_training_dummy'") { throw 'Rollback guard missing' }
Write-Output 'PASS: template fields, fixed levels, damage/reward flags and rollback shape (offline; SQL not executed)'
Write-Output 'PASS: all template columns exist in the current core world schema'
Write-Output ('Harness directory: ' + $BuildDirectory)

