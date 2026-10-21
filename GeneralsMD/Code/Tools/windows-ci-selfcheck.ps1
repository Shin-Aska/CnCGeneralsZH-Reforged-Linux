param(
    [Parameter(Mandatory = $true)] [string] $EvidenceDir,
    [ValidateSet('Baseline', 'Red', 'All', 'Environment')] [string] $Mode = 'All',
    [string] $CiScript = ''
)

$ErrorActionPreference = 'Stop'
if ($CiScript -eq '') { $CiScript = Join-Path $PSScriptRoot '..\..\..\windows-ci.ps1' }
if (Test-Path -LiteralPath $EvidenceDir) { throw 'EvidenceDir must be fresh' }
$EvidenceDir = [IO.Path]::GetFullPath($EvidenceDir)
$null = New-Item -ItemType Directory -Path $EvidenceDir
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$cacheLine = Select-String -LiteralPath (Join-Path $repo 'build64\CMakeCache.txt') -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
if (-not $cacheLine) { throw 'A configured CMake cache is required; no build is performed' }
$cmake = $cacheLine.Matches[0].Groups[1].Value
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw 'Configured CTest is missing' }
$powershell = (Get-Process -Id $PID).Path
. (Join-Path $PSScriptRoot 'windows-ci-gpu.ps1')
$gpuNames = @('dx9_smoke', 'dx9_smoke_msaa', 'test_dx11device', 'test_dx11pipeline',
    'test_dx11resource', 'test_dx11backend', 'test_dx11post', 'test_dx11twin',
    'test_dx11texture', 'test_dx11water_scene', 'test_dx11water_surface',
    'test_dx11water_future', 'test_ffvertexd3d9', 'test_pixelcentre',
    'test_d3dx9portable_oracle', 'test_water_grid')
$cpuNames = @('test_cpu_alpha', 'test_ffshadercompile', 'test_dx11state', 'test_dx11layout', 'test_dx11water')
$results = @()

function Get-EnvironmentReceipt {
    $variables = Get-CiEnvironment @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_PLATFORM', 'PATH')
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        foreach ($variable in @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_PLATFORM', 'PATH')) {
            $value = $variables[$variable].Value
            [pscustomobject]@{ Name = $variable; Exists = $variables[$variable].Exists;
                Length = if ($null -eq $value) { $null } else { $value.Length };
                Sha256 = if ($null -eq $value) { $null } else { [BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($value))) } }
        }
    } finally { $hash.Dispose() }
}

