param([string]$BackupDirectory = $PSScriptRoot, [switch]$StartWorld)
$ErrorActionPreference = 'Stop'
$backupPath = [IO.Path]::GetFullPath($BackupDirectory).TrimEnd('\')
$manifest = ConvertFrom-Json ([IO.File]::ReadAllText((Join-Path $backupPath 'manifest.json')))
$targetPath = [IO.Path]::GetFullPath($manifest.Target).TrimEnd('\')
if ($targetPath -ne 'I:\C60WOW\newcmangos' -or
    !$backupPath.StartsWith($targetPath + '\backups\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Rollback target is outside the approved deployment directory.'
}
$executable = Join-Path $targetPath 'mangosd.exe'
$oldExecutable = Join-Path $backupPath 'mangosd.exe.before'
if (Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq 'mangosd.exe' -and $_.ExecutablePath -eq $executable
}) { throw 'Stop the target world cleanly before rollback. No process was stopped by this script.' }
if ((Get-FileHash -LiteralPath $oldExecutable).Hash -ne $manifest.OriginalExecutableSHA256) {
    throw 'Original executable backup hash mismatch.'
}
if ((Get-FileHash -LiteralPath $executable).Hash -ne $manifest.DeployedExecutableSHA256) {
    throw 'The deployed program was changed later. Review before overwriting it.'
}
$sqlHelper = Join-Path $backupPath 'Invoke-TargetWorldSql.ps1'
$sqlFile = Join-Path $backupPath '02-rollback-binding.sql'
$raw = & $sqlHelper -SqlFile $sqlFile -RequireWorldStopped -OutputJson (Join-Path $backupPath 'sql-rollback-result.json')
$result = ConvertFrom-Json ($raw -join "`n")
Copy-Item -LiteralPath $oldExecutable -Destination $executable -Force
if ((Get-FileHash -LiteralPath $executable).Hash -ne $manifest.OriginalExecutableSHA256) {
    throw 'Restored program hash mismatch.'
}
$manifest | Add-Member -NotePropertyName RollbackUTC -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $backupPath 'manifest.json') -Encoding utf8
if ($StartWorld) {
    $process = Start-Process -FilePath $executable -WorkingDirectory $targetPath -WindowStyle Hidden -PassThru -ArgumentList @('-c','mangosd.conf')
    Write-Output "Restored world start requested, PID $($process.Id). Check Server.log and port 8085."
}
Write-Output "Restored original program; targeted SQL restored $($result[1].Rows[0][0]) binding(s)."
