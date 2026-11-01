# AI continuation handoff — 2026-11-01

Read this first, then the root `AGENTS.md`. This is the portable continuation
summary. The ignored root `AI-HANDOFF-2026-10-28.txt` is a longer chronological
archive on the original developer laptop; it is not required on another PC.
Update this document after meaningful changes, preserving the distinction between
reported observations, measured evidence and untested hypotheses.

## Current objective

Resolution/performance settings are now part of the normal NVN application,
requested for community use. Esc -> Graphics / performance offers 0.5x (960x540),
0.75x (1440x810), 1x (1920x1080), and 2x/4K supersampling (3840x2160). Saving
resolution is independent of Online settings and applies next launch through
Play WindowSmash NVN.bat. Root-local nvn_graphics.ini is ignored/preserved across
rebuilds and removed only with the uninstaller's explicit personal-data option.
Without a saved choice, scale remains0.5. Developer ResolutionScale overrides
the saved choice for that run. Custom valid scales0.25..2.0 are preserved.

The normal builder compiles fresh runtime/host.c as port_host.obj instead of
linking a stale common host.obj during runtime-only rebuilds. This includes the
f69a2d9 status fix in runtime-only builds. Graphics parsing/launcher precedence,
locale handling, environment restoration and settings-preservation tests passed;
uninstaller tests passed. The first menu candidate compiled/linked and completed
300 startup frames with clean shutdown/zero Vulkan failures. A clean production
build compiled/linked and completed180 startupframes with clean shutdown/zero
Vulkanfailures. The normal executable was updated after a hash-verified rollback
copy of the previous executable/dependencies/metadata was retained locally under
build/nvn-opensmash-port/rollback-before-resolution-<timestamp>. Server settings,
warm cache and saves were not copied over. No in-game menu visual/click/restart
verification yet. No4K
performance qualification. Source tests are not claims of cross-hardware stability.

The many experimental BAT launchers were removed. Unproven constant-upload cache,
CPU scopes, core-affinity comparisons and trace helpers are archived locally under
ignored diagnostics/local-intel-20261102, not part of the production patch.
No experimental GPU timestamps, split submissions or constant-cache feature is
enabled. Existing working executable is retained as a local rollback when updating
the normal application. Never publish executables/caches/game data/settings/logs.

Intel full-resolution follow-up: a clean fixed Training run with the status fix
averaged57.8FPS across10,835battleframes (median16.87ms,p9926.40ms,max36.06ms),
6frames>33.4ms, negligible shader holds and clean exit. CPU affinity AB repeated
normal55.69/fast-only58.88/normal59.20/fast-only59.01FPS: no repeatable benefit;
normal affinity restored. Private memory stayed~8.627GB in that fixed scene;
low available RAM alone did not explain drops or establish a leak/hardware ceiling.
A later plain Intel run suffered VK_ERROR_DEVICE_LOST around46,425frames with
profiling/experimental options off and normal affinity. It was visibly presenting,
shader workers idle and locks free. This is unresolved, not proven laptop fault.
WPR capture was canceled during save; no usable CPU trace exists. Keep first-use
shader hitches, periodic status hitches, sustained variation and device loss distinct.

Continue identifying and reducing **offline jitter** where measurements justify it.
The user wants implementation backed by measurements and explanations, rather
than repeated speculative tests. Preserve the smooth NVN graphics path, the
installer, controller support and WindowSmash networking.

The user is moving the conversation to Codex on that AMD laptop. Work from the
actual checkout root there; do not reuse another machine's absolute paths,
compiled objects, shader caches or log conclusions.

## What the user has established

| Machine | Hardware | User-reported behavior |
|---|---|---|
| Laptop 1, original development PC | Intel Core i5-1240P, Iris Xe, 16 GB RAM | Offline play is smooth. |
| Laptop 2, new continuation PC | AMD Ryzen 7 PRO 8840HS, Radeon 780M, 32 GB RAM | Offline logs have now been inspected: large hitches correlate with first-use shader/pipeline work and fall substantially with a warm cache. Smaller pacing variation remains unattributed. See the measured runs below. |

- Public-server ICMP round trips were roughly 242–321 ms. These measurements
  describe the public host, not the actual direct peer path during a match.