function Invoke-Scenario([string] $Name, [string[]] $Names, [string] $Phase, [string] $SkipName = '',
    [string] $SkipMessage = '', [switch] $SameProcess) {
    $scenario = Join-Path $EvidenceDir $Name
    $null = New-Item -ItemType Directory -Path $scenario
    Copy-Item -LiteralPath $CiScript -Destination (Join-Path $scenario 'windows-ci.ps1')
    $helper = Join-Path $PSScriptRoot 'windows-ci-gpu.ps1'
    if (Test-Path -LiteralPath $helper) {
        $helperDir = Join-Path $scenario 'GeneralsMD\Code\Tools'
        $null = New-Item -ItemType Directory -Path $helperDir -Force
        Copy-Item -LiteralPath $helper -Destination $helperDir
    }
    $build = Join-Path $scenario 'build64'
    $events = Join-Path $scenario 'events'
    $null = New-Item -ItemType Directory -Path $build, $events
    [IO.File]::WriteAllText((Join-Path $build 'CMakeCache.txt'), "CMAKE_COMMAND:INTERNAL=$cmake`n")
    $probe = Join-Path $scenario 'cpu-probe.ps1'
    @'
param([string] $Name, [string] $Events, [int] $Result = 0, [string] $Message = '')
$started = [DateTime]::UtcNow.Ticks
Start-Sleep -Milliseconds 350
$record = @{ Name = $Name; Pid = $PID; Start = $started; End = [DateTime]::UtcNow.Ticks;
    Unattended = $env:ZH_UNATTENDED; UserData = $env:ZH_USER_DATA_DIR }
$record | ConvertTo-Json -Compress | Set-Content -LiteralPath (Join-Path $Events ($Name + '-' + $PID + '-' + [guid]::NewGuid().ToString('N') + '.json')) -Encoding UTF8
if ($Message -ne '') { Write-Output $Message }
exit $Result
'@ | Set-Content -LiteralPath $probe -Encoding UTF8
    $definitions = @()
    foreach ($testName in $Names) {
        $code = if ($testName -eq $SkipName) { 77 } else { 0 }
        $words = @($powershell, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $probe,
            '-Name', $testName, '-Events', $events, '-Result', "$code")
        if ($SkipMessage -ne '') { $words += @('-Message', $SkipMessage) }
        $arguments = @($words | ForEach-Object { '"' + $_.Replace('\', '/').Replace('"', '\"') + '"' }) -join ' '
        $definitions += "add_test($testName $arguments)"
        if ($testName -eq $SkipName) { $definitions += "set_tests_properties($testName PROPERTIES SKIP_RETURN_CODE 77)" }
    }
    [IO.File]::WriteAllText((Join-Path $build 'CTestTestfile.cmake'), ($definitions -join "`n") + "`n")
    $resultFile = Join-Path $scenario 'desktop-result.json'
    $work = Join-Path $scenario 'work'
    $command = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $scenario 'windows-ci.ps1'),
        '-SkipBuild', '-Phase', $Phase, '-WorkDir', $work)
    if ($Phase -eq 'desktop') { $command += @('-ResultFile', $resultFile) }
    $parallelState = Get-CiEnvironment @('CTEST_PARALLEL_LEVEL')
    try {
        $env:CTEST_PARALLEL_LEVEL = '8'
        $environmentBefore = @(Get-EnvironmentReceipt)
        if ($SameProcess) {
            $lines = @(& (Join-Path $scenario 'windows-ci.ps1') -SkipBuild -Phase $Phase -WorkDir $work -Platform x64 -DllPath $scenario 2>&1 | ForEach-Object { "$_" })
        } else {
            $nativePreference = $ErrorActionPreference
            try {
                # Windows PowerShell turns redirected native stderr into error records.
                # Expected child failures must reach the exit-code assertions below.
                $ErrorActionPreference = 'Continue'
                $lines = @(& $powershell @command 2>&1 | ForEach-Object { "$_" })
            } finally { $ErrorActionPreference = $nativePreference }
        }
        $exitCode = $LASTEXITCODE
        $environmentAfter = @(Get-EnvironmentReceipt)
    } finally {
        Restore-CiEnvironment $parallelState
    }
    $lines | Set-Content -LiteralPath (Join-Path $scenario 'cli.log') -Encoding UTF8
    $records = @(Get-ChildItem -LiteralPath $events -Filter '*.json' | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json })
    $gpu = @($records | Where-Object { $_.Name -in $gpuNames })
    $overlap = @()
    for ($a = 0; $a -lt $gpu.Count; $a++) {
        for ($b = $a + 1; $b -lt $gpu.Count; $b++) {
            if ($gpu[$a].Start -lt $gpu[$b].End -and $gpu[$b].Start -lt $gpu[$a].End) {
                $overlap += "$($gpu[$a].Name)/$($gpu[$b].Name)"
            }
        }
    }
    $desktop = if (Test-Path -LiteralPath $resultFile) { Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json } else { $null }
    $cpuResult = Join-Path $work 'cpu-result.json'
    $cpu = if (Test-Path -LiteralPath $cpuResult) { Get-Content -LiteralPath $cpuResult -Raw | ConvertFrom-Json } else { $null }
    return [pscustomobject]@{ Name = $Name; ExitCode = $exitCode; Records = $records;
        Overlap = $overlap; Desktop = $desktop; Cpu = $cpu; Directory = $scenario;
        EnvironmentBefore = $environmentBefore; EnvironmentAfter = $environmentAfter }
}

function Add-Check([string] $Name, [bool] $Passed, $Evidence) {
    $script:results += [pscustomobject]@{ Name = $Name; Passed = $Passed; Evidence = $Evidence }
}

