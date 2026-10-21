param([Parameter(Mandatory=$true)][ValidateSet('red','green')][string]$Phase)
$ErrorActionPreference='Stop'
$body=Join-Path $PSScriptRoot ('run-'+$Phase+'-cpu.ps1')
if(!(Test-Path -LiteralPath $body)){throw 'phase not yet authorized/frozen'}
& $body
