Native Vulkan backend development
=================================

Status, 2026-10-27

An experimental direct Vulkan game executable is now available at
build\native-vulkan\ssbu-native-vulkan.exe. runtime/gpu_vulkan.cpp implements
the gfx_* command processor interface and is linked into that separate build.
The default ssbu.exe and existing D3D11/DXVK launchers are unchanged.

Implemented

native_device.cpp: Vulkan 1.3 device/queue, host-visible upload buffers,
device-local images, descriptors, command submission, barriers and persisted
driver pipeline cache. Two frame contexts allow CPU preparation to overlap
GPU execution. Each owns its uploads, descriptors, command pool, fence and
acquire semaphore. Reuse waits for that context's fence; compute and readback
still wait explicitly. Pipeline compilation remains synchronous.
Consecutive draws with identical attachments now share a dynamic rendering
instance. Copies, layout changes, clears and compute work end the instance;
explicit dependencies remain between distinct instances. This avoids a full
pipeline barrier and attachment load/store cycle for every UI draw.

native_shader.cpp: existing translator-generated HLSL -> DXC -> SPIR-V.
No DXBC or D3D11/DXVK runtime participates. Explicit descriptor sets separate
vertex and pixel resources. Generic attributes have explicit locations.
The gfx_draw binding bridge uses the same mapping. SPIR-V binaries are cached
beside the executable in shader-cache, keyed by rewritten source, stage,
compiler version and compilation/layout settings. SSBU_VK_NO_SHADER_CACHE
disables that cache for compiler investigations.

native_pipeline.cpp: reusable graphics and compute pipelines, per-stage
descriptor layouts, multiple color attachment formats, blend/depth/stencil
state, dynamic viewport/scissor/blend constants/stencil reference.

native_present.cpp: Win32 Vulkan surface and swapchain, aspect fitting,
image blits, acquire/present synchronization, and resize handling. Present
completion semaphores belong to swapchain images so reuse follows reacquisition.
The shared game display layer will continue to own the real window and input.

native_maxwell.cpp: console register state -> Vulkan viewport, scissor,
topology, packed target formats, blend/depth/stencil state and translator
parameters. Negative viewport height retains the translator's existing
clip-space convention. RGB and alpha blending remain independent.

gpu_vulkan.cpp: actual command-processor entry points, guest address reads,
indexed/array draws, constant and vertex buffers, shader lookup, texture
decoding/upload and samplers, render-target sampling and feedback snapshots,
masked color/depth/stencil clears, scaled GPU image blits, compute dispatch
with overlapping storage-buffer ranges sharing one allocation, and present.
GPU virtual and guest unmapping notices retire dependent images after pending
GPU work finishes. Unbound texture headers use zero textures, matching the
old backend's null-resource behavior. Target selection follows most recent
use rather than creation order when the game reuses memory.

Verification already run on Intel Iris Xe, driver 32.0.101.7092

* Compute: all 32 GPU results correct.
* Graphics: all 4,096 pixels correct for R10 image sampling into an RGBA target.
* Presentation: eight hidden-window frames, with resize and semaphore reuse.
* Shader corpus: 716/716 saved game shaders compiled to SPIR-V, comprising
  473 vertex, 239 pixel, and four compute programs. Compilation alone does
  not establish correct execution for each program.
* Original ps_7acee7cf222346b5 effect shader: nine controlled native Vulkan
  draws compared with Microsoft WARP, 4,425 surviving and 4,791 discarded
  pixels, maximum component difference 5.96046448e-08. This is a fixture
  vertex shader and synthetic resources, not a replay of the full game scene.
* Maxwell register-state regressions: viewport coverage, lower-left scissors,
  depth remapping, per-face stencil, independent alpha blending, write masks
  and packed/sRGB formats pass.
* The actual game booted through 330 presented frames and produced a recognizable
  opening-movie frame. This validates startup and its rendering path, not a
  training match, character skinning, or compute effects in gameplay.

These checks do not establish full-game stability, correct rendering of every
effect, or an improvement in frame rate. This is an experimental correctness
build: compute output is synchronized back to guest memory per dispatch.
Unchanged watched buffers/textures are now reused; small or changing buffers
retain snapshots for correctness. GPU-only compute residency, asynchronous
compilation and reducing CPU buffer/shader work remain performance work.

Known first-build limitations: the settings/graphical performance overlay and
D3D shader-warming process are omitted; display.ini still controls the window
and normal game/controller input remains available. F3 saves a native BMP and
F6 starts the existing CPU sampling profiler. CPU-only 2D blit sources retain
the old backend's unsupported behavior. Overlapping self-blits and unsupported
formats/instructions report explicit errors. Broad gameplay validation remains.

Build the separate game executable using existing compiled game objects

powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\diagnostics\Build-NativeVulkanGame.ps1"

The default object response file is build\ssbu-intel-vs-override-test.rsp;
-ResponseFile selects another equivalent game-object list. The script replaces
the D3D renderer/overlay/menu objects with the native backend and leaves the
default executable alone. The resulting game imports no D3D11/DXGI compiler DLLs.

Launch the experimental game for a training-stage rendering check

powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\diagnostics\Test-NativeVulkanGame.ps1"

This uses Intel, half resolution, statistics logging, and restores its process
environment after the game closes. The log is
build\native-vulkan\play-native-vulkan.log. F3 writes screenshots\native_*.bmp.
The five-second freeze watchdog is disabled in this test launcher because
synchronous initial pipeline compilation can trigger false freeze reports.

For an automated startup check add -Bounded -CaptureFrame 300 -ExitFrame 330.
It has a 60-second process timeout and verifies the requested frame count was
reached. SSBU_VK_CAPTURE_TARGETS=1 additionally saves the intermediate color
targets at that capture. This can add readback delays; leave it unset for play.

Build and automated verification from the repository root

powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\diagnostics\Test-NativeVulkan.ps1"

Optional -ShaderDirectory compiles a captured HLSL directory.
Optional -EffectShader runs the unmodified effect against a fresh WARP reference.
Optional -SkipGpu builds and optionally checks source compilation without GPU work.
GPU checks require normal access to Intel's driver cache; restricted sandbox
execution can produce unrelated shader-cache errors. No gameplay or screenshots
are required. Logs and executables are under build\native-vulkan.

