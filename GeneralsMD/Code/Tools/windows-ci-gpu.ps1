# Partition the registered CTest inventory before any test executes. Unknown DX11
# targets are conservative GPU jobs; the three existing CPU targets are explicit.
function Test-CiGpuTest([string] $Name) {
    if ($Name -match '^test_dx11') {
        return $Name -notin @('test_dx11state', 'test_dx11layout', 'test_dx11water')
    }
    return $Name -in @('dx9_smoke', 'dx9_smoke_msaa', 'test_ffvertexd3d9',
        'test_pixelcentre', 'test_d3dx9portable_oracle', 'test_water_grid')
}

function Get-CiEnvironment([string[]] $Names) {
    $variables = [Environment]::GetEnvironmentVariables('Process')
    $saved = @{}
    foreach ($name in $Names) {
        $keys = @($variables.Keys | Where-Object { $_ -ieq $name })
        $saved[$name] = [pscustomobject]@{ Exists = $keys.Count -gt 0;
            Value = if ($keys.Count -gt 0) { $variables[$keys[0]] } else { $null } }
    }
    return $saved
}

function Restore-CiEnvironment($Saved) {
    foreach ($name in $Saved.Keys) {
        $entry = $Saved[$name]
        if (-not $entry.Exists) {
            [Environment]::SetEnvironmentVariable($name, [System.Management.Automation.Language.NullString]::Value, 'Process')
            continue
        }
        [Environment]::SetEnvironmentVariable($name, $entry.Value, 'Process')
        # .NET Framework deletes an empty value, even when it was originally present.
        # Only that demonstrated case needs the Windows API; newer runtimes retain it.
        if ($entry.Value -eq '' -and @([Environment]::GetEnvironmentVariables('Process').Keys) -inotcontains $name) {
            if (-not ('ZhWindowsCiEmptyEnvironment' -as [type])) {
                Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class ZhWindowsCiEmptyEnvironment {
    [DllImport("kernel32.dll", EntryPoint="SetEnvironmentVariableW", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool SetVariable(string name, string value);
    public static void Restore(string name) {
        if (!SetVariable(name, String.Empty)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
}
'@ -ErrorAction Stop
            }
            [ZhWindowsCiEmptyEnvironment]::Restore($name)
        }
    }
}

function Invoke-CiTestLane {
    param(
        [Parameter(Mandatory = $true)] [string] $CTest,
        [Parameter(Mandatory = $true)] [string] $Build,
        [Parameter(Mandatory = $true)] [string] $Config,
        [Parameter(Mandatory = $true)] [string] $WorkDir,
        [ValidateSet('cpu', 'gpu')] [string] $Lane,
        [string] $NoSound = ''
    )
    $result = [ordered]@{ Classification = 'FAILED'; Detail = ''; Planned = 0; Executed = 0;
        TestNames = @(); Skipped = @(); Log = @(); JUnit = $null; ExitCode = $null }
    try {
        if (-not (Test-Path -LiteralPath $CTest -PathType Leaf)) { throw "CTest is missing: $CTest" }
        $inventoryText = @(& $CTest --test-dir $Build -C $Config --show-only=json-v1 2>&1 | ForEach-Object { "$_" })
        if ($LASTEXITCODE -ne 0) { throw "CTest enumeration failed (exit $LASTEXITCODE): $($inventoryText -join ' ')" }
        $inventory = ($inventoryText -join "`n") | ConvertFrom-Json -ErrorAction Stop
        $names = @($inventory.tests | Where-Object {
            $gpu = Test-CiGpuTest $_.name
            if ($Lane -eq 'gpu') { $gpu }
            else { -not $gpu -and ($NoSound -eq '' -or $_.name -notmatch $NoSound) }
        } | ForEach-Object { [string]$_.name })
        $result.Planned = $names.Count
        $result.TestNames = $names
        if ($names.Count -eq 0) {
            $result.Classification = 'UNAVAILABLE'
            $result.Detail = "no $Lane tests are registered"
            return [pscustomobject]$result
        }
        $null = New-Item -ItemType Directory -Force -Path $WorkDir -ErrorAction Stop
        $junit = Join-Path $WorkDir ("$Lane-" + [guid]::NewGuid().ToString('N') + '.xml')
        $pattern = '^(' + (($names | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
        $parallel = if ($Lane -eq 'gpu') { 1 } else { 4 }
        $result.Log = @(& $CTest --test-dir $Build -C $Config -j $parallel --timeout 900 `
            --output-on-failure --no-tests=error -R $pattern --output-junit $junit 2>&1 | ForEach-Object { "$_" })
        $result.ExitCode = $LASTEXITCODE
        $result.JUnit = $junit
        if (-not (Test-Path -LiteralPath $junit -PathType Leaf)) { throw 'CTest produced no result document' }
        [xml]$document = Get-Content -LiteralPath $junit -Raw -ErrorAction Stop
        $cases = @($document.SelectNodes('//testcase'))
        $seen = @($cases | ForEach-Object { $_.GetAttribute('name') })
        if ($cases.Count -ne $names.Count -or @($names | Where-Object { $_ -notin $seen }).Count -ne 0 `
            -or @($seen | Select-Object -Unique).Count -ne $seen.Count) {
            throw 'CTest result does not cover the complete selected inventory exactly once'
        }
        $result.Skipped = @($cases | Where-Object {
            $output = $_.SelectSingleNode('system-out')
            $reportedSkip = $Lane -eq 'gpu' -and $null -ne $output -and
                $output.InnerText -match '(?im)^\s*(skip:|no .+ - skipped\s*$)'
            $null -ne $_.SelectSingleNode('skipped') -or $reportedSkip
        } |
            ForEach-Object { $_.GetAttribute('name') })
        $result.Executed = $cases.Count - $result.Skipped.Count
        $failed = @($cases | Where-Object { $null -ne $_.SelectSingleNode('failure') -or $null -ne $_.SelectSingleNode('error') })
        if ($result.ExitCode -ne 0 -or $failed.Count -gt 0) {
            $result.Detail = "CTest failed (exit $($result.ExitCode), $($failed.Count) failed results)"
        } elseif ($result.Executed -eq 0 -or ($Lane -eq 'gpu' -and $result.Skipped.Count -gt 0)) {
            $result.Classification = 'UNAVAILABLE'
            $result.Detail = "executed $($result.Executed)/$($result.Planned); skipped: $($result.Skipped -join ', ')"
        } else {
            $result.Classification = 'passed'
            $result.Detail = "executed $($result.Executed)/$($result.Planned)"
        }
    } catch {
        $result.Detail = $_.Exception.Message
    }
    return [pscustomobject]$result
}
