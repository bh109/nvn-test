$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$saved=@{};foreach($name in @('SSBU_NVN_FRONTEND','SSBU_NVN_SDK_CHECK','SSBU_NVN_CLOCK_CHECK','SSBU_NVN_WORKER_CHECK')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    $env:SSBU_NVN_FRONTEND='audit';$env:SSBU_NVN_SDK_CHECK='1';$env:SSBU_NVN_CLOCK_CHECK='1';$env:SSBU_NVN_WORKER_CHECK='1'
    & "$PSScriptRoot\Test-NativeVulkanGame.ps1" -OutputDirectory build\nvn-worker-adapter-test -SkipPreparation -InstanceBatch -BatchCompute -DenseResources -FlatSnapshots -FenceSleep -SerialPresent -Bounded -ExitFrame 300 -TimeoutSeconds 120
    $log=Join-Path $root 'build\nvn-worker-adapter-test\boot.stderr.log'
    foreach($marker in @('[OpenSmash SDK] PASS: 9 actual recompiled SDK storage checks','[OpenSmash display] PASS: existing HWND','[OpenSmash workers] PASS: 4 private contexts/stacks, 64 streaming raw/RLE/compressed frames')){
        if(!(Select-String -LiteralPath $log -SimpleMatch $marker -Quiet)){throw "Missing integration check: $marker"}
    }
    Write-Host 'PASS: actual-game shader worker decoding, SDK and window/clock adapter checks, followed by 300 rendered frames.'
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