Dependencies (development-only, not checked into the runtime)

Khronos Vulkan-Headers v1.3.290, extracted under deps\vulkan-native:
https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.290.tar.gz
Downloaded archive SHA256:
f38a653bf93cab7a2a229a53d2d53b1cba9a2819e4c0a7de13c54085bde9bcf5

Microsoft DirectXShaderCompiler v1.8.2505.1, Windows x64, extracted to
deps\vulkan-native\dxc, using release asset dxc_2025_07_14.zip:
https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1
Downloaded archive SHA256:
9ad895a6b039e3a8f8c22a1009f866800b840a74b50db9218d13319e215ea8a4

The repository's matching Microsoft static C++ libraries are required as well
as its existing C libraries. They were extracted from the same official
CRT Desktop package recorded in deps\msvc\windowsmash.json, with its recorded
SHA256 verified. Tools/msvc_deps.py now retains libcpmt.lib for new installs.
An older dependency installation may still need that library added from its
recorded matching CRT package before compiling these C++ files.

SPIR-V compiler reference:
https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst

Driver-facing implementation: Vulkan loader from the installed graphics driver.
DXC DLLs are copied beside the verification executables by the build script.
The WARP reference executable alone links D3D11; the native Vulkan tests do not.

Title/menu performance changes, 2026-10-27

native_watch.cpp shares per-page write epochs across observers, using the
same Windows MEM_WRITE_WATCH mechanism as the existing D3D backend. Textures
query their dirty pages at first use in a frame and reuse unchanged images;
unwatched images retain a full checksum fallback, once per check epoch rather
than per draw. Explicit guest writes advance the check epoch. Tests cover
overlapping resources so one observer resetting the OS bits cannot hide a
write from another resource.

native_buffers.cpp keeps watched constant/vertex buffers of at least 4 KiB,
with a 512 MiB cache budget. The same unchanged buffer can serve many draws.
A changed buffer referenced by any incomplete submission gets a fresh
arena snapshot; it is never overwritten under an earlier draw. Persistent
storage is suballocated from 32 MiB slabs rather than creating a Vulkan memory
allocation for every small buffer. A full 512 MiB pool is recycled only across
a completed-submission boundary. A GPU test records two versions before
submitting, verifies both output values, then checks persistent reuse and
independent descriptor offsets in the same slab. A further GPU check submits
one version asynchronously, changes its guest source, and verifies both
in-flight versions after the second submission completes. Retired images and
views carry submission serials and survive until their last use completes.

Shader size decoding is cached by guest address and stage, with a full code
checksum still checked before reuse. Unmapping invalidates those entries.
The [native perf] log lines report texture/lookup CPU time, texture hits,
checksum volume, rendering-instance count, buffer reuse, and copied bytes.
They also separate upload CPU time, GPU fence waits, queue submission,
swapchain acquisition and presentation. Clock pacing selects MAILBOX when
available (IMMEDIATE otherwise, then FIFO fallback); display pacing keeps
FIFO. The prototype previously forced FIFO regardless of display.ini.

diagnostics/Measure-NativeVulkanTitle.ps1 provides a single-instance timed
run, uses the existing SSBU_INPUT hook to skip the movie, saves configurable frame 1900 by default,
and stops/validates its own test process. Its JSON deliberately leaves
sceneConfirmed=false until the saved image is inspected. FPS comes from
differences in the game's frame counters, excluding shader/scene transitions
when comparing steady-state results.

The 2026-10-27 title measurement with two frame contexts reached 22.6-23.6 FPS
over the steady 42-62 second interval, versus roughly 19 FPS in the preceding
single-context run. Both saved frame-900 images were inspected and show the
Press Any Button title screen at half resolution. These are sequential local
measurements, not controlled full-game benchmarks. The title remains slow:
buffer uploads and shader lookup remain significant CPU costs. Regression
checks for compute, graphics, presentation/resize and in-flight buffers pass.

Battle-transition instrumentation separates cumulative Maxwell-to-HLSL
translation, HLSL binding rewriting, DXC compilation (SPIR-V cache hits/misses),
and graphics/compute pipeline creation. SSBU_LOG_STATS also records preparation
begin/end events, with durations, so an interrupted run retains the last stage.
Compute counters separate input allocation/copying, submit/wait, and guest
writeback. Shader lookup and compute-total counters are inclusive: they overlap
these detailed counters and must not be added to them. A missed preparation end
can also mean the process was closed, rather than an internal deadlock.
diagnostics/Summarize-NativeVulkanWork.ps1 prints the slowest preparation calls
and latest cumulative counters from the regular game log. This instrumentation
does not yet move compilation off the rendering thread.

Compute readback, 2026-10-27

Battle telemetry recorded 40.7 seconds in guest writeback across 5,396 compute
dispatches, alongside 25.7 seconds of DXC work and 20.9 seconds creating graphics
pipelines. Those totals overlap compute's inclusive total and describe that
specific run. A 512 MiB CPU-copy probe measured reads at 248 MiB/s from the
default coherent upload memory (flags 0x7), versus 8,009 MiB/s from coherent
HOST_CACHED memory (flags 0xf). Compute storage buffers now prefer the latter
when compatible, retaining the original coherent fallback. Upload-only buffers
keep their previous memory policy. The probe can be built from
diagnostics/benchmark_native_uploads.cpp with native_device.obj; it now uses
the actual buffer allocator rather than selecting memory itself. The compute
GPU regression verifies readback results and preference when compatible cached
memory exists. This does not eliminate synchronous compute transfers or shader
compilation pauses, and the copy probe does not predict full-game FPS.

The following user battle run recorded 1.352 seconds of writeback across 8,242
dispatches (0.164 ms/dispatch), versus 40.677 seconds across 5,396 previously
(7.538 ms/dispatch). These were different play sessions, not an identical replay.
DXC work (47.3 seconds), graphics pipeline creation (47.7 seconds), and compute
submit/wait (28.4 seconds) remained significant in that run.

