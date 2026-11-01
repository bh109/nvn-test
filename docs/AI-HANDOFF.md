# AI continuation handoff — 2026-11-01

Read this first, then the root `AGENTS.md`. This is the portable continuation
summary. The ignored root `AI-HANDOFF-2026-10-28.txt` is a longer chronological
archive on the original developer laptop; it is not required on another PC.
Update this document after meaningful changes, preserving the distinction between
reported observations, measured evidence and untested hypotheses.

## Current objective

Continue on the second, AMD laptop to identify and reduce **offline jitter**.
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
| Laptop 2, new continuation PC | AMD Ryzen 7 PRO 8840HS, Radeon 780M, 32 GB RAM | Some jitter occurs even offline. AMD driver version and relevant logs have not yet been inspected. |

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

Cold shader/pipeline preparation on the newly built laptop is plausible, not
proven. Do not assume the AMD machine is inherently too weak, that RAM is the
problem, that every hitch is compilation, or that networking explains an offline
symptom. Average 60 FPS also does not rule out uneven frame timing or simulation
stalls between otherwise regularly presented frames.

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
Latest functional code at handoff: `262f17d4d5e58938a5de845a6862b6f622c3cff8`.
Later commits may update documentation; inspect Git before assuming this is HEAD.

- Game CPU instructions are statically recompiled locally from owned game files.
- Original WindowSmash graphics interpreted Tegra/Maxwell commands and rendered
  through D3D11. An earlier direct Vulkan LLE approach was slow/unstable here.
- Current NVN HLE intercepts the graphics API earlier, using vendored OpenSmash
  NVN/Vulkan source with WindowSmash host/kernel/input/network adapters.
- `runtime/nvn/port_runtime.cpp`: actual frontend/backend initialization and
  presented-frame statistics; `optional_adapter.cpp`: deliberately disabled
  upstream features, including `r1_on()` returning 0.
- `third_party/opensmash-nvn/runtime/`: pinned original handwritten graphics source.
  `diagnostics/Stage-OpenSmashGraphics.ps1` applies the reviewed adaptations. Its
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
- Play: `Play Intel NVN Vulkan.bat`.
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
