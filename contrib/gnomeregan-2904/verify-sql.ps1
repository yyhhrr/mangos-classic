param(
    [string]$MySqlDirectory = 'I:\C60WOW\newcmangos\mysql5',
    [string]$ClientLibrary = 'I:\C60WOW\newcmangos\libmysql.dll'
)
$ErrorActionPreference = 'Stop'
# Deliberately uses a disposable, empty-password server on a different port.
# Never reads mangosd.conf and never connects to the deployed databases.
$testPort = 33407
if (Get-NetTCPConnection -LocalPort $testPort -State Listen -ErrorAction SilentlyContinue) {
    throw 'Isolated test port is already in use.'
}
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$testRoot = Join-Path $tempRoot ('gnomeregan2904-sql-' + [Guid]::NewGuid().ToString('N'))
if (![IO.Path]::GetFullPath($testRoot).StartsWith($tempRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Test directory is outside TEMP.'
}
New-Item -ItemType Directory -Path $testRoot | Out-Null
$dataRoot = Join-Path $testRoot 'data'
New-Item -ItemType Directory -Path $dataRoot | Out-Null
$serverExe = Join-Path $MySqlDirectory 'bin\mysqld.exe'
$server = $null
$db = $null
try {
    $initialize = Start-Process -FilePath $serverExe -WindowStyle Hidden -PassThru -ArgumentList @(
        '--no-defaults', '--initialize-insecure', ('--basedir="' + $MySqlDirectory + '"'),
        ('--datadir="' + $dataRoot + '"'), ('--log-error="' + (Join-Path $testRoot 'initialize.log') + '"')
    )
    if (!$initialize.WaitForExit(30000)) { $initialize.Kill(); throw 'Test initialization timed out.' }
    if ($initialize.ExitCode -ne 0) { throw 'Isolated MySQL initialization failed; see test log.' }
    $server = Start-Process -FilePath $serverExe -WindowStyle Hidden -PassThru -ArgumentList @(
        '--no-defaults', ('--basedir="' + $MySqlDirectory + '"'), ('--datadir="' + $dataRoot + '"'),
        '--bind-address=127.0.0.1', "--port=$testPort", '--skip-log-bin', '--event-scheduler=OFF',
        ('--tmpdir="' + $testRoot + '"'), ('--secure-file-priv="' + $testRoot + '"'),
        ('--pid-file="' + (Join-Path $testRoot 'test.pid') + '"'),
        ('--log-error="' + (Join-Path $testRoot 'server.log') + '"')
    )
    $libraryLiteral = $ClientLibrary.Replace('"', '""')
    if (-not ('GnomereganSqlTestClient' -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public sealed class GnomereganSqlTestClient : IDisposable {
 const string Lib = @"$libraryLiteral";
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr mysql_init(IntPtr p);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr mysql_real_connect(IntPtr p,string h,string u,string pw,IntPtr db,uint port,IntPtr socket,ulong flags);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern int mysql_query(IntPtr p,[MarshalAs(UnmanagedType.LPUTF8Str)]string sql);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr mysql_store_result(IntPtr p);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr mysql_fetch_row(IntPtr r);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern void mysql_free_result(IntPtr r);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern int mysql_next_result(IntPtr p);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern uint mysql_errno(IntPtr p);
 [DllImport(Lib, CallingConvention=CallingConvention.Cdecl)] static extern void mysql_close(IntPtr p);
 IntPtr connection;
 public GnomereganSqlTestClient() {
  connection=mysql_init(IntPtr.Zero);
  if(mysql_real_connect(connection,"127.0.0.1","root","",IntPtr.Zero,33407,IntPtr.Zero,65536)==IntPtr.Zero) {
   Dispose(); throw new Exception("Isolated server is not ready.");
  }
 }
 public string Scalar(string sql) {
  if(mysql_query(connection,sql)!=0) throw new Exception("Isolated SQL failed, code "+mysql_errno(connection));
  string first=null;
  do {
   IntPtr result=mysql_store_result(connection);
   if(result!=IntPtr.Zero) {
    try { IntPtr row; while((row=mysql_fetch_row(result))!=IntPtr.Zero) {
     if(first==null) first=Marshal.PtrToStringUTF8(Marshal.ReadIntPtr(row));
    }} finally { mysql_free_result(result); }
   }
   int next=mysql_next_result(connection); if(next<0) break;
   if(next>0) throw new Exception("Isolated SQL batch failed, code "+mysql_errno(connection));
  } while(true);
  return first;
 }
 public void Dispose() { if(connection!=IntPtr.Zero) { mysql_close(connection); connection=IntPtr.Zero; } }
}
"@
    }
    for ($attempt = 0; $attempt -lt 50; $attempt++) {
        try { $db = [GnomereganSqlTestClient]::new(); break } catch {
            if ($server.HasExited) { throw 'Isolated MySQL exited before accepting connections.' }
            Start-Sleep -Milliseconds 200
        }
    }
    if (!$db) { throw 'Isolated MySQL did not become ready.' }
    $identity = $db.Scalar('SELECT @@datadir;')
    if ([IO.Path]::GetFullPath($identity).TrimEnd('\') -ne [IO.Path]::GetFullPath($dataRoot)) {
        throw 'Isolated server datadir mismatch; no fixture SQL executed.'
    }
    if ($db.Scalar('SELECT @@port;') -ne "$testPort") { throw 'Test port mismatch.' }
    $db.Scalar(@'
CREATE DATABASE quest2904_test;
USE quest2904_test;
CREATE TABLE creature_template (
 Entry INT PRIMARY KEY, Name VARCHAR(80), AIName VARCHAR(80), ScriptName VARCHAR(80), UnitFlags INT, Faction INT
);
CREATE TABLE broadcast_text (Id INT PRIMARY KEY);
INSERT INTO creature_template VALUES
 (7850,'Kernobee','','npc_kernobee',33536,35),
 (7897,'Alarm-a-bomb 2600','','',33536,594),
 (84901,'User training dummy','','custom_dummy',33536,35);
INSERT INTO broadcast_text VALUES (3881),(3927),(3929),(3949),(3950);
'@) | Out-Null
    $apply = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '01_bind_alarm.sql'))
    $rollback = [IO.File]::ReadAllText((Join-Path $PSScriptRoot '02_rollback_alarm.sql'))
    $script:checked = 0
    function Assert-Sql([string]$expected, [string]$sql, [string]$description) {
        if ($db.Scalar($sql) -cne $expected) { throw "SQL assertion failed: $description" }
        $script:checked++
        Write-Output "PASS $description"
    }
    Assert-Sql '1' $apply 'first apply binds exactly one NPC'
    Assert-Sql '0' $apply 'second apply is idempotent'
    Assert-Sql '33536/594' "SELECT CONCAT(UnitFlags,'/',Faction) FROM creature_template WHERE Entry=7897;" 'template flags and faction preserved'
    Assert-Sql 'custom_dummy' 'SELECT ScriptName FROM creature_template WHERE Entry=84901;' 'user dummy binding preserved'
    Assert-Sql 'npc_kernobee' 'SELECT ScriptName FROM creature_template WHERE Entry=7850;' 'escort binding preserved'
    Assert-Sql '1' $rollback 'targeted rollback restores inspected baseline'
    Assert-Sql '0' $rollback 'second rollback is idempotent'
    $db.Scalar("UPDATE creature_template SET ScriptName='later_custom' WHERE Entry=7897;") | Out-Null
    Assert-Sql '0' $apply 'existing custom binding is not overwritten'
    Assert-Sql '0' $rollback 'later custom binding is not rolled back'
    $db.Scalar("UPDATE creature_template SET ScriptName='',AIName='EventAI' WHERE Entry=7897;") | Out-Null
    Assert-Sql '0' $apply 'existing EventAI is not overwritten'
    $db.Scalar("UPDATE creature_template SET AIName='' WHERE Entry=7897; DELETE FROM broadcast_text WHERE Id=3949;") | Out-Null
    Assert-Sql '0' $apply 'missing warning text prevents incomplete activation'
    $db.Scalar('INSERT INTO broadcast_text VALUES (3949);') | Out-Null
    Assert-Sql '1' $apply 'restored prerequisites permit reapply'
    Assert-Sql '1' $rollback 'final targeted rollback succeeds'
    Write-Output "$script:checked SQL checks passed on isolated MySQL $($db.Scalar('SELECT @@version;')) at port $testPort."
    Write-Output "Disposable logs and data retained at $testRoot"
} finally {
    if ($db) { $db.Dispose() }
    # Stop only the process started here, after checking its PID file and own directory.
    if ($server -and !$server.HasExited) {
        $pidFile = Join-Path $testRoot 'test.pid'
        if ((Test-Path -LiteralPath $pidFile) -and ([IO.File]::ReadAllText($pidFile).Trim() -eq "$($server.Id)")) {
            Stop-Process -Id $server.Id -Force
        } else {
            throw "Cannot verify own test process PID; retained process $($server.Id) for inspection."
        }
    }
}