ReadbackPool now retains coherent CPU-readable storage buffers across compute
dispatches. Slots are best-fit, rounded to powers of two, and reused only after
their last submission serial completes. Completed idle buffers are evicted above
a 256 MiB soft budget; a larger live dispatch can exceed that budget. Descriptors
still expose only the requested range. GPU tests check output correctness, reuse,
and isolation of same-submission and asynchronous pending slots. The game logs
allocation/reuse counts and retained memory for comparisons.

Program-code checksums now use the same shared write-watch epochs as buffers and
textures, with conservative checksum fallback outside watched memory. Changed
code is still decoded and hashed; unmapping clears program entries. Jump-table
specialization is still checked independently. [native shader code] reports the
unchanged-code reuse count and checksum volume. This reduces recurring CPU work;
first-use DXC and Vulkan pipeline creation are still synchronous.

Synchronization reference for the batching boundary:
https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdEndRenderingKHR.html
Presentation-mode reference:
https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html

After shader-code write tracking, the unattended 65-second run reported
59.6 FPS at 478.1 draws/frame during the 35-40 second title interval, versus
22.6-23.6 FPS in the earlier two-frame build. This is a short local measurement,
not a full-match benchmark. A subsequent 180-second run with desktop capture
and input attempts had variable FPS and progressed through attract movies into
match loading. Its frame-1900 BMP was inspected and is the title screen; later
intervals must not be labelled title or gameplay measurements without scene
confirmation. The run exercised 3 readback-pool allocations and 102 reuses.
Automated keyboard input did not reliably enter a selected local match.

Shader-cache audit/replay

The regular diagnostic launcher now sets SSBU_VK_CAPTURE_SHADERS=1 and restores
it on exit. This saves original generated HLSL under build/native-vulkan/
shader-inputs, named by stage and the full rewritten-source cache identity.
It also logs raw shader name, SPIR-V cache identity, and hit/miss. Raw Maxwell
names alone do not distinguish signatures, compute local sizes, shared memory,
or jump-table specializations. Differing cache IDs need source comparison;
they must not be assumed to be a broken cache. A regression checks identical
source produces identical cached SPIR-V without another compile.

diagnostics/Replay-NativeVulkanShaders.ps1 replays these captured compiler inputs
without starting the game. -ForceCompile bypasses the SPIR-V cache to measure
DXC work; the default verifies cached replay. Each replay gets its own log.
This does not replay complete draws or warm unknown shader variants/pipelines.
Captures enable offline investigation; they do not remove compilation pauses.

Repeated compilation fix: deterministic temporary names

The scripted reproduction captured cs_e9cdd6461c29fb78 twice with different
cache IDs. A source diff found only texture-result temporary names t261 versus
t321. The shared translator deliberately keeps an HLSL texture counter across
programs, making raw source depend on translation order. Native Vulkan now
alpha-renames tN identifiers deterministically before computing its SPIR-V
cache key, excluding register declarations, strings, and comments. The shared
translator and legacy D3D path remain unchanged. Existing legacy entries are
imported when available, because changing temporary names preserves semantics.

Offline replay of 642 captured variants passed with 642 hits and no DXC work
after migration. Both captured versions of that compute shader now map to
7e6f6af968856db5.spv. GPU regression tests include this naming/cache issue and
register-slot preservation. The nine-case original effect comparison against
WARP still passes at maximum component error 5.96046448e-08. This fixes one
cause of repeated compilation, not GPU synchronization or every new pipeline.

Measure-NativeVulkanTitle.ps1 now accepts -InputScript to drive the existing
SSBU_INPUT hook and captures shader inputs. Fixed-time menu scripts depend on
loading duration and saved menu state; they may reach rule editing instead of
a match. Verify the actual scene before calling their counters gameplay FPS.
The post-fix navigation run reached an unsaved rule-name prompt, which was
cancelled; it is not a valid battle-performance comparison.

Steady battle measurements, 2026-10-28

The user run's 160-170 second interval presented 160 frames without DXC work.
It dispatched 2,163 compute kernels (13.5/frame), spending 3.780 seconds in
compute submit/wait (23.6 ms/frame), with 1.536 seconds of draw-buffer upload
CPU time (9.6 ms/frame). Compute still synchronizes back to guest memory after
each dispatch. These CPU counters alone cannot separate GPU kernel execution
from prior queued work, driver overhead, and CPU rescheduling.

Test-NativeVulkanGame.ps1 -GpuTimings enables optional hardware timestamp
queries, with per-frame query pools and collection after the associated fence.
Logs separate GPU compute duration and whole command-buffer GPU duration from
CPU waits. Timestamp deltas handle the queue's valid-bit width and timestamp
period; unsupported queues leave timings disabled. This diagnostic adds marker
commands and should not be treated as an overhead-free FPS benchmark. It is off
in the normal launcher. Regression tests exercise compute timestamp results,
graphics, frame reuse and presentation/resize with the mode enabled.

Shader variant lookup now uses a hash index of matching shader keys instead of
scanning every loaded shader on every draw. Jump-table specialization is still
checked for each candidate. [native shader lookup] reports candidate checks.
This reduces unnecessary CPU search work; it does not defer compute readback.

Initial conservative compute batching, opt-in (superseded below)

The next user log recorded 1.150 seconds of GPU compute execution across 13,884
dispatches and 40.766 seconds of surrounding CPU submit/wait time. That difference
includes earlier graphics work and synchronization, not just CPU overhead.

Test-NativeVulkanGame.ps1 -BatchCompute enables experimental batching and
compatible dependent-dispatch residency. -GpuTimings additionally enables query
instrumentation; omit it for ordinary performance comparisons. Results remain in
coherent buffers until an incompatible dependency, CPU-visible access, or stream
end requires publication. GPU/global ranges, mapped guest aliases, constant buffers, shader
code, texture/sampler headers, and texture backing are checked before preparing
the next dispatch's uploads. Noncontiguous or aliased ranges within a dispatch
fall back to the synchronous path. A 64 MiB pending-result threshold limits
accumulation between dispatches. Compatible covered ranges can be reused by
later kernels without publication.

