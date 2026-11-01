param([Parameter(Mandatory=$true)][string]$ReferenceRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\nvn-bridge-tests'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$cc=Join-Path $root 'deps\llvm\bin\clang-cl.exe';$sysroot=Join-Path $root 'deps\msvc'
& $cc /nologo /TC /O2 /W4 /winsysroot $sysroot /I "$ReferenceRoot\runtime" /c "$PSScriptRoot\test_opensmash_context_reference.c" "/Fo$out\reference-context.obj"
if($LASTEXITCODE -ne 0){throw 'Actual reference ABI fixture did not compile'}
& $cc /nologo /std:c++17 /EHsc /O2 /W4 /winsysroot $sysroot /I "$root\runtime" "$PSScriptRoot\test_opensmash_context.cpp" "$out\reference-context.obj" "/Fo$out\\" "/Fe$out\test-opensmash-context.exe" /link "/winsysroot:$sysroot"
if($LASTEXITCODE -ne 0){throw 'Context adapter fixture did not link'}
& "$out\test-opensmash-context.exe"
if($LASTEXITCODE -ne 0){throw 'Context adapter fixture failed'}
