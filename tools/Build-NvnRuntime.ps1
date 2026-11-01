param(
    [Parameter(Mandatory=$true)][string]$Python,
    [Parameter(Mandatory=$true)][string]$Compiler,
    [Parameter(Mandatory=$true)][string]$CompilerHelper,
    [string]$WindowsSysroot='',
    [string]$ResponseFile='build\ssbu.exe.rsp',
    [string]$OutputDirectory='build\nvn-opensmash-port'
)
$ErrorActionPreference='Stop'
$Compiler=(Get-Command $Compiler -ErrorAction Stop).Source
$linker=@()
if(Test-Path -LiteralPath (Join-Path (Split-Path $Compiler -Parent) 'lld-link.exe')){$linker=@('-fuse-ld=lld')}
$root=Split-Path $PSScriptRoot -Parent
$out=if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $root $OutputDirectory}
$response=if([IO.Path]::IsPathRooted($ResponseFile)){$ResponseFile}else{Join-Path $root $ResponseFile}
if(!(Test-Path -LiteralPath $response)){throw 'Common game object list missing. Run Install WindowSmash NVN.bat.'}
if(Get-Process ssbu-nvn-vulkan -ErrorAction SilentlyContinue){throw 'Close the NVN game before rebuilding it.'}
New-Item -ItemType Directory -Path $out -Force | Out-Null
$systemCompile=@();$systemLink=@()
if($WindowsSysroot){$systemCompile=@('/winsysroot',$WindowsSysroot);$systemLink=@("/winsysroot:$WindowsSysroot")}
$headers=Join-Path $root 'third_party\nvn-build-headers'
$source=Join-Path $root 'build\opensmash-nvn-source\runtime'
$includes=@('/I',"$root\runtime",'/I',$out,'/I',$source,'/I',"$headers\vulkan-1.3.290",'/I',"$headers\dxc")
function Compile-Unit([string]$Name,[string]$File,[string[]]$Flags){
    $log=Join-Path $out "$Name-build.log"
    & $Compiler /nologo /O2 /W0 @systemCompile @Flags @includes /c "/Fo$out\$Name.obj" $File *> $log
    if($LASTEXITCODE -ne 0){Get-Content -LiteralPath $log -Tail 20;throw "NVN compile failed: $Name"}
    Write-Host "Compiled $Name"
    return ('"'+"$out\$Name.obj"+'"')
}
Push-Location $root
try{
    & "$root\tools\Stage-NvnGraphics.ps1" -Python $Python
    if(!$?){throw 'NVN graphics staging failed'}
    & $Python "$root\tools\nvn_contract.py" --output-dir $out
    if($LASTEXITCODE -ne 0){throw 'Unsupported game images for NVN adapter'}
    $defines=@()
    foreach($name in @('fit','set_interval','present_plan','wait_refresh','before_present','pace','present_done','clock_sample','l1_early','l1_early_skip','l1_ms','l1_pipe_default','l1_submit','l1_trace','present_defer','present_due','present_flushed','title_note')){$defines+="/Ddisplay_$name=opensmash_display_$name"}
    foreach($name in @('enabled','frame','font_rgba','shot_wanted','shot_done')){$defines+="/Dmenu_$name=opensmash_menu_$name"}
    foreach($name in @('active','size','take')){$defines+="/Dosk_$name=opensmash_osk_$name"}
    $defines+='/Dfn_table=(*opensmash_fn_table)';$defines+='/Dfn_table_count=opensmash_fn_table_count'
    $defines | Set-Content "$out\upstream-symbol-defines.txt" -Encoding ASCII
    $new=@()
    foreach($name in @('nvn_stub','nvn_gl','nvn_vk','nvn_c4','nvn_c5','nvn_sdk','mx_translate','tex_decode','gfx_options')){
        $new+=Compile-Unit "upstream_$name" "$source\$name.c" (@('/TC')+$defines)
    }
    foreach($name in @('bridge','host_adapter','display_adapter','present_clock','optional_adapter','port_runtime','online_menu')){
        $new+=Compile-Unit "port_$name" "$root\runtime\nvn\$name.cpp" @('/std:c++17','/EHsc','/DSSBU_OPENSMASH_PORT')
    }
    # gpu_vulkan retains guarded legacy entry points. Compile their support from
    # source, rather than borrowing objects from a prior experimental build.
    foreach($name in @('native_device','native_shader','native_pipeline','native_present','native_maxwell','native_watch','native_buffers','native_recipes','native_fence')){
        $new+=Compile-Unit $name "$root\runtime\vulkan\$name.cpp" @('/std:c++17','/EHsc')
    }
    $new+=Compile-Unit 'port_gpu_vulkan' "$root\runtime\gpu_vulkan.cpp" @('/std:c++17','/EHsc','/DSSBU_OPENSMASH_PORT')
    $new+=Compile-Unit 'port_gpu' "$root\runtime\gpu.c" @('/TC','/arch:AVX2','/DSSBU_NATIVE_COMPUTE_BATCH')
    $new+=Compile-Unit 'port_vi' "$root\runtime\ipc_vi.c" @('/TC','/DSSBU_NVN_CLOCK_BRIDGE')
    $new+=Compile-Unit 'port_kernel' "$root\runtime\kernel.c" @('/TC')
    $entry='void f_7109454d90(Cpu *c) {'
    $bootstrapFiles=@(Get-ChildItem -LiteralPath "$root\build\sdk" -Filter 'g*.c' -File | Where-Object {Select-String -LiteralPath $_.FullName -SimpleMatch $entry -Quiet})
    if($bootstrapFiles.Count -ne 1){throw 'Expected exactly one generated SDK bootstrap definition'}
    $sdk=[IO.File]::ReadAllText($bootstrapFiles[0].FullName)
    if(([regex]::Matches($sdk,[regex]::Escape($entry))).Count -ne 1){throw 'SDK bootstrap source changed; adapter needs review'}
    [IO.File]::WriteAllText("$out\sdk_nvn_bootstrap.c",$sdk.Replace($entry,'void nvn_sdk_bootstrap_original(Cpu *c) {'))
    $new+=Compile-Unit 'sdk_nvn_bootstrap' "$out\sdk_nvn_bootstrap.c" @('/TC','/I',"$root\build\sdk")
    $bootstrapPattern=[regex]::Escape('\sdk\'+$bootstrapFiles[0].BaseName+'.obj')+'"?$'
    $objects=@(Get-Content -LiteralPath $response | Where-Object {$_ -notmatch '\\(gpu|gpu_d3d11|gpu_overlay|gpu_menu|ipc_vi|kernel)\.obj"?$' -and $_ -notmatch $bootstrapPattern})+$new
    foreach($object in $objects){if(!(Test-Path -LiteralPath $object.Trim('"'))){throw "Missing link input: $object"}}
    [IO.File]::WriteAllLines("$out\game.rsp",$objects)
    $next=Join-Path $out 'ssbu-nvn-vulkan-next.exe'
    & $Compiler /nologo @linker @systemCompile "@$out\game.rsp" "/Fe$next" /link @systemLink /HIGHENTROPYVA:NO "/MAP:$out\ssbu-nvn-vulkan-next.map" winmm.lib Synchronization.lib user32.lib gdi32.lib dxguid.lib ws2_32.lib ole32.lib oleaut32.lib opengl32.lib shell32.lib psapi.lib dbghelp.lib build/codecs/opus.lib build/codecs/libvpx.lib *> "$out\game-link.log"
    if($LASTEXITCODE -ne 0){Get-Content "$out\game-link.log" -Tail 30;throw 'NVN link failed; existing playable executable was not replaced'}
    $sdl=Join-Path $root 'deps\SDL3\lib\x64\SDL3.dll'
    if(!(Test-Path -LiteralPath $sdl)){throw 'SDL3 missing; rerun the NVN installer'}
    Copy-Item -LiteralPath $sdl -Destination $out -Force
    Copy-Item -LiteralPath $CompilerHelper -Destination "$out\ssbu_glslang.dll" -Force
    Copy-Item -LiteralPath "$root\third_party\opensmash-nvn\LICENSE-SDL3.txt" -Destination "$out\LICENSE-SDL3.txt" -Force
    $glslangLicense=Join-Path (Split-Path $CompilerHelper -Parent) 'release\LICENSE.txt'
    if(Test-Path -LiteralPath $glslangLicense){Copy-Item -LiteralPath $glslangLicense -Destination "$out\LICENSE-glslang.txt" -Force}
    @{sha256=(Get-FileHash -LiteralPath $CompilerHelper -Algorithm SHA256).Hash;source='third_party/opensmash-nvn/runtime/vk_glslang_shim.c';purpose='GLSL to SPIR-V compiler helper'} | ConvertTo-Json | Set-Content "$out\compiler-provenance.json" -Encoding ASCII
    foreach($name in @('LICENSE.OpenSmash','source-manifest.json')){Copy-Item -LiteralPath "$root\build\opensmash-nvn-source\$name" -Destination $out -Force}
    $current=Join-Path $out 'ssbu-nvn-vulkan.exe'
    if(Test-Path -LiteralPath $current){Copy-Item -LiteralPath $current -Destination "$out\ssbu-nvn-vulkan-previous.exe" -Force}
    Move-Item -LiteralPath $next -Destination $current -Force
    Move-Item -LiteralPath "$out\ssbu-nvn-vulkan-next.map" -Destination "$out\ssbu-nvn-vulkan.map" -Force
    foreach($folder in @('cache','reports','screenshots')){New-Item -ItemType Directory -Path "$out\$folder" -Force | Out-Null}
    Write-Host "NVN build ready: $current"
}finally{Pop-Location}