- A local Online server worked: the user successfully matched the two laptops.
- Connecting the laptops directly by Ethernet improved online play substantially,
  but jitter remained. Actual interface/peer-path timing was not captured, so do
  not claim a fully measured wired network baseline merely from cable presence.
- Offline CPU play was smooth on laptop 1 and jittery on laptop 2. The exact
  scene and warm-cache repeatability of that comparison have not been verified.
- A logged `lost the relay ... reconnecting` near the end of the original PC's
  run was **the user intentionally closing the server**. The user explicitly
  confirmed this. It is NOT evidence of the reported jitter.

The measured AMD runs below identify first-use shader/pipeline preparation as
the dominant cause of the large hitches in the tested scene. This does not
attribute every remaining hitch. Do not assume the AMD machine is inherently
too weak, that RAM is the problem, or that networking explains an offline
symptom. Average 60 FPS does not rule out uneven frame timing or simulation stalls.

## First actions on laptop 2

1. Read the existing local logs **before launching again**, because the launcher
   overwrites them:
   - `build/nvn-opensmash-port/play.stdout.log`
   - `build/nvn-opensmash-port/play.stderr.log`
   Save a local copy of relevant evidence under an ignored dated diagnostics
   folder. Never commit raw logs or machine/game data.
2. Check the actual Vulkan adapter and driver in startup output. The launcher
   still has “Intel” in its name, but selects generic NVN/Vulkan, physical device
   index 0 and scale 0.5; its name does not force an Intel driver. Confirm the
   renderer is actually using the Radeon 780M. Collect the AMD driver version
   if the logs do not expose it clearly.
3. Inspect `nvn_perf`, `nvn_vk_perf`, shader/pipeline hold counters, worker/cache
   messages and any errors. Distinguish battle windows from menus, loading and
   victory screens. One laptop's logs cannot establish the other's behavior.
4. If needed, have the user run the same offline Mario vs Mario matchup on normal
   Battlefield, CPU level 3, with the same rules, moving/attacking for a few
   minutes. Repeat the same scene with the existing cache warm. This was the
   suggested comparison; it is not an already certified benchmark.
5. Compare spikes with shader/pipeline work. If warmed offline play still
   jitters, investigate local CPU/GPU frame pacing, scheduling, power/thermal
   behavior and driver-specific costs. Do not change drivers or settings blindly.
6. Only then correlate wired-match peer RTT/variation/packet gaps with frame and
   simulation timing on both machines. `net_status` can report direct vs relayed
   peers and RTT; detailed packet/frame correlation is not implemented yet.
   Avoid enabling an enormous per-packet log as a substitute for bounded telemetry.

No mandatory new capture command has been given. Start with evidence already on
this laptop. Prefer a small, justified instrumentation change over many manually
repeated shader-highlight experiments. Do not delete the warm cache to test a
persistent jitter issue without preserving it and explaining the comparison.

## Working architecture and important source

Repository: `https://github.com/bh109/nvn-test.git`, branch `main`.
The AMD cache-save policy landed in `2cb51e3`; the filename/title cleanup described
below follows it. Inspect Git for the current HEAD rather than using an old
handoff commit as the latest source.

- Game CPU instructions are statically recompiled locally from owned game files.
- Original WindowSmash graphics interpreted Tegra/Maxwell commands and rendered
  through D3D11. An earlier direct Vulkan LLE approach was slow/unstable here.
- Current NVN HLE intercepts the graphics API earlier, using vendored OpenSmash
  NVN/Vulkan source with WindowSmash host/kernel/input/network adapters.
- `runtime/nvn/port_runtime.cpp`: actual frontend/backend initialization and
  presented-frame statistics; `optional_adapter.cpp`: deliberately disabled
  upstream features, including `r1_on()` returning 0.
- `third_party/opensmash-nvn/runtime/`: pinned original handwritten graphics source.
  `tools/Stage-NvnGraphics.ps1` applies the reviewed adaptations. Its
  hash gate rejects casual vendor edits; preserve provenance and update changes
  deliberately. Do not bypass it to make a build pass.
- `runtime/nvn/online_menu.cpp`: native Win32 Esc Online panel, separate from the
  disabled upstream GPU overlay menu. Public/Custom selector, player name, direct
  connections, status and Start server on this PC. Settings apply next game start.
