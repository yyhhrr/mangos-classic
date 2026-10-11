param([string]$DbcDirectory = 'I:\C60WOW\newcmangos\data\dbc')
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
function Read-DbcRecord([string]$name, [uint32]$id, [uint32]$expectedFields) {
    $path = Join-Path $DbcDirectory $name
    $bytes = [IO.File]::ReadAllBytes($path)
    if ([Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'WDBC') { throw "Invalid DBC: $name" }
    $records = [BitConverter]::ToUInt32($bytes,4)
    $fields = [BitConverter]::ToUInt32($bytes,8)
    $stride = [BitConverter]::ToUInt32($bytes,12)
    if ($fields -ne $expectedFields -or $stride -ne $fields*4) { throw "Unexpected 1.12 DBC layout: $name" }
    for ($n=0; $n -lt $records; $n++) {
        $offset = 20 + $n*$stride
        if ([BitConverter]::ToUInt32($bytes,$offset) -eq $id) {
            return [pscustomobject]@{Bytes=$bytes; Offset=$offset; Path=$path; Fields=$fields}
        }
    }
    throw "Missing record $id in $name"
}
function U32($record,[int]$field) { [BitConverter]::ToUInt32($record.Bytes,$record.Offset+$field*4) }
function F32($record,[int]$field) { [BitConverter]::ToSingle($record.Bytes,$record.Offset+$field*4) }
$spell = Read-DbcRecord 'Spell.dbc' 9874 173
# These are DBCStructure.h indices, not the smaller SQL column positions.
$dbcChecks = [ordered]@{
    Id=@(0,9874); School=@(1,2); Attributes=@(6,0); AttributesEx=@(7,0);
    AttributesEx2=@(8,0); AttributesEx3=@(9,0); AttributesEx4=@(10,0);
    CastingTimeIndex=@(18,1); RecoveryTime=@(19,0); CategoryRecoveryTime=@(20,0);
    Effect1=@(61,2); Effect2=@(62,0); Effect3=@(63,3);
    EffectDieSides1=@(64,211); EffectBaseDice1=@(67,1); EffectBasePoints1=@(76,594);
    EffectImplicitTargetA1=@(82,22); EffectImplicitTargetB1=@(85,15);
    EffectRadiusIndex1=@(88,8); DmgClass=@(164,0)
}
foreach ($name in $dbcChecks.Keys) {
    $check = $dbcChecks[$name]
    if ((U32 $spell $check[0]) -ne $check[1]) { throw "Spell 9874 DBC mismatch: $name" }
}
if ((F32 $spell 70) -ne 0 -or (F32 $spell 73) -ne 0 -or (F32 $spell 37) -ne 0) {
    throw '9874 has unexpected scaling or projectile speed.'
}
$radius = Read-DbcRecord 'SpellRadius.dbc' 8 4
if ((F32 $radius 1) -ne 5 -or (F32 $radius 2) -ne 0 -or (F32 $radius 3) -ne 5) {
    throw '9874 does not have the expected fixed five-yard radius.'
}
$cast = Read-DbcRecord 'SpellCastTimes.dbc' 1 4
if ((U32 $cast 1) -ne 0) { throw '9874 is not instant.' }
$faction = Read-DbcRecord 'FactionTemplate.dbc' 594 14
if ((U32 $faction 1) -ne 32 -or (U32 $faction 3) -ne 8 -or (U32 $faction 4) -ne 0 -or (U32 $faction 5) -ne 0) {
    throw 'Alarm faction 594 is not the inspected neutral fixture.'
}

$sqlPath = Join-Path $taskRepo 'sql\base\dbc\original_data\Spell.sql'
$sqlText = [IO.File]::ReadAllText($sqlPath)
$schema = [regex]::Match($sqlText,'(?s)CREATE TABLE `spell_template` \((.*?)\) ENGINE').Groups[1].Value
$columns = @([regex]::Matches($schema,'(?m)^\s+`([^`]+)`') | ForEach-Object {$_.Groups[1].Value})
$tuple = [regex]::Match($sqlText,'\(9874,([^\r\n]*?)\)(?:,|;)')
if (!$tuple.Success) { throw 'Missing 9874 original SQL record.' }
$values = ('9874,'+$tuple.Groups[1].Value).Split(',')
if ($columns.Count -ne 158 -or $values.Count -ne 158) { throw 'Unexpected original spell SQL layout.' }
$expectedSql = [ordered]@{School=2; Effect1=2; EffectBasePoints1=594; EffectBaseDice1=1;
    EffectDieSides1=211; EffectImplicitTargetA1=22; EffectImplicitTargetB1=15;
    EffectRadiusIndex1=8; EffectDicePerLevel1=0; EffectRealPointsPerLevel1=0; DmgClass=0}
foreach ($name in $expectedSql.Keys) {
    $index = [Array]::IndexOf($columns,$name)
    if ($index -lt 0 -or [double]$values[$index] -ne $expectedSql[$name]) { throw "Original SQL mismatch: $name" }
}
[pscustomobject]@{
    Spell=9874; RawMinimum=595; RawMaximum=805; School='Fire'; RadiusYards=5;
    TargetA=22; TargetB=15; DamageClass=0; Instant=$true; ProjectileSpeed=0;
    DbcAndOriginalSqlMatch=$true; RuntimeWorldSpellTemplateChecked=$false;
    SpellDbcSHA256=(Get-FileHash $spell.Path -Algorithm SHA256).Hash;
    RadiusDbcSHA256=(Get-FileHash $radius.Path -Algorithm SHA256).Hash;
    Note='Raw spell data verification only; live damage/LOS/grid behavior is not measured.'
} | ConvertTo-Json
