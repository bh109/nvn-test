param([ValidateRange(1,65535)][int]$Port=39421,[string]$Bind='0.0.0.0',[switch]$DryRun)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$PSScriptRoot\nvn_common.ps1"
$python=Find-NvnPython ''
Write-Host "WindowSmash Online lobby: TCP and UDP $Bind`:$Port"
Write-Host "On this PC, set the game's lobby address to 127.0.0.1:$Port and restart the game."
Write-Host 'Other PCs on your home network use this PC''s LAN address, for example:'
foreach($address in [Net.Dns]::GetHostAddresses([Net.Dns]::GetHostName())){
    if($address.AddressFamily -eq [Net.Sockets.AddressFamily]::InterNetwork -and ![Net.IPAddress]::IsLoopback($address) -and !$address.ToString().StartsWith('169.254.')){
        Write-Host "  $address`:$Port"
    }
}
Write-Host 'Choose your Wi-Fi/Ethernet address; virtual adapter addresses may also appear.'
Write-Host 'Allow inbound TCP and UDP on this port in the host firewall.'
Write-Host 'Internet players need a reachable public address/port or a nearby hosted server.'
Write-Host 'This is a separate lobby/player pool. Keep this window open; Ctrl+C stops it.'
$arguments=@("$PSScriptRoot\ssbu_online_lobby.py",'--bind',$Bind,'--port',"$Port",'--state',"$root\tools\nex\state",'--verbose')
if($DryRun){Write-Host "Dry run: $python $($arguments -join ' ')";return}
Push-Location $root
try{& $python @arguments;if($LASTEXITCODE -ne 0){throw "Lobby server stopped with code $LASTEXITCODE"}}finally{Pop-Location}