- `runtime/gpu_vulkan.cpp`: retained legacy support plus guarded NVN window/input
  integration. Guarding prevents old GPU work from becoming the actual NVN path.
- `runtime/net_relay.c`, `runtime/hle_net.c`, `runtime/hle_online.c`: WindowSmash
  transport and service implementation; game session/netcode remains in use.

The custom bottom progress bar is preparation progress, not a guarantee that all
future shader work is complete. A thick bar can accompany a held loading screen;
a thin bar can accompany background work away from a fight. A first-use shader
or pipeline can still hitch during a battle without displaying the bar. The
launcher disables full-screen startup preparation (`SSBU_VK_PREP=0`,
`SSBU_VK_LOAD_AT_START=0`) and retains loading-screen holds (`SSBU_VK_NOHOLD=0`).

## Build, play and server workflows

- End-user install/rebuild: `Install WindowSmash NVN.bat`.
- The current adapter pins the verified 13.0.5 game images and checks segment
  hashes and callback mappings. Do not disable those checks for another version.
- Play: `Play WindowSmash NVN.bat`.
- Developer runtime-only rebuild, after common objects exist:
  `python tools/build_nvn.py --runtime-only` using the checkout's selected Python.
- Output: `build/nvn-opensmash-port/ssbu-nvn-vulkan.exe`, SDL3 DLL, shader helper,
  settings, warm cache and build/run logs. Preserve working/rollback executables,
  saves and caches while testing; no other checkout is a production dependency.
- Hosting normally starts from Esc. Standalone advanced launcher is
  `launchers/Host WindowSmash NVN Lobby.bat`, moved out of root deliberately.
  `tools/Start-NvnLobby.ps1` runs `tools/ssbu_online_lobby.py` on TCP+UDP 39421.
  Host game uses `127.0.0.1:39421`; other machines use the host's reachable LAN
  address. Both must save and restart after changing servers. Separate server
  means separate players/ratings; it does not merge the public lobby population.
- Local server accounts/ratings are under ignored `tools/nex/state/`. No firewall
  or router settings are changed automatically. See `docs/ONLINE-NVN.md`.
- Uninstaller recycles project build/deps/cache, with opt-ins for prepared data
  and saves/settings. It is folder-based, not an exact installer-ownership
  manifest. It retains shared system prerequisites, source and server state.
  Do not use it for this investigation: it would discard useful local evidence.

## Recent fixes and evidence

- AMD full-resolution follow-up on 2026-11-02 used the laptop's warm cache,
  1920x1080 internal rendering, and the controlled Mario-versus-Mario scene.
  With experimental options and profiling off, 12,616 battle frames averaged
  59.9 FPS: median present spacing 16.64 ms, p99 22.33 ms, maximum 53.30 ms,
  with 15 frames over 33.4 ms. There was no fight-time stage compilation or
  waiting. A repeat with only Vulkan timestamps enabled measured GPU time at
  6.07 ms median, 8.26 ms p99 and 8.65 ms maximum, ruling out raw GPU saturation
  for this scene; presentation pacing remained similar.
- A new opt-in, capped `SSBU_VK_GAPLOG=<ms>` staging adaptation attributed long
  battle gaps without enabling broad profiling. A warm full-resolution run
  measured 8,560 battle frames at 60.0 FPS, GPU p99 7.27 ms, and only two gaps
  over 33.4 ms. Both were periodic and almost entirely before Vulkan work:
  frame 9279 was 33.59 ms (32.63 ms before the 0.96 ms present job), and frame
  9879 was 34.54 ms (33.57 ms before the 0.97 ms present job). They were exactly
  600 frames apart and immediately followed the runtime's 10-second `[status]`
  report and guest-thread dump.
- `print_status()` writes the guest-thread dump while holding the kernel lock.
  `SSBU_STATUS_THREADS=0` now suppresses only that expensive periodic detail,
  retaining the lightweight status counters; the NVN production launcher sets
  it. Crash/freeze diagnostics use separate paths and remain intact. A subsequent
  full-resolution run covered 18,729 battle frames and 37 ten-second reports:
  all 37 retained the lightweight fiber/status line, none emitted guest-thread
  detail, and the repeatable 600-frame gaps disappeared. That run did contain
  one unrelated 391.03 ms pre-present hitch while warming 12 previously unseen
  stage-library cache entries (including a 177 ms fight-thread compilation);
  the expanded warm cache was saved cleanly at exit. The periodic status hitch
  is therefore fixed, but first-seen content can still cause cache-warming
  hitches and should not be described as general jitter being eliminated.
