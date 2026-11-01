$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\nvn-bridge-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$compiler=Join-Path $root 'deps\llvm\bin\clang-cl.exe'
$sysroot=Join-Path $root 'deps\msvc'
& $compiler /nologo /std:c++17 /EHsc /O2 /W4 /winsysroot $sysroot /I "$root\runtime" "$root\runtime\nvn\bridge.cpp" "$PSScriptRoot\test_nvn_bridge.cpp" "/Fo$out\\" "/Fe$out\test-nvn-bridge.exe" /link "/winsysroot:$sysroot"
if($LASTEXITCODE -ne 0){throw 'NVN bridge test build failed'}
& "$out\test-nvn-bridge.exe" off
if($LASTEXITCODE -ne 0){throw 'Disabled bridge test failed'}
& "$out\test-nvn-bridge.exe" audit
if($LASTEXITCODE -ne 0){throw 'Transparent bridge test failed'}
