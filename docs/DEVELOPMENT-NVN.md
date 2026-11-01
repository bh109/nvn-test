# Developing the NVN build

Read [AGENTS.md](../AGENTS.md) and the [current handoff](AI-HANDOFF.md) first.
The normal user flow is in the [README](../README.md).

## Source map

| Area | Source |
|---|---|
| Installer / build orchestration | [installer](../tools/install_windowsmash.ps1), [build_nvn.py](../tools/build_nvn.py) |
| Runtime compilation and linking | [Build-NvnRuntime.ps1](../tools/Build-NvnRuntime.ps1) |
| Launch settings, resolution, cache and logs | [Run-NvnGame.ps1](../tools/Run-NvnGame.ps1) |
| Esc settings and resolution validation | [online_menu.cpp](../runtime/nvn/online_menu.cpp), [graphics_settings.h](../runtime/nvn/graphics_settings.h) |
| NVN host/SDK/context adapters and clock | [runtime/nvn](../runtime/nvn/) |
| Pinned ssbu-recomp source and licenses | [vendor source](../third_party/opensmash-nvn/) |
| Reviewed upstream adaptations / hashes | [Stage-NvnGraphics.ps1](../tools/Stage-NvnGraphics.ps1) |
| Host services and status reporting | [host.c](../runtime/host.c), [kernel.c](../runtime/kernel.c) |
| WindowSmash networking | [net_relay.c](../runtime/net_relay.c), [hle_net.c](../runtime/hle_net.c), [hle_online.c](../runtime/hle_online.c) |
| Portable tests | [tests](../tests/) |
| Adapter and contract checks | [diagnostics](../diagnostics/README.md) |

Do not bypass vendored-source hash checks. Apply deliberate adaptations through
the staging script, retaining upstream attribution. Game-image contracts currently
support the verified 13.0.5 images. Retained legacy renderer sources also supply
shared link symbols; removing them requires a dependency refactor and build check.

## Build and test

First run **Install WindowSmash NVN.bat** with your own game inputs. This prepares
the CPU objects and dependencies. For subsequent runtime changes, from the project
folder in PowerShell:

```powershell
py -3 tools/build_nvn.py --runtime-only --output-directory build/nvn-dev
py -3 -m unittest discover -s tests -p test_nvn_graphics_settings.py -v
```

Use the Python selected by your installation if `py -3` is unavailable. The
graphics-settings tests require the project's Windows compiler prerequisites.
Other tests have their own platform/prerequisite requirements; inspect them
before choosing a broader suite.

A separate output folder preserves the working executable. To launch it:

```powershell
& '.\tools\Run-NvnGame.ps1' -Live -BuildDirectory build\nvn-dev
```

Use its own cache for isolation, or explicitly select an existing cache for a
warm-cache comparison. Never transfer driver caches between GPUs. Runtime-only
builds compile fresh host code, including status-report fixes.

## Compare performance fairly

Use Mario versus Mario, normal Battlefield Training, speed 1×, CPU Stop, both at
0%, menu closed. Keep the window visible. Record resolution, cache state,
adapter/driver, frame ranges and actual presentation counts.

Change one variable and repeat the comparison. Menus/victory screens and frames
simulated while minimized are not battle benchmarks. The final battle summary
currently records only frame indices below 40,000; use window reports for longer
sessions. Timestamp profiling adds overhead and needs its own stability check.

The handoff separates shader stalls, status-report stalls, remaining slowdowns
and device loss. Reported 4K success on the AMD laptop does not explain the Intel
failures or guarantee other hardware. Rollback is not integrated in this variant.

## Preserve evidence and credits

Keep working builds, saves, settings and warm caches during experiments. Do not
commit game dumps, generated game C, keys, firmware, executables, logs, captures,
caches or personal settings. Follow [PUBLISHING-NVN.md](PUBLISHING-NVN.md).
Keep raw evidence in ignored dated folders and add sanitized findings to the
[handoff](AI-HANDOFF.md). Historical experiments remain in Git history.
