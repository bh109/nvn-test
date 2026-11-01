param([string]$ReferenceRoot='',[string]$OutputDirectory='build\opensmash-nvn-source',[string]$Python='')
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
. "$root\tools\nvn_common.ps1"
$python=Find-NvnPython $Python
if(!$ReferenceRoot){$ReferenceRoot=Join-Path $root 'third_party\opensmash-nvn'}
$reference=(Resolve-Path -LiteralPath $ReferenceRoot).Path
$sourceRoot=Join-Path $reference 'runtime'
$out=if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $root $OutputDirectory}
$runtime=Join-Path $out 'runtime';New-Item -ItemType Directory -Path $runtime -Force|Out-Null
& $python "$root\tools\nvn_contract.py" --output-dir $out
if($LASTEXITCODE -ne 0){throw 'Cannot stage graphics against unsupported SDK/main images'}
$pin=Join-Path $reference 'upstream-manifest.json'
if(Test-Path -LiteralPath $pin){
    foreach($file in (Get-Content -LiteralPath $pin -Raw | ConvertFrom-Json).files){
        if((Get-FileHash -LiteralPath (Join-Path $reference $file.path) -Algorithm SHA256).Hash -ne $file.sha256){throw "Vendored graphics source changed: $($file.path)"}
    }
}
$pending=[Collections.Generic.Queue[string]]::new()
foreach($unit in @('nvn_stub.c','nvn_gl.c','nvn_vk.c','nvn_c4.c','nvn_c5.c','nvn_sdk.c','mx_translate.c','tex_decode.c','gfx_options.c')){$pending.Enqueue($unit)}
$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$manifest=@()
while($pending.Count){
    $relative=$pending.Dequeue();if(!$seen.Add($relative)){continue}
    $source=[IO.Path]::GetFullPath((Join-Path $sourceRoot $relative))
    if(!$source.StartsWith($sourceRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw "Include escapes source root: $relative"}
    $destination=Join-Path $runtime $relative
    $text=[IO.File]::ReadAllText($source)
    foreach($match in [regex]::Matches($text,'(?m)^\s*#include\s+"([^"]+)"')){
        $parent=Split-Path $relative -Parent
        $include=if($parent){Join-Path $parent $match.Groups[1].Value}else{$match.Groups[1].Value}
        if(!(Test-Path -LiteralPath (Join-Path $sourceRoot $include))){throw "Missing reference include: $include"}
        $pending.Enqueue($include)
    }
    New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force|Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $manifest+=@{path=$relative;source=$source;sourceSha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;adaptations=@()}
}
function Replace-Checked([string]$File,[string]$Before,[string]$After,[string]$Reason){
    $path=Join-Path $runtime $File;$text=[IO.File]::ReadAllText($path)
    if(([regex]::Matches($text,[regex]::Escape($Before))).Count -ne 1){throw "Adaptation marker changed: $File ($Reason)"}
    [IO.File]::WriteAllText($path,$text.Replace($Before,$After))
    ($manifest|Where-Object {$_.path -eq $File}).adaptations+=@{reason=$Reason;before=$Before;after=$After}
}
Replace-Checked 'nvn_sdk.c' '#define SDK(a) (SDK_BASE + (uint64_t)(a))' '#define SDK(a) (0x7109000000ull + (uint64_t)(a))' 'Use verified mapped WindowSmash SDK model pointers'
Replace-Checked 'nvn_vk_store.c' '#include "hostrt.h"' ("#include `"hostrt.h`"`nvoid opensmash_worker_enter(Ctx *, void *, uint64_t);`nvoid opensmash_worker_leave(Ctx *);") 'Declare explicit worker context lifecycle bridge'
$stack='c->x[31] = (uint64_t)(uintptr_t)(stack + stack_size - 256);'
Replace-Checked 'nvn_vk_store.c' $stack ($stack+"`n    opensmash_worker_enter(c, stack, stack_size);") 'Register each container decoder worker with its private stack'
Replace-Checked 'nvn_vk_store.c' '    _aligned_free(stack);' ("    opensmash_worker_leave(c);`n    _aligned_free(stack);") 'Unregister worker before releasing its stack'
Replace-Checked 'nvn_vk_draw.c' '    if (vt->layout == to) return;' '    if (vt->layout == to && to != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) return;' 'Keep transfer-write dependency when a clear/upload is followed by another write in the same image layout'
$barCheckpoint='        CKPT_OP("progress bar blit (swapchain image, index)", V.images[index], index, 0, 0);'
Replace-Checked 'nvn_vk_draw.c' $barCheckpoint ("        image_barrier(V.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);`n"+$barCheckpoint) 'Order the game image, progress track and overlapping progress fill writes; layout remains transfer destination'
$textStart='static void ovl_text(uint32_t index, int x, int y, int dot, const char *s) {'
Replace-Checked 'nvn_vk_draw.c' $textStart ($textStart+"`n    image_barrier(V.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);") 'Order overlay text after earlier swapchain image writes; glyph runs within this call are disjoint'
$oskBlit='    vkCmdBlitImage(R.cmd, g_osk.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, V.images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);'
Replace-Checked 'nvn_vk_draw.c' $oskBlit ("    image_barrier(V.images[index], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);`n"+$oskBlit) 'Order optional keyboard overlay after the game image'
$overlayStart='static void overlay_draw(uint32_t index) {'
$overlayTest=@'
    /* Explicit regression mode: exercise overlapping track/fill on every present,
     * including a warm-cache intro that would otherwise never show loading UI. */
    static int overlay_test = -1, overlay_test_frames;
    if (overlay_test < 0) overlay_test = getenv("SSBU_VK_OVERLAY_SELFTEST") && atoi(getenv("SSBU_VK_OVERLAY_SELFTEST"));
    if (overlay_test) {
        g_ovl.frac = 0.9f; g_ovl.thick = 8; g_ovl.screen = 1;
        snprintf(g_ovl.text[0], sizeof g_ovl.text[0], "SYNC TEST 90 %%");
        if (++overlay_test_frames == 60) printf("nvn_overlay_selftest: 60 overlay frames exercised (track, fill, text)\n");
    }
'@
Replace-Checked 'nvn_vk_draw.c' $overlayStart ($overlayStart+"`n"+$overlayTest) 'Opt-in actual-GPU overlay regression exercise; disabled in normal gameplay'
$callback='static VkBool32 VKAPI_PTR vk_debug_cb(VkFlags severity, VkFlags types,'
$vk=[IO.File]::ReadAllText((Join-Path $runtime 'nvn_vk.c'))
if($vk.Contains($callback)){
    Replace-Checked 'nvn_vk.c' $callback 'static VkBool32 VKAPI_PTR vk_debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,' 'Use exact Khronos debug callback parameter types'
}else{throw 'Reference Vulkan debug callback signature changed; review it before staging.'}
Copy-Item -LiteralPath (Join-Path $reference 'LICENSE') -Destination (Join-Path $out 'LICENSE.OpenSmash') -Force
foreach($entry in $manifest){$entry.stagedSha256=(Get-FileHash -LiteralPath (Join-Path $runtime $entry.path) -Algorithm SHA256).Hash}
@{purpose='Graphics port staging only; no generated game functions or keys';reference=$reference;files=$manifest;license='LICENSE.OpenSmash';note='All quoted local includes are recorded; system/Vulkan headers and compiler flags remain external build inputs.'}|ConvertTo-Json -Depth 7|Set-Content "$out\source-manifest.json" -Encoding UTF8
Write-Host "Staged $($manifest.Count) source/include files with GPL license, hashes and explicit adaptation records: $out"
