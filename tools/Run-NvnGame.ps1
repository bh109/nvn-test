param([switch]$Live,[switch]$Online,[int]$ExitFrame=120,[int]$TimeoutSeconds=240,[int]$ShotFrame=20,[ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$LogTag='play',[string]$BuildDirectory='build\nvn-opensmash-port',[string]$CacheDirectory='')
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=if([IO.Path]::IsPathRooted($BuildDirectory)){$BuildDirectory}else{Join-Path $root $BuildDirectory};$exe=Join-Path $out 'ssbu-nvn-vulkan.exe'
$cache=if($CacheDirectory){[IO.Path]::GetFullPath((Join-Path $root $CacheDirectory))}else{Join-Path $out 'cache'}
if(!(Test-Path -LiteralPath $exe)){throw 'Run Install WindowSmash NVN.bat first to build the game.'}
if(Get-Process ssbu-nvn-vulkan -ErrorAction SilentlyContinue){throw 'Close the existing NVN Vulkan game before starting another run; it shares the candidate cache.'}
$values=@{
    SSBU_NVN_FRONTEND='opensmash';SSBU_RENDERER='vk';SSBU_VK_DEVICE='0';SSBU_VK_SCALE='0.5';
    SSBU_GRAPHICS_CONFIG="$out\graphics.ini";SSBU_VK_CACHE=$cache;SSBU_NVN_REPORT_DIR="$out\reports";
    SSBU_MENU='0';SSBU_L1_PIPE='0';SSBU_L1_EARLY='0';SSBU_RUNAHEAD='0';SSBU_PACE_COUNTWAIT='0';
    SSBU_VK_PREP='0';SSBU_VK_LOAD_AT_START='0';SSBU_VK_NOHOLD='0';SSBU_VK_SELFTEST='0';
    SSBU_WATCHDOG='0';SSBU_NVN_EXIT_FRAME=$(if($Live){'0'}else{[string]$ExitFrame});
    SSBU_SHOT=$(if($Live){'0'}else{[string]$ShotFrame});SSBU_SHOT_DIR="$out\screenshots"
}
if(!(Test-Path -LiteralPath "$out\graphics.ini")){
    @'
renderer = vulkan
scale = 0.5
present = mailbox
shader_wait = hold
shader_prepare = off
shader_load = loading
menu = off
runahead = off
latency = normal
'@|Set-Content "$out\graphics.ini" -Encoding ASCII
}
$saved=@{};foreach($name in $values.Keys){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
Push-Location $root
try{
    foreach($name in $values.Keys){[Environment]::SetEnvironmentVariable($name,$values[$name],'Process')}
    $arguments=@(('"'+(Join-Path $root 'dump\modules')+'"'),'--status','10');if($Online){$arguments+='--online'}
    $stdout=Join-Path $out "$LogTag.stdout.log";$stderr=Join-Path $out "$LogTag.stderr.log"
    $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $root -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $processHandle=$process.Handle # keep a handle so Windows PowerShell retains ExitCode
    Write-Host "WindowSmash NVN Vulkan started, PID $($process.Id). Logs: $out"
    if($Live){return}
    $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while(!$process.WaitForExit(1000)){if([DateTime]::UtcNow -ge $deadline){Stop-Process -Id $process.Id;throw "NVN game exceeded bounded timeout; inspect $stdout and $stderr"}}
    $process.WaitForExit();Get-Content $stderr -Tail 12
    if($process.ExitCode -ne 0){throw "WindowSmash NVN Vulkan exited with code $($process.ExitCode)."}
    if(!(Select-String -LiteralPath $stderr -SimpleMatch '[NVN port] bounded run completed:' -Quiet)){throw 'NVN game stopped before its requested frame count.'}
    Write-Host "PASS: NVN Vulkan renderer completed $ExitFrame game frames."
}finally{Pop-Location;foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
