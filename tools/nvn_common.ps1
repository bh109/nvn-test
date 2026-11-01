# Shared build-tool discovery. The installer passes its selected Python explicitly.
function Test-OwnedNvnShortcutTarget([string]$Target,[string]$ProjectRoot) {
    if(!$Target -or ![IO.Path]::IsPathRooted($Target)){return $false}
    $targetPath=[IO.Path]::GetFullPath($Target)
    foreach($name in @('Play WindowSmash NVN.bat','Play Intel NVN Vulkan.bat')){
        if($targetPath -ieq [IO.Path]::GetFullPath((Join-Path $ProjectRoot $name))){return $true}
    }
    return $false
}

function Update-NvnShortcut([string]$Shortcut,[string]$ProjectRoot,[switch]$DryRun) {
    if(!(Test-Path -LiteralPath $Shortcut)){return $false}
    if((Get-Item -LiteralPath $Shortcut -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){return $false}
    $shell=New-Object -ComObject WScript.Shell
    $entry=$shell.CreateShortcut($Shortcut)
    if(!(Test-OwnedNvnShortcutTarget $entry.TargetPath $ProjectRoot)){return $false}
    $target=Join-Path $ProjectRoot 'Play WindowSmash NVN.bat'
    if($entry.TargetPath -ieq $target -and $entry.WorkingDirectory -ieq $ProjectRoot -and $entry.Description -eq 'WindowSmash NVN Vulkan'){return $false}
    if(!$DryRun){
        $entry.TargetPath=$target
        $entry.WorkingDirectory=$ProjectRoot
        $entry.Description='WindowSmash NVN Vulkan'
        $entry.Save()
    }
    return $true
}

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
