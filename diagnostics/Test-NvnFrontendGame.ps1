param([switch]$Bounded,[int]$ExitFrame=300)
$ErrorActionPreference='Stop'
$saved=[Environment]::GetEnvironmentVariable('SSBU_NVN_FRONTEND','Process')
try{
    $env:SSBU_NVN_FRONTEND='audit'
    & "$PSScriptRoot\Test-NativeVulkanGame.ps1" -OutputDirectory build\nvn-frontend-audit -SkipPreparation -InstanceBatch -BatchCompute -DenseResources -FlatSnapshots -FenceSleep -SerialPresent -Bounded:$Bounded -ExitFrame $ExitFrame -TimeoutSeconds 120
    # This is an interception check, not proof the replacement renderer works.
    if($Bounded){
        $log=Join-Path (Split-Path $PSScriptRoot -Parent) 'build\nvn-frontend-audit\boot.stderr.log'
        if(!(Select-String -LiteralPath $log -SimpleMatch '[NVN bridge] call nvnDeviceGetProcAddress' -Quiet)){throw 'The game did not reach the NVN device resolver bridge.'}
        if(!(Select-String -LiteralPath $log -SimpleMatch '[NVN bridge] call nvnQueuePresentTexture' -Quiet)){throw 'The game did not present through the NVN bridge.'}
        Write-Host 'PASS: real game fetched and called NVN functions through the transparent bridge, including presentation.'
    }
}finally{[Environment]::SetEnvironmentVariable('SSBU_NVN_FRONTEND',$saved,'Process')}
