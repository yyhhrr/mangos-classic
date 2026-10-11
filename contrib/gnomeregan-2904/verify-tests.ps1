param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (!$BuildDirectory) {
    $BuildDirectory = Join-Path $taskRepo 'audit-reports\gnomeregan-2904-20261010\tests-build'
}
$taskSource = Join-Path $taskRepo 'src\game\AI\ScriptDevAI\scripts\eastern_kingdoms\gnomeregan\gnomeregan.cpp'
& cmake -S (Join-Path $PSScriptRoot 'tests') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DSCRIPT_UNDER_TEST=$taskSource"
if ($LASTEXITCODE) { throw 'Test configuration failed.' }
& cmake --build $BuildDirectory --config Release --parallel 1 -- /nr:false
if ($LASTEXITCODE) { throw 'Test compilation failed.' }
& ctest --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Escort regression tests failed.' }