- Naming cleanup: the GPU-neutral launcher is `Play WindowSmash NVN.bat`.
  Production launch/staging scripts are now `tools/Run-NvnGame.ps1` and
  `tools/Stage-NvnGraphics.ps1`. Developer diagnostics are
  `diagnostics/Rebuild-NvnRuntime.ps1` and
  `diagnostics/Test-NvnVulkanValidation.ps1`. Build/cache folder names, executable
  filename and existing persistent data remain unchanged. The NVN window title
  and title-note callback use `WindowSmash NVN Vulkan`; legacy LLE diagnostics
  keep their separate names. Upstream source attribution is retained.
- Installer upgrades only this checkout's owned legacy desktop shortcut, and the
  uninstaller recognizes both launcher names. Another checkout's shortcut is
  never retargeted. Shortcut ownership/dry-run/idempotence tests passed, as did
  renamed-launcher routing, PowerShell parsing, installer dry-run and existing
  installer/uninstaller tests. A full runtime candidate was built in a separate
  ignored `build/nvn-naming-check/` folder; the protected working executable,
  configuration, saves and caches were not replaced. No title screenshot or live
  game test was performed for this naming change. Rerunning the installer applies
  the compiled title change and refreshes the owned desktop shortcut.

- AMD laptop offline evidence captured on 2026-11-01, before and after one
  warm-cache repeat of the comparable Mario-versus-Mario Battlefield Training
  scene. Raw logs were copied only to ignored dated `diagnostics/local-amd-*`
  folders; do not publish them. Both runs selected the Radeon 780M through
  Vulkan 1.4.325, driver token `0x0080016c`, and shut down with zero Vulkan call
  failures. This confirms the launcher name did not select an Intel adapter.
- The first preserved run measured 12,228 battle frames at 58.5 FPS: present
  spacing median 16.60 ms, p99 21.44 ms, maximum 808.97 ms, with 41 frames over
  33.4 ms. Fourteen stage libraries compiled on the render thread in fight
  frames, costing 2,798.1 ms. In all, draw-time holds cost 4,371 ms; the longest
  was 285.3 ms. The run added 187 stage-library cache misses and saved a larger
  AMD pipeline cache. This is direct evidence that incomplete first-use cache
  coverage caused severe offline hitches in that run.
- The immediate warm-cache repeat measured 15,688 battle frames at 59.9 FPS:
  present spacing median 16.63 ms, p99 20.26 ms, maximum 309.83 ms, with only one
  frame over 33.4 ms. Render-thread fight compilation fell to two libraries and
  146.5 ms; draw-time holds fell to 294 ms total. The two new stage-library
  cache misses coincide with the late run's only major hitch window. The cache
  was not rewritten because only two creations had been added, below the
  current save threshold of eight.
- The staging adaptation now changes that exit policy to skip rewriting only
  when zero new driver-cache entries were created. On this laptop, serializing
  and writing the roughly 92 MB cache in the first run took about 362 ms total
  on the exit path; repeating a 146.5 ms in-fight miss on every launch is a
  worse trade. The pinned vendor file and its manifest hash remain unchanged;
  the policy change is recorded by `Stage-NvnGraphics.ps1` like the other
  local adaptations. Rebuild and perform one more warm-cache repeat to verify
  that the two late entries are saved and absent on the following launch.
  Runtime-only staging, contract/hash validation, compilation and linking all
  passed on the AMD laptop after this change. The verification run measured
  12,606 battle frames at 60.0 FPS: present spacing median 16.64 ms, p99
  20.02 ms, maximum 37.05 ms, with two frames over 33.4 ms. No stage library
  compiled or waited on the render thread in a fight, and first-drawn pipelines
  cost 0.00 ms there. The run saved two new cache entries at exit; cache
  serialization plus writing took about 207 ms, with no frame gap caused by the
  exit save. This confirms the large reported offline hitches were cache-warming
  effects on the newly built AMD installation for this tested scene. It does
  not prove every stage/effect is warmed or that all smaller pacing variation
  has been eliminated.
