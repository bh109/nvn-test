$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\nvn-display-tests';New-Item -ItemType Directory -Path $out -Force|Out-Null
$cc=Join-Path $root 'deps\llvm\bin\clang-cl.exe';$sysroot=Join-Path $root 'deps\msvc'
# Fixture C fatal replacement throws, so omit /EHc's extern-C no-throw assumption.
& $cc /nologo /std:c++17 /EHs /O2 /W4 /winsysroot $sysroot /I "$root\runtime" "$root\runtime\nvn\present_clock.cpp" "$root\runtime\nvn\display_adapter.cpp" "$PSScriptRoot\test_opensmash_display.cpp" "/Fo$out\\" "/Fe$out\test-opensmash-display.exe" /link "/winsysroot:$sysroot" user32.lib
if($LASTEXITCODE -ne 0){throw 'Display adapter test build failed'}
& "$out\test-opensmash-display.exe"
if($LASTEXITCODE -ne 0){throw 'Display adapter test failed'}