The native build compiles its own gpu.c object with SSBU_NATIVE_COMPUTE_BATCH.
It publishes results before semaphore releases, syncpoint increments, CPU copies,
inline/constant-buffer writes and reads of queued command/QMD data. These calls
compile out of the legacy command processor. Default native play keeps batching
off. -BatchCompute is required to test it; the launcher restores its environment.

GPU regressions verify independent dispatches share a single submit/wait, retain
correct values, publish together, and reject aliased dependencies. A CPU test
includes the actual gpu.c and verifies publication order for semaphore releases,
syncpoints, inline writes and constant-buffer writes. Startup with batching and
timestamps enabled reached 330 frames. These checks do not establish full-match
performance or stability. Batching counters report deferred dispatches, result
ranges and publication waits for the gameplay comparison.

In batching mode, compute's inclusive counter measures dispatch-call time;
publication from later graphics/command callbacks is outside those calls and is
recorded in the separate submit/wait and writeback counters. Do not add these
overlapping counters or interpret dispatch-call time as total compute latency.

GPU-resident chaining within a command stream

The initial batching run deferred 20,113 dispatches but still published 18,558
times: it avoided only 7.7 percent of waits. Dependencies prevented most grouping.
The -BatchCompute path now retains compatible result ranges for later kernels
and shader-buffer bindings. Fully covered GPU/guest aliases use the retained
buffer and appropriate aligned descriptor offset; updated subranges remain in
the original owner so publication includes the later writes. Partial, resized,
unaligned, or noncontiguous views fall back to publication before uploads.

Graphics preflight distinguishes GPU data bindings from CPU reads. Compatible
vertex/global/constant buffers can be bound directly. CPU jump-table lookups,
texture handles/headers/backing, global descriptors and converted index buffers
require publication if they depend on retained data. These checks occur before
preparing upload slices, since submitting switches frame contexts. Clear,
presentation, stream ends and CPU completion/access points still publish results.
This is bounded residency within a command stream, not a persistent GPU cache
across every guest fence or submission.

A GPU regression chains three dependent kernels (including alias and subrange
views) into an integer graphics target in one submission. It checks both CPU
publication (42 and 1) and all 16 graphics pixels (42), with no early guest writes.
The [native residency] counters report compute reuse, direct GPU shader-buffer
bindings and incompatible-range fallbacks.

Battle validation, 2026-10-28

The full native regression suite passed, including the resident compute/graphics
chain and actual command-processor CPU visibility ordering. The game was rebuilt
and two bounded local matches were reached through the controller test endpoint.
These were different fighter combinations, not a controlled replay comparison.

The unprofiled run (resident-match-unprofiled.log/.json in build/native-vulkan)
reached 32-44 FPS during several battle intervals, but also suffered severe
compilation/pipeline hitches. This is not sustained full-speed performance.
The second run's screenshot native_20261028_015839_663.bmp confirms a live
Squirtle-versus-Yoshi match on Small Battlefield. Its final counters were 18,885
deferred dispatches, 2,339 publication waits and 37,314 compute-buffer reuses.
Direct graphics-buffer reuse was zero in this real run, despite passing the
synthetic graphics test. Do not report actual graphics residency from that test.

profile_20261028_015754.txt completed during the second run. Main-thread busy
samples concentrate in guest NVN fence/time polling; GPU-thread/compiler samples
also show DXC and Intel compilation. The profile is not a clean steady-state
benchmark, and profiling/report generation can itself disturb frame rate.
Remaining submit/wait time includes preceding GPU work and driver scheduling;
it must not all be labelled CPU overhead. Shader/pipeline compilation remains
synchronous, and full-speed battle performance is unresolved.

Publication attribution and redundant state, 2026-10-28

[native publication] logs identify each actual CPU publication by function and
source line, including cumulative count, submission/wait and copy time. Empty
flush calls are not counted. The bounded publication-baseline run recorded 1,907
publications at image clears (17.23 s waiting), 327 at draw shader-code preflight
(2.06 s), and 23 at presentation (0.17 s). These are run totals, not per-frame
latencies or a GPU-kernel execution measurement.

Image clears now stay in the GPU command buffer without publishing unrelated
compute buffers. Image transitions/render barriers preserve GPU ordering. CPU
visibility, actual buffer dependencies, stream ends and presentation still
publish. Conservative shader-code windows crossing unmapped memory now check
mapped pieces for guest aliases, rather than treating every hole as a dependency.

Graphics pipeline keys use a fixed 96-word array instead of allocating/growing a
vector on every draw. Disabled blend equations and depth/stencil settings, plus
unused attachment slots, share keys. Enabled operations, attachment formats and
color write masks remain distinct. CPU regressions check these distinctions;
GPU regression includes an unrelated image clear between dependent kernels.
These changes do not implement asynchronous shader or pipeline compilation.

The full CPU/GPU regression suite passed and the game was rebuilt at 09:16.
publication-optimized.log/.json preserve the subsequent bounded run. Its final
counters were 13,418 deferred dispatches, 1,250 publications, 30,615 compute
buffer reuses and 1,986 direct graphics-buffer reads. Clear and shader-window
publications disappeared; remaining publications were mainly partially covered
raw buffer views and uniforms. Menu pipeline creations at 45 s fell from 106 to
51. These work reductions are not a demonstrated end-to-end FPS improvement:
the comparison used different evolving game state/cache contents, the new run
encountered more shader misses and severe compilation stalls, and no visual
capture at frame 6,500 was reached before its deadline. Full-speed gameplay
remains unresolved. The tests stopped their own processes at the time limit.

Recorded pipeline preparation, 2026-10-28

The native game now records successful graphics/compute pipelines under
build/native-vulkan/pipeline-recipes. Program files contain SPIR-V and explicit
descriptor bindings; small pipeline files pair a program identity with its
canonical state. Versioned, bounded, checksummed records avoid serializing
compiler padding, pointers or live Vulkan handles. Preparation validates records
and skips corrupt, truncated or missing data. Records are atomically replaced.