- Therefore sustained GPU overload is not supported by these logs, while cache
  coverage is the dominant measured cause of the large hitches. Do not claim
  all AMD jitter is fixed: the warm run still had 214 battle frames over 20 ms,
  no GPU timestamp data was enabled, and the smaller residual pacing variation
  has not yet been attributed. Also note the first run opened a 1920x1080
  swapchain and the warm repeat opened 1280x720, so the comparison is strongest
  for compile/hold correlation rather than resolution-sensitive GPU cost.
- `dca3c19`: fixed sibling imports under embeddable Python's isolated `._pth`
  configuration. Both `build_nvn.py` and `nvn_contract.py` add their own tools
  directory to `sys.path`; removed an unused `import build_game`. Two regression
  tests reproduced failures before the fix under `-I`, then passed. Keep them.
- `bd62cf6`: native Esc Online controls and local Online server launcher.
- `262f17d`: Public/Custom selector, removed redundant Use this PC preset, moved
  hosting BAT to `launchers/`, corrected relative paths and documentation.
- Source-only installer tests: 7 passed. Network/native settings tests: 2 passed,
  including a real loopback TCP connection and SN protocol UDP probe against the
  bundled Online server. Uninstaller tests: 5 passed previously.
- Exact latest functional source built/linked on laptop 1. Relocated host BAT
  dry-run from unrelated CWD passed. Earlier 180-frame offline startup smoke had
  zero Vulkan failures. These are not AMD battle qualification or a wired
  two-PC jitter diagnosis. The user subsequently reported the controls work.
- Laptop 2 now runs the game after its installer issue; a complete audited cold
  Windows installation/performance qualification is still distinct from that
  user report. No fresh game extraction or full CPU regeneration is needed for
  every runtime change.
- Validation-enabled shutdown timeout remains unresolved. Earlier validated
  battle drawing had no reported core/sync errors after the overlay ordering
  fix, but this does not prove teardown is fixed. Normal non-validation runs
  have often exited cleanly. Do not add global waits as an unmeasured fix.

## Rollback research: existing work, not an enabled feature here

The user asked about rollback. Primary community sources checked on 2026-11-01:

- OpenSmash documentation:
  `https://github.com/OpenSmash-Ultimate/ssbu-recomp/blob/main/docs/ONLINE.md`
  documents experimental two-player rollback. It reports controlled simulated
  150 ms/loss tests, limited real-world testing, and possible hitches during
  replays of three or more frames. These are upstream claims, not our validation.
- `https://github.com/SSBUArena/ssbu-arena` advertises live relay-based rollback
  derived from OpenSmash's snapshot system. Reliability has not been independently
  verified in our work.

Evaluate existing source before proposing rollback from scratch. Our graphics
port did NOT bring over rollback; an environment toggle cannot enable it.
Integration must account for deterministic simulation, guest/kernel state,
snapshot/restore, input ownership, replay costs and audio/render/network side
effects. Do not assume the different lobby protocols or WindowSmash public lobby
will accept a new rollback session unchanged. Rollback may hide late remote
inputs; it does not cure offline rendering stalls and adds replay workload.

## Working rules for the next agent

- Explain findings in plain language and distinguish evidence from hypotheses.
- User prefers double-click tools; avoid making routine use depend on pasted
  environment-variable commands. Launch the app and let the user navigate when
  a controlled gameplay scene is needed.
- Avoid repeated one-shader/manual screenshot hunts; the old color corruption
  investigation is historical and the current report is performance jitter.
- Preserve local `online_server.txt` customization. It is tracked upstream but
  must not be staged accidentally with private/custom settings.
- Only publish source/tools/tests/docs and required licenses. Never commit game
  files, translated game C, signature bytes generated from game images, keys,
  firmware, executables, caches, saves, raw logs or server account state.
- Keep `AI-HANDOFF-2026-10-28.txt` local if available; update this portable summary
  with sanitized findings when continuing across machines. No unique device IDs,
  private IPs, credentials or machine-specific input paths belong here.
