# Shared build-tool discovery. The installer passes its selected Python explicitly.
function Find-NvnPython([string]$Requested) {
    if($Requested){return (Get-Command $Requested -ErrorAction Stop).Source}
    $portable=Join-Path (Split-Path $PSScriptRoot -Parent) 'deps\python\python.exe'
    if(Test-Path -LiteralPath $portable){return $portable}
    # Setup can select an OS Python through py -3 without putting python.exe
    # on PATH. Avoid accepting the Windows Store python.exe alias blindly.
    $launcher=Get-Command py.exe -ErrorAction SilentlyContinue
    if($launcher){
        $candidate=& $launcher.Source -3 -c 'import sys; print(sys.executable)' 2>$null
        if($LASTEXITCODE -eq 0 -and $candidate -and (Test-Path -LiteralPath ([string]$candidate))){return [string]$candidate}
    }
    $command=Get-Command python.exe -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    throw 'Python not found. Run Install WindowSmash NVN.bat, or pass -Python with your Python executable.'
}
