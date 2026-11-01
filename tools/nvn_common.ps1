# Shared build-tool discovery. The installer passes its selected Python explicitly.
function Find-NvnPython([string]$Requested) {
    if($Requested){return (Get-Command $Requested -ErrorAction Stop).Source}
    $portable=Join-Path (Split-Path $PSScriptRoot -Parent) 'deps\python\python.exe'
    if(Test-Path -LiteralPath $portable){return $portable}
    $command=Get-Command python.exe -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    throw 'Python not found. Run Install WindowSmash NVN.bat, or pass -Python with your Python executable.'
}
