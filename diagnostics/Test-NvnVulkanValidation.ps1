param([switch]$Live,[switch]$OverlayCheck)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\nvn-opensmash-port\validation';New-Item -ItemType Directory -Path $out -Force|Out-Null
$layer=Join-Path $root 'deps\vulkan-validation\20261031'
if(!(Test-Path -LiteralPath "$layer\VkLayer_khronos_validation.json")){throw 'Portable Khronos layer missing'}
@'
khronos_validation.validate_sync = true
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.log_filename = stdout
khronos_validation.report_flags = error,warn,info
khronos_validation.enable_message_limit = true
khronos_validation.duplicate_message_limit = 5
'@|Set-Content "$out\vk_layer_settings.txt" -Encoding ASCII
$values=@{VK_LAYER_PATH=$layer;VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation';VK_LAYER_SETTINGS_PATH=$out;SSBU_VK_VALIDATE='1'}
if($OverlayCheck){
    if($Live){throw 'OverlayCheck is a bounded regression test; do not combine with Live.'}
    $values['SSBU_VK_OVERLAY_SELFTEST']='1'
}
$saved=@{};foreach($name in $values.Keys){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    foreach($name in $values.Keys){[Environment]::SetEnvironmentVariable($name,$values[$name],'Process')}
    if($Live){
        & "$root\tools\Run-NvnGame.ps1" -Live -LogTag 'validation-battle'
        Write-Host 'Battle validation game launched OFFLINE. Set the fixed Training scene. Validation adds overhead; this run is for correctness, not FPS. Inspect validation-battle stdout/stderr after closing; launch is not a PASS.'
        return
    }
    & "$root\tools\Run-NvnGame.ps1" -ExitFrame 300 -ShotFrame 280 -TimeoutSeconds 240
    $game=Join-Path $root 'build\nvn-opensmash-port'
    $text=[IO.File]::ReadAllText("$game\play.stdout.log")+[IO.File]::ReadAllText("$game\play.stderr.log")
    Copy-Item -LiteralPath "$game\play.stdout.log","$game\play.stderr.log" -Destination $out -Force
    if(!$text.Contains('CURRENT-VALIDATION-ENABLED') -or !$text.Contains('Synchronization')){throw 'Khronos core/synchronization activation not verified'}
    if($text.Contains('Validation Error:')){throw 'NVN Vulkan renderer produced validation errors; inspect validation logs'}
    if($OverlayCheck -and !$text.Contains('nvn_overlay_selftest: 60 overlay frames exercised')){throw 'Overlay regression path was not exercised'}
    Write-Host 'PASS: NVN Vulkan renderer 300-frame boot with verified Khronos core/synchronization validation and no validation errors.'
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
