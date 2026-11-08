param([Parameter(Mandatory=$true)][int]$GameProcessId,
      [Parameter(Mandatory=$true)][string]$BuildDirectory,
      [string]$CacheDirectory='', [switch]$Online)
$ErrorActionPreference='Stop'
# Wait for renderer shutdown/cache saving before launching a replacement process.
$game=Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
if($game -and !$game.WaitForExit(120000)){throw 'The old game has not closed. Reopen it manually after it exits.'}
if($Online){foreach($name in @('SSBU_NET_RELAY','SSBU_NET_ROOM','SSBU_NET_NAME','SSBU_NET_DIRECT')){[Environment]::SetEnvironmentVariable($name,$null,'Process')}}
$arguments=@{Live=$true;BuildDirectory=$BuildDirectory;LogTag='play'}
if($CacheDirectory){$arguments.CacheDirectory=$CacheDirectory}
if($Online){$arguments.Online=$true}
& (Join-Path $PSScriptRoot 'Run-NvnGame.ps1') @arguments
