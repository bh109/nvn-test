param([Parameter(Mandatory=$true)][string]$Installer)
$ErrorActionPreference='Stop'
$signature=Get-AuthenticodeSignature -LiteralPath $Installer
if($signature.Status -ne 'Valid' -or !$signature.SignerCertificate -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation'){
    throw 'The Visual C++ runtime installer does not have a valid Microsoft signature.'
}
Write-Host 'Microsoft Visual C++ x64 runtime is needed. Complete its setup window, then this build will continue.'
# This is an interactive installer so the user can review Microsoft's terms.
$installProcess=Start-Process -FilePath $Installer -ArgumentList @('/install','/norestart') -PassThru
$processHandle=$installProcess.Handle
$installProcess.WaitForExit()
if($installProcess.ExitCode -in @(3010,1641)){throw 'Microsoft runtime setup requires a restart. Restart Windows, then run Install WindowSmash NVN.bat again.'}
if($installProcess.ExitCode -ne 0){throw "Microsoft runtime setup stopped with code $($installProcess.ExitCode). Rerun the NVN installer when ready."}
