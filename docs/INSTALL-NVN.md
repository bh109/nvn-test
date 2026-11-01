# Install and play: NVN Vulkan variant

Double-click **Install WindowSmash NVN.bat** in the project folder. It uses the
existing setup window to select your game files, keys, firmware and online name,
then builds the NVN Vulkan executable. You do not need an OpenSmash checkout,
an earlier experimental executable, a Codex installation or precompiled game objects.

| Step | What happens |
|---|---|
| 1 | Check Windows x64, AVX2, RAM and free disk space. |
| 2 | Find or install Python, the compiler/Windows SDK and SDL3. Microsoft's toolchain license is presented by the existing installer. |
| 3 | Choose your own game files, keys and firmware in the setup window. The adapter currently supports the verified **13.0.5** game images only. |
| 4 | Prepare game data and recompile the game's CPU code. The first build can take hours; unchanged game objects are reused on subsequent builds. |
| 5 | Verify the SDK/main segment hashes and NVN callback addresses, then build the bundled graphics source. |
| 6 | Download SHA256-pinned Khronos glslang libraries and build the shader compiler helper. |
| 7 | Link the NVN executable and place SDL3 and the compiler helper beside it. Publish only after linking succeeds. |
| 8 | Start **Play Intel NVN Vulkan.bat**, or use the optional **Play WindowSmash NVN** desktop shortcut. |

The helper uses the Microsoft Visual C++ x64 runtime. If it is missing, setup
downloads the [Microsoft redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe),
checks its Microsoft signature, and opens its setup window for you to review and
complete. If it requests a restart, restart and rerun this installer. The compiler import libraries are downloaded from
the exact Microsoft packages recorded by the portable toolchain, with hash checks.

Setup does not upstream-update this variant. It leaves saves, online settings,
the NVN shader cache and the original installer source in place. It does not
build or prepare DirectX shaders. The first start with a cold NVN cache can take
longer; subsequent starts reuse the cache. Keep the project folder in place.

## Logs and retries

For lobby selection, hosting and connection status, see [NVN networking](ONLINE-NVN.md).
The NVN build has an Esc Online settings window, independent of the disabled
upstream graphics overlay menu.

If setup stops, read **install-nvn.log**, correct the reported problem and run
the same installer again. Runtime compiler/linker logs are under
`build/nvn-opensmash-port/`. A successful rebuild saves the previous executable
as `ssbu-nvn-vulkan-previous.exe`. An existing playable executable is retained
if compilation or linking fails. The launcher depends on
`diagnostics/Test-OpenSmashPort.ps1`; keep it with the project.

From Command Prompt, in the project folder:

```cmd
"Install WindowSmash NVN.bat"
"Play Intel NVN Vulkan.bat"
```

From PowerShell:

```powershell
& '.\Install WindowSmash NVN.bat'
& '.\Play Intel NVN Vulkan.bat'
```

Preview setup without downloading, showing the wizard or building:

```cmd
"Install WindowSmash NVN.bat" -DryRun
```

Installer options include `-Game`, `-Keys`, `-Firmware`, `-OnlineName`, `-Jobs`,
`-NoWizard` and `-SkipBuild`. The last prepares game files without building.
For example, `-Jobs 2` lowers compilation concurrency on a memory-constrained PC.

## Uninstall

Double-click **Uninstall WindowSmash NVN.bat** and review its removal plan.
It moves this project's `build/`, `deps/` and `cache/` to the Windows Recycle Bin
and removes the desktop shortcut only if it points to this installation.
Close the game first. Source files stay available for a later reinstall.

Prepared game data in `dump/` and saves/personal settings are kept by default.
The uninstaller asks separately whether to remove those. It never targets the
original game/keys/firmware folders recorded by setup, and refuses linked paths
or a removal plan overlapping those inputs. Shared system Python and Microsoft
Visual C++ installations are retained. Review the Recycle Bin before emptying it
to reclaim disk space.

Preview without changing anything, from PowerShell:

```powershell
& '.\Uninstall WindowSmash NVN.bat' -DryRun
```

For an unattended uninstall, `-Yes` accepts the displayed plan without prompting;
`-RemovePreparedData` and `-RemoveUserData` explicitly include those data groups.
To remove the optional installer-created firewall rule too, add
`-RemoveFirewallRule` from an administrator terminal. It matches both the rule
name and this installation's executable path; other installations are retained.

## Development

`tools/build_nvn.py` orchestrates preparation, common CPU recompilation and the
final NVN link. `tools/Build-NvnRuntime.ps1` compiles graphics/adapters from source.
`tools/recomp.py all ... --objects-only` creates the common object list without
linking an unwanted DirectX executable. `--runtime-only` on `build_nvn.py` is a
developer shortcut requiring previously prepared, current common game objects.

`third_party/opensmash-nvn/` contains the original handwritten graphics source,
license, per-file hashes and pinned game contract metadata. The staging script
applies this port's reviewed adaptations and records them in a build manifest.
The contract contains hashes and addresses; game signature bytes and generated
game code are created locally from the user's own files, not bundled here.
Vulkan/DXC build headers and their licenses are in `third_party/nvn-build-headers/`.
The glslang shim source is included; its libraries come from the pinned official
[Khronos glslang 16.6.0 release](https://github.com/KhronosGroup/glslang/releases/tag/16.6.0).

This remains a playable experimental variant. Near-60 FPS evidence was collected
at 0.5 resolution scale on this Intel laptop, not guaranteed for every machine.
Battle core/synchronization validation reported no drawing errors after the
overlay fix; validation-enabled shutdown can still time out. Online behavior
uses WindowSmash networking and `online_server.txt`; peer network delay is not
yet measured by this installer.
