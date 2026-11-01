param([Parameter(Mandatory=$true)][string]$ReferenceRoot, [string]$Sequence='', [switch]$NoPreparation, [string]$DebugMatch='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $ReferenceRoot 'build';$work=Join-Path $build 'work'
$state=Get-Content -LiteralPath (Join-Path $build 'state.json') -Raw | ConvertFrom-Json
$exe=Join-Path $work 'ssbu_native.exe'
$main=Join-Path $build ('binaries\'+$state.game_version+'\main.elf')
$rdcmd=Join-Path $root 'deps\renderdoc-tools\portable\RenderDoc_1.46_64\renderdoccmd.exe'
foreach($path in @($exe,$main,$rdcmd,$state.dump.romfs)){if(!(Test-Path -LiteralPath $path)){throw "Missing reference capture dependency: $path"}}
$destination=Join-Path $root ('build\opensmash-captures\'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $destination -Force | Out-Null
# Match the native capture's internal scale and output window without changing
# the owner's graphics.ini. Console mode remains docked in this copied config.
$config=[IO.File]::ReadAllText((Join-Path $build 'config\graphics.ini'))
$config=[regex]::Replace($config,'(?m)^resolution_scale\s*=.*$','resolution_scale = 0.5')
$config=[regex]::Replace($config,'(?m)^width\s*=.*$','width = 1600')
$config=[regex]::Replace($config,'(?m)^height\s*=.*$','height = 900')
$configPath=Join-Path $destination 'graphics.ini';[IO.File]::WriteAllText($configPath,$config)
$values=@{SSBU_ROMFS=$state.dump.romfs;SSBU_USERDATA=(Join-Path $build 'saves');SSBU_FONTS=(Join-Path $build 'sysdata\fonts');SSBU_TZDIR=(Join-Path $build 'sysdata\zoneinfo');SSBU_GRAPHICS_CONFIG=$configPath;VK_LAYER_PATH=(Split-Path $rdcmd -Parent);VK_INSTANCE_LAYERS='VK_LAYER_RENDERDOC_Capture';ENABLE_VULKAN_RENDERDOC_CAPTURE='1'}
$saved=@{}
if($Sequence){$values.SSBU_SEQ=$Sequence;$values.SSBU_SEQ_KEYBOARD='1';$values.SSBU_SKIP_MOVIES='1';$values.SSBU_SEQ_LOG='300'}
if($NoPreparation){$values.SSBU_VK_PREPARE='off';$values.SSBU_VK_PREP='0'}
if($DebugMatch){$values.SSBU_DEBUG_MATCH=$DebugMatch}
foreach($entry in Get-ChildItem Env:){if($entry.Name -like 'SSBU_*' -or $entry.Name -like 'RECOMP_*' -or $values.ContainsKey($entry.Name)){$saved[$entry.Name]=$entry.Value}}
Push-Location $work
try{
    foreach($entry in @(Get-ChildItem Env:)){if($entry.Name -like 'SSBU_*' -or $entry.Name -like 'RECOMP_*'){[Environment]::SetEnvironmentVariable($entry.Name,$null,'Process')}}
    foreach($name in $values.Keys){[Environment]::SetEnvironmentVariable($name,$values[$name],'Process')}
    Write-Host "OpenSmash capture at matched scale0.5/window1600x900: $destination"
    $log=Join-Path $destination 'capture.log'
    $stream=[IO.File]::Open($log,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::ReadWrite)
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false));$writer.AutoFlush=$true
    try{
        $ErrorActionPreference='Continue'
        & $rdcmd capture --wait-for-exit --working-dir $work --capture-file (Join-Path $destination 'opensmash') --opt-api-validation $exe $main '0' 2>&1 | ForEach-Object {$writer.WriteLine($_.ToString())}
        $code=$LASTEXITCODE
    }finally{$writer.Dispose();$ErrorActionPreference='Stop'}
    @{executable=$exe;exitCode=$code;captures=@(Get-ChildItem -LiteralPath $destination -Filter '*.rdc' -File | ForEach-Object FullName);log=$log;graphicsConfig=$configPath;environment=$values;performanceBenchmark=$false} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'result.json')
}finally{
    foreach($entry in @(Get-ChildItem Env:)){if($entry.Name -like 'SSBU_*' -or $entry.Name -like 'RECOMP_*' -or $values.ContainsKey($entry.Name)){[Environment]::SetEnvironmentVariable($entry.Name,$null,'Process')}}
    foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}
    Pop-Location
}