Build-NativeVulkanGame.ps1 also builds prepare-native-vulkan.exe. The normal
Test-NativeVulkanGame.ps1 launcher runs Prepare-NativeVulkanPipelines.ps1 before
starting the game; -SkipPreparation bypasses it for comparisons. The standalone
tool creates previously seen pipelines without loading the game, drawing frames,
or calling DXC, and saves the resulting driver cache. Its process is bounded and
must run with the game closed. No recorded pipelines is a successful no-op.
Newly encountered shaders/effects still use synchronous first-use compilation.
This is preparation of known work, not an asynchronous compiler or a guarantee
of full-speed battle performance.

Driver cache saves now replace the file atomically and skip unchanged pipeline
revisions. During the recording run each approximately 128 MiB snapshot took
about 180-200 ms. The game checkpoints at most every 30 seconds rather than
every 300 frames; normal close and explicit bounded completion still save.
[native cache saves] reports actual writes and cumulative time. Recipe and shader
files are recorded independently, so subsequent preparation can recover known
work even if a force-stopped game did not checkpoint the driver cache.

Regression tests passed for descriptor/state roundtrips, rejection of corrupt or
truncated records, 16 expected pixels from a reconstructed graphics pipeline,
and skipping unchanged cache saves while saving new revisions. The full native
CPU/GPU suite passed. A real recording run reached Squirtle versus Mario on Small
Battlefield (native_20261028_093516_163.bmp) and collected 276 pipeline recipes.
Preparing those records reported 276 ready, zero skipped, 290.9 ms preparation
work and zero DXC misses; timing excludes device initialization. The recording
run still had first-use compilation stalls, so these results do not establish
an FPS improvement. recipe-recording.log/.json preserve that run.

Partial GPU buffer views

With -BatchCompute, compatible read-only shader buffer views can now cover only
part of a retained result, span several retained results, or include unchanged
surrounding guest data. upload_gpu takes a cached base snapshot, copies it into
a fresh frame slice and overlays intersecting retained results with GPU buffer
copies. The original owner remains pending for later CPU publication. Full,
aligned views continue to bind the owner directly without a copy. Cached base
buffers are not modified, preserving earlier draws and in-flight versions.

Overlay eligibility requires a contiguous guest mapping, bounded size and
four-byte copy alignment. Unaligned/unmapped views and CPU metadata reads retain
the synchronous fallback. GPU barriers order compute writes, transfer reads and
subsequent shader reads. Upload/cache/readback buffers carry the appropriate
transfer usage flags. -NoBufferOverlays disables this new path for comparisons;
the launcher restores SSBU_VK_BUFFER_OVERLAYS after exit. [native overlays]
reports view count and GPU-copied bytes; these copies are not zero-copy reads.

The full native CPU/GPU suite passed with a test that inserts an image clear,
constructs a larger buffer around aliased dependent results, preserves unchanged
prefix/suffix words, renders 16 expected pixels from the assembled buffer and
publishes original results correctly, all in one submission. Alignment/bounds
fallbacks are checked as well.

gpu-overlay-run.log/.json preserve the bounded game run: 32,010 assembled views,
741 MiB copied on GPU, 10,396 deferred dispatches, 489 publications and zero
incompatible-range fallbacks at its last status checkpoint. Remaining recorded
publications were CPU visibility callbacks and presentation. The run still had
35 shader-cache misses and severe first-use stalls; this is not proof of an
end-to-end FPS improvement. Preparation afterward rebuilt all 387 known pipeline
recipes with no skips or DXC calls (7.2 s preparation work). Future unseen effects
still require compilation. No battle capture at frame 6,000 was reached before
the run deadline; do not treat an older screenshot as that run's visual result.

Live controller test endpoint

The experimental native build compiles its own hle_hid.c object. SSBU_INPUT_FILE
optionally names a local file containing "sequence BUTTON hold_seconds"; updates
are throttled, synchronized across HID readers and restricted to known controller
buttons/stick directions with holds of 0.01-5 seconds. Ordinary controls are
unchanged when the variable is unset. Measure-NativeVulkanTitle.ps1 -LiveInput
creates the endpoint, and Send-NativeVulkanInput.ps1 queues one test-controller
action. This uses the app's HID test path, not Windows keyboard injection. It
allows scene-observed navigation without relying on short key taps or blind
wall-clock scripts. The normal play launcher does not enable the endpoint.

Sparse constant-buffer snapshots, 2026-10-28

Aggregate size/eligibility counters identified the main constant-copy volume:
16-64 KiB ranges with partially unmapped GPU virtual pages bypassed the previous
linear cache. The existing bridge reads holes as zero. A zero-only shortcut
was tested but had zero hits in the measured game scene; it is not the cause
of the observed copy reduction.

BufferCache::get_sparse caches <=64 KiB snapshots containing mapped fragments
and zero holes. Every use rechecks mappings and exactly compares mapped bytes;
it does not issue per-fragment Windows write-watch queries. Changes create
separate GPU versions while earlier submissions may still use the old data.
Guest invalidation checks each interior mapped fragment. GPU unmapping checks
the original virtual range. The shadow budget remains 128 MiB and the GPU slab
budget remains 512 MiB; unsupported/full cases retain the original read path.

Regression fixtures cover watched and unwatched guest memory, identical writes,
real changes, immutable GPU values, zero holes, added/removed mappings, interior
guest invalidation, linear/sparse transitions and unaligned range boundaries.
A measured earlier version reduced the large-buffer category from ~111 GiB
requested to 9 MiB copied, but did not establish an FPS improvement. Read the
latest AI-HANDOFF-2026-10-28.txt continuation for subsequent measurements.

-NoSparseConstants in Test-NativeVulkanGame.ps1 or Measure-NativeVulkanTitle.ps1
sets SSBU_VK_SPARSE_CONSTANTS=0 for comparison. -NoUnmappedZero separately
sets SSBU_VK_ZERO_UNMAPPED=0. These launchers restore process environment.

Measure-NativeVulkanTitle.ps1 also accepts -CaptureSeconds 125 -CaptureFrame 0
for an elapsed-time scene capture (seconds after first successful presentation).
This avoids missing a capture when low FPS prevents a frame-number target.
The JSON records test settings. sceneConfirmed remains false automatically;
a screenshot must be inspected before claiming that an interval was battle.