if ($Mode -eq 'Baseline') {
    $baseline = Invoke-Scenario 'existing-desktop-cli' @('dx9_smoke', 'dx9_smoke_msaa', 'test_dx11device', 'test_cpu_alpha') 'desktop'
    Add-Check 'unchanged-desktop-selects-three-known-tests' ($baseline.ExitCode -eq 0 -and $baseline.Desktop.Gpu -eq 'passed' -and $baseline.Records.Count -eq 3) $baseline
} else {
    if ($Mode -ne 'Environment') {
    $partition = Invoke-Scenario 'partition-and-serialization' ($gpuNames + $cpuNames + @('test_miles_miniaudio')) 'all'
    $counts = @()
    foreach ($testName in $gpuNames) {
        if (@($partition.Records | Where-Object { $_.Name -eq $testName }).Count -ne 1) { $counts += $testName }
    }
    Add-Check 'device-tests-execute-once-serially' ($partition.ExitCode -eq 0 -and $counts.Count -eq 0 -and $partition.Overlap.Count -eq 0) $partition
    $unexpected = @($partition.Records | Where-Object { $_.Name -notin ($gpuNames + $cpuNames) })
    $cpuRecords = @($partition.Records | Where-Object { $_.Name -in $cpuNames })
    $gpuRecords = @($partition.Records | Where-Object { $_.Name -in $gpuNames })
    $lastCpu = ($cpuRecords | Measure-Object -Property End -Maximum).Maximum
    $firstGpu = ($gpuRecords | Measure-Object -Property Start -Minimum).Minimum
    Add-Check 'cpu-phase-preserves-selection-and-finishes-before-gpu' ($unexpected.Count -eq 0 -and $cpuRecords.Count -eq $cpuNames.Count -and $lastCpu -le $firstGpu) $partition.Cpu
    $empty = Invoke-Scenario 'empty-gpu' @('test_cpu_alpha') 'desktop'
    Add-Check 'empty-gpu-is-unavailable' ($empty.Desktop.Gpu -like 'UNAVAILABLE*' -and $empty.ExitCode -ne 0) $empty
    $skipped = Invoke-Scenario 'skipped-gpu' @('dx9_smoke', 'dx9_smoke_msaa', 'test_dx11device') 'desktop' 'test_dx11device'
    Add-Check 'skipped-gpu-is-unavailable' ($skipped.Desktop.Gpu -like 'UNAVAILABLE*' -and $skipped.ExitCode -ne 0) $skipped
    $reported = Invoke-Scenario 'reported-skip-gpu' @('test_dx11texture') 'desktop' '' 'skip: no Direct3D 9 device on this machine'
    Add-Check 'success-exit-with-device-skip-is-unavailable' ($reported.Desktop.Gpu -like 'UNAVAILABLE*' -and $reported.ExitCode -ne 0) $reported
    }
    $saved = Get-CiEnvironment @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_PLATFORM', 'PATH')
    try {
        $env:ZH_UNATTENDED = 'caller-marker'
        $env:ZH_USER_DATA_DIR = 'caller-private-marker'
        $env:ZH_PLATFORM = 'caller-platform-marker'
        $beforePath = $env:PATH
        $environment = Invoke-Scenario 'cpu-env-restoration' @('test_cpu_alpha', 'test_dx11water_scene') 'cpu' -SameProcess
        $unchanged = $env:ZH_UNATTENDED -ceq 'caller-marker' -and $env:ZH_USER_DATA_DIR -ceq 'caller-private-marker' -and
            $env:ZH_PLATFORM -ceq 'caller-platform-marker' -and $env:PATH -ceq $beforePath
        Add-Check 'cpu-only-cli-restores-caller-environment' ($unchanged -and $environment.ExitCode -eq 0 -and $environment.Records.Count -eq 1 -and $environment.Records[0].Name -eq 'test_cpu_alpha') $environment
        $failure = Invoke-Scenario 'cpu-env-failure-restoration' @('test_dx11water_scene') 'cpu' -SameProcess
        $unchanged = $env:ZH_UNATTENDED -ceq 'caller-marker' -and $env:ZH_USER_DATA_DIR -ceq 'caller-private-marker' -and
            $env:ZH_PLATFORM -ceq 'caller-platform-marker' -and $env:PATH -ceq $beforePath
        Add-Check 'failed-cpu-cli-restores-caller-environment' ($unchanged -and $failure.ExitCode -ne 0 -and $failure.Records.Count -eq 0) $failure
        foreach ($variable in @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_PLATFORM')) {
            Remove-Item -LiteralPath "Env:\$variable" -ErrorAction SilentlyContinue
        }
        $absent = Invoke-Scenario 'cpu-env-absence-restoration' @('test_cpu_alpha') 'cpu' -SameProcess
        $unchanged = $null -eq [Environment]::GetEnvironmentVariable('ZH_UNATTENDED', 'Process') -and
            $null -eq [Environment]::GetEnvironmentVariable('ZH_USER_DATA_DIR', 'Process') -and
            $null -eq [Environment]::GetEnvironmentVariable('ZH_PLATFORM', 'Process') -and $env:PATH -ceq $beforePath
        Add-Check 'cpu-cli-restores-absent-environment-values' ($unchanged -and $absent.ExitCode -eq 0) $absent
        $emptyState = @{}
        foreach ($variable in @('ZH_UNATTENDED', 'ZH_USER_DATA_DIR', 'ZH_PLATFORM')) {
            $emptyState[$variable] = [pscustomobject]@{ Exists = $true; Value = '' }
        }
        Restore-CiEnvironment $emptyState
        $empty = Invoke-Scenario 'cpu-env-empty-restoration' @('test_cpu_alpha') 'cpu' -SameProcess
        $emptyValues = @($empty.EnvironmentBefore | Where-Object { $_.Name -ne 'PATH' -and $_.Exists -and $_.Length -eq 0 })
        $sameEmpty = ($empty.EnvironmentBefore | ConvertTo-Json -Compress) -ceq ($empty.EnvironmentAfter | ConvertTo-Json -Compress)
        Add-Check 'cpu-cli-restores-present-empty-environment-values' ($empty.ExitCode -eq 0 -and $emptyValues.Count -eq 3 -and $sameEmpty) $empty
        Restore-CiEnvironment $emptyState
        $emptyFailure = Invoke-Scenario 'cpu-env-empty-failure-restoration' @('test_dx11water_scene') 'cpu' -SameProcess
        $emptyValues = @($emptyFailure.EnvironmentBefore | Where-Object { $_.Name -ne 'PATH' -and $_.Exists -and $_.Length -eq 0 })
        $sameEmpty = ($emptyFailure.EnvironmentBefore | ConvertTo-Json -Compress) -ceq ($emptyFailure.EnvironmentAfter | ConvertTo-Json -Compress)
        Add-Check 'failed-cpu-cli-restores-present-empty-environment-values' ($emptyFailure.ExitCode -ne 0 -and $emptyValues.Count -eq 3 -and $sameEmpty) $emptyFailure
    } finally {
        Restore-CiEnvironment $saved
    }
    $tokens = $null; $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($CiScript, [ref]$tokens, [ref]$parseErrors)
    $desktopBody = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-DesktopPart' }, $true).Body.GetScriptBlock()
    $sessionResult = & {
        function Get-Process { param([int] $Id) return [pscustomobject]@{ SessionId = 0 } }
        & $desktopBody
    }
    Add-Check 'session-zero-rejects-direct-desktop-phase' ($sessionResult.Gpu -like 'UNAVAILABLE*' -and $sessionResult.Log.Count -eq 0) @{ Scope = 'actual function with session query stub only'; Result = $sessionResult }
}
$passed = @($results | Where-Object { -not $_.Passed }).Count -eq 0
@{ Scope = 'CPU synthetic CTest commands; no game or GPU'; PowerShellVersion = $PSVersionTable.PSVersion.ToString();
    Mode = $Mode; Passed = $passed; Results = $results } |
    ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'results.json') -Encoding UTF8
$results | Select-Object Name, Passed | Format-Table -AutoSize
exit $(if ($passed) { 0 } else { 1 })
