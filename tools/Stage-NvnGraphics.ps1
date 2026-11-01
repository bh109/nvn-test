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
$pcacheThreshold='        if (have_file && !marker && added < 8 && !(e && !strcmp(e, "always"))) {      /* under 8 new entries: not worth 0.9 GB of writing (run s14prep1: 1 entry = 11 s of exit); they are made again next time, a few ms */'
$pcacheThresholdFixed='        if (have_file && !marker && added < 1 && !(e && !strcmp(e, "always"))) {      /* Preserve every new driver-cache entry: even one AMD stage-library miss can stall a battle for tens of milliseconds. */'
Replace-Checked 'nvn_vk_m4.c' $pcacheThreshold $pcacheThresholdFixed 'Persist even a small number of expensive driver-cache misses instead of repeating fight hitches next launch'
$pcacheSkipLog='            printf("nvn_vk: pipeline cache NOT rewritten at exit: %lld creations were added since %s (under 8: not worth rewriting the file; SSBU_VK_PCSAVE=always writes it anyway)\n", (long long)added, g_pc_dirty_saved ? "the last save of this run" : "it was read at start");'
$pcacheSkipLogFixed='            printf("nvn_vk: pipeline cache NOT rewritten at exit: no creations were added since %s (SSBU_VK_PCSAVE=always writes it anyway)\n", g_pc_dirty_saved ? "the last save of this run" : "it was read at start");'
Replace-Checked 'nvn_vk_m4.c' $pcacheSkipLog $pcacheSkipLogFixed 'Describe the zero-new-entry cache-save policy accurately'
$gapRecord='            g_fr_wall[frame] = t_prev ? (float)((double)(t_now - t_prev) * 1000.0 / (double)fq.QuadPart) : 0.0f;'
$gapRecordFixed=@'
            g_fr_wall[frame] = t_prev ? (float)((double)(t_now - t_prev) * 1000.0 / (double)fq.QuadPart) : 0.0f;
            {   /* WindowSmash: bounded attribution for long battle presentation gaps; off unless SSBU_VK_GAPLOG=<ms>. */
                static int gap_ms = -1, gap_lines;
                if (gap_ms < 0) gap_ms = getenv("SSBU_VK_GAPLOG") ? atoi(getenv("SSBU_VK_GAPLOG")) : 0;
                if (battle && t_prev && gap_ms > 0 && g_fr_wall[frame] >= (float)gap_ms && gap_lines++ < 100) {
                    double k = 1000.0 / (double)fq.QuadPart;
                    printf("nvn_vk_gap: frame %lld: %.2f ms between presents = %.2f before present job + %.2f present job (hold %.2f, pictures %.2f, previous queue-present %.2f, acquire %.2f, blit %.2f, submit %.2f, queue-present %.2f, front-end %.2f, reports %.2f)\n",
                           (long long)frame, (double)(t_now - t_prev) * k, (double)(t_present - t_prev) * k, (double)(t_now - t_present) * k,
                           (double)(tp[0] - t_present) * k, (double)(tp[1] - tp[0]) * k, (double)(tp[2] - tp[1]) * k,
                           (double)(tp[7] - tp[2]) * k, (double)(tp[3] - tp[7]) * k, (double)(tp[4] - tp[3]) * k,
                           (double)(tp[5] - tp[4]) * k, (double)(tp[6] - tp[5]) * k, (double)(t_now - tp[6]) * k);
                }
            }
'@
Replace-Checked 'nvn_vk_draw.c' $gapRecord $gapRecordFixed 'Add opt-in capped attribution of long battle gaps without enabling broad profiling'
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