Sparse-cache default and controlled comparison update, 2026-10-28

Sparse caching is currently OPT-IN because a match FPS benefit was not proven.
Use -SparseConstants to enable it in the play/measurement launcher; default
play retains the previous linear-cache path. -NoSparseConstants overrides it.
-CompareSparseSeconds 20 in the measurement launcher alternates cache modes
within the same ongoing match after 1000 compute dispatches. The summarizer
Summarize-NativeSparseComparison.ps1 excludes the first two phases and phases
with >100 ms compiler/pipeline work; comparisonComplete=false means there were
not enough clean phases in both modes. Do not infer success from copy bytes.

Native-only CPU_ACCESS sites in gpu.c now log their original function/line,
so CPU visibility waits distinguish release, syncpoint, DMA, inline and constant
writes. Actual publication conditions/order are unchanged and regression-tested.

Native NVN fence polling experiment, 2026-10-28

native_fence.h/.cpp provides an opt-in bounded host wait for the guest NVN
semaphore-poll loop. -FenceSleep in the play/measurement launcher enables
SSBU_VK_FENCE_SLEEP=1. Default remains OFF. A wait is at most the requested
1 ms host timeout and only attempted when the remaining guest budget is at
least 1000 us; actual OS scheduling can return later. The original SDK word,
clock, timeout, result and periodic flush logic remains. A separate originally
unbounded post-kernel-success loop also uses the same bounded pause. Every
return goes back to original guest checks; no readiness is fabricated.

Build-NativeVulkanGame.ps1 creates build/native-vulkan/sdk_fence_wait.c from the
existing generated SDK g0026.c only when f_7109427010 and both expected polling
sites match their adjacent instructions. It substitutes native_sdk_fence.obj
for ONLY build/sdk/g0026.obj in the native link. The original generated source
and compiled SDK object are unchanged. Unsupported layouts retain the original
object. fence-wait-hook.json records availability/sites/EXE timestamp; the
launchers reject -FenceSleep without matching metadata. Do not hand-edit the
generated native copy; edit the guarded builder hook and native helper.

The supported sites are L_42711c and L_427230. The earlier L_427168 experiment
was never exercised and has been superseded. WakeByAddressAll is issued ONLY
after semaphore release stores its payload, not for arbitrary GPU word writes.
The 1 ms timeout fallback handles aliases or writers without a notification.
The native suite tests disabled/satisfied/wrapped/short timeout behavior,
spurious notifications, actual signaling and writers that do not notify.
Publication-order tests still pass. No GPU completion fence is skipped.

Verified active run: fence-sleep-active-run.log/.json, profile_20261028_141029.txt.
Main-thread sampled busy15.6% versus67.9% in an earlier different capture;
99.1% of its waiting-stack samples lead through pause_fence. 82570 waits,
0 API failures, ~115972 ms total paused over210s. This is evidence of reduced
CPU spinning, not a rigorous matched-scene FPS gain. The run hit40 new shader
compiles (~20.7s DXC, ~26.1s graphics pipeline creation), so stable60FPS is NOT
established. Native_20261028_141018_133.bmp is a visually inspected battle frame.

Profiler correction: debug_profile.c now sorts public/private map symbols
across LLD sections and ignores millions of SEH labels/filter aliases. Unnamed
unwind ranges do not inherit a different preceding function. It also collects
GPU waiting stacks. The prior hot gfx_warm_shaders_main label was false nearest-
public attribution. Build now recompiles native_debug_profile.obj separately.
The real parser has a synthetic private-symbol/unwind regression fixture.
System-library samples classified as waiting are not scheduler-state proof;
use separate GPU timestamp, compilation and publication counters as well.

Microsoft API references:
https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress
https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakebyaddressall

Experimental play:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ".\diagnostics\Test-NativeVulkanGame.ps1" -BatchCompute -FenceSleep

See AI-HANDOFF-2026-10-28.txt for current evidence, retained logs, failed
experiments and remaining performance work. No user-side screenshot test is
needed to reproduce the automated measurements described there.

2026-10-28: sampled draw timing and attachment dependency experiment

Draw timestamps are opt-in: -DrawTimingsEvery 17 on the game/measurement
launcher, with -DrawProfileAfterDispatches 1000 (default). Groups include the
VS/PS semantic keys including indirect-table variants. Existing query pools
cap/drop samples at256 slots; fence-completed results collect once and reset
on frame reuse. Status snapshots take the renderer lock or defer to present.
Summarize-NativeDrawTimings.ps1 extracts the latest snapshot to compact JSON.
BOTTOM timestamps alter dependencies/overlap: these intervals rank candidates,
not isolated vertex/fragment stages or an undisturbed FPS score. Default OFF.

- AttachmentBarriers (PowerShell spelling: -AttachmentBarriers) enables
SSBU_VK_ATTACHMENT_BARRIERS=1. Device::render still orders attachment writes
before LOAD/blend/depth operations in separate dynamic rendering instances,
but permits unrelated shader work to overlap. Graphics shader buffers in this
bridge are readonly. Explicit compute, buffer-copy and image transitions keep
their full barriers. Nonzero rendering flags/pNext/no attachments fall back to
full dependencies. This is NOT a contract for arbitrary writable graphics
shaders. Default OFF pending measured benefit and broader validation.

Full native suite runs both barrier modes. New GPU fixture requires red/depth
write, rejected green/depth LOAD, blue alpha blend to purple, sampling that
target, then transfer clear/sample to green. It verifies every pixel and the
configured barrier counter. Existing dependent compute/overlay/present tests
also pass both modes.

Measure-NativeVulkanTitle.ps1 -CompareAttachmentSeconds 20 alternates OFF/ON
after1000 compute dispatches in one process/match. Enables whole-submission
timestamps automatically; per-draw profiling and simultaneous sparse A/B are
rejected to avoid mixing experiments. Summarize-NativeAttachmentComparison.ps1
excludes first2 phases and >100ms shader/pipeline compilation phases, requires
at least2 eligible phases/mode. Live scene and thermals remain uncontrolled;
GPU collection crosses phase boundaries. Do not infer a universal FPS gain.
300s game comparison finished: insufficient clean phases (OFF1/ON0), no
performance conclusion. Late clean OFF phase3.87FPS/~212ms GPU per frame;
cold compilation separately consumed~99s cumulative. Keep experiment OFF.
See AI-HANDOFF-2026-10-28.txt and archived comparison artifacts for details.

