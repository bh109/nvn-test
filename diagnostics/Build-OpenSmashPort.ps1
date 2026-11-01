# Developer runtime rebuild. End users should run Install WindowSmash NVN.bat.
# Uses common game objects from source, never a previous experimental renderer.
param([string]$Python='', [string]$OutputDirectory='build\nvn-opensmash-port', [string]$CompilerHelper='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root\tools\nvn_common.ps1"
$python=Find-NvnPython $Python
$arguments=@("$root\tools\build_nvn.py",'--runtime-only','--output-directory',$OutputDirectory)
if($CompilerHelper){$arguments+=@('--compiler-helper',$CompilerHelper)}
& $python @arguments
if($LASTEXITCODE -ne 0){throw 'NVN runtime build failed; inspect the logs above.'}
