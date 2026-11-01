$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$saved=@{};foreach($name in @('SSBU_NVN_FRONTEND','SSBU_NVN_SDK_CHECK')){$saved[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try{
    $env:SSBU_NVN_FRONTEND='audit';$env:SSBU_NVN_SDK_CHECK='1'
    & "$PSScriptRoot\Test-NativeVulkanGame.ps1" -OutputDirectory build\nvn-sdk-adapter-test -SkipPreparation -InstanceBatch -BatchCompute -DenseResources -FlatSnapshots -FenceSleep -SerialPresent -Bounded -ExitFrame 300 -TimeoutSeconds 120
    $log=Join-Path $root 'build\nvn-sdk-adapter-test\boot.stderr.log'
    if(!(Select-String -LiteralPath $log -SimpleMatch '[OpenSmash SDK] PASS: 9 actual recompiled SDK storage checks' -Quiet)){throw 'Real game did not pass the actual SDK callback checks.'}
    Write-Host 'PASS: upstream SDK model called WindowSmash recompiled SDK through the host adapter, followed by 300 rendered game frames.'
}finally{foreach($name in $saved.Keys){[Environment]::SetEnvironmentVariable($name,$saved[$name],'Process')}}
