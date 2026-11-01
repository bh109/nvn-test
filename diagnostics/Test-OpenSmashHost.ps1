param([Parameter(Mandatory=$true)][string]$ReferenceRoot)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\nvn-host-tests';New-Item -ItemType Directory -Path $out -Force|Out-Null
$cc=Join-Path $root 'deps\llvm\bin\clang-cl.exe';$sysroot=Join-Path $root 'deps\msvc'
& $cc /nologo /TC /O2 /W0 /winsysroot $sysroot /I "$ReferenceRoot\runtime" /c "$ReferenceRoot\runtime\nvn_stub.c" "$PSScriptRoot\test_opensmash_host_fixture.c" "/Fo$out\\"
if($LASTEXITCODE -ne 0){throw 'Upstream NVN fixture build failed'}
# The test replaces fatal exits with C++ exceptions, including C-linkage calls.
# /EHs deliberately omits /EHc's assumption that extern-C functions never throw.
& $cc /nologo /std:c++17 /EHs /O2 /W4 /winsysroot $sysroot /I "$root\runtime" "$root\runtime\nvn\host_adapter.cpp" "$PSScriptRoot\test_opensmash_host.cpp" "$out\nvn_stub.obj" "$out\test_opensmash_host_fixture.obj" "/Fo$out\\" "/Fe$out\test-opensmash-host.exe" /link "/winsysroot:$sysroot"
if($LASTEXITCODE -ne 0){throw 'Host adapter test build failed'}
& "$out\test-opensmash-host.exe"
if($LASTEXITCODE -ne 0){throw 'Host adapter test failed'}