2026-10-28: optional aligned float vertex fetch
CURRENT STATUS: research option only. A prepared FastVertex game run reported
VK_ERROR_DEVICE_LOST(-4) entering battle; normal play should omit -FastVertex.
Numeric tests passing do not establish game stability or an overall FPS gain.
The unchanged vertex path subsequently completed210s and captured a clear
Jigglypuff/Mario battle. Different fighters prevent causal attribution.

-FastVertex on Test-NativeVulkanGame.ps1 or Measure-NativeVulkanTitle.ps1 sets
SSBU_VK_FAST_VERTEX=1 (default OFF). The native HLSL compiler inserts a direct
Load/Load2/Load3/Load4 path for aligned32-bit float1..4 attributes. Keeps missing
components0/1 and bit31 BGR reversal. Unaligned/packed/half/normalized/integer
and unknown formats retain the reference decoder. Shared VERTEX_FETCH string
moved verbatim to runtime/shader_vertex_fetch.h; no D3D/MSL behavior change.
Source hash includes actual inserted code, so baseline SPIR-V is not reused
for changed vertex shaders. Pixel/compute compilation unaffected by the option.
Existing shader recipes remain valid; unseen fast VS/pipeline combinations
can compile during first play. Runtime status reports vertex_fast_programs.

Full native suite now verifies baseline, attachment barriers, and FastVertex
modes. test_native_vertex_fetch.cpp compiles the EXACT shared decoder reference
and candidate, compares11520 GPU cases bit-for-bit, including all type fields,
recognized/unknown sizes, all byte alignments, BGR, defaults, NaN payloads,
signed zero and descriptor-edge/OOB reads. Actual VS compile-option selection
is asserted; existing graphics/compute/residency/present tests still pass.

Controlled compute helper microbenchmark:1,474,560decodes/dispatch,6 alternating
samples per mode; observed reference2.94..3.43ms versusfast2.18..2.49ms. Earlier
smaller589824decode workload~1.40ms versus~0.97ms. This is a decoder benchmark,
NOT a measured game-FPS increase. Real VS shader complexity, attribute formats,
pixels and synchronization affect the overall result. Keep it opt-in pending
broader game validation and a fixed scene comparison.

Current EXE16:36:08 +08,960325632bytes. See handoff for current game-run status.

FastVertex preparation integration:
Version1 .nvprog files store SPIR-V/bindings, NOT HLSL source. The preparation
tool indexes captured shader-inputs/vs_<cache-id>.hlsl together with the exact
shader-cache/<cache-id>.spv, then matches complete SPIR-V word vectors to the
recipe. Available source is converted to the FastVertex variant before known
pipeline states are prepared. Missing sources retain the original valid recipe
and are counted explicitly. No recipe schema/identity migration or deletion.
prepare_fast_vertex handles raw/annotated source; source/SPIR-V equality with
live compilation and idempotency are regression tested. Both game launchers
forward -FastVertex to preparation and preserve caller environment. Fast
preparation allows600s first migration (ordinary180s); previously unseen states
still compile at first use. Check the final fast-variant count, not merely a
successful process exit. First attempted integration prepared0 variants because
source was not serialized; that incorrect assumption was fixed before claiming
migration success. See handoff for actual integration result.

-CaptureAfterDispatches12000 triggers a one-shot screenshot after battle compute
work reaches that count, independent of cold compilation/menu wall time. Defaults
to0/off. Capture still must be inspected to establish scene identity. Timed
screenshots at150/320s previously hit versus-intro/victory, so do not label their
FPS intervals battle FPS. Current native EXE16:48:26+08,960325632bytes.

Final integration outcome: first corrected optimized migration prepared1317
states,1108recipe-program variants,0missing sources in529.6s; identical warm
repeat after incremental records prepared1359states/1148variants,0DXCmisses
in4.239s. These startup results do NOT establish game stability: prepared
FastVertex run reportedVK_ERROR_DEVICE_LOST(-4) enteringbattle. KeepFastVertex
OFF for normal play. Baseline afterward completed210s with an inspected clear
Jigglypuff/Mario battle, but differentfighters prevent causal comparison and
lag remains. Device-loss logs retained; proper fixedscene capture/validation
or minimal GPUrepro is required before promoting the experimental path.

CPU descriptor resource-table candidate (2026-10-28):
native_descriptor_slots.h replaces per-binding std::map nodes with optional
fixed slot arrays plus presence bits and map overflow for unusual bindings.
New slots are zero initialized individually; unused array storage is not read.
Pointer stability and high-binding fallback are covered by CPU assertions and
an actual Vulkan compute fixture including binding400. Graphics image/sampler
fixture also uses the dense table. Full native suite passed all three modes.
No shader, GPU resource lifetime, memory barrier or coherence change.
-DenseResources opts in; default stays MAP. -ResourceToggle enables F5 MAP/DENSE
switching on the GPU thread for same-process comparisons. Experimental game
candidate lives separately in build/native-vulkan-resource-test; original
build/native-vulkan executable has not been replaced. See AI-HANDOFF for results.

Per-command upload snapshot lookup (2026-10-28):
native_snapshot_cache.h provides optional fixed256-slot open-addressed lookup,
cleared with an epoch at the exact old draw/dispatch boundaries. Saturated
commands fall back to a map; zero keys and repeated updates are supported.
No cross-command guest-content caching, shader change, GPU lifetime change or
synchronization removal. -FlatSnapshots enables it; default MAP. -SnapshotToggle
makes F5 switch the snapshot lookup only, taking precedence over ResourceToggle.
Keep DenseResources constant when measuring it. Full native suite includes
randomized reference-map comparisons, collisions/overflow and clear/mode tests.

