param([Parameter(Mandatory=$true)][string]$ReferenceRoot,[switch]$LinkOnly,[switch]$HostAdapter,[switch]$DisplayAdapter)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($DisplayAdapter){$HostAdapter=$true}
$out=Join-Path $root $(if($DisplayAdapter){'build\opensmash-graphics-display-probe'}else{'build\opensmash-graphics-probe'})
New-Item -ItemType Directory -Path $out -Force | Out-Null
$reference=(Resolve-Path -LiteralPath $ReferenceRoot).Path
$runtime=Join-Path $reference 'runtime'
$compiler=Join-Path $root 'deps\llvm\bin\clang-cl.exe'
$sysroot=Join-Path $root 'deps\msvc'
$units=@('nvn_stub','nvn_gl','nvn_vk','nvn_c4','nvn_c5','nvn_sdk','mx_translate','tex_decode')
$displayDefines=@()
if($DisplayAdapter){
    foreach($name in @('fit','set_interval','present_plan','wait_refresh','before_present','pace','present_done')){$displayDefines+="/Ddisplay_$name=opensmash_display_$name"}
    $displayDefines|Set-Content "$out\display-symbol-defines.txt" -Encoding ASCII
}
$manifest=@()
if(!$LinkOnly){foreach($unit in $units){
    $source=Join-Path $runtime "$unit.c"
    $manifest+=@{source=$source;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash}
    $log=Join-Path $out "$unit-build.log"
    $oldPreference=$ErrorActionPreference
    try{
        $ErrorActionPreference='Continue'
        # The reference uses VkFlags instead of the enum type for the debug
        # callback's first argument. ABI-compatible on Win64; retain it as a
        # warning in this compile-only probe, without editing reference files.
        & $compiler /nologo /TC /O2 /W0 /clang:-Wno-error=incompatible-function-pointer-types /winsysroot $sysroot /I $runtime /I "$root\deps\vulkan-native\Vulkan-Headers-1.3.290\include" @displayDefines /c "/Fo$out\$unit.obj" $source *> $log
        $code=$LASTEXITCODE
    }finally{$ErrorActionPreference=$oldPreference}
    if($code -ne 0){Get-Content $log -Tail 20;throw "Reference graphics compilation failed: $unit"}
    Write-Host "Compiled reference graphics unit: $unit"
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$out\source-manifest.json" -Encoding UTF8
}else{
    foreach($item in (Get-Content -LiteralPath "$out\source-manifest.json" -Raw|ConvertFrom-Json)){
        if((Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash -ne $item.sha256){throw 'Reference changed; rerun without LinkOnly.'}
    }
}
# Expected to fail until the host adapter is complete. Never use /FORCE to
# create a DLL containing unresolved calls, or ship the output of this probe.
$objects=@($units|ForEach-Object {Join-Path $out "$_.obj"})
if($HostAdapter){
    & $compiler /nologo /std:c++17 /EHsc /O2 /W0 /winsysroot $sysroot /I "$root\runtime" /c "$root\runtime\nvn\host_adapter.cpp" "/Fo$out\host_adapter.obj"
    if($LASTEXITCODE -ne 0){throw 'Host adapter compilation failed'}
    $objects+=Join-Path $out 'host_adapter.obj'
}
if($DisplayAdapter){
    foreach($unit in @('present_clock','display_adapter')){
        & $compiler /nologo /std:c++17 /EHsc /O2 /W0 /winsysroot $sysroot /I "$root\runtime" /c "$root\runtime\nvn\$unit.cpp" "/Fo$out\$unit.obj"
        if($LASTEXITCODE -ne 0){throw "Display adapter $unit compilation failed"}
        $objects+=Join-Path $out "$unit.obj"
    }
}
$variant=if($HostAdapter){'-with-host'}else{''}
$oldPreference=$ErrorActionPreference
try{
    $ErrorActionPreference='Continue'
    & $compiler /nologo /LD /winsysroot $sysroot @objects "/Fe$out\dependency-probe$variant.dll" /link "/winsysroot:$sysroot" /NOENTRY /EXPORT:register_nvn_imports /EXPORT:nvn_gl_pick /EXPORT:nvn_vk_init /ERRORLIMIT:0 kernel32.lib user32.lib gdi32.lib opengl32.lib winmm.lib shell32.lib ole32.lib psapi.lib libucrt.lib libvcruntime.lib libcpmt.lib *> "$out\link-dependencies$variant.log"
    $code=$LASTEXITCODE
}finally{$ErrorActionPreference=$oldPreference}
$text=[IO.File]::ReadAllText("$out\link-dependencies$variant.log")
$missing=@([regex]::Matches($text,'undefined symbol: ([^\r\n]+)')|ForEach-Object {$_.Groups[1].Value}|Sort-Object -Unique)
@{purpose='Compile/link dependency probe only; not a playable backend';reference=$reference;hostAdapter=$HostAdapter.IsPresent;displayAdapter=$DisplayAdapter.IsPresent;linkExitCode=$code;unresolvedCount=$missing.Count;unresolved=$missing} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$out\dependencies$variant.json" -Encoding UTF8
if($code -ne 0 -and !$missing.Count){throw 'Probe failed without an undefined-symbol report; inspect link-dependencies.log'}
Write-Host "Reference graphics compiled. Host adapter dependencies remaining: $($missing.Count). See $out\dependencies$variant.json"