Fixed-scene sparse-constants result: same Mario/Mario Battlefield Training1x,
CPUStop, three20s phases each, no material compilation, DenseResources alwaysON:
OFF31.32FPS/4.621ms upload CPU per frame; ON30.33FPS/5.468ms. Keep sparseOFF.
Saved copy volume is not sufficient evidence of a performance improvement.
Logs/results in build/native-vulkan-resource-test/sparse-fixed-*.

Fixed-scene snapshot lookup result (all four20s windows zero compilation):
MAP31.45/31.45FPS; FLAT31.95/33.35FPS. Upload preparation averaged4.718ms/frame
MAP vs3.624ms FLAT (~23% lower); throughput mean~3.8% higher. Overall processCPU
perframe did not improve consistently, so do not claim overall CPU reduction.
Idle animations/day-night/workload vary; no60FPS or active-fighting claim.
Actual command table max19 entries, no fallback inserts in tested run.
Built isolated22:41:22+08 EXE,960328704bytes. Full regressionsPASS.
Keep defaultMAP until broader validation. See snapshot-test JSONs and handoff.

IMPLEMENTATION VERIFIED / BATTLE PERFORMANCE PENDING - 2026-10-31 08:04 +08
First OpenSmash-inspired change implemented: optional native Vulkan hardware
vertex fetch for bounded aligned float32 scalar/vec2/vec3/vec4 attributes. Other
attributes retain raw decoder. This is NOT the old FastVertex optimization.
- Env SSBU_VK_HARDWARE_VERTEX=1; launcher -HardwareVertex. Default OFF.
- Sources: runtime/vulkan/native_vertex_input.h, native_shader.cpp,
  native_pipeline.h/.cpp, native_recipes.cpp, vk_api.inc; runtime/gpu_vulkan.cpp;
  vertex usage bits added to device upload slabs, BufferCache and ReadbackPool.
- Pipeline key now192 words, including32 format/stride/offset triples. Writer
  preserves96-word recipes without native inputs, reader accepts both sizes.
  Hardware recipes separated at build/native-vulkan/pipeline-recipes-hardware
  to avoid feeding new layouts to an older preparation executable.
- Shader variants keyed by original Shader pointer + attribute mask; HLSL
  rewrite removes only selected stream descriptors/functions, preserving
  built-in vertex IDs and mixed fallback. Attribute formats stay pipeline state;
  per-draw BGR channel order retained. Instance/unknown bounds/OOB/unaligned/
  unsupported conversions remain software. No GPU waits/lifetimes removed.
- Full native regression suite PASS in all3 modes, including128 new hardware
  GPU cases per mode: payload bits/NaNs/defaults/BGR, nonzero firstVertex and
  indexed baseVertex, mixed fallback, safety rejection. Existing tests PASS.
  Legacy96-word recipe fixture + new layout roundtrip/preparation PASS.
- Isolated EXE build/native-vulkan-hardware-vertex-test/ssbu-native-vulkan.exe
  960352768 bytes, 2026-10-31 08:02:30 +08. Existing EXEs untouched.
- Original shader cache seeded from snapshot-test by content-hashed .spv files.
- Real game bounded smoke PASS660 frames. Latest status10749 hardware draws /
  15111 converted attributes, 0 skipped draws/0 shader failures, no device loss.
  Screenshot screenshots/native_20261031_080338_916.bmp visually inspected:
  Samus intro geometry/colors intact. This is NOT a battle/FPS benchmark.
- Evidence build/native-vulkan-hardware-vertex-test/suite-final.log,
  verification*.log, recipe-verification.log, boot.stderr.log, boot.stdout.log.
- Need next: same Mario/Mario Battlefield Training1xStop, both0%, candidate ON
  versus OFF, warm shaders first. Candidate first battle may compile new variants.
  Do not enable FastVertex/AttachmentBarriers/SparseConstants for this test.
  No60FPS claim. Synchronization and cross-batch compute retention not ported.
Launch from root:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\diagnostics\Test-NativeVulkanGame.ps1 -OutputDirectory build\native-vulkan-hardware-vertex-test -HardwareVertex -BatchCompute -FenceSleep -DenseResources -FlatSnapshots -GpuTimings -SkipPreparation
For baseline omit only -HardwareVertex, keep other flags and scene identical.
User asked implementation; first port complete, matched battle measurement remains.


2026-10-31 08:48 FINAL CHECKPOINT / GPU RESET BLOCKED UI CAPTURE
Early split prototype remains OFF by default. Do not recommend -SplitDraws256.
Baseline launched under existing RenderDoc via new Capture-NativeVulkan.ps1;
no actual GPU .rdc capture obtained. Windows computer-use capture failed twice:
Direct3D11CaptureFramePool.CreateFreeThreaded: GPU device instance suspended,
GetDeviceRemovedReason required, 0x887A0005. First failure followed prior game
GPU device loss. Refreshed current window via list_windows/get_window and retried
once; identical failure. Stopped UI input per computer-use skill. Terminated only
our own baseline capture game (exact split-test EXE path filter); capture wrapper
will preserve result.json/log with no .rdc. This is a capture-helper limitation,
not evidence that a fresh baseline game necessarily lost its Vulkan device.
RenderDoc capture attempt: build/native-vulkan-split-test/capture-20261031-084525.
Do not treat this as successful capture or claim actual scene/cause of device loss
was pinpointed. Further UI testing needs the capture surface to recover first.
Completed this turn: controlled hardware A/B measurements (no robust large gain),
new optional early-submission implementation + retained compute pool protection,
GPU lifetime regression fixture (all modesPASS), isolated game build, real game
failure recorded. NO FPS fix promoted. Previous hardware/snapshot binaries remain
available. Default runtime flags keep early submissions and hardware vertexOFF.
Next professional step: actual native Vulkan Training capture/validation to audit
cross-submission descriptors/uploads/resources; pool fixture alone did not cover
whole-game lifetime behavior. Do not skip fences or repeatedly rerun GPU-loss path.
Source reference OpenSmash runtime/nvn_vk_draw.c cmd_submit/ring_put/cb_batch_end;
its NVN layer/lifetimes are architecturally different, not a direct function swap.

